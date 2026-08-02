#include "ui.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#define BOARD_STATUS_GAP_WIDTH 3
#define STATUS_INNER_WIDTH 20

#define COLOR_RED "\x1B[91m"
#define COLOR_GREEN "\x1B[92m"
#define COLOR_YELLOW "\x1B[93m"
#define COLOR_BLUE "\x1B[94m"
#define COLOR_MAGENTA "\x1B[95m"
#define COLOR_CYAN "\x1B[96m"
#define STYLE_DIM "\x1B[2m"
#define COLOR_RESET "\033[0m"

#define WARNING_LABEL COLOR_YELLOW " ! Warning:" COLOR_RESET
#define CLEAR_SCREEN "\033[H\033[0J"

struct ui_theme {
  const char *empty;
  const char *body;
  const char *tail;
  const char *head_up;
  const char *head_down;
  const char *head_left;
  const char *head_right;
  const char *obstacle;
  const char *snack;
  const char *poison;
  const char *top_left;
  const char *top_right;
  const char *bottom_left;
  const char *bottom_right;
  const char *middle_left;
  const char *middle_right;
  const char *horizontal;
  const char *vertical;
  const char *prompt;
  const char *separator;
  const char *multiply;
  const char *game_over_dash;
  const char *debug_on_feedback;
  const char *debug_off_feedback;
  const char *error_label;
};

static const struct ui_theme UNICODE_THEME = {
    .empty = "·",
    .body = "●",
    .tail = "○",
    .head_up = "▲",
    .head_down = "▼",
    .head_left = "◀",
    .head_right = "▶",
    .obstacle = "▣",
    .snack = "✦",
    .poison = "×",
    .top_left = "╭",
    .top_right = "╮",
    .bottom_left = "╰",
    .bottom_right = "╯",
    .middle_left = "├",
    .middle_right = "┤",
    .horizontal = "─",
    .vertical = "│",
    .prompt = "❯",
    .separator = "·",
    .multiply = "×",
    .game_over_dash = "—",
    .debug_on_feedback = COLOR_CYAN " ◆ " COLOR_RESET "Debug view on",
    .debug_off_feedback = COLOR_CYAN " ◆ " COLOR_RESET "Debug view off",
    .error_label = COLOR_RED " × Error:" COLOR_RESET,
};

static const struct ui_theme ASCII_THEME = {
    .empty = ".",
    .body = "=",
    .tail = "=",
    .head_up = "^",
    .head_down = "v",
    .head_left = "<",
    .head_right = ">",
    .obstacle = "#",
    .snack = "*",
    .poison = "!",
    .top_left = "+",
    .top_right = "+",
    .bottom_left = "+",
    .bottom_right = "+",
    .middle_left = "+",
    .middle_right = "+",
    .horizontal = "-",
    .vertical = "|",
    .prompt = ">",
    .separator = ".",
    .multiply = "x",
    .game_over_dash = "-",
    .debug_on_feedback = COLOR_CYAN " * " COLOR_RESET "Debug view on",
    .debug_off_feedback = COLOR_CYAN " * " COLOR_RESET "Debug view off",
    .error_label = COLOR_RED " x Error:" COLOR_RESET,
};

static struct termios original_terminal;
static bool terminal_initialized = false;
static volatile sig_atomic_t *interrupted_flag = NULL;

static const struct ui_theme *theme_for_mode(enum ui_mode mode) {
  return mode == UI_MODE_ASCII ? &ASCII_THEME : &UNICODE_THEME;
}

static const struct ui_theme *theme_for_ui(const struct ui_state *ui) {
  return theme_for_mode(ui->mode);
}

static void print_repeated(const char *text, int count) {
  for (int index = 0; index < count; index++)
    printf("%s", text);
}

static void print_command_prompt(const struct ui_theme *theme) {
  printf(COLOR_CYAN " %s " COLOR_RESET, theme->prompt);
}

static int board_outer_width(int board_size) { return 2 * board_size + 3; }

static int game_layout_width(int board_size) {
  return board_outer_width(board_size) + BOARD_STATUS_GAP_WIDTH +
         STATUS_INNER_WIDTH + 2;
}

void ui_initialize_state(struct ui_state *ui, enum ui_mode mode) {
  ui->show_debug = false;
  ui->mode = mode;
}

void ui_set_interrupted_flag(volatile sig_atomic_t *interrupted) {
  interrupted_flag = interrupted;
}

const char *ui_error_label(enum ui_mode mode) {
  return theme_for_mode(mode)->error_label;
}

