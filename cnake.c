/************************************************************
 * File:    cnake.c                                         *
 * Date:    2014-10-16 - 27.12.2014-12-27 (2026-07-21)      *
 * Author:  Aaron Dettmann                                  *
 * Purpose: Cnake - Primitive ASCII snake game written in C *
 ************************************************************/

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/*---------- Version ----------*/
#define VERSION "0.4.1"
#define DEFAULT_BOARD_SIZE 6
#define MIN_BOARD_SIZE 4
#define MAX_BOARD_SIZE 10

enum board_size_parse_status {
  BOARD_SIZE_INVALID = 0,
  BOARD_SIZE_OUT_OF_RANGE,
  BOARD_SIZE_VALID
};

enum game_status {
  GAME_RUNNING = 0,
  GAME_WON,
  GAME_OVER_SNAKE,
  GAME_OVER_OBSTACLE,
  GAME_OVER_POISON
};

enum command_result {
  COMMAND_ERROR = -1,
  COMMAND_QUIT = 0,
  COMMAND_SKIP_TURN,
  COMMAND_MOVE
};

enum exit_code {
  EXIT_OK = 0,
  EXIT_INPUT_ERROR = 1,
  EXIT_MEMORY_ERROR = 2,
  EXIT_TILE_PLACEMENT_ERROR = 3,
  EXIT_TERMINAL_ERROR = 4,
  EXIT_COMMAND_ERROR = 5
};

enum tile_type {
  TILE_EMPTY = 0,
  TILE_HEAD = -1,
  TILE_OBSTACLE = -2,
  TILE_SNACK = -3,
  TILE_POISON = -4
};

/*---------- Colors ----------*/
#define RED "\x1B[31m" /* red */
#define GRN "\x1B[32m" /* green */
#define YEL "\x1B[33m" /* yellow */
#define BLU "\x1B[34m" /* blue */
#define RES "\033[0m"  /* reset */

/*---------- Escape Codes ----------*/
/*
   "\033[H"  // Move cursor to the upper-left corner of the screen
   "\033[0J" // Clear from the cursor to the end of the screen
*/

#define CLEAR "\033[H\033[0J"

static inline int *board_tile_ptr(int *board, int board_size, int x_pos,
                                  int y_pos) {
  return &board[y_pos * board_size + x_pos];
}

static inline int board_tile(const int *board, int board_size, int x_pos,
                             int y_pos) {
  return board[y_pos * board_size + x_pos];
}

/*---------- Function Prototypes ----------*/
void sig_handler(int signo);
bool init_terminal(void);
void restore_terminal(void);
int read_stdin_char(char *, int);
void discard_escape_sequence(void);
int read_command(char *);
void print_header(void);
void draw_border(int board_size);
void draw_game_board(const int *, int, char, char, char, char, char, char,
                     const int[3], int, int);
void print_game_status_message(enum game_status, char, char, char, char);
void help(void);
enum board_size_parse_status parse_board_size(const char *, int *);
bool apply_move_command(int, char, char *, int *, int *, const char **);
enum command_result handle_player_command(int, char *, char *, int *, int *,
                                          int *, int *, int *);
bool has_adjacent_obstacle(const int *, int, int, int);
bool is_spawn_position_valid(const int *, int, int, int, enum tile_type);
bool find_spawn_position(const int *, int, enum tile_type, int *, int *);
enum game_status handle_tile_effect(int, int *, int[3], int *, int *);
void advance_snake(int *, int, int, int, int, int, int);
bool respawn_tile_if_needed(int *, int, int, enum tile_type);
bool place_tile(int *, int, enum tile_type);
bool board_contains_value(const int *, int, int);

/*---------- Terminal State ----------*/
static struct termios original_terminal;
static int terminal_initialized = 0;
static volatile sig_atomic_t interrupted = 0;

/*============================*/
/*---------- MAIN() ----------*/
/*============================*/

