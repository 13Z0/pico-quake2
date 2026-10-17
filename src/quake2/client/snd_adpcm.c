// snd_adpcm.c -- pico-quake2: IMA ADPCM storage for cached sounds.
//
// Sounds are kept at 4 bits per sample, in blocks of ADPCM_BLOCK_SAMPLES. Each block
// starts with the decoder state (predictor and step index) at that point, so the
// mixer can start decoding at any block. Blocks only checkpoint the continuous
// encoder state, so decoding across blocks is bit-exact with streaming.

#include "client.h"
#include "snd_loc.h"

static const int Q2_SRAM_DATA ima_index_table[16] =
{
	-1, -1, -1, -1, 2, 4, 6, 8,
	-1, -1, -1, -1, 2, 4, 6, 8
};

static const short Q2_SRAM_DATA ima_step_table[89] =
{
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
	50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
	253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
	1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
	3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487,
	12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

int ADPCM_EncodedSize (int samples)
{
	int		blocks = (samples + ADPCM_BLOCK_SAMPLES - 1) / ADPCM_BLOCK_SAMPLES;
	return blocks * ADPCM_BLOCK_BYTES;
}

static inline int ADPCM_Step (adpcm_state_t *st, int nib)
{
	int		step = ima_step_table[st->index];
	int		delta = step >> 3;

	if (nib & 4)
		delta += step;
	if (nib & 2)
		delta += step >> 1;
	if (nib & 1)
		delta += step >> 2;
	st->predictor += (nib & 8) ? -delta : delta;
	if (st->predictor > 32767)
		st->predictor = 32767;
	else if (st->predictor < -32768)
		st->predictor = -32768;
	st->index += ima_index_table[nib];
	if (st->index < 0)
		st->index = 0;
	else if (st->index > 88)
		st->index = 88;
	return st->predictor;
}

/*
================
ADPCM encoder

Samples go in one at a time; each completed block is handed to emit().
================
*/
void ADPCM_EncodeStart (adpcm_encoder_t *e, int first_sample, void (*emit) (void *ctx, const byte *block, int len), void *ctx)
{
	memset (e, 0, sizeof(*e));
	e->st.predictor = first_sample;
	e->emit = emit;
	e->ctx = ctx;
}

void ADPCM_EncodeSample (adpcm_encoder_t *e, int s)
{
	int		diff, step, nib;

	if (!e->n)
	{	// block checkpoint: the decoder state before its first sample
		e->block[0] = e->st.predictor & 0xff;
		e->block[1] = (e->st.predictor >> 8) & 0xff;
		e->block[2] = e->st.index;
		e->block[3] = 0;
		memset (e->block + 4, 0, ADPCM_BLOCK_BYTES - 4);
	}

	// choose the nibble the decoder will reproduce most closely
	step = ima_step_table[e->st.index];
	diff = s - e->st.predictor;
	nib = 0;
	if (diff < 0)
	{
		nib = 8;
		diff = -diff;
	}
	if (diff >= step)
	{
		nib |= 4;
		diff -= step;
	}
	if (diff >= (step >> 1))
	{
		nib |= 2;
		diff -= step >> 1;
	}
	if (diff >= (step >> 2))
		nib |= 1;
	ADPCM_Step (&e->st, nib);

	e->block[4 + (e->n >> 1)] |= (e->n & 1) ? nib << 4 : nib;
	if (++e->n == ADPCM_BLOCK_SAMPLES)
	{
		e->emit (e->ctx, e->block, ADPCM_BLOCK_BYTES);
		e->n = 0;
	}
}

void ADPCM_EncodeFinish (adpcm_encoder_t *e)
{
	if (e->n)
		e->emit (e->ctx, e->block, ADPCM_BLOCK_BYTES);	// a partial block is stored whole
	e->n = 0;
}

static void ADPCM_EmitToBuffer (void *ctx, const byte *block, int len)
{
	byte	**out = ctx;
	memcpy (*out, block, len);
	*out += len;
}

/*
================
ADPCM_Encode

Encodes outcount samples taken from 8- or 16-bit mono PCM at srcrate, resampled to
outrate (nearest sample, as ResampleSfx does), into out.
================
*/
void ADPCM_Encode (const byte *src, int srcwidth, int srcrate, int outrate, int outcount, byte *out)
{
	adpcm_encoder_t	e;
	int		i, samplefrac = 0, fracstep;
	byte	*p = out;

	fracstep = (int)((float)srcrate / outrate * 256);
	ADPCM_EncodeStart (&e, (srcwidth == 2) ? LittleShort (((short *)src)[0]) : ((int)src[0] - 128) << 8,
		ADPCM_EmitToBuffer, &p);
	for (i = 0; i < outcount; i++)
	{
		int		srcsample = samplefrac >> 8;

		samplefrac += fracstep;
		ADPCM_EncodeSample (&e, (srcwidth == 2) ? LittleShort (((short *)src)[srcsample])
			: ((int)src[srcsample] - 128) << 8);
	}
	ADPCM_EncodeFinish (&e);
}

/*
================
ADPCM_Seek

Sets st to decode from sample pos: loads the checkpoint of pos's block and decodes
up to pos.
================
*/
Q2_HOT void ADPCM_Seek (const byte *data, int pos, adpcm_state_t *st)
{
	const byte	*block = data + (pos / ADPCM_BLOCK_SAMPLES) * ADPCM_BLOCK_BYTES;
	int			k;

	st->predictor = (short)(block[0] | (block[1] << 8));
	st->index = block[2];
	st->pos = pos - pos % ADPCM_BLOCK_SAMPLES;
	for (k = 0; st->pos < pos; k++, st->pos++)
		ADPCM_Step (st, (block[4 + (k >> 1)] >> ((k & 1) * 4)) & 15);
}

/*
================
ADPCM_Decode

Decodes count samples from st->pos into out and advances st.
================
*/
Q2_HOT void ADPCM_Decode (const byte *data, adpcm_state_t *st, short *out, int count)
{
	while (count--)
	{
		int			k = st->pos % ADPCM_BLOCK_SAMPLES;
		const byte	*block = data + (st->pos / ADPCM_BLOCK_SAMPLES) * ADPCM_BLOCK_BYTES;

		if (!k)
		{	// resynchronise with the block checkpoint (identical when streaming)
			st->predictor = (short)(block[0] | (block[1] << 8));
			st->index = block[2];
		}
		*out++ = ADPCM_Step (st, (block[4 + (k >> 1)] >> ((k & 1) * 4)) & 15);
		st->pos++;
	}
}
