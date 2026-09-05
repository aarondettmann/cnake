/**
 * @file game.h
 * @brief Cnake - Game model and mechanics.
 */

#ifndef CNAKE_GAME_H
#define CNAKE_GAME_H

#include <stdbool.h>

#define DEFAULT_BOARD_SIZE 5
#define MIN_BOARD_SIZE 4
#define MAX_BOARD_SIZE 10

/* Reaching this score highlights the total and shows a one-off message. */
#define ANSWER_SCORE 42

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

/*
 * Negative values identify special tiles. Positive values are snake body
 * countdowns; the segment with value TILE_TAIL is the tail.
 */
enum tile_type {
  TILE_EMPTY = 0,
  TILE_TAIL = 1,
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
  int *board;
  int board_size;
  struct position head;
  int length;
  int obstacle_count;
  struct game_stats stats;
  enum direction direction;
  enum game_status status;
};

void game_initialize_state(struct game_state *game);
bool game_create(struct game_state *game, int board_size);
void game_destroy(struct game_state *game);
bool game_initialize_board(struct game_state *game);
int game_board_tile(const struct game_state *game, int x, int y);
void game_set_board_tile(struct game_state *game, int x, int y, int value);
void game_step(struct game_state *game, enum direction direction);
void game_grow(struct game_state *game, int amount);
int game_score(const struct game_state *game);

#endif
