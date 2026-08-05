/**
 * @file input.h
 * @brief Cnake - Semantic input parsing.
 */

#ifndef CNAKE_INPUT_H
#define CNAKE_INPUT_H

#include "terminal.h"

enum player_command {
  PLAYER_COMMAND_INVALID = 0,
  PLAYER_COMMAND_MOVE_UP,
  PLAYER_COMMAND_MOVE_DOWN,
  PLAYER_COMMAND_MOVE_LEFT,
  PLAYER_COMMAND_MOVE_RIGHT,
  PLAYER_COMMAND_HELP,
  PLAYER_COMMAND_TOGGLE_DEBUG,
  PLAYER_COMMAND_GROW,
  PLAYER_COMMAND_QUIT,
};

enum replay_command {
  REPLAY_COMMAND_INVALID = 0,
  REPLAY_COMMAND_RETRY,
  REPLAY_COMMAND_QUIT,
};

enum input_read_result {
  INPUT_READ_ERROR = -1,
  INPUT_READ_EOF = 0,
  INPUT_READ_OK = 1,
  INPUT_READ_ESCAPE = 2,
};

enum input_read_result input_read_command(struct terminal_state *terminal,
                                          char *input);
enum input_read_result input_read_help(struct terminal_state *terminal,
                                       char *input);
enum player_command input_parse_player_command(char input);
enum replay_command input_parse_replay_command(char input);

#endif
