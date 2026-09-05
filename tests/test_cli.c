#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli.h"
#include "game.h"
#include "test.h"

static void test_defaults(void) {
  char *argv[] = {"cnake"};
  struct cli_options options;

  CHECK_EQ(cli_parse_options(1, argv, &options), CLI_PARSE_OK);
  CHECK_EQ(options.action, CLI_ACTION_RUN);
  CHECK(!options.ascii);
  CHECK(!options.monochrome);
  CHECK(!options.seed_provided);
  CHECK_EQ(options.board_size, DEFAULT_BOARD_SIZE);
}

static void check_order(char *argv[]) {
  struct cli_options options;

  CHECK_EQ(cli_parse_options(6, argv, &options), CLI_PARSE_OK);
  CHECK_EQ(options.action, CLI_ACTION_RUN);
  CHECK(options.ascii);
  CHECK(options.monochrome);
  CHECK(options.seed_provided);
  CHECK_EQ(options.seed, 1729);
  CHECK_EQ(options.board_size, 6);
}

static void test_option_order(void) {
  char *ascii_first[] = {"cnake", "--ascii", "--monochrome", "--seed", "1729",
                         "6",     NULL};
  char *size_first[] = {"cnake",        "6", "--ascii", "--seed", "1729",
                        "--monochrome", NULL};
  char *seed_first[] = {"cnake",        "--seed", "1729", "--ascii",
                        "--monochrome", "6",      NULL};

  check_order(ascii_first);
  check_order(size_first);
  check_order(seed_first);
}

static void test_informational_options(void) {
  char *help_argv[] = {"cnake", "--unknown", "--help", "extra"};
  char *version_argv[] = {"cnake", "extra", "--version", "--seed"};
  char *first_argv[] = {"cnake", "--version", "--help"};
  struct cli_options options;

  CHECK_EQ(cli_parse_options(4, help_argv, &options), CLI_PARSE_OK);
  CHECK_EQ(options.action, CLI_ACTION_HELP);

  CHECK_EQ(cli_parse_options(4, version_argv, &options), CLI_PARSE_OK);
  CHECK_EQ(options.action, CLI_ACTION_VERSION);

  CHECK_EQ(cli_parse_options(3, first_argv, &options), CLI_PARSE_OK);
  CHECK_EQ(options.action, CLI_ACTION_VERSION);
}

static void test_parse_errors(void) {
  char too_large[64];
  snprintf(too_large, sizeof(too_large), "%llu",
           (unsigned long long)UINT_MAX + 1);

  char *missing_seed[] = {"cnake", "--seed"};
  char *option_after_seed[] = {"cnake", "--seed", "--ascii"};
  char *invalid_seed[] = {"cnake", "--seed", "12x"};
  char *large_seed[] = {"cnake", "--seed", too_large};
  char *duplicate_seed[] = {"cnake", "--seed", "1", "--seed", "2"};
  char *unknown[] = {"cnake", "--ascii", "--monochrome", "--unknown"};
  char *extra[] = {"cnake", "6", "7"};
  struct cli_options options;

  CHECK_EQ(cli_parse_options(2, missing_seed, &options),
           CLI_PARSE_MISSING_SEED);
  CHECK_EQ(cli_parse_options(3, option_after_seed, &options),
           CLI_PARSE_MISSING_SEED);
  CHECK(options.ascii);
  CHECK_EQ(cli_parse_options(3, invalid_seed, &options),
           CLI_PARSE_INVALID_SEED);
  CHECK_EQ(cli_parse_options(3, large_seed, &options), CLI_PARSE_INVALID_SEED);
  CHECK_EQ(cli_parse_options(5, duplicate_seed, &options),
           CLI_PARSE_DUPLICATE_SEED);
  CHECK_EQ(cli_parse_options(4, unknown, &options), CLI_PARSE_UNKNOWN_OPTION);
  CHECK(options.ascii);
  CHECK(options.monochrome);
  CHECK(strcmp(options.error_argument, "--unknown") == 0);
  CHECK_EQ(cli_parse_options(3, extra, &options), CLI_PARSE_EXTRA_ARGUMENT);
  CHECK(strcmp(options.error_argument, "7") == 0);
}

static void check_board_size(const char *argument, int expected) {
  char *argv[] = {"cnake", (char *)argument};
  struct cli_options options;

  CHECK_EQ(cli_parse_options(2, argv, &options), CLI_PARSE_OK);
  CHECK_EQ(options.board_size, expected);
}

static void check_rejected_board_size(const char *argument) {
  char *argv[] = {"cnake", (char *)argument};
  struct cli_options options;

  CHECK_EQ(cli_parse_options(2, argv, &options), CLI_PARSE_INVALID_BOARD_SIZE);
  CHECK(strcmp(options.error_argument, argument) == 0);
}

static void test_board_size_parsing(void) {
  char *help_argv[] = {"cnake", "3", "--help"};
  struct cli_options options;

  check_board_size("4", MIN_BOARD_SIZE);
  check_board_size("10", MAX_BOARD_SIZE);
  check_rejected_board_size("3");
  check_rejected_board_size("11");
  check_rejected_board_size("six");
  check_rejected_board_size("6x");
  check_rejected_board_size("10 ");

  /* Informational options still short-circuit board size validation. */
  CHECK_EQ(cli_parse_options(3, help_argv, &options), CLI_PARSE_OK);
  CHECK_EQ(options.action, CLI_ACTION_HELP);
}

int main(void) {
  test_defaults();
  test_option_order();
  test_informational_options();
  test_parse_errors();
  test_board_size_parsing();

  return test_finish("CLI");
}
