/**
 * @file main.c
 * @brief Cnake - CLI entry point.
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

#include "app.h"
#include "cli.h"
#include "game.h"
#include "ui.h"

#include <limits.h>
#include <stdio.h>

#ifndef CNAKE_VERSION
#error "CNAKE_VERSION must be defined by the build"
#endif

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
         "  --monochrome  Disable ANSI text styling and use default terminal "
         "colors\n"
         "  --seed UINT   Reproduce a game with an unsigned integer seed\n",
         MIN_BOARD_SIZE, MAX_BOARD_SIZE, DEFAULT_BOARD_SIZE);
}

static int print_cli_error(enum cli_parse_status status,
                           const struct cli_options *options,
                           const char *program_name) {
  struct ui_state ui;
  ui_initialize_state(&ui, options->ascii ? UI_MODE_ASCII : UI_MODE_UNICODE,
                      options->monochrome || !ui_styling_supported(stderr));
  const char *error_label = ui_error_label(&ui);

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
  case CLI_PARSE_INVALID_BOARD_SIZE:
    fprintf(stderr, "%s Board size must be a number from %d to %d.\n",
            error_label, MIN_BOARD_SIZE, MAX_BOARD_SIZE);
    break;
  case CLI_PARSE_OK:
    return APP_EXIT_OK;
  }

  print_usage(stderr, program_name);
  return APP_EXIT_INPUT_ERROR;
}

int main(int argc, char *argv[]) {
  struct cli_options options;
  enum cli_parse_status parse_status = cli_parse_options(argc, argv, &options);

  if (parse_status != CLI_PARSE_OK)
    return print_cli_error(parse_status, &options, argv[0]);

  if (options.action == CLI_ACTION_HELP) {
    print_help(argv[0]);
    return APP_EXIT_OK;
  }

  if (options.action == CLI_ACTION_VERSION) {
    printf("cnake %s\n", CNAKE_VERSION);
    return APP_EXIT_OK;
  }

  return app_run(&options);
}
