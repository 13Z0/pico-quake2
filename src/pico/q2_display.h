// q2_display.h -- HSTX video output for the engine's 320x240 8bpp framebuffers.

#ifndef Q2_DISPLAY_H
#define Q2_DISPLAY_H

#include <stdint.h>

// Brings up HSTX 640x480 on core1 with the boot console on screen. Idempotent.
void	q2_display_init (void);
// Hands the screen back to the boot console (fatal errors).
void	q2_display_show_console (void);
// HSTX frames scanned out since init (60 per second).
uint32_t	q2_display_frames (void);

#endif
