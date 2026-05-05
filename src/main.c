/*
 * Copyright © 2026 Ian D. Romanick
 * SPDX-License-Identifier: GPL-3.0-only
 */
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_TERMIOS_H
#include <termios.h>
#else
#include <termio.h>
#endif

#ifdef linux
#include <time.h>
#endif

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/times.h>

#include "my_bool.h"
#include "two_player.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#define MAX2(a, b) ((a) > (b) ? (a) : (b))

/* Tetris well is 10 blocks wide by 20 blocks tall. This will be drawn as 20
 * characters by 20 characters. The well is drawn 2 lines down from the top of
 * the screen. The new piece is initially drawn at a position affectively
 * above the top of the well.
 *
 * The well is stored as an array of 20 integers for the active play
 * field, 3 extra integers for the area above the well where new
 * pieces spawn, and 4 extra integers at the bottom. The extra at
 * bottom is so large so various access to the well (e.g., collision
 * detection) don't have to perform bounds checks on the array. This
 * is the guardband.
 */
#define WELL_SPAWN 3
#define WELL_GUARD_BAND 4
#define WELL_SIZE (20 + WELL_SPAWN + WELL_GUARD_BAND)

struct tetromino_frame {
    uint16_t shift;
    uint16_t mask[4];
    const char *draw;
    const char *erase;
};

struct tetromino {
    /* One of O, I, S, Z, L, J, or T. */
    char name;

    /* Number of animation frames. 1, 2, or 4. */
    uint8_t frames;

    struct tetromino_frame f[4];
};

#include "tetrominos.h"

struct garbage_state {
    uint16_t garbage;
    int16_t remain;
    uint16_t seed;
};

/* After this number of rows of garbage have been added, a new garbage
 * column is selected.
 */
#define GARBAGE_CHANGE_COUNT 9

struct game_mode {
    uint16_t initial_level;
    uint16_t num_players;
    uint16_t rng_mode;
};

static void
move_to(uint16_t x, uint16_t y)
{
    if (y == 0) {
	printf("\033[;%df", x);
    } else {
	printf("\033[%d;%df", y, x);
    }
}

static const char q80[] =
    "qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq";

static const char spc80[] =
    "                                                                                ";

static void
draw_box(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    uint16_t i;

    printf("\033(0\033[7m");

    move_to(x, y);
    printf("lw");
    fwrite(q80, 1, w - 4, stdout);
    printf("wk");

    for (i = 2; i < h; i++) {
        move_to(x, y + i - 1);
        printf("xx\033[0m");
        fwrite(spc80, 1, w - 4, stdout);
        printf("\033[7mxx");
    }

    move_to(x, y + h - 1);
    printf("wv");
    fwrite(q80, 1, w - 4, stdout);
    printf("vj\033[0m");
}

static void
draw_well_from_scratch(const uint16_t *well, const uint16_t *piece_counts,
		       uint16_t lines)
{
    uint16_t i;
    uint16_t j;
    char border[2];

    fputs("\033[3;20f\033[7m\033(0", stdout);

    border[0] = 'l';
    border[1] = 'k';
    for (i = WELL_SPAWN; i < 20 + WELL_SPAWN; i++) {
	printf("\033[7m%c%c\033[m", border[0], border[1]);

	for (j = 15; j > 5; j--) {
	    const uint16_t bit = 1u << j;

	    if ((well[i] & bit) != 0) {
		printf("aa");
	    } else {
		printf("  ");
	    }
	}

	printf("\033[7m%c%c\n\033[19C", border[0], border[1]);
	border[0] = 'x';
	border[1] = 'x';
    }

    fputs("mvqqqqqqqqqqqqqqqqqqqqvj\033(B", stdout);

    fprintf(stdout, "\033[3f\033[7m\033(0");
    fprintf(stdout, "lwqqqqqqqqqqqqwk\n");
    fprintf(stdout, "xx\033[m\033[12C\033[7mxx\n");
    fprintf(stdout, "xx\033[m \033(BTop Score  \033(0\033[7mxx\n");
    fprintf(stdout, "xx\033[m\033[12C\033[7mxx\n");
    fprintf(stdout, "xx\033[m\033[12C\033[7mxx\n");
    fprintf(stdout, "xx\033[m \033(BScore      \033(0\033[7mxx\n");
    fprintf(stdout, "xx\033[m\033[12C\033[7mxx\n");
    fprintf(stdout, "xx\033[m\033[12C\033[7mxx\n");
    fprintf(stdout, "xx\033[m \033(BLines      \033(0\033[7mxx\n");
    fprintf(stdout, "xx\033[m\033[12C\033[7mxx\n");
    fprintf(stdout, "xx\033[m\033[12C\033[7mxx\n");
    fprintf(stdout, "xx\033[m \033(BLevel      \033(0\033[7mxx\n");
    fprintf(stdout, "xx\033[m\033[12C\033[7mxx\n");
    fprintf(stdout, "xx\033[m\033[12C\033[7mxx\n");
    fprintf(stdout, "mvqqqqqqqqqqqqvj\033(B");

    fprintf(stdout, "\033[3;46f\033[7m\033(0");
    fprintf(stdout, "lwqq\033[m NEXT \033[7mqqwk\033[B\033[14D");
    fprintf(stdout, "xx\033[m          \033[7mxx\033[B\033[14D");
    fprintf(stdout, "xx\033[m          \033[7mxx\033[B\033[14D");
    fprintf(stdout, "xx\033[m          \033[7mxx\033[B\033[14D");
    fprintf(stdout, "xx\033[m          \033[7mxx\033[B\033[14D");
    fprintf(stdout, "xx\033[m          \033[7mxx\033[B\033[14D");
    fprintf(stdout, "xx\033[m          \033[7mxx\033[B\033[14D");
    fprintf(stdout, "mvqqqqqqqqqqvj\033(B");

    fprintf(stdout, "\033[3;61f\033[0m\033)0");
    for (i = 0; i < 7; i++) {
	printf("%c: %d\033[%d;61f", all_pieces[i].name,
	       piece_counts[i], 4 + i);
    }
}

