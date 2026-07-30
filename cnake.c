/****************************************************************
 * File:    cnake.c                                             *
 * Date:    2014-10-16 - 2014-12-27 (updated 2026-07-30)        *
 * Author:  Aaron Dettmann                                      *
 * Purpose: Cnake - A simple text-based snake game written in C *
 ****************************************************************/

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/*---------- Configuration and Data Model ----------*/

#define VERSION "0.5.0"
#define DEFAULT_BOARD_SIZE 6
#define MIN_BOARD_SIZE 4
#define MAX_BOARD_SIZE 10
#define HEADER_INNER_WIDTH 34

enum board_size_parse_status {
  BOARD_SIZE_INVALID = 0,
  BOARD_SIZE_OUT_OF_RANGE,
  BOARD_SIZE_VALID,
};

enum game_status {
  GAME_RUNNING = 0,
  GAME_WON,
  GAME_OVER_SNAKE,
  GAME_OVER_OBSTACLE,
  GAME_OVER_POISON,
};

enum direction {
  DIRECTION_UP = 0,
  DIRECTION_DOWN,
  DIRECTION_LEFT,
  DIRECTION_RIGHT,
};

enum player_command {
  PLAYER_COMMAND_INVALID = 0,
  PLAYER_COMMAND_MOVE_UP,
  PLAYER_COMMAND_MOVE_DOWN,
  PLAYER_COMMAND_MOVE_LEFT,
  PLAYER_COMMAND_MOVE_RIGHT,
  PLAYER_COMMAND_HELP,
  PLAYER_COMMAND_TOGGLE_DEBUG,
  PLAYER_COMMAND_GROW,
  PLAYER_COMMAND_QUIT,
};

enum command_action {
  COMMAND_REPROMPT = 0,
  COMMAND_QUIT,
  COMMAND_SKIP_TURN,
  COMMAND_MOVE,
};

enum update_status {
  UPDATE_OK = 0,
  UPDATE_SNACK_PLACEMENT_ERROR,
  UPDATE_POISON_PLACEMENT_ERROR,
};

enum exit_code {
  EXIT_OK = 0,
  EXIT_INPUT_ERROR = 1,
  EXIT_MEMORY_ERROR = 2,
  EXIT_TILE_PLACEMENT_ERROR = 3,
  EXIT_TERMINAL_ERROR = 4,
  EXIT_COMMAND_ERROR = 5,
};

/*
 * Negative values identify special tiles. Positive values are snake body
 * countdowns; the segment with value 1 is the tail.
 */
enum tile_type {
  TILE_EMPTY = 0,
  TILE_HEAD = -1,
  TILE_OBSTACLE = -2,
  TILE_SNACK = -3,
  TILE_POISON = -4,
};

struct position {
  int x;
  int y;
};

struct game_stats {
  int moves;
  int snacks;
  int poison;
};

struct game_state {
  int *board; // Dynamically allocated row-major tile storage
  int board_size;
  unsigned int seed;
  struct position head;
  int length;
  int obstacle_count;
  struct game_stats stats;
  enum direction direction;
  enum game_status status;
  bool show_debug;
};

/*---------- Terminal Presentation ----------*/

#define COLOR_RED "\x1B[91m"
#define COLOR_GREEN "\x1B[92m"
#define COLOR_YELLOW "\x1B[93m"
#define COLOR_BLUE "\x1B[94m"
#define COLOR_MAGENTA "\x1B[95m"
#define COLOR_CYAN "\x1B[96m"
#define STYLE_DIM "\x1B[2m"
#define COLOR_RESET "\033[0m"

#define ERROR_LABEL COLOR_RED " × Error:" COLOR_RESET
#define WARNING_LABEL COLOR_YELLOW " ! Warning:" COLOR_RESET
#define COMMAND_PROMPT COLOR_CYAN " ❯ " COLOR_RESET
#define CLEAR_SCREEN "\033[H\033[0J"

