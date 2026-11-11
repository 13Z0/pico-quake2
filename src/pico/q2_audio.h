// q2_audio.h -- sound output (q2_audio.c): the engine's SNDDMA ring, played by core1.

#ifndef Q2_AUDIO_H
#define Q2_AUDIO_H

// Around flash writes (q2_flash.c): stops core1's pump, which may run from flash,
// and waits until it is out of it.
void q2_audio_hold (void);
void q2_audio_release (void);

// After a switch between HDMI and DVI display mode (q2_display.c): the sound
// follows at the next frame (DVI has no audio on the cable, see q2_audio.c).
void q2_audio_display_changed (void);

#endif
