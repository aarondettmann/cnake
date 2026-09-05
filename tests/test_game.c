#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "test.h"

static bool create_game(struct game_state *game, int board_size) {
  if (game_create(game, board_size))
    return true;

  fprintf(stderr, "Unable to allocate %dx%d test board.\n", board_size,
          board_size);
  return false;
}

static void clear_fixture(struct game_state *game) {
  size_t tile_count = (size_t)game->board_size * (size_t)game->board_size;
  memset(game->board, 0, tile_count * sizeof(*game->board));
  game->head = (struct position){0, 0};
  game->length = 1;
  game->obstacle_count = 0;
  game->stats = (struct game_stats){0, 0, 0};
  game->direction = DIRECTION_RIGHT;
  game->status = GAME_RUNNING;
}

static void place_head(struct game_state *game, int x, int y) {
  game->head = (struct position){x, y};
  game_set_board_tile(game, x, y, TILE_HEAD);
}

static int count_tiles(const struct game_state *game, int value) {
  int count = 0;

  for (int y = 0; y < game->board_size; y++) {
    for (int x = 0; x < game->board_size; x++) {
      if (game_board_tile(game, x, y) == value)
        count++;
    }
  }

  return count;
}

static int count_positive_tiles(const struct game_state *game) {
  int count = 0;

  for (int y = 0; y < game->board_size; y++) {
    for (int x = 0; x < game->board_size; x++) {
      if (game_board_tile(game, x, y) > TILE_EMPTY)
        count++;
    }
  }

  return count;
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

static bool board_has_only_known_tiles(const struct game_state *game) {
  for (int y = 0; y < game->board_size; y++) {
    for (int x = 0; x < game->board_size; x++) {
      int tile = game_board_tile(game, x, y);
      if (tile != TILE_EMPTY && tile != TILE_HEAD && tile != TILE_OBSTACLE &&
          tile != TILE_SNACK && tile != TILE_POISON)
        return false;
    }
  }

  return true;
}

static bool obstacles_are_separated(const struct game_state *game) {
  for (int y = 0; y < game->board_size; y++) {
    for (int x = 0; x < game->board_size; x++) {
      if (game_board_tile(game, x, y) == TILE_OBSTACLE &&
          has_adjacent_obstacle(game, x, y))
        return false;
    }
  }

  return true;
}

static void test_initialization_invariants(void) {
  for (int board_size = MIN_BOARD_SIZE; board_size <= MAX_BOARD_SIZE;
       board_size++) {
    struct game_state game;
    if (!create_game(&game, board_size))
      exit(EXIT_FAILURE);

    srand(1729u + (unsigned int)board_size);
    CHECK(game_initialize_board(&game));
    CHECK_EQ(game.head.x, 0);
    CHECK_EQ(game.head.y, 0);
    CHECK_EQ(game_board_tile(&game, 0, 0), TILE_HEAD);
    CHECK_EQ(count_tiles(&game, TILE_HEAD), 1);
    CHECK_EQ(count_tiles(&game, TILE_SNACK), 1);
    CHECK_EQ(count_tiles(&game, TILE_POISON), 1);
    CHECK_EQ(count_positive_tiles(&game), 0);
    CHECK_EQ(count_tiles(&game, TILE_OBSTACLE), game.obstacle_count);
    CHECK(game.obstacle_count <= board_size * board_size / 10 + 1);
    CHECK(board_has_only_known_tiles(&game));
    CHECK(obstacles_are_separated(&game));

    game_destroy(&game);
  }
}

static void test_direction_moves_and_wrap(void) {
  struct game_state game;
  if (!create_game(&game, 4))
    exit(EXIT_FAILURE);

  struct {
    enum direction direction;
    struct position start;
    struct position end;
  } cases[] = {
      {DIRECTION_UP, {0, 0}, {0, 3}},
      {DIRECTION_DOWN, {0, 3}, {0, 0}},
      {DIRECTION_LEFT, {0, 0}, {3, 0}},
      {DIRECTION_RIGHT, {3, 0}, {0, 0}},
  };

  for (size_t index = 0; index < sizeof(cases) / sizeof(*cases); index++) {
    clear_fixture(&game);
    place_head(&game, cases[index].start.x, cases[index].start.y);

    game_step(&game, cases[index].direction);

    CHECK_EQ(game.stats.moves, 1);
    CHECK_EQ(game.head.x, cases[index].end.x);
    CHECK_EQ(game.head.y, cases[index].end.y);
    CHECK_EQ(game.direction, cases[index].direction);
    CHECK_EQ(game.status, GAME_RUNNING);
    CHECK_EQ(game_board_tile(&game, cases[index].start.x, cases[index].start.y),
             TILE_EMPTY);
    CHECK_EQ(game_board_tile(&game, cases[index].end.x, cases[index].end.y),
             TILE_HEAD);
    CHECK_EQ(count_positive_tiles(&game), 0);
  }

  game_destroy(&game);
}

static void test_tail_countdown_and_removal(void) {
  struct game_state game;
  if (!create_game(&game, 4))
    exit(EXIT_FAILURE);

  clear_fixture(&game);
  game.length = 3;
  place_head(&game, 2, 0);
  game_set_board_tile(&game, 1, 0, 2);
  game_set_board_tile(&game, 0, 0, 1);

  game_step(&game, DIRECTION_RIGHT);

  CHECK_EQ(game.head.x, 3);
  CHECK_EQ(game.head.y, 0);
  CHECK_EQ(game_board_tile(&game, 3, 0), TILE_HEAD);
  CHECK_EQ(game_board_tile(&game, 2, 0), 2);
  CHECK_EQ(game_board_tile(&game, 1, 0), 1);
  CHECK_EQ(game_board_tile(&game, 0, 0), TILE_EMPTY);

  game_destroy(&game);
}

static void test_self_collision(void) {
  struct game_state game;
  if (!create_game(&game, 4))
    exit(EXIT_FAILURE);

  clear_fixture(&game);
  game.length = 3;
  place_head(&game, 1, 1);
  game_set_board_tile(&game, 2, 1, 2);
  game_set_board_tile(&game, 3, 1, TILE_TAIL);

  game_step(&game, DIRECTION_RIGHT);

  CHECK_EQ(game.status, GAME_OVER_SNAKE);
  CHECK_EQ(game_board_tile(&game, 1, 1), TILE_HEAD);
  CHECK_EQ(game_board_tile(&game, 2, 1), 2);
  CHECK_EQ(game_board_tile(&game, 3, 1), TILE_TAIL);

  game_destroy(&game);
}

/* The tail vacates its tile in the same step, so moving onto it is safe. */
static void test_moving_into_tail_is_allowed(void) {
  struct game_state game;
  if (!create_game(&game, 4))
    exit(EXIT_FAILURE);

  clear_fixture(&game);
  game.length = 4;
  place_head(&game, 0, 1);
  game_set_board_tile(&game, 1, 1, 3);
  game_set_board_tile(&game, 1, 0, 2);
  game_set_board_tile(&game, 0, 0, TILE_TAIL);

  game_step(&game, DIRECTION_UP);

  CHECK_EQ(game.status, GAME_RUNNING);
  CHECK_EQ(game.length, 4);
  CHECK_EQ(game.head.x, 0);
  CHECK_EQ(game.head.y, 0);
  CHECK_EQ(game_board_tile(&game, 0, 0), TILE_HEAD);
  CHECK_EQ(game_board_tile(&game, 0, 1), 3);
  CHECK_EQ(game_board_tile(&game, 1, 1), 2);
  CHECK_EQ(game_board_tile(&game, 1, 0), TILE_TAIL);
  CHECK_EQ(count_positive_tiles(&game), 3);

  /* A snake of length two may reverse onto its tail. */
  clear_fixture(&game);
  game.length = 2;
  place_head(&game, 1, 0);
  game_set_board_tile(&game, 0, 0, TILE_TAIL);

  game_step(&game, DIRECTION_LEFT);

  CHECK_EQ(game.status, GAME_RUNNING);
  CHECK_EQ(game_board_tile(&game, 0, 0), TILE_HEAD);
  CHECK_EQ(game_board_tile(&game, 1, 0), TILE_TAIL);

  game_destroy(&game);
}

static void test_obstacle_collision(void) {
  struct game_state game;
  if (!create_game(&game, 4))
    exit(EXIT_FAILURE);

  clear_fixture(&game);
  place_head(&game, 1, 1);
  game_set_board_tile(&game, 2, 1, TILE_OBSTACLE);

  game_step(&game, DIRECTION_RIGHT);

  CHECK_EQ(game.status, GAME_OVER_OBSTACLE);

  game_destroy(&game);
}

static void test_fatal_poison_collision(void) {
  struct game_state game;
  if (!create_game(&game, 4))
    exit(EXIT_FAILURE);

  clear_fixture(&game);
  place_head(&game, 1, 1);
  game_set_board_tile(&game, 2, 1, TILE_POISON);

  game_step(&game, DIRECTION_RIGHT);

  CHECK_EQ(game.status, GAME_OVER_POISON);
  CHECK_EQ(game.length, 1);
  CHECK_EQ(game.stats.poison, 1);

  game_destroy(&game);
}

static void test_snack_growth_and_respawn(void) {
  struct game_state game;
  if (!create_game(&game, 4))
    exit(EXIT_FAILURE);

  clear_fixture(&game);
  place_head(&game, 0, 0);
  game_set_board_tile(&game, 1, 0, TILE_SNACK);
  game_set_board_tile(&game, 3, 3, TILE_POISON);

  srand(7u);
  game_step(&game, DIRECTION_RIGHT);

  CHECK_EQ(game.status, GAME_RUNNING);
  CHECK_EQ(game.length, 2);
  CHECK_EQ(game.stats.snacks, 1);
  CHECK_EQ(game_board_tile(&game, 1, 0), TILE_HEAD);
  CHECK_EQ(game_board_tile(&game, 0, 0), 1);
  CHECK_EQ(count_tiles(&game, TILE_SNACK), 1);
  CHECK_EQ(count_tiles(&game, TILE_POISON), 1);
  CHECK_EQ(count_positive_tiles(&game), 1);

  game_destroy(&game);
}

static void test_nonfatal_poison_shrink_and_respawn(void) {
  struct game_state game;
  if (!create_game(&game, 4))
    exit(EXIT_FAILURE);

  clear_fixture(&game);
  game.length = 2;
  place_head(&game, 1, 0);
  game_set_board_tile(&game, 0, 0, 1);
  game_set_board_tile(&game, 2, 0, TILE_POISON);
  game_set_board_tile(&game, 3, 3, TILE_SNACK);

  srand(11u);
  game_step(&game, DIRECTION_RIGHT);

  CHECK_EQ(game.status, GAME_RUNNING);
  CHECK_EQ(game.length, 1);
  CHECK_EQ(game.stats.poison, 1);
  CHECK_EQ(game_board_tile(&game, 2, 0), TILE_HEAD);
  CHECK_EQ(game_board_tile(&game, 1, 0), TILE_EMPTY);
  CHECK_EQ(count_tiles(&game, TILE_POISON), 1);
  CHECK_EQ(count_tiles(&game, TILE_SNACK), 1);
  CHECK_EQ(count_positive_tiles(&game), 0);

  game_destroy(&game);
}

static void test_manual_growth(void) {
  struct game_state game;
  if (!create_game(&game, 4))
    exit(EXIT_FAILURE);

  clear_fixture(&game);
  place_head(&game, 0, 0);
  game_grow(&game, 5);

  CHECK_EQ(game.length, 6);

  game_step(&game, DIRECTION_RIGHT);

  CHECK_EQ(game_board_tile(&game, 1, 0), TILE_HEAD);
  CHECK_EQ(game_board_tile(&game, 0, 0), 5);
  CHECK_EQ(count_positive_tiles(&game), 1);

  game_destroy(&game);
}

static void test_full_board_win_detection(void) {
  struct game_state game;
  if (!create_game(&game, 4))
    exit(EXIT_FAILURE);

  clear_fixture(&game);
  place_head(&game, 0, 0);
  game_set_board_tile(&game, 1, 0, TILE_SNACK);
  game_set_board_tile(&game, 2, 0, TILE_POISON);

  for (int y = 0; y < game.board_size; y++) {
    for (int x = 0; x < game.board_size; x++) {
      if ((x == 0 && y == 0) || (x == 1 && y == 0) || (x == 2 && y == 0))
        continue;

      game_set_board_tile(&game, x, y, TILE_OBSTACLE);
      game.obstacle_count++;
    }
  }

  game_step(&game, DIRECTION_RIGHT);

  CHECK_EQ(game.status, GAME_WON);
  CHECK_EQ(game.length, 2);
  CHECK_EQ(game.stats.snacks, 1);
  CHECK_EQ(count_tiles(&game, TILE_EMPTY), 0);
  CHECK_EQ(count_tiles(&game, TILE_SNACK), 0);

  game_destroy(&game);
}

static void test_score_rewards_snacks_and_efficiency(void) {
  struct game_state game;
  game_initialize_state(&game);

  CHECK_EQ(game_score(&game), 0);

  game.stats.snacks = 10;
  CHECK_EQ(game_score(&game), 0);

  game.stats.moves = 15;
  CHECK_EQ(game_score(&game), 107);

  game.stats.moves = 30;
  CHECK_EQ(game_score(&game), 103);

  game.stats.poison = 2;
  CHECK_EQ(game_score(&game), 93);

  game.stats.snacks = 1;
  game.stats.moves = 1;
  game.stats.poison = 0;
  int perfect_single_snack_score = game_score(&game);
  CHECK_EQ(perfect_single_snack_score, 20);

  game.stats.snacks = 2;
  game.stats.moves = 200;
  CHECK(game_score(&game) >= perfect_single_snack_score);
}

int main(void) {
  test_initialization_invariants();
  test_direction_moves_and_wrap();
  test_tail_countdown_and_removal();
  test_self_collision();
  test_moving_into_tail_is_allowed();
  test_obstacle_collision();
  test_fatal_poison_collision();
  test_snack_growth_and_respawn();
  test_nonfatal_poison_shrink_and_respawn();
  test_manual_growth();
  test_full_board_win_detection();
  test_score_rewards_snacks_and_efficiency();

  return test_finish(NULL);
}
