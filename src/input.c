#include "input.h"

static enum input_read_result
input_result_from_terminal(enum terminal_read_status status) {
  switch (status) {
  case TERMINAL_READ_ERROR:
    return INPUT_READ_ERROR;
  case TERMINAL_READ_EOF:
    return INPUT_READ_EOF;
  case TERMINAL_READ_OK:
    return INPUT_READ_OK;
  case TERMINAL_READ_ESCAPE:
    return INPUT_READ_ESCAPE;
  }

  return INPUT_READ_ERROR;
}

enum input_read_result input_read_command(struct terminal_state *terminal,
                                          char *input) {
  return input_result_from_terminal(terminal_read_command_key(terminal, input));
}

enum input_read_result input_read_help(struct terminal_state *terminal,
                                       char *input) {
  return input_result_from_terminal(terminal_read_key(terminal, input));
}

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
