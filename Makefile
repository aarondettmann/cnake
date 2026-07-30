CPPFLAGS += -D_POSIX_C_SOURCE=200809L
CFLAGS += -std=c11 -O2 -Wall -Wextra -Wpedantic

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
INSTALL ?= install

.PHONY: all clean install uninstall

all: cnake

cnake: cnake.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

clean:
	$(RM) cnake

install: cnake
	$(INSTALL) -d "$(DESTDIR)$(BINDIR)"
	$(INSTALL) -m 755 cnake "$(DESTDIR)$(BINDIR)/cnake"

uninstall:
	$(RM) "$(DESTDIR)$(BINDIR)/cnake"
