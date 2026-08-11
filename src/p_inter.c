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
/// \file  p_inter.c
/// \brief Handling interactions (i.e., collisions)

#include "d_netcmd.h"
#include "doomdef.h"
#include "doomstat.h"
#include "i_system.h"
#include "am_map.h"
#include "g_game.h"
#include "m_random.h"
#include "p_local.h"
#include "p_mobj.h"
#include "s_sound.h"
#include "r_main.h"
#include "st_stuff.h"
#include "hu_stuff.h"
#include "lua_hook.h"
#include "lua_script.h" // lua_compatmode
#include "m_cond.h" // unlockables, emblems, etc
#include "p_setup.h"
#include "m_cheat.h" // objectplace
#include "m_misc.h"
#include "v_video.h" // video flags for CEchos
#include "f_finale.h"

// SRB2kart
#include "k_kart.h"
#include "k_battle.h"
#include "k_pwrlv.h"
#include "k_grandprix.h"
#include "k_boss.h"
#include "p_spec.h"
#include "k_objects.h"
#include "k_items.h"
#include "acs/interface.h"
#include "k_itemlist.hpp"

// CTF player names
#define CTFTEAMCODE(pl) pl->ctfteam ? (pl->ctfteam == 1 ? "\x85" : "\x84") : ""
#define CTFTEAMENDCODE(pl) pl->ctfteam ? "\x80" : ""

//
// GET STUFF
//

//
// P_CanPickupItem
//
// Returns true if the player is in a state where they can pick up items.
//
dboolean P_CanPickupItem(player_t *player, UINT8 weapon)
{
	dboolean force = false;

	if (LUA_HookCanPickupItem(player, weapon, &force))
		return false;

	if (!force)
	{
		if (player->exiting || mapreset || (player->pflags & PF_ELIMINATED))
			return false;

		if ((gametypes[gametype]->rules & GTR_BUMPERS) // No bumpers in Match
		&& !weapon
		&& player->bumper <= 0)
			return false;

		if (weapon)
		{
			// Item slot already taken up
			if (weapon == PICKUPITEM_EGGMINE)
			{
				return (player->itemtype != KITEM_EGGBRICK || player->itemamount == 0);
			}
			else if (weapon == PICKUPITEM_EGGMAN)
			{
				// Invulnerable
				if (player->flashing > 0
					|| K_IsPlayerDamaged(player)
					|| player->squishedtimer > 0
					|| player->invincibilitytimer > 0
					|| player->smonitortimer > 0
					|| player->growshrinktimer > 0
					|| player->hyudorotimer > 0
					|| player->flametimer > 0)
					return false;

				// Already have fake
				if (player->roulettetype == KROULETTETYPE_EGGMAN
					|| player->eggmanexplode)
					return false;
			}
			else
			{
				// Item-specific timer going off
				if (player->stealingtimer || player->stolentimer
					|| player->rocketsneakertimer
					|| player->eggmanexplode
					|| (K_SMonitorSlotHogging(player) && player->itemreserveamount > 0)
					|| (player->growshrinktimer > 0)
					|| player->flametimer)
					return false;

				// Item slot already taken up
				if (player->itemroulette
					|| (weapon != PICKUPITEM_FLOATING && player->itemamount)
					|| (weapon == PICKUPITEM_ITEM && player->itemreservetype == KITEM_BONUSITEMROLL)
					|| (player->itemflags & IF_ITEMOUT))
					return false;
				
				if (weapon == PICKUPITEM_FLOATING && K_GetShieldFromPlayer(player) != KSHIELD_NONE)
					return false; // No stacking shields!
			}
		}
	}

	return true;
}

//
// P_DoNightsScore
//
// When you pick up some items in nights, it displays
// a score sign, and awards you some drill time.
//
static void P_DoNightsScore(player_t *player)
{
	mobj_t *dummymo;

	if (player->exiting)
		return; // Don't do any fancy shit for failures.

	dummymo = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z+player->mo->height/2, MT_NIGHTSCORE);
	if (player->bot)
		player = &players[consoleplayer];

	/*if (G_IsSpecialStage(gamemap)) // Global link count? Maybe not a good idea...
	{
		INT32 i;
		for (i = 0; i < MAXPLAYERS; i++)
			if (playeringame[i])
			{
				if (++players[i].linkcount > players[i].maxlink)
					players[i].maxlink = players[i].linkcount;
				players[i].linktimer = 2*TICRATE;
			}
	}
	else // Individual link counts*/
	{
		if (++player->linkcount > player->maxlink)
			player->maxlink = player->linkcount;
		player->linktimer = 2*TICRATE;
	}

	if (player->linkcount < 10)
	{
		/*if (player->bonustime)
		{
			P_AddPlayerScore(player, player->linkcount*20);
			P_SetMobjState(dummymo, dummymo->info->xdeathstate+player->linkcount-1);
		}
		else*/
		{
			P_AddPlayerScore(player, player->linkcount*10);
			P_SetMobjState(dummymo, dummymo->info->spawnstate+player->linkcount-1);
		}
	}
	else
	{
		/*if (player->bonustime)
		{
			P_AddPlayerScore(player, 200);
			P_SetMobjState(dummymo, dummymo->info->xdeathstate+9);
		}
		else*/
		{
			P_AddPlayerScore(player, 100);
			P_SetMobjState(dummymo, dummymo->info->spawnstate+9);
		}
	}

	// Hoops are the only things that should add to your drill meter
	//player->drillmeter += TICRATE;
	dummymo->momz = FRACUNIT;
	dummymo->fuse = 3*TICRATE;

	// What?! NO, don't use the camera! Scale up instead!
	//P_InstaThrust(dummymo, R_PointToAngle2(dummymo->x, dummymo->y, camera[0].x, camera[0].y), 3*FRACUNIT);
	dummymo->scalespeed = FRACUNIT/25;
	dummymo->destscale = 2*FRACUNIT;
}

/** Takes action based on a ::MF_SPECIAL thing touched by a player.
  * Actually, this just checks a few things (heights, toucher->player, no
  * objectplace, no dead or disappearing things)
  *
  * The special thing may be collected and disappear, or a sound may play, or
  * both.
  *
  * \param special     The special thing.
  * \param toucher     The player's mobj.
  * \param heightcheck Whether or not to make sure the player and the object
  *                    are actually touching.
  */
