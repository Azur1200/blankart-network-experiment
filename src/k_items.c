// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2018-2025 by Kart Krew.
// Copyright (C) 2026 by "yama".
// Copyright (C) 2026 Blankart Team.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  k_items.c
/// \brief Kart item systems.

// SRB2kart Roulette Code - Position Based

#include "doomdef.h"
#include "doomstat.h"
#include "d_netcmd.h"
#include "d_player.h"
#include "g_game.h"
#include "info.h"
#include "m_easing.h"   // Invincibility gradienting
#include "m_fixed.h"
#include "m_random.h"
#include "p_local.h"
#include "p_mobj.h"
#include "p_setup.h"
#include "r_main.h"
#include "s_sound.h"
#include "typedef.h"
#include "deh_tables.h"
#include "lua_hook.h"
#include "z_zone.h"

#include "k_battle.h"
#include "k_boss.h"
#include "k_bot.h"
#include "k_kart.h"
#include "k_waypoint.h"
#include "k_director.h"
#include "k_grandprix.h"
#include "k_cluster.hpp"
#include "k_itemlist.hpp"
#include "k_items.h"
#include "k_collide.h"

#include <tracy/tracy/TracyC.h>

kartitem_t kartitems[MAXKARTITEMS] = {0};
kartresult_t kartresults[MAXKARTRESULTS] = {0};
UINT8 numkartitems = 0;
UINT8 numkartresults = 0;
UINT8 oddstablemax[MAXODDSTABLES] = { MAXODDS, 2, 4 };

static CV_PossibleValue_t kartdebugitem_cons_t[MAXKARTITEMS] = {{0, "NONE"}, {0, NULL}};
consvar_t cv_kartdebugitem = CVAR_INIT ("kartdebugitem", "NONE", CV_NETVAR|CV_CHEAT, kartdebugitem_cons_t, NULL);
static CV_PossibleValue_t kartdebugamount_cons_t[] = {{1, "MIN"}, {255, "MAX"}, {0, NULL}};
consvar_t cv_kartdebugamount = CVAR_INIT ("kartdebugamount", "1", CV_NETVAR|CV_CHEAT, kartdebugamount_cons_t, NULL);

static CV_PossibleValue_t kartitemvariant_cons_t[] = {{0, "Legacy"}, {1, "Alternative"}, {0, NULL}};

#define NUMKARTODDS 	(MAXODDS*10)

// Base multiplication to ALL item odds to simulate fractional precision.
// Reduced from 4 to 1 due to 75-count probability.
#define BASEODDSMUL 1

// Multiplication to odds for Battle item odds specifically.
#define BATTLEODDSMUL 4

// Maximum probability.
#define REALMAXPROB 75
#define MAXPROBABILITY (REALMAXPROB * BASEODDSMUL)

#define REALMAXBATTLEPROB 10
#define MAXBATTLEPROBABILITY (REALMAXBATTLEPROB * BATTLEODDSMUL)

consvar_t cv_fancyroulette = CVAR_INIT ("fancyroulette", "Off", CV_SAVE, CV_OnOff, NULL);
consvar_t cv_fancyroulettespeed = CVAR_INIT ("fancyroulettespeed", "3", CV_SAVE, CV_Natural, NULL);

static CV_PossibleValue_t huditemamount_cons_t[] = {{0, "Vanilla"}, {1, "Always"}, {2, "Multiple"},{0, NULL}};
consvar_t cv_huditemamount = CVAR_INIT ("showitemamountnumber", "Vanilla", CV_SAVE, huditemamount_cons_t, NULL);

// Item related CVARs
consvar_t cv_karteggmine_slotlock = CVAR_INIT ("karteggmine_slotlock", "Off", CV_NETVAR|CV_CHEAT|CV_GUARD, CV_OnOff, NULL);
consvar_t cv_karteggmine_slotbrick = CVAR_INIT ("karteggmine_slotbrick", "On", CV_NETVAR|CV_CHEAT|CV_GUARD, CV_OnOff, NULL);

consvar_t cv_kartthunder_radius = CVAR_INIT ("kartthunder_radius", "320", CV_NETVAR|CV_CHEAT|CV_GUARD, CV_Natural, NULL);

// item function tables, in the form of {standard, alternate}
itemthinkfunc_f *KI_ItemThinkFunctions[][2] =
{
	[KITEM_THUNDERSHIELD] = 	{K_ItemThink_ThunderShield, K_ItemThink_AttractionShield},
	[KITEM_BUBBLESHIELD] = 		{K_ItemThink_BubbleShield, NULL},
	[KITEM_BONUSITEMROLL] = 	{K_ItemThink_BonusRoll, NULL},
	[MAXKARTITEMS] = 			{NULL,NULL}
};
itemusefunc_f *KI_ItemUseFunctions[][2] =
{
	[KITEM_SNEAKER] = 			{K_UseItem_Sneaker, NULL},
	[KITEM_ROCKETSNEAKER] = 	{K_UseItem_RocketSneaker, NULL},
	[KITEM_INVINCIBILITY] = 	{K_UseItem_Invincibility, K_UseItem_SMonitor},
	[KITEM_BANANA] = 			{K_UseItem_Banana, NULL},
	[KITEM_EGGMAN] = 			{K_UseItem_Eggman, K_UseItem_EggMine},
	[KITEM_ORBINAUT] = 			{K_UseItem_Orbinaut, NULL},
	[KITEM_JAWZ] = 				{K_UseItem_Jawz, NULL},
	[KITEM_MINE] = 				{K_UseItem_Mine, NULL},
	[KITEM_LANDMINE] = 			{K_UseItem_LandMine, NULL},
	[KITEM_BALLHOG] = 			{K_UseItem_Ballhog, NULL},
	[KITEM_SPB] =	 			{K_UseItem_SPB, NULL},
	[KITEM_GROW] =	 			{K_UseItem_Grow, NULL},
	[KITEM_SHRINK] =	 		{K_UseItem_Shrink, K_UseItem_Mini},
	[KITEM_THUNDERSHIELD] =	 	{K_UseItem_ThunderShield, K_UseItem_AttractionShield},
	[KITEM_FLAMESHIELD] =	 	{K_UseItem_FlameShield, NULL},
	[KITEM_HYUDORO] =		 	{K_UseItem_Hyudoro, NULL},
	[KITEM_POGOSPRING] =	 	{K_UseItem_PogoSpring, NULL},
	[KITEM_SUPERRING] =		 	{K_UseItem_SuperRing, NULL},
	[KITEM_KITCHENSINK] =	 	{K_UseItem_KitchenSink, NULL},
	[KITEM_EGGBRICK] =		 	{K_UseItem_EggBrick, NULL},
	[MAXKARTITEMS] = 			{K_UseItem_Sad, NULL}
};
itemusefunc_f *KI_ItemHoldUseFunctions[][2] =
{
	[KITEM_THUNDERSHIELD] = 	{NULL, K_HoldUseItem_AttractionShield},
	[KITEM_FLAMESHIELD] = 		{K_HoldUseItem_FlameShield, NULL},
	[MAXKARTITEMS] = 			{NULL,NULL}
};
itemusefunc_f *KI_ItemReleaseUseFunctions[][2] =
{
	[KITEM_THUNDERSHIELD] = 	{NULL, K_ReleaseUseItem_AttractionShield},
	[KITEM_FLAMESHIELD] = 		{K_ReleaseUseItem_FlameShield, NULL},
	[MAXKARTITEMS] = 			{NULL,NULL}
};

void K_RegisterItem(kartitemtype_e itemtype)
{
	kartdebugitem_cons_t[itemtype].strvalue = DEH_KartItemName(itemtype);
	kartdebugitem_cons_t[itemtype].value = itemtype;
	kartdebugitem_cons_t[itemtype + 1].strvalue = NULL;
	kartdebugitem_cons_t[itemtype + 1].value = 0;
}

kartresult_t *K_RegisterResult(const char *name, boolean alternate)
{
	kartresult_t *result = &kartresults[numkartresults];

	if (alternate)
	{
		// link to the normal result
		kartresult_t *prev = K_GetKartResult(name);
		if (prev == NULL)
			I_Error("K_RegisterResult: Result %s doesn't exist, can't create alt result", name);

		result->cvar = prev->cvar;
		result->type = prev->type;
		result->amount = prev->amount;
		result->isalt = true;

		// check if the kartitem needs to have its alt cvar registered
		kartitem_t *item = &kartitems[result->type];
		if (item->altcvar == NULL)
		{
			char *cvname = malloc(strlen(DEH_KartItemName(result->type))+1 + 8);
			sprintf(cvname, "altitem_%s", DEH_KartItemName(result->type));
			strlwr(cvname);

			consvar_t *var = Z_Calloc(sizeof(consvar_t), PU_STATIC, &item->altcvar);
			var->name = cvname;
			var->defaultvalue = "Legacy";
			var->flags = CV_NETVAR/*|CV_CALL*/;
			var->PossibleValue = kartitemvariant_cons_t;
			var->func = NULL;
			CV_RegisterVar(var);
		}
	}
	else
	{
		consvar_t *var = Z_Calloc(sizeof(consvar_t), PU_STATIC, &result->cvar);
		char *cvname = Z_StrDup(name);
		strlwr(cvname);

		var->name = cvname;
		var->defaultvalue = "On";
		var->flags = CV_NETVAR|CV_CHEAT;
		var->PossibleValue = CV_OnOff;
		var->func = NULL;
		CV_RegisterVar(var);
	}

	numkartresults++;
	return result;
}

kartresult_t *K_GetKartResultAlt(const char *name, boolean alternate)
{
	for (UINT8 i = 0; i < numkartresults; i++)
	{
		if (fasticmp(name, kartresults[i].cvar->name) && kartresults[i].isalt == alternate)
			return &kartresults[i];
	}

	return NULL;
}

void K_SetupItemOdds(void)
{
	// Update the item's variant based on the cvar's value.
	for (kartitemtype_e i = 0; i < numkartitems; i++)
	{
		kartitem_t *item = &kartitems[i];
		if (item->altcvar != NULL && item->altenabled != !!item->altcvar->value)
		{
			CONS_Printf("KITEM_%s: updating variant to %s\n", DEH_KartItemName(i), item->altcvar->string);
			item->altenabled = !!item->altcvar->value;
		}
	}

	for (UINT8 i = 0; i < numkartresults; i++)
		kartresults[i].cooldown = 0;
}

boolean K_ItemResultEnabled(const kartresult_t *result)
{
	if (result == NULL)
		return false;

	if (result->type == KITEM_SUPERRING && !K_RingsActive())
		return false;

	return result->cvar->value == 1;
}

boolean K_IsKartItemAlternate(kartitemtype_e itemtype)
{
	if (!itemtype) return false;
	return itemtype < numkartitems && kartitems[itemtype].altenabled;
}

kartitemflags_e K_GetItemFlags(kartitemtype_e itemtype)
{
	if (itemtype >= numkartitems)
		return 0;

	return kartitems[itemtype].flags[K_IsKartItemAlternate(itemtype) ? 1 : 0];
}

kartitemequip_e K_GetItemEquipStyle(kartitemtype_e itemtype)
{
	if (itemtype >= numkartitems)
		return KITEMEQUIP_NONE;

	return kartitems[itemtype].equipstyle[K_IsKartItemAlternate(itemtype) ? 1 : 0];
}

UINT8 K_GetItemNumberDisplayMin(kartitemtype_e type, boolean tiny)
{
	if (type == MAXKARTITEMS)
		type = 0; // KITEM_SAD displays like normal
	if (type >= numkartitems)
		return 1; // actually broken items show x1 to let you know
	
	if (cv_huditemamount.value > 0) 
		return cv_huditemamount.value;

	return K_GetItemFlags(type) & KIF_ANIMATED ? 2 : kartitems[type].graphics[tiny ? 1 : 0].numpatches + 1;
}

patch_t *K_GetCachedItemPatchEx(kartitemtype_e type, boolean tiny, UINT8 amount, SINT8 altmode)
{
	kartitem_t *item = &kartitems[type >= numkartresults ? 0 : type];
	boolean alt = altmode == -1 ? K_IsKartItemAlternate(type) : !!altmode;
	kartitemgraphics_t *graphics = &item->graphics[(tiny ? 1 : 0) + (alt ? 2 : 0)];

	// fallback if alt graphics don't exist
	if (graphics->numpatches == 0 && altmode == -1)
		graphics = &item->graphics[tiny ? 1 : 0];

	if (graphics->numpatches == 0)
		return missingpat;

	tic_t timer = leveltime;
	if (!G_GamestateUsesLevel())
		timer = finalecount;

	if (K_GetItemFlags(type) & KIF_ANIMATED)
		return graphics->patches[(timer % (graphics->numpatches*3)) / 3];
	else
		return graphics->patches[CLAMP(amount - 1, 0, graphics->numpatches - 1)];
}

// sprite3 baybeeeee!!!!
// the renderer uses mobj->threshold to determine the item type
void K_UpdateMobjItemOverlay(mobj_t *part, kartitemtype_e itemType, UINT8 itemCount)
{
	part->sprite = SPR_ITEM;
	part->frame = FF_FULLBRIGHT|FF_PAPERSPRITE;

	if (itemType >= numkartitems)
		itemType = 0;

	UINT8 numpatches = kartitems[itemType].graphics[0].numpatches;
	if (K_GetItemFlags(itemType) & KIF_ANIMATED)
		part->frame |= (leveltime % (numpatches*3)) / 3;
	else
		part->frame |= CLAMP(itemCount - 1, 0, numpatches - 1);
}

/**	\brief	Are the odds in legacy distancing mode?

	\return	true if kartforcelegacyodds is enabled, or the map uses legacy WPs
*/
boolean K_LegacyOddsMode(void)
{
	return (K_UsingLegacyCheckpoints() || (cv_kartforcelegacyodds.value));
}

static boolean K_InStartCooldown(void)
{
	return (leveltime < (30*TICRATE)+starttime);
}

// Magic number distance for use with item roulette tiers
#define ACTIVEDISTVAR (K_LegacyOddsMode() ? DISTVAR_LEGACY : DISTVAR)

#define ENDDIST (12*ACTIVEDISTVAR) // Distance when the game stops giving you bananas

static boolean K_RaceForceSPB(SINT8 playerpos, UINT32 pdis)
{
	UINT8 idx = K_LegacyOddsMode() ? 1 : 0;

	kartresult_t *spb_res = K_GetKartResult("selfpropelledbomb");

	if (!spb_res)
	{
		return false;
	}

	if (spb_res->augcvar[idx] == NULL)
	{
		// ?!
		return false;
	}

	return ((gametypes[gametype]->rules & GTR_CIRCUIT) && playerpos == 2 && pdis > (UINT32)(7 * spb_res->augcvar[idx]->value / 2));
}

// 1/21/2025: I hate tiptoeing around the integer limit.
// This is at a smaller scale.
static UINT32 K_Dist2D(INT32 x1, INT32 y1, INT32 x2, INT32 y2)
{
	// d = √((x2 - x1)² + (y2 - y1)²)
	INT32 xdiff, ydiff;
	INT64 xprod, yprod;

	xdiff = (x2 - x1);
	ydiff = (y2 - y1);

	xprod = ((INT64)xdiff * (INT64)xdiff);
	yprod = ((INT64)ydiff * (INT64)ydiff);

	return (UINT32)(IntSqrt64(xprod + yprod));
}

