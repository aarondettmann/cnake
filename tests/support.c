#define _XOPEN_SOURCE 600

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "test.h"

int test_check_count = 0;
int test_failure_count = 0;

int test_finish(const char *suite_label) {
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

bool open_terminal_pair(int *master, int *slave) {
  *master = posix_openpt(O_RDWR | O_NOCTTY);
  if (*master == -1)
    return false;

  if (grantpt(*master) == -1 || unlockpt(*master) == -1) {
    close(*master);
    return false;
  }

  char *slave_name = ptsname(*master);
  if (slave_name == NULL) {
    close(*master);
    return false;
  }

  *slave = open(slave_name, O_RDWR | O_NOCTTY);
  if (*slave == -1) {
    close(*master);
    return false;
  }

  return true;
}

/*
 * Read the master until end of input, keeping the first DRAIN_CAPACITY bytes
 * and discarding the rest, so the terminal keeps draining however much is
 * written. The collected bytes are forwarded in one go once there is nothing
 * left to read, which the parent consumes while this process writes, so the
 * pipe capacity never limits the transfer.
 */
#define DRAIN_CAPACITY 65536
#define DRAIN_READ_CHUNK 4096

static void drain_master(int master, int transfer) {
  static char collected[DRAIN_CAPACITY];
  char discarded[DRAIN_READ_CHUNK];
  size_t length = 0;

  for (;;) {
    size_t room = sizeof(collected) - length;
    char *target = room > 0 ? collected + length : discarded;
    size_t request =
        room > 0 && room < sizeof(discarded) ? room : sizeof(discarded);

    ssize_t bytes_read = read(master, target, request);
    if (bytes_read > 0) {
      if (room > 0)
        length += (size_t)bytes_read;
      continue;
    }

    if (bytes_read == -1 && errno == EINTR)
      continue;

    break;
  }

  size_t written = 0;
  while (written < length) {
    ssize_t bytes_written =
        write(transfer, collected + written, length - written);
    if (bytes_written > 0) {
      written += (size_t)bytes_written;
      continue;
    }

    if (bytes_written == -1 && errno == EINTR)
      continue;

    _exit(EXIT_FAILURE);
  }

  _exit(EXIT_SUCCESS);
}

bool start_terminal_drain(int master, int slave, struct terminal_drain *drain) {
  int transfer[2];

  drain->process = -1;
  drain->output = -1;

  if (pipe(transfer) == -1)
    return false;

  drain->process = fork();
  if (drain->process == -1) {
    close(transfer[0]);
    close(transfer[1]);
    return false;
  }

  if (drain->process == 0) {
    close(transfer[0]);
    close(slave);
    drain_master(master, transfer[1]);
  }

  close(transfer[1]);
  drain->output = transfer[0];
  return true;
}

char *finish_terminal_drain(struct terminal_drain *drain, int slave) {
  char *text = NULL;
  size_t length = 0;
  size_t capacity = 0;

  if (slave != -1) {
    while (tcdrain(slave) == -1 && errno == EINTR)
      ;
    close(slave);
  }

  for (;;) {
    if (capacity < length + DRAIN_READ_CHUNK + 1) {
      size_t wanted = capacity == 0 ? 8192 : capacity * 2;
      char *grown = realloc(text, wanted);
      if (grown == NULL) {
        free(text);
        text = NULL;
        break;
      }
      text = grown;
      capacity = wanted;
    }

    ssize_t bytes_read =
        read(drain->output, text + length, capacity - length - 1);
    if (bytes_read > 0) {
      length += (size_t)bytes_read;
      continue;
    }

    if (bytes_read == -1 && errno == EINTR)
      continue;

    if (bytes_read == -1) {
      free(text);
      text = NULL;
    }
    break;
  }

  if (text != NULL)
    text[length] = '\0';

  close(drain->output);
  drain->output = -1;

  int status;
  if (waitpid(drain->process, &status, 0) != drain->process ||
      !WIFEXITED(status) || WEXITSTATUS(status) != EXIT_SUCCESS) {
    free(text);
    text = NULL;
  }
  drain->process = -1;

  return text;
}

char *read_capture(FILE *capture) {
  if (fseek(capture, 0, SEEK_END) != 0)
    return NULL;

  long size = ftell(capture);
  if (size < 0 || fseek(capture, 0, SEEK_SET) != 0)
    return NULL;

  char *text = malloc((size_t)size + 1);
  if (text == NULL)
    return NULL;

  size_t bytes_read = fread(text, 1, (size_t)size, capture);
  text[bytes_read] = '\0';
  return text;
}

int count_occurrences(const char *text, const char *needle) {
  int count = 0;
  size_t needle_length = strlen(needle);

  for (const char *cursor = strstr(text, needle); cursor != NULL;
       cursor = strstr(cursor + needle_length, needle))
    count++;

  return count;
}
