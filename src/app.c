#include "app.h"

#include "game.h"
#include "input.h"
#include "terminal.h"
#include "ui.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ANSWER_SCORE 42

static const int TERMINATION_SIGNALS[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT};

enum command_action {
  COMMAND_REPROMPT = 0,
  COMMAND_QUIT,
  COMMAND_REDRAW,
  COMMAND_SHOW_HELP,
};

static volatile sig_atomic_t interrupted = 0;

static void handle_signal(int signal_number) { interrupted = signal_number; }

struct signal_handler_state {
  struct sigaction previous[sizeof(TERMINATION_SIGNALS) /
                            sizeof(*TERMINATION_SIGNALS)];
  size_t installed;
};

static void restore_signal_handlers(struct signal_handler_state *state) {
  while (state->installed > 0) {
    state->installed--;
    sigaction(TERMINATION_SIGNALS[state->installed],
              &state->previous[state->installed], NULL);
  }
}

static bool install_signal_handlers(struct signal_handler_state *state) {
  struct sigaction action = {0};
  action.sa_handler = handle_signal;
  sigemptyset(&action.sa_mask);

  for (size_t index = 0;
       index < sizeof(TERMINATION_SIGNALS) / sizeof(*TERMINATION_SIGNALS);
       index++) {
    if (sigaction(TERMINATION_SIGNALS[index], &action,
                  &state->previous[index]) == -1) {
      restore_signal_handlers(state);
      return false;
    }
    state->installed++;
  }

  return true;
}

static void print_error(const struct ui_state *ui, const char *message) {
  fprintf(stderr, "%s %s\n", ui_error_label(ui->mode, ui->monochrome),
          message);
}

static void print_command_error(const struct ui_state *ui) {
  fprintf(stderr, "\n%s Unable to read a command.\n",
          ui_error_label(ui->mode, ui->monochrome));
}

static int prompt_for_board_size(const struct ui_state *ui, int *board_size) {
  while (true) {
    char input[32];
    ui_render_board_size_prompt(ui);

    if (fgets(input, sizeof(input), stdin) == NULL) {
      if (interrupted) {
        putchar('\n');
        return 128 + interrupted;
      }
      return APP_EXIT_INPUT_ERROR;
    }

    if (strchr(input, '\n') == NULL) {
      int discarded;
      while ((discarded = getchar()) != '\n' && discarded != EOF)
        ;
    }

    if (input[0] == '\n') {
      *board_size = DEFAULT_BOARD_SIZE;
      return APP_EXIT_OK;
    }

    enum board_size_parse_status status =
        cli_parse_board_size(input, board_size);
    if (status == BOARD_SIZE_VALID)
      return APP_EXIT_OK;

    ui_print_board_size_error(ui, status == BOARD_SIZE_OUT_OF_RANGE);
  }
}

static enum command_action
apply_player_command(struct game_state *game, struct ui_state *ui,
                     enum player_command command, const char **feedback) {
  switch (command) {
  case PLAYER_COMMAND_MOVE_UP:
    game_step(game, DIRECTION_UP);
    return COMMAND_REDRAW;
  case PLAYER_COMMAND_MOVE_DOWN:
    game_step(game, DIRECTION_DOWN);
    return COMMAND_REDRAW;
  case PLAYER_COMMAND_MOVE_LEFT:
    game_step(game, DIRECTION_LEFT);
    return COMMAND_REDRAW;
  case PLAYER_COMMAND_MOVE_RIGHT:
    game_step(game, DIRECTION_RIGHT);
    return COMMAND_REDRAW;
  case PLAYER_COMMAND_HELP:
    return COMMAND_SHOW_HELP;
  case PLAYER_COMMAND_TOGGLE_DEBUG:
    *feedback = ui_toggle_debug(ui);
    return COMMAND_REDRAW;
  case PLAYER_COMMAND_GROW:
    game_grow(game, 5);
    *feedback = ui_grow_feedback(ui);
    return COMMAND_REDRAW;
  case PLAYER_COMMAND_QUIT:
    return COMMAND_QUIT;
  case PLAYER_COMMAND_INVALID:
    return COMMAND_REPROMPT;
  }

  return COMMAND_REPROMPT;
}

static enum command_action
read_player_action(struct game_state *game, struct ui_state *ui,
                   struct terminal_state *terminal, const char **feedback,
                   int *exit_status) {
  ui_render_command_prompt(ui, *feedback, false, false);
  *feedback = NULL;

  while (!interrupted) {
    char input;
    enum input_read_result result = input_read_command(terminal, &input);

    if (result == INPUT_READ_ESCAPE)
      continue;
    if (result == INPUT_READ_ERROR) {
      print_command_error(ui);
      *exit_status = APP_EXIT_COMMAND_ERROR;
      return COMMAND_QUIT;
    }
    if (result == INPUT_READ_EOF)
      return COMMAND_QUIT;

    enum player_command command = input_parse_player_command(input);
    if (command == PLAYER_COMMAND_INVALID) {
      ui_render_command_prompt(ui, NULL, true, true);
      continue;
    }

    int previous_score = game_score(game);
    enum command_action action =
        apply_player_command(game, ui, command, feedback);
    if (previous_score != ANSWER_SCORE && game_score(game) == ANSWER_SCORE)
      *feedback = ui_answer_feedback(ui);
    return action;
  }

  return COMMAND_QUIT;
}

