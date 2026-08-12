#include "app.h"

#include "game.h"
#include "input.h"
#include "terminal.h"
#include "ui.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static const int TERMINATION_SIGNALS[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT};

enum command_action {
  COMMAND_REDRAW = 0,
  COMMAND_QUIT,
  COMMAND_SHOW_HELP,
};

/*
 * Everything a round needs. The seed advances between rounds unless the player
 * requested one, exit_status carries the first failure out to app_run, and
 * feedback holds the one-off message the next prompt displays.
 */
struct app_context {
  struct game_state *game;
  struct ui_state *ui;
  struct terminal_state *terminal;
  unsigned int seed;
  bool fixed_seed;
  int board_size;
  int exit_status;
  enum ui_feedback feedback;
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
  fprintf(stderr, "%s %s\n", ui_error_label(ui), message);
}

static void print_warning(const struct ui_state *ui, const char *message) {
  fprintf(stderr, "%s %s\n", ui_warning_label(ui), message);
}

static void restore_terminal(struct terminal_state *terminal,
                             const struct ui_state *ui) {
  unsigned int failures = terminal_restore(terminal);

  if (failures & TERMINAL_RESTORE_SCREEN_FAILED)
    print_warning(ui, "Unable to restore terminal screen.");
  if (failures & TERMINAL_RESTORE_SETTINGS_FAILED)
    print_warning(ui, "Unable to restore terminal settings.");
}

static void print_command_error(const struct ui_state *ui) {
  fprintf(stderr, "\n%s Unable to read a command.\n", ui_error_label(ui));
}

static enum command_action apply_player_command(struct app_context *app,
                                                enum player_command command) {
  switch (command) {
  case PLAYER_COMMAND_MOVE_UP:
    game_step(app->game, DIRECTION_UP);
    break;
  case PLAYER_COMMAND_MOVE_DOWN:
    game_step(app->game, DIRECTION_DOWN);
    break;
  case PLAYER_COMMAND_MOVE_LEFT:
    game_step(app->game, DIRECTION_LEFT);
    break;
  case PLAYER_COMMAND_MOVE_RIGHT:
    game_step(app->game, DIRECTION_RIGHT);
    break;
  case PLAYER_COMMAND_HELP:
    return COMMAND_SHOW_HELP;
  case PLAYER_COMMAND_TOGGLE_DEBUG:
    app->feedback = ui_toggle_debug(app->ui);
    break;
  case PLAYER_COMMAND_GROW:
    game_grow(app->game, 5);
    app->feedback = UI_FEEDBACK_GROW;
    break;
  case PLAYER_COMMAND_QUIT:
    return COMMAND_QUIT;
  case PLAYER_COMMAND_INVALID:
    break;
  }

  return COMMAND_REDRAW;
}

/*
 * Wait for one key press. Escape sequences are swallowed, and every reason to
 * stop reading - a signal, end of input, a read error - reports false, so
 * callers only have to handle the key they asked for.
 */
static bool read_key_or_quit(struct app_context *app, bool skip_newlines,
                             char *key) {
  while (!interrupted) {
    enum terminal_read_status status =
        skip_newlines ? terminal_read_command_key(app->terminal, key)
                      : terminal_read_key(app->terminal, key);

    switch (status) {
    case TERMINAL_READ_OK:
      return true;
    case TERMINAL_READ_ESCAPE:
      continue;
    case TERMINAL_READ_EOF:
      return false;
    case TERMINAL_READ_ERROR:
      print_command_error(app->ui);
      app->exit_status = APP_EXIT_COMMAND_ERROR;
      return false;
    }
  }

  return false;
}

/* Command prompts ignore a bare Enter; the help screen closes on any key. */
static bool read_command_key_or_quit(struct app_context *app, char *key) {
  return read_key_or_quit(app, true, key);
}

static bool read_any_key_or_quit(struct app_context *app, char *key) {
  return read_key_or_quit(app, false, key);
}

static enum command_action read_player_action(struct app_context *app) {
  char input;

  ui_render_command_prompt(app->ui, app->feedback, false, false);
  app->feedback = UI_FEEDBACK_NONE;

  while (read_command_key_or_quit(app, &input)) {
    enum player_command command = input_parse_player_command(input);
    if (command == PLAYER_COMMAND_INVALID) {
      ui_render_command_prompt(app->ui, UI_FEEDBACK_NONE, true, true);
      continue;
    }

    int previous_score = game_score(app->game);
    enum command_action action = apply_player_command(app, command);
    if (previous_score != ANSWER_SCORE && game_score(app->game) == ANSWER_SCORE)
      app->feedback = UI_FEEDBACK_ANSWER;
    return action;
  }

