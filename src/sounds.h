// SONIC ROBO BLAST 2
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

// Customisable sounds for Skins
typedef enum
{
	// SRB2kart
	SKSKWIN, // Win quote
	SKSKLOSE, // Lose quote
	SKSKPAN1, // Pain
	SKSKPAN2,
	SKSKATK1, // Offense item taunt
	SKSKATK2,
	SKSKBST1, // Boost item taunt
	SKSKBST2,
	SKSKSLOW, // Overtake taunt
	SKSKHITM, // Hit confirm taunt
	SKSKPOWR, // Power item taunt
	NUMSKINSOUNDS
} skinsound_t;

// free sfx for S_AddSoundFx()
#define NUMSFXFREESLOTS 2500 // Matches SOC Editor.
#define NUMSKINSFXSLOTS (MAXSKINS*NUMSKINSOUNDS)

//
// SoundFX struct.
//

struct sfxinfo_t
{
	// up to 6-character name
	const char *name;

	// Sfx singularity (only one at a time)
	boolean singularity;

	// Sfx priority
	INT32 priority;

	// pitch if a link
	INT32 pitch;

	// volume if a link
	INT32 volume;

	// sound data
	void *data;

	// length of sound data
	size_t length;

	// sound that can be remapped for a skin, indexes skins[].skinsounds
	// 0 up to (NUMSKINSOUNDS-1), -1 = not skin specifc
	INT32 skinsound;

	// this is checked every second to see if sound
	// can be thrown out (if 0, then decrement, if -1,
	// then throw out, if > 0, then it is in use)
	INT32 usefulness;

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
sfxenum_t S_AddSoundFx(const char *name, boolean singular, INT32 flags, boolean skinsound);
extern sfxenum_t sfxfree; // sound test and slotting
void S_RemoveSoundFx(sfxenum_t id);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
