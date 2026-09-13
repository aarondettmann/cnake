#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "game.h"
#include "test.h"
#include "ui.h"

enum capture_surface {
  CAPTURE_BOARD,
  CAPTURE_MESSAGES,
  CAPTURE_PROMPTS,
  CAPTURE_ALL,
};

static bool create_empty_game(struct game_state *game, int board_size) {
  CHECK(game_create(game, board_size));
  if (game->board == NULL)
    return false;

  memset(game->board, 0,
         (size_t)game->board_size * (size_t)game->board_size *
             sizeof(*game->board));
  return true;
}

/* Feedback notices are only reachable through the command prompt. */
static void render_messages(struct ui_state *ui) {
  enum ui_feedback debug_on = ui_toggle_debug(ui);
  enum ui_feedback debug_off = ui_toggle_debug(ui);

  printf("%s\n", ui_error_label(ui));
  printf("%s\n", ui_warning_label(ui));

  ui_render_command_prompt(ui, debug_on, false, false);
  ui_render_command_prompt(ui, debug_off, false, false);
  ui_render_command_prompt(ui, UI_FEEDBACK_GROW, false, false);
  ui_render_command_prompt(ui, UI_FEEDBACK_ANSWER, false, false);

  ui_render_help(ui);
  ui_render_help_prompt(ui);
  printf("\n");
}

static void render_prompts(struct ui_state *ui) {
  ui_render_command_prompt(ui, UI_FEEDBACK_NONE, false, false);
  ui_render_command_prompt(ui, UI_FEEDBACK_NONE, true, true);
  ui_render_command_prompt(ui, UI_FEEDBACK_NONE, true, true);

  const enum game_status statuses[] = {
      GAME_WON,
      GAME_OVER_SNAKE,
      GAME_OVER_OBSTACLE,
      GAME_OVER_POISON,
  };

  for (size_t index = 0; index < sizeof(statuses) / sizeof(*statuses);
       index++) {
    ui_render_replay_prompt(ui, statuses[index], false, false);
    if (statuses[index] == GAME_OVER_POISON) {
      ui_render_replay_prompt(ui, statuses[index], true, true);
      ui_render_replay_prompt(ui, statuses[index], true, true);
    }
    printf("\n");
  }
}

static char *capture_ui_output(struct game_state *game, struct ui_state *ui,
                               enum capture_surface surface) {
  FILE *capture = tmpfile();
  if (capture == NULL)
    return NULL;

  fflush(stdout);
  int saved_stdout = dup(STDOUT_FILENO);
  if (saved_stdout == -1 || dup2(fileno(capture), STDOUT_FILENO) == -1) {
    if (saved_stdout != -1)
      close(saved_stdout);
    fclose(capture);
    return NULL;
  }

  if (surface == CAPTURE_BOARD || surface == CAPTURE_ALL) {
    ui_print_header(1729, game->board_size, ui);
    ui_draw_game_board(game, ui);
  }
  if (surface == CAPTURE_MESSAGES || surface == CAPTURE_ALL)
    render_messages(ui);
  if (surface == CAPTURE_PROMPTS || surface == CAPTURE_ALL)
    render_prompts(ui);
  fflush(stdout);

  if (dup2(saved_stdout, STDOUT_FILENO) == -1) {
    close(saved_stdout);
    fclose(capture);
    return NULL;
  }
  close(saved_stdout);

  char *output = read_capture(capture);
  fclose(capture);
  return output;
}

static bool output_is_ascii(const char *output) {
  for (const unsigned char *byte = (const unsigned char *)output; *byte;
       byte++) {
    if (*byte >= 0x80)
      return false;
  }

  return true;
}

static bool has_ansi_sgr(const char *input) {
  for (const unsigned char *byte = (const unsigned char *)input; *byte;
       byte++) {
    if (*byte != '\033' || byte[1] != '[')
      continue;

    const unsigned char *cursor = byte + 2;
    while (*cursor != '\0' &&
           ((*cursor >= '0' && *cursor <= '9') || *cursor == ';'))
      cursor++;

    if (*cursor == 'm')
      return true;
  }

  return false;
}