static enum command_action show_help(const struct ui_state *ui,
                                     struct terminal_state *terminal,
                                     int *exit_status) {
  ui_render_help(ui);
  ui_render_help_prompt(ui);

  while (!interrupted) {
    char input;
    enum input_read_result result = input_read_help(terminal, &input);

    if (result == INPUT_READ_ESCAPE)
      continue;
    if (result == INPUT_READ_ERROR) {
      print_command_error(ui);
      *exit_status = APP_EXIT_COMMAND_ERROR;
      return COMMAND_QUIT;
    }
    if (result == INPUT_READ_EOF)
      return COMMAND_QUIT;
    return input == 'q' ? COMMAND_QUIT : COMMAND_REDRAW;
  }

  return COMMAND_QUIT;
}

static enum command_action play_game(struct game_state *game,
                                     struct ui_state *ui,
                                     struct terminal_state *terminal,
                                     unsigned int seed, int *exit_status) {
  const char *feedback = NULL;

  while (!interrupted) {
    ui_begin_frame();
    ui_print_header(seed, game->board_size, ui);
    ui_draw_game_board(game, ui);

    if (game->status != GAME_RUNNING)
      return COMMAND_REDRAW;

    enum command_action action =
        read_player_action(game, ui, terminal, &feedback, exit_status);
    if (action == COMMAND_SHOW_HELP)
      action = show_help(ui, terminal, exit_status);
    if (action == COMMAND_QUIT)
      return action;
  }

  return COMMAND_QUIT;
}

static enum replay_command
read_replay_command(const struct ui_state *ui, struct terminal_state *terminal,
                    enum game_status game_status, int *exit_status) {
  ui_render_replay_prompt(ui, game_status, false, false);

  while (!interrupted) {
    char input;
    enum input_read_result result = input_read_command(terminal, &input);

    if (result == INPUT_READ_ESCAPE)
      continue;
    if (result == INPUT_READ_ERROR) {
      print_command_error(ui);
      *exit_status = APP_EXIT_COMMAND_ERROR;
      return REPLAY_COMMAND_QUIT;
    }
    if (result == INPUT_READ_EOF)
      return REPLAY_COMMAND_QUIT;

    enum replay_command command = input_parse_replay_command(input);
    if (command != REPLAY_COMMAND_INVALID)
      return command;

    ui_render_replay_prompt(ui, game_status, true, true);
  }

  return REPLAY_COMMAND_QUIT;
}

static int run_games(struct game_state *game, struct ui_state *ui,
                     struct terminal_state *terminal, unsigned int seed,
                     int board_size) {
  int exit_status = APP_EXIT_OK;

  while (!interrupted) {
    if (!game_create(game, board_size)) {
      print_error(ui, "Unable to allocate the game board.");
      return APP_EXIT_MEMORY_ERROR;
    }

    srand(seed);
    if (!game_initialize_board(game)) {
      print_error(ui, "Unable to place all tiles on the board.");
      return APP_EXIT_TILE_PLACEMENT_ERROR;
    }

    enum command_action action =
        play_game(game, ui, terminal, seed, &exit_status);
    if (exit_status != APP_EXIT_OK || interrupted)
      break;

    if (action == COMMAND_QUIT && game->status == GAME_RUNNING) {
      printf("\n");
      break;
    }

    enum replay_command replay =
        read_replay_command(ui, terminal, game->status, &exit_status);
    if (exit_status != APP_EXIT_OK || interrupted)
      break;
    if (replay == REPLAY_COMMAND_QUIT) {
      printf("\n");
      break;
    }

    game_destroy(game);
  }

  return exit_status;
}

int app_run(const struct cli_options *options) {
  struct game_state game;
  struct ui_state ui;
  struct terminal_state terminal;
  struct signal_handler_state signal_handlers = {0};
  int board_size = 0;
  int exit_status = APP_EXIT_OK;
  unsigned int seed =
      options->seed_provided ? options->seed : (unsigned int)time(NULL);

  interrupted = 0;
  game_reset(&game);
  ui_initialize_state(&ui, options->ascii ? UI_MODE_ASCII : UI_MODE_UNICODE,
                      options->monochrome);
  terminal_initialize_state(&terminal, &interrupted, options->monochrome);

  if (!install_signal_handlers(&signal_handlers)) {
    print_error(&ui, "Unable to install signal handler.");
    return APP_EXIT_TERMINAL_ERROR;
  }

  bool valid_board_size =
      options->board_size != NULL &&
      cli_parse_board_size(options->board_size, &board_size) == BOARD_SIZE_VALID;

  if (!terminal_enter_screen(&terminal)) {
    print_error(&ui, "Unable to initialize terminal screen.");
    exit_status = APP_EXIT_TERMINAL_ERROR;
    goto cleanup;
  }

  if (!valid_board_size) {
    ui_begin_frame();
    ui_print_header(seed, DEFAULT_BOARD_SIZE, &ui);
    exit_status = prompt_for_board_size(&ui, &board_size);
    if (exit_status != APP_EXIT_OK)
      goto cleanup;
  }

  if (!terminal_initialize(&terminal)) {
    print_error(&ui, "Unable to enable direct input mode.");
    exit_status = APP_EXIT_TERMINAL_ERROR;
    goto cleanup;
  }

  exit_status = run_games(&game, &ui, &terminal, seed, board_size);

cleanup:
  terminal_restore(&terminal);
  if (interrupted) {
    exit_status = 128 + interrupted;
    ui_print_interrupted_exit(&ui);
  }
  game_destroy(&game);
  restore_signal_handlers(&signal_handlers);
  return exit_status;
}
