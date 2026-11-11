// q2_audio.c -- sound output: the engine's SNDDMA ring, played by core1 to HDMI or
// the headphone jack.
//
// The engine mixes on core0 (S_Update, client/snd_dma.c) into dma.buffer: a ring of
// 16-bit stereo frames at 22050 Hz, painted up to s_mixahead ahead of the play
// position. Core1 plays it: pico_hdmi's core1 loop calls q2_audio_core1_task between
// scanline interrupts, which takes frames at the play position, interpolates them to
// 44100 Hz and hands them to the active sink:
//
//   HDMI   data-island audio in the video signal (HDMI display mode)
//   jack   I2S to the board's DAC: on the Fruit Jam the headphones while they are
//          plugged in, and in DVI display mode its speaker otherwise
//
// In HDMI mode the Fruit Jam's speaker never plays (as in pico-duke3D), and the jack
// gets silence while HDMI is active so the DAC's PLL stays locked and plugging in
// does not pop.
//
// A board whose I2S DAC has no headphone detect (Q2_AUDIO_I2S_DRIVER other than
// TLV320, e.g. the PCM5100A of HW_CONFIG 2) plays to both sinks at once in HDMI
// mode, as pico-duke3D does there: each sample goes to HDMI and to I2S, and the sink
// with less room decides how much is taken, so the slower of the two clocks paces
// the play position.
//
// DVI display mode carries no audio, and the HSTX driver does not take data islands
// from the queue then, so the HDMI sink is never used in it: everything goes to I2S
// (apply_route). A board without a working DAC is silent in DVI mode.
//
// Play stops at the last painted frame instead of wrapping into stale audio: when
// core0 stalls (a slow frame, asset caching) the sink gets silence, and the play
// position never runs more than s_mixahead past what the engine has seen, so it
// never misses a ring wrap (GetSoundtime counts wraps).
//
// Core1 may run from flash here (the I2S driver's level query), so it stops pumping
// while flash is written (q2_audio_hold, from q2_flash.c), when XIP is off.

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hardware/sync.h"

#include "audio_i2s.h"
#include "tlv320dac3100.h"
#include "hstx_packet.h"
#include "hstx_data_island_queue.h"
#include "video_output.h"

#include "client/client.h"
#include "client/snd_loc.h"

#include "q2_audio.h"

extern int	soundtime;			// client/snd_dma.c

#define MIX_RATE		22050
#define OUT_RATE		(MIX_RATE * 2)
#define RING_FRAMES		8192		// 371 ms: s_mixahead plus the longest normal frame
#define I2S_DMA_CHAN	6			// channels 4+ interrupt on DMA_IRQ_1; HSTX owns DMA_IRQ_0

// How much audio the sinks are kept filled with
#define DI_TARGET		192			// data islands (4 samples each, 17 ms) of the 256-island HDMI queue
#define I2S_TARGET		2048		// samples (46 ms) of the I2S ring (I2S_AUDIO_RING_SIZE 4096)

// The board's I2S DAC (board header); TLV320 is the Fruit Jam's codec with its jack
#ifndef Q2_AUDIO_I2S_DRIVER
#define Q2_AUDIO_I2S_DRIVER	PICO_AUDIO_I2S_DRIVER_TLV320
#endif
#define HAS_JACK_DETECT	(Q2_AUDIO_I2S_DRIVER == PICO_AUDIO_I2S_DRIVER_TLV320)

typedef enum { SINK_HDMI, SINK_JACK, SINK_BOTH } sink_t;

static uint32_t			ring[RING_FRAMES];		// L in bits 0-15, R in bits 16-31
static volatile uint32_t	play_pos;				// frames played since SNDDMA_Init
static volatile uint32_t	painted_end;			// play_pos value up to which the ring is painted
static uint32_t			seen_pos;				// play_pos as of the last SNDDMA_GetDMAPos
static volatile sink_t	sink = SINK_HDMI;
static bool				i2s_ok;
static bool				hp_in;			// headphones plugged in (TLV320 jack detect)
static bool				route_dirty = true;	// sink to be decided again (core0)
static int16_t			prev_l, prev_r;

