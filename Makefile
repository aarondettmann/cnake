# CFLAGS holds only optimisation and debugging options, so a user or CI job
# can replace it (CFLAGS="-O0 -g") without losing the standard or the warnings.
CPPFLAGS += -D_POSIX_C_SOURCE=200809L
WARNINGS ?= -Wall -Wextra -Wpedantic -Wshadow
CFLAGS ?= -O2
ALL_CFLAGS = -std=c11 $(WARNINGS) $(CFLAGS)
SANITIZE_FLAGS ?= -g -fsanitize=address,undefined -fno-omit-frame-pointer

CNAKE_VERSION := $(strip $(shell cat VERSION))
VERSION_CPPFLAGS := -DCNAKE_VERSION=\"$(CNAKE_VERSION)\"

ifeq ($(CNAKE_VERSION),)
$(error VERSION must contain a version number)
endif

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
BASHCOMPDIR ?= $(PREFIX)/share/bash-completion/completions
INSTALL ?= install
OBJDIR ?= build

SRC := src/main.c src/app.c src/cli.c src/game.c src/input.c src/terminal.c \
	src/ui.c
OBJ := $(SRC:src/%.c=$(OBJDIR)/%.o)
DEPS := $(OBJ:.o=.d)
TESTS := game cli ui input terminal app
TEST_BINS := $(TESTS:%=$(OBJDIR)/test_%)
TEST_OBJ := $(TESTS:%=$(OBJDIR)/tests/test_%.o) $(OBJDIR)/tests/support.o
TEST_DEPS := $(TEST_OBJ:.o=.d)
DEPFLAGS = -MMD -MP

.PHONY: all clean check check-sanitize install uninstall
.SECONDARY: $(TEST_OBJ)

all: cnake

cnake: $(OBJ)
	$(CC) $(CPPFLAGS) $(ALL_CFLAGS) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

$(OBJDIR)/main.o: src/main.c VERSION
	@mkdir -p "$(dir $@)"
	$(CC) $(CPPFLAGS) $(VERSION_CPPFLAGS) $(ALL_CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(OBJDIR)/%.o: src/%.c
	@mkdir -p "$(dir $@)"
	$(CC) $(CPPFLAGS) $(ALL_CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(OBJDIR)/tests/%.o: tests/%.c
	@mkdir -p "$(dir $@)"
	$(CC) $(CPPFLAGS) $(ALL_CFLAGS) -Isrc $(DEPFLAGS) -c -o $@ $<

# Every test binary links its own object, the shared support object and the
# production objects listed as extra prerequisites below.
$(OBJDIR)/test_%: $(OBJDIR)/tests/test_%.o $(OBJDIR)/tests/support.o
	$(CC) $(CPPFLAGS) $(ALL_CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(OBJDIR)/test_game: $(OBJDIR)/game.o
$(OBJDIR)/test_cli: $(OBJDIR)/cli.o
$(OBJDIR)/test_ui: $(OBJDIR)/game.o $(OBJDIR)/ui.o
$(OBJDIR)/test_input: $(OBJDIR)/input.o
$(OBJDIR)/test_terminal: $(OBJDIR)/terminal.o
$(OBJDIR)/test_app: $(OBJDIR)/app.o $(OBJDIR)/cli.o $(OBJDIR)/game.o \
	$(OBJDIR)/input.o $(OBJDIR)/terminal.o $(OBJDIR)/ui.o

check: cnake $(TEST_BINS)
	for test in $(TEST_BINS); do ./$$test || exit 1; done
	bash -n completions/cnake
	test "$$(./cnake --version)" = "cnake $(CNAKE_VERSION)"
	./cnake --help | grep -F -- "--ascii" >/dev/null
	./cnake --help | grep -F -- "--monochrome" >/dev/null
	./cnake --unknown --help >/dev/null
	./cnake --version --unknown >/dev/null

# Rebuild everything with AddressSanitizer and UndefinedBehaviorSanitizer in a
# separate object directory and run the checks against that build.
check-sanitize:
	$(MAKE) check OBJDIR="$(OBJDIR)/sanitize" \
		CFLAGS="$(CFLAGS) $(SANITIZE_FLAGS)" \
		LDFLAGS="$(LDFLAGS) $(SANITIZE_FLAGS)"

clean:
	$(RM) -r cnake "$(OBJDIR)"

install: cnake
	$(INSTALL) -d "$(DESTDIR)$(BINDIR)"
	$(INSTALL) -m 755 cnake "$(DESTDIR)$(BINDIR)/cnake"
	$(INSTALL) -d "$(DESTDIR)$(BASHCOMPDIR)"
	$(INSTALL) -m 644 completions/cnake \
		"$(DESTDIR)$(BASHCOMPDIR)/cnake"

uninstall:
	$(RM) "$(DESTDIR)$(BINDIR)/cnake"
	$(RM) "$(DESTDIR)$(BASHCOMPDIR)/cnake"

-include $(DEPS) $(TEST_DEPS)
