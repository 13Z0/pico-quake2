// q_hunk.c -- model hunks on the engine heap.
//
// The stock code reserved the full maximum (16 MB for a world, 2 MB for an alias
// model) and zeroed all of it. Here Hunk_Begin takes at most the largest free block,
// Hunk_Alloc zeroes only what it hands out, and Hunk_End shrinks the block in place
// (TLSF realloc never moves a shrinking block), so a hunk costs what it holds.
// Only one hunk is open at a time, as in the original.
//
// A "block hunk" (Hunk_BeginBlocks) instead keeps every Hunk_Alloc in its own heap
// block, linked from a small head that serves as the model's extradata. World
// models use it: their data is reached through per-lump pointers, so they never
// need one multi-megabyte contiguous block, which a heap fragmented by the previous
// level may not have.

#include <stdio.h>
#include <ctype.h>

#include "../qcommon/qcommon.h"
#include "q_mem.h"

int		hunkcount;

static byte	*membase;
static int	hunkmaxsize;
static int	cursize;

typedef struct hunkblock_s
{
	struct hunkblock_s	*next;
	int					pad;		// keeps the data 8 bytes past the block start
} hunkblock_t;

typedef struct
{
	hunkblock_t	*blocks;
} hunkhead_t;

static hunkhead_t	*blockhunk;		// the open block hunk, if any

void *Hunk_BeginBlocks (void)
{
	blockhunk = malloc (sizeof(hunkhead_t));
	if (!blockhunk)
		Sys_Error ("Hunk_BeginBlocks: out of memory");
	blockhunk->blocks = NULL;
	cursize = 0;
	return blockhunk;
}

void Hunk_FreeBlocks (void *base)
{
	hunkhead_t	*head = base;
	hunkblock_t	*b, *next;

	if (!head)
		return;
	for (b = head->blocks; b; b = next)
	{
		next = b->next;
		free (b);
	}
	free (head);
	hunkcount--;
}

void *Hunk_Begin (int maxsize)
{
	size_t avail = Qmem_LargestFree ();	// 0 when running on the system allocator

	if (avail && (size_t)maxsize > avail)
		maxsize = (int)avail;

	cursize = 0;
	hunkmaxsize = maxsize;
	membase = malloc (maxsize);
	if (!membase)
		Sys_Error ("Hunk_Begin: failed to reserve %i bytes", maxsize);
	return (void *)membase;
}

void *Hunk_Alloc (int size)
{
	byte	*p;

	// round to cacheline
	size = (size+31)&~31;

	if (blockhunk)
	{
		hunkblock_t	*b = malloc (sizeof(hunkblock_t) + size);

		if (!b)
			Sys_Error ("Hunk_Alloc: out of memory (%i bytes)", size);
		b->next = blockhunk->blocks;
		blockhunk->blocks = b;
		cursize += size;
		memset (b + 1, 0, size);
		return (void *)(b + 1);
	}

	if (cursize + size > hunkmaxsize)
		Sys_Error ("Hunk_Alloc overflow (%i + %i > %i)", cursize, size, hunkmaxsize);

	p = membase + cursize;
	cursize += size;
	memset (p, 0, size);
	return (void *)p;
}

int Hunk_End (void)
{
	byte	*p;

	if (blockhunk)
	{
		blockhunk = NULL;
		hunkcount++;
		return cursize;
	}

	if (Qmem_ShrinksInPlace ())
	{
		p = realloc (membase, cursize ? cursize : 32);
		if (p != membase)
			Sys_Error ("Hunk_End: hunk moved while shrinking");
	}

	hunkcount++;
	return cursize;
}

void Hunk_Free (void *base)
{
	if ( base )
		free (base);

	hunkcount--;
}
