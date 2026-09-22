#define _XOPEN_SOURCE 600

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

static void test_state_initialization(void) {
  volatile sig_atomic_t interrupted = SIGINT;
  struct terminal_state terminal;

  terminal_initialize_state(&terminal, &interrupted);

  CHECK(terminal.interrupted == &interrupted);
  CHECK(terminal.resumed == 0);
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

  struct terminal_drain drain;
  CHECK(start_terminal_drain(master, slave, &drain));

  int saved_stdout = dup(STDOUT_FILENO);
  CHECK(saved_stdout != -1);
  fflush(stdout);
  CHECK(dup2(slave, STDOUT_FILENO) != -1);

  struct terminal_state terminal;
  terminal_initialize_state(&terminal, NULL);
  CHECK(terminal_enter_screen(&terminal));
  CHECK(!terminal.initialized);
  CHECK(terminal.screen_active);

  CHECK(terminal_restore(&terminal) == TERMINAL_RESTORE_OK);
  CHECK(!terminal.initialized);
  CHECK(!terminal.screen_active);

  CHECK(dup2(saved_stdout, STDOUT_FILENO) != -1);
  close(saved_stdout);
  char *output = finish_terminal_drain(&drain, slave);
  close(master);
  CHECK(output != NULL);
  if (output != NULL) {
    const char *entered = strstr(output, enter_screen);
    CHECK(entered != NULL);
    CHECK(entered != NULL && strstr(entered, leave_screen) != NULL);
    free(output);
  }
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

  struct terminal_drain drain;
  CHECK(start_terminal_drain(master, slave, &drain));

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

  CHECK(terminal_enter_raw_mode(&terminal));
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

  struct termios restored;
  CHECK(tcgetattr(STDIN_FILENO, &restored) != -1);
  CHECK_EQ(restored.c_lflag, original.c_lflag);
  CHECK_EQ(restored.c_cc[VMIN], original.c_cc[VMIN]);
  CHECK_EQ(restored.c_cc[VTIME], original.c_cc[VTIME]);

  (void)terminal_restore(&terminal);
  restore_stdin(saved_stdin);
  CHECK(dup2(saved_stdout, STDOUT_FILENO) != -1);
  close(saved_stdout);
  char *output = finish_terminal_drain(&drain, slave);
  close(master);
  CHECK(output != NULL);
  if (output != NULL) {
    const char *entered = strstr(output, enter_screen);
    CHECK(entered != NULL);
    CHECK(entered != NULL && strstr(entered, leave_screen) != NULL);
    free(output);
  }
}

static void test_command_reading(void) {
  int saved_stdin = -1;
  CHECK(redirect_stdin_from_text("\n\r\033[Aq", &saved_stdin));
  if (saved_stdin == -1)
    return;

  struct terminal_state terminal;
  terminal_initialize_state(&terminal, NULL);
  char input;
  CHECK_EQ(terminal_read_command_key(&terminal, &input), TERMINAL_READ_ESCAPE);
  CHECK_EQ(input, 'A');
  CHECK_EQ(terminal_read_command_key(&terminal, &input), TERMINAL_READ_OK);
  CHECK_EQ(input, 'q');
  CHECK_EQ(terminal_read_command_key(&terminal, &input), TERMINAL_READ_EOF);

  restore_stdin(saved_stdin);
}

/*
 * Cursor keys arrive as CSI or SS3 sequences and report their final byte.
 * Sequences with parameters and other escape sequences report '\0'. The
 * non-CSI sequence comes last because it drains every byte that follows it.
 */
static void test_escape_sequence_reading(void) {
  int saved_stdin = -1;
  CHECK(redirect_stdin_from_text("\033OB\033[1;5C\033[3~\033x", &saved_stdin));
  if (saved_stdin == -1)
    return;

  struct terminal_state terminal;
  terminal_initialize_state(&terminal, NULL);
  char input;
  CHECK_EQ(terminal_read_key(&terminal, &input), TERMINAL_READ_ESCAPE);
  CHECK_EQ(input, 'B');
  CHECK_EQ(terminal_read_key(&terminal, &input), TERMINAL_READ_ESCAPE);
  CHECK_EQ(input, '\0');
  CHECK_EQ(terminal_read_key(&terminal, &input), TERMINAL_READ_ESCAPE);
  CHECK_EQ(input, '\0');
  CHECK_EQ(terminal_read_key(&terminal, &input), TERMINAL_READ_ESCAPE);
  CHECK_EQ(input, '\0');
  CHECK_EQ(terminal_read_key(&terminal, &input), TERMINAL_READ_EOF);

  restore_stdin(saved_stdin);
}

/* A lone ESC is an escape sequence without a final byte. */
static void test_lone_escape_reading(void) {
  int saved_stdin = -1;
  CHECK(redirect_stdin_from_text("\033", &saved_stdin));
  if (saved_stdin == -1)
    return;

  struct terminal_state terminal;
  terminal_initialize_state(&terminal, NULL);
  char input = 'x';
  CHECK_EQ(terminal_read_key(&terminal, &input), TERMINAL_READ_ESCAPE);
  CHECK_EQ(input, '\0');
  CHECK_EQ(terminal_read_key(&terminal, &input), TERMINAL_READ_EOF);

  restore_stdin(saved_stdin);
}

/* A resumption is reported once, before any further input is consumed. */
static void test_resumed_reading(void) {
  int saved_stdin = -1;
  CHECK(redirect_stdin_from_text("q", &saved_stdin));
  if (saved_stdin == -1)
    return;

  struct terminal_state terminal;
  terminal_initialize_state(&terminal, NULL);
  terminal_resume(&terminal);
  CHECK(terminal.resumed != 0);

  char input;
  CHECK_EQ(terminal_read_key(&terminal, &input), TERMINAL_READ_RESUMED);
  CHECK(terminal.resumed == 0);
  CHECK_EQ(terminal_read_key(&terminal, &input), TERMINAL_READ_OK);
  CHECK_EQ(input, 'q');

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
  test_escape_sequence_reading();
  test_lone_escape_reading();
  test_resumed_reading();
  test_help_reading();

  return test_finish("terminal");
}
