/*******************************************************
 * Datei: cnake.c                                      *
 * Datum: 16.10.2014 (27.12.2014)                      *
 * Autor: Aaron Dettmann                               *
 * Zweck: Cnake v0.3.8 - Primitives "Snake"-Spiel in C *
 *******************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>

/*---------- Version ----------*/
#define VERSION "0.3.8"

/*---------- Farben ----------*/
#define RED "\x1B[31m" /* red */
#define GRN "\x1B[32m" /* green */
#define YEL "\x1B[33m" /* yellow */
#define BLU "\x1B[34m" /* blue */
#define RES "\033[0m"  /* reset */

/*---------- Escape Codes ----------*/
/*
   "\033[H"  // Move cursor to upper-left corner of the screen
   "\033[0J" // Clear from the cursor to the end of the screen
*/

#define CLEAR "\033[H\033[0J"

/*---------- Funktionsprototypen ----------*/
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
    /*---------- Deklaration der Variablen ----------*/
    int  i, j,                   /* Schleifen-Laufvariablen                                      */
         bs = 0,                 /* Board size --> Seitenlaenge des Spielfeldes                  */
         x, y, x_old, y_old,     /* Positionskoordinaten                                         */
         exit = 0,               /* In Hauptschleife ausgewertet --> Game Over!; Gewonnen!; ...  */
         length = 1,             /* Laenge der Schlange                                          */
         numofobs,               /* "Number of obstacles"                                        */
         stats[3] = { 0, 0, 0 }, /* Statistics [number of moves][eaten snacks][eaten poison]     */
         show = 0,               /* Show/Noshow                                                  */
         **board;                /* Spielbrett                                                   */

    char cmd      = 'x', /* Befehl            */
         terra    = ' ', /* ( 0) Feld / Boden */
         body     = '=', /* (>0) Koerper      */
         head     = '>', /* (-1) Kopf         */
         obstacle = '#', /* (-2) Hindernisse  */
         snack    = '*', /* (-3) Snack        */
         poison   = '!'; /* (-4) Gift         */

    x = y = x_old = y_old = 0;

    /*---------- signal handling ----------*/
    signal(SIGINT, sig_handler);

    /*---------- Festlegung der Spielfeldgroeße ----------*/
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
            printf("Lege die Spielfeldgroesse fest [4-10]: ");
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

    /*---------- Belegung des Spielfeldes ----------*/
    for( i = 0; i < bs; i++)
        for( j = 0; j < bs; j++ )
            board[i][j] = 0;

    /*---------- Seed fuer rand() ----------*/
    srand(time(NULL));

    /*---------- Startposition ----------*/
    board[y][x] = -1;        /* head */

    /*---------- Erzeugung zufaelliger Postionen ----------*/
    numofobs = (int)( ((bs*bs)/10)+1 );

    for(i=0; i<numofobs; i++)
        gen_rand_pos(&x, &y, board, bs, -2);        /* obstacle */

    gen_rand_pos(&x, &y, board, bs, -4);                /* poison */
    gen_rand_pos(&x, &y, board, bs, -3);                /* snack */

    /*---------- Spielbeginn mit Hauptschleife ----------*/
    hl();
    printf("==> Spielfeld: %dx%d\n\n", bs, bs);

    while(cmd != 'q')
    {
        /*---------- Aufbau und Ausgabe des Spielfeldes ----------*/
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

                 if(i == 0) printf(RED "%c%c>\t" RES "Schlange  (%d)", body, body, length );
            else if(i == 1) printf(YEL "%c  \t"  RES "Snack     (%d)", snack, stats[1]    );
            else if(i == 2) printf(GRN "%c  \t"  RES "Gift      (%d)", poison, stats[2]   );
            else if(i == 3) printf(BLU "%c  \t"  RES "Hindernis", obstacle   );

            printf(RES "\n");
        }

        grenze(bs);

        /*---------- Ausgabe der board[][]-Werte - NUR FUER TESTZWECKE! ----------*/
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

        /*---------- Spiel gewonnen oder verloren? ----------*/
        if(exit)
        {

            if(exit == 1)
            {
                printf("\nGEWONNEN!\n");
                break;
            }

                 if(exit == 2)        printf("\nSchlange!"  RED " %c%c> " RES, body, body );
            else if(exit == 3)        printf("\nHindernis!" BLU " %c "    RES, obstacle   );
            else if(exit == 4)        printf("\nGift!"      GRN " %c "    RES, poison     );

            printf("~ GAME OVER!\n");
            break;
        }

        /*---------- Alte Position wird abgespeichert ----------*/
        x_old = x;
        y_old = y;

        /*---------- Befehlseingabe ----------*/
        do
        {
            do
            {
                printf("\nGib einen Befehl ein [h]: ");
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
                printf("==> Hoch!\n\n");
                y -= 1;
                if(y < 0)        y = bs-1;
            }
            else if(cmd == 's')
            {
                hl();
                printf("==> Runter!\n\n");
                y += 1;
                if(y > bs-1)        y = 0;
            }
            else if(cmd == 'a')
            {
                hl();
                printf("==> Links!\n\n");
                head = '<';
                x -= 1;
                if(x < 0)        x = bs-1;

            }
            else if(cmd == 'd')
            {
                hl();
                printf("==> Rechts!\n\n");
                head = '>';
                x += 1;
                if(x > bs-1)        x = 0;
            }
            else if(cmd == 'c')        /* CHEAT CODE ==> FUER TESTZWECKE */
            {
                hl();
                printf("==> Laenge +5!\n\n");
                length += 5;
                break;
            }
            else if(cmd == 'v')
            {
                hl();
                printf("==> Show/Noshow!\n\n");
                if(!show)        show = 1;
                else                show = 0;
                break;
            }
            else if(cmd == 'q')
                break;

            else
            {
                printf("\nE: Unbekannter Befehl!\n");
                continue;
            }

            stats[0] += 1;
            break;

        } while(1);

        if(cmd == 'q')
            break;

        if(cmd == 'v' || cmd == 'c')
            continue;

        /*---------- Ereignisse ----------*/

        if(board[y][x] > 0) /* Zusammenstoss "tail" */
        {
            exit = 2;
            continue;
        }
        else if(board[y][x] == -2) /* Zusammenstoss "obstacle" */
        {
            exit = 3;
            continue;
        }
        else if(board[y][x] == -3) /* "snack" gefunden */
        {
            length++;
            stats[1] += 1; /* +1 snack */

            gen_rand_pos(&x, &y, board, bs, -3);

        }
        else if(board[y][x] == -4) /* gift gefunden */
        {
            stats[2] += 1; /* +1 gift */

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

        /*---------- Neue Position von "head" ----------*/
        board[y][x] = -1;              /* Kopfposition */
        board[y_old][x_old] = length;  /* Laenge der Schlange wird in alter Position gespeichert */

        /*---------- Pruefen auf freie Felder ----------*/
        if(find_element(board, bs, 0) == 0)     /* Spiel gewonnen */
        {
            exit = 1;
            continue;
        }

        /*---------- Schwanz der Schlange ----------*/
        for(i=0; i<bs; i++)
            for(j=0; j<bs; j++)
                if(board[i][j]>0)       /* alle werte >0 sind Koerperteile der Schlange */
                    board[i][j] -= 1;

    } /* ENDE Haupt-while-Schleife */

    /*---------- Abschluss des Spiels ----------*/
    printf("\nSchritte:\t%4d\n", stats[0]);
    printf(  "Snacks:\t\t%4d\n", stats[1]);
    printf(  "Gift:\t\t%4d\n",   stats[2]);
    printf(  "Laenge:\t\t%d/%d+\n\n", length, (bs*bs - numofobs - 1));

    return 0;

} /* ENDE main() */