// Basic integer distancing, to quote myself:
// "Even if you did 256 units for 1 fracunit in distancing, it'd be a better result than trying to
// deal with overflows on a system that's already being pushed to the limit by needing 65536 units
// for precision. No seriously, I don't think anyone's losing sleep over "hmmmmm, 0.0000152 or
// 0.0039?????" when most 2D game engines only give a fuck about MAYBE 0.001"
/*static UINT32 K_IntDistance(fixed_t curx,
			 fixed_t cury,
			 fixed_t curz,
			 fixed_t destx,
			 fixed_t desty,
			 fixed_t destz)
{
	return K_Dist2D(0,
		      curz / FRACUNIT,
		      K_Dist2D(curx / FRACUNIT, cury / FRACUNIT, destx / FRACUNIT, desty / FRACUNIT),
		      destz / FRACUNIT);
}*/

// This one uses map scaling instead, use in case of loss of depth on mobjscaled maps.
UINT32 K_IntDistanceForMap(fixed_t curx,
			   fixed_t cury,
			   fixed_t curz,
			   fixed_t destx,
			   fixed_t desty,
			   fixed_t destz)
{
	return K_Dist2D(0,
			curz / mapobjectscale,
			K_Dist2D(curx / mapobjectscale,
				 cury / mapobjectscale,
				 destx / mapobjectscale,
				 desty / mapobjectscale),
			destz / mapobjectscale);
}


/**	\brief	Prevent overpowered rolls while under the effects of Alt. Shrink.

	\param	item		the item to check

	\return	Don't double for this item?
*/
boolean K_DontDoubleMyItems(kartresult_t *res)
{
	if (res == NULL)
		return false;
	return ((res->flags & KRF_DONTCHANGERESULT) == KRF_DONTCHANGERESULT);
	/*return type == KITEM_BALLHOG || type == KITEM_SPB
	    || type == KITEM_INVINCIBILITY || type == KITEM_GROW
	    || type == KITEM_BUBBLESHIELD || type == KITEM_FLAMESHIELD
	    || type == KITEM_ROCKETSNEAKER || type == KITEM_SHRINK
	    || type == KITEM_HYUDORO
	    || (type == KITEM_BANANA && amount >= 4) // Do you REALLY want 20 bananas?
	    || (type == KITEM_JAWZ && amount >= 2) // Or 4 Jawz?
	    || type >= KITEM_FIRSTFREESLOT; // TODO: excludes custom items for now*/
}

void K_SetItemOut(player_t *player, kartitemtype_e itemtype, itemflags_t flags)
{
	player->itemflags |= flags;
	player->equippeditem = itemtype;
}

void K_UnsetItemOutEx(player_t *player, itemflags_t flags)
{
	player->itemflags &= ~(flags);
	player->bananadrag = 0;
	player->equippeditem = KITEM_NONE;
}

static sfxenum_t resultfx[] = {
	[KITEMBLINK_NORMAL] = sfx_itrolf,
	[KITEMBLINK_MASHED] = sfx_itrolm,
	[KITEMBLINK_KARMA] = sfx_itrolk,
	[KITEMBLINK_DEBUG] = sfx_dbgsal,
};

void K_AddPlayerReserveItem(player_t *player, kartitemtype_e type, UINT8 amount)
{
	// will disallow adding non-matching items
	if (player->itemreservetype != 0 && type != player->itemreservetype)
	{
		return;
	}

	// adds items to the reserve instead of setting
	player->itemreservetype = type;
	player->itemreserveamount += amount;
}

void K_AwardPlayerItemOrReserve(player_t *player, kartitemtype_e type, UINT8 amount, boolean nodoubles, kartitemblink_e blink)
{
	if (player->itemtype == KITEM_NONE && player->itemamount == 0)
	{
		K_AwardPlayerItem(player, type, amount, nodoubles, blink);
	}
	else
	{
		if (K_IsAltShrunk(player) && !nodoubles && blink != KITEMBLINK_DEBUG)
			amount *= 2;

		K_AddPlayerReserveItem(player, type, amount);
	}
}

// be careful with invalid item types here
void K_AwardPlayerItem(player_t *player, kartitemtype_e type, UINT8 amount, boolean nodoubles, kartitemblink_e blink)
{
	K_BotResetItemConfirm(player, true);

	player->itemblink = TICRATE;
	player->itemblinkmode = blink;
	player->itemroulette = 0; // Since we're done, clear the roulette number
	player->roulettetype = KROULETTETYPE_NORMAL; // This too

	if (P_IsDisplayPlayer(player))
		S_StartSound(NULL, resultfx[blink]);

	player->itemtype = type;

	if (K_IsAltShrunk(player) && !nodoubles && blink != KITEMBLINK_DEBUG)
		amount *= 2;

	player->itemamount = amount;

	if (itemlistactive)
		K_AddItemRollToList((INT32)(player - players), type, amount);
}

void K_TransferPlayerReserveItem(player_t *player)
{
	UINT8 transferamount = player->itemreserveamount;
	if (transferamount == 0 || player->itemreservetype == 0)
		return;
	if (!P_CanPickupItem(player, PICKUPITEM_RESERVE))
		return;

	K_BotResetItemConfirm(player, true);
	
	if (K_GetItemFlags(player->itemreservetype) & KIF_SINGLEFROMRESERVE)
	{
		transferamount = 1;
	}

	if (player->itemreservetype == KITEM_BONUSITEMROLL)
	{
		S_StartSound(player->mo, sfx_kc2e);
		K_StartRoulette(player, KROULETTETYPE_BONUS);
	}
	else
	{
		player->itemblink = TICRATE;
		player->itemblinkmode = 0;
		if (P_IsDisplayPlayer(player))
		{
			S_StartSound(NULL, resultfx[0]);
		}

		player->itemtype = player->itemreservetype;
		player->itemamount = transferamount;

		if (itemlistactive)
			K_AddItemRollToList((INT32)(player - players), player->itemreservetype, player->itemreserveamount);
	}

	// update reserve
	player->itemreserveamount -= transferamount;
	if (player->itemreserveamount == 0)
	{
		player->itemreservetype = 0;
	}
}

void K_AwardPlayerResult(player_t *player, kartresult_t *result, kartitemblink_e blink)
{
	kartitemtype_e type = result != NULL ? result->type : MAXKARTITEMS;
	UINT8 amount = result != NULL ? result->amount : 1;
	const boolean nodoubles = K_DontDoubleMyItems(result);

	K_AwardPlayerItem(player, type, amount, nodoubles, blink);

	if (result == NULL)
		return;

	if (result->basecooldown > 0)
		result->cooldown = result->basecooldown;
}

static fixed_t K_ItemOddsScale(UINT8 numPlayers, boolean spbrush)
{
	// CEP: due to how baseplayer works, 17P+ lobbies will STILL have the disastrous odds of 0.22 prior, if not WORSE
	// let's try adding another condition

	const UINT8 basePlayer = 16; // The player count we design most of the game around.
	const UINT8 vanillaMax = 17; // CEP: Maximum players in "vanilla" (non-30P) clients.
	const UINT8 extPlayer = 24; // CEP: Cap for 17P+ so that odds don't get too muddled.

	UINT8 playerCount = (spbrush ? 2 : numPlayers);
	fixed_t playerScaling = 0;

	// Then, it multiplies it further if the player count isn't equal to basePlayer.
	// This is done to make low player count races more interesting and high player count rates more fair.
	// (If you're in SPB mode and in 2nd place, it acts like it's a 1v1, so the catch-up game is not weakened.)
	if (playerCount < basePlayer)
	{
		// Less than basePlayer: increase odds significantly.
		// 2P: x1.4
		playerScaling = (basePlayer - playerCount) * (FRACUNIT / 10);
	}
	else if (playerCount > basePlayer)
	{
		// More than basePlayer: reduce odds slightly.

		// CEP: 17P+ adjustments
		if (playerCount < vanillaMax)
		{
			// Less than vanillaMax: Use standard calculations.
			// 16P: x0.6
			playerScaling = (basePlayer - playerCount) * (FRACUNIT / 20);

		}
		else if (playerCount > vanillaMax)
		{
			// More than vanillaMax: Increase odds to fit with the increased playercount
			// 24P: x0.6
			// 30P: x0.45
			playerScaling = (basePlayer - min(extPlayer, playerCount)) * (FRACUNIT / 40); // adding a cap here to be sure
		}
	}

	return playerScaling;
}

UINT32 K_ScaleItemDistance(UINT32 distance, UINT8 numPlayers, boolean spbrush)
{
	if (mapobjectscale != FRACUNIT)
	{
		// Bring back to normal scale.
		distance = FixedDiv(distance * FRACUNIT, mapobjectscale) / FRACUNIT;
	}

	if (franticitems == true)
	{
		// Frantic items pretends everyone's farther apart, for crazier items.
		distance = (15 * distance) / 14;
	}

	if (numPlayers > 0)
	{
		// Items get crazier with the fewer players that you have.
		distance = FixedMul(
			distance * FRACUNIT,
			FRACUNIT + (K_ItemOddsScale(numPlayers, spbrush) / 2)
		) / FRACUNIT;
	}

	return distance;
}

#define INVODDS 30

// Prevent integer overflows; don't let this go past 16383
#define SMONITOR_DESPERATION 4
#define MAXSMONITORODDS ((MAXPROBABILITY * 2) * SMONITOR_DESPERATION)

// Odds value for the S-Monitor to force itself on trailing players.
#define SMONITOR_FORCEODDS (MAXSMONITORODDS / 2)

static INT32 K_KartGetSMonitorOdds(UINT32 dist)
{
	const UINT32 forcedist = (UINT32)(K_LegacyOddsMode() ? SMONITORCONGADIST : SMONITORDIST);
	// If you're far enough for this to be in your item slot, you're far enough to SERIOUSLY need this.
	return dist >= forcedist ? MAXSMONITORODDS : 0;
}