static const char *const GLYPH_EMPTY = "·";
static const char *const GLYPH_BODY = "●";
static const char *const GLYPH_TAIL = "○";
static const char *const GLYPH_HEAD_UP = "▲";
static const char *const GLYPH_HEAD_DOWN = "▼";
static const char *const GLYPH_HEAD_LEFT = "◀";
static const char *const GLYPH_HEAD_RIGHT = "▶";
static const char *const GLYPH_OBSTACLE = "▣";
static const char *const GLYPH_SNACK = "✦";
static const char *const GLYPH_POISON = "×";

/*---------- Board and Terminal State ----------*/

static struct termios original_terminal;
static bool terminal_initialized = false;
static volatile sig_atomic_t interrupted = 0;

static size_t board_index(const struct game_state *game, int x, int y) {
  return (size_t)y * (size_t)game->board_size + (size_t)x;
}

static int board_tile(const struct game_state *game, int x, int y) {
  return game->board[board_index(game, x, y)];
}

static void set_board_tile(struct game_state *game, int x, int y, int value) {
  game->board[board_index(game, x, y)] = value;
}

/*---------- Terminal Input ----------*/

static void handle_signal(int signal_number) { interrupted = signal_number; }

static bool initialize_terminal(void) {
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

static void restore_terminal(void) {
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
      return 0;

    if (ready == -1) {
      if (errno == EINTR) {
        if (interrupted)
          return 0;

        continue;
      }

      return -1;
    }

    ssize_t bytes_read = read(STDIN_FILENO, input, 1);
    if (bytes_read == 1)
      return 1;

    if (bytes_read == 0)
      return 0;

    if (errno == EINTR) {
      if (interrupted)
        return 0;

      continue;
    }

    return -1;
  }
}

/*
 * Consume an unsupported terminal escape sequence without delaying normal
 * single-key commands.
 */
static void discard_escape_sequence(void) {
  char input;
  int status = read_stdin_char(&input, 10);

  if (status != 1)
    return;

  if (input != '[' && input != 'O') {
    while (read_stdin_char(&input, 5) == 1)
      ;

    return;
  }

  do {
    status = read_stdin_char(&input, 5);
  } while (status == 1 && (input < '@' || input > '~'));
}

/*
 * Read the next non-newline command byte. Return value 2 means that an escape
 * sequence was consumed and the caller should prompt again.
 */
static int read_command_char(char *input) {
  int status;

  do {
    status = read_stdin_char(input, -1);
    if (status != 1)
      return status;
  } while (*input == '\n' || *input == '\r');

  if (*input == '\033') {
    discard_escape_sequence();
    return 2;
  }

  return 1;
}

/*---------- Program Input ----------*/

static void print_usage(const char *program_name) {
  fprintf(stderr, " Usage: %s [board-size] [--seed unsigned-integer]\n",
          program_name);
}

static bool parse_seed(const char *input, unsigned int *seed) {
  if (*input == '\0' || !isdigit((unsigned char)*input))
    return false;

  char *end;

  errno = 0;
  unsigned long parsed_seed = strtoul(input, &end, 10);

  if (errno != 0 || *end != '\0' || parsed_seed > UINT_MAX)
    return false;

  *seed = (unsigned int)parsed_seed;
  return true;
}

/*
 * Remove a trailing seed option from the arguments passed to board-size
 * handling. This keeps the existing optional positional size and prompt.
 */
