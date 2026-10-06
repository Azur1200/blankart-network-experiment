// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2024 by Sally "TehRealSalt" Cochenour
// Copyright (C) 2024 by Kart Krew
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  k_botitem.cpp
/// \brief Bot item usage logic

#include <algorithm>

#include <tracy/tracy/Tracy.hpp>

#include "d_netcmd.h"
#include "doomdef.h"
#include "d_player.h"
#include "g_game.h"
#include "r_main.h"
#include "p_local.h"
#include "k_bot.h"
#include "lua_hook.h"
#include "byteptr.h"
#include "d_net.h" // nodetoplayer
#include "k_kart.h"
#include "i_system.h"
#include "p_maputl.h"
#include "m_random.h"
#include "r_things.h" // numskins
#include "m_easing.h"
#include "k_items.h"
#include "k_waypoint.h"

// Looks for players around the bot, and presses the item button
// if there is one in range.
// Returns true if a player was found & we can press the item button, otherwise false.
static dboolean K_BotUseItemNearPlayer(botdata_t *bd, const player_t *player, fixed_t radius)
{
	ZoneScoped;

	uint8_t i;

	if (bd->itemwasdown)
	{
		return false;
	}

	for (i = 0; i < MAXPLAYERS; i++)
	{
		player_t *target = NULL;
		fixed_t dist = INT32_MAX;

		if (!playeringame[i])
		{
			continue;
		}

		target = &players[i];

		if (target->mo == NULL || P_MobjWasRemoved(target->mo)
			|| player == target || target->spectator
			|| target->flashing)
		{
			continue;
		}

		dist = P_AproxDistance(P_AproxDistance(
			player->mo->x - target->mo->x,
			player->mo->y - target->mo->y),
			(player->mo->z - target->mo->z) / 4
		);

		if (dist <= radius)
		{
			bd->itemdown = true;
			return true;
		}
	}

	return false;
}

/*--------------------------------------------------
	static player_t *K_PlayerNearSpot(const player_t *player, fixed_t x, fixed_t y, fixed_t radius)

		Looks for players around a specified x/y coordinate.

	Input Arguments:-
		player - Bot to compare against.
		x - X coordinate to look around.
		y - Y coordinate to look around.
		radius - The radius to look for players in.

	Return:-
		The player we found, NULL if nothing was found.
--------------------------------------------------*/
static player_t *K_PlayerNearSpot(const player_t *player, fixed_t x, fixed_t y, fixed_t radius)
{
	ZoneScoped;

	uint8_t i;

	for (i = 0; i < MAXPLAYERS; i++)
	{
		player_t *target = NULL;
		fixed_t dist = INT32_MAX;

		if (!playeringame[i])
		{
			continue;
		}

		target = &players[i];

		if (target->mo == NULL || P_MobjWasRemoved(target->mo)
			|| player == target || target->spectator
			|| target->flashing)
		{
			continue;
		}

		dist = P_AproxDistance(
			x - target->mo->x,
			y - target->mo->y
		);

		if (dist <= radius)
		{
			return target;
		}
	}

	return NULL;
}

/*--------------------------------------------------
	static player_t *K_PlayerPredictThrow(const player_t *player, uint8_t extra)

		Looks for players around the predicted coordinates of their thrown item.

	Input Arguments:-
		player - Bot to compare against.
		extra - Extra throwing distance, for aim forward on mines.

	Return:-
		The player we're trying to throw at, NULL if none was found.
--------------------------------------------------*/
static player_t *K_PlayerPredictThrow(const player_t *player, uint8_t extra)
{
	ZoneScoped;

	const fixed_t dist = (30 + (extra * 10)) * player->mo->scale;
	const uint32_t airtime = FixedDiv(dist + player->mo->momz, gravity);
	const fixed_t throwspeed = FixedMul(82 * mapobjectscale, K_GetKartGameSpeedScalar(gamespeed));
	const fixed_t estx = player->mo->x + P_ReturnThrustX(NULL, player->mo->angle, (throwspeed + player->speed) * airtime);
	const fixed_t esty = player->mo->y + P_ReturnThrustY(NULL, player->mo->angle, (throwspeed + player->speed) * airtime);

	return K_PlayerNearSpot(player, estx, esty, player->mo->radius * 2);
}

