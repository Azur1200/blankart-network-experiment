// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 2025 by Classic Development Team.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file al_sound.c
/// \brief OpenAL interface for sound

#ifdef HAVE_OPENAL

#ifdef HAVE_GME
#ifdef HAVE_ZLIB
#ifndef _MSC_VER
#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif
#endif

#ifndef _LFS64_LARGEFILE
#define _LFS64_LARGEFILE
#endif

#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 0
#endif

#include <zlib.h>
#endif // HAVE_ZLIB
#endif // HAVE_GME

#include <alc.h>
#include <al.h>
#include <alext.h>

#include <sndfile.h>

#ifdef HAVE_GME
#include <gme/gme.h>
#define GME_TREBLE 5.0f
#define GME_BASS 1.0f
#endif // HAVE_GME

#define SAMPLERATE 44100
#define BUFFERSIZE 4096
#define BUFFERCOUNT 8

typedef enum
{
	Int16,
	Float,
	// IMA4,
	// MSADPCM
} sample_format_t;

#ifdef HAVE_OPENMPT
#include "libopenmpt/libopenmpt.h"
#endif

#include "../i_sound.h"
#include "../s_sound.h"
#include "../byteptr.h"
#include "../z_zone.h"
#include "../w_wad.h"

#define INVALID_HANDLE 0

typedef struct
{
	char *data;
	char *cur;
	char *end;
} viofile_t;

UINT8 sound_started = false;

static float music_volume, sfx_volume, internal_volume = 1.0f;

// fading
static boolean is_fading;
static float fading_source;
static float fading_target;
static UINT32 fading_timer;
static UINT32 fading_duration;
static void (*fading_callback)(void);
static boolean fading_nocleanup;

static viofile_t musicfile;
static SNDFILE *musicstream;
static SF_INFO musicinfo;
static ALCdevice *device;
static ALCcontext *context;
static int bufferindex;
static ALuint musicbuffers[BUFFERCOUNT] = { INVALID_HANDLE };
static ALuint music = INVALID_HANDLE;

#ifdef HAVE_GME
static Music_Emu *gme;
static UINT16 current_track;
#endif

#ifdef HAVE_OPENMPT
static int mod_err = OPENMPT_ERROR_OK;
static const char *mod_err_str;
static INT16 current_subsong = -1;
static size_t probesize;
static int result;
#endif

static const char *GetALError(ALenum error)
{
	switch (error)
	{
		case AL_NO_ERROR:
			I_Error("GetALError called when there were no error!");

		case AL_INVALID_NAME:
			return "Bad ID";

		case AL_INVALID_ENUM:
			return "Bad enum value";

		case AL_INVALID_VALUE:
			return "Bad value";

		case AL_INVALID_OPERATION:
			return "Operation not valid";

		case AL_OUT_OF_MEMORY:
			return "Out of memory";

		default:
			return "Unknown error";
	}
}

#if defined (HAVE_GME) && defined (HAVE_ZLIB)
static const char* GetZLibError(int zErr)
{
	switch (zErr)
	{
		case Z_ERRNO:
			return "Z_ERRNO";
		case Z_STREAM_ERROR:
			return "Z_STREAM_ERROR";
		case Z_DATA_ERROR:
			return "Z_DATA_ERROR";
		case Z_MEM_ERROR:
			return "Z_MEM_ERROR";
		case Z_BUF_ERROR:
			return "Z_BUF_ERROR";
		case Z_VERSION_ERROR:
			return "Z_VERSION_ERROR";
		default:
			return "unknown error";
	}
}
#endif

void I_StartupSound(void)
{
	if (sound_started)
		return; // not an error condition

	CONS_Printf("I_StartupSound()...\n");
	device = alcOpenDevice(NULL);
	if (device == NULL)
	{
		CONS_Alert(CONS_ERROR, "Error initializing OpenAL: %s\n", GetALError(alGetError()));
		return;
	}

	// enable the limiter and disable hrtf
	// idk if either is necessary tbh but fuckit
	ALCint attrs[] = {
		ALC_OUTPUT_LIMITER_SOFT, ALC_TRUE,
		ALC_HRTF_SOFT, ALC_FALSE,
		0
	};

	context = alcCreateContext(device, attrs);
	if (context == NULL)
	{
		CONS_Alert(CONS_ERROR, "Error initializing OpenAL: %s\n", GetALError(alGetError()));
		alcCloseDevice(device);
		return;
	}

	alcMakeContextCurrent(context);

	music = INVALID_HANDLE;
	bufferindex = 0;
	for (size_t i = 0; i < BUFFERCOUNT; i++)
		musicbuffers[i] = INVALID_HANDLE;
	music_volume = sfx_volume = 0;

#ifdef HAVE_OPENMPT
	CONS_Printf("libopenmpt version: %s\n", openmpt_get_string("library_version"));
	CONS_Printf("libopenmpt build date: %s\n", openmpt_get_string("build"));
#endif

	sound_started = true;
}

void I_ShutdownSound(void)
{
	if (!sound_started)
		return; // not an error condition
	CONS_Printf("I_ShutdownSound(): shut down\n");
	sound_started = false;

	alcMakeContextCurrent(NULL);
	alcDestroyContext(context);
	alcCloseDevice(device);

#ifdef HAVE_GME
	if (gme)
	{
		gme_delete(gme);
		gme = NULL;
	}
#endif
#ifdef HAVE_OPENMPT
	if (openmpt_mhandle)
	{
		openmpt_module_destroy(openmpt_mhandle);
		openmpt_mhandle = NULL;
	}
	current_subsong = -1;
#endif
	if (musicstream != NULL)
	{
		sf_close(musicstream);
		musicstream = NULL;
	}
}