static int determine_seed(int argc, char *argv[], int *board_argc,
                          unsigned int *seed) {
  *board_argc = argc;
  *seed = (unsigned int)time(NULL);

  int seed_option = -1;
  for (int index = 1; index < argc; index++) {
    if (strcmp(argv[index], "--seed") == 0) {
      seed_option = index;
      break;
    }
  }

  if (seed_option != -1) {
    if (seed_option == argc - 1) {
      fprintf(stderr, ERROR_LABEL " --seed requires an unsigned integer.\n");
      print_usage(argv[0]);
      return EXIT_INPUT_ERROR;
    }

    if (seed_option != argc - 2 || seed_option > 2) {
      // clang-format off
      fprintf(stderr, ERROR_LABEL " --seed must appear once at the end of the command.\n");
      // clang-format on
      print_usage(argv[0]);
      return EXIT_INPUT_ERROR;
    }

    if (!parse_seed(argv[seed_option + 1], seed)) {
      fprintf(stderr,
              ERROR_LABEL " Seed must be a decimal integer from 0 to %u.\n",
              UINT_MAX);
      return EXIT_INPUT_ERROR;
    }

    *board_argc = seed_option;
  }

  if (*board_argc > 2) {
    fprintf(stderr, ERROR_LABEL " Unexpected command-line argument.\n");
    print_usage(argv[0]);
    return EXIT_INPUT_ERROR;
  }

  if (*board_argc == 2 && strncmp(argv[1], "--", 2) == 0) {
    fprintf(stderr, ERROR_LABEL " Unknown option: %s\n", argv[1]);
    print_usage(argv[0]);
    return EXIT_INPUT_ERROR;
  }

  return EXIT_OK;
}

