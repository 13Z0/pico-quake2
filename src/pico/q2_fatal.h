// q2_fatal.h -- fatal errors on the HDMI output.

#ifndef Q2_FATAL_H
#define Q2_FATAL_H

// Shows the message under the boot console's log and stops. The conditions that
// get here (no SD card, no PSRAM, no game data, an engine error) do not go away on
// a reset, so the screen stays up for the user to read.
void	q2_fatal (const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));
// Same, for an error whose text was already printed (Sys_Error).
void	q2_fatal_halt (void) __attribute__((noreturn));

#endif
