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
// snd_mem.c: sound caching

#include "client.h"
#include "snd_loc.h"
#include "../qcommon/fcache.h"
#include <stddef.h>

int			cache_full_cycle;

byte *S_Alloc (int size);

/*
================
ResampleSfx
================
*/
void ResampleSfx (sfx_t *sfx, int inrate, int inwidth, byte *data)
{
	int		outcount;
	int		srcsample;
	float	stepscale;
	int		i;
	int		sample, samplefrac, fracstep;
	sfxcache_t	*sc;
	
	sc = sfx->cache;
	if (!sc)
		return;

	stepscale = (float)inrate / dma.speed;	// this is usually 0.5, 1, or 2

	outcount = sc->length / stepscale;
	sc->length = outcount;
	if (sc->loopstart != -1)
		sc->loopstart = sc->loopstart / stepscale;

	sc->speed = dma.speed;
	if (s_loadas8bit->value)
		sc->width = 1;
	else
		sc->width = inwidth;
	sc->stereo = 0;

// resample / decimate to the current source rate

	if (stepscale == 1 && inwidth == 1 && sc->width == 1)
	{
// fast special case
		for (i=0 ; i<outcount ; i++)
			((signed char *)sc->data)[i]
			= (int)( (unsigned char)(data[i]) - 128);
	}
	else
	{
// general case
		samplefrac = 0;
		fracstep = stepscale*256;
		for (i=0 ; i<outcount ; i++)
		{
			srcsample = samplefrac >> 8;
			samplefrac += fracstep;
			if (inwidth == 2)
				sample = LittleShort ( ((short *)data)[srcsample] );
			else
				sample = (int)( (unsigned char)(data[srcsample]) - 128) << 8;
			if (sc->width == 2)
				((short *)sc->data)[i] = sample;
			else
				((signed char *)sc->data)[i] = sample >> 8;
		}
	}
}

//=============================================================================

