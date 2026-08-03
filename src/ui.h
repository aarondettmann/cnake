/**
 * @file ui.h
 * @brief Cnake - Terminal input and presentation.
 */

#ifndef CNAKE_UI_H
#define CNAKE_UI_H

#include <signal.h>
#include <stdbool.h>

#include "game.h"

enum board_size_parse_status {
  BOARD_SIZE_INVALID = 0,
  BOARD_SIZE_OUT_OF_RANGE,
  BOARD_SIZE_VALID,
};

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

enum command_read_status {
  COMMAND_READ_ERROR = -1,
  COMMAND_READ_EOF = 0,
  COMMAND_READ_OK = 1,
  COMMAND_READ_ESCAPE = 2,
};

enum ui_mode {
  UI_MODE_UNICODE = 0,
  UI_MODE_ASCII,
};

struct ui_state {
  bool show_debug;
  bool monochrome;
  enum ui_mode mode;
};

void ui_initialize_state(struct ui_state *ui, enum ui_mode mode,
                         bool monochrome);
void ui_set_interrupted_flag(volatile sig_atomic_t *interrupted);
const char *ui_error_label(enum ui_mode mode, bool monochrome);
bool ui_initialize_terminal(void);
void ui_restore_terminal(void);
enum board_size_parse_status ui_parse_board_size(const char *input,
                                                 int *board_size);
int ui_prompt_for_board_size(const struct ui_state *ui, int *board_size);
void ui_print_header(unsigned int seed, int board_size,
                     const struct ui_state *ui);
void ui_draw_game_board(const struct game_state *game,
                        const struct ui_state *ui);
void ui_print_game_status_message(enum game_status status,
                                  const struct ui_state *ui);
void ui_print_game_summary(const struct game_state *game,
                           const struct ui_state *ui);
void ui_print_help(const struct ui_state *ui);
void ui_print_unknown_command(const struct ui_state *ui);
int ui_prompt_for_command(const struct ui_state *ui,
                          enum player_command *command, const char **feedback);
const char *ui_toggle_debug(struct ui_state *ui);
const char *ui_grow_feedback(const struct ui_state *ui);
void ui_print_interrupted_exit(const struct ui_state *ui);

#endif