// updates all result cooldown timers, and sets cooldowns for "unique" items
void K_UpdateItemCooldown(void)
{
	INT32 i;

	// tick down the cooldown timers
	for (i = 0; i < numkartresults; i++)
		if (kartresults[i].cooldown > 0)
			kartresults[i].cooldown--;

	// figure out which unique items need their cooldown set
	boolean setcooldown[MAXKARTITEMS] = {0};

	// start by checking each player's item slot (KIF_UNIQUESLOT)
	for (i = 0; i < MAXPLAYERS; i++)
	{
		player_t *player = &players[i];

		if (!playeringame[i] || player->spectator || player->exiting > 0)
			continue;

		if (gametypes[gametype]->rules & GTR_BUMPERS && player->bumper == 0)
			continue;

		// special case for legacy shrink
		if (player->growshrinktimer < 0 && !K_IsKartItemAlternate(KITEM_SHRINK))
			setcooldown[KITEM_SHRINK] = true;

		if (player->itemtype <= 0 || player->itemtype >= numkartitems || player->itemamount <= 0)
			continue;

		if (K_GetItemFlags(player->itemtype) & KIF_UNIQUESLOT)
			setcooldown[player->itemtype] = true;
	}

	// next, check for dropped items (KIF_UNIQUEDROP)
	for (thinker_t *th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
	{
		mobj_t *mobj = (mobj_t *)th;

		if (P_MobjWasRemoved(mobj))
			continue;

		switch (mobj->type)
		{
		case MT_FLOATINGITEM:
			if (mobj->threshold > 0 && mobj->threshold < numkartitems && K_GetItemFlags(mobj->threshold) & KIF_UNIQUEDROP)
				setcooldown[mobj->threshold] = true;
			break;

		case MT_SPB: // SPBs in the field also count
			setcooldown[KITEM_SPB] = true;
			break;

		default:
			break;
		}
	}

	// now it's FINALLY time to set cooldowns!
	// unique item cooldowns only apply to race odds, unless it's an indirect item
	for (i = 0; i < numkartresults; i++)
	{
		kartresult_t *result = &kartresults[i];
		if (result->isalt == K_IsKartItemAlternate(result->type)
		 && (gametypes[gametype]->rules & GTR_RACEODDS || result->flags & KRF_INDIRECTITEM)
		 && setcooldown[result->type])
			result->cooldown = result->basecooldown;
	}

	// all indirect items share a single timer, whichever is higher
	K_SetIndirectItemCooldown(K_GetIndirectItemCooldown());
}

tic_t K_GetIndirectItemCooldown(void)
{
	tic_t maxcooldown = 0;
	for (UINT8 i = 0; i < numkartresults; i++)
	{
		if (kartresults[i].flags & KRF_INDIRECTITEM)
			maxcooldown = max(maxcooldown, kartresults[i].cooldown);
	}
	return maxcooldown;
}

void K_SetIndirectItemCooldown(tic_t cooldown)
{
	for (UINT8 i = 0; i < numkartresults; i++)
	{
		if (kartresults[i].flags & KRF_INDIRECTITEM)
			kartresults[i].cooldown = cooldown;
	}
}

#define LUNATIC_RUNNERAUG_CRUNCHER (29127) // FRACUNIT / 2.25

static INT32 GetItemOdds(kartroulette_t *roulette, kartresult_t *result, UINT8 *forceme)
{
	INT32 newodds;
	INT32 i;

	SINT8 first = -1, second = -1;
	UINT32 secondToFirst = UINT32_MAX;

	UINT8 flags = result->flags;

	if (result->cvar->value == 0 && !modeattacking)
		return 0;

	if (kartitems[result->type].altenabled != result->isalt)
		return 0; // wrong alt item result

	/*
	if (roulette->bot)
	{
		// TODO: Item use on bots should all be passed-in functions.
		// Instead of manually inserting these, it should return 0
		// for any items without an item use function supplied

		switch (item)
		{
			case KITEM_SNEAKER:
				break;
			default:
				return 0;
		}
	}
	*/

	INT32 oddsmul = BASEODDSMUL;

	// Item type used for actual odds retrieval and cooldown assignments.
	UINT8 oddstable;

	if (gametypes[gametype]->rules & GTR_BATTLEODDS)
		oddstable = ODDS_BATTLE;
	else if (gametypes[gametype]->rules & GTR_RACEODDS)
		oddstable = ODDS_RACE;
	else
		oddstable = ODDS_SPECIAL;

	I_Assert(roulette->pos < oddstablemax[oddstable]); // DO NOT allow positions past the bounds of the table

	if (gametypes[gametype]->rules & GTR_BATTLEODDS)
		oddsmul = BATTLEODDSMUL;

	// TODO: braaap (make a separate table for the current level!)
	newodds = result->odds[oddstable][roulette->pos];

	// Blow up the odds with a multiplier.
	newodds *= oddsmul;

	INT32 shieldtype = K_GetShieldFromItem(result->type);

	roulette->pexiting = 0;
	roulette->pingame = 0;
	roulette->firstDist = UINT32_MAX;

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator)
			continue;

		if (!(gametypes[gametype]->rules & GTR_BUMPERS) || players[i].bumper)
			roulette->pingame++;

		if (players[i].exiting)
			roulette->pexiting++;

		if (shieldtype != KSHIELD_NONE && ((shieldtype == K_GetShieldFromItem(players[i].itemtype))
			|| (shieldtype == K_GetShieldFromPlayer(&players[i]))))
		{
			// Don't allow more than one of each shield type at a time
			return 0;
		}

		if (players[i].mo && gametype == GT_RACE)
		{
			if (players[i].position == 1 && first == -1)
				first = i;
			if (players[i].position == 2 && second == -1)
				second = i;
		}
	}

	if (first != -1 && second != -1) // calculate 2nd's distance from 1st, for SPB
	{

		if (!K_LegacyOddsMode())
		{
			roulette->firstDist = players[first].distancetofinish;

			if (mapobjectscale != FRACUNIT)
			{
				roulette->firstDist = FixedDiv(roulette->firstDist * FRACUNIT, mapobjectscale) / FRACUNIT;
			}

			secondToFirst = K_ScaleItemDistance(
				players[second].distancetofinish - players[first].distancetofinish,
				roulette->pingame, roulette->spbrush
			);
		}
		else
		{
			secondToFirst = P_AproxDistance(P_AproxDistance(
				players[first].mo->x/4 - players[second].mo->x/4,
				players[first].mo->y/4 - players[second].mo->y/4),
				players[first].mo->z/4 - players[second].mo->z/4);

			// Scale it to prevent overflow issues.
			secondToFirst = (secondToFirst / FRACUNIT)*2;

			secondToFirst = K_ScaleItemDistance(
				secondToFirst,
				roulette->pingame, roulette->spbrush
			);
		}
	}

	// TODO: convert into uniqueodds functions (what happened to "already shrunk"?)
	if (result->type == KITEM_SHRINK)
	{
		if (!K_IsKartItemAlternate(KITEM_SHRINK))
		{
			if (roulette->pingame-1 <= roulette->pexiting)
				newodds = 0;
		}
		else
		{
			if (roulette->rival || roulette->is_shrunk)
			{
				// Rival bot or already shrunk. DON'T roll another.
				newodds = 0;
			}
		}
	}

	if (result->type == KITEM_SUPERRING && !K_RingsActive())
	{
		// Don't roll Super Ring when rings are generally off.
		newodds = 0;
	}

	if (result->unique_odds[oddstable])
	{
		// This item has unique odds!

		//CONS_Printf("Unique odds found. Assigning uoddsfunc...");
		useoddsfunc_f *uoddsfunc = result->unique_odds[oddstable];

		//CONS_Printf("Running...");
		newodds = uoddsfunc(newodds, roulette, result, forceme);
		//CONS_Printf("OK!\n");
	}

	// In very small matches, remove the stupid bottom half item limiter
	if (roulette->pingame < 6)
	{
		flags &= ~KRF_NOTFORBOTTOM;
	}

	if (newodds == 0)
	{
		// Nothing else we want to do with odds matters at this point :p
		return newodds;
	}

	UINT8 aug_idx = K_LegacyOddsMode() ? 1 : 0;

	if ((flags & KRF_RUNNERAUGMENT) && (result->augcvar[aug_idx] != NULL))
	{
		// These odds get stronger as 1st's frontrun increases.
		INT32 runner_distval = result->augcvar[aug_idx]->value;

		if ((grandprixinfo.gp == true) && (grandprixinfo.lunaticmode == true))
		{
			// Lunatic Mode: Divide this distance by 2.25 for
			// aggressive frontrun prevention.
			runner_distval = FixedMul(runner_distval, LUNATIC_RUNNERAUG_CRUNCHER);
		}

		if (roulette->rival_frontrunner == true || (roulette->is_shrunk == true && cv_kartaltshrink_runneranger.value))
		{
			// The rival is frontrunning. Be *especially* vicious against them!
			// Players with Alt. Shrink also get to be more vicious...
			runner_distval = (runner_distval / 2);
		}

		const INT32 distFromStart = max(secondToFirst - runner_distval, 0);
		const INT32 distRange = (7 * runner_distval / 2) - runner_distval;
		const INT32 mulMax = 24;

		INT32 multiplier = (distFromStart * mulMax) / distRange;

		if (multiplier < 0)
			multiplier = 0;
		if (multiplier > mulMax)
			multiplier = mulMax;

		newodds *= multiplier;
	}

	if (result->cooldown > 0)
	{
		// This item is on cooldown; don't let it get rolled.
		newodds = 0;
	}
	else if (flags & KRF_COOLDOWNONSTART && K_InStartCooldown())
	{
		// This item should not appear at the beginning of a race. (Usually really powerful crowd-breaking items)
		newodds = 0;
	}
	else if (!K_LegacyOddsMode() && flags & KRF_NOTNEAREND && roulette->ourDist < (UINT32)ENDDIST)
	{
		// This item should not appear at the end of a race. (Usually trap items that lose their effectiveness)
		newodds = 0;
	}
	else if (flags & KRF_NOTFORBOTTOM && roulette->inBottom && leveltime >= (30*TICRATE)+starttime)
	{
		// This item should not appear for losing players. (Usually items that feel less effective at these positions)
		newodds = 0;
	}
	else if (flags & KRF_HIDEFROMSPB && (spbplace != -1))
	{
		// This item doesn't appear if an SPB is chasing a player.
		newodds = 0;
	}
	else if (flags & KRF_POWERITEM)
	{
		// This item is a "power item". This activates "frantic item" toggle related functionality.
		fixed_t fracOdds = newodds * FRACUNIT;

		if (franticitems == true)
		{
			// First, power items multiply their odds by 2 if frantic items are on; easy-peasy.
			fracOdds *= 2;
		}

		if (roulette->rival == true)
		{
			// The Rival bot gets frantic-like items, also :p
			fracOdds *= 2;
		}
		else if (roulette->is_shrunk == true)
		{
			// As do Alt. Shrunk players
			fracOdds *= 2;
		}

		fracOdds = FixedMul(fracOdds, FRACUNIT + K_ItemOddsScale(roulette->pingame, roulette->spbrush));

		if (roulette->mashed > 0)
		{
			// Lastly, it *divides* it based on your mashed value, so that power items are less likely when you mash.
			fracOdds = FixedDiv(fracOdds, FRACUNIT + roulette->mashed);
		}

		newodds = fracOdds / FRACUNIT;
	}

	return newodds;
}

#undef LUNATIC_RUNNERAUG_CRUNCHER

void K_KartGetItemOdds(kartroulette_t *roulette, INT32 outodds[static MAXKARTRESULTS])
{
	// Reset forceme
	memset(roulette->forceme, 0, sizeof(roulette->forceme));

	for (UINT8 i = 0; i < numkartresults; i++)
	{
		outodds[i] = GetItemOdds(roulette, &kartresults[i], &roulette->forceme[i]);
	}
};

//{ SRB2kart Roulette Code - Distance Based, yes waypoints

UINT8 K_FindUseodds(const player_t *player, fixed_t mashed, UINT32 pdis, UINT8 bestbumper, boolean spbrush)
{
	fixed_t oddsfac = max(FRACUNIT, (MAXODDS * FRACUNIT) / 8);
	UINT8 i, j;
	UINT8 useodds = 0;
	UINT8 disttable[(MAXODDS - 1) * 2];
	UINT8 distlen = 0;
	boolean oddsvalid[MAXODDS];

	boolean rivalodds = false;
	boolean rivalrunner = false;

	boolean shrinkodds = false;
	//boolean shrinkfreebie = false;

	// Unused now, oops :V
	(void)bestbumper;

	if (player->bot && player->botvars.rival)
	{
		// Rival bots get crazier items.
		rivalodds = true;
	}
	
	if (K_IsAltShrunk(player))
	{
		// Players using Alt. Shrink ALSO get crazier items.
		shrinkodds = true;
	}

	if (player->bot && player->botvars.rival && player->position <= 1)
	{
		// A rival is frontrunning!
		rivalrunner = true;
	}

	INT32 itemodds[MAXKARTRESULTS];
	kartroulette_t roulette = {
		.pdis = pdis,
		.playerpos = player->position,
		//.pos = i,
		.ourDist = player->distancetofinish,
		.clusterDist = player->distancefromcluster,
		.mashed = mashed,
		.spbrush = spbrush,
		.bot = player->bot,
		.shrink_freebie = (player->roulettetype == KROULETTETYPE_SHRINK),
		.is_shrunk = shrinkodds,
		.rival = rivalodds,
		.rival_frontrunner = rivalrunner,
		.inBottom = K_IsPlayerLosing(player),
	};

	for (i = 0; i < MAXODDS; i++)
	{
		boolean available = false;

		if ((gametypes[gametype]->rules & GTR_BATTLEODDS) && i > 1)
		{
			oddsvalid[i] = false;
			break;
		}

		roulette.pos = i;
		K_KartGetItemOdds(&roulette, itemodds);

		for (j = 0; j < numkartresults; j++)
		{
			if (itemodds[j] > 0)
			{
				available = true;
				break;
			}
		}

		oddsvalid[i] = available;
	}

#define SETUPDISTTABLE(odds, num) \
	if (oddsvalid[odds]) \
		for (i = num; i; --i) \
        { \
			disttable[distlen++] = odds; \
			distlen = min(sizeof(disttable) - 1, distlen); \
        }

	if (gametypes[gametype]->rules & GTR_BATTLEODDS) // Battle Mode
	{
		if (player->roulettetype == KROULETTETYPE_KARMA && oddsvalid[1] == true)
		{
			// 1 is the extreme odds of player-controlled "Karma" items
			useodds = 1;
		}
		else
		{
			useodds = 0;

			if (oddsvalid[0] == false && oddsvalid[1] == true)
			{
				// try to use karma odds as a fallback
				useodds = 1;
			}
		}
	}
	else if (gametypes[gametype]->rules & GTR_RACEODDS)
	{

		INT32 tablediv = FixedMul(2, oddsfac);
		INT32 jj;

		// "Why j instead of i"?
		// Using i causes an infinite loop due to SETUPDISTTABLE. Yep.
		for (j = 0; j < MAXODDS; j++)
		{
			if (j == (MAXODDS - 1))
			{
				// Attempt to replicate vanilla behavior; Useodds 8 is set up like this.
				SETUPDISTTABLE(j,1);
			}
			else
			{
				jj = max(1, ((j - 3) / tablediv) + 1);

				if (jj < 1)
				{
					jj = 1;
				}

				//CONS_Printf("SETUPDISTTABLE(%d, %d)\n", j, jj);

				SETUPDISTTABLE(j,jj);
			}
		}

		/*SETUPDISTTABLE(0,1);
		SETUPDISTTABLE(1,1);
		SETUPDISTTABLE(2,1);
		SETUPDISTTABLE(3,2);
		SETUPDISTTABLE(4,2);
		SETUPDISTTABLE(5,3);
		SETUPDISTTABLE(6,3);
		SETUPDISTTABLE(7,1);*/

		const INT32 usedistvar = FixedDiv(ACTIVEDISTVAR, oddsfac);

		if (pdis == 0)
			useodds = disttable[0];
		else if (pdis > (UINT32)ACTIVEDISTVAR * ((12 * distlen) / sizeof(disttable)))
			useodds = disttable[distlen ? distlen - 1 : distlen];
		else
		{
			for (i = 1; i < (sizeof(disttable) - 1); i++)
			{
				INT32 distcalc = min(distlen-1u, (i * distlen) / sizeof(disttable));

				if (pdis <= (UINT32)usedistvar * distcalc)
				{
					useodds = disttable[distcalc];
					break;
				}
			}
		}
	}

#undef SETUPDISTTABLE

	return min(MAXODDS - 1, useodds);
}

UINT16 roulette_size;
INT32 K_GetRollingRouletteItem(player_t *player)
{
	UINT8* translation = K_GetRollableItems();
	return translation[(player->itemroulette / 3) % roulette_size];
}

INT32 K_GetRollingRouletteItemOffset(player_t *player, INT16 offset, INT32 animspeed)
{
	UINT8* translation = K_GetRollableItems();
	INT32 icon = ((player->itemroulette + animspeed)/animspeed);
	return translation[((icon + offset + roulette_size) % roulette_size)];
}

UINT8* K_GetRollableItems(void)
{
	static UINT8 translation[MAXKARTRESULTS];

	static INT16 odds_cached = -1;

	// Race odds have more columns than Battle
	const UINT8 EMPTYODDS[MAXODDS] = {0}; // or not

	if (odds_cached != gametype)
	{
		UINT8 oddstable = 0;
		kartitemtype_e seen[MAXKARTITEMS];
		size_t numseen = 0, j;

		roulette_size = 0;

		if (gametypes[gametype]->rules & GTR_BATTLEODDS)
			oddstable = ODDS_BATTLE;
		else
			oddstable = ODDS_RACE;

		for (UINT8 i = 0; i < numkartresults; i++)
		{
			kartresult_t *result = &kartresults[i];

			if (K_GetItemFlags(result->type) & KIF_HIDEFROMROULETTE)
				continue;

			for (j = 0; j < numseen; j++)
				if (seen[j] == result->type)
					break;

			if (j == numseen && memcmp(result->odds[oddstable], EMPTYODDS, sizeof(EMPTYODDS)))
				translation[roulette_size++] = seen[numseen++] = result->type;
		}
		odds_cached = gametype;
	}

	return translation;
}

// Legacy odds are fickle and finicky, so we exaggerate distances
// to simulate parity with pathfind odds.
#define LEGACYODDSEXAGGERATE (2*FRACUNIT/3)

/**	\brief	Gets the distance between a player and a position by chaining all other players
 * 			between them and the target position.

	\param	player	player object
	\param  startPos first position to trace to the player from

	\return	(UINT32) pdis
*/
UINT32 K_GetCongaLineDistance(const player_t *player, UINT8 startPos)
{
	SINT8 sortedPlayers[MAXPLAYERS];
	UINT8 sortLength = 0;

	UINT32 pdis = 0;
	INT32 i;

	memset(sortedPlayers, -1, sizeof(sortedPlayers));

	if (player->mo != NULL && P_MobjWasRemoved(player->mo) == false && player->position > 0)
	{
		// Sort all of the players ahead of you.
		// Then tally up their distances in a conga line.

		// This will create a much more consistent item
		// distance algorithm than the "spider web" thing
		// that it was doing before.

		// Add yourself to the list.
		// You'll always be the end of the list,
		// so we can also calculate the length here.
		sortedPlayers[ player->position - 1 ] = player - players;
		sortLength = player->position;

		// Will only need to do this if there's goint to be
		// more than yourself in the list.
		if (sortLength > 1)
		{
			SINT8 firstIndex = -1;
			SINT8 secondIndex = -1;
			INT32 startFrom = INT32_MAX;

			// Add all of the other players.
			for (i = 0; i < MAXPLAYERS; i++)
			{
				INT32 pos = INT32_MAX;

				if (!playeringame[i] || players[i].spectator)
				{
					continue;
				}

				if (players[i].mo == NULL || P_MobjWasRemoved(players[i].mo) == true)
				{
					continue;
				}

				pos = players[i].position;

				if ((pos <= 0) || (pos > MAXPLAYERS) || (pos < startPos))
				{
					// Invalid position.
					continue;
				}

				if (pos >= player->position)
				{
					// Tied / behind us.
					// Also handles ourselves, obviously.
					continue;
				}

				// Ties are done with port priority, if there are any.
				if (sortedPlayers[ pos - 1 ] == -1)
				{
					sortedPlayers[ pos - 1 ] = i;
				}
			}

			// The chance of this list having gaps is improbable,
			// but not impossible. So we need to spend some extra time
			// to prevent the gaps from mattering.
			for (i = 0; i < sortLength-1; i++)
			{
				if (sortedPlayers[i] >= 0 && sortedPlayers[i] < MAXPLAYERS)
				{
					// First valid index in the list found.
					firstIndex = sortedPlayers[i];

					// Start the next loop after this player.
					startFrom = i + 1;
					break;
				}
			}

			if (firstIndex >= 0 && firstIndex < MAXPLAYERS
				&& startFrom < sortLength)
			{
				// First index is valid, so we can
				// start comparing the players.

				player_t *firstPlayer = NULL;
				player_t *secondPlayer = NULL;

				for (i = startFrom; i < sortLength; i++)
				{
					if (sortedPlayers[i] >= 0 && sortedPlayers[i] < MAXPLAYERS)
					{
						secondIndex = sortedPlayers[i];

						firstPlayer = &players[firstIndex];
						secondPlayer = &players[secondIndex];

						// Add the distance to the player behind you.
						// At a (relative to map) integer scale using basic distancing
						// arithmetic; more accurate and less concern for overflows.
						// TODO: Overflow-safe addition (caps at UINT32_MAX).
						pdis += K_IntDistanceForMap(
								secondPlayer->mo->x,
								secondPlayer->mo->y,
								secondPlayer->mo->z,
								firstPlayer->mo->x,
								firstPlayer->mo->y,
								firstPlayer->mo->z);

						// Advance to next index.
						firstIndex = secondIndex;
					}
				}
			}
		}
	}

	return pdis;
}