void P_TouchSpecialThing(mobj_t *special, mobj_t *toucher, dboolean heightcheck)
{
	player_t *player;
	INT32 i;

	if (objectplacing)
		return;

	I_Assert(special != NULL);
	I_Assert(toucher != NULL);

	// Dead thing touching.
	// Can happen with a sliding player corpse.
	if (toucher->health <= 0)
		return;
	if (special->health <= 0)
		return;

	if (heightcheck)
	{
		fixed_t toucher_bottom = toucher->z;
		fixed_t special_bottom = special->z;

		if (toucher->flags & MF_PICKUPFROMBELOW)
			toucher_bottom -= toucher->height;

		if (special->flags & MF_PICKUPFROMBELOW)
			special_bottom -= special->height;

		if (toucher->momz < 0) {
			if (toucher_bottom + toucher->momz > special->z + special->height)
				return;
		} else if (toucher_bottom > special->z + special->height)
			return;
		if (toucher->momz > 0) {
			if (toucher->z + toucher->height + toucher->momz < special_bottom)
				return;
		} else if (toucher->z + toucher->height < special_bottom)
			return;
	}

	player = toucher->player;
	I_Assert(player != NULL); // Only players can touch stuff!

	if (player->spectator)
		return;

	// Ignore multihits in "ouchie" mode
	if (special->flags & (MF_ENEMY|MF_BOSS) && special->flags2 & MF2_FRET)
		return;

	if (LUA_HookTouchSpecial(special, toucher) || P_MobjWasRemoved(special))
		return;

	if ((special->flags & (MF_ENEMY|MF_BOSS)) && !(special->flags & MF_MISSILE))
	{
		////////////////////////////////////////////////////////
		/////ENEMIES & BOSSES!!/////////////////////////////////
		////////////////////////////////////////////////////////

		P_DamageMobj(toucher, special, special, 1, DMG_NORMAL);
		return;
	}
	else if (special->flags & MF_FIRE)
	{
		P_DamageMobj(toucher, special, special, 1, DMG_NORMAL);
		return;
	}
	else
	{
	// We now identify by object type, not sprite! Tails 04-11-2001
	switch (special->type)
	{
		case MT_MEMENTOSTP:	 // Mementos teleport
			// Teleport player to the other teleporter (special->target). We'll assume there's always only ever 2.
			if (!special->target)
				return;	// foolproof crash prevention check!!!!!

			P_SetOrigin(player->mo, special->target->x, special->target->y, special->target->z + (48<<FRACBITS));
			player->mo->angle = special->target->angle;
			P_SetObjectMomZ(player->mo, 12<<FRACBITS, false);
			P_InstaThrust(player->mo, player->mo->angle, 20<<FRACBITS);
			return;
		case MT_FLOATINGITEM: // SRB2Kart
			if (!P_CanPickupItem(player, PICKUPITEM_FLOATING) || (player->itemamount && player->itemtype != special->threshold))
				return;

			if ((gametypes[gametype]->rules & GTR_BUMPERS) && player->bumper <= 0)
				return;

			if (K_SMonitorSlotHogging(player) && (player->itemreservetype == KITEM_NONE || player->itemreservetype == special->threshold))
			{
				K_AddPlayerReserveItem(player, special->threshold, special->movecount);
			}
			else
			{
				player->itemtype = special->threshold;
				if ((UINT16)(player->itemamount) + special->movecount > 255)
					player->itemamount = 255;
				else
					player->itemamount += special->movecount;
	
				// Add the pickup to our roll list
				if (itemlistactive)
				{
					K_AddItemRollToList((INT32)(player - players),
								player->itemtype,
								player->itemamount);
				}
			}

			S_StartSound(special, special->info->deathsound);

			P_SetTarget(&special->tracer, toucher);
			special->flags2 |= MF2_NIGHTSPULL;
			special->destscale = mapobjectscale>>4;
			special->scalespeed <<= 1;

			special->flags &= ~MF_SPECIAL;
			return;
		case MT_RANDOMITEM:
			if (!itembreaker && !P_CanPickupItem(player, PICKUPITEM_ITEM))
				return;

			if ((gametypes[gametype]->rules & GTR_BUMPERS) && player->bumper <= 0)
			{
				if (player->karmamode || player->karmadelay)
					return;
				player->karmamode = KARMAMODE_ITEM;
			}

			special->momx = special->momy = special->momz = 0;
			P_SetTarget(&special->target, toucher);
			P_KillMobj(special, toucher, toucher, DMG_NORMAL);
			break;
		case MT_ITEMCAPSULE:
			if ((gametypes[gametype]->rules & GTR_BUMPERS) && player->bumper <= 0)
				return;

			if (special->scale < special->extravalue1) // don't break it while it's respawning
				return;

			switch (special->threshold)
			{
				case KITEM_SUPERRING:
					if (player->pflags & PF_RINGLOCK) // no cheaty rings
						return;
					break;
				default:
					if (!P_CanPickupItem(player, PICKUPITEM_ITEM))
						return;
					break;
			}

			S_StartSound(toucher, special->info->deathsound);
			P_KillMobj(special, toucher, toucher, DMG_NORMAL);
			return;
		case MT_KARMAHITBOX:
			if (!special->target->player)
				return;
			if (player == special->target->player)
				return;
			if (player->bumper <= 0)
				return;
			if (special->target->player->exiting || player->exiting)
				return;

			if (P_PlayerInPain(special->target->player))
				return;

			if (special->target->player->karmadelay > 0)
				return;

			if (!special->target->player->karmamode)
			{
				mobj_t *boom;

				if (P_DamageMobj(toucher, special, special->target, 1, DMG_KARMA) == false)
				{
					return;
				}

				boom = P_SpawnMobj(special->target->x, special->target->y, special->target->z, MT_BOOMEXPLODE);

				boom->scale = special->target->scale;
				boom->destscale = special->target->scale;
				boom->momz = 5*FRACUNIT;

				if (special->target->color)
					boom->color = special->target->color;
				else
					boom->color = SKINCOLOR_KETCHUP;

				S_StartSound(boom, special->info->attacksound);

				special->target->player->karthud[khud_yougotem] = 2*TICRATE;
				special->target->player->karmadelay = comebacktime;
			}
			else if (special->target->player->karmamode == KARMAMODE_ITEM && P_CanPickupItem(player, PICKUPITEM_ITEM))
			{
				mobj_t *poof = P_SpawnMobj(special->x, special->y, special->z, MT_EXPLODE);
				S_StartSound(poof, special->info->seesound);

				// Karma fireworks
				for (i = 0; i < 5; i++)
				{
					mobj_t *firework = P_SpawnMobj(special->x, special->y, special->z, MT_KARMAFIREWORK);
					firework->momx = (special->target->momx + toucher->momx) / 2;
					firework->momy = (special->target->momy + toucher->momy) / 2;
					firework->momz = (special->target->momz + toucher->momz) / 2;
					P_Thrust(firework, FixedAngle((72*i)<<FRACBITS), P_RandomRange(PR_DECORATION,1,8)*special->scale);
					P_SetObjectMomZ(firework, P_RandomRange(PR_DECORATION,1,8)*special->scale, false);
					firework->color = special->target->color;
				}

				special->target->player->karmamode = KARMAMODE_DEFAULT;
				special->target->player->karmapoints++;
				if (K_RingsActive())
					K_AwardPlayerRings(special->target->player, 10, true);

				if (special->target->player->karmapoints >= 2)
					K_TakeBumpersFromPlayer(special->target->player, player, 1);

				special->target->player->karmadelay = comebacktime;

				K_StartRoulette(player, KROULETTETYPE_KARMA);
			}
			else if (special->target->player->karmamode == KARMAMODE_EGGMAN && P_CanPickupItem(player, PICKUPITEM_EGGMAN))
			{
				mobj_t *poof = P_SpawnMobj(special->x, special->y, special->z, MT_EXPLODE);
				UINT8 ptadd = 1; // No WANTED bonus for tricking

				S_StartSound(poof, special->info->seesound);

				if (player->bumper == 1) // If you have only one bumper left, and see if it's a 1v1
				{
					INT32 numingame = 0;

					for (i = 0; i < MAXPLAYERS; i++)
					{
						if (!playeringame[i] || players[i].spectator || players[i].bumper <= 0)
							continue;
						numingame++;
					}

					if (numingame <= 2) // If so, then an extra karma point so they are 100% certain to switch places; it's annoying to end matches with a fake kill
						ptadd++;
				}

				special->target->player->karmamode = KARMAMODE_DEFAULT;
				special->target->player->karmapoints += ptadd;
				if (K_RingsActive())
					K_AwardPlayerRings(special->target->player, 10, true);

				if (ptadd > 1)
					special->target->player->karthud[khud_yougotem] = 2*TICRATE;

				if (special->target->player->karmapoints >= 2)
					K_TakeBumpersFromPlayer(special->target->player, player, 1);

				special->target->player->karmadelay = comebacktime;

				K_StartRoulette(player, KROULETTETYPE_EGGMAN);

				if (special->target->player->eggmanblame >= 0
					&& special->target->player->eggmanblame < MAXPLAYERS
					&& playeringame[special->target->player->eggmanblame]
					&& !players[special->target->player->eggmanblame].spectator)
					player->eggmanblame = special->target->player->eggmanblame;
				else
					player->eggmanblame = -1;

				special->target->player->eggmanblame = -1;
			}

			return;
		case MT_SPB:
			if ((special->target == toucher || special->target == toucher->target) && (special->threshold > 0))
				return;

			if (special->health <= 0 || toucher->health <= 0)
				return;

			if (player->spectator)
				return;

			if (special->tracer && !P_MobjWasRemoved(special->tracer) && toucher == special->tracer)
			{
				mobj_t *spbexplode;

				if (player->bubbleblowup > 0)
				{
					K_PopPlayerShield(player);
					special->extravalue1 = 2; // WAIT...
					special->extravalue2 = 52; // Slightly over the respawn timer length
					return;
				}

				if (player->invincibilitytimer > 0 ||
					player->smonitortimer > 0 ||
					player->growshrinktimer > 0 ||
					player->hyudorotimer > 0)
				{
					//player->flashing = 0;
					K_DropHnextList(player);
					K_StripItems(player);
				}

				S_StopSound(special); // Don't continue playing the gurgle or the siren

				spbexplode = P_SpawnMobj(toucher->x, toucher->y, toucher->z, MT_SPBEXPLOSION);
				spbexplode->extravalue1 = 1; // Tell K_ExplodePlayer to use extra knockback
				if (special->target && !P_MobjWasRemoved(special->target))
					P_SetTarget(&spbexplode->target, special->target);

				P_RemoveMobj(special);
			}
			else
			{
				P_DamageMobj(player->mo, special, special->target, 1, DMG_NORMAL);
			}
			return;
		/*
		case MT_EERIEFOG:
			special->frame &= ~FF_TRANS80;
			special->frame |= FF_TRANS90;
			return;
		*/
		case MT_SMK_MOLE:
			if (special->target && !P_MobjWasRemoved(special->target))
				return;

			if (special->health <= 0 || toucher->health <= 0)
				return;

			if (!player->mo || player->spectator)
				return;

			// kill
			if (K_PlayerCanPunt(player))
			{
				P_KillMobj(special, toucher, toucher, DMG_NORMAL);
				return;
			}

			// no interaction
			if (player->flashing > 0 || player->hyudorotimer > 0 || P_PlayerInPain(player))
				return;

			// attach to player!
			P_SetTarget(&special->target, toucher);
			S_StartSound(special, sfx_s1a2);
			return;
		case MT_CDUFO: // SRB2kart
			if (special->fuse || !P_CanPickupItem(player, PICKUPITEM_ITEM) || ((gametypes[gametype]->rules & GTR_BUMPERS) && player->bumper <= 0))
				return;

			K_StartRoulette(player, KROULETTETYPE_KARMA);

			// Karma fireworks
			for (i = 0; i < 5; i++)
			{
				mobj_t *firework = P_SpawnMobj(special->x, special->y, special->z, MT_KARMAFIREWORK);
				firework->momx = toucher->momx;
				firework->momy = toucher->momy;
				firework->momz = toucher->momz;
				P_Thrust(firework, FixedAngle((72*i)<<FRACBITS), P_RandomRange(PR_DECORATION, 1,8)*special->scale);
				P_SetObjectMomZ(firework, P_RandomRange(PR_DECORATION, 1,8)*special->scale, false);
				firework->color = toucher->color;
			}

			S_StartSound(toucher, sfx_cdfm73); // they don't make this sound in the original game but it's nice to have a "reward" for good play

			//special->momx = special->momy = special->momz = 0;
			special->momz = -(3*special->scale)/2;
			//P_SetTarget(&special->target, toucher);
			special->fuse = 2*TICRATE;
			break;

		case MT_BALLOON: // SRB2kart
			P_SetObjectMomZ(toucher, 20<<FRACBITS, false);
			break;
		case MT_TUMBLEGEM:
		case MT_TUMBLECOIN:
			{
				SINT8 flip = P_MobjFlip(special);
				if ((toucher->momx || toucher->momy) && (flip * special->momz <= 0))
				{
					special->momx = toucher->momx;
					special->momy = toucher->momy;
					special->momz = flip * max(P_AproxDistance(toucher->momx, toucher->momy) / 4, FixedMul(special->info->speed, special->scale));
					if (flip * toucher->momz > 0)
						special->momz += toucher->momz / 8;
					if ((statenum_t)(special->state-states) != special->info->seestate)
						P_SetMobjState(special, special->info->seestate);
					S_StartSound(special, special->info->activesound);
				}
			}
			return;
		case MT_TRAPBUBBLE:
			if ((special->target == toucher || special->target == toucher->target) && (special->threshold > 0))
				return;

			if (special->tracer && !P_MobjWasRemoved(special->tracer))
				return;

			if (special->health <= 0 || toucher->health <= 0)
				return;

			if (!player->mo || player->spectator)
				return;

			if (special->extravalue3)
				return; // Popped bubbles should not be run into EVER

			fixed_t extrarise = 0;
			extrarise = (fixed_t)(special->script_args[1]);

			fixed_t risespeed = 16 + extrarise;

			// attach to player!
			P_SetTarget(&special->tracer, toucher);
			toucher->flags |= MF_NOGRAVITY;
			toucher->momz = ((risespeed + 4) * max(mapobjectscale, toucher->scale)) * P_MobjFlip(toucher);

			fixed_t toucher_speed = K_Momentum2D(toucher);

			// Force the player to our angle
			// If you setpiece-fail, someone's either being an ass or did something REALLY WRONG
			P_InstaThrust(toucher, special->angle, toucher_speed);

			// Snap to the unfortunate player and quit moving laterally, or we can end up quite far away
			special->momx = 0;
			special->momy = 0;
			special->x = toucher->x;
			special->y = toucher->y;
			special->z = toucher->z;

			S_StartSound(toucher, sfx_s1b2);
			return;
		case MT_RING:
		case MT_FLINGRING:
			if (special->extravalue1)
				return;

			// No picking up rings while your rings are locked.
			if (player->pflags & PF_RINGLOCK)
				return;

			// Don't immediately pick up spilled rings
			if (special->threshold > 0 || P_PlayerInPain(player))
				return;

			// Don't pick up SPB rings when being trailed by SPB.
			if (special->extravalue3 && spbplace != -1 && player->position == spbplace)
				return;

			if (!(P_CanPickupItem(player, PICKUPITEM_RING)))
				return;

			// Reached the cap, don't waste 'em!
			if (RINGTOTAL(player) >= player->ringmax)
				return;

			special->momx = special->momy = special->momz = 0;

			special->extravalue1 = 1; // Ring collect animation timer
			special->angle = R_PointToAngle2(toucher->x, toucher->y, special->x, special->y); // animation angle
			P_SetTarget(&special->target, toucher); // toucher for thinker

			// For MT_FLINGRING - don't delete yourself mid-pickup.
			special->renderflags &= ~RF_DONTDRAW;
			special->fuse = 0;

			player->pickuprings++;

			return;
		case MT_COIN:
		case MT_FLINGCOIN:
			if (!(P_CanPickupItem(player, PICKUPITEM_RING)))
				return;

			special->momx = special->momy = 0;
			P_GivePlayerRings(player, 1);
			break;
		case MT_BLUEBALL:
			if (!(P_CanPickupItem(player, PICKUPITEM_RING)))
				return;

			P_GivePlayerRings(player, 1);

			special->momx = special->momy = special->momz = 0;
			special->destscale = FixedMul(8*FRACUNIT, special->scale);
			if (states[special->info->deathstate].tics > 0)
				special->scalespeed = FixedDiv(FixedDiv(special->destscale, special->scale), states[special->info->deathstate].tics<<FRACBITS);
			else
				special->scalespeed = 4*FRACUNIT/5;
			break;
		// Special Stage Token
		case MT_EMMY:
			if (player->bot)
				return;
			tokenlist += special->health;

			if (ALL7EMERALDS(emeralds)) // Got all 7
			{
				P_GivePlayerRings(player, 50);
				nummaprings += 50; // no cheating towards Perfect!
			}
			else
				token++;

			if (special->tracer) // token BG
				P_RemoveMobj(special->tracer);
			break;

		// Secret emblem thingy
		case MT_EMBLEM:
			{
				if (demo.playback || special->health > MAXEMBLEMS)
					return;

				emblemlocations[special->health-1].collected = true;
				M_UpdateUnlockablesAndExtraEmblems();
				G_SaveGameData();
				break;
			}

		// CTF Flags
		case MT_REDFLAG:
		case MT_BLUEFLAG:
			return;

		case MT_STARPOST:
			P_TouchStarPost(special, player, special->args[1]);
			return;
		case MT_HOOPCOLLIDE:
			// This produces a kind of 'domino effect' with the hoop's pieces.
			for (; special != NULL && special->hprev != NULL; special = special->hprev); // Move to the first sprite in the hoop
			i = 0;
			for (; special != NULL && special->type == MT_HOOP; special = special->hnext)
			{
				if (!P_MobjWasRemoved(special))
				{
					special->fuse = 11;
					special->movedir = i;
					if (!P_MobjWasRemoved(special->target))
					{
						special->extravalue1 = special->target->extravalue1;
						special->extravalue2 = special->target->extravalue2;
						special->target->threshold = 4242;
					}
				}
				i++;
			}
			// Make the collision detectors disappear.
			{
				mobj_t *hnext;
				for (; special != NULL; special = hnext)
				{
					hnext = special->hnext;
					P_RemoveMobj(special);
				}
			}

			P_DoNightsScore(player);

			/*
			// Hoops are the only things that should add to the drill meter
			// Also, one tic's worth of drill is too much.
			if (G_IsSpecialStage(gamemap))
			{
				for (i = 0; i < MAXPLAYERS; i++)
					if (playeringame[i] && players[i].powers[pw_carry] == CR_NIGHTSMODE)
						players[i].drillmeter += TICRATE/2;
			}
			else if (player->bot && player->bot != BOT_MPAI)
				players[consoleplayer].drillmeter += TICRATE/2;
			else
				player->drillmeter += TICRATE/2;*/

			// Play hoop sound -- pick one depending on the current link.
			if (player->linkcount <= 5)
				S_StartSound(toucher, sfx_hoop1);
			else if (player->linkcount <= 10)
				S_StartSound(toucher, sfx_hoop2);
			else
				S_StartSound(toucher, sfx_hoop3);
			return;

		case MT_EGGSHIELD:
			{
				fixed_t touchx, touchy, touchspeed;
				angle_t angle;

				if (P_AproxDistance(toucher->x-special->x, toucher->y-special->y) >
					P_AproxDistance((toucher->x-toucher->momx)-special->x, (toucher->y-toucher->momy)-special->y))
				{
					touchx = toucher->x + toucher->momx;
					touchy = toucher->y + toucher->momy;
				}
				else
				{
					touchx = toucher->x;
					touchy = toucher->y;
				}

				angle = R_PointToAngle2(special->x, special->y, touchx, touchy) - special->angle;
				touchspeed = P_AproxDistance(toucher->momx, toucher->momy);

				// Blocked by the shield?
				if (!(angle > ANGLE_90 && angle < ANGLE_270))
				{
					toucher->momx = P_ReturnThrustX(special, special->angle, touchspeed);
					toucher->momy = P_ReturnThrustY(special, special->angle, touchspeed);
					toucher->momz = -toucher->momz;

					// Play a bounce sound?
					S_StartSound(toucher, special->info->painsound);
					return;
				}
			}
			return;

		case MT_BIGTUMBLEWEED:
		case MT_LITTLETUMBLEWEED:
			if (toucher->momx || toucher->momy)
			{
				special->momx = toucher->momx;
				special->momy = toucher->momy;
				special->momz = P_AproxDistance(toucher->momx, toucher->momy)/4;

				if (toucher->momz > 0)
					special->momz += toucher->momz/8;

				P_SetMobjState(special, special->info->seestate);
			}
			return;

		case MT_WATERDROP:
			if (special->state == &states[special->info->spawnstate])
			{
				special->z = toucher->z+toucher->height-FixedMul(8*FRACUNIT, special->scale);
				special->momz = 0;
				special->flags |= MF_NOGRAVITY;
				P_SetMobjState (special, special->info->deathstate);
				S_StartSound (special, special->info->deathsound+(P_RandomKey(PR_SFX, special->info->mass)));
			}
			return;

		case MT_LOOPENDPOINT:
			Obj_LoopEndpointCollide(special, toucher);
			return;

		case MT_BIGMINE:
		case MT_BIGAIRMINE:
			// Spawn explosion!
			P_SpawnMobj(special->x, special->y, special->z, special->info->mass);
			P_RadiusAttack(special, special, special->info->damage, DMG_NORMAL, true);
			S_StartSound(special, special->info->deathsound);
			P_SetMobjState(special, special->info->deathstate);
			return;

		case MT_NIGHTSBUMPER:
			// Don't trigger if the stage is ended/failed
			if (player->exiting)
				return;

			if (player->bumpertime < TICRATE/4)
			{
				angle_t fa;
				fixed_t xspeed, yspeed;
				const fixed_t speed = FixedMul(FixedDiv(special->info->speed*FRACUNIT,75*FRACUNIT), FixedSqrt(FixedMul(toucher->scale,special->scale)));

				S_StartSound(toucher, special->info->seesound);

				player->bumpertime = TICRATE/2;

				P_UnsetThingPosition(toucher);
				toucher->x = special->x;
				toucher->y = special->y;
				P_SetThingPosition(toucher);
				toucher->z = special->z+(special->height/4);

				if (special->threshold > 0)
					fa = (FixedAngle(((special->threshold*30)-1)*FRACUNIT)>>ANGLETOFINESHIFT) & FINEMASK;
				else
					fa = 0;

				xspeed = FixedMul(FINECOSINE(fa),speed);
				yspeed = FixedMul(FINESINE(fa),speed);

				P_InstaThrust(toucher, special->angle, xspeed/10);
				toucher->momz = yspeed/11;

				toucher->angle = special->angle;

				P_SetPlayerAngle(player, toucher->angle);

				P_ResetPlayer(player);

				P_SetPlayerMobjState(toucher, S_KART_STILL); // SRB2kart - was S_PLAY_FALL1
			}
			return;

		case MT_DASHRING:
		case MT_RAINBOWDASHRING:
			Obj_DashRingTouch(special, player);
			return;

		default: // SOC or script pickup
			P_SetTarget(&special->target, toucher);
			break;
		}
	}

	S_StartSound(toucher, special->info->deathsound); // was NULL, but changed to player so you could hear others pick up rings
	P_KillMobj(special, NULL, toucher, DMG_NORMAL);
	if (!P_MobjWasRemoved(special))
		special->shadowscale = 0;
}

