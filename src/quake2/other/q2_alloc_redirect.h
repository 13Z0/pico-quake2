// q2_alloc_redirect.h -- force-included (-include) into every engine source file.
// Routes the engine's heap calls to the PSRAM heap in q_mem.c. Not used for q_mem.c
// itself, for the platform layer, or for libc, whose own buffers stay in SRAM.

#ifndef Q2_ALLOC_REDIRECT_H
#define Q2_ALLOC_REDIRECT_H

#include <stddef.h>

void	*q2_malloc (size_t size);
void	*q2_calloc (size_t count, size_t size);
void	*q2_realloc (void *ptr, size_t size);
void	q2_free (void *ptr);
char	*q2_strdup (const char *s);

#define malloc	q2_malloc
#define calloc	q2_calloc
#define realloc	q2_realloc
#define free	q2_free
#define strdup	q2_strdup

#endif