static boolean I_UpdateMusicVolume(void)
{
	alSourcef(music, AL_GAIN, music_volume*internal_volume*(musicdef_volume/100.f));
	ALenum err = alGetError();
	if (err != AL_NO_ERROR)
	{
		CONS_Alert(CONS_ERROR, "Error setting music volume: %s\n", GetALError(err));
		return false;
	}

	return true;
}

static boolean I_QueueNextSample(boolean unqueue)
{
	static INT16 *stream = NULL;
	sample_format_t sample_format = Int16;
	ALenum format = AL_FORMAT_MONO16;

	// keeping it at PU_CACHE_UNLOCKED is fine since we won't do any Z allocations while we use it
	if (stream == NULL)
		Z_Malloc(sizeof(INT16) * 2 * BUFFERSIZE, PU_CACHE_UNLOCKED, &stream);

	size_t samplerate = SAMPLERATE;
	size_t count;
#ifdef HAVE_OPENMPT
	if (openmpt_mhandle != NULL)
	{
		count = openmpt_module_read_interleaved_stereo(openmpt_mhandle, SAMPLERATE >> 1, BUFFERSIZE, stream) * 2 * sizeof(INT16); // HACK: SAMPLERATE >> 1 cause idk, this makes it play at the correct speed for some reason
	}
	else
#endif
	if (musicstream != NULL)
	{
		// use float format for those, otherwise shit will kill your ears
		// thx WOOF for making me aware of this
		switch ((musicinfo.format & SF_FORMAT_SUBMASK))
		{
			case SF_FORMAT_PCM_24:
			case SF_FORMAT_PCM_32:
			case SF_FORMAT_FLOAT:
			case SF_FORMAT_DOUBLE:
			case SF_FORMAT_VORBIS:
			case SF_FORMAT_OPUS:
			case SF_FORMAT_ALAC_20:
			case SF_FORMAT_ALAC_24:
			case SF_FORMAT_ALAC_32:
#ifdef HAVE_SNDFILE_MPEG
			case SF_FORMAT_MPEG_LAYER_I:
			case SF_FORMAT_MPEG_LAYER_II:
			case SF_FORMAT_MPEG_LAYER_III:
#endif
				// check if we actually even support shit
				// otherwise everything will explode kek
				if (alIsExtensionPresent("AL_EXT_FLOAT32"))
				{
					sample_format = Float;
				}
				break;
			default:
				sample_format = Int16;
				break;
		}

		if (musicinfo.channels == 2) // stereo
		{
			format = sample_format == Float ? AL_FORMAT_STEREO_FLOAT32 : AL_FORMAT_STEREO16;
		}
		else
		{
			format = sample_format == Float ? AL_FORMAT_MONO_FLOAT32 : AL_FORMAT_MONO16;
		}

		if (sample_format == Float)
		{
			count = sf_read_float(musicstream, (float *)stream, BUFFERSIZE) * musicinfo.channels;
			count *= 2; // float is 4 bytes not 2, so just double this shit for now kek
		}
		else
		{
			count = sf_read_short(musicstream, stream, BUFFERSIZE) * musicinfo.channels;
		}

		if (count == 0)
		{
			// TODO: looppoints
			sf_seek(musicstream, 0, SEEK_SET);
		}

		samplerate = musicinfo.samplerate;
	}
	else
	{
		CONS_Alert(CONS_ERROR, "Music is active, but no source is available\n");
		return false;
	}

	ALenum err;
	if (unqueue)
	{
		alSourceUnqueueBuffers(music, 1, &musicbuffers[bufferindex]);
		err = alGetError();
		if (err != AL_NO_ERROR) // not fatal, we can keep going in this case
		{
			CONS_Alert(CONS_ERROR, "Error unqueuing buffer: %s\n", GetALError(err));
			return false;
		}
	}

	alBufferData(musicbuffers[bufferindex], format, stream, count, samplerate);
	err = alGetError();
	if (err != AL_NO_ERROR)
	{
		CONS_Alert(CONS_ERROR, "Error loading data into buffer: %s\n", GetALError(err));
		return false;
	}

	alSourceQueueBuffers(music, 1, &musicbuffers[bufferindex]);
	err = alGetError();
	if (err != AL_NO_ERROR)
	{
		CONS_Alert(CONS_ERROR, "Error queuing up buffer: %s\n", GetALError(err));
		return false;
	}

	bufferindex = (bufferindex + 1) % BUFFERCOUNT;
	return true;
}

