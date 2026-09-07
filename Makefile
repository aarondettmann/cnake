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
TEST_GAME_BIN := $(OBJDIR)/test_game
TEST_CLI_BIN := $(OBJDIR)/test_cli
TEST_UI_BIN := $(OBJDIR)/test_ui
TEST_INPUT_BIN := $(OBJDIR)/test_input
TEST_TERMINAL_BIN := $(OBJDIR)/test_terminal
TEST_APP_BIN := $(OBJDIR)/test_app
TEST_BINS := $(TEST_GAME_BIN) $(TEST_CLI_BIN) $(TEST_UI_BIN) $(TEST_INPUT_BIN) \
	$(TEST_TERMINAL_BIN) $(TEST_APP_BIN)
TEST_OBJ := $(OBJDIR)/tests/test_game.o $(OBJDIR)/tests/test_cli.o \
	$(OBJDIR)/tests/test_ui.o $(OBJDIR)/tests/test_input.o \
	$(OBJDIR)/tests/test_terminal.o $(OBJDIR)/tests/test_app.o
TEST_DEPS := $(TEST_OBJ:.o=.d)
DEPFLAGS = -MMD -MP

.PHONY: all clean check check-sanitize install uninstall

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

$(TEST_GAME_BIN): $(OBJDIR)/game.o $(OBJDIR)/tests/test_game.o
	$(CC) $(CPPFLAGS) $(ALL_CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(TEST_CLI_BIN): $(OBJDIR)/cli.o $(OBJDIR)/tests/test_cli.o
	$(CC) $(CPPFLAGS) $(ALL_CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(TEST_UI_BIN): $(OBJDIR)/game.o $(OBJDIR)/ui.o $(OBJDIR)/tests/test_ui.o
	$(CC) $(CPPFLAGS) $(ALL_CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(TEST_INPUT_BIN): $(OBJDIR)/input.o $(OBJDIR)/tests/test_input.o
	$(CC) $(CPPFLAGS) $(ALL_CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(TEST_TERMINAL_BIN): $(OBJDIR)/terminal.o $(OBJDIR)/tests/test_terminal.o
	$(CC) $(CPPFLAGS) $(ALL_CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(TEST_APP_BIN): $(OBJDIR)/app.o $(OBJDIR)/cli.o $(OBJDIR)/game.o \
	$(OBJDIR)/input.o $(OBJDIR)/terminal.o $(OBJDIR)/ui.o \
	$(OBJDIR)/tests/test_app.o
	$(CC) $(CPPFLAGS) $(ALL_CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

check: cnake $(TEST_BINS)
	./$(TEST_GAME_BIN)
	./$(TEST_CLI_BIN)
	./$(TEST_UI_BIN)
	./$(TEST_INPUT_BIN)
	./$(TEST_TERMINAL_BIN)
	./$(TEST_APP_BIN)
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