static sig_atomic_t interrupted_value(void) {
  return interrupted_flag != NULL ? *interrupted_flag : 0;
}

bool ui_initialize_terminal(void) {
  if (!isatty(STDIN_FILENO))
    return false;

  if (tcgetattr(STDIN_FILENO, &original_terminal) == -1)
    return false;

  struct termios terminal = original_terminal;
  terminal.c_lflag &= ~(ICANON | ECHO);
  terminal.c_cc[VMIN] = 1;
  terminal.c_cc[VTIME] = 0;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &terminal) == -1)
    return false;

  terminal_initialized = true;
  return true;
}

void ui_restore_terminal(void) {
  if (!terminal_initialized)
    return;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_terminal) == -1)
    fprintf(stderr, WARNING_LABEL " Unable to restore terminal settings.\n");

  terminal_initialized = false;
}

/*
 * Read one byte from standard input. A negative timeout blocks indefinitely.
 * Returns 1 for input, 0 for a timeout/end/interruption, and -1 for an error.
 */
static int read_stdin_char(char *input, int timeout_ms) {
  while (true) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(STDIN_FILENO, &read_fds);

    int ready;
    if (timeout_ms < 0) {
      ready = select(STDIN_FILENO + 1, &read_fds, NULL, NULL, NULL);
    } else {
      struct timeval timeout = {
          .tv_sec = timeout_ms / 1000,
          .tv_usec = (timeout_ms % 1000) * 1000,
      };
      ready = select(STDIN_FILENO + 1, &read_fds, NULL, NULL, &timeout);
    }

    if (ready == 0)
      return COMMAND_READ_EOF;

    if (ready == -1) {
      if (errno == EINTR) {
        if (interrupted_value())
          return COMMAND_READ_EOF;

        continue;
      }

      return COMMAND_READ_ERROR;
    }

    ssize_t bytes_read = read(STDIN_FILENO, input, 1);
    if (bytes_read == 1)
      return COMMAND_READ_OK;

    if (bytes_read == 0)
      return COMMAND_READ_EOF;

    if (errno == EINTR) {
      if (interrupted_value())
        return COMMAND_READ_EOF;

      continue;
    }

    return COMMAND_READ_ERROR;
  }
}

/*
 * Consume an unsupported terminal escape sequence without delaying normal
 * single-key commands.
 */
static void discard_escape_sequence(void) {
  char input;
  int status = read_stdin_char(&input, 10);

  if (status != COMMAND_READ_OK)
    return;

  if (input != '[' && input != 'O') {
    while (read_stdin_char(&input, 5) == COMMAND_READ_OK)
      ;

    return;
  }

  do {
    status = read_stdin_char(&input, 5);
  } while (status == COMMAND_READ_OK && (input < '@' || input > '~'));
}

/*
 * Read the next non-newline command byte. Return value 2 means that an escape
 * sequence was consumed and the caller should prompt again.
 */
static int read_command_char(char *input) {
  int status;

  do {
    status = read_stdin_char(input, -1);
    if (status != COMMAND_READ_OK)
      return status;
  } while (*input == '\n' || *input == '\r');

  if (*input == '\033') {
    discard_escape_sequence();
    return COMMAND_READ_ESCAPE;
  }

  return COMMAND_READ_OK;
}

enum board_size_parse_status ui_parse_board_size(const char *input,
                                                 int *board_size) {
  char *end;

  errno = 0;
  long parsed_size = strtol(input, &end, 10);

  if (errno != 0 || end == input)
    return BOARD_SIZE_INVALID;

  while (*end != '\0' && isspace((unsigned char)*end))
    end++;

  if (*end != '\0')
    return BOARD_SIZE_INVALID;

  if (parsed_size < MIN_BOARD_SIZE || parsed_size > MAX_BOARD_SIZE)
    return BOARD_SIZE_OUT_OF_RANGE;

  *board_size = (int)parsed_size;
  return BOARD_SIZE_VALID;
}

