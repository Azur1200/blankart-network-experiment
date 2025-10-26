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
/// \file  k_odds.cpp
/// \brief Kart item odds systems.

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
#include "s_sound.h"
#include "typedef.h"

#include "k_battle.h"
#include "k_boss.h"
#include "k_bot.h"
#include "k_kart.h"
#include "k_waypoint.h"

#include "k_cluster.hpp"
#include "k_odds.h"

consvar_t *KartItemCVars[NUMKARTRESULTS-1] =
{
	&cv_sneaker,
	&cv_rocketsneaker,
	&cv_invincibility,
	&cv_banana,
	&cv_eggmanmonitor,
	&cv_orbinaut,
	&cv_jawz,
	&cv_mine,
	&cv_ballhog,
	&cv_selfpropelledbomb,
	&cv_grow,
	&cv_shrink,
	&cv_thundershield,
	&cv_hyudoro,
	&cv_pogospring,
	&cv_kitchensink,
	&cv_superring,
	&cv_landmine,
	&cv_bubbleshield,
	&cv_flameshield,
	&cv_dualsneaker,
	&cv_triplesneaker,
	&cv_triplebanana,
	&cv_decabanana,
	&cv_tripleorbinaut,
	&cv_quadorbinaut,
	&cv_dualjawz
};

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

// Less ugly 2D arrays
// Expanded 16-tier useodds, for more flexible calculations.
// Item odds are now based around the max number being 75 as well. 
static UINT8 K_KartItemOddsRace[NUMKARTRESULTS-1][MAXODDS] =
{
	//0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
    //B   C   D   E   F   G   H   I   J   K   L   M   N   O   P   Q
    { 0,  0,  0,  0,  0,  9, 18, 30, 10,  0,  0,  0,  0,  0,  0,  0}, // Sneaker
    { 0,  0,  0,  0,  0,  0,  0,  0,  0,  3,  7, 15, 42, 48, 37, 37}, // Rocket Sneaker
    { 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  4, 24, 50, 75, 75}, // Invincibility
    {67, 52, 27, 24, 20, 12,  5,  0,  0,  0,  0,  0,  0,  0,  0,  0}, // Banana
    {18, 15, 12,  7,  3,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0}, // Eggman Monitor
    {20, 35, 25, 12,  8,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0}, // Orbinaut
    { 0, 11, 22,  9,  7,  5,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0}, // Jawz
    { 0,  0,  5,  3,  2,  1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0}, // Mine
    { 0,  0,  0,  0,  2,  2,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0}, // Ballhog
    { 0,  0,  2,  3,  4,  5,  6,  9, 10, 12,  8,  5,  2,  2,  2,  0}, // Self-Propelled Bomb
    { 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  7, 18,  9,  0,  0,  0}, // Grow
    { 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  3,  7,  3,  0,  0}, // Shrink
    {12,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0}, // Thunder Shield
    { 0,  0,  0,  0,  0,  2,  2,  5,  4,  2,  0,  0,  0,  0,  0,  0}, // Hyudoro
    { 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0}, // Pogo Spring
    { 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0}, // Kitchen Sink
    { 7, 11, 15,  7,  3,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0}, // Super Ring
    { 8,  2,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0}, // Land Mine
    { 0,  0,  0, 12, 15,  8,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0}, // Bubble Shield
    { 0,  0,  0,  0,  0,  0,  0,  0,  0,  9, 18, 36, 37, 18,  7,  0}, // Flame Shield
    { 0,  0,  0,  0,  0,  0,  0,  5, 25, 20,  9,  0,  0,  0,  0,  0}, // Sneaker x2
    { 0,  0,  0,  0,  0,  0,  0,  0,  0,  7, 21, 45, 32,  7,  0,  0}, // Sneaker x3
    { 0,  7,  7,  7,  9,  7,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0}, // Banana x3
    { 0,  0,  0,  0,  0,  0,  3,  0,  0,  0,  0,  0,  0,  0,  0,  0}, // Banana x10
    { 0,  0,  0,  2,  5,  2,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0}, // Orbinaut x3
    { 0,  0,  0,  0,  1,  1,  1,  0,  0,  0,  0,  0,  0,  0,  0,  0}, // Orbinaut x4
    { 0,  0,  0,  1,  5,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0}  // Jawz x2
};

