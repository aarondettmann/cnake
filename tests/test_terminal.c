#define _XOPEN_SOURCE 600

#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "terminal.h"
#include "test.h"

static bool redirect_stdin_from_text(const char *input, int *saved_stdin) {
  int input_pipe[2];
  if (pipe(input_pipe) == -1)
    return false;

  size_t input_length = strlen(input);
  if (write(input_pipe[1], input, input_length) != (ssize_t)input_length) {
    close(input_pipe[0]);
    close(input_pipe[1]);
    return false;
  }
  close(input_pipe[1]);

  *saved_stdin = dup(STDIN_FILENO);
  bool redirected =
      *saved_stdin != -1 && dup2(input_pipe[0], STDIN_FILENO) != -1;
  close(input_pipe[0]);
  if (!redirected && *saved_stdin != -1)
    close(*saved_stdin);
  return redirected;
}

static void restore_stdin(int saved_stdin) {
  CHECK(dup2(saved_stdin, STDIN_FILENO) != -1);
  close(saved_stdin);
}

static bool open_terminal_pair(int *master, int *slave) {
  *master = posix_openpt(O_RDWR | O_NOCTTY);
  if (*master == -1)
    return false;

  if (grantpt(*master) == -1 || unlockpt(*master) == -1) {
    close(*master);
    return false;
  }

  char *slave_name = ptsname(*master);
  if (slave_name == NULL) {
    close(*master);
    return false;
  }

  *slave = open(slave_name, O_RDWR | O_NOCTTY);
  if (*slave == -1) {
    close(*master);
    return false;
  }

  return true;
}

static void test_state_initialization(void) {
  volatile sig_atomic_t interrupted = SIGINT;
  struct terminal_state terminal;

  terminal_initialize_state(&terminal, &interrupted);

  CHECK(terminal.interrupted == &interrupted);
  CHECK(!terminal.initialized);
  CHECK(!terminal.screen_active);
}

static void test_screen_lifecycle_without_input_mode(void) {
  const char *enter_screen = "\033[?1049h\033[?25l";
  const char *leave_screen = "\033[?25h\033[?1049l";
  int master = -1;
  int slave = -1;
  CHECK(open_terminal_pair(&master, &slave));
  if (master == -1 || slave == -1)
    return;

  int saved_stdout = dup(STDOUT_FILENO);
  CHECK(saved_stdout != -1);
  fflush(stdout);
  CHECK(dup2(slave, STDOUT_FILENO) != -1);

  struct terminal_state terminal;
  terminal_initialize_state(&terminal, NULL);
  CHECK(terminal_enter_screen(&terminal));
  CHECK(!terminal.initialized);
  CHECK(terminal.screen_active);

  char screen_output[32] = {0};
  ssize_t bytes_read = read(master, screen_output, strlen(enter_screen));
  CHECK_EQ(bytes_read, (ssize_t)strlen(enter_screen));
  CHECK(memcmp(screen_output, enter_screen, strlen(enter_screen)) == 0);

  CHECK(terminal_restore(&terminal) == TERMINAL_RESTORE_OK);
  CHECK(!terminal.initialized);
  CHECK(!terminal.screen_active);

  memset(screen_output, 0, sizeof(screen_output));
  bytes_read = read(master, screen_output, strlen(leave_screen));
  CHECK_EQ(bytes_read, (ssize_t)strlen(leave_screen));
  CHECK(memcmp(screen_output, leave_screen, strlen(leave_screen)) == 0);

  CHECK(dup2(saved_stdout, STDOUT_FILENO) != -1);
  close(saved_stdout);
  close(slave);
  close(master);
}

