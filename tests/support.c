#define _XOPEN_SOURCE 600

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
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