/** Saves a player's level progress at a star post
  *
  * \param post The star post to trigger
  * \param player The player that should receive the checkpoint
  * \param snaptopost If true, the respawn point will use the star post's position, otherwise player x/y and star post z
  */
void P_TouchStarPost(mobj_t *post, player_t *player, dboolean snaptopost)
{
	mobj_t *toucher = player->mo;

	(void)snaptopost;

	// SRB2kart - 150117
	if (K_UsingLegacyCheckpoints() && player->exiting) //STOP MESSING UP MY STATS FASDFASDF
		return;

	// Going backwards triggers sound
	if (K_CheckpointThreshold(false) == numstarposts
	    ? post->health - player->starpostnum > 1
	    : post->health >= player->starpostnum + K_CheckpointThreshold(false))
	{
		if (!player->checkskip && !player->bot)
		{
			S_StartSound(toucher, sfx_s26d);
			if (netgame && cv_antigrief.value)
			{
				player->grieftime += TICRATE;
			}

		}

		player->checkskip = 3;
		return;
	}

	// With the parameter + angle setup, we can go up to 1365 star posts. Who needs that many?
	if (post->health > 1365)
	{
		CONS_Debug(DBG_GAMELOGIC, "Bad Starpost Number!\n");
		return;
	}

	if (player->starpostnum >= post->health)
		return; // Already hit this post

	// Handles Respawning related things on maps wtih starposts
	{
		// Save the player's time and position.
		player->starposttime = player->realtime; //this makes race mode's timers work correctly whilst not affecting sp -x
		//player->starposttime = leveltime;
		player->starpostx = toucher->x;
		player->starposty = toucher->y;
		player->starpostz = post->z;
		player->starpostangle = post->angle;
		player->starpostflip = post->spawnpoint->options & MTF_OBJECTFLIP;	// store flipping
	}

	player->grieftime = 0;
	player->starpostnum = post->health;
}

/** Checks if the level timer is over the timelimit and the round should end,
  * unless you are in overtime. In which case leveltime may stretch out beyond
  * timelimitintics and overtime's status will be checked here each tick.
  *
  * \sa cv_timelimit, P_CheckPointLimit, P_UpdateSpecials
  */