/*
==============
GetWavinfoFile

pico-quake2: GetWavinfo for a file read by ranges, so the sample data never has to be
in memory at once. Same rules as GetWavinfo: first "fmt " chunk, loop start from the
first "cue " chunk, loop length from a "mark" in the next LIST chunk after it, first
"data" chunk.
==============
*/
static int WavLong (const byte *p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

static wavinfo_t GetWavinfoFile (char *path, int filelen)
{
	wavinfo_t	info;
	byte		hdr[36];
	int			pos, len, cue_end = -1;
	int			data_pos = -1, data_len = 0;
	qboolean	have_fmt = false, have_cue = false, have_mark = false;

	memset (&info, 0, sizeof(info));
	info.loopstart = -1;
	if (filelen < 12 || FS_LoadFileRange (path, 0, 12, hdr) != 12
		|| strncmp ((char *)hdr, "RIFF", 4) || strncmp ((char *)hdr + 8, "WAVE", 4))
		return info;

	for (pos = 12; pos + 8 <= filelen; pos += 8 + ((len + 1) & ~1))
	{
		if (FS_LoadFileRange (path, pos, 8, hdr) != 8)
			break;
		len = WavLong (hdr + 4);
		if (len < 0)
			break;
		if (!have_fmt && !strncmp ((char *)hdr, "fmt ", 4))
		{
			if (FS_LoadFileRange (path, pos, 24, hdr) != 24 || (hdr[8] | (hdr[9] << 8)) != 1)
				return info;		// Microsoft PCM only
			info.channels = hdr[10] | (hdr[11] << 8);
			info.rate = WavLong (hdr + 12);
			info.width = (hdr[22] | (hdr[23] << 8)) / 8;
			have_fmt = true;
		}
		else if (!have_cue && !strncmp ((char *)hdr, "cue ", 4))
		{
			if (FS_LoadFileRange (path, pos, 36, hdr) == 36)
				info.loopstart = WavLong (hdr + 32);
			have_cue = true;
			cue_end = pos + 8 + ((len + 1) & ~1);
		}
		else if (have_cue && !have_mark && pos >= cue_end && !strncmp ((char *)hdr, "LIST", 4))
		{
			have_mark = true;	// only the first LIST after the cue chunk counts
			if (FS_LoadFileRange (path, pos, 32, hdr) == 32 && !strncmp ((char *)hdr + 28, "mark", 4))
				info.samples = info.loopstart + WavLong (hdr + 24);
		}
		else if (data_pos < 0 && !strncmp ((char *)hdr, "data", 4))
		{
			data_pos = pos + 8;
			data_len = len;
		}
	}
	if (!have_fmt || data_pos < 0 || !info.width)
	{
		info.channels = 0;
		return info;
	}
	if (!info.samples || info.samples > data_len / info.width)
		info.samples = data_len / info.width;
	info.dataofs = data_pos;
	return info;
}

/*
==============
S_StreamSoundToCache

pico-quake2: converts a WAV at the mixer rate to ADPCM while reading it in chunks and
writes the result straight into the flash asset cache, so neither the file nor the
converted sound is held in PSRAM. Returns NULL when that is not possible (no cache,
no room, a rate that needs resampling); the caller then loads it the usual way.
==============
*/
static void S_EmitToCache (void *ctx, const byte *block, int len)
{
	FC_Write (ctx, block, len);
}

static sfxcache_t *S_StreamSoundToCache (char *path)
{
	wavinfo_t		info;
	sfxcache_t		hdr;
	fc_writer_t		*w;
	adpcm_encoder_t	enc;
	byte			*buf;
	int				filelen, total, done, chunk, i;
	sfxcache_t		*sc = NULL;

	filelen = FS_LoadFile (path, NULL);
	if (filelen <= 0)
		return NULL;
	info = GetWavinfoFile (path, filelen);
	if (info.channels != 1 || info.rate != dma.speed || info.samples <= 0)
		return NULL;

	memset (&hdr, 0, sizeof(hdr));
	hdr.length = info.samples;
	hdr.loopstart = info.loopstart;
	hdr.speed = dma.speed;
	hdr.width = 2;
	hdr.format = SFX_ADPCM;
	total = (int)offsetof (sfxcache_t, data) + ADPCM_EncodedSize (info.samples);

	w = Z_Malloc (sizeof(*w));
	if (!FC_WriteBegin (w, path, FC_SOUND, total))
	{
		Z_Free (w);
		return NULL;
	}
	FC_Write (w, &hdr, (int)offsetof (sfxcache_t, data));

	buf = Z_Malloc (8 * 1024);
	for (done = 0; done < info.samples; done += chunk)
	{
		chunk = info.samples - done < (8 * 1024) / info.width ? info.samples - done : (8 * 1024) / info.width;
		if (FS_LoadFileRange (path, info.dataofs + done * info.width, chunk * info.width, buf) != chunk * info.width)
			break;
		for (i = 0; i < chunk; i++)
		{
			int	s = (info.width == 2) ? (short)(buf[2*i] | (buf[2*i+1] << 8)) : ((int)buf[i] - 128) << 8;
			if (!done && !i)
				ADPCM_EncodeStart (&enc, s, S_EmitToCache, w);
			ADPCM_EncodeSample (&enc, s);
		}
	}
	if (done == info.samples)
	{
		ADPCM_EncodeFinish (&enc);
		sc = (sfxcache_t *)FC_WriteEnd (w);
	}
	Z_Free (buf);
	Z_Free (w);
	return sc;
}

/*
==============
S_LoadSound
==============
*/
sfxcache_t *S_LoadSound (sfx_t *s)
{
    char	namebuffer[MAX_QPATH];
	byte	*data;
	wavinfo_t	info;
	int		len;
	float	stepscale;
	sfxcache_t	*sc;
	int		size;
	char	*name;

	if (s->name[0] == '*')
		return NULL;

// see if still in memory
	sc = s->cache;
	if (sc)
		return sc;

//Com_Printf ("S_LoadSound: %x\n", (int)stackbuf);
// load it in
	if (s->truename)
		name = s->truename;
	else
		name = s->name;

	if (name[0] == '#')
		strcpy(namebuffer, &name[1]);
	else
		Com_sprintf (namebuffer, sizeof(namebuffer), "sound/%s", name);

	// pico-quake2: sounds are mixed in place from the flash asset cache. An entry
	// converted for another mixer rate or sample width is not usable.
	sc = (sfxcache_t *)FC_Find (namebuffer, FC_SOUND, NULL);
	if (sc && sc->speed == dma.speed && (s_adpcm->value ? sc->format == SFX_ADPCM
		: (sc->format == SFX_PCM && sc->width == (s_loadas8bit->value ? 1 : 2))))
	{
		s->cache = sc;
		return sc;
	}
	if (s_adpcm->value)
	{
		sc = S_StreamSoundToCache (namebuffer);
		if (sc)
		{
			s->cache = sc;
			return sc;
		}
		if (FC_LevelOverflow ())
			return NULL;	// the client rebuilds the cache and reloads
	}

//	Com_Printf ("loading %s\n",namebuffer);

	size = FS_LoadFile (namebuffer, (void **)&data);

	if (!data)
	{
		Com_DPrintf ("Couldn't load %s\n", namebuffer);
		return NULL;
	}

	info = GetWavinfo (s->name, data, size);
	if (info.channels != 1)
	{
		Com_Printf ("%s is a stereo sample\n",s->name);
		FS_FreeFile (data);
		return NULL;
	}

	stepscale = (float)info.rate / dma.speed;	
	len = info.samples / stepscale;

	// pico-quake2: keep the sound as 4-bit IMA ADPCM (decoded by the mixer)
	if (s_adpcm->value)
	{
		sfxcache_t	*cached;
		int			bytes = ADPCM_EncodedSize (len);

		sc = s->cache = Z_Malloc (offsetof (sfxcache_t, data) + bytes);
		sc->length = len;
		sc->loopstart = info.loopstart == -1 ? -1 : (int)(info.loopstart / stepscale);
		sc->speed = dma.speed;
		sc->width = 2;
		sc->stereo = 0;
		sc->format = SFX_ADPCM;
		ADPCM_Encode (data + info.dataofs, info.width, info.rate, dma.speed, len, sc->data);
		FS_FreeFile (data);

		cached = (sfxcache_t *)FC_Store (namebuffer, FC_SOUND, sc, (int)offsetof (sfxcache_t, data) + bytes);
		if (cached)
		{
			Z_Free (sc);
			sc = s->cache = cached;
		}
		return sc;
	}

	// pico-quake2: size for the stored width (ResampleSfx writes 8-bit samples when
	// s_loadas8bit is set); the stock code used the source width and wasted half.
	len = len * (s_loadas8bit->value ? 1 : info.width) * info.channels;

	sc = s->cache = Z_Malloc (len + sizeof(sfxcache_t));
	if (!sc)
	{
		FS_FreeFile (data);
		return NULL;
	}
	
	sc->length = info.samples;
	sc->loopstart = info.loopstart;
	sc->speed = info.rate;
	sc->width = info.width;
	sc->stereo = info.channels;

	ResampleSfx (s, sc->speed, sc->width, data + info.dataofs);

	FS_FreeFile (data);

	{
		sfxcache_t	*cached;

		sc->format = SFX_PCM;
		cached = (sfxcache_t *)FC_Store (namebuffer, FC_SOUND, sc,
			(int)offsetof (sfxcache_t, data) + sc->length * sc->width);
		if (cached)
		{
			Z_Free (sc);
			sc = s->cache = cached;
		}
	}

	return sc;
}



/*
===============================================================================

WAV loading

===============================================================================
*/


byte	*data_p;
byte 	*iff_end;
byte 	*last_chunk;
byte 	*iff_data;
int 	iff_chunk_len;


short GetLittleShort(void)
{
	short val = 0;
	val = *data_p;
	val = val + (*(data_p+1)<<8);
	data_p += 2;
	return val;
}

int GetLittleLong(void)
{
	int val = 0;
	val = *data_p;
	val = val + (*(data_p+1)<<8);
	val = val + (*(data_p+2)<<16);
	val = val + (*(data_p+3)<<24);
	data_p += 4;
	return val;
}

void FindNextChunk(char *name)
{
	while (1)
	{
		data_p=last_chunk;

		if (data_p >= iff_end)
		{	// didn't find the chunk
			data_p = NULL;
			return;
		}
		
		data_p += 4;
		iff_chunk_len = GetLittleLong();
		if (iff_chunk_len < 0)
		{
			data_p = NULL;
			return;
		}
//		if (iff_chunk_len > 1024*1024)
//			Sys_Error ("FindNextChunk: %i length is past the 1 meg sanity limit", iff_chunk_len);
		data_p -= 8;
		last_chunk = data_p + 8 + ( (iff_chunk_len + 1) & ~1 );
		if (!strncmp(data_p, name, 4))
			return;
	}
}

void FindChunk(char *name)
{
	last_chunk = iff_data;
	FindNextChunk (name);
}


void DumpChunks(void)
{
	char	str[5];
	
	str[4] = 0;
	data_p=iff_data;
	do
	{
		memcpy (str, data_p, 4);
		data_p += 4;
		iff_chunk_len = GetLittleLong();
		Com_Printf ("0x%x : %s (%d)\n", (int)(data_p - 4), str, iff_chunk_len);
		data_p += (iff_chunk_len + 1) & ~1;
	} while (data_p < iff_end);
}

/*
============
GetWavinfo
============
*/
wavinfo_t GetWavinfo (char *name, byte *wav, int wavlength)
{
	wavinfo_t	info;
	int     i;
	int     format;
	int		samples;

	memset (&info, 0, sizeof(info));

	if (!wav)
		return info;
		
	iff_data = wav;
	iff_end = wav + wavlength;

// find "RIFF" chunk
	FindChunk("RIFF");
	if (!(data_p && !strncmp(data_p+8, "WAVE", 4)))
	{
		Com_Printf("Missing RIFF/WAVE chunks\n");
		return info;
	}

// get "fmt " chunk
	iff_data = data_p + 12;
// DumpChunks ();

	FindChunk("fmt ");
	if (!data_p)
	{
		Com_Printf("Missing fmt chunk\n");
		return info;
	}
	data_p += 8;
	format = GetLittleShort();
	if (format != 1)
	{
		Com_Printf("Microsoft PCM format only\n");
		return info;
	}

	info.channels = GetLittleShort();
	info.rate = GetLittleLong();
	data_p += 4+2;
	info.width = GetLittleShort() / 8;

// get cue chunk
	FindChunk("cue ");
	if (data_p)
	{
		data_p += 32;
		info.loopstart = GetLittleLong();
//		Com_Printf("loopstart=%d\n", sfx->loopstart);

	// if the next chunk is a LIST chunk, look for a cue length marker
		FindNextChunk ("LIST");
		if (data_p)
		{
			if (!strncmp (data_p + 28, "mark", 4))
			{	// this is not a proper parse, but it works with cooledit...
				data_p += 24;
				i = GetLittleLong ();	// samples in loop
				info.samples = info.loopstart + i;
//				Com_Printf("looped length: %i\n", i);
			}
		}
	}
	else
		info.loopstart = -1;

// find data chunk
	FindChunk("data");
	if (!data_p)
	{
		Com_Printf("Missing data chunk\n");
		return info;
	}

	data_p += 4;
	samples = GetLittleLong () / info.width;

	if (info.samples)
	{
		if (samples < info.samples)
			Com_Error (ERR_DROP, "Sound %s has a bad loop length", name);
	}
	else
		info.samples = samples;

	info.dataofs = data_p - wav;
	
	return info;
}