/*--------------------------------------------------
	static player_t *K_PlayerInCone(const player_t *player, uint16_t cone, dboolean flip)

		Looks for players in the .

	Input Arguments:-
		player - Bot to compare against.
		radius - How far away the targets can be.
		cone - Size of cone, in degrees as an integer.
		flip - If true, look behind. Otherwise, check in front of the player.

	Return:-
		true if a player was found in the cone, otherwise false.
--------------------------------------------------*/
static player_t *K_PlayerInCone(const player_t *player, fixed_t radius, uint16_t cone, dboolean flip)
{
	ZoneScoped;

	uint8_t i;

	for (i = 0; i < MAXPLAYERS; i++)
	{
		player_t *target = NULL;
		fixed_t dist = INT32_MAX;

		if (!playeringame[i])
		{
			continue;
		}

		target = &players[i];

		if (target->mo == NULL || P_MobjWasRemoved(target->mo)
			|| player == target || target->spectator
			|| target->flashing
			|| !P_CheckSight(player->mo, target->mo))
		{
			continue;
		}

		dist = P_AproxDistance(P_AproxDistance(
			player->mo->x - target->mo->x,
			player->mo->y - target->mo->y),
			(player->mo->z - target->mo->z) / 4
		);

		if (dist <= radius)
		{
			angle_t a = player->mo->angle - R_PointToAngle2(player->mo->x, player->mo->y, target->mo->x, target->mo->y);
			int16_t ad = 0;

			if (a < ANGLE_180)
			{
				ad = AngleFixed(a)>>FRACBITS;
			}
			else
			{
				ad = 360-(AngleFixed(a)>>FRACBITS);
			}

			ad = abs(ad);

			if (flip)
			{
				if (ad >= 180-cone)
				{
					return target;
				}
			}
			else
			{
				if (ad <= cone)
				{
					return target;
				}
			}
		}
	}

	return NULL;
}

/*--------------------------------------------------
	static dboolean K_RivalBotAggression(const player_t *bot, const player_t *target)

		Returns if a bot is a rival & wants to be aggressive to a player.

	Input Arguments:-
		bot - Bot to check.
		target - Who the bot wants to attack.

	Return:-
		false if not the rival. false if the target is another bot. Otherwise, true.
--------------------------------------------------*/
static dboolean K_RivalBotAggression(const player_t *bot, const player_t *target)
{
	if (bot == NULL || target == NULL)
	{
		// Invalid.
		return false;
	}

	if (bot->bot == false)
	{
		// lol
		return false;
	}

	if (bot->botvars.rival == false)
	{
		// Not the rival, we aren't self-aware.
		return false;
	}

	if (target->bot == false)
	{
		// This bot knows that the real threat is the player.
		return true;
	}

	// Calling them your friends is misleading, but you'll at least spare them.
	return false;
}

// Handles updating item confirm values for offense items.
// target - Who the bot wants to attack.
// amount - Amount to increase item confirm time by.
static void K_ItemConfirmForTarget(botdata_t *bd, const player_t *bot, const player_t *target, uint16_t amount)
{
	if (bot == NULL || target == NULL)
	{
		return;
	}

	if (K_RivalBotAggression(bot, target) == true)
	{
		// Double the rate when you're aggressive.
		bd->itemconfirm += amount << 1;
	}
	else
	{
		// Do as normal.
		bd->itemconfirm += amount;
	}
}

/// @brief checks if given bot isn't prevented from using items due to item cooldowns
/// @param bot bot to check cooldowns for
/// @return if bot is able to use item-related actions (using items or mashing the roulette)
static dboolean K_CheckBotItemCooldown(const player_t *bot)
{
	return bot->itemusecooldown <= 0;
}


// Presses the item button & aim buttons for the bot.
// dir - Aiming direction: 1 for forwards, -1 for backwards, 0 for neutral.
// Returns true if we could press, false if not.
static dboolean K_BotGenericPressItem(botdata_t *bd, int8_t dir)
{
	ZoneScoped;

	if (bd->itemwasdown)
	{
		return false;
	}

	bd->itemthrow = dir;
	bd->itemdown = true;
	//bd->itemconfirm = 0;
	return true;
}

// Item usage for generic items that you need to tap.
static void K_BotItemGenericTap(botdata_t *bd)
{
	ZoneScoped;

	if (!bd->itemwasdown)
	{
		bd->itemdown = true;
		//bd->itemconfirm = 0;
	}
}

// Decides if a bot is ready to reveal their trap item or not.
// mine - Set to true to handle Mine-specific behaviors.
static dboolean K_BotRevealsGenericTrap(botdata_t *bd, const player_t *player, dboolean mine)
{
	ZoneScoped;

	const fixed_t coneDist = FixedMul(1280 * mapobjectscale, K_GetKartGameSpeedScalar(gamespeed));

	if (abs(bd->turnamt) >= KART_FULLTURN/2)
	{
		// DON'T reveal on turns, we can place bananas on turns whenever we have multiple to spare,
		// or if you missed your intentioned throw/place on a player.
		return false;
	}

	// Check the predicted throws.
	if (K_PlayerPredictThrow(player, 0) != NULL)
	{
		return true;
	}

	if (mine)
	{
		if (K_PlayerPredictThrow(player, 1) != NULL)
		{
			return true;
		}
	}

	// Check your behind.
	if (K_PlayerInCone(player, coneDist, 15, true) != NULL)
	{
		return true;
	}

	return false;
}

