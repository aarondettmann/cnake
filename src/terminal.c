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

/* Write the whole text with write(2); safe to call from a signal handler. */
static bool write_all(const char *text) {
  size_t remaining = strlen(text);

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

/*
 * Screen switching bypasses stdio, so pending buffered output has to be pushed
 * out first. Otherwise it would surface on whichever screen happens to be
 * active when stdio flushes next.
 */
static bool write_stdout(const char *text) {
  fflush(stdout);
  return write_all(text);
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

static bool apply_raw_settings(const struct terminal_state *terminal) {
  struct termios raw = terminal->original;
  raw.c_lflag &= ~(tcflag_t)(ICANON | ECHO);
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0;

  return tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != -1;
}

bool terminal_enter_raw_mode(struct terminal_state *terminal) {
  if (!isatty(STDIN_FILENO))
    return false;

  if (tcgetattr(STDIN_FILENO, &terminal->original) == -1)
    return false;

  if (!apply_raw_settings(terminal))
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

/*
 * Suspension hands the terminal back to the shell and takes it again once the
 * process continues. Both functions are called from a signal handler, so they
 * only use async-signal-safe calls and leave stdio buffers untouched; the
 * initialized and screen_active flags stay set so that terminal_restore still
 * knows what to undo.
 */
void terminal_suspend(struct terminal_state *terminal) {
  if (terminal->screen_active)
    write_all(LEAVE_ALTERNATE_SCREEN);
  if (terminal->initialized)
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &terminal->original);
}

void terminal_resume(struct terminal_state *terminal) {
  if (terminal->initialized)
    apply_raw_settings(terminal);
  if (terminal->screen_active)
    write_all(ENTER_ALTERNATE_SCREEN);
  terminal->resumed = 1;
}

static bool terminal_interrupted(const struct terminal_state *terminal) {
  return terminal->interrupted != NULL && *terminal->interrupted != 0;
}

/*
 * Read one byte from standard input. A negative timeout blocks indefinitely.
 * A timeout and end of input have the same result because callers consume both
 * as the absence of another byte. The resumed flag is reported but not cleared
 * here, so a resumption noticed in the middle of an escape sequence is still
 * reported by the next read_key call.
 */
static enum terminal_read_status
read_stdin_byte(const struct terminal_state *terminal, char *input,
                int timeout_ms) {
  while (true) {
    if (terminal->resumed)
      return TERMINAL_READ_RESUMED;

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

/*
 * Consume the rest of an escape sequence that started with ESC. CSI (ESC [) and
 * SS3 (ESC O) sequences end with a byte in the range '@' to '~'; that byte is
 * stored in *input when no parameter or intermediate bytes preceded it, which
 * is the shape of the cursor key sequences. Anything else, including a lone
 * ESC, stores '\0'.
 */
static void read_escape_sequence(const struct terminal_state *terminal,
                                 char *input) {
  char byte;

  *input = '\0';

  if (read_stdin_byte(terminal, &byte, 10) != TERMINAL_READ_OK)
    return;

  if (byte != '[' && byte != 'O') {
    while (read_stdin_byte(terminal, &byte, 5) == TERMINAL_READ_OK)
      ;
    return;
  }

  bool parameters_seen = false;
  while (read_stdin_byte(terminal, &byte, 5) == TERMINAL_READ_OK) {
    if (byte >= '@' && byte <= '~') {
      if (!parameters_seen)
        *input = byte;
      return;
    }
    parameters_seen = true;
  }
}

static enum terminal_read_status read_key(struct terminal_state *terminal,
                                          char *input, bool skip_newlines) {
  enum terminal_read_status status;

  do {
    status = read_stdin_byte(terminal, input, -1);
    if (status == TERMINAL_READ_RESUMED)
      terminal->resumed = 0;
    if (status != TERMINAL_READ_OK)
      return status;
  } while (skip_newlines && (*input == '\n' || *input == '\r'));

  if (*input == '\033') {
    read_escape_sequence(terminal, input);
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
