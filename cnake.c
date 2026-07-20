/******************************************************
 * File:    cnake.c                                   *
 * Date:    16.10.2014 & 27.12.2014 (2026-07-01)      *
 * Author:  Aaron Dettmann                            *
 * Purpose: Cnake - Primitive snake-game written in C *
 ******************************************************/

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/*---------- Version ----------*/
#define VERSION "0.3.8"

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

/*---------- Function Prototypes ----------*/
void sig_handler(int signo);
void hl(void);
void grenze(int length);
void help(void);
int has_adjacent_obstacle(int **, int, int, int);
int is_spawn_position_valid(int **, int, int, int, int);
int find_spawn_position(int **, int, int, int *, int *);
int place_tile(int **, int, int);
int find_element(int **, int, int);
void ausgabe(int **, int, char *);

/*============================*/
/*---------- MAIN() ----------*/
/*============================*/

int main(int argc, char *argv[]) {
  /*---------- Variable Declarations ----------*/
  int i, j,   /* Loop variables                                              */
      bs = 0, /* Board size --> side length of the playing field             */
      x, y, x_old, y_old, /* Position coordinates */
      exit = 0,   /* Evaluated in the main loop --> Game Over!; Win!; ...   */
      length = 1, /* Length of the snake */
      numofobs = 0, /* Number of obstacles   */
      target_numofobs, /* Desired number of obstacles */
      stats[3] = {0, 0, 0}, /* Statistics [moves][snacks eaten][poison eaten] */
      show = 0,             /* Show/Hide debug output             */
      spawn_snack = 0,      /* Respawn snack after tail update */
      spawn_poison = 0,     /* Respawn poison after tail update */
      **board; /* Game board                                                  */

  char cmd = 'x',     /* Command        */
      terra = ' ',    /* ( 0) Empty tile */
      body = '=',     /* (>0) Body       */
      head = '>',     /* (-1) Head       */
      obstacle = '#', /* (-2) Obstacle   */
      snack = '*',    /* (-3) Snack      */
      poison = '!';   /* (-4) Poison     */

  x = y = x_old = y_old = 0;

  /*---------- Signal Handling ----------*/
  signal(SIGINT, sig_handler);

  /*---------- Determine Board Size ----------*/
  hl();

  if (argc > 1) {
    bs = atoi(argv[1]);
    if (bs < 4 || bs > 10)
      bs = 0;
  }

  if (!bs) {
    do {
      printf("Choose the board size [4-10]: ");
      if (!scanf(" %d", &bs))
        return 1;
    } while (bs < 4 || bs > 10);
  }

  if ((board = (int **)malloc(bs * sizeof(int *))) == NULL)
    return 2;

  for (i = 0; i < bs; i++)
    if ((board[i] = (int *)malloc(bs * sizeof(int))) == NULL)
      return 2;

  /*---------- Initialize the Board ----------*/
  for (i = 0; i < bs; i++)
    for (j = 0; j < bs; j++)
      board[i][j] = 0;

  /*---------- Seed rand() ----------*/
  srand(time(NULL));

  /*---------- Starting Position ----------*/
  board[y][x] = -1; /* head */

  /*---------- Generate Random Positions ----------*/
  target_numofobs = (int)(((bs * bs) / 10) + 1);

  for (i = 0; i < target_numofobs; i++)
    if (place_tile(board, bs, -2))
      numofobs++;
    else
      break;

  if (!place_tile(board, bs, -4) || !place_tile(board, bs, -3)) {
    fprintf(stderr, "Error: Unable to place all tiles on the board.\n");
    return 3;
  }

  /*---------- Start Main Game Loop ----------*/
  hl();
  printf("==> Board: %dx%d\n\n", bs, bs);

  while (cmd != 'q') {
    /*---------- Draw the Game Board ----------*/
    grenze(bs);

    for (i = 0; i < bs; i++) {
      printf(GRN " | ");

      for (j = 0; j < bs; j++) {
        if (board[i][j] > 0)
          printf(RED "%c ", body);
        else if (board[i][j] == 0)
          printf(RES "%c ", terra);
        else if (board[i][j] == -1)
          printf(RED "%c ", head);
        else if (board[i][j] == -2)
          printf(BLU "%c ", obstacle);
        else if (board[i][j] == -3)
          printf(YEL "%c ", snack);
        else if (board[i][j] == -4)
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

    grenze(bs);

    /*---------- Display board[][] values - FOR DEBUGGING ONLY! ----------*/
    if (show) {
      printf("\n");

      for (i = 0; i < bs; i++) {
        printf("|");

        for (j = 0; j < bs; j++)
          printf("%2d ", board[i][j]);

        printf("|\n");
      }
    }

    /*---------- Check Win/Loss Condition ----------*/
    if (exit) {

      if (exit == 1) {
        printf("\nYOU WIN!\n");
        break;
      }

      if (exit == 2)
        printf("\nSnake!" RED " %c%c> " RES, body, body);
      else if (exit == 3)
        printf("\nObstacle!" BLU " %c " RES, obstacle);
      else if (exit == 4)
        printf("\nPoison!" GRN " %c " RES, poison);

      printf("~ GAME OVER!\n");
      break;
    }

    /*---------- Save Previous Position ----------*/
    x_old = x;
    y_old = y;

    /*---------- Read Player Command ----------*/
    do {
      do {
        printf("\nEnter a command [h]: ");
        scanf(" %c", &cmd);
      } while (getchar() != '\n');

      if (cmd == 'h') {
        help();
        continue;
      } else if (cmd == 'w') {
        hl();
        printf("==> Up!\n\n");
        y -= 1;
        if (y < 0)
          y = bs - 1;
      } else if (cmd == 's') {
        hl();
        printf("==> Down!\n\n");
        y += 1;
        if (y > bs - 1)
          y = 0;
      } else if (cmd == 'a') {
        hl();
        printf("==> Left!\n\n");
        head = '<';
        x -= 1;
        if (x < 0)
          x = bs - 1;

      } else if (cmd == 'd') {
        hl();
        printf("==> Right!\n\n");
        head = '>';
        x += 1;
        if (x > bs - 1)
          x = 0;
      } else if (cmd == 'c') /* CHEAT CODE --> FOR TESTING */
      {
        hl();
        printf("==> Length +5!\n\n");
        length += 5;
        break;
      } else if (cmd == 'v') {
        hl();
        printf("==> Toggle Debug View!\n\n");
        if (!show)
          show = 1;
        else
          show = 0;
        break;
      } else if (cmd == 'q')
        break;

      else {
        printf("\nError: Unknown command!\n");
        continue;
      }

      stats[0] += 1;
      break;

    } while (1);

    if (cmd == 'q')
      break;

    if (cmd == 'v' || cmd == 'c')
      continue;

    /*---------- Events ----------*/
    spawn_snack = 0;
    spawn_poison = 0;

    if (board[y][x] > 0) /* Collision with snake body */
    {
      exit = 2;
      continue;
    } else if (board[y][x] == -2) /* Collision with obstacle */
    {
      exit = 3;
      continue;
    } else if (board[y][x] == -3) /* Snack found */
    {
      length++;
      stats[1] += 1; /* +1 snack */

      spawn_snack = 1;

    } else if (board[y][x] == -4) /* Poison found */
    {
      stats[2] += 1; /* +1 poison */

      if (length > 1) {
        length -= 1;
        spawn_poison = 1;
      } else {
        exit = 4;
        continue;
      }
    }

    /*---------- Update Head Position ----------*/
    board[y][x] = -1; /* Head position */
    board[y_old][x_old] =
        length; /* Store the snake length at the previous head position */

    /*---------- Update Snake Tail ----------*/
    for (i = 0; i < bs; i++)
      for (j = 0; j < bs; j++)
        if (board[i][j] > 0) /* All values >0 are snake body segments */
          board[i][j] -= 1;

    if (spawn_snack && !place_tile(board, bs, -3) &&
        find_element(board, bs, 0) != 0) {
      fprintf(stderr, "Error: Unable to place a snack.\n");
      return 3;
    }

    if (spawn_poison && !place_tile(board, bs, -4) &&
        find_element(board, bs, 0) != 0) {
      fprintf(stderr, "Error: Unable to place poison.\n");
      return 3;
    }

    /*---------- Check for Free Tiles ----------*/
    if (find_element(board, bs, 0) == 0) /* Game won */
    {
      exit = 1;
      continue;
    }

  } /* End of main game loop */

  /*---------- Game Summary ----------*/
  printf("\nMoves:\t\t%4d\n", stats[0]);
  printf("Snacks:\t\t%4d\n", stats[1]);
  printf("Poison:\t%4d\n", stats[2]);
  printf("Length:\t\t%d/%d+\n\n", length, (bs * bs - numofobs - 1));

  return 0;

} /* End of main() */

/*================================*/
/*---------- Functions -----------*/
/*================================*/

/*---------- Signal Handling ----------*/
void sig_handler(int sigint) {
  printf(RES "\n\n");
  exit(sigint);
}

/*---------- Header ----------*/
void hl(void) { printf(CLEAR "#---------- Cnake %s ----------#\n\n", VERSION); }

/*---------- Draw Outer Board Border ----------*/
void grenze(int length) {
  int n;

  printf(GRN " |");

  for (n = 0; n <= 2 * length; n++)
    printf("-");

  printf("|\n" RES);
}

/*---------- Help ----------*/
void help(void) {
  printf("\nCommands:\n"
         "w: move up\n"
         "s: move down\n"
         "a: move left\n"
         "d: move right\n"
         "v: toggle debug view\n"
         "q: quit\n");
}

/*---------- Check Whether an Obstacle Would Spawn Too Close ----------*/
int has_adjacent_obstacle(int *boardl[], int bsl, int x_pos, int y_pos) {
  int x_check, y_check;

  for (y_check = y_pos - 1; y_check <= y_pos + 1; y_check++)
    for (x_check = x_pos - 1; x_check <= x_pos + 1; x_check++) {
      if (y_check < 0 || y_check >= bsl || x_check < 0 || x_check >= bsl)
        continue;

      if (y_check == y_pos && x_check == x_pos)
        continue;

      if (boardl[y_check][x_check] == -2)
        return 1;
    }

  return 0;
}

/*---------- Check Whether a Spawn Position Is Valid ----------*/
int is_spawn_position_valid(int *boardl[], int bsl, int x_pos, int y_pos,
                            int type) {
  if (boardl[y_pos][x_pos] != 0)
    return 0;

  if (type == -2 && has_adjacent_obstacle(boardl, bsl, x_pos, y_pos))
    return 0;

  return 1;
}

/*---------- Find a Valid Position for a Tile ----------*/
int find_spawn_position(int *boardl[], int bsl, int type, int *x_out,
                        int *y_out) {
  int i, index, total, x_pos, y_pos, start;

  total = bsl * bsl;
  start = rand() % total;

  for (i = 0; i < total; i++) {
    index = (start + i) % total;
    y_pos = index / bsl;
    x_pos = index % bsl;

    if (is_spawn_position_valid(boardl, bsl, x_pos, y_pos, type)) {
      *x_out = x_pos;
      *y_out = y_pos;
      return 1;
    }
  }

  return 0;
}

/*---------- Place a Tile on the Board ----------*/
int place_tile(int *boardl[], int bsl, int type) {
  int x_pos, y_pos;

  if (!find_spawn_position(boardl, bsl, type, &x_pos, &y_pos))
    return 0;

  boardl[y_pos][x_pos] = type;
  return 1;
}

/*---------- Search for a Value in the Matrix ----------*/
int find_element(int *boardl[], int bsl, int value) {
  int i, j;

  for (i = 0; i < bsl; i++)
    for (j = 0; j < bsl; j++)
      if (boardl[i][j] == value)
        return 1; /* Value found */

  return 0;
}

/*-------------------------------------------------------- TODO --------------------------------------------------------//

  ==> Proper signal handling?

  ==> Support W A S D movement without requiring the Enter key?
      // Or use the arrow keys instead?

  ==> ncurses???

//----------------------------------------------------------------------------------------------------------------------*/

/*----- EOF -----*/