// Item usage for Eggman shields.
// mine - Set to true to handle Mine-specific behaviors.
static void K_BotItemGenericTrapShield(botdata_t *bd, const player_t *player, dboolean mine)
{
	ZoneScoped;

	if (player->itemflags & IF_ITEMOUT)
	{
		return;
	}

	bd->itemconfirm++;

	if (K_BotRevealsGenericTrap(bd, player, mine) || (bd->itemconfirm > 5*TICRATE))
	{
		K_BotGenericPressItem(bd, 0);
	}
}

// Item usage for orbitting shields.
static void K_BotItemGenericOrbitShield(botdata_t *bd, const player_t *player)
{
	ZoneScoped;

	if (player->itemflags & IF_ITEMOUT)
	{
		return;
	}

	K_BotGenericPressItem(bd, 0);
}

// Less skilled bots stack wastefully, low enough bots don't stack at all.
static int8_t K_BotSneakerStackThreshold(int8_t difficulty)
{
	if (difficulty < 5)
		return 0;

	return 10 - ((difficulty-1)*5) / 13 + 1;
}

// Item usage for sneakers.
static void K_BotItemSneaker(botdata_t *bd, const player_t *player)
{
	ZoneScoped;

	if (P_IsObjectOnGround(player->mo) == false)
	{
		// Don't use while mid-air.
		return;
	}

	dboolean stack = K_StackingActive();
	dboolean sneakerstack = (stack && (cv_kartstacking_sneaker_maxgrade.value > 1));
	const int8_t stacktime = K_BotSneakerStackThreshold(player->botvars.difficulty);

	if ((player->offroad && K_ApplyOffroad(player)) // Stuck in offroad, use it NOW
		|| K_GetWaypointIsShortcut(player->nextwaypoint) == true // Going toward a shortcut!
		|| player->speed < K_GetKartSpeed(player, false, true) / 2 // Being slowed down too much
		|| (sneakerstack && player->sneakertimer > 0 && player->sneakertimer < stacktime && (bd->acceldown && !bd->brakedown)) // Stack them sneakers!
		|| (stack && player->speedboost > (FRACUNIT/8) && !player->sneakertimer) // Have another type of boost (drafting)
		|| bd->itemconfirm > 4*TICRATE) // Held onto it for too long
	{
		if (((sneakerstack && player->sneakertimer > 0 && player->sneakertimer < stacktime)
			|| player->sneakertimer == 0
			|| (stack && player->speedboost > (FRACUNIT/8) && !player->sneakertimer))
			&& !bd->itemwasdown)
		{
			bd->itemdown = true;
			//bd->itemconfirm = 2*TICRATE;
		}
	}
	else
	{
		bd->itemconfirm++;
	}
}

// Item usage for rocket sneakers.
static void K_BotItemRocketSneaker(botdata_t *bd, const player_t *player)
{
	ZoneScoped;

	if (P_IsObjectOnGround(player->mo) == false)
	{
		// Don't use while mid-air.
		return;
	}

	if (bd->itemconfirm > TICRATE)
	{
		if (player->sneakertimer > 0 && K_StackingActive())
		{
			K_BotItemSneaker(bd, player);
		}
		else
		{
			if (player->sneakertimer == 0 && !bd->itemwasdown)
			{
				bd->itemdown = true;
				//bd->itemconfirm = 0;
			}
		}
	}
	else
	{
		bd->itemconfirm++;
	}
}

// Item usage for trap item throwing.
static void K_BotItemBanana(botdata_t *bd, const player_t *player)
{
	ZoneScoped;

	const fixed_t coneDist = FixedMul(1280 * mapobjectscale, K_GetKartGameSpeedScalar(gamespeed));
	int8_t throwdir = -1;
	dboolean tryLookback = false;
	player_t *target = NULL;

	bd->itemconfirm++;

	target = K_PlayerInCone(player, coneDist, 15, true);
	if (target != NULL)
	{
		K_ItemConfirmForTarget(bd, player, target, player->botvars.difficulty);
		throwdir = -1;
		tryLookback = true;
	}

	if (abs(bd->turnamt) >= KART_FULLTURN/2)
	{
		bd->itemconfirm += player->botvars.difficulty / 2;
		throwdir = -1;
	}
	else
	{
		target = K_PlayerPredictThrow(player, 0);

		if (target != NULL)
		{
			K_ItemConfirmForTarget(bd, player, target, player->botvars.difficulty * 2);
			throwdir = 1;
		}
	}

	if (tryLookback == true && throwdir == -1)
	{
		bd->dolookback = true;
	}

	if (bd->itemconfirm > 10*TICRATE || player->bananadrag >= TICRATE)
	{
		K_BotGenericPressItem(bd, throwdir);
	}
}

