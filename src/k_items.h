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
#define _(name, ...) KITEM_##name,
#include "info/kartitems.h"
#undef _
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

// item flags for HUD and usage
typedef enum
{
	KIF_ANIMATED = 1<<0,        // animate patches instead of using diffrent patches for different amounts (inversion of xItemLib's XIF_ICONFORAMT)
	KIF_DARKBG = 1<<1,          // use dark item roulette BG
	KIF_COLPATCH2PLAYER = 1<<2, // colourize patch to player
} ATTRPACK kartitemflags_e;

// flags relevant to item rolls and usage
typedef enum
{
	KRF_INDIRECTITEM = 1<<0,    // This item affects others indirectly. A 20 second cooldown is applied to all other items with this flag when an indirect item is rolled.
	KRF_POWERITEM = 1<<1,       // This item is a "power item". This activates "frantic item" toggle related functionality.
	KRF_COOLDOWNONSTART = 1<<2, // This item should not appear at the beginning of a race. (Usually really powerful crowd-breaking items)
	KRF_NOTNEAREND = 1<<3,      // This item should not appear at the end of a race. (Usually trap items that lose their effectiveness)
	KRF_NOTFORBOTTOM = 1<<4,    // After the first 30 seconds, this item stops appearing for losing players. (Usually items that feel less effective at these positions)
	KRF_UNIQUE = 1<<5,          // If already in a player's item slot, prevents all other players from obtaining this item from item boxes
	KRF_RUNNERAUGMENT = 1<<6,   // This item's odds are affected by the severity of 1st place's frontrun. (Very likely only for SPBs, but who knows?)
	// free: 1<<7
} ATTRPACK kartresultflags_e;

// Unique useodds function
typedef INT32 (useoddsfunc_f)(INT32 odds, const kartroulette_t *roulette, kartresult_t *result);

#define MAXITEMPATCHES 10

struct kartitemgraphics_t
{
	UINT8 numpatches;
	char *patchnames[MAXITEMPATCHES];
	patch_t *patches[MAXITEMPATCHES];
};

struct kartitem_t
{
	dehinfo_t info;
	kartitemgraphics_t graphics[2];
	kartitemflags_e flags;

	consvar_t *altcvar; // if not NULL, an altitem exists
	boolean altenabled;

	// note: could be how we do this?
	// int (*oddsOverrideFunc)(player_t *p, int pos, int mashed, boolean spbrush, fixed_t seconddist, int pingame, int pexiting);
	// int (*resultOverrideFunc)(player_t *p);
};

struct kartresult_t
{
	// this block is shared by all item variants
	consvar_t *cvar; // contains name
	kartitemtype_e type;
	UINT8 amount;

	char *displayname;
	boolean isalt; // is alt (i salt)
	kartresultflags_e flags;
	UINT8 forceme; // This item is being forced into a player's item slot for one reason or another. Higher value = higher priority.
	UINT8 odds[MAXODDSTABLES][MAXODDS];

	// Functions that tell the game to use unique means of rolling these items.
	// If NULL, the functions never execute.
	useoddsfunc_f *unique_odds[MAXODDSTABLES];

	tic_t basebgone, bgone;
};

// contains all data for a call to K_KartGetItemOdds
struct kartroulette_t
{
	UINT32 pdis; // the player's PDIS value
	SINT8 playerpos; // the player's position
	UINT8 pos; // position in the odds table (why is this just "pos"???)
	UINT32 ourDist; // the player's finish line distance
	UINT32 clusterDist; // distance to cluster
	fixed_t mashed;
	boolean spbrush;
	boolean bot;
	boolean rival;
	boolean inBottom;

	// extras for useodds functions
	UINT8 pingame;
	UINT8 pexiting;
	UINT32 firstDist;
};

extern kartitem_t kartitems[MAXKARTITEMS];
extern UINT8 numkartitems;
extern kartresult_t kartresults[MAXKARTRESULTS];
extern UINT8 numkartresults;
extern UINT8 oddstablemax[MAXODDSTABLES];

void K_RegisterItem(kartitemtype_e itemtype);
kartresult_t *K_RegisterResult(const char *name, boolean alternate);
kartresult_t *K_GetKartResultAlt(const char *name, boolean alternate);
#define K_GetKartResult(name) K_GetKartResultAlt(name, false)
boolean K_ItemResultEnabled(const kartresult_t *result);
boolean K_IsKartItemAlternate(kartitemtype_e itemtype);
void K_SetupItemOdds(void);

void K_KartHandleUniqueCooldown(void);
boolean K_LegacyOddsMode(void);
UINT32 K_GetCongaLineDistance(const player_t *player, UINT8 startPos);

UINT32 K_CalculateInitalPDIS(const player_t *player, UINT8 pingame);
UINT32 K_CalculatePDIS(const player_t *player, UINT8 numPlayers, boolean *spbrush);
UINT8 K_FindUseodds(const player_t *player, fixed_t mashed, UINT32 pdis, UINT8 bestbumper, boolean spbrush);
UINT32 K_ScaleItemDistance(UINT32 distance, UINT8 numPlayers, boolean spbrush);
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

INT32 KO_AltInvinOdds(INT32 odds, const kartroulette_t *roulette, kartresult_t *result);
INT32 KO_SPBRaceOdds(INT32 odds, const kartroulette_t *roulette, kartresult_t *result);

#ifdef __cplusplus
} // extern "C"
#endif

#ifndef __cplusplus
void K_KartGetItemOdds(kartroulette_t *roulette, INT32 outodds[static MAXKARTRESULTS]);
#endif

#endif // __K_ITEMS__
