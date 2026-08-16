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
/// \file  sounds.h
/// \brief Sound and music info

#ifndef __SOUNDS__
#define __SOUNDS__

#include "doomdef.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DUALSNDBUFFER // use two handles per channel (OpenAL workaround :face_holding_back_tears:)

// keep this one around at least, for compatibility code
#define NUMSKINSOUNDS 11

// free sfx for S_AddSoundFx()
#define NUMSFXFREESLOTS 7000 // Matches SOC Editor.
#define NUMSKINSFXSLOTS (50000 - NUMSFXFREESLOTS)

//
// SoundFX struct.
//

struct sfxinfo_t
{
	// up to 6-character name
	const char *name;

	// Sfx singularity (only one at a time)
	dboolean singularity;

	// Sfx priority
	int32_t priority;

	// Sfx flags
	int32_t flags;

	// volume if a link
	int32_t volume;

	// sound data
	void *data;

#ifdef DUALSNDBUFFER
	dboolean stereo;
#endif

	// length of sound data
	size_t length;

	// this is checked every second to see if sound
	// can be thrown out (if 0, then decrement, if -1,
	// then throw out, if > 0, then it is in use)
	int32_t usefulness;

	// lump number of sfx
	lumpnum_t lumpnum;

	// closed caption info/wiki table bait
	char caption[32];
};

// the complete set of sound effects
extern sfxinfo_t S_sfx[];

//
// Identifiers for all sfx in game.
//

// List of sounds (don't modify this comment!)
typedef enum
{
#define _(name, ...) sfx_##name,
#include "info/sounds.h"
#undef _

	// free slots for S_AddSoundFx() at run-time --------------------
	sfx_freeslot0,
	//
	// ... 60 free sounds here ...
	//
	sfx_lastfreeslot = sfx_freeslot0 + NUMSFXFREESLOTS-1,
	// end of freeslots ---------------------------------------------

	sfx_skinsoundslot0,
	sfx_lastskinsoundslot = sfx_skinsoundslot0 + NUMSKINSFXSLOTS-1,
	NUMSFX
} sfxenum_t;


void S_InitRuntimeSounds(void);
sfxenum_t S_AddSoundFx(const char *name, dboolean skinsound);
extern sfxenum_t sfxfree, skinsfxfree; // sound test and slotting
void S_RemoveSoundFx(sfxenum_t id);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
