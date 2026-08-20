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
/// \file i_sound.c
/// \brief dummy interface for sound

#include "../i_sound.h"


uint8_t sound_started = false;

consvar_t cv_soundefx = CVAR_INIT ("soundefx", "On", CV_SAVE, CV_OnOff, NULL);

void I_StartupSound(void)
{

}

void I_ShutdownSound(void)
{

}

void I_UpdateSound(void)
{

}

void *I_GetSfx(sfxinfo_t *sfx)
{
	(void)sfx;

	return NULL;
}

void I_FreeSfx(sfxinfo_t *sfx)
{
	(void)sfx;
}

#ifdef DUALSNDBUFFER
void I_UpdateSoundParams(int32_t handle, uint8_t vol, uint8_t sep, uint8_t pitch, efx_t *efx, const void *origin, int8_t direction)
#else
void I_UpdateSoundParams(int32_t handle, uint8_t vol, uint8_t sep, uint8_t pitch, efx_t *efx, const void *origin)
#endif
{
	(void)handle;
	(void)vol;
	(void)sep;
	(void)pitch;
	(void)efx;
	(void)origin;
#ifdef DUALSNDBUFFER
	(void)direction;
#endif
}

#ifdef DUALSNDBUFFER
void I_StartSound(sfxenum_t id, uint8_t vol, uint8_t sep, uint8_t pitch, uint8_t priority, int32_t channel, efx_t *efx, const void *origin, int32_t *handleL, int32_t *handleR)
#else
int32_t I_StartSound(sfxenum_t id, uint8_t vol, uint8_t sep, uint8_t pitch, uint8_t priority, int32_t channel, efx_t *efx, const void *origin)
#endif
{
	(void)id;
	(void)vol;
	(void)sep;
	(void)pitch;
	(void)priority;
	(void)channel;
	(void)efx;
	(void)origin;
#ifdef DUALSNDBUFFER
	(void)handleL;
	(void)handleR;
#else
	return 0;
#endif
}

void I_StopSound(int32_t handle)
{
	(void)handle;
}

dboolean I_SoundIsPlaying(int32_t handle)
{
	(void)handle;
	return false;
}

void I_SetSfxVolume(uint8_t volume)
{
	(void)volume;
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
	return MU_NONE;
}

dboolean I_SongPlaying(void)
{
	return true;
}

dboolean I_SongPaused(void)
{
	return false;
}

dboolean I_SetSongSpeed(float speed)
{
	(void)speed;
	return false;
}

uint32_t I_GetSongLength(void)
{
	return 0;
}

dboolean I_SetSongLoopPoint(uint32_t looppoint)
{
	(void)looppoint;
	return false;
}

uint32_t I_GetSongLoopPoint(void)
{
	return 0;
}

dboolean I_SetSongPosition(uint32_t position)
{
	(void)position;
	return false;
}

uint32_t I_GetSongPosition(void)
{
	return 0;
}


dboolean I_LoadSong(char *data, size_t len)
{
	(void)data;
	(void)len;
	return false;
}

void I_UnloadSong(void)
{

}

dboolean I_PlaySong(dboolean looping)
{
	(void)looping;
	return false;
}

void I_StopSong(void)
{

}

void I_PauseSong(void)
{

}

void I_ResumeSong(void)
{

}

void I_SetMusicVolume(uint8_t volume)
{
	(void)volume;
}

dboolean I_SetSongTrack(int32_t track)
{
	(void)track;
	return false;
}

void I_UpdateSongLagThreshold(void){}
void I_UpdateSongLagConditions(void){}

void I_SetInternalMusicVolume(uint8_t volume)
{
	(void)volume;
}

void I_StopFadingSong(void)
{

}

dboolean I_FadeSongFromVolume(uint8_t target_volume, uint8_t source_volume, uint32_t ms, void (*callback)(void))
{
	(void)target_volume;
	(void)source_volume;
	(void)ms;
	(void)callback;
	return false;
}

dboolean I_FadeSong(uint8_t target_volume, uint32_t ms, void (*callback)(void))
{
	(void)target_volume;
	(void)ms;
	(void)callback;
	return false;
}

dboolean I_FadeOutStopSong(uint32_t ms)
{
	(void)ms;
	return false;
}

dboolean I_FadeInPlaySong(uint32_t ms, dboolean looping)
{
	(void)ms;
	(void)looping;
	return false;
}


efx_t *I_CreateEFX(efx_t *efx)
{
	(void)efx;
	return NULL;
}

float I_GetEFXTail(efx_t *efx)
{
	(void)efx;
	return 0;
}

void I_DeleteEFX(efx_t *efx)
{
	(void)efx;
}

void I_CreateGlobalEFX(efx_t *efx)
{
	(void)efx;
}

void I_DeleteGlobalEFX(void)
{

}

void I_InitEFXArray(efx_t *efx)
{
	(void)efx;
}

void I_RegisterSoundStuff(void)
{

}

dboolean I_3DSound(void)
{
	return false;
}

void I_UpdateSpeedofSoundScale(fixed_t scale)
{

}