int ui_prompt_for_board_size(const struct ui_state *ui, int *board_size) {
  const struct ui_theme *theme = theme_for_ui(ui);

  while (true) {
    print_command_prompt(theme);
    printf("Board size [%d-%d] " STYLE_DIM "(default %d)" COLOR_RESET ": ",
           MIN_BOARD_SIZE, MAX_BOARD_SIZE, DEFAULT_BOARD_SIZE);
    fflush(stdout);

    char input[32];
    if (!fgets(input, sizeof(input), stdin)) {
      if (interrupted_value()) {
        putchar('\n');
        return 128 + interrupted_value();
      }

      return 1;
    }

    if (strchr(input, '\n') == NULL) {
      int discarded;
      while ((discarded = getchar()) != '\n' && discarded != EOF)
        ;
    }

    if (input[0] == '\n') {
      *board_size = DEFAULT_BOARD_SIZE;
      return 0;
    }

    enum board_size_parse_status status =
        ui_parse_board_size(input, board_size);

    if (status == BOARD_SIZE_VALID)
      return 0;

    if (status == BOARD_SIZE_INVALID) {
      printf("%s Enter a number from %d to %d, or press Enter for %d.\n",
             theme->error_label, MIN_BOARD_SIZE, MAX_BOARD_SIZE,
             DEFAULT_BOARD_SIZE);
    } else {
      printf("%s Board size must be between %d and %d.\n", theme->error_label,
             MIN_BOARD_SIZE, MAX_BOARD_SIZE);
    }
  }
}

static const char *head_glyph(enum direction direction,
                              const struct ui_theme *theme) {
  switch (direction) {
  case DIRECTION_UP:
    return theme->head_up;
  case DIRECTION_DOWN:
    return theme->head_down;
  case DIRECTION_LEFT:
    return theme->head_left;
  case DIRECTION_RIGHT:
    return theme->head_right;
  }

  return theme->head_right;
}

void ui_print_header(unsigned int seed, int board_size,
                     const struct ui_state *ui) {
  const struct ui_theme *theme = theme_for_ui(ui);
  const char *const snake[] = {theme->body, theme->body, theme->head_right};
  char seed_text[sizeof(seed) * 3 + 3];
  int seed_width =
      snprintf(seed_text, sizeof(seed_text), "%u", (unsigned int)seed);
  int title_width =
      (int)(sizeof(snake) / sizeof(*snake) + sizeof("  CNAKE () ") - 1) +
      seed_width;
  int header_inner_width = game_layout_width(board_size) - 2;
  int fill_width = header_inner_width - title_width;
  int left_fill = fill_width / 2;
  int right_fill = fill_width - left_fill;

  printf(CLEAR_SCREEN COLOR_CYAN " %s", theme->top_left);
  print_repeated(theme->horizontal, left_fill);

  printf(" " COLOR_GREEN);
  for (size_t index = 0; index < sizeof(snake) / sizeof(*snake); index++)
    printf("%s", snake[index]);

  printf(" CNAKE " STYLE_DIM "(%s)" COLOR_RESET COLOR_CYAN " ", seed_text);

  print_repeated(theme->horizontal, right_fill);
  printf("%s\n %s", theme->top_right, theme->bottom_left);
  print_repeated(theme->horizontal, header_inner_width);
  printf("%s\n\n" COLOR_RESET, theme->bottom_right);
}

static void draw_border(int board_size, bool bottom,
                        const struct ui_theme *theme) {
  const char *left = bottom ? theme->bottom_left : theme->top_left;
  const char *right = bottom ? theme->bottom_right : theme->top_right;

  printf(STYLE_DIM COLOR_CYAN " %s", left);
  print_repeated(theme->horizontal, board_outer_width(board_size) - 2);
  printf("%s" COLOR_RESET, right);
}

static void print_tile(int tile, enum direction direction,
                       const struct ui_theme *theme) {
  if (tile > TILE_EMPTY) {
    printf(COLOR_GREEN "%s " COLOR_RESET,
           tile == 1 ? theme->tail : theme->body);
    return;
  }

  switch (tile) {
  case TILE_EMPTY:
    printf(STYLE_DIM "%s " COLOR_RESET, theme->empty);
    break;
  case TILE_HEAD:
    printf(COLOR_GREEN "%s " COLOR_RESET, head_glyph(direction, theme));
    break;
  case TILE_OBSTACLE:
    printf(COLOR_BLUE "%s " COLOR_RESET, theme->obstacle);
    break;
  case TILE_SNACK:
    printf(COLOR_YELLOW "%s " COLOR_RESET, theme->snack);
    break;
  case TILE_POISON:
    printf(COLOR_MAGENTA "%s " COLOR_RESET, theme->poison);
    break;
  }
}

