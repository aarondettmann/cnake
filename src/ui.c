#include "ui.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BOARD_STATUS_GAP_WIDTH 3
#define STATUS_INNER_WIDTH 20
#define STATUS_DEBUG_GAP_WIDTH 3

/* Filled, snake, score, snack and poison rows. */
#define STATUS_ROW_COUNT 5

/*
 * The filled row prints "<filled>/<total>" right-aligned in this field. Both
 * numbers hold at most three digits while a board has fewer than 1000 tiles,
 * which leaves "999/999" and one separating space.
 */
#define STATUS_FILLED_FIELD_WIDTH 8

static_assert(MAX_BOARD_SIZE * MAX_BOARD_SIZE < 1000,
              "the filled status row assumes at most three digits per number");

/* Width of the help panel between its frame characters. */
#define HELP_INNER_WIDTH 30
#define HELP_TITLE " CONTROLS "
#define HELP_DEVELOPER_TITLE " DEVELOPER KEYS "

#define COLOR_RED "\x1B[91m"
#define COLOR_GREEN "\x1B[92m"
#define COLOR_YELLOW "\x1B[93m"
#define COLOR_BLUE "\x1B[94m"
#define COLOR_MAGENTA "\x1B[95m"
#define COLOR_CYAN "\x1B[96m"
#define STYLE_DIM "\x1B[2m"
#define STYLE_RESET "\033[0m"

#define CURSOR_HOME "\033[H"
#define CLEAR_TO_SCREEN_END "\033[0J"
#define CLEAR_TO_LINE_END "\033[0K"
#define CLEAR_LINE "\033[2K"
#define CURSOR_UP_ONE_LINE "\033[1A"
#define UNICODE_ERROR_LABEL_TEXT " × Error:"
#define ASCII_ERROR_LABEL_TEXT " x Error:"
#define WARNING_LABEL_TEXT " ! Warning:"

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
  const char *fill;
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
    .fill = "▦",
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
    .fill = "%",
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

static const struct ui_theme *theme_for_ui(const struct ui_state *ui) {
  return ui->mode == UI_MODE_ASCII ? &ASCII_THEME : &UNICODE_THEME;
}

static const struct ui_style *style_for_ui(const struct ui_state *ui) {
  return ui->monochrome ? &MONOCHROME_STYLE : &ANSI_STYLE;
}

static void print_repeated(const char *text, int count) {
  for (int index = 0; index < count; index++)
    printf("%s", text);
}

static void finish_line(void) { printf("%s\n", CLEAR_TO_LINE_END); }

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

/* Return the text width of one debug row with fixed-width, space-separated
 * cells. */
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

/*
 * ANSI styling only applies to a terminal, so a redirected stream receives
 * plain text. The NO_COLOR convention (https://no-color.org) suppresses styling
 * as well: the variable takes effect when it is present and non-empty,
 * whatever its value.
 */
bool ui_styling_supported(FILE *stream) {
  const char *no_color = getenv("NO_COLOR");

  if (no_color != NULL && *no_color != '\0')
    return false;

  int descriptor = fileno(stream);
  return descriptor != -1 && isatty(descriptor);
}

void ui_initialize_state(struct ui_state *ui, enum ui_mode mode,
                         bool monochrome) {
  ui->show_debug = false;
  ui->monochrome = monochrome;
  ui->mode = mode;
}

const char *ui_error_label(const struct ui_state *ui) {
  if (ui->mode == UI_MODE_ASCII) {
    return ui->monochrome ? ASCII_ERROR_LABEL_TEXT
                          : COLOR_RED ASCII_ERROR_LABEL_TEXT STYLE_RESET;
  }

  return ui->monochrome ? UNICODE_ERROR_LABEL_TEXT
                        : COLOR_RED UNICODE_ERROR_LABEL_TEXT STYLE_RESET;
}

const char *ui_warning_label(const struct ui_state *ui) {
  return ui->monochrome ? WARNING_LABEL_TEXT
                        : COLOR_YELLOW WARNING_LABEL_TEXT STYLE_RESET;
}

