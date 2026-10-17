// q2_console.c -- the boot console (see q2_console.h).

#include <string.h>

#include "pico/stdio.h"
#include "pico/stdio/driver.h"

#include "q2_console.h"
#include "FrensFonts.h"
#include "font_8x8.h"		// the pico_shared 8x8 font (FrensFonts.cpp is C++)

static unsigned font_slice (char c, int row)
{
	return (unsigned char)font_8x8[(c - FONT_FIRST_ASCII) + row * FONT_N_CHARS];
}

#define FB_W	320

static char		grid[Q2CON_ROWS][Q2CON_COLS];
static int		cur_row = 1, cur_col;		// row 0 is the title bar
static uint8_t	*surface;
static const char	*title = "pico-quake2";

static void paint_row (int row)
{
	int		col, y, x;
	uint8_t	bg = row ? Q2CON_BG : Q2CON_TITLE_BG;
	uint8_t	fg = row ? Q2CON_FG : Q2CON_TITLE_FG;

	if (!surface)
		return;
	for (col = 0; col < Q2CON_COLS; col++)
	{
		char	c = grid[row][col];

		if (c < FONT_FIRST_ASCII || c >= FONT_FIRST_ASCII + FONT_N_CHARS)
			c = ' ';
		for (y = 0; y < FONT_CHAR_HEIGHT; y++)
		{
			unsigned	slice = font_slice (c, y);
			uint8_t		*px = surface + (row * FONT_CHAR_HEIGHT + y) * FB_W + col * FONT_CHAR_WIDTH;

			for (x = 0; x < FONT_CHAR_WIDTH; x++, slice >>= 1)
				px[x] = (slice & 1) ? fg : bg;
		}
	}
}

static void set_title_row (void)
{
	size_t	n = strlen (title);

	memset (grid[0], ' ', Q2CON_COLS);
	if (n > Q2CON_COLS)
		n = Q2CON_COLS;
	memcpy (grid[0] + (Q2CON_COLS - n) / 2, title, n);
}

static void scroll (void)
{
	int		r;

	memmove (grid[1], grid[2], (Q2CON_ROWS - 2) * Q2CON_COLS);
	memset (grid[Q2CON_ROWS - 1], ' ', Q2CON_COLS);
	for (r = 1; r < Q2CON_ROWS; r++)
		paint_row (r);
}

void q2_console_write (const char *s, size_t len)
{
	int		dirty = -1;

	while (len--)
	{
		char	c = *s++;

		if (c == '\r')
		{
			cur_col = 0;
			continue;
		}
		if (c == '\n' || cur_col == Q2CON_COLS)
		{
			if (dirty >= 0)
				paint_row (dirty);
			dirty = -1;
			cur_col = 0;
			if (++cur_row == Q2CON_ROWS)
			{
				cur_row = Q2CON_ROWS - 1;
				scroll ();
			}
			if (c == '\n')
				continue;
		}
		if (c == '\t')
			c = ' ';
		grid[cur_row][cur_col++] = c;
		dirty = cur_row;
	}
	if (dirty >= 0)
		paint_row (dirty);
}

static void stdio_out (const char *buf, int len)
{
	q2_console_write (buf, (size_t)len);
}

static stdio_driver_t console_driver = { .out_chars = stdio_out };

void q2_console_attach_stdio (void)
{
	memset (grid, ' ', sizeof(grid));
	set_title_row ();
	stdio_set_driver_enabled (&console_driver, true);
}

void q2_console_show (uint8_t *fb)
{
	int		r;

	surface = fb;
	for (r = 0; r < Q2CON_ROWS; r++)
		paint_row (r);
}

void q2_console_stop (void)
{
	surface = NULL;
}

bool q2_console_active (void)
{
	return surface != NULL;
}

void q2_console_set_title (const char *t)
{
	title = t;
	set_title_row ();
	paint_row (0);
}
