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
#define STATUS_DEBUG_GAP_WIDTH 3

#define COLOR_RED "\x1B[91m"
#define COLOR_GREEN "\x1B[92m"
#define COLOR_YELLOW "\x1B[93m"
#define COLOR_BLUE "\x1B[94m"
#define COLOR_MAGENTA "\x1B[95m"
#define COLOR_CYAN "\x1B[96m"
#define STYLE_DIM "\x1B[2m"
#define STYLE_RESET "\033[0m"

#define CLEAR_SCREEN "\033[H\033[0J"
#define CLEAR_LINE "\033[2K"
#define CURSOR_UP_ONE_LINE "\033[1A"
#define WARNING_LABEL_TEXT " ! Warning:"
#define UNICODE_ERROR_LABEL_TEXT " × Error:"
#define ASCII_ERROR_LABEL_TEXT " x Error:"

struct ui_style {
  const char *red;
  const char *green;
  const char *yellow;
  const char *blue;
  const char *magenta;
  const char *cyan;
  const char *dim;
  const char *reset;
};

static const struct ui_style ANSI_STYLE = {
    .red = COLOR_RED,
    .green = COLOR_GREEN,
    .yellow = COLOR_YELLOW,
    .blue = COLOR_BLUE,
    .magenta = COLOR_MAGENTA,
    .cyan = COLOR_CYAN,
    .dim = STYLE_DIM,
    .reset = STYLE_RESET,
};

static const struct ui_style MONOCHROME_STYLE = {
    .red = "",
    .green = "",
    .yellow = "",
    .blue = "",
    .magenta = "",
    .cyan = "",
    .dim = "",
    .reset = "",
};

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
  const char *score;
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
    .score = "★",
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
    .score = "+",
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
};

static struct termios original_terminal;
static bool terminal_initialized = false;
static bool monochrome_output = false;
static volatile sig_atomic_t *interrupted_flag = NULL;

enum { COMMAND_READ_ESCAPE = 2 };

static const struct ui_theme *theme_for_mode(enum ui_mode mode) {
  return mode == UI_MODE_ASCII ? &ASCII_THEME : &UNICODE_THEME;
}

static const struct ui_theme *theme_for_ui(const struct ui_state *ui) {
  return theme_for_mode(ui->mode);
}

static const struct ui_style *style_for_monochrome(bool monochrome) {
  return monochrome ? &MONOCHROME_STYLE : &ANSI_STYLE;
}

static const struct ui_style *style_for_ui(const struct ui_state *ui) {
  return style_for_monochrome(ui->monochrome);
}

static void print_repeated(const char *text, int count) {
  for (int index = 0; index < count; index++)
    printf("%s", text);
}

static void print_command_prompt(const struct ui_theme *theme,
                                 const struct ui_style *style) {
  printf("%s %s %s", style->cyan, theme->prompt, style->reset);
}

static int board_outer_width(int board_size) { return 2 * board_size + 3; }

static int game_layout_width(int board_size) {
  return board_outer_width(board_size) + BOARD_STATUS_GAP_WIDTH +
         STATUS_INNER_WIDTH + 2;
}

/* Count decimal digits in an unsigned integer for width calculations. */
static int decimal_digit_count(unsigned int value) {
  int digits = 1;

  while (value >= 10) {
    value /= 10;
    digits++;
  }

  return digits;
}

/*
 * Compute the rendered title width for framed panels. Titles can optionally
 * include board dimensions (for example "STATUS · 8×8").
 */
static int panel_title_width(const char *label, int board_size,
                             bool include_size) {
  int title_width = (int)strlen(label) + 2;

  if (!include_size)
    return title_width;

  /* " LABEL SEP NxN " => label + spaces/separators + two board-size numbers */
  int size_digits = decimal_digit_count((unsigned int)board_size);
  return title_width + 4 + (2 * size_digits);
}

/*
 * Find the maximum signed integer width needed for debug cells by scanning all
 * board values, including space for a minus sign when necessary.
 */