void ui_begin_frame(void) { printf("%s", CURSOR_HOME); }

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
  int snake_width = (int)(sizeof(snake) / sizeof(*snake));
  char seed_text[sizeof("4294967295")];
  int seed_width = snprintf(seed_text, sizeof(seed_text), "%u", seed);
  /* The parts of " <snake> CNAKE (<seed>) "; glyphs are one column each. */
  int title_width = snake_width + seed_width +
                    (int)(strlen(" ") + strlen(" CNAKE (") + strlen(") "));
  int header_inner_width = game_layout_width(board_size) - 2;
  int fill_width = header_inner_width - title_width;
  int left_fill = fill_width / 2;
  int right_fill = fill_width - left_fill;

  printf("%s %s", style->cyan, theme->top_left);
  print_repeated(theme->horizontal, left_fill);

  printf(" %s", style->green);
  for (size_t index = 0; index < sizeof(snake) / sizeof(*snake); index++)
    printf("%s", snake[index]);

  printf(" CNAKE %s(%s)%s%s ", style->dim, seed_text, style->reset,
         style->cyan);

  print_repeated(theme->horizontal, right_fill);
  printf("%s", theme->top_right);
  finish_line();
  printf(" %s", theme->bottom_left);
  print_repeated(theme->horizontal, header_inner_width);
  printf("%s", theme->bottom_right);
  finish_line();
  printf("%s", style->reset);
  finish_line();
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
    printf("%s%s %s", style->green,
           tile == TILE_TAIL ? theme->tail : theme->body, style->reset);
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

/* The board is won when no empty tile remains, so this counts up to the total
 * tile count. */
static int filled_tile_count(const struct game_state *game) {
  return game->board_size * game->board_size - game_empty_tile_count(game);
}

