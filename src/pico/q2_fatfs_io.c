// q2_fatfs_io.c -- FatFs-backed file I/O (adapted from pico-duke3D's duke_fatfs_io.c).
//
// The engine reaches files through stdio (fopen/fread/fseek in qcommon/files.c and
// the game's savegame code), which ends in the newlib syscalls _open/_read/_write/
// _lseek/_close/_fstat. They are implemented here on FatFs, with the POSIX names
// as well; fds 0/1/2 stay with the console.
//
// files.c re-opens a pak for every file it reads from it and seeks to the file's
// offset. FatFs would walk the FAT chain from the start of the pak on each seek, so
// read-only opens of large files get a fast-seek cluster map, built once per file and
// shared by every later open of it.
//
// Also: QG_FindFirst/QG_FindNext/QG_FindClose (f_findfirst) and QG_Mkdir.

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "pico/stdlib.h"
#include "pico/stdio.h"
#include "hardware/pio.h"
#include "ff.h"
#include "tf_card.h"

#include "quakegeneric.h"
#include "q2_fatfs_io.h"
#include "q2_fatal.h"

#define Q2_MAXFDS		8		// the engine keeps one handle per pak open plus a few files
#define Q2_FD_BASE		3		// clear of stdio fds 0/1/2

#define FASTSEEK_MIN	(1024 * 1024)	// files at least this large get a cluster map
#define FASTSEEK_FILES	8

#define SD_MOUNT_ATTEMPTS	5

static FIL		fils[Q2_MAXFDS];
static uint8_t	used[Q2_MAXFDS];
static FATFS	fs;

static struct
{
	char	path[64];
	DWORD	*table;
} linkmaps[FASTSEEK_FILES];

void q2_fatfs_init (void)
{
	static pico_fatfs_spi_config_t cfg = {
		SDCARD_SPI, CLK_SLOW_DEFAULT, CLK_FAST_DEFAULT_PIO,
		SD_RX, SD_CS, SD_SCK, SD_TX, true
	};
	FILINFO		fno;
	FRESULT		fr;

	if (pico_fatfs_set_config (&cfg))
		printf ("q2_fatfs: hardware SPI%d\n", spi_get_index (SDCARD_SPI));
	else
	{
		pico_fatfs_config_spi_pio (SDCARD_PIO, pio_claim_unused_sm (SDCARD_PIO, true));
		printf ("q2_fatfs: PIO-SPI\n");
	}
	// The Fruit Jam's card sometimes does not answer its first initialisation
	// after a reset (FR_NOT_READY). FatFs initialises the card again on the next
	// mount, so a few retries ride that out.
	for (int attempt = 1; ; attempt++)
	{
		fr = f_mount (&fs, "", 1);
		if (fr == FR_OK || attempt == SD_MOUNT_ATTEMPTS)
			break;
		printf ("q2_fatfs: mount failed (FatFs error %d), retrying\n", fr);
		sleep_ms (250);
	}
	if (fr != FR_OK)
		q2_fatal ("No SD card (FatFs error %d).\n"
			"Insert a FAT/exFAT card with\n" Q2_ASSET_DIR "/baseq2/pak0.pak.", fr);
	fr = f_chdir (Q2_ASSET_DIR);
	if (fr != FR_OK)
		q2_fatal (Q2_ASSET_DIR " not found on the SD card.\n"
			"Create it and copy the baseq2 folder\nof Quake II into it.");
	fr = f_stat ("baseq2/pak0.pak", &fno);
	if (fr != FR_OK)
		q2_fatal ("baseq2/pak0.pak not found in\n" Q2_ASSET_DIR ".\n"
			"Copy the baseq2 folder of Quake II\n(or of the shareware demo) there.");
	printf ("q2_fatfs: " Q2_ASSET_DIR "/baseq2/pak0.pak, %lu KB\n", (unsigned long)(fno.fsize / 1024));
}

static const char *strip_dotslash (const char *p)
{
	if (p && p[0] == '.' && (p[1] == '/' || p[1] == '\\'))
		return p + 2;
	return p;
}

/*
==============================================================================

fd table

==============================================================================
*/

static FIL *fil_for (int fd)
{
	int		slot = fd - Q2_FD_BASE;

	if (slot < 0 || slot >= Q2_MAXFDS || !used[slot])
		return NULL;
	return &fils[slot];
}