// core0 <-> core1 handshake around flash writes
static volatile bool	hold;
static volatile bool	pumping;

// HDMI: 4 samples per data island
static audio_sample_t	di_acc[4];
static int				di_count;
static int				di_frame;

static void __not_in_flash_func(push_hdmi) (int16_t l, int16_t r)
{
	hstx_packet_t		packet;
	hstx_data_island_t	island;

	di_acc[di_count].left = l;
	di_acc[di_count].right = r;
	if (++di_count < 4)
		return;
	di_count = 0;
	di_frame = hstx_packet_set_audio_samples_cs (&packet, di_acc, 4, di_frame);
	hstx_encode_data_island (&island, &packet, false, true);
	hstx_di_queue_push (&island);
}

static inline void __not_in_flash_func(push_jack) (int16_t l, int16_t r)
{
	audio_i2s_enqueue_sample (((uint32_t)(uint16_t)l << 16) | (uint16_t)r);
}

static inline void __not_in_flash_func(push) (sink_t s, int16_t l, int16_t r)
{
	if (s != SINK_JACK)
		push_hdmi (l, r);
	if (s != SINK_HDMI)
		push_jack (l, r);
}

// Output samples (44.1 kHz) the active sink can take before reaching its target;
// for both sinks, what the fuller one can take.
static int __not_in_flash_func(sink_room) (sink_t s, int *i2s_free)
{
	int		jack = 0, hdmi = 0;

	*i2s_free = i2s_ok ? audio_i2s_get_freebuffer_size () : 0;
	if (s != SINK_HDMI)
	{
		int	queued = I2S_AUDIO_RING_SIZE - 1 - *i2s_free;
		jack = queued < I2S_TARGET ? I2S_TARGET - queued : 0;
	}
	if (s != SINK_JACK)
	{
		int	level = (int)hstx_di_queue_get_level ();
		hdmi = level < DI_TARGET ? (DI_TARGET - level) * 4 : 0;
	}
	if (s == SINK_JACK)
		return jack;
	if (s == SINK_HDMI)
		return hdmi;
	return jack < hdmi ? jack : hdmi;
}

static void __not_in_flash_func(pump) (void)
{
	sink_t		s = sink;
	int			i2s_free;
	int			room = sink_room (s, &i2s_free);
	uint32_t	pos = play_pos;
	uint32_t	end = painted_end;

	while (room >= 2)
	{
		int16_t		l, r, ml, mr;
		uint32_t	f;

		if ((int32_t)(end - pos) <= 0)
		{
			// Nothing painted: silence, and the play position waits. HDMI alone
			// fills its own gaps; with both sinks each gets the same silence, so
			// their levels stay together.
			if (s != SINK_HDMI)
				while (room-- > 0)
					push (s, 0, 0);
			prev_l = prev_r = 0;
			break;
		}
		f = ring[pos & (RING_FRAMES - 1)];
		l = (int16_t)(f & 0xffff);
		r = (int16_t)(f >> 16);
		ml = (int16_t)((prev_l + l) >> 1);
		mr = (int16_t)((prev_r + r) >> 1);
		prev_l = l;
		prev_r = r;
		push (s, ml, mr);
		push (s, l, r);
		pos++;
		room -= 2;
	}
	play_pos = pos;

	// keep the jack's clock running on silence while HDMI plays
	if (s == SINK_HDMI && i2s_ok)
	{
		int	queued = I2S_AUDIO_RING_SIZE - 1 - i2s_free;

		for (; queued < I2S_TARGET; queued++)
			push_jack (0, 0);
	}
}

static void __not_in_flash_func(q2_audio_core1_task) (void)
{
	pumping = true;
	__dmb ();
	if (!hold)
		pump ();
	__dmb ();
	pumping = false;
}

void q2_audio_hold (void)
{
	hold = true;
	__dmb ();
	while (pumping)
		tight_loop_contents ();
}

void q2_audio_release (void)
{
	__dmb ();
	hold = false;
}

