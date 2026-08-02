/**
 * @file main.c
 * @brief Cnake - Main program orchestration.
 *
 * ---
 *
 * Cnake is a simple text-based snake game written in C.
 *
 * The first version of the game was written in 2014-10-16 to 2014-12-27.
 * The UI/UX has since been significantly improved.
 *
 * ---
 *
 * Copyright (c) 2026 Aaron Dettmann.
 * Licensed under the Apache License, Version 2.0.
 */

#include "cli.h"
#include "game.h"
#include "ui.h"

#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifndef CNAKE_VERSION
#error "CNAKE_VERSION must be defined by the build"
#endif

enum command_action {
  COMMAND_REPROMPT = 0,
  COMMAND_QUIT,
  COMMAND_SKIP_TURN,
  COMMAND_MOVE,
};

enum exit_code {
  EXIT_OK = 0,
  EXIT_INPUT_ERROR = 1,
  EXIT_MEMORY_ERROR = 2,
  EXIT_TILE_PLACEMENT_ERROR = 3,
  EXIT_TERMINAL_ERROR = 4,
  EXIT_COMMAND_ERROR = 5,
};

static volatile sig_atomic_t interrupted = 0;

static void handle_signal(int signal_number) { interrupted = signal_number; }

static void print_usage(FILE *stream, const char *program_name) {
  fprintf(stream, "Usage: %s [board-size] [options]\n", program_name);
}

static void print_help(const char *program_name) {
  print_usage(stdout, program_name);
  printf("\n"
         "Play Cnake with a board size from %d to %d (default %d).\n"
         "\n"
         "Options:\n"
         "  --help        Display this help and exit\n"
         "  --version     Display the version and exit\n"
         "  --ascii       Use pure ASCII characters for all terminal output\n"
         "  --seed UINT   Reproduce a game with an unsigned integer seed\n",
         MIN_BOARD_SIZE, MAX_BOARD_SIZE, DEFAULT_BOARD_SIZE);
}

static int print_cli_error(enum cli_parse_status status,
                           const struct cli_options *options,
                           const char *program_name) {
  enum ui_mode mode = options->ascii ? UI_MODE_ASCII : UI_MODE_UNICODE;
  const char *error_label = ui_error_label(mode);

  switch (status) {
  case CLI_PARSE_UNKNOWN_OPTION:
    fprintf(stderr, "%s Unknown option: %s\n", error_label,
            options->error_argument);
    break;
  case CLI_PARSE_MISSING_SEED:
    fprintf(stderr, "%s --seed requires an unsigned integer.\n", error_label);
    break;
  case CLI_PARSE_INVALID_SEED:
    fprintf(stderr, "%s Seed must be a decimal integer from 0 to %u.\n",
            error_label, UINT_MAX);
    break;
  case CLI_PARSE_DUPLICATE_SEED:
    fprintf(stderr, "%s --seed may only be specified once.\n", error_label);
    break;
  case CLI_PARSE_EXTRA_ARGUMENT:
    fprintf(stderr, "%s Unexpected command-line argument: %s\n", error_label,
            options->error_argument);
    break;
  case CLI_PARSE_OK:
    return EXIT_OK;
  }

  print_usage(stderr, program_name);
  return EXIT_INPUT_ERROR;
}

/*
 * Help, invalid input, and escape sequences do not advance the game. UI-only
 * commands redraw without changing the model; movement commands update it.
 */
static enum command_action apply_player_command(struct game_state *game,
                                                struct ui_state *ui,
                                                enum player_command command,
                                                const char **feedback) {
  switch (command) {
  case PLAYER_COMMAND_MOVE_UP:
    game_move(game, DIRECTION_UP);
    return COMMAND_MOVE;
  case PLAYER_COMMAND_MOVE_DOWN:
    game_move(game, DIRECTION_DOWN);
    return COMMAND_MOVE;
  case PLAYER_COMMAND_MOVE_LEFT:
    game_move(game, DIRECTION_LEFT);
    return COMMAND_MOVE;
  case PLAYER_COMMAND_MOVE_RIGHT:
    game_move(game, DIRECTION_RIGHT);
    return COMMAND_MOVE;
  case PLAYER_COMMAND_HELP:
    ui_print_help(ui);
    return COMMAND_REPROMPT;
  case PLAYER_COMMAND_TOGGLE_DEBUG:
    *feedback = ui_toggle_debug(ui);
    return COMMAND_SKIP_TURN;
  case PLAYER_COMMAND_GROW:
    game_grow(game, 5);
    *feedback = ui_grow_feedback();
    return COMMAND_SKIP_TURN;
  case PLAYER_COMMAND_QUIT:
    return COMMAND_QUIT;
  case PLAYER_COMMAND_INVALID:
    ui_print_unknown_command(ui);
    return COMMAND_REPROMPT;
  }

  return COMMAND_REPROMPT;
}