static void print_status_row(const struct game_state *game, int row,
                             const struct ui_theme *theme) {
  print_repeated(" ", BOARD_STATUS_GAP_WIDTH);
  printf(STYLE_DIM COLOR_CYAN "%s" COLOR_RESET, theme->vertical);

  switch (row) {
  case 0:
    printf(" " COLOR_GREEN "%s%s%s" COLOR_RESET " Snake      %3d ", theme->body,
           theme->body, theme->head_right, game->length);
    break;
  case 1:
    printf(" " COLOR_YELLOW "%s" COLOR_RESET "   Snack      %3d ", theme->snack,
           game->stats.snacks);
    break;
  case 2:
    printf(" " COLOR_MAGENTA "%s" COLOR_RESET "   Poison     %3d ",
           theme->poison, game->stats.poison);
    break;
  case 3:
    printf(" " COLOR_BLUE "%s" COLOR_RESET "   Obstacle       ",
           theme->obstacle);
    break;
  default:
    printf("%*s", STATUS_INNER_WIDTH, "");
    break;
  }

  printf(STYLE_DIM COLOR_CYAN "%s\n" COLOR_RESET, theme->vertical);
}

/*
 * Draw the board and fixed-width status panel side by side, followed by the
 * optional numeric board representation used for debugging.
 */
void ui_draw_game_board(const struct game_state *game,
                        const struct ui_state *ui) {
  const struct ui_theme *theme = theme_for_ui(ui);
  int title_fill = game->board_size < 10 ? 4 : 2;

  draw_border(game->board_size, false, theme);
  print_repeated(" ", BOARD_STATUS_GAP_WIDTH);
  printf(STYLE_DIM COLOR_CYAN "%s", theme->top_left);
  print_repeated(theme->horizontal, 2);
  printf(" STATUS %s %d%s%d ", theme->separator, game->board_size,
         theme->multiply, game->board_size);
  print_repeated(theme->horizontal, title_fill);
  printf("%s\n" COLOR_RESET, theme->top_right);

  for (int y = 0; y < game->board_size; y++) {
    printf(STYLE_DIM COLOR_CYAN " %s " COLOR_RESET, theme->vertical);

    for (int x = 0; x < game->board_size; x++)
      print_tile(game_board_tile(game, x, y), game->direction, theme);

    printf(STYLE_DIM COLOR_CYAN "%s" COLOR_RESET, theme->vertical);
    print_status_row(game, y, theme);
  }

  draw_border(game->board_size, true, theme);
  print_repeated(" ", BOARD_STATUS_GAP_WIDTH);
  printf(STYLE_DIM COLOR_CYAN "%s", theme->bottom_left);
  print_repeated(theme->horizontal, STATUS_INNER_WIDTH);
  printf("%s\n" COLOR_RESET, theme->bottom_right);

  if (!ui->show_debug)
    return;

  printf("\n");
  for (int y = 0; y < game->board_size; y++) {
    printf("|");

    for (int x = 0; x < game->board_size; x++)
      printf("%2d ", game_board_tile(game, x, y));

    printf("|\n");
  }
}

void ui_print_game_status_message(enum game_status status,
                                  const struct ui_state *ui) {
  const struct ui_theme *theme = theme_for_ui(ui);

  switch (status) {
  case GAME_WON:
    printf(COLOR_GREEN "\n %s YOU WIN! %s\n" COLOR_RESET, theme->snack,
           theme->snack);
    return;
  case GAME_OVER_SNAKE:
    printf("\n Snake!" COLOR_GREEN " %s%s%s " COLOR_RESET, theme->body,
           theme->body, theme->head_right);
    break;
  case GAME_OVER_OBSTACLE:
    printf("\n Obstacle!" COLOR_BLUE " %s " COLOR_RESET, theme->obstacle);
    break;
  case GAME_OVER_POISON:
    printf("\n Poison!" COLOR_MAGENTA " %s " COLOR_RESET, theme->poison);
    break;
  case GAME_RUNNING:
    return;
  }

  printf(COLOR_RED "%s GAME OVER! %s\n" COLOR_RESET, theme->game_over_dash,
         theme->game_over_dash);
}

static void print_summary_row(const struct ui_theme *theme, const char *label,
                              int value) {
  printf(" %s " COLOR_RESET "%-14s%4d " COLOR_CYAN "%s\n", theme->vertical,
         label, value, theme->vertical);
}