// Item usage for trap item throwing.
static void K_BotItemMine(botdata_t *bd, const player_t *player)
{
	ZoneScoped;

	const fixed_t coneDist = FixedMul(1280 * mapobjectscale, K_GetKartGameSpeedScalar(gamespeed));
	int8_t throwdir = 0;
	dboolean tryLookback = false;
	player_t *target = NULL;

	bd->itemconfirm++;

	target = K_PlayerInCone(player, coneDist, 15, true);
	if (target != NULL)
	{
		K_ItemConfirmForTarget(bd, player, target, player->botvars.difficulty);
		throwdir = -1;
	}

	if (abs(bd->turnamt) >= KART_FULLTURN/2)
	{
		bd->itemconfirm += player->botvars.difficulty / 2;
		throwdir = -1;
		tryLookback = true;
	}
	else
	{
		target = K_PlayerPredictThrow(player, 0);
		if (target != NULL)
		{
			K_ItemConfirmForTarget(bd, player, target, player->botvars.difficulty * 2);
			throwdir = 0;
		}

		target = K_PlayerPredictThrow(player, 1);
		if (target != NULL)
		{
			K_ItemConfirmForTarget(bd, player, target, player->botvars.difficulty * 2);
			throwdir = 1;
		}
	}

	if (tryLookback == true && throwdir == -1)
	{
		bd->dolookback = true;
	}

	if (bd->itemconfirm > 10*TICRATE || player->bananadrag >= TICRATE)
	{
		K_BotGenericPressItem(bd, throwdir);
	}
}

// Item usage for landmine tossing.
static void K_BotItemLandmine(botdata_t *bd, const player_t *player)
{
	ZoneScoped;

	const fixed_t coneDist = FixedMul(1280 * mapobjectscale, K_GetKartGameSpeedScalar(gamespeed));
	player_t *target = NULL;

	bd->itemconfirm++;

	if (abs(bd->turnamt) >= KART_FULLTURN/2)
	{
		bd->itemconfirm += player->botvars.difficulty / 2;
	}

	target = K_PlayerInCone(player, coneDist, 15, true);
	if (target != NULL)
	{
		K_ItemConfirmForTarget(bd, player, target, player->botvars.difficulty);
		bd->dolookback = true;
	}

	if (bd->itemconfirm > 10*TICRATE)
	{
		K_BotGenericPressItem(bd, -1);
	}
}

// Item usage for Eggman item throwing.
static void K_BotItemEggman(botdata_t *bd, const player_t *player)
{
	ZoneScoped;

	const fixed_t coneDist = FixedMul(1280 * mapobjectscale, K_GetKartGameSpeedScalar(gamespeed));
	const uint8_t stealth = K_EggboxStealth(player->mo->x, player->mo->y);
	int8_t throwdir = -1;
	dboolean tryLookback = false;
	player_t *target = NULL;

	bd->itemconfirm++;

	target = K_PlayerPredictThrow(player, 0);
	if (target != NULL)
	{
		K_ItemConfirmForTarget(bd, player, target, player->botvars.difficulty / 2);
		throwdir = 1;
	}

	target = K_PlayerInCone(player, coneDist, 15, true);
	if (target != NULL)
	{
		K_ItemConfirmForTarget(bd, player, target, player->botvars.difficulty);
		throwdir = -1;
		tryLookback = true;
	}

	if (stealth > 1 || player->itemroulette)
	{
		bd->itemconfirm += player->botvars.difficulty * 4;
		throwdir = -1;
	}

	if (tryLookback == true && throwdir == -1)
	{
		bd->dolookback = true;
	}

	if (bd->itemconfirm > 10*TICRATE || player->bananadrag >= TICRATE)
	{
		K_BotGenericPressItem(bd, throwdir);
	}
}

/*--------------------------------------------------
	static dboolean K_BotRevealsEggbox(const player_t *player)

		Decides if a bot is ready to place their Eggman item or not.

	Input Arguments:-
		player - Bot that has the eggbox.

	Return:-
		true if we want the bot to reveal their eggbox, otherwise false.
--------------------------------------------------*/
static dboolean K_BotRevealsEggbox(const player_t *player)
{
	ZoneScoped;

	const fixed_t coneDist = FixedMul(1280 * mapobjectscale, K_GetKartGameSpeedScalar(gamespeed));
	const uint8_t stealth = K_EggboxStealth(player->mo->x, player->mo->y);
	player_t *target = NULL;

	// This is a stealthy spot for an eggbox, lets reveal it!
	if (stealth > 1)
	{
		return true;
	}

	// Check the predicted throws.
	target = K_PlayerPredictThrow(player, 0);
	if (target != NULL)
	{
		return true;
	}

	// Check your behind.
	target = K_PlayerInCone(player, coneDist, 15, true);
	if (target != NULL)
	{
		return true;
	}

	return false;
}