void P_CheckTimeLimit(void)
{
	INT32 i, k;

	if (exitcountdown)
		return;

	if (!timelimitintics)
		return;

	if (leveltime < starttime)
	{
		if (secretextratime)
			secretextratime--;
		return;
	}

	INT32 timeleft = timelimitintics + starttime - leveltime;
	if (timeleft > 0)
	{
		if (secretextratime)
		{
			secretextratime--;
			timelimitintics++;
		}
		else if (extratimeintics)
		{
			timelimitintics++;
			if (leveltime & 1)
				;
			else
			{
				if (extratimeintics > 20)
				{
					extratimeintics -= 20;
					timelimitintics += 20;
				}
				else
				{
					timelimitintics += extratimeintics;
					extratimeintics = 0;
				}
				S_StartSound(NULL, sfx_ptally);
			}
		}
		else
		{
			if (timelimitintics > 60*TICRATE && !itembreaker)
			{
				if (timeleft == 60*TICRATE)
					S_StartSound(NULL, sfx_cdfm67); // guys it's a bell it's just like splatoon guys
				if (timeleft == 59*TICRATE)
					S_StartSound(NULL, sfx_bhurry); // hurry up :))))
			}

			// last five second countdown
			if (timeleft % TICRATE == 0)
			{
				if (timeleft <= 5*TICRATE)
					S_StartSound(NULL, sfx_kc35); // 5, 4
				if (timeleft <= 3*TICRATE)
					S_StartSound(NULL, sfx_kc3d); // 3, 2, 1
			}
		}
		return;
	}

	if (gameaction == ga_completed)
		return;

	if (!itembreaker && !grandprixinfo.gp && cv_overtime.value)
	{
		INT32 playerarray[MAXPLAYERS];
		INT32 tempplayer = 0;
		INT32 spectators = 0;
		INT32 playercount = 0;

		//Figure out if we have enough participating players to care.
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (players[i].exiting)
				return;
			if (playeringame[i] && players[i].spectator)
				spectators++;
		}

#ifdef TESTOVERTIMEINFREEPLAY
		if (timelimitintics > 0 && (gametypes[gametype]->rules & GTR_TIMELIMIT) && startedInFreePlay)
		{
			goto overtimesound;
		}
#endif
		
		if ((D_NumPlayers() - spectators) > 1)
		{
			// Play the starpost sfx after the first second of overtime.
			if (gamestate == GS_LEVEL && (leveltime == (timelimitintics + TICRATE)))
				S_StartSound(NULL, sfx_strpst);

			// Normal Match
			if (!(gametypes[gametype]->rules & GTR_TEAMS))
			{
				//Store the nodes of participating players in an array.
				for (i = 0; i < MAXPLAYERS; i++)
				{
					if (playeringame[i] && !players[i].spectator)
					{
						playerarray[playercount] = i;
						playercount++;
					}
				}

				if (playercount > MAXPLAYERS)
					playercount = MAXPLAYERS;

				//Sort 'em.
				for (i = 1; i < playercount; i++)
				{
					for (k = i; k < playercount; k++)
					{
						if (players[playerarray[i-1]].roundscore < players[playerarray[k]].roundscore)
						{
							tempplayer = playerarray[i-1];
							playerarray[i-1] = playerarray[k];
							playerarray[k] = tempplayer;
						}
					}
				}

				//End the round if the top players aren't tied.
				if (players[playerarray[0]].roundscore == players[playerarray[1]].roundscore)
					goto overtimesound;
			}
			else
			{
				//In team match and CTF, determining a tie is much simpler. =P
				if (redscore == bluescore)
					goto overtimesound;
			}
		}
	}

	P_DoAllPlayersExit(0, false);
	return;

overtimesound:
	if (timeleft == 0)
	{
		S_StartSound(NULL, sfx_s3k50); // overtime warning
		S_StartSound(NULL, sfx_s3k9d);
	}
	if (timeleft == -2*TICRATE)
		S_StartSoundAtVolume(NULL, sfx_s3k50, 128);
	if (timeleft == -4*TICRATE)
		S_StartSoundAtVolume(NULL, sfx_s3k50, 96);
}

/** Checks if a player's score is over the pointlimit and the round should end.
  *
  * \sa cv_pointlimit, P_CheckTimeLimit, P_UpdateSpecials
  */
void P_CheckPointLimit(void)
{
	INT32 i;

	if (exitcountdown)
		return;

	if (!K_CanChangeRules(true))
		return;

	if (!cv_pointlimit.value)
		return;

	if (!(gametypes[gametype]->rules & GTR_POINTLIMIT))
		return;

	if (itembreaker)
		return;

	// pointlimit is nonzero, check if it's been reached by this player
	if (G_GametypeHasTeams())
	{
		// Just check both teams
		if ((UINT32)cv_pointlimit.value <= redscore || (UINT32)cv_pointlimit.value <= bluescore)
		{
			if (server)
				SendNetXCmd(XD_EXITLEVEL, NULL, 0);
		}
	}
	else
	{
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i] || players[i].spectator)
				continue;

			if ((UINT32)cv_pointlimit.value <= players[i].roundscore)
			{
				P_DoAllPlayersExit(0, false);

				/*if (server)
					SendNetXCmd(XD_EXITLEVEL, NULL, 0);*/
				return; // good thing we're leaving the function immediately instead of letting the loop get mangled!
			}
		}
	}
}

// Checks whether or not to end a race netgame.
dboolean P_CheckRacers(void)
{
	const dboolean griefed = (spectateGriefed > 0);

	dboolean eliminateLast = (!K_CanChangeRules(true) || (cv_karteliminatelast.value != 0));
	dboolean allHumansDone = true;
	//dboolean allBotsDone = true;

	UINT8 numPlaying = 0;
	UINT8 numExiting = 0;
	UINT8 numHumans = 0;
	UINT8 numBots = 0;

	UINT8 i;

	// Check if all the players in the race have finished. If so, end the level.
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator || players[i].lives <= 0)
		{
			// Y'all aren't even playing
			continue;
		}

		numPlaying++;

		if (players[i].bot)
		{
			numBots++;
		}
		else
		{
			numHumans++;
		}

		if (players[i].exiting || (players[i].pflags & PF_NOCONTEST))
		{
			numExiting++;
		}
		else
		{
			if (players[i].bot)
			{
				//allBotsDone = false;
			}
			else
			{
				allHumansDone = false;
			}
		}
	}

	if (numPlaying <= 1)
	{
		// Never do this without enough players.
		eliminateLast = false;
	}
	else
	{
		if (griefed == true)
		{
			// Don't do this if someone spectated
			eliminateLast = false;
		}
		else if (grandprixinfo.gp == true)
		{
			// Always do this in GP
			eliminateLast = true;
		}
	}

	if (eliminateLast == true && (numExiting >= numPlaying-1))
	{
		// Everyone's done playing but one guy apparently.
		// Just kill everyone who is still playing.

		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i] || players[i].spectator || players[i].lives <= 0)
			{
				// Y'all aren't even playing
				continue;
			}

			if (players[i].exiting || (players[i].pflags & PF_NOCONTEST))
			{
				// You're done, you're free to go.
				continue;
			}

			P_DoTimeOver(&players[i]);
		}

		// Everyone should be done playing at this point now.
		racecountdown = exitcountdown = 0;
		return true;
	}

	if (numHumans > 0 && allHumansDone == true && !cv_kartbot_finish.value)
	{
		// There might be bots that are still going,
		// but all of the humans are done, so we can exit now.
		racecountdown = exitcountdown = 0;
		return true;
	}

	// SO, we're not done playing.
	// Let's see if it's time to start the death counter!

	if (racecountdown == 0)
	{
		// If the winners are all done, then start the death timer.
		UINT8 winningPos = max(1, numPlaying / 2);

		if (numPlaying % 2) // Any remainder? Then round up.
		{
			winningPos++;
		}

		if (numExiting >= winningPos)
		{
			tic_t countdown = 30*TICRATE; // 30 seconds left to finish, get going!

			if (K_CanChangeRules(true) == true)
			{
				// Custom timer
				countdown = cv_countdowntime.value * TICRATE;
			}

			racecountdown = countdown + 1;
		}
	}

	// We're still playing, but no one else is,
	// so we need to reset spectator griefing.
	if (numPlaying <= 1)
	{
		spectateGriefed = 0;
	}

	// We are still having fun and playing the game :)
	return false;
}

#define DEADFLINGSPEED(mo) (4 * mo->scale)
#define DEADFLINGMINSPD (24 * FRACUNIT)

/** Kills an object.
  *
  * \param target    The victim.
  * \param inflictor The attack weapon. May be NULL (environmental damage).
  * \param source    The attacker. May be NULL.
  * \param damagetype The type of damage dealt that killed the target. If bit 7 (0x80) was set, this was an instant-death.
  * \todo Cleanup, refactor, split up.
  * \sa P_DamageMobj
  */