static UINT8 K_KartItemOddsBattle[NUMKARTRESULTS][2] =
{
	//R  S
	{ 2, 1 }, // Sneaker
	{ 0, 0 }, // Rocket Sneaker
	{ 4, 1 }, // Invincibility
	{ 0, 0 }, // Banana
	{ 1, 0 }, // Eggman Monitor
	{ 8, 0 }, // Orbinaut
	{ 8, 1 }, // Jawz
	{ 6, 1 }, // Mine
	{ 2, 1 }, // Ballhog
	{ 0, 0 }, // Self-Propelled Bomb
	{ 2, 1 }, // Grow
	{ 0, 0 }, // Shrink
	{ 4, 0 }, // Thunder Shield
	{ 2, 0 }, // Hyudoro
	{ 3, 0 }, // Pogo Spring
	{ 0, 0 }, // Kitchen Sink
	{ 0, 0 }, // Super Ring
	{ 2, 0 }, // Land Mine
	{ 1, 0 }, // Bubble Shield
	{ 1, 0 }, // Flame Shield
	{ 0, 0 }, // Sneaker x2
	{ 0, 1 }, // Sneaker x3
	{ 0, 0 }, // Banana x3
	{ 1, 1 }, // Banana x10
	{ 2, 0 }, // Orbinaut x3
	{ 1, 1 }, // Orbinaut x4
	{ 5, 1 }  // Jawz x2
};

static UINT8 K_KartItemOddsSpecial[NUMKARTRESULTS-1][4] =
{
	//M  N  O  P
	{ 1, 1, 0, 0 }, // Sneaker
	{ 0, 0, 0, 0 }, // Rocket Sneaker
	{ 0, 0, 0, 0 }, // Invincibility
	{ 0, 0, 0, 0 }, // Banana
	{ 0, 0, 0, 0 }, // Eggman Monitor
	{ 1, 1, 0, 0 }, // Orbinaut
	{ 1, 1, 0, 0 }, // Jawz
	{ 0, 0, 0, 0 }, // Mine
	{ 0, 0, 0, 0 }, // Ballhog
	{ 0, 0, 0, 1 }, // Self-Propelled Bomb
	{ 0, 0, 0, 0 }, // Grow
	{ 0, 0, 0, 0 }, // Shrink
	{ 0, 0, 0, 0 }, // Thunder Shield
	{ 0, 0, 0, 0 }, // Hyudoro
	{ 0, 0, 0, 0 }, // Pogo Spring
	{ 0, 0, 0, 0 }, // Kitchen Sink
	{ 0, 0, 0, 0 }, // Super Ring
	{ 0, 0, 0, 0 }, // Land Mine
	{ 0, 0, 0, 0 }, // Bubble Shield
	{ 0, 0, 0, 0 }, // Flame Shield
	{ 0, 1, 1, 0 }, // Sneaker x2
	{ 0, 0, 1, 1 }, // Sneaker x3
	{ 0, 0, 0, 0 }, // Banana x3
	{ 0, 0, 0, 0 }, // Banana x10
	{ 0, 1, 1, 0 }, // Orbinaut x3
	{ 0, 0, 1, 1 }, // Orbinaut x4
	{ 0, 0, 1, 1 }  // Jawz x2
};

// Cooldown time table; contains both base (index 0) and current (index 1)
// times. Base times are in seconds, current times are in tics.
tic_t ItemBGone[NUMKARTRESULTS][2] =
{
	//BASE  CURR
	{ 0,    0 }, // Null/Sad Face
	{ 0,    0 }, // Sneaker
	{ 0,    0 }, // Rocket Sneaker
	{ 0,    0 }, // Invincibility
	{ 0,    0 }, // Banana
	{ 10,   0 }, // Eggman Monitor
	{ 0,    0 }, // Orbinaut
	{ 5,    0 }, // Jawz
	{ 0,    0 }, // Mine
	{ 10,   0 }, // Ballhog
	{ 20,   0 }, // Self-Propelled Bomb
	{ 3,    0 }, // Grow
	{ 20,   0 }, // Shrink
	{ 0,    0 }, // Thunder Shield
	{ 20,   0 }, // Hyudoro
	{ 0,    0 }, // Pogo Spring
	{ 0,    0 }, // Kitchen Sink
	{ 0,    0 }, // Super Ring
	{ 0,    0 }, // Land Mine
	{ 5,    0 }, // Bubble Shield
	{ 8,    0 }, // Flame Shield
	{ 0,    0 }, // Sneaker x2
	{ 0,    0 }, // Sneaker x3
	{ 0,    0 }, // Banana x3
	{ 30,   0 }, // Banana x10
	{ 10,   0 }, // Orbinaut x3
	{ 20,   0 }, // Orbinaut x4
	{ 10,   0 }  // Jawz x2
};

// TODO: Vectorize all item tables and shove them into gamemode-uniqe pools (k_oddstable.cpp?).
// Basically necessary for any hopes of easy SOC support in the near future.

