#define _XOPEN_SOURCE 600

#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "app.h"
#include "test.h"

struct app_result {
  int exit_status;
  char *output;
  char *error;
};

static char *read_capture(FILE *capture) {
  if (fseek(capture, 0, SEEK_END) != 0)
    return NULL;

  long size = ftell(capture);
  if (size < 0 || fseek(capture, 0, SEEK_SET) != 0)
    return NULL;

  char *text = malloc((size_t)size + 1);
  if (text == NULL)
    return NULL;

  size_t bytes_read = fread(text, 1, (size_t)size, capture);
  text[bytes_read] = '\0';
  return text;
}

static bool run_app_with_input_fd(const struct cli_options *options,
                                  int input_fd, struct app_result *result) {
  FILE *output_capture = tmpfile();
  FILE *error_capture = tmpfile();
  int saved_stdin = dup(STDIN_FILENO);
  int saved_stdout = dup(STDOUT_FILENO);
  int saved_stderr = dup(STDERR_FILENO);
  if (output_capture == NULL || error_capture == NULL || saved_stdin == -1 ||
      saved_stdout == -1 || saved_stderr == -1) {
    if (output_capture != NULL)
      fclose(output_capture);
    if (error_capture != NULL)
      fclose(error_capture);
    if (saved_stdin != -1)
      close(saved_stdin);
    if (saved_stdout != -1)
      close(saved_stdout);
    if (saved_stderr != -1)
      close(saved_stderr);
    return false;
  }

  struct sigaction previous_sigint;
  if (sigaction(SIGINT, NULL, &previous_sigint) == -1) {
    fclose(output_capture);
    fclose(error_capture);
    close(saved_stdin);
    close(saved_stdout);
    close(saved_stderr);
    return false;
  }

  fflush(stdout);
  fflush(stderr);
  bool redirected =
      dup2(input_fd, STDIN_FILENO) != -1 &&
      dup2(fileno(output_capture), STDOUT_FILENO) != -1 &&
      dup2(fileno(error_capture), STDERR_FILENO) != -1;
  if (!redirected) {
    dup2(saved_stdin, STDIN_FILENO);
    dup2(saved_stdout, STDOUT_FILENO);
    dup2(saved_stderr, STDERR_FILENO);
    fclose(output_capture);
    fclose(error_capture);
    close(saved_stdin);
    close(saved_stdout);
    close(saved_stderr);
    return false;
  }

  clearerr(stdin);
  result->exit_status = app_run(options);
  fflush(stdout);
  fflush(stderr);

  dup2(saved_stdin, STDIN_FILENO);
  dup2(saved_stdout, STDOUT_FILENO);
  dup2(saved_stderr, STDERR_FILENO);
  clearerr(stdin);
  close(saved_stdin);
  close(saved_stdout);
  close(saved_stderr);
  sigaction(SIGINT, &previous_sigint, NULL);

  result->output = read_capture(output_capture);
  result->error = read_capture(error_capture);
  fclose(output_capture);
  fclose(error_capture);
  return result->output != NULL && result->error != NULL;
}

