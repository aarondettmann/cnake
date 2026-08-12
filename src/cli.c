#include "cli.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"

static bool parse_seed(const char *input, unsigned int *seed) {
  if (*input == '\0' || !isdigit((unsigned char)*input))
    return false;

  char *end;

  errno = 0;
  unsigned long parsed_seed = strtoul(input, &end, 10);

  if (errno != 0 || *end != '\0' || parsed_seed > UINT_MAX)
    return false;

  *seed = (unsigned int)parsed_seed;
  return true;
}

static bool parse_board_size(const char *input, int *board_size) {
  char *end;

  errno = 0;
  long parsed_size = strtol(input, &end, 10);

  if (errno != 0 || end == input || *end != '\0')
    return false;

  if (parsed_size < MIN_BOARD_SIZE || parsed_size > MAX_BOARD_SIZE)
    return false;

  *board_size = (int)parsed_size;
  return true;
}

/*
 * Informational options deliberately short-circuit all validation. Scan for
 * presentation mode flags at the same time so later parse errors use the
 * requested style.
 */
static void scan_global_options(int argc, char *argv[],
                                struct cli_options *options) {
  for (int index = 1; index < argc; index++) {
    if (strcmp(argv[index], "--ascii") == 0)
      options->ascii = true;
    else if (strcmp(argv[index], "--monochrome") == 0)
      options->monochrome = true;

    if (options->action != CLI_ACTION_RUN)
      continue;

    if (strcmp(argv[index], "--help") == 0)
      options->action = CLI_ACTION_HELP;
    else if (strcmp(argv[index], "--version") == 0)
      options->action = CLI_ACTION_VERSION;
  }
}

enum cli_parse_status cli_parse_options(int argc, char *argv[],
                                        struct cli_options *options) {
  const char *board_size_argument = NULL;

  *options = (struct cli_options){.board_size = DEFAULT_BOARD_SIZE};
  scan_global_options(argc, argv, options);

  if (options->action != CLI_ACTION_RUN)
    return CLI_PARSE_OK;

  for (int index = 1; index < argc; index++) {
    const char *argument = argv[index];

    if (strcmp(argument, "--ascii") == 0 ||
        strcmp(argument, "--monochrome") == 0)
      continue;

    if (strcmp(argument, "--seed") == 0) {
      if (options->seed_provided) {
        options->error_argument = argument;
        return CLI_PARSE_DUPLICATE_SEED;
      }

      if (index == argc - 1 || strncmp(argv[index + 1], "--", 2) == 0) {
        options->error_argument = argument;
        return CLI_PARSE_MISSING_SEED;
      }

      if (!parse_seed(argv[++index], &options->seed)) {
        options->error_argument = argv[index];
        return CLI_PARSE_INVALID_SEED;
      }

      options->seed_provided = true;
      continue;
    }

    if (strncmp(argument, "--", 2) == 0) {
      options->error_argument = argument;
      return CLI_PARSE_UNKNOWN_OPTION;
    }

    if (board_size_argument != NULL) {
      options->error_argument = argument;
      return CLI_PARSE_EXTRA_ARGUMENT;
    }

    board_size_argument = argument;
  }

  if (board_size_argument != NULL &&
      !parse_board_size(board_size_argument, &options->board_size)) {
    options->error_argument = board_size_argument;
    return CLI_PARSE_INVALID_BOARD_SIZE;
  }

  return CLI_PARSE_OK;
}
