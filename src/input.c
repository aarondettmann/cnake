#include "input.h"

enum player_command input_parse_player_command(char input) {
  switch (input) {
  case 'k':
  case 'w':
    return PLAYER_COMMAND_MOVE_UP;
  case 'j':
  case 's':
    return PLAYER_COMMAND_MOVE_DOWN;
  case 'h':
  case 'a':
    return PLAYER_COMMAND_MOVE_LEFT;
  case 'l':
  case 'd':
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

/* Final byte of a cursor key sequence: ESC [ A or ESC O A for up, and so on. */
enum player_command input_parse_escape_command(char final_byte) {
  switch (final_byte) {
  case 'A':
    return PLAYER_COMMAND_MOVE_UP;
  case 'B':
    return PLAYER_COMMAND_MOVE_DOWN;
  case 'D':
    return PLAYER_COMMAND_MOVE_LEFT;
  case 'C':
    return PLAYER_COMMAND_MOVE_RIGHT;
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
