// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2025 by Classic Development Team.
// Copyright (C) 2014-2020 by Sonic Team Junior.
// Copyright (C) 2026 Blankart Team.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file al_sound.c
/// \brief OpenAL interface for sound

#ifdef HAVE_OPENAL

#include <tracy/tracy/TracyC.h>

#ifdef HAVE_GME
#ifdef HAVE_ZLIB
#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
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

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"
#include "AL/efx.h"
#include "AL/efx-presets.h"

#include <sndfile.h>

#ifdef HAVE_GME
#include <gme/gme.h>
#define GME_TREBLE 5.0f
#define GME_BASS 1.0f
#endif // HAVE_GME

#define INVALID_HANDLE 0
#define SAMPLERATE 44100
#define BUFFERSIZE 4096
#define BUFFERCOUNT 8

#define FLOATEP 0.00001

// don't set gain too high, avoids clipping with multiple sounds playing
#define VOLUME_DIV 200.f

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
#include "../i_threads.h"
#include "../i_system.h" // I_Sleep
#include "../fastcmp.h"
#include "../r_defs.h"

#include <SDL3/SDL.h>

typedef struct
{
	char *data;
	char *cur;
	char *end;
} viofile_t;

uint8_t sound_started = false;

static float music_volume, sfx_volume, internal_volume = 1.0f;

/* C doesn't allow casting between function and non-function pointer types, so
 * with C99 we need to use a union to reinterpret the pointer type. Pre-C99
 * still needs to use a normal cast and live with the warning (C++ is fine with
 * a regular reinterpret_cast).
 */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define FUNCTION_CAST(T, ptr) (union{void *p; T f;}){ptr}.f
#elif defined(__cplusplus)
#define FUNCTION_CAST(T, ptr) reinterpret_cast<T>(ptr)
#else
#define FUNCTION_CAST(T, ptr) (T)(ptr)
#endif

// EFX
static LPALGENEFFECTS alGenEffects;
static LPALDELETEEFFECTS alDeleteEffects;
static LPALISEFFECT alIsEffect;
static LPALEFFECTI alEffecti;
static LPALEFFECTIV alEffectiv;
static LPALEFFECTF alEffectf;
static LPALEFFECTFV alEffectfv;
static LPALGETEFFECTI alGetEffecti;
static LPALGETEFFECTIV alGetEffectiv;
static LPALGETEFFECTF alGetEffectf;
static LPALGETEFFECTFV alGetEffectfv;

/* Auxiliary Effect Slot object functions */
static LPALGENAUXILIARYEFFECTSLOTS alGenAuxiliaryEffectSlots;
static LPALDELETEAUXILIARYEFFECTSLOTS alDeleteAuxiliaryEffectSlots;
static LPALISAUXILIARYEFFECTSLOT alIsAuxiliaryEffectSlot;
static LPALAUXILIARYEFFECTSLOTI alAuxiliaryEffectSloti;
static LPALAUXILIARYEFFECTSLOTIV alAuxiliaryEffectSlotiv;
static LPALAUXILIARYEFFECTSLOTF alAuxiliaryEffectSlotf;
static LPALAUXILIARYEFFECTSLOTFV alAuxiliaryEffectSlotfv;
static LPALGETAUXILIARYEFFECTSLOTI alGetAuxiliaryEffectSloti;
static LPALGETAUXILIARYEFFECTSLOTIV alGetAuxiliaryEffectSlotiv;
static LPALGETAUXILIARYEFFECTSLOTF alGetAuxiliaryEffectSlotf;
static LPALGETAUXILIARYEFFECTSLOTFV alGetAuxiliaryEffectSlotfv;

// fading
static dboolean is_fading;
static float fading_source;
static float fading_target;
static uint32_t fading_timer;
static uint32_t fading_duration;
static int32_t fading_id;
static void (*fading_callback)(void);
static dboolean fading_do_callback;
static dboolean fading_nocleanup;

static void SoundEFX_OnChange(void)
{
	COM_ImmedExecute("restartaudio");
}

consvar_t cv_soundefx = CVAR_INIT ("soundefx", "On", CV_SAVE|CV_CALL|CV_NOINIT, CV_OnOff, SoundEFX_OnChange);

// any access (read/write) to this struct MUST lock the mutex
typedef struct audiostate_s
{
	I_mutex mutex;

	viofile_t musicfile;
	SNDFILE *musicstream;
	SF_INFO musicinfo;
	ALCdevice *device;
	ALCcontext *context;
	int bufferindex;
	ALuint musicbuffers[BUFFERCOUNT];
	ALuint music;
	float loop_point;
	dboolean looping;

	struct
	{
		dboolean enabled;
		ALuint effect;
		ALuint slot;
	} globalefx;

	dboolean startplay;
	dboolean shutdown;
} audiostate_t;
static audiostate_t audio;

// use this helper to avoid boilerplate!
#define LOCKAUDIO \
	CLEANUP(UnlockMutex) char dummy__; \
	I_lock_mutex(&audio.mutex) \

static void UnlockMutex(char *dummy)
{
	(void)dummy;
	I_unlock_mutex(audio.mutex);
}

#ifdef HAVE_GME
static Music_Emu *gme;
static uint16_t current_track;
#endif

#ifdef HAVE_OPENMPT
static int mod_err = OPENMPT_ERROR_OK;
static const char *mod_err_str;
static int16_t current_subsong = -1;
static size_t probesize;
static int result;
#endif

#define TRY(func, ...) (func(__VA_ARGS__), ALError(__func__, #func))

static dboolean ALError(const char *srcfunc, const char *alfunc)
{
	const char *str;
	switch (alGetError())
	{
	case AL_NO_ERROR:
		str = NULL; break;
	case AL_INVALID_NAME:
		str = "Bad ID"; break;
	case AL_INVALID_ENUM:
		str = "Bad enum value"; break;
	case AL_INVALID_VALUE:
		str = "Bad value"; break;
	case AL_INVALID_OPERATION:
		str = "Operation not valid"; break;
	case AL_OUT_OF_MEMORY:
		str = "Out of memory"; break;
	default:
		str = "Unknown error"; break;
	}

	if (str != NULL)
		CONS_Alert(CONS_ERROR, "%s: %s: %s\n", srcfunc, alfunc, str);
	return str == NULL;
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

static sample_format_t I_GetSampleFormat(SF_INFO info)
{
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
				return Float;
			}
			break;
		default:
			return Int16;
			break;
	}

	return Int16;
}

// who tf is Al
static ALenum I_GetALFormat(SF_INFO info)
{
	sample_format_t sample_format = I_GetSampleFormat(info);

	if (info.channels == 2) // stereo
	{
		return (sample_format == Float) ? AL_FORMAT_STEREO_FLOAT32 : AL_FORMAT_STEREO16;
	}

	return (sample_format == Float) ? AL_FORMAT_MONO_FLOAT32 : AL_FORMAT_MONO16;
}

static void AudioThread(void *userdata);

static void var_cleanup(void)
{
	LOCKAUDIO;

	fading_source = fading_target = fading_timer = fading_duration = 0;

	audio.loop_point = 0.0f;
	audio.looping = false;
	audio.startplay = false;

	is_fading = false;

	// HACK: See music_loop, where we want the fade timing to proceed after a non-looping
	// song has stopped playing
	if (!fading_nocleanup)
		fading_callback = NULL;
	else
		fading_nocleanup = false; // use it once, set it back immediately

	fading_do_callback = false;

	internal_volume = 1.0f;
}