static void
draw_controls(void)
{
    fprintf(stdout,
	    "\033[13;46f"
	    "\033[14;46f rotate              rotate"
	    "\033[15;46f counter            clockwise"
	    "\033[16;46fclockwise          /"
	    "\033[17;46f          \\       /"
	    "\033[18;46f           q     e"
	    "\033[19;46f           a  s  d"
	    "\033[20;46f          /   |   \\"
	    "\033[21;46f      move    |    move"
	    "\033[22;46f     left     |     right"
	    "\033[23;46f            hard"
	    "\033[24;46f            drop"
	    "\033[3;61f\033[0m\033)0");
}

static void
erase_piece(const struct tetromino *t,
	   uint16_t x, uint16_t y, uint16_t rotation)
{
    printf("\033[m\033(0");
    move_to(22 + 2 * x, y);
    fwrite(t->f[rotation].erase, 1,
	   strlen(t->f[rotation].erase), stdout);
    printf("\033[m\033(B");
}

static void
draw_piece(const struct tetromino *t,
	   uint16_t x, uint16_t y, uint16_t rotation)
{
    printf("\033[7m\033(0");
    move_to(22 + 2 * x, y);
    fwrite(t->f[rotation].draw, 1,
	   strlen(t->f[rotation].draw), stdout);
    printf("\033[m\033(B");
}

static void
draw_complete_lines(const uint16_t *complete, uint16_t count)
{
    uint16_t i;

    for (i = 0; i < count; i++) {
	printf("\033[%d;22f\033(0\033[5maaaaaaaaaaaaaaaaaaaa", complete[i]);
    }
}

static int
format_number_u16(uint16_t n, char *s)
{
    /* 5 digits and one separator. The NUL is not stored here. */
    char buf[5 + 1];
    uint16_t i = 0;
    uint16_t k = 0;

    /* Generate the characters of the string in reverse order. */
    do {
	if (k == 3) {
	    buf[i++] = ',';
	    k = 0;
	}

	buf[i++] = '0' + (n % 10);
	n /= 10;
	k++;
    } while(n > 0);

    /* Reverse the string from the local buffer to the caller's
     * buffer.
     */
    k = 0;
    do {
	s[k] = buf[i - 1 - k];
	k++;
    } while(k < i);

    s[k++] = '\0';
    return k;
}

static int
format_number_u32(uint32_t n, char *s)
{
    /* 10 digits and 3 separators. The NUL is not stored here. */
    char buf[10 + 3];
    uint16_t i = 0;
    uint16_t k = 0;

    /* Generate the characters of the string in reverse order. */
    do {
	if (k == 3) {
	    buf[i++] = ',';
	    k = 0;
	}

	buf[i++] = '0' + (n % 10);
	n /= 10;
	k++;
    } while(n > 0);

    /* Reverse the string from the local buffer to the caller's
     * buffer.
     */
    k = 0;
    do {
	s[k] = buf[i - (k + 1)];
	k++;
    } while(k < i);

    s[k++] = '\0';
    return k;
}

