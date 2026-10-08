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
 * Body segments hold the values 1 to the number of segments behind the head,
 * so the largest board value is that number.
 */
static int body_segment_count(const struct game_state *game) {
  int tile_count = game->board_size * game->board_size;
  int count = 0;

  for (int index = 0; index < tile_count; index++) {
    if (game->board[index] > count)
      count = game->board[index];
  }

  return count;
}

/*
 * The body on the board gains at most one segment per step, so an increase in
 * length by more than one, as from game_grow, is drawn over several steps.
 * During such a step the tail keeps its tile. Poison eaten during these steps
 * lowers the target, but the body still gains a segment while it remains below
 * the target.
 */
static int next_body_segment_count(const struct game_state *game, int count) {
  int target = game->length - 1;
  return target > count + 1 ? count + 1 : target;
}

/*
 * Move the body one tile towards the head and bring the number of segments on
 * the board to next_body_segment_count. Every segment is lowered by the number
 * of tiles that leave the tail end: 1 for a plain move, 0 after a snack and 2
 * after poison. A segment lowered to zero or below is removed.
 */
static void advance_snake(struct game_state *game,
                          struct position previous_head) {
  int count = body_segment_count(game);
  int next_count = next_body_segment_count(game, count);
  int decrement = count + 1 - next_count;

  int tile_count = game->board_size * game->board_size;
  for (int index = 0; index < tile_count; index++) {
    int tile = game->board[index];
    if (tile > TILE_EMPTY)
      game->board[index] = tile > decrement ? tile - decrement : TILE_EMPTY;
  }

  game_set_board_tile(game, game->head.x, game->head.y, TILE_HEAD);
  game_set_board_tile(game, previous_head.x, previous_head.y, next_count);
}

/*
 * A body segment occupies its tile after the next step unless it is the tail
 * and the tail moves. The tail stays only while the body is still growing
 * towards game->length.
 */
static bool is_body_collision(const struct game_state *game, int tile) {
  if (tile == TILE_TAIL) {
    int count = body_segment_count(game);
    return next_body_segment_count(game, count) > count;
  }

  return tile > TILE_TAIL;
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
 * the segment is a tail that vacates its tile in the same step.
 */
static bool is_reversal_into_body(const struct game_state *game,
                                  enum direction direction) {
  if (direction != opposite_direction(game->direction))
    return false;

  struct position target = adjacent_position(game, game->head, direction);
  return is_body_collision(game, game_board_tile(game, target.x, target.y));
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

  if (is_body_collision(game, tile)) {
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
