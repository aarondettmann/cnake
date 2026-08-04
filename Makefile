CPPFLAGS += -D_POSIX_C_SOURCE=200809L
CFLAGS += -std=c11 -O2 -Wall -Wextra -Wpedantic

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

SRC := src/main.c src/cli.c src/game.c src/ui.c
OBJ := $(SRC:src/%.c=$(OBJDIR)/%.o)
DEPS := $(OBJ:.o=.d)
TEST_GAME_BIN := $(OBJDIR)/test_game
TEST_CLI_BIN := $(OBJDIR)/test_cli
TEST_UI_BIN := $(OBJDIR)/test_ui
TEST_BINS := $(TEST_GAME_BIN) $(TEST_CLI_BIN) $(TEST_UI_BIN)
TEST_OBJ := $(OBJDIR)/tests/test_game.o $(OBJDIR)/tests/test_cli.o \
	$(OBJDIR)/tests/test_ui.o
TEST_DEPS := $(TEST_OBJ:.o=.d)
DEPFLAGS = -MMD -MP

.PHONY: all clean check install uninstall

all: cnake

cnake: $(OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

$(OBJDIR)/main.o: src/main.c VERSION
	@mkdir -p "$(dir $@)"
	$(CC) $(CPPFLAGS) $(VERSION_CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(OBJDIR)/%.o: src/%.c
	@mkdir -p "$(dir $@)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(OBJDIR)/tests/%.o: tests/%.c
	@mkdir -p "$(dir $@)"
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc $(DEPFLAGS) -c -o $@ $<

$(TEST_GAME_BIN): $(OBJDIR)/game.o $(OBJDIR)/tests/test_game.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(TEST_CLI_BIN): $(OBJDIR)/cli.o $(OBJDIR)/tests/test_cli.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(TEST_UI_BIN): $(OBJDIR)/game.o $(OBJDIR)/ui.o $(OBJDIR)/tests/test_ui.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

check: cnake $(TEST_BINS)
	./$(TEST_GAME_BIN)
	./$(TEST_CLI_BIN)
	./$(TEST_UI_BIN)
	bash -n completions/cnake
	test "$$(./cnake --version)" = "cnake $(CNAKE_VERSION)"
	./cnake --help | grep -F -- "--ascii" >/dev/null
	./cnake --help | grep -F -- "--monochrome" >/dev/null
	./cnake --unknown --help >/dev/null
	./cnake --version --unknown >/dev/null

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