int main(int argc, char *argv[]) {
  /*---------- Variable Declarations ----------*/
  int i,                    // Loop variables
      bs = 0,               // Board size --> side length of the playing field
      discard,              // Discard long board-size input
      x, y, x_old, y_old,   // Position coordinates
      length = 1,           // Length of the snake
      obstacle_count = 0,   // Number of obstacles
      target_obstacle_count, // Desired number of obstacles
      stats[3] = {0, 0, 0}, // Statistics [moves][snacks eaten][poison eaten]
      show = 0,             // Show/Hide debug output
      spawn_snack = 0,      // Respawn snack after tail update
      spawn_poison = 0;     // Respawn poison after tail update
  int *board = NULL;        // Game board
  char cmd = '\0',          // Command
      board_size_input[32], // Board-size prompt input
      *newline,             // Trailing newline position
      terra = ' ',          // Empty tile
      body = '=',           // Body segment
      head = '>',           // Head tile
      obstacle = '#',       // Obstacle tile
      snack = '*',          // Snack tile
      poison = '!';         // Poison tile
  struct sigaction action;
  enum board_size_parse_status board_size_status;
  enum command_result command_result;
  enum game_status game_status = GAME_RUNNING;

  x = y = x_old = y_old = 0;

  /*---------- Signal Handling ----------*/
  if (atexit(restore_terminal) != 0) {
    fprintf(stderr, "Error: Unable to register terminal cleanup.\n");
    return EXIT_TERMINAL_ERROR;
  }

  action.sa_handler = sig_handler;
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;

  if (sigaction(SIGINT, &action, NULL) == -1) {
    fprintf(stderr, "Error: Unable to install signal handler.\n");
    return EXIT_TERMINAL_ERROR;
  }

  /*---------- Determine Board Size ----------*/
  print_header();

  if (argc > 1 && parse_board_size(argv[1], &bs) != BOARD_SIZE_VALID)
    bs = 0;

  if (!bs) {
    do {
      printf("Choose the board size [%d-%d] (default: %d): ", MIN_BOARD_SIZE,
             MAX_BOARD_SIZE, DEFAULT_BOARD_SIZE);
      if (!fgets(board_size_input, sizeof(board_size_input), stdin))
        return EXIT_INPUT_ERROR;

      newline = strchr(board_size_input, '\n');
      if (newline == NULL)
        while ((discard = getchar()) != '\n' && discard != EOF)
          ;

      if (board_size_input[0] == '\n') {
        bs = DEFAULT_BOARD_SIZE;
        break;
      }

      board_size_status = parse_board_size(board_size_input, &bs);

      if (board_size_status == BOARD_SIZE_INVALID) {
        printf("Error: Enter a number from %d to %d, or press Enter for %d.\n",
               MIN_BOARD_SIZE, MAX_BOARD_SIZE, DEFAULT_BOARD_SIZE);
        continue;
      }

      if (board_size_status == BOARD_SIZE_OUT_OF_RANGE) {
        printf("Error: Board size must be between %d and %d.\n", MIN_BOARD_SIZE,
               MAX_BOARD_SIZE);
        continue;
      }
    } while (!bs);
  }

  board = (int *)calloc((size_t)bs * (size_t)bs, sizeof(*board));
  if (board == NULL)
    return EXIT_MEMORY_ERROR;

  /*---------- Seed rand() ----------*/
  srand(time(NULL));

  /*---------- Starting Position ----------*/
  *board_tile_ptr(board, bs, x, y) = TILE_HEAD;

  /*---------- Generate Random Positions ----------*/
  target_obstacle_count = (int)(((bs * bs) / 10) + 1);

  for (i = 0; i < target_obstacle_count; i++)
    if (place_tile(board, bs, TILE_OBSTACLE))
      obstacle_count++;
    else
      break;

  if (!place_tile(board, bs, TILE_POISON) ||
      !place_tile(board, bs, TILE_SNACK)) {
    fprintf(stderr, "Error: Unable to place all tiles on the board.\n");
    free(board);
    return EXIT_TILE_PLACEMENT_ERROR;
  }

  if (!init_terminal()) {
    fprintf(stderr, "Error: Unable to enable direct input mode.\n");
    free(board);
    return EXIT_TERMINAL_ERROR;
  }

  /*---------- Start Main Game Loop ----------*/
  print_header();
  printf("==> Board: %dx%d\n\n", bs, bs);

  while (1) {
    if (interrupted)
      break;

    draw_game_board(board, bs, terra, body, head, obstacle, snack, poison,
                    stats, length, show);

    if (game_status != GAME_RUNNING) {
      print_game_status_message(game_status, body, head, obstacle, poison);
      break;
    }

    x_old = x;
    y_old = y;

    command_result = handle_player_command(bs, &cmd, &head, &x, &y, &length,
                                           &show, &stats[0]);
    if (command_result == COMMAND_ERROR) {
      free(board);
      return EXIT_COMMAND_ERROR;
    }

    if (interrupted || command_result == COMMAND_QUIT)
      break;

    if (command_result == COMMAND_SKIP_TURN)
      continue;

    game_status = handle_tile_effect(board_tile(board, bs, x, y), &length,
                                     stats, &spawn_snack, &spawn_poison);
    if (game_status != GAME_RUNNING)
      continue;

    advance_snake(board, bs, x, y, x_old, y_old, length);

    if (!respawn_tile_if_needed(board, bs, spawn_snack, TILE_SNACK)) {
      fprintf(stderr, "Error: Unable to place a snack.\n");
      free(board);
      return EXIT_TILE_PLACEMENT_ERROR;
    }

    if (!respawn_tile_if_needed(board, bs, spawn_poison, TILE_POISON)) {
      fprintf(stderr, "Error: Unable to place poison.\n");
      free(board);
      return EXIT_TILE_PLACEMENT_ERROR;
    }

    if (!board_contains_value(board, bs, TILE_EMPTY)) {
      game_status = GAME_WON;
      continue;
    }

  } /* End of main game loop */

  restore_terminal();

  if (interrupted) {
    free(board);
    printf(RES "\n\n");
    return 128 + interrupted;
  }

  /*---------- Game Summary ----------*/
  printf("\nMoves:\t\t%4d\n", stats[0]);
  printf("Snacks:\t\t%4d\n", stats[1]);
  printf("Poison:\t%4d\n", stats[2]);
  printf("Length:\t\t%d/%d+\n\n", length, (bs * bs - obstacle_count - 1));

  free(board);
  return EXIT_OK;

} /* End of main() */

