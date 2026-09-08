#ifndef CNAKE_TEST_H
#define CNAKE_TEST_H

#include <stdbool.h>
#include <stdio.h>

/* Counters and helpers shared by every test binary; defined in support.c. */
extern int test_check_count;
extern int test_failure_count;

int test_finish(const char *suite_label);

/* Open a pseudo-terminal pair. Both descriptors are owned by the caller. */
bool open_terminal_pair(int *master, int *slave);

/* Read the whole contents of a tmpfile() stream into a new NUL-terminated
   buffer, or return NULL on failure. The stream is left at end of file. */
char *read_capture(FILE *capture);

/* Number of non-overlapping occurrences of needle in text. */
int count_occurrences(const char *text, const char *needle);

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
    long long actual_value = (actual);                                         \
    long long expected_value = (expected);                                     \
    test_check_count++;                                                        \
    if (actual_value != expected_value) {                                      \
      fprintf(stderr, "FAIL %s:%d: expected %s == %lld, got %lld\n", __FILE__, \
              __LINE__, #actual, expected_value, actual_value);                \
      test_failure_count++;                                                    \
    }                                                                          \
  } while (0)

#endif