static char *strip_ansi(const char *input) {
  size_t length = strlen(input);
  char *output = malloc(length + 1);
  if (output == NULL)
    return NULL;

  size_t output_index = 0;
  for (size_t index = 0; index < length; index++) {
    if (input[index] == '\033' && index + 1 < length &&
        input[index + 1] == '[') {
      index += 2;
      while (index < length && ((unsigned char)input[index] < '@' ||
                                (unsigned char)input[index] > '~'))
        index++;
      continue;
    }

    output[output_index++] = input[index];
  }

  output[output_index] = '\0';
  return output;
}

/* Check whether a single rendered line contains both substrings in order. */
static bool line_contains_both(const char *text, const char *left,
                               const char *right) {
  const char *cursor = text;

  while (*cursor != '\0') {
    const char *line_end = strchr(cursor, '\n');
    if (line_end == NULL)
      line_end = cursor + strlen(cursor);

    const char *left_match = strstr(cursor, left);
    if (left_match != NULL && left_match < line_end) {
      const char *right_match = strstr(left_match, right);
      if (right_match != NULL && right_match < line_end)
        return true;
    }

    if (*line_end == '\0')
      break;

    cursor = line_end + 1;
  }

  return false;
}

/*
 * Compare the length of rendered lines. ASCII output has one byte per column,
 * so byte length equals display width.
 */
static bool lines_have_equal_length(const char *text, int first_line,
                                    int line_count) {
  const char *cursor = text;
  size_t expected = 0;

  for (int index = 0; index < first_line + line_count; index++) {
    const char *line_end = strchr(cursor, '\n');
    if (line_end == NULL)
      return false;

    if (index >= first_line) {
      size_t length = (size_t)(line_end - cursor);
      if (index == first_line)
        expected = length;
      else if (length != expected)
        return false;
    }

    cursor = line_end + 1;
  }

  return true;
}

static void test_ascii_board_rendering(void) {
  struct game_state game;
  if (!create_empty_game(&game, 6))
    return;

  game.direction = DIRECTION_RIGHT;
  game.length = 3;
  game.obstacle_count = 1;
  game.stats.moves = 15;
  game.stats.snacks = 10;
  game.stats.poison = 2;
  game.head = (struct position){2, 0};
  game_set_board_tile(&game, 1, 0, 2);
  game_set_board_tile(&game, 2, 0, TILE_HEAD);
  game_set_board_tile(&game, 5, 0, TILE_POISON);
  game_set_board_tile(&game, 1, 1, TILE_SNACK);
  game_set_board_tile(&game, 3, 1, TILE_OBSTACLE);

  struct ui_state ui;
  ui_initialize_state(&ui, UI_MODE_ASCII, false);

  char *output = capture_ui_output(&game, &ui, CAPTURE_BOARD);
  CHECK(output != NULL);
  if (output == NULL) {
    game_destroy(&game);
    return;
  }

  CHECK(output_is_ascii(output));
  CHECK(strstr(output, "\x1B[92m= \x1B[0m") != NULL);
  CHECK(strstr(output, "\x1B[93m* \x1B[0m") != NULL);
  CHECK(strstr(output, "\x1B[94m# \x1B[0m") != NULL);
  CHECK(strstr(output, "\x1B[95m! \x1B[0m") != NULL);

  char *plain_output = strip_ansi(output);
  CHECK(plain_output != NULL);
  if (plain_output != NULL) {
    CHECK(strstr(plain_output, " +---------- ==> CNAKE (1729) ----------+") !=
          NULL);
    CHECK(strstr(plain_output, " +-------------+") != NULL);
    CHECK(strstr(plain_output, " | . = > . . ! |") != NULL);
    CHECK(strstr(plain_output, " | . * . # . . |") != NULL);
    const char *score_row = strstr(plain_output, "| +   Score       97 |");
    const char *filled_row = strstr(plain_output, "| %   Filled    5/36 |");
    const char *snake_row = strstr(plain_output, "Snake");
    CHECK(score_row != NULL);
    CHECK(filled_row != NULL);
    CHECK(snake_row != NULL);
    CHECK(filled_row < snake_row);
    CHECK(snake_row < score_row);
    /* The obstacle count is fixed at setup and no longer has a row. */
    CHECK(strstr(plain_output, "Obstacle") == NULL);
    CHECK(strstr(plain_output, " SCORE ") == NULL);
  }

  free(plain_output);
  free(output);
  game_destroy(&game);
}

