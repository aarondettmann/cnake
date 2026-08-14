#include "terminal.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#define ENTER_ALTERNATE_SCREEN "\033[?1049h\033[?25l"
#define LEAVE_ALTERNATE_SCREEN "\033[?25h\033[?1049l"

void terminal_initialize_state(struct terminal_state *terminal,
                               volatile sig_atomic_t *interrupted) {
  *terminal = (struct terminal_state){
      .interrupted = interrupted,
  };
}

/*
 * Screen switching bypasses stdio, so pending buffered output has to be pushed
 * out first. Otherwise it would surface on whichever screen happens to be
 * active when stdio flushes next.
 */
static bool write_stdout(const char *text) {
  size_t remaining = strlen(text);

  fflush(stdout);

  while (remaining > 0) {
    ssize_t written = write(STDOUT_FILENO, text, remaining);
    if (written > 0) {
      text += written;
      remaining -= (size_t)written;
      continue;
    }

    if (written == -1 && errno == EINTR)
      continue;

    return false;
  }

  return true;
}

bool terminal_enter_screen(struct terminal_state *terminal) {
  if (terminal->screen_active || !isatty(STDOUT_FILENO))
    return true;

  terminal->screen_active = true;
  if (write_stdout(ENTER_ALTERNATE_SCREEN))
    return true;

  write_stdout(LEAVE_ALTERNATE_SCREEN);
  terminal->screen_active = false;
  return false;
}

bool terminal_enter_raw_mode(struct terminal_state *terminal) {
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

unsigned int terminal_restore(struct terminal_state *terminal) {
  unsigned int failures = TERMINAL_RESTORE_OK;

  if (terminal->screen_active && !write_stdout(LEAVE_ALTERNATE_SCREEN))
    failures |= TERMINAL_RESTORE_SCREEN_FAILED;
  terminal->screen_active = false;

  if (terminal->initialized &&
      tcsetattr(STDIN_FILENO, TCSAFLUSH, &terminal->original) == -1)
    failures |= TERMINAL_RESTORE_SETTINGS_FAILED;
  terminal->initialized = false;

  return failures;
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
