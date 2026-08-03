#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "game.h"
#include "test.h"
#include "ui.h"

static bool redirect_stdin(const char *input, int *saved_stdin,
                           FILE **input_file) {
  *input_file = tmpfile();
  if (*input_file == NULL)
    return false;

  if (fputs(input, *input_file) == EOF || fflush(*input_file) == EOF ||
      fseek(*input_file, 0, SEEK_SET) != 0) {
    fclose(*input_file);
    return false;
  }

  *saved_stdin = dup(STDIN_FILENO);
  if (*saved_stdin == -1 || dup2(fileno(*input_file), STDIN_FILENO) == -1) {
    if (*saved_stdin != -1)
      close(*saved_stdin);
    fclose(*input_file);
    return false;
  }

  clearerr(stdin);
  return true;
}

static void restore_stdin(int saved_stdin, FILE *input_file) {
  CHECK(dup2(saved_stdin, STDIN_FILENO) != -1);
  close(saved_stdin);
  fclose(input_file);
  clearerr(stdin);
}

static void render_ascii_surfaces(struct game_state *game,
                                  struct ui_state *ui) {
  ui_print_header(1729, game->board_size, ui);
  ui_draw_game_board(game, ui);
  ui_print_game_status_message(GAME_WON, ui);
  ui_print_game_status_message(GAME_OVER_SNAKE, ui);
  ui_print_game_status_message(GAME_OVER_OBSTACLE, ui);
  ui_print_game_status_message(GAME_OVER_POISON, ui);
  ui_print_game_summary(game, ui);
  ui_print_help(ui);
  ui_print_unknown_command(ui);
  printf("%s\n", ui_error_label(ui->mode));
  printf("%s\n", ui_toggle_debug(ui));
  printf("%s\n", ui_toggle_debug(ui));
  printf("%s\n", ui_grow_feedback());

  int saved_stdin;
  FILE *input_file;
  if (redirect_stdin("\n", &saved_stdin, &input_file)) {
    int board_size = 0;
    CHECK_EQ(ui_prompt_for_board_size(ui, &board_size), 0);
    CHECK_EQ(board_size, DEFAULT_BOARD_SIZE);
    restore_stdin(saved_stdin, input_file);
  } else {
    CHECK(false);
  }

  if (redirect_stdin("q", &saved_stdin, &input_file)) {
    enum player_command command = PLAYER_COMMAND_INVALID;
    const char *feedback = NULL;
    CHECK_EQ(ui_prompt_for_command(ui, &command, &feedback), COMMAND_READ_OK);
    CHECK_EQ(command, PLAYER_COMMAND_QUIT);
    restore_stdin(saved_stdin, input_file);
  } else {
    CHECK(false);
  }
}

static char *capture_ascii_output(struct game_state *game,
                                  struct ui_state *ui) {
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

  render_ascii_surfaces(game, ui);
  fflush(stdout);

  if (dup2(saved_stdout, STDOUT_FILENO) == -1) {
    close(saved_stdout);
    fclose(capture);
    return NULL;
  }
  close(saved_stdout);

  if (fseek(capture, 0, SEEK_END) != 0) {
    fclose(capture);
    return NULL;
  }

  long output_size = ftell(capture);
  if (output_size < 0 || fseek(capture, 0, SEEK_SET) != 0) {
    fclose(capture);
    return NULL;
  }

  char *output = malloc((size_t)output_size + 1);
  if (output == NULL) {
    fclose(capture);
    return NULL;
  }

  size_t bytes_read = fread(output, 1, (size_t)output_size, capture);
  output[bytes_read] = '\0';
  fclose(capture);
  return output;
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

static void test_ascii_rendering(void) {
  struct game_state game;
  CHECK(game_create(&game, 6));
  if (game.board == NULL)
    return;

  memset(game.board, 0,
         (size_t)game.board_size * (size_t)game.board_size *
             sizeof(*game.board));
  game.direction = DIRECTION_RIGHT;
  game.length = 3;
  game.head = (struct position){2, 0};
  game_set_board_tile(&game, 1, 0, 2);
  game_set_board_tile(&game, 2, 0, TILE_HEAD);
  game_set_board_tile(&game, 5, 0, TILE_POISON);
  game_set_board_tile(&game, 1, 1, TILE_SNACK);
  game_set_board_tile(&game, 3, 1, TILE_OBSTACLE);

  struct ui_state ui;
  ui_initialize_state(&ui, UI_MODE_ASCII);

  char *output = capture_ascii_output(&game, &ui);
  CHECK(output != NULL);
  if (output == NULL) {
    game_destroy(&game);
    return;
  }

  for (const unsigned char *byte = (const unsigned char *)output; *byte; byte++)
    CHECK(*byte < 0x80);

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
    CHECK(strstr(plain_output, "+---------- CONTROLS ----------+") != NULL);
    CHECK(strstr(plain_output, " > Board size") != NULL);
    CHECK(strstr(plain_output, " > Command") != NULL);
    CHECK(strstr(plain_output, " x Error:") != NULL);
  }

  free(plain_output);
  free(output);
  game_destroy(&game);
}

static void test_unicode_default(void) {
  struct ui_state ui;
  ui_initialize_state(&ui, UI_MODE_UNICODE);

  CHECK(ui.mode == UI_MODE_UNICODE);
  CHECK(strstr(ui_error_label(ui.mode), "× Error:") != NULL);
}

int main(void) {
  test_ascii_rendering();
  test_unicode_default();

  return test_finish("UI");
}