static void test_ascii_messages(void) {
  struct game_state game;
  if (!create_empty_game(&game, 6))
    return;

  struct ui_state ui;
  ui_initialize_state(&ui, UI_MODE_ASCII, false);

  char *output = capture_ui_output(&game, &ui, CAPTURE_MESSAGES);
  CHECK(output != NULL);
  if (output != NULL) {
    CHECK(output_is_ascii(output));
    CHECK(strstr(output, "\x1B[93m + \x1B[0mThe answer is snake.") != NULL);

    char *plain_output = strip_ansi(output);
    CHECK(plain_output != NULL);
    if (plain_output != NULL) {
      CHECK(strstr(plain_output, "+ The answer is snake.") != NULL);
      CHECK(strstr(plain_output, "+---------- CONTROLS ----------+") != NULL);
      CHECK(strstr(plain_output, "Show help") != NULL);
      CHECK(strstr(plain_output, "Toggle debug view") != NULL);
      CHECK(strstr(plain_output, "Add five segments") != NULL);
      CHECK(strstr(plain_output, "Quit") != NULL);
      CHECK(strstr(plain_output, "Return [any key] / Quit [q]:") != NULL);
      CHECK(strstr(plain_output, " x Error:") != NULL);
    }
    free(plain_output);
  }

  free(output);
  game_destroy(&game);
}

static void test_prompt_transitions(void) {
  struct game_state game;
  if (!create_empty_game(&game, 6))
    return;

  struct ui_state ui;
  ui_initialize_state(&ui, UI_MODE_ASCII, false);

  char *output = capture_ui_output(&game, &ui, CAPTURE_PROMPTS);
  CHECK(output != NULL);
  if (output != NULL) {
    CHECK(output_is_ascii(output));
    CHECK_EQ(count_occurrences(output, "\033[1A"), 4);
    CHECK_EQ(count_occurrences(output, "Move "), 3);
    CHECK_EQ(count_occurrences(output, "Retry "), 6);
    CHECK(strstr(output, "\r\033[2K\r\n\033[2K\r") != NULL);
    CHECK(strstr(output, "\r\033[1A\r\033[2K") != NULL);

    char *plain_output = strip_ansi(output);
    CHECK(plain_output != NULL);
    if (plain_output != NULL) {
      CHECK(strstr(plain_output, "Move [hjkl] / Help [?] / Quit [q]:") != NULL);
      CHECK(strstr(plain_output, "Retry [r] / Quit [q]:") != NULL);
      CHECK(strstr(plain_output, "x Unknown command. Press ? for help.") !=
            NULL);
      CHECK(strstr(plain_output, "x Choose r to retry or q to quit.") != NULL);
      CHECK(strstr(plain_output, "* YOU WIN! *") != NULL);
      CHECK(strstr(plain_output, "==> Snake! - GAME OVER! -") != NULL);
      CHECK(strstr(plain_output, "# Obstacle! - GAME OVER! -") != NULL);
      CHECK(strstr(plain_output, "! Poison! - GAME OVER! -") != NULL);
    }
    free(plain_output);
  }

  free(output);
  game_destroy(&game);
}

static void test_unicode_theme_strings(void) {
  struct game_state game;
  if (!create_empty_game(&game, 6))
    return;

  struct ui_state ui;
  ui_initialize_state(&ui, UI_MODE_UNICODE, false);

  CHECK(strstr(ui_error_label(&ui), "× Error:") != NULL);
  CHECK(strstr(ui_warning_label(&ui), "! Warning:") != NULL);

  char *output = capture_ui_output(&game, &ui, CAPTURE_MESSAGES);
  CHECK(output != NULL);
  if (output != NULL) {
    CHECK(strstr(output, "\x1B[93m ★ \x1B[0mThe answer is snake.") != NULL);
    CHECK(strstr(output, "\x1B[96m ◆ \x1B[0mDebug view on.") != NULL);
    CHECK(strstr(output, "\x1B[96m ◆ \x1B[0mDebug view off.") != NULL);
    free(output);
  }

  game_destroy(&game);
}

