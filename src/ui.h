/**
 * @file ui.h
 * @brief Cnake - Presentation.
 */

#ifndef CNAKE_UI_H
#define CNAKE_UI_H

#include <stdbool.h>
#include <stdio.h>

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

/* One-off notices shown above the command prompt. */
enum ui_feedback {
  UI_FEEDBACK_NONE = 0,
  UI_FEEDBACK_DEBUG_ON,
  UI_FEEDBACK_DEBUG_OFF,
  UI_FEEDBACK_GROW,
  UI_FEEDBACK_ANSWER,
};

bool ui_styling_supported(FILE *stream);
void ui_initialize_state(struct ui_state *ui, enum ui_mode mode,
                         bool monochrome);
const char *ui_error_label(const struct ui_state *ui);
const char *ui_warning_label(const struct ui_state *ui);
void ui_begin_frame(void);
void ui_print_header(unsigned int seed, int board_size,
                     const struct ui_state *ui);
void ui_draw_game_board(const struct game_state *game,
                        const struct ui_state *ui);
void ui_render_command_prompt(const struct ui_state *ui,
                              enum ui_feedback feedback, bool invalid,
                              bool replace);
void ui_render_replay_prompt(const struct ui_state *ui, enum game_status status,
                             bool invalid, bool replace);
void ui_render_help(const struct ui_state *ui);
void ui_render_help_prompt(const struct ui_state *ui);
enum ui_feedback ui_toggle_debug(struct ui_state *ui);
void ui_print_interrupted_exit(const struct ui_state *ui);

#endif
