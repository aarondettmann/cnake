/******************************************************
 * File:    cnake.c                                   *
 * Date:    16.10.2014 & 27.12.2014 (2026-07-01)      *
 * Author:  Aaron Dettmann                            *
 * Purpose: Cnake - Primitive snake-game written in C *
 ******************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
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
void gen_rand_pos(int*, int*, int**, int, int);
int  find_element(int**, int, int);
void ausgabe(int**, int, char*);

/*============================*/
/*---------- MAIN() ----------*/
/*============================*/

int main(int argc, char *argv[])
{
    /*---------- Variable Declarations ----------*/
    int  i, j,                   /* Loop variables                                              */
         bs = 0,                 /* Board size --> side length of the playing field             */
         x, y, x_old, y_old,     /* Position coordinates                                        */
         exit = 0,               /* Evaluated in the main loop --> Game Over!; Win!; ...        */
         length = 1,             /* Length of the snake                                         */
         numofobs,               /* Number of obstacles                                         */
         stats[3] = { 0, 0, 0 }, /* Statistics [moves][snacks eaten][poison eaten]              */
         show = 0,               /* Show/Hide debug output                                      */
         **board;                /* Game board                                                  */

    char cmd      = 'x', /* Command        */
         terra    = ' ', /* ( 0) Empty tile */
         body     = '=', /* (>0) Body       */
         head     = '>', /* (-1) Head       */
         obstacle = '#', /* (-2) Obstacle   */
         snack    = '*', /* (-3) Snack      */
         poison   = '!'; /* (-4) Poison     */

    x = y = x_old = y_old = 0;

    /*---------- Signal Handling ----------*/
    signal(SIGINT, sig_handler);

    /*---------- Determine Board Size ----------*/
    hl();

    if(argc>1)
    {
        bs = atoi(argv[1]);
        if(bs<4 || bs>10)
            bs = 0;
    }

    if(!bs)
    {
        do
        {
            printf("Choose the board size [4-10]: ");
            if( !scanf(" %d", &bs))
                return 1;
        }
        while(bs<4 || bs>10);
    }

    if( ( board = (int**)malloc(bs*sizeof(int*)) ) == NULL )
        return 2;

    for(i=0; i<bs; i++)
        if( ( board[i] = (int*)malloc(bs*sizeof(int)) ) == NULL )
            return 2;

    /*---------- Initialize the Board ----------*/
    for( i = 0; i < bs; i++)
        for( j = 0; j < bs; j++ )
            board[i][j] = 0;

    /*---------- Seed rand() ----------*/
    srand(time(NULL));

    /*---------- Starting Position ----------*/
    board[y][x] = -1;        /* head */

    /*---------- Generate Random Positions ----------*/
    numofobs = (int)( ((bs*bs)/10)+1 );

    for(i=0; i<numofobs; i++)
        gen_rand_pos(&x, &y, board, bs, -2);        /* obstacle */

    gen_rand_pos(&x, &y, board, bs, -4);                /* poison */
    gen_rand_pos(&x, &y, board, bs, -3);                /* snack */

    /*---------- Start Main Game Loop ----------*/
    hl();
    printf("==> Board: %dx%d\n\n", bs, bs);

    while(cmd != 'q')
    {
        /*---------- Draw the Game Board ----------*/
        grenze(bs);

        for(i=0; i<bs; i++)
        {
            printf(GRN " | ");

            for(j=0; j<bs; j++)
            {
                     if(board[i][j]  >  0) printf(RED "%c ", body     );
                else if(board[i][j] ==  0) printf(RES "%c ", terra    );
                else if(board[i][j] == -1) printf(RED "%c ", head     );
                else if(board[i][j] == -2) printf(BLU "%c ", obstacle );
                else if(board[i][j] == -3) printf(YEL "%c ", snack    );
                else if(board[i][j] == -4) printf(GRN "%c ", poison   );
            }

            printf(GRN "|\t");

                 if(i == 0) printf(RED "%c%c>\t" RES "Snake     (%d)", body, body, length );
            else if(i == 1) printf(YEL "%c  \t"  RES "Snack     (%d)", snack, stats[1]    );
            else if(i == 2) printf(GRN "%c  \t"  RES "Poison    (%d)", poison, stats[2]   );
            else if(i == 3) printf(BLU "%c  \t"  RES "Obstacle", obstacle   );

            printf(RES "\n");
        }

        grenze(bs);

        /*---------- Display board[][] values - FOR DEBUGGING ONLY! ----------*/
        if(show)
        {
            printf("\n");

            for( i=0; i<bs; i++)
            {
                printf("|");

                for( j=0; j<bs; j++ )
                    printf("%2d ", board[i][j]);

                printf("|\n");
            }
        }

        /*---------- Check Win/Loss Condition ----------*/
        if(exit)
        {

            if(exit == 1)
            {
                printf("\nYOU WIN!\n");
                break;
            }

                 if(exit == 2)        printf("\nSnake!"     RED " %c%c> " RES, body, body );
            else if(exit == 3)        printf("\nObstacle!"  BLU " %c "    RES, obstacle   );
            else if(exit == 4)        printf("\nPoison!"    GRN " %c "    RES, poison     );

            printf("~ GAME OVER!\n");
            break;
        }

        /*---------- Save Previous Position ----------*/
        x_old = x;
        y_old = y;

        /*---------- Read Player Command ----------*/
        do
        {
            do
            {
                printf("\nEnter a command [h]: ");
                scanf(" %c", &cmd);
            } while(getchar() != '\n');

            if(cmd == 'h')
            {
                help();
                continue;
            }
            else if(cmd == 'w')
            {
                hl();
                printf("==> Up!\n\n");
                y -= 1;
                if(y < 0)        y = bs-1;
            }
            else if(cmd == 's')
            {
                hl();
                printf("==> Down!\n\n");
                y += 1;
                if(y > bs-1)        y = 0;
            }
            else if(cmd == 'a')
            {
                hl();
                printf("==> Left!\n\n");
                head = '<';
                x -= 1;
                if(x < 0)        x = bs-1;

            }
            else if(cmd == 'd')
            {
                hl();
                printf("==> Right!\n\n");
                head = '>';
                x += 1;
                if(x > bs-1)        x = 0;
            }
            else if(cmd == 'c')        /* CHEAT CODE --> FOR TESTING */
            {
                hl();
                printf("==> Length +5!\n\n");
                length += 5;
                break;
            }
            else if(cmd == 'v')
            {
                hl();
                printf("==> Toggle Debug View!\n\n");
                if(!show)        show = 1;
                else             show = 0;
                break;
            }
            else if(cmd == 'q')
                break;

            else
            {
                printf("\nError: Unknown command!\n");
                continue;
            }

            stats[0] += 1;
            break;

        } while(1);

        if(cmd == 'q')
            break;

        if(cmd == 'v' || cmd == 'c')
            continue;

        /*---------- Events ----------*/

        if(board[y][x] > 0) /* Collision with snake body */
        {
            exit = 2;
            continue;
        }
        else if(board[y][x] == -2) /* Collision with obstacle */
        {
            exit = 3;
            continue;
        }
        else if(board[y][x] == -3) /* Snack found */
        {
            length++;
            stats[1] += 1; /* +1 snack */

            gen_rand_pos(&x, &y, board, bs, -3);

        }
        else if(board[y][x] == -4) /* Poison found */
        {
            stats[2] += 1; /* +1 poison */

            if(length > 1)
            {
                length -= 1;
                gen_rand_pos(&x, &y, board, bs, -4);
            }
            else
            {
                exit = 4;
                continue;
            }
        }

        /*---------- Update Head Position ----------*/
        board[y][x] = -1;              /* Head position */
        board[y_old][x_old] = length;  /* Store the snake length at the previous head position */

        /*---------- Check for Free Tiles ----------*/
        if(find_element(board, bs, 0) == 0)     /* Game won */
        {
            exit = 1;
            continue;
        }

        /*---------- Update Snake Tail ----------*/
        for(i=0; i<bs; i++)
            for(j=0; j<bs; j++)
                if(board[i][j] > 0)    /* All values >0 are snake body segments */
                    board[i][j] -= 1;

    } /* End of main game loop */

    /*---------- Game Summary ----------*/
    printf("\nMoves:\t\t%4d\n", stats[0]);
    printf(  "Snacks:\t\t%4d\n", stats[1]);
    printf(  "Poison:\t%4d\n",   stats[2]);
    printf(  "Length:\t\t%d/%d+\n\n", length, (bs*bs - numofobs - 1));

    return 0;

} /* End of main() */

