// q2_settings.h -- the port's own settings on the SD card (see q2_settings.c).

#ifndef Q2_SETTINGS_H
#define Q2_SETTINGS_H

#include <stdbool.h>

// Reads /settings_QUAKE2.dat and decides the display mode to start in. Called by
// q2_fatfs_init right after the card is mounted, before the display starts.
void	q2_settings_load (void);

// The display mode to start in: true for DVI. The board default until loaded.
bool	q2_settings_boot_dvi (void);

// Stores the display mode picked in the menu (written at once if it changed).
void	q2_settings_save_display (bool dvi);

#endif
