// q2_psram.h -- PSRAM bring-up (see q2_psram.c).

#ifndef Q2_PSRAM_H
#define Q2_PSRAM_H

#include <stddef.h>

void	q2_psram_init (void);
const char	*q2_psram_error (void);		// NULL when PSRAM is usable
void	*q2_psram_heap (size_t *size);	// the engine heap region

#endif
