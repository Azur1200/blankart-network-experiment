// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2018-2025 by Kart Krew.
// Copyright (C) 2025 by "Anonimus".
// Copyright (C) 2025 Blankart Team.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  k_items.h
/// \brief Kart item systems.

#ifndef __K_ITEMS__
#define __K_ITEMS__

#include "doomdef.h"
#include "doomtype.h"

#ifdef __cplusplus
extern "C" {
#endif

// Max odds count
#define MAXODDS 16

// Distance variables
#define DISTVAR CV_Get(&cv_kartoddsdist)
#define DISTVAR_LEGACY CV_Get(&cv_kartlegacyoddsdist)
#define SPBDISTVAR CV_Get(&cv_kartspbdist)
#define SPBDISTVAR_LEGACY CV_Get(&cv_kartlegacyspbdist)

typedef enum
{
	KITEM_SAD = -1,
	KITEM_NONE = 0,
	KITEM_SNEAKER,
	KITEM_ROCKETSNEAKER,
	KITEM_INVINCIBILITY,
	KITEM_BANANA,
	KITEM_EGGMAN,
	KITEM_ORBINAUT,
	KITEM_JAWZ,
	KITEM_MINE,
	KITEM_BALLHOG,
	KITEM_SPB,
	KITEM_GROW,
	KITEM_SHRINK,
	KITEM_THUNDERSHIELD,
	KITEM_HYUDORO,
	KITEM_POGOSPRING,
	KITEM_KITCHENSINK,
	KITEM_SUPERRING,
	KITEM_LANDMINE,
	KITEM_BUBBLESHIELD,
	KITEM_FLAMESHIELD,

	KITEM_FIRSTFREESLOT,
	KITEM_LASTFREESLOT = 126,
	MAXKARTITEMS
} ATTRPACK kartitemtype_e;

#define MAXKARTRESULTS 255

typedef enum
{
	ODDS_RACE,
	ODDS_BATTLE,
	ODDS_SPECIAL,
	MAXODDSTABLES
} ATTRPACK kartoddstable_e;

struct kartitemgraphics_t
{
	UINT8 numpatches;
	const char **patchnames;
	patch_t **patches;
};

struct kartitem_t
{
	const char *name;
	consvar_t *altcvar;
	kartitemgraphics_t graphics[2];
	UINT8 hudflags;
	
	// note: could be how we do this?
	// int (*oddsOverrideFunc)(player_t *p, int pos, int mashed, boolean spbrush, fixed_t seconddist, int pingame, int pexiting);
	// int (*resultOverrideFunc)(player_t *p);
};

// item flags relevant to item rolls and usage
enum
{
	KRF_INDIRECTITEM = 1<<0,
	KRF_POWERITEM = 1<<1,
	KRF_COOLDOWNONSTART = 1<<2,
	KRF_NOTNEAREND = 1<<3,
	KRF_NOTFORBOTTOM = 1<<4,
	KRF_UNIQUE = 1<<5,
};

// item flags relevant to patch rendering in the HUD
enum
{
	KPF_ANIMATED = 1<<0,		// animate patches instead of using diffrent patches for different amounts (inversion of xItemLib's XIF_ICONFORAMT)
	KPF_DARKBG = 1<<1,			// use dark item roulette BG
	KPF_COLPATCH2PLAYER = 1<<2,	// colourize patch to player
};

struct kartresult_t
{
	const char *name;
	consvar_t *cvar;
	kartitemtype_e type;
	UINT8 amount;
	UINT8 odds[MAXODDSTABLES][MAXODDS];
	UINT8 flags;
	tic_t basebgone, bgone;
};

extern kartitem_t kartitems[MAXKARTITEMS];
extern UINT8 numkartitems;
extern kartresult_t kartresults[MAXKARTRESULTS];
extern UINT8 numkartresults;

kartresult_t *K_GetKartResult(const char *name);
void K_InitializeItems(void);
boolean K_ItemResultEnabled(const char *name);
void K_SetupItemOdds(void);

void K_KartHandleShieldCooldown(kartitemtype_e item);
boolean K_LegacyOddsMode(void);
UINT32 K_GetCongaLineDistance(const player_t *player, UINT8 startPos);

UINT32 K_CalculateInitalPDIS(const player_t *player, UINT8 pingame);
UINT32 K_CalculatePDIS(const player_t *player, UINT8 numPlayers, boolean *spbrush);
UINT8 K_FindUseodds(const player_t *player, fixed_t mashed, UINT32 pdis, UINT8 bestbumper, boolean spbrush);
UINT32 K_ScaleItemDistance(UINT32 distance, UINT8 numPlayers, boolean spbrush);
INT32 K_KartGetItemOdds(UINT8 pos, const kartresult_t *result, UINT32 ourDist, UINT32 clusterDist, fixed_t mashed, boolean spbrush, boolean bot, boolean rival, boolean inBottom);
INT32 K_GetRollingRouletteItem(player_t *player);
void K_KartItemRoulette(player_t *player, ticcmd_t *cmd);

boolean K_IsShrunk(const player_t *player);
INT16 K_GetShrinkTime(player_t *player);
fixed_t K_AccomodateShrinkScaling(fixed_t x);

boolean K_IsShrunkMode(const player_t *player, boolean legacy);

#define K_IsLegacyShrunk(player) (K_IsShrunkMode(player, true))
#define K_IsAltShrunk(player) (K_IsShrunkMode(player, false))

void K_AltShrinkIFrames(player_t *player);
void K_AltShrinkPityIncrease(player_t *player);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __K_ITEMS__