/**	\brief	Sets the cooldown timer for an item. When active, the item is removed
 * from all players' item pools for some time.

	\param	item		(SINT8) item to assign cooldown for
	\param	time		(tic_t) cooldown time

	\return	void
*/
void K_SetBGone(SINT8 item, tic_t time)
{
	ItemBGone[item][GONER_CURRCOOLDOWN] = time;
}

/**	\brief	Sets the cooldown timer for an item to its "base" (intended) time.

	\param	item		(SINT8) item to assign base cooldown for

	\return	void
*/
void K_SetBGoneToBase(SINT8 item)
{
	ItemBGone[item][GONER_CURRCOOLDOWN] = (ItemBGone[item][GONER_BASECOOLDOWN] * TICRATE);
}

/**	\brief	Gets the cooldown timer for an item.

	\param	item		(SINT8) item to retrieve cooldown for
	\param	base		(bool) if true, returns the "base" (intended) cooldown instead

	\return	(tic_t) cooldown time
*/
tic_t K_GetBGone(SINT8 item, boolean base)
{
	return ItemBGone[item][base ? GONER_BASECOOLDOWN : GONER_CURRCOOLDOWN] * (base ? TICRATE : 1);
}

// Magic number distance for use with item roulette tiers
#define ACTIVEDISTVAR (K_UsingLegacyCheckpoints() ? DISTVAR_LEGACY : DISTVAR)
#define ACTIVESPBDIST (K_UsingLegacyCheckpoints() ? SPBDISTVAR_LEGACY : SPBDISTVAR)

#define SPBSTARTDIST (ACTIVESPBDIST) // Distance when SPB can start appearing
#define SPBFORCEDIST (7 * ACTIVESPBDIST / 2) // Distance when SPB is forced onto 2nd place (3.5x SPBDISTVAR)

#define ENDDIST (12*ACTIVEDISTVAR) // Distance when the game stops giving you bananas

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
static UINT32 K_IntDistanceForMap(fixed_t curx,
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

SINT8 K_ItemResultToType(SINT8 getitem)
{
	if (getitem <= 0 || getitem >= NUMKARTRESULTS) // Sad (Fallback)
	{
		if (getitem != 0)
		{
			CONS_Printf("ERROR: K_GetItemResultToItemType - Item roulette gave bad item (%d) :(\n", getitem);
		}

		return KITEM_SAD;
	}

	if (getitem >= NUMKARTITEMS)
	{
		switch (getitem)
		{
			case KRITEM_DUALSNEAKER:
			case KRITEM_TRIPLESNEAKER:
				return KITEM_SNEAKER;

			case KRITEM_TRIPLEBANANA:
			case KRITEM_TENFOLDBANANA:
				return KITEM_BANANA;

			case KRITEM_TRIPLEORBINAUT:
			case KRITEM_QUADORBINAUT:
				return KITEM_ORBINAUT;

			case KRITEM_DUALJAWZ:
				return KITEM_JAWZ;

			default:
				I_Error("K_ItemResultToType: Bad item redirect for result %d\n", getitem);
				break;
		}
	}

	return getitem;
}

UINT8 K_ItemResultToAmount(SINT8 getitem)
{
	switch (getitem)
	{
		case KRITEM_DUALSNEAKER:
		case KRITEM_DUALJAWZ:
			return 2;

		case KRITEM_TRIPLESNEAKER:
		case KRITEM_TRIPLEBANANA:
		case KRITEM_TRIPLEORBINAUT:
			return 3;

		case KRITEM_QUADORBINAUT:
			return 4;

		case KRITEM_TENFOLDBANANA:
			return 10;

		default:
			return 1;
	}
}

/**	\brief	Item Roulette for Kart

	\param	player		player
	\param	getitem		what item we're looking for

	\return	void
*/
static void K_KartGetItemResult(player_t *player, SINT8 getitem)
{
	if (getitem == KITEM_SPB || getitem == KITEM_SHRINK) // Indirect items
		indirectitemcooldown = 20*TICRATE;

	if (K_GetBGone(getitem, true) > 0) // Item cooldowns
	{
		K_SetBGoneToBase(getitem);
	}

	K_BotResetItemConfirm(player, true);

	player->itemtype = K_ItemResultToType(getitem);
	UINT8 itemamount = K_ItemResultToAmount(getitem);
	if (cv_kartdebugitem.value != KITEM_NONE && cv_kartdebugitem.value == player->itemtype && cv_kartdebugamount.value > 1)
		itemamount = cv_kartdebugamount.value;
	player->itemamount = itemamount;
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
#define INVINDESPERATION 4
#define MAXINVODDS ((MAXPROBABILITY * 2) * INVINDESPERATION)

// Odds value for Alt. Invin. to force itself on trailing players.
#define INVFORCEODDS (MAXINVODDS / 2)

static INT32 K_KartGetInvincibilityOdds(UINT32 dist)
{
	UINT32 invindist = INVINDIST/2;

	if (dist < invindist)
		return 0;

	INT32 finodds = 0;
	fixed_t fac = (min(32000, (fixed_t)dist) * FRACUNIT) / (INVINDIST);

	if (fac > FRACUNIT)
	{
		// Desperation! Climb exponentially until Invincibility is practically guaranteed.
		fac = (((min(32000, (fixed_t)dist) * FRACUNIT) / (INVINDIST)) - FRACUNIT) >> 1;
		finodds = Easing_InCubic(fac, INVODDS, MAXINVODDS);
	}
	else
	{
		if (fac <= FRACHALF)
		{
			// Invincibility is practically useless at lower distances.
			// At below half, remove it from the item pool.
			return 0;
		}
		// Basic linear climb to "reasonable" odds.
		finodds = FixedMul(INVODDS, fac);
	}

	return min(MAXINVODDS, finodds);
}

// Assigns general cooldowns to shield items.
void K_KartHandleShieldCooldown(SINT8 item)
{
	INT32 i;

	UINT8 pingame = 0, pexiting = 0;

	INT32 shieldtype = KSHIELD_NONE;
	shieldtype = K_GetShieldFromItem(item);

	if (shieldtype == KSHIELD_NONE)
	{
		// Not a shield!
		return;
	}

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator)
			continue;

		if (!(gametyperules & GTR_BUMPERS) || players[i].bumper)
			pingame++;

		if (players[i].exiting)
			pexiting++;

		if (((shieldtype == K_GetShieldFromItem(players[i].itemtype))
		    || (shieldtype == K_GetShieldFromPlayer(&players[i]))))
		{
			// If this shield has a cooldown, force-apply the cooldown preeemptively for
			// the entire time the shield is being held by a player.
			if (K_GetBGone(item, true) > 0)
			{
				K_SetBGoneToBase(item);
				break;
			}
		}
	}
}

