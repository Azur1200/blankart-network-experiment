// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  i_sound.h
/// \brief System interface, sound, music

#ifndef __I_SOUND__
#define __I_SOUND__

#include "doomdef.h"
#include "sounds.h"
#include "command.h"
#include "m_fixed.h"

#ifdef HAVE_OPENAL
#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"
#include "AL/efx.h"
#include "AL/efx-presets.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

// copied from SDL mixer, plus GME
typedef enum {
	MU_NONE,
	MU_WAV,
	MU_MOD,
	MU_MID,
	MU_OGG,
	MU_OPUS,
	MU_MP3,
	MU_FLAC,
	MU_GME,
	MU_MOD_EX, // libopenmpt
	MU_MID_EX, // Non-native MIDI
} musictype_t;

typedef enum
{
	EFFECT_NONE = 0,
	EFFECT_EAXREVERB,
	EFFECT_REVERB,
	EFFECT_CHORUS,
	EFFECT_DISTORTION,
	EFFECT_ECHO,
	EFFECT_FLANGER,
	EFFECT_FREQSHIFT,
	EFFECT_VOCALMORPH,
	EFFECT_PITCHSHIFT,
	EFFECT_RINGMOD,
	EFFECT_AUTOWAH,
	EFFECT_COMPRESSOR,
	EFFECT_EQ,
} effecttypes_t;

typedef enum
{
	CHORUSWAVE_SINUSOID = 0,
	CHORUSWAVE_TRIANGLE = 1,
} choruswaves_t;

typedef enum
{
	FLANGERWAVE_SINUSOID = 0,
	FLANGERWAVE_TRIANGLE = 1,
} flangerwaves_t;

typedef enum
{
	FREQSHIFTDIR_DOWN = 0,
	FREQSHIFTDIR_UP = 1,
	FREQSHIFTDIR_OFF = 2,
} freqshiftdirection_t;

typedef enum
{
	VOCALMORPH_SIN = 0,
	VOCALMORPH_TRIANGLE = 1,
	VOCALMORPH_SAW = 2,
} vocalmorphwaves_t;

typedef enum
{
	RINGMOD_SIN = 0,
	RINGMOD_SAW = 1,
	RINGMOD_SQUARE = 2,
} ringmodwaves_t;

typedef struct
{
#ifdef HAVE_OPENAL
	struct
	{
		ALuint effect;
		ALuint slot;
	} al;
#endif

	uint32_t type;

	struct
	{
		float density;
		float diffusion;
		float gain;
		float gainhf;
		float gainlf;
		float decay_time;
		float decay_hfratio;
		float decay_lfratio;
		float reflections_gain;
		float reflections_delay;
		f_vector3_t reflections_pan;
		float late_reverb_gain;
		float late_reverb_delay;
		f_vector3_t late_reverb_pan;
		float echo_time;
		float echo_depth;
		float modulation_time;
		float modulation_depth;
		float air_absorption_gainhf;
		float hfreference;
		float lfreference;
		float room_rolloff_factor;
		dboolean decay_hflimit;
	} eaxreverb;

	struct
	{
		float density;
		float diffusion;
		float gain;
		float gainhf;
		float decay_time;
		float decay_hfratio;
		float reflections_gain;
		float reflections_delay;
		float late_reverb_gain;
		float late_reverb_delay;
		float air_absorption_gainhf;
		float room_rolloff_factor;
		dboolean decay_hflimit;
	} reverb;

	struct
	{
		int8_t waveform;
		int16_t phase;
		float rate;
		float depth;
		float feedback;
		float delay;
	} chorus;

	struct
	{
		float edge;
		float gain;
		float lowpass_cutoff;
		float eqcenter;
		float eqbandwidth;
	} distortion;

	struct
	{
		float delay;
		float lrdelay;
		float damping;
		float feedback;
		float spread;
	} echo;

	struct
	{
		int8_t waveform;
		int16_t phase;
		float rate;
		float depth;
		float feedback;
		float delay;
	} flanger;

	struct
	{
		float frequency;
		int8_t left_direction;
		int8_t right_direction;
	} freqshift;

	struct
	{
		int8_t phonemea;
		int8_t phonemeb;
		int8_t phonemea_coarse_tuning;
		int8_t phonemeb_coarse_tuning;
		int8_t waveform;
		float rate;
	} vocal_morpher;

	struct
	{
		int8_t coarse_tune;
		int8_t fine_tune;
	} pitchshift;

	struct
	{
		float frequency;
		float highpass_cutoff;
		int8_t waveform;
	} ringmod;

	struct
	{
		float attack_time;
		float release_time;
		float resonance;
		float peak_gain;
	} autowah;

	struct
	{
		dboolean onoff;
	} compressor;

	struct
	{
		float low_gain;
		float low_cutoff;
		float mid1_gain;
		float mid1_center;
		float mid1_width;
		float mid2_gain;
		float mid2_center;
		float mid2_width;
		float high_gain;
		float high_cutoff;
	} eq;
} efx_t;

/**	\brief Sound subsystem runing and waiting
*/
extern uint8_t sound_started;

/**	\brief info of samplerate
*/
extern consvar_t cv_samplerate;
//extern consvar_t cv_rndsoundpitch;

/**	\brief	The I_GetSfx function

	\param	sfx	sfx to setup

	\return	data for sfx
*/
void *I_GetSfx(sfxinfo_t *sfx);