void I_StartupSound(void)
{
	if (sound_started)
		return; // not an error condition

	//LOCKAUDIO; don't need this on startup

	CONS_Printf("I_StartupSound()...\n");
	audio.device = alcOpenDevice(NULL);
	if (audio.device == NULL)
	{
		CONS_Alert(CONS_ERROR, "OpenAL: Can't open audio device");
		return;
	}

	// enable the limiter and disable hrtf
	// idk if either is necessary tbh but fuckit
	ALCint attrs[] = {
		ALC_OUTPUT_LIMITER_SOFT, ALC_FALSE, // G: too noticeable
		ALC_HRTF_SOFT, ALC_FALSE,
		ALC_FREQUENCY, SAMPLERATE, // G: resampling to 48 kHz muffles the audio on My Machine
		0
	};

	audio.context = alcCreateContext(audio.device, attrs);
	if (audio.context == NULL)
	{
		CONS_Alert(CONS_ERROR, "OpenAL: Can't create context");
		alcCloseDevice(audio.device);
		return;
	}

	alcMakeContextCurrent(audio.context);

	const char *vendor = alGetString(AL_VENDOR);
	const char *renderer = alGetString(AL_RENDERER);
	I_OutputMsg("OpenAL Vendor: %s\n", vendor);
	I_OutputMsg("OpenAL Renderer: %s\n", renderer);


	if (!fastcmp("OpenAL Soft", renderer))
	{
		I_ShutdownSound();
		I_Error("OpenAL: You do not seem to be using OpenAL-Soft.\n");
	}

	if(!alcIsExtensionPresent(alcGetContextsDevice(alcGetCurrentContext()), "ALC_EXT_EFX"))
	{
		I_ShutdownSound();
		I_Error("OpenAL: Your OpenAL version does not seem to support EFX .\n");
	}

#define LOAD_PROC(T, x)  ((x) = FUNCTION_CAST(T, alGetProcAddress(#x)))
    LOAD_PROC(LPALGENEFFECTS, alGenEffects);
    LOAD_PROC(LPALDELETEEFFECTS, alDeleteEffects);
    LOAD_PROC(LPALISEFFECT, alIsEffect);
    LOAD_PROC(LPALEFFECTI, alEffecti);
    LOAD_PROC(LPALEFFECTIV, alEffectiv);
    LOAD_PROC(LPALEFFECTF, alEffectf);
    LOAD_PROC(LPALEFFECTFV, alEffectfv);
    LOAD_PROC(LPALGETEFFECTI, alGetEffecti);
    LOAD_PROC(LPALGETEFFECTIV, alGetEffectiv);
    LOAD_PROC(LPALGETEFFECTF, alGetEffectf);
    LOAD_PROC(LPALGETEFFECTFV, alGetEffectfv);

    LOAD_PROC(LPALGENAUXILIARYEFFECTSLOTS, alGenAuxiliaryEffectSlots);
    LOAD_PROC(LPALDELETEAUXILIARYEFFECTSLOTS, alDeleteAuxiliaryEffectSlots);
    LOAD_PROC(LPALISAUXILIARYEFFECTSLOT, alIsAuxiliaryEffectSlot);
    LOAD_PROC(LPALAUXILIARYEFFECTSLOTI, alAuxiliaryEffectSloti);
    LOAD_PROC(LPALAUXILIARYEFFECTSLOTIV, alAuxiliaryEffectSlotiv);
    LOAD_PROC(LPALAUXILIARYEFFECTSLOTF, alAuxiliaryEffectSlotf);
    LOAD_PROC(LPALAUXILIARYEFFECTSLOTFV, alAuxiliaryEffectSlotfv);
    LOAD_PROC(LPALGETAUXILIARYEFFECTSLOTI, alGetAuxiliaryEffectSloti);
    LOAD_PROC(LPALGETAUXILIARYEFFECTSLOTIV, alGetAuxiliaryEffectSlotiv);
    LOAD_PROC(LPALGETAUXILIARYEFFECTSLOTF, alGetAuxiliaryEffectSlotf);
    LOAD_PROC(LPALGETAUXILIARYEFFECTSLOTFV, alGetAuxiliaryEffectSlotfv);
#undef LOAD_PROC

	audio.globalefx.enabled = false;
	audio.music = INVALID_HANDLE;
	audio.bufferindex = 0;

	for (size_t i = 0; i < BUFFERCOUNT; i++)
		audio.musicbuffers[i] = INVALID_HANDLE;

	var_cleanup();
	music_volume = sfx_volume = 0.f;

#ifdef HAVE_OPENMPT
	const char *openmptstr;
	openmptstr = openmpt_get_string("library_version");
	I_OutputMsg("libopenmpt version: %s\n", openmptstr);
	openmpt_free_string(openmptstr);
	openmptstr = openmpt_get_string("build");
	I_OutputMsg("libopenmpt build date: %s\n", openmptstr);
	openmpt_free_string(openmptstr);
#endif

#ifdef HAVE_THREADS
	I_spawn_thread("update-audio", AudioThread, NULL);
#endif

	sound_started = true;
}

void I_ShutdownSound(void)
{
	if (!sound_started)
		return; // not an error condition
	CONS_Printf("I_ShutdownSound(): shut down\n");
	sound_started = false;

	// kill the audio thread in a half-assed way
	{
		LOCKAUDIO;
		audio.shutdown = true;
	}
	while (true)
	{
		LOCKAUDIO;
		if (audio.shutdown == false)
			break;
	}

	// yay, the audio thread is dead! we can shut down now
	alcMakeContextCurrent(NULL);
	alcDestroyContext(audio.context);
	alcCloseDevice(audio.device);

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
	if (audio.musicstream != NULL)
	{
		sf_close(audio.musicstream);
		audio.musicstream = NULL;
	}
}

static dboolean I_UpdateMusicVolume(void)
{
	LOCKAUDIO;
	return TRY(alSourcef, audio.music, AL_GAIN, music_volume*internal_volume*(musicdef_volume/(float)MAX_VOLUME));
}

// Thank you random stack overflow user (second post)!
// https://stackoverflow.com/questions/10160401/openal-get-the-current-playing-position-of-a-source

static float SamplesToSeconds(sf_count_t samples, int sampleRate) {
	return (float)samples / sampleRate;
}

static sf_count_t SecondsToSamples(float seconds, int sampleRate) {
	return (sf_count_t)lround(seconds * sampleRate);
}

static dboolean I_QueueNextSample(dboolean unqueue)
{
	TracyCZone(__zone, true);
	static int16_t *stream = NULL;
	ALenum format = AL_FORMAT_MONO16;

	LOCKAUDIO;

	// keeping it at PU_CACHE_UNLOCKED is fine since we won't do any Z allocations while we use it
	if (stream == NULL)
		Z_Malloc(sizeof(int16_t) * 2 * BUFFERSIZE, PU_CACHE_UNLOCKED, &stream);

	size_t samplerate = SAMPLERATE;
	size_t count;
#ifdef HAVE_OPENMPT
	if (openmpt_mhandle != NULL)
	{
		count = openmpt_module_read_interleaved_stereo(openmpt_mhandle, SAMPLERATE, BUFFERSIZE, stream) * 2 * sizeof(int16_t);
		format = AL_FORMAT_STEREO16;

		// this is kinda dumb but works lol
		// resets the songpos after it loops
		if (I_GetSongPosition() >= I_GetSongLength())
		{
			openmpt_module_set_position_seconds(openmpt_mhandle, 0);
		}
	}
	else
#endif
#ifdef HAVE_GME
	if (gme != NULL)
	{
		count = BUFFERSIZE * 2 * sizeof(int16_t);
		gme_play(gme, BUFFERSIZE * 2, stream);
		format = AL_FORMAT_STEREO16;
		samplerate = SAMPLERATE;
	}
	else
#endif
	if (audio.musicstream != NULL)
	{
		format = I_GetALFormat(audio.musicinfo);
		sample_format_t sample_format = I_GetSampleFormat(audio.musicinfo);

		if (sample_format == Float)
		{
			count = sf_read_float(audio.musicstream, (float *)stream, BUFFERSIZE) * sizeof(float); // float is 4 bytes not 2, so just double this shit for now kek
		}
		else
		{
			count = sf_read_short(audio.musicstream, (short *)stream, BUFFERSIZE) * sizeof(short);
		}

		if (count == 0)
		{
			// Loop music otherwise stop.
			if (audio.looping)
			{
				float songlength = I_GetSongLength() / 1000;
				float looppoint = audio.loop_point;

				// Bad Loop Point?
				// Go back to the start.
				if (audio.loop_point > songlength)
					looppoint = 0.0f;

				sf_count_t finalposition = SecondsToSamples(looppoint, audio.musicinfo.samplerate);
				sf_seek(audio.musicstream, finalposition, SF_SEEK_SET);
			}
		}

		samplerate = audio.musicinfo.samplerate;
	}
	else
	{
		CONS_Alert(CONS_ERROR, "Music is active, but no source is available\n");
		TracyCZoneEnd(__zone);
		return false;
	}

	if (unqueue && !TRY(alSourceUnqueueBuffers, audio.music, 1, &audio.musicbuffers[audio.bufferindex]))
	{
		TracyCZoneEnd(__zone);
		return false;
	}

	if (!TRY(alBufferData, audio.musicbuffers[audio.bufferindex], format, stream, count, samplerate))
	{
		TracyCZoneEnd(__zone);
		return false;
	}

	if (!TRY(alSourceQueueBuffers, audio.music, 1, &audio.musicbuffers[audio.bufferindex]))
	{
		TracyCZoneEnd(__zone);
		return false;
	}

	audio.bufferindex = (audio.bufferindex + 1) % BUFFERCOUNT;


	if (count == 0 && !audio.looping)
	{
		I_StopSong();
		TracyCZoneEnd(__zone);
		return false;
	}

	TracyCZoneEnd(__zone);
	return true;
}

static void UpdateSound(audiostate_t *aud)
{
	TracyCZone(__zone, true);
	if (aud->music != INVALID_HANDLE)
	{
		ALint processed;
		ALint queued;

		// handle this in here not in i_loadsong!
		if (aud->startplay)
		{
			for (size_t i = 0; i < BUFFERCOUNT; i++)
			{
				if (!I_QueueNextSample(false))
				{
					I_UnloadSong();
					TracyCZoneEnd(__zone);
					return;
				}
			}

			TRY(alSourcePlay, aud->music);

			aud->startplay = false;
			TracyCZoneEnd(__zone);
			return;
		}

		if (!TRY(alGetSourcei, aud->music, AL_BUFFERS_PROCESSED, &processed))
		{
			TracyCZoneEnd(__zone);
			return;
		}

		if (!TRY(alGetSourcei, aud->music, AL_BUFFERS_QUEUED, &queued))
		{
			TracyCZoneEnd(__zone);
			return;
		}

		// semi-hack because this is sometimes not called for a longer period of time
		dboolean need_restart = processed == BUFFERCOUNT;
		while (processed > 0)
		{
			if (!I_QueueNextSample(true))
				break;
			processed--;
		}
		if (need_restart)
			TRY(alSourcePlay, aud->music);
	}
	TracyCZoneEnd(__zone);
}

void I_UpdateSound(void)
{
#ifndef HAVE_THREADS
	UpdateSound(&audio);
#endif

	if (fading_do_callback)
	{
		if (fading_callback)
			(*fading_callback)();
		fading_callback = NULL;
		fading_do_callback = false;
	}
}