UINT32 K_CalculateInitalPDIS(const player_t *player, UINT8 pingame)
{
	UINT8 i;
	UINT32 pdis = 0;

	(void)pingame;

	if (!K_LegacyOddsMode())
	{
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (playeringame[i] && !players[i].spectator
				&& players[i].position == 1)
			{
				// This player is first! Yay!

				if (player->distancetofinish <= players[i].distancetofinish)
				{
					// Guess you're in first / tied for first?
					pdis = 0;
				}
				else
				{
					// Subtract 1st's distance from your distance, to get your distance from 1st!
					pdis = player->distancetofinish - players[i].distancetofinish;
				}
				break;
			}
		}
	}
	else
	{
		pdis = K_GetCongaLineDistance(player, 1);

		// Exaggerate odds; don't you love the legacy system? :Trollic:
		pdis = FixedMul(pdis, LEGACYODDSEXAGGERATE);
	}

	if (player->itemroulette && (player->roulettetype == KROULETTETYPE_SHRINK))
	{
		// Alt. Shrink freebie roll, divide by 4 to curb on ESPECIALLY stupid rolls
		pdis /= 4;
	}

	return pdis;
}

#undef LEGACYODDSEXAGGERATE

UINT32 K_CalculatePDIS(const player_t *player, UINT8 numPlayers, boolean *spbrush)
{
	UINT32 pdis = 0;

	pdis = K_CalculateInitalPDIS(player, numPlayers);

	if (cv_kartspbrush.value && spbplace != -1 && player->position == spbplace+1)
	{
		// SPB Rush Mode: It's 2nd place's job to catch-up items and make 1st place's job hell
		pdis = (3 * pdis) / 2;
		*spbrush = true;
	}

	pdis = K_ScaleItemDistance(pdis, numPlayers, *spbrush);

	if (player->bot && player->botvars.rival)
	{
		// Rival has better odds :)
		pdis = (15 * pdis) / 14;
	}

	// Can almost barely overflow this calc, fudge to prevent this.
	if (pdis > 30000)
		pdis = 30000;

	return pdis;
}

static boolean K_BattleForceSPB(player_t *player)
{
	boolean battlecond = ((gametypes[gametype]->rules & GTR_WANTED) && (gametypes[gametype]->rules & GTR_WANTEDSPB) && (mostwanted != -1) && (!K_IsPlayerMostWanted(player)));

	return battlecond;
}

void K_KartItemRoulette(player_t *player, ticcmd_t *cmd)
{
	INT32 i;
	UINT8 pingame = 0;
	UINT8 roulettestop;
	UINT32 pdis = 0;
	UINT8 useodds = 0;
	INT32 spawnchance[MAXKARTRESULTS];
	INT64 totalspawnchance = 0; // 75-scale numbers are going to get BIG. This is for paranoia's sake.
	UINT8 bestbumper = 0;
	fixed_t mashed = 0;
	boolean dontforcespb = false;
	boolean spbrush = false;
	INT32 roulettespeed = 3;

	// This makes the roulette cycle through items - if this is 0, you shouldn't be here.
	if (!player->itemroulette)
		return;
	player->itemroulette++;

	// Gotta check how many players are active at this moment.
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator)
			continue;
		pingame++;
		if (players[i].exiting)
			dontforcespb = true;
		if (players[i].bumper > bestbumper)
			bestbumper = players[i].bumper;
	}

	// No forced SPB in 1v1s, it has to be randomly rolled
	if (pingame <= 2)
		dontforcespb = true;

	// This makes the roulette produce the random noises.
	if (cv_fancyroulette.value)
	{
		roulettespeed = cv_fancyroulettespeed.value;
	}
	if ((player->itemroulette % roulettespeed) == 1 && P_IsDisplayPlayer(player))
	{
#define PLAYROULETTESND S_StartSound(NULL, sfx_itrol1 + ((player->itemroulette / roulettespeed) % 8))
		for (i = 0; i <= r_splitscreen; i++)
		{
			if (player == &players[displayplayers[i]] && players[displayplayers[i]].itemroulette)
				PLAYROULETTESND;
		}
#undef PLAYROULETTESND
	}

	roulettestop = TICRATE + (3*(pingame - player->position));

	// If the roulette finishes or the player presses BT_ATTACK, stop the roulette and calculate the item.
	// I'm returning via the exact opposite, however, to forgo having another bracket embed. Same result either way, I think.
	// Finally, if you get past this check, now you can actually start calculating what item you get.
	if ((cmd->buttons & BT_ATTACK) && (player->itemroulette >= roulettestop)
		&& (K_RingsActive() || (modeattacking == ATTACKING_NONE))
		&& !(player->itemflags & (IF_ITEMOUT|IF_EGGMANOUT|IF_USERINGS))
		&& !(player->itemusecooldown))
	{
		// Mashing reduces your chances for the good items
		mashed = FixedDiv((player->itemroulette)*FRACUNIT, ((TICRATE*3)+roulettestop)*FRACUNIT) - FRACUNIT;
	}
	else if (!(player->itemroulette >= (TICRATE*3)))
		return;


	pdis = K_CalculatePDIS(player, pingame, &spbrush);

	// SPECIAL CASE No. 1:
	// Fake Eggman items
	if (player->roulettetype == KROULETTETYPE_EGGMAN)
	{
		player->eggmanexplode = 4*TICRATE;
		player->itemroulette = 0;
		player->roulettetype = KROULETTETYPE_NORMAL;
		if (P_IsDisplayPlayer(player))
			S_StartSound(NULL, sfx_itrole);
		return;
	}

	// SPECIAL CASE No. 2:
	// Give a debug item instead if specified
	if (cv_kartdebugitem.value != 0 && !modeattacking)
	{
		K_AwardPlayerItem(player, cv_kartdebugitem.value, cv_kartdebugamount.value, true, KITEMBLINK_DEBUG);
		return;
	}

	// SPECIAL CASE No. 3:
	// This Gametype never specified an odds type. Roll something random please!
	if (!(gametypes[gametype]->rules & GTR_RACEODDS) && !(gametypes[gametype]->rules & GTR_BATTLEODDS))
	{
		UINT8 itemroll = P_RandomRange(PR_ITEM_ROULETTE, 0, numkartresults - 1);
		K_AwardPlayerResult(player, &kartresults[itemroll], KITEMBLINK_NORMAL);
		return;
	}

	// SPECIAL CASE No. 4:
	// Record Attack / alone mashing behavior
	if ((modeattacking || pingame == 1)
		&& ((gametypes[gametype]->rules & GTR_RACEODDS)
		|| ((gametypes[gametype]->rules & GTR_BATTLEODDS) && (itembreaker || bossinfo.boss))))
	{
		if ((gametypes[gametype]->rules & GTR_RACEODDS))
		{
			if (mashed && (K_ItemResultEnabled(K_GetKartResult("superring")) || (modeattacking && K_RingsActive()))) // ANY mashed value? You get rings.
			{
				K_AwardPlayerResult(player, K_GetKartResult("superring"), KITEMBLINK_MASHED);
			}
			else
			{
				if (modeattacking || K_ItemResultEnabled(K_GetKartResult("sneaker"))) // Waited patiently? You get a sneaker!
					K_AwardPlayerResult(player, K_GetKartResult("sneaker"), KITEMBLINK_NORMAL);
				else  // Default to sad if nothing's enabled...
					K_AwardPlayerResult(player, NULL, KITEMBLINK_NORMAL);
			}
		}
		else if (gametypes[gametype]->rules & GTR_BATTLEODDS)
		{
			if (mashed && (bossinfo.boss || K_ItemResultEnabled(K_GetKartResult("banana"))) && !itembreaker) // ANY mashed value? You get a banana.
				K_AwardPlayerResult(player, K_GetKartResult("banana"), KITEMBLINK_MASHED);
			else if (bossinfo.boss)
				K_AwardPlayerResult(player, K_GetKartResult("orbinaut"), KITEMBLINK_NORMAL);
			else if (itembreaker)
				K_AwardPlayerResult(player, K_GetKartResult("sneaker"), KITEMBLINK_MASHED);
		}
		return;
	}

	// SPECIAL CASE No. 5:
	// Being in ring debt occasionally forces Super Ring on you if you mashed
	if (K_ItemResultEnabled(K_GetKartResult("superring")) && mashed && player->rings < 0)
	{
		INT32 debtamount = min(abs(player->ringmin), abs(player->rings));
		if (P_RandomChance(PR_ITEM_ROULETTE, (debtamount*FRACUNIT)/abs(player->ringmin)))
		{
			K_AwardPlayerResult(player, K_GetKartResult("superring"), KITEMBLINK_MASHED);
			return;
		}
	}

	// SPECIAL CASE No. 6:
	// In battle, an SPB is forced onto players to target the "most wanted" player
	if (K_BattleForceSPB(player)
		&& spbplace == -1 && K_GetKartResult("selfpropelledbomb")->cooldown == 0 && !dontforcespb
		&& K_ItemResultEnabled(K_GetKartResult("selfpropelledbomb")))
	{
		K_AwardPlayerResult(player, K_GetKartResult("selfpropelledbomb"), KITEMBLINK_KARMA);
		return;
	}

	// NOW that we're done with most of those specialized cases, we can move onto the REAL item roulette tables.
	// Initializes existing spawnchance values
	memset(spawnchance, 0, sizeof(spawnchance));

	// Split into another function for a debug function below
	useodds = K_FindUseodds(player, mashed, pdis, bestbumper, spbrush);

	kartroulette_t roulette = {
		.pdis = pdis,
		.playerpos = player->position,
		.pos = useodds,
		.ourDist = player->distancetofinish,
		.clusterDist = player->distancefromcluster,
		.mashed = mashed,
		.spbrush = spbrush,
		.bot = player->bot,
		.shrink_freebie = (player->roulettetype == KROULETTETYPE_SHRINK),
		.is_shrunk = (K_IsAltShrunk(player)),
		.rival = (player->bot && player->botvars.rival),
		.rival_frontrunner = ((player->bot && player->botvars.rival) && (player->position <= 1)),
		.inBottom = K_IsPlayerLosing(player),
	};

	K_KartGetItemOdds(&roulette, spawnchance);

	// SPECIAL CASE No. 7:
	// Item forcing; the item with the highest "forceme" priority is the one given.
	{
		kartresult_t *forceresult;
		UINT8 bestforce = 0;

		for (i = 0; i < numkartresults; i++)
		{
			if (roulette.forceme[i] && roulette.forceme[i] > bestforce)
			{
				bestforce = roulette.forceme[i];
				forceresult = &kartresults[i];
			}
		}

		if ((bestforce) && (forceresult))
		{
			K_AwardPlayerResult(player, forceresult, KITEMBLINK_KARMA);
			return;
		}
	}

	for (i = 0; i < numkartresults; i++)
	{
		totalspawnchance += spawnchance[i];
		spawnchance[i] = totalspawnchance;
	}

	// Award the player whatever power is rolled
	kartresult_t *result = NULL;
	if (totalspawnchance > 0)
	{
		totalspawnchance = P_RandomKey(PR_ITEM_ROULETTE, totalspawnchance);
		for (i = 0; i < numkartresults && spawnchance[i] <= totalspawnchance; i++);
		result = &kartresults[i];
	}
	K_AwardPlayerResult(player, result, player->roulettetype == KROULETTETYPE_KARMA ? KITEMBLINK_KARMA : mashed ? KITEMBLINK_MASHED : KITEMBLINK_NORMAL);
}

void K_StartRoulette(player_t *player, kartroulettetype_e roulettetype)
{
	if (roulettetype == KROULETTETYPE_EGGMAN)
	{
		K_DropItems(player);
	}
	else if (roulettetype != KROULETTETYPE_BONUS && K_SMonitorSlotHogging(player) && player->itemreserveamount == 0)
	{
		K_AddPlayerReserveItem(player, KITEM_BONUSITEMROLL, 1);
		return;
	}

	player->itemroulette = 1;
	player->roulettetype = roulettetype;
}

void K_SetPlayerItemCooldown(player_t *player, tic_t timer, boolean force)
{
	if ((timer >= player->itemusecooldown) || force)
	{
		player->itemusecooldown = timer;
		player->itemusecooldownmax = max(timer, player->itemusecooldownmax);
	}
}

// Unique odds functions, for REAL this time

// S-Monitor odds 
INT32 KO_SMonitorOdds(INT32 odds, const kartroulette_t *roulette, const kartresult_t *result, UINT8 *forceme)
{
	(void)result;

	UINT32 cdist = roulette->clusterDist;

	if ((grandprixinfo.gp == true) && (grandprixinfo.lunaticmode))
	{
		// I'm tired, boss.
		cdist = (9 * cdist / 5);
	}

	odds = K_KartGetSMonitorOdds(cdist);

	// Special case: if you're SERIOUSLY far behind before the cooldown finishes, ignore it and start forcing,
	if (odds >= SMONITOR_FORCEODDS)
	{
		*forceme = 3; // Take priority over SPBs
	}
	else if (K_InStartCooldown())
	{
		odds = 0;
	}

	odds *= BASEODDSMUL;

	return odds;
}

// SPB odds
INT32 KO_SPBRaceOdds(INT32 odds, const kartroulette_t *roulette, const kartresult_t *result, UINT8 *forceme)
{
	(void)result;
	if (!K_LegacyOddsMode() && roulette->firstDist < (UINT32)ENDDIST) // No SPB near the end of the race
	{
		odds = 0;
	}
	else if (K_LegacyOddsMode() && roulette->pexiting > 0)
	{
		odds = 0;
	}

	UINT32 raceforce_pdis = roulette->pdis;

	if ((grandprixinfo.gp == true) && (grandprixinfo.lunaticmode == true))
	{
		// Multiply by 2.25
		raceforce_pdis = FixedMul(raceforce_pdis, 9 * FRACUNIT / 2);
	}

	if (roulette->rival_frontrunner == true)
	{
		// Frontrunning rival? Multiply by 1.5
		raceforce_pdis = FixedMul(raceforce_pdis, 3 * FRACUNIT / 2);
	}

	// No forced SPB in 1v1s, it has to be randomly rolled
	if (roulette->pingame <= 2)
	{
		*forceme = 0;
	}
	else if (K_RaceForceSPB(roulette->playerpos, raceforce_pdis)
		&& spbplace == -1 && K_GetKartResult("selfpropelledbomb")->cooldown == 0)
	{
		// Force SPB onto 2nd if they get too far behind.
		*forceme = 2;
	}

	return odds;
}

