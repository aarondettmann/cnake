/**
 * @file input.h
 * @brief Cnake - Semantic input parsing.
 */

#ifndef CNAKE_INPUT_H
#define CNAKE_INPUT_H

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

enum player_command input_parse_player_command(char input);
enum replay_command input_parse_replay_command(char input);

#endif