static void
draw_score(uint32_t score, uint32_t top_score, uint16_t lines, uint16_t level)
{
    /* 10 digits, 3 separators, and a NUL. */
    char buf[10 + 3 + 1];
    int len;

    len = format_number_u32(top_score, buf);
    fprintf(stdout, "\033[m\033[6;%df", (3 + 2 + 10) - len);
    fwrite(buf, 1, len - 1, stdout);

    len = format_number_u32(score, buf);
    fprintf(stdout, "\033[m\033[9;%df", (3 + 2 + 10) - len);
    fwrite(buf, 1, len - 1, stdout);

    len = format_number_u16(lines, buf);
    fprintf(stdout, "\033[12;%df", (3 + 2 + 10) - len);
    fwrite(buf, 1, len - 1, stdout);

    len = format_number_u16(level, buf);
    fprintf(stdout, "\033[15;%df", (3 + 2 + 10) - len);
    fwrite(buf, 1, len - 1, stdout);
}

static void
draw_final_scores(uint32_t my_score, uint32_t their_score, bool all_done)
{
    /* 10 digits, 3 separators, and a NUL. */
    char buf[10 + 3 + 1];
    int len;

    if (!all_done) {
        move_to(22, 6);
        printf("Waiting for other   ");
        move_to(22, 7);
        printf("   player to finish.");
    }

    move_to(22, 9);
    printf(" Their score:       ");
    move_to(22, 10);

    len = format_number_u32(their_score, buf);
    fwrite(spc80, 1, 20 - len, stdout);

    printf(buf);
    putchar(' ');

    move_to(22, 12);
    printf(" Your score:        ");
    move_to(22, 13);

    len = format_number_u32(my_score, buf);
    fwrite(spc80, 1, 20 - len, stdout);

    printf(buf);
    putchar(' ');

    if (all_done) {
        move_to(22, 15);

        if (their_score > my_score) {
            printf("==== You  lost. ====");
        } else if (their_score < my_score) {
            printf("===== You won! =====");
        } else {
            printf("==== Tie game!! ====");
        }

        move_to(22, 17);
        printf("Press a key to exit.");
    }
}


/* Simple LFSR random number generated taken directly from
 * https://en.wikipedia.org/wiki/Linear-feedback_shift_register
 */
static uint16_t
lfsr_galois(uint16_t *seed)
{
    uint16_t lfsr = *seed;
    uint16_t msb = lfsr & 0x8000;

    lfsr <<= 1;
    if (msb != 0)
        lfsr ^= 0x002Du;

    *seed = lfsr;

    return lfsr;
}

static void
game_init_well_state(uint16_t *well)
{
    uint16_t i;

    for (i = 0; i < 23; i++)
	well[i] = 0x003f;

    well[23] = 0xffff;
    well[24] = 0xffff;
    well[25] = 0xffff;
    well[26] = 0xffff;
}

static void
game_set_piece(uint16_t *well, const uint16_t *piece, int x, int y)
{
    uint16_t *w = &well[y];

    w[0] |= (piece[0] >> x);
    w[1] |= (piece[1] >> x);
    w[2] |= (piece[2] >> x);
    w[3] |= (piece[3] >> x);
}

static bool
game_can_do(const uint16_t *well, const uint16_t *piece, int x, int y)
{
    const uint16_t *w = &well[y];

    return ((w[0] & (piece[0] >> x)) == 0 &&
	    (w[1] & (piece[1] >> x)) == 0 &&
	    (w[2] & (piece[2] >> x)) == 0 &&
	    (w[3] & (piece[3] >> x)) == 0);
}

static uint16_t
game_check_complete_lines(const uint16_t *well, uint16_t *which)
{
    uint16_t i;
    uint16_t j = 0;

    /* The first 3 rows of the well are not part of the game board. That is
     * the region where pieces spawn.
     */
    for (i = WELL_SPAWN; i < 20 + WELL_SPAWN; i++) {
	if (well[i] == 0xffff)
	    which[j++] = i;
    }

    return j;
}

static void
game_remove_lines(uint16_t * well, const uint16_t *which, uint16_t count)
{
    uint16_t i;
    uint16_t j;

    for (i = 0; i < count; i++) {
	assert(i == 0 || which[i] > which[i - i]);

	for (j = which[i]; j >= WELL_SPAWN; j--)
	    well[j] = well[j - 1];
    }
}