// Item usage for Eggman shields.
static void K_BotItemEggmanShield(botdata_t *bd, const player_t *player)
{
	ZoneScoped;

	if (player->itemflags & IF_EGGMANOUT)
	{
		return;
	}

	bd->itemconfirm++;

	if (K_BotRevealsEggbox(player) == true || (bd->itemconfirm > 20*TICRATE))
	{
		K_BotGenericPressItem(bd, 0);
	}
}

// Item usage for Eggman explosions.
static void K_BotItemEggmanExplosion(botdata_t *bd, const player_t *player)
{
	ZoneScoped;

	if (player->position == 1 && bd->acceldown)
	{
		// Hey, we aren't gonna find anyone up here...
		// why don't we slow down a bit? :)
		bd->brakedown = true;
	}

	K_BotUseItemNearPlayer(bd, player, 128*player->mo->scale);
}

// Item usage for Orbinaut throwing.
static void K_BotItemOrbinaut(botdata_t *bd, const player_t *player)
{
	ZoneScoped;

	const fixed_t topspeed = K_GetKartSpeed(player, false, true);
	fixed_t radius = FixedMul(2560 * mapobjectscale, K_GetKartGameSpeedScalar(gamespeed));
	int8_t throwdir = -1;
	dboolean tryLookback = false;
	uint8_t snipeMul = 2;
	player_t *target = NULL;

	if (player->speed > topspeed)
	{
		radius = FixedMul(radius, FixedDiv(player->speed, topspeed));
		snipeMul = 3; // Confirm faster when you'll throw it with a bunch of extra speed!!
	}

	bd->itemconfirm++;

	target = K_PlayerInCone(player, radius, 15, false);
	if (target != NULL)
	{
		K_ItemConfirmForTarget(bd, player, target, player->botvars.difficulty * snipeMul);
		throwdir = 1;
	}
	else
	{
		target = K_PlayerInCone(player, radius, 15, true);

		if (target != NULL)
		{
			K_ItemConfirmForTarget(bd, player, target, player->botvars.difficulty);
			throwdir = -1;
			tryLookback = true;
		}
	}

	if (tryLookback == true && throwdir == -1)
	{
		bd->dolookback = true;
	}

	if (bd->itemconfirm > 25*TICRATE)
	{
		K_BotGenericPressItem(bd, throwdir);
	}
}

// Item usage for Ballhog throwing.
static void K_BotItemBallhog(botdata_t *bd, const player_t *player)
{
	ZoneScoped;

	const fixed_t topspeed = K_GetKartSpeed(player, false, true);
	fixed_t radius = FixedMul(2560 * mapobjectscale, K_GetKartGameSpeedScalar(gamespeed));
	int8_t throwdir = -1;
	dboolean tryLookback = false;
	uint8_t snipeMul = 2;
	player_t *target = NULL;
	dboolean hold = false;

	if (player->speed > topspeed)
	{
		radius = FixedMul(radius, FixedDiv(player->speed, topspeed));
		snipeMul = 3; // Confirm faster when you'll throw it with a bunch of extra speed!!
	}

	target = K_PlayerInCone(player, radius, 15, false);
	if (target != NULL)
	{
		K_ItemConfirmForTarget(bd, player, target, player->botvars.difficulty * snipeMul);
		throwdir = 1;
	}
	else
	{
		target = K_PlayerInCone(player, radius, 15, true);

		if (target != NULL)
		{
			K_ItemConfirmForTarget(bd, player, target, player->botvars.difficulty);
			throwdir = -1;
			tryLookback = true;
		}
	}

	if (tryLookback == true && throwdir == -1)
	{
		bd->dolookback = true;
	}

	if (target != NULL)
	{
		// Charge up!
		hold = true;
	}
	else
	{
		// If we lose sight of the target, then we'll just
		// let go and it'll do a partial-blast.

		// If we've been waiting for too long though, then
		// we'll go for the full charge :)
		bd->itemconfirm++;
		hold = (bd->itemconfirm > 10*TICRATE);
	}

	if (hold == true)
	{
		bd->itemthrow = throwdir;
		bd->itemdown = true;
	}
}