void P_KillMobj(mobj_t *target, mobj_t *inflictor, mobj_t *source, UINT8 damagetype)
{
	mobj_t *mo;

	if (target->flags & (MF_ENEMY|MF_BOSS))
		target->momx = target->momy = target->momz = 0;

	// SRB2kart
	if (target->type != MT_PLAYER && !(target->flags & MF_MONITOR)
		 && !(target->type == MT_ORBINAUT || target->type == MT_ORBINAUT_SHIELD
		 || target->type == MT_JAWZ || target->type == MT_JAWZ_DUD || target->type == MT_JAWZ_SHIELD
		 || target->type == MT_BANANA || target->type == MT_BANANA_SHIELD
		 || target->type == MT_EGGMANITEM || target->type == MT_EGGMANITEM_SHIELD
		 || target->type == MT_BALLHOG || target->type == MT_SPB)) // kart dead items
		target->flags |= MF_NOGRAVITY; // Don't drop Tails 03-08-2000
	else
		target->flags &= ~MF_NOGRAVITY; // lose it if you for whatever reason have it, I'm looking at you shields
	//

	if (target->flags2 & MF2_NIGHTSPULL)
	{
		P_SetTarget(&target->tracer, NULL);
		target->movefactor = 0; // reset NightsItemChase timer
	}

	// dead target is no more shootable
	target->flags &= ~(MF_SHOOTABLE|MF_FLOAT|MF_SPECIAL);
	target->flags2 &= ~(MF2_SKULLFLY|MF2_NIGHTSPULL);
	target->health = 0; // This makes it easy to check if something's dead elsewhere.

	if (target->type != MT_BATTLEBUMPER)
	{
		target->shadowscale = 0;
	}

	if (LUA_HookMobjDeath(target, inflictor, source, damagetype) || P_MobjWasRemoved(target))
		return;

	P_ActivateThingSpecial(target, source);

	//K_SetHitLagForObjects(target, inflictor, MAXHITLAGTICS, true);

	// SRB2kart
	// I wish I knew a better way to do this
	if (P_IsKartEquippedItem(target->type) && (!P_MobjWasRemoved(target->target)) && target->target->player && (!P_MobjWasRemoved(target->target->player->mo)))
	{
		if ((target->target->player->itemflags & IF_EGGMANOUT) && target->type == MT_EGGMANITEM_SHIELD)
			target->target->player->itemflags &= ~IF_EGGMANOUT;

		if (target->target->player->itemflags & IF_ITEMOUT)
		{
			kartitemequip_e equipstyle = K_GetItemEquipStyle(target->target->player->equippeditem);
			if (equipstyle == KITEMEQUIP_TRAIL)
			{
				if (target->movedir != 0 && target->movedir < (UINT16)target->target->player->itemamount)
				{
					if (target->target->hnext)
						K_KillBananaChain(target->target->hnext, inflictor, source);
					target->target->player->itemamount = 0;
					K_DecrementAndCheckCurrentItem(target->target->player);
				}
				else if (target->target->player->itemamount)
				{
					K_DecrementAndCheckCurrentItem(target->target->player);
				}
			}
			else if (equipstyle == KITEMEQUIP_ORBIT)
			{
				if (target->target->player->itemamount)
				{
					K_DecrementAndCheckCurrentItem(target->target->player);
				}
				if (target->lastlook != 0)
				{
					K_RepairOrbitChain(target);
				}
			}

			if (target->target->hnext == target)
				P_SetTarget(&target->target->hnext, NULL);
		}
	}
	// Above block does not clean up rocket sneakers when a player dies, so we need to do it here target->target is null when using rocket sneakers
	if (target->player)
	{
		K_DropRocketSneaker(target->player);
		target->player->equippeditem = KITEM_NONE;
	}

	// Let EVERYONE know what happened to a player! 01-29-2002 Tails
	if (target->player && !target->player->spectator)
	{
		if (metalrecording) // Ack! Metal Sonic shouldn't die! Cut the tape, end recording!
			G_StopMetalRecording(true);

		target->renderflags &= ~RF_DONTDRAW;
	}

	// if killed by a player
	if (source && source->player)
	{
		if (target->flags & MF_MONITOR || target->type == MT_RANDOMITEM)
		{
			P_SetTarget(&target->target, source);
			//source->player->numboxes++;

			if (itembreaker)
			{
				K_AwardScaledPlayerRings(source->player, ASR_ITEMBOX);

				target->flags2 |= MF2_BOSSFLEE;
				target->flags2 |= MF2_DONTRESPAWN;
				K_SpawnBattlePoints(source->player, NULL, 1);

				// All targets busted!
				if (++numtargets >= nummapboxes)
				{
					P_DoAllPlayersExit(0, (grandprixinfo.gp == true));
				}
				else if (timelimitintics)
				{
					S_StartSound(NULL, sfx_s221);
					extratimeintics += 3*TICRATE;
					secretextratime = TICRATE/2;
				}
			}

			if (cv_itemrespawn.value && modeattacking == ATTACKING_NONE && !itembreaker)
			{
				target->fuse = cv_itemrespawntime.value*TICRATE + 2; // Random box generation
			}
		}
	}

	// if a player avatar dies...
	if (target->player)
	{
		UINT8 i;

		target->flags &= ~(MF_SOLID|MF_SHOOTABLE); // does not block
		P_UnsetThingPosition(target);
		target->flags |= MF_NOBLOCKMAP|MF_NOCLIP|MF_NOCLIPHEIGHT|MF_NOGRAVITY;
		P_SetThingPosition(target);
		target->standingslope = NULL;
		target->terrain = NULL;
		target->pmomz = 0;

		target->player->playerstate = PST_DEAD;

		if (target->player == &players[consoleplayer])
		{
			// don't die in auto map,
			// switch view prior to dying
			if (automapactive)
				AM_Stop();
		}

		//added : 22-02-98: recenter view for next life...
		for (i = 0; i <= r_splitscreen; i++)
		{
			if (target->player == &players[displayplayers[i]])
			{
				localaiming[i] = 0;
				break;
			}
		}

		if (gametypes[gametype]->rules & GTR_BUMPERS)
			K_CheckBumpers();

		K_ResetPogoSpring(target->player);

		ACS_RunPlayerDeathScript(target->player);
	}

	if (source && target && target->player && source->player)
		P_PlayVictorySound(source); // Killer laughs at you. LAUGHS! BWAHAHAHA!

	// Other death animation effects
	switch(target->type)
	{
		case MT_BOUNCEPICKUP:
		case MT_RAILPICKUP:
		case MT_AUTOPICKUP:
		case MT_EXPLODEPICKUP:
		case MT_SCATTERPICKUP:
		case MT_GRENADEPICKUP:
			P_SetObjectMomZ(target, FRACUNIT, false);
			target->fuse = target->info->damage;
			break;

		case MT_YELLOWSHELL:
			P_SpawnMobjFromMobj(target, 0, 0, 0, MT_YELLOWSPRING);
			break;

		case MT_CRAWLACOMMANDER:
			target->momx = target->momy = target->momz = 0;
			break;

		case MT_EGGSHIELD:
			P_SetObjectMomZ(target, 4*target->scale, false);
			P_InstaThrust(target, target->angle, 3*target->scale);
			target->flags = (target->flags|MF_NOCLIPHEIGHT) & ~MF_NOGRAVITY;
			break;

		case MT_EGGMOBILE3:
			{
				thinker_t *th;
				UINT32 i = 0; // to check how many clones we've removed

				// scan the thinkers to make sure all the old pinch dummies are gone on death
				for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
				{
					if (th->function == (actionf_p1)P_RemoveThinkerDelayed)
						continue;

					mo = (mobj_t *)th;
					if (mo->type != (mobjtype_t)target->info->mass)
						continue;
					if (mo->tracer != target)
						continue;

					P_RemoveMobj(mo);

					if (++i == 2) // we've already removed 2 of these, let's stop now
						break;
				}
			}
			break;

		case MT_BIGMINE:
			if (inflictor)
			{
				fixed_t dx = target->x - inflictor->x, dy = target->y - inflictor->y, dz = target->z - inflictor->z;
				fixed_t dm = FixedHypot(dz, FixedHypot(dy, dx));
				target->momx = FixedDiv(FixedDiv(dx, dm), dm)*512;
				target->momy = FixedDiv(FixedDiv(dy, dm), dm)*512;
				target->momz = FixedDiv(FixedDiv(dz, dm), dm)*512;
			}
			if (source)
				P_SetTarget(&target->tracer, source);
			break;

		case MT_BLASTEXECUTOR:
			if (target->spawnpoint)
				P_LinedefExecute(target->spawnpoint->angle, (source ? source : inflictor), target->subsector->sector);
			break;

		case MT_EGGTRAP:
			// Time for birdies! Yaaaaaaaay!
			target->fuse = TICRATE;
			break;

		case MT_PLAYER:
			if (damagetype == DMG_SPECTATOR)
				break;

			const dboolean nocontest = (damagetype == DMG_TIMEOVER);

			const dboolean despawnme = (cv_kartdeathbounces.value == 0) || ((!nocontest) || (nocontest && (target->renderflags & RF_DONTDRAW)));

			fixed_t playerspeed = K_Momentum2D(target);

			// Comically launch the player out the kart and make them pinball around
			dboolean flatout2 = false;

			if (despawnme)
			{
				target->fuse = 3*TICRATE; // timer before mobj disappears from view (even if not an actual player)

				if (target->player && (target->player->deadbounces != 0))
				{
					target->player->deadbounces = 0;
				}
			}
			else if (target->player)
			{
				target->fuse = -1;

				const INT32 numbounces = (cv_kartdeathbounces.value > 0) ? cv_kartdeathbounces.value - 1 : cv_kartdeathbounces.value;
				target->player->deadbounces = numbounces; // FIXME: Customizable bounce lengths...
				flatout2 = true;
			}

			target->momx = target->momy = target->momz = 0;
			P_SetObjectMomZ(target, 14*FRACUNIT, false);
			P_PlayDeathSound(target);

			if (target->player == NULL || skins[target->player->skin]->flags & SF_OLDDEATH)
				break;

			target->player->extendeddeath = false;

			angle_t flingangle = (flatout2) ? target->angle : target->angle + ANGLE_180;
			fixed_t angleoffset = P_RandomRange(PR_DEADFLING, 20, 40)*FRACUNIT;

			if (angleoffset < 30*FRACUNIT)
				angleoffset -= 60*FRACUNIT;

			if (target->player->pflags & PF_NOCONTEST)
			{
				UINT32 bounceflags = (flatout2) ? MF_NOCLIP : 0;
				target->flags &= ~(MF_NOCLIPHEIGHT|bounceflags); // do the bounce!
				target->friction = 4*FRACUNIT/5; // reduce momentum if we die on the ground
				angleoffset /= 2;
			}

			flingangle += FixedAngle(angleoffset);

			// Spawn kart frame
			mobj_t *kart = P_SpawnMobjFromMobj(target, 0, 0, 0, MT_KART_LEFTOVER);

			if (kart && !P_MobjWasRemoved(kart))
			{
				kart->angle = target->angle;
				kart->color = target->color;
				kart->extravalue1 = target->player->kartweight;

				// Copy interp data
				kart->old_angle = target->old_angle;
				kart->old_x = target->old_x;
				kart->old_y = target->old_y;
				kart->old_z = target->old_z;

				if (target->player->pflags & PF_NOCONTEST)
					P_SetTarget(&target->tracer, kart);

				if (damagetype != DMG_TIMEOVER)
				{
					kart->angle = (flatout2) ? flingangle : (flingangle + ANGLE_180);
					P_KillMobj(kart, inflictor, source, damagetype);
				}
				else
					P_SetMobjState(kart, kart->info->deathstate);
			}

			if (!flatout2)
			{
				playerspeed = DEADFLINGSPEED(target);
			}
			else
			{
				target->player->extendeddeath = true;
				playerspeed = max(FixedMul(FixedMul(K_GetKartGameSpeedScalar(gamespeed), DEADFLINGMINSPD), target->scale), playerspeed);
			}

			P_InstaThrust(target, flingangle, playerspeed);
			target->player->rmomx = target->momx; // fix friction
			target->player->rmomy = target->momy;
			break;

		case MT_KART_LEFTOVER:
			if (!P_MobjWasRemoved(inflictor))
				target->angle = R_PointToAngle2(
					target->x, target->y,
					inflictor->x - inflictor->momx, inflictor->y - inflictor->momy
				);

			target->fuse = 2*TICRATE;
			target->flags &= ~(MF_SOLID|MF_SHOOTABLE);
			P_UnsetThingPosition(target);
			target->flags |= MF_NOBLOCKMAP|MF_NOCLIP|MF_NOCLIPHEIGHT|MF_NOGRAVITY;
			P_SetThingPosition(target);
			P_InstaThrust(target, target->angle, target->scale);
			P_SetObjectMomZ(target, 10*FRACUNIT, false);
			break;

		case MT_METALSONIC_RACE:
			target->fuse = TICRATE*3;
			target->momx = target->momy = target->momz = 0;
			P_SetObjectMomZ(target, 14*FRACUNIT, false);
			target->flags = (target->flags & ~MF_NOGRAVITY)|(MF_NOCLIP|MF_NOCLIPTHING);
			break;

		// SRB2Kart:
		case MT_SMK_ICEBLOCK:
			{
				mobj_t *cur = target->hnext;
				while (cur && !P_MobjWasRemoved(cur))
				{
					P_SetMobjState(cur, S_SMK_ICEBLOCK2);
					cur = cur->hnext;
				}
				target->fuse = 10;
				S_StartSound(target, sfx_s3k80);
			}
			break;

		case MT_ITEMCAPSULE:
		{

			// set respawn fuse
			if (modeattacking) // no respawns
				;
			else if (target->threshold == KITEM_SUPERRING)
				target->fuse = 20*TICRATE;
			else
				target->fuse = 40*TICRATE;

			// remove inside item
			if (target->tracer && !P_MobjWasRemoved(target->tracer))
			{
				P_RemoveMobj(target->tracer);
			}

			// give the player an item!
			if (source && source->player)
			{
				player_t *player = source->player;

				// special behavior for ring capsules
				if (target->threshold == KITEM_SUPERRING)
				{
					K_AwardPlayerRings(player, 5 * target->movecount, true);
					break;
				}

				if (K_SMonitorSlotHogging(player) && (player->itemreservetype == KITEM_NONE || player->itemreservetype == target->threshold))
				{
					K_AddPlayerReserveItem(player, target->threshold, max(1, target->movecount));
				}
				else
				{
					K_AwardPlayerItem(player, target->threshold, max(1, target->movecount), true, KITEMBLINK_NORMAL);
				}
			}
			break;
		}

		case MT_BATTLEBUMPER:
			{
				mobj_t *owner = target->target;
				mobj_t *overlay;

				S_StartSound(target, sfx_kc52);
				target->flags &= ~MF_NOGRAVITY;

				target->destscale = (3 * target->destscale) / 2;
				target->scalespeed = FRACUNIT/100;

				if (owner && !P_MobjWasRemoved(owner))
				{
					P_Thrust(target, R_PointToAngle2(owner->x, owner->y, target->x, target->y), 4 * target->scale);
				}

				target->momz += (24 * target->scale) * P_MobjFlip(target);
				target->fuse = 8;

				overlay = P_SpawnMobjFromMobj(target, 0, 0, 0, MT_OVERLAY);

				P_SetTarget(&target->tracer, overlay);
				P_SetTarget(&overlay->target, target);

				overlay->color = target->color;
				P_SetMobjState(overlay, S_INVISIBLE);
			}
			break;

		default:
			break;
	}

	if ((target->type == MT_JAWZ || target->type == MT_JAWZ_DUD || target->type == MT_JAWZ_SHIELD) && !(target->flags2 & MF2_AMBUSH))
	{
		target->z += P_MobjFlip(target)*20*target->scale;
	}

	// kill tracer
	if (target->type == MT_FROGGER)
	{
		if (target->tracer && !P_MobjWasRemoved(target->tracer))
			P_KillMobj(target->tracer, inflictor, source, DMG_NORMAL);
	}

	if (target->type == MT_FROGGER || target->type == MT_ROBRA_HEAD || target->type == MT_BLUEROBRA_HEAD) // clean hnext list
	{
		mobj_t *cur = target->hnext;
		while (cur && !P_MobjWasRemoved(cur))
		{
			P_KillMobj(cur, inflictor, source, DMG_NORMAL);
			cur = cur->hnext;
		}
	}

	// Bounce up on death
	if (target->type == MT_SMK_PIPE || target->type == MT_SMK_MOLE || target->type == MT_SMK_THWOMP)
	{
		target->flags &= (~MF_NOGRAVITY);

		if (target->eflags & MFE_VERTICALFLIP)
			target->z -= target->height;
		else
			target->z += target->height;

		S_StartSound(target, target->info->deathsound);

		P_SetObjectMomZ(target, 8<<FRACBITS, false);
		if (inflictor)
			P_InstaThrust(target, R_PointToAngle2(inflictor->x, inflictor->y, target->x, target->y)+ANGLE_90, 16<<FRACBITS);
	}

	// Final state setting - do something instead of P_SetMobjState;
	if (target->type == MT_SPIKE && target->info->deathstate != S_NULL)
	{
		const angle_t ang = ((inflictor) ? inflictor->angle : 0) + ANGLE_90;
		const fixed_t scale = target->scale;
		const fixed_t xoffs = P_ReturnThrustX(target, ang, 8*scale), yoffs = P_ReturnThrustY(target, ang, 8*scale);
		const UINT16 flip = (target->eflags & MFE_VERTICALFLIP);
		mobj_t *chunk;
		fixed_t momz;

		S_StartSound(target, target->info->deathsound);

		if (target->info->xdeathstate != S_NULL)
		{
			momz = 6*scale;
			if (flip)
				momz *= -1;
#define makechunk(angtweak, xmov, ymov) \
			chunk = P_SpawnMobjFromMobj(target, 0, 0, 0, MT_SPIKE);\
			P_SetMobjState(chunk, target->info->xdeathstate);\
			chunk->health = 0;\
			chunk->angle = angtweak;\
			P_UnsetThingPosition(chunk);\
			chunk->flags = MF_NOCLIP;\
			chunk->x += xmov;\
			chunk->y += ymov;\
			P_SetThingPosition(chunk);\
			P_InstaThrust(chunk,chunk->angle, 4*scale);\
			chunk->momz = momz

			makechunk(ang + ANGLE_180, -xoffs, -yoffs);
			makechunk(ang, xoffs, yoffs);

#undef makechunk
		}

		momz = 7*scale;
		if (flip)
			momz *= -1;

		chunk = P_SpawnMobjFromMobj(target, 0, 0, 0, MT_SPIKE);
		P_SetMobjState(chunk, target->info->deathstate);
		chunk->health = 0;
		chunk->angle = ang + ANGLE_180;
		P_UnsetThingPosition(chunk);
		chunk->flags = MF_NOCLIP;
		chunk->x -= xoffs;
		chunk->y -= yoffs;
		if (flip)
			chunk->z -= 12*scale;
		else
			chunk->z += 12*scale;
		P_SetThingPosition(chunk);
		P_InstaThrust(chunk, chunk->angle, 2*scale);
		chunk->momz = momz;

		P_SetMobjState(target, target->info->deathstate);
		target->health = 0;
		target->angle = ang;
		P_UnsetThingPosition(target);
		target->flags = MF_NOCLIP;
		target->x += xoffs;
		target->y += yoffs;
		target->z = chunk->z;
		P_SetThingPosition(target);
		P_InstaThrust(target, target->angle, 2*scale);
		target->momz = momz;
	}
	else if (target->type == MT_WALLSPIKE && target->info->deathstate != S_NULL)
	{
		const angle_t ang = (/*(inflictor) ? inflictor->angle : */target->angle) + ANGLE_90;
		const fixed_t scale = target->scale;
		const fixed_t xoffs = P_ReturnThrustX(target, ang, 8*scale), yoffs = P_ReturnThrustY(target, ang, 8*scale), forwardxoffs = P_ReturnThrustX(target, target->angle, 7*scale), forwardyoffs = P_ReturnThrustY(target, target->angle, 7*scale);
		const UINT16 flip = (target->eflags & MFE_VERTICALFLIP);
		mobj_t *chunk;
		dboolean sprflip;

		S_StartSound(target, target->info->deathsound);
		if (!P_MobjWasRemoved(target->tracer))
			P_RemoveMobj(target->tracer);

		if (target->info->xdeathstate != S_NULL)
		{
			sprflip = P_RandomChance(PR_DECORATION, FRACUNIT/2);

#define makechunk(angtweak, xmov, ymov) \
			chunk = P_SpawnMobjFromMobj(target, 0, 0, 0, MT_WALLSPIKE);\
			P_SetMobjState(chunk, target->info->xdeathstate);\
			chunk->health = 0;\
			chunk->angle = target->angle;\
			P_UnsetThingPosition(chunk);\
			chunk->flags = MF_NOCLIP;\
			chunk->x += xmov - forwardxoffs;\
			chunk->y += ymov - forwardyoffs;\
			P_SetThingPosition(chunk);\
			P_InstaThrust(chunk, angtweak, 4*scale);\
			chunk->momz = P_RandomRange(PR_DECORATION, 5, 7)*scale;\
			if (flip)\
				chunk->momz *= -1;\
			if (sprflip)\
				chunk->frame |= FF_VERTICALFLIP

			makechunk(ang + ANGLE_180, -xoffs, -yoffs);
			sprflip = !sprflip;
			makechunk(ang, xoffs, yoffs);

#undef makechunk
		}

		sprflip = P_RandomChance(PR_DECORATION, FRACUNIT/2);

		chunk = P_SpawnMobjFromMobj(target, 0, 0, 0, MT_WALLSPIKE);

		P_SetMobjState(chunk, target->info->deathstate);
		chunk->health = 0;
		chunk->angle = target->angle;
		P_UnsetThingPosition(chunk);
		chunk->flags = MF_NOCLIP;
		chunk->x += forwardxoffs - xoffs;
		chunk->y += forwardyoffs - yoffs;
		P_SetThingPosition(chunk);
		P_InstaThrust(chunk, ang + ANGLE_180, 2*scale);
		chunk->momz = P_RandomRange(PR_DECORATION, 5, 7)*scale;
		if (flip)
			chunk->momz *= -1;
		if (sprflip)
			chunk->frame |= FF_VERTICALFLIP;

		P_SetMobjState(target, target->info->deathstate);
		target->health = 0;
		P_UnsetThingPosition(target);
		target->flags = MF_NOCLIP;
		target->x += forwardxoffs + xoffs;
		target->y += forwardyoffs + yoffs;
		P_SetThingPosition(target);
		P_InstaThrust(target, ang, 2*scale);
		target->momz = P_RandomRange(PR_DECORATION, 5, 7)*scale;
		if (flip)
			target->momz *= -1;
		if (!sprflip)
			target->frame |= FF_VERTICALFLIP;
	}
	else if (target->player)
	{
		P_SetPlayerMobjState(target, target->info->deathstate);
	}
	else
#ifdef DEBUG_NULL_DEATHSTATE
		P_SetMobjState(target, S_NULL);
#else
		P_SetMobjState(target, target->info->deathstate);
#endif

	/** \note For player, the above is redundant because of P_SetMobjState (target, S_PLAY_DIE1)
	   in P_DamageMobj()
	   Graue 12-22-2003 */
}

