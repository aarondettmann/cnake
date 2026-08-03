#ifndef CNAKE_TEST_H
#define CNAKE_TEST_H

#include <stdio.h>
#include <stdlib.h>

static int test_check_count = 0;
static int test_failure_count = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    test_check_count++;                                                        \
    if (!(condition)) {                                                        \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);     \
      test_failure_count++;                                                    \
    }                                                                          \
  } while (0)

#define CHECK_EQ(actual, expected)                                             \
  do {                                                                         \
    int actual_value = (actual);                                               \
    int expected_value = (expected);                                           \
    test_check_count++;                                                        \
    if (actual_value != expected_value) {                                      \
      fprintf(stderr, "FAIL %s:%d: expected %s == %d, got %d\n", __FILE__,     \
              __LINE__, #actual, expected_value, actual_value);                \
      test_failure_count++;                                                    \
    }                                                                          \
  } while (0)

static int test_finish(const char *suite_label) {
  if (test_failure_count != 0) {
    fprintf(stderr, "%d of %d checks failed.\n", test_failure_count,
            test_check_count);
    return EXIT_FAILURE;
  }

  if (suite_label == NULL)
    printf("Passed %d checks.\n", test_check_count);
  else
    printf("Passed %d %s checks.\n", test_check_count, suite_label);

  return EXIT_SUCCESS;
}

#endif
