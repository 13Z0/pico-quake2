// q_mem.h -- the engine heap.
//
// On the device the heap is the part of PSRAM not taken by the engine's static data;
// on the host harness it is an arena of the same size, or the system allocator when
// Qmem_Init is given no memory (so AddressSanitizer can see overflows).
// Every malloc/calloc/realloc/free/strdup in the engine is redirected here by
// q2_alloc_redirect.h, which the build force-includes into the engine sources.

#ifndef Q_MEM_H
#define Q_MEM_H

#include <stddef.h>

typedef struct
{
	size_t	size;			// heap size in bytes (0 = system allocator)
	size_t	used;			// bytes in live blocks
	size_t	peak;			// high-water mark of used since the last reset
	size_t	largest_free;	// largest single block that can still be allocated
	unsigned	live_blocks;
} qmem_stats_t;

void	Qmem_Init (void *base, size_t size);
void	Qmem_GetStats (qmem_stats_t *st);
void	Qmem_ResetPeak (void);
size_t	Qmem_LargestFree (void);
int		Qmem_ShrinksInPlace (void);
void	Qmem_DebugDumpLive (void);	// host harness: live blocks by caller (needs Q2_MEMLIVE)	// realloc to a smaller size never moves the block

void	*q2_malloc (size_t size);
void	*q2_calloc (size_t count, size_t size);
void	*q2_realloc (void *ptr, size_t size);
void	q2_free (void *ptr);
char	*q2_strdup (const char *s);

#endif