static dboolean P_PlayerHitsPlayer(mobj_t *target, mobj_t *inflictor, mobj_t *source, INT32 damage, UINT8 damagetype)
{
	(void)inflictor;
	(void)damage;

	// SRB2Kart: We want to hurt ourselves, so it's now DMG_CANTHURTSELF
	if (damagetype & DMG_CANTHURTSELF)
	{
		// You can't kill yourself, idiot...
		if (source == target)
			return false;

		if (G_GametypeHasTeams())
		{
			// Don't hurt your team, either!
			if (source->player->ctfteam == target->player->ctfteam)
				return false;
		}
	}

	return true;
}

static dboolean P_KillPlayer(player_t *player, mobj_t *inflictor, mobj_t *source, UINT8 type)
{
	(void)source;
	(void)inflictor;

	if (type == DMG_SPECTATOR && (G_GametypeHasTeams() || G_GametypeHasSpectators()))
	{
		P_SetPlayerSpectator(player-players);
	}

	if (player->exiting)
	{
		player->mo->destscale = 1;
		player->mo->flags |= MF_NOCLIPTHING;
		return false;
	}

	K_DestroyBumpers(player, 1);

	switch (type)
	{
		case DMG_DEATHPIT:
			// Respawn kill types
			//K_DoIngameRespawn(player);
			//return false;
		case DMG_SPECTATOR:
			// disappearifies, but still gotta put items back in play
			break;
		default:
			// Everything else REALLY kills
			break;
	}

	player->carry = CR_NONE;

	K_KartResetPlayerColor(player);

	P_ResetPlayer(player);

	if (player->spectator == false)
	{
		player->mo->renderflags &= ~RF_DONTDRAW;
	}

	P_SetPlayerMobjState(player->mo, player->mo->info->deathstate);

	if (type == DMG_TIMEOVER)
	{
		if (gametypes[gametype]->rules & GTR_CIRCUIT)
		{
			mobj_t *boom = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_FZEROBOOM);
			boom->scale = player->mo->scale;
			boom->angle = player->mo->angle;
			P_SetTarget(&boom->target, player->mo);

			if (skins[player->skin]->flags & SF_OLDDEATH)
			{
				player->mo->flags |= (MF_NOGRAVITY|MF_NOCLIP);
				player->mo->renderflags |= RF_DONTDRAW;
			}
		}

		K_DestroyBumpers(player, player->bumper);
		player->pflags |= PF_ELIMINATED;
	}

	return true;
}

