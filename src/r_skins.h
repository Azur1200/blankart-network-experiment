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
/// \file  r_skins.h
/// \brief Skins stuff

#ifndef __R_SKINS__
#define __R_SKINS__

#include "info.h"
#include "sounds.h"
#include "d_player.h" // skinflags
#include "r_patch.h"
#include "r_picformats.h" // spriteinfo_t
#include "r_defs.h" // spritedef_t

#ifdef __cplusplus
extern "C" {
#endif

/// Defaults
#define SKINNAMESIZE 16
#define VOICENAMESIZE 32
#define SKINRIVALS 3
// should be all lowercase!! S_SKIN processing does a strlwr
#define DEFAULTSKIN "sonic"
#define DEFAULTSKIN2 "tails" // secondary player
#define DEFAULTSKIN3 "knuckles" // third player
#define DEFAULTSKIN4 "eggman" // fourth player

extern consvar_t cv_skinselectstyle, cv_skinselectsort;

typedef enum
{
	KSKVC_MAXVANILLA = MAXSKINS,
	KSKVC_FIRSTFREESLOT,
	KSKVC_LASTFREESLOT = 65535,
	MAXSKINVOICES
} kartvoicetype_e;

// Player voice struct
// ## TODO:
// - Dub implementation (freeslotting, voice lookup, assigning voices on race start)
// - Get rid of soundsid for good, replace it with some hacky compat system
struct kartvoice_t
{
    UINT32 id;

	char name[VOICENAMESIZE+1]; // INT16 descriptive name of the voice.

	// UINT16 ID of the parent skin this voice belongs to.
	// Checked when the voice is assigned to prevent erroneous dubs.
	UINT16 parent;
	
	sfxenum_t win;
    sfxenum_t lose;
    sfxenum_t pain[2];
    sfxenum_t attack[2];
    sfxenum_t boost[2];
    sfxenum_t overtake;
    sfxenum_t hitem;
    sfxenum_t power;
};

/// The skin_t struct
struct skin_t
{
	char name[SKINNAMESIZE+1]; // INT16 descriptive name of the skin
	UINT16 wadnum;
	skinflags_t flags;

	char realname[SKINNAMESIZE+1]; // Display name for level completion.
	char facerank[9], facewant[9], facemmap[9]; // Arbitrarily named patch lumps

	// SRB2kart
	UINT8 kartspeed;
	UINT8 kartweight;
	//

	INT32 followitem;

	// Definable color translation table
	UINT8 starttranscolor;
	UINT16 prefcolor;
	UINT16 supercolor;
	UINT16 prefoppositecolor; // if 0 use tables instead

	fixed_t highresscale; // scale of highres, default is 0.5

	char rivals[SKINRIVALS][SKINNAMESIZE+1]; // Your top 3 rivals for GP mode. Uses names so that you can reference skins that aren't added

	// specific sounds per skin
	sfxenum_t soundsid[NUMSKINSOUNDS]; // sound # in S_sfx table (Deprecated; remove eventually)

	// Pointer to a skin's voice.
	// If an mobj_t doesn't have a voice, it defaults to this value, given a skin is in use.
	kartvoice_t *voice;

	// contains super versions too
	spritedef_t sprites[NUMPLAYERSPRITES*2];
	spriteinfo_t sprinfo[NUMPLAYERSPRITES*2];
};

enum facepatches {
	FACE_RANK = 0,
	FACE_WANTED,
	FACE_MINIMAP,
	NUMFACES
};

enum skinmenusortoption
{
	SKINMENUSORT_ID = 0,
	SKINMENUSORT_NAME,
	SKINMENUSORT_SPEED,
	SKINMENUSORT_WEIGHT,
	SKINMENUSORT_ACCEL,
	SKINMENUSORT_HANDLING,
	SKINMENUSORT_PREFCOLOR,
	NUMSKINMENUSORT,
};

/// Externs
extern INT32 numskins, numskinvoices, numfreeslotvoices;
extern skin_t skins[MAXSKINS];
extern INT32 skinsorted[MAXSKINS];
extern kartvoice_t skinvoices[MAXSKINVOICES];

extern CV_PossibleValue_t Forceskin_cons_t[];

extern const UINT8 kart2spr2[26][2];

/// Function prototypes
void R_InitSkins(void);
void SortSkins(void);
INT32 FindSortedSkinIndex(INT32 skinnum);

void SetPlayerSkin(INT32 playernum,const char *skinname);
void SetPlayerSkinByNum(INT32 playernum,INT32 skinnum); // Tails 03-16-2002
boolean R_SkinUsable(INT32 playernum, INT32 skinnum);
UINT32 R_GetSkinAvailabilities(void);
INT32 R_SkinAvailable(const char *name);
void R_PatchSkins(UINT16 wadnum);
void R_AddSkins(UINT16 wadnum);

UINT8 P_GetSkinSprite2(skin_t *skin, UINT8 spr2, player_t *player);
UINT8 P_KartFrameToSprite2(skin_t *skin, UINT8 inframe, UINT8 *outframe);

sfxenum_t P_GetVoiceSFX(kartvoice_t *voice, INT32 skinsound);

#ifdef __cplusplus
} // extern "C"
#endif

#endif //__R_SKINS__