static void
game_insert_garbage(uint16_t *well, uint16_t count, struct garbage_state *state)
{
    uint16_t i;

    /* Move the stack up by count lines. */
    for (i = 0; i < (WELL_SIZE - WELL_GUARD_BAND) - count; i++)
       well[i] = well[i + count];

    for (/* empty */; i < (WELL_SIZE - WELL_GUARD_BAND); i++) {
       if (--state->remain < 0) {
           /* Select a new garbage column. */
           state->garbage =
               ~((uint16_t)0x8000u >> (lfsr_galois(&state->seed) % 10u));

           /* Reset the counter. */
           state->remain = GARBAGE_CHANGE_COUNT - 1;
       }

       well[i] = state->garbage;
    }
}

static void
tick_sleep(uint16_t t)
{
#if defined linux
    uint16_t x = t % 60;
    struct timespec duration = { t / 60, 0 };

    if (x != 0)
	duration.tv_nsec = 1000000000 / (60 / x);

    nanosleep(&duration, NULL);
#else
    struct tms tm;

    long x = times(&tm) + t;

    while (times(&tm) < x)
        ;
#endif
}

/* This is mostly the "guideline scoring system." T-spins are not detected, so
 * extra points are not awarded for those. Access as "(number of lines * 2) +
 * previous was Tetris."
 */
static const uint16_t points_for_lines[] = {
    0, 0, 100, 100, 300, 300, 500, 500, 800, 1200
};

static const uint16_t delay_for_level[] = {
/*   0   1   2   3   4   5   6   7   8   9 */
    48, 43, 38, 33, 28, 23, 18, 13,  8,  6,
     5,  5,  5,  4,  4,  4,  3,  3,  3,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  1,
};

/* Back-to-back bonus is +1 lines of garbage. This bonus should also be awarded
 * for T-spin clears. Perfect clears, which are not currently tracked, should be
 * 10 lines of garbage (total) regardless of the clear count or bonus situation.
 */
static const uint16_t garbage_for_lines[] = {
    0, 0, 0, 0, 1, 1, 2, 2, 4, 5
};

static void
init_file_io()
{
    int fd;

    fd = open("/dev/tty", O_RDWR | O_NDELAY);
    if (fd == -1) {
        perror("opening TTY");
        exit(1);
    }

#ifdef HAVE_TERMIOS_H
    struct termios raw;

    tcgetattr(fd, &raw);
    raw.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(fd, TCSAFLUSH, &raw);
#else
    struct termio raw;

    ioctl(fd, TCGETA, &raw);
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_iflag &= ~(ICRNL | INLCR);

    /* librcurses napms uses VTIME to implement the wait. */
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    ioctl(fd, TCSETA, &raw);
#endif

#ifdef HAVE_DUP2
    dup2(fd, 0);
    dup2(fd, 1);
#else
    close(0);
    dup(fd);

    close(1);
    dup(fd);
#endif
}

static int
do_title_screen()
{
    /* Cursor off, clear screen. */
    fputs("\033[?25l\033[2J", stdout);
    move_to(40 - (23 / 2), 10);
    printf("Terminal Tetris for Two");
    move_to(40 - (42 / 2), 11);
    printf("Press Q to quit, or any other key to play.");
    fflush(stdout);

    while (true) {
        char c = 0;
	if (read(0, &c, 1) > 0) {
            if (c == 'q' || c == 'Q')
                return 1;

            return 0;
        }

        sleep(1);
    }

    return 0;
}

struct widget {
    int8_t x, y;
    /* Up, right, down, left. */
    int8_t move[4];
    bool selected, highlighted;
    const char *text;
};

struct widget level_widgets[] = {
    { 42, 4, {  0,  1,  5,  0 }, true,  true,  "0" },
    { 48, 4, {  0,  1,  5, -1 }, false, false, "1" },
    { 54, 4, {  0,  1,  5, -1 }, false, false, "2" },
    { 60, 4, {  0,  1,  5, -1 }, false, false, "3" },
    { 66, 4, {  0,  0,  5, -1 }, false, false, "4" },
    { 42, 7, { -5,  1,  0, -1 }, false, false, "5" },
    { 48, 7, { -5,  1,  0, -1 }, false, false, "6" },
    { 54, 7, { -5,  1,  0, -1 }, false, false, "7" },
    { 60, 7, { -5,  1,  0, -1 }, false, false, "8" },
    { 66, 7, { -5,  0,  0, -1 }, false, false, "9" },
    {  1, 4, {  1,  1,  1,  1 }, false, false, "            Initial level of play" },
    { -1, },
};

