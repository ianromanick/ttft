/*
 * Copyright © 2026 Ian D. Romanick
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>
#include <termios.h>
#include <assert.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

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
    unsigned shift;
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

static void
draw_well_from_scratch(const uint16_t *well, const uint16_t *piece_counts,
		       uint16_t lines)
{
    uint16_t i;
    uint16_t j;
    char border[2];

    fputs("\x1b[3;20f\x1b[7m\x1b(0", stdout);

    border[0] = 'l';
    border[1] = 'k';
    for (i = WELL_SPAWN; i < 20 + WELL_SPAWN; i++) {
	printf("\x1b[7m%c%c\x1b[m", border[0], border[1]);

	for (j = 15; j > 5; j--) {
	    const uint16_t bit = 1u << j;

	    if ((well[i] & bit) != 0) {
		printf("aa");
	    } else {
		printf("  ");
	    }
	}

	printf("\x1b[7m%c%c\n\x1b[19C", border[0], border[1]);
	border[0] = 'x';
	border[1] = 'x';
    }

    fputs("mvqqqqqqqqqqqqqqqqqqqqvj\x1b(B", stdout);

    fprintf(stdout, "\x1b[3f\x1b[7m\x1b(0");
    fprintf(stdout, "lwqqqqqqqqqqqqwk\n");
    fprintf(stdout, "xx\x1b[m\x1b[12C\x1b[7mxx\n");
    fprintf(stdout, "xx\x1b[m \x1b(BTop Score  \x1b(0\x1b[7mxx\n");
    fprintf(stdout, "xx\x1b[m\x1b[12C\x1b[7mxx\n");
    fprintf(stdout, "xx\x1b[m\x1b[12C\x1b[7mxx\n");
    fprintf(stdout, "xx\x1b[m \x1b(BScore      \x1b(0\x1b[7mxx\n");
    fprintf(stdout, "xx\x1b[m\x1b[12C\x1b[7mxx\n");
    fprintf(stdout, "xx\x1b[m\x1b[12C\x1b[7mxx\n");
    fprintf(stdout, "xx\x1b[m \x1b(BLines      \x1b(0\x1b[7mxx\n");
    fprintf(stdout, "xx\x1b[m\x1b[12C\x1b[7mxx\n");
    fprintf(stdout, "xx\x1b[m\x1b[12C\x1b[7mxx\n");
    fprintf(stdout, "xx\x1b[m \x1b(BLevel      \x1b(0\x1b[7mxx\n");
    fprintf(stdout, "xx\x1b[m\x1b[12C\x1b[7mxx\n");
    fprintf(stdout, "xx\x1b[m\x1b[12C\x1b[7mxx\n");
    fprintf(stdout, "mvqqqqqqqqqqqqvj\x1b(B");

    fprintf(stdout, "\x1b[3;46f\x1b[7m\x1b(0");
    fprintf(stdout, "lwqq\x1b[m NEXT \x1b[7mqqwk\x1b[B\x1b[14D");
    fprintf(stdout, "xx\x1b[m          \x1b[7mxx\x1b[B\x1b[14D");
    fprintf(stdout, "xx\x1b[m          \x1b[7mxx\x1b[B\x1b[14D");
    fprintf(stdout, "xx\x1b[m          \x1b[7mxx\x1b[B\x1b[14D");
    fprintf(stdout, "xx\x1b[m          \x1b[7mxx\x1b[B\x1b[14D");
    fprintf(stdout, "xx\x1b[m          \x1b[7mxx\x1b[B\x1b[14D");
    fprintf(stdout, "xx\x1b[m          \x1b[7mxx\x1b[B\x1b[14D");
    fprintf(stdout, "mvqqqqqqqqqqvj\x1b(B");

    fprintf(stdout, "\x1b[3;61f\x1b[0m\x1b)0");
    for (i = 0; i < 7; i++) {
	printf("%c: %d\x1b[%d;61f", all_pieces[i].name,
	       piece_counts[i], 4 + i);
    }
}

static void
draw_controls(void)
{
    fprintf(stdout,
	    "\x1b[13;46f"
	    "\x1b[14;46f rotate              rotate"
	    "\x1b[15;46f counter            clockwise"
	    "\x1b[16;46fclockwise          /"
	    "\x1b[17;46f          \\       /"
	    "\x1b[18;46f           q     e"
	    "\x1b[19;46f           a  s  d"
	    "\x1b[20;46f          /   |   \\"
	    "\x1b[21;46f      move    |    move"
	    "\x1b[22;46f     left     |     right"
	    "\x1b[23;46f            hard"
	    "\x1b[24;46f            drop"
	    "\x1b[3;61f\x1b[0m\x1b)0");
}

static void
move_to(uint16_t x, uint16_t y)
{
    if (y == 0) {
	printf("\x1b[;%df", x);
    } else {
	printf("\x1b[%d;%df", y, x);
    }
}

static void
erase_piece(const struct tetromino *t,
	   uint16_t x, uint16_t y, uint16_t rotation)
{
    printf("\x1b[m\x1b(0");
    move_to(22 + 2 * x, y);
    fwrite(t->f[rotation].erase, 1,
	   strlen(t->f[rotation].erase), stdout);
    printf("\x1b[m\x1b(B");
}

static void
draw_piece(const struct tetromino *t,
	   uint16_t x, uint16_t y, uint16_t rotation)
{
    printf("\x1b[7m\x1b(0");
    move_to(22 + 2 * x, y);
    fwrite(t->f[rotation].draw, 1,
	   strlen(t->f[rotation].draw), stdout);
    printf("\x1b[m\x1b(B");
}

static void
draw_complete_lines(const uint16_t *complete, uint16_t count)
{
    uint16_t i;

    for (i = 0; i < count; i++) {
	printf("\x1b[%d;22f\x1b(0\x1b[5maaaaaaaaaaaaaaaaaaaa", complete[i]);
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
draw_score(uint32_t score, uint16_t lines, uint16_t level)
{
    /* 10 digits, 3 separators, and a NUL. */
    char buf[10 + 3 + 1];
    int len;

    len = format_number_u32(score, buf);
    fprintf(stdout, "\x1b[m\x1b[9;%df", (3 + 2 + 10) - len);
    fwrite(buf, 1, len - 1, stdout);

    len = format_number_u16(lines, buf);
    fprintf(stdout, "\x1b[12;%df", (3 + 2 + 10) - len);
    fwrite(buf, 1, len - 1, stdout);

    len = format_number_u16(level, buf);
    fprintf(stdout, "\x1b[15;%df", (3 + 2 + 10) - len);
    fwrite(buf, 1, len - 1, stdout);
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