/**	\brief	Item Roulette for Kart

	\param	player	player object passed from P_KartPlayerThink

	\return	void
*/

INT32 K_KartGetItemOdds(
	UINT8 pos, SINT8 item,
	UINT32 ourDist,
    UINT32 clusterDist,
	fixed_t mashed,
	boolean spbrush, boolean bot, boolean rival, boolean inBottom)
{
	INT32 newodds;
	INT32 i;

	UINT8 pingame = 0, pexiting = 0;

	SINT8 first = -1, second = -1;
	UINT32 firstDist = UINT32_MAX;
	UINT32 secondToFirst = UINT32_MAX;

	boolean powerItem = false;
	boolean cooldownOnStart = false;
	boolean indirectItem = false;
	boolean notNearEnd = false;
	boolean notForBottom = false;

	INT32 shieldtype = KSHIELD_NONE;

	I_Assert(item > KITEM_NONE); // too many off by one scenarioes.
	I_Assert(KartItemCVars[NUMKARTRESULTS-2] != NULL); // Make sure this exists

	if (!KartItemCVars[item-1]->value && !modeattacking)
		return 0;

	/*
	if (bot)
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
	(void)bot;

	INT32 oddsmul = BASEODDSMUL;

	if (gametyperules & GTR_BATTLEODDS)
	{
		I_Assert(pos < 2); // DO NOT allow positions past the bounds of the table
		newodds = K_KartItemOddsBattle[item-1][pos];
		oddsmul = BATTLEODDSMUL;
	}
	else if (gametyperules & GTR_RACEODDS)
	{
		I_Assert(pos < MAXODDS); // Ditto
		newodds = K_KartItemOddsRace[item-1][pos];
	}
	else
	{
		newodds = 0;
	}

	// Blow up the odds with a multiplier.
	newodds *= oddsmul;

	shieldtype = K_GetShieldFromItem(item);

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator)
			continue;

		if (!(gametyperules & GTR_BUMPERS) || players[i].bumper)
			pingame++;

		if (players[i].exiting)
			pexiting++;

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

		if (!K_UsingLegacyCheckpoints())
		{
			firstDist = players[first].distancetofinish;

			if (mapobjectscale != FRACUNIT)
			{
				firstDist = FixedDiv(firstDist * FRACUNIT, mapobjectscale) / FRACUNIT;
			}

			secondToFirst = K_ScaleItemDistance(
				players[second].distancetofinish - players[first].distancetofinish,
				pingame, spbrush
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
				pingame, spbrush
			);
		}
	}

	switch (item)
	{
		case KITEM_BANANA:
			notNearEnd = true;
			notForBottom = true;
			break;
		case KITEM_EGGMAN:
			// It blows you up and is overall ridiculous. This was *overdue*.
			cooldownOnStart = true;
			powerItem = true;

			notNearEnd = true;
			notForBottom = true;
			break;
		case KITEM_SUPERRING:
			notNearEnd = true;
			notForBottom = true;

			if ((K_RingsActive() == false)) // No rings rolled if rings are turned off.
			{
				newodds = 0;
			}

			break;
		case KITEM_ROCKETSNEAKER:
		case KITEM_LANDMINE:
		case KRITEM_TRIPLESNEAKER:
			powerItem = true;
			break;
		case KITEM_BUBBLESHIELD:
			// Experiment: Given the refactoring of the item, remove it from the cooldown pool.
			// Also: a given.
		case KITEM_BALLHOG:
		case KITEM_JAWZ:
		case KRITEM_DUALJAWZ:
		case KRITEM_TRIPLEORBINAUT:
		case KRITEM_QUADORBINAUT:
			notForBottom = true;
			powerItem = true;
			break;
		case KRITEM_TRIPLEBANANA:
		case KRITEM_TENFOLDBANANA:
			powerItem = true;
			notForBottom = true;
			notNearEnd = true;
			break;
		case KITEM_INVINCIBILITY:
			if ((K_GetKartInvinType() == KARTINVIN_ALTERN) && (gametyperules & GTR_RACEODDS))
			{
				// It's a power item, yes, but we don't want mashing to lessen
				// its chances, so we lie to the game's face.
				// Nonetheless, apply the start cooldown.
				cooldownOnStart = true;

				// Also, PLEASE prevent shitty last lap bagging endings.
				notNearEnd = true;

				// Unique odds for Invincibility.
				newodds = K_KartGetInvincibilityOdds(clusterDist);

				// Special case: if you're SERIOUSLY far behind before the cooldown
				// finishes, remove the cooldown flag.
				if (newodds >= INVFORCEODDS)
				{
					cooldownOnStart = false;
				}
                
				newodds *= BASEODDSMUL;
				break;
			}
		/*FALLTHRU*/
		case KITEM_MINE:
		case KITEM_GROW:
		case KITEM_FLAMESHIELD:
			cooldownOnStart = true;
			powerItem = true;
			break;
		case KITEM_SPB:
			cooldownOnStart = true;
			indirectItem = true;
			notNearEnd = true;

			if (!K_UsingLegacyCheckpoints() && firstDist < (UINT32)ENDDIST) // No SPB near the end of the race
			{
				newodds = 0;
			}
			else if (K_UsingLegacyCheckpoints() && pexiting > 0)
			{
				newodds = 0;
			}
			else
			{
				const INT32 distFromStart = max(secondToFirst - SPBSTARTDIST, 0);
				const INT32 distRange = SPBFORCEDIST - SPBSTARTDIST;
				const INT32 mulMax = 24;

				INT32 multiplier = (distFromStart * mulMax) / distRange;

				if (multiplier < 0)
					multiplier = 0;
				if (multiplier > mulMax)
					multiplier = mulMax;

				newodds *= multiplier;
			}
			break;
		case KITEM_SHRINK:
			cooldownOnStart = true;
			powerItem = true;
			indirectItem = true;
			notNearEnd = true;

			if (pingame-1 <= pexiting)
				newodds = 0;
			break;
		case KITEM_THUNDERSHIELD:
			cooldownOnStart = true;
			powerItem = true;

			if (spbplace != -1)
				newodds = 0;
			break;
		case KITEM_HYUDORO:
			cooldownOnStart = true;
			break;
		default:
			break;
	}

	// In very small matches, remove the stupid bottom half item limiter
	if (pingame < 6)
	{
		notForBottom = false;
	}

	if (newodds == 0)
	{
		// Nothing else we want to do with odds matters at this point :p
		return newodds;
	}

	if (K_GetBGone(item, false) > 0)
	{
		// (Replaces hyubgone) This item is on cooldown; don't let it get rolled.
		newodds = 0;
	}
	else if ((indirectItem == true) && (indirectitemcooldown > 0))
	{
		// Too many items that act indirectly in a match can feel kind of bad.
		newodds = 0;
	}
	else if ((cooldownOnStart == true) && (leveltime < (30*TICRATE)+starttime))
	{
		// This item should not appear at the beginning of a race. (Usually really powerful crowd-breaking items)
		newodds = 0;
	}
	else if (!K_UsingLegacyCheckpoints() && (notNearEnd == true) && (ourDist < (UINT32)ENDDIST))
	{
		// This item should not appear at the end of a race. (Usually trap items that lose their effectiveness)
		newodds = 0;
	}
	else if ((notForBottom == true) && (inBottom == true))
	{
		// This item should not appear for losing players. (Usually items that feel less effective at these positions)
		newodds = 0;
	}
	else if (powerItem == true)
	{
		// This item is a "power item". This activates "frantic item" toggle related functionality.
		fixed_t fracOdds = newodds * FRACUNIT;

		if (franticitems == true)
		{
			// First, power items multiply their odds by 2 if frantic items are on; easy-peasy.
			fracOdds *= 2;
		}

		if (rival == true)
		{
			// The Rival bot gets frantic-like items, also :p
			fracOdds *= 2;
		}

		fracOdds = FixedMul(fracOdds, FRACUNIT + K_ItemOddsScale(pingame, spbrush));

		if (mashed > 0)
		{
			// Lastly, it *divides* it based on your mashed value, so that power items are less likely when you mash.
			fracOdds = FixedDiv(fracOdds, FRACUNIT + mashed);
		}

		newodds = fracOdds / FRACUNIT;
	}

	return newodds;
}