// Grow odds
INT32 KO_GrowRaceOdds(INT32 odds, const kartroulette_t *roulette, const kartresult_t *result, UINT8 *forceme)
{
	(void)result;
	(void)forceme;
	if (roulette->is_shrunk)
	{
		// Using Alt. Shrink; if you've already changed size, do you *really* need this?
		odds = 0;
	}

	return odds;
}

#define ALTSHRINK_EPSILON (320 * FRACUNIT)
#define NEIGHBOR_IFRAMES (TICRATE / 2)
#define BASE_IFRAMES (2 * TICRATE)

void K_DoGrowShrink(player_t *player, boolean shrinking)
{
	player->mo->scalespeed = mapobjectscale/TICRATE;
	player->mo->destscale = FixedMul(mapobjectscale, (shrinking) ? SHRINK_SCALE : GROW_SCALE);

	if (K_PlayerShrinkCheat(player) == true)
	{
		player->mo->destscale = FixedMul(player->mo->destscale, SHRINK_SCALE);
	}

	if ((shrinking) && (ALTSHRINKTIME > 0))
	{
		player->growshrinktimer = ALTSHRINKTIME * -TICRATE;
	}
	else
	{
		player->growshrinktimer = (gametypes[gametype]->rules & GTR_CLOSERPLAYERS ? 8 : 12) * ((shrinking) ? -TICRATE : TICRATE);
	}

	if (shrinking)
	{
		// Find neighbors
		INT32 n = K_CountNeighboringPlayers(player, ALTSHRINK_EPSILON, &K_ClusterFilter_NoFilter, 0, &(vector3_t){});

		// For every neighbor, add some iframes for a clean breakaway.
		UINT32 iframes = BASE_IFRAMES + n * NEIGHBOR_IFRAMES;
		player->flashing = iframes > UINT16_MAX ? UINT16_MAX : iframes;
	}

	if (player->invincibilitytimer > 0)
	{
		; // invincibility has priority in P_RestoreMusic, no point in starting here
	}
	else if (player->smonitortimer > 0)
	{
		; // Ditto for S-Monitor
	}
	else if (P_IsLocalPlayer(player) == true)
	{
		S_ChangeMusicSpecial((shrinking) ? "kshrnk" : "kgrow");
	}
	else //used to be "if (P_IsDisplayPlayer(player) == false)"
	{
		if (!shrinking)
		{
			S_StartSound(player->mo, (cv_kartinvinsfx.value ? sfx_alarmg : sfx_kgrow));
		}
	}

	P_RestoreMusic(player);
}

static void K_DoShrink(player_t *user)
{
	INT32 i;
	mobj_t *mobj, *next;

	S_StartSound(user->mo, sfx_kc46); // Sound the BANG!

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator || !players[i].mo)
			continue;
		if (&players[i] == user)
			continue;
		if (players[i].position < user->position)
		{
			//P_FlashPal(&players[i], PAL_NUKE, 10);

			// Grow should get taken away.
			if (players[i].growshrinktimer > 0)
				K_RemoveGrowShrink(&players[i]);
			else
			{
				// Start shrinking!
				K_DropItems(&players[i]);
				players[i].growshrinktimer = -(15*TICRATE);

				if (players[i].mo && !P_MobjWasRemoved(players[i].mo))
				{
					players[i].mo->scalespeed = mapobjectscale/TICRATE;
					players[i].mo->destscale = FixedMul(mapobjectscale, SHRINK_SCALE);

					if (K_PlayerShrinkCheat(&players[i]) == true)
					{
						players[i].mo->destscale = FixedMul(players[i].mo->destscale, SHRINK_SCALE);
					}

					S_StartSound(players[i].mo, sfx_kc59);
				}
			}
		}
	}

	// kill everything in the kitem list while we're at it:
	for (mobj = kitemcap; mobj; mobj = next)
	{
		next = mobj->itnext;

		// check if the item is being held by a player behind us before removing it.
		// check if the item is a "shield" first, bc i'm p sure thrown items keep the player that threw em as target anyway

		if (mobj->type == MT_BANANA_SHIELD || mobj->type == MT_JAWZ_SHIELD ||
		mobj->type == MT_SSMINE_SHIELD || mobj->type == MT_EGGMANITEM_SHIELD ||
		mobj->type == MT_SINK_SHIELD || mobj->type == MT_ORBINAUT_SHIELD)
		{
			if (mobj->target && mobj->target->player)
			{
				if (mobj->target->player->position > user->position)
					continue; // this guy's behind us, don't take his stuff away!
			}
		}

		mobj->destscale = 0;
		mobj->flags &= ~(MF_SOLID|MF_SHOOTABLE|MF_SPECIAL);
		mobj->flags |= MF_NOCLIPTHING; // Just for safety

		if (mobj->type == MT_SPB)
			spbplace = -1;
	}
}

static void K_DoHyudoroSteal(player_t *player)
{
	INT32 i, numplayers = 0;
	INT32 playerswappable[MAXPLAYERS];
	INT32 stealplayer = -1; // The player that's getting stolen from
	INT32 prandom = 0;
	boolean sink = P_RandomChance(PR_ITEM_HYUDORO, FRACUNIT/64);
	boolean force_sink = false;
	INT32 hyu = hyudorotime;

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (playeringame[i] && players[i].mo && players[i].mo->health > 0 && players[i].playerstate == PST_LIVE
			&& player != &players[i] && !players[i].exiting && !players[i].spectator // Player in-game

			// Can steal from this player
			&& ((gametype == GT_RACE && (cv_karthyudorostealbehind.value || players[i].position < player->position))
			|| ((gametypes[gametype]->rules & GTR_BUMPERS) && players[i].bumper > 0))

			// Has an item
			&& (players[i].itemtype
			&& players[i].itemamount
			&& !(players[i].itemflags & IF_ITEMOUT)
			&& !players[i].itemblink
			//todo: make this more modular for hold-button items (these should never be stolen when being used)
			&& !(
				players[i].flametimer +
				players[i].bubblecool +
				players[i].bubbleblowup +
				players[i].bubblebuffer +
				players[i].attractioncharge
			)
			))
		{
			kartitemflags_e flags = K_GetItemFlags(players[i].itemtype);

			// I'd have liked for this to be a result-based thing, but that'd need a rework in of itself.
			if ((flags & KIF_HYUCANTSTEAL) != KIF_HYUCANTSTEAL)
			{
				playerswappable[numplayers] = i;
				numplayers++;
			}
		}
	}

	prandom = P_RandomFixed(PR_ITEM_HYUDORO);
	S_StartSound(player->mo, sfx_s3k92);

	if (numplayers == 1) // With just 2 players, we just need to set the other player to be the one to steal from
	{
		stealplayer = playerswappable[numplayers-1];
	}
	else if (numplayers > 1) // We need to choose between the available candidates for the 2nd player
	{
		stealplayer = playerswappable[prandom%(numplayers-1)];
	}

	force_sink = LUA_HookKartHyudoro(player, &stealplayer, sink);

	if (force_sink || (sink && numplayers > 0 && K_ItemResultEnabled(K_GetKartResult("kitchensink")))) // BEHOLD THE KITCHEN SINK
	{
		player->hyudorotimer = hyu;
		player->stealingtimer = stealtime;

		player->itemtype = KITEM_KITCHENSINK;
		player->itemamount = 1;
		K_UnsetItemOut(player);

		// Add the Sink roll to the list
		if (itemlistactive)
			K_AddItemRollToList((INT32)(player - players), KITEM_KITCHENSINK, 1);

		// Woah this could be big, lets get the inside scoop...
		if (!player->exiting)
			K_DirectorForceSwitch(player - players, 1);
		return;
	}
	else if ((gametype == GT_RACE && player->position == 1) || numplayers == 0) // No-one can be stolen from? Oh well...
	{
		player->hyudorotimer = hyu;
		player->stealingtimer = stealtime;
		return;
	}

	if (stealplayer > -1) // Now here's where we do the stealing, has to be done here because we still know the player we're stealing from
	{
		player->hyudorotimer = hyu;
		player->stealingtimer = stealtime;
		players[stealplayer].stolentimer = stealtime;

		player->itemtype = players[stealplayer].itemtype;
		player->itemamount = players[stealplayer].itemamount;
		K_UnsetItemOut(player);

		if (itemlistactive)
		{
			K_AddItemRollToList((INT32)(player - players),
					    players[stealplayer].itemtype,
					    players[stealplayer].itemamount);
		}

		players[stealplayer].itemtype = KITEM_NONE;
		players[stealplayer].itemamount = 0;
		K_DisableSeekingReticule(&players[stealplayer]);
		K_UnsetItemOut(&players[stealplayer]);

		if (P_IsDisplayPlayer(&players[stealplayer]) && !r_splitscreen)
			S_StartSound(NULL, sfx_s3k92);
	}
}

// Rough size of the outer-rim sprites, after scaling.
// (The hitbox is already pretty strict due to only 1 active frame,
// we don't need to have it disjointedly small too...)
#define THUNDERSPRITE 80

void K_DoThunderShield(player_t *player)
{
	mobj_t *mo;
	int i = 0;
	fixed_t sx;
	fixed_t sy;
	angle_t an;

	S_StartSound(player->mo, sfx_zio3);
	K_ThunderShieldAttack(player->mo, (cv_kartthunder_radius.value + THUNDERSPRITE) * FRACUNIT);

	// spawn vertical bolt
	mo = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_THOK);
	P_SetTarget(&mo->target, player->mo);
	P_SetMobjState(mo, S_LZIO11);
	mo->color = SKINCOLOR_TEAL;
	mo->scale = player->mo->scale*3 + (player->mo->scale/2);

	mo = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_THOK);
	P_SetTarget(&mo->target, player->mo);
	P_SetMobjState(mo, S_LZIO21);
	mo->color = SKINCOLOR_CYAN;
	mo->scale = player->mo->scale*3 + (player->mo->scale/2);

	// spawn horizontal bolts;
	for (i=0; i<7; i++)
	{
		mo = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_THOK);
		mo->angle = P_RandomRange(PR_ITEM_ROULETTE, 0, 359)*ANG1;
		mo->fuse = P_RandomRange(PR_ITEM_ROULETTE, 20, 50);
		P_SetTarget(&mo->target, player->mo);
		P_SetMobjState(mo, S_KLIT1);
	}

	// spawn the radius thing:
	an = ANGLE_22h;
	for (i=0; i<15; i++)
	{
		sx = player->mo->x + FixedMul((player->mo->scale*cv_kartthunder_radius.value), FINECOSINE((an*i)>>ANGLETOFINESHIFT));
		sy = player->mo->y + FixedMul((player->mo->scale*cv_kartthunder_radius.value), FINESINE((an*i)>>ANGLETOFINESHIFT));
		mo = P_SpawnMobj(sx, sy, player->mo->z, MT_THOK);
		mo->angle = an*i;
		mo->extravalue1 = cv_kartthunder_radius.value;	// Used to know whether we should teleport by radius or something.
		mo->scale = player->mo->scale*3;
		P_SetTarget(&mo->target, player->mo);
		P_SetMobjState(mo, S_KSPARK1);
	}
}

void K_DoAttractionShield(player_t *player, fixed_t power)
{
	mobj_t *mo;
	int i = 0;
	fixed_t sx;
	fixed_t sy;
	angle_t an;

	if (S_SoundPlaying(player->mo, sfx_vwre))
	{
		S_StopSoundByID(player->mo, sfx_vwre);
	}

	S_StartSound(player->mo, sfx_zio3);
	K_AttractionShieldAttack(player->mo, (cv_kartthunder_radius.value + THUNDERSPRITE) * FRACUNIT, power, power >= FRACUNIT);

	// spawn vertical bolt
	mo = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_THOK);
	P_SetTarget(&mo->target, player->mo);
	P_SetMobjState(mo, S_LZIO11);
	mo->color = SKINCOLOR_TEAL;
	mo->scale = player->mo->scale*3 + (player->mo->scale/2);
	mo->color = SKINCOLOR_YELLOW;
	mo->colorized = true;

	mo = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_THOK);
	P_SetTarget(&mo->target, player->mo);
	P_SetMobjState(mo, S_LZIO21);
	mo->color = SKINCOLOR_CYAN;
	mo->scale = player->mo->scale*3 + (player->mo->scale/2);
	mo->color = SKINCOLOR_YELLOW;
	mo->colorized = true;

	// spawn the radius thing:
	an = ANGLE_22h;
	for (i=0; i<15; i++)
	{
		sx = player->mo->x + FixedMul((player->mo->scale*cv_kartthunder_radius.value), FINECOSINE((an*i)>>ANGLETOFINESHIFT));
		sy = player->mo->y + FixedMul((player->mo->scale*cv_kartthunder_radius.value), FINESINE((an*i)>>ANGLETOFINESHIFT));
		mo = P_SpawnMobj(sx, sy, player->mo->z, MT_THOK);
		mo->angle = an*i;
		mo->extravalue1 = cv_kartthunder_radius.value;	// Used to know whether we should teleport by radius or something.
		mo->scale = player->mo->scale*3;
		P_SetTarget(&mo->target, player->mo);
		P_SetMobjState(mo, S_KSPARK1);
		mo->color = SKINCOLOR_YELLOW;
		mo->colorized = true;
	}
}

#undef THUNDERSPRITE

#define MAXSHARDCOUNT 40
#define SHARDROT (360 * FRACUNIT / 40)
void K_BreakBubbleShield(player_t* player)
{
	if (K_GetShieldFromPlayer(player) != KSHIELD_BUBBLE)
		return;

	const INT32 flip = P_MobjFlip(player->mo);
	const fixed_t scalediff = player->shieldtracer->scale - mapobjectscale;
	const fixed_t shieldrad = player->shieldtracer->radius;

	for (INT32 i = 0; i < MAXSHARDCOUNT; i++)
	{
		vector2_t mul_vec = { FRACUNIT, 0 };

		fixed_t randang = SHARDROT * (i + 1);
		fixed_t randzang = P_RandomRange(PR_ITEM_ROULETTE, 10, 120) * FRACUNIT;
		fixed_t move_magnitude =
		    FixedMul((P_RandomRange(PR_ITEM_ROULETTE, 4, 16) * FRACUNIT), mapobjectscale + (scalediff / 4));

		FV2_Rotate(&mul_vec, randzang);

		// Do shitty initial 3D rotations around the shield's radius.
		mobj_t *shard = P_SpawnMobj(
		    player->shieldtracer->x +
		    FixedMul(FixedMul(mul_vec.x, shieldrad), FCOS(FixedAngle(randang))),
		    player->shieldtracer->y +
		    FixedMul(FixedMul(mul_vec.x, shieldrad), FSIN(FixedAngle(randang))),
		    player->shieldtracer->z + ((player->shieldtracer->height + scalediff) / 4)
		                            + FixedMul(mul_vec.y * flip, shieldrad),
		    MT_BUBBLESHIELD_DEBRIS);

		if (!P_MobjWasRemoved(shard))
		{
			//CONS_Printf(M_GetText("randzang: %d, randang: %d\n"), randzang / FRACUNIT, randang / FRACUNIT);

			mul_vec.x = FixedMul(move_magnitude, mul_vec.x);
			mul_vec.y = FixedMul(move_magnitude, mul_vec.y * flip);

			vector3_t mom = {
				.x = FixedMul(mul_vec.x, FCOS(FixedAngle(randang))),
				.y = FixedMul(mul_vec.x, FSIN(FixedAngle(randang))),
				.z = mul_vec.y,
			};

			shard->momx = mom.x + player->mo->momx;
			shard->momy = mom.y + player->mo->momy;
			shard->momz = mom.z + player->mo->momz;

			shard->fuse = 3*TICRATE;
			shard->extravalue1 = i % 6; // 16% of shards play the death sound
		}
	}

	S_StartSound(player->mo, sfx_kc41);
}
#undef MAXSHARDCOUNT
#undef SHARDROT

