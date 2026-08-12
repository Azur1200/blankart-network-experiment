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
#define VOICEARRAYSIZE 2
#define SKINRIVALS 3
// should be all lowercase!! S_SKIN processing does a strlwr
#define DEFAULTSKIN "sonic"
#define DEFAULTSKIN2 "tails" // secondary player
#define DEFAULTSKIN3 "knuckles" // third player
#define DEFAULTSKIN4 "eggman" // fourth player

extern consvar_t cv_skinselectstyle, cv_skinselectsort;

#define MAXSKINVOICES 16 // 16 voices * 4096 skins = 65536 voices in total :^)

// Replacement for cv_voice
struct kartvoiceinfo_t
{
	char name[VOICENAMESIZE+1];
	kartvoicenum_t value;
};

extern kartvoiceinfo_t localvoicedata[MAXSPLITSCREENPLAYERS];

// Player voice struct
struct kartvoice_t
{
    uint32_t id;

	// The voice's name.
	char name[VOICENAMESIZE+1];

	// Display name, for things such as menus
	char realname[VOICENAMESIZE+1];
	
	sfxenum_t win;
    sfxenum_t lose;
    sfxenum_t pain[VOICEARRAYSIZE];
    sfxenum_t attack[VOICEARRAYSIZE];
    sfxenum_t boost[VOICEARRAYSIZE];
    sfxenum_t overtake;
    sfxenum_t hitem;
    sfxenum_t power;
};

/// The skin_t struct
struct skin_t
{
	char name[SKINNAMESIZE+1]; // name of skin
	uint32_t namehash;
	uint16_t skinnum;
	uint16_t wadnum;
	skinflags_t flags;

	char realname[SKINNAMESIZE+1]; // Display name for level completion.
	char facerank[9], facewant[9], facemmap[9]; // Arbitrarily named patch lumps

	// SRB2kart
	uint8_t kartspeed;
	uint8_t kartweight;
	//

	int32_t followitem;

	// Definable color translation table
	uint8_t starttranscolor;
	uint16_t prefcolor;
	uint16_t supercolor;
	uint16_t prefoppositecolor; // if 0 use tables instead

	fixed_t highresscale; // scale of highres, default is 0.5

	char rivals[SKINRIVALS][SKINNAMESIZE+1]; // Your top 3 rivals for GP mode. Uses names so that you can reference skins that aren't added

	// Pointer to a skin's voice, providing specific sounds per-skin.
	// If an mobj_t doesn't have a voice, it defaults to this value, given a skin is in use.
	kartvoice_t voices[MAXSKINVOICES];

	kartvoicenum_t numvoices;

	// Proxy value for the (now removed) soundsid array.
	// This is ONLY used in a Lua context.
	// It doesn't actually have anything and will never actually have anything.
	uint8_t soundsid[2];

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
extern int32_t numskins;
extern skin_t **skins;

extern int32_t num_multivoiceskins;
extern skin_t * multivoiceskins[MAXSKINS];

extern int32_t skinsorted[MAXSKINS];

extern CV_PossibleValue_t Forceskin_cons_t[];

extern const uint8_t kart2spr2[26][2];

/// Function prototypes
void R_InitSkins(void);
void SortSkins(void);
int32_t FindSortedSkinIndex(int32_t skinnum);

void SetPlayerSkin(int32_t playernum,const char *skinname);
void SetPlayerSkinByNum(int32_t playernum,int32_t skinnum); // Tails 03-16-2002
dboolean R_SkinUsable(int32_t playernum, int32_t skinnum);
uint8_t *R_GetSkinAvailabilities(void);
int32_t R_SkinAvailableEx(const char *name, dboolean usehash);
#define R_SkinAvailable(name) R_SkinAvailableEx(name, true)
void R_PatchSkins(uint16_t wadnum);
void R_AddSkins(uint16_t wadnum);
void R_ApplySkin(mobj_t *mo, skin_t *skin);
void R_ApplySkinOnly(mobj_t *mo, skin_t *skin);

uint8_t P_GetSkinSprite2(skin_t *skin, uint8_t spr2, player_t *player);
uint8_t P_KartFrameToSprite2(skin_t *skin, uint8_t inframe, uint8_t *outframe);

int32_t R_FindIDForVoice(skin_t *skin, const char *voicename);
void SetPlayerVoice(int32_t playernum, const char *voicename);
void SetPlayerVoiceByNum(int32_t playernum, uint16_t voicenum);
sfxenum_t R_GetLegacySkinSound(const kartvoice_t *voice, sfxenum_t sound);

#ifdef __cplusplus
} // extern "C"
#endif

#endif //__R_SKINS__