static int debug_row_value_width(const struct game_state *game) {
  unsigned int max_abs_value = 0;
  bool has_negative = false;

  for (int y = 0; y < game->board_size; y++) {
    for (int x = 0; x < game->board_size; x++) {
      int value = game_board_tile(game, x, y);
      long magnitude = value < 0 ? -(long)value : (long)value;
      if (magnitude > (long)max_abs_value)
        max_abs_value = (unsigned int)magnitude;

      if (value < 0)
        has_negative = true;
    }
  }

  int value_width = decimal_digit_count(max_abs_value);
  if (has_negative)
    value_width++;

  return value_width;
}

/* Return the text width of one debug row with fixed-width, space-separated cells. */
static int debug_row_text_width(int board_size, int value_width) {
  if (board_size <= 0)
    return 0;

  return board_size * value_width + (board_size - 1);
}

/* Choose a debug panel width that fits both title and widest numeric row. */
static int debug_panel_inner_width(int board_size, int value_width) {
  int row_width = debug_row_text_width(board_size, value_width);
  int title_width = panel_title_width("DEBUG", board_size, false);
  int minimum_width = title_width + 2;
  int panel_width = row_width + 2;

  return panel_width > minimum_width ? panel_width : minimum_width;
}

/* Render a panel top border with centered title text. */
static void print_panel_top(int inner_width, const char *label, int board_size,
                            bool include_size, const struct ui_theme *theme,
                            const struct ui_style *style) {
  int title_width = panel_title_width(label, board_size, include_size);
  int fill_width = inner_width - title_width;
  int left_fill = fill_width / 2;
  int right_fill = fill_width - left_fill;

  printf("%s%s%s", style->dim, style->cyan, theme->top_left);
  print_repeated(theme->horizontal, left_fill);

  if (include_size) {
    printf(" %s %s %d%s%d ", label, theme->separator, board_size,
           theme->multiply, board_size);
  } else {
    printf(" %s ", label);
  }

  print_repeated(theme->horizontal, right_fill);
  printf("%s", theme->top_right);
}

/* Render a panel bottom border with the given inner width. */
static void print_panel_bottom(int inner_width, const struct ui_theme *theme,
                               const struct ui_style *style) {
  printf("%s%s%s", style->dim, style->cyan, theme->bottom_left);
  print_repeated(theme->horizontal, inner_width);
  printf("%s", theme->bottom_right);
}

void ui_initialize_state(struct ui_state *ui, enum ui_mode mode,
                         bool monochrome) {
  ui->show_debug = false;
  ui->monochrome = monochrome;
  ui->mode = mode;
  monochrome_output = monochrome;
}

void ui_set_interrupted_flag(volatile sig_atomic_t *interrupted) {
  interrupted_flag = interrupted;
}

const char *ui_error_label(enum ui_mode mode, bool monochrome) {
  if (mode == UI_MODE_ASCII) {
    return monochrome ? ASCII_ERROR_LABEL_TEXT
                      : COLOR_RED ASCII_ERROR_LABEL_TEXT STYLE_RESET;
  } else {
    return monochrome ? UNICODE_ERROR_LABEL_TEXT
                      : COLOR_RED UNICODE_ERROR_LABEL_TEXT STYLE_RESET;
  }
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

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_terminal) == -1) {
    const struct ui_style *style = style_for_monochrome(monochrome_output);
    fprintf(stderr, "%s%s%s Unable to restore terminal settings.\n",
            style->yellow, WARNING_LABEL_TEXT, style->reset);
  }

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
  const struct ui_style *style = style_for_ui(ui);

  while (true) {
    print_command_prompt(theme, style);
    printf("Board size [%d-%d] %s(default %d)%s: ", MIN_BOARD_SIZE,
           MAX_BOARD_SIZE, style->dim, DEFAULT_BOARD_SIZE, style->reset);
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
             ui_error_label(ui->mode, ui->monochrome), MIN_BOARD_SIZE,
             MAX_BOARD_SIZE, DEFAULT_BOARD_SIZE);
    } else {
      printf("%s Board size must be between %d and %d.\n",
             ui_error_label(ui->mode, ui->monochrome), MIN_BOARD_SIZE,
             MAX_BOARD_SIZE);
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
  const struct ui_style *style = style_for_ui(ui);
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

  printf("%s%s %s", CLEAR_SCREEN, style->cyan, theme->top_left);
  print_repeated(theme->horizontal, left_fill);

  printf(" %s", style->green);
  for (size_t index = 0; index < sizeof(snake) / sizeof(*snake); index++)
    printf("%s", snake[index]);

  printf(" CNAKE %s(%s)%s%s ", style->dim, seed_text, style->reset,
         style->cyan);

  print_repeated(theme->horizontal, right_fill);
  printf("%s\n %s", theme->top_right, theme->bottom_left);
  print_repeated(theme->horizontal, header_inner_width);
  printf("%s\n\n%s", theme->bottom_right, style->reset);
}

