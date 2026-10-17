// q_mem.c -- the engine heap (see q_mem.h). TLSF keeps allocation O(1) and
// fragmentation low; realloc shrinks in place, which Hunk_End relies on.
//
// Not thread safe: the engine allocates from core 0 only.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__linux__)
#include <malloc.h>
#endif

#include "q_mem.h"
#include "tlsf.h"

static tlsf_t	heap;
static size_t	heap_size;
static size_t	used, peak;
static unsigned	live;

void Qmem_Init (void *base, size_t size)
{
	used = peak = 0;
	live = 0;
	heap = NULL;
	heap_size = 0;
	if (base && size)
	{
		heap = tlsf_create_with_pool (base, size);
		heap_size = size;
	}
}

static size_t block_size (void *p)
{
	if (heap)
		return tlsf_block_size (p);
#if defined(__linux__)
	return malloc_usable_size (p);
#else
	return 0;
#endif
}

static void account_alloc (void *p)
{
	used += block_size (p);
	live++;
	if (used > peak)
		peak = used;
}

static void account_free (void *p)
{
	used -= block_size (p);
	live--;
}

static void report_failure (size_t size)
{
	fprintf (stderr, "q_mem: allocation of %u bytes failed: %u KB used of %u KB, "
		"largest free %u KB, %u blocks\n", (unsigned)size, (unsigned)(used / 1024),
		(unsigned)(heap_size / 1024), (unsigned)(Qmem_LargestFree () / 1024), live);
}

#if defined(Q2_HOST)
// Host only. Q2_MEMTRACE=<bytes> logs every allocation of at least that size with
// its callers; Q2_MEMLIVE=1 keeps a table of live blocks and prints them grouped by
// caller when an allocation fails (resolve addresses with addr2line -f -e q2_host).
#define LIVE_SLOTS	(1 << 15)
static struct { void *p; size_t size; void *c[3]; } live_tab[LIVE_SLOTS];
static int memlive = -1;

static unsigned live_hash (void *p)
{
	return ((unsigned)(size_t)p >> 3) * 2654435761u >> 17;
}

static void live_add (void *p, size_t size, void *c0, void *c1, void *c2)
{
	unsigned h = live_hash (p);
	int i;
	for (i = 0; i < LIVE_SLOTS; i++, h = (h + 1) & (LIVE_SLOTS - 1))
		if (!live_tab[h].p)
		{
			live_tab[h].p = p; live_tab[h].size = size;
			live_tab[h].c[0] = c0; live_tab[h].c[1] = c1; live_tab[h].c[2] = c2;
			return;
		}
}

static void live_remove (void *p)
{
	unsigned h = live_hash (p);
	int i;
	for (i = 0; i < LIVE_SLOTS; i++, h = (h + 1) & (LIVE_SLOTS - 1))
		if (live_tab[h].p == p)
		{
			// backward-shift deletion keeps the probe chains intact
			unsigned j = h, k;
			live_tab[h].p = NULL;
			for (;;)
			{
				j = (j + 1) & (LIVE_SLOTS - 1);
				if (!live_tab[j].p)
					return;
				k = live_hash (live_tab[j].p);
				if ((j > h && (k <= h || k > j)) || (j < h && (k <= h && k > j)))
				{
					live_tab[h] = live_tab[j];
					live_tab[j].p = NULL;
					h = j;
				}
			}
		}
}

static void free_walker (void *ptr, size_t size, int is_used, void *user)
{
	if (!is_used && size >= 16384)
		fprintf (stderr, "q_mem:   free block %u KB at +%u KB\n", (unsigned)(size / 1024),
			(unsigned)(((char *)ptr - (char *)user) / 1024));
}

static void live_dump (void)
{
	if (heap)
	{
		fprintf (stderr, "q_mem: free blocks of 16 KB or more:\n");
		tlsf_walk_pool (tlsf_get_pool (heap), free_walker, tlsf_get_pool (heap));
	}
	// aggregate by caller chain (quadratic, but only runs once, on failure)
	static int done[LIVE_SLOTS];
	int i, j;
	const char *e = getenv ("Q2_MEMLIVE_MIN");
	size_t live_min = e ? (size_t)atol (e) : 16384;
	memset (done, 0, sizeof(done));
	fprintf (stderr, "q_mem: live blocks by caller (KB, count, callers):\n");
	for (i = 0; i < LIVE_SLOTS; i++)
	{
		size_t total = 0; int n = 0;
		if (!live_tab[i].p || done[i])
			continue;
		for (j = i; j < LIVE_SLOTS; j++)
			if (live_tab[j].p && !done[j] && !memcmp (live_tab[j].c, live_tab[i].c, sizeof(live_tab[i].c)))
			{
				total += live_tab[j].size; n++; done[j] = 1;
			}
		if (total >= live_min)
			fprintf (stderr, "q_mem:   %6u KB %5d  %p %p %p\n", (unsigned)(total / 1024), n,
				live_tab[i].c[0], live_tab[i].c[1], live_tab[i].c[2]);
	}
}