void I_UpdateSound(void)
{
	if (music != INVALID_HANDLE)
	{
		ALint processed;
		ALint queued;
		alGetSourcei(music, AL_BUFFERS_PROCESSED, &processed);
		ALenum err = alGetError();
		if (err != AL_NO_ERROR)
		{
			CONS_Alert(CONS_ERROR, "Cannot query number of processed buffers: %s\n", GetALError(err));
			return;
		}

		alGetSourcei(music, AL_BUFFERS_QUEUED, &queued);
		err = alGetError();
		if (err != AL_NO_ERROR)
		{
			CONS_Alert(CONS_ERROR, "Cannot query number of queued buffers: %s\n", GetALError(err));
			return;
		}

		// semi-hack because this is sometimes not called for a longer period of time
		boolean need_restart = processed == BUFFERCOUNT;
		while (processed > 0)
		{
			if (!I_QueueNextSample(true))
				break;
			processed--;
		}
		if (need_restart)
		{
			alSourcePlay(music);
			err = alGetError();
			if (err != AL_NO_ERROR)
				CONS_Alert(CONS_WARNING, "Error restarting sound: %s\n", GetALError(err));
		}
	}

	if (is_fading)
	{
		if (!is_fading ||
			internal_volume == fading_target ||
			fading_duration == 0)
		{
			I_StopFadingSong();
		}
		else if (I_SongPaused()) // don't decrement timer
			return;
		else if ((fading_timer -= 10) <= 0)
		{
			internal_volume = fading_target;
			I_UpdateMusicVolume();
			I_StopFadingSong();
		}
		else
		{
			UINT8 delta = fabsf(fading_target - fading_source);
			fixed_t factor = FixedDiv(fading_duration - fading_timer, fading_duration);
			if (fading_target < fading_source)
				internal_volume = max(min(internal_volume, fading_source - FixedMul(delta, factor)), fading_target);
			else if (fading_target > fading_source)
				internal_volume = min(max(internal_volume, fading_source + FixedMul(delta, factor)), fading_target);
			I_UpdateMusicVolume();
		}
	}
}

static char *ds2chunk(void *stream, size_t *len, size_t *samplerate)
{
	UINT16 ver, freq;
	UINT32 samples, i, newsamples;
	UINT8 *sound;

	SINT8 *s;
	INT16 *d;
	INT16 o;
	fixed_t frac;

	// lump header
	ver = READUINT16(stream); // sound version format?
	if (ver != 3) // It should be 3 if it's a doomsound...
		return NULL; // onos! it's not a doomsound!

	freq = READUINT16(stream);

	// dont divide by 0
	if (freq == 0)
		return NULL;

	*samplerate = (size_t)freq << 1; // yeah dont ask me i have no fucking clue why we gotta double that :chaosleep:

	samples = READUINT32(stream);

	// convert from signed 8bit ???hz to signed 16bit 44100hz.
	switch(freq)
	{
	case 44100:
		if (samples >= UINT32_MAX>>2)
			return NULL; // would wrap, can't store.
		newsamples = samples;
		break;
	case 22050:
		if (samples >= UINT32_MAX>>3)
			return NULL; // would wrap, can't store.
		newsamples = samples<<1;
		break;
	case 11025:
		if (samples >= UINT32_MAX>>4)
			return NULL; // would wrap, can't store.
		newsamples = samples<<2;
		break;
	default:
		frac = (SAMPLERATE << FRACBITS) / (UINT32)freq;
		if (!(frac & 0xFFFF)) // other solid multiples (change if FRACBITS != 16)
			newsamples = samples * (frac >> FRACBITS);
		else // strange and unusual fractional frequency steps, plus anything higher than 44100hz.
			newsamples = FixedMul(FixedDiv(samples, freq), SAMPLERATE) + 1; // add 1 to counter truncation.
		if (newsamples >= UINT32_MAX>>2)
			return NULL; // would and/or did wrap, can't store.
		break;
	}
	sound = Z_Malloc(newsamples<<2, PU_SOUND, NULL); // samples * frequency shift * bytes per sample * channels

	s = (SINT8 *)stream;
	d = (INT16 *)sound;

	i = 0;

	// dont resample shit, let openal handle everything
	while (i++ < samples)
	{
		o = ((INT16)(*s++)+0x80)<<8; // changed signedness and shift up to 16 bits
		*d++ = o; // left channel
		*d++ = o; // right channel
	}

	*len = (UINT8 *)d-sound;
	return sound;
}

static sf_count_t GetVIOFileLen(void *userdata)
{
	viofile_t *file = userdata;
	return (size_t)(file->end - file->data);
}

static sf_count_t ReadVIOFile(void *ptr, sf_count_t count, void *userdata)
{
	viofile_t *file = userdata;
	if (count > file->end - file->cur)
		count = file->end - file->cur;

	memcpy(ptr, file->cur, count);
	file->cur += count;
	return count;
}

static sf_count_t WriteVIOFile(const void *ptr, sf_count_t count, void *userdata)
{
	(void)ptr;
	(void)count;
	(void)userdata;
	return 0; // read-only
}

static sf_count_t SeekVIOFile(sf_count_t offset, int whence, void *userdata)
{
	viofile_t *file = userdata;
	if (whence == SEEK_SET)
		file->cur = file->data + offset;
	else if (whence == SEEK_CUR)
		file->cur += offset;
	else if (whence == SEEK_END)
		file->cur = file->end + offset;
	if (file->cur > file->end)
		file->cur = file->end;
	if (file->cur < file->data)
		file->cur = file->data;
	return file->cur - file->data;
}

static sf_count_t TellVIOFile(void *userdata)
{
	viofile_t *file = userdata;
	return file->cur - file->data;
}

static SF_VIRTUAL_IO virtual_io =
{
	.get_filelen = GetVIOFileLen,
	.seek = SeekVIOFile,
	.read = ReadVIOFile,
	.write = WriteVIOFile,
	.tell = TellVIOFile,
};