/*================================*/
/*---------- Functions -----------*/
/*================================*/

/*---------- Signal Handling ----------*/
void sig_handler(int signo) { interrupted = signo; }

/*---------- Enable Direct Terminal Input ----------*/
bool init_terminal(void) {
  struct termios terminal;

  if (!isatty(STDIN_FILENO))
    return false;

  if (tcgetattr(STDIN_FILENO, &original_terminal) == -1)
    return false;

  terminal = original_terminal;
  terminal.c_lflag &= ~(ICANON | ECHO);
  terminal.c_cc[VMIN] = 1;
  terminal.c_cc[VTIME] = 0;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &terminal) == -1)
    return false;

  terminal_initialized = 1;
  return true;
}

/*---------- Restore Terminal Settings ----------*/
void restore_terminal(void) {
  if (!terminal_initialized)
    return;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_terminal) == -1)
    fprintf(stderr, "Warning: Unable to restore terminal settings.\n");

  terminal_initialized = 0;
}

/*---------- Read One Byte from Standard Input ----------*/
int read_stdin_char(char *cmd, int timeout_ms) {
  fd_set readfds;
  struct timeval timeout;
  ssize_t bytes_read;
  int ready;

  while (1) {
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    if (timeout_ms < 0)
      ready = select(STDIN_FILENO + 1, &readfds, NULL, NULL, NULL);
    else {
      timeout.tv_sec = timeout_ms / 1000;
      timeout.tv_usec = (timeout_ms % 1000) * 1000;
      ready = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout);
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

    bytes_read = read(STDIN_FILENO, cmd, 1);

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

/*---------- Discard an Unsupported Escape Sequence ----------*/
void discard_escape_sequence(void) {
  char input;
  int status;

  status = read_stdin_char(&input, 10);
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

/*---------- Read One Command ----------*/
int read_command(char *cmd) {
  int status;

  do {
    status = read_stdin_char(cmd, -1);
    if (status != 1)
      return status;
  } while (*cmd == '\n' || *cmd == '\r');

  if (*cmd == '\033') {
    discard_escape_sequence();
    return 2;
  }

  return 1;
}

/*---------- Header ----------*/
void print_header(void) {
  printf(CLEAR "=========== CNAKE %s ===========\n\n", VERSION);
}

/*---------- Draw Outer Board Border ----------*/
void draw_border(int board_size) {
  int n;

  printf(GRN " |");

  for (n = 0; n <= 2 * board_size; n++)
    printf("-");

  printf("|\n" RES);
}

/*---------- Draw the Game Board ----------*/
void draw_game_board(const int *board, int board_size, char terra, char body,
                     char head, char obstacle, char snack, char poison,
                     const int stats[3], int length, int show) {
  int i, j, tile_value;

  draw_border(board_size);

  for (i = 0; i < board_size; i++) {
    printf(GRN " | ");

    for (j = 0; j < board_size; j++) {
      tile_value = board_tile(board, board_size, j, i);

      if (tile_value > TILE_EMPTY)
        printf(RED "%c ", body);
      else if (tile_value == TILE_EMPTY)
        printf(RES "%c ", terra);
      else if (tile_value == TILE_HEAD)
        printf(RED "%c ", head);
      else if (tile_value == TILE_OBSTACLE)
        printf(BLU "%c ", obstacle);
      else if (tile_value == TILE_SNACK)
        printf(YEL "%c ", snack);
      else if (tile_value == TILE_POISON)
        printf(GRN "%c ", poison);
    }

    printf(GRN "|\t");

    if (i == 0)
      printf(RED "%c%c>\t" RES "Snake     (%d)", body, body, length);
    else if (i == 1)
      printf(YEL "%c  \t" RES "Snack     (%d)", snack, stats[1]);
    else if (i == 2)
      printf(GRN "%c  \t" RES "Poison    (%d)", poison, stats[2]);
    else if (i == 3)
      printf(BLU "%c  \t" RES "Obstacle", obstacle);

    printf(RES "\n");
  }

  draw_border(board_size);

  if (show) {
    printf("\n");

    for (i = 0; i < board_size; i++) {
      printf("|");

      for (j = 0; j < board_size; j++)
        printf("%2d ", board_tile(board, board_size, j, i));

      printf("|\n");
    }
  }
}

/*---------- Print the Game Status Message ----------*/
void print_game_status_message(enum game_status game_status, char body,
                               char head, char obstacle, char poison) {
  if (game_status == GAME_WON) {
    printf("\nYOU WIN!\n");
    return;
  }

  if (game_status == GAME_OVER_SNAKE)
    printf("\nSnake!" RED " %c%c> " RES, body, body, head);
  else if (game_status == GAME_OVER_OBSTACLE)
    printf("\nObstacle!" BLU " %c " RES, obstacle);
  else if (game_status == GAME_OVER_POISON)
    printf("\nPoison!" GRN " %c " RES, poison);

  printf("~ GAME OVER!\n");
}

/*---------- Help ----------*/
void help(void) {
  printf("\nKeys (vim-style):\n"
         "h: move left\n"
         "j: move down\n"
         "k: move up\n"
         "l: move right\n"
         "?: show help\n"
         "c: length +5 (cheat)\n"
         "v: toggle debug view\n"
         "q: quit\n");
}

/*---------- Parse the Board Size ----------*/
enum board_size_parse_status parse_board_size(const char *input,
                                              int *board_size) {
  char *endptr;
  long parsed_bs;

  errno = 0;
  parsed_bs = strtol(input, &endptr, 10);

  if (errno != 0 || endptr == input)
    return BOARD_SIZE_INVALID;

  while (*endptr != '\0' && isspace((unsigned char)*endptr))
    endptr++;

  if (*endptr != '\0')
    return BOARD_SIZE_INVALID;

  if (parsed_bs < MIN_BOARD_SIZE || parsed_bs > MAX_BOARD_SIZE)
    return BOARD_SIZE_OUT_OF_RANGE;

  *board_size = (int)parsed_bs;
  return BOARD_SIZE_VALID;
}

bool apply_move_command(int board_size, char cmd, char *head, int *x, int *y,
                        const char **direction_label) {
  int dx = 0;
  int dy = 0;
  char next_head = '\0';

  switch (cmd) {
  case 'k':
    dy = -1;
    next_head = '^';
    *direction_label = "Up";
    break;
  case 'j':
    dy = 1;
    next_head = 'v';
    *direction_label = "Down";
    break;
  case 'h':
    dx = -1;
    next_head = '<';
    *direction_label = "Left";
    break;
  case 'l':
    dx = 1;
    next_head = '>';
    *direction_label = "Right";
    break;
  default:
    return false;
  }

  *head = next_head;
  *x = (*x + dx + board_size) % board_size;
  *y = (*y + dy + board_size) % board_size;
  return true;
}

/*---------- Read and Apply One Player Command ----------*/
enum command_result handle_player_command(int board_size, char *cmd, char *head,
                                          int *x, int *y, int *length,
                                          int *show, int *move_count) {
  int cmd_status;
  const char *direction_label;

  do {
    printf("\nPress a key [?]: ");
    fflush(stdout);

    cmd_status = read_command(cmd);
    if (cmd_status == -1) {
      fprintf(stderr, "\nError: Unable to read a command.\n");
      return COMMAND_ERROR;
    }

    if (cmd_status == 2)
      continue;

    if (cmd_status == 0)
      return COMMAND_QUIT;

    if (*cmd == '?') {
      help();
      continue;
    } else if (apply_move_command(board_size, *cmd, head, x, y,
                                  &direction_label)) {
      print_header();
      printf("==> %s!\n\n", direction_label);
    } else if (*cmd == 'c') { /* CHEAT CODE --> FOR TESTING */
      print_header();
      printf("==> Length +5!\n\n");
      *length += 5;
      return COMMAND_SKIP_TURN;
    } else if (*cmd == 'v') {
      print_header();
      printf("==> Toggle Debug View!\n\n");
      *show = !*show;
      return COMMAND_SKIP_TURN;
    } else if (*cmd == 'q')
      return COMMAND_QUIT;
    else {
      printf("\nError: Unknown command!\n");
      continue;
    }

    *move_count += 1;
    return COMMAND_MOVE;

  } while (1);
}

/*---------- Check Whether an Obstacle Would Spawn Too Close ----------*/
bool has_adjacent_obstacle(const int *board, int board_size, int x_pos,
                           int y_pos) {
  int x_check, y_check;

  for (y_check = y_pos - 1; y_check <= y_pos + 1; y_check++)
    for (x_check = x_pos - 1; x_check <= x_pos + 1; x_check++) {
      if (y_check < 0 || y_check >= board_size || x_check < 0 ||
          x_check >= board_size)
        continue;

      if (y_check == y_pos && x_check == x_pos)
        continue;

      if (board_tile(board, board_size, x_check, y_check) == TILE_OBSTACLE)
        return true;
    }

  return false;
}

/*---------- Check Whether a Spawn Position Is Valid ----------*/
bool is_spawn_position_valid(const int *board, int board_size, int x_pos,
                             int y_pos, enum tile_type type) {
  if (board_tile(board, board_size, x_pos, y_pos) != TILE_EMPTY)
    return false;

  if (type == TILE_OBSTACLE &&
      has_adjacent_obstacle(board, board_size, x_pos, y_pos))
    return false;

  return true;
}

/*---------- Find a Valid Position for a Tile ----------*/
bool find_spawn_position(const int *board, int board_size, enum tile_type type,
                         int *x_out, int *y_out) {
  int i, index, total, x_pos, y_pos, start;

  total = board_size * board_size;
  start = rand() % total;

  for (i = 0; i < total; i++) {
    index = (start + i) % total;
    y_pos = index / board_size;
    x_pos = index % board_size;

    if (is_spawn_position_valid(board, board_size, x_pos, y_pos, type)) {
      *x_out = x_pos;
      *y_out = y_pos;
      return true;
    }
  }

  return false;
}

/*---------- Apply the Current Tile Effect ----------*/
enum game_status handle_tile_effect(int tile_value, int *length, int stats[3],
                                    int *spawn_snack, int *spawn_poison) {
  *spawn_snack = 0;
  *spawn_poison = 0;

  if (tile_value > TILE_EMPTY)
    return GAME_OVER_SNAKE;

  if (tile_value == TILE_OBSTACLE)
    return GAME_OVER_OBSTACLE;

  if (tile_value == TILE_SNACK) {
    *length += 1;
    stats[1] += 1; /* +1 snack */
    *spawn_snack = 1;
    return GAME_RUNNING;
  }

  if (tile_value == TILE_POISON) {
    stats[2] += 1; /* +1 poison */

    if (*length > 1) {
      *length -= 1;
      *spawn_poison = 1;
      return GAME_RUNNING;
    }

    return GAME_OVER_POISON;
  }

  return GAME_RUNNING;
}

/*---------- Advance the Snake ----------*/
void advance_snake(int *board, int board_size, int x_pos, int y_pos, int x_previous,
                   int y_previous, int length) {
  int i, j;

  *board_tile_ptr(board, board_size, x_pos, y_pos) = TILE_HEAD; /* Head position */
  *board_tile_ptr(board, board_size, x_previous, y_previous) =
      length; /* Store the snake length at the previous head position */

  for (i = 0; i < board_size; i++)
    for (j = 0; j < board_size; j++)
      if (board_tile(board, board_size, j, i) > TILE_EMPTY)
        /* All values >0 are snake body segments */
        *board_tile_ptr(board, board_size, j, i) -= 1;
}

/*---------- Respawn a Consumed Tile ----------*/
bool respawn_tile_if_needed(int *board, int board_size, int should_respawn,
                            enum tile_type type) {
  return !should_respawn || place_tile(board, board_size, type) ||
         !board_contains_value(board, board_size, TILE_EMPTY);
}

/*---------- Place a Tile on the Board ----------*/
bool place_tile(int *board, int board_size, enum tile_type type) {
  int x_pos, y_pos;

  if (!find_spawn_position(board, board_size, type, &x_pos, &y_pos))
    return false;

  *board_tile_ptr(board, board_size, x_pos, y_pos) = type;
  return true;
}

/*---------- Search for a Value in the Matrix ----------*/
bool board_contains_value(const int *board, int board_size, int value) {
  int i, j;

  for (i = 0; i < board_size; i++)
    for (j = 0; j < board_size; j++)
      if (board_tile(board, board_size, j, i) == value)
        return true; /* Value found */

  return false;
}

/*----- EOF -----*/
