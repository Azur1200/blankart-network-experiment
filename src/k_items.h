// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2018-2025 by Kart Krew.
// Copyright (C) 2025 by "yama".
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
#include "r_defs.h"

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
	KITEMEQUIP_NONE,
	KITEMEQUIP_ORBIT,	// orbiters (orbi, jawz)
	KITEMEQUIP_TRAIL,	// dragged items (bananas, mines, sinks)
	KITEMEQUIP_ROCKETS,	// direct player position with offset (rockets)
	KITEMEQUIP_SHIELD,	//TODO: spawns a shield when active (currently acts like KITEMEQUIP_NONE)
} kartitemequip_e;

typedef enum
{
	KITEMBLINK_NORMAL,
	KITEMBLINK_MASHED,
	KITEMBLINK_KARMA,
	KITEMBLINK_DEBUG,
} kartitemblink_e;

typedef enum
{
	KROULETTETYPE_NORMAL,
	KROULETTETYPE_KARMA,
	KROULETTETYPE_EGGMAN,
} kartroulettetype_e;

typedef enum
{
	ODDS_RACE,
	ODDS_BATTLE,
	ODDS_SPECIAL,
	MAXODDSTABLES
} ATTRPACK kartoddstable_e;

// item flags for usage and HUD
typedef enum
{
	KIF_UNIQUESLOT = 1<<0,       // Prevents the item from being rolled if in a player's item slot
	KIF_UNIQUEDROP = 1<<1,       // Prevents the item from being rolled if it exists as a dropped item
	KIF_UNIQUE = KIF_UNIQUESLOT|KIF_UNIQUEDROP,

	KIF_ANIMATED = 1<<2,         // animate patches instead of using diffrent patches for different amounts (inversion of xItemLib's XIF_ICONFORAMT)
	KIF_DARKBG = 1<<3,           // use dark item roulette BG
	KIF_COLPATCH2PLAYER = 1<<4,  // colourize patch to player
	KIF_HIDEFROMROULETTE = 1<<5, // don't show this item in the roulette (inversion of xItem's showInRoulette item flag)
} ATTRPACK kartitemflags_e;

// result flags relevant to the roulette
typedef enum
{
	KRF_INDIRECTITEM = 1<<0,    // This result's item affects others indirectly. All results with this flag share a single cooldown timer.
	KRF_POWERITEM = 1<<1,       // This result is a "power item". This activates "frantic item" toggle related functionality.
	KRF_COOLDOWNONSTART = 1<<2, // This result should not appear at the beginning of a race. (Usually really powerful crowd-breaking items)
	KRF_NOTNEAREND = 1<<3,      // This result should not appear at the end of a race. (Usually trap items that lose their effectiveness)
	KRF_NOTFORBOTTOM = 1<<4,    // After the first 30 seconds, this result stops appearing for losing players. (Usually items that feel less effective at these positions)
	KRF_RUNNERAUGMENT = 1<<5,   // This result's odds are affected by the severity of 1st place's frontrun. (Very likely only for SPBs, but who knows?)
	KRF_HIDEFROMSPB = 1<<6,     // This result refuses to appear when a Self-Propelled Bomb is active in a race or battle.
} ATTRPACK kartresultflags_e;

// Unique useodds function
typedef INT32 (useoddsfunc_f)(INT32 odds, const kartroulette_t *roulette, const kartresult_t *result, UINT8 *forceme);

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
	kartitemequip_e equipstyle[2];
	kartitemflags_e flags[2];
	kartitemgraphics_t graphics[4];
	spritedef_t spritedef;

	consvar_t *altcvar; // if not NULL, an altitem exists
	boolean altenabled;
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
	UINT8 odds[MAXODDSTABLES][MAXODDS];

	// Functions that tell the game to use unique means of rolling these items.
	// If NULL, the functions never execute.
	useoddsfunc_f *unique_odds[MAXODDSTABLES];

	tic_t basecooldown, cooldown;
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

	// output: which results are being forced into a player's item slot for one reason or another. Higher value = higher priority.
	UINT8 forceme[MAXKARTRESULTS];

	// extras for useodds functions
	UINT8 pingame;
	UINT8 pexiting;
	UINT32 firstDist;
};