static void test_answer_score_is_yellow(void) {
  struct game_state game;
  if (!create_empty_game(&game, 6))
    return;

  game.stats.moves = 6;
  game.stats.snacks = 4;
  game.stats.poison = 1;
  CHECK_EQ(game_score(&game), ANSWER_SCORE);

  struct ui_state ui;
  ui_initialize_state(&ui, UI_MODE_ASCII, false);

  char *output = capture_ui_output(&game, &ui, CAPTURE_BOARD);
  CHECK(output != NULL);
  if (output != NULL) {
    CHECK(strstr(output, "Score \x1B[93m      42\033[0m ") != NULL);
    free(output);
  }

  game_destroy(&game);
}

static void test_monochrome_suppresses_sgr(void) {
  struct game_state game;
  if (!create_empty_game(&game, 6))
    return;

  game.direction = DIRECTION_RIGHT;
  game.length = 1;

  struct ui_state ui;
  ui_initialize_state(&ui, UI_MODE_ASCII, true);

  char *output = capture_ui_output(&game, &ui, CAPTURE_ALL);
  CHECK(output != NULL);
  if (output != NULL) {
    CHECK(strstr(output, "\x1B[H") != NULL);
    CHECK(strstr(output, "\x1B[H\x1B[0J") == NULL);
    CHECK(strstr(output, "\x1B[0K\n") != NULL);
    CHECK(!has_ansi_sgr(output));
    CHECK(strstr(output, " x Error:") != NULL);
    CHECK(strstr(output, " * Debug view on.") != NULL);
    CHECK(strstr(output, " ! Length +5.") != NULL);
    CHECK(strstr(output, " + The answer is snake.") != NULL);
    free(output);
  }

  game_destroy(&game);
}

static void test_rendered_rows_clear_stale_suffixes(void) {
  struct game_state game;
  if (!create_empty_game(&game, 6))
    return;

  struct ui_state ui;
  ui_initialize_state(&ui, UI_MODE_ASCII, true);

  char *board_output = capture_ui_output(&game, &ui, CAPTURE_BOARD);
  CHECK(board_output != NULL);
  if (board_output != NULL) {
    CHECK_EQ(count_occurrences(board_output, "\033[0K\n"), game.board_size + 5);
    CHECK(strstr(board_output, "\033[H\033[0J") == NULL);
    free(board_output);
  }

  /*
   * The status panel keeps all its rows on boards shorter than itself, so a
   * small board renders as many lines as a board the height of the panel.
   */
  struct game_state small_game;
  if (create_empty_game(&small_game, 4)) {
    char *small_output = capture_ui_output(&small_game, &ui, CAPTURE_BOARD);
    CHECK(small_output != NULL);
    if (small_output != NULL) {
      CHECK_EQ(count_occurrences(small_output, "\033[0K\n"), 10);
      free(small_output);
    }

    game_destroy(&small_game);
  }

  char *message_output = capture_ui_output(&game, &ui, CAPTURE_MESSAGES);
  CHECK(message_output != NULL);
  if (message_output != NULL) {
    CHECK_EQ(count_occurrences(message_output, "\033[0K\n"), 12);
    free(message_output);
  }

  game_destroy(&game);
}

