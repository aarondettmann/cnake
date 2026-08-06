/**
 * @file ui.h
 * @brief Cnake - Presentation.
 */

#ifndef CNAKE_UI_H
#define CNAKE_UI_H

#include <stdbool.h>

#include "game.h"

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
const char *ui_error_label(enum ui_mode mode, bool monochrome);
void ui_render_board_size_prompt(const struct ui_state *ui);
void ui_print_board_size_error(const struct ui_state *ui, bool out_of_range);
void ui_begin_frame(void);
void ui_print_header(unsigned int seed, int board_size,
                     const struct ui_state *ui);
void ui_draw_game_board(const struct game_state *game,
                        const struct ui_state *ui);
void ui_render_command_prompt(const struct ui_state *ui, const char *feedback,
                              bool invalid, bool replace);
void ui_render_replay_prompt(const struct ui_state *ui,
                             enum game_status status, bool invalid,
                             bool replace);
void ui_render_help(const struct ui_state *ui);
void ui_render_help_prompt(const struct ui_state *ui);
const char *ui_toggle_debug(struct ui_state *ui);
const char *ui_grow_feedback(const struct ui_state *ui);
const char *ui_answer_feedback(const struct ui_state *ui);
void ui_print_interrupted_exit(const struct ui_state *ui);

#endif