/**	\brief	The I_FreeSfx function

	\param	sfx	sfx to be freed up

	\return	void
*/
void I_FreeSfx(sfxinfo_t *sfx);

/**	\brief Init at program start...
*/
void I_StartupSound(void);

/**	\brief ... shut down and relase at program termination.
*/
void I_ShutdownSound(void);

/// ------------------------
///  SFX I/O
/// ------------------------

/**	\brief	Starts a sound in a particular sound channel.
	\param	id	sfxid
	\param	vol	volume for sound
	\param	sep	left-right balancle
	\param	pitch	not used
	\param	priority	not used

	\return	sfx handle
*/

#ifdef DUALSNDBUFFER
void I_StartSound(sfxenum_t id, uint8_t vol, uint8_t sep, uint8_t pitch, uint8_t priority, int32_t channel, efx_t *efx, const void *origin, int32_t *handleL, int32_t *handleR);
#else
int32_t I_StartSound(sfxenum_t id, uint8_t vol, uint8_t sep, uint8_t pitch, uint8_t priority, int32_t channel, efx_t *efx, const void *origin);
#endif

/**	\brief	Stops a sound channel.

	\param	handle	stop sfx handle

	\return	void
*/
void I_StopSound(int32_t handle);

/**	\brief Some digital sound drivers need this.
*/
void I_UpdateSound(void);

/**	\brief	Called by S_*() functions to see if a channel is still playing.

	\param	handle	sfx handle

	\return	0 if no longer playing, 1 if playing.
*/
dboolean I_SoundIsPlaying(int32_t handle);



/**	\brief	Updates the sfx handle

	\param	handle	sfx handle
	\param	vol	volume
	\param	sep	separation
	\param	pitch	ptich

	\return	void
*/
void I_UpdateSoundParams(int32_t handle, uint8_t vol, uint8_t sep, uint8_t pitch, efx_t *efx, const void *origin);

/**	\brief	The I_SetSfxVolume function

	\param	volume	volume to set at

	\return	void
*/
void I_SetSfxVolume(uint8_t volume);

/// ------------------------
//  MUSIC SYSTEM
/// ------------------------

/** \brief Init the music systems
*/
void I_InitMusic(void);

/** \brief Shutdown the music systems
*/
void I_ShutdownMusic(void);

/// ------------------------
//  MUSIC PROPERTIES
/// ------------------------

musictype_t I_SongType(void);
dboolean I_SongPlaying(void);
dboolean I_SongPaused(void);

/// ------------------------
//  MUSIC EFFECTS
/// ------------------------

dboolean I_SetSongSpeed(float speed);

/// ------------------------
//  MUSIC SEEKING
/// ------------------------

uint32_t I_GetSongLength(void);

dboolean I_SetSongLoopPoint(uint32_t looppoint);
uint32_t I_GetSongLoopPoint(void);

dboolean I_SetSongPosition(uint32_t position);
uint32_t I_GetSongPosition(void);

void I_UpdateSongLagThreshold (void);
void I_UpdateSongLagConditions (void);

/// ------------------------
//  MUSIC PLAYBACK
/// ------------------------

/**	\brief	Registers a song handle to song data.

	\param	data	pointer to song data
	\param	len	len of data

	\return	song handle

	\todo Remove this
*/
dboolean I_LoadSong(char *data, size_t len);

/**	\brief	See ::I_LoadSong, then think backwards

	\param	handle	song handle

	\sa I_LoadSong
	\todo remove midi handle
*/
void I_UnloadSong(void);

/**	\brief	Called by anything that wishes to start music

	\param	handle	Song handle
	\param	looping	looping it if true

	\return	if true, it's playing the song

	\todo pass music name, not handle
*/
dboolean I_PlaySong(dboolean looping);

/**	\brief	Stops a song over 3 seconds

	\param	handle	Song handle
	\return	void

	/todo drop handle
*/
void I_StopSong(void);

/**	\brief	PAUSE game handling.

	\param	handle	song handle

	\return	void
*/
void I_PauseSong(void);

/**	\brief	RESUME game handling

	\param	handle	song handle

	\return	void
*/
void I_ResumeSong(void);

/**	\brief	The I_SetMusicVolume function

	\param	volume	volume to set at

	\return	void
*/
void I_SetMusicVolume(uint8_t volume);

dboolean I_SetSongTrack(int32_t track);

/// ------------------------
/// MUSIC FADING
/// ------------------------

void I_SetInternalMusicVolume(uint8_t volume);
void I_StopFadingSong(void);
dboolean I_FadeSongFromVolume(uint8_t target_volume, uint8_t source_volume, uint32_t ms, void (*callback)(void));
dboolean I_FadeSong(uint8_t target_volume, uint32_t ms, void (*callback)(void));
dboolean I_FadeOutStopSong(uint32_t ms);
dboolean I_FadeInPlaySong(uint32_t ms, dboolean looping);

efx_t *I_CreateEFX(efx_t *efx);
float I_GetEFXTail(efx_t *efx);
void I_DeleteEFX(efx_t *efx);

void I_CreateGlobalEFX(efx_t *efx);
void I_DeleteGlobalEFX(void);

void I_InitEFXArray(efx_t *efx);

extern consvar_t cv_soundefx;

#ifdef __cplusplus
} // extern "C"
#endif

#endif
