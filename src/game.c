#include "game.h"

#include <assert.h>
#include <limits.h>
#include <stdlib.h>

void game_initialize_state(struct game_state *game) {
  *game = (struct game_state){
      .board = NULL,
      .board_size = 0,
      .head = {0, 0},
      .length = 1,
      .obstacle_count = 0,
      .stats = {0, 0, 0},
      .direction = DIRECTION_RIGHT,
      .status = GAME_RUNNING,
  };
}

bool game_create(struct game_state *game, int board_size) {
  game_initialize_state(game);
  game->board_size = board_size;
  game->board =
      calloc((size_t)board_size * (size_t)board_size, sizeof(*game->board));
  return game->board != NULL;
}

void game_destroy(struct game_state *game) {
  free(game->board);
  game_initialize_state(game);
}

static size_t board_index(const struct game_state *game, int x, int y) {
  return (size_t)y * (size_t)game->board_size + (size_t)x;
}

int game_board_tile(const struct game_state *game, int x, int y) {
  return game->board[board_index(game, x, y)];
}

void game_set_board_tile(struct game_state *game, int x, int y, int value) {
  game->board[board_index(game, x, y)] = value;
}

static bool has_adjacent_obstacle(const struct game_state *game, int x, int y) {
  for (int nearby_y = y - 1; nearby_y <= y + 1; nearby_y++) {
    for (int nearby_x = x - 1; nearby_x <= x + 1; nearby_x++) {
      if (nearby_y < 0 || nearby_y >= game->board_size || nearby_x < 0 ||
          nearby_x >= game->board_size)
        continue;

      if (nearby_x == x && nearby_y == y)
        continue;

      if (game_board_tile(game, nearby_x, nearby_y) == TILE_OBSTACLE)
        return true;
    }
  }

  return false;
}

static bool is_spawn_position_valid(const struct game_state *game, int x, int y,
                                    enum tile_type type) {
  if (game_board_tile(game, x, y) != TILE_EMPTY)
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

  game_set_board_tile(game, position.x, position.y, type);
  return true;
}

bool game_initialize_board(struct game_state *game) {
  game_set_board_tile(game, game->head.x, game->head.y, TILE_HEAD);

  int target_obstacle_count = game->board_size * game->board_size / 10 + 1;

  for (int count = 0; count < target_obstacle_count; count++) {
    if (!place_tile(game, TILE_OBSTACLE))
      break;

    game->obstacle_count++;
  }

  return place_tile(game, TILE_POISON) && place_tile(game, TILE_SNACK);
}

/*
 * Store the current length at the old head, then decrement every positive
 * board value. The countdown naturally removes the tail at zero.
 */
static void advance_snake(struct game_state *game,
                          struct position previous_head) {
  game_set_board_tile(game, game->head.x, game->head.y, TILE_HEAD);
  game_set_board_tile(game, previous_head.x, previous_head.y, game->length);

  int tile_count = game->board_size * game->board_size;
  for (int index = 0; index < tile_count; index++) {
    if (game->board[index] > TILE_EMPTY)
      game->board[index]--;
  }
}

/*
 * A failed respawn is acceptable only when no empty tile remains; that state
 * is handled as a win at the end of the step.
 */
static void respawn_tile_if_needed(struct game_state *game, bool should_respawn,
                                   enum tile_type type) {
  if (!should_respawn)
    return;

  if (!place_tile(game, type))
    assert(game_empty_tile_count(game) == 0);
}

static struct position adjacent_position(const struct game_state *game,
                                         struct position from,
                                         enum direction direction) {
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

  return (struct position){
      .x = (from.x + delta_x + game->board_size) % game->board_size,
      .y = (from.y + delta_y + game->board_size) % game->board_size,
  };
}

static enum direction opposite_direction(enum direction direction) {
  switch (direction) {
  case DIRECTION_UP:
    return DIRECTION_DOWN;
  case DIRECTION_DOWN:
    return DIRECTION_UP;
  case DIRECTION_LEFT:
    return DIRECTION_RIGHT;
  case DIRECTION_RIGHT:
    return DIRECTION_LEFT;
  }

  return direction;
}

/*
 * The previous move led from the segment behind the head to the head, so the
 * opposite direction leads back onto that segment. Such a move is fatal unless
 * the segment is the tail, which vacates its tile in the same step.
 */
static bool is_reversal_into_body(const struct game_state *game,
                                  enum direction direction) {
  if (direction != opposite_direction(game->direction))
    return false;

  struct position target = adjacent_position(game, game->head, direction);
  return game_board_tile(game, target.x, target.y) > TILE_TAIL;
}

static void move_head(struct game_state *game, enum direction direction) {
  game->direction = direction;
  game->head = adjacent_position(game, game->head, direction);
  game->stats.moves++;
}

void game_grow(struct game_state *game, int amount) { game->length += amount; }

/*
 * A reversal into the body is rejected without changing the state, so that a
 * single mistyped key does not end the game. Every other move is applied.
 */
bool game_step(struct game_state *game, enum direction direction) {
  if (is_reversal_into_body(game, direction))
    return false;

  struct position previous_head = game->head;
  move_head(game, direction);

  int tile = game_board_tile(game, game->head.x, game->head.y);
  bool spawn_snack = false;
  bool spawn_poison = false;

  /* The tail vacates its tile during this step, so only the rest of the body
   * is a collision. */
  if (tile > TILE_TAIL) {
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
    case TILE_TAIL:
    case TILE_HEAD:
      break;
    }
  }

  if (game->status != GAME_RUNNING)
    return true;

  advance_snake(game, previous_head);
  respawn_tile_if_needed(game, spawn_snack, TILE_SNACK);
  respawn_tile_if_needed(game, spawn_poison, TILE_POISON);

  if (game_empty_tile_count(game) == 0)
    game->status = GAME_WON;

  return true;
}

int game_empty_tile_count(const struct game_state *game) {
  int tile_count = game->board_size * game->board_size;
  int empty_count = 0;

  for (int index = 0; index < tile_count; index++) {
    if (game->board[index] == TILE_EMPTY)
      empty_count++;
  }

  return empty_count;
}

int game_score(const struct game_state *game) {
  int moves = game->stats.moves;
  int snacks = game->stats.snacks;
  int poison = game->stats.poison;

  if (moves <= 0 || snacks <= 0)
    return 0;

  long long bonus_numerator = 99LL * snacks;
  long long bonus_denominator = 10LL * moves;
  long long bonus =
      (bonus_numerator + bonus_denominator / 2) / bonus_denominator;
  long long score = 10LL * snacks - 5LL * poison + bonus;
  if (score < 0)
    return 0;
  return score > INT_MAX ? INT_MAX : (int)score;
}
