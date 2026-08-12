#include "input.h"

enum player_command input_parse_player_command(char input) {
  switch (input) {
  case 'k':
    return PLAYER_COMMAND_MOVE_UP;
  case 'j':
    return PLAYER_COMMAND_MOVE_DOWN;
  case 'h':
    return PLAYER_COMMAND_MOVE_LEFT;
  case 'l':
    return PLAYER_COMMAND_MOVE_RIGHT;
  case '?':
    return PLAYER_COMMAND_HELP;
  case 'v':
    return PLAYER_COMMAND_TOGGLE_DEBUG;
  case 'c':
    return PLAYER_COMMAND_GROW;
  case 'q':
    return PLAYER_COMMAND_QUIT;
  default:
    return PLAYER_COMMAND_INVALID;
  }
}

enum replay_command input_parse_replay_command(char input) {
  if (input == 'r')
    return REPLAY_COMMAND_RETRY;
  if (input == 'q')
    return REPLAY_COMMAND_QUIT;
  return REPLAY_COMMAND_INVALID;
}