struct widget players_widgets[] = {
    {  1, 11, {  1,  1,  1,  1 }, false, false, "   # of players (not implemented)" },
    { 42, 11, {  0,  1,  0,  0 }, true,  false, "1 Player" },
    { 59, 11, {  0,  0,  0, -1 }, false, false, "2 Player" },
    { -1, },
};

struct widget rng_widgets[] = {
    { 42, 15, {  0,  1,  0,  0 }, true,  false, "Classic RNG" },
    { 56, 15, {  0,  0,  0, -1 }, false, false, "Shuffle RNG" },
    {  1, 15, {  1,  1,  1,  1 }, false, false, "Piece selection (not implemented)" },
    { -1, },
};

struct widget start_widgets[] = {
    { 29, 19, {  0,  1,  0,  0 }, true,  false, "Start" },
    { 42, 19, {  0,  0,  0, -1 }, false, false, "Return to title" },
    { -1, },
};

static void
draw_widget(const struct widget *w)
{
    uint16_t len = strlen(w->text);

    move_to(w->x, w->y);

    fputs(w->highlighted ? "\033[7m" : "\033[0m", stdout);
    fwrite(spc80, 1, len + 4, stdout);

    move_to(w->x, w->y + 1);
    fwrite(spc80, 1, 2, stdout);

    if (w->selected != w->highlighted)
        fputs(w->selected ? "\033[7m" : "\033[0m", stdout);

    fputs(w->text, stdout);

    if (w->selected != w->highlighted)
        fputs(w->highlighted ? "\033[7m" : "\033[0m", stdout);

    fwrite(spc80, 1, 2, stdout);

    move_to(w->x, w->y + 2);

    fwrite(spc80, 1, len + 4, stdout);
}

static void
draw_widgets(const struct widget *w)
{
    uint16_t i;

    for (i = 0; w[i].x >= 0; i++)
        draw_widget(&w[i]);
}

static void
clear_highlighted(struct widget *w)
{
    uint16_t i;

    for (i = 0; w[i].x >= 0; i++)
        w[i].highlighted = false;
}

static void
copy_selected_to_highlighted(struct widget *w)
{
    uint16_t i;

    for (i = 0; w[i].x >= 0; i++)
        w[i].highlighted = w[i].selected;
}

static int16_t
get_highlighted(struct widget *w)
{
    uint16_t i;

    for (i = 0; w[i].x >= 0; i++)
        if (w[i].highlighted)
            return i;

    return -1;
}

static int16_t
get_selected(struct widget *w)
{
    uint16_t i;

    for (i = 0; w[i].x >= 0; i++)
        if (w[i].selected)
            return i;

    return -1;
}

static bool
do_menu_screen(struct game_mode *mode)
{
    static struct widget *menus[] = {
        level_widgets,
        players_widgets,
        rng_widgets,
        start_widgets,
    };
    uint16_t m = 0;

    /* Cursor off, clear screen. */
    fputs("\033[?25l\033[2J", stdout);

    move_to(2, 2);
    printf("W-A-S-D to navigate a menu. Enter to select. Tab to skip.");

    copy_selected_to_highlighted(level_widgets);
    clear_highlighted(players_widgets);
    clear_highlighted(rng_widgets);
    clear_highlighted(start_widgets);

    draw_widgets(level_widgets);
    draw_widgets(players_widgets);
    draw_widgets(rng_widgets);
    draw_widgets(start_widgets);

    fflush(stdout);

    while (m < ARRAY_SIZE(menus)) {
        int16_t h = get_highlighted(menus[m]);
        int16_t old_h = h;
        int16_t s = get_selected(menus[m]);
        int16_t old_s = s;
        uint16_t old_m = m;
        char c = 0;
        bool redraw = false;

	if (read(0, &c, 1) > 0) {
            switch (c) {
            case 'w':
            case 'W':
                menus[m][h].highlighted = false;
                h += menus[m][h].move[0];
                menus[m][h].highlighted = true;
                redraw = true;
                break;
            case 'a':
            case 'A':
                menus[m][h].highlighted = false;
                h += menus[m][h].move[3];
                menus[m][h].highlighted = true;
                redraw = true;
                break;
            case 's':
            case 'S':
                menus[m][h].highlighted = false;
                h += menus[m][h].move[2];
                menus[m][h].highlighted = true;
                redraw = true;
                break;
            case 'd':
            case 'D':
                menus[m][h].highlighted = false;
                h += menus[m][h].move[1];
                menus[m][h].highlighted = true;
                redraw = true;
                break;
            case '\t':
                menus[m][h].highlighted = false;
                redraw = true;
                m++;
                break;
            case '\n':
            case '\r':
            case ' ':
                s = h;

                if (old_s >= 0)
                    menus[m][old_s].selected = false;

                menus[m][s].selected = true;
                menus[m][s].highlighted = false;
                m++;

                redraw = true;
                break;
            }

            if (redraw) {
                draw_widget(&menus[old_m][old_h]);
                draw_widget(&menus[old_m][h]);
                draw_widget(&menus[old_m][old_s]);
                draw_widget(&menus[old_m][s]);

                if (m != old_m && m < ARRAY_SIZE(menus)) {
                    copy_selected_to_highlighted(menus[m]);
                    draw_widget(&menus[m][get_highlighted(menus[m])]);
                }

                fflush(stdout);
            }
        }

        tick_sleep(1);
    }

    mode->initial_level = get_selected(level_widgets);
    mode->num_players = get_selected(players_widgets);
    mode->rng_mode = get_selected(rng_widgets);

    return get_selected(start_widgets) <= 0;
}