int main(int argc, char *argv[]) {
  struct cli_options options;
  enum cli_parse_status parse_status = cli_parse_options(argc, argv, &options);

  if (parse_status != CLI_PARSE_OK)
    return print_cli_error(parse_status, &options, argv[0]);

  if (options.action == CLI_ACTION_HELP) {
    print_help(argv[0]);
    return EXIT_OK;
  }

  if (options.action == CLI_ACTION_VERSION) {
    printf("cnake %s\n", CNAKE_VERSION);
    return EXIT_OK;
  }

  struct game_state game;
  struct ui_state ui;
  unsigned int seed =
      options.seed_provided ? options.seed : (unsigned int)time(NULL);
  int board_size = 0;
  int exit_status = EXIT_OK;

  game_reset(&game);
  ui_initialize_state(&ui, options.ascii ? UI_MODE_ASCII : UI_MODE_UNICODE);
  ui_set_interrupted_flag(&interrupted);

  if (atexit(ui_restore_terminal) != 0) {
    fprintf(stderr, "%s Unable to register terminal cleanup.\n",
            ui_error_label(ui.mode));
    return EXIT_TERMINAL_ERROR;
  }

  struct sigaction action = {0};
  action.sa_handler = handle_signal;
  sigemptyset(&action.sa_mask);

  if (sigaction(SIGINT, &action, NULL) == -1) {
    fprintf(stderr, "%s Unable to install signal handler.\n",
            ui_error_label(ui.mode));
    return EXIT_TERMINAL_ERROR;
  }

  bool valid_board_size =
      options.board_size != NULL &&
      ui_parse_board_size(options.board_size, &board_size) == BOARD_SIZE_VALID;

  ui_print_header(seed, valid_board_size ? board_size : DEFAULT_BOARD_SIZE,
                  &ui);

  if (!valid_board_size) {
    exit_status = ui_prompt_for_board_size(&ui, &board_size);
    if (exit_status != EXIT_OK)
      return exit_status;
  }

  if (!game_create(&game, board_size)) {
    fprintf(stderr, "%s Unable to allocate the game board.\n",
            ui_error_label(ui.mode));
    return EXIT_MEMORY_ERROR;
  }

  srand(seed);

  if (!game_initialize_board(&game)) {
    fprintf(stderr, "%s Unable to place all tiles on the board.\n",
            ui_error_label(ui.mode));
    exit_status = EXIT_TILE_PLACEMENT_ERROR;
    goto cleanup;
  }

  if (!ui_initialize_terminal()) {
    fprintf(stderr, "%s Unable to enable direct input mode.\n",
            ui_error_label(ui.mode));
    exit_status = EXIT_TERMINAL_ERROR;
    goto cleanup;
  }

  const char *feedback = NULL;

  /* Each turn starts from a complete render of the current state. */
  while (!interrupted) {
    ui_print_header(seed, game.board_size, &ui);
    ui_draw_game_board(&game, &ui);

    if (game.status != GAME_RUNNING) {
      ui_print_game_status_message(game.status, &ui);
      break;
    }

    enum command_action action_result = COMMAND_REPROMPT;
    struct position previous_head = game.head;

    while (action_result == COMMAND_REPROMPT) {
      enum player_command command = PLAYER_COMMAND_INVALID;
      int input_status = ui_prompt_for_command(&ui, &command, &feedback);

      if (input_status == COMMAND_READ_ERROR) {
        fprintf(stderr, "\n%s Unable to read a command.\n",
                ui_error_label(ui.mode));
        exit_status = EXIT_COMMAND_ERROR;
        goto cleanup;
      }

      if (input_status == COMMAND_READ_EOF) {
        action_result = COMMAND_QUIT;
        break;
      }

      if (input_status == COMMAND_READ_ESCAPE)
        continue;

      previous_head = game.head;
      action_result = apply_player_command(&game, &ui, command, &feedback);
    }

    if (action_result == COMMAND_QUIT)
      break;

    if (action_result == COMMAND_SKIP_TURN)
      continue;

    enum update_status update_result = game_update(&game, previous_head);
    if (update_result == UPDATE_SNACK_PLACEMENT_ERROR) {
      fprintf(stderr, "%s Unable to place a snack.\n", ui_error_label(ui.mode));
      exit_status = EXIT_TILE_PLACEMENT_ERROR;
      goto cleanup;
    }

    if (update_result == UPDATE_POISON_PLACEMENT_ERROR) {
      fprintf(stderr, "%s Unable to place poison.\n", ui_error_label(ui.mode));
      exit_status = EXIT_TILE_PLACEMENT_ERROR;
      goto cleanup;
    }
  }

  if (interrupted)
    exit_status = 128 + interrupted;

cleanup:
  ui_restore_terminal();

  if (interrupted) {
    ui_print_interrupted_exit();
  } else if (exit_status == EXIT_OK) {
    ui_print_game_summary(&game, &ui);
  }

  game_destroy(&game);
  return exit_status;
}