static void draw_border(int board_size, bool bottom,
                        const struct ui_theme *theme,
                        const struct ui_style *style) {
  const char *left = bottom ? theme->bottom_left : theme->top_left;
  const char *right = bottom ? theme->bottom_right : theme->top_right;

  printf("%s%s %s", style->dim, style->cyan, left);
  print_repeated(theme->horizontal, board_outer_width(board_size) - 2);
  printf("%s%s", right, style->reset);
}

static void print_tile(int tile, enum direction direction,
                       const struct ui_theme *theme,
                       const struct ui_style *style) {
  if (tile > TILE_EMPTY) {
    printf("%s%s %s", style->green, tile == 1 ? theme->tail : theme->body,
           style->reset);
    return;
  }

  switch (tile) {
  case TILE_EMPTY:
    printf("%s%s %s", style->dim, theme->empty, style->reset);
    break;
  case TILE_HEAD:
    printf("%s%s %s", style->green, head_glyph(direction, theme), style->reset);
    break;
  case TILE_OBSTACLE:
    printf("%s%s %s", style->blue, theme->obstacle, style->reset);
    break;
  case TILE_SNACK:
    printf("%s%s %s", style->yellow, theme->snack, style->reset);
    break;
  case TILE_POISON:
    printf("%s%s %s", style->magenta, theme->poison, style->reset);
    break;
  }
}

static void print_status_row(const struct game_state *game, int row,
                             const struct ui_theme *theme,
                             const struct ui_style *style) {
  print_repeated(" ", BOARD_STATUS_GAP_WIDTH);
  printf("%s%s%s%s", style->dim, style->cyan, theme->vertical, style->reset);

  switch (row) {
  case 0: {
    int score = game_score(game);
    printf(" %s%s%s   Score ", style->yellow, theme->score, style->reset);
    if (score == 42)
      printf("%s%8d%s ", style->yellow, score, style->reset);
    else
      printf("%8d ", score);
    break;
  }
  case 1:
    printf(" %s%s%s%s%s Snake     %4d ", style->green, theme->body,
           theme->body, theme->head_right, style->reset, game->length);
    break;
  case 2:
    printf(" %s%s%s   Snack     %4d ", style->yellow, theme->snack,
           style->reset, game->stats.snacks);
    break;
  case 3:
    printf(" %s%s%s   Poison    %4d ", style->magenta, theme->poison,
           style->reset, game->stats.poison);
    break;
  case 4:
    printf(" %s%s%s   Obstacle       ", style->blue, theme->obstacle,
           style->reset);
    break;
  default:
    printf("%*s", STATUS_INNER_WIDTH, "");
    break;
  }

  printf("%s%s%s%s", style->dim, style->cyan, theme->vertical, style->reset);
}

