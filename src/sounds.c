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
/// \file  sounds.c
/// \brief music/sound tables, and related sound routines

#include "doomtype.h"
#include "i_sound.h"
#include "sounds.h"
#include "r_defs.h"
#include "r_skins.h"
#include "z_zone.h"
#include "w_wad.h"
#include "lua_script.h"

//
// Information about all the sfx
//

sfxinfo_t S_sfx[NUMSFX] =
{

/*****
	Legacy doesn't use the PITCH variable, so now it is used for
	various flags. See soundflags_t.
*****/

  // A HUMBLE REQUEST FROM YOUR FRIENDLY NEIGHBORHOOD toaster!
  //
  // If you see a caption that's just "" (shows the lumpname in-game),
  // and you intend to use the sound associated with it in a mod,
  // PLEASE give it a caption through SOC or Lua.
  //
  // If the first character of the caption is '/', no caption will be
  // produced; only do  this for "unimportant" sounds that aren't used
  // to indicate gameplay.
  //
  // (to whomstever updates the sounds list wiki page for 2.2, please
  // either copy this comment across, or make sure its desire is
  // codified in the initial paragraph of the page.)
  //
  // Closed Captioning may be a niche feature, but it's an important one.
  // Thank you! ^u^

  // G: vanilla sounds are SOCced now but... eh, might as well keep this

  // skin sounds free slots to add sounds at run time (Boris HACK!!!)
  // initialized to NULL

  0
};

static char soundnames[NUMSFX][7] = {
#define _(name, ...) #name,
#include "info/sounds.h"
#undef _
};

// Prepare free sfx slots to add sfx at run time
void S_InitRuntimeSounds (void)
{
	sfxenum_t i;
	INT32 value;
	char soundname[10];

	for (i = 0; i < NUMSFX; i++)
	{
		if (i >= sfx_freeslot0)
		{
			value = (i+1) - sfx_freeslot0;
			sprintf(soundname, "fr%04x", value);
			strcpy(soundnames[i], soundname);
		}

		S_sfx[i].name = soundnames[i];
		S_sfx[i].singularity = false;
		S_sfx[i].priority = 0;
		S_sfx[i].flags = 0;
		S_sfx[i].volume = -1;
		S_sfx[i].data = NULL;
		S_sfx[i].length = 0;
		S_sfx[i].usefulness = -1;
		S_sfx[i].lumpnum = LUMPERROR;
		//strlcpy(S_sfx[i].caption, "", 1);
		S_sfx[i].caption[0] = '\0';
	}
}

sfxenum_t sfxfree = sfx_freeslot0, skinsfxfree = sfx_skinsoundslot0;

// Add a new sound fx into a free sfx slot.
//
sfxenum_t S_AddSoundFx(const char *name, dboolean skinsound)
{
	sfxenum_t i;

	if (skinsound)
	{
		if (skinsfxfree > sfx_lastskinsoundslot)
		{
			CONS_Alert(CONS_WARNING, M_GetText("No more free skin sound slots\n"));
			return sfx_None;
		}
		i = skinsfxfree++;
	}
	else
	{
		if (sfxfree > sfx_lastfreeslot)
		{
			CONS_Alert(CONS_WARNING, M_GetText("No more free sound slots\n"));
			return sfx_None;
		}
		i = sfxfree++;
	}

	strncpy(soundnames[i], name, 6);
	S_sfx[i].singularity = false;
	S_sfx[i].priority = 60;
	S_sfx[i].flags = 0;
	S_sfx[i].volume = -1;
	S_sfx[i].lumpnum = LUMPERROR;
	S_sfx[i].usefulness = -1;

	/// \todo if precached load it here
	S_sfx[i].data = NULL;

	return i;
}

void S_RemoveSoundFx(sfxenum_t id)
{
	if (id >= sfx_freeslot0 && id < NUMSFX && S_sfx[id].priority != 0)
	{
		S_sfx[id].lumpnum = LUMPERROR;
		I_FreeSfx(&S_sfx[id]);
		S_sfx[id].priority = 0;
	}
}