static void test_debug_panel_matches_main_layout(void) {
  struct game_state game;
  if (!create_empty_game(&game, 10))
    return;

  game.direction = DIRECTION_RIGHT;
  game.length = 7;
  game_set_board_tile(&game, 0, 0, 123);
  game_set_board_tile(&game, 1, 0, 4);
  game_set_board_tile(&game, 2, 0, -10);

  struct ui_state ui;
  ui_initialize_state(&ui, UI_MODE_ASCII, false);
  ui.show_debug = true;

  char *output = capture_ui_output(&game, &ui, CAPTURE_BOARD);
  CHECK(output != NULL);
  if (output == NULL) {
    game_destroy(&game);
    return;
  }

  char *plain_output = strip_ansi(output);
  CHECK(plain_output != NULL);
  if (plain_output != NULL) {
    CHECK(strstr(plain_output, " DEBUG ") != NULL);
    CHECK(strstr(plain_output, " DEBUG . 10x10 ") == NULL);
    CHECK(strstr(plain_output, "123") != NULL);
    CHECK(strstr(plain_output, "-10") != NULL);
    CHECK(strstr(plain_output, "   4") != NULL);
    /* The first status row and the first debug row share a line. */
    CHECK(line_contains_both(plain_output, "Filled", "123"));
  }

  free(plain_output);
  free(output);
  game_destroy(&game);
}

/*
 * On a board shorter than the status panel, the board and debug columns run
 * out of frame before the panel does and pad their columns instead.
 */
static void test_short_board_columns_are_padded(void) {
  struct game_state game;
  if (!create_empty_game(&game, 4))
    return;

  game.direction = DIRECTION_RIGHT;
  game.length = 3;
  game_set_board_tile(&game, 0, 0, TILE_HEAD);
  game_set_board_tile(&game, 1, 1, TILE_OBSTACLE);

  struct ui_state ui;
  ui_initialize_state(&ui, UI_MODE_ASCII, true);
  ui.show_debug = true;

  char *output = capture_ui_output(&game, &ui, CAPTURE_BOARD);
  CHECK(output != NULL);
  if (output == NULL) {
    game_destroy(&game);
    return;
  }

  /* Three header lines, then the five status rows and the panel frame. */
  CHECK_EQ(count_occurrences(output, "\033[0K\n"), 10);

  char *plain_output = strip_ansi(output);
  CHECK(plain_output != NULL);
  if (plain_output != NULL)
    CHECK(lines_have_equal_length(plain_output, 3, 7));

  free(plain_output);
  free(output);
  game_destroy(&game);
}

static void test_unicode_debug_header_alignment(void) {
  struct game_state game;
  if (!create_empty_game(&game, 4))
    return;

  game.direction = DIRECTION_RIGHT;
  game.length = 122;
  game.stats.moves = 15;
  game.stats.snacks = 10;
  game.stats.poison = 2;
  game_set_board_tile(&game, 0, 0, 121);
  game_set_board_tile(&game, 1, 0, TILE_HEAD);
  game_set_board_tile(&game, 0, 2, TILE_OBSTACLE);
  game_set_board_tile(&game, 2, 2, TILE_OBSTACLE);
  game_set_board_tile(&game, 3, 2, TILE_SNACK);
  game_set_board_tile(&game, 3, 3, TILE_POISON);

  struct ui_state ui;
  ui_initialize_state(&ui, UI_MODE_UNICODE, false);
  ui.show_debug = true;

  char *output = capture_ui_output(&game, &ui, CAPTURE_BOARD);
  CHECK(output != NULL);
  if (output == NULL) {
    game_destroy(&game);
    return;
  }

  char *plain_output = strip_ansi(output);
  CHECK(plain_output != NULL);
  if (plain_output != NULL) {
    CHECK(line_contains_both(plain_output, " STATUS · 4×4 ", " DEBUG "));
    CHECK(strstr(plain_output, " DEBUG · 4×4 ") == NULL);
    CHECK(strstr(plain_output, "★   Score       97") != NULL);
    /* The status panel keeps all its rows on boards shorter than the panel. */
    CHECK(strstr(plain_output, "×   Poison") != NULL);
  }

  free(plain_output);
  free(output);
  game_destroy(&game);
}

int main(void) {
  test_ascii_board_rendering();
  test_ascii_messages();
  test_prompt_transitions();
  test_unicode_theme_strings();
  test_answer_score_is_yellow();
  test_monochrome_suppresses_sgr();
  test_rendered_rows_clear_stale_suffixes();
  test_debug_panel_matches_main_layout();
  test_short_board_columns_are_padded();
  test_unicode_debug_header_alignment();

  return test_finish("UI");
}