static ALuint LoadDataIntoBuffer(void *input, size_t size)
{
	size_t samplerate = SAMPLERATE;
	char *data = ds2chunk(input, &size, &samplerate);
	ALenum format = AL_FORMAT_MONO16;
	sample_format_t sample_format = Int16;

	if (data == NULL)
	{
#ifdef HAVE_GME
		Music_Emu *emu;
		gme_info_t *gme_info;

		// VGZ format
		if (((UINT8 *)input)[0] == 0x1F
			&& ((UINT8 *)input)[1] == 0x8B)
		{
#ifdef HAVE_ZLIB
			UINT8 *inflatedData;
			size_t inflatedLen;
			z_stream stream;
			int zErr; // Somewhere to handle any error messages zlib tosses out

			memset(&stream, 0x00, sizeof (z_stream)); // Init zlib stream
			// Begin the inflation process
			inflatedLen = *(UINT32 *)input + (size-4); // Last 4 bytes are the decompressed size, typically
			inflatedData = (UINT8 *)Z_Malloc(inflatedLen, PU_SOUND, NULL); // Make room for the decompressed data
			stream.total_in = stream.avail_in = size;
			stream.total_out = stream.avail_out = inflatedLen;
			stream.next_in = (UINT8 *)input;
			stream.next_out = inflatedData;

			zErr = inflateInit2(&stream, 32 + MAX_WBITS);
			if (zErr == Z_OK) // We're good to go
			{
				zErr = inflate(&stream, Z_FINISH);
				if (zErr == Z_STREAM_END) {
					// Run GME on new data
					if (!gme_open_data(inflatedData, inflatedLen, &emu, SAMPLERATE))
					{
						short *mem;
						UINT32 len;
						gme_equalizer_t eq = {GME_TREBLE, GME_BASS, 0,0,0,0,0,0,0,0};

						Z_Free(inflatedData); // GME supposedly makes a copy for itself, so we don't need this lying around

						gme_start_track(emu, 0);
						gme_set_equalizer(emu, &eq);
						gme_track_info(emu, &gme_info, 0);

						mem = Z_Malloc(len, PU_SOUND, 0);
						gme_play(emu, len >> 1, mem);
						gme_free_info(gme_info);
						gme_delete(emu);

						data = (char *)mem;
						size = len;
					}
				}
				else
					CONS_Alert(CONS_ERROR,"Encountered %s when running inflate: %s\n", GetZLibError(zErr), stream.msg);
				(void)inflateEnd(&stream);
			}
			else // Hold up, zlib's got a problem
				CONS_Alert(CONS_ERROR,"Encountered %s when running inflateInit: %s\n", GetZLibError(zErr), stream.msg);
			Z_Free(inflatedData); // GME didn't open jack, but don't let that stop us from freeing this up
#else
			return INVALID_HANDLE; // No zlib support
#endif
		}
		// Try to read it as a GME sound
		else if (!gme_open_data(input, size, &emu, SAMPLERATE))
		{
			short *mem;
			gme_equalizer_t eq = {GME_TREBLE, GME_BASS, 0,0,0,0,0,0,0,0};

			gme_start_track(emu, 0);
			gme_set_equalizer(emu, &eq);
			gme_track_info(emu, &gme_info, 0);

			size = (gme_info->play_length * 441 / 10) << 2;
			mem = Z_Malloc(size, PU_SOUND, 0);
			gme_play(emu, size >> 1, mem);
			gme_free_info(gme_info);
			gme_delete(emu);

			data = (char *)mem;
		}
#endif
		if (data == NULL)
		{
			viofile_t file =
			{
				.data = input,
				.cur = input,
				.end = (char *)input + size,
			};
			SF_INFO info =
			{
				.frames = 1,
				.samplerate = samplerate,
				.channels = 1,
				.format = 0,
				.sections = 1,
				.seekable = 1,
			};
			SNDFILE *snd = sf_open_virtual(&virtual_io, SFM_READ, &info, &file);
			if (snd == NULL)
				return INVALID_HANDLE;

			// use float format for those, otherwise shit will kill your ears
			// thx WOOF for making me aware of this
			switch ((info.format & SF_FORMAT_SUBMASK))
			{
				case SF_FORMAT_PCM_24:
				case SF_FORMAT_PCM_32:
				case SF_FORMAT_FLOAT:
				case SF_FORMAT_DOUBLE:
				case SF_FORMAT_VORBIS:
				case SF_FORMAT_OPUS:
				case SF_FORMAT_ALAC_20:
				case SF_FORMAT_ALAC_24:
				case SF_FORMAT_ALAC_32:
#ifdef HAVE_SNDFILE_MPEG
				case SF_FORMAT_MPEG_LAYER_I:
				case SF_FORMAT_MPEG_LAYER_II:
				case SF_FORMAT_MPEG_LAYER_III:
#endif
				// check if we actually even support shit
				// otherwise everything will explode kek
				if (alIsExtensionPresent("AL_EXT_FLOAT32"))
				{
					sample_format = Float;
				}
				break;
				default:
					sample_format = Int16;
					break;
			}

			if (info.channels == 2) // stereo
			{
				format = sample_format == Float ? AL_FORMAT_STEREO_FLOAT32 : AL_FORMAT_STEREO16;
			}
			else
			{
				format = sample_format == Float ? AL_FORMAT_MONO_FLOAT32 : AL_FORMAT_MONO16;
			}

			size = 4096;
			size_t pos = 0;
			data = Z_Malloc(size, PU_SOUND, NULL);

			if (sample_format == Float)
			{
				for (;;)
				{
					sf_count_t count = sf_read_float(snd, (float *)&data[pos], (size - pos) / sizeof(float));
					if (count == 0)
						break;
					pos += count * sizeof(float);
					if (pos >= size)
					{
						size <<= 1;
						data = Z_Realloc(data, size, PU_SOUND, NULL);
					}
				}
			}
			else
			{
				for (;;)
				{
					sf_count_t count = sf_read_short(snd, (short *)&data[pos], (size - pos) / sizeof(short));
					if (count == 0)
						break;
					pos += count * sizeof(short);
					if (pos >= size)
					{
						size <<= 1;
						data = Z_Realloc(data, size, PU_SOUND, NULL);
					}
				}
			}

			size = pos;
			samplerate = info.samplerate;
			sf_close(snd);
		}
	}

	ALuint buffer;
	alGenBuffers(1, &buffer);
	ALuint err = alGetError();
	if (err != AL_NO_ERROR)
	{
		CONS_Alert(CONS_ERROR, "Error generating audio buffer: %s\n", GetALError(err));
		Z_Free(data);
		return INVALID_HANDLE;
	}

	alBufferData(buffer, format, data, size, samplerate);
	Z_Free(data);
	err = alGetError();
	if (err != AL_NO_ERROR)
	{
		CONS_Alert(CONS_ERROR, "Error loading data into buffer: %s\n", GetALError(err));
		alDeleteBuffers(1, &buffer);
		return INVALID_HANDLE;
	}
	return buffer;
}

