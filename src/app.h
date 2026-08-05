/**
 * @file app.h
 * @brief Cnake - Application controller.
 */

#ifndef CNAKE_APP_H
#define CNAKE_APP_H

#include "cli.h"

enum app_exit_status {
  APP_EXIT_OK = 0,
  APP_EXIT_INPUT_ERROR = 1,
  APP_EXIT_MEMORY_ERROR = 2,
  APP_EXIT_TILE_PLACEMENT_ERROR = 3,
  APP_EXIT_TERMINAL_ERROR = 4,
  APP_EXIT_COMMAND_ERROR = 5,
};

int app_run(const struct cli_options *options);

#endif
