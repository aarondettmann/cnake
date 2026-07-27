CC := gcc
CPPFLAGS := -D_POSIX_C_SOURCE=200809L
CFLAGS := -std=c11 -O2 -Wall -Wextra -Wpedantic

.PHONY: all clean

all: cnake

cnake: cnake.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

clean:
	$(RM) cnake
