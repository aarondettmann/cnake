#include "terminal.h"

#include <errno.h>
#include <stdio.h>
#include <sys/select.h>
#include <unistd.h>

#define COLOR_YELLOW "\x1B[93m"
#define STYLE_RESET "\033[0m"
#define WARNING_LABEL_TEXT " ! Warning:"

void terminal_initialize_state(struct terminal_state *terminal,
                               volatile sig_atomic_t *interrupted,
                               bool monochrome) {
  *terminal = (struct terminal_state){
      .interrupted = interrupted,
      .monochrome = monochrome,
  };
}

bool terminal_initialize(struct terminal_state *terminal) {
  if (!isatty(STDIN_FILENO))
    return false;

  if (tcgetattr(STDIN_FILENO, &terminal->original) == -1)
    return false;

  struct termios raw = terminal->original;
  raw.c_lflag &= ~(ICANON | ECHO);
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    return false;

  terminal->initialized = true;
  return true;
}

void terminal_restore(struct terminal_state *terminal) {
  if (!terminal->initialized)
    return;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &terminal->original) == -1) {
    const char *style = terminal->monochrome ? "" : COLOR_YELLOW;
    const char *reset = terminal->monochrome ? "" : STYLE_RESET;
    fprintf(stderr, "%s%s%s Unable to restore terminal settings.\n", style,
            WARNING_LABEL_TEXT, reset);
  }

  terminal->initialized = false;
}

static bool terminal_interrupted(const struct terminal_state *terminal) {
  return terminal->interrupted != NULL && *terminal->interrupted != 0;
}

/*
 * Read one byte from standard input. A negative timeout blocks indefinitely.
 * A timeout and end of input have the same result because callers consume both
 * as the absence of another byte.
 */
static enum terminal_read_status
read_stdin_byte(const struct terminal_state *terminal, char *input,
                int timeout_ms) {
  while (true) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(STDIN_FILENO, &read_fds);

    int ready;
    if (timeout_ms < 0) {
      ready = select(STDIN_FILENO + 1, &read_fds, NULL, NULL, NULL);
    } else {
      struct timeval timeout = {
          .tv_sec = timeout_ms / 1000,
          .tv_usec = (timeout_ms % 1000) * 1000,
      };
      ready = select(STDIN_FILENO + 1, &read_fds, NULL, NULL, &timeout);
    }

    if (ready == 0)
      return TERMINAL_READ_EOF;

    if (ready == -1) {
      if (errno == EINTR) {
        if (terminal_interrupted(terminal))
          return TERMINAL_READ_EOF;
        continue;
      }
      return TERMINAL_READ_ERROR;
    }

    ssize_t bytes_read = read(STDIN_FILENO, input, 1);
    if (bytes_read == 1)
      return TERMINAL_READ_OK;
    if (bytes_read == 0)
      return TERMINAL_READ_EOF;

    if (errno == EINTR) {
      if (terminal_interrupted(terminal))
        return TERMINAL_READ_EOF;
      continue;
    }

    return TERMINAL_READ_ERROR;
  }
}

static void discard_escape_sequence(const struct terminal_state *terminal) {
  char input;
  enum terminal_read_status status = read_stdin_byte(terminal, &input, 10);

  if (status != TERMINAL_READ_OK)
    return;

  if (input != '[' && input != 'O') {
    while (read_stdin_byte(terminal, &input, 5) == TERMINAL_READ_OK)
      ;
    return;
  }

  do {
    status = read_stdin_byte(terminal, &input, 5);
  } while (status == TERMINAL_READ_OK && (input < '@' || input > '~'));
}

static enum terminal_read_status
read_key(struct terminal_state *terminal, char *input, bool skip_newlines) {
  enum terminal_read_status status;

  do {
    status = read_stdin_byte(terminal, input, -1);
    if (status != TERMINAL_READ_OK)
      return status;
  } while (skip_newlines && (*input == '\n' || *input == '\r'));

  if (*input == '\033') {
    discard_escape_sequence(terminal);
    return TERMINAL_READ_ESCAPE;
  }

  return TERMINAL_READ_OK;
}

enum terminal_read_status terminal_read_key(struct terminal_state *terminal,
                                            char *input) {
  return read_key(terminal, input, false);
}

enum terminal_read_status
terminal_read_command_key(struct terminal_state *terminal, char *input) {
  return read_key(terminal, input, true);
}
