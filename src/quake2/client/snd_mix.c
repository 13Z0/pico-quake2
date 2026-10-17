/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// snd_mix.c -- portable code to mix sounds for snd_dma.c

#include "client.h"
#include "snd_loc.h"

// pico-quake2: in SRAM, and halved (8 KB); S_PaintChannels mixes in chunks of it
#define	PAINTBUFFER_SIZE	1024
Q2_SRAM_BSS portable_samplepair_t paintbuffer[PAINTBUFFER_SIZE];
int 	*snd_p, snd_linear_count, snd_vol;
short	*snd_out;




Q2_HOT void S_WriteLinearBlastStereo16 (void)
{
	int		i;
	int		val;

	for (i=0 ; i<snd_linear_count ; i+=2)
	{
		val = snd_p[i]>>8;
		if (val > 0x7fff)
			snd_out[i] = 0x7fff;
		else if (val < (short)0x8000)
			snd_out[i] = (short)0x8000;
		else
			snd_out[i] = val;

		val = snd_p[i+1]>>8;
		if (val > 0x7fff)
			snd_out[i+1] = 0x7fff;
		else if (val < (short)0x8000)
			snd_out[i+1] = (short)0x8000;
		else
			snd_out[i+1] = val;
	}
}


Q2_HOT void S_TransferStereo16 (unsigned long *pbuf, int endtime)
{
	int		lpos;
	int		lpaintedtime;
	
	snd_p = (int *) paintbuffer;
	lpaintedtime = paintedtime;

	while (lpaintedtime < endtime)
	{
	// handle recirculating buffer issues
		lpos = lpaintedtime & ((dma.samples>>1)-1);

		snd_out = (short *) pbuf + (lpos<<1);

		snd_linear_count = (dma.samples>>1) - lpos;
		if (lpaintedtime + snd_linear_count > endtime)
			snd_linear_count = endtime - lpaintedtime;

		snd_linear_count <<= 1;

	// write a linear blast of samples
		S_WriteLinearBlastStereo16 ();

		snd_p += snd_linear_count;
		lpaintedtime += (snd_linear_count>>1);
	}
}

/*
===================
S_TransferPaintBuffer

===================
*/
void S_TransferPaintBuffer(int endtime)
{
	int 	out_idx;
	int 	count;
	int 	out_mask;
	int 	*p;
	int 	step;
	int		val;
	unsigned long *pbuf;

	pbuf = (unsigned long *)dma.buffer;

	if (s_testsound->value)
	{
		int		i;
		int		count;

		// write a fixed sine wave
		count = (endtime - paintedtime);
		for (i=0 ; i<count ; i++)
			paintbuffer[i].left = paintbuffer[i].right = sin((paintedtime+i)*0.1)*20000*256;
	}


	if (dma.samplebits == 16 && dma.channels == 2)
	{	// optimized case
		S_TransferStereo16 (pbuf, endtime);
	}
	else
	{	// general case
		p = (int *) paintbuffer;
		count = (endtime - paintedtime) * dma.channels;
		out_mask = dma.samples - 1; 
		out_idx = paintedtime * dma.channels & out_mask;
		step = 3 - dma.channels;

		if (dma.samplebits == 16)
		{
			short *out = (short *) pbuf;
			while (count--)
			{
				val = *p >> 8;
				p+= step;
				if (val > 0x7fff)
					val = 0x7fff;
				else if (val < (short)0x8000)
					val = (short)0x8000;
				out[out_idx] = val;
				out_idx = (out_idx + 1) & out_mask;
			}
		}
		else if (dma.samplebits == 8)
		{
			unsigned char *out = (unsigned char *) pbuf;
			while (count--)
			{
				val = *p >> 8;
				p+= step;
				if (val > 0x7fff)
					val = 0x7fff;
				else if (val < (short)0x8000)
					val = (short)0x8000;
				out[out_idx] = (val>>8) + 128;
				out_idx = (out_idx + 1) & out_mask;
			}
		}
	}
}


/*
===============================================================================

CHANNEL MIXING

===============================================================================
*/

void S_PaintChannelFrom8 (channel_t *ch, sfxcache_t *sc, int endtime, int offset);
void S_PaintChannelFrom16 (channel_t *ch, sfxcache_t *sc, int endtime, int offset);
void S_PaintChannelFromADPCM (channel_t *ch, sfxcache_t *sc, int endtime, int offset);