//{ SRB2kart Roulette Code - Distance Based, yes waypoints

UINT8 K_FindUseodds(const player_t *player, fixed_t mashed, UINT32 pdis, UINT8 bestbumper, boolean spbrush)
{
	fixed_t oddsfac = max(FRACUNIT, (MAXODDS * FRACUNIT) / 8);
	INT32 oddsdiv = ((MAXODDS - 1) * 2);

	UINT8 i, j;
	UINT8 useodds = 0;
	UINT8 disttable[oddsdiv];
	UINT8 distlen = 0;
	boolean oddsvalid[MAXODDS];

	// Unused now, oops :V
	(void)bestbumper;

	for (i = 0; i < MAXODDS; i++)
	{
		boolean available = false;

		if ((gametyperules & GTR_BATTLEODDS) && i > 1)
		{
			oddsvalid[i] = false;
			break;
		}

		for (j = 1; j < NUMKARTRESULTS; j++)
		{
			if (K_KartGetItemOdds(
					i, j,
					player->distancetofinish,
                    player->distancefromcluster,
					mashed,
					spbrush,
					player->bot,
					(player->bot && player->botvars.rival),
					K_IsPlayerLosing(player)
				) > 0)
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
			distlen = min(oddsdiv - 1, distlen); \
        }

	if (gametyperules & GTR_BATTLEODDS) // Battle Mode
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
	else if (gametyperules & GTR_RACEODDS)
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
		else if (pdis > (UINT32)ACTIVEDISTVAR * ((12 * distlen) / oddsdiv))
			useodds = disttable[distlen-1];
		else
		{
			for (i = 1; i < (oddsdiv - 1); i++)
			{
				INT32 distcalc = min(distlen-1, (i * distlen) / oddsdiv);

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

INT32 K_GetRollingRouletteItem(player_t *player)
{
	static UINT8 translation[NUMKARTITEMS-1];
	static UINT16 roulette_size;

	static INT16 odds_cached = -1;

	// Race odds have more columns than Battle
	const UINT8 EMPTYODDS[sizeof K_KartItemOddsRace[0]] = {0};

	if (odds_cached != gametype)
	{
		UINT8 *odds_row;
		size_t odds_row_size;

		UINT8 i;

		roulette_size = 0;

		if (gametyperules & GTR_BATTLEODDS)
		{
			odds_row = K_KartItemOddsBattle[0];
			odds_row_size = sizeof K_KartItemOddsBattle[0];
		}
		else
		{
			odds_row = K_KartItemOddsRace[0];
			odds_row_size = sizeof K_KartItemOddsRace[0];
		}

		for (i = 1; i < NUMKARTITEMS; ++i)
		{
			if (memcmp(odds_row, EMPTYODDS, odds_row_size))
			{
				translation[roulette_size] = i;
				roulette_size++;
			}

			odds_row += odds_row_size;
		}

		roulette_size *= 3;
		odds_cached = gametype;
	}

	return translation[(player->itemroulette % roulette_size) / 3];
}

// Legacy odds are fickle and finicky, so we exaggerate distances
// to simulate parity with pathfind odds.
#define LEGACYODDSEXAGGERATE (2*FRACUNIT/3)

UINT32 K_CalculateInitalPDIS(const player_t *player, UINT8 pingame)
{
	UINT8 i;
	UINT32 pdis = 0;

	(void)pingame;

	if (!K_UsingLegacyCheckpoints())
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
		SINT8 sortedPlayers[MAXPLAYERS];
		UINT8 sortLength = 0;

		memset(sortedPlayers, -1, sizeof(sortedPlayers));

		if (player->mo != NULL && P_MobjWasRemoved(player->mo) == false)
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

					if (pos <= 0 || pos > MAXPLAYERS)
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
	}

	// Exaggerate odds; don't you love the legacy system? :Trollic:
	pdis = FixedMul(pdis, LEGACYODDSEXAGGERATE);

	return pdis;
}

#undef LEGACYODDSEXAGGERATE

UINT32 K_CalculatePDIS(const player_t *player, UINT8 numPlayers, boolean *spbrush)
{
	UINT32 pdis = 0;

	pdis = K_CalculateInitalPDIS(player, numPlayers);

	if (spbplace != -1 && player->position == spbplace+1)
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

static boolean K_CanForceSPB(player_t *player, UINT32 pdis)
{
	boolean battlecond, racecond;

	battlecond = ((gametyperules & GTR_WANTED) && (gametyperules & GTR_WANTEDSPB) && (mostwanted != -1) && (!K_IsPlayerMostWanted(player)));
	racecond = ((gametyperules & GTR_CIRCUIT) && player->position == 2 && pdis > (UINT32)SPBFORCEDIST);

	return ((battlecond) || (racecond));
}

void K_KartItemRoulette(player_t *player, ticcmd_t *cmd)
{
	INT32 i;
	UINT8 pingame = 0;
	UINT8 roulettestop;
	UINT32 pdis = 0;
	UINT8 useodds = 0;
	INT32 spawnchance[NUMKARTRESULTS];
	INT64 totalspawnchance = 0; // 75-scale numbers are going to get BIG. This is for paranoia's sake.
	UINT8 bestbumper = 0;
	fixed_t mashed = 0;
	boolean dontforcespb = false;
	boolean spbrush = false;

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
	if ((player->itemroulette % 3) == 1 && P_IsDisplayPlayer(player))
	{
#define PLAYROULETTESND S_StartSound(NULL, sfx_itrol1 + ((player->itemroulette / 3) % 8))
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
		&& !(player->itemflags & (IF_ITEMOUT|IF_EGGMANOUT|IF_USERINGS)))
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
		player->itemroulette = KROULETTE_DISABLED;
		player->roulettetype = KROULETTETYPE_NORMAL;
		if (P_IsDisplayPlayer(player))
			S_StartSound(NULL, sfx_itrole);
		return;
	}

	// SPECIAL CASE No. 2:
	// Give a debug item instead if specified
	if (cv_kartdebugitem.value != 0 && !modeattacking)
	{
		K_KartGetItemResult(player, cv_kartdebugitem.value);
		player->itemblink = TICRATE;
		player->itemblinkmode = KITEMBLINKMODE_KARMA;
		player->itemroulette = KROULETTE_DISABLED;
		player->roulettetype = KROULETTETYPE_NORMAL;
		if (P_IsDisplayPlayer(player))
			S_StartSound(NULL, sfx_dbgsal);
		return;
	}

	// SPECIAL CASE No. 3:
	// This Gametype never specified an odds type. Roll something random please!
	if (!(gametyperules & GTR_RACEODDS) && !(gametyperules & GTR_BATTLEODDS))
	{
		SINT8 itemroll = P_RandomRange(KITEM_SNEAKER, NUMKARTITEMS - 1);

		K_KartGetItemResult(player, itemroll);
		player->itemblink = TICRATE;
		player->itemblinkmode = KITEMBLINKMODE_NORMAL;
		player->itemroulette = KROULETTE_DISABLED;
		player->roulettetype = KROULETTETYPE_NORMAL;
		if (P_IsDisplayPlayer(player))
			S_StartSound(NULL, sfx_itrolf);
		return;
	}

	// SPECIAL CASE No. 4:
	// Record Attack / alone mashing behavior
	if ((modeattacking || pingame == 1)
		&& ((gametyperules & GTR_RACEODDS)
		|| ((gametyperules & GTR_BATTLEODDS) && (itembreaker || bossinfo.boss))))
	{
		if ((gametyperules & GTR_RACEODDS))
		{
			if (mashed && ((K_RingsActive() == true) && (modeattacking || cv_superring.value))) // ANY mashed value? You get rings.
			{
				K_KartGetItemResult(player, KITEM_SUPERRING);
				player->itemblinkmode = KITEMBLINKMODE_MASHED;
				if (P_IsDisplayPlayer(player))
					S_StartSound(NULL, sfx_itrolm);
			}
			else
			{
				if (modeattacking || cv_sneaker.value) // Waited patiently? You get a sneaker!
					K_KartGetItemResult(player, KITEM_SNEAKER);
				else  // Default to sad if nothing's enabled...
					K_KartGetItemResult(player, KITEM_SAD);
				player->itemblinkmode = KITEMBLINKMODE_NORMAL;
				if (P_IsDisplayPlayer(player))
					S_StartSound(NULL, sfx_itrolf);
			}
		}
		else if (gametyperules & GTR_BATTLEODDS)
		{
			if (mashed && (bossinfo.boss || cv_banana.value) && !itembreaker) // ANY mashed value? You get a banana.
			{
				K_KartGetItemResult(player, KITEM_BANANA);
				player->itemblinkmode = KITEMBLINKMODE_MASHED;
				if (P_IsDisplayPlayer(player))
					S_StartSound(NULL, sfx_itrolm);
			}
			else if (bossinfo.boss)
			{
				K_KartGetItemResult(player, KITEM_ORBINAUT);
				player->itemblinkmode = KITEMBLINKMODE_NORMAL;
				if (P_IsDisplayPlayer(player))
					S_StartSound(NULL, sfx_itrolf);
			}
			else if (itembreaker)
			{
				K_KartGetItemResult(player, KITEM_SNEAKER);
				player->itemblinkmode = KITEMBLINKMODE_MASHED;
				if (P_IsDisplayPlayer(player))
					S_StartSound(NULL, sfx_itrolm);
			}
		}

		player->itemblink = TICRATE;
		player->itemroulette = KROULETTE_DISABLED;
		player->roulettetype = KROULETTETYPE_NORMAL;
		return;
	}

	// SPECIAL CASE No. 5:
	// Being in ring debt occasionally forces Super Ring on you if you mashed
	if ((K_RingsActive() == true) && mashed && player->rings < 0 && cv_superring.value)
	{
		INT32 debtamount = min(abs(player->ringmin), abs(player->rings));
		if (P_RandomChance((debtamount*FRACUNIT)/abs(player->ringmin)))
		{
			K_KartGetItemResult(player, KITEM_SUPERRING);
			player->itemblink = TICRATE;
			player->itemblinkmode = KITEMBLINKMODE_MASHED;
			player->itemroulette = KROULETTE_DISABLED;
			player->roulettetype = KROULETTETYPE_NORMAL;
			if (P_IsDisplayPlayer(player))
				S_StartSound(NULL, sfx_itrolm);
			return;
		}
	}

	// SPECIAL CASE No. 6:
	// Force SPB onto 2nd if they get too far behind
	// In battle, an SPB is forced onto players to target the "most wanted" player
	if (K_CanForceSPB(player, pdis)
		&& spbplace == -1 && !indirectitemcooldown && !dontforcespb
		&& cv_selfpropelledbomb.value)
	{
		K_KartGetItemResult(player, KITEM_SPB);
		player->itemblink = TICRATE;
		player->itemblinkmode = KITEMBLINKMODE_KARMA;
		player->itemroulette = KROULETTE_DISABLED;
		player->roulettetype = KROULETTETYPE_NORMAL;
		if (P_IsDisplayPlayer(player))
			S_StartSound(NULL, sfx_itrolk);
		return;
	}

	// NOW that we're done with all of those specialized cases, we can move onto the REAL item roulette tables.
	// Initializes existing spawnchance values
	for (i = 0; i < NUMKARTRESULTS; i++)
		spawnchance[i] = 0;

	// Split into another function for a debug function below
	useodds = K_FindUseodds(player, mashed, pdis, bestbumper, spbrush);

	for (i = 1; i < NUMKARTRESULTS; i++)
	{

		spawnchance[i] = (totalspawnchance += K_KartGetItemOdds(
			useodds, i,
			player->distancetofinish,
            player->distancefromcluster,
			mashed,
			spbrush,
			player->bot,
			(player->bot && player->botvars.rival),
			K_IsPlayerLosing(player))
		);
	}

	// Award the player whatever power is rolled
	if (totalspawnchance > 0)
	{
		totalspawnchance = P_RandomKey(totalspawnchance);
		for (i = 0; i < NUMKARTRESULTS && spawnchance[i] <= totalspawnchance; i++);

		K_KartGetItemResult(player, i);
	}
	else
	{
		player->itemtype = KITEM_SAD;
		player->itemamount = 1;
	}

	if (P_IsDisplayPlayer(player))
		S_StartSound(NULL, ((player->roulettetype == KROULETTETYPE_KARMA) ? sfx_itrolk : (mashed ? sfx_itrolm : sfx_itrolf)));

	player->itemblink = TICRATE;
	player->itemblinkmode = ((player->roulettetype == KROULETTETYPE_KARMA) ? KITEMBLINKMODE_KARMA : (mashed ? KITEMBLINKMODE_MASHED :  KITEMBLINKMODE_NORMAL));

	player->itemroulette = KROULETTE_DISABLED; // Since we're done, clear the roulette number
	player->roulettetype = KROULETTETYPE_NORMAL; // This too
}