// Item usage for Jawz throwing.
static void K_BotItemJawz(botdata_t *bd, const player_t *player)
{
	ZoneScoped;

	const fixed_t topspeed = K_GetKartSpeed(player, false, true);
	fixed_t radius = FixedMul(2560 * mapobjectscale, K_GetKartGameSpeedScalar(gamespeed));
	int8_t throwdir = 1;
	dboolean tryLookback = false;
	uint8_t snipeMul = 2;
	int32_t lastTarg = player->lastitemtarget;
	player_t *target = NULL;

	if (player->speed > topspeed)
	{
		radius = FixedMul(radius, FixedDiv(player->speed, topspeed));
		snipeMul = 3; // Confirm faster when you'll throw it with a bunch of extra speed!!
	}

	bd->itemconfirm++;

	target = K_PlayerInCone(player, radius, 15, true);
	if (target != NULL)
	{
		K_ItemConfirmForTarget(bd, player, target, player->botvars.difficulty);
		throwdir = -1;
		tryLookback = true;
	}

	if (lastTarg >= 0 && lastTarg < MAXPLAYERS
		&& playeringame[lastTarg] == true
		&& players[lastTarg].spectator == false
		&& players[lastTarg].mo != NULL
		&& P_MobjWasRemoved(players[lastTarg].mo) == false)
	{
		mobj_t *targMo = players[lastTarg].mo;
		mobj_t *mobj = NULL, *next = NULL;
		dboolean targettedAlready = false;

		target = &players[lastTarg];

		// Make sure no other Jawz are targetting this player.
		for (mobj = kitemcap; mobj; mobj = next)
		{
			next = mobj->itnext;

			if (mobj->type == MT_JAWZ && mobj->target == targMo)
			{
				targettedAlready = true;
				break;
			}
		}

		if (targettedAlready == false)
		{
			K_ItemConfirmForTarget(bd, player, target, player->botvars.difficulty * snipeMul);
			throwdir = 1;
		}
	}

	if (tryLookback == true && throwdir == -1)
	{
		bd->dolookback = true;
	}

	if (bd->itemconfirm > 25*TICRATE)
	{
		K_BotGenericPressItem(bd, throwdir);
	}
}

// Item usage for Lightning Shield.
static void K_BotItemLightning(botdata_t *bd, const player_t *player)
{
	ZoneScoped;

	fixed_t radius = 192 * player->mo->scale;
	radius = Easing_Linear(FRACUNIT * player->botvars.difficulty / MAXBOTDIFFICULTY, 2*radius, radius);

	if (K_BotUseItemNearPlayer(bd, player, radius) == false)
	{
		if (K_GetBlockedShieldItem(player, radius) || bd->itemconfirm > 10*TICRATE)
		{
			K_BotGenericPressItem(bd, 0);
		}
		else
		{
			bd->itemconfirm++;
		}
	}
}

// Item usage for Bubble Shield.
static void K_BotItemBubble(botdata_t *bd, const player_t *player)
{
	ZoneScoped;

	dboolean hold = false;

	// We are holding this thing too long, lets get rid of it.
	if (bd->itemconfirm > 15*TICRATE)
	{
		bd->itemconfirm++;

		if (player->itemflags & IF_HOLDREADY)
		{
			bd->itemdown = true;
		}

		return;
	}

	if (player->bubblecool <= 0)
	{
		fixed_t radius = 192 * player->mo->scale;
		radius = Easing_Linear(FRACUNIT * player->botvars.difficulty / MAXBOTDIFFICULTY, 2*radius, radius);
		hold = K_GetBlockedShieldItem(player, radius);
	}
	else if (player->bubblecool < bubbletime)
	{
		hold = true;
	}

	if (hold && (player->itemflags & IF_HOLDREADY))
	{
		bd->itemdown = true;
	}

	bd->itemconfirm++;
}

// Item usage for Flame Shield.
static void K_BotItemFlame(botdata_t *bd, const player_t *player)
{
	ZoneScoped;

	dboolean forceoverheat = ((player->offroad && K_ApplyOffroad(player)) || (K_GetWaypointIsShortcut(player->nextwaypoint) == true));

	// To explain these first two cases:
	// If I just check like it was before bots would get stuck holding item since bots below first don't tick flametimer
	// This delays this by a tick so they don't press during a window where it wouldn't work
	if (player->flametimer == flametime && !bd->itemdelay)
	{
		bd->itemdown = false;
		bd->itemdelay = 2;
		bd->itemconfirm = UINT8_MAX; // please help
	}
	else if (player->flametimer == flametime && bd->itemconfirm == 0)
	{
		bd->itemdown = true;
	}

	// if we're smart enough, allow overheating the flame shield if we want to go off-road
	else if (player->flametimer >= flametime-5 && !(forceoverheat && player->botvars.difficulty >= 6))
	{
		bd->itemdelay = 5;
	}

	if (player->botvars.difficulty < 6 || player->flametimer <= 2*TICRATE || forceoverheat)
	{
		// We aren't smart enough to use this properly.
		// ...or we are doing the finishing blow.
		bd->itemdown = true;
		bd->itemdelay = 0;
		bd->itemconfirm++;
		return;
	}

	if (!bd->itemdelay)
	{
		if (player->flamestore < FLAMESTOREMAX - TICRATE/4)
		{
			bd->itemdown = true;
		}
		else
		{
			uint8_t difficultyadjust = MAXBOTDIFFICULTY - player->botvars.difficulty;
			bd->itemdelay = (TICRATE/2) + difficultyadjust;
		}

		bd->itemconfirm++;
	}
	else
	{
		bd->itemdelay--;
		bd->itemconfirm = 0;
	}
}