static void K_ThrowLandMine(player_t *player)
{
	mobj_t *landMine;
	mobj_t *throwmo;

	landMine = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z + player->mo->height/2, MT_LANDMINE);
	K_FlipFromObject(landMine, player->mo);
	landMine->threshold = 10;

	if (landMine->info->seesound)
		S_StartSound(player->mo, landMine->info->seesound);

	P_SetTarget(&landMine->target, player->mo);

	P_SetScale(landMine, player->mo->scale);
	landMine->destscale = player->mo->destscale;

	landMine->angle = player->mo->angle;

	landMine->momz = (30 * mapobjectscale * P_MobjFlip(player->mo)) + player->mo->momz;
	landMine->color = player->skincolor;

	throwmo = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z + player->mo->height/2, MT_FIREDITEM);
	P_SetTarget(&throwmo->target, player->mo);
	// Ditto:
	if (player->mo->eflags & MFE_VERTICALFLIP)
	{
		throwmo->z -= player->mo->height;
		throwmo->eflags |= MFE_VERTICALFLIP;
	}

	throwmo->movecount = 0; // above player
}

void K_PlayerItemThink(player_t *player, boolean onground)
{
	TracyCZone(__zone, true);

	const UINT16 buttons = K_GetKartButtons(player);
	const boolean ATTACK_JUST_DOWN = ((buttons & BT_ATTACK) && !(player->oldcmd.buttons & BT_ATTACK));
	const boolean HOLDING_ITEM = (player->equippeditem != 0);
	const boolean NO_HYUDORO = (player->stealingtimer == 0 && player->stolentimer == 0);
	
	// actions of the equipped item go first, then the item in the player's item slot (relevant for standard eggman)
	kartitemtype_e itemtype = player->equippeditem;
	boolean altitem = K_IsKartItemAlternate(itemtype);
	boolean force = false;

#ifdef TRACY_ENABLE
	{
		const char *itemname = DEH_KartItemName(itemtype);
		TracyCMessage(itemname, strlen(itemname));
	}
#endif

	// uses player->itemtype to maintain the old logic
	if (LUA_HookPlayerItem(player, player->itemtype, HOLDING_ITEM, &force))
	{
		TracyCZoneEnd(__zone);
		return;
	}
	
	if (itemtype == KITEM_NONE)
	{
		itemtype = player->itemtype;
		altitem = K_IsKartItemAlternate(itemtype);
	}

	if (itemtype != KITEM_NONE)
	{
		itemthinkfunc_f *itemthinker = KI_ItemThinkFunctions[itemtype][altitem];
		itemusefunc_f *itemuse = NULL;
		
		if (!LUA_HookPlayerItemThink(player, itemtype))
		{
			if (itemthinker != NULL)
			{
				itemthinker(player);
			}
		}

		if ((buttons & BT_ATTACK))
		{
			if (ATTACK_JUST_DOWN)
			{
				if (!LUA_HookPlayerItemUse(player, itemtype, ATTACK_JUST_DOWN, onground, force, NO_HYUDORO))
				{
					itemuse = KI_ItemUseFunctions[itemtype][altitem];
				}
			}
			else
			{
				// note that this doesn't run for the first tick the item is used
				if (!LUA_HookPlayerItemHoldUse(player, itemtype, ATTACK_JUST_DOWN, onground, force, NO_HYUDORO))
				{
					itemuse = KI_ItemHoldUseFunctions[itemtype][altitem];
				}
			}
		}

		if ((player->oldcmd.buttons & BT_ATTACK) && !(buttons & BT_ATTACK))
		{
			if (!LUA_HookPlayerItemReleaseUse(player, itemtype, ATTACK_JUST_DOWN, onground, force, NO_HYUDORO))
			{
				itemuse = KI_ItemReleaseUseFunctions[itemtype][altitem];
			}
		}

		if (itemuse != NULL)
		{
			itemuse(player, ATTACK_JUST_DOWN, onground, force, NO_HYUDORO);
		}
	}

	TracyCZoneEnd(__zone);
}

INT16 K_GetShrinkTime(const player_t *player)
{
	return player->growshrinktimer * -1;
}

boolean K_IsAltShrunk(const player_t *player)
{
	if (!player)
	{
#ifdef PARANOIA
		CONS_Printf("K_IsAltShrunk: passed NULL player\n");
#endif
		return false;
	}
	return player->growshrinktimer < 0 && K_IsKartItemAlternate(KITEM_SHRINK);
}

#define PITY_SHRINKINCREASE_BASE (3)
#define PITY_SHRINKINCREASE (TICRATE / 2)

void K_AltShrinkPityIncrease(player_t *player)
{
    // Increase your shrink timer by a little bit for every player you run into.
    INT32 shrinktime = K_GetShrinkTime(player);

    fixed_t dimin = FRACUNIT;

    dimin = max(0, 5 - player->altshrinktimeshit) * FRACUNIT / 5;

    shrinktime = min(INT16_MAX, shrinktime + PITY_SHRINKINCREASE_BASE + FixedMul(PITY_SHRINKINCREASE, dimin));

    player->growshrinktimer = ((INT16)shrinktime) * -1;
}

#undef PITY_SHRINKINCREASE_BASE
#undef PITY_SHRINKINCREASE

void K_SpawnEggMineBumpEffect(mobj_t *mo)
{
	mobj_t *fx = P_SpawnMobj(mo->x, mo->y, mo->z, MT_BUMP);
	if (mo->eflags & MFE_VERTICALFLIP)
	{
		fx->eflags |= MFE_VERTICALFLIP;
	}
	else
	{
		fx->eflags |= ~MFE_VERTICALFLIP;
	}
	fx->scale = mo->scale;
	S_StartSound(mo, sfx_cdfm15);
}

void K_DoEggMineStrip(mobj_t *tmo, mobj_t *inflictor, mobj_t *source)
{
	(void)source;
	if (inflictor && (inflictor->type == MT_LANDMINE || inflictor->type == MT_EGGMINE))
	{
		if (tmo->player && P_CanPickupItem(tmo->player, PICKUPITEM_EGGMINE))
		{
			S_StartSound(tmo, sfx_egrbhe);
			K_DropItems(tmo->player);

			if (cv_karteggmine_slotlock.value)
			{
				K_SetPlayerItemCooldown(tmo->player, TICRATE, false);
			}

			if (cv_karteggmine_slotbrick.value)
			{
				tmo->player->itemtype = KITEM_EGGBRICK;
				tmo->player->itemamount = 1;

				if (itemlistactive)
					K_AddItemRollToList((INT32)(tmo->player - players), KITEM_EGGBRICK, 1);

				tmo->player->itemblink = TICRATE;
				tmo->player->itemblinkmode = KITEMBLINK_MASHED;
			}
		}
	}
}

void K_EnableSeekingReticule(player_t *player, boolean passive)
{
	player->itemflags |= IF_SEEKING;
	if (passive)
	{
		player->itemflags |= IF_PASSIVESEEKING;
	}
}

void K_DisableSeekingReticule(player_t *player)
{
	player->itemflags &= ~(IF_SEEKING|IF_PASSIVESEEKING);
}

void K_KartSeekingReticule(player_t *player)
{
	// seeking item reticule
	if (player->itemflags & IF_SEEKING)
	{
		INT32 lasttarg = player->lastitemtarget;
		player_t *targ;
		mobj_t *ret;

		if (lasttarg >= 0 && lasttarg < MAXPLAYERS &&
			((player->itemtargetdelay && playeringame[lasttarg] && !players[lasttarg].spectator)
			 || (player->itemflags & IF_PASSIVESEEKING)))
		{
			targ = &players[lasttarg];
			player->itemtargetdelay--;
		}
		else
		{
			targ = K_FindJawzTarget(player->mo, player);
		}

		if (!targ || !targ->mo || P_MobjWasRemoved(targ->mo))
		{
			player->lastitemtarget = -1;
			player->itemtargetdelay = 0;
			return;
		}

		ret = P_SpawnMobj(targ->mo->x, targ->mo->y, targ->mo->z, MT_PLAYERRETICULE);
		ret->old_x = targ->mo->old_x;
		ret->old_y = targ->mo->old_y;
		ret->old_z = targ->mo->old_z;
		P_SetTarget(&ret->target, targ->mo);
		ret->frame |= ((leveltime % 10) / 2);
		ret->tics = 1;
		ret->color = player->skincolor;

		if (targ-players != lasttarg)
		{
			if (P_IsDisplayPlayer(player) || P_IsDisplayPlayer(targ))
				S_StartSound(NULL, sfx_s3k89);
			else
				S_StartSound(targ->mo, sfx_s3k89);

			player->lastitemtarget = targ-players;
			player->itemtargetdelay = 5;
		}
	}
	else
	{
		player->itemflags &= ~IF_PASSIVESEEKING;
		player->lastitemtarget = -1;
		player->itemtargetdelay = 0;
	}
}

player_t *K_FindJawzTarget(mobj_t *actor, player_t *source)
{
	fixed_t best = -1;
	player_t *wtarg = NULL;
	INT32 i;

	for (i = 0; i < MAXPLAYERS; i++)
	{
		angle_t thisang;
		player_t *player;

		if (!playeringame[i])
			continue;

		player = &players[i];

		if (player->spectator)
			continue; // spectator

		if (!player->mo)
			continue;

		if (player->mo->health <= 0)
			continue; // dead

		// Don't target yourself, stupid.
		if (player == source)
			continue;

		// Don't home in on teammates.
		if (G_GametypeHasTeams() && source->ctfteam == player->ctfteam)
			continue;

		// Invisible, don't bother
		if (player->hyudorotimer)
			continue;

		// Find the angle, see who's got the best.
		thisang = actor->angle - R_PointToAngle2(actor->x, actor->y, player->mo->x, player->mo->y);
		if (thisang > ANGLE_180)
			thisang = InvAngle(thisang);

		// Jawz only go after the person directly ahead of you in race... sort of literally now!
		if (gametypes[gametype]->rules & GTR_CIRCUIT)
		{
			// Don't go for people who are behind you
			if (thisang > ANGLE_67h)
				continue;
			// Don't pay attention to people who aren't above your position
			if (player->position >= source->position)
				continue;
			if ((best == -1) || (player->position > best))
			{
				wtarg = player;
				best = player->position;
			}
		}
		else
		{
			fixed_t thisdist;
			fixed_t thisavg;

			// Don't go for people who are behind you
			if (thisang > ANGLE_45)
				continue;

			// Don't pay attention to dead players
			if (player->bumper <= 0)
				continue;

			// Z pos too high/low
			if (abs(player->mo->z - (actor->z + actor->momz)) > RING_DIST/8)
				continue;

			thisdist = P_AproxDistance(player->mo->x - (actor->x + actor->momx), player->mo->y - (actor->y + actor->momy));

			if (thisdist > 2*RING_DIST) // Don't go for people who are too far away
				continue;

			thisavg = (AngleFixed(thisang) + thisdist) / 2;

			//CONS_Printf("got avg %d from player # %d\n", thisavg>>FRACBITS, i);

			if ((best == -1) || (thisavg < best))
			{
				wtarg = player;
				best = thisavg;
			}
		}
	}

	return wtarg;
}

// after using this you must handle setting any additional values on the mobj (like skincolour)
mobj_t *K_SpawnEquippedItem(player_t *player, kartitemequip_e equipstyle, mobjtype_t type, INT32 moloop, INT32 totalamount, mobj_t *prev)
{
	mobj_t *mo = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, type);
	angle_t newangle;
	if (!mo)
	{
		return NULL;
	}

	mo->flags |= MF_NOCLIPTHING;
	mo->threshold = 10;

	switch (equipstyle)
	{
		case KITEMEQUIP_ORBIT: // Kart orbit items
			newangle = (player->mo->angle + ANGLE_157h) + FixedAngle(((360 / totalamount) * moloop) << FRACBITS) + ANGLE_90;
			mo->angle = newangle;
			mo->movecount = totalamount;
			mo->movedir = mo->lastlook = moloop+1;
			break;
		case KITEMEQUIP_TRAIL: // Kart trailing items
			mo->movecount = totalamount;
			mo->movedir = moloop+1;
			break;
		case KITEMEQUIP_ROCKETS: // Rocket Sneaker et autres side-by-side style items
			K_MatchGenericExtraFlags(mo, player->mo);
			mo->angle = player->mo->angle;
			mo->movecount = moloop%totalamount;
			mo->movedir = mo->lastlook = moloop+1;
			break;
		case KITEMEQUIP_SHIELD: // Force Field Shields
			K_FlipFromObject(mo, player->mo);
			P_SetScale(mo, (mo->destscale = (5*mo->destscale)>>2));
			P_SetTarget(&player->shieldtracer, mo);
			break;
		default:
			break;
	}

	P_SetTarget(&mo->target, player->mo);
	if (prev != NULL)
	{
		if (equipstyle != KITEMEQUIP_SHIELD)
		{
			P_SetTarget(&mo->hprev, prev);
		}
		P_SetTarget(&prev->hnext, mo);
	}
	return mo;
}

void K_DecrementAndCheckCurrentItem(player_t *player)
{
	if (player->itemamount > 0)
	{
		player->itemamount--;
	}
	if (player->itemamount <= 0)
	{
		K_UnsetItemOut(player);
		if (!(player->itemflags & IF_PASSIVESEEKING))
		{
			K_DisableSeekingReticule(player);
		}
		player->itemtype = KITEM_NONE;
	}
}

void K_UseItem_Sneaker(player_t *player, boolean itemjustdown, boolean onground, boolean force, boolean nohyudoro)
{
	(void)itemjustdown;

	if (nohyudoro && (onground || force))
	{
		K_DoSneaker(player, SNEAKERTYPE_SNEAKER);
		K_PlayBoostTaunt(player->mo);
		K_DecrementAndCheckCurrentItem(player);
		K_BotResetItemConfirm(player, false);
	}
}

