// q2_console.h -- the boot console: 40x30 characters of stdout on the HDMI output.
//
// Text is captured from the first printf (as a pico_stdio driver) and painted into
// an 8bpp framebuffer with the palette indices below, until the engine draws its
// first frame. The grid keeps updating afterwards, so a fatal error can show the
// lines that led to it.

#ifndef Q2_CONSOLE_H
#define Q2_CONSOLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define Q2CON_COLS		40
#define Q2CON_ROWS		30

// palette indices the console paints with (q2_display.c loads matching colours)
#define Q2CON_BG		0
#define Q2CON_FG		1
#define Q2CON_TITLE_BG	2
#define Q2CON_TITLE_FG	3

void	q2_console_attach_stdio (void);
void	q2_console_write (const char *s, size_t len);
// paints the whole grid into fb (320x240, stride 320) and keeps painting on writes
void	q2_console_show (uint8_t *fb);
void	q2_console_stop (void);		// the engine owns the screen from now on
bool	q2_console_active (void);
void	q2_console_set_title (const char *title);	// not copied

#endif