static bool run_app(const struct cli_options *options, const char *input,
                    struct app_result *result) {
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

  bool ran = run_app_with_input_fd(options, input_pipe[0], result);
  close(input_pipe[0]);
  return ran;
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

static void free_result(struct app_result *result) {
  free(result->output);
  free(result->error);
}

static struct cli_options test_options(int board_size) {
  return (struct cli_options){
      .action = CLI_ACTION_RUN,
      .ascii = true,
      .monochrome = true,
      .seed_provided = true,
      .seed = 1729,
      .board_size = board_size,
  };
}

static void test_play_requires_a_terminal(void) {
  struct cli_options options = test_options(6);
  struct app_result result = {0};

  bool ran = run_app(&options, "", &result);
  CHECK(ran);
  if (!ran)
    return;
  CHECK_EQ(result.exit_status, APP_EXIT_TERMINAL_ERROR);
  CHECK(strstr(result.output, "\033[H\033[0J") == NULL);
  CHECK(strstr(result.error, "Unable to enable direct input mode.") != NULL);

  free_result(&result);
}

static void test_quit_restores_terminal(void) {
  int master = -1;
  int slave = -1;
  CHECK(open_terminal_pair(&master, &slave));
  if (master == -1 || slave == -1)
    return;

  struct termios original;
  CHECK(tcgetattr(slave, &original) != -1);

  pid_t writer = fork();
  CHECK(writer != -1);
  if (writer == -1) {
    close(slave);
    close(master);
    return;
  }

  if (writer == 0) {
    for (int attempt = 0; attempt < 5000; attempt++) {
      struct termios current;
      if (tcgetattr(slave, &current) == 0 &&
          (current.c_lflag & (ICANON | ECHO)) == 0) {
        char quit = 'q';
        _exit(write(master, &quit, 1) == 1 ? EXIT_SUCCESS : EXIT_FAILURE);
      }

      struct timespec delay = {
          .tv_sec = 0,
          .tv_nsec = 1000 * 1000,
      };
      nanosleep(&delay, NULL);
    }
    _exit(EXIT_FAILURE);
  }

  struct cli_options options = test_options(4);
  struct app_result result = {0};
  bool ran = run_app_with_input_fd(&options, slave, &result);

  int writer_status;
  CHECK(waitpid(writer, &writer_status, 0) == writer);
  CHECK(WIFEXITED(writer_status));
  CHECK_EQ(WEXITSTATUS(writer_status), EXIT_SUCCESS);
  CHECK(ran);
  if (ran) {
    CHECK_EQ(result.exit_status, APP_EXIT_OK);
    CHECK(strstr(result.output, "Command [?]:") != NULL);
    CHECK(strstr(result.output, "\033[H") != NULL);
    CHECK(strstr(result.output, "\033[0J") != NULL);
    CHECK(strstr(result.output, "\033[H\033[0J") == NULL);
    CHECK(result.error[0] == '\0');
    free_result(&result);
  }

  struct termios restored;
  CHECK(tcgetattr(slave, &restored) != -1);
  CHECK_EQ(restored.c_lflag, original.c_lflag);

  close(slave);
  close(master);
}

static void test_signal_restores_terminal(int signal_number) {
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
  if (saved_stdin == -1 || saved_stdout == -1) {
    if (saved_stdin != -1)
      close(saved_stdin);
    if (saved_stdout != -1)
      close(saved_stdout);
    close(slave);
    close(master);
    return;
  }

  fflush(stdout);
  CHECK(dup2(slave, STDIN_FILENO) != -1);
  CHECK(dup2(slave, STDOUT_FILENO) != -1);

  pid_t signaler = fork();
  CHECK(signaler != -1);
  if (signaler == -1) {
    dup2(saved_stdin, STDIN_FILENO);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdin);
    close(saved_stdout);
    close(slave);
    close(master);
    return;
  }

  if (signaler == 0) {
    for (int attempt = 0; attempt < 5000; attempt++) {
      struct termios current;
      if (tcgetattr(slave, &current) == 0 &&
          (current.c_lflag & (ICANON | ECHO)) == 0) {
        _exit(kill(getppid(), signal_number) == 0 ? EXIT_SUCCESS
                                                  : EXIT_FAILURE);
      }

      struct timespec delay = {
          .tv_sec = 0,
          .tv_nsec = 1000 * 1000,
      };
      nanosleep(&delay, NULL);
    }
    _exit(EXIT_FAILURE);
  }

  struct cli_options options = test_options(4);
  int exit_status = app_run(&options);
  fflush(stdout);

  CHECK(dup2(saved_stdin, STDIN_FILENO) != -1);
  CHECK(dup2(saved_stdout, STDOUT_FILENO) != -1);
  close(saved_stdin);
  close(saved_stdout);

  int signaler_status;
  CHECK(waitpid(signaler, &signaler_status, 0) == signaler);
  CHECK(WIFEXITED(signaler_status));
  CHECK_EQ(WEXITSTATUS(signaler_status), EXIT_SUCCESS);
  CHECK_EQ(exit_status, 128 + signal_number);

  struct termios restored;
  CHECK(tcgetattr(slave, &restored) != -1);
  CHECK_EQ(restored.c_lflag, original.c_lflag);

  close(slave);
  char output[16384] = {0};
  size_t output_size = 0;
  ssize_t bytes_read;
  while (output_size < sizeof(output) - 1 &&
         (bytes_read = read(master, output + output_size,
                            sizeof(output) - output_size - 1)) > 0)
    output_size += (size_t)bytes_read;

  CHECK(output_size > 0);
  if (output_size > 0) {
    output[output_size] = '\0';
    CHECK(strstr(output, enter_screen) != NULL);
    CHECK(strstr(output, leave_screen) != NULL);
  }

  close(master);
}

int main(void) {
  test_play_requires_a_terminal();
  test_quit_restores_terminal();
  test_signal_restores_terminal(SIGINT);
  test_signal_restores_terminal(SIGTERM);
  test_signal_restores_terminal(SIGHUP);
  test_signal_restores_terminal(SIGQUIT);

  return test_finish("application");
}