static long trace_threshold = -1;

static int trace_enabled (void)
{
	if (trace_threshold < 0)
	{
		const char *e = getenv ("Q2_MEMTRACE");
		trace_threshold = e ? atol (e) : 0;
		memlive = getenv ("Q2_MEMLIVE") != NULL;
	}
	return trace_threshold > 0 || memlive > 0;
}

static void trace_alloc (size_t size, void *p, void *c0, void *c1, void *c2)
{
	long threshold = trace_threshold;
	if (threshold > 0 && size >= (size_t)threshold)
		fprintf (stderr, "q_mem: +%u at %p from %p %p %p (used %u KB)\n", (unsigned)size, p, c0, c1, c2, (unsigned)(used / 1024));
	if (memlive && p)
		live_add (p, block_size (p), c0, c1, c2);
	if (memlive && !p)
		live_dump ();
}
// the callers are only looked up when tracing is on (it needs frame pointers)
#define TRACE(sz, p) do { if (trace_enabled ()) trace_alloc ((sz), (p), __builtin_return_address (0), \
	__builtin_return_address (1), __builtin_return_address (2)); } while (0)
#define TRACE_FREE(p) do { if (memlive > 0) live_remove (p); } while (0)
#else
#define TRACE(sz, p)
#define TRACE_FREE(p)
#endif

void *q2_malloc (size_t size)
{
	void *p = heap ? tlsf_malloc (heap, size ? size : 1) : malloc (size ? size : 1);
	if (p)
		account_alloc (p);
	else
		report_failure (size);
	TRACE (size, p);
	return p;
}

void *q2_calloc (size_t count, size_t size)
{
	size_t total = count * size;
	void *p;

	if (size && total / size != count)
		return NULL;
	p = q2_malloc (total);
	if (p)
		memset (p, 0, total);
	return p;
}

void *q2_realloc (void *ptr, size_t size)
{
	void *p;

	if (!ptr)
		return q2_malloc (size);
	if (!size)
	{
		q2_free (ptr);
		return NULL;
	}
	TRACE_FREE (ptr);
	account_free (ptr);
	p = heap ? tlsf_realloc (heap, ptr, size) : realloc (ptr, size);
	account_alloc (p ? p : ptr);	// on failure the old block is still live
	TRACE (size, p ? p : ptr);
	return p;
}

void q2_free (void *ptr)
{
	if (!ptr)
		return;
	TRACE_FREE (ptr);
	account_free (ptr);
	if (heap)
		tlsf_free (heap, ptr);
	else
		free (ptr);
}

char *q2_strdup (const char *s)
{
	size_t n = strlen (s) + 1;
	char *p = q2_malloc (n);
	if (p)
		memcpy (p, s, n);
	return p;
}

static void largest_walker (void *ptr, size_t size, int is_used, void *user)
{
	size_t *largest = user;
	if (!is_used && size > *largest)
		*largest = size;
}

size_t Qmem_LargestFree (void)
{
	size_t largest = 0;

	if (!heap)
		return 0;
	tlsf_walk_pool (tlsf_get_pool (heap), largest_walker, &largest);
	// TLSF rounds a request up to the next size class (up to 1/32 of its size) before
	// searching, so only a slightly smaller request is guaranteed to fit.
	largest -= largest / 16;
	return largest > 64 ? largest - 64 : 0;
}

void Qmem_DebugDumpLive (void)
{
#if defined(Q2_HOST)
	if (memlive > 0)
		live_dump ();
#endif
}

int Qmem_ShrinksInPlace (void)
{
	return heap != NULL;
}

void Qmem_GetStats (qmem_stats_t *st)
{
	st->size = heap_size;
	st->used = used;
	st->peak = peak;
	st->largest_free = Qmem_LargestFree ();
	st->live_blocks = live;
}

void Qmem_ResetPeak (void)
{
	peak = used;
}