void *I_GetSfx(sfxinfo_t *sfx)
{
	if (sfx->data != NULL)
		return sfx->data;

	if (sfx->lumpnum == LUMPERROR)
		sfx->lumpnum = S_GetSfxLumpNum(sfx);
	sfx->length = W_LumpLength(sfx->lumpnum);

	void *lump = W_CacheLumpNum(sfx->lumpnum, PU_SOUND);

	ALuint buffer = LoadDataIntoBuffer(lump, sfx->length);
	if (buffer == INVALID_HANDLE)
		return NULL;

	sfx->data = (void *)(uintptr_t)buffer;
	return sfx->data;
}

void I_FreeSfx(sfxinfo_t *sfx)
{
	if (sfx->data)
	{
		ALuint buffer = (uintptr_t)sfx->data;
		alDeleteBuffers(1, &buffer);
	}
	sfx->data = NULL;
	sfx->lumpnum = LUMPERROR;
}

void I_UpdateSoundParams(INT32 handle, UINT8 vol, UINT8 sep, UINT8 pitch)
{
	if (handle == INVALID_HANDLE)
		return;

	ALuint source = handle;
	alSourcef(source, AL_GAIN, (sfx_volume * (float)vol / 255.0f)/4.0f);
	ALenum err = alGetError();
	if (err != AL_NO_ERROR)
		CONS_Alert(CONS_WARNING, "Error setting sound volume: %s\n", GetALError(err));

	alSourcef(source, AL_PITCH, (float)pitch / 128.0f);
	err = alGetError();
	if (err != AL_NO_ERROR)
		CONS_Alert(CONS_WARNING, "Error setting sound pitch: %s\n", GetALError(err));

	(void)sep;
}

INT32 I_StartSound(sfxenum_t id, UINT8 vol, UINT8 sep, UINT8 pitch, UINT8 priority, INT32 channel)
{
	ALuint source;
	alGenSources(1, &source);
	ALuint err = alGetError();
	if (err != AL_NO_ERROR)
	{
		CONS_Alert(CONS_ERROR, "Error generating sound source: %s\n", GetALError(err));
		return INVALID_HANDLE;
	}

	alSourcei(source, AL_BUFFER, (uintptr_t)S_sfx[id].data);
	err = alGetError();
	if (err != AL_NO_ERROR)
	{
		CONS_Alert(CONS_ERROR, "Error assigning buffer to source: %s\n", GetALError(err));
		alDeleteSources(1, &source);
		return INVALID_HANDLE;
	}

	alSourcei(source, AL_SOURCE_RELATIVE, AL_TRUE);
	err = alGetError();
	if (err != AL_NO_ERROR)
	{
		CONS_Alert(CONS_ERROR, "Error setting relative audio sounce: %s\n", GetALError(err));
		alDeleteSources(1, &source);
		return INVALID_HANDLE;
	}

	I_UpdateSoundParams(source, vol, sep, pitch);

	alSourcePlay(source);
	err = alGetError();
	if (err != AL_NO_ERROR)
	{
		CONS_Alert(CONS_ERROR, "Error playing sound: %s\n", GetALError(err));
		alDeleteSources(1, &source);
		return INVALID_HANDLE;
	}

	(void)sep; // panning is now handled by real object positioning instead
	(void)priority; // priority and channel management is handled by SRB2...
	(void)channel; // no longer used
	return source;
}

void I_StopSound(INT32 handle)
{
	if (handle == INVALID_HANDLE)
		return;

	ALuint source = handle;
	alSourceStop(source);
	ALenum err = alGetError();
	if (err != AL_NO_ERROR)
		CONS_Alert(CONS_ERROR, "Error stopping sound: %s\n", GetALError(err));

	alDeleteSources(1, &source);
	err = alGetError();
	if (err != AL_NO_ERROR)
		CONS_Alert(CONS_ERROR, "Error deleting source: %s\n", GetALError(err));
}

boolean I_SoundIsPlaying(INT32 handle)
{
	if (handle == INVALID_HANDLE || !alIsSource((ALuint)handle))
		return false;

	ALint value;
	alGetSourcei((ALuint)handle, AL_SOURCE_STATE, &value);
	ALenum err = alGetError();
	if (err != AL_NO_ERROR)
	{
		CONS_Alert(CONS_ERROR, "Error getting sound state: %s\n", GetALError(err));
		return false;
	}
	return value == AL_PLAYING;
}