static enum board_size_parse_status parse_board_size(const char *input,
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

/*
 * Use a valid command-line size when supplied; otherwise prompt until the
 * player enters a valid size or accepts the default.
 */
static int determine_board_size(int argc, char *argv[], int *board_size) {
  if (argc > 1 && parse_board_size(argv[1], board_size) == BOARD_SIZE_VALID)
    return EXIT_OK;

  while (true) {
    printf(COMMAND_PROMPT "Board size [%d-%d] " STYLE_DIM
                          "(default %d)" COLOR_RESET ": ",
           MIN_BOARD_SIZE, MAX_BOARD_SIZE, DEFAULT_BOARD_SIZE);
    fflush(stdout);

    char input[32];
    if (!fgets(input, sizeof(input), stdin)) {
      if (interrupted) {
        putchar('\n');
        return 128 + interrupted;
      }

      return EXIT_INPUT_ERROR;
    }

    if (strchr(input, '\n') == NULL) {
      int discarded;
      while ((discarded = getchar()) != '\n' && discarded != EOF)
        ;
    }

    if (input[0] == '\n') {
      *board_size = DEFAULT_BOARD_SIZE;
      return EXIT_OK;
    }

    enum board_size_parse_status status = parse_board_size(input, board_size);

    if (status == BOARD_SIZE_VALID)
      return EXIT_OK;

    if (status == BOARD_SIZE_INVALID) {
      printf(ERROR_LABEL
             " Enter a number from %d to %d, or press Enter for %d.\n",
             MIN_BOARD_SIZE, MAX_BOARD_SIZE, DEFAULT_BOARD_SIZE);
    } else {
      printf(ERROR_LABEL " Board size must be between %d and %d.\n",
             MIN_BOARD_SIZE, MAX_BOARD_SIZE);
    }
  }
}

/*---------- Rendering ----------*/

static const char *head_glyph(enum direction direction) {
  switch (direction) {
  case DIRECTION_UP:
    return GLYPH_HEAD_UP;
  case DIRECTION_DOWN:
    return GLYPH_HEAD_DOWN;
  case DIRECTION_LEFT:
    return GLYPH_HEAD_LEFT;
  case DIRECTION_RIGHT:
    return GLYPH_HEAD_RIGHT;
  }

  return GLYPH_HEAD_RIGHT;
}

static void print_header(unsigned int seed) {
  const char *const snake[] = {GLYPH_BODY, GLYPH_BODY, GLYPH_HEAD_RIGHT};
  char seed_text[sizeof(seed) * 3 + 3];
  int seed_width =
      snprintf(seed_text, sizeof(seed_text), "%u", (unsigned int)seed);
  int title_width =
      (int)(sizeof(snake) / sizeof(*snake) + sizeof("  CNAKE () ") - 1) +
      seed_width;
  int fill_width = HEADER_INNER_WIDTH - title_width;
  int left_fill = fill_width / 2;
  int right_fill = fill_width - left_fill;

  printf(CLEAR_SCREEN COLOR_CYAN " ╭");
  for (int count = 0; count < left_fill; count++)
    printf("─");

  printf(" " COLOR_GREEN);
  for (size_t index = 0; index < sizeof(snake) / sizeof(*snake); index++)
    printf("%s", snake[index]);

  printf(" CNAKE " STYLE_DIM "(%s)" COLOR_RESET COLOR_CYAN " ", seed_text);

  for (int count = 0; count < right_fill; count++)
    printf("─");

  printf("╮\n ╰");
  for (int count = 0; count < HEADER_INNER_WIDTH; count++)
    printf("─");

  printf("╯\n\n" COLOR_RESET);
}

static void draw_border(int board_size, bool bottom) {
  printf(STYLE_DIM COLOR_CYAN " %s", bottom ? "╰" : "╭");

  for (int column = 0; column <= 2 * board_size; column++)
    printf("─");

  printf("%s" COLOR_RESET, bottom ? "╯" : "╮");
}

static void print_tile(int tile, enum direction direction) {
  if (tile > TILE_EMPTY) {
    printf(COLOR_GREEN "%s " COLOR_RESET, tile == 1 ? GLYPH_TAIL : GLYPH_BODY);
    return;
  }

  switch (tile) {
  case TILE_EMPTY:
    printf(STYLE_DIM "%s " COLOR_RESET, GLYPH_EMPTY);
    break;
  case TILE_HEAD:
    printf(COLOR_GREEN "%s " COLOR_RESET, head_glyph(direction));
    break;
  case TILE_OBSTACLE:
    printf(COLOR_BLUE "%s " COLOR_RESET, GLYPH_OBSTACLE);
    break;
  case TILE_SNACK:
    printf(COLOR_YELLOW "%s " COLOR_RESET, GLYPH_SNACK);
    break;
  case TILE_POISON:
    printf(COLOR_MAGENTA "%s " COLOR_RESET, GLYPH_POISON);
    break;
  }
}

static void print_status_row(const struct game_state *game, int row) {
  printf("   " STYLE_DIM COLOR_CYAN "│" COLOR_RESET);

  switch (row) {
  case 0:
    printf(" " COLOR_GREEN "%s%s%s" COLOR_RESET " Snake      %3d ", GLYPH_BODY,
           GLYPH_BODY, GLYPH_HEAD_RIGHT, game->length);
    break;
  case 1:
    printf(" " COLOR_YELLOW "%s" COLOR_RESET "   Snack      %3d ", GLYPH_SNACK,
           game->stats.snacks);
    break;
  case 2:
    printf(" " COLOR_MAGENTA "%s" COLOR_RESET "   Poison     %3d ",
           GLYPH_POISON, game->stats.poison);
    break;
  case 3:
    printf(" " COLOR_BLUE "%s" COLOR_RESET "   Obstacle       ",
           GLYPH_OBSTACLE);
    break;
  default:
    printf("%20s", ""); // Preserve box width for empty lines
    break;
  }

  printf(STYLE_DIM COLOR_CYAN "│\n" COLOR_RESET);
}

/*
 * Draw the board and fixed-width status panel side by side, followed by the
 * optional numeric board representation used for debugging.
 */
static void draw_game_board(const struct game_state *game) {
  const char *title_fill = game->board_size < 10 ? "────" : "──";

  draw_border(game->board_size, false);
  printf("   " STYLE_DIM COLOR_CYAN "╭── STATUS · %d×%d %s╮\n" COLOR_RESET,
         game->board_size, game->board_size, title_fill);

  for (int y = 0; y < game->board_size; y++) {
    printf(STYLE_DIM COLOR_CYAN " │ " COLOR_RESET);

    for (int x = 0; x < game->board_size; x++)
      print_tile(board_tile(game, x, y), game->direction);

    printf(STYLE_DIM COLOR_CYAN "│" COLOR_RESET);
    print_status_row(game, y);
  }

  draw_border(game->board_size, true);
  printf("   " STYLE_DIM COLOR_CYAN "╰────────────────────╯\n" COLOR_RESET);

  if (!game->show_debug)
    return;

  printf("\n");
  for (int y = 0; y < game->board_size; y++) {
    printf("|");

    for (int x = 0; x < game->board_size; x++)
      printf("%2d ", board_tile(game, x, y));

    printf("|\n");
  }
}

static void print_game_status_message(enum game_status status) {
  switch (status) {
  case GAME_WON:
    printf(COLOR_GREEN "\n ✦ YOU WIN! ✦\n" COLOR_RESET);
    return;
  case GAME_OVER_SNAKE:
    printf("\n Snake!" COLOR_GREEN " %s%s%s " COLOR_RESET, GLYPH_BODY,
           GLYPH_BODY, GLYPH_HEAD_RIGHT);
    break;
  case GAME_OVER_OBSTACLE:
    printf("\n Obstacle!" COLOR_BLUE " %s " COLOR_RESET, GLYPH_OBSTACLE);
    break;
  case GAME_OVER_POISON:
    printf("\n Poison!" COLOR_MAGENTA " %s " COLOR_RESET, GLYPH_POISON);
    break;
  case GAME_RUNNING:
    return;
  }

  printf(COLOR_RED "— GAME OVER! —\n" COLOR_RESET);
}

static void print_game_summary(const struct game_state *game) {
  int capacity = game->board_size * game->board_size - game->obstacle_count - 1;

  // clang-format off
  printf("\n" COLOR_CYAN
         " ╭────── SCORE ───────╮\n"
         " │ " COLOR_RESET "%-14s%4d " COLOR_CYAN "│\n"
         " │ " COLOR_RESET "%-14s%4d " COLOR_CYAN "│\n"
         " │ " COLOR_RESET "%-14s%4d " COLOR_CYAN "│\n"
         " │ " COLOR_RESET "%-10s%3d/%-3d+ " COLOR_CYAN "│\n"
         " ╰────────────────────╯\n\n" COLOR_RESET,
         "Moves", game->stats.moves, "Snacks", game->stats.snacks, "Poison",
         game->stats.poison, "Length", game->length, capacity);
  // clang-format on
}

static void print_help(void) {
  // clang-format off
  printf("\n\n" COLOR_CYAN
         "╭────────── CONTROLS ──────────╮\n"
         "│ " COLOR_RESET
         "          " COLOR_YELLOW "k" COLOR_GREEN " ▲" COLOR_RESET
         "                " COLOR_CYAN "│\n"
         "│ " COLOR_RESET
          "      " COLOR_YELLOW "h" COLOR_GREEN " ◀     ▶ " COLOR_YELLOW "l" COLOR_RESET 
          "            " COLOR_CYAN
         "│\n"
         "│ " COLOR_RESET
          "          " COLOR_YELLOW "j" COLOR_GREEN " ▼" COLOR_RESET
          "                " COLOR_CYAN "│\n"
         "├──────────────────────────────┤\n"
         "│ " COLOR_YELLOW "%-2s" COLOR_RESET "%-27s" COLOR_CYAN "│\n"
         "│ " COLOR_YELLOW "%-2s" COLOR_RESET "%-27s" COLOR_CYAN "│\n"
         "│ " COLOR_YELLOW "%-2s" COLOR_RESET "%-27s" COLOR_CYAN "│\n"
         "│ " COLOR_YELLOW "%-2s" COLOR_RESET "%-27s" COLOR_CYAN "│\n"
         "╰──────────────────────────────╯\n"
         COLOR_RESET,
         "?", "Show help",
         "v", "Toggle debug view",
         "c", "Add five segments",
         "q", "Quit"
  );
  // clang-format on
}

/*---------- Command Handling ----------*/

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
static int prompt_for_command(enum player_command *command,
                              const char **feedback) {
  printf("\n");

  if (*feedback != NULL) {
    printf("%s   ", *feedback);
    *feedback = NULL;
  }

  printf(COMMAND_PROMPT "Command " STYLE_DIM "[?]" COLOR_RESET ": ");
  fflush(stdout);

  char input;
  int status = read_command_char(&input);

  if (status == 1)
    *command = parse_player_command(input);

  return status;
}

static void move_head(struct game_state *game, enum direction direction) {
  int delta_x = 0;
  int delta_y = 0;

  switch (direction) {
  case DIRECTION_UP:
    delta_y = -1;
    break;
  case DIRECTION_DOWN:
    delta_y = 1;
    break;
  case DIRECTION_LEFT:
    delta_x = -1;
    break;
  case DIRECTION_RIGHT:
    delta_x = 1;
    break;
  }

  game->direction = direction;
  game->head.x = (game->head.x + delta_x + game->board_size) % game->board_size;
  game->head.y = (game->head.y + delta_y + game->board_size) % game->board_size;
}

/*
 * Apply immediate command effects and tell the main loop whether to reprompt,
 * redraw without advancing, quit, or update the game after movement.
 */
static enum command_action apply_player_command(struct game_state *game,
                                                enum player_command command,
                                                const char **feedback) {
  switch (command) {
  case PLAYER_COMMAND_MOVE_UP:
    move_head(game, DIRECTION_UP);
    break;
  case PLAYER_COMMAND_MOVE_DOWN:
    move_head(game, DIRECTION_DOWN);
    break;
  case PLAYER_COMMAND_MOVE_LEFT:
    move_head(game, DIRECTION_LEFT);
    break;
  case PLAYER_COMMAND_MOVE_RIGHT:
    move_head(game, DIRECTION_RIGHT);
    break;
  case PLAYER_COMMAND_HELP:
    print_help();
    return COMMAND_REPROMPT;
  case PLAYER_COMMAND_TOGGLE_DEBUG:
    game->show_debug = !game->show_debug;
    // clang-format off
    *feedback = game->show_debug ? COLOR_CYAN " ◆ " COLOR_RESET "Debug view on"
                                 : COLOR_CYAN " ◆ " COLOR_RESET "Debug view off";
    // clang-format on
    return COMMAND_SKIP_TURN;
  case PLAYER_COMMAND_GROW:
    game->length += 5;
    *feedback = COLOR_YELLOW " ! " COLOR_RESET "Length +5";
    return COMMAND_SKIP_TURN;
  case PLAYER_COMMAND_QUIT:
    return COMMAND_QUIT;
  case PLAYER_COMMAND_INVALID:
    printf("\n" ERROR_LABEL " Unknown command.\n");
    return COMMAND_REPROMPT;
  }

  game->stats.moves++;
  return COMMAND_MOVE;
}

/*---------- Tile Placement ----------*/

static bool has_adjacent_obstacle(const struct game_state *game, int x, int y) {
  for (int nearby_y = y - 1; nearby_y <= y + 1; nearby_y++) {
    for (int nearby_x = x - 1; nearby_x <= x + 1; nearby_x++) {
      if (nearby_y < 0 || nearby_y >= game->board_size || nearby_x < 0 ||
          nearby_x >= game->board_size)
        continue;

      if (nearby_x == x && nearby_y == y)
        continue;

      if (board_tile(game, nearby_x, nearby_y) == TILE_OBSTACLE)
        return true;
    }
  }

  return false;
}

static bool is_spawn_position_valid(const struct game_state *game, int x, int y,
                                    enum tile_type type) {
  if (board_tile(game, x, y) != TILE_EMPTY)
    return false;

  return type != TILE_OBSTACLE || !has_adjacent_obstacle(game, x, y);
}

/*
 * Start at a random tile, then scan the entire board. This preserves random
 * placement while guaranteeing termination when few empty tiles remain.
 */
static bool find_spawn_position(const struct game_state *game,
                                enum tile_type type,
                                struct position *position) {
  int tile_count = game->board_size * game->board_size;
  int start = rand() % tile_count;

  for (int offset = 0; offset < tile_count; offset++) {
    int index = (start + offset) % tile_count;
    int x = index % game->board_size;
    int y = index / game->board_size;

    if (is_spawn_position_valid(game, x, y, type)) {
      position->x = x;
      position->y = y;
      return true;
    }
  }

  return false;
}

static bool place_tile(struct game_state *game, enum tile_type type) {
  struct position position;

  if (!find_spawn_position(game, type, &position))
    return false;

  set_board_tile(game, position.x, position.y, type);
  return true;
}

static bool initialize_board(struct game_state *game) {
  set_board_tile(game, game->head.x, game->head.y, TILE_HEAD);

  int target_obstacle_count = game->board_size * game->board_size / 10 + 1;

  for (int count = 0; count < target_obstacle_count; count++) {
    if (!place_tile(game, TILE_OBSTACLE))
      break;

    game->obstacle_count++;
  }

  return place_tile(game, TILE_POISON) && place_tile(game, TILE_SNACK);
}

/*---------- Snake and Game Updates ----------*/

static bool board_contains_value(const struct game_state *game, int value) {
  int tile_count = game->board_size * game->board_size;

  for (int index = 0; index < tile_count; index++) {
    if (game->board[index] == value)
      return true;
  }

  return false;
}

/*
 * Store the current length at the old head, then decrement every positive
 * board value. The countdown naturally removes the tail at zero.
 */
static void advance_snake(struct game_state *game,
                          struct position previous_head) {
  set_board_tile(game, game->head.x, game->head.y, TILE_HEAD);
  set_board_tile(game, previous_head.x, previous_head.y, game->length);

  int tile_count = game->board_size * game->board_size;
  for (int index = 0; index < tile_count; index++) {
    if (game->board[index] > TILE_EMPTY)
      game->board[index]--;
  }
}

/*
 * A failed respawn is acceptable only when no empty tile remains; that state
 * is handled as a win after the update.
 */
static bool respawn_tile_if_needed(struct game_state *game, bool should_respawn,
                                   enum tile_type type) {
  if (!should_respawn)
    return true;

  if (place_tile(game, type))
    return true;

  return !board_contains_value(game, TILE_EMPTY);
}

/*
 * Resolve the destination tile, advance the snake, respawn consumed items,
 * and detect a full-board win.
 */
static enum update_status update_game(struct game_state *game,
                                      struct position previous_head) {
  int tile = board_tile(game, game->head.x, game->head.y);
  bool spawn_snack = false;
  bool spawn_poison = false;

  if (tile > TILE_EMPTY) {
    game->status = GAME_OVER_SNAKE;
  } else {
    switch (tile) {
    case TILE_OBSTACLE:
      game->status = GAME_OVER_OBSTACLE;
      break;
    case TILE_SNACK:
      game->length++;
      game->stats.snacks++;
      spawn_snack = true;
      break;
    case TILE_POISON:
      game->stats.poison++;

      if (game->length == 1) {
        game->status = GAME_OVER_POISON;
      } else {
        game->length--;
        spawn_poison = true;
      }
      break;
    case TILE_EMPTY:
    case TILE_HEAD:
      break;
    }
  }

  if (game->status != GAME_RUNNING)
    return UPDATE_OK;

  advance_snake(game, previous_head);

  if (!respawn_tile_if_needed(game, spawn_snack, TILE_SNACK))
    return UPDATE_SNACK_PLACEMENT_ERROR;

  if (!respawn_tile_if_needed(game, spawn_poison, TILE_POISON))
    return UPDATE_POISON_PLACEMENT_ERROR;

  if (!board_contains_value(game, TILE_EMPTY))
    game->status = GAME_WON;

  return UPDATE_OK;
}

/*---------- Program Entry Point ----------*/

int main(int argc, char *argv[]) {
  struct game_state game = {
      .board = NULL,
      .board_size = 0,
      .seed = 0,
      .head = {0, 0},
      .length = 1,
      .obstacle_count = 0,
      .stats = {0, 0, 0},
      .direction = DIRECTION_RIGHT,
      .status = GAME_RUNNING,
      .show_debug = false,
  };

  if (atexit(restore_terminal) != 0) {
    fprintf(stderr, ERROR_LABEL " Unable to register terminal cleanup.\n");
    return EXIT_TERMINAL_ERROR;
  }

  struct sigaction action = {0};
  action.sa_handler = handle_signal;
  sigemptyset(&action.sa_mask);

  if (sigaction(SIGINT, &action, NULL) == -1) {
    fprintf(stderr, ERROR_LABEL " Unable to install signal handler.\n");
    return EXIT_TERMINAL_ERROR;
  }

  int board_argc;
  int exit_status = determine_seed(argc, argv, &board_argc, &game.seed);
  if (exit_status != EXIT_OK)
    return exit_status;

  print_header(game.seed);

  exit_status = determine_board_size(board_argc, argv, &game.board_size);
  if (exit_status != EXIT_OK)
    return exit_status;

  game.board = calloc((size_t)game.board_size * (size_t)game.board_size,
                      sizeof(*game.board));
  if (game.board == NULL) {
    fprintf(stderr, ERROR_LABEL " Unable to allocate the game board.\n");
    return EXIT_MEMORY_ERROR;
  }

  srand(game.seed);

  if (!initialize_board(&game)) {
    fprintf(stderr, ERROR_LABEL " Unable to place all tiles on the board.\n");
    exit_status = EXIT_TILE_PLACEMENT_ERROR;
    goto cleanup;
  }

  if (!initialize_terminal()) {
    fprintf(stderr, ERROR_LABEL " Unable to enable direct input mode.\n");
    exit_status = EXIT_TERMINAL_ERROR;
    goto cleanup;
  }

  const char *feedback = NULL;

  /* Each turn starts from a complete render of the current state. */
  while (!interrupted) {
    print_header(game.seed);
    draw_game_board(&game);

    if (game.status != GAME_RUNNING) {
      print_game_status_message(game.status);
      break;
    }

    enum command_action action_result = COMMAND_REPROMPT;
    struct position previous_head = game.head;

    /* Help, invalid input, and escape sequences do not advance the game. */
    while (action_result == COMMAND_REPROMPT) {
      enum player_command command = PLAYER_COMMAND_INVALID;
      int input_status = prompt_for_command(&command, &feedback);

      if (input_status == -1) {
        fprintf(stderr, "\n" ERROR_LABEL " Unable to read a command.\n");
        exit_status = EXIT_COMMAND_ERROR;
        goto cleanup;
      }

      if (input_status == 0) {
        action_result = COMMAND_QUIT;
        break;
      }

      if (input_status == 2)
        continue;

      previous_head = game.head;
      action_result = apply_player_command(&game, command, &feedback);
    }

    if (action_result == COMMAND_QUIT)
      break;

    if (action_result == COMMAND_SKIP_TURN)
      continue;

    enum update_status update_result = update_game(&game, previous_head);
    if (update_result == UPDATE_SNACK_PLACEMENT_ERROR) {
      fprintf(stderr, ERROR_LABEL " Unable to place a snack.\n");
      exit_status = EXIT_TILE_PLACEMENT_ERROR;
      goto cleanup;
    }

    if (update_result == UPDATE_POISON_PLACEMENT_ERROR) {
      fprintf(stderr, ERROR_LABEL " Unable to place poison.\n");
      exit_status = EXIT_TILE_PLACEMENT_ERROR;
      goto cleanup;
    }
  }

  if (interrupted)
    exit_status = 128 + interrupted;

cleanup:
  /* Keep terminal restoration and board cleanup paired on every exit path. */
  restore_terminal();

  if (interrupted) {
    printf(COLOR_RESET "\n\n");
  } else if (exit_status == EXIT_OK) {
    print_game_summary(&game);
  }

  free(game.board);
  return exit_status;
}
