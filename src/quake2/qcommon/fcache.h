// fcache.h -- flash asset cache.
//
// Read-only game assets (wall textures, models, skins, pics, sounds) are kept in a
// flash region and used in place through the XIP window, so they cost no PSRAM.
// Assets a level needs that are not cached yet are copied from the paks on SD when
// the level loads; entries no level has used recently are evicted to make room.
//
// Lifetime rule: an entry may be referenced by an in-memory object (image_t,
// model_t, sfx_t) only while its level sequence is the current one, or while it is
// pinned. The registration code drops all non-pic objects when a level begins, so
// evicting older entries can never leave a dangling pointer.

#ifndef FCACHE_H
#define FCACHE_H

typedef enum
{
	FC_RAW = 1,		// the file as stored in the pak (wal, md2, sp2)
	FC_PIC8 = 2,	// decoded 8-bit image: fc_pic8_t header + pixels
	FC_SOUND = 3	// sound in the mixer's cached format
} fc_kind_t;

typedef struct
{
	int		width, height;
	int		transparent;	// any pixel == 255
	int		pad;
	unsigned char	pixels[1];
} fc_pic8_t;

// Platform flash access. Offsets are relative to the start of the region; erase
// ranges are multiples of FC_SECTOR, program ranges multiples of FC_PAGE.
#define FC_SECTOR	4096
#define FC_PAGE		256

typedef struct
{
	unsigned char	*base;		// where the region can be read (XIP window on the device)
	unsigned	size;			// bytes, a multiple of 64 KB
	void	(*erase) (unsigned offset, unsigned len);
	void	(*program) (unsigned offset, const void *data, unsigned len);
} fc_flash_t;

typedef struct
{
	unsigned	entries, used_bytes, free_bytes, capacity;
	unsigned	hits, misses, stores, store_failures, evictions;
	unsigned	bytes_erased, bytes_programmed;
} fc_stats_t;

void	FC_SetFlash (const fc_flash_t *flash);	// before Qcommon_Init; NULL = no cache

// Returns the cached data for name/kind and its length, or NULL.
const void	*FC_Find (const char *name, int kind, int *length);
// Copies len bytes into the cache; returns the cached copy, or NULL when the asset
// cannot be cached (no cache, name too long, no room). Callers then keep it in RAM.
const void	*FC_Store (const char *name, int kind, const void *data, int len);
// Copies a whole file (loose or in a pak) into the cache in small chunks, without
// holding it in RAM. Returns the cached copy and its length, or NULL.
const void	*FC_StoreFile (const char *path, int kind, int *length);

// Streams an entry of known length into the cache in pieces of any size, for assets
// converted on the fly. FC_WriteEnd returns the cached copy, or NULL on failure
// (also when fewer than len bytes were written).
typedef struct
{
	int			slot;
	unsigned	offset, crc;
	int			len, done, carry;
	int			kind;
	char		name[64];
	unsigned char	page[256];
} fc_writer_t;

int		FC_WriteBegin (fc_writer_t *w, const char *name, int kind, int len);
void	FC_Write (fc_writer_t *w, const void *data, int n);
const void	*FC_WriteEnd (fc_writer_t *w);
// Pinned entries are never evicted this boot (for pics, which are never freed).
void	FC_Pin (const char *name, int kind);
// True when p points into the cache region (such memory must not be freed).
int		FC_Owns (const void *p);

void	FC_BeginLevel (const char *mapname);	// starts a new level sequence once per map
// True when an asset of the current level could not be placed (the region is too
// fragmented or too full). Loaders then skip the asset instead of loading it into
// PSRAM, and the client rebuilds: it drops the level's objects, calls FC_EvictAll
// and registers the level again into the emptied region.
int		FC_LevelOverflow (void);
void	FC_EvictAll (void);		// every unpinned entry; only when nothing refers to them
void	FC_Commit (void);						// writes the directory if it changed
void	FC_GetStats (fc_stats_t *st);

#endif
