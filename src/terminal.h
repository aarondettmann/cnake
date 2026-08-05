/**
 * @file terminal.h
 * @brief Cnake - Terminal lifecycle and byte input.
 */

#ifndef CNAKE_TERMINAL_H
#define CNAKE_TERMINAL_H

#include <signal.h>
#include <stdbool.h>
#include <termios.h>

enum terminal_read_status {
  TERMINAL_READ_ERROR = -1,
  TERMINAL_READ_EOF = 0,
  TERMINAL_READ_OK = 1,
  TERMINAL_READ_ESCAPE = 2,
};

struct terminal_state {
  struct termios original;
  volatile sig_atomic_t *interrupted;
  bool initialized;
  bool monochrome;
};

void terminal_initialize_state(struct terminal_state *terminal,
                               volatile sig_atomic_t *interrupted,
                               bool monochrome);
bool terminal_initialize(struct terminal_state *terminal);
void terminal_restore(struct terminal_state *terminal);
enum terminal_read_status terminal_read_key(struct terminal_state *terminal,
                                            char *input);
enum terminal_read_status
terminal_read_command_key(struct terminal_state *terminal, char *input);

#endif
