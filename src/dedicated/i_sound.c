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


UINT8 sound_started = false;

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

void I_UpdateSoundParams(INT32 handle, UINT8 vol, UINT8 sep, UINT8 pitch, efx_t *efx, const void *origin)
{
	(void)handle;
	(void)vol;
	(void)sep;
	(void)pitch;
	(void)efx;
	(void)origin;
}

INT32 I_StartSound(sfxenum_t id, UINT8 vol, UINT8 sep, UINT8 pitch, UINT8 priority, INT32 channel, efx_t *efx, const void * origin)
{
	(void)id;
	(void)vol;
	(void)sep;
	(void)pitch;
	(void)priority;
	(void)channel;
	(void)efx;
	(void)origin;
	return 0;
}

void I_StopSound(INT32 handle)
{
	(void)handle;
}

boolean I_SoundIsPlaying(INT32 handle)
{
	(void)handle;
	return false;
}

void I_SetSfxVolume(UINT8 volume)
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

boolean I_SongPlaying(void)
{
	return true;
}

boolean I_SongPaused(void)
{
	return false;
}

boolean I_SetSongSpeed(float speed)
{
	(void)speed;
	return false;
}

UINT32 I_GetSongLength(void)
{
	return 0;
}

boolean I_SetSongLoopPoint(UINT32 looppoint)
{
	(void)looppoint;
	return false;
}

UINT32 I_GetSongLoopPoint(void)
{
	return 0;
}

boolean I_SetSongPosition(UINT32 position)
{
	(void)position;
	return false;
}

UINT32 I_GetSongPosition(void)
{
	return 0;
}


boolean I_LoadSong(char *data, size_t len)
{
	(void)data;
	(void)len;
	return false;
}

void I_UnloadSong(void)
{

}

boolean I_PlaySong(boolean looping)
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

void I_SetMusicVolume(UINT8 volume)
{
	(void)volume;
}

boolean I_SetSongTrack(INT32 track)
{
	(void)track;
	return false;
}

void I_UpdateSongLagThreshold(void){}
void I_UpdateSongLagConditions(void){}

void I_SetInternalMusicVolume(UINT8 volume)
{
	(void)volume;
}

void I_StopFadingSong(void)
{

}

boolean I_FadeSongFromVolume(UINT8 target_volume, UINT8 source_volume, UINT32 ms, void (*callback)(void))
{
	(void)target_volume;
	(void)source_volume;
	(void)ms;
	(void)callback;
	return false;
}

boolean I_FadeSong(UINT8 target_volume, UINT32 ms, void (*callback)(void))
{
	(void)target_volume;
	(void)ms;
	(void)callback;
	return false;
}

boolean I_FadeOutStopSong(UINT32 ms)
{
	(void)ms;
	return false;
}

boolean I_FadeInPlaySong(UINT32 ms, boolean looping)
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