  return COMMAND_QUIT;
}

static enum command_action show_help(struct app_context *app) {
  char input;

  ui_render_help(app->ui);
  ui_render_help_prompt(app->ui);

  if (!read_any_key_or_quit(app, &input))
    return COMMAND_QUIT;

  return input == 'q' ? COMMAND_QUIT : COMMAND_REDRAW;
}

static enum command_action play_game(struct app_context *app) {
  app->feedback = UI_FEEDBACK_NONE;

  while (!interrupted) {
    ui_begin_frame();
    ui_print_header(app->seed, app->game->board_size, app->ui);
    ui_draw_game_board(app->game, app->ui);

    if (app->game->status != GAME_RUNNING)
      return COMMAND_REDRAW;

    enum command_action action = read_player_action(app);
    if (action == COMMAND_SHOW_HELP)
      action = show_help(app);
    if (action == COMMAND_QUIT)
      return action;
  }

  return COMMAND_QUIT;
}

static enum replay_command read_replay_command(struct app_context *app) {
  char input;

  ui_render_replay_prompt(app->ui, app->game->status, false, false);

  while (read_command_key_or_quit(app, &input)) {
    enum replay_command command = input_parse_replay_command(input);
    if (command != REPLAY_COMMAND_INVALID)
      return command;

    ui_render_replay_prompt(app->ui, app->game->status, true, true);
  }

  return REPLAY_COMMAND_QUIT;
}

/*
 * A requested seed stays fixed so a board can be replayed exactly. Without one,
 * every round draws a fresh seed from the running generator; reusing the same
 * seed would hand the player an identical board on every retry.
 */
static void run_games(struct app_context *app) {
  while (!interrupted) {
    if (!game_create(app->game, app->board_size)) {
      print_error(app->ui, "Unable to allocate the game board.");
      app->exit_status = APP_EXIT_MEMORY_ERROR;
      return;
    }

    srand(app->seed);
    if (!game_initialize_board(app->game)) {
      print_error(app->ui, "Unable to place all tiles on the board.");
      app->exit_status = APP_EXIT_TILE_PLACEMENT_ERROR;
      return;
    }

    enum command_action action = play_game(app);
    if (app->exit_status != APP_EXIT_OK || interrupted)
      return;

    if (action == COMMAND_QUIT && app->game->status == GAME_RUNNING) {
      printf("\n");
      return;
    }

    enum replay_command replay = read_replay_command(app);
    if (app->exit_status != APP_EXIT_OK || interrupted)
      return;
    if (replay == REPLAY_COMMAND_QUIT) {
      printf("\n");
      return;
    }

    game_destroy(app->game);
    if (!app->fixed_seed)
      app->seed = (unsigned int)rand();
  }
}

int app_run(const struct cli_options *options) {
  struct game_state game;
  struct ui_state ui;
  struct terminal_state terminal;
  struct signal_handler_state signal_handlers = {0};
  struct app_context app = {
      .game = &game,
      .ui = &ui,
      .terminal = &terminal,
      .seed = options->seed_provided ? options->seed : (unsigned int)time(NULL),
      .fixed_seed = options->seed_provided,
      .board_size = options->board_size,
      .exit_status = APP_EXIT_OK,
  };

  interrupted = 0;
  game_reset(&game);
  ui_initialize_state(&ui, options->ascii ? UI_MODE_ASCII : UI_MODE_UNICODE,
                      options->monochrome);
  terminal_initialize_state(&terminal, &interrupted);

  if (!install_signal_handlers(&signal_handlers)) {
    print_error(&ui, "Unable to install signal handler.");
    return APP_EXIT_TERMINAL_ERROR;
  }

  if (!terminal_enter_screen(&terminal)) {
    print_error(&ui, "Unable to initialize terminal screen.");
    app.exit_status = APP_EXIT_TERMINAL_ERROR;
    goto cleanup;
  }

  if (!terminal_initialize(&terminal)) {
    print_error(&ui, "Unable to enable direct input mode.");
    app.exit_status = APP_EXIT_TERMINAL_ERROR;
    goto cleanup;
  }

  run_games(&app);

cleanup:
  restore_terminal(&terminal, &ui);
  if (interrupted) {
    app.exit_status = 128 + interrupted;
    ui_print_interrupted_exit(&ui);
  }
  game_destroy(&game);
  restore_signal_handlers(&signal_handlers);
  return app.exit_status;
}