// Item usage for rings.
static void K_BotItemRings(botdata_t *bd, const player_t *player)
{
	ZoneScoped;

	int32_t saferingsval = 16 - K_GetKartRingPower(player, false);

	if (leveltime < starttime)
	{
		// Don't use rings during POSITION!!
		return;
	}

	if (!bd->acceldown)
	{
		// Don't use rings if you're not trying to accelerate.
		return;
	}

	if (!player->pogospring && P_IsObjectOnGround(player->mo) == false)
	{
		// Don't use while mid-air.
		return;
	}

	if (K_ChainingActive())
	{
		if (((player->sneakertimer > 0) && player->sneakertimer <= (sneakertime/2))
			|| ((player->driftboost > 0) && player->driftboost <= 20))
		{
			// Chain your boost with some rings for extra fun.
			saferingsval -= 3;
		}
		else if ((player->startboost > 0) && player->startboost <= 20)
		{
			// Start Boost chains have a different threshold.
			saferingsval -= 2;
		}
	}

	if (player->speed < (K_GetKartSpeed(player, false, true) * 9) / 10 // Being slowed down too much
		|| player->speedboost > (FRACUNIT/5)) // Have another type of boost
	{
		saferingsval -= 5;
	}

	if (player->rings > saferingsval)
	{
		bd->itemdown = true;
	}
}

/*--------------------------------------------------
	static void K_BotCalculateUseodds(player_t *player)

		Item usage for item roulette mashing.

	Input Arguments:-
		player - Bot to do this for.

	Return:-
		Returns the useodds of this bot.
--------------------------------------------------*/
static uint32_t K_BotCalculateUseodds(const player_t *player)
{
	uint32_t pdis = 0;
	uint8_t pingame = 0, bestbumper = 0;
	dboolean spbrush = false;
	uint8_t i;

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator)
			continue;
		pingame++;
		if (players[i].bumper > bestbumper)
			bestbumper = players[i].bumper;
	}

	// Calculate pdis for useodds
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (playeringame[i] && !players[i].spectator
			&& players[i].position == 1)
		{
			// This player is first! Yay!
			pdis = player->distancetofinish - players[i].distancetofinish;
			break;
		}
	}

	if (spbplace != -1 && player->position == spbplace+1)
	{
		// SPB Rush Mode: It's 2nd place's job to catch-up items and make 1st place's job hell
		pdis = (3 * pdis) / 2;
		spbrush = true;
	}

	pdis = K_ScaleItemDistance(pdis, pingame, spbrush);

	if (player->bot && player->botvars.rival)
	{
		// Rival has better odds :)
		pdis = (15 * pdis) / 14;
	}

	return K_FindUseodds(player, 0, pdis, bestbumper, spbrush);
}

// Item usage for item roulette mashing.
static void K_BotItemRouletteMash(botdata_t *bd, const player_t *player)
{
	dboolean mash = false;

	if (bd->itemwasdown)
	{
		return;
	}

	if (player->rings < 0 && K_ItemResultEnabled(K_GetKartResult("superring")))
	{
		// Uh oh, we need a loan!
		// It'll be better in the long run for bots to lose an item set for 5-15 free rings.
		mash = true;
	}

	// Mash based on useodds to approximate bots in the back mashing like players would.
	if (mash == false)
	{
		const fixed_t minuseodds = 4*FRACUNIT;
		const fixed_t maxuseodds = 15*FRACUNIT;
		fixed_t useodds = K_BotCalculateUseodds(player)*FRACUNIT;

		if (useodds > minuseodds)
		{
			fixed_t chance = CLAMP(FixedDiv(useodds - minuseodds, maxuseodds - minuseodds), 0, FRACUNIT);

			chance = Easing_InSine(chance, 0, FRACUNIT);

			if (M_RandomChance(chance))
				mash = true;
		}
	}

	if (mash == true)
	{
		bd->itemdown = true;
	}
}

// Determines if we should use the Regular Item Delay Logic.
static dboolean K_CanHandleItemDelay(botdata_t *bd, const player_t *player)
{
	if (!bd->itemdelay)
	{
		// We don't even have any. Don't bother.
		return false;
	}

	if (player->flametimer > 0)
	{
		// Flame Shield? We handle this there instead.
		return false;

	}

	return true;
}