/*================================*/
/*---------- Funktionen ----------*/
/*================================*/

/*---------- signal handling ----------*/
void sig_handler(int sigint)
{
    printf(RES "\n\n");
    exit(sigint);
}

/*---------- Kopfzeile ----------*/
void hl(void)
{
    printf(CLEAR "#---------- Cnake %s ----------#\n\n", VERSION);
}

/*---------- Aeussere Spielfeldbegrenzung ----------*/
void grenze(int length)
{
    int n;

    printf(GRN " |");

    for(n=0; n<=2*length; n++)
        printf("-");

    printf("|\n" RES);
}

/*---------- Hilfe ----------*/
void help(void)
{
    printf( "\nOptionen:\n"
            "w: hoch\n"
            "s: runter\n"
            "a: links\n"
            "s: rechts\n"
            "v: show/noshow\n"
            "q: beenden\n");
}

/*---------- Generierung einer Zufallsposition ----------*/
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

/*---------- Suche Element in Matrix ----------*/
int find_element(int* boardl[], int bsl, int value)
{
    int i, j;

    for(i=0; i<bsl; i++)
        for(j=0; j<bsl; j++)
            if(boardl[i][j] == value)
                return 1;                /* Element wurde gefunden */

    return 0;
}

/*==================================================*/

/*-------------------------------------------------------- TODO --------------------------------------------------------//

  ==> Algorithmus zur Erzeugung der zufaelligen Position in eine Funktion verlegen --> get_rand_pos()
  --> Bei obstacle pruefen, ob Elemente ggf. zu nah bei einander (--> dann neuer Funktionsdurchlauf)
  --> Algorithmus verbessern --> z.B. erst 1 Zufallsposition erzeugen; falls besetzt naechst rechte Position
  testen usw. (bessere Kontrolle der Anzahl Durchlaeufe)
  --> Wie Uebergabe an Funktion und Zufallsposition richtig zurueckgeben?

  ==> signal handling richtig?

  ==> W A S D ohne Bestaetigung der Enter-Taste? // oder Pfeiltasten?

  ==> ncurses???

  ==> Alternative fuer system("clear")        ==>        printf("\033[H" "\033[2J");

//----------------------------------------------------------------------------------------------------------------------*/

/*----- EOF -----*/

