#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli.h"

static int check_count = 0;
static int failure_count = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    check_count++;                                                             \
    if (!(condition)) {                                                        \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);     \
      failure_count++;                                                         \
    }                                                                          \
  } while (0)

#define CHECK_EQ(actual, expected)                                             \
  do {                                                                         \
    int actual_value = (actual);                                               \
    int expected_value = (expected);                                           \
    check_count++;                                                             \
    if (actual_value != expected_value) {                                      \
      fprintf(stderr, "FAIL %s:%d: expected %s == %d, got %d\n", __FILE__,     \
              __LINE__, #actual, expected_value, actual_value);                \
      failure_count++;                                                         \
    }                                                                          \
  } while (0)

static void test_defaults(void) {
  char *argv[] = {"cnake"};
  struct cli_options options;

  CHECK_EQ(cli_parse_options(1, argv, &options), CLI_PARSE_OK);
  CHECK_EQ(options.action, CLI_ACTION_RUN);
  CHECK(!options.ascii);
  CHECK(!options.seed_provided);
  CHECK(options.board_size == NULL);
}

static void check_order(char *argv[]) {
  struct cli_options options;

  CHECK_EQ(cli_parse_options(5, argv, &options), CLI_PARSE_OK);
  CHECK_EQ(options.action, CLI_ACTION_RUN);
  CHECK(options.ascii);
  CHECK(options.seed_provided);
  CHECK_EQ(options.seed, 1729);
  CHECK(strcmp(options.board_size, "6") == 0);
}

static void test_option_order(void) {
  char *ascii_first[] = {"cnake", "--ascii", "--seed", "1729", "6", NULL};
  char *size_first[] = {"cnake", "6", "--ascii", "--seed", "1729", NULL};
  char *seed_first[] = {"cnake", "--seed", "1729", "--ascii", "6", NULL};

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
  char *unknown[] = {"cnake", "--ascii", "--unknown"};
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
  CHECK_EQ(cli_parse_options(3, unknown, &options), CLI_PARSE_UNKNOWN_OPTION);
  CHECK(options.ascii);
  CHECK(strcmp(options.error_argument, "--unknown") == 0);
  CHECK_EQ(cli_parse_options(3, extra, &options), CLI_PARSE_EXTRA_ARGUMENT);
  CHECK(strcmp(options.error_argument, "7") == 0);
}

int main(void) {
  test_defaults();
  test_option_order();
  test_informational_options();
  test_parse_errors();

  if (failure_count != 0) {
    fprintf(stderr, "%d of %d checks failed.\n", failure_count, check_count);
    return EXIT_FAILURE;
  }

  printf("Passed %d CLI checks.\n", check_count);
  return EXIT_SUCCESS;
}