void ui_print_game_summary(const struct game_state *game,
                           const struct ui_state *ui) {
  const struct ui_theme *theme = theme_for_ui(ui);

  printf("\n" COLOR_CYAN " %s", theme->top_left);
  print_repeated(theme->horizontal, 6);
  printf(" SCORE ");
  print_repeated(theme->horizontal, 7);
  printf("%s\n", theme->top_right);
  print_summary_row(theme, "Moves", game->stats.moves);
  print_summary_row(theme, "Snacks", game->stats.snacks);
  print_summary_row(theme, "Poison", game->stats.poison);
  printf(" %s " COLOR_RESET "%-10s%3d/%-3d+ " COLOR_CYAN "%s\n",
         theme->vertical, "Length", game->length, game_capacity(game),
         theme->vertical);
  printf(" %s", theme->bottom_left);
  print_repeated(theme->horizontal, 20);
  printf("%s\n\n" COLOR_RESET, theme->bottom_right);
}

static void print_help_option(const struct ui_theme *theme, const char *command,
                              const char *description) {
  printf("%s " COLOR_YELLOW "%-2s" COLOR_RESET "%-27s" COLOR_CYAN "%s\n",
         theme->vertical, command, description, theme->vertical);
}

void ui_print_help(const struct ui_state *ui) {
  const struct ui_theme *theme = theme_for_ui(ui);

  printf("\n\n" COLOR_CYAN "%s", theme->top_left);
  print_repeated(theme->horizontal, 10);
  printf(" CONTROLS ");
  print_repeated(theme->horizontal, 10);
  printf("%s\n", theme->top_right);
  printf("%s " COLOR_RESET "          " COLOR_YELLOW "k" COLOR_GREEN
         " %s" COLOR_RESET "                " COLOR_CYAN "%s\n",
         theme->vertical, theme->head_up, theme->vertical);
  printf("%s " COLOR_RESET "      " COLOR_YELLOW "h" COLOR_GREEN
         " %s     %s " COLOR_YELLOW "l" COLOR_RESET "            " COLOR_CYAN
         "%s\n",
         theme->vertical, theme->head_left, theme->head_right, theme->vertical);
  printf("%s " COLOR_RESET "          " COLOR_YELLOW "j" COLOR_GREEN
         " %s" COLOR_RESET "                " COLOR_CYAN "%s\n",
         theme->vertical, theme->head_down, theme->vertical);
  printf("%s", theme->middle_left);
  print_repeated(theme->horizontal, 30);
  printf("%s\n", theme->middle_right);
  print_help_option(theme, "?", "Show help");
  print_help_option(theme, "v", "Toggle debug view");
  print_help_option(theme, "c", "Add five segments");
  print_help_option(theme, "q", "Quit");
  printf("%s", theme->bottom_left);
  print_repeated(theme->horizontal, 30);
  printf("%s\n" COLOR_RESET, theme->bottom_right);
}

void ui_print_unknown_command(const struct ui_state *ui) {
  printf("\n%s Unknown command.\n", ui_error_label(ui->mode));
}

static enum player_command parse_player_command(char input) {
  switch (input) {
  case 'k':
    return PLAYER_COMMAND_MOVE_UP;
  case 'j':
    return PLAYER_COMMAND_MOVE_DOWN;
  case 'h':
    return PLAYER_COMMAND_MOVE_LEFT;
  case 'l':
    return PLAYER_COMMAND_MOVE_RIGHT;
  case '?':
    return PLAYER_COMMAND_HELP;
  case 'v':
    return PLAYER_COMMAND_TOGGLE_DEBUG;
  case 'c':
    return PLAYER_COMMAND_GROW;
  case 'q':
    return PLAYER_COMMAND_QUIT;
  default:
    return PLAYER_COMMAND_INVALID;
  }
}

/*
 * Display one-shot feedback, prompt for a command, and translate a successfully
 * read byte into a semantic player command.
 */
int ui_prompt_for_command(const struct ui_state *ui,
                          enum player_command *command, const char **feedback) {
  const struct ui_theme *theme = theme_for_ui(ui);

  printf("\n");

  if (*feedback != NULL) {
    printf("%s   ", *feedback);
    *feedback = NULL;
  }

  print_command_prompt(theme);
  printf("Command " STYLE_DIM "[?]" COLOR_RESET ": ");
  fflush(stdout);

  char input;
  int status = read_command_char(&input);

  if (status == COMMAND_READ_OK)
    *command = parse_player_command(input);

  return status;
}

const char *ui_toggle_debug(struct ui_state *ui) {
  ui->show_debug = !ui->show_debug;
  const struct ui_theme *theme = theme_for_ui(ui);
  return ui->show_debug ? theme->debug_on_feedback : theme->debug_off_feedback;
}

const char *ui_grow_feedback(void) {
  return COLOR_YELLOW " ! " COLOR_RESET "Length +5";
}

void ui_print_interrupted_exit(void) { printf(COLOR_RESET "\n\n"); }
