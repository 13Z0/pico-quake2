// q2_psram.c -- PSRAM bring-up.
//
// cmake/psram_linker.cmake puts libq2.a's .bss in .psram_bss and its .data in
// .psram_data at the PSRAM XIP window (0x11000000). crt0 handles neither: after
// SetupPsram() maps the window, .psram_bss is zeroed and .psram_data is copied from
// its load address in flash. What remains of the chip above them is the engine heap.
//
// Runs before the display is up (SetupPsram drives the QMI in direct mode, which
// stalls XIP, so core1's scanout must not be running yet). Failures are recorded
// and reported by main() once the screen exists.

#include <stdio.h>
#include <string.h>

#include "SetupPsram.h"

#include "q2_psram.h"

extern char __psram_data_start[], __psram_data_end[], __psram_data_load[];
extern char __psram_bss_start[], __psram_bss_end[];
extern char __psram_heap_start[], __psram_heap_end[];

#define Q2_PSRAM_REQUIRED	(8 * 1024 * 1024)

static char			error_buf[128];
static const char	*error;
static char			*heap_end;

const char *q2_psram_error (void)
{
	return error;
}

void q2_psram_init (void)
{
	int32_t	size = SetupPsram (PSRAM_CS_PIN);
	size_t	data_len = (size_t)(__psram_data_end - __psram_data_start);
	size_t	bss_len = (size_t)(__psram_bss_end - __psram_bss_start);

	if (size <= 0)
	{
		snprintf (error_buf, sizeof(error_buf),
			"No PSRAM found on GPIO %d.\nQuake II needs 8 MB of PSRAM.", PSRAM_CS_PIN);
		error = error_buf;
		return;
	}
	printf ("q2_psram: %ld KB on GPIO %d\n", (long)(size / 1024), PSRAM_CS_PIN);
	if (size < Q2_PSRAM_REQUIRED)
	{
		snprintf (error_buf, sizeof(error_buf),
			"PSRAM too small: %ld KB.\nQuake II needs 8 MB of PSRAM.", (long)(size / 1024));
		error = error_buf;
		return;
	}

	memcpy (__psram_data_start, __psram_data_load, data_len);
	memset (__psram_bss_start, 0, bss_len);

	heap_end = (char *)0x11000000 + size;
	if (heap_end > __psram_heap_end)
		heap_end = __psram_heap_end;
	printf ("q2_psram: engine data %u KB + bss %u KB, heap %u KB\n",
		(unsigned)(data_len / 1024), (unsigned)(bss_len / 1024),
		(unsigned)((heap_end - __psram_heap_start) / 1024));
}

void *q2_psram_heap (size_t *size)
{
	if (error || !heap_end)
	{
		*size = 0;
		return NULL;
	}
	*size = (size_t)(heap_end - __psram_heap_start);
	return __psram_heap_start;
}