void I_SetSfxVolume(int volume)
{
	sfx_volume = (float)volume / 100.f;
}

void I_InitMusic(void)
{
}

void I_ShutdownMusic(void)
{
	I_UnloadSong();
}

musictype_t I_SongType(void)
{
#ifdef HAVE_GME
	if (gme)
		return MU_GME;
#endif
#ifdef HAVE_OPENMPT
	if (openmpt_mhandle)
		return MU_MOD_EX;
#endif
	if (music != INVALID_HANDLE)
		return MU_MP3;
	// TODO: add fluidsynth support
	return MU_NONE;
}

boolean I_SongPlaying(void)
{
	if (music == INVALID_HANDLE)
		return false;

	ALint value;
	alGetSourcei(music, AL_SOURCE_STATE, &value);
	ALenum err = alGetError();
	if (err != AL_NO_ERROR)
	{
		CONS_Alert(CONS_ERROR, "Error getting play state: %s\n", GetALError(err));
		return false;
	}
	return value == AL_PLAYING;
}

boolean I_SongPaused(void)
{
	if (music == INVALID_HANDLE)
		return false;

	ALint value;
	alGetSourcei(music, AL_SOURCE_STATE, &value);
	ALenum err = alGetError();
	if (err != AL_NO_ERROR)
	{
		CONS_Alert(CONS_ERROR, "Error getting pause state: %s\n", GetALError(err));
		return false;
	}
	return value == AL_PAUSED;
}

boolean I_SetSongSpeed(float speed)
{
	if (music == INVALID_HANDLE)
		return false;
	if (speed > 250.0f)
		speed = 250.0f; //limit speed up to 250x

	// FIXME: implement
	return false;
}

UINT32 I_GetSongLength(void)
{
	if (music == INVALID_HANDLE)
		return 0;

#ifdef HAVE_GME
	if (gme)
	{
		gme_info_t *info;
		gme_err_t gme_e = gme_track_info(gme, &info, current_track);

		size_t length;

		if (gme_e != NULL)
		{
			CONS_Alert(CONS_ERROR, "GME error: %s\n", gme_e);
			length = 0;
		}
		else
		{
			// reconstruct info->play_length, from GME source
			// we only want intro + 1 loop, not 2
			length = info->length;
			if (length <= 0)
			{
				length = info->intro_length + info->loop_length; // intro + 1 loop
				if (length <= 0)
					length = 150 * 1000; // 2.5 minutes
			}
		}

		gme_free_info(info);
		return max(length, 0);
	}
#endif
#ifdef HAVE_OPENMPT
	if (openmpt_mhandle)
		return (UINT32)(openmpt_module_get_duration_seconds(openmpt_mhandle) * 1000.);
#endif

	sf_count_t cur = sf_seek(musicstream, 0, SEEK_CUR);
	sf_count_t length = sf_seek(musicstream, 0, SEEK_END);
	sf_seek(musicstream, cur, SEEK_SET);
	return length / 2 / musicinfo.samplerate;
}

boolean I_SetSongLoopPoint(UINT32 looppoint)
{
	(void)looppoint;
	return false; // FIXME: implement
}

UINT32 I_GetSongLoopPoint(void)
{
	return 0; // FIXME: implement
}

boolean I_SetSongPosition(UINT32 position)
{
	if (music == INVALID_HANDLE)
		return false;

	alSourcef(music, AL_SEC_OFFSET, (float)position / 1000.0f);
	ALenum err = alGetError();
	if (err != AL_NO_ERROR)
	{
		CONS_Alert(CONS_ERROR, "Error setting song position: %s\n", GetALError(err));
		return false;
	}

	return true;
}

UINT32 I_GetSongPosition(void)
{
	if (music == INVALID_HANDLE)
		return 0;

	float position;
	alGetSourcef(music, AL_SEC_OFFSET, &position);
	return position * 1000.0f;
}


