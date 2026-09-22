#ifndef CNAKE_TEST_H
#define CNAKE_TEST_H

#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>

/* Counters and helpers shared by every test binary; defined in support.c. */
extern int test_check_count;
extern int test_failure_count;

int test_finish(const char *suite_label);

/* Open a pseudo-terminal pair. Both descriptors are owned by the caller. */
bool open_terminal_pair(int *master, int *slave);

/* Read the whole contents of a tmpfile() stream into a new NUL-terminated
   buffer, or return NULL on failure. The stream is left at end of file. */
char *read_capture(FILE *capture);

/* A helper process collecting everything written to a pseudo-terminal. */
struct terminal_drain {
  pid_t process;
  int output;
};

/*
 * Start reading the master before running the code under test. tcsetattr with
 * TCSAFLUSH applies the settings once the output written to the terminal has
 * been transmitted, which for a pseudo-terminal means read by the master.
 * Linux reports no pending output for a pseudo-terminal and returns at once,
 * while macOS waits, so a test that only reads the master afterwards blocks
 * there forever.
 *
 * The caller restores any descriptor it pointed at the slave, and waits for
 * any other process it forked, before calling finish_terminal_drain.
 */
bool start_terminal_drain(int master, int slave, struct terminal_drain *drain);

/*
 * Wait for the terminal to transmit what is still queued, close the slave, and
 * collect the drained bytes as a new NUL-terminated buffer, or NULL on
 * failure, reaping the helper process. Closing a terminal discards its queues
 * on some systems, so the wait is what keeps the last output written before
 * the close, and the helper owns the close to keep the two in order.
 */
char *finish_terminal_drain(struct terminal_drain *drain, int slave);

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