static BYTE fatfs_mode (int oflag)
{
	BYTE	m;

	switch (oflag & O_ACCMODE)
	{
	case O_WRONLY:	m = FA_WRITE; break;
	case O_RDWR:	m = FA_READ | FA_WRITE; break;
	default:		m = FA_READ; break;
	}
	if (oflag & O_CREAT)
	{
		if (oflag & O_EXCL)
			m |= FA_CREATE_NEW;
		else if (oflag & O_TRUNC)
			m |= FA_CREATE_ALWAYS;
		else
			m |= FA_OPEN_ALWAYS;
	}
	else if (oflag & O_TRUNC)
		m |= FA_CREATE_ALWAYS;
	else
		m |= FA_OPEN_EXISTING;
	if (oflag & O_APPEND)
		m |= FA_OPEN_APPEND;
	return m;
}

// Attaches a fast-seek cluster map to a large file opened read-only.
static void attach_linkmap (FIL *f, const char *path)
{
	int		i, freeslot = -1;
	DWORD	*t;

	if (f_size (f) < FASTSEEK_MIN || strlen (path) >= sizeof(linkmaps[0].path))
		return;
	for (i = 0; i < FASTSEEK_FILES; i++)
	{
		if (linkmaps[i].table && !strcasecmp (linkmaps[i].path, path))
		{
			f->cltbl = linkmaps[i].table;
			return;
		}
		if (!linkmaps[i].table && freeslot < 0)
			freeslot = i;
	}
	if (freeslot < 0)
		return;

	// the table needs 2 DWORDs per fragment plus one; FatFs reports the size needed
	t = malloc (16 * sizeof(DWORD));
	if (!t)
		return;
	t[0] = 16;
	f->cltbl = t;
	if (f_lseek (f, CREATE_LINKMAP) == FR_NOT_ENOUGH_CORE)
	{
		DWORD	need = t[0];
		DWORD	*bigger = realloc (t, need * sizeof(DWORD));

		if (!bigger)
		{
			free (t);
			f->cltbl = NULL;
			return;
		}
		t = bigger;
		t[0] = need;
		f->cltbl = t;
		if (f_lseek (f, CREATE_LINKMAP) != FR_OK)
		{
			free (t);
			f->cltbl = NULL;
			return;
		}
	}
	f_lseek (f, 0);
	strcpy (linkmaps[freeslot].path, path);
	linkmaps[freeslot].table = t;
}

static int q2_open (const char *path, int oflag)
{
	int		slot = -1, i;

	for (i = 0; i < Q2_MAXFDS; i++)
		if (!used[i])
		{
			slot = i;
			break;
		}
	if (slot < 0)
	{
		errno = EMFILE;
		return -1;
	}
	path = strip_dotslash (path);
	if (f_open (&fils[slot], path, fatfs_mode (oflag)) != FR_OK)
	{
		errno = ENOENT;
		return -1;
	}
	if ((oflag & O_ACCMODE) == O_RDONLY)
		attach_linkmap (&fils[slot], path);
	used[slot] = 1;
	return slot + Q2_FD_BASE;
}

static int q2_close (int fd)
{
	FIL		*f = fil_for (fd);
	FRESULT	fr;

	if (!f)
		return -1;
	fr = f_close (f);
	used[fd - Q2_FD_BASE] = 0;
	return fr == FR_OK ? 0 : -1;
}

static _ssize_t q2_read (int fd, void *buf, size_t nbyte)
{
	FIL		*f = fil_for (fd);
	UINT	br = 0;

	if (!f || f_read (f, buf, (UINT)nbyte, &br) != FR_OK)
		return -1;
	return (_ssize_t)br;
}

static _ssize_t q2_write (int fd, const void *buf, size_t nbyte)
{
	FIL		*f = fil_for (fd);
	UINT	bw = 0;

	if (!f || f_write (f, buf, (UINT)nbyte, &bw) != FR_OK)
		return -1;
	return (_ssize_t)bw;	// short when the card is full
}

static off_t q2_lseek (int fd, off_t offset, int whence)
{
	FIL		*f = fil_for (fd);
	FSIZE_t	pos;

	if (!f)
		return -1;
	switch (whence)
	{
	case SEEK_SET:	pos = (FSIZE_t)offset; break;
	case SEEK_CUR:	pos = f_tell (f) + offset; break;
	case SEEK_END:	pos = f_size (f) + offset; break;
	default:		return -1;
	}
	if (f_lseek (f, pos) != FR_OK)
		return -1;
	return (off_t)pos;
}

static int console_write (const char *buf, int len)
{
	return stdio_put_string (buf, len, false, true);
}

/*
==============================================================================

POSIX names

==============================================================================
*/

int open (const char *path, int oflag, ...)		{ return q2_open (path, oflag); }
int close (int fd)								{ return q2_close (fd); }
_ssize_t read (int fd, void *buf, size_t n)		{ return q2_read (fd, buf, n); }
off_t lseek (int fd, off_t off, int whence)		{ return q2_lseek (fd, off, whence); }

