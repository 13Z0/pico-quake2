// q2_fatal.c -- fatal errors on the HDMI output (see q2_fatal.h).

#include <stdarg.h>
#include <stdio.h>

#include "pico/stdlib.h"

#include "q2_fatal.h"
#include "q2_console.h"
#include "q2_display.h"

static volatile bool	in_fatal;

static void __attribute__((noreturn)) halt_forever (void)
{
	stdio_flush ();
	while (1)
		tight_loop_contents ();
}

static void begin (void)
{
	q2_console_set_title ("FATAL ERROR");
	q2_display_show_console ();
	printf ("\n*** FATAL ERROR ***\n");
}

static void __attribute__((noreturn)) finish (void)
{
#if BUILD_FOR_BOOTLOADER
	printf ("\nPress RESET to return to the boot menu.\n");
#else
	printf ("\nPress RESET or power-cycle the board.\n");
#endif
	halt_forever ();
}

void q2_fatal (const char *fmt, ...)
{
	va_list	ap;

	if (in_fatal)
		halt_forever ();
	in_fatal = true;
	begin ();
	va_start (ap, fmt);
	vprintf (fmt, ap);
	va_end (ap);
	printf ("\n");
	finish ();
}

void q2_fatal_halt (void)
{
	if (in_fatal)
		halt_forever ();
	in_fatal = true;
	begin ();
	finish ();
}