/*================================*/
/*---------- Functions -----------*/
/*================================*/

/*---------- Signal Handling ----------*/
void sig_handler(int sigint)
{
    printf(RES "\n\n");
    exit(sigint);
}

/*---------- Header ----------*/
void hl(void)
{
    printf(CLEAR "#---------- Cnake %s ----------#\n\n", VERSION);
}

/*---------- Draw Outer Board Border ----------*/
void grenze(int length)
{
    int n;

    printf(GRN " |");

    for(n=0; n<=2*length; n++)
        printf("-");

    printf("|\n" RES);
}

/*---------- Help ----------*/
void help(void)
{
    printf( "\nCommands:\n"
            "w: move up\n"
            "s: move down\n"
            "a: move left\n"
            "d: move right\n"
            "v: toggle debug view\n"
            "q: quit\n");
}

/*---------- Generate a Random Position ----------*/
void gen_rand_pos(int *xl, int *yl, int* boardl[], int bsl, int type)
{
    int x_rand, y_rand;

    do
    {
        y_rand = rand() % bsl;
        x_rand = rand() % bsl;
    }
    while( ( y_rand == *yl && x_rand == *xl) || boardl[y_rand][x_rand] );

    boardl[y_rand][x_rand] = type;
}

/*---------- Search for a Value in the Matrix ----------*/
int find_element(int* boardl[], int bsl, int value)
{
    int i, j;

    for(i=0; i<bsl; i++)
        for(j=0; j<bsl; j++)
            if(boardl[i][j] == value)
                return 1;                /* Value found */

    return 0;
}

/*==================================================*/

/*-------------------------------------------------------- TODO --------------------------------------------------------//

  ==> Move the random position generation algorithm into a dedicated function --> get_rand_pos()

  --> For obstacles, check whether they are spawned too close to one another
      (if so, generate a new position)

  --> Improve the placement algorithm, e.g. generate one random position first;
      if it is occupied, check the next position to the right, and so on
      (better control over the number of iterations)

  --> What is the proper way to pass the generated random position
      back from the function?

  ==> Proper signal handling?

  ==> Support W A S D movement without requiring the Enter key?
      // Or use the arrow keys instead?

  ==> ncurses???

  ==> Alternative to system("clear")
      ==> printf("\033[H" "\033[2J");

//----------------------------------------------------------------------------------------------------------------------*/

/*----- EOF -----*/