extern consvar_t cv_fancyroulette;
extern consvar_t cv_fancyroulettespeed;

extern kartitem_t kartitems[MAXKARTITEMS];
extern UINT8 numkartitems;
extern kartresult_t kartresults[MAXKARTRESULTS];
extern UINT8 numkartresults;
extern UINT8 oddstablemax[MAXODDSTABLES];

void K_RegisterItem(kartitemtype_e itemtype);
kartresult_t *K_RegisterResult(const char *name, boolean alternate);
kartresult_t *K_GetKartResultAlt(const char *name, boolean alternate);
#define K_GetKartResult(name) K_GetKartResultAlt(name, false)
void K_SetupItemOdds(void);
boolean K_ItemResultEnabled(const kartresult_t *result);
boolean K_IsKartItemAlternate(kartitemtype_e itemtype);
kartitemflags_e K_GetItemFlags(kartitemtype_e itemtype);
kartitemequip_e K_GetItemEquipStyle(kartitemtype_e mobjtype);
UINT8 K_GetItemNumberDisplayMin(kartitemtype_e type, boolean tiny);
patch_t *K_GetCachedItemPatchEx(kartitemtype_e type, boolean tiny, UINT8 amount, SINT8 altmode);
#define K_GetCachedItemPatch(type, tiny, amount) K_GetCachedItemPatchEx(type, tiny, amount, -1)

void K_SetItemOut(player_t *player, kartitemtype_e itemtype, itemflags_t flags);
void K_UnsetItemOut(player_t *player);

void K_AwardPlayerItem(player_t *player, kartitemtype_e type, UINT8 amount, kartitemblink_e blink);
void K_UpdateMobjItemOverlay(mobj_t *part, kartitemtype_e itemType, UINT8 itemCount);
void K_UpdateItemCooldown(void);
tic_t K_GetIndirectItemCooldown(void);
void K_SetIndirectItemCooldown(tic_t cooldown);
boolean K_LegacyOddsMode(void);
UINT32 K_GetCongaLineDistance(const player_t *player, UINT8 startPos);

UINT32 K_CalculateInitalPDIS(const player_t *player, UINT8 pingame);
UINT32 K_CalculatePDIS(const player_t *player, UINT8 numPlayers, boolean *spbrush);
UINT8 K_FindUseodds(const player_t *player, fixed_t mashed, UINT32 pdis, UINT8 bestbumper, boolean spbrush);
UINT32 K_ScaleItemDistance(UINT32 distance, UINT8 numPlayers, boolean spbrush);
INT32 K_GetRollingRouletteItem(player_t *player);
INT32 K_GetRollingRouletteItemOffset(player_t *player, INT16 offset, INT32 animspeed);
UINT8* K_GetRollableItems(void);
void K_KartItemRoulette(player_t *player, ticcmd_t *cmd);
void K_StartRoulette(player_t *player, kartroulettetype_e roulettetype);

void K_SetPlayerItemCooldown(player_t *player, tic_t timer, boolean force);

useoddsfunc_f KO_AltInvinOdds;
useoddsfunc_f KO_SPBRaceOdds;

void K_DoThunderShield(player_t *player);
void K_BreakBubbleShield(player_t* player);
INT16 K_GetShrinkTime(const player_t *player);
boolean K_IsAltShrunk(const player_t *player);
void K_AltShrinkPityIncrease(player_t *player);

void K_PlayerItemThink(player_t *player, boolean onground);

#ifdef __cplusplus
} // extern "C"
#endif

#ifndef __cplusplus
void K_KartGetItemOdds(kartroulette_t *roulette, INT32 outodds[static MAXKARTRESULTS]);
#endif

#endif // __K_ITEMS__