// Determines what ShouldX Hook's status should be used.
static UINT8 P_ShouldHookDamage(mobj_t *target, mobj_t *inflictor, mobj_t *source, INT32 damage, UINT8 damagetype)
{
	dboolean targetisplayer = false;
	const dboolean killer = (damagetype & DMG_DEATHMASK);
	const dboolean specdamage = (damagetype == DMG_SPECTATOR);
	INT32 use_damage = damage;

	if (target && (!P_MobjWasRemoved(target)) && target->player)
	{
		targetisplayer = true;
	}

	if (lua_compatmode && targetisplayer && specdamage)
	{
		// Before the introduction of dedicated damage types, this magic number was used to kill spectator players.
		use_damage = 42000;
	}
	else if (lua_compatmode && targetisplayer && killer)
	{
		// Before the introduction of dedicated damage types, this magic number was used to kill players.
		use_damage = 10000;
	}

	UINT8 shouldDamage = LUA_HookShouldDamage(target, inflictor, source, use_damage, damagetype);
	UINT8 shouldSpin = LUA_HookShouldSpin(target, inflictor, source, use_damage, damagetype);
	UINT8 shouldExplode = LUA_HookShouldExplode(target, inflictor, source, use_damage, damagetype);
	UINT8 shouldSquish = LUA_HookShouldSquish(target, inflictor, source, use_damage, damagetype);
	UINT8 status;
	const UINT8 type = (damagetype & DMG_TYPEMASK);
	status = shouldDamage;

	if (!killer && !specdamage)
	{
		if (shouldSpin > 0 && ((type == DMG_NORMAL) || (type == DMG_WIPEOUT) || (type == DMG_FLIPOVER)))
			status = shouldSpin;
		else if (shouldExplode > 0 && ((type == DMG_EXPLODE) || (type == DMG_KARMA)))
			status = shouldExplode;
		else if (shouldSquish > 0 && (type == DMG_SQUISH))
			status = shouldSquish;
	}

	return status;
}

/** Damages an object, which may or may not be a player.
  * For melee attacks, source and inflictor are the same.
  *
  * \param target     The object being damaged.
  * \param inflictor  The thing that caused the damage: creature, missile,
  *                   gargoyle, and so forth. Can be NULL in the case of
  *                   environmental damage, such as slime or crushing.
  * \param source     The creature or person responsible. For example, if a
  *                   player is hit by a ring, the player who shot it. In some
  *                   cases, the target will go after this object after
  *                   receiving damage. This can be NULL.
  * \param damage     Amount of damage to be dealt.
  * \param damagetype Type of damage to be dealt. If bit 7 (0x80) is set, this is an instant-kill.
  * \return True if the target sustained damage, otherwise false.
  * \todo Clean up this mess, split into multiple functions.
  * \sa P_KillMobj
  */