static void print_status_row(const struct game_state *game, int row,
                             const struct ui_theme *theme,
                             const struct ui_style *style) {
  printf("%s%s%s%s", style->dim, style->cyan, theme->vertical, style->reset);

  switch (row) {
  case 0: {
    /* Sized for any int pair; the field width is what constrains display. */
    char filled_text[sizeof("-2147483648/-2147483648")];
    snprintf(filled_text, sizeof(filled_text), "%d/%d", filled_tile_count(game),
             game->board_size * game->board_size);
    printf(" %s%s%s   Filled%*s ", style->cyan, theme->fill, style->reset,
           STATUS_FILLED_FIELD_WIDTH, filled_text);
    break;
  }
  case 1:
    printf(" %s%s%s%s%s Snake     %4d ", style->green, theme->body, theme->body,
           theme->head_right, style->reset, game->length);
    break;
  case 2: {
    int score = game_score(game);
    printf(" %s%s%s   Score ", style->yellow, theme->score, style->reset);
    if (score == ANSWER_SCORE)
      printf("%s%8d%s ", style->yellow, score, style->reset);
    else
      printf("%8d ", score);
    break;
  }
  case 3:
    printf(" %s%s%s   Snack     %4d ", style->yellow, theme->snack,
           style->reset, game->stats.snacks);
    break;
  case 4:
    printf(" %s%s%s   Poison    %4d ", style->magenta, theme->poison,
           style->reset, game->stats.poison);
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
 * Panels sit side by side but need not be equally tall, so every column decides
 * per rendered line which part of its frame it contributes.
 */
enum panel_line {
  PANEL_LINE_TOP,
  PANEL_LINE_CONTENT,
  PANEL_LINE_BOTTOM,
  PANEL_LINE_BLANK,
};

static enum panel_line panel_line_at(int line, int content_rows) {
  if (line == 0)
    return PANEL_LINE_TOP;
  if (line <= content_rows)
    return PANEL_LINE_CONTENT;
  if (line == content_rows + 1)
    return PANEL_LINE_BOTTOM;
  return PANEL_LINE_BLANK;
}

/* Board lines carry one leading space in front of the frame. */
static int board_line_width(int board_size) {
  return board_outer_width(board_size) + 1;
}

static void print_board_line(const struct game_state *game, int line,
                             const struct ui_theme *theme,
                             const struct ui_style *style) {
  switch (panel_line_at(line, game->board_size)) {
  case PANEL_LINE_TOP:
    draw_border(game->board_size, false, theme, style);
    return;
  case PANEL_LINE_BOTTOM:
    draw_border(game->board_size, true, theme, style);
    return;
  case PANEL_LINE_BLANK:
    print_repeated(" ", board_line_width(game->board_size));
    return;
  case PANEL_LINE_CONTENT:
    break;
  }

  int y = line - 1;
  printf("%s%s %s %s", style->dim, style->cyan, theme->vertical, style->reset);

  for (int x = 0; x < game->board_size; x++)
    print_tile(game_board_tile(game, x, y), game->direction, theme, style);

  printf("%s%s%s%s", style->dim, style->cyan, theme->vertical, style->reset);
}

static void print_status_line(const struct game_state *game, int line,
                              int content_rows, const struct ui_theme *theme,
                              const struct ui_style *style) {
  switch (panel_line_at(line, content_rows)) {
  case PANEL_LINE_TOP:
    print_panel_top(STATUS_INNER_WIDTH, "STATUS", game->board_size, true, theme,
                    style);
    return;
  case PANEL_LINE_BOTTOM:
    print_panel_bottom(STATUS_INNER_WIDTH, theme, style);
    return;
  case PANEL_LINE_BLANK:
    print_repeated(" ", STATUS_INNER_WIDTH + 2);
    return;
  case PANEL_LINE_CONTENT:
    print_status_row(game, line - 1, theme, style);
    return;
  }
}

static void print_debug_line(const struct game_state *game, int line,
                             int value_width, int panel_inner_width,
                             const struct ui_theme *theme,
                             const struct ui_style *style) {
  switch (panel_line_at(line, game->board_size)) {
  case PANEL_LINE_TOP:
    print_panel_top(panel_inner_width, "DEBUG", game->board_size, false, theme,
                    style);
    return;
  case PANEL_LINE_BOTTOM:
    print_panel_bottom(panel_inner_width, theme, style);
    return;
  case PANEL_LINE_BLANK:
    print_repeated(" ", panel_inner_width + 2);
    return;
  case PANEL_LINE_CONTENT:
    print_debug_row(game, line - 1, value_width, panel_inner_width, theme,
                    style);
    return;
  }
}

/*
 * Draw the board with fixed-width status and optional debug panels. The status
 * panel keeps all its rows even when the board is shorter than the panel, so it
 * extends past the board frame on the smallest boards.
 */
void ui_draw_game_board(const struct game_state *game,
                        const struct ui_state *ui) {
  const struct ui_theme *theme = theme_for_ui(ui);
  const struct ui_style *style = style_for_ui(ui);
  int status_rows =
      game->board_size > STATUS_ROW_COUNT ? game->board_size : STATUS_ROW_COUNT;
  int total_lines = status_rows + 2;
  int debug_value_width = 0;
  int debug_inner_width = 0;

  if (ui->show_debug) {
    debug_value_width = debug_row_value_width(game);
    debug_inner_width =
        debug_panel_inner_width(game->board_size, debug_value_width);
  }

  for (int line = 0; line < total_lines; line++) {
    print_board_line(game, line, theme, style);

    print_repeated(" ", BOARD_STATUS_GAP_WIDTH);
    print_status_line(game, line, status_rows, theme, style);

    if (ui->show_debug) {
      print_repeated(" ", STATUS_DEBUG_GAP_WIDTH);
      print_debug_line(game, line, debug_value_width, debug_inner_width, theme,
                       style);
    }

    printf("%s", style->reset);
    finish_line();
  }
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

/*
 * The movement diagram as plain text. The h/j/k/l keys render yellow, and the
 * ^ v < > stand-ins render green through the theme's head glyphs; everything
 * else is layout.
 */
static const char *const HELP_DIAGRAM_ROWS[] = {
    "          k ^",
    "      h <     > l",
    "          j v",
    "",
    "  also: arrow keys, wasd",
};

static const char *help_arrow_glyph(char symbol, const struct ui_theme *theme) {
  switch (symbol) {
  case '^':
    return theme->head_up;
  case 'v':
    return theme->head_down;
  case '<':
    return theme->head_left;
  case '>':
    return theme->head_right;
  default:
    return NULL;
  }
}

/* A movement key is drawn as a single letter surrounded by spaces. */
static bool is_help_movement_key(const char *row, const char *symbol) {
  return strchr("hjkl", *symbol) != NULL &&
         (symbol == row || symbol[-1] == ' ') &&
         (symbol[1] == '\0' || symbol[1] == ' ');
}

static void print_help_diagram_row(const char *row,
                                   const struct ui_theme *theme,
                                   const struct ui_style *style) {
  const char *active_color = NULL;

  printf(" %s %s", theme->vertical, style->reset);

  for (const char *symbol = row; *symbol != '\0'; symbol++) {
    const char *glyph = help_arrow_glyph(*symbol, theme);
    const char *color = NULL;

    if (glyph != NULL)
      color = style->green;
    else if (is_help_movement_key(row, symbol))
      color = style->yellow;

    if (color != NULL && color != active_color) {
      printf("%s", color);
      active_color = color;
    }

    if (glyph != NULL)
      printf("%s", glyph);
    else
      printf("%c", *symbol);
  }

  printf("%s", style->reset);
  print_repeated(" ", HELP_INNER_WIDTH - 1 - (int)strlen(row));
  printf("%s%s", style->cyan, theme->vertical);
  finish_line();
}

static void print_help_option(const struct ui_theme *theme, const char *command,
                              const char *description,
                              const struct ui_style *style) {
  /* One leading space and a two-column key field precede the description. */
  printf(" %s %s%-2s%s%-*s%s%s", theme->vertical, style->yellow, command,
         style->reset, HELP_INNER_WIDTH - 3, description, style->cyan,
         theme->vertical);
  finish_line();
}

/*
 * A horizontal frame line with an optional title centred in it. The caller
 * finishes the line, so that a reset can precede the line end.
 */
static void print_help_rule(const char *left, const char *title,
                            const char *right, const struct ui_theme *theme,
                            const struct ui_style *style) {
  int title_width = title != NULL ? (int)strlen(title) : 0;
  int left_fill = (HELP_INNER_WIDTH - title_width) / 2;
  int right_fill = HELP_INNER_WIDTH - title_width - left_fill;

  printf(" %s%s", style->cyan, left);
  print_repeated(theme->horizontal, left_fill);
  if (title != NULL)
    printf("%s", title);
  print_repeated(theme->horizontal, right_fill);
  printf("%s", right);
}

/*
 * The debug view and the growth cheat are developer controls, so they follow
 * the gameplay controls under a separate title.
 */
static void print_help(const struct ui_state *ui) {
  const struct ui_theme *theme = theme_for_ui(ui);
  const struct ui_style *style = style_for_ui(ui);

  print_help_rule(theme->top_left, HELP_TITLE, theme->top_right, theme, style);
  finish_line();

  for (size_t index = 0;
       index < sizeof(HELP_DIAGRAM_ROWS) / sizeof(*HELP_DIAGRAM_ROWS); index++)
    print_help_diagram_row(HELP_DIAGRAM_ROWS[index], theme, style);

  print_help_rule(theme->middle_left, NULL, theme->middle_right, theme, style);
  finish_line();
  print_help_option(theme, "?", "Show help", style);
  print_help_option(theme, "q", "Quit", style);
  print_help_rule(theme->middle_left, HELP_DEVELOPER_TITLE, theme->middle_right,
                  theme, style);
  finish_line();
  print_help_option(theme, "v", "Toggle debug view", style);
  print_help_option(theme, "c", "Add five segments", style);
  print_help_rule(theme->bottom_left, NULL, theme->bottom_right, theme, style);
  printf("%s", style->reset);
  finish_line();
}

enum ui_prompt {
  UI_PROMPT_COMMAND = 0,
  UI_PROMPT_REPLAY,
  UI_PROMPT_HELP,
};

enum ui_message {
  UI_MESSAGE_NONE = 0,
  UI_MESSAGE_FEEDBACK,
  UI_MESSAGE_GAME_STATUS,
  UI_MESSAGE_INVALID_COMMAND,
};

/*
 * Only the glyph of a feedback notice depends on the theme, so one entry per
 * notice covers both modes. The colour is dropped in monochrome mode, like
 * every other style code.
 */
struct ui_feedback_text {
  const char *unicode_glyph;
  const char *ascii_glyph;
  const char *color;
  const char *text;
};

static const struct ui_feedback_text FEEDBACK_TEXT[] = {
    [UI_FEEDBACK_DEBUG_ON] = {"◆", "*", COLOR_CYAN, "Debug view on."},
    [UI_FEEDBACK_DEBUG_OFF] = {"◆", "*", COLOR_CYAN, "Debug view off."},
    [UI_FEEDBACK_GROW] = {"!", "!", COLOR_YELLOW, "Length +5."},
    [UI_FEEDBACK_ANSWER] = {"★", "+", COLOR_YELLOW, "The answer is snake."},
    [UI_FEEDBACK_REVERSAL] = {"!", "!", COLOR_YELLOW,
                              "Cannot reverse onto the body."},
};

static void print_feedback_message(enum ui_feedback feedback,
                                   const struct ui_state *ui) {
  if (feedback == UI_FEEDBACK_NONE)
    return;

  const struct ui_feedback_text *notice = &FEEDBACK_TEXT[feedback];
  const struct ui_style *style = style_for_ui(ui);
  const char *glyph =
      ui->mode == UI_MODE_ASCII ? notice->ascii_glyph : notice->unicode_glyph;

  printf("%s %s %s%s", ui->monochrome ? "" : notice->color, glyph, style->reset,
         notice->text);
}

static void print_input_prompt(enum ui_prompt prompt,
                               const struct ui_theme *theme,
                               const struct ui_style *style) {
  print_command_prompt(theme, style);

  if (prompt == UI_PROMPT_COMMAND) {
    printf("Move %s[hjkl]%s / Help %s[?]%s / Quit %s[q]%s: ", style->dim,
           style->reset, style->dim, style->reset, style->dim, style->reset);
  } else if (prompt == UI_PROMPT_REPLAY) {
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
static void render_input_area(const struct ui_state *ui, enum ui_prompt prompt,
                              enum ui_message message,
                              enum ui_feedback feedback,
                              enum game_status status, bool replace) {
  const struct ui_theme *theme = theme_for_ui(ui);
  const struct ui_style *style = style_for_ui(ui);

  if (replace)
    printf("\r%s", CURSOR_UP_ONE_LINE);

  printf("\r%s", CLEAR_LINE);

  switch (message) {
  case UI_MESSAGE_FEEDBACK:
    print_feedback_message(feedback, ui);
    break;
  case UI_MESSAGE_GAME_STATUS:
    print_game_status_message(status, ui);
    break;
  case UI_MESSAGE_INVALID_COMMAND:
    printf("%s %s%s ", style->red, ui->mode == UI_MODE_ASCII ? "x" : "×",
           style->reset);
    if (prompt == UI_PROMPT_COMMAND)
      printf("Unknown command. Press ? for help.");
    else
      printf("Choose r to retry or q to quit.");
    break;
  case UI_MESSAGE_NONE:
    break;
  }

  printf("\r\n%s\r", CLEAR_LINE);
  print_input_prompt(prompt, theme, style);
  printf("%s", CLEAR_TO_SCREEN_END);
  fflush(stdout);
}

void ui_render_command_prompt(const struct ui_state *ui,
                              enum ui_feedback feedback, bool invalid,
                              bool replace) {
  enum ui_message message = UI_MESSAGE_NONE;

  if (feedback != UI_FEEDBACK_NONE)
    message = UI_MESSAGE_FEEDBACK;
  else if (invalid)
    message = UI_MESSAGE_INVALID_COMMAND;

  render_input_area(ui, UI_PROMPT_COMMAND, message, feedback, GAME_RUNNING,
                    replace);
}

void ui_render_replay_prompt(const struct ui_state *ui,
                             enum game_status game_status, bool invalid,
                             bool replace) {
  enum ui_message message =
      invalid ? UI_MESSAGE_INVALID_COMMAND : UI_MESSAGE_GAME_STATUS;

  render_input_area(ui, UI_PROMPT_REPLAY, message, UI_FEEDBACK_NONE,
                    game_status, replace);
}

void ui_render_help(const struct ui_state *ui) {
  ui_begin_frame();
  print_help(ui);
}

void ui_render_help_prompt(const struct ui_state *ui) {
  render_input_area(ui, UI_PROMPT_HELP, UI_MESSAGE_NONE, UI_FEEDBACK_NONE,
                    GAME_RUNNING, false);
}

enum ui_feedback ui_toggle_debug(struct ui_state *ui) {
  ui->show_debug = !ui->show_debug;

  return ui->show_debug ? UI_FEEDBACK_DEBUG_ON : UI_FEEDBACK_DEBUG_OFF;
}

static const char *summary_outcome(enum game_status status) {
  switch (status) {
  case GAME_WON:
    return "You win!";
  case GAME_OVER_SNAKE:
    return "Game over: ran into the snake.";
  case GAME_OVER_OBSTACLE:
    return "Game over: hit an obstacle.";
  case GAME_OVER_POISON:
    return "Game over: poisoned.";
  case GAME_RUNNING:
    break;
  }

  return "Game quit.";
}

/*
 * The summary is printed on the normal screen after the game has ended. It
 * repeats the result of the last round together with the command line that
 * replays its board.
 */
void ui_print_summary(const struct game_state *game, unsigned int seed,
                      const struct ui_state *ui) {
  const struct ui_style *style = style_for_ui(ui);
  const char *color = "";

  if (game->status == GAME_WON)
    color = style->green;
  else if (game->status != GAME_RUNNING)
    color = style->red;

  printf(" %s%s%s Score %d, length %d, filled %d/%d.\n", color,
         summary_outcome(game->status), style->reset, game_score(game),
         game->length, filled_tile_count(game),
         game->board_size * game->board_size);
  printf(" %sReplay this board:%s cnake %d --seed %u\n", style->dim,
         style->reset, game->board_size, seed);
}

void ui_print_interrupted_exit(const struct ui_state *ui) {
  const struct ui_style *style = style_for_ui(ui);
  printf("%s\n\n", style->reset);
}
