// q2_fatfs_io.h -- SD card and file I/O (see q2_fatfs_io.c).

#ifndef Q2_FATFS_IO_H
#define Q2_FATFS_IO_H

// Where the game data lives on the SD card: <dir>/baseq2/pak0.pak, saves in
// <dir>/baseq2/save. The engine's basedir.
#define Q2_ASSET_DIR	"/roms/quake2"

// Mounts the card and checks for the game data; fatal (on screen) if missing.
void	q2_fatfs_init (void);

#endif
