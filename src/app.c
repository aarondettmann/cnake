#include "app.h"

#include "game.h"
#include "input.h"
#include "terminal.h"
#include "ui.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

enum command_action {
  COMMAND_REDRAW = 0,
  COMMAND_QUIT,
  COMMAND_SHOW_HELP,
};

/* Outcome of waiting for a key press. */
enum key_status {
  KEY_PRESSED = 0,
  KEY_REDRAW,
  KEY_STOP,
};

/*
 * Everything a round needs. The seed advances between rounds unless the player
 * requested one, exit_status and error_message carry the first failure out to
 * app_run, and feedback holds the one-off message the next prompt displays.
 */
struct app_context {
  struct game_state *game;
  struct ui_state *ui;
  struct terminal_state *terminal;
  unsigned int seed;
  bool fixed_seed;
  int board_size;
  int exit_status;
  const char *error_message;
  enum ui_feedback feedback;
};

static volatile sig_atomic_t interrupted = 0;

/* The terminal to hand back to the shell while the process is suspended. */
static struct terminal_state *suspendable_terminal = NULL;

static void handle_termination(int signal_number) {
  interrupted = signal_number;
}

/*
 * Ctrl-Z arrives as SIGTSTP. The terminal is restored before the process
 * stops, so the shell gets a normal screen back, and taken again once the
 * process continues. The stop itself comes from re-raising the signal with the
 * default disposition, which returns only after SIGCONT. Every call in here is
 * async-signal-safe.
 */
static void handle_suspension(int signal_number) {
  int saved_errno = errno;
  struct terminal_state *terminal = suspendable_terminal;
  struct sigaction default_action = {0};
  struct sigaction previous_action;
  sigset_t signal_set;

  if (terminal != NULL)
    terminal_suspend(terminal);

  default_action.sa_handler = SIG_DFL;
  sigemptyset(&default_action.sa_mask);
  sigaction(signal_number, &default_action, &previous_action);

  sigemptyset(&signal_set);
  sigaddset(&signal_set, signal_number);
  sigprocmask(SIG_UNBLOCK, &signal_set, NULL);
  raise(signal_number);
  sigprocmask(SIG_BLOCK, &signal_set, NULL);

  sigaction(signal_number, &previous_action, NULL);
  if (terminal != NULL)
    terminal_resume(terminal);

  errno = saved_errno;
}

static const struct {
  int number;
  void (*handler)(int);
} HANDLED_SIGNALS[] = {
    {SIGINT, handle_termination}, {SIGTERM, handle_termination},
    {SIGHUP, handle_termination}, {SIGQUIT, handle_termination},
    {SIGTSTP, handle_suspension},
};

#define HANDLED_SIGNAL_COUNT                                                   \
  (sizeof(HANDLED_SIGNALS) / sizeof(*HANDLED_SIGNALS))

struct signal_handler_state {
  struct sigaction previous[HANDLED_SIGNAL_COUNT];
  size_t installed;
};

static void restore_signal_handlers(struct signal_handler_state *state) {
  while (state->installed > 0) {
    state->installed--;
    sigaction(HANDLED_SIGNALS[state->installed].number,
              &state->previous[state->installed], NULL);
  }
}

