// q2_audio.h -- sound output (q2_audio.c): the engine's SNDDMA ring, played by core1.

#ifndef Q2_AUDIO_H
#define Q2_AUDIO_H

// Around flash writes (q2_flash.c): stops core1's pump, which may run from flash,
// and waits until it is out of it.
void q2_audio_hold (void);
void q2_audio_release (void);

#endif