/* Render one debug panel row with width-aligned integer cells. */
static void print_debug_row(const struct game_state *game, int row,
                            int value_width, int panel_inner_width,
                            const struct ui_theme *theme,
                            const struct ui_style *style) {
  int row_width = debug_row_text_width(game->board_size, value_width);
  int right_padding = panel_inner_width - row_width - 2;

  print_repeated(" ", STATUS_DEBUG_GAP_WIDTH);
  printf("%s%s%s%s ", style->dim, style->cyan, theme->vertical, style->reset);

  for (int x = 0; x < game->board_size; x++) {
    if (x > 0)
      printf(" ");
    printf("%*d", value_width, game_board_tile(game, x, row));
  }

  print_repeated(" ", right_padding);
  printf(" %s%s%s%s", style->dim, style->cyan, theme->vertical, style->reset);
}

/*
 * Draw the board with fixed-width status and optional debug panels.
 */
void ui_draw_game_board(const struct game_state *game,
                        const struct ui_state *ui) {
  const struct ui_theme *theme = theme_for_ui(ui);
  const struct ui_style *style = style_for_ui(ui);
  int debug_value_width = 0;
  int debug_inner_width = 0;

  if (ui->show_debug) {
    debug_value_width = debug_row_value_width(game);
    debug_inner_width = debug_panel_inner_width(game->board_size,
                                                debug_value_width);
  }

  draw_border(game->board_size, false, theme, style);
  print_repeated(" ", BOARD_STATUS_GAP_WIDTH);
  print_panel_top(STATUS_INNER_WIDTH, "STATUS", game->board_size, true, theme,
                  style);

  if (ui->show_debug) {
    print_repeated(" ", STATUS_DEBUG_GAP_WIDTH);
    print_panel_top(debug_inner_width, "DEBUG", game->board_size, false, theme,
                    style);
  }

  printf("\n%s", style->reset);

  for (int y = 0; y < game->board_size; y++) {
    printf("%s%s %s %s", style->dim, style->cyan, theme->vertical,
           style->reset);

    for (int x = 0; x < game->board_size; x++)
      print_tile(game_board_tile(game, x, y), game->direction, theme, style);

    printf("%s%s%s%s", style->dim, style->cyan, theme->vertical, style->reset);
    print_status_row(game, y, theme, style);

    if (ui->show_debug)
      print_debug_row(game, y, debug_value_width, debug_inner_width, theme,
                      style);

    printf("\n");
  }

  draw_border(game->board_size, true, theme, style);
  print_repeated(" ", BOARD_STATUS_GAP_WIDTH);
  print_panel_bottom(STATUS_INNER_WIDTH, theme, style);

  if (ui->show_debug) {
    print_repeated(" ", STATUS_DEBUG_GAP_WIDTH);
    print_panel_bottom(debug_inner_width, theme, style);
  }

  printf("\n%s", style->reset);
}

static void print_game_status_message(enum game_status status,
                                      const struct ui_state *ui) {
  const struct ui_theme *theme = theme_for_ui(ui);
  const struct ui_style *style = style_for_ui(ui);

  switch (status) {
  case GAME_WON:
    printf("%s %s YOU WIN! %s%s", style->green, theme->snack, theme->snack,
           style->reset);
    return;
  case GAME_OVER_SNAKE:
    printf("%s %s%s%s%s Snake! ", style->green, theme->body, theme->body,
           theme->head_right, style->reset);
    break;
  case GAME_OVER_OBSTACLE:
    printf("%s %s%s Obstacle! ", style->blue, theme->obstacle, style->reset);
    break;
  case GAME_OVER_POISON:
    printf("%s %s%s Poison! ", style->magenta, theme->poison, style->reset);
    break;
  case GAME_RUNNING:
    return;
  }

  printf("%s%s GAME OVER! %s%s", style->red, theme->game_over_dash,
         theme->game_over_dash, style->reset);
}

static void print_help_option(const struct ui_theme *theme, const char *command,
                              const char *description,
                              const struct ui_style *style) {
  printf(" %s %s%-2s%s%-27s%s%s\n", theme->vertical, style->yellow, command,
         style->reset, description, style->cyan, theme->vertical);
}