dboolean P_DamageMobj(mobj_t *target, mobj_t *inflictor, mobj_t *source, INT32 damage, UINT8 damagetype)
{
	player_t *player;
	dboolean force = false;
	dboolean spbpop = false;
	dboolean painsound = false;
	dboolean hithazard = false;

	if (objectplacing)
		return false;

	if (target->health <= 0)
		return false;

	// Spectator handling
	if (damagetype != DMG_SPECTATOR && target->player && target->player->spectator)
		return false;

	if (damagetype == DMG_SPECTATOR && target->player && target->player->spectator)
		damagetype = DMG_INSTAKILL;

	if (!P_MobjWasRemoved(source) && source->player && source->player->spectator)
		return false;

	switch (target->type)
	{
		case MT_SPB:
			spbpop = (damagetype & DMG_TYPEMASK) == DMG_VOLTAGE;
			break;

		default:
			break;
	}

	// Everything above here can't be forced.
	if (!metalrecording)
	{
		UINT8 shouldForce = P_ShouldHookDamage(target, inflictor, source, damage, damagetype);
		if (P_MobjWasRemoved(target))
			return (shouldForce == 1); // mobj was removed
		if (shouldForce == 1)
			force = true;
		else if (shouldForce == 2)
			return false;
	}

	if (!force)
	{
		if (!spbpop)
		{
			if (!(target->flags & MF_SHOOTABLE))
				return false; // shouldn't happen...
		}
	}

	if (target->flags2 & MF2_SKULLFLY)
		target->momx = target->momy = target->momz = 0;

	if (target->flags & (MF_ENEMY|MF_BOSS))
	{
		if (!force && target->flags2 & MF2_FRET) // Currently flashing from being hit
			return false;

		if (LUA_HookMobjDamage(target, inflictor, source, damage, damagetype) || P_MobjWasRemoved(target))
			return true;

		if (target->health > 1)
			target->flags2 |= MF2_FRET;
	}

	player = target->player;

	if (player) // Player is the target
	{
		if (player->pflags & PF_GODMODE)
			return false;

		if (player->exiting && ((damagetype & DMG_DEATHMASK) || itembreaker))
		{
			if (inflictor)
				K_DoInstashield(player);
			return false;
		}

		if (!force)
		{
			// Player hits another player
			if (source && source->player)
			{
				if (!P_PlayerHitsPlayer(target, inflictor, source, damage, damagetype))
					return false;
			}
		}

		// Instant-Death
		if ((damagetype & DMG_DEATHMASK))
		{
			if (!P_KillPlayer(player, inflictor, source, damagetype))
			{
				return false;
			}
			else
			{
				//I_OutputMsg("[%s] respawn/death logged\n", player_names[player - players]);
				
				// DON'T let sinks be a free rank-grief!
				// Don't penalize the player for getting hit off the map, either!
				if (!(inflictor && !P_MobjWasRemoved(inflictor) && inflictor->type == MT_SINK) && !player->ranklog.ijustgothitasshole)
					player->ranklog.numrespawns++; // Log a respawn
			}
		}
		else if (LUA_HookMobjDamage(target, inflictor, source, damage, damagetype))
		{
			return true;
		}
		else
		{
			const UINT8 type = (damagetype & DMG_TYPEMASK);

			const dboolean explosioncombo = type == DMG_EXPLODE // This damage type can do evil stuff like ALWAYS combo
			                               && player->bubbleboost == 0; // ...but popping a Bubble Shield protects you!

			// An Arrow Bullet player ran into a player with a stronger power item. Damage them!
			const dboolean intercept = ((!P_MobjWasRemoved(inflictor)) && inflictor->player && K_InterceptArrowBullet(inflictor->player));

			// Check if the player is allowed to be damaged!
			// If not, then spawn the instashield effect instead.
			if (!force)
			{
				if (gametypes[gametype]->rules & GTR_BUMPERS)
				{
					if ((player->bumper <= 0 && player->karmadelay) || (player->karmamode == KARMAMODE_ITEM))
					{
						// No bumpers & in WAIT, can't be hurt
						K_DoInstashield(player);
						return false;
					}
				}
				else
				{
					if (damagetype & DMG_STEAL)
					{
						// Gametype does not have bumpers, steal damage is intended to not do anything
						// (No instashield is intentional)
						return false;
					}
				}

				if (player->invincibilitytimer > 0 ||
					player->smonitortimer > 0 ||
					player->growshrinktimer > 0 ||
					player->hyudorotimer > 0)
				{
					// Full invulnerability
					K_DoInstashield(player);
					return false;
				}

				// Check if we should allow explosion combos.
				if ((explosioncombo == false) && (player->flashing > 0 || player->squishedtimer > 0))
				{
					// Post-hit invincibility
					K_DoInstashield(player);
					return false;
				}

				// Alt. Shrink: Tank non-explosion hits, as long as you're in Arrow Bullet mode.
				if ((explosioncombo == false) && (intercept == false) && (K_AltShrinkArrowBulletCondition(player)))
				{
					K_DoInstashield(player);
					return false;
				}

				// Bubble Shield protects you from spinout, but not knockback
				if (K_GetShieldFromPlayer(player) == KSHIELD_BUBBLE
				 && explosioncombo == false && source != NULL && type != DMG_VOLTAGE
				 // experiment: disable this since defense can be prepared in advance instead of requiring reaction
				 && cv_kartbubble_defense_canidle.value)
				{
					K_PopPlayerShield(player);
					K_DoInstashield(player);
					return false;
				}
			}

			// We successfully damaged them! Give 'em some bumpers!
			if (source && source != player->mo && source->player)
			{
				if (gametypes[gametype]->rules & GTR_BUMPERS)
				{
					K_BattleAwardHit(source->player, player, inflictor, 1);

					if (damagetype & DMG_STEAL)
					{
						K_TakeBumpersFromPlayer(source->player, player, 1);
					}
					else if (damage & DMG_KARMA && source->player->bumper <= 0)
					{
						source->player->karmapoints++;
						if (source->player->karmapoints >= 2)
						{
							K_TakeBumpersFromPlayer(source->player, player, 1);
						}
						else
						{
							K_DestroyBumpers(player, 1);
						}
					}
					else
					{
						K_DestroyBumpers(player, 1);
					}
				}

				// Update the ranklog
				//I_OutputMsg("[%s] hitconfirm\n", player_names[source->player - players]);
				source->player->ranklog.numhitconfirms++;

				// Some post-hit mercy
				player->ranklog.ijustgothitasshole = 7 * TICRATE;

				K_TryHurtSoundExchange(target, source);
			}
			else
			{
				K_DestroyBumpers(player, 1);
			}

			if ((!inflictor) || (!source))
			{
				// Must be some stray hazard
				hithazard = true;
			}

			if (source && source == player->mo)
			{
				// Self hit; not a hazard
				hithazard = false;
			}

			if (type == DMG_EXPLODE || type == DMG_KARMA || type == DMG_SQUISH ||
				(inflictor && (inflictor->type == MT_ORBINAUT || inflictor->type == MT_ORBINAUT_SHIELD
				|| inflictor->type == MT_JAWZ || inflictor->type == MT_JAWZ_SHIELD || inflictor->type == MT_JAWZ_DUD
				|| inflictor->type == MT_SMK_THWOMP || inflictor->player)))
			{
				player->sneakertimer = player->numsneakers = player->numpanels = 0;
				player->flamestore = player->flamedash = 0;
				player->bubbleboost = 0;
				player->mo->flags2 &= ~MF2_WATERRUN;
			}

			player->driftboost = 0;
			player->recoverydash = 0;
			player->recoverydashcharge = 0;
			player->airdriftspeed = 0;
			player->ringboost = 0;
			player->glanceDir = 0;
			player->attractionattack = 0;
			player->pflags &= ~PF_GAINAX;

			if (gametypes[gametype]->rules & GTR_BUMPERS)
			{
				if (player->bumper <= 0)
				{
					player->karmadelay = comebacktime;
					if ((gametypes[gametype]->rules & GTR_KARMA) && player->karmamode == KARMAMODE_EGGMAN )
					{
						mobj_t *poof = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_EXPLODE);
						S_StartSound(poof, mobjinfo[MT_KARMAHITBOX].seesound);
						player->karmamode = KARMAMODE_DEFAULT;
					}
				}
			}

			INT16 ringburst = 3;

			// Handle ringloss based on hittype.
			if (inflictor)
			{
				switch(inflictor->type)
				{
					case MT_BANANA:
						if (inflictor->health > 1)
						{
							// Sniped!
							ringburst = 10;
						}
						else
					// FALLTHRU
					case MT_BALLHOG:
					case MT_BANANA_SHIELD:
						ringburst = 3;
						break;

					case MT_ORBINAUT:
					case MT_ORBINAUT_SHIELD:
					case MT_JAWZ:
					case MT_JAWZ_DUD:
					case MT_JAWZ_SHIELD:
						ringburst = 5;
						break;

					case MT_PLAYER:
						if (inflictor->player)
						{
							if (inflictor->player->invincibilitytimer)
							{
								// Hit by invincibility
								ringburst = 5;
							}
						}
						break;

					default:
						ringburst = 3;
						break;
				}

			}

			switch (type)
			{
				case DMG_EXPLODE:
				case DMG_KARMA:
					// 10 on SPB hit, 5 on normal explosion
					painsound = true;

					if (source && source->player && type == DMG_KARMA && K_RingsActive())
						K_AwardPlayerRings(source->player, 10, true);

					ringburst = K_ExplodePlayer(player, inflictor, source);
					LUA_HookPlayerExplode(target, inflictor, source, damage, damagetype);
					break;
				case DMG_SQUISH:
					if (inflictor)
					{
						// Squished by player or object
						ringburst = 5;
					}
					painsound = true;
					K_SquishPlayer(player, inflictor, source);
					LUA_HookPlayerSquish(target, inflictor, source, damage, damagetype);
					break;
				case DMG_FLIPOVER:
					if (P_IsDisplayPlayer(player))
						P_StartQuake(32<<FRACBITS, 5);
					K_FlipPlayer(player, inflictor, source);
					LUA_HookPlayerSpin(target, inflictor, source, damage, damagetype);
					K_KartPainEnergyFling(player);
					break;
				case DMG_CRASH:
					if (P_IsDisplayPlayer(player))
						P_StartQuake(32<<FRACBITS, 5);
					K_TumblePlayer(player, inflictor, source);
					LUA_HookPlayerSpin(target, inflictor, source, damage, damagetype);
					K_KartPainEnergyFling(player);
					break;
				case DMG_WIPEOUT:
					if (P_IsDisplayPlayer(player))
						P_StartQuake(32<<FRACBITS, 5);
					K_SpinPlayer(player, inflictor, source, KSPIN_WIPEOUT);
					LUA_HookPlayerSpin(target, inflictor, source, damage, damagetype);
					K_KartPainEnergyFling(player);
					break;
				case DMG_VOLTAGE:
				case DMG_NORMAL:
				default:
					K_SpinPlayer(player, inflictor, source, KSPIN_SPINOUT);
					LUA_HookPlayerSpin(target, inflictor, source, damage, damagetype);
					break;
			}

			// Have a shield? You get hit, but don't lose your rings!
			if (K_GetShieldFromPlayer(player) != KSHIELD_NONE)
			{
				ringburst = 0;
				K_PopPlayerShield(player);
			}

			player->flashing = K_GetKartFlashing(player);

			if (ringburst > 0)
			{
				P_PlayerRingBurst(player, ringburst);
				P_PlayRinglossSound(target);
			}

			if (inflictor)
			{
				if (inflictor->player)
					painsound = true;

				switch(inflictor->type)
				{
					case MT_ORBINAUT:
					case MT_ORBINAUT_SHIELD:
					case MT_JAWZ:
					case MT_JAWZ_SHIELD:
					case MT_JAWZ_DUD:
					case MT_SMK_THWOMP:
					case MT_MINEEXPLOSION:
					case MT_SPBEXPLOSION:
						painsound = true;
						break;

					default:
						break;
				}
			}

			if (painsound)
			{
				K_PlayPainSound(target, source);
			}

			if (hithazard)
			{
				// Log a hazard hit
				//I_OutputMsg("[%s] damaged by hazard\n", player_names[player - players]);
				player->ranklog.hitdamagingsector++;
			}

			if ((explosioncombo == true) || (cv_kartdebughuddrop.value && !modeattacking))
			{
				K_DropItems(player);
			}
			else
			{
				K_DropHnextList(player);
			}

			player->instashield = 15;

			return true;
		}
	}
	else
	{
		if (damagetype & DMG_STEAL)
		{
			// Not a player, steal damage is intended to not do anything
			return false;
		}
	}

	// do the damage
	if (damagetype & DMG_DEATHMASK)
		target->health = 0;
	else
		target->health -= damage;

	if (source && source->player && target)
		G_GhostAddHit((INT32) (source->player - players), target);

	if (target->health <= 0)
	{
		P_KillMobj(target, inflictor, source, damagetype);
		return true;
	}

	if (player)
		P_ResetPlayer(target->player);
	else
		P_SetMobjState(target, target->info->painstate);

	if (!P_MobjWasRemoved(target))
	{
		// if not intent on another player,
		// chase after this one
		P_SetTarget(&target->target, source);
	}

	return true;
}

#define RING_LAYER_SIDE_SIZE (3)
#define RING_LAYER_SIZE (RING_LAYER_SIDE_SIZE * 2)

static void P_FlingBurst
(		player_t *player,
		angle_t fa,
		mobjtype_t objType,
		tic_t objFuse,
		fixed_t objScale,
		INT32 i)
{
	mobj_t *mo = P_SpawnMobjFromMobj(player->mo, 0, 0, 0, objType);
	P_SetTarget(&mo->target, player->mo);

	mo->threshold = 10; // not useful for spikes
	mo->fuse = objFuse;

	if (objScale != FRACUNIT)
	{
		P_SetScale(mo, FixedMul(objScale, mo->scale));
		mo->destscale = mo->scale;
	}

	if (i & 1)
	{
		fa += ANGLE_180;
	}

	// Pitch offset changes every other ring
	angle_t offset = ANGLE_90 / (RING_LAYER_SIDE_SIZE + 2);
	angle_t fp = offset + (((i / 2) % RING_LAYER_SIDE_SIZE) * (offset * 3 >> 1));

	const UINT8 layer = i / RING_LAYER_SIZE;
	const fixed_t thrust = (13 * mo->scale) + (7 * mo->scale * layer);
	mo->momx = (player->mo->momx / 2) + FixedMul(FixedMul(thrust, FINECOSINE(fp >> ANGLETOFINESHIFT)), FINECOSINE(fa >> ANGLETOFINESHIFT));
	mo->momy = (player->mo->momy / 2) + FixedMul(FixedMul(thrust, FINECOSINE(fp >> ANGLETOFINESHIFT)), FINESINE(fa >> ANGLETOFINESHIFT));
	mo->momz = (player->mo->momz / 2) + (FixedMul(thrust, FINESINE(fp >> ANGLETOFINESHIFT)) * P_MobjFlip(mo));
}

/** Spills an injured player's rings.
  *
  * \param player    The player who is losing rings.
  * \param num_rings Number of rings lost. A maximum of 20 rings will be
  *                  spawned.
  * \sa P_PlayerFlagBurst
  */
void P_PlayerRingBurst(player_t *player, INT32 num_rings)
{
	INT32 spill_total, num_fling_rings;
	INT32 i;
	angle_t fa;

	// Rings need to be enabled!
	if ((K_RingsActive() == false) )
		return;

	// Better safe than sorry.
	if (!player)
		return;

	// Have a shield? You get hit, but don't lose your rings!
	if (K_GetShieldFromPlayer(player) != KSHIELD_NONE)
		return;

	// 20 is the maximum number of rings that can be taken from you at once - half the span of your counter
	if (num_rings > 20)
		num_rings = 20;
	else if (num_rings <= 0)
		return;

	spill_total = -P_GivePlayerRings(player, -num_rings);
	num_fling_rings = spill_total + min(0, player->rings);

	// determine first angle
	fa = player->mo->angle + ((P_RandomByte(PR_ITEM_RINGS) & 1) ? -ANGLE_90 : ANGLE_90);

	for (i = 0; i < num_fling_rings; i++)
	{
		P_FlingBurst(player, fa, MT_FLINGRING, 60*TICRATE, FRACUNIT, i);
	}
}
