// q2_display.c -- HSTX video output and the renderer's SWimp_* hooks.
//
// The engine renders 320x240 8bpp into one of two framebuffers in SRAM while core1
// scans out the other: its scanline callback (in SRAM, inside the DMA interrupt)
// expands each 320-pixel row through a 256-entry table of doubled RGB555 pixels into
// the 640-pixel line, and each row is sent twice for 480 lines. Buffer flips and
// palette changes are latched at vsync, so neither tears.

#include <string.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"

#include "video_output.h"
#include "hstx_data_island_queue.h"

#include "ref_soft/r_local.h"

#include "q2_display.h"
#include "q2_console.h"

#define FB_W	320
#define FB_H	240

static uint8_t	fb[2][FB_W * FB_H] __attribute__((aligned(4)));
static uint32_t	lut[2][256];			// RGB555 pixel in both halves (2x horizontal)
static uint32_t	__scratch_y("q2_core1_stack") core1_stack[1024];	// 4 KB SCRATCH_Y bank (unused by the SDK here)

static volatile int		front;			// framebuffer core1 scans out
static volatile int		pending_front = -1;
static volatile int		lut_front;
static volatile int		pending_lut = -1;
static int				engine_lut = -1;	// engine palette held back while the boot console shows
static volatile uint32_t	frames;
static bool		video_up;

static void __not_in_flash_func(scanline_cb) (uint32_t v_scanline, uint32_t active_line, uint32_t *line_buffer)
{
	const uint8_t	*src = fb[front] + (active_line >> 1) * FB_W;
	const uint32_t	*pal = lut[lut_front];
	int				i;

	(void)v_scanline;
	for (i = 0; i < FB_W; i += 4)
	{
		line_buffer[i] = pal[src[i]];
		line_buffer[i + 1] = pal[src[i + 1]];
		line_buffer[i + 2] = pal[src[i + 2]];
		line_buffer[i + 3] = pal[src[i + 3]];
	}
}

static void __not_in_flash_func(vsync_cb) (void)
{
	frames++;
	if (pending_front >= 0)
	{
		front = pending_front;
		pending_front = -1;
	}
	if (pending_lut >= 0)
	{
		lut_front = pending_lut;
		pending_lut = -1;
	}
}

static uint32_t rgb555x2 (unsigned r, unsigned g, unsigned b)
{
	uint32_t	px = ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
	return px | (px << 16);
}

// The palette the boot console paints with (Q2CON_* indices).
static void load_console_palette (uint32_t *pal)
{
	memset (pal, 0, 256 * sizeof(uint32_t));
	pal[Q2CON_BG] = rgb555x2 (0, 0, 0);
	pal[Q2CON_FG] = rgb555x2 (255, 255, 255);
	pal[Q2CON_TITLE_BG] = rgb555x2 (120, 60, 20);
	pal[Q2CON_TITLE_FG] = rgb555x2 (255, 255, 255);
}

void q2_display_init (void)
{
	if (video_up)
		return;

	// clk_hstx is set to 126 MHz in q2_boot.c (25.2 MHz pixel clock).
	if (clock_get_hz (clk_hstx) != 126000000u)
		printf ("q2_display: clk_hstx is %lu Hz, expected 126 MHz\n", (unsigned long)clock_get_hz (clk_hstx));

	memset (fb, Q2CON_BG, sizeof(fb));
	load_console_palette (lut[0]);
	front = 0;
	lut_front = 0;

	// HDMI mode, with data-island audio at 44.1 kHz (q2_audio.c). The rate is set
	// after video_output_init (which sets 48 kHz) and before core1 starts, since it
	// rebuilds the blanking lines core1 sends.
	video_output_set_dvi_mode (false);
	hstx_di_queue_init ();		// the audio queue and its silence packet (hstx.c, not built, would do it)
	video_output_set_vsync_callback (vsync_cb);
	video_output_init (FB_W * 2, FB_H * 2);
	pico_hdmi_set_audio_sample_rate (44100);
	video_output_set_scanline_callback (scanline_cb);
	multicore_launch_core1_with_stack (video_output_core1_run, core1_stack, sizeof(core1_stack));
	video_up = true;

	q2_console_show (fb[0]);
	printf ("q2_display: HSTX 640x480 up\n");
}

void q2_display_show_console (void)
{
	int		f;

	q2_display_init ();
	f = front;
	load_console_palette (lut[lut_front ^ 1]);
	pending_lut = lut_front ^ 1;
	q2_console_show (fb[f]);
}

uint32_t q2_display_frames (void)
{
	return frames;
}

/*
==============================================================================

SWimp_* (the software renderer's platform hooks)

==============================================================================
*/

int SWimp_Init (void *hInstance, void *wndProc)
{
	(void)hInstance;
	(void)wndProc;
	q2_display_init ();
	vid.buffer = fb[front ^ 1];
	vid.rowbytes = FB_W;
	return true;
}

rserr_t SWimp_SetMode (int *pwidth, int *pheight, int mode, qboolean fullscreen)
{
	(void)mode;
	(void)fullscreen;
	*pwidth = FB_W;
	*pheight = FB_H;
	vid.buffer = fb[front ^ 1];
	vid.rowbytes = FB_W;
	ri.Vid_NewWindow (FB_W, FB_H);
	return rserr_ok;
}

void SWimp_Shutdown (void)
{
}

void SWimp_AppActivate (qboolean active)
{
	(void)active;
}

void SWimp_BeginFrame (float camera_separation)
{
	(void)camera_separation;
}

// pal: 256 entries of r, g, b, unused
void SWimp_SetPalette (const unsigned char *pal)
{
	int			i, back;
	uint32_t	*dst;

	if (!pal)
		return;
	back = lut_front ^ 1;
	dst = lut[back];
	for (i = 0; i < 256; i++, pal += 4)
		dst[i] = rgb555x2 (pal[0], pal[1], pal[2]);
	// The engine sets its palette while the boot console is still on screen, and the
	// console's colours are indices 0-3, nearly black in the Quake palette: hold the
	// palette back until the engine shows its first frame (SWimp_EndFrame).
	if (q2_console_active ())
		engine_lut = back;
	else
		pending_lut = back;
}

// Waits until the flip requested by the last SWimp_EndFrame has happened, i.e. until
// vid.buffer is no longer on screen (at most one frame; bounded in case core1 has
// stopped). The renderer calls it before its first pixel write of a frame.
void SWimp_WaitBuffer (void)
{
	uint32_t	t0;

	if (pending_front < 0)
		return;
	t0 = time_us_32 ();
	while (pending_front >= 0 && time_us_32 () - t0 < 50000)
		tight_loop_contents ();
}

void SWimp_EndFrame (void)
{
	int			back = front ^ 1;

	if (q2_console_active ())
	{
		q2_console_stop ();		// the engine has drawn a frame of its own
		if (engine_lut >= 0)
		{
			pending_lut = engine_lut;	// latched with this frame, at the same vsync
			engine_lut = -1;
		}
	}

	// a frame that wrote nothing must still not schedule two flips at once
	SWimp_WaitBuffer ();

	// show what was just drawn from the next vsync and draw the next frame into the
	// buffer that is on screen until then; SWimp_WaitBuffer holds off the first write
	pending_front = back;
	vid.buffer = fb[back ^ 1];
}