#if defined linux
static void
tick_sleep(uint16_t t)
{
    uint16_t x = t % 60;
    struct timespec duration = { t / 60, 0 };

    if (x != 0)
	duration.tv_nsec = 1000000000 / (60 / x);

    nanosleep(&duration, NULL);
}
#endif

/* This is mostly the "guideline scoring system." T-spins are not detected, so
 * extra points are not awarded for those. Access as "(number of lines * 2) +
 * previous was Tetris."
 */
static const uint16_t points_for_lines[] = {
    0, 0, 100, 100, 300, 300, 500, 500, 800, 1200
};

int
main(int argc, char **argv)
{
    uint16_t well[WELL_SIZE];
    uint16_t piece_counts[7];

    struct termios raw;

    tcgetattr(STDIN_FILENO, &raw);
    raw.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    srand(time(NULL));

    game_init_well_state(well);

    /* Cursor off, clear screen. */
    fputs("\x1b[?25l\x1b[2J", stdout);

    uint16_t x = 4;
    uint16_t y = 0;
    uint16_t rotation = 0;
    uint16_t old_x = 0;
    uint16_t old_y = 0xffff;
    uint16_t old_rotation = 0;
    uint16_t delay_reset = 20;
    uint16_t delay = delay_reset;
    uint16_t lines = 0;
    uint16_t level = 1;
    uint32_t score = 0;
    bool prev_was_tetris = false;

    int16_t lines_next_level = 10;
    /* We want the speed ups at 4, 7, 9, and 13. */
    uint16_t levels_next_speedup = 3;

    /* At level 13 this will become 0, and the game will likely be too
     * fast to play.
     */
    uint16_t ticks_to_sleep = 4;

    const struct tetromino *piece = &all_pieces[rand() % ARRAY_SIZE(all_pieces)];
    const struct tetromino *next_piece = &all_pieces[rand() % ARRAY_SIZE(all_pieces)];

    memset(piece_counts, 0, sizeof(piece_counts));
    piece_counts[piece - all_pieces]++;

    draw_well_from_scratch(well, piece_counts, 0);
    draw_controls();
    draw_score(score, lines, level);
    draw_piece(piece, x, y, rotation);

    while (true) {
	int need_new_piece;

	if (old_y == 0xffff) {
	    draw_piece(piece, x, y, rotation);

	    erase_piece(piece, 14 + piece->f[0].shift, 5, 0);
	    draw_piece(next_piece, 14 + next_piece->f[0].shift, 5, 0);

	    /* If the new piece cannot be placed, the well is full, and the
	     * game is over.
	     */
	    if (!game_can_do(well, piece->f[rotation].mask, x, y))
		break;
	}

	fflush(stdout);
	tick_sleep(ticks_to_sleep);

	old_x = x;
	old_y = y;
	old_rotation = rotation;

	int ret;
	struct pollfd pfd = { STDIN_FILENO, POLLIN, 0 };

	ret = poll(&pfd, 1, 1);
	if (ret == -1)
	    perror("poll");

	if (ret > 0) {
	    char c = 0;
	    int r = read(0, &c, 1);

	    switch (c) {
	    case 'a':
		if (x > 0)
		    x--;
		break;

	    case 'd':
		x++;
		break;

	    case 's': {
		uint16_t drop = 1;

		while (game_can_do(well, piece->f[rotation].mask, x, y + drop))
		    drop++;

		if (--drop > 0) {
		    /* Hard drops are 2 points per cell. */
		    score += 2 * drop;

		    /* FIXME: This doesn't actually set the piece. It won't
		     * actually set until the delay expires. Perhaps this is
		     * desirable?
		     */
		    y += drop;
		}

		break;
	    }

	    case 'e':
	    case 'q': {
		int16_t dir = c == 'e' ? 1 : -1;
		int16_t delta;

		rotation = (rotation + dir) & 3;

		delta = piece->f[old_rotation].shift - piece->f[rotation].shift;
		x = x >= delta ? x - delta : 0;
		break;
	    }

	    case 'r':
		draw_well_from_scratch(well, piece_counts, lines);
		break;
	    }
	}

	if (x != old_x || y != old_y || rotation != old_rotation) {
	    if (!game_can_do(well, piece->f[rotation].mask, x, y)) {
		x = old_x;
		rotation = old_rotation;
	    }
	}

	need_new_piece = 0;
	if (--delay == 0) {
	    y++;
	    delay = delay_reset;

	    if (!game_can_do(well, piece->f[rotation].mask, x, y)) {
		y--;
		need_new_piece = 1;
	    }
	}

	if (x != old_x || y != old_y || rotation != old_rotation) {
	    erase_piece(piece, old_x, old_y, old_rotation);
	    draw_piece(piece, x, y, rotation);
	}

	if (need_new_piece) {
	    game_set_piece(well, piece->f[rotation].mask, x, y);
	    x = 4;
	    y = 0;
	    rotation = 0;
	    old_y = 0xffff;

	    uint16_t complete[4];
	    uint16_t count = game_check_complete_lines(well, complete);

	    if (count > 0) {
		draw_complete_lines(complete, count);

		lines += count;
		score += level * points_for_lines[2 * count + prev_was_tetris];
		prev_was_tetris = count == 4;

		lines_next_level -= count;
		if (lines_next_level <= 0) {
		    lines_next_level += 10;

		    level++;
		    levels_next_speedup--;

		    if (levels_next_speedup == 0) {
			levels_next_speedup = 5;
		    }
		}

		draw_score(score, lines, level);

		fflush(stdout);
		tick_sleep(120);
		game_remove_lines(well, complete, count);
		draw_well_from_scratch(well, piece_counts, lines);
	    }

	    piece_counts[next_piece - all_pieces]++;

	    piece = next_piece;
	    next_piece = &all_pieces[rand() % ARRAY_SIZE(all_pieces)];
	}
    }

    fputs("\x1b[24;0f", stdout);
    fputs("\x1b[?25h", stdout);
    return 0;
}