void K_UseItem_RocketSneaker(player_t *player, boolean itemjustdown, boolean onground, boolean force, boolean nohyudoro)
{
	(void)itemjustdown;

	if (nohyudoro && (onground || force))
	{
		if (player->rocketsneakertimer == 0)
		{
			INT32 moloop;
			mobj_t *mo = NULL;
			mobj_t *prev = player->mo;

			K_PlayBoostTaunt(player->mo);
			S_StartSound(player->mo, sfx_s3k3a);

			//K_DoSneaker(player, SNEAKERTYPE_ROCKETSNEAKER);

			player->rocketsneakertimer = (itemtime*3);
			K_DecrementAndCheckCurrentItem(player);
			K_SetItemOut(player, KITEM_ROCKETSNEAKER, 0);
			K_UpdateHnextList(player, true);

			for (moloop = 0; moloop < 2; moloop++)
			{
				mo = K_SpawnEquippedItem(player, KITEMEQUIP_ROCKETS, MT_ROCKETSNEAKER, moloop, 2, prev);
				prev = mo;
			}
			K_BotResetItemConfirm(player, false);
		}
		else
		{
			K_DoSneaker(player, SNEAKERTYPE_ROCKETSNEAKER);
			K_PlayBoostTaunt(player->mo);
			if (player->rocketsneakertimer <= 2*TICRATE)
				player->rocketsneakertimer = 1;
			else
				player->rocketsneakertimer -= 2*TICRATE;
			K_BotResetItemConfirm(player, false);
		}
	}
}

void K_UseItem_Invincibility(player_t *player, boolean itemjustdown, boolean onground, boolean force, boolean nohyudoro)
{
	(void)onground;
	(void)force;
	(void)itemjustdown;

	if (nohyudoro)
	{
		K_DoInvincibility(player, INVINTIME);
		K_PlayPowerGloatSound(player->mo);
		K_DecrementAndCheckCurrentItem(player);
		K_BotResetItemConfirm(player, false);
	}
}

void K_UseItem_SMonitor(player_t *player, boolean itemjustdown, boolean onground, boolean force, boolean nohyudoro)
{
	(void)onground;
	(void)force;
	(void)itemjustdown;

	if (nohyudoro)
	{
		K_DoSMonitor(player, SMONITORTIME);
		K_PlayPowerGloatSound(player->mo);
		K_DecrementAndCheckCurrentItem(player);
		K_BotResetItemConfirm(player, false);
	}
}

void K_UseItem_Banana(player_t *player, boolean itemjustdown, boolean onground, boolean force, boolean nohyudoro)
{
	(void)onground;
	(void)force;
	(void)itemjustdown;
	
	if (player->equippeditem == KITEM_BANANA)
	{
		K_DecrementAndCheckCurrentItem(player);

		mobj_t *mo = K_ThrowKartItem(player, false, MT_BANANA, -1, 0);
		mo->color = player->skincolor;

		K_PlayAttackTaunt(player->mo);
		K_UpdateHnextList(player, false);
		K_BotResetItemConfirm(player, false);
	}
	else if (nohyudoro)
	{
		INT32 moloop;
		mobj_t *mo;
		mobj_t *prev = player->mo;

		//K_PlayAttackTaunt(player->mo);
		K_SetItemOut(player, KITEM_BANANA, IF_ITEMOUT);
		S_StartSound(player->mo, sfx_s254);

		for (moloop = 0; moloop < player->itemamount; moloop++)
		{
			mo = K_SpawnEquippedItem(player, KITEMEQUIP_TRAIL, MT_BANANA_SHIELD, moloop, player->itemamount, prev);
			if (!mo)
			{
				player->itemamount = moloop;
				break;
			}
			
			mo->color = player->skincolor;
			prev = mo;
		}
		K_BotResetItemConfirm(player, false);
	}
}

void K_UseItem_Eggman(player_t *player, boolean itemjustdown, boolean onground, boolean force, boolean nohyudoro)
{
	(void)onground;
	(void)force;
	(void)itemjustdown;
	
	if (player->equippeditem == KITEM_EGGMAN)
	{
		K_ThrowKartItem(player, false, MT_EGGMANITEM, -1, 0);
		K_PlayAttackTaunt(player->mo);
		K_UnsetItemOut(player);
		K_UpdateHnextList(player, true);
		K_BotResetItemConfirm(player, false);
	}
	else if (nohyudoro)
	{
		mobj_t *mo;
		K_DecrementAndCheckCurrentItem(player);
		K_SetItemOut(player, KITEM_EGGMAN, IF_EGGMANOUT);
		S_StartSound(player->mo, sfx_s254);
		mo = K_SpawnEquippedItem(player, KITEMEQUIP_TRAIL, MT_EGGMANITEM_SHIELD, 0, 1, player->mo);
		if (mo)
		{
			K_FlipFromObject(mo, player->mo);
			mo->color = SKINCOLOR_BLACK;
		}
		K_BotResetItemConfirm(player, false);
	}
}

void K_UseItem_EggMine(player_t *player, boolean itemjustdown, boolean onground, boolean force, boolean nohyudoro)
{
	(void)onground;
	(void)force;
	(void)itemjustdown;
	
	if (player->equippeditem == KITEM_EGGMAN)
	{
		mobj_t *mo;
		K_DecrementAndCheckCurrentItem(player);

		if (player->throwdir == 1)
		{
			mo = K_ThrowKartItem(player, false, MT_EGGMINE, 1, 0);
			mo->health = 3;
			mo->whiteshadow = true;
			mo->lastlook = 1;
		}
		else
		{
			mo = K_ThrowKartItem(player, true, MT_EGGMINE, 4, 0);
			if (player->throwdir == 0)
			{
				mo->health = 2;
				mo->whiteshadow = true;
			}
			else
			{
				
				mo->health = 1;
				mo->whiteshadow = false;
			}
			mo->lastlook = 1;
		}
		K_UpdateHnextList(player, false);
		K_PlayAttackTaunt(player->mo);
		K_BotResetItemConfirm(player, false);
	}
	else if (nohyudoro)
	{
		INT32 moloop;
		mobj_t *mo = NULL;
		mobj_t *prev = player->mo;

		K_SetItemOut(player, KITEM_EGGMAN, IF_ITEMOUT);
		S_StartSound(player->mo, sfx_s3k3a);

		for (moloop = 0; moloop < player->itemamount; moloop++)
		{
			mo = K_SpawnEquippedItem(player, KITEMEQUIP_ORBIT, MT_EGGMINE_SHIELD, moloop, player->itemamount, prev);
			if (!mo)
			{
				player->itemamount = moloop;
				break;
			}
			prev = mo;
		}
		K_BotResetItemConfirm(player, false);
	}
}

void K_UseItem_Orbinaut(player_t *player, boolean itemjustdown, boolean onground, boolean force, boolean nohyudoro)
{
	(void)onground;
	(void)force;
	(void)itemjustdown;
	
	if (player->equippeditem == KITEM_ORBINAUT)
	{
		K_DecrementAndCheckCurrentItem(player);
		K_ThrowKartItem(player, true, MT_ORBINAUT, 1, 0);
		K_PlayAttackTaunt(player->mo);
		K_UpdateHnextList(player, false);
		K_BotResetItemConfirm(player, false);
	}
	else if (nohyudoro)
	{
		INT32 moloop;
		mobj_t *mo = NULL;
		mobj_t *prev = player->mo;

		//K_PlayAttackTaunt(player->mo);
		K_SetItemOut(player, KITEM_ORBINAUT, IF_ITEMOUT);
		S_StartSound(player->mo, sfx_s3k3a);

		for (moloop = 0; moloop < player->itemamount; moloop++)
		{
			mo = K_SpawnEquippedItem(player, KITEMEQUIP_ORBIT, MT_ORBINAUT_SHIELD, moloop, player->itemamount, prev);
			if (!mo)
			{
				player->itemamount = moloop;
				break;
			}
			mo->color = player->skincolor;
			prev = mo;
		}
		K_BotResetItemConfirm(player, false);
	}
}

void K_UseItem_Jawz(player_t *player, boolean itemjustdown, boolean onground, boolean force, boolean nohyudoro)
{
	(void)onground;
	(void)force;
	(void)itemjustdown;
	
	if (player->equippeditem == KITEM_JAWZ)
	{
		K_DecrementAndCheckCurrentItem(player);
		if (player->throwdir == 1 || player->throwdir == 0)
		{
			K_ThrowKartItem(player, true, MT_JAWZ, 1, 0);
		}
		else if (player->throwdir == -1) // Throwing backward gives you a dud that doesn't home in
		{
			K_ThrowKartItem(player, true, MT_JAWZ_DUD, -1, 0);
		}
		K_PlayAttackTaunt(player->mo);
		K_UpdateHnextList(player, false);
		K_BotResetItemConfirm(player, false);
	}
	else if (nohyudoro)
	{
		INT32 moloop;
		mobj_t *mo = NULL;
		mobj_t *prev = player->mo;

		//K_PlayAttackTaunt(player->mo);
		K_SetItemOut(player, KITEM_JAWZ, IF_ITEMOUT|IF_SEEKING);
		S_StartSound(player->mo, sfx_s3k3a);
		K_EnableSeekingReticule(player, false);

		for (moloop = 0; moloop < player->itemamount; moloop++)
		{
			mo = K_SpawnEquippedItem(player, KITEMEQUIP_ORBIT, MT_JAWZ_SHIELD, moloop, player->itemamount, prev);
			if (!mo)
			{
				player->itemamount = moloop;
				break;
			}
			prev = mo;
		}
		K_BotResetItemConfirm(player, false);
	}
}

void K_UseItem_Mine(player_t *player, boolean itemjustdown, boolean onground, boolean force, boolean nohyudoro)
{
	(void)onground;
	(void)force;
	(void)itemjustdown;
	
	if (player->equippeditem == KITEM_MINE)
	{
		mobj_t *mo = NULL;
		K_DecrementAndCheckCurrentItem(player);
		mo = K_ThrowKartItem(player, false, MT_SSMINE, 1, 1);
		if (mo != NULL && player->throwdir <= -1)
		{
			mo->extravalue1 = 49; // Pads the start-up length from 21 frames to a full 2 seconds
		}
		K_PlayAttackTaunt(player->mo);
		K_UnsetItemOut(player);
		K_UpdateHnextList(player, true);
		K_BotResetItemConfirm(player, false);
	}
	else if (nohyudoro)
	{
		K_SetItemOut(player, KITEM_MINE, IF_ITEMOUT);
		S_StartSound(player->mo, sfx_s254);
		K_SpawnEquippedItem(player, KITEMEQUIP_TRAIL, MT_SSMINE_SHIELD, 0, 1, player->mo);
		K_BotResetItemConfirm(player, false);
	}
}

void K_UseItem_LandMine(player_t *player, boolean itemjustdown, boolean onground, boolean force, boolean nohyudoro)
{
	(void)onground;
	(void)force;
	(void)itemjustdown;
	
	if (nohyudoro)
	{
		K_DecrementAndCheckCurrentItem(player);
		K_ThrowLandMine(player);
		K_PlayAttackTaunt(player->mo);
		K_BotResetItemConfirm(player, false);
	}
}

void K_UseItem_Ballhog(player_t *player, boolean itemjustdown, boolean onground, boolean force, boolean nohyudoro)
{
	(void)onground;
	(void)force;
	(void)itemjustdown;
	
	if (nohyudoro)
	{
		K_DecrementAndCheckCurrentItem(player);
		K_ThrowKartItem(player, true, MT_BALLHOG, 1, 0);
		K_PlayAttackTaunt(player->mo);
		K_BotResetItemConfirm(player, false);
	}
}

void K_UseItem_SPB(player_t *player, boolean itemjustdown, boolean onground, boolean force, boolean nohyudoro)
{
	(void)onground;
	(void)force;
	(void)itemjustdown;
	
	if (nohyudoro)
	{
		K_DecrementAndCheckCurrentItem(player);
		K_ThrowKartItem(player, true, MT_SPB, 1, 0);
		K_PlayAttackTaunt(player->mo);
		K_BotResetItemConfirm(player, false);
	}
}

void K_UseItem_Grow(player_t *player, boolean itemjustdown, boolean onground, boolean force, boolean nohyudoro)
{
	(void)onground;
	(void)force;
	(void)itemjustdown;
	
	if (nohyudoro)
	{
		K_DecrementAndCheckCurrentItem(player);
		if (player->growshrinktimer < 0 && !K_IsKartItemAlternate(KITEM_SHRINK))
		{
			// If you're shrunk, then "grow" will just make you normal again...
			K_RemoveGrowShrink(player);
		}
		else
		{
			// ...in Legacy mode.
			// Alt. Shrink's a powerup, so Grow overrides!
			if (K_IsAltShrunk(player))
			{
				player->growshrinktimer = 0; // Paranoia
			}

			K_PlayPowerGloatSound(player->mo);

			K_DoGrowShrink(player, false);

			S_StartSound(player->mo, sfx_kc5a);
		}

		K_BotResetItemConfirm(player, false);
	}
}

void K_UseItem_Shrink(player_t *player, boolean itemjustdown, boolean onground, boolean force, boolean nohyudoro)
{
	(void)onground;
	(void)force;
	(void)itemjustdown;
	
	if (nohyudoro)
	{
		K_DoShrink(player);
		K_DecrementAndCheckCurrentItem(player);
		K_PlayPowerGloatSound(player->mo);
		K_BotResetItemConfirm(player, false);
	}
}

void K_UseItem_Mini(player_t *player, boolean itemjustdown, boolean onground, boolean force, boolean nohyudoro)
{
	(void)onground;
	(void)force;
	(void)itemjustdown;
	
	if (nohyudoro)
	{
		K_DoGrowShrink(player, true);
		S_StartSound(player->mo, sfx_kc46);
		K_DecrementAndCheckCurrentItem(player);
		K_PlayPowerGloatSound(player->mo);
		K_BotResetItemConfirm(player, false);

		// Shrink isn't very strong on its own--give a freebie roll to really bring down the hurt!
		if (player->itemamount <= 0)
		{
			K_StartRoulette(player, KROULETTETYPE_SHRINK);
		}
	}
}

void K_UseItem_ThunderShield(player_t *player, boolean itemjustdown, boolean onground, boolean force, boolean nohyudoro)
{
	(void)onground;
	(void)force;
	(void)itemjustdown;
	
	if (nohyudoro)
	{
		K_DoThunderShield(player);
		if (player->itemamount > 0)
		{
			// Why is this a conditional?
			// Thunder shield: the only item that allows you to
			// activate a mine while you're out of its radius,
			// the SAME tic it sets your itemamount to 0
			// ...:dumbestass:
			K_DecrementAndCheckCurrentItem(player);
			K_PlayAttackTaunt(player->mo);
			K_BotResetItemConfirm(player, false);
		}
	}
}

void K_UseItem_AttractionShield(player_t *player, boolean itemjustdown, boolean onground, boolean force, boolean nohyudoro)
{	
	if (nohyudoro)
	{
		S_StartSound(player->mo, sfx_vwre);
		K_HoldUseItem_AttractionShield(player, itemjustdown, onground, force, nohyudoro);
	}
}