static int
do_two_player_screen(uint16_t *seed)
{
    const int box_x = 40 - (46 / 2);
    struct tetris_message tm;
    bool i_am_ready = false;
    bool they_are_ready = false;
    uint16_t i;

    draw_box(box_x, 7, 46, 7);

    move_to(box_x + 3, 9);
    printf("\033(BConnecting to other player...");

    fflush(stdout);

    move_to(box_x + 3 + 30, 9);

    if (connect_to_other_game(seed) < 0) {
        printf("failed.");
        fflush(stdout);
    }

    printf("connected.");
    move_to(box_x + 3, 10);
    printf("Press any key when ready to play.");
    fflush(stdout);

    for (i = 0; i < 1800; i++) {
        char c;
	if (read(0, &c, 1) > 0) {
            i_am_ready = true;

            if (!they_are_ready) {
                move_to(box_x + 3, 10);
                printf("Waiting for the other player... ");
                fflush(stdout);
            }

            send_ready();
        }

        if (poll_message(&tm) > 0) {
            if (tm.msg_type == READY_TO_PLAY) {
                they_are_ready = true;

                if (!i_am_ready) {
                    move_to(box_x + 3, 11);
                    printf("(Other player is ready.)");
                    fflush(stdout);
                }
            }
        }

        if (i_am_ready && they_are_ready)
            break;

        tick_sleep(1);
    }

    if (i == 1800)
        return -1;

    move_to(box_x + 3, 9);
    printf("                                        ");
    move_to(box_x + 3, 10);
    printf("3...                                    ");
    move_to(box_x + 3, 11);
    printf("                                        ");

    fflush(stdout);
    sleep(1);
    move_to(box_x + 3 + 5, 10);
    printf("2...");
    fflush(stdout);
    sleep(1);
    move_to(box_x + 3 + 10, 10);
    printf("1...");
    fflush(stdout);
    sleep(1);
    move_to(box_x + 3 + 15, 10);
    printf("TETRIS!!!");
    fflush(stdout);
    sleep(1);

    return 0;
}

struct rng_state {
    uint8_t prev;
    uint8_t curr;
    uint16_t seed;
};

static void
rng_state_init(struct rng_state *s, uint16_t seed)
{
    /* Tetrominos are numbered [0, 6]. For the initial state, select different
     * invalid numbers for curr and prev.
     */
    s->prev = 7;
    s->curr = 8;
    s->seed = seed;
}

static const struct tetromino *
select_piece(struct rng_state *s)
{
    uint8_t rng = (lfsr_galois(&s->seed) >> 8) % 7u;

    /* According to https://harddrop.com/wiki/Tetris_(Game_Boy)#Randomizer, the
     * goal of the Game Boy Tetris randomizer was to prevent getting the same
     * piece 3 times in a row. Maintain the spirit of the algorithm without
     * duplicating all of the details.
     */
    if (s->prev == s->curr) {
        if (s->curr == rng) {
            rng = (lfsr_galois(&s->seed) >> 8) % 7u;

            if (s->curr == rng) {
                rng = (lfsr_galois(&s->seed) >> 8) % 7u;
            }
        }
    }

    s->prev = s->curr;
    s->curr = rng;
    return &all_pieces[rng];
}

enum game_state {
    normal,
    drop_one,