static void print_help(const struct ui_state *ui) {
  const struct ui_theme *theme = theme_for_ui(ui);
  const struct ui_style *style = style_for_ui(ui);

  printf(" %s%s", style->cyan, theme->top_left);
  print_repeated(theme->horizontal, 10);
  printf(" CONTROLS ");
  print_repeated(theme->horizontal, 10);
  printf("%s\n", theme->top_right);
  printf(" %s %s          %sk%s %s%s                %s%s\n", theme->vertical,
         style->reset, style->yellow, style->green, theme->head_up,
         style->reset, style->cyan, theme->vertical);
  printf(" %s %s      %sh%s %s     %s %sl%s            %s%s\n",
         theme->vertical, style->reset, style->yellow, style->green,
         theme->head_left, theme->head_right, style->yellow, style->reset,
         style->cyan, theme->vertical);
  printf(" %s %s          %sj%s %s%s                %s%s\n", theme->vertical,
         style->reset, style->yellow, style->green, theme->head_down,
         style->reset, style->cyan, theme->vertical);
  printf(" %s", theme->middle_left);
  print_repeated(theme->horizontal, 30);
  printf("%s\n", theme->middle_right);
  print_help_option(theme, "?", "Show help", style);
  print_help_option(theme, "v", "Toggle debug view", style);
  print_help_option(theme, "c", "Add five segments", style);
  print_help_option(theme, "q", "Quit", style);
  printf(" %s", theme->bottom_left);
  print_repeated(theme->horizontal, 30);
  printf("%s\n%s", theme->bottom_right, style->reset);
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

enum input_prompt {
  INPUT_PROMPT_COMMAND = 0,
  INPUT_PROMPT_REPLAY,
  INPUT_PROMPT_HELP,
};

enum input_message {
  INPUT_MESSAGE_NONE = 0,
  INPUT_MESSAGE_FEEDBACK,
  INPUT_MESSAGE_GAME_STATUS,
  INPUT_MESSAGE_INVALID_COMMAND,
};

static void print_input_prompt(enum input_prompt prompt,
                               const struct ui_theme *theme,
                               const struct ui_style *style) {
  print_command_prompt(theme, style);

  if (prompt == INPUT_PROMPT_COMMAND) {
    printf("Command %s[?]%s: ", style->dim, style->reset);
  } else if (prompt == INPUT_PROMPT_REPLAY) {
    printf("Retry %s[r]%s / Quit %s[q]%s: ", style->dim, style->reset,
           style->dim, style->reset);
  } else {
    printf("Return %s[any key]%s / Quit %s[q]%s: ", style->dim, style->reset,
           style->dim, style->reset);
  }
}

/*
 * Keep one message row above one prompt row. On retries, move back to the
 * message row and replace both rows instead of appending terminal output.
 */
static void render_input_area(const struct ui_state *ui,
                              enum input_prompt prompt,
                              enum input_message message,
                              const char *feedback, enum game_status status,
                              bool replace) {
  const struct ui_theme *theme = theme_for_ui(ui);
  const struct ui_style *style = style_for_ui(ui);

  if (replace)
    printf("\r%s", CURSOR_UP_ONE_LINE);

  printf("\r%s", CLEAR_LINE);

  switch (message) {
  case INPUT_MESSAGE_FEEDBACK:
    printf("%s", feedback);
    break;
  case INPUT_MESSAGE_GAME_STATUS:
    print_game_status_message(status, ui);
    break;
  case INPUT_MESSAGE_INVALID_COMMAND:
    printf("%s %s%s ", style->red,
           ui->mode == UI_MODE_ASCII ? "x" : "×", style->reset);
    if (prompt == INPUT_PROMPT_COMMAND)
      printf("Unknown command. Press ? for help.");
    else
      printf("Choose r to retry or q to quit.");
    break;
  case INPUT_MESSAGE_NONE:
    break;
  }

  printf("\r\n%s\r", CLEAR_LINE);
  print_input_prompt(prompt, theme, style);
  fflush(stdout);
}

/*
 * Display one-shot feedback, prompt for a command, and translate a successfully
 * read byte into a semantic player command.
 */
int ui_prompt_for_command(const struct ui_state *ui,
                          enum player_command *command, const char **feedback) {
  enum input_message message =
      *feedback == NULL ? INPUT_MESSAGE_NONE : INPUT_MESSAGE_FEEDBACK;

  render_input_area(ui, INPUT_PROMPT_COMMAND, message, *feedback, GAME_RUNNING,
                    false);
  *feedback = NULL;

  while (true) {
    char input;
    int status = read_command_char(&input);

    if (status == COMMAND_READ_ESCAPE)
      continue;

    if (status != COMMAND_READ_OK)
      return status;

    *command = parse_player_command(input);
    if (*command != PLAYER_COMMAND_INVALID)
      return COMMAND_READ_OK;

    render_input_area(ui, INPUT_PROMPT_COMMAND,
                      INPUT_MESSAGE_INVALID_COMMAND, NULL, GAME_RUNNING, true);
  }
}

int ui_prompt_for_replay(const struct ui_state *ui, enum game_status game_status,
                         enum replay_command *command) {
  render_input_area(ui, INPUT_PROMPT_REPLAY, INPUT_MESSAGE_GAME_STATUS, NULL,
                    game_status, false);

  while (true) {
    char input;
    int status = read_command_char(&input);

    if (status == COMMAND_READ_ESCAPE)
      continue;

    if (status != COMMAND_READ_OK)
      return status;

    if (input == 'r')
      *command = REPLAY_COMMAND_RETRY;
    else if (input == 'q')
      *command = REPLAY_COMMAND_QUIT;
    else {
      render_input_area(ui, INPUT_PROMPT_REPLAY,
                        INPUT_MESSAGE_INVALID_COMMAND, NULL, GAME_RUNNING,
                        true);
      continue;
    }

    return COMMAND_READ_OK;
  }
}

int ui_show_help(const struct ui_state *ui, bool *quit) {
  printf("%s", CLEAR_SCREEN);
  print_help(ui);
  render_input_area(ui, INPUT_PROMPT_HELP, INPUT_MESSAGE_NONE, NULL,
                    GAME_RUNNING, false);

  while (true) {
    char input;
    int status = read_stdin_char(&input, -1);

    if (status != COMMAND_READ_OK)
      return status;

    if (input == '\033') {
      discard_escape_sequence();
      continue;
    }

    *quit = input == 'q';
    return COMMAND_READ_OK;
  }
}

const char *ui_toggle_debug(struct ui_state *ui) {
  ui->show_debug = !ui->show_debug;
  // clang-format off
  if (ui->mode == UI_MODE_ASCII) {
    return ui->show_debug
               ? (ui->monochrome ? " * Debug view on." : COLOR_CYAN " * " STYLE_RESET "Debug view on.")
               : (ui->monochrome ? " * Debug view off." : COLOR_CYAN " * " STYLE_RESET "Debug view off.");
  } else {
    return ui->show_debug
               ? (ui->monochrome ? " ◆ Debug view on." : COLOR_CYAN " ◆ " STYLE_RESET "Debug view on.")
               : (ui->monochrome ? " ◆ Debug view off." : COLOR_CYAN " ◆ " STYLE_RESET "Debug view off.");
  }
  // clang-format on
}

const char *ui_grow_feedback(const struct ui_state *ui) {
  return ui->monochrome ? " ! Length +5."
                        : COLOR_YELLOW " ! " STYLE_RESET "Length +5.";
}

const char *ui_answer_feedback(const struct ui_state *ui) {
  if (ui->mode == UI_MODE_ASCII)
    return ui->monochrome
               ? " + The answer is snake."
               : COLOR_YELLOW " * " STYLE_RESET "The answer is snake.";

  return ui->monochrome
             ? " ★ The answer is snake."
             : COLOR_YELLOW " ★ " STYLE_RESET "The answer is snake.";
}

void ui_print_interrupted_exit(const struct ui_state *ui) {
  const struct ui_style *style = style_for_ui(ui);
  printf("%s\n\n", style->reset);
}