static boolean InitializeMusicStreaming(void *input, size_t size)
{
	// Not a doom sound? Try something else.
#ifdef HAVE_GME
	// VGZ format

	gme_info_t *gme_info;
	if (((UINT8 *)input)[0] == 0x1F
		&& ((UINT8 *)input)[1] == 0x8B)
	{
#ifdef HAVE_ZLIB
		UINT8 *inflatedData;
		size_t inflatedLen;
		z_stream stream;
		int zErr; // Somewhere to handle any error messages zlib tosses out

		memset(&stream, 0x00, sizeof (z_stream)); // Init zlib stream
		// Begin the inflation process
		inflatedLen = *(UINT32 *)input + (size-4); // Last 4 bytes are the decompressed size, typically
		inflatedData = (UINT8 *)Z_Malloc(inflatedLen, PU_SOUND, NULL); // Make room for the decompressed data
		stream.total_in = stream.avail_in = size;
		stream.total_out = stream.avail_out = inflatedLen;
		stream.next_in = (UINT8 *)input;
		stream.next_out = inflatedData;

		zErr = inflateInit2(&stream, 32 + MAX_WBITS);
		if (zErr == Z_OK) // We're good to go
		{
			zErr = inflate(&stream, Z_FINISH);
			if (zErr == Z_STREAM_END)
			{
				// Run GME on new data
				if (!gme_open_data(inflatedData, inflatedLen, &gme, SAMPLERATE))
				{
					short *mem;
					UINT32 len;
					gme_equalizer_t eq = {GME_TREBLE, GME_BASS, 0,0,0,0,0,0,0,0};

					Z_Free(inflatedData); // GME supposedly makes a copy for itself, so we don't need this lying around

					gme_start_track(gme, 0);
					gme_set_equalizer(gme, &eq);
					gme_track_info(gme, &gme_info, 0);

					mem = Z_Malloc(len, PU_SOUND, 0);
					gme_play(gme, len >> 1, mem);
					gme_free_info(gme_info);
				}
			}
			else
				CONS_Alert(CONS_ERROR,"Encountered %s when running inflate: %s\n", GetZLibError(zErr), stream.msg);
			(void)inflateEnd(&stream);
		}
		else // Hold up, zlib's got a problem
			CONS_Alert(CONS_ERROR,"Encountered %s when running inflateInit: %s\n", GetZLibError(zErr), stream.msg);
		Z_Free(inflatedData); // GME didn't open jack, but don't let that stop us from freeing this up
#else
		return INVALID_HANDLE; // No zlib support
#endif
	}
	// Try to read it as a GME sound
	else if (!gme_open_data(input, size, &gme, SAMPLERATE))
	{
		short *mem;
		UINT32 len;
		gme_equalizer_t eq = {GME_TREBLE, GME_BASS, 0,0,0,0,0,0,0,0};

		gme_start_track(gme, 0);
		gme_set_equalizer(gme, &eq);
		gme_track_info(gme, &gme_info, 0);

		len = (gme_info->play_length * 441 / 10) << 2;
		mem = Z_Malloc(len, PU_SOUND, 0);
		gme_play(gme, len >> 1, mem);
		gme_free_info(gme_info);
	}
#endif

	musicfile.data = input;
	musicfile.cur = input;
	musicfile.end = (char *)input + size;
	musicinfo.frames = 1,
	musicinfo.samplerate = SAMPLERATE,
	musicinfo.channels = 1,
	musicinfo.format = 0,
	musicinfo.sections = 1,
	musicinfo.seekable = 1,
	musicstream = sf_open_virtual(&virtual_io, SFM_READ, &musicinfo, &musicfile);
	if (musicstream != NULL)
		return true;

#ifdef HAVE_OPENMPT
	/*
		If the size of the data to be checked is bigger than the recommended size (> 2048 bytes)
		Let's just set the probe size to the recommended size
		Otherwise let's give it the full data size
	*/

	if (size > openmpt_probe_file_header_get_recommended_size())
		probesize = openmpt_probe_file_header_get_recommended_size();
	else
		probesize = size;

	result = openmpt_probe_file_header(OPENMPT_PROBE_FILE_HEADER_FLAGS_DEFAULT, input, probesize, size, NULL, NULL, NULL, NULL, NULL, NULL);
	if (result != OPENMPT_PROBE_FILE_HEADER_RESULT_SUCCESS)
		return false;

	openmpt_mhandle = openmpt_module_create_from_memory2(input, size, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
	if (openmpt_mhandle)
	{
		current_subsong = 0;
		return true;
	}

	mod_err = openmpt_module_error_get_last(openmpt_mhandle);
	mod_err_str = openmpt_error_string(mod_err);
	CONS_Alert(CONS_ERROR, "openmpt_module_create_from_memory2: %s\n", mod_err_str);
	return false;
#endif
}

boolean I_LoadSong(char *data, size_t len)
{
	/*
	const char *key1 = "LOOP";
	const char *key2 = "POINT=";
	const char *key3 = "MS=";
	const size_t key1len = strlen(key1);
	const size_t key2len = strlen(key2);
	const size_t key3len = strlen(key3);
	char *p = data;
	*/

	if (music != INVALID_HANDLE)
		I_UnloadSong();

	if (!InitializeMusicStreaming(data, len))
		return false;

	alGenSources(1, &music);
	ALuint err = alGetError();
	if (err != AL_NO_ERROR)
	{
		CONS_Alert(CONS_ERROR, "Error generating music source: %s\n", GetALError(err));
		I_UnloadSong();
		return false;
	}

	alGenBuffers(BUFFERCOUNT, musicbuffers);
	err = alGetError();
	if (err != AL_NO_ERROR)
	{
		CONS_Alert(CONS_ERROR, "Error generating music buffers: %s\n", GetALError(err));
		I_UnloadSong();
		return false;
	}

	for (size_t i = 0; i < BUFFERCOUNT; i++)
	{
		if (!I_QueueNextSample(false))
		{
			I_UnloadSong();
			return false;
		}
	}

	err = alGetError();
	if (err != AL_NO_ERROR)
	{
		CONS_Alert(CONS_ERROR, "Error playing music: %s\n", GetALError(err));
		I_UnloadSong();
		return false;
	}

	// Find the OGG loop point.
	/*
	loop_point = 0.0f;
	song_length = 0.0f;

	while ((UINT32)(p - data) < len)
	{
		if (fpclassify(loop_point) == FP_ZERO && !strncmp(p, key1, key1len))
		{
			p += key1len; // skip LOOP
			if (!strncmp(p, key2, key2len)) // is it LOOPPOINT=?
			{
				p += key2len; // skip POINT=
				loop_point = (float)((44.1L+atoi(p)) / 44100.0L); // LOOPPOINT works by sample count.
				// because SDL_Mixer is USELESS and can't even tell us
				// something simple like the frequency of the streaming music,
				// we are unfortunately forced to assume that ALL MUSIC is 44100hz.
				// This means a lot of tracks that are only 22050hz for a reasonable downloadable file size will loop VERY badly.
			}
			else if (!strncmp(p, key3, key3len)) // is it LOOPMS=?
			{
				p += key3len; // skip MS=
				loop_point = (float)(atoi(p) / 1000.0L); // LOOPMS works by real time, as miliseconds.
				// Everything that uses LOOPMS will work perfectly with SDL_Mixer.
			}
		}

		if (fpclassify(loop_point) != FP_ZERO) // Got what we needed
			break;
		else // continue searching
			p++;
	}
	*/
	return true;
}

void I_UnloadSong(void)
{
	I_StopSong();

#ifdef HAVE_GME
	if (gme)
	{
		gme_delete(gme);
		gme = NULL;
	}
#endif
#ifdef HAVE_OPENMPT
	if (openmpt_mhandle)
	{
		openmpt_module_destroy(openmpt_mhandle);
		openmpt_mhandle = NULL;
	}
#endif
	if (musicstream != NULL)
	{
		sf_close(musicstream);
		musicstream = NULL;
	}
	if (music != INVALID_HANDLE)
	{
		alDeleteSources(1, &music);
		alDeleteBuffers(BUFFERCOUNT, musicbuffers);
		for (size_t i = 0; i < BUFFERCOUNT; i++)
			musicbuffers[i] = INVALID_HANDLE;
		music = INVALID_HANDLE;
	}
}

boolean I_PlaySong(boolean looping)
{
	if (music == INVALID_HANDLE)
		return false;

	if (!I_UpdateMusicVolume())
		return false;

#ifdef HAVE_OPENMPT
	if (openmpt_mhandle)
	{
		openmpt_module_select_subsong(openmpt_mhandle, 0);
		openmpt_module_set_render_param(openmpt_mhandle, OPENMPT_MODULE_RENDER_INTERPOLATIONFILTER_LENGTH, cv_modfilter.value);
		if (looping)
			openmpt_module_set_repeat_count(openmpt_mhandle, -1); // Always repeat
		current_subsong = 0;
	}
#endif

	alSourcePlay(music);
	ALenum err = alGetError();
	if (err != AL_NO_ERROR)
	{
		CONS_Alert(CONS_ERROR, "Error playing music: %s\n", GetALError(err));
		return false;
	}
	return true;
}

void I_StopSong(void)
{
	// HACK: See music_loop on why we want fade timing to proceed
	// after end of song
	if (!fading_nocleanup)
		I_StopFadingSong();

#ifdef HAVE_GME
	if (gme)
		current_track = -1;
#endif
#ifdef HAVE_OPENMPT
	if (openmpt_mhandle)
		current_subsong = -1;
#endif
	if (music != INVALID_HANDLE)
		alSourceStop(music);
}

void I_PauseSong(void)
{
	if (music != INVALID_HANDLE)
		alSourcePause(music);
}

void I_ResumeSong(void)
{
	alSourcePlay(music);
}

void I_SetMusicVolume(int volume)
{
	music_volume = (float)volume / 100.f;
	if (music != INVALID_HANDLE)
		I_UpdateMusicVolume();
}

boolean I_SetSongTrack(INT32 track)
{
	(void)track;
	// FIXME: implement
	return false;
}

void I_UpdateSongLagThreshold(void){}
void I_UpdateSongLagConditions(void){}

void I_SetInternalMusicVolume(UINT8 volume)
{
	internal_volume = volume / 100.0f;
	if (music != INVALID_HANDLE)
		I_UpdateMusicVolume();
}

void I_StopFadingSong(void)
{
	is_fading = false;
	fading_source = fading_target = fading_timer = fading_duration = 0;
	// don't unset fading_nocleanup here just yet; fading_callback is cleaned up
	// in var_cleanup()
}

boolean I_FadeSongFromVolume(UINT8 target_volume, UINT8 source_volume, UINT32 ms, void (*callback)(void))
{
	INT16 volume_delta;

	source_volume = min(source_volume, 100);
	volume_delta = (INT16)(target_volume - source_volume);

	I_StopFadingSong();

	if (!ms && volume_delta)
	{
		I_SetInternalMusicVolume(target_volume);
		if (callback)
			(*callback)();
		return true;

	}
	else if (!volume_delta)
	{
		if (callback)
			(*callback)();
		return true;
	}

	// Round MS to nearest 10
	// If n - lower > higher - n, then round up
	ms = (ms - ((ms / 10) * 10) > (((ms / 10) * 10) + 10) - ms) ?
		(((ms / 10) * 10) + 10) // higher
		: ((ms / 10) * 10); // lower

	if (!ms)
		I_SetInternalMusicVolume(target_volume);
	else if (source_volume != target_volume)
	{
		is_fading = true;
		fading_timer = fading_duration = ms;
		fading_source = source_volume / 100.0f;
		fading_target = target_volume / 100.0f;
		fading_callback = callback;

		if (internal_volume != source_volume)
			I_SetInternalMusicVolume(source_volume);
	}

	return is_fading;
}

boolean I_FadeSong(UINT8 target_volume, UINT32 ms, void (*callback)(void))
{
	return I_FadeSongFromVolume(target_volume, internal_volume, ms, callback);
}

boolean I_FadeOutStopSong(UINT32 ms)
{
	return I_FadeSongFromVolume(0, internal_volume, ms, &I_StopSong);
}

boolean I_FadeInPlaySong(UINT32 ms, boolean looping)
{
	if (I_PlaySong(looping))
		return I_FadeSongFromVolume(100, 0, ms, NULL);
	else
		return false;
}

#endif // defined(HAVE_OPENAL)