static void test_terminal_lifecycle(void) {
  const char *enter_screen = "\033[?1049h\033[?25l";
  const char *leave_screen = "\033[?25h\033[?1049l";
  int master = -1;
  int slave = -1;
  CHECK(open_terminal_pair(&master, &slave));
  if (master == -1 || slave == -1)
    return;

  struct termios original;
  CHECK(tcgetattr(slave, &original) != -1);

  int saved_stdin = dup(STDIN_FILENO);
  int saved_stdout = dup(STDOUT_FILENO);
  CHECK(saved_stdin != -1);
  CHECK(saved_stdout != -1);
  fflush(stdout);
  CHECK(dup2(slave, STDIN_FILENO) != -1);
  CHECK(dup2(slave, STDOUT_FILENO) != -1);

  struct terminal_state terminal;
  terminal_initialize_state(&terminal, NULL);
  CHECK(terminal_enter_screen(&terminal));
  CHECK(!terminal.initialized);
  CHECK(terminal.screen_active);

  char screen_output[32] = {0};
  ssize_t bytes_read = read(master, screen_output, strlen(enter_screen));
  CHECK_EQ(bytes_read, (ssize_t)strlen(enter_screen));
  CHECK(memcmp(screen_output, enter_screen, strlen(enter_screen)) == 0);

  CHECK(terminal_initialize(&terminal));
  CHECK(terminal.initialized);
  CHECK(terminal.screen_active);

  struct termios raw;
  CHECK(tcgetattr(STDIN_FILENO, &raw) != -1);
  CHECK((raw.c_lflag & (ICANON | ECHO)) == 0);
  CHECK_EQ(raw.c_cc[VMIN], 1);
  CHECK_EQ(raw.c_cc[VTIME], 0);

  CHECK(terminal_restore(&terminal) == TERMINAL_RESTORE_OK);
  CHECK(!terminal.initialized);
  CHECK(!terminal.screen_active);

  memset(screen_output, 0, sizeof(screen_output));
  bytes_read = read(master, screen_output, strlen(leave_screen));
  CHECK_EQ(bytes_read, (ssize_t)strlen(leave_screen));
  CHECK(memcmp(screen_output, leave_screen, strlen(leave_screen)) == 0);

  struct termios restored;
  CHECK(tcgetattr(STDIN_FILENO, &restored) != -1);
  CHECK_EQ(restored.c_lflag, original.c_lflag);
  CHECK_EQ(restored.c_cc[VMIN], original.c_cc[VMIN]);
  CHECK_EQ(restored.c_cc[VTIME], original.c_cc[VTIME]);

  (void)terminal_restore(&terminal);
  restore_stdin(saved_stdin);
  CHECK(dup2(saved_stdout, STDOUT_FILENO) != -1);
  close(saved_stdout);
  close(slave);
  close(master);
}

static void test_command_reading(void) {
  int saved_stdin = -1;
  CHECK(redirect_stdin_from_text("\n\r\033[Aq", &saved_stdin));
  if (saved_stdin == -1)
    return;

  struct terminal_state terminal;
  terminal_initialize_state(&terminal, NULL);
  char input;
  CHECK_EQ(terminal_read_command_key(&terminal, &input),
           TERMINAL_READ_ESCAPE);
  CHECK_EQ(terminal_read_command_key(&terminal, &input), TERMINAL_READ_OK);
  CHECK_EQ(input, 'q');
  CHECK_EQ(terminal_read_command_key(&terminal, &input), TERMINAL_READ_EOF);

  restore_stdin(saved_stdin);
}

static void test_help_reading(void) {
  int saved_stdin = -1;
  CHECK(redirect_stdin_from_text("\033[A\n", &saved_stdin));
  if (saved_stdin == -1)
    return;

  struct terminal_state terminal;
  terminal_initialize_state(&terminal, NULL);
  char input;
  CHECK_EQ(terminal_read_key(&terminal, &input), TERMINAL_READ_ESCAPE);
  CHECK_EQ(terminal_read_key(&terminal, &input), TERMINAL_READ_OK);
  CHECK_EQ(input, '\n');

  restore_stdin(saved_stdin);
}

int main(void) {
  test_state_initialization();
  test_screen_lifecycle_without_input_mode();
  test_terminal_lifecycle();
  test_command_reading();
  test_help_reading();

  return test_finish("terminal");
}
