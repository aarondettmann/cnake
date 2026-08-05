/**
 * @file cli.h
 * @brief Cnake - CLI interface definitions.
 */

#ifndef CNAKE_CLI_H
#define CNAKE_CLI_H

#include <stdbool.h>

enum cli_action {
  CLI_ACTION_RUN = 0,
  CLI_ACTION_HELP,
  CLI_ACTION_VERSION,
};

enum cli_parse_status {
  CLI_PARSE_OK = 0,
  CLI_PARSE_UNKNOWN_OPTION,
  CLI_PARSE_MISSING_SEED,
  CLI_PARSE_INVALID_SEED,
  CLI_PARSE_DUPLICATE_SEED,
  CLI_PARSE_EXTRA_ARGUMENT,
};

enum board_size_parse_status {
  BOARD_SIZE_INVALID = 0,
  BOARD_SIZE_OUT_OF_RANGE,
  BOARD_SIZE_VALID,
};

struct cli_options {
  enum cli_action action;
  bool ascii;
  bool monochrome;
  bool seed_provided;
  unsigned int seed;
  const char *board_size;
  const char *error_argument;
};

enum cli_parse_status cli_parse_options(int argc, char *argv[],
                                        struct cli_options *options);
enum board_size_parse_status cli_parse_board_size(const char *input,
                                                  int *board_size);

#endif
