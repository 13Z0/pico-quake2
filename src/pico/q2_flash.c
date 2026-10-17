// q2_flash.c -- flash access for the asset cache (qcommon/fcache.c).
//
// The cache region is read through the cached XIP window and written with the SDK's
// flash_range_erase/flash_range_program, which take the QMI out of XIP mode for the
// duration. While they run nothing may execute from or read flash or PSRAM:
//
//   - this code runs from SRAM, and every interrupt on core0 is masked except
//     the USB frame interrupt, whose handler is in SRAM and keeps the attached
//     USB devices from suspending (q2_usb_flash_begin);
//   - core1 runs the HSTX scanout, whose code and data are in SRAM, and the audio
//     pump, which is held off for the duration (q2_audio_hold);
//   - the bootrom resets QMI M0 (flash) timing, which is restored afterwards (the
//     SDK saves and restores the CS1/PSRAM setup itself, and cleans the XIP cache
//     first so no PSRAM write-back is lost);
//   - the data to program is copied into an SRAM buffer first, because the source
//     may be in PSRAM, which is unreachable while the QMI is in direct mode.
//
// Erases are split into 64 KB steps so interrupts are not held off for long.

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/structs/qmi.h"

#include "qcommon/fcache.h"
#include "q2_flash.h"
#include "q2_audio.h"
#include "q2_input.h"

#define XIP_BASE_ADDR	0x10000000u
#define ERASE_STEP		(64 * 1024)
#define PROGRAM_STEP	4096

// Q2_FLASH_DIAG=1: log every erase and program with its duration on the UART
#ifndef Q2_FLASH_DIAG
#define Q2_FLASH_DIAG	0
#endif

static fc_flash_t	cache_flash;
static uint8_t		__scratch_x("q2_flash_bounce") bounce[PROGRAM_STEP] __attribute__((aligned(4)));	// 4 KB SCRATCH_X bank

static uint32_t flash_offset (unsigned offset)
{
	return (Q2_FCACHE_BASE - XIP_BASE_ADDR) + offset;
}

static void __no_inline_not_in_flash_func(erase_safe) (uint32_t ofs, uint32_t count)
{
	q2_audio_hold ();
	q2_irq_state_t	irq = q2_usb_flash_begin ();
	uint32_t		timing = qmi_hw->m[0].timing, rfmt = qmi_hw->m[0].rfmt, rcmd = qmi_hw->m[0].rcmd;

	flash_range_erase (ofs, count);
	qmi_hw->m[0].rfmt = rfmt;
	qmi_hw->m[0].rcmd = rcmd;
	qmi_hw->m[0].timing = timing;
	__compiler_memory_barrier ();
	q2_usb_flash_end (irq);
	q2_audio_release ();
}

static void __no_inline_not_in_flash_func(program_safe) (uint32_t ofs, const uint8_t *data, uint32_t count)
{
	q2_audio_hold ();
	q2_irq_state_t	irq = q2_usb_flash_begin ();
	uint32_t		timing = qmi_hw->m[0].timing, rfmt = qmi_hw->m[0].rfmt, rcmd = qmi_hw->m[0].rcmd;

	flash_range_program (ofs, data, count);
	qmi_hw->m[0].rfmt = rfmt;
	qmi_hw->m[0].rcmd = rcmd;
	qmi_hw->m[0].timing = timing;
	__compiler_memory_barrier ();
	q2_usb_flash_end (irq);
	q2_audio_release ();
}

static void cache_erase (unsigned offset, unsigned len)
{
	while (len)
	{
		// 64 KB at a time where aligned (the bootrom then uses block erase)
		unsigned	step = (offset % ERASE_STEP == 0 && len >= ERASE_STEP) ? ERASE_STEP : FC_SECTOR;

#if Q2_FLASH_DIAG
		printf ("fl: erase %06x %u\n", offset, step);
		uint32_t	t0 = time_us_32 ();
#endif
		erase_safe (flash_offset (offset), step);
#if Q2_FLASH_DIAG
		printf ("fl: ok %lu us\n", (unsigned long)(time_us_32 () - t0));
#endif
		offset += step;
		len -= step;
	}
}

static void cache_program (unsigned offset, const void *data, unsigned len)
{
	const uint8_t	*src = data;

	while (len)
	{
		unsigned	step = len < PROGRAM_STEP ? len : PROGRAM_STEP;

		memcpy (bounce, src, step);
#if Q2_FLASH_DIAG
		printf ("fl: program %06x %u\n", offset, step);
		uint32_t	t0 = time_us_32 ();
#endif
		program_safe (flash_offset (offset), bounce, step);
#if Q2_FLASH_DIAG
		printf ("fl: ok %lu us\n", (unsigned long)(time_us_32 () - t0));
#endif
		offset += step;
		src += step;
		len -= step;
	}
}

void q2_flash_init (void)
{
	cache_flash.base = (unsigned char *)Q2_FCACHE_BASE;
	cache_flash.size = Q2_FCACHE_SIZE;
	cache_flash.erase = cache_erase;
	cache_flash.program = cache_program;
	FC_SetFlash (&cache_flash);
}