_ssize_t write (int fd, const void *buf, size_t n)
{
	if (fd == 1 || fd == 2)
		return console_write ((const char *)buf, (int)n);
	return q2_write (fd, buf, n);
}

int access (const char *path, int mode)
{
	FILINFO	fno;

	if (f_stat (strip_dotslash (path), &fno) != FR_OK)
	{
		errno = ENOENT;
		return -1;
	}
	if ((mode & W_OK) && (fno.fattrib & AM_RDO))
	{
		errno = EACCES;
		return -1;
	}
	return 0;
}

static int q2_stat (const char *path, struct stat *st)
{
	FILINFO	fno;

	if (!st)
	{
		errno = EFAULT;
		return -1;
	}
	if (f_stat (strip_dotslash (path), &fno) != FR_OK)
	{
		errno = ENOENT;
		return -1;
	}
	memset (st, 0, sizeof(*st));
	st->st_mode = (fno.fattrib & AM_DIR) ? S_IFDIR : S_IFREG;
	st->st_size = (off_t)fno.fsize;
	return 0;
}
int stat (const char *path, struct stat *st)	{ return q2_stat (path, st); }
int _stat (const char *path, struct stat *st)	{ return q2_stat (path, st); }

int mkdir (const char *path, mode_t mode)
{
	FRESULT	fr = f_mkdir (strip_dotslash (path));

	(void)mode;
	if (fr == FR_OK || fr == FR_EXIST)
		return 0;
	errno = (fr == FR_NO_PATH) ? ENOENT : EIO;
	return -1;
}

// savegames remove and replace files (SV_WipeSavegame, SV_CopySaveGame)
int unlink (const char *path)		{ return f_unlink (strip_dotslash (path)) == FR_OK ? 0 : -1; }
int _unlink (const char *path)		{ return unlink (path); }
int remove (const char *path)		{ return unlink (path); }

int rename (const char *from, const char *to)
{
	return f_rename (strip_dotslash (from), strip_dotslash (to)) == FR_OK ? 0 : -1;
}

/*
==============================================================================

newlib syscalls (stdio's FILE * layer)

==============================================================================
*/

int _open (const char *fn, int oflag, ...)		{ return q2_open (fn, oflag); }
int _close (int fd)								{ return q2_close (fd); }
off_t _lseek (int fd, off_t pos, int whence)	{ return q2_lseek (fd, pos, whence); }

int _read (int handle, char *buffer, int length)
{
	if (handle == 0)
		return 0;	// no console input
	return (int)q2_read (handle, buffer, (size_t)length);
}

int _write (int handle, char *buffer, int length)
{
	if (handle == 1 || handle == 2)
		return console_write (buffer, length);
	return (int)q2_write (handle, buffer, (size_t)length);
}

int _fstat (int fd, struct stat *buf)
{
	FIL		*f;

	if (!buf)
		return -1;
	memset (buf, 0, sizeof(*buf));
	if (fd >= 0 && fd <= 2)
	{
		buf->st_mode = S_IFCHR;
		return 0;
	}
	f = fil_for (fd);
	if (!f)
		return -1;
	buf->st_mode = S_IFREG;
	buf->st_size = (off_t)f_size (f);
	return 0;
}

int _isatty (int fd)
{
	// only the console is a tty; a file reported as one would be line buffered
	return (fd >= 0 && fd <= 2) ? 1 : 0;
}

/*
==============================================================================

quakegeneric hooks: directory listing and mkdir

==============================================================================
*/

static DIR		find_dir;
static FILINFO	find_info;
static bool		find_open;
static char		find_base[128], find_path[200];

static const char *find_result (FRESULT fr, int *isdir)
{
	if (fr != FR_OK || !find_info.fname[0])
		return NULL;
	snprintf (find_path, sizeof(find_path), "%s/%s", find_base, find_info.fname);
	*isdir = (find_info.fattrib & AM_DIR) != 0;
	return find_path;
}

const char *QG_FindFirst (const char *pattern, int *isdir)
{
	const char	*slash = strrchr (pattern, '/');
	const char	*pat;

	QG_FindClose ();
	if (slash)
	{
		snprintf (find_base, sizeof(find_base), "%.*s", (int)(slash - pattern), pattern);
		pat = slash + 1;
	}
	else
	{
		strcpy (find_base, ".");
		pat = pattern;
	}
	find_open = true;
	return find_result (f_findfirst (&find_dir, &find_info, strip_dotslash (find_base), pat), isdir);
}

const char *QG_FindNext (int *isdir)
{
	if (!find_open)
		return NULL;
	return find_result (f_findnext (&find_dir, &find_info), isdir);
}

void QG_FindClose (void)
{
	if (find_open)
		f_closedir (&find_dir);
	find_open = false;
}

void QG_Mkdir (const char *path)
{
	mkdir (path, 0777);
}