// Picks the sink for the display mode and the jack, on core0 (codec I2C). Core1
// only reads `sink`.
static void apply_route (void)
{
	bool	dvi = video_output_get_dvi_mode ();
	sink_t	s;

	route_dirty = false;
	if (!i2s_ok)
		s = SINK_HDMI;
	else if (HAS_JACK_DETECT)
	{
		s = (dvi || hp_in) ? SINK_JACK : SINK_HDMI;
		// HDMI: the monitor plays, the speaker never does. DVI: the speaker plays
		// unless headphones are plugged in (the codec mutes it for them itself).
		audio_i2s_muteInternalSpeaker (!dvi || hp_in);
	}
	else
		s = dvi ? SINK_JACK : SINK_BOTH;
	if (s != sink)
	{
		sink = s;
		printf ("q2_audio: %s\n", s == SINK_HDMI ? "HDMI" : s == SINK_BOTH ? "HDMI and I2S"
			: !HAS_JACK_DETECT ? "I2S" : hp_in ? "headphones" : "speaker");
	}
}

void q2_audio_display_changed (void)
{
	route_dirty = true;
}

/*
==============================================================================

SNDDMA (client/snd_loc.h)

==============================================================================
*/

qboolean SNDDMA_Init (void)
{
	static bool	hw_up;

	q2_audio_hold ();				// snd_restart: the pump may still be registered
	memset (ring, 0, sizeof(ring));
	play_pos = painted_end = seen_pos = 0;
	prev_l = prev_r = 0;
	q2_audio_release ();

	dma.channels = 2;
	dma.samplebits = 16;
	dma.speed = MIX_RATE;
	dma.samples = RING_FRAMES * 2;
	dma.submission_chunk = 1;
	dma.samplepos = 0;
	dma.buffer = (byte *)ring;

	// The DAC and I2S are set up once; snd_restart only restarts the ring.
	if (!hw_up)
	{
		hw_up = true;
		dma_channel_claim (I2S_DMA_CHAN);
		i2s_ok = audio_i2s_setup (Q2_AUDIO_I2S_DRIVER, OUT_RATE, I2S_DMA_CHAN) != NULL;
		if (!i2s_ok)
			printf ("q2_audio: no I2S, HDMI only\n");
		else if (HAS_JACK_DETECT)
		{
			audio_i2s_muteInternalSpeaker (true);
			audio_i2s_setVolume (14);
			// a headset plugged in at boot arrives as the first poll's CONNECT
		}
		route_dirty = true;
	}
	if (route_dirty)
		apply_route ();

	video_output_set_background_task (q2_audio_core1_task);
	printf ("q2_audio: %d Hz mix, %d Hz out\n", MIX_RATE, OUT_RATE);
	return true;
}

int SNDDMA_GetDMAPos (void)
{
	seen_pos = play_pos;
	dma.samplepos = (int)((seen_pos * 2) & (uint32_t)(dma.samples - 1));
	return dma.samplepos;
}

void SNDDMA_Shutdown (void)
{
	video_output_set_background_task (NULL);
	q2_audio_hold ();
	q2_audio_release ();
}

void SNDDMA_BeginPainting (void)
{
}

// After each S_Update: publish how far the ring is painted, and follow the jack.
void SNDDMA_Submit (void)
{
	enum headphone_toggle_t	ev;

	// The engine's soundtime is seen_pos in its own count (which it resets now and
	// then), so publish the painted end relative to it.
	int		ahead = paintedtime - soundtime;

	__dmb ();					// the frames before the new end are in the ring
	painted_end = seen_pos + (uint32_t)(ahead > 0 ? ahead : 0);

	if (i2s_ok && HAS_JACK_DETECT)
	{
		ev = tlv320_poll_headphone ();
		if (ev == HP_TOGGLE_CONNECT || ev == HP_TOGGLE_DISCONNECT)
		{
			hp_in = ev == HP_TOGGLE_CONNECT;
			route_dirty = true;
		}
	}
	if (route_dirty)
		apply_route ();
}