static bool install_signal_handlers(struct signal_handler_state *state) {
  for (size_t index = 0; index < HANDLED_SIGNAL_COUNT; index++) {
    struct sigaction action = {0};
    action.sa_handler = HANDLED_SIGNALS[index].handler;
    sigemptyset(&action.sa_mask);

    if (sigaction(HANDLED_SIGNALS[index].number, &action,
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

/*
 * Errors are reported after the terminal is restored. Anything written while
 * the alternate screen is active disappears with that screen, so the message is
 * kept until app_run has switched back.
 */
static void fail(struct app_context *app, int exit_status,
                 const char *message) {
  if (app->exit_status != APP_EXIT_OK)
    return;

  app->exit_status = exit_status;
  app->error_message = message;
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

static void move_snake(struct app_context *app, enum direction direction) {
  if (!game_step(app->game, direction))
    app->feedback = UI_FEEDBACK_REVERSAL;
}

static enum command_action apply_player_command(struct app_context *app,
                                                enum player_command command) {
  switch (command) {
  case PLAYER_COMMAND_MOVE_UP:
    move_snake(app, DIRECTION_UP);
    break;
  case PLAYER_COMMAND_MOVE_DOWN:
    move_snake(app, DIRECTION_DOWN);
    break;
  case PLAYER_COMMAND_MOVE_LEFT:
    move_snake(app, DIRECTION_LEFT);
    break;
  case PLAYER_COMMAND_MOVE_RIGHT:
    move_snake(app, DIRECTION_RIGHT);
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
 * Wait for one key press. Every reason to stop reading - a signal, end of
 * input, a read error - reports KEY_STOP, and a suspension that ended reports
 * KEY_REDRAW so the caller can repaint the screen the shell has overwritten.
 * An escape sequence counts as a key press: the key holds the final byte of
 * the sequence and the escape flag is set, so every prompt treats cursor keys
 * and unknown sequences the same way it treats plain characters.
 */
static enum key_status read_key_or_quit(struct app_context *app,
                                        bool skip_newlines, char *key,
                                        bool *escape) {
  while (!interrupted) {
    enum terminal_read_status status =
        skip_newlines ? terminal_read_command_key(app->terminal, key)
                      : terminal_read_key(app->terminal, key);

    switch (status) {
    case TERMINAL_READ_OK:
      *escape = false;
      return KEY_PRESSED;
    case TERMINAL_READ_ESCAPE:
      *escape = true;
      return KEY_PRESSED;
    case TERMINAL_READ_RESUMED:
      return KEY_REDRAW;
    case TERMINAL_READ_EOF:
      return KEY_STOP;
    case TERMINAL_READ_ERROR:
      fail(app, APP_EXIT_COMMAND_ERROR, "Unable to read a command.");
      return KEY_STOP;
    }
  }

  return KEY_STOP;
}

/* Command prompts ignore a bare Enter; the help screen closes on any key. */
static enum key_status read_command_key_or_quit(struct app_context *app,
                                                char *key, bool *escape) {
  return read_key_or_quit(app, true, key, escape);
}

static enum key_status read_any_key_or_quit(struct app_context *app, char *key,
                                            bool *escape) {
  return read_key_or_quit(app, false, key, escape);
}

static void draw_board(const struct app_context *app) {
  ui_begin_frame();
  ui_print_header(app->seed, app->game->board_size, app->ui);
  ui_draw_game_board(app->game, app->ui);
}

static enum command_action read_player_action(struct app_context *app) {
  char input;
  bool escape;

  ui_render_command_prompt(app->ui, app->feedback, false, false);
  app->feedback = UI_FEEDBACK_NONE;

  while (true) {
    switch (read_command_key_or_quit(app, &input, &escape)) {
    case KEY_STOP:
      return COMMAND_QUIT;
    case KEY_REDRAW:
      return COMMAND_REDRAW;
    case KEY_PRESSED:
      break;
    }

    enum player_command command = escape ? input_parse_escape_command(input)
                                         : input_parse_player_command(input);
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
}

static enum command_action show_help(struct app_context *app) {
  char input;
  bool escape;

  while (true) {
    ui_render_help(app->ui);
    ui_render_help_prompt(app->ui);

    switch (read_any_key_or_quit(app, &input, &escape)) {
    case KEY_STOP:
      return COMMAND_QUIT;
    case KEY_REDRAW:
      continue;
    case KEY_PRESSED:
      return !escape && input == 'q' ? COMMAND_QUIT : COMMAND_REDRAW;
    }
  }
}

static enum command_action play_game(struct app_context *app) {
  app->feedback = UI_FEEDBACK_NONE;

  while (!interrupted) {
    draw_board(app);

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
  bool escape;

  ui_render_replay_prompt(app->ui, app->game->status, false, false);

  while (true) {
    switch (read_command_key_or_quit(app, &input, &escape)) {
    case KEY_STOP:
      return REPLAY_COMMAND_QUIT;
    case KEY_REDRAW:
      draw_board(app);
      ui_render_replay_prompt(app->ui, app->game->status, false, false);
      continue;
    case KEY_PRESSED:
      break;
    }

    enum replay_command command =
        escape ? REPLAY_COMMAND_INVALID : input_parse_replay_command(input);
    if (command != REPLAY_COMMAND_INVALID)
      return command;

    ui_render_replay_prompt(app->ui, app->game->status, true, true);
  }
}

/*
 * A requested seed stays fixed so a board can be replayed exactly. Without one,
 * every round draws a fresh seed from the running generator; reusing the same
 * seed would hand the player an identical board on every retry.
 */
static void run_games(struct app_context *app) {
  while (!interrupted) {
    if (!game_create(app->game, app->board_size)) {
      fail(app, APP_EXIT_MEMORY_ERROR, "Unable to allocate the game board.");
      return;
    }

    srand(app->seed);
    if (!game_initialize_board(app->game)) {
      fail(app, APP_EXIT_TILE_PLACEMENT_ERROR,
           "Unable to place all tiles on the board.");
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
  struct ui_state diagnostics_ui;
  struct terminal_state terminal;
  struct signal_handler_state signal_handlers = {0};
  enum ui_mode mode = options->ascii ? UI_MODE_ASCII : UI_MODE_UNICODE;
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
  game_initialize_state(&game);
  /*
   * The board is written to standard output and diagnostics to standard error.
   * Styling is determined separately for each of the two streams.
   */
  ui_initialize_state(&ui, mode,
                      options->monochrome || !ui_styling_supported(stdout));
  ui_initialize_state(&diagnostics_ui, mode,
                      options->monochrome || !ui_styling_supported(stderr));
  terminal_initialize_state(&terminal, &interrupted);
  suspendable_terminal = &terminal;

  if (!install_signal_handlers(&signal_handlers)) {
    fail(&app, APP_EXIT_TERMINAL_ERROR, "Unable to install signal handler.");
    goto cleanup;
  }

  if (!terminal_enter_screen(&terminal)) {
    fail(&app, APP_EXIT_TERMINAL_ERROR,
         "Unable to initialize terminal screen.");
    goto cleanup;
  }

  if (!terminal_enter_raw_mode(&terminal)) {
    fail(&app, APP_EXIT_TERMINAL_ERROR, "Unable to enable direct input mode.");
    goto cleanup;
  }

  run_games(&app);

cleanup:
  restore_terminal(&terminal, &diagnostics_ui);
  if (app.error_message != NULL)
    print_error(&diagnostics_ui, app.error_message);
  /*
   * The final board leaves together with the alternate screen, so the result
   * and the seed of the last round are repeated on the normal screen. A game
   * ended by an error or a signal has no result to report.
   */
  if (app.error_message == NULL && !interrupted && game.board != NULL)
    ui_print_summary(&game, app.seed, &ui);
  if (interrupted) {
    app.exit_status = 128 + interrupted;
    ui_print_interrupted_exit(&ui);
  }
  game_destroy(&game);
  restore_signal_handlers(&signal_handlers);
  suspendable_terminal = NULL;
  return app.exit_status;
}