// See header file for description.
void K_BotItemUsage(botdata_t *bd, const player_t *player)
{
	ZoneScoped;

	if (player->itemflags & IF_USERINGS)
	{
		if (player->rings > 0)
		{
			// Use rings!
			K_BotItemRings(bd, player);
		}
		return;
	}
	
	if (K_CanHandleItemDelay(bd, player))
	{
		bd->itemdelay--;
		bd->itemconfirm = 0;
		return;
	}

	if (!K_CheckBotItemCooldown(player)) return;
	
	if (player->itemroulette)
	{
		// Mashing behaviors
		K_BotItemRouletteMash(bd, player);
		return;
	}

	if (player->stealingtimer != 0)
		return;

	if (player->eggmanexplode)
	{
		K_BotItemEggmanExplosion(bd, player);
	}
	else if ((!K_IsKartItemAlternate(KITEM_EGGMAN)) && (player->itemflags & IF_EGGMANOUT))
	{
		K_BotItemEggman(bd, player);
	}
	else if (player->rocketsneakertimer > 0)
	{
		K_BotItemRocketSneaker(bd, player);
	}
	else if (player->flametimer > 0)
	{
		K_BotItemFlame(bd, player);
	}
	else
	{
		switch (player->itemtype)
		{
		default:
			if (player->itemtype != KITEM_NONE)
			{
				K_BotItemGenericTap(bd);
			}

			//bd->itemconfirm = 0;
			break;
		case KITEM_INVINCIBILITY:
		case KITEM_SPB:
		case KITEM_GROW:
		case KITEM_SHRINK:
		case KITEM_SUPERRING:
			K_BotItemGenericTap(bd);
			break;
		case KITEM_ROCKETSNEAKER:
			if (player->rocketsneakertimer <= 0)
			{
				K_BotItemGenericTap(bd);
			}
			break;
		case KITEM_FLAMESHIELD:
			if (player->flametimer <= 0)
			{
				K_BotItemGenericTap(bd);
			}
			break;
		case KITEM_SNEAKER:
			K_BotItemSneaker(bd, player);
			break;
		case KITEM_BANANA:
			if (!(player->itemflags & IF_ITEMOUT))
			{
				K_BotItemGenericTrapShield(bd, player, false);
			}
			else
			{
				K_BotItemBanana(bd, player);
			}
			break;
		case KITEM_EGGMAN:
			if (K_IsKartItemAlternate(KITEM_EGGMAN))
			{
				if (!(player->itemflags & IF_ITEMOUT))
				{
					K_BotItemGenericOrbitShield(bd, player);
				}
				else if (player->position != 1) // Hold onto orbiting items when in 1st :)
				{
					// about the same usage (?), maybe a dedicated usage function one day
					K_BotItemMine(bd, player);
				}
			}
			else
			{
				K_BotItemEggmanShield(bd, player);
			}
			break;
		case KITEM_ORBINAUT:
			if (!(player->itemflags & IF_ITEMOUT))
			{
				K_BotItemGenericOrbitShield(bd, player);
			}
			else if (player->position != 1) // Hold onto orbiting items when in 1st :)
			{
				K_BotItemOrbinaut(bd, player);
			}
			break;
		case KITEM_JAWZ:
			if (!(player->itemflags & IF_ITEMOUT))
			{
				K_BotItemGenericOrbitShield(bd, player);
			}
			else if (player->position != 1) // Hold onto orbiting items when in 1st :)
			{
				K_BotItemJawz(bd, player);
			}
			break;
		case KITEM_MINE:
			if (!(player->itemflags & IF_ITEMOUT))
			{
				K_BotItemGenericTrapShield(bd, player, true);
			}
			else
			{
				K_BotItemMine(bd, player);
			}
			break;
		case KITEM_LANDMINE:
		case KITEM_HYUDORO: // Function re-use, as they have about the same usage.
			K_BotItemLandmine(bd, player);
			break;
		case KITEM_BALLHOG:
			K_BotItemBallhog(bd, player);
			break;
		case KITEM_THUNDERSHIELD:
			K_BotItemLightning(bd, player);
			break;
		case KITEM_BUBBLESHIELD:
			K_BotItemBubble(bd, player);
			break;
		}
	}
}

// See header file for description.
void K_UpdateBotGameplayVarsItemUsage(player_t *player)
{
	if (player->itemflags & IF_USERINGS)
	{
		return;
	}

	if (player->itemflags & IF_USERINGS)
	{
		;
	}
	else
	{
		if (player->itemroulette)
		{
			// Mashing behaviors
			return;
		}

		if (player->stealingtimer == 0)
		{
			if (player->eggmanexplode)
			{
				;
			}
			else if (player->itemflags & IF_EGGMANOUT)
			{
				;
			}
			else if (player->rocketsneakertimer > 0)
			{
				;
			}
			else if (player->flametimer > 0)
			{
				;
			}
			else
			{
				switch (player->itemtype)
				{
					default:
					{
						break;
					}
				}
			}
		}
	}
}