Q2_HOT void S_PaintChannels(int endtime)
{
	int 	i;
	int 	end;
	channel_t *ch;
	sfxcache_t	*sc;
	int		ltime, count;
	playsound_t	*ps;

	snd_vol = s_volume->value*256;

//Com_Printf ("%i to %i\n", paintedtime, endtime);
	while (paintedtime < endtime)
	{
	// if paintbuffer is smaller than DMA buffer
		end = endtime;
		if (endtime - paintedtime > PAINTBUFFER_SIZE)
			end = paintedtime + PAINTBUFFER_SIZE;

		// start any playsounds
		while (1)
		{
			ps = s_pendingplays.next;
			if (NULL == ps)
			{
				break;
			}
			if (ps == &s_pendingplays)
				break;	// no more pending sounds
			if (ps->begin <= paintedtime)
			{
				S_IssuePlaysound (ps);
				continue;
			}

			if (ps->begin < end)
				end = ps->begin;		// stop here
			break;
		}

	// clear the paint buffer
		if (s_rawend < paintedtime)
		{
//			Com_Printf ("clear\n");
			memset(paintbuffer, 0, (end - paintedtime) * sizeof(portable_samplepair_t));
		}
		else
		{	// copy from the streaming sound source
			int		s;
			int		stop;

			stop = (end < s_rawend) ? end : s_rawend;

			for (i=paintedtime ; i<stop ; i++)
			{
				s = i&(MAX_RAW_SAMPLES-1);
				paintbuffer[i-paintedtime] = s_rawsamples[s];
			}
//		if (i != end)
//			Com_Printf ("partial stream\n");
//		else
//			Com_Printf ("full stream\n");
			for ( ; i<end ; i++)
			{
				paintbuffer[i-paintedtime].left =
				paintbuffer[i-paintedtime].right = 0;
			}
		}


	// paint in the channels.
		ch = channels;
		for (i=0; i<MAX_CHANNELS ; i++, ch++)
		{
			ltime = paintedtime;
		
			while (ltime < end)
			{
				if (!ch->sfx || (!ch->leftvol && !ch->rightvol) )
					break;

				// max painting is to the end of the buffer
				count = end - ltime;

				// might be stopped by running out of data
				if (ch->end - ltime < count)
					count = ch->end - ltime;
		
				sc = S_LoadSound (ch->sfx);
				if (!sc)
					break;

				if (count > 0 && ch->sfx)
				{	
					if (sc->format == SFX_ADPCM)
						S_PaintChannelFromADPCM(ch, sc, count, ltime - paintedtime);
					else if (sc->width == 1)// FIXME; 8 bit asm is wrong now
						S_PaintChannelFrom8(ch, sc, count,  ltime - paintedtime);
					else
						S_PaintChannelFrom16(ch, sc, count, ltime - paintedtime);
	
					ltime += count;
				}

			// if at end of loop, restart
				if (ltime >= ch->end)
				{
					if (ch->autosound)
					{	// autolooping sounds always go back to start
						ch->pos = 0;
						ch->end = ltime + sc->length;
					}
					else if (sc->loopstart >= 0)
					{
						ch->pos = sc->loopstart;
						ch->end = ltime + sc->length - ch->pos;
					}
					else				
					{	// channel just stopped
						ch->sfx = NULL;
					}
				}
			}
															  
		}

	// transfer out according to DMA format
		S_TransferPaintBuffer(end);
		paintedtime = end;
	}
}

// pico-quake2: the 32 KB snd_scaletable is gone. S_PaintChannelFrom8 multiplies
// instead (one cycle on the Cortex-M33), which also fixes the stock table lookup:
// it indexed the table with leftvol >> 11, always row 0 since volumes are at most
// 255, so every 8-bit sound played silent.
void S_InitScaletable (void)
{
	s_volume->modified = false;
}





Q2_HOT void S_PaintChannelFrom8 (channel_t *ch, sfxcache_t *sc, int count, int offset)
{
	int 	data;
	int		leftvol, rightvol;
	signed char *sfx;
	int		i;
	portable_samplepair_t	*samp;

	if (ch->leftvol > 255)
		ch->leftvol = 255;
	if (ch->rightvol > 255)
		ch->rightvol = 255;

	// same scale as S_PaintChannelFrom16: (sample16 * vol * snd_vol) >> 8,
	// with sample16 = sample8 << 8
	leftvol = ch->leftvol * snd_vol;
	rightvol = ch->rightvol * snd_vol;
	sfx = (signed char *)sc->data + ch->pos;

	samp = &paintbuffer[offset];

	for (i=0 ; i<count ; i++, samp++)
	{
		data = sfx[i];
		samp->left += data * leftvol;
		samp->right += data * rightvol;
	}
	
	ch->pos += count;
}




Q2_HOT void S_PaintChannelFrom16 (channel_t *ch, sfxcache_t *sc, int count, int offset)
{
	int data;
	int left, right;
	int leftvol, rightvol;
	signed short *sfx;
	int	i;
	portable_samplepair_t	*samp;

	leftvol = ch->leftvol*snd_vol;
	rightvol = ch->rightvol*snd_vol;
	sfx = (signed short *)sc->data + ch->pos;

	samp = &paintbuffer[offset];
	for (i=0 ; i<count ; i++, samp++)
	{
		data = sfx[i];
		left = (data * leftvol)>>8;
		right = (data * rightvol)>>8;
		samp->left += left;
		samp->right += right;
	}

	ch->pos += count;
}

/*
==================
S_PaintChannelFromADPCM

pico-quake2: mixes a channel whose sound is stored as IMA ADPCM. The decoder state is
kept in the channel, so consecutive paints continue where the last one stopped;
a restarted or looped channel seeks within a block.
==================
*/
Q2_HOT void S_PaintChannelFromADPCM (channel_t *ch, sfxcache_t *sc, int count, int offset)
{
	short	buf[64];
	int		leftvol, rightvol;
	int		i, n;
	portable_samplepair_t	*samp;

	leftvol = ch->leftvol*snd_vol;
	rightvol = ch->rightvol*snd_vol;

	if (ch->adpcm_sc != sc || ch->adpcm.pos != ch->pos)
	{
		ADPCM_Seek (sc->data, ch->pos, &ch->adpcm);
		ch->adpcm_sc = sc;
	}

	samp = &paintbuffer[offset];
	while (count > 0)
	{
		n = count < 64 ? count : 64;
		ADPCM_Decode (sc->data, &ch->adpcm, buf, n);
		for (i=0 ; i<n ; i++, samp++)
		{
			samp->left += (buf[i] * leftvol) >> 8;
			samp->right += (buf[i] * rightvol) >> 8;
		}
		count -= n;
		ch->pos += n;
	}
}