static void AudioThread(void *userdata)
{
	TracyCZone(__zone, true);
	(void)userdata;


	while (true)
	{
		// doesn't need to be that frequent, this thread just feeds buffers
		I_Sleep(50);

		{
			LOCKAUDIO;
			if (audio.shutdown)
			{
				audio.shutdown = false;
				TracyCZoneEnd(__zone);
				return;
			}
			UpdateSound(&audio);
		}
	}
	TracyCZoneEnd(__zone);
}

static char *ds2chunk(const void *stream, size_t *len, size_t *samplerate)
{
	uint16_t ver, freq;
	uint32_t samples, i;
	uint8_t *sound;

	int16_t o;

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

	sound = Z_Malloc(samples<<2, PU_SOUND, NULL); // samples * frequency shift * bytes per sample * channels

	const int8_t *s = stream;
	uint8_t *d = sound;

	i = 0;

	// dont resample shit, let openal handle everything
	while (i++ < samples)
	{
		o = ((int16_t)(*s++)+0x80)<<8; // changed signedness and shift up to 16 bits
		WRITEINT16(d, o); // left channel
		WRITEINT16(d, o); // right channel
	}

	*len = d - sound;
	return (char *)sound;
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
	CLEANUP(Z_Pfree) char *data = ds2chunk(input, &size, &samplerate);
	ALenum format = AL_FORMAT_MONO16;

	if (data == NULL)
	{
#ifdef HAVE_GME
		Music_Emu *emu;
		gme_info_t *gme_info;

		// VGZ format
		if (((uint8_t *)input)[0] == 0x1F
			&& ((uint8_t *)input)[1] == 0x8B)
		{
#ifdef HAVE_ZLIB
			uint8_t *inflatedData;
			size_t inflatedLen;
			z_stream stream;
			int zErr; // Somewhere to handle any error messages zlib tosses out
			memset(&stream, 0x00, sizeof (z_stream)); // Init zlib stream
			// Begin the inflation process
			inflatedLen = *(uint32_t *)input + (size-4); // Last 4 bytes are the decompressed size, typically
			inflatedData = (uint8_t *)Z_Malloc(inflatedLen, PU_SOUND, NULL); // Make room for the decompressed data
			stream.total_in = stream.avail_in = size;
			stream.total_out = stream.avail_out = inflatedLen;
			stream.next_in = (uint8_t *)input;
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
						uint32_t len;
						gme_equalizer_t eq = {GME_TREBLE, GME_BASS, 0,0,0,0,0,0,0,0};

						Z_Free(inflatedData); // GME supposedly makes a copy for itself, so we don't need this lying around

						gme_start_track(emu, 0);
						gme_set_equalizer(emu, &eq);
						gme_track_info(emu, &gme_info, 0);

						len = (gme_info->play_length * 441 / 10) << 2;
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

			size = 4096;
			size_t pos = 0;
			data = Z_Malloc(size, PU_SOUND, NULL);

			format = I_GetALFormat(info);
			sample_format_t sample_format = I_GetSampleFormat(info);

			for (;;)
			{
				sf_count_t count;

				if (sample_format == Float)
					count = sf_read_float(snd, (float *)&data[pos], (size - pos) / sizeof(float));
				else
					count = sf_read_short(snd, (short *)&data[pos], (size - pos) / sizeof(short));

				if (count == 0)
					break;

				pos += count * ((sample_format == Float) ? sizeof(float) : sizeof(short));
				if (pos >= size)
				{
					size <<= 1;
					data = Z_Realloc(data, size, PU_SOUND, NULL);
				}
			}

			size = pos;
			samplerate = info.samplerate;
			sf_close(snd);

			// SFC_SET_NORM_FLOAT apparently doesn't work, so gotta do this ourselves
			if (sample_format == Float)
			{
				float *norm = (float *)data;
				for (pos = 0; pos < size / sizeof(float); pos++)
					norm[pos] = CLAMP(norm[pos], -1.0f, 1.0f);
			}
		}
	}

	ALuint buffer;
	if (!TRY(alGenBuffers, 1, &buffer))
		return INVALID_HANDLE;

	if (!TRY(alBufferData, buffer, format, data, size, samplerate))
	{
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

void I_UpdateSoundParams(int32_t handle, uint8_t vol, uint8_t sep, uint8_t pitch, efx_t *efx, const void *origin)
{
	LOCKAUDIO;

	if (handle == INVALID_HANDLE)
		return;

	const mobj_t *originmobj = (const mobj_t *)origin;
	ALuint source = handle;
	TRY(alSourcef, source, AL_GAIN, sfx_volume * (vol / 255.0f));
	TRY(alSourcef, source, AL_PITCH, pitch / 128.0f);

	// TODO: Real 3D Audio.
	// create a panning effect by moving the source in an arc around the listener
	// Based on https://github.com/kcat/openal-soft/issues/194
	float leftpan = min((0xff-sep)<<1, 0xff) / 255.0f;
	float rightpan = min(sep<<1, 0xff) / 255.0f;
	float pan = (acosf(leftpan) + asinf(rightpan)) / ((float)M_PI); // average angle in [0,1]
	pan = 2 * pan - 1; // convert to [-1, 1]
	pan = pan * 0.5f; // 0.5 = sin(30') for a +/- 30 degree arc
	TRY(alSourcei, source, AL_SOURCE_RELATIVE, AL_TRUE);
	TRY(alSource3f, source, AL_POSITION, pan, 0, -sqrtf(1.0f - pan*pan));

	if (cv_soundefx.value && !dedicated)
	{
		if (efx != NULL && TRY(alIsEffect, efx->al.effect) && TRY(alIsAuxiliaryEffectSlot, efx->al.slot))
		{
			TRY(alSource3i, source, AL_AUXILIARY_SEND_FILTER, efx->al.slot, 0, AL_FILTER_NULL);
		}
		else if (origin != NULL && audio.globalefx.enabled
			&& mapheaderinfo[gamemap-1]->globalEFX->type != EFFECT_NONE
			&& TRY(alIsEffect, audio.globalefx.effect)
			&& TRY(alIsAuxiliaryEffectSlot, audio.globalefx.slot))
		{
			if (originmobj && originmobj->type && originmobj->subsector && originmobj->subsector->sector && (originmobj->subsector->sector->flags & MSF_NOEFX))
				TRY(alSource3i, source, AL_AUXILIARY_SEND_FILTER, AL_EFFECTSLOT_NULL, 0, AL_FILTER_NULL);
			else
				TRY(alSource3i, source, AL_AUXILIARY_SEND_FILTER, audio.globalefx.slot, 0, AL_FILTER_NULL);
		}
	}
}

int32_t I_StartSound(sfxenum_t id, uint8_t vol, uint8_t sep, uint8_t pitch, uint8_t priority, int32_t channel, efx_t *efx, const void * origin)
{
	ALuint source;
	if (!TRY(alGenSources, 1, &source))
		return INVALID_HANDLE;

	if (!TRY(alSourcei, source, AL_BUFFER, (uintptr_t)S_sfx[id].data))
	{
		alDeleteSources(1, &source);
		return INVALID_HANDLE;
	}

	if (!TRY(alSourcei, source, AL_SOURCE_RELATIVE, AL_TRUE))
	{
		alDeleteSources(1, &source);
		return INVALID_HANDLE;
	}

	I_UpdateSoundParams(source, vol, sep, pitch, efx, origin);

	if (!TRY(alSourcePlay, source))
	{
		alDeleteSources(1, &source);
		return INVALID_HANDLE;
	}

	//(void)sep; // panning is now handled by real object positioning instead
	(void)priority; // priority and channel management is handled by SRB2...
	(void)channel; // no longer used
	return source;
}

void I_StopSound(int32_t handle)
{
	if (handle == INVALID_HANDLE)
		return;

	ALuint source = handle;
	TRY(alSourceStop, source);
	TRY(alDeleteSources, 1, &source);
}

dboolean I_SoundIsPlaying(int32_t handle)
{
	if (handle == INVALID_HANDLE || !alIsSource((ALuint)handle))
		return false;

	ALint value;
	if (!TRY(alGetSourcei, (ALuint)handle, AL_SOURCE_STATE, &value))
		return false;

	return value == AL_PLAYING;
}

void I_SetSfxVolume(uint8_t volume)
{
	sfx_volume = volume / VOLUME_DIV;
}

static void do_fading_callback(void)
{
	// TODO: Should I use a mutex here or something?
	fading_do_callback = true;
}

static uint32_t music_fade(void *userdata, SDL_TimerID timerID, Uint32 interval)
{
	(void)userdata;
	(void)timerID;

	if (!is_fading
		|| fabs(internal_volume - fading_target) < FLOATEP // EQUALS I love floats I love floats
		|| fading_duration == 0)
	{
		I_StopFadingSong();
		do_fading_callback();
		return 0;
	}
	else if (I_SongPaused()) // don't decrement timer
	{
		return interval;
	}
	else if ((fading_timer -= 10) <= 0)
	{
		internal_volume = fading_target;
		I_UpdateMusicVolume();
		I_StopFadingSong();
		do_fading_callback();
		return 0;
	}
	else
	{
		float delta = fabsf(fading_target - fading_source);
		float factor = (fading_duration - fading_timer) / (float)fading_duration;

		if (fading_target < fading_source)
			internal_volume = max(min(internal_volume, fading_source - (delta * factor)), fading_target);
		else if (fading_target > fading_source)
			internal_volume = min(max(internal_volume, fading_source + (delta * factor)), fading_target);

		I_UpdateMusicVolume();

		return interval;
	}
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
	LOCKAUDIO;

#ifdef HAVE_GME
	if (gme)
		return MU_GME;
#endif
#ifdef HAVE_OPENMPT
	if (openmpt_mhandle)
		return MU_MOD_EX;
#endif

	// TODO if ever ported back to SRB2: add fluidsynth support

	if (audio.music != INVALID_HANDLE)
	{
		switch ((audio.musicinfo.format & SF_FORMAT_SUBMASK))
		{
			case SF_FORMAT_OPUS:
				return MU_OPUS;
				break;
			case SF_FORMAT_VORBIS:
				return MU_OGG;
				break;
			case SF_FORMAT_FLAC:
			case SF_FORMAT_ALAC_20:
			case SF_FORMAT_ALAC_24:
			case SF_FORMAT_ALAC_32:
				return MU_FLAC;
				break;
			case SF_FORMAT_WAV:
				return MU_WAV;
				break;
#ifdef HAVE_SNDFILE_MPEG
			case SF_FORMAT_MPEG_LAYER_I:
			case SF_FORMAT_MPEG_LAYER_II:
			case SF_FORMAT_MPEG_LAYER_III:
				return MU_MP3;
				break;
#endif
		}
	}

	return MU_NONE;
}

/*dboolean I_SongAcutallyPlaying(void)
{
	LOCKAUDIO;

	if (audio.music == INVALID_HANDLE)
		return false;

	ALint value;
	if (!TRY(alGetSourcei, audio.music, AL_SOURCE_STATE, &value))
		return false;

	return value == AL_PLAYING;
}*/

dboolean I_SongPlaying(void)
{
	LOCKAUDIO;

	if (audio.music == INVALID_HANDLE)
		return false;

	return true;
}

dboolean I_SongPaused(void)
{
	LOCKAUDIO;

	if (audio.music == INVALID_HANDLE)
		return false;

	ALint value;
	if (!TRY(alGetSourcei, audio.music, AL_SOURCE_STATE, &value))
		return false;

	return value == AL_PAUSED;
}

dboolean I_SetSongSpeed(float speed)
{
	LOCKAUDIO;

	if (audio.music == INVALID_HANDLE)
		return false;
	if (speed > 250.0f)
		speed = 250.0f; //limit speed up to 250x

	// FIXME: implement
	return false;
}

static uint32_t get_adjusted_position(uint32_t position)
{
	// all in milliseconds
	uint32_t length = I_GetSongLength();

	if (length)
	{
		uint32_t looppoint = I_GetSongLoopPoint();
		return position >= length ? (position % (length-looppoint)) : position;
	}
	else
		return position;
}

uint32_t I_GetSongLength(void)
{
	LOCKAUDIO;

	if (audio.music == INVALID_HANDLE)
		return 0;

#ifdef HAVE_GME
	if (gme)
	{
		gme_info_t *info;
		gme_err_t gme_e = gme_track_info(gme, &info, current_track);

		size_t length;

		if (gme_e != NULL)
		{
			CONS_Alert(CONS_ERROR, "I_GetSongLength: GME error: %s\n", gme_e);
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
		return (uint32_t)(openmpt_module_get_duration_seconds(openmpt_mhandle) * 1000.);
#endif

	float position = SamplesToSeconds(audio.musicinfo.frames, audio.musicinfo.samplerate);

	return position * 1000.0f;
}

dboolean I_SetSongLoopPoint(uint32_t looppoint)
{
	(void)looppoint;

	LOCKAUDIO;

	const musictype_t mustype = I_SongType();

	if (mustype == MU_GME || mustype == MU_MOD)
		return false;

	uint32_t length = I_GetSongLength();

	if (length > 0)
		looppoint %= length;

	audio.loop_point = max((float)(looppoint / 1000.0L), 0);

	return true;
}

uint32_t I_GetSongLoopPoint(void)
{
	LOCKAUDIO;

	if (audio.music == INVALID_HANDLE)
		return 0;

#ifdef HAVE_GME
	if (gme)
	{
		int32_t looppoint;
		gme_info_t *info;
		gme_err_t gme_e = gme_track_info(gme, &info, current_track);

		if (gme_e != NULL)
		{
			CONS_Alert(CONS_ERROR, "I_GetSongLoopPoint: GME error: %s\n", gme_e);
			looppoint = 0;
		}
		else
		{
			looppoint = info->intro_length > 0 ? info->intro_length : 0;
		}

		gme_free_info(info);
		return max(looppoint, 0);
	}
	else
#endif

	return (uint32_t)(audio.loop_point * 1000);
}

dboolean I_SetSongPosition(uint32_t position)
{
	LOCKAUDIO;

	if (audio.music == INVALID_HANDLE)
		return false;

#ifdef HAVE_GME
	if (gme)
	{
		// this is unstable, so fail silently
		return true;
		// this isn't required technically, but GME thread-locks for a second
		// if you seek too high from the counter
		// length = I_GetSongLength();
		// if (length)
		// 	position = get_adjusted_position(position);

		// SDL_LockAudio();
		// gme_err_t gme_e = gme_seek(gme, position);
		// SDL_UnlockAudio();

		// if (gme_e != NULL)
		// {
		// 	CONS_Alert(CONS_ERROR, "I_SetSongPosition: GME error: %s\n", gme_e);
		// 	return false;
		// }
		// else
		// 	return true;
	}
	else
#endif
#ifdef HAVE_OPENMPT
	if (openmpt_mhandle)
	{
		// This isn't 100% correct because we don't account for loop points because we can't get them.
		// But if you seek past end of song, OpenMPT seeks to 0. So adjust the position anyway.
		openmpt_module_set_position_seconds(openmpt_mhandle, (double)(get_adjusted_position(position)/1000.0L)); // returns new position
		return true;
	}
	else
#endif
	{
		uint32_t length = I_GetSongLength(); // get it in MS

		if (length)
			position = get_adjusted_position(position);

		position = position / 1000.0f;

		sf_count_t finalposition = SecondsToSamples(position, audio.musicinfo.samplerate);
		sf_seek(audio.musicstream, finalposition, SF_SEEK_SET);

		return true;
	}
}

uint32_t I_GetSongPosition(void)
{
	LOCKAUDIO;

	if (audio.music == INVALID_HANDLE)
		return 0;

#ifdef HAVE_GME
	if (gme)
	{
		int32_t position = gme_tell(gme);

		gme_info_t *info;
		gme_err_t gme_e = gme_track_info(gme, &info, current_track);

		if (gme_e != NULL)
		{
			CONS_Alert(CONS_ERROR, "I_GetSongPosition: GME error: %s\n", gme_e);
			return position;
		}
		else
		{
			// adjust position, since GME's counter keeps going past loop
			if (info->length > 0)
				position %= info->length;
			else if (info->intro_length + info->loop_length > 0)
				position = position >= (info->intro_length + info->loop_length) ? (position % info->loop_length) : position;
			else
				position %= 150 * 1000; // 2.5 minutes
		}

		gme_free_info(info);

		return (uint32_t)(max(position, 0));
	}
	else
#endif
#ifdef HAVE_OPENMPT
	if (openmpt_mhandle)
		// This will be incorrect if we adjust for length because we can't get loop points.
		// So return unadjusted. See note in SetMusicPosition: we adjust for that.
		return (uint32_t)(openmpt_module_get_position_seconds(openmpt_mhandle)*1000.);
	else
#endif
	{
		sf_count_t samples = sf_seek(audio.musicstream, 0, SF_SEEK_CUR);

		float position = SamplesToSeconds(samples, audio.musicinfo.samplerate);

		return (uint32_t)(position * 1000.0f);
	}
}


static dboolean InitializeMusicStreaming(void *input, size_t size)
{
	LOCKAUDIO;

	// Not a doom sound? Try something else.

#ifdef HAVE_GME
	// VGZ format
	if (((uint8_t *)input)[0] == 0x1F
		&& ((uint8_t *)input)[1] == 0x8B)
	{
#ifdef HAVE_ZLIB
		uint8_t *inflatedData;
		size_t inflatedLen;
		z_stream stream;
		int zErr; // Somewhere to handle any error messages zlib tosses out

		memset(&stream, 0x00, sizeof (z_stream)); // Init zlib stream
		// Begin the inflation process
		inflatedLen = *(uint32_t *)input + (size-4); // Last 4 bytes are the decompressed size, typically
		inflatedData = (uint8_t *)Z_Malloc(inflatedLen, PU_SOUND, NULL); // Make room for the decompressed data
		stream.total_in = stream.avail_in = size;
		stream.total_out = stream.avail_out = inflatedLen;
		stream.next_in = (uint8_t *)input;
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
					gme_equalizer_t eq = {GME_TREBLE, GME_BASS, 0,0,0,0,0,0,0,0};
					gme_set_equalizer(gme, &eq);
					gme_start_track(gme, 0);
					current_track = 0;
					Z_Free(inflatedData); // GME supposedly makes a copy for itself, so we don't need this lying around
					inflateEnd(&stream);
					return true;
				}
			}
			else
				CONS_Alert(CONS_ERROR,"Encountered %s when running inflate: %s\n", GetZLibError(zErr), stream.msg);

			(void)inflateEnd(&stream);
		}
		else // Hold up, zlib's got a problem
		{
			CONS_Alert(CONS_ERROR,"Encountered %s when running inflateInit: %s\n", GetZLibError(zErr), stream.msg);
		}

		Z_Free(inflatedData); // GME didn't open jack, but don't let that stop us from freeing this up
		return false;
#else
		return INVALID_HANDLE; // No zlib support
#endif
	}
	// Try to read it as a GME sound
	else if (!gme_open_data(input, size, &gme, SAMPLERATE))
	{
		gme_equalizer_t eq = {GME_TREBLE, GME_BASS, 0,0,0,0,0,0,0,0};
		gme_set_equalizer(gme, &eq);
		gme_start_track(gme, 0);
		current_track = 0;
		return true;
	}
#endif
	audio.musicfile.data = input;
	audio.musicfile.cur = input;
	audio.musicfile.end = (char *)input + size;
	audio.musicinfo.frames = 1,
	audio.musicinfo.samplerate = SAMPLERATE,
	audio.musicinfo.channels = 1,
	audio.musicinfo.format = 0,
	audio.musicinfo.sections = 1,
	audio.musicinfo.seekable = 1,
	audio.musicstream = sf_open_virtual(&virtual_io, SFM_READ, &audio.musicinfo, &audio.musicfile);
	if (audio.musicstream != NULL)
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

dboolean I_LoadSong(char *data, size_t len)
{
	const char *key1 = "LOOP";
	const char *key2 = "POINT=";
	const char *key3 = "MS=";
	const size_t key1len = strlen(key1);
	const size_t key2len = strlen(key2);
	const size_t key3len = strlen(key3);
	char *p = data;

	LOCKAUDIO;

	if (audio.music != INVALID_HANDLE)
		I_UnloadSong();

	var_cleanup();

	if (!InitializeMusicStreaming(data, len))
		return false;

	if (!TRY(alGenSources, 1, &audio.music))
	{
		I_UnloadSong();
		return false;
	}

	if (!TRY(alGenBuffers, BUFFERCOUNT, audio.musicbuffers))
	{
		I_UnloadSong();
		return false;
	}

	// Find the OGG loop point.
	audio.loop_point = 0.0f;

	while ((uint32_t)(p - data) < len)
	{
		if (fpclassify(audio.loop_point) == FP_ZERO && !strncmp(p, key1, key1len))
		{
			p += key1len; // skip LOOP
			if (!strncmp(p, key2, key2len)) // is it LOOPPOINT=?
			{
				p += key2len; // skip POINT=
				//audio.loop_point = (float)(((audio.musicinfo.samplerate / 1000.0L)+atoi(p)) / audio.musicinfo.samplerate); // LOOPPOINT works by sample count.
				// Soooo.... another plan
				// people were looping music around how shitty mixer_sound's loopcode is.
				// so we need their shitty calc for it to sound right for some tracks. This SUCKS.
				audio.loop_point = (float)((44.1L+atoi(p)) / 44100.0L);
			}
			else if (!strncmp(p, key3, key3len)) // is it LOOPMS=?
			{
				p += key3len; // skip MS=
				audio.loop_point = (float)(atof(p) / 1000.0); // LOOPMS works by real time, as miliseconds.
				// Everything that uses LOOPMS will work perfectly with SDL_Mixer.
			}
		}

		if (fpclassify(audio.loop_point) != FP_ZERO) // Got what we needed
			break;
		else // continue searching
			p++;
	}

	return true;
}

void I_UnloadSong(void)
{
	LOCKAUDIO;

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
	if (audio.musicstream != NULL)
	{
		sf_close(audio.musicstream);
		audio.musicstream = NULL;
	}
	if (audio.music != INVALID_HANDLE)
	{
		alDeleteSources(1, &audio.music);
		alDeleteBuffers(BUFFERCOUNT, audio.musicbuffers);
		for (size_t i = 0; i < BUFFERCOUNT; i++)
			audio.musicbuffers[i] = INVALID_HANDLE;
		audio.music = INVALID_HANDLE;
		audio.loop_point = 0.0f;
		audio.startplay = false;
	}
}

dboolean I_PlaySong(dboolean looping)
{
	LOCKAUDIO;

	if (audio.music == INVALID_HANDLE)
		return false;

	if (!I_UpdateMusicVolume())
		return false;

	audio.looping = looping;

#ifdef HAVE_GME
	if (gme)
	{
#if defined (GME_VERSION) && GME_VERSION >= 0x000603
		if (looping)
			gme_set_autoload_playback_limit(gme, 0);
#endif
	}
	else
#endif
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

	//if (!TRY(alSourcePlay, audio.music))
		//return false;

	audio.startplay = true;

	return true;
}

void I_StopSong(void)
{
	LOCKAUDIO;

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
	if (audio.music != INVALID_HANDLE)
		alSourceStop(audio.music);

	var_cleanup();
}

void I_PauseSong(void)
{
	LOCKAUDIO;

	if (audio.music != INVALID_HANDLE)
		alSourcePause(audio.music);
}

void I_ResumeSong(void)
{
	LOCKAUDIO;

	alSourcePlay(audio.music);
}

void I_SetMusicVolume(uint8_t volume)
{
	LOCKAUDIO;

	music_volume = volume / VOLUME_DIV;
	if (audio.music != INVALID_HANDLE)
		I_UpdateMusicVolume();
}

dboolean I_SetSongTrack(int32_t track)
{
	LOCKAUDIO;

	if (audio.music == INVALID_HANDLE)
		return false;

#ifdef HAVE_GME
	// If the specified track is within the number of tracks playing, then change it
	if (gme)
	{
		if (current_track == track)
			return false;

		if (track >= 0 && track < gme_track_count(gme)-1)
		{
			gme_err_t gme_e = gme_start_track(gme, track);

			if (gme_e != NULL)
			{
				CONS_Alert(CONS_ERROR, "GME error: %s\n", gme_e);
				return false;
			}

			current_track = track;

			return true;
		}

		return false;
	}
	else
	{
#endif
#ifdef HAVE_OPENMPT
		if (openmpt_mhandle)
		{
			if (current_subsong == track)
				return false;
			if (track >= 0 && track < openmpt_module_get_num_subsongs(openmpt_mhandle))
			{
				openmpt_module_select_subsong(openmpt_mhandle, track);
				current_subsong = track;
				return true;
			}

			return false;
		}
#endif
#ifdef HAVE_GME
	}
#endif

	(void)track;
	return false;
}

void I_UpdateSongLagThreshold(void){}
void I_UpdateSongLagConditions(void){}

void I_SetInternalMusicVolume(uint8_t volume)
{
	LOCKAUDIO;

	internal_volume = volume / (float)MAX_VOLUME; // don't divide by VOLUME_DIV, this is a multiplier on music volume
	if (audio.music != INVALID_HANDLE)
		I_UpdateMusicVolume();
}

void I_StopFadingSong(void)
{
	if (fading_id)
		SDL_RemoveTimer(fading_id);

	is_fading = false;
	fading_source = fading_target = fading_timer = fading_duration = 0;
	// don't unset fading_nocleanup here just yet; fading_callback is cleaned up
	// in var_cleanup()
}

dboolean I_FadeSongFromVolume(uint8_t target_volume, uint8_t source_volume, uint32_t ms, void (*callback)(void))
{
	int16_t volume_delta;

	source_volume = min(source_volume, MAX_VOLUME);
	volume_delta = (int16_t)(target_volume - source_volume);

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
		fading_id = SDL_AddTimer(10, music_fade, NULL);

		if (fading_id)
		{
			is_fading = true;
			fading_timer = fading_duration = ms;
			fading_source = source_volume / 100.0f;
			fading_target = target_volume / 100.0f;
			fading_callback = callback;

			if (fabs(internal_volume - (source_volume / 100.0f)) > FLOATEP) // NOT EQUAL I love floats I love floats
				I_SetInternalMusicVolume(source_volume);
		}
	}

	return is_fading;
}

dboolean I_FadeSong(uint8_t target_volume, uint32_t ms, void (*callback)(void))
{
	return I_FadeSongFromVolume(target_volume, (internal_volume * 100.f), ms, callback);
}
dboolean I_FadeOutStopSong(uint32_t ms)
{
	return I_FadeSongFromVolume(0, (internal_volume * 100.f), ms, &I_StopSong);
}

dboolean I_FadeInPlaySong(uint32_t ms, dboolean looping)
{
	if (I_PlaySong(looping))
		return I_FadeSongFromVolume(100, 0, ms, NULL);
	else
		return false;
}

// Debugging
#define VAL(x) #x

#define APPLY_EFFECT(alEffectType, field, altype, alfield) TRY(alEffectType, effect, AL_##altype##_##alfield, efx->field); \
CONS_Debug(DBG_EFX, VAL(AL_##altype##_##alfield) ": %f\n", (float)efx->field)

#define APPLY_EFFECT_BOOL(field, altype, alfield) TRY(alEffecti, effect, AL_##altype##_##alfield, efx->field ? AL_TRUE : AL_FALSE); \
CONS_Debug(DBG_EFX, VAL(AL_##altype##_##alfield) ": %s\n", efx->field ? "TRUE" : "FALSE")

#define APPLY_EFFECT_FVEC3(alEffectType, field, altype, alfield, min, max) \
ALfloat vec3fields##alfield[3]; \
vec3fields##alfield[0] = efx->field.x; \
vec3fields##alfield[1] = efx->field.y; \
vec3fields##alfield[2] = efx->field.z; \
TRY(alEffectType, effect, AL_##altype##_##alfield, vec3fields##alfield); \
CONS_Debug(DBG_EFX, VAL(AL_##altype##_##alfield) ": x=%f y=%f z=%f\n", vec3fields##alfield[0], vec3fields##alfield[1], vec3fields##alfield[2])


static void I_HandleEFXType(efx_t *efx, ALuint effect)
{
	CONS_Debug(DBG_EFX, "Attempting to Handle EFX type.\n");
	switch (efx->type)
	{
		case EFFECT_EAXREVERB:
			TRY(alEffecti, effect, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB);

			APPLY_EFFECT(alEffectf, eaxreverb.density, EAXREVERB, DENSITY);
			APPLY_EFFECT(alEffectf, eaxreverb.diffusion, EAXREVERB, DIFFUSION);
			APPLY_EFFECT(alEffectf, eaxreverb.gain, EAXREVERB, GAIN);
			APPLY_EFFECT(alEffectf, eaxreverb.gainhf, EAXREVERB, GAINHF);
			APPLY_EFFECT(alEffectf, eaxreverb.gainlf, EAXREVERB, GAINLF);
			APPLY_EFFECT(alEffectf, eaxreverb.decay_time, EAXREVERB, DECAY_TIME);
			APPLY_EFFECT(alEffectf, eaxreverb.decay_hfratio, EAXREVERB, DECAY_HFRATIO);
			APPLY_EFFECT(alEffectf, eaxreverb.decay_lfratio, EAXREVERB, DECAY_LFRATIO);
			APPLY_EFFECT(alEffectf, eaxreverb.reflections_gain, EAXREVERB, REFLECTIONS_GAIN);
			APPLY_EFFECT(alEffectf, eaxreverb.reflections_delay, EAXREVERB, REFLECTIONS_DELAY);
			APPLY_EFFECT_FVEC3(alEffectfv, eaxreverb.reflections_pan, EAXREVERB, REFLECTIONS_PAN, -1.0, 1.0);
			APPLY_EFFECT(alEffectf, eaxreverb.late_reverb_gain, EAXREVERB, LATE_REVERB_GAIN);
			APPLY_EFFECT(alEffectf, eaxreverb.late_reverb_delay, EAXREVERB, LATE_REVERB_DELAY);
			APPLY_EFFECT_FVEC3(alEffectfv, eaxreverb.late_reverb_pan, EAXREVERB, LATE_REVERB_PAN, -1.0, 1.0);
			APPLY_EFFECT(alEffectf, eaxreverb.echo_time, EAXREVERB, ECHO_TIME);
			APPLY_EFFECT(alEffectf, eaxreverb.echo_depth, EAXREVERB, ECHO_DEPTH);
			APPLY_EFFECT(alEffectf, eaxreverb.modulation_time, EAXREVERB, MODULATION_TIME);
			APPLY_EFFECT(alEffectf, eaxreverb.modulation_depth, EAXREVERB, MODULATION_DEPTH);
			APPLY_EFFECT(alEffectf, eaxreverb.air_absorption_gainhf, EAXREVERB, AIR_ABSORPTION_GAINHF);
			APPLY_EFFECT(alEffectf, eaxreverb.hfreference, EAXREVERB, HFREFERENCE);
			APPLY_EFFECT(alEffectf, eaxreverb.lfreference, EAXREVERB, LFREFERENCE);
			APPLY_EFFECT(alEffectf, eaxreverb.room_rolloff_factor, EAXREVERB, ROOM_ROLLOFF_FACTOR);
			APPLY_EFFECT_BOOL(eaxreverb.decay_hflimit, EAXREVERB, DECAY_HFLIMIT);
			break;
		case EFFECT_REVERB:
			TRY(alEffecti, effect, AL_EFFECT_TYPE, AL_EFFECT_REVERB);

			APPLY_EFFECT(alEffectf, reverb.density, REVERB, DENSITY);
			APPLY_EFFECT(alEffectf, reverb.diffusion, REVERB, DIFFUSION);
			APPLY_EFFECT(alEffectf, reverb.gain, REVERB, GAIN);
			APPLY_EFFECT(alEffectf, reverb.gainhf, REVERB, GAINHF);
			APPLY_EFFECT(alEffectf, reverb.decay_time, REVERB, DECAY_TIME);
			APPLY_EFFECT(alEffectf, reverb.decay_hfratio, REVERB, DECAY_HFRATIO);
			APPLY_EFFECT(alEffectf, reverb.reflections_gain, REVERB, REFLECTIONS_GAIN);
			APPLY_EFFECT(alEffectf, reverb.reflections_delay, REVERB, REFLECTIONS_DELAY);
			APPLY_EFFECT(alEffectf, reverb.late_reverb_gain, REVERB, LATE_REVERB_GAIN);
			APPLY_EFFECT(alEffectf, reverb.late_reverb_delay, REVERB, LATE_REVERB_DELAY);
			APPLY_EFFECT(alEffectf, reverb.air_absorption_gainhf, REVERB, AIR_ABSORPTION_GAINHF);
			APPLY_EFFECT_BOOL(reverb.decay_hflimit, REVERB, DECAY_HFLIMIT);
			break;
		case EFFECT_CHORUS:
			TRY(alEffecti, effect, AL_EFFECT_TYPE, AL_EFFECT_CHORUS);

			APPLY_EFFECT(alEffecti, chorus.waveform, CHORUS, WAVEFORM);
			APPLY_EFFECT(alEffecti, chorus.phase, CHORUS, PHASE);
			APPLY_EFFECT(alEffectf, chorus.rate, CHORUS, RATE);
			APPLY_EFFECT(alEffectf, chorus.depth, CHORUS, DEPTH);
			APPLY_EFFECT(alEffectf, chorus.feedback, CHORUS, FEEDBACK);
			APPLY_EFFECT(alEffectf, chorus.delay, CHORUS, DELAY);
			break;
		case EFFECT_DISTORTION:
			TRY(alEffecti, effect, AL_EFFECT_TYPE, AL_EFFECT_DISTORTION);

			APPLY_EFFECT(alEffectf, distortion.edge, DISTORTION, EDGE);
			APPLY_EFFECT(alEffectf, distortion.gain, DISTORTION, GAIN);
			APPLY_EFFECT(alEffectf, distortion.lowpass_cutoff, DISTORTION, LOWPASS_CUTOFF);
			APPLY_EFFECT(alEffectf, distortion.eqcenter, DISTORTION, EQCENTER);
			APPLY_EFFECT(alEffectf, distortion.eqbandwidth, DISTORTION, EQBANDWIDTH);
			break;
		case EFFECT_ECHO:
			TRY(alEffecti, effect, AL_EFFECT_TYPE, AL_EFFECT_ECHO);

			APPLY_EFFECT(alEffectf, echo.delay, ECHO, DELAY);
			APPLY_EFFECT(alEffectf, echo.lrdelay, ECHO, LRDELAY);
			APPLY_EFFECT(alEffectf, echo.damping, ECHO, DAMPING);
			APPLY_EFFECT(alEffectf, echo.feedback, ECHO, FEEDBACK);
			APPLY_EFFECT(alEffectf, echo.spread, ECHO, SPREAD);
			break;
		case EFFECT_FLANGER:
			TRY(alEffecti, effect, AL_EFFECT_TYPE, AL_EFFECT_FLANGER);

			APPLY_EFFECT(alEffecti, flanger.waveform, FLANGER, WAVEFORM);
			APPLY_EFFECT(alEffecti, flanger.phase, FLANGER, PHASE);
			APPLY_EFFECT(alEffectf, flanger.rate, FLANGER, RATE);
			APPLY_EFFECT(alEffectf, flanger.depth, FLANGER, DEPTH);
			APPLY_EFFECT(alEffectf, flanger.feedback, FLANGER, FEEDBACK);
			APPLY_EFFECT(alEffectf, flanger.delay, FLANGER, DELAY);
			break;
		case EFFECT_FREQSHIFT:
			TRY(alEffecti, effect, AL_EFFECT_TYPE, AL_EFFECT_FREQUENCY_SHIFTER);

			APPLY_EFFECT(alEffectf, freqshift.frequency, FREQUENCY_SHIFTER, FREQUENCY);
			APPLY_EFFECT(alEffecti, freqshift.left_direction, FREQUENCY_SHIFTER, LEFT_DIRECTION);
			APPLY_EFFECT(alEffecti, freqshift.right_direction, FREQUENCY_SHIFTER, RIGHT_DIRECTION);
			break;
		case EFFECT_VOCALMORPH:
			TRY(alEffecti, effect, AL_EFFECT_TYPE, AL_EFFECT_VOCAL_MORPHER);

			APPLY_EFFECT(alEffecti, vocal_morpher.phonemea, VOCAL_MORPHER, PHONEMEA);
			APPLY_EFFECT(alEffecti, vocal_morpher.phonemeb, VOCAL_MORPHER, PHONEMEB);
			APPLY_EFFECT(alEffecti, vocal_morpher.phonemea_coarse_tuning, VOCAL_MORPHER, PHONEMEA_COARSE_TUNING);
			APPLY_EFFECT(alEffecti, vocal_morpher.phonemeb_coarse_tuning, VOCAL_MORPHER, PHONEMEB_COARSE_TUNING);
			APPLY_EFFECT(alEffecti, vocal_morpher.waveform, VOCAL_MORPHER, WAVEFORM);
			APPLY_EFFECT(alEffectf, vocal_morpher.rate, VOCAL_MORPHER, RATE);
			break;
		case EFFECT_PITCHSHIFT:
			TRY(alEffecti, effect, AL_EFFECT_TYPE, AL_EFFECT_PITCH_SHIFTER);

			APPLY_EFFECT(alEffecti, pitchshift.coarse_tune, PITCH_SHIFTER, COARSE_TUNE);
			APPLY_EFFECT(alEffecti, pitchshift.fine_tune, PITCH_SHIFTER, FINE_TUNE);
			break;
		case EFFECT_RINGMOD:
			TRY(alEffecti, effect, AL_EFFECT_TYPE, AL_EFFECT_RING_MODULATOR);

			APPLY_EFFECT(alEffectf, ringmod.frequency, RING_MODULATOR, FREQUENCY);
			APPLY_EFFECT(alEffectf, ringmod.highpass_cutoff, RING_MODULATOR, HIGHPASS_CUTOFF);
			APPLY_EFFECT(alEffecti, ringmod.waveform, RING_MODULATOR, WAVEFORM);
			break;
		case EFFECT_AUTOWAH:
			TRY(alEffecti, effect, AL_EFFECT_TYPE, AL_EFFECT_AUTOWAH);

			APPLY_EFFECT(alEffectf, autowah.attack_time, AUTOWAH, ATTACK_TIME);
			APPLY_EFFECT(alEffectf, autowah.release_time, AUTOWAH, RELEASE_TIME);
			APPLY_EFFECT(alEffectf, autowah.resonance, AUTOWAH, RESONANCE);
			APPLY_EFFECT(alEffectf, autowah.peak_gain, AUTOWAH, PEAK_GAIN);
			break;
		case EFFECT_COMPRESSOR:
			TRY(alEffecti, effect, AL_EFFECT_TYPE, AL_EFFECT_COMPRESSOR);

			APPLY_EFFECT(alEffectf, compressor.onoff, COMPRESSOR, ONOFF);
			break;
		case EFFECT_EQ:
			TRY(alEffecti, effect, AL_EFFECT_TYPE, AL_EFFECT_EQUALIZER);

			APPLY_EFFECT(alEffectf, eq.low_gain, EQUALIZER, LOW_GAIN);
			APPLY_EFFECT(alEffectf, eq.low_cutoff, EQUALIZER, LOW_CUTOFF);
			APPLY_EFFECT(alEffectf, eq.mid1_gain, EQUALIZER, MID1_GAIN);
			APPLY_EFFECT(alEffectf, eq.mid1_center, EQUALIZER, MID1_CENTER);
			APPLY_EFFECT(alEffectf, eq.mid1_width, EQUALIZER, MID1_WIDTH);
			APPLY_EFFECT(alEffectf, eq.mid2_gain, EQUALIZER, MID2_GAIN);
			APPLY_EFFECT(alEffectf, eq.mid2_center, EQUALIZER, MID2_CENTER);
			APPLY_EFFECT(alEffectf, eq.mid2_width, EQUALIZER, MID2_WIDTH);
			APPLY_EFFECT(alEffectf, eq.high_gain, EQUALIZER, HIGH_GAIN);
			APPLY_EFFECT(alEffectf, eq.high_cutoff, EQUALIZER, HIGH_CUTOFF);
			break;
	}
}

#undef APPLY_EFFECT
#undef APPLY_EFFECT_FVEC3

efx_t *I_CreateEFX(efx_t *efx)
{
	LOCKAUDIO;

	if (!cv_soundefx.value || dedicated)
		return NULL;

	efx_t *efxc = Z_Calloc(sizeof(efx_t), PU_STATIC, NULL);

	CONS_Debug(DBG_EFX, "I_CreateEFX: Attempting to allocate new EFX.\n");

	if (efxc == NULL)
		I_Error("OpenAL: Ran out of memory generating EFX.\n");

	if (efx != NULL)
	{
		memcpy(efxc, efx, sizeof(efx_t));
		CONS_Debug(DBG_EFX, "I_CreateEFX: Given EFX exists, attempt to copy its data over.\n");
	}

	// Init effects and slots.
	CONS_Debug(DBG_EFX, "I_CreateEFX: Generating Effect and Slot.\n");
	TRY(alGenEffects, 1, &efxc->al.effect);
	TRY(alGenAuxiliaryEffectSlots, 1, &efxc->al.slot);

	if (TRY(alIsEffect, efxc->al.effect) && TRY(alIsAuxiliaryEffectSlot, efxc->al.slot))
	{
		CONS_Debug(DBG_EFX, "I_CreateEFX: Effect and Slot successfully created.\n");
		I_HandleEFXType(efxc, efxc->al.effect);

		TRY(alAuxiliaryEffectSloti, efxc->al.slot, AL_EFFECTSLOT_EFFECT, efxc->al.effect);
		return efxc;
	}

	CONS_Debug(DBG_EFX, "I_CreateEFX: Effect or Slot failed to be created, freeing.\n");
	Z_Free(efxc);

	return NULL;
}

float I_GetEFXTail(efx_t *efx)
{
	LOCKAUDIO;

	if (!cv_soundefx.value || dedicated)
		return 0;

	if (efx == NULL)
		return 0;

	float tailtime = 0;
	switch(efx->type)
	{
		case EFFECT_EAXREVERB:
			tailtime = efx->eaxreverb.decay_time;
			break;

		case EFFECT_REVERB:
			tailtime = efx->reverb.decay_time;
			break;

		default:
			tailtime = 0;
			break;
	}

	CONS_Debug(DBG_EFX, "I_GetEFXTail: EFX tail is %f aka %f.\n", tailtime, tailtime*TICRATE);
	return tailtime*TICRATE;
}

void I_DeleteEFX(efx_t *efx)
{
	LOCKAUDIO;

	if (efx == NULL)
		return;

	if (TRY(alIsEffect, efx->al.effect))
	{
		CONS_Debug(DBG_EFX, "I_DeleteEFX: Effect exists, attempting to delete.\n");
		TRY(alDeleteEffects, 1, &efx->al.effect);
	}

	if (TRY(alIsAuxiliaryEffectSlot, efx->al.slot))
	{
		CONS_Debug(DBG_EFX, "I_DeleteEFX: Slot exists, attempting to delete.\n");
		TRY(alDeleteAuxiliaryEffectSlots, 1, &efx->al.slot);
	}

	CONS_Debug(DBG_EFX, "I_DeleteEFX: Freeing allocated memory.\n");
	Z_Free(efx);
}

void I_CreateGlobalEFX(efx_t *efx)
{
	LOCKAUDIO;

	if (!cv_soundefx.value || dedicated)
		return;

	if (mapheaderinfo[gamemap-1]->globalEFX->type == EFFECT_NONE)
		return;

	if (efx == NULL)
		return;

	// Init effects and slots.
	CONS_Debug(DBG_EFX, "I_CreateGlobalEFX: Generating Effect and Slot.\n");
	TRY(alGenEffects, 1, &audio.globalefx.effect);
	TRY(alGenAuxiliaryEffectSlots, 1, &audio.globalefx.slot);

	if (TRY(alIsEffect, audio.globalefx.effect) && TRY(alIsAuxiliaryEffectSlot, audio.globalefx.slot))
	{
		CONS_Debug(DBG_EFX, "I_CreateGlobalEFX: Effect and Slot successfully created.\n");
		I_HandleEFXType(efx, audio.globalefx.effect);
		TRY(alAuxiliaryEffectSloti, audio.globalefx.slot, AL_EFFECTSLOT_EFFECT, audio.globalefx.effect);

		CONS_Debug(DBG_EFX, "I_CreateGlobalEFX: Global EFX enabled.\n");
		audio.globalefx.enabled = true;
		return;
	}

	CONS_Debug(DBG_EFX, "I_CreateEFX: Effect or Slot failed to be created.\n");

	if (TRY(alIsEffect, audio.globalefx.effect))
	{
		CONS_Debug(DBG_EFX, "I_CreateGlobalEFX: Effect exists, attempting to delete.\n");
		TRY(alDeleteEffects, 1, &audio.globalefx.effect);
	}

	if (TRY(alIsAuxiliaryEffectSlot, audio.globalefx.slot))
	{
		CONS_Debug(DBG_EFX, "I_CreateGlobalEFX: Slot exists, attempting to delete.\n");
		TRY(alDeleteAuxiliaryEffectSlots, 1, &audio.globalefx.slot);
	}

	CONS_Debug(DBG_EFX, "I_CreateGlobalEFX: Global EFX disabled.\n");
	audio.globalefx.enabled = false;
}

void I_DeleteGlobalEFX(void)
{
	LOCKAUDIO;

	if (audio.globalefx.enabled == false)
		return;

	if (TRY(alIsEffect, audio.globalefx.effect))
	{
		CONS_Debug(DBG_EFX, "I_DeleteGlobalEFX: Effect exists, attempting to delete.\n");
		TRY(alDeleteEffects, 1, &audio.globalefx.effect);
	}

	if (TRY(alIsAuxiliaryEffectSlot, audio.globalefx.slot))
	{
		CONS_Debug(DBG_EFX, "I_DeleteGlobalEFX: Slot exists, attempting to delete.\n");
		TRY(alDeleteAuxiliaryEffectSlots, 1, &audio.globalefx.slot);
	}

	CONS_Debug(DBG_EFX, "I_DeleteGlobalEFX: Global EFX disabled.\n");
	audio.globalefx.enabled = false;
}

#define RESET_EFFECT(field, altype, alfield) efx->field = AL_##altype##_DEFAULT_##alfield; \
CONS_Debug(DBG_EFX, VAL(AL_##altype##_##alfield) ": %f\n", (float)efx->field)

#define RESET_EFFECT_FVEC3(field, altype, alfield) efx->field.x = AL_##altype##_DEFAULT_##alfield##_XYZ; \
efx->field.y = AL_##altype##_DEFAULT_##alfield##_XYZ; \
efx->field.z = AL_##altype##_DEFAULT_##alfield##_XYZ; \
CONS_Debug(DBG_EFX, VAL(AL_##altype##_##alfield) ": x=%f y=%f z=%f\n", efx->field.x, efx->field.y, efx->field.z)

void I_InitEFXArray(efx_t *efx)
{
	if (efx == NULL)
		return;

	efx->type = EFFECT_NONE;

	CONS_Debug(DBG_EFX, "I_InitEFXArray: Resetting EFX array to defaults.\n");

	RESET_EFFECT(eaxreverb.density, EAXREVERB, DENSITY);
	RESET_EFFECT(eaxreverb.diffusion, EAXREVERB, DIFFUSION);
	RESET_EFFECT(eaxreverb.gain, EAXREVERB, GAIN);
	RESET_EFFECT(eaxreverb.gainhf, EAXREVERB, GAINHF);
	RESET_EFFECT(eaxreverb.gainlf, EAXREVERB, GAINLF);
	RESET_EFFECT(eaxreverb.decay_time, EAXREVERB, DECAY_TIME);
	RESET_EFFECT(eaxreverb.decay_hfratio, EAXREVERB, DECAY_HFRATIO);
	RESET_EFFECT(eaxreverb.decay_lfratio, EAXREVERB, DECAY_LFRATIO);
	RESET_EFFECT(eaxreverb.reflections_gain, EAXREVERB, REFLECTIONS_GAIN);
	RESET_EFFECT(eaxreverb.reflections_delay, EAXREVERB, REFLECTIONS_DELAY);
	RESET_EFFECT_FVEC3(eaxreverb.reflections_pan, EAXREVERB, REFLECTIONS_PAN);
	RESET_EFFECT(eaxreverb.late_reverb_gain, EAXREVERB, LATE_REVERB_GAIN);
	RESET_EFFECT(eaxreverb.late_reverb_delay, EAXREVERB, LATE_REVERB_DELAY);
	RESET_EFFECT_FVEC3(eaxreverb.late_reverb_pan, EAXREVERB, LATE_REVERB_PAN);
	RESET_EFFECT(eaxreverb.echo_time, EAXREVERB, ECHO_TIME);
	RESET_EFFECT(eaxreverb.echo_depth, EAXREVERB, ECHO_DEPTH);
	RESET_EFFECT(eaxreverb.modulation_time, EAXREVERB, MODULATION_TIME);
	RESET_EFFECT(eaxreverb.modulation_depth, EAXREVERB, MODULATION_DEPTH);
	RESET_EFFECT(eaxreverb.air_absorption_gainhf, EAXREVERB, AIR_ABSORPTION_GAINHF);
	RESET_EFFECT(eaxreverb.hfreference, EAXREVERB, HFREFERENCE);
	RESET_EFFECT(eaxreverb.lfreference, EAXREVERB, LFREFERENCE);
	RESET_EFFECT(eaxreverb.room_rolloff_factor, EAXREVERB, ROOM_ROLLOFF_FACTOR);
	RESET_EFFECT(eaxreverb.decay_hflimit, EAXREVERB, DECAY_HFLIMIT);

	RESET_EFFECT(reverb.density, REVERB, DENSITY);
	RESET_EFFECT(reverb.diffusion, REVERB, DIFFUSION);
	RESET_EFFECT(reverb.gain, REVERB, GAIN);
	RESET_EFFECT(reverb.gainhf, REVERB, GAINHF);
	RESET_EFFECT(reverb.decay_time, REVERB, DECAY_TIME);
	RESET_EFFECT(reverb.decay_hfratio, REVERB, DECAY_HFRATIO);
	RESET_EFFECT(reverb.reflections_gain, REVERB, REFLECTIONS_GAIN);
	RESET_EFFECT(reverb.reflections_delay, REVERB, REFLECTIONS_DELAY);
	RESET_EFFECT(reverb.late_reverb_gain, REVERB, LATE_REVERB_GAIN);
	RESET_EFFECT(reverb.late_reverb_delay, REVERB, LATE_REVERB_DELAY);
	RESET_EFFECT(reverb.air_absorption_gainhf, REVERB, AIR_ABSORPTION_GAINHF);
	RESET_EFFECT(reverb.decay_hflimit, REVERB, DECAY_HFLIMIT);

	RESET_EFFECT(chorus.waveform, CHORUS, WAVEFORM);
	RESET_EFFECT(chorus.phase, CHORUS, PHASE);
	RESET_EFFECT(chorus.rate, CHORUS, RATE);
	RESET_EFFECT(chorus.depth, CHORUS, DEPTH);
	RESET_EFFECT(chorus.feedback, CHORUS, FEEDBACK);
	RESET_EFFECT(chorus.delay, CHORUS, DELAY);

	RESET_EFFECT(distortion.edge, DISTORTION, EDGE);
	RESET_EFFECT(distortion.gain, DISTORTION, GAIN);
	RESET_EFFECT(distortion.lowpass_cutoff, DISTORTION, LOWPASS_CUTOFF);
	RESET_EFFECT(distortion.eqcenter, DISTORTION, EQCENTER);
	RESET_EFFECT(distortion.eqbandwidth, DISTORTION, EQBANDWIDTH);

	RESET_EFFECT(echo.delay, ECHO, DELAY);
	RESET_EFFECT(echo.lrdelay, ECHO, LRDELAY);
	RESET_EFFECT(echo.damping, ECHO, DAMPING);
	RESET_EFFECT(echo.feedback, ECHO, FEEDBACK);
	RESET_EFFECT(echo.spread, ECHO, SPREAD);

	RESET_EFFECT(flanger.waveform, FLANGER, WAVEFORM);
	RESET_EFFECT(flanger.phase, FLANGER, PHASE);
	RESET_EFFECT(flanger.rate, FLANGER, RATE);
	RESET_EFFECT(flanger.depth, FLANGER, DEPTH);
	RESET_EFFECT(flanger.feedback, FLANGER, FEEDBACK);
	RESET_EFFECT(flanger.delay, FLANGER, DELAY);

	RESET_EFFECT(freqshift.frequency, FREQUENCY_SHIFTER, FREQUENCY);
	RESET_EFFECT(freqshift.left_direction, FREQUENCY_SHIFTER, LEFT_DIRECTION);
	RESET_EFFECT(freqshift.right_direction, FREQUENCY_SHIFTER, RIGHT_DIRECTION);

	RESET_EFFECT(vocal_morpher.phonemea, VOCAL_MORPHER, PHONEMEA);
	RESET_EFFECT(vocal_morpher.phonemeb, VOCAL_MORPHER, PHONEMEB);
	RESET_EFFECT(vocal_morpher.phonemea_coarse_tuning, VOCAL_MORPHER, PHONEMEA_COARSE_TUNING);
	RESET_EFFECT(vocal_morpher.phonemeb_coarse_tuning, VOCAL_MORPHER, PHONEMEB_COARSE_TUNING);
	RESET_EFFECT(vocal_morpher.waveform, VOCAL_MORPHER, WAVEFORM);
	RESET_EFFECT(vocal_morpher.rate, VOCAL_MORPHER, RATE);

	RESET_EFFECT(pitchshift.coarse_tune, PITCH_SHIFTER, COARSE_TUNE);
	RESET_EFFECT(pitchshift.fine_tune, PITCH_SHIFTER, FINE_TUNE);

	RESET_EFFECT(ringmod.frequency, RING_MODULATOR, FREQUENCY);
	RESET_EFFECT(ringmod.highpass_cutoff, RING_MODULATOR, HIGHPASS_CUTOFF);
	RESET_EFFECT(ringmod.waveform, RING_MODULATOR, WAVEFORM);

	RESET_EFFECT(autowah.attack_time, AUTOWAH, ATTACK_TIME);
	RESET_EFFECT(autowah.release_time, AUTOWAH, RELEASE_TIME);
	RESET_EFFECT(autowah.resonance, AUTOWAH, RESONANCE);
	RESET_EFFECT(autowah.peak_gain, AUTOWAH, PEAK_GAIN);

	RESET_EFFECT(compressor.onoff, COMPRESSOR, ONOFF);

	RESET_EFFECT(eq.low_gain, EQUALIZER, LOW_GAIN);
	RESET_EFFECT(eq.low_cutoff, EQUALIZER, LOW_CUTOFF);
	RESET_EFFECT(eq.mid1_gain, EQUALIZER, MID1_GAIN);
	RESET_EFFECT(eq.mid1_center, EQUALIZER, MID1_CENTER);
	RESET_EFFECT(eq.mid1_width, EQUALIZER, MID1_WIDTH);
	RESET_EFFECT(eq.mid2_gain, EQUALIZER, MID2_GAIN);
	RESET_EFFECT(eq.mid2_center, EQUALIZER, MID2_CENTER);
	RESET_EFFECT(eq.mid2_width, EQUALIZER, MID2_WIDTH);
	RESET_EFFECT(eq.high_gain, EQUALIZER, HIGH_GAIN);
	RESET_EFFECT(eq.high_cutoff, EQUALIZER, HIGH_CUTOFF);
}

#undef RESET_EFFECT
#undef RESET_EFFECT_FVEC3
#undef VAL

#endif // defined(HAVE_OPENAL)