    /* Hard drop state must appear before states where the hard drop input is
     * ignored.
     */
    hard_drop,
    lock_piece,
    clearing_lines,
    spawn_piece,
    close_window,
    waiting_other_game_over,
    waiting_game_over,
    game_over,
};

static void
play_game(uint16_t initial_level, uint16_t seed, bool two_player)
{
    uint16_t well[WELL_SIZE];
    uint16_t piece_counts[7];
    struct garbage_state gs;
    struct rng_state rngs;

    uint16_t x = 4;
    uint16_t y = 0;
    uint16_t rotation = 0;
    uint16_t old_x = 0;
    uint16_t old_y = 0xffff;
    uint16_t old_rotation = 0;
    uint16_t level = initial_level;
    uint16_t delay_reset = delay_for_level[level];
    uint16_t delay = delay_reset;
    uint16_t lines = 0;
    uint32_t score = 0;
    uint32_t their_score = 0;
    bool their_game_over = false;
    bool prev_was_tetris = false;
    uint16_t garbage_lines = 0;

    int16_t send_score_delay = 60;

    int16_t lines_next_level = 10 * (level + 1);

    uint16_t complete[4];
    uint16_t complete_count;

    const struct tetromino *next_piece;
    const struct tetromino *piece;
    enum game_state state;

    gs.garbage = 0;
    gs.remain = 0;
    gs.seed = seed;

    rng_state_init(&rngs, seed);

    game_init_well_state(well);

    /* Cursor off, clear screen. */
    fputs("\033[?25l\033[2J", stdout);

    next_piece = select_piece(&rngs);
    piece = next_piece;

    memset(piece_counts, 0, sizeof(piece_counts));
    piece_counts[piece - all_pieces]++;

    draw_well_from_scratch(well, piece_counts, 0);
    draw_controls();
    draw_score(score, score, lines, level);
    draw_piece(piece, x, y, rotation);

    state = spawn_piece;
    while (state != game_over) {
        bool redraw_piece = false;
        bool redraw_score = false;
        struct tetris_message tm;
        char c = 0;

	old_x = x;
	old_y = y;
	old_rotation = rotation;

	if (read(0, &c, 1) > 0) {
	    switch (c) {
            case 'a':
            case 'A':
		if (x > 0)
		    x--;
		break;

            case 'd':
            case 'D':
		x++;
		break;

            case 's':
            case 'S': {
                if (state < hard_drop)
                    state = hard_drop;

		break;
	    }

            case 'e':
            case 'q':
            case 'E':
            case 'Q': {
		int16_t dir = c == 'e' ? 1 : -1;
		int16_t delta;

		rotation = (rotation + dir) & 3;

		delta = piece->f[old_rotation].shift - piece->f[rotation].shift;
		x = (int16_t)x >= delta ? x - delta : 0;
		break;
	    }

	    case 'r':
	    case 'R':
		draw_well_from_scratch(well, piece_counts, lines);
		fflush(stdout);
		break;
	    }
	}

        delay--;

        switch (state) {
        case normal:
        case drop_one:
            if (x != old_x || rotation != old_rotation) {
                if (!game_can_do(well, piece->f[rotation].mask, x, y)) {
                    x = old_x;
                    rotation = old_rotation;
                }

                redraw_piece = true;
            }

            if (state == drop_one) {
                if (!game_can_do(well, piece->f[rotation].mask, x, ++y)) {
                    y--;
                    state = lock_piece;
                } else {
                    redraw_piece = true;
                    state = normal;
                    delay = delay_reset;
                }
            } else if (delay == 0) {
                state = drop_one;
            }
            break;

        case lock_piece: {
            /* old_x and old_rotation are used to ignore lateral movement or
             * rotation while locking. This state will immediately transition to a differnt
             */
	    game_set_piece(well, piece->f[old_rotation].mask, old_x, y);

	    complete_count = game_check_complete_lines(well, complete);
	    if (complete_count > 0) {
                uint16_t idx = 2 * complete_count + prev_was_tetris;

		draw_complete_lines(complete, complete_count);

		lines += complete_count;
		score += level * points_for_lines[idx];
		prev_was_tetris = complete_count == 4;

		lines_next_level -= complete_count;
		if (lines_next_level <= 0) {
		    lines_next_level += 10;

		    level++;

                    if (level >= ARRAY_SIZE(delay_for_level))
                        delay_reset = delay_for_level[ARRAY_SIZE(delay_for_level) - 1];
                    else
                        delay_reset = delay_for_level[level];
                }

                delay = 20;
                state = clearing_lines;
                redraw_score = true;

                if (two_player) {
                    send_score(score, garbage_for_lines[idx]);
                    send_score_delay = 60;
                }
	    } else {
                state = spawn_piece;
            }
            break;
        }

        case hard_drop:
            /* Ignore lateral movement or rotation during hard drop. */
            x = old_x;
            rotation = old_rotation;

            /* Similar to drop_one except the game stays in this state until
             * transitioning to lock_piece. Also accumulate points along the
             * way.
             */
            score += 2;
	    if (!game_can_do(well, piece->f[rotation].mask, x, ++y)) {
                score -= 2;
		y--;
                state = lock_piece;
            } else {
                redraw_piece = true;
            }

            redraw_score = true;
            break;

        case clearing_lines:
            if (delay == 0) {
		game_remove_lines(well, complete, complete_count);
		draw_well_from_scratch(well, piece_counts, lines);

                state = spawn_piece;
                redraw_score = true;
            }
            break;

        case spawn_piece:
	    piece_counts[next_piece - all_pieces]++;

	    x = 4;
	    y = 0;
            old_x = x;
	    rotation = 0;

            old_x = x;
            old_y = y;
            old_rotation = rotation;

	    piece = next_piece;

	    next_piece = select_piece(&rngs);

            delay = delay_reset;
            state = normal;
            redraw_piece = true;

           if (garbage_lines > 0) {
               game_insert_garbage(well, garbage_lines, &gs);
               draw_well_from_scratch(well, piece_counts, lines);
               garbage_lines = 0;
           }

	    erase_piece(piece, 14 + piece->f[0].shift, 5, 0);
	    draw_piece(next_piece, 14 + next_piece->f[0].shift, 5, 0);

	    /* If the new piece cannot be placed, the well is full, and the
	     * game is over.
	     */
	    if (!game_can_do(well, piece->f[rotation].mask, x, y)) {
                if (two_player)
                    send_game_over(score);

                delay = 1;
                y = 3;
                state = close_window;
            }

            break;

        case close_window:
            y = old_y;

            if (delay <= 0) {
                if (y < 23) {
                    move_to(22, y);
                    printf("====================");
                    fflush(stdout);

                    delay = 7;
                    y++;
                } else {
                    if (two_player) {
                        draw_final_scores(score, their_score, their_game_over);
                        state = waiting_other_game_over;
                    } else {
                        move_to(22, 13);
                        printf("Press a key to exit.");

                        state = waiting_game_over;
                    }

                    fflush(stdout);
                }
            }
            break;

        case waiting_other_game_over:
            if (their_game_over)
                state = waiting_game_over;
            break;

        case waiting_game_over:
            if (c != 0)
                state = game_over;
            break;

        case game_over:
            break;
        }

        if (poll_message(&tm) > 0) {
            if (tm.msg_type == SEND_SCORE) {
                /* FINISHME: Some protocol robustness is in order here.
                 * The received score must be >= the existing known score.
                 */
                their_score = tm.msg_data[0];
                garbage_lines += tm.msg_data[1];
                redraw_score = true;
            } else if (tm.msg_type == MY_GAME_OVER) {
                their_game_over = true;
                their_score = tm.msg_data[0];
            }
        }

        if (redraw_score) {
            draw_score(score, MAX2(score, their_score), lines, level);

            if (two_player && (state == waiting_other_game_over ||
                               state == waiting_game_over)) {
                draw_final_scores(score, their_score, their_game_over);
            }
        }

	if (redraw_piece) {
	    erase_piece(piece, old_x, old_y, old_rotation);
	    draw_piece(piece, x, y, rotation);
	}

        /* So the other player knows this player still exists, send the score
         * about once per second.
         */
        if (two_player && --send_score_delay < 0) {
            send_score(score, 0);
            send_score_delay = 60;
        }

        if (redraw_score || redraw_piece)
            fflush(stdout);

	tick_sleep(1);
    }

    return;
}

int
main(int argc, char **argv)
{
    init_file_io();

    while (true) {
        struct game_mode mode;

        if (do_title_screen() == 1)
            break;

        if (do_menu_screen(&mode)) {
            struct tms t;
            uint16_t seed = times(&t);

            if (mode.num_players == 2) {
                /* If the connection fails for any reason, go back to the title
                 * screen.
                 */
                if (do_two_player_screen(&seed) < 0)
                    continue;
            }

            play_game(mode.initial_level, seed, mode.num_players == 2);
        }
    }

    fully_disconnect();
    fputs("\033[24;0f", stdout);
    fputs("\033[?25h", stdout);
    return 0;
}
