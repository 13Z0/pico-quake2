// q2_sys.c -- quakegeneric platform hooks that are not files or video.
//
// Sound output is in q2_audio.c, input in q2_input.c.

#include "pico/stdlib.h"

#include "client/client.h"
#include "quakegeneric.h"

// Called many times per frame: a 32-bit timer read and no 64-bit division
// (time_us_64 () / 1000 cost about 1% of the frame).
int QG_Milliseconds (void)
{
	static uint32_t	last_us, ms, rem_us;
	uint32_t		now = time_us_32 ();

	rem_us += now - last_us;	// wraps correctly (unsigned)
	last_us = now;
	ms += rem_us / 1000;
	rem_us %= 1000;
	return (int)ms;
}

// Console commands typed on the UART (for example "timedemo 1" from a PC).
const char *QG_ConsoleInput (void)
{
	static char	line[256];
	static int	len;
	int			c;

	while ((c = getchar_timeout_us (0)) != PICO_ERROR_TIMEOUT)
	{
		if (c == '\r' || c == '\n')
		{
			if (!len)
				continue;
			line[len] = 0;
			len = 0;
			return line;
		}
		if (len < (int)sizeof(line) - 1)
			line[len++] = (char)c;
	}
	return NULL;
}

void QG_CaptureMouse (void)
{
}

void QG_ReleaseMouse (void)
{
}