void K_HoldUseItem_AttractionShield(player_t *player, boolean itemjustdown, boolean onground, boolean force, boolean nohyudoro)
{
	(void)onground;
	(void)force;
	(void)itemjustdown;
	
	fixed_t effectscale = 75*player->mo->scale/100;

	if (player->attractioncharge >= ATTRACTIONCHARGETIME)
	{
		if (player->attractioncharge == ATTRACTIONCHARGETIME)
		{
			player->attractioncharge++;
			S_StartSound(player->mo, sfx_ncchip);
		}
		if (S_SoundPlaying(player->mo, sfx_vwre))
		{
			S_StopSoundByID(player->mo, sfx_vwre);
		}
		K_EnableSeekingReticule(player, false);
		player->bananadrag += 4;
	}
	else
	{
		effectscale = Easing_Linear(CLAMP(FixedDiv(player->attractioncharge, ATTRACTIONCHARGETIME), 0, FRACUNIT),
			player->mo->scale/4,
			player->mo->scale/2
		);
		K_EnableSeekingReticule(player, false);
	}

	// stolen from machine water breathing
	if (P_RandomChance(PR_ITEM_ATTRACT, FRACUNIT/5))
	{
		fixed_t r = player->mo->radius>>FRACBITS;
		fixed_t x = player->mo->x + (P_RandomRange(PR_ITEM_ATTRACT, r, -r)<<FRACBITS);
		fixed_t y = player->mo->y + (P_RandomRange(PR_ITEM_ATTRACT, r, -r)<<FRACBITS);
		fixed_t z = player->mo->z + (P_RandomKey(PR_ITEM_ATTRACT, player->mo->height>>FRACBITS)<<FRACBITS);
		mobj_t *spark = P_SpawnMobj(x, y, z, MT_WATERZAP);
		S_StartSound(spark, sfx_beelec);
		spark->color = SKINCOLOR_YELLOW;
		spark->colorized = true;
	}

	if (player->attractioncharge % 8 == 0)
	{
		mobj_t *mo = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_THOK);
		mo->angle = P_RandomRange(PR_ITEM_ATTRACT, 0, 359)*ANG1;
		mo->fuse = P_RandomRange(PR_ITEM_ATTRACT, 20, 50);
		P_SetScale(mo, effectscale);
		mo->destscale = mo->scale/2;
		mo->color = SKINCOLOR_YELLOW;
		mo->colorized = true;

		P_SetTarget(&mo->target, player->mo);
		P_SetMobjState(mo, S_KLIT1);

		mo->renderflags |= RF_VERTICALFLIP|RF_ADD|RF_FULLBRIGHT;
	}
	if (player->attractioncharge % 13 == 0)
	{
		S_StartSound(player->mo, sfx_beelec);
	}

	player->attractioncharge++;
}

void K_ReleaseUseItem_AttractionShield(player_t *player, boolean itemjustdown, boolean onground, boolean force, boolean nohyudoro)
{
	(void)onground;
	(void)force;
	(void)itemjustdown;
	
	if (player->attractioncharge)
	{
		if (S_SoundPlaying(player->mo, sfx_vwre))
		{
			S_StopSoundByID(player->mo, sfx_vwre);
		}

		if (player->attractioncharge >= ATTRACTIONCHARGETIME)
		{
			S_StartSound(player->mo, sfx_kc4c);
			player->attractionattack_hipower = true;
			player->attractionattack = ATTRACTIONATTACKTIME_FULL;
		}
		else
		{
			player->attractionattack = Easing_Linear(
				CLAMP(FixedDiv(player->attractioncharge, ATTRACTIONCHARGETIME), 0, FRACUNIT),
				ATTRACTIONATTACKTIME_MIN * FRACUNIT,
				ATTRACTIONATTACKTIME_MAX * FRACUNIT
			)/FRACUNIT;
		}
		K_DoAttractionShield(player, CLAMP(FixedDiv(player->attractioncharge, ATTRACTIONCHARGETIME), 0, FRACUNIT));
		S_StartSound(player->mo, sfx_s3k81);

		if (player->itemamount > 0)
		{
			K_DecrementAndCheckCurrentItem(player);
			K_PlayAttackTaunt(player->mo);
			K_BotResetItemConfirm(player, false);
		}
		
		K_EnableSeekingReticule(player, true);
	}
	player->attractioncharge = 0;
}

void K_UseItem_FlameShield(player_t *player, boolean itemjustdown, boolean onground, boolean force, boolean nohyudoro)
{	
	if (nohyudoro)
	{
		if (K_GetShieldFromPlayer(player) != KSHIELD_FLAME)
		{
			K_DecrementAndCheckCurrentItem(player);
			player->flametimer = flametime;
			player->flamedash = 0;
			K_SpawnEquippedItem(player, KITEMEQUIP_SHIELD, MT_FLAMESHIELD, 0, 1, NULL);
			S_StartSound(player->mo, sfx_s3k3e);
			// make sure this is after K_DecrementCurrentItem so that equip state stays set properly
			K_SetItemOut(player, KITEM_FLAMESHIELD, 0);
			player->itemflags &= ~IF_HOLDREADY;
		}
		K_HoldUseItem_FlameShield(player, itemjustdown, onground, force, nohyudoro);
	}
}

void K_HoldUseItem_FlameShield(player_t *player, boolean itemjustdown, boolean onground, boolean force, boolean nohyudoro)
{
	(void)onground;
	(void)force;
	(void)itemjustdown;
	
	if (player->equippeditem == KITEM_FLAMESHIELD && (player->itemflags & IF_HOLDREADY))
	{
		SINT8 incr = (gametypes[gametype]->rules & GTR_CLOSERPLAYERS) ? 3 : 2;
		SINT8 metincr = (gametypes[gametype]->rules & GTR_CLOSERPLAYERS) ? 4 : 3;
		SINT8 comincr = (gametypes[gametype]->rules & GTR_CLOSERPLAYERS) ? 10 : 4;

		// uses fuel faster, but raises temperature faster
		if (cv_kartflame_fastfuel.value && (!player->flameburnstop))
		{
			incr = incr * 2;
			comincr = comincr * 2;
			metincr = metincr * 3 / 2;
		}

		// burning a trap / missile stops you from raising temperature for a tiny bit
		if (player->flameburnstop)
			metincr = 0;

		if (onground)
		{
			if (player->flamestore == 0)
			{
				S_StartSound(player->mo, sfx_s3k9b);
				K_PlayBoostTaunt(player->mo);

				if (player->karthud[khud_flamecamtime] == 0)
					player->karthud[khud_flamecamtime] = TICRATE/2;
			}

			player->flamedash += incr;
		}

		// allow burning in the air
		player->flamestore = min(player->flamestore + metincr, FLAMESTOREMAX);
		player->flametimer -= comincr;

		if (player->flametimer <= 0)
			K_PopPlayerShield(player);
	}
}

void K_ReleaseUseItem_FlameShield(player_t *player, boolean itemjustdown, boolean onground, boolean force, boolean nohyudoro)
{
	(void)onground;
	(void)force;
	(void)itemjustdown;

	if (!(player->itemflags & IF_HOLDREADY))
	{
		player->itemflags |= IF_HOLDREADY;
	}
}

void K_UseItem_Hyudoro(player_t *player, boolean itemjustdown, boolean onground, boolean force, boolean nohyudoro)
{
	(void)onground;
	(void)force;
	(void)itemjustdown;
	
	if (nohyudoro)
	{
		K_DecrementAndCheckCurrentItem(player);
		K_DoHyudoroSteal(player); // yes. yes they do.
		K_PlayAttackTaunt(player->mo);
		K_BotResetItemConfirm(player, false);
	}
}

void K_UseItem_PogoSpring(player_t *player, boolean itemjustdown, boolean onground, boolean force, boolean nohyudoro)
{
	if (nohyudoro && (onground || force) && player->pogospring == 0)
	{
		K_DecrementAndCheckCurrentItem(player);
		K_PlayBoostTaunt(player->mo);
		K_DoPogoSpring(player->mo, 32<<FRACBITS, 2);
		player->pogospring = 1;
		K_BotResetItemConfirm(player, false);
	}
}

void K_UseItem_SuperRing(player_t *player, boolean itemjustdown, boolean onground, boolean force, boolean nohyudoro)
{
	(void)onground;
	(void)force;
	(void)itemjustdown;
	
	if (nohyudoro)
	{
		boolean offroad = (player->offroad && K_ApplyOffroad(player));
		player->superringdrag = max(K_GetKartSpeed(player, true, false), player->speed);
		INT32 thrustspeed = 12*player->mo->scale;

		if (onground && !offroad)
		{
			P_Thrust(player->mo, player->mo->angle, thrustspeed);
			P_Thrust(player->mo, K_MomentumAngle(player->mo), thrustspeed);
		}
		else
		{
			thrustspeed = 6*player->mo->scale;

			if (player->pogospring > 0)
				thrustspeed /= 4;

			if (offroad)
				P_Thrust(player->mo, player->mo->angle, thrustspeed);
			P_Thrust(player->mo, K_MomentumAngle(player->mo), thrustspeed);
		}

		// LOUDER!
		S_StartSound(player->mo, sfx_turhit);
		S_StartSound(player->mo, sfx_turhit);
		S_StartSound(player->mo, sfx_s23c);

		K_SpawnDashDustRelease(player, true);

		UINT8 modifier = 32 / (player->kartspeed + player->kartweight + 2);

		player->ringboost += (TICRATE/6) + modifier;
		if (onground)
			player->outruntime += (TICRATE/6) + modifier;
		player->superringleeway = 5;

		// make friction not instantly kill the new momentum
		player->rmomx = player->mo->momx - player->cmomx;
		player->rmomy = player->mo->momy - player->cmomy;

		K_DecrementAndCheckCurrentItem(player);
		K_AwardScaledPlayerRings(player, ASR_SUPERRING);
		K_BotResetItemConfirm(player, false);
	}
}

void K_UseItem_KitchenSink(player_t *player, boolean itemjustdown, boolean onground, boolean force, boolean nohyudoro)
{
	(void)onground;
	(void)force;
	(void)itemjustdown;
	
	if (player->equippeditem == KITEM_KITCHENSINK)
	{
		K_DecrementAndCheckCurrentItem(player);
		K_ThrowKartItem(player, false, MT_SINK, 1, 2);
		K_PlayAttackTaunt(player->mo);
		K_UnsetItemOut(player);
		K_UpdateHnextList(player, true);
		K_BotResetItemConfirm(player, false);
	}
	else if (nohyudoro)
	{
		K_SetItemOut(player, KITEM_KITCHENSINK, IF_ITEMOUT);
		S_StartSound(player->mo, sfx_s254);
		K_SpawnEquippedItem(player, KITEMEQUIP_TRAIL, MT_SINK_SHIELD, 0, 1, player->mo);
		K_BotResetItemConfirm(player, false);

		// Woah this could be big, lets get the inside scoop...
		if (!player->exiting)
			K_DirectorForceSwitch(player - players, 1);
	}
}

void K_UseItem_EggBrick(player_t *player, boolean itemjustdown, boolean onground, boolean force, boolean nohyudoro)
{
	(void)onground;
	(void)force;
	(void)itemjustdown;
	
	if (nohyudoro && player->bricktimer <= 0)
	{
		S_StartSound(player->mo, sfx_bckuse);
		player->bricktimer = stealtime;
		K_DecrementAndCheckCurrentItem(player);
		K_BotResetItemConfirm(player, false);
	}
}

void K_UseItem_Sad(player_t *player, boolean itemjustdown, boolean onground, boolean force, boolean nohyudoro)
{
	(void)onground;
	(void)force;
	(void)itemjustdown;
	
	if (nohyudoro && player->sadtimer <= 0)
	{
		player->sadtimer = stealtime;
		K_DecrementAndCheckCurrentItem(player);
		K_BotResetItemConfirm(player, false);
	}
}

void K_ItemThink_ThunderShield(player_t *player)
{
	if (K_GetShieldFromPlayer(player) != KSHIELD_THUNDER)
	{
		K_SpawnEquippedItem(player, KITEMEQUIP_SHIELD, MT_THUNDERSHIELD, 0, 1, NULL);
		S_StartSound(player->mo, sfx_s3k41);
		K_SetItemOut(player, KITEM_THUNDERSHIELD, 0);
	}
}

void K_ItemThink_AttractionShield(player_t *player)
{
	if (K_GetShieldFromPlayer(player) != KSHIELD_ATTRACTION)
	{
		K_SpawnEquippedItem(player, KITEMEQUIP_SHIELD, MT_ATTRACTIONSHIELD, 0, 1, NULL);
		S_StartSound(player->mo, sfx_attrsg);
		K_SetItemOut(player, KITEM_THUNDERSHIELD, 0);
	}
}

void K_ItemThink_BubbleShield(player_t *player)
{
	if (K_GetShieldFromPlayer(player) != KSHIELD_BUBBLE)
	{
		K_SpawnEquippedItem(player, KITEMEQUIP_SHIELD, MT_BUBBLESHIELD, 0, 1, NULL);
		S_StartSound(player->mo, sfx_s3k3f);
		if (player->bubblehealth <= 0 || player->bubblehealth > MAXBUBBLEHEALTH)
			player->bubblehealth = MAXBUBBLEHEALTH;
		// bit of mercy so bubble doesn't get instantly killed on creation
		player->bubblebuffer = TICRATE/2;
		K_SetItemOut(player, KITEM_BUBBLESHIELD, 0);
		player->itemflags &= ~IF_HOLDREADY;
		return;
	}

	if ((player->cmd.buttons & BT_ATTACK && (player->itemflags & IF_HOLDREADY))
			|| (player->bubbleblowup > 0 && player->bubblecool <= bubbletime/2)) // auto
	{
		if (player->bubbleblowup <= 0 && player->bubblecool <= 0)
		{
			S_StartSound(player->mo, sfx_s3k75);
		}

		if (player->bubblecool < bubbletime && player->bubblehealth > 0)
		{
			player->bubbleblowup += 3;
			player->bubblehealth--;
		}
		else if (player->bubblecool >= bubbletime)
			player->bubbleblowup++; // overcharge bonus

		if (++player->bubblecool >= bubbletime + TICRATE/2)
		{
			// If you overcharge the Bubble Shield at
			// any point, it pops and gives you a boost.
			K_PopPlayerShield(player);

			if (P_IsObjectOnGround(player->mo) || (player->pogospring))
			{
				// If you're on the ground, you're going to
				// immediately feel the effects of this boost.
				// Play a sound to let everyone know!
				S_StartSoundAtVolume(player->mo, sfx_cdfm57, 170);
				K_PlayBoostTaunt(player->mo);
			}

			// experiment: don't boost, just give invulnerability
			if (cv_kartbubble_boost_allow.value)
			{
				player->bubbleboost = BUBBLEBOOSTTIME;
			}
			player->flashing = 4*TICRATE/7;
		}
	}
	else
	{
		if (player->bubblecool > bubbletime)
			player->bubblecool = bubbletime;
	
		if (player->bubbleblowup > 0)
		{
			player->bubbleblowup--;
			if (player->bubbleblowup == 0)
			{
				K_BotResetItemConfirm(player, false);
			}
			if (player->bubblecool < bubbletime)
				player->bubblecool++;
		}
	
		if (player->bubbleblowup == 0 && player->bubblecool > 0)
		{
			player->bubblecool--;
	
			if (player->bubblecool == 0 && player->bubblehealth <= 0)
				K_PopPlayerShield(player);
		}
	}

	if (player->bubblecool > 0 && !(player->cmd.buttons & BT_ATTACK))
	{
		player->itemflags &= ~IF_HOLDREADY;
	}
	else if (player->bubblecool <= 0 && !(player->cmd.buttons & BT_ATTACK))
	{
		player->itemflags |= IF_HOLDREADY;
	}
}

void K_ItemThink_BonusRoll(player_t *player)
{
	K_StartRoulette(player, KROULETTETYPE_NORMAL);
	player->itemamount = 0;
}
