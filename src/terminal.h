/**
 * @file terminal.h
 * @brief Cnake - Terminal lifecycle and byte input.
 */

#ifndef CNAKE_TERMINAL_H
#define CNAKE_TERMINAL_H

#include <signal.h>
#include <stdbool.h>
#include <termios.h>

/*
 * TERMINAL_READ_ESCAPE reports a consumed escape sequence. For a CSI or SS3
 * sequence without parameter bytes (ESC [ A, ESC O A, ...) the final byte is
 * stored in the output, so callers can recognise cursor keys; any other
 * sequence stores '\0'. TERMINAL_READ_RESUMED reports that the process was
 * suspended and continued while waiting, so the screen needs to be redrawn.
 */
enum terminal_read_status {
  TERMINAL_READ_ERROR = -1,
  TERMINAL_READ_EOF = 0,
  TERMINAL_READ_OK = 1,
  TERMINAL_READ_ESCAPE = 2,
  TERMINAL_READ_RESUMED = 3,
};

/* Flags from terminal_restore, so callers can phrase their own warnings. */
enum terminal_restore_status {
  TERMINAL_RESTORE_OK = 0,
  TERMINAL_RESTORE_SCREEN_FAILED = 1 << 0,
  TERMINAL_RESTORE_SETTINGS_FAILED = 1 << 1,
};

struct terminal_state {
  struct termios original;
  volatile sig_atomic_t *interrupted;
  volatile sig_atomic_t resumed;
  bool initialized;
  bool screen_active;
};

void terminal_initialize_state(struct terminal_state *terminal,
                               volatile sig_atomic_t *interrupted);
bool terminal_enter_screen(struct terminal_state *terminal);
bool terminal_enter_raw_mode(struct terminal_state *terminal);
unsigned int terminal_restore(struct terminal_state *terminal);
void terminal_suspend(struct terminal_state *terminal);
void terminal_resume(struct terminal_state *terminal);
enum terminal_read_status terminal_read_key(struct terminal_state *terminal,
                                            char *input);
enum terminal_read_status
terminal_read_command_key(struct terminal_state *terminal, char *input);

#endif
