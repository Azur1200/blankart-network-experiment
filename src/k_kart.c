// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2025 by NepDisk.
// Copyright (C) 2025 by Kart Krew.
// Copyright (C) 2018 by ZarroTsu.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  k_kart.c
/// \brief SRB2kart general.
///        All of the SRB2kart-unique stuff.

// TODO: Break this up into more files.
// Files dedicated only for "general miscellanea"
// are straight-up bad coding practice.
// It's better to have niche files that are
// too short than one file that's too massive.

#include "k_kart.h"
#include "d_netcmd.h"
#include "d_player.h"
#include "doomstat.h"
#include "info.h"
#include "k_battle.h"
#include "k_boss.h"
#include "k_pwrlv.h"
#include "k_color.h"
#include "doomdef.h"
#include "hu_stuff.h"
#include "g_game.h"
#include "m_fixed.h"
#include "m_random.h"
#include "p_local.h"
#include "p_mobj.h"
#include "p_slopes.h"
#include "p_setup.h"
#include "r_defs.h"
#include "r_draw.h"
#include "r_local.h"
#include "s_sound.h"
#include "st_stuff.h"
#include "typedef.h"
#include "v_video.h"
#include "z_zone.h"
#include "m_misc.h"
#include "m_cond.h"
#include "f_finale.h"
#include "lua_hud.h"	// For Lua hud checks
#include "lua_hook.h"	// For MobjDamage and ShouldDamage
#include "m_cheat.h"	// objectplacing
#include "p_spec.h"
#include "m_easing.h"   // Invincibility gradienting


#include "k_stats.h"
#include "k_waypoint.h"
#include "k_bot.h"
#include "k_hud.h"
#include "k_terrain.h"
#include "k_director.h"
#include "k_collide.h"
#include "k_follower.h"
#include "k_grandprix.h"
#include "k_cluster.hpp"
#include "k_odds.h"

#include "h_timers.h"
#include "blan/b_soc.h"

consvar_t cv_kartstacking_colorflame = CVAR_INIT ("kartstacking_colorflame", "On", CV_SAVE, CV_OnOff, NULL);
consvar_t cv_kartstacking_sneakerstacksound = CVAR_INIT ("kartstacking_sneakerstacksound", "On", CV_SAVE, CV_OnOff, NULL);
consvar_t cv_kartchainingsound = CVAR_INIT ("kartchaining_chainsound", "On", CV_SAVE, CV_OnOff, NULL);
consvar_t cv_kartdriftsounds = CVAR_INIT ("kartdriftsounds", "On", CV_SAVE, CV_OnOff, NULL);
consvar_t cv_kartdriftefx = CVAR_INIT ("kartdriftefx", "On", CV_SAVE, CV_OnOff, NULL);
static CV_PossibleValue_t driftsparkpulse_cons_t[] = {{0, "MIN"}, {FRACUNIT*3, "MAX"}, {0, NULL}};
consvar_t cv_driftsparkpulse = CVAR_INIT ("driftsparkpulse", "1.4", CV_SAVE|CV_FLOAT, driftsparkpulse_cons_t, NULL);
consvar_t cv_saltyhop = CVAR_INIT ("hardcodehop", "Off", CV_SAVE, CV_OnOff, NULL);
consvar_t cv_karthitemdialog = CVAR_INIT ("karthitemdialog", "On", CV_SAVE, CV_OnOff, NULL);

// SOME IMPORTANT VARIABLES DEFINED IN DOOMDEF.H:
// gamespeed is cc (0 for easy, 1 for normal, 2 for hard)
// franticitems is Frantic Mode items, bool
// encoremode is Encore Mode (duh), bool
// comeback is Battle Mode's karma comeback, also bool
// battlewanted is an array of the WANTED player nums, -1 for no player in that slot
// indirectitemcooldown is timer before anyone's allowed another Shrink/SPB
// mapreset is set when enough players fill an empty server

// Cluster point.
// During a legacy race, this is an actual 3D vector.
// During a waypointed race, this is simply a storage point for the "cluster distance";
// the distance to finish with the most active number of players.
boolean clusterplayer[MAXPLAYERS];
vector3_t clusterpoint, clusterdtf;

void K_TimerInit(void)
{
	UINT8 i;
	UINT8 numPlayers = 0;//, numspec = 0;

	starttime = introtime = 0;

	if (!bossinfo.boss)
	{
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i])
			{
				continue;
			}

			if (players[i].spectator == true)
			{
				//numspec++;
				continue;
			}

			numPlayers++;
		}

		introtime = (108) + 5; // 108 for rotation, + 5 for white fade
		starttime = 6*TICRATE + (3*TICRATE/4);

		if (gametyperules & GTR_NOCOUNTDOWN)
		{
			starttime = 0;
		}
	}

	// NOW you can try to setup Item Breaker, if there's not enough players for a match
	K_BattleInit();

	timelimitintics = extratimeintics = secretextratime = 0;
	if ((gametyperules & GTR_TIMELIMIT) && !bossinfo.boss)
	{
		if (!K_CanChangeRules())
		{
			if (grandprixinfo.gp)
			{
				timelimitintics = (20*TICRATE);
			}
			else
			{
				timelimitintics = timelimits[gametype] * (60*TICRATE);
			}
		}
		else
#ifndef TESTOVERTIMEINFREEPLAY
			if (!itembreaker)
#endif
		{
			timelimitintics = cv_timelimit.value * (60*TICRATE);
		}
	}
}

UINT32 K_GetPlayerDontDrawFlag(player_t *player)
{
	UINT32 flag = 0;

	if (player == &players[displayplayers[0]])
		flag = RF_DONTDRAWP1;
	else if (r_splitscreen >= 1 && player == &players[displayplayers[1]])
		flag = RF_DONTDRAWP2;
	else if (r_splitscreen >= 2 && player == &players[displayplayers[2]])
		flag = RF_DONTDRAWP3;
	else if (r_splitscreen >= 3 && player == &players[displayplayers[3]])
		flag = RF_DONTDRAWP4;

	return flag;
}

// Angle reflection used by springs & speed pads
angle_t K_ReflectAngle(angle_t yourangle, angle_t theirangle, fixed_t yourspeed, fixed_t theirspeed)
{
	INT32 angoffset;
	boolean subtract = false;

	angoffset = yourangle - theirangle;

	if ((angle_t)angoffset > ANGLE_180)
	{
		// Flip on wrong side
		angoffset = InvAngle((angle_t)angoffset);
		subtract = !subtract;
	}

	// Fix going directly against the spring's angle sending you the wrong way
	if ((angle_t)angoffset > ANGLE_90)
	{
		angoffset = ANGLE_180 - angoffset;
	}

	// Offset is reduced to cap it (90 / 2 = max of 45 degrees)
	angoffset /= 2;

	// Reduce further based on how slow your speed is compared to the spring's speed
	// (set both to 0 to ignore this)
	if (theirspeed != 0 && yourspeed != 0)
	{
		if (theirspeed > yourspeed)
		{
			angoffset = FixedDiv(angoffset, FixedDiv(theirspeed, yourspeed));
		}
	}

	if (subtract)
		angoffset = (signed)(theirangle) - angoffset;
	else
		angoffset = (signed)(theirangle) + angoffset;

	return (angle_t)angoffset;
}

//{ SRB2kart Net Variables

void K_RegisterKartStuff(void)
{
	CV_RegisterVar(&cv_sneaker);
	CV_RegisterVar(&cv_rocketsneaker);
	CV_RegisterVar(&cv_invincibility);
	CV_RegisterVar(&cv_banana);
	CV_RegisterVar(&cv_eggmanmonitor);
	CV_RegisterVar(&cv_orbinaut);
	CV_RegisterVar(&cv_jawz);
	CV_RegisterVar(&cv_mine);
	CV_RegisterVar(&cv_ballhog);
	CV_RegisterVar(&cv_selfpropelledbomb);
	CV_RegisterVar(&cv_grow);
	CV_RegisterVar(&cv_shrink);
	CV_RegisterVar(&cv_thundershield);
	CV_RegisterVar(&cv_hyudoro);
	CV_RegisterVar(&cv_pogospring);
	CV_RegisterVar(&cv_kitchensink);

	// Nu-ITEMS
	CV_RegisterVar(&cv_superring);
	CV_RegisterVar(&cv_landmine);
	CV_RegisterVar(&cv_bubbleshield);
	CV_RegisterVar(&cv_flameshield);

	CV_RegisterVar(&cv_dualsneaker);
	CV_RegisterVar(&cv_triplesneaker);
	CV_RegisterVar(&cv_triplebanana);
	CV_RegisterVar(&cv_decabanana);
	CV_RegisterVar(&cv_tripleorbinaut);
	CV_RegisterVar(&cv_quadorbinaut);
	CV_RegisterVar(&cv_dualjawz);

	CV_RegisterVar(&cv_kartminimap);
	CV_RegisterVar(&cv_kartcheck);
	CV_RegisterVar(&cv_kartinvinsfx);
	CV_RegisterVar(&cv_kartspeed);
	CV_RegisterVar(&cv_kartbattlespeed);
	CV_RegisterVar(&cv_kartbumpers);
	CV_RegisterVar(&cv_kartfrantic);
	CV_RegisterVar(&cv_kartcomeback);
	CV_RegisterVar(&cv_kartencore);
	CV_RegisterVar(&cv_kartvoterulechanges);
	CV_RegisterVar(&cv_kartgametypepreference);
	CV_RegisterVar(&cv_kartspeedometer);
	CV_RegisterVar(&cv_kartvoices);
	CV_RegisterVar(&cv_karthitemdialog);
	CV_RegisterVar(&cv_kartbot);
	CV_RegisterVar(&cv_kartbot_cap);
	CV_RegisterVar(&cv_kartbot_modifiermax);
	CV_RegisterVar(&cv_kartbot_basetrackcomplexity);
	CV_RegisterVar(&cv_forcebots);
	CV_RegisterVar(&cv_botcontrol);
	CV_RegisterVar(&cv_karteliminatelast);
	CV_RegisterVar(&cv_kartusepwrlv);
	CV_RegisterVar(&cv_votetime);

	CV_RegisterVar(&cv_kartdebugitem);
	CV_RegisterVar(&cv_kartdebugamount);
	CV_RegisterVar(&cv_kartdebugshrink);
	CV_RegisterVar(&cv_kartdebugdistribution);
	CV_RegisterVar(&cv_kartdebughuddrop);
	CV_RegisterVar(&cv_kartdebugwaypoints);
	CV_RegisterVar(&cv_kartdebuglap);
	CV_RegisterVar(&cv_kartdebugbot);
    CV_RegisterVar(&cv_kartdebugcluster);
	CV_RegisterVar(&cv_kartdebugrings);

	CV_RegisterVar(&cv_kartdebugcheckpoint);
	CV_RegisterVar(&cv_kartdebugnodes);
	CV_RegisterVar(&cv_kartdebugcolorize);
	CV_RegisterVar(&cv_kartdebugdirector);

	// HUD cvars
	K_RegisterKartHUDStuff();

	CV_RegisterVar(&cv_stagetitle);

	CV_RegisterVar(&cv_lessflicker);

	CV_RegisterVar(&cv_kartrings);
	CV_RegisterVar(&cv_kartringsmin);
	CV_RegisterVar(&cv_kartringsmax);
	CV_RegisterVar(&cv_kartringsstart);

	// Stacking
	CV_RegisterVar(&cv_kartstacking);
	CV_RegisterVar(&cv_kartstacking_diminishparam);
	CV_RegisterVar(&cv_kartstacking_maxvanillaboost);
	CV_RegisterVar(&cv_kartstacking_speedboostdropoff);
	CV_RegisterVar(&cv_kartstacking_speedboostdropoff_brake);
	CV_RegisterVar(&cv_kartstacking_accelstack);
	CV_RegisterVar(&cv_kartstacking_colorflame);
	CV_RegisterVar(&cv_kartstacking_sneakerstacksound);

	// Vanilla Boosts
	CV_RegisterVar(&cv_kartstacking_sneaker_easyspeedboost);
	CV_RegisterVar(&cv_kartstacking_sneaker_normalspeedboost);
	CV_RegisterVar(&cv_kartstacking_sneaker_hardspeedboost);
	CV_RegisterVar(&cv_kartstacking_sneaker_expertspeedboost);
	CV_RegisterVar(&cv_kartstacking_sneaker_accelboost);
	CV_RegisterVar(&cv_kartstacking_sneaker_maxgrade);
	CV_RegisterVar(&cv_kartstacking_sneaker_stackable);

	CV_RegisterVar(&cv_kartstacking_invincibility_legacyspeedboost);
	CV_RegisterVar(&cv_kartstacking_invincibility_legacyaccelboost);
    CV_RegisterVar(&cv_kartstacking_invincibility_alternatespeedboost);
	CV_RegisterVar(&cv_kartstacking_invincibility_alternateaccelboost);
	CV_RegisterVar(&cv_kartstacking_invincibility_stackable);

	CV_RegisterVar(&cv_kartstacking_grow_speedboost);
	CV_RegisterVar(&cv_kartstacking_grow_accelboost);
	CV_RegisterVar(&cv_kartstacking_grow_stackable);

	CV_RegisterVar(&cv_kartstacking_flame_speedval);
	CV_RegisterVar(&cv_kartstacking_flame_accelboost);
	CV_RegisterVar(&cv_kartstacking_flame_stackable);

	CV_RegisterVar(&cv_kartstacking_start_speedboost);
	CV_RegisterVar(&cv_kartstacking_start_accelboost);
	CV_RegisterVar(&cv_kartstacking_start_stackable);

	CV_RegisterVar(&cv_kartstacking_drift_speedboost);
	CV_RegisterVar(&cv_kartstacking_drift_accelboost);
	CV_RegisterVar(&cv_kartstacking_drift_stackable);

	CV_RegisterVar(&cv_kartstacking_ring_speedboost);
	CV_RegisterVar(&cv_kartstacking_ring_accelboost);
	CV_RegisterVar(&cv_kartstacking_ring_stackable);

	CV_RegisterVar(&cv_kartstacking_slope_decay);
	CV_RegisterVar(&cv_kartstacking_slope_brakemod);
	CV_RegisterVar(&cv_kartstacking_slope_speedboost_max);
	CV_RegisterVar(&cv_kartstacking_slope_speedboost_cap);
	CV_RegisterVar(&cv_kartstacking_slope_accelboost);
	CV_RegisterVar(&cv_kartstacking_slope_stackable);

	CV_RegisterVar(&cv_kartdrafting);
	CV_RegisterVar(&cv_kartdrafting_closedraft);
	CV_RegisterVar(&cv_kartdrafting_closedeadzone);
	CV_RegisterVar(&cv_kartdrafting_basedistance);
	CV_RegisterVar(&cv_kartstacking_drafting_minspeed);
	CV_RegisterVar(&cv_kartstacking_drafting_maxspeed);

	CV_RegisterVar(&cv_kartchaining);
	CV_RegisterVar(&cv_kartchainingoffroad);
	CV_RegisterVar(&cv_kartchainingsound);

	CV_RegisterVar(&cv_kartitembreaker);

	//CV_RegisterVar(&cv_kartwalltransfer);

	CV_RegisterVar(&cv_kartpurpledrift);

	CV_RegisterVar(&cv_kartbumpspark);

	CV_RegisterVar(&cv_kartbumpspring);

	CV_RegisterVar(&cv_kartslipdash);

	CV_RegisterVar(&cv_kartslopeboost);

    CV_RegisterVar(&cv_kartinvintype);
    CV_RegisterVar(&cv_kartinvindist);
    CV_RegisterVar(&cv_kartinvindistmul);

    CV_RegisterVar(&cv_kartinvin_maxtime);
    CV_RegisterVar(&cv_kartinvin_midtime);

	CV_RegisterVar(&cv_kartdriftsounds);
	CV_RegisterVar(&cv_kartdriftefx);
	CV_RegisterVar(&cv_driftsparkpulse);
	CV_RegisterVar(&cv_itemtimers);
	CV_RegisterVar(&cv_saltyhop);

	CV_RegisterVar(&cv_naturalcamera);
}

//}

boolean K_IsPlayerLosing(player_t *player)
{
	INT32 winningpos = 1;
	UINT8 i, pcount = 0;

	if (itembreaker && numgotboxes == 0)
		return true; // Didn't even TRY?

	if (itembreaker || bossinfo.boss)
		return (player->bumper <= 0); // anything short of DNF is COOL

	if (player->position == 1)
		return false;

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator)
			continue;
		if (players[i].position > pcount)
			pcount = players[i].position;
	}

	if (pcount <= 1)
		return false;

	winningpos = pcount/2;
	if (pcount % 2) // any remainder?
		winningpos++;

	return (player->position > winningpos);
}

fixed_t K_GetKartGameSpeedScalar(SINT8 value)
{
	// Easy = 81.25%
	// Normal = 100%
	// Hard = 118.75%
	// Nightmare = 137.5% ?!?!
	return ((13 + (3*value)) << FRACBITS) / 16;
}

//{ SRB2kart p_user.c Stuff

static fixed_t K_PlayerWeight(mobj_t *mobj, mobj_t *against)
{
	fixed_t weight = 5*FRACUNIT;

	if (!mobj->player)
		return weight;

	if (against && !P_MobjWasRemoved(against) && against->player
		&& ((!P_PlayerInPain(against->player) && P_PlayerInPain(mobj->player)) // You're hurt
		|| (against->player->itemtype == KITEM_BUBBLESHIELD && mobj->player->itemtype != KITEM_BUBBLESHIELD))) // They have a Bubble Shield
	{
		weight = 0; // This player does not cause any bump action
	}
	else
	{
		// Applies rubberbanding, to prevent rubberbanding bots
		// from causing super crazy bumps.
		fixed_t spd = K_GetKartSpeed(mobj->player, false, true);

		weight = (mobj->player->kartweight) * FRACUNIT;

		if (mobj->player->itemtype == KITEM_BUBBLESHIELD)
			weight += 9*FRACUNIT;

		if (mobj->player->speed > spd)
			weight += (mobj->player->speed - spd)/8;
	}

	return weight;
}

fixed_t K_GetMobjWeight(mobj_t *mobj, mobj_t *against)
{
	fixed_t weight = 5*FRACUNIT;

	switch (mobj->type)
	{
		case MT_PLAYER:
			if (!mobj->player)
				break;
			weight = K_PlayerWeight(mobj, against);
			break;
		case MT_BUBBLESHIELD:
			if (!mobj->target)
				break;
			weight = K_PlayerWeight(mobj->target, against);
			break;
		case MT_FALLINGROCK:
			if (against->player)
			{
				if (against->player->invincibilitytimer || against->player->growshrinktimer > 0 || (K_GetShieldFromPlayer(against->player) == KSHIELD_BUBBLE))
					weight = 0;
				else
					weight = K_PlayerWeight(against, NULL);
			}
			break;
		case MT_ORBINAUT:
		case MT_ORBINAUT_SHIELD:
			if (against->player)
				weight = K_PlayerWeight(against, NULL);
			break;
		case MT_JAWZ:
		case MT_JAWZ_DUD:
		case MT_JAWZ_SHIELD:
			if (against->player)
				weight = K_PlayerWeight(against, NULL) + (3*FRACUNIT);
			else
				weight += 3*FRACUNIT;
			break;
		default:
			break;
	}

	return weight;
}

// Checks if the bump interaction was a flip-over.
static boolean K_CheckMobjFlippedOver(mobj_t* mobj1, mobj_t* mobj2)
{
    return ((mobj1->player && (mobj1->player->pflags & PF_JUSTFLIPPED)) ||
            (mobj2->player && (mobj2->player->pflags & PF_JUSTFLIPPED)));
}

static boolean K_JustBumpedException(mobj_t *mobj)
{
	switch (mobj->type)
	{
		case MT_WALLSPIKE:
			return true;
		default:
			break;
	}

	if (mobj->flags & MF_PUSHABLE)
		return true;

	return false;
}

boolean K_KartBouncing(mobj_t *mobj1, mobj_t *mobj2, boolean bounce, boolean solid)
{
	mobj_t *fx;
	fixed_t momdifx, momdify;
	fixed_t distx, disty;
	fixed_t dot, force;
	fixed_t mass1, mass2;
	INT32 p1shield, p2shield;

	if (!mobj1 || !mobj2)
		return false;

	// Don't bump when you're being reborn
	if ((mobj1->player && mobj1->player->playerstate != PST_LIVE)
		|| (mobj2->player && mobj2->player->playerstate != PST_LIVE))
		return false;

	if ((mobj1->player && mobj1->player->respawn)
		|| (mobj2->player && mobj2->player->respawn))
		return false;

	{ // Don't bump if you're flashing
		INT32 flash;

		flash = K_GetKartFlashing(mobj1->player);
		if (mobj1->player && mobj1->player->flashing > 0 && mobj1->player->flashing < flash)
		{
			if (mobj1->player->flashing < flash-1)
				mobj1->player->flashing++;
			return false;
		}

		flash = K_GetKartFlashing(mobj2->player);
		if (mobj2->player && mobj2->player->flashing > 0 && mobj2->player->flashing < flash)
		{
			if (mobj2->player->flashing < flash-1)
				mobj2->player->flashing++;
			return false;
		}
	}

	if ((mobj1->player && mobj1->player->squishedtimer > 0)
		|| (mobj2->player && mobj2->player->squishedtimer > 0))
		return false;


	// Don't bump if you've recently bumped
	if (mobj1->player && mobj1->player->justbumped && !K_JustBumpedException(mobj2))
	{
		mobj1->player->justbumped = bumptime;
		return false;
	}

	if (mobj2->player && mobj2->player->justbumped && !K_JustBumpedException(mobj1))
	{
		mobj2->player->justbumped = bumptime;
		return false;
	}

	// Don't bump if you're flipping over
	if (mobj1->player && mobj1->player->flipovertimer)
	{
		mobj1->player->justbumped = bumptime;
		return false;
	}

	if (mobj2->player && mobj2->player->flipovertimer)
	{
		mobj2->player->justbumped = bumptime;
		return false;
	}

	mass1 = K_GetMobjWeight(mobj1, mobj2);

	if (solid == true && mass1 > 0)
		mass2 = mass1;
	else
		mass2 = K_GetMobjWeight(mobj2, mobj1);

	momdifx = mobj1->momx - mobj2->momx;
	momdify = mobj1->momy - mobj2->momy;

	// Adds the OTHER player's momentum times a bunch, for the best chance of getting the correct direction
	distx = (mobj1->x + mobj2->momx*3) - (mobj2->x + mobj1->momx*3);
	disty = (mobj1->y + mobj2->momy*3) - (mobj2->y + mobj1->momy*3);

	if (distx == 0 && disty == 0)
	{
		// if there's no distance between the 2, they're directly on top of each other, don't run this
		return false;
	}

	{ // Normalize distance to the sum of the two objects' radii, since in a perfect world that would be the distance at the point of collision...
		fixed_t dist = P_AproxDistance(distx, disty);
		fixed_t nx, ny;

		dist = dist ? dist : 1;

		nx = FixedDiv(distx, dist);
		ny = FixedDiv(disty, dist);

		distx = FixedMul(mobj1->radius+mobj2->radius, nx);
		disty = FixedMul(mobj1->radius+mobj2->radius, ny);

		if (momdifx == 0 && momdify == 0)
		{
			// If there's no momentum difference, they're moving at exactly the same rate. Pretend they moved into each other.
			momdifx = -nx;
			momdify = -ny;
		}
	}

	// if the speed difference is less than this let's assume they're going proportionately faster from each other
	if (P_AproxDistance(momdifx, momdify) < (25*mapobjectscale))
	{
		fixed_t momdiflength = P_AproxDistance(momdifx, momdify);
		fixed_t normalisedx = FixedDiv(momdifx, momdiflength);
		fixed_t normalisedy = FixedDiv(momdify, momdiflength);
		momdifx = FixedMul((25*mapobjectscale), normalisedx);
		momdify = FixedMul((25*mapobjectscale), normalisedy);
	}

	dot = FixedMul(momdifx, distx) + FixedMul(momdify, disty);

	if (dot >= 0)
	{
		// They're moving away from each other
		return false;
	}

	force = FixedDiv(dot, FixedMul(distx, distx)+FixedMul(disty, disty));

	if (bounce == true && mass2 > 0) // Perform a Goomba Bounce.
		mobj1->momz = -mobj1->momz;
	else
	{
		fixed_t newz = mobj1->momz;
		if (mass2 > 0)
			mobj1->momz = mobj2->momz;
		if (mass1 > 0 && solid == false)
			mobj2->momz = newz;
	}

	if (mass2 > 0)
	{
		mobj1->momx = mobj1->momx - FixedMul(FixedMul(FixedDiv(2*mass2, mass1 + mass2), force), distx);
		mobj1->momy = mobj1->momy - FixedMul(FixedMul(FixedDiv(2*mass2, mass1 + mass2), force), disty);
	}

	if (mass1 > 0 && solid == false)
	{
		mobj2->momx = mobj2->momx - FixedMul(FixedMul(FixedDiv(2*mass1, mass1 + mass2), force), -distx);
		mobj2->momy = mobj2->momy - FixedMul(FixedMul(FixedDiv(2*mass1, mass1 + mass2), force), -disty);
	}

	// Do the bump fx when we've CONFIRMED we can bump.
	if ((mobj1->player && mobj1->player->itemtype == KITEM_BUBBLESHIELD) || (mobj2->player && mobj2->player->itemtype == KITEM_BUBBLESHIELD))
		S_StartSound(mobj1, sfx_s3k44);
	else if (!K_CheckMobjFlippedOver(mobj1, mobj2))
		S_StartSound(mobj1, sfx_s3k49);

	fx = P_SpawnMobj(mobj1->x/2 + mobj2->x/2, mobj1->y/2 + mobj2->y/2, mobj1->z/2 + mobj2->z/2, MT_BUMP);
	if (mobj1->eflags & MFE_VERTICALFLIP)
		fx->eflags |= MFE_VERTICALFLIP;
	else
		fx->eflags &= ~MFE_VERTICALFLIP;
	P_SetScale(fx, mobj1->scale);

	// Because this is done during collision now, rmomx and rmomy need to be recalculated
	// so that friction doesn't immediately decide to stop the player if they're at a standstill
	// Also set justbumped here
	if (mobj1->player)
	{
		mobj1->player->rmomx = mobj1->momx - mobj1->player->cmomx;
		mobj1->player->rmomy = mobj1->momy - mobj1->player->cmomy;
		mobj1->player->justbumped = bumptime;

		p1shield = K_GetShieldFromPlayer(mobj1->player);

		// Moved here so it only fires once on bump.
		if (p1shield == KSHIELD_BUBBLE)
		{
			// Each bump does chip damage to the shield.
			K_RemoveBubbleHealth(mobj1->player, BUBBLEBUMPCHIP);

			if (mobj1->player->bubblehealth > 0)
			{
				// Play a distinct "crack!" noise to clue the player in.
				S_StartSound(mobj1, sfx_s3k6e);
			}
		}
		else if (p1shield == KSHIELD_NONE)
		{
			if (mobj2->player && (mobj1->player->rings > 0))
			{
				P_PlayerRingBurst(mobj1->player, 1);
			}
		}

		if (mobj1->player->spinouttimer)
		{
			mobj1->player->wipeoutslow = wipeoutslowtime+1;
			mobj1->player->spinouttimer = max(wipeoutslowtime+1, mobj1->player->spinouttimer);
			//mobj1->player->spinouttype = KSPIN_WIPEOUT; // Enforce type
		}
	}

	if (mobj2->player)
	{
		mobj2->player->rmomx = mobj2->momx - mobj2->player->cmomx;
		mobj2->player->rmomy = mobj2->momy - mobj2->player->cmomy;
		mobj2->player->justbumped = bumptime;

		p2shield = K_GetShieldFromPlayer(mobj2->player);

		// Moved here so it only fires once on bump.
		if (p2shield == KSHIELD_BUBBLE)
		{
			// Each bump does chip damage to the shield.
			K_RemoveBubbleHealth(mobj2->player, BUBBLEBUMPCHIP);

			if (mobj2->player->bubblehealth > 0)
			{
				// Play a distinct "crack!" noise to clue the player in.
				S_StartSound(mobj2, sfx_s3k6e);
			}
		}
		else if (p2shield == KSHIELD_NONE)
		{
			if (mobj1->player && (mobj2->player->rings > 0))
			{
				P_PlayerRingBurst(mobj2->player, 1);
			}
		}

		if (mobj2->player->spinouttimer)
		{
			mobj2->player->wipeoutslow = wipeoutslowtime+1;
			mobj2->player->spinouttimer = max(wipeoutslowtime+1, mobj2->player->spinouttimer);
			//mobj2->player->spinouttype = KSPIN_WIPEOUT; // Enforce type
		}
	}

	return true;
}

/**	\brief	Checks that the player is on an offroad subsector for realsies. Also accounts for line riding to prevent cheese.

	\param	mo	player mobj object

	\return	boolean
*/
static fixed_t K_CheckOffroadCollide(mobj_t *mo)
{
	// Check for sectors in touching_sectorlist
	msecnode_t *node;	// touching_sectorlist iter
	sector_t *s;		// main sector shortcut
	sector_t *s2;		// FOF sector shortcut
	ffloor_t *rover;	// FOF

	fixed_t flr;
	fixed_t cel;	// floor & ceiling for height checks to make sure we're touching the offroad sector.

	I_Assert(mo != NULL);
	I_Assert(!P_MobjWasRemoved(mo));

	for (node = mo->touching_sectorlist; node; node = node->m_sectorlist_next)
	{
		if (!node->m_sector)
			break;	// shouldn't happen.

		s = node->m_sector;
		// 1: Check for the main sector, make sure we're on the floor of that sector and see if we can apply offroad.
		// Make arbitrary Z checks because we want to check for 1 sector in particular, we don't want to affect the player if the offroad sector is way below them and they're lineriding a normal sector above.

		flr = P_MobjFloorZ(mo, s, s, mo->x, mo->y, NULL, false, true);
		cel = P_MobjCeilingZ(mo, s, s, mo->x, mo->y, NULL, true, true);	// get Z coords of both floors and ceilings for this sector (this accounts for slopes properly.)
		// NOTE: we don't use P_GetZAt with our x/y directly because the mobj won't have the same height because of its hitbox on the slope. Complex garbage but tldr it doesn't work.

		if ( ((s->flags & MSF_FLIPSPECIAL_FLOOR) && mo->z == flr)	// floor check
			|| ((mo->eflags & MFE_VERTICALFLIP && (s->flags & MSF_FLIPSPECIAL_CEILING) && (mo->z + mo->height) == cel)) )	// ceiling check.
		{
			return s->offroad;
		}

		// 2: If we're here, we haven't found anything. So let's try looking for FOFs in the sectors using the same logic.
		for (rover = s->ffloors; rover; rover = rover->next)
		{
			if (!(rover->fofflags & FOF_EXISTS))	// This FOF doesn't exist anymore.
				continue;

			s2 = &sectors[rover->secnum];	// makes things easier for us

			flr = P_GetFOFBottomZ(mo, s, rover, mo->x, mo->y, NULL);
			cel = P_GetFOFTopZ(mo, s, rover, mo->x, mo->y, NULL);	// Z coords for fof top/bottom.

			// we will do essentially the same checks as above instead of bothering with top/bottom height of the FOF.
			// Reminder that an FOF's floor is its bottom, silly!
			if ( ((s2->flags & MSF_FLIPSPECIAL_FLOOR) && mo->z == cel)	// "floor" check
				|| ((s2->flags & MSF_FLIPSPECIAL_CEILING) && (mo->z + mo->height) == flr) )	// "ceiling" check.
			{
				return s2->offroad;
			}
		}
	}

	return 0; // couldn't find any offroad
}

static fixed_t K_OffroadGradient(player_t *player, fixed_t offroad)
{
	// At 50% or lower Invincibility, offroad creeps up on you.
	fixed_t invinoffroad = (K_GetKartInvinType() == KARTINVIN_LEGACY) ? ((player->invincibilitytimer) ? FRACUNIT : 0) : min(FRACUNIT, K_InvincibilityGradient(player->invincibilitytimer) << 1);
	fixed_t fac = CLAMP(FRACUNIT - invinoffroad, 0, FRACUNIT);

	return FixedMul(offroad, fac);
}

/**	\brief	Updates the Player's offroad value once per frame

	\param	player	player object passed from K_KartPlayerThink

	\return	void
*/
static void K_UpdateOffroad(player_t *player)
{
	terrain_t *terrain = player->mo->terrain;
	fixed_t offroadstrength = 0;

	// TODO: Make this use actual special touch code.
	if (terrain != NULL && terrain->offroad > 0 && K_AffectingTerrainActive())
	{
		offroadstrength = (terrain->offroad << FRACBITS);
	}
	else
	{
		offroadstrength = K_CheckOffroadCollide(player->mo);
	}

	// Gradient our offroad strength.
	offroadstrength = K_OffroadGradient(player, offroadstrength);

	// If you are in offroad, a timer starts.
	if (offroadstrength)
	{
		if (player->offroad == 0)
			player->offroad = (TICRATE/2);

		if (player->offroad > 0)
		{
			fixed_t offroad;

			offroad = offroadstrength / (TICRATE/2);

			player->offroad += offroad;
		}

		if (player->offroad > offroadstrength)
			player->offroad = offroadstrength;
	}
	else
		player->offroad = 0;
}

/**	\brief	Updates the player's drafting values once per frame
 *
 *	\param	player	player object passed from K_KartPlayerThink
 *
 *	\return	void
 */

static void K_UpdateDraft(player_t *player)
{
	if (!K_DraftingActive())
	{
		player->draftpower = 0;
		player->draftleeway = 0;
		player->lastdraft = -1;
		return;
	}

	fixed_t topspd = K_GetKartSpeed(player, false, false);
	fixed_t draftdistance;
	fixed_t minDist;
	UINT8 leniency;
	UINT8 i;

	// Distance you have to be to draft. If you're still accelerating, then this distance is lessened.
	// This distance biases toward low weight! (min weight gets ??? units, max weight gets 2560 units)
	// This distance is also scaled based on game speed.
	draftdistance = (cv_kartdrafting_basedistance.value + (128 * (9 - player->kartweight))) * player->mo->scale;
	if (player->speed < topspd)
		draftdistance = FixedMul(draftdistance, FixedDiv(player->speed, topspd));
	draftdistance = FixedMul(draftdistance, K_GetKartGameSpeedScalar(gamespeed));

	// On the contrary, the leniency period biases toward high weight.
	leniency = (3*TICRATE)/4 + ((player->kartweight-1) * (TICRATE/6));

	minDist = cv_kartdrafting_closedeadzone.value * player->mo->scale;

	if (gametype == GT_BATTLE)
	{
		// TODO: gametyperules
		minDist /= 4;
		draftdistance *= 2;
		leniency *= 4;
	}

	// Not enough speed to draft.
	if (player->speed >= 20*player->mo->scale)
	{
//#define EASYDRAFTTEST
		// Let's hunt for players to draft off of!
		for (i = 0; i < MAXPLAYERS; i++)
		{
			fixed_t dist, olddraft;
#ifndef EASYDRAFTTEST
			angle_t yourangle, theirangle, diff;
#endif

			if (!playeringame[i] || players[i].spectator || !players[i].mo)
				continue;

#ifndef EASYDRAFTTEST
			// Don't draft on yourself :V
			if (&players[i] == player)
				continue;
#endif

			// Not enough speed to draft off of.
			if (players[i].speed < 20*players[i].mo->scale)
				continue;

			// You can't draft a ghost.
			if (players[i].hyudorotimer)
				continue;

#ifndef EASYDRAFTTEST
			yourangle = K_MomentumAngle(player->mo);
			theirangle = K_MomentumAngle(players[i].mo);

			// Not in front of this player.
			diff = AngleDelta(R_PointToAngle2(player->mo->x, player->mo->y, players[i].mo->x, players[i].mo->y), yourangle);
			if (diff > ANG10)
			{
				continue;
			}

			// Not moving in the same direction.
			diff = AngleDelta(yourangle, theirangle);
			if (diff > ANGLE_90)
			{
				continue;
			}

			//if (P_IsLocalPlayer(player))
				//CONS_Printf("Within Range of %s!\nAngle = %d\n", player_names[player - players], diff);
#endif

			dist = P_AproxDistance(P_AproxDistance(players[i].mo->x - player->mo->x, players[i].mo->y - player->mo->y), players[i].mo->z - player->mo->z);

#ifndef EASYDRAFTTEST

			// TOO close to draft.
			if (!cv_kartdrafting_closedraft.value && (dist < minDist))
			{
				continue;
			}

			// Not close enough to draft.
			if (dist > draftdistance && draftdistance > 0)
			{
				continue;
			}
#endif

			// Bots are unusually good at keeping their facing aligned on long, tight turns.
			// Force them to give up draft in these situations, like a drifting player typically would.
			UINT16 rejectThreshold = KART_FULLTURN/4;
			if (K_PlayerUsesBotMovement(player) && (abs(player->oldcmd.turning + player->cmd.turning) >= rejectThreshold))
			{
				continue;
			}

			olddraft = player->draftpower;

			player->draftleeway = leniency;
			player->lastdraft = i;

			// Draft power is used later in K_GetKartBoostPower, ranging from 0 for normal speed and FRACUNIT for max draft speed.
			// How much this increments every tic biases toward acceleration! (min speed gets ???% per tic, max speed gets ???% per tic)
			if (player->draftpower < FRACUNIT)
			{
				fixed_t add = (FRACUNIT/200) + ((9 - player->kartspeed+(player->kartspeed/3)) * ((3*FRACUNIT)/1600));
				player->draftpower += add;

				if (gametype == GT_BATTLE)
				{
					// TODO: gametyperules
					// Double gain in Battle
					player->draftpower += add;
				}
			}

			if (player->draftpower > FRACUNIT)
				player->draftpower = FRACUNIT;

			// Play draft finish noise
			if (olddraft < FRACUNIT && player->draftpower >= FRACUNIT)
			{
				S_StartSound(player->mo, sfx_s266);
				S_StartSound(player->mo, sfx_s3k7c);
			}

			return; // Finished doing our draft.
		}
	}

	// No one to draft off of? Then you can knock that off.
	if (player->draftleeway > 0) // Prevent small disruptions from stopping your draft.
	{
		if (P_IsObjectOnGround(player->mo) == true)
		{
			// Allow maintaining draft in air setpieces.
			player->draftleeway--;
		}
	}
	else // Remove draft speed boost.
	{
		player->draftpower = 0;
		player->lastdraft = -1;
	}
}

void K_KartPainEnergyFling(player_t *player)
{
	static const UINT8 numfling = 5;
	INT32 i;
	mobj_t *mo;
	angle_t fa;
	fixed_t ns;
	fixed_t z;

	// Better safe than sorry.
	if (!player)
		return;

	// P_PlayerRingBurst: "There's no ring spilling in kart, so I'm hijacking this for the same thing as TD"
	// :oh:

	for (i = 0; i < numfling; i++)
	{
		INT32 objType = mobjinfo[MT_FLINGENERGY].reactiontime;
		fixed_t momxy, momz; // base horizonal/vertical thrusts

		z = player->mo->z;
		if (player->mo->eflags & MFE_VERTICALFLIP)
			z += player->mo->height - mobjinfo[objType].height;

		mo = P_SpawnMobj(player->mo->x, player->mo->y, z, objType);

		mo->fuse = 8*TICRATE;
		P_SetTarget(&mo->target, player->mo);

		mo->destscale = player->mo->scale;
		P_SetScale(mo, player->mo->scale);

		// Angle offset by player angle, then slightly offset by amount of fling
		fa = ((i*FINEANGLES/16) + (player->mo->angle>>ANGLETOFINESHIFT) - ((numfling-1)*FINEANGLES/32)) & FINEMASK;

		if (i > 15)
		{
			momxy = 3*FRACUNIT;
			momz = 4*FRACUNIT;
		}
		else
		{
			momxy = 28*FRACUNIT;
			momz = 3*FRACUNIT;
		}

		ns = FixedMul(momxy, mo->scale);
		mo->momx = FixedMul(FINECOSINE(fa),ns);

		ns = momz;
		P_SetObjectMomZ(mo, ns, false);

		if (i & 1)
			P_SetObjectMomZ(mo, ns, true);

		if (player->mo->eflags & MFE_VERTICALFLIP)
			mo->momz *= -1;
	}
}

// Adds gravity flipping to an object relative to its master and shifts the z coordinate accordingly.
void K_FlipFromObject(mobj_t *mo, mobj_t *master)
{
	mo->eflags = (mo->eflags & ~MFE_VERTICALFLIP)|(master->eflags & MFE_VERTICALFLIP);
	mo->flags2 = (mo->flags2 & ~MF2_OBJECTFLIP)|(master->flags2 & MF2_OBJECTFLIP);

	if (mo->eflags & MFE_VERTICALFLIP)
		mo->z += master->height - FixedMul(master->scale, mo->height);
}

void K_MatchGenericExtraFlags(mobj_t *mo, mobj_t *master)
{
	// flipping
	// handle z shifting from there too. This is here since there's no reason not to flip us if needed when we do this anyway;
	K_FlipFromObject(mo, master);

	// visibility (usually for hyudoro)
	mo->renderflags = (mo->renderflags & ~RF_DONTDRAW) | (master->renderflags & RF_DONTDRAW);
}

// same as above, but does not adjust Z height when flipping
void K_GenericExtraFlagsNoZAdjust(mobj_t *mo, mobj_t *master)
{
	// flipping
	mo->eflags = (mo->eflags & ~MFE_VERTICALFLIP)|(master->eflags & MFE_VERTICALFLIP);
	mo->flags2 = (mo->flags2 & ~MF2_OBJECTFLIP)|(master->flags2 & MF2_OBJECTFLIP);

	// visibility (usually for hyudoro)
	mo->renderflags = (mo->renderflags & ~RF_DONTDRAW) | (master->renderflags & RF_DONTDRAW);
}


void K_SpawnDashDustRelease(player_t *player)
{
	fixed_t newx;
	fixed_t newy;
	mobj_t *dust;
	angle_t travelangle;
	INT32 i;

	I_Assert(player != NULL);
	I_Assert(player->mo != NULL);
	I_Assert(!P_MobjWasRemoved(player->mo));

	if (!P_IsObjectOnGround(player->mo))
		return;

	if (!player->speed && !player->startboost)
		return;

	travelangle = player->mo->angle;

	if (player->drift || (player->pflags & PF_DRIFTEND))
		travelangle -= (ANGLE_45/5)*player->drift;

	for (i = 0; i < 2; i++)
	{
		newx = player->mo->x + P_ReturnThrustX(player->mo, travelangle + ((i&1) ? -1 : 1)*ANGLE_90, FixedMul(48*FRACUNIT, player->mo->scale));
		newy = player->mo->y + P_ReturnThrustY(player->mo, travelangle + ((i&1) ? -1 : 1)*ANGLE_90, FixedMul(48*FRACUNIT, player->mo->scale));
		dust = P_SpawnMobj(newx, newy, player->mo->z, MT_FASTDUST);

		P_SetTarget(&dust->target, player->mo);
		dust->angle = travelangle - (((i&1) ? -1 : 1) * ANGLE_45);
		dust->destscale = player->mo->scale;
		P_SetScale(dust, player->mo->scale);

		dust->momx = 3*player->mo->momx/5;
		dust->momy = 3*player->mo->momy/5;
		dust->momz = 3*P_GetMobjZMovement(player->mo)/5;

		K_MatchGenericExtraFlags(dust, player->mo);
	}
}

static fixed_t K_GetBrakeFXScale(player_t *player, fixed_t maxScale)
{
	fixed_t s = FixedDiv(player->speed,
			K_GetKartSpeed(player, false, false));

	s = max(s, FRACUNIT);
	s = min(s, maxScale);

	return s;
}

static void K_SpawnBrakeDriftSparks(player_t *player) // Be sure to update the mobj thinker case too!
{
	mobj_t *sparks;

	I_Assert(player != NULL);
	I_Assert(player->mo != NULL);
	I_Assert(!P_MobjWasRemoved(player->mo));

	// Position & etc are handled in its thinker, and its spawned invisible.
	// This avoids needing to dupe code if we don't need it.
	sparks = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_BRAKEDRIFT);
	P_SetTarget(&sparks->target, player->mo);
	P_SetScale(sparks, (sparks->destscale = FixedMul(K_GetBrakeFXScale(player, 3*FRACUNIT), player->mo->scale)));
	K_MatchGenericExtraFlags(sparks, player->mo);
	sparks->renderflags |= RF_DONTDRAW;
}

void K_SpawnNormalSpeedLines(player_t *player)
{
	fixed_t rand_x;
	fixed_t rand_y;
	fixed_t rand_z;

	rand_z = player->mo->z + (player->mo->height/2) + (P_RandomRange(-20,20) * player->mo->scale);
	rand_y = player->mo->y + (P_RandomRange(-36,36) * player->mo->scale);
	rand_x = player->mo->x + (P_RandomRange(-36,36) * player->mo->scale);

	mobj_t *fast = P_SpawnMobj(rand_x, rand_y, rand_z, MT_FASTLINE);

	P_SetTarget(&fast->target, player->mo);
	fast->angle = K_MomentumAngle(player->mo);
	fast->momx = 3*player->mo->momx/4;
	fast->momy = 3*player->mo->momy/4;
	fast->momz = 3*P_GetMobjZMovement(player->mo)/4;

	K_MatchGenericExtraFlags(fast, player->mo);

	if (player->tripwireLeniency)
	{
		fast->destscale = fast->destscale * 2;
		P_SetScale(fast, 3*fast->scale/2);
	}

	if (player->invincibilitytimer)
	{
		const tic_t defaultTime = itemtime+(2*TICRATE);
		if (player->invincibilitytimer > defaultTime)
		{
			fast->color = player->mo->color;
		}
		else
		{
			fast->color = SKINCOLOR_INVINCFLASH;
		}
		fast->colorized = true;
	}
	else if (player->tripwireLeniency)
	{
		// Make it pink+blue+big when you can go through tripwire
		fast->color = (leveltime & 1) ? SKINCOLOR_LILAC : SKINCOLOR_JAWZ;
		fast->colorized = true;
		fast->renderflags |= RF_ADD;
	}
	else if (player->pflags & PF_AIRFAILSAFE)
	{
		fast->color = SKINCOLOR_WHITE;
		fast->colorized = true;
	}
}

void K_SpawnDraftSpeedLines(player_t *player, fixed_t scale, skincolornum_t color, boolean translucent)
{
	fixed_t rand_x;
	fixed_t rand_y;
	fixed_t rand_z;

	rand_z = player->mo->z + (player->mo->height/2) + (P_RandomRange(-20,20) * player->mo->scale);
	rand_y = player->mo->y + (P_RandomRange(-36,36) * player->mo->scale);
	rand_x = player->mo->x + (P_RandomRange(-36,36) * player->mo->scale);

	mobj_t *fast = P_SpawnMobj(rand_x, rand_y, rand_z, MT_FASTLINE);

	P_SetTarget(&fast->target, player->mo);
	fast->angle = K_MomentumAngle(player->mo);
	fast->momx = 3*player->mo->momx/4;
	fast->momy = 3*player->mo->momy/4;
	fast->momz = 3*P_GetMobjZMovement(player->mo)/4;

	K_MatchGenericExtraFlags(fast, player->mo);

	fast->colorized = true;
	P_SetScale2(fast, scale, false);
	fast->color = color;
	if (translucent)
	{
		fast->renderflags |= RF_ADD;
	}
}

void K_SpawnInvincibilitySpeedLines(mobj_t *mo)
{
	fixed_t rand_x;
	fixed_t rand_y;
	fixed_t rand_z;

	rand_z = P_RandomRange(0, 64) * FRACUNIT;
	rand_y = P_RandomRange(-48, 48) * FRACUNIT;
	rand_x = P_RandomRange(-48, 48) * FRACUNIT;

	mobj_t *fast = P_SpawnMobjFromMobj(mo, rand_x, rand_y, rand_z, MT_FASTLINE);
	P_SetMobjState(fast, S_KARTINVLINES1);

	P_SetTarget(&fast->target, mo);
	fast->angle = K_MomentumAngle(mo);

	fast->momx = 3*mo->momx/4;
	fast->momy = 3*mo->momy/4;
	fast->momz = 3*P_GetMobjZMovement(mo)/4;

	K_MatchGenericExtraFlags(fast, mo);

	fast->color = mo->color;
	fast->colorized = true;

	if (mo->player->invincibilitytimer < 10*TICRATE)
		fast->destscale = 6*((mo->player->invincibilitytimer/TICRATE)*FRACUNIT)/8;
}

static void K_SpawnStackingEffect(player_t *player)
{
	// Thanks to 1ndev for code used for booststack->scale and boosttack->frame! (taken and modified from BoostStack)
	mobj_t  *booststack = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_BOOSTSTACK);
	booststack->flags2 |= MF2_DONTSYNC; // No desyncs pls!
	P_SetTarget(&booststack->target, player->mo);
	P_SetScale(booststack, FixedMul(FRACUNIT + FixedMul(2*FRACUNIT - FRACUNIT, FixedDiv(min(player->numboosts,4)*FRACUNIT, 4*FRACUNIT)), mapobjectscale));

	booststack->angle = player->mo->angle + ANGLE_90;
	booststack->color = player->mo->color;
}

void K_SpawnBumpEffect(mobj_t *mo)
{
	mobj_t *fx = P_SpawnMobj(mo->x, mo->y, mo->z, MT_BUMP);
	if (mo->eflags & MFE_VERTICALFLIP)
		fx->eflags |= MFE_VERTICALFLIP;
	else
		fx->eflags &= ~MFE_VERTICALFLIP;
	fx->scale = mo->scale;

	S_StartSound(mo, sfx_s3k49);
}

static SINT8 K_GlanceAtPlayers(player_t *glancePlayer)
{
	const fixed_t maxdistance = FixedMul(1280 * mapobjectscale, K_GetKartGameSpeedScalar(gamespeed));
	const angle_t blindSpotSize = ANG10; // ANG5
	UINT8 i;
	SINT8 glanceDir = 0;
	SINT8 lastValidGlance = 0;

	// See if there's any players coming up behind us.
	// If so, your character will glance at 'em.
	for (i = 0; i < MAXPLAYERS; i++)
	{
		player_t *p;
		angle_t back;
		angle_t diff;
		fixed_t distance;
		SINT8 dir = -1;

		if (!playeringame[i])
		{
			// Invalid player
			continue;
		}

		p = &players[i];

		if (p == glancePlayer)
		{
			// FOOL! Don't glance at yerself!
			continue;
		}

		if (!p->mo || P_MobjWasRemoved(p->mo))
		{
			// Invalid mobj
			continue;
		}

		if (p->spectator || p->hyudorotimer > 0)
		{
			// Not playing / invisible
			continue;
		}

		distance = R_PointToDist2(glancePlayer->mo->x, glancePlayer->mo->y, p->mo->x, p->mo->y);
		distance = R_PointToDist2(0, glancePlayer->mo->z, distance, p->mo->z);

		if (distance > maxdistance)
		{
			continue;
		}

		back = glancePlayer->mo->angle + ANGLE_180;
		diff = R_PointToAngle2(glancePlayer->mo->x, glancePlayer->mo->y, p->mo->x, p->mo->y) - back;

		if (diff > ANGLE_180)
		{
			diff = InvAngle(diff);
			dir = -dir;
		}

		if (diff > ANGLE_90)
		{
			// Not behind the player
			continue;
		}

		if (diff < blindSpotSize)
		{
			// Small blindspot directly behind your back, gives the impression of smoothly turning.
			continue;
		}

		if (P_CheckSight(glancePlayer->mo, p->mo) == true)
		{
			// Not blocked by a wall, we can glance at 'em!
			// Adds, so that if there's more targets on one of your sides, it'll glance on that side.
			glanceDir += dir;

			// That poses a limitation if there's an equal number of targets on both sides...
			// In that case, we'll pick the last chosen glance direction.
			lastValidGlance = dir;
		}
	}

	if (glanceDir > 0)
	{
		return 1;
	}
	else if (glanceDir < 0)
	{
		return -1;
	}

	return lastValidGlance;
}

/**	\brief	Calculates the respawn timer and drop-boosting

	\param	player	player object passed from K_KartPlayerThink

	\return	void
*/
static void K_RespawnChecker(player_t *player)
{
	UINT16 buttons = K_GetKartButtons(player);
	if (player->spectator)
		return;

	if (player->respawn > 1)
	{
		player->respawn--;
		player->mo->momz = 0;
		player->flashing = 2;
		player->nocontrol = 2;
		if (leveltime % 8 == 0)
		{
			INT32 i;
			if (!mapreset)
				S_StartSound(player->mo, sfx_s3kcas);

			for (i = 0; i < 8; i++)
			{
				mobj_t *mo;
				angle_t newangle;
				fixed_t newx, newy, newz;

				newangle = FixedAngle(((360/8)*i)*FRACUNIT);
				newx = player->mo->x + P_ReturnThrustX(player->mo, newangle, 31<<FRACBITS); // does NOT use scale, since this effect doesn't scale properly
				newy = player->mo->y + P_ReturnThrustY(player->mo, newangle, 31<<FRACBITS);
				if (player->mo->eflags & MFE_VERTICALFLIP)
					newz = player->mo->z + player->mo->height;
				else
					newz = player->mo->z;

				mo = P_SpawnMobj(newx, newy, newz, MT_DEZLASER);
				if (mo)
				{
					if (player->mo->eflags & MFE_VERTICALFLIP)
						mo->eflags |= MFE_VERTICALFLIP;
					P_SetTarget(&mo->target, player->mo);
					mo->angle = newangle+ANGLE_90;
					mo->momz = (8<<FRACBITS) * P_MobjFlip(player->mo);
					P_SetScale(mo, (mo->destscale = FRACUNIT));
				}
			}
		}
	}
	else if (player->respawn == 1)
	{
		if (player->growshrinktimer < 0)
		{
			player->mo->scalespeed = mapobjectscale/TICRATE;
			player->mo->destscale = (6*mapobjectscale)/8;
			if (K_PlayerShrinkCheat(player) && !modeattacking && !player->bot)
				player->mo->destscale = (6*player->mo->destscale)/8;
		}

		if (!P_IsObjectOnGround(player->mo) && !mapreset)
		{
			player->flashing = K_GetKartFlashing(player);

			// Sal: The old behavior was stupid and prone to accidental usage.
			// Let's rip off Mania instead, and turn this into a Drop Dash!

			if (buttons & BT_ACCELERATE)
				player->dropdash++;
			else
				player->dropdash = 0;

			if (player->dropdash == TICRATE/4)
				S_StartSound(player->mo, sfx_ddash);

			if ((player->dropdash >= TICRATE/4)
				&& (player->dropdash & 1))
				player->mo->colorized = true;
			else
				player->mo->colorized = false;
		}
		else
		{
			if ((buttons & BT_ACCELERATE) && (player->dropdash >= TICRATE/4))
			{
				S_StartSound(player->mo, sfx_s23c);
				player->startboost = 50;
				K_SpawnDashDustRelease(player);
			}

			P_PlayerRingBurst(player, 2);
			player->mo->colorized = false;
			player->dropdash = 0;
			player->respawn = 0;
		}
	}
}

/**	\brief Handles the state changing for moving players, moved here to eliminate duplicate code

	\param	player	player data

	\return	void
*/
void K_KartMoveAnimation(player_t *player)
{
	const fixed_t fastspeed = (K_GetKartSpeed(player, false, true) * 17) / 20; // 85%
	const fixed_t speedthreshold = player->mo->scale / 8;

	const boolean onground = P_IsObjectOnGround(player->mo);

	UINT16 buttons = K_GetKartButtons(player);
	const boolean spinningwheels = (player->speed > 0);
	const boolean lookback = ((buttons & BT_LOOKBACK) == BT_LOOKBACK);
	const boolean skincompat = wadfiles[((skin_t *)player->mo->skin)->wadnum]->compatmode;

	SINT8 turndir = intsign(player->cmd.turning);
	SINT8 destGlanceDir = 0;
	SINT8 drift = player->drift;
	UINT8 spr2, glanceofs;

	player->mo->rollingxoffset = 0;
	player->mo->rollingyoffset = 0;

	if (!lookback)
		player->pflags &= ~PF_GAINAX;

	// Sliptides: drift -> lookback frames
	if (abs(player->aizdriftturn) >= ANGLE_90 && !skincompat)
	{
		destGlanceDir = -(2*intsign(player->aizdriftturn));
		player->glanceDir = destGlanceDir;
		drift = turndir = 0;
		player->pflags &= ~PF_GAINAX;
	}
	else if (player->aizdriftturn)
	{
		drift = intsign(player->aizdriftturn);
		turndir = 0;
	}
	else if (drift == 0)
	{
		// Only try glancing if you're driving straight.
		// This avoids all-players loops when we don't need it.
		destGlanceDir = K_GlanceAtPlayers(player);

		if (lookback == true)
		{
			//statenum_t gainaxstate = S_GAINAX_TINY;
			if (destGlanceDir == 0)
			{
				if (player->glanceDir != 0)
				{
					// Keep to the side you were already on.
					if (player->glanceDir < 0)
					{
						destGlanceDir = -1;
					}
					else
					{
						destGlanceDir = 1;
					}
				}
				else
				{
					// Look to your right by default
					destGlanceDir = -1;
				}
			}
			else
			{
				// Looking back AND glancing? Amplify the look!
				destGlanceDir *= 2;
				/*if (player->itemamount && player->itemtype)
					gainaxstate = S_GAINAX_HUGE;
				else
					gainaxstate = S_GAINAX_MID1;*/
			}

			/*if (destGlanceDir && !(player->pflags & PF_GAINAX))
			{
				mobj_t *gainax = P_SpawnMobjFromMobj(player->mo, 0, 0, 0, MT_GAINAX);
				gainax->movedir = (destGlanceDir < 0) ? (ANGLE_270-ANG10) : (ANGLE_90+ANG10);
				P_SetTarget(&gainax->target, player->mo);
				P_SetMobjState(gainax, gainaxstate);
				gainax->flags2 |= MF2_AMBUSH;
				player->pflags |= PF_GAINAX;
			}*/
		}
		else if (player->cmd.forwardmove < 0 && destGlanceDir == 0)
		{
			// Reversing -- like looking back, but doesn't stack on the other glances.
			if (player->glanceDir != 0)
			{
				// Keep to the side you were already on.
				if (player->glanceDir < 0)
				{
					destGlanceDir = -1;
				}
				else
				{
					destGlanceDir = 1;
				}
			}
			else
			{
				// Look to your right by default
				destGlanceDir = -1;
			}
		}
		else if (turndir)
		{
			// Not glancing
			destGlanceDir = 0;
			player->glanceDir = 0;
		}
	}

	if (onground == false)
		spr2 = SPR2_FSTN; // Only use certain frames in the air, to make it look like your tires are spinning fruitlessly!
	else if (drift > 0)
		spr2 = SPR2_DRLN; // Drifting LEFT!
	else if (drift < 0)
		spr2 = SPR2_DRRN; // Drifting RIGHT!
	else if (player->speed >= fastspeed && player->speed >= (player->lastspeed - speedthreshold))
		spr2 = SPR2_FSTN; // Going REAL fast!
	else if (spinningwheels)
		spr2 = SPR2_SLWN; // Drivin' slow.
	else
		spr2 = SPR2_STIN; // Completely still.

	glanceofs = abs(player->glanceDir)*2 + (player->glanceDir < 0);

	// do we NOT have any glance/look sprites?
	if (glanceofs && P_GetSkinSprite2(player->mo->skin, spr2 + glanceofs+1, player) == spr2)
	{
		// then don't bother glancing
		// this lets kart 3D models still show their turning frames when looking back
		glanceofs = 0;
	}

	if (onground && drift)
	{
		if ((player->jitterlegacy) && (!skincompat))
		{
			// Make RR characters imitate legacy jitters.
			player->mo->rollingxoffset = ((player->driftelapsed & 1) * 2) * -intsign(drift);
		}
		else
		{
			if (drift > 0)
				turndir *= -1;

			if (turndir == -1)
				spr2 += 2; // Inwards drift
			else if (turndir == 1)
				spr2 += 1; // Outwards drift
		}
	}
	else if (glanceofs)
		spr2 += glanceofs+1;
	else if (turndir == -1)
		spr2 += 2; // turn right
	else if (turndir == 1)
		spr2 += 1; // turn left

	if (player->mo->state != &states[S_KART_STILL + spr2])
		P_SetPlayerMobjState(player->mo, S_KART_STILL + spr2);

	if (!onground && !spinningwheels)
	{
		// TODO: The "tires still in the air" states should have it's own SPR2s.
		// This was a quick hack to get the same functionality with less work,
		// but it's really dunderheaded & isn't customizable at all.
		player->mo->frame = (player->mo->frame & ~FF_FRAMEMASK);
		player->mo->tics++; // Makes it properly use frame 0
	}

#undef SetState

	// Update your glance value to smooth it out.
	if (player->glanceDir > destGlanceDir)
	{
		player->glanceDir--;
	}
	else if (player->glanceDir < destGlanceDir)
	{
		player->glanceDir++;
	}

	if (!player->glanceDir)
		player->pflags &= ~PF_GAINAX;

	// Update lastspeed value -- we use to display slow driving frames instead of fast driving when slowing down.
	player->lastspeed = player->speed;
}

static void K_TauntVoiceTimers(player_t *player)
{
	if (!player)
		return;

	player->karthud[khud_tauntvoices] = 6*TICRATE;
	player->karthud[khud_voices] = 4*TICRATE;
}

static void K_RegularVoiceTimers(player_t *player)
{
	if (!player)
		return;

	player->karthud[khud_voices] = 4*TICRATE;

	if (player->karthud[khud_tauntvoices] < 4*TICRATE)
		player->karthud[khud_tauntvoices] = 4*TICRATE;
}

void K_PlayAttackTaunt(mobj_t *source)
{
	sfxenum_t pick = P_RandomKey(2); // Gotta roll the RNG every time this is called for sync reasons
	boolean tasteful = (!source->player || !source->player->karthud[khud_tauntvoices]);

	if (cv_kartvoices.value && (tasteful || cv_kartvoices.value == 2))
		S_StartSound(source, sfx_kattk1+pick);

	if (!tasteful)
		return;

	K_TauntVoiceTimers(source->player);
}

void K_PlayBoostTaunt(mobj_t *source)
{
	sfxenum_t pick = P_RandomKey(2); // Gotta roll the RNG every time this is called for sync reasons
	boolean tasteful = (!source->player || !source->player->karthud[khud_tauntvoices]);

	if (cv_kartvoices.value && (tasteful || cv_kartvoices.value == 2))
		S_StartSound(source, sfx_kbost1+pick);

	if (!tasteful)
		return;

	K_TauntVoiceTimers(source->player);
}

void K_PlayOvertakeSound(mobj_t *source)
{
	boolean tasteful = (!source->player || !source->player->karthud[khud_voices]);

	if (!(gametype == GT_RACE)) // Only in race
		return;

	// 4 seconds from before race begins, 10 seconds afterwards
	if (leveltime < starttime+(10*TICRATE))
		return;

	if (cv_kartvoices.value && (tasteful || cv_kartvoices.value == 2))
		S_StartSound(source, sfx_kslow);

	if (!tasteful)
		return;

	K_RegularVoiceTimers(source->player);
}

void K_PlayPainSound(mobj_t *source, mobj_t *other)
{
	sfxenum_t pick = P_RandomKey(2); // Gotta roll the RNG every time this is called for sync reasons

	sfxenum_t sfx_id = ((skin_t *)source->skin)->soundsid[S_sfx[sfx_khurt1 + pick].skinsound];
	boolean alwaysHear = false;

	if (cv_kartvoices.value)
	{
		if (cv_karthitemdialog.value)
		{
			if (other != NULL && P_MobjWasRemoved(other) == false && other->player != NULL)
			{
				alwaysHear = P_IsDisplayPlayer(other->player);
			}

			S_StartSound(alwaysHear ? NULL : source, sfx_id);
		}
		else
		{
			S_StartSound(source, sfx_id);
		}
	}
	else
	{
		S_StartSound(source, sfx_slip);
	}

	K_RegularVoiceTimers(source->player);
}

void K_PlayHitEmSound(mobj_t *source, mobj_t *other)
{
	sfxenum_t sfx_id = ((skin_t *)source->skin)->soundsid[S_sfx[sfx_khitem].skinsound];
	boolean alwaysHear = false;

	if (cv_kartvoices.value)
	{
		if (cv_karthitemdialog.value)
		{
			if (other != NULL && P_MobjWasRemoved(other) == false && other->player != NULL)
			{
				alwaysHear = P_IsDisplayPlayer(other->player);
			}

			if (cv_kartvoices.value)
			{
				S_StartSound(alwaysHear ? NULL : source, sfx_id);
			}
		}
		else
		{
			S_StartSound(source, sfx_id);
		}
	}
	else
	{
		S_StartSound(source, sfx_s1c9); // The only lost gameplay functionality with voices disabled
	}

	K_RegularVoiceTimers(source->player);
}

void K_TryHurtSoundExchange(mobj_t *victim, mobj_t *attacker)
{
	if (victim == NULL || P_MobjWasRemoved(victim) == true || victim->player == NULL)
	{
		return;
	}

	// In a perfect world we could move this here, but there's
	// a few niche situations where we want a pain sound from
	// the victim, but no confirm sound from the attacker.

	//K_PlayPainSound(victim, attacker);

	if (attacker == NULL || P_MobjWasRemoved(attacker) == true || attacker->player == NULL)
	{
		return;
	}

	attacker->player->confirmVictim = (victim->player - players);
	attacker->player->confirmVictimDelay = TICRATE/2;

	if (attacker->player->follower != NULL)
	{
		const follower_t *fl = &followers[attacker->player->followerskin];
		attacker->player->follower->movecount = fl->hitconfirmtime; // movecount is used to play the hitconfirm animation for followers.
	}
}

void K_PlayPowerGloatSound(mobj_t *source)
{
	if (cv_kartvoices.value)
	{
		S_StartSound(source, sfx_kgloat);
	}

	K_RegularVoiceTimers(source->player);
}

static void K_HandleDelayedHitByEm(player_t *player)
{
	if (player->confirmVictimDelay == 0)
	{
		return;
	}

	player->confirmVictimDelay--;

	if (player->confirmVictimDelay == 0)
	{
		mobj_t *victim = NULL;

		if (player->confirmVictim < MAXPLAYERS && playeringame[player->confirmVictim])
		{
			player_t *victimPlayer = &players[player->confirmVictim];

			if (victimPlayer != NULL && victimPlayer->spectator == false)
			{
				victim = victimPlayer->mo;
			}
		}

		K_PlayHitEmSound(player->mo, victim);
	}
}

void K_MomentumToFacing(player_t *player)
{
	angle_t dangle = player->mo->angle - K_MomentumAngle(player->mo);

	if (dangle > ANGLE_180)
		dangle = InvAngle(dangle);

	// If you aren't on the ground or are moving in too different of a direction don't do this
	if (player->mo->eflags & MFE_JUSTHITFLOOR)
		; // Just hit floor ALWAYS redirects
	else if (!P_IsObjectOnGround(player->mo) || dangle > ANGLE_90)
		return;

	P_Thrust(player->mo, player->mo->angle, player->speed - FixedMul(player->speed, player->mo->friction));
	player->mo->momx = FixedMul(player->mo->momx - player->cmomx, player->mo->friction) + player->cmomx;
	player->mo->momy = FixedMul(player->mo->momy - player->cmomy, player->mo->friction) + player->cmomy;
}

boolean K_ApplyOffroad(const player_t *player)
{
	boolean sneakertimer = CANTCHAINOFFROAD ? (player->sneakertimer && player->realsneakertimer) : player->sneakertimer > 0;

	if (modeattacking != ATTACKING_NONE)
		sneakertimer = player->sneakertimer > 0;

	if (player->hyudorotimer || sneakertimer)
		return false;
	return true;
}

fixed_t K_PlayerTripwireSpeedThreshold(const player_t *player)
{
	fixed_t required_speed = 2 * K_GetKartSpeed(player, false, false); // 200%

	if (player->offroad && K_ApplyOffroad(player))
	{
		// Increase to 300% if you're lawnmowering.
		required_speed = (required_speed * 3) / 2;
	}

	if (player->botvars.rubberband > FRACUNIT && K_PlayerUsesBotMovement(player) == true)
	{
		// Make it harder for bots to do this when rubberbanding.

		// This is actually biased really hard against the bot,
		// because the bot rubberbanding speed increase is
		// decreased with other boosts.

		required_speed = FixedMul(required_speed, player->botvars.rubberband);
	}

	return required_speed;
}

tripwirepass_t K_TripwirePassConditions(const player_t *player)
{
	if (
			player->invincibilitytimer ||
			(player->sneakertimer && player->realsneakertimer)
		)
		return TRIPWIRE_BLASTER;

	if (
			player->flamestore ||
			((player->speed > K_PlayerTripwireSpeedThreshold(player)) && player->tripwireReboundDelay == 0)
	)
		return TRIPWIRE_BOOST;

	if (
			player->growshrinktimer > 0 ||
			player->hyudorotimer
		)
		return TRIPWIRE_IGNORE;

	return TRIPWIRE_NONE;
}

boolean K_TripwirePass(const player_t *player)
{
	return (player->tripwirePass != TRIPWIRE_NONE);
}

// Safe guard cvars to prevent cheating in RA.
INT32 K_RAGuard(consvar_t cvar)
{
	if (modeattacking != ATTACKING_NONE)
	{
		if (cvar.flags & CV_FLOAT)
		{
			// This is a float, atof the value and run it through FloatToFixed
			return FloatToFixed(atof(cvar.defaultvalue));
		}
		else
		{
			// Handle string bools first.
			if (!strcmp(cvar.defaultvalue, "On") || !strcmp(cvar.defaultvalue, "Yes"))
			{
				return true;
			}

			if (!strcmp(cvar.defaultvalue, "Off") || !strcmp(cvar.defaultvalue, "No"))
			{
				return false;
			}

			// This is a int, just use atoi the value normally.
			return atoi(cvar.defaultvalue);
		}
	}

	return cvar.value;
}

boolean K_ItemMobjAllowedtoWaterRun(mobj_t *item)
{
	switch (item->type)
	{
		case MT_ORBINAUT:
		case MT_JAWZ:
		case MT_JAWZ_DUD:
		case MT_BANANA_SHIELD:
		case MT_SSMINE_SHIELD:
			return true;
			break;
		default:
			return false;
			break;
	}
	return false;
}

boolean K_WaterRun(mobj_t *mobj)
{
	// Let dragged items waterrun with the player for free.
	switch (mobj->type)
	{
		case MT_BANANA_SHIELD:
		case MT_SSMINE_SHIELD:
			if (mobj->target && (mobj->target->flags2 & MF2_WATERRUN))
				return true;
			break;
		default:
			break;
	}

	if (mobj->flags2 & MF2_WATERRUN)
	{
		return true;
	}

	return false;
}

void K_SpawnWaterTrail(mobj_t *mobj)
{
	fixed_t topspeed = INT32_MAX;
	fixed_t speed = INT32_MAX;
	fixed_t runspd = 14 * mobj->scale;
	fixed_t trailScale = FRACUNIT;

	if (mobj->momz != 0)
	{
		// Only while touching ground.
		return;
	}

	if (mobj->watertop == INT32_MAX || mobj->waterbottom == INT32_MIN)
	{
		// Invalid water plane.
		return;
	}

	if (!((!(mobj->eflags & MFE_VERTICALFLIP) && mobj->z + mobj->height >= mobj->watertop && mobj->z <= mobj->watertop)
		|| (mobj->eflags & MFE_VERTICALFLIP && mobj->z + mobj->height >= mobj->waterbottom && mobj->z <= mobj->waterbottom)))
	{
		// No valid watertop.
		return;
	}

	if (mobj->player != NULL)
	{
		if (mobj->player->spectator)
		{
			// Not as spectator.
			return;
		}

		if (mobj->player->carry == CR_SLIDING)
		{
			// Not in water slides.
			return;
		}

		topspeed = K_GetKartSpeed(mobj->player, false, false);
		runspd = FixedMul(runspd, mobj->movefactor);
	}
	else
	{
		topspeed = FixedMul(mobj->scale, K_GetKartSpeedFromStat(5, false));
	}

	speed = P_AproxDistance(mobj->momx, mobj->momy);

	if (speed <= runspd)
	{
		// Not fast enough.
		return;
	}

	if (topspeed > runspd)
	{
		trailScale = FixedMul(FixedDiv(speed - runspd, topspeed - runspd), mapobjectscale);
	}
	else
		trailScale = mapobjectscale; // Scaling is based off difference between runspeed and top speed

	if (trailScale > 0)
	{
		const angle_t forwardangle = K_MomentumAngle(mobj);
		const fixed_t visualradius = mobj->radius + (8 * mobj->scale);
		const SINT8 numFrames = 5;
		const INT32 curFrame = (leveltime % numFrames)|FF_PAPERSPRITE;
		fixed_t x1, x2, y1, y2;
		mobj_t *water;

		x1 = mobj->x + mobj->momx + P_ReturnThrustX(mobj, forwardangle + ANGLE_90, visualradius);
		y1 = mobj->y + mobj->momy + P_ReturnThrustY(mobj, forwardangle + ANGLE_90, visualradius);
		x1 = x1 + P_ReturnThrustX(mobj, forwardangle, visualradius);
		y1 = y1 + P_ReturnThrustY(mobj, forwardangle, visualradius);

		x2 = mobj->x + mobj->momx + P_ReturnThrustX(mobj, forwardangle - ANGLE_90, visualradius);
		y2 = mobj->y + mobj->momy + P_ReturnThrustY(mobj, forwardangle - ANGLE_90, visualradius);
		x2 = x2 + P_ReturnThrustX(mobj, forwardangle, visualradius);
		y2 = y2 + P_ReturnThrustY(mobj, forwardangle, visualradius);

		// Left
		// underlay
		water = P_SpawnMobj(x1, y1,
			((mobj->eflags & MFE_VERTICALFLIP) ? mobj->waterbottom - FixedMul(mobjinfo[MT_WATERTRAILUNDERLAY].height, mobj->scale) : mobj->watertop), MT_WATERTRAILUNDERLAY);
		water->angle = forwardangle - ANGLE_180 - ANGLE_22h;
		water->destscale = trailScale;
		water->momx = mobj->momx;
		water->momy = mobj->momy;
		water->momz = mobj->momz;
		P_SetScale(water, trailScale);
		P_SetMobjState(water, S_WATERTRAILSUNDERLAY);
		water->frame = curFrame|FF_ADD|FF_TRANS40;

		// overlay
		water = P_SpawnMobj(x1, y1,
			((mobj->eflags & MFE_VERTICALFLIP) ? mobj->waterbottom - FixedMul(mobjinfo[MT_WATERTRAIL].height, mobj->scale) : mobj->watertop), MT_WATERTRAIL);
		water->angle = forwardangle - ANGLE_180 - ANGLE_22h;
		water->destscale = trailScale;
		water->momx = mobj->momx;
		water->momy = mobj->momy;
		water->momz = mobj->momz;
		P_SetScale(water, trailScale);
		P_SetMobjState(water, S_WATERTRAILS);
		water->frame = curFrame;

		// Right
		// Underlay
		water = P_SpawnMobj(x2, y2,
			((mobj->eflags & MFE_VERTICALFLIP) ? mobj->waterbottom - FixedMul(mobjinfo[MT_WATERTRAILUNDERLAY].height, mobj->scale) : mobj->watertop), MT_WATERTRAILUNDERLAY);
		water->angle = forwardangle - ANGLE_180 + ANGLE_22h;
		water->destscale = trailScale;
		water->momx = mobj->momx;
		water->momy = mobj->momy;
		water->momz = mobj->momz;
		P_SetScale(water, trailScale);
		P_SetMobjState(water, S_WATERTRAILSUNDERLAY);
		water->frame = curFrame|FF_ADD|FF_TRANS40;

		// Overlay
		water = P_SpawnMobj(x2, y2,
			((mobj->eflags & MFE_VERTICALFLIP) ? mobj->waterbottom - FixedMul(mobjinfo[MT_WATERTRAIL].height, mobj->scale) : mobj->watertop), MT_WATERTRAIL);
		water->angle = forwardangle - ANGLE_180 + ANGLE_22h;
		water->destscale = trailScale;
		water->momx = mobj->momx;
		water->momy = mobj->momy;
		water->momz = mobj->momz;
		P_SetScale(water, trailScale);
		P_SetMobjState(water, S_WATERTRAILS);
		water->frame = curFrame;

		if (!S_SoundPlaying(mobj, sfx_s3kdbs))
		{
			const INT32 volume = (min(trailScale, FRACUNIT) * 255) / FRACUNIT;
			S_StartSoundAtVolume(mobj, sfx_s3kdbs, volume);
		}
	}

	// Little water sound while touching water - just a nicety.
	if ((mobj->eflags & MFE_TOUCHWATER) && !(mobj->eflags & MFE_UNDERWATER))
	{
		if (P_RandomChance(FRACUNIT/2) && leveltime % TICRATE == 0)
			S_StartSound(mobj, sfx_floush);
	}
}

static fixed_t K_FlameShieldDashVar(INT32 val)
{
	// 1 second = 15% + ????% top speed
	return (FRACUNIT/6) + (((val * (FRACUNIT)) / TICRATE) / 2);
}

static inline fixed_t K_GetProjectileSpeed(void)
{
	switch (gamespeed)
	{
		case KARTSPEED_EASY:
			return 68*mapobjectscale; // Avg Speed is 34
			break;
		case KARTSPEED_HARD:
			return 96*mapobjectscale; // Avg Speed is 48
			break;
		case KARTSPEED_EXPERT:
			return 110*mapobjectscale; // Avg Speed is 62
			break;
		default:
			return 82*mapobjectscale; // Avg Speed is 41
			break;
	}
}

static inline fixed_t K_GetSneakerBoostSpeed(void)
{
	switch (gamespeed)
	{
		case KARTSPEED_EASY:
			return EASYSNEAKERSPEEDBOOST;
			break;
		case KARTSPEED_HARD:
			return HARDSNEAKERSPEEDBOOST;
			break;
		case KARTSPEED_EXPERT:
			return EXPERTSNEAKERSPEEDBOOST;
			break;
		default:
			return NORMALSNEAKERSPEEDBOOST;
			break;
	}
}

// Used to determine the speed and power of Invincibility.
fixed_t K_InvincibilityGradient(UINT16 time)
{
    return (min(936, (fixed_t)time) * FRACUNIT / BASEINVINTIME);
}

static fixed_t K_InvincibilityEasing(fixed_t x)
{
    fixed_t u = max(FRACUNIT / 4, (min(32000, x) * FRACUNIT) / INVINDIST);

    if (x < INVINDIST)
        return u;

    u = max(0, (u - FRACUNIT));

    if (u < FRACUNIT)
        return Easing_InCubic(u, FRACUNIT, (INVINMIDTIME/10));

    return Easing_OutCubic(
        min(FRACUNIT, FixedMul(u - FRACUNIT, FRACUNIT/4)), (INVINMIDTIME/10), (INVINMAXTIME/10));
}


UINT16 K_GetInvincibilityTime(player_t *player)
{
    UINT32 i, pingame;

    if (K_GetKartInvinType() == KARTINVIN_LEGACY)
        return BASEINVINTIME;

    pingame = 0;
    for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator)
			continue;

		if (!(gametyperules & GTR_BUMPERS) || players[i].bumper)
			pingame++;

        if (pingame > 1) // We only want to see if one player is playing.
            continue;
    }

    if (pingame <= 1)
        return BASEINVINTIME;

    fixed_t clustermul = K_InvincibilityEasing(player->distancefromcluster);
    return FixedMul(BASEINVINTIME, clustermul);
}

static fixed_t K_GetInvincibilitySpeed(UINT16 time)
{
    if (K_GetKartInvinType() == KARTINVIN_LEGACY)
        return INVINSPEEDBOOSTLGC;

    fixed_t t = min(FRACUNIT, K_InvincibilityGradient(time));
    return Easing_OutCubic(t, 0, INVINSPEEDBOOSTALT);
}

static fixed_t K_GetInvincibilityAccel(UINT16 time)
{
    if (K_GetKartInvinType() == KARTINVIN_LEGACY)
        return INVINACCELBOOSTLGC;

    fixed_t t = min(FRACUNIT, K_InvincibilityGradient(time));
    return Easing_OutCubic(t, 0, INVINACCELBOOSTALT);
}

static fixed_t diminish(fixed_t speedboost)
{
	return FixedSqrt(speedboost + DIMINISHPARAM) - FixedSqrt(DIMINISHPARAM);
}

void K_DoBoost(player_t *player, fixed_t speedboost, fixed_t accelboost, boolean stack, boolean visible)
{
	if (stack && K_StackingActive())
	{
		player->boostinfo.stackspeedboost += speedboost;
	}

	player->boostinfo.nonstackspeedboost = max(player->boostinfo.nonstackspeedboost, speedboost);

	if (cv_kartstacking_accelstack.value && K_StackingActive())
	{
		player->boostinfo.accelboost += accelboost;
	}
	else
	{
		player->boostinfo.accelboost = max(player->boostinfo.accelboost, accelboost);
	}

	if (visible)
	{
		player->boostinfo.grade = CLAMP(player->boostinfo.grade+1, 0, stackingactive ? UINT8_MAX : 1);
	}
}

void K_ClearBoost(player_t *player)
{
	player->boostinfo.stackspeedboost = 0;
	player->boostinfo.nonstackspeedboost = 0;
	player->boostinfo.accelboost = 0;
	player->boostinfo.grade = 0;
}

static fixed_t K_RingDurationBoost(const player_t *player)
{
	fixed_t ret = FRACUNIT;

	if (K_PlayerUsesBotMovement(player))
	{
		// x1.125 for Lv. 9
		const fixed_t modifier = K_BotMapModifier();
		fixed_t add = (((player->botvars.difficulty-1) * modifier)/8) / (DIFFICULTBOT-1);

		ret += add;
	}

	if (player->botvars.rival == true)
	{
		// + x1.125 for Rival
		ret += FRACUNIT/8;
	}

	return ret;
}

static fixed_t K_DecaySlopeBoost(fixed_t prevslopeboost, INT32 brake)
{
	return max(prevslopeboost - (SLOPEDECAY + brake + prevslopeboost/60), 0);
}

static void K_DoSlopeBrake(player_t *player, fixed_t prevslopeboost, INT32 brake)
{
	player->slopeboost = K_DecaySlopeBoost(prevslopeboost, brake);
	player->slopeaccel = 0;
	player->prevslopeboost = player->slopeboost;
}

static void K_UpdateSlopeBoost(player_t *player)
{
	pslope_t *slope = player->mo->standingslope;
	boolean flip = player->mo->eflags & MFE_VERTICALFLIP;
	angle_t momangle = R_PointToAngle2(0, 0, player->mo->momx, player->mo->momy), hillangle = 0;
	fixed_t anglemult = 0, slopemult = 0, mult = 0;
	fixed_t addedboost = 0, addedaccel = 0;
	INT32 brake = 0;

	if (!K_SlopeBoostActive())
	{
		player->slopeboost = player->slopeaccel = player->prevslopeboost = 0;
		return;
	}

	if ((K_GetKartButtons(player) & BT_BRAKE) || player->offroad || !player->speed)
	{
		brake = SLOPEBRAKEMOD;
	}

	if (!slope || (player->offroad && K_ApplyOffroad(player)) || !player->speed)
	{
		K_DoSlopeBrake(player, player->prevslopeboost, brake);
		return;
	}

	if ((((int)slope->zangle > 0) && flip) || (((int)slope->zangle < 0) && (!flip)))
	{
		hillangle = momangle - slope->xydirection;
	}
	else
	{
		hillangle = momangle - (slope->xydirection + ANGLE_180);
	}

	hillangle = max(abs((int)hillangle) - ANG1*3, 0);  // ANG1*3 somehow fixes some slopes???

	if (hillangle >= ANGLE_90)
	{
		K_DoSlopeBrake(player, player->prevslopeboost, brake);
		return;
	}

	anglemult = FixedDiv(AngleFixed(ANGLE_90-hillangle), 90*FRACUNIT);
	slopemult = FixedDiv(AngleFixed(min(abs((int)slope->zangle)+ANG1*3, ANGLE_90)), 90*FRACUNIT);

	mult = FixedMul(anglemult, slopemult);

	addedboost = min(FixedMul(mult, SLOPESPEEDBOOSTMAX), SLOPESPEEDBOOSTCAP);
	addedaccel = FixedMul(mult, SLOPEACCELBOOST);

	if (K_GetKartButtons(player) & BT_BRAKE)
	{
		addedboost = K_DecaySlopeBoost(player->prevslopeboost, brake);
	}
	else if (addedboost >= player->prevslopeboost)
	{
		addedboost = min(addedboost, player->prevslopeboost+mult*5/100);
	}
	else
	{
		addedboost = max(K_DecaySlopeBoost(player->prevslopeboost, brake), addedboost);
	}

	player->slopeboost = addedboost;
	player->slopeaccel = addedaccel;

	player->prevslopeboost = addedboost;
}

static fixed_t K_BoostRescale(fixed_t value,fixed_t oldmin,fixed_t oldmax,fixed_t newmin,fixed_t newmax)
{
	return newmin + FixedMul(FixedDiv(value-oldmin , oldmax-oldmin), newmax-newmin);
}

// sets boostpower, speedboost and accelboost to whatever we need it to be
static void K_GetKartBoostPower(player_t *player)
{
	fixed_t boostpower = FRACUNIT;
	fixed_t finalspeedboost = 0;
	fixed_t finalaccelboost = 0;
	UINT8 finalgrade = 0;

	if (player->spinouttimer && player->wipeoutslow == 1) // Slow down after you've been bumped
	{
		player->boostpower = player->speedboost = player->accelboost = player->prevspeedboost = 0;
		K_ClearBoost(player);
		return;
	}

	// Offroad is separate, it's difficult to factor it in with a variable value anyway.
	if (K_ApplyOffroad(player) && player->offroad >= 0)
		boostpower = FixedDiv(boostpower, player->offroad + FRACUNIT);

	if (player->bananadrag > TICRATE)
		boostpower = (4*boostpower)/5;

	if (player->sneakertimer) // Sneaker
	{
		UINT8 i;
		fixed_t sneakerspeedboost = K_GetSneakerBoostSpeed();
		UINT8 numsneakers = player->numsneakers ? player->numsneakers : 1;

		for (i = 0; i < numsneakers; i++)
		{
			K_DoBoost(player, sneakerspeedboost, ACCELSTACK ? 0 : SNEAKERACCELBOOST, SNEAKERSTACKABLE, SNEAKERSTACKABLE); // + ???% top speed, + 800% acceleration
		}

		if (ACCELSTACK)
		{
			K_DoBoost(player, 0, SNEAKERACCELBOOST, true, false); // + 800% acceleration
		}
	}

	if (player->invincibilitytimer) // Invincibility
	{
        fixed_t invspeedboost = K_GetInvincibilitySpeed(player->invincibilitytimer);
        fixed_t invaccelboost = K_GetInvincibilityAccel(player->invincibilitytimer);
		K_DoBoost(player, invspeedboost, invaccelboost, INVINSTACKABLE, INVINSTACKABLE); // + 37.5% top speed, + 300% acceleration
	}

	if (player->growshrinktimer > 0) // Grow
	{
		K_DoBoost(player, GROWSPEEDBOOST, GROWACCELBOOST, GROWSTACKABLE, GROWSTACKABLE); // + 20% top speed, + 0% acceleration
	}

	if (player->flamestore) // Flame Shield dash
	{
		fixed_t dash = K_FlameShieldDashVar(player->flamedash);
		fixed_t intermediate = 0;
		fixed_t boost = 0;
		fixed_t val = FLAMESPEEDVAL; // Rim idea: diminish starts around 1.2x sneaker speed and plateaus around 1.4-1.5x

		intermediate = FixedDiv(FixedMul(val, FRACUNIT*-1/2) - FRACUNIT/4,-val+FRACUNIT/2);
		boost = FixedMul(val,(FRACUNIT-FixedDiv(FRACUNIT,(dash+intermediate))));

		K_DoBoost(player, boost, FLAMEACCELBOOST, FLAMESTACKABLE, FLAMESTACKABLE);
	}

	if (player->startboost) // Startup Boost
	{
		K_DoBoost(player, STARTSPEEDBOOST, STARTACCELBOOST, STARTSTACKABLE, STARTSTACKABLE); // + 25% top speed, + 600% acceleration
	}

	if (player->driftboost) // Drift Boost
	{
		K_DoBoost(player, DRIFTSPEEDBOOST, DRIFTACCELBOOST, DRIFTSTACKABLE, DRIFTSTACKABLE); // + 25% top speed, + 400% acceleration
	}

	if (player->ringboost) // Ring Boost
	{
		K_DoBoost(player, RINGSPEEDBOOST, RINGACCELBOOST, RINGSTACKABLE, RINGSTACKABLE); // + 20% top speed, + 400% acceleration
	}

	if (player->slopeboost || player->slopeaccel)
	{
		K_DoBoost(player, player->slopeboost, player->slopeaccel, SLOPESTACKABLE, false); // + ???% top speed, + 300% acceleration
	}

	// This should always remain the last boost before drafting
	if (player->botvars.rubberband > FRACUNIT && K_PlayerUsesBotMovement(player) == true)
	{
		//K_DoBoost(player, player->botvars.rubberband - FRACUNIT, 0, false, false);
		//Always stack this boost....
		player->boostinfo.stackspeedboost += player->botvars.rubberband - FRACUNIT;
	}

	if (player->draftpower > 0) // Drafting
	{
		fixed_t draftspeed = K_BoostRescale(player->kartspeed*FRACUNIT, 9*FRACUNIT, FRACUNIT, DRAFTMINSPEED, DRAFTMAXSPEED);
		fixed_t bonusaccel = K_BoostRescale(player->kartspeed*FRACUNIT, 4*FRACUNIT, 9*FRACUNIT, 0, FRACUNIT/2);

		player->boostinfo.stackspeedboost += FixedMul(draftspeed, player->draftpower);
		player->boostinfo.nonstackspeedboost = max(player->boostinfo.nonstackspeedboost, FixedMul(draftspeed, player->draftpower));

		// Now for a small amount of bonus accel for higher speeds.
		if (player->draftpower >= FRACUNIT && player->kartspeed > 4)
		{
			// CONS_Printf("bonusaccel: %d\n", bonusaccel);
			if (cv_kartstacking_accelstack.value && K_StackingActive())
			{
				player->boostinfo.accelboost += bonusaccel;
			}
			else
			{
				player->boostinfo.accelboost = max(player->boostinfo.accelboost, bonusaccel);
			}
		}

		player->boostinfo.grade++;
	}

	player->boostpower = boostpower;

	// Combine nonstack and stacking boosts here.
	finalspeedboost = max(player->boostinfo.nonstackspeedboost, diminish(player->boostinfo.stackspeedboost));
	finalaccelboost = player->boostinfo.accelboost;
	finalgrade = player->boostinfo.grade;

	// value smoothing
	if (K_StackingActive())
	{
		if (player->offroad && K_ApplyOffroad(player))
		{
			player->speedboost = max(finalspeedboost, player->speedboost)/2;
			player->accelboost = max(finalaccelboost, player->accelboost)/2;
		}
		else if (K_Sliptiding(player) || (K_GetKartButtons(player) & BT_BRAKE))
		{
			player->speedboost = max(player->prevspeedboost - SPEEDBOOSTDROPOFF_BRAKE, min(player->speedboost, MAXVANILLABOOST));
			player->accelboost = finalaccelboost;
		}
		else if (finalspeedboost >= player->prevspeedboost)
		{
			player->speedboost = max(player->speedboost, finalspeedboost);
			player->accelboost = max(player->accelboost, finalaccelboost);
		}
		else
		{
			player->speedboost = max(player->speedboost + (finalspeedboost - player->speedboost) / (TICRATE/2), player->speedboost - SPEEDBOOSTDROPOFF);
			player->accelboost = finalaccelboost;
		}
	}
	else
	{
		if (finalspeedboost > player->speedboost)
		{
			player->speedboost = finalspeedboost;
		}
		else
		{
			player->speedboost += (finalspeedboost - player->speedboost) / (TICRATE/2);
		}

		player->accelboost = finalaccelboost;
	}

	player->numboosts = finalgrade;
	K_ClearBoost(player);
	player->prevspeedboost = player->speedboost;
}

// Returns value based on being Grown or Shrunk otherwise returns FRACUNIT
static fixed_t K_GrowShrinkOrNormal(const player_t *player)
{
	fixed_t scaleDiff = player->mo->scale - mapobjectscale;
	fixed_t playerScale = FixedDiv(player->mo->scale, mapobjectscale);
	fixed_t speedMul = FRACUNIT;

	if (scaleDiff > 0 || scaleDiff < 0)
	{
		// Grown or Shrunk
		speedMul = playerScale;
	}

	return speedMul;
}

// Returns kart speed from a stat. Boost power and scale are NOT taken into account, no player or object is necessary.
fixed_t K_GetKartSpeedFromStat(UINT8 kartspeed, boolean karmabomb)
{
	const fixed_t xspd = (3*FRACUNIT)/64;
	fixed_t g_cc = K_GetKartGameSpeedScalar(gamespeed) + xspd;
	fixed_t k_speed = 150;
	fixed_t finalspeed;

	if (karmabomb)
		kartspeed = 1;

	k_speed += kartspeed*3; // 153 - 177

	finalspeed = FixedMul(k_speed<<14, g_cc);
	return finalspeed;
}

fixed_t K_GetKartSpeed(const player_t *player, boolean doboostpower, boolean dorubberband)
{
	boolean karmabomb = ((gametyperules & GTR_KARMA) && player->bumper <= 0);
	fixed_t finalspeed;

	if (doboostpower && !player->pogospring && !P_IsObjectOnGround(player->mo))
		return (75*mapobjectscale); // air speed cap

	finalspeed = K_GetKartSpeedFromStat(player->kartspeed, karmabomb);

	if (K_PlayerUsesBotMovement(player))
	{
		// Increase bot speed by 1-10% depending on difficulty
		const fixed_t modifier = K_BotMapModifier();
		fixed_t add = ((player->botvars.difficulty-1) * FixedMul(FRACUNIT / 10, modifier)) / (DIFFICULTBOT-1);
		finalspeed = FixedMul(finalspeed, FRACUNIT + add);
	}

	finalspeed = FixedMul(finalspeed, player->mo->scale);

	if (dorubberband == true && player->botvars.rubberband < FRACUNIT && K_PlayerUsesBotMovement(player) == true)
	{
		finalspeed = FixedMul(finalspeed, player->botvars.rubberband);
	}

	if (doboostpower)
	{
		finalspeed = FixedMul(finalspeed, player->boostpower+player->speedboost);
	}

	if (player->outrun != 0)
	{
		// Milky Way's roads
		finalspeed += FixedMul(player->outrun, K_GrowShrinkOrNormal(player));
	}


	return finalspeed;
}

fixed_t K_GetKartAccel(const player_t *player)
{
	fixed_t k_accel = 32; // 36;
	UINT8 kartspeed = player->kartspeed;

	if ((gametyperules & GTR_KARMA) && player->bumper <= 0)
		kartspeed = 1;

	k_accel += 4 * (9 - kartspeed); // 32 - 64

	return FixedMul(k_accel, FRACUNIT+player->accelboost);
}

UINT16 K_GetKartFlashing(const player_t *player)
{
	UINT16 tics = flashingtics;

	if (player == NULL)
	{
		return tics;
	}

	tics += (tics/8) * (player->kartspeed);
	return tics;
}

boolean K_PlayerShrinkCheat(const player_t *player)
{
	if (cv_kartdebugshrink.value)
		return true;

	return (
		(player->pflags & PF_SHRINKACTIVE)
		&& (player->bot == false)
		&& (modeattacking == false) // Anyone want to make another record attack category?
	);
}

void K_UpdateShrinkCheat(player_t *player)
{
	const boolean mobjValid = (player->mo != NULL && P_MobjWasRemoved(player->mo) == false);

	if (player->pflags & PF_SHRINKME)
	{
		player->pflags |= PF_SHRINKACTIVE;
	}
	else
	{
		player->pflags &= ~PF_SHRINKACTIVE;
	}

	if (mobjValid == true && K_PlayerShrinkCheat(player) == true)
	{
		player->mo->destscale = FixedMul(mapobjectscale, SHRINK_SCALE);
	}
}

boolean K_KartKickstart(const player_t *player)
{
	return ((player->pflags & PF_KICKSTARTACCEL)
		&& (!K_PlayerUsesBotMovement(player))
		&& (player->kickstartaccel >= ACCEL_KICKSTART));
}

UINT16 K_GetKartButtons(const player_t *player)
{
	return (player->cmd.buttons |
		(K_KartKickstart(player) ? BT_ACCELERATE : 0));
}

SINT8 K_GetForwardMove(const player_t *player)
{
	SINT8 forwardmove = player->cmd.forwardmove;

	if (K_KartKickstart(player)) // unlike the brute forward of sneakers, allow for backwards easing here
	{
		if (player->cmd.buttons & BT_BRAKE)
		{
			// Allow v1 style clutching by holding brake.
			forwardmove = MAXPLMOVE/2;
		}
		else
		{
			// Continue as usual.
			forwardmove = MAXPLMOVE;
		}
	}

	if ((player->exiting || mapreset) ||
		player->pflags & PF_STASIS ||
		player->spinouttimer ||
		player->flipovertimer) // pw_introcam?
	{
		forwardmove = 0;
		if (player->sneakertimer)
			forwardmove = MAXPLMOVE;
	}

	return forwardmove;
}

SINT8 K_GetSideMove(const player_t *player)
{
	SINT8 sidemove = player->cmd.sidemove;

	if ((player->exiting || mapreset) ||
		player->pflags & PF_STASIS ||
		player->spinouttimer ||
		player->flipovertimer) // pw_introcam?
	{
		sidemove = 0;
	}

	if (!player->pogospring)
	{
		sidemove = 0;
	}

	return sidemove;
}

fixed_t K_GetNewSpeed(const player_t *player)
{
	const fixed_t accelmax = 4000;
	const fixed_t p_speed = K_GetKartSpeed(player, true, true);
	fixed_t p_accel = K_GetKartAccel(player);
	fixed_t newspeed, oldspeed, finalspeed;

	if (K_PlayerUsesBotMovement(player) == true && player->botvars.rubberband > 0)
	{
		// Acceleration is tied to top speed...
		// so if we want JUST a top speed boost, we have to do this...
		p_accel = FixedDiv(p_accel, player->botvars.rubberband);
	}

	oldspeed = R_PointToDist2(0, 0, player->rmomx, player->rmomy);
	newspeed = FixedDiv(FixedDiv(FixedMul(oldspeed, accelmax - p_accel) + FixedMul(p_speed, p_accel), accelmax), K_PlayerBaseFriction(player, ORIG_FRICTION));

	if (player->pogospring) // Pogo Spring minimum/maximum thrust
	{
		const fixed_t hscale = mapobjectscale;
		fixed_t minspeed = 24*hscale;
		fixed_t maxspeed = 28*hscale;

		if (player->mo->terrain && K_AffectingTerrainActive())
		{
			minspeed = player->mo->terrain->pogoSpringMin*hscale;
			maxspeed = player->mo->terrain->pogoSpringMax*hscale;
		}

		if (newspeed > maxspeed && player->pogospring == 2)
			newspeed = maxspeed;
		if (newspeed < minspeed)
			newspeed = minspeed;
	}

	finalspeed = newspeed - oldspeed;

	return finalspeed;
}

fixed_t K_3dKartMovement(const player_t *player, boolean onground, SINT8 forwardmove)
{
	fixed_t finalspeed = K_GetNewSpeed(player);

	if (!onground) return 0; // If the player isn't on the ground, there is no change in speed

	// forwardmove is:
	//  50 while accelerating,
	//  25 while clutching,
	//   0 with no gas, and
	// -25 when only braking.

	finalspeed *= forwardmove/25;
	finalspeed /= 2;

	if (forwardmove < 0 && finalspeed > mapobjectscale*2)
		return finalspeed/2;
	else if (forwardmove < 0)
		return -mapobjectscale/2;

	if (finalspeed < 0)
		finalspeed = 0;

	return finalspeed;
}

angle_t K_MomentumAngle(mobj_t *mo)
{
	if (FixedHypot(mo->momx, mo->momy) >= mo->scale)
	{
		return R_PointToAngle2(0, 0, mo->momx, mo->momy);
	}
	else
	{
		return mo->angle; // default to facing angle, rather than 0
	}
}

void K_AwardPlayerRings(player_t *player, UINT16 rings, boolean overload)
{
	UINT16 superring;

	if (!overload)
	{
		INT32 totalrings =
			RINGTOTAL(player) + (player->superring);

		/* capped at 20 rings */
		if ((totalrings + rings) > player->ringmax)
		{
			if (totalrings >= player->ringmax)
				return; // woah dont let that go negative buster
			rings = (player->ringmax - totalrings);
		}
	}

	superring = player->superring + rings;

	/* check if not overflow */
	if (superring > player->superring)
		player->superring = superring;
}

static INT32 K_GetRaceSplitsToggle(void)
{
	if (cv_showlapemblem.value < 2)
	{
		// Splits aren't on.
		return 0;
	}

	// Not taking any chances with this stupid goddamn engine.
	return max((INT32)(cv_racesplits.value), 1);
}

static void K_SetupSplitForPlayer(player_t *us, player_t *them, tic_t ourtime, tic_t theirtime)
{
	us->karthud[khud_splittimer] = 3*TICRATE;

	INT32 delta = (INT32)theirtime - (INT32)ourtime; 	// how ahead are we? bigger number = more ahead, negative = behind
	us->karthud[khud_splittime] = -1 * delta; 			// (HUD expects this to be backwards, but this is how i felt today!)

	INT32 winning = 0;

	if (delta > 0)
		winning = 2; // winning aid gaining
	else if (delta < 0)
		winning = -2; // behind and falling

	if (winning > 0 && delta < us->pace)
		winning = 1; // winning but falling
	else if (winning < 0 && delta > us->pace)
		winning = -1; // behind but gaiming

	us->pace = delta;

	us->karthud[khud_splitwin] = winning;
	us->karthud[khud_splitskin] = them->skin;
	us->karthud[khud_splitcolor] = them->skincolor;
	if (us->position != 1)
		us->karthud[khud_splitposition] = them->position;
	else
		us->karthud[khud_splitposition] = 0;
}

void K_HandleRaceSplits(player_t *player, tic_t time, UINT8 checkpoint)
{
	if (checkpoint >= MAXRACESPLITS)
		return;

	const INT32 splitstoggle = K_GetRaceSplitsToggle();

	player->splits[checkpoint] = time;

	player_t *lowest = player;
	player_t *next = player;
	UINT8 numrealsplits = 0;

	// find fastest player for this checkpoint and # players who have already crossed
	for (UINT8 i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i])
			continue;

		player_t *check = &players[i];

		if (check == player)
			continue;
		if (check->spectator)
			continue;
		if (check->splits[checkpoint] == 0)
			continue;

		numrealsplits++;

		if (check->splits[checkpoint] < lowest->splits[checkpoint])
			lowest = check;

		if (check->splits[checkpoint] > next->splits[checkpoint] || next == player)
			next = check;
	}

	// no one to compare against yet
	if (lowest == player || numrealsplits == 0)
		return;

	// if there's exactly one player ahead of us, they need a blue split generated
	// so they can see how far behind we are
	if (lowest != player && numrealsplits == 1)
	{
		K_SetupSplitForPlayer(lowest, player, lowest->splits[checkpoint], player->splits[checkpoint]);
	}

	if (numrealsplits && splitstoggle)
	{
		player_t *target = (splitstoggle == 2) ? lowest : next;
		K_SetupSplitForPlayer(player, target, player->splits[checkpoint], target->splits[checkpoint]);
	}
}

void K_DoInstashield(player_t *player)
{
	mobj_t *layera;
	mobj_t *layerb;

	if (player->instashield > 0)
		return;

	player->instashield = 15; // length of instashield animation
	S_StartSound(player->mo, sfx_cdpcm9);

	layera = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_INSTASHIELDA);
	layera->old_x = player->mo->old_x;
	layera->old_y = player->mo->old_y;
	layera->old_z = player->mo->old_z;
	P_SetTarget(&layera->target, player->mo);

	layerb = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_INSTASHIELDB);
	layerb->old_x = player->mo->old_x;
	layerb->old_y = player->mo->old_y;
	layerb->old_z = player->mo->old_z;
	P_SetTarget(&layerb->target, player->mo);
}

void K_BattleAwardHit(player_t *player, player_t *victim, mobj_t *inflictor, UINT8 bumpersRemoved)
{
	UINT8 points = 1;
	boolean trapItem = false;

	if (player == NULL || victim == NULL)
	{
		// Invalid player or victim
		return;
	}

	if (player == victim)
	{
		// You cannot give yourself points
		return;
	}

	if ((inflictor && !P_MobjWasRemoved(inflictor)) && (inflictor->type == MT_BANANA && inflictor->health > 1))
	{
		trapItem = true;
	}

	// Only apply score bonuses to non-bananas
	if (trapItem == false)
	{
		if (K_IsPlayerWanted(victim))
		{
			// +3 points for hitting a wanted player
			points = 3;
		}
		else if (gametyperules & GTR_BUMPERS)
		{
			if ((victim->bumper > 0) && (victim->bumper <= bumpersRemoved))
			{
				// +2 points for finishing off a player
				points = 2;
			}
		}
	}

	if (gametyperules & GTR_POINTLIMIT)
	{
		P_AddPlayerScore(player, points);
		K_SpawnBattlePoints(player, victim, points);
	}
}

void K_SpinPlayer(player_t *player, mobj_t *inflictor, mobj_t *source, INT32 type)
{
	(void)inflictor;
	(void)source;

	K_DirectorFollowAttack(player, inflictor, source);

	player->spinouttype = type;

	if (( player->spinouttype & KSPIN_THRUST ))
	{
		// At spinout, player speed is increased to 1/4 their regular speed, moving them forward
		fixed_t spd = K_GetKartSpeed(player, true, true) / 4;

		if (player->speed < spd)
			P_InstaThrust(player->mo, player->mo->angle, FixedMul(spd, player->mo->scale));

		S_StartSound(player->mo, sfx_slip);
	}

	K_StatPlayerHit(player, source ? source->player : NULL);
	player->spinouttimer = (3*TICRATE/2)+2;
	P_SetPlayerMobjState(player->mo, S_KART_SPINOUT);
}

void K_FlipPlayer(player_t *player, mobj_t *inflictor, mobj_t *source)
{
	K_DirectorFollowAttack(player, inflictor, source);

	K_StatPlayerHit(player, source ? source->player : NULL);
	player->flipovertimer = 1;

	{
		fixed_t flipoveradd = R_PointToDist2(0, 0, player->rmomx, player->rmomy);

		S_StartSound(player->mo, sfx_flipos);

		player->flipoverangle = K_MomentumAngle(player->mo)-ANG15;

		if (!inflictor)
		{
			// This is probably a damage sector so lets not make this feel like ass.
			// Also don't softlock.
			P_InstaThrust(player->mo, player->flipoverangle, FixedMul(FLIPOVERSPEED+flipoveradd+(5*FRACUNIT), player->mo->scale));
		}
		else
		{
			P_InstaThrust(player->mo, player->flipoverangle, FixedMul(FLIPOVERSPEED+(flipoveradd/8), player->mo->scale));
		}

		player->mo->momz = FixedMul(FlipOverZMomentum(gravity), player->mo->scale);
		player->pflags |= PF_JUSTFLIPPED;
	}

	P_SetPlayerMobjState(player->mo, S_KART_SPINOUT);
}

static void K_RemoveGrowShrink(player_t *player)
{
	if (player->mo && !P_MobjWasRemoved(player->mo))
	{
		if (player->growshrinktimer > 0) // Play Shrink noise
			S_StartSound(player->mo, sfx_kc59);
		else if (player->growshrinktimer < 0) // Play Grow noise
			S_StartSound(player->mo, sfx_kc5a);

		K_KartResetPlayerColor(player);

		player->mo->scalespeed = mapobjectscale/TICRATE;
		player->mo->destscale = mapobjectscale;

		if (K_PlayerShrinkCheat(player) == true)
		{
			player->mo->destscale = FixedMul(player->mo->destscale, SHRINK_SCALE);
		}
	}

	player->growshrinktimer = 0;
	player->growcancel = -1;

	P_RestoreMusic(player);
}

void K_SquishPlayer(player_t *player, mobj_t *inflictor, mobj_t *source)
{
	(void)inflictor;
	(void)source;

	player->squishedtimer = TICRATE;

	// Reduce Shrink timer
	if (player->growshrinktimer < 0)
	{
		player->growshrinktimer += TICRATE;
		if (player->growshrinktimer >= 0)
			K_RemoveGrowShrink(player);
	}

	player->mo->flags |= MF_NOCLIP;

	player->instashield = 15;
}

void K_ApplyTripWire(player_t *player, tripwirestate_t state)
{
	// We are either softlocked or wildly misbehaving. Stop that!
	if (state == TRIPSTATE_BLOCKED && player->tripwireReboundDelay && (player->speed > 5 * K_GetKartSpeed(player, false, false)))
		P_DamageMobj(player->mo, NULL, NULL, 1, DMG_NORMAL);

	if (state == TRIPSTATE_PASSED)
	{
		//S_StartSound(player->mo, sfx_ssa015);
		player->tripwireLeniency += TICRATE/2;
	}
	else if (state == TRIPSTATE_BLOCKED)
	{
		S_StartSound(player->mo, sfx_kc40);
		player->tripwireReboundDelay = 60;
	}

	player->tripwireState = state;
}

INT32 K_ExplodePlayer(player_t *player, mobj_t *inflictor, mobj_t *source) // A bit of a hack, we just throw the player up higher here and extend their spinout timer
{
	INT32 ringburst = 5;

	(void)source;

	K_DirectorFollowAttack(player, inflictor, source);

	player->mo->momz = 18*mapobjectscale*P_MobjFlip(player->mo); // please stop forgetting mobjflip checks!!!!
	if (player->mo->eflags & MFE_UNDERWATER)
		player->mo->momz = (117 * player->mo->momz) / 200;
	player->mo->momx = player->mo->momy = 0;

	player->spinouttype = KSPIN_EXPLOSION;
	player->spinouttimer = (3*TICRATE/2)+2;

	if (inflictor && !P_MobjWasRemoved(inflictor))
	{
		if (inflictor->type == MT_SPBEXPLOSION && inflictor->extravalue1)
		{
			player->spinouttimer = ((5*player->spinouttimer)/2)+1;
			player->mo->momz *= 2;
			ringburst = 10;
		}

		if (inflictor->type == MT_LANDMINE)
		{
			player->spinouttimer = (((5*player->spinouttimer)/2)+1)/4;
			player->mo->momz = FixedDiv(player->mo->momz, FRACUNIT + FRACUNIT/2);
		}
	}

	P_SetPlayerMobjState(player->mo, S_KART_SPINOUT);

	if (P_IsDisplayPlayer(player))
		P_StartQuake(64<<FRACBITS, 5);

	return ringburst;
}

boolean K_IsPlayerDamaged(const player_t *player)
{
	return ((player->spinouttimer > 0) || (player->flipovertimer > 0));
}

void K_HandleBumperChanges(player_t *player, UINT8 prevBumpers)
{
	if (!(gametyperules & GTR_BUMPERS))
	{
		// Bumpers aren't being used
		return;
	}

	// TODO: replace all console text print-outs with a real visual

	if (player->bumper > 0 && prevBumpers == 0)
	{
		if (netgame)
		{
			CONS_Printf(M_GetText("%s is back in the game!\n"), player_names[player-players]);
		}
	}
	else if (player->bumper == 0 && prevBumpers > 0)
	{
		mobj_t *karmahitbox = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_KARMAHITBOX);
		P_SetTarget(&karmahitbox->target, player->mo);

		karmahitbox->destscale = player->mo->destscale;
		P_SetScale(karmahitbox, player->mo->scale);

		player->karmadelay = comebacktime;

		if (bossinfo.boss)
		{
			P_DoTimeOver(player);
		}
		else if (netgame)
		{
			CONS_Printf(M_GetText("%s lost all of their bumpers!\n"), player_names[player-players]);
		}
	}
	if (K_IsPlayerWanted(player))
		K_CalculateBattleWanted();
	K_CheckBumpers();
}

void K_DestroyBumpers(player_t *player, UINT8 amount)
{
	UINT8 oldBumpers = player->bumper;

	if (!(gametyperules & GTR_BUMPERS))
	{
		return;
	}

	amount = min(amount, player->bumper);

	if (amount == 0)
	{
		return;
	}

	player->bumper -= amount;
	K_HandleBumperChanges(player, oldBumpers);
}

void K_TakeBumpersFromPlayer(player_t *player, player_t *victim, UINT8 amount)
{
	UINT8 oldPlayerBumpers = player->bumper;
	UINT8 oldVictimBumpers = victim->bumper;

	UINT8 tookBumpers = 0;

	if (!(gametyperules & GTR_BUMPERS))
	{
		return;
	}

	amount = min(amount, victim->bumper);

	if (amount == 0)
	{
		return;
	}

	while ((tookBumpers < amount) && (victim->bumper > 0))
	{
		UINT8 newbumper = player->bumper;

		angle_t newangle, diff;
		fixed_t newx, newy;

		mobj_t *newmo;

		if (newbumper <= 1)
		{
			diff = 0;
		}
		else
		{
			diff = FixedAngle(360*FRACUNIT/newbumper);
		}

		newangle = player->mo->angle;
		newx = player->mo->x + P_ReturnThrustX(player->mo, newangle + ANGLE_180, 64*FRACUNIT);
		newy = player->mo->y + P_ReturnThrustY(player->mo, newangle + ANGLE_180, 64*FRACUNIT);

		newmo = P_SpawnMobj(newx, newy, player->mo->z, MT_BATTLEBUMPER);
		newmo->threshold = newbumper;

		P_SetTarget(&newmo->tracer, victim->mo);
		P_SetTarget(&newmo->target, player->mo);

		newmo->angle = (diff * (newbumper-1));
		newmo->color = victim->skincolor;

		if (newbumper+1 < 2)
		{
			P_SetMobjState(newmo, S_BATTLEBUMPER3);
		}
		else if (newbumper+1 < 3)
		{
			P_SetMobjState(newmo, S_BATTLEBUMPER2);
		}
		else
		{
			P_SetMobjState(newmo, S_BATTLEBUMPER1);
		}

		player->bumper++;
		player->karmapoints = 0;
		victim->bumper--;
		tookBumpers++;
	}

	if (tookBumpers == 0)
	{
		// No change occured.
		return;
	}

	// Play steal sound
	S_StartSound(player->mo, sfx_3db06);

	K_HandleBumperChanges(player, oldPlayerBumpers);
	K_HandleBumperChanges(victim, oldVictimBumpers);
}

// source is the mobj that originally threw the bomb that exploded etc.
// Spawns the sphere around the explosion that handles spinout
void K_SpawnKartExplosion(fixed_t x, fixed_t y, fixed_t z, fixed_t radius, INT32 number, mobjtype_t type, angle_t rotangle, boolean spawncenter, boolean ghostit, mobj_t *source)
{
	mobj_t *mobj;
	mobj_t *ghost = NULL;
	INT32 i;
	TVector v;
	TVector *res;
	fixed_t finalx, finaly, finalz, dist;
	//mobj_t hoopcenter;
	angle_t degrees, fa, closestangle;
	fixed_t mobjx, mobjy, mobjz;

	//hoopcenter.x = x;
	//hoopcenter.y = y;
	//hoopcenter.z = z;

	//hoopcenter.z = z - mobjinfo[type].height/2;

	degrees = FINEANGLES/number;

	closestangle = 0;

	// Create the hoop!
	for (i = 0; i < number; i++)
	{
		fa = (i*degrees);
		v[0] = FixedMul(FINECOSINE(fa),radius);
		v[1] = 0;
		v[2] = FixedMul(FINESINE(fa),radius);
		v[3] = FRACUNIT;

		res = VectorMatrixMultiply(v, *RotateXMatrix(rotangle));
		M_Memcpy(&v, res, sizeof (v));
		res = VectorMatrixMultiply(v, *RotateZMatrix(closestangle));
		M_Memcpy(&v, res, sizeof (v));

		finalx = x + v[0];
		finaly = y + v[1];
		finalz = z + v[2];

		mobj = P_SpawnMobj(finalx, finaly, finalz, type);

		mobj->z -= mobj->height>>1;

		// change angle
		mobj->angle = R_PointToAngle2(mobj->x, mobj->y, x, y);

		// change slope
		dist = P_AproxDistance(P_AproxDistance(x - mobj->x, y - mobj->y), z - mobj->z);

		if (dist < 1)
			dist = 1;

		mobjx = mobj->x;
		mobjy = mobj->y;
		mobjz = mobj->z;

		if (ghostit)
		{
			ghost = P_SpawnGhostMobj(mobj);
			P_SetMobjState(mobj, S_NULL);
			mobj = ghost;
		}

		if (spawncenter)
		{
			mobj->x = x;
			mobj->y = y;
			mobj->z = z;
		}

		mobj->momx = FixedMul(FixedDiv(mobjx - x, dist), FixedDiv(dist, 6*FRACUNIT));
		mobj->momy = FixedMul(FixedDiv(mobjy - y, dist), FixedDiv(dist, 6*FRACUNIT));
		mobj->momz = FixedMul(FixedDiv(mobjz - z, dist), FixedDiv(dist, 6*FRACUNIT));

		if (source && !P_MobjWasRemoved(source))
			P_SetTarget(&mobj->target, source);
	}
}

#define MINEQUAKEDIST 4096

// Spawns the purely visual explosion
void K_SpawnMineExplosion(mobj_t *source, UINT8 color)
{
	INT32 i, radius, height;
	mobj_t *smoldering = P_SpawnMobj(source->x, source->y, source->z, MT_SMOLDERING);
	mobj_t *dust;
	mobj_t *truc;
	INT32 speed, speed2;
	fixed_t rand_x;
	fixed_t rand_y;
	fixed_t rand_z;

	K_MatchGenericExtraFlags(smoldering, source);
	smoldering->tics = TICRATE*3;
	radius = source->radius>>FRACBITS;
	height = source->height>>FRACBITS;

	if (!color)
		color = SKINCOLOR_KETCHUP;

	for (i = 0; i < 32; i++)
	{
		dust = P_SpawnMobj(source->x, source->y, source->z, MT_SMOKE);
		P_SetMobjState(dust, S_OPAQUESMOKE1);
		dust->angle = (ANGLE_180/16) * i;
		P_SetScale(dust, source->scale);
		dust->destscale = source->scale*10;
		dust->scalespeed = source->scale/12;
		P_InstaThrust(dust, dust->angle, FixedMul(20*FRACUNIT, source->scale));

		rand_z = source->z + P_RandomRange(0, height)*FRACUNIT;
		rand_y = source->y + P_RandomRange(-radius, radius)*FRACUNIT;
		rand_x = source->x + P_RandomRange(-radius, radius)*FRACUNIT;

		truc = P_SpawnMobj(rand_x, rand_y, rand_z, MT_BOOMEXPLODE);
		K_MatchGenericExtraFlags(truc, source);
		P_SetScale(truc, source->scale);
		truc->destscale = source->scale*6;
		truc->scalespeed = source->scale/12;
		speed = FixedMul(10*FRACUNIT, source->scale)>>FRACBITS;
		truc->momx = P_RandomRange(-speed, speed)*FRACUNIT;
		truc->momy = P_RandomRange(-speed, speed)*FRACUNIT;
		speed = FixedMul(20*FRACUNIT, source->scale)>>FRACBITS;
		truc->momz = P_RandomRange(-speed, speed)*FRACUNIT*P_MobjFlip(truc);
		if (truc->eflags & MFE_UNDERWATER)
			truc->momz = (117 * truc->momz) / 200;
		truc->color = color;
	}

	for (i = 0; i < 16; i++)
	{
		rand_z = source->z + P_RandomRange(0, height)*FRACUNIT;
		rand_y = source->y + P_RandomRange(-radius, radius)*FRACUNIT;
		rand_x = source->x + P_RandomRange(-radius, radius)*FRACUNIT;

		dust = P_SpawnMobj(rand_x, rand_y, rand_z, MT_SMOKE);
		P_SetMobjState(dust, S_OPAQUESMOKE1);
		P_SetScale(dust, source->scale);
		dust->destscale = source->scale*10;
		dust->scalespeed = source->scale/12;
		dust->tics = 30;
		dust->momz = P_RandomRange(FixedMul(3*FRACUNIT, source->scale)>>FRACBITS, FixedMul(7*FRACUNIT, source->scale)>>FRACBITS)*FRACUNIT;

		rand_z = source->z + P_RandomRange(0, height)*FRACUNIT;
		rand_y = source->y + P_RandomRange(-radius, radius)*FRACUNIT;
		rand_x = source->x + P_RandomRange(-radius, radius)*FRACUNIT;

		truc = P_SpawnMobj(rand_x, rand_y, rand_z, MT_BOOMPARTICLE);
		K_MatchGenericExtraFlags(truc, source);
		P_SetScale(truc, source->scale);
		truc->destscale = source->scale*5;
		truc->scalespeed = source->scale/12;
		speed = FixedMul(20*FRACUNIT, source->scale)>>FRACBITS;
		truc->momx = P_RandomRange(-speed, speed)*FRACUNIT;
		truc->momy = P_RandomRange(-speed, speed)*FRACUNIT;
		speed = FixedMul(15*FRACUNIT, source->scale)>>FRACBITS;
		speed2 = FixedMul(45*FRACUNIT, source->scale)>>FRACBITS;
		truc->momz = P_RandomRange(speed, speed2)*FRACUNIT*P_MobjFlip(truc);
		if (P_RandomChance(FRACUNIT/2))
			truc->momz = -truc->momz;
		if (truc->eflags & MFE_UNDERWATER)
			truc->momz = (117 * truc->momz) / 200;
		truc->tics = TICRATE*2;
		truc->color = color;
	}
}

static mobj_t *K_SpawnKartMissile(mobj_t *source, mobjtype_t type, angle_t an, INT32 flags2, fixed_t speed)
{
	mobj_t *th;
	fixed_t x, y, z;
	fixed_t finalspeed = speed;
	mobj_t *throwmo;

	if (source->player && source->player->speed > K_GetKartSpeed(source->player, false, false))
	{
		angle_t input = source->angle - an;
		boolean invert = (input > ANGLE_180);
		if (invert)
			input = InvAngle(input);

		finalspeed = max(speed, FixedMul(speed, FixedMul(
			FixedDiv(source->player->speed, K_GetKartSpeed(source->player, false, false)), // Multiply speed to be proportional to your own, boosted maxspeed.
			(((180<<FRACBITS) - AngleFixed(input)) / 180) // multiply speed based on angle diff... i.e: don't do this for firing backward :V
			)));
	}

	x = source->x + source->momx + FixedMul(finalspeed, FINECOSINE(an>>ANGLETOFINESHIFT));
	y = source->y + source->momy + FixedMul(finalspeed, FINESINE(an>>ANGLETOFINESHIFT));
	z = source->z; // spawn on the ground please

	if (P_MobjFlip(source) < 0)
	{
		z = source->z+source->height - mobjinfo[type].height;
	}

	th = P_SpawnMobj(x, y, z, type);

	th->flags2 |= flags2;

	if (source->flags2 & MF2_WATERRUN)
	{
		// Allow certain items to run on water as well!
		if (K_ItemMobjAllowedtoWaterRun(th))
		{
			th->flags2 |= MF2_WATERRUN;
		}
	}

	th->threshold = 10;

	if (th->info->seesound)
		S_StartSound(source, th->info->seesound);

	P_SetTarget(&th->target, source);

	if (P_IsObjectOnGround(source))
	{
		// floorz and ceilingz aren't properly set to account for FOFs and Polyobjects on spawn
		// This should set it for FOFs
		P_SetOrigin(th, th->x, th->y, th->z);
		// spawn on the ground if the player is on the ground
		if (P_MobjFlip(source) < 0)
		{
			th->z = th->ceilingz - th->height;
			th->eflags |= MFE_VERTICALFLIP;
		}
		else
			th->z = th->floorz;
	}

	th->angle = an;

	th->momx = FixedMul(finalspeed, FINECOSINE(an>>ANGLETOFINESHIFT));
	th->momy = FixedMul(finalspeed, FINESINE(an>>ANGLETOFINESHIFT));

	switch (type)
	{
		case MT_ORBINAUT:
			if (source && source->player)
				th->color = source->player->skincolor;
			else
				th->color = SKINCOLOR_GREY;
			th->movefactor = finalspeed;
			break;
		case MT_JAWZ:
			if (source && source->player)
			{
				INT32 lasttarg = source->player->lastjawztarget;
				th->cvmem = source->player->skincolor;
				if ((lasttarg >= 0 && lasttarg < MAXPLAYERS)
					&& playeringame[lasttarg]
					&& !players[lasttarg].spectator
					&& players[lasttarg].mo)
				{
					P_SetTarget(&th->tracer, players[lasttarg].mo);
				}
			}
			else
				th->cvmem = SKINCOLOR_KETCHUP;
			/* FALLTHRU */
		case MT_JAWZ_DUD:
			S_StartSound(th, th->info->activesound);
			/* FALLTHRU */
		case MT_SPB:
			th->movefactor = finalspeed;
			break;
		case MT_BUBBLESHIELDTRAP:
			P_SetScale(th, ((5*th->destscale)>>2)*4);
			th->destscale = (5*th->destscale)>>2;
			S_StartSound(th, sfx_s3kbfl);
			S_StartSound(th, sfx_cdfm35);
			break;
		default:
			break;
	}

	if (type != MT_BUBBLESHIELDTRAP)
	{
		x = x + P_ReturnThrustX(source, an, source->radius + th->radius);
		y = y + P_ReturnThrustY(source, an, source->radius + th->radius);
		throwmo = P_SpawnMobj(x, y, z, MT_FIREDITEM);
		throwmo->movecount = 1;
		throwmo->movedir = source->angle - an;
		P_SetTarget(&throwmo->target, source);
	}

	return NULL;
}

UINT16 K_DriftSparkColor(player_t *player, INT32 charge)
{
	INT32 ds = K_GetKartDriftSparkValue(player);
	UINT16 color = SKINCOLOR_NONE;

	if (charge < 0)
	{
		// Stage 0: GREY
		color = SKINCOLOR_GREY;
	}
	else if (charge >= ds*4)
	{
		// Stage 4: Rainbow
		if (charge <= (ds*4)+(32*3))
		{
			// transition
			color = SKINCOLOR_SILVER;
		}
		else
		{
			color = K_RainbowColor(leveltime);
		}
	}
	else if (K_PurpleDriftActive() && charge >= ds*3)
	{
		// Stage 3: Purple
		if (charge <= (ds*3)+(32*3))
		{
			// transition
			color = SKINCOLOR_VIOLET;
		}
		else
		{
			color = SKINCOLOR_PURPLE;
		}
	}
	else if (charge >= ds*2)
	{
		// Stage 2: Red
		if (charge <= (ds*2)+(32*3))
		{
			// transition
			color = SKINCOLOR_TANGERINE;
		}
		else
		{
			color = SKINCOLOR_KETCHUP;
		}
	}
	else if (charge >= ds)
	{
		// Stage 1: Blue
		if (charge <= (ds)+(32*3))
		{
			// transition
			color = SKINCOLOR_MAGENTA;
		}
		else
		{
			color = SKINCOLOR_SAPPHIRE;
		}
	}

	return color;
}

#define DRIFTSPARKGROWTICS 8

static void K_SpawnDriftSparks(player_t *player)
{
	fixed_t newx;
	fixed_t newy;
	mobj_t *spark;
	angle_t travelangle;
	INT32 i;

	I_Assert(player != NULL);
	I_Assert(player->mo != NULL);
	I_Assert(!P_MobjWasRemoved(player->mo));

	if (leveltime % 2 == 1)
		return;

	if (!player->drift || player->driftcharge < K_GetKartDriftSparkValue(player))
		return;

	travelangle = player->mo->angle-(ANGLE_45/5)*player->drift;

	for (i = 0; i < 2; i++)
	{
		fixed_t driftExtraScale = 0;
		newx = player->mo->x + P_ReturnThrustX(player->mo, travelangle + ((i&1) ? -1 : 1)*ANGLE_135, FixedMul(32*FRACUNIT, player->mo->scale));
		newy = player->mo->y + P_ReturnThrustY(player->mo, travelangle + ((i&1) ? -1 : 1)*ANGLE_135, FixedMul(32*FRACUNIT, player->mo->scale));
		spark = P_SpawnMobj(newx, newy, player->mo->z, MT_DRIFTSPARK);

		P_SetTarget(&spark->target, player->mo);
		spark->angle = travelangle-(ANGLE_45/5)*player->drift;

		// scale increase while driftspark level gained timer is running
		driftExtraScale = FixedDiv(player->driftsparkGrowTimer, DRIFTSPARKGROWTICS);
		spark->destscale = FixedMul(player->mo->scale, FRACUNIT + FixedMul(driftExtraScale, cv_driftsparkpulse.value));
		P_SetScale(spark, FixedMul(player->mo->scale, FRACUNIT + FixedMul(driftExtraScale, cv_driftsparkpulse.value)));

		spark->momx = player->mo->momx/2;
		spark->momy = player->mo->momy/2;
		//spark->momz = player->mo->momz/2;

		if (player->driftcharge >= K_GetKartDriftSparkValue(player)*4)
		{
			spark->color = 1 + (leveltime % (numskincolors-1));

		}
		else if (K_PurpleDriftActive() && player->driftcharge >= K_GetKartDriftSparkValue(player)*3)
		{
			if (player->driftcharge <= (K_GetKartDriftSparkValue(player)*3)+(24*3))
				spark->color = SKINCOLOR_VIOLET; // transition
			else
				spark->color = SKINCOLOR_PURPLE;
		}
		else if (player->driftcharge >= K_GetKartDriftSparkValue(player)*2)
		{
			if (player->driftcharge <= (K_GetKartDriftSparkValue(player)*2)+(24*3))
				spark->color = SKINCOLOR_RASPBERRY; // transition
			else
				spark->color = SKINCOLOR_KETCHUP;
		}
		else
		{
			spark->color = SKINCOLOR_SAPPHIRE;
		}

		if ((player->drift > 0 && player->cmd.turning > 0) // Inward drifts
			|| (player->drift < 0 && player->cmd.turning < 0))
		{
			if ((player->drift < 0 && (i & 1))
				|| (player->drift > 0 && !(i & 1)))
				P_SetMobjState(spark, S_DRIFTSPARK_A1);
			else if ((player->drift < 0 && !(i & 1))
				|| (player->drift > 0 && (i & 1)))
				P_SetMobjState(spark, S_DRIFTSPARK_C1);
		}
		else if ((player->drift > 0 && player->cmd.turning < 0) // Outward drifts
			|| (player->drift < 0 && player->cmd.turning > 0))
		{
			if ((player->drift < 0 && (i & 1))
				|| (player->drift > 0 && !(i & 1)))
				P_SetMobjState(spark, S_DRIFTSPARK_C1);
			else if ((player->drift < 0 && !(i & 1))
				|| (player->drift > 0 && (i & 1)))
				P_SetMobjState(spark, S_DRIFTSPARK_A1);
		}

		K_MatchGenericExtraFlags(spark, player->mo);
	}
}


static void K_SpawnAIZDust(player_t *player)
{
	fixed_t newx;
	fixed_t newy;
	mobj_t *spark;
	angle_t travelangle;

	I_Assert(player != NULL);
	I_Assert(player->mo != NULL);
	I_Assert(!P_MobjWasRemoved(player->mo));

	if (leveltime % 2 == 1)
		return;

	if (!P_IsObjectOnGround(player->mo))
		return;

	travelangle = K_MomentumAngle(player->mo);
	//S_StartSound(player->mo, sfx_s3k47);

	{
		newx = player->mo->x + P_ReturnThrustX(player->mo, travelangle - (player->aizdriftstrat*ANGLE_45), FixedMul(24*FRACUNIT, player->mo->scale));
		newy = player->mo->y + P_ReturnThrustY(player->mo, travelangle - (player->aizdriftstrat*ANGLE_45), FixedMul(24*FRACUNIT, player->mo->scale));
		spark = P_SpawnMobj(newx, newy, player->mo->z, MT_AIZDRIFTSTRAT);

		spark->angle = travelangle+(player->aizdriftstrat*ANGLE_90);
		P_SetScale(spark, (spark->destscale = (3*player->mo->scale)>>2));

		spark->momx = (6*player->mo->momx)/5;
		spark->momy = (6*player->mo->momy)/5;
		spark->momz = P_GetMobjZMovement(player->mo);

		K_MatchGenericExtraFlags(spark, player->mo);
	}
}

void K_SpawnBoostTrail(player_t *player)
{
	fixed_t newx, newy, newz;
	fixed_t ground;
	mobj_t *flame;
	angle_t travelangle;
	INT32 i;

	I_Assert(player != NULL);
	I_Assert(player->mo != NULL);
	I_Assert(!P_MobjWasRemoved(player->mo));

	if (!P_IsObjectOnGround(player->mo)
		|| player->hyudorotimer != 0
		|| ((gametyperules & GTR_BUMPERS) && player->bumper <= 0 && player->karmadelay))
		return;

	if (player->mo->eflags & MFE_VERTICALFLIP)
		ground = player->mo->ceilingz;
	else
		ground = player->mo->floorz;

	if (player->drift != 0)
		travelangle = player->mo->angle;
	else
		travelangle = K_MomentumAngle(player->mo);

	for (i = 0; i < 2; i++)
	{
		newx = player->mo->x + P_ReturnThrustX(player->mo, travelangle + ((i&1) ? -1 : 1)*ANGLE_135, FixedMul(24*FRACUNIT, player->mo->scale));
		newy = player->mo->y + P_ReturnThrustY(player->mo, travelangle + ((i&1) ? -1 : 1)*ANGLE_135, FixedMul(24*FRACUNIT, player->mo->scale));
		newz = P_GetZAt(player->mo->standingslope, newx, newy, ground);

		if (player->mo->eflags & MFE_VERTICALFLIP)
		{
			newz -= FixedMul(mobjinfo[MT_SNEAKERTRAIL].height, player->mo->scale);
		}

		flame = P_SpawnMobj(newx, newy, ground, MT_SNEAKERTRAIL);

		P_SetTarget(&flame->target, player->mo);
		flame->angle = travelangle;
		flame->fuse = TICRATE*2;
		flame->destscale = player->mo->scale;
		P_SetScale(flame, player->mo->scale);
		// not K_MatchGenericExtraFlags so that a stolen sneaker can be seen
		K_FlipFromObject(flame, player->mo);

		flame->momx = 8;
		P_XYMovement(flame);
		if (P_MobjWasRemoved(flame))
			continue;

		if (player->mo->eflags & MFE_VERTICALFLIP)
		{
			if (flame->z + flame->height < flame->ceilingz)
				P_RemoveMobj(flame);
		}
		else if (flame->z > flame->floorz)
			P_RemoveMobj(flame);
	}
}

void K_SpawnSparkleTrail(mobj_t *mo)
{
	const INT32 rad = (mo->radius*2)>>FRACBITS;
	mobj_t *sparkle;
	INT32 i;

	I_Assert(mo != NULL);
	I_Assert(!P_MobjWasRemoved(mo));

	for (i = 0; i < 3; i++)
	{
		fixed_t newx = mo->x + mo->momx + (P_RandomRange(-rad, rad)<<FRACBITS);
		fixed_t newy = mo->y + mo->momy + (P_RandomRange(-rad, rad)<<FRACBITS);
		fixed_t newz = mo->z + mo->momz + (P_RandomRange(0, mo->height>>FRACBITS)<<FRACBITS);

		sparkle = P_SpawnMobj(newx, newy, newz, MT_SPARKLETRAIL);
		K_FlipFromObject(sparkle, mo);

		P_SetTarget(&sparkle->target, mo);
		sparkle->destscale = mo->destscale;
		P_SetScale(sparkle, mo->scale);
		sparkle->color = mo->color;
	}

	P_SetMobjState(sparkle, S_KARTINVULN_LARGE1);
}

static void K_SpawnDraftDust(mobj_t *mo)
{
	I_Assert(mo != NULL);
	I_Assert(!P_MobjWasRemoved(mo));

	if (leveltime % 2)
	{
		SINT8 i;
		for (i = 0; i < 2; i++)
		{
			fixed_t newx = mo->x + P_ReturnThrustX(mo, mo->angle + ((i == 0) ? -1 : 1)*ANGLE_90, FixedMul(16*FRACUNIT, mo->scale));
			fixed_t newy = mo->y + P_ReturnThrustY(mo, mo->angle + ((i == 0) ? -1 : 1)*ANGLE_90, FixedMul(16*FRACUNIT, mo->scale));
			mobj_t *dust = P_SpawnMobj(newx, newy, mo->z, MT_PARTICLE);

			P_SetTarget(&dust->target, mo);
			dust->angle = K_MomentumAngle(mo);
			dust->destscale = mo->scale*3;
			dust->scalespeed = mo->scale/6;
			P_SetScale(dust, mo->scale);
			K_FlipFromObject(dust, mo);
			dust->momx = 3*mo->momx/5;
			dust->momy = 3*mo->momy/5;
			dust->renderflags |= RF_GHOSTLY;
			dust->fuse = 5;
		}
	}
}

void K_SpawnWipeoutTrail(mobj_t *mo, boolean translucent)
{
	mobj_t *dust;
	angle_t aoff;

	I_Assert(mo != NULL);
	I_Assert(!P_MobjWasRemoved(mo));

	if (mo->player)
		aoff = (mo->player->drawangle + ANGLE_180);
	else
		aoff = (mo->angle + ANGLE_180);

	if ((leveltime / 2) & 1)
		aoff -= ANGLE_45;
	else
		aoff += ANGLE_45;

	fixed_t rand_x;
	fixed_t rand_y;

	rand_y = mo->y + FixedMul(24*mo->scale, FINESINE(aoff>>ANGLETOFINESHIFT)) + (P_RandomRange(-8,8) << FRACBITS);
	rand_x = mo->x + FixedMul(24*mo->scale, FINECOSINE(aoff>>ANGLETOFINESHIFT)) + (P_RandomRange(-8,8) << FRACBITS);

	dust = P_SpawnMobj(rand_x, rand_y, mo->z, MT_WIPEOUTTRAIL);

	P_SetTarget(&dust->target, mo);
	dust->angle = K_MomentumAngle(mo);
	dust->destscale = mo->scale;
	P_SetScale(dust, mo->scale);
	K_FlipFromObject(dust, mo);

	if (translucent) // offroad effect
	{
		dust->momx = mo->momx/2;
		dust->momy = mo->momy/2;
		dust->momz = mo->momz/2;
	}

	if (translucent)
		dust->renderflags |= RF_GHOSTLY;
}

//	K_DriftDustHandling
//	Parameters:
//		spawner: The map object that is spawning the drift dust
//	Description: Spawns the drift dust for objects, players use rmomx/y, other objects use regular momx/y.
//		Also plays the drift sound.
//		Other objects should be angled towards where they're trying to go so they don't randomly spawn dust
//		Do note that most of the function won't run in odd intervals of frames
void K_DriftDustHandling(mobj_t *spawner)
{
	angle_t anglediff;
	const INT16 spawnrange = spawner->radius>>FRACBITS;

	if (!P_IsObjectOnGround(spawner) || leveltime % 2 != 0)
		return;

	if (spawner->player)
	{
		if (spawner->player->pflags & PF_SKIDDOWN)
		{
			anglediff = abs((signed)(spawner->angle - spawner->player->drawangle));
			if (leveltime % 6 == 0)
				S_StartSound(spawner, sfx_screec); // repeated here because it doesn't always happen to be within the range when this is the case
		}
		else
		{
			angle_t playerangle = spawner->angle;

			if (spawner->player->speed < 5<<FRACBITS)
				return;

			if (spawner->player->cmd.forwardmove < 0)
				playerangle += ANGLE_180;

			anglediff = abs((signed)(playerangle - R_PointToAngle2(0, 0, spawner->player->rmomx, spawner->player->rmomy)));
		}
	}
	else
	{
		if (P_AproxDistance(spawner->momx, spawner->momy) < 5<<FRACBITS)
			return;

		anglediff = abs((signed)(spawner->angle - R_PointToAngle2(0, 0, spawner->momx, spawner->momy)));
	}

	if (anglediff > ANGLE_180)
		anglediff = InvAngle(anglediff);

	if (anglediff > ANG10*4) // Trying to turn further than 40 degrees
	{
		fixed_t spawnx = P_RandomRange(-spawnrange, spawnrange)<<FRACBITS;
		fixed_t spawny = P_RandomRange(-spawnrange, spawnrange)<<FRACBITS;
		INT32 speedrange = 2;
		mobj_t *dust = P_SpawnMobj(spawner->x + spawnx, spawner->y + spawny, spawner->z, MT_DRIFTDUST);
		dust->momx = FixedMul(spawner->momx + (P_RandomRange(-speedrange, speedrange)<<FRACBITS), 3*(spawner->scale)/4);
		dust->momy = FixedMul(spawner->momy + (P_RandomRange(-speedrange, speedrange)<<FRACBITS), 3*(spawner->scale)/4);
		dust->momz = P_MobjFlip(spawner) * (P_RandomRange(1, 4) * (spawner->scale));
		P_SetScale(dust, spawner->scale/2);
		dust->destscale = spawner->scale * 3;
		dust->scalespeed = spawner->scale/12;
		P_SetTarget(&dust->target, spawner);

		if (leveltime % 6 == 0)
			S_StartSound(spawner, sfx_screec);

		K_MatchGenericExtraFlags(dust, spawner);
	}
}

static mobj_t *K_FindLastTrailMobj(player_t *player)
{
	mobj_t *trail;

	if (!player || !(trail = player->mo) || !player->mo->hnext || !player->mo->hnext->health)
		return NULL;

	while (trail->hnext && !P_MobjWasRemoved(trail->hnext) && trail->hnext->health)
	{
		trail = trail->hnext;
	}

	return trail;
}

mobj_t *K_ThrowKartItem(player_t *player, boolean missile, mobjtype_t mapthing, INT32 defaultDir, INT32 altthrow)
{
	mobj_t *mo;
	INT32 dir;
	fixed_t PROJSPEED;
	angle_t newangle;
	fixed_t newx, newy, newz;
	mobj_t *throwmo;

	if (!player)
		return NULL;

	// Figure out projectile speed by game speed
	if (missile && mapthing != MT_BALLHOG) // Trying to keep compatability...
	{
		PROJSPEED = mobjinfo[mapthing].speed;
		if (gamespeed == KARTSPEED_EASY)
			PROJSPEED = FixedMul(PROJSPEED, FRACUNIT-FRACUNIT/4);
		else if (gamespeed == KARTSPEED_HARD)
			PROJSPEED = FixedMul(PROJSPEED, FRACUNIT+FRACUNIT/4);
		else if (gamespeed == KARTSPEED_EXPERT)
			PROJSPEED = FixedMul(PROJSPEED, FRACUNIT+FRACUNIT/2);
		PROJSPEED = FixedMul(PROJSPEED, mapobjectscale);
	}
	else
	{
		PROJSPEED = K_GetProjectileSpeed();
	}

	if (altthrow)
	{
		if (altthrow == 2) // Kitchen sink throwing
		{
#if 0
			if (player->throwdir == 1)
				dir = 3;
			else if (player->throwdir == -1)
				dir = 1;
			else
				dir = 2;
#else
			if (player->throwdir == 1)
				dir = 2;
			else
				dir = 1;
#endif
		}
		else
		{
			if (player->throwdir == 1)
				dir = 2;
			else if (player->throwdir == -1)
				dir = -1;
			else
				dir = 1;
		}
	}
	else
	{
		if (player->throwdir != 0)
			dir = player->throwdir;
		else
			dir = defaultDir;
	}

	if (missile) // Shootables
	{
		if (mapthing == MT_BALLHOG) // Messy
		{
			mo = NULL; // can't return multiple projectiles
			if (dir == -1)
			{
				// Shoot backward
				K_SpawnKartMissile(player->mo, mapthing, (player->mo->angle + ANGLE_180) - 0x06000000, 0, PROJSPEED/8);
				K_SpawnKartMissile(player->mo, mapthing, (player->mo->angle + ANGLE_180) - 0x03000000, 0, PROJSPEED/8);
				K_SpawnKartMissile(player->mo, mapthing, player->mo->angle + ANGLE_180, 0, PROJSPEED/8);
				K_SpawnKartMissile(player->mo, mapthing, (player->mo->angle + ANGLE_180) + 0x03000000, 0, PROJSPEED/8);
				K_SpawnKartMissile(player->mo, mapthing, (player->mo->angle + ANGLE_180) + 0x06000000, 0, PROJSPEED/8);
			}
			else
			{
				// Shoot forward
				K_SpawnKartMissile(player->mo, mapthing, player->mo->angle - 0x06000000, 0, PROJSPEED);
				K_SpawnKartMissile(player->mo, mapthing, player->mo->angle - 0x03000000, 0, PROJSPEED);
				K_SpawnKartMissile(player->mo, mapthing, player->mo->angle, 0, PROJSPEED);
				K_SpawnKartMissile(player->mo, mapthing, player->mo->angle + 0x03000000, 0, PROJSPEED);
				K_SpawnKartMissile(player->mo, mapthing, player->mo->angle + 0x06000000, 0, PROJSPEED);
			}
		}
		else
		{
			if (dir == -1 && mapthing != MT_SPB)
			{
				// Shoot backward
				mo = K_SpawnKartMissile(player->mo, mapthing, player->mo->angle + ANGLE_180, 0, PROJSPEED/8);
			}
			else
			{
				// Shoot forward
				mo = K_SpawnKartMissile(player->mo, mapthing, player->mo->angle, 0, PROJSPEED);
			}
		}
	}
	else
	{
		player->bananadrag = 0; // RESET timer, for multiple bananas

		if (dir > 0)
		{
			// Shoot forward
			mo = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z + player->mo->height/2, mapthing);
			//K_FlipFromObject(mo, player->mo);
			// These are really weird so let's make it a very specific case to make SURE it works...
			if (player->mo->eflags & MFE_VERTICALFLIP)
			{
				mo->z -= player->mo->height;
				mo->flags2 |= MF2_OBJECTFLIP;
				mo->eflags |= MFE_VERTICALFLIP;
			}

			mo->threshold = 10;
			P_SetTarget(&mo->target, player->mo);

			S_StartSound(player->mo, mo->info->seesound);

			if (mo)
			{
				angle_t fa = player->mo->angle>>ANGLETOFINESHIFT;
				fixed_t HEIGHT = (20 + (dir*10))*mapobjectscale + (player->mo->momz*P_MobjFlip(player->mo));

				mo->momz = HEIGHT*P_MobjFlip(mo);
				mo->momx = player->mo->momx + FixedMul(FINECOSINE(fa), PROJSPEED*dir);
				mo->momy = player->mo->momy + FixedMul(FINESINE(fa), PROJSPEED*dir);

				mo->extravalue2 = dir;

				if (mo->eflags & MFE_UNDERWATER)
					mo->momz = (117 * mo->momz) / 200;
			}

			// this is the small graphic effect that plops in you when you throw an item:
			throwmo = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z + player->mo->height/2, MT_FIREDITEM);
			P_SetTarget(&throwmo->target, player->mo);
			// Ditto:
			if (player->mo->eflags & MFE_VERTICALFLIP)
			{
				throwmo->z -= player->mo->height;
				throwmo->flags2 |= MF2_OBJECTFLIP;
				throwmo->eflags |= MFE_VERTICALFLIP;
			}

			throwmo->movecount = 0; // above player
		}
		else
		{
			mobj_t *lasttrail = K_FindLastTrailMobj(player);

			if (lasttrail)
			{
				newx = lasttrail->x;
				newy = lasttrail->y;
				newz = lasttrail->z;
			}
			else
			{
				// Drop it directly behind you.
				fixed_t dropradius = FixedHypot(player->mo->radius, player->mo->radius) + FixedHypot(mobjinfo[mapthing].radius, mobjinfo[mapthing].radius);

				newangle = player->mo->angle;

				newx = player->mo->x + P_ReturnThrustX(player->mo, newangle + ANGLE_180, dropradius);
				newy = player->mo->y + P_ReturnThrustY(player->mo, newangle + ANGLE_180, dropradius);
				newz = player->mo->z;
			}

			mo = P_SpawnMobj(newx, newy, newz, mapthing); // this will never return null because collision isn't processed here
			K_FlipFromObject(mo, player->mo);

			mo->threshold = 10;
			P_SetTarget(&mo->target, player->mo);

			if (P_IsObjectOnGround(player->mo))
			{
				// floorz and ceilingz aren't properly set to account for FOFs and Polyobjects on spawn
				// This should set it for FOFs
				P_SetOrigin(mo, mo->x, mo->y, mo->z); // however, THIS can fuck up your day. just absolutely ruin you.
				if (P_MobjWasRemoved(mo))
					return NULL;

				if (P_MobjFlip(mo) > 0)
				{
					if (mo->floorz > mo->target->z - mo->height)
					{
						mo->z = mo->floorz;
					}
				}
				else
				{
					if (mo->ceilingz < mo->target->z + mo->target->height + mo->height)
					{
						mo->z = mo->ceilingz - mo->height;
					}
				}
			}

			if (player->mo->eflags & MFE_VERTICALFLIP)
				mo->eflags |= MFE_VERTICALFLIP;

			if (mapthing == MT_SSMINE)
				mo->extravalue1 = 49; // Pads the start-up length from 21 frames to a full 2 seconds
			else if (mapthing == MT_BUBBLESHIELDTRAP)
			{
				P_SetScale(mo, ((5*mo->destscale)>>2)*4);
				mo->destscale = (5*mo->destscale)>>2;
				S_StartSound(mo, sfx_s3kbfl);
			}
		}
	}

	return mo;
}

void K_PuntMine(mobj_t *origMine, mobj_t *punter)
{
	angle_t fa = K_MomentumAngle(punter);
	fixed_t z = (punter->momz * P_MobjFlip(punter)) + (30 * FRACUNIT);
	fixed_t spd;
	mobj_t *mine;

	if (!origMine || P_MobjWasRemoved(origMine))
		return;

	// This guarantees you hit a mine being dragged
	if (origMine->type == MT_SSMINE_SHIELD) // Create a new mine, and clean up the old one
	{
		mobj_t *mineOwner = origMine->target;

		mine = P_SpawnMobj(origMine->x, origMine->y, origMine->z, MT_SSMINE);

		P_SetTarget(&mine->target, mineOwner);

		mine->angle = origMine->angle;
		mine->flags2 = origMine->flags2;
		mine->floorz = origMine->floorz;
		mine->ceilingz = origMine->ceilingz;

		P_SetScale(mine, origMine->scale);
		mine->destscale = origMine->destscale;
		mine->scalespeed = origMine->scalespeed;

		// Copy interp data
		mine->old_angle = origMine->old_angle;
		mine->old_x = origMine->old_x;
		mine->old_y = origMine->old_y;
		mine->old_z = origMine->old_z;

		// Since we aren't using P_KillMobj, we need to clean up the hnext reference
		P_SetTarget(&mineOwner->hnext, NULL);
		K_UnsetItemOut(mineOwner->player);

		if (mineOwner->player->itemamount)
		{
			mineOwner->player->itemamount--;
		}

		if (!mineOwner->player->itemamount)
		{
			mineOwner->player->itemtype = KITEM_NONE;
		}

		P_RemoveMobj(origMine);
	}
	else
	{
		mine = origMine;
	}

	if (!mine || P_MobjWasRemoved(mine))
		return;

	if (mine->threshold > 0)
		return;

	spd = K_GetProjectileSpeed();

	mine->flags |= MF_NOCLIPTHING;

	P_SetMobjState(mine, S_SSMINE_AIR1);
	mine->threshold = 10;
	mine->reactiontime = mine->info->reactiontime;

	mine->momx = punter->momx + FixedMul(FINECOSINE(fa >> ANGLETOFINESHIFT), spd);
	mine->momy = punter->momy + FixedMul(FINESINE(fa >> ANGLETOFINESHIFT), spd);
	P_SetObjectMomZ(mine, z, false);

	mine->flags &= ~MF_NOCLIPTHING;
}

#define THUNDERRADIUS 320

// Rough size of the outer-rim sprites, after scaling.
// (The hitbox is already pretty strict due to only 1 active frame,
// we don't need to have it disjointedly small too...)
#define THUNDERSPRITE 80

static void K_DoThunderShield(player_t *player)
{
	mobj_t *mo;
	int i = 0;
	fixed_t sx;
	fixed_t sy;
	angle_t an;

	S_StartSound(player->mo, sfx_zio3);
	K_ThunderShieldAttack(player->mo, (THUNDERRADIUS + THUNDERSPRITE) * FRACUNIT);

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
		mo->angle = P_RandomRange(0, 359)*ANG1;
		mo->fuse = P_RandomRange(20, 50);
		P_SetTarget(&mo->target, player->mo);
		P_SetMobjState(mo, S_KLIT1);
	}

	// spawn the radius thing:
	an = ANGLE_22h;
	for (i=0; i<15; i++)
	{
		sx = player->mo->x + FixedMul((player->mo->scale*THUNDERRADIUS), FINECOSINE((an*i)>>ANGLETOFINESHIFT));
		sy = player->mo->y + FixedMul((player->mo->scale*THUNDERRADIUS), FINESINE((an*i)>>ANGLETOFINESHIFT));
		mo = P_SpawnMobj(sx, sy, player->mo->z, MT_THOK);
		mo->angle = an*i;
		mo->extravalue1 = THUNDERRADIUS;	// Used to know whether we should teleport by radius or something.
		mo->scale = player->mo->scale*3;
		P_SetTarget(&mo->target, player->mo);
		P_SetMobjState(mo, S_KSPARK1);
	}
}

#undef THUNDERRADIUS
#undef THUNDERSPRITE

static void K_FlameDashLeftoverSmoke(mobj_t *src)
{
	UINT8 i;

	for (i = 0; i < 2; i++)
	{
		mobj_t *smoke = P_SpawnMobj(src->x, src->y, src->z+(8<<FRACBITS), MT_BOOSTSMOKE);

		P_SetScale(smoke, src->scale);
		smoke->destscale = 3*src->scale/2;
		smoke->scalespeed = src->scale/12;

		smoke->momx = 3*src->momx/4;
		smoke->momy = 3*src->momy/4;
		smoke->momz = 3*P_GetMobjZMovement(src)/4;

		fixed_t rand_angle;
		fixed_t rand_move;

		rand_move = P_RandomRange(0, 8) * src->scale;
		rand_angle = src->angle + FixedAngle(P_RandomRange(135, 225)<<FRACBITS);

		P_Thrust(smoke, rand_angle, rand_move);
		smoke->momz += P_RandomRange(0, 4) * src->scale;
	}
}

static void K_DoHyudoroSteal(player_t *player)
{
	INT32 i, numplayers = 0;
	INT32 playerswappable[MAXPLAYERS];
	INT32 stealplayer = -1; // The player that's getting stolen from
	INT32 prandom = 0;
	boolean sink = P_RandomChance(FRACUNIT/64);
	INT32 hyu = hyudorotime;

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (playeringame[i] && players[i].mo && players[i].mo->health > 0 && players[i].playerstate == PST_LIVE
			&& player != &players[i] && !players[i].exiting && !players[i].spectator // Player in-game

			// Can steal from this player
			&& (gametype == GT_RACE //&& players[i].position < player->position)
			|| ((gametyperules & GTR_BUMPERS) && players[i].bumper > 0))

			// Has an item
			&& (players[i].itemtype
			&& players[i].itemamount
			&& !(players[i].itemflags & IF_ITEMOUT)
			&& !players[i].itemblink))
		{
			playerswappable[numplayers] = i;
			numplayers++;
		}
	}

	prandom = P_RandomFixed();
	S_StartSound(player->mo, sfx_s3k92);

	if (sink && numplayers > 0 && cv_kitchensink.value) // BEHOLD THE KITCHEN SINK
	{
		player->hyudorotimer = hyu;
		player->stealingtimer = stealtime;

		player->itemtype = KITEM_KITCHENSINK;
		player->itemamount = 1;
		K_UnsetItemOut(player);
		return;
	}
	else if ((gametype == GT_RACE && player->position == 1) || numplayers == 0) // No-one can be stolen from? Oh well...
	{
		player->hyudorotimer = hyu;
		player->stealingtimer = stealtime;
		return;
	}
	else if (numplayers == 1) // With just 2 players, we just need to set the other player to be the one to steal from
	{
		stealplayer = playerswappable[numplayers-1];
	}
	else if (numplayers > 1) // We need to choose between the available candidates for the 2nd player
	{
		stealplayer = playerswappable[prandom%(numplayers-1)];
	}

	if (stealplayer > -1) // Now here's where we do the stealing, has to be done here because we still know the player we're stealing from
	{
		player->hyudorotimer = hyu;
		player->stealingtimer = stealtime;
		players[stealplayer].stolentimer = stealtime;

		player->itemtype = players[stealplayer].itemtype;
		player->itemamount = players[stealplayer].itemamount;
		K_UnsetItemOut(player);

		players[stealplayer].itemtype = KITEM_NONE;
		players[stealplayer].itemamount = 0;
		K_UnsetItemOut(&players[stealplayer]);

		if (P_IsDisplayPlayer(&players[stealplayer]) && !r_splitscreen)
			S_StartSound(NULL, sfx_s3k92);
	}
}

// Handle these else where to reduce code duplication between panels and sneakers
static void K_SneakerPanelStackSound(player_t *player)
{
	const sfxenum_t normalsfx = sfx_cdfm01;
	const sfxenum_t smallsfx = sfx_cdfm40;
	sfxenum_t sfx = normalsfx;

	if ((player->numsneakers > 0) && K_StackingActive() && cv_kartstacking_sneakerstacksound.value)
	{
		// Use a less annoying sound when stacking sneakers.
		sfx = smallsfx;
	}

	S_StopSoundByID(player->mo, normalsfx);
	S_StopSoundByID(player->mo, smallsfx);
	S_StartSound(player->mo, sfx);
}

static void K_SneakerPanelEffect(player_t *player, INT32 type)
{
	if (!player->sneakertimer)
	{
		if (type == 2)
		{
			if (player->mo->hnext)
			{
				mobj_t *cur = player->mo->hnext;
				while (cur && !P_MobjWasRemoved(cur))
				{
					if (!cur->tracer)
					{
						mobj_t *overlay = P_SpawnMobj(cur->x, cur->y, cur->z, MT_BOOSTFLAME);
						P_SetTarget(&overlay->target, cur);
						P_SetTarget(&cur->tracer, overlay);
						P_SetScale(overlay, (overlay->destscale = 3*cur->scale/4));
						K_FlipFromObject(overlay, cur);
					}
					cur = cur->hnext;
				}
			}
		}
		else
		{
			mobj_t *overlay = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_BOOSTFLAME);
			P_SetTarget(&overlay->target, player->mo);
			P_SetScale(overlay, (overlay->destscale = player->mo->scale));
			K_FlipFromObject(overlay, player->mo);
		}
	}
}

void K_DoSneaker(player_t *player, INT32 type)
{
	const fixed_t intendedboost = K_GetSneakerBoostSpeed();
	const tic_t sneakerduration = (type == SNEAKERTYPE_WATERPANEL) ? waterpaneltime : sneakertime;

	if (player->floorboost == 0 || player->floorboost == 3)
	{
		K_SneakerPanelStackSound(player);
		K_SpawnDashDustRelease(player);
		if (intendedboost > player->speedboost)
			player->karthud[khud_destboostcam] = FixedMul(FRACUNIT, FixedDiv((intendedboost - player->speedboost), intendedboost));

	}

	K_SneakerPanelEffect(player, type);

	if (type != SNEAKERTYPE_PANEL && type != SNEAKERTYPE_WATERPANEL)
	{
		K_PlayBoostTaunt(player->mo);
	}

	player->sneakertimer = sneakerduration;
	player->realsneakertimer = sneakerduration;

	if (player->sneakertimer && (player->floorboost == 0 || player->floorboost == 3))
	{
		player->numsneakers = CLAMP(player->numsneakers+1, 0, stackingactive ? MAXSNEAKERSTACK : 1);
	}

	if (type == SNEAKERTYPE_WATERPANEL)
	{
		// Water Running please!
		player->mo->flags2 |= MF2_WATERRUN;
	}

	// set angle for spun out players:
	player->boostangle = player->mo->angle;
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

void K_DoPogoSpring(mobj_t *mo, fixed_t vertispeed, UINT8 sound)
{
	const fixed_t vscale = mapobjectscale + (mo->scale - mapobjectscale);

	if (mo->player && mo->player->spectator)
		return;

	if (mo->eflags & MFE_SPRUNG)
		return;

	mo->standingslope = NULL;

	mo->eflags |= MFE_SPRUNG;

	if (mo->eflags & MFE_VERTICALFLIP)
		vertispeed *= -1;

	if (vertispeed == 0)
	{
		fixed_t thrust;

		if (mo->player)
		{
			thrust = 3*mo->player->speed/2;
			if (thrust < 48<<FRACBITS)
				thrust = 48<<FRACBITS;
			if (thrust > 72<<FRACBITS)
				thrust = 72<<FRACBITS;
			if (mo->player->pogospring != 2)
			{
				if (mo->player->sneakertimer)
					thrust = FixedMul(thrust, 5*FRACUNIT/4);
				else if (mo->player->invincibilitytimer)
					thrust = FixedMul(thrust, 9*FRACUNIT/8);
				else if (mo->player->flamestore)
					thrust = FixedMul(thrust, 9*FRACUNIT/8);
			}
		}
		else
		{
			thrust = FixedDiv(3*P_AproxDistance(mo->momx, mo->momy)/2, 5*FRACUNIT/2);
			if (thrust < 16<<FRACBITS)
				thrust = 16<<FRACBITS;
			if (thrust > 32<<FRACBITS)
				thrust = 32<<FRACBITS;
		}

		mo->momz = P_MobjFlip(mo)*FixedMul(FINESINE(ANGLE_22h>>ANGLETOFINESHIFT), FixedMul(thrust, vscale));
	}
	else
		mo->momz = FixedMul(vertispeed, vscale);

	if (mo->eflags & MFE_UNDERWATER)
		mo->momz = (117 * mo->momz) / 200;

	if (sound)
		S_StartSound(mo, (sound == 1 ? sfx_kc2f : sfx_kpogos));
}

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

void K_DoInvincibility(player_t *player, tic_t time)
{
	if (!player->invincibilitytimer)
	{
		mobj_t *overlay = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_INVULNFLASH);
		P_SetTarget(&overlay->target, player->mo);
		overlay->destscale = player->mo->scale;
		P_SetScale(overlay, player->mo->scale);

        if (K_GetKartInvinType() == KARTINVIN_ALTERN)
        {
            mobj_t *aura = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_OVERLAY);
            P_SetTarget(&aura->target, player->mo);
            aura->destscale = player->mo->scale;
            P_SetScale(aura, player->mo->scale);
            aura->extravalue2 = 1;
        }
	}

    if (K_GetKartInvinType() == KARTINVIN_ALTERN)
    {
        // Rim suggestion: Don't allow already invincible players to chain.
        if (K_InvincibilityGradient(player->invincibilitytimer) < (FRACUNIT/2))
        {
            // Be nice to players at half-power or less.
            player->invincibilitytimer = max(player->invincibilitytimer, time);
        }
    }
    else
    {
        player->invincibilitytimer = time;
    }

	player->maxinvincibilitytime = time;

	if (P_IsLocalPlayer(player) == true)
	{
		S_ChangeMusicSpecial("kinvnc");
	}
	else //used to be "if (P_IsDisplayPlayer(player) == false)"
	{
		S_StartSound(player->mo, (cv_kartinvinsfx.value ? sfx_alarmi : sfx_kinvnc));
	}

	P_RestoreMusic(player);
}

void K_KillBananaChain(mobj_t *banana, mobj_t *inflictor, mobj_t *source)
{
	mobj_t *cachenext;

killnext:
	if (P_MobjWasRemoved(banana))
		return;

	cachenext = banana->hnext;

	if (banana->health)
	{
		if (banana->eflags & MFE_VERTICALFLIP)
			banana->z -= banana->height;
		else
			banana->z += banana->height;

		S_StartSound(banana, banana->info->deathsound);
		P_KillMobj(banana, inflictor, source, DMG_NORMAL);

		P_SetObjectMomZ(banana, 8*FRACUNIT, false);
		if (inflictor)
			P_InstaThrust(banana, R_PointToAngle2(inflictor->x, inflictor->y, banana->x, banana->y)+ANGLE_90, 16*FRACUNIT);
	}

	if ((banana = cachenext))
		goto killnext;
}

// Just for firing/dropping items.
void K_UpdateHnextList(player_t *player, boolean clean)
{
	mobj_t *work = player->mo, *nextwork;

	if (!work)
		return;

	nextwork = work->hnext;

	while ((work = nextwork) && !(work == NULL || P_MobjWasRemoved(work)))
	{
		nextwork = work->hnext;

		if (!clean && (!work->movedir || work->movedir <= (UINT16)player->itemamount))
		{
			continue;
		}

		P_RemoveMobj(work);
	}

	if (player->mo->hnext == NULL || P_MobjWasRemoved(player->mo->hnext))
	{
		// Like below, try to clean up the pointer if it's NULL.
		// Maybe this was a cause of the shrink/eggbox fails?
		P_SetTarget(&player->mo->hnext, NULL);
	}
}

// For getting hit!
void K_DropHnextList(player_t *player)
{
	mobj_t *work = player->mo, *nextwork, *dropwork;
	INT32 flip;
	mobjtype_t type;
	boolean orbit, ponground, dropall = true;

	if (work == NULL || P_MobjWasRemoved(work))
	{
		return;
	}

	flip = P_MobjFlip(player->mo);
	ponground = P_IsObjectOnGround(player->mo);

	nextwork = work->hnext;

	while ((work = nextwork) && !(work == NULL || P_MobjWasRemoved(work)))
	{
		nextwork = work->hnext;

		if (!work->health)
			continue; // taking care of itself

		switch (work->type)
		{
			// Kart orbit items
			case MT_ORBINAUT_SHIELD:
				orbit = true;
				type = MT_ORBINAUT;
				break;
			case MT_JAWZ_SHIELD:
				orbit = true;
				type = MT_JAWZ_DUD;
				break;
			// Kart trailing items
			case MT_BANANA_SHIELD:
				orbit = false;
				type = MT_BANANA;
				break;
			case MT_SSMINE_SHIELD:
				orbit = false;
				dropall = false;
				type = MT_SSMINE;
				break;
			case MT_EGGMANITEM_SHIELD:
				orbit = false;
				type = MT_EGGMANITEM;
				break;
			// intentionally do nothing
			case MT_ROCKETSNEAKER:
			case MT_SINK_SHIELD:
				return;
			default:
				continue;
		}

		dropwork = P_SpawnMobj(work->x, work->y, work->z, type);

		P_SetTarget(&dropwork->target, player->mo);
		P_AddKartItem(dropwork); // needs to be called here so shrink can bust items off players in front of the user.

		dropwork->angle = work->angle;

		dropwork->scalespeed = work->scalespeed;
		dropwork->spritexscale = work->spritexscale;
		dropwork->spriteyscale = work->spriteyscale;

		dropwork->flags |= MF_NOCLIPTHING;
		dropwork->flags2 = work->flags2;
		dropwork->eflags = work->eflags;

		dropwork->renderflags = work->renderflags;
		dropwork->color = work->color;
		dropwork->colorized = work->colorized;
		dropwork->whiteshadow = work->whiteshadow;

		dropwork->floorz = work->floorz;
		dropwork->ceilingz = work->ceilingz;

		dropwork->health = work->health; // will never be set to 0 as long as above guard exists

		// Copy interp data
		dropwork->old_angle = work->old_angle;
		dropwork->old_x = work->old_x;
		dropwork->old_y = work->old_y;
		dropwork->old_z = work->old_z;

		if (ponground)
		{
			// floorz and ceilingz aren't properly set to account for FOFs and Polyobjects on spawn
			// This should set it for FOFs
			//P_SetOrigin(dropwork, dropwork->x, dropwork->y, dropwork->z); -- handled better by above floorz/ceilingz passing

			if (flip == 1)
			{
				if (dropwork->floorz > dropwork->target->z - dropwork->height)
				{
					dropwork->z = dropwork->floorz;
				}
			}
			else
			{
				if (dropwork->ceilingz < dropwork->target->z + dropwork->target->height + dropwork->height)
				{
					dropwork->z = dropwork->ceilingz - dropwork->height;
				}
			}
		}

		if (orbit) // splay out
		{
			dropwork->flags2 |= MF2_AMBUSH;

			dropwork->z += flip;

			dropwork->momx = player->mo->momx>>1;
			dropwork->momy = player->mo->momy>>1;
			dropwork->momz = 3*flip*mapobjectscale;

			if (dropwork->eflags & MFE_UNDERWATER)
				dropwork->momz = (117 * dropwork->momz) / 200;

			P_Thrust(dropwork, work->angle - ANGLE_90, 6*mapobjectscale);

			dropwork->movecount = 2;
			dropwork->movedir = work->angle - ANGLE_90;

			P_SetMobjState(dropwork, dropwork->info->deathstate);

			dropwork->tics = -1;

			if (type == MT_JAWZ_DUD)
			{
				dropwork->z += 20*flip*dropwork->scale;
			}
			else
			{
				dropwork->angle -= ANGLE_90;
			}
		}
		else // plop on the ground
		{
			dropwork->threshold = 10;
			dropwork->flags &= ~MF_NOCLIPTHING;
		}

		P_RemoveMobj(work);
	}

	// we need this here too because this is done in afterthink - pointers are cleaned up at the START of each tic...
	P_SetTarget(&player->mo->hnext, NULL);

	player->bananadrag = 0;

	if (player->itemflags & IF_EGGMANOUT)
	{
		player->itemflags &= ~IF_EGGMANOUT;
	}
	else if ((player->itemflags & IF_ITEMOUT)
		&& (dropall || (--player->itemamount <= 0)))
	{
		player->itemamount = 0;
		K_UnsetItemOut(player);
		player->itemtype = KITEM_NONE;
	}
}

// Bubble shield break SFX and VFX (maybe, hopefully)
static void K_BreakBubbleShield(player_t *player)
{
	// TODO: Some more dramatic SFX and VFX (DSS3K59, bubble shield shards fly all over)
	S_StartSound(player->mo, sfx_s3k4b);
}

// For getting hit!
void K_PopPlayerShield(player_t *player)
{
	INT32 shield = K_GetShieldFromPlayer(player);
	boolean isbubble = false;

	// Doesn't apply if player is invalid.
	if (player->mo == NULL || P_MobjWasRemoved(player->mo))
	{
		return;
	}

	switch (shield)
	{
		case KSHIELD_THUNDER:
			K_DoThunderShield(player);
			break;
		case KSHIELD_BUBBLE:
			isbubble = true;
			break;
		case KSHIELD_FLAME:
			S_StartSound(player->mo, sfx_s3k47);
			break;
	}

	if ((isbubble) && (player->bubblehealth > 0))
	{
		// Nice whiff; see ya!
		K_BreakBubbleShield(player);
	}

	player->curshield = KSHIELD_NONE;
	player->itemtype = KITEM_NONE;
	player->itemamount = 0;

	player->flametimer = 0;
	//player->flamestore = 0
	//player->flamedash = 0;

	player->bubbleblowup = 0;
	player->bubblecool = 0;
	player->bubblehealth = 0;

	K_UnsetItemOut(player);
}

// Depletes a bubble shield's health, and pops the shield at 0 health.
void K_RemoveBubbleHealth(player_t *player, INT16 sub)
{
	player->bubblehealth = (UINT8)(max(0, (INT16)(player->bubblehealth) - sub));

	if (player->bubblehealth <= 0)
	{
		K_BreakBubbleShield(player);
		K_PopPlayerShield(player);
	}
}

mobj_t *K_CreatePaperItem(fixed_t x, fixed_t y, fixed_t z, angle_t angle, SINT8 flip, UINT8 type, UINT8 amount)
{
	mobj_t *drop = P_SpawnMobj(x, y, z, MT_FLOATINGITEM);
	P_SetScale(drop, drop->scale>>4);
	drop->destscale = (3*drop->destscale)/2;

	drop->angle = angle;
	P_Thrust(drop, FixedAngle(P_RandomFixed() * 180) + angle, 16*mapobjectscale);

	drop->momz = flip * 3 * mapobjectscale;
	if (drop->eflags & MFE_UNDERWATER)
		drop->momz = (117 * drop->momz) / 200;

	if (type == 0)
	{
		UINT8 useodds = 0;
		INT32 spawnchance[NUMKARTRESULTS];
		INT32 totalspawnchance = 0;
		INT32 i;

		memset(spawnchance, 0, sizeof (spawnchance));

		useodds = amount;

		for (i = 1; i < NUMKARTRESULTS; i++)
		{
			spawnchance[i] = (totalspawnchance += K_KartGetItemOdds(
				useodds, i,
				UINT32_MAX,
                0,
				0,
				false, false, false
				)
			);
		}

		if (totalspawnchance > 0)
		{
			totalspawnchance = P_RandomKey(totalspawnchance);
			for (i = 0; i < NUMKARTRESULTS && spawnchance[i] <= totalspawnchance; i++);

			// TODO: this is bad!
			// K_KartGetItemResult requires a player
			// but item roulette will need rewritten to change this

			const UINT8 newType = K_ItemResultToType(i);
			const UINT8 newAmount = K_ItemResultToAmount(i);

			if (newAmount > 1)
			{
				UINT8 j;

				for (j = 0; j < newAmount-1; j++)
				{
					K_CreatePaperItem(
						x, y, z,
						angle, flip,
						newType, 1
					);
				}
			}

			drop->threshold = newType;
			drop->movecount = 1;
		}
		else
		{
			drop->threshold = 1;
			drop->movecount = 1;
		}
	}
	else
	{
		drop->threshold = type;
		drop->movecount = amount;
	}

	drop->flags |= MF_NOCLIPTHING;

	return drop;
}

// For getting EXTRA hit!
void K_DropItems(player_t *player)
{
	K_DropHnextList(player);

	if (player->mo && !P_MobjWasRemoved(player->mo) && player->itemamount > 0)
	{
		mobj_t *drop = K_CreatePaperItem(
			player->mo->x, player->mo->y, player->mo->z + player->mo->height/2,
			player->mo->angle + ANGLE_90, P_MobjFlip(player->mo),
			player->itemtype, player->itemamount
		);

		K_FlipFromObject(drop, player->mo);
	}

	K_StripItems(player);
}

void K_DropRocketSneaker(player_t *player)
{
	mobj_t *shoe = player->mo;
	fixed_t flingangle;
	boolean leftshoe = true; //left shoe is first

	if (!(player->mo && !P_MobjWasRemoved(player->mo) && player->mo->hnext && !P_MobjWasRemoved(player->mo->hnext)))
		return;

	while ((shoe = shoe->hnext) && !P_MobjWasRemoved(shoe))
	{
		if (shoe->type != MT_ROCKETSNEAKER)
			return; //woah, not a rocketsneaker, bail! safeguard in case this gets used when you're holding non-rocketsneakers

		shoe->renderflags &= ~RF_DONTDRAW;
		shoe->flags &= ~MF_NOGRAVITY;
		shoe->angle += ANGLE_45;

		if (shoe->eflags & MFE_VERTICALFLIP)
			shoe->z -= shoe->height;
		else
			shoe->z += shoe->height;

		//left shoe goes off tot eh left, right shoe off to the right
		if (leftshoe)
			flingangle = -(ANG60);
		else
			flingangle = ANG60;

		S_StartSound(shoe, shoe->info->deathsound);
		P_SetObjectMomZ(shoe, 8*FRACUNIT, false);
		P_InstaThrust(shoe, R_PointToAngle2(shoe->target->x, shoe->target->y, shoe->x, shoe->y)+flingangle, 16*FRACUNIT);
		shoe->momx += shoe->target->momx;
		shoe->momy += shoe->target->momy;
		shoe->momz += shoe->target->momz;
		shoe->extravalue2 = 1;

		leftshoe = false;
	}
	P_SetTarget(&player->mo->hnext, NULL);
	player->rocketsneakertimer = 0;
}

void K_DropKitchenSink(player_t *player)
{
	if (!(player->mo && !P_MobjWasRemoved(player->mo) && player->mo->hnext && !P_MobjWasRemoved(player->mo->hnext)))
		return;

	if (player->mo->hnext->type != MT_SINK_SHIELD)
		return; //so we can just call this function regardless of what is being held

	P_KillMobj(player->mo->hnext, NULL, NULL, DMG_NORMAL);

	P_SetTarget(&player->mo->hnext, NULL);
}

// When an item in the hnext chain dies.
void K_RepairOrbitChain(mobj_t *orbit)
{
	mobj_t *cachenext = orbit->hnext;

	// First, repair the chain
	if (orbit->hnext && orbit->hnext->health && !P_MobjWasRemoved(orbit->hnext))
	{
		P_SetTarget(&orbit->hnext->hprev, orbit->hprev);
		P_SetTarget(&orbit->hnext, NULL);
	}

	if (orbit->hprev && orbit->hprev->health && !P_MobjWasRemoved(orbit->hprev))
	{
		P_SetTarget(&orbit->hprev->hnext, cachenext);
		P_SetTarget(&orbit->hprev, NULL);
	}

	// Then recount to make sure item amount is correct
	if (orbit->target && orbit->target->player && !P_MobjWasRemoved(orbit->target))
	{
		INT32 num = 0;

		mobj_t *cur = orbit->target->hnext;
		mobj_t *prev = NULL;

		while (cur && !P_MobjWasRemoved(cur))
		{
			prev = cur;
			cur = cur->hnext;
			if (++num > orbit->target->player->itemamount)
				P_RemoveMobj(prev);
			else
				prev->movedir = num;
		}

		if (orbit->target && !P_MobjWasRemoved(orbit->target) && orbit->target->player->itemamount != num)
			orbit->target->player->itemamount = num;
	}
}

// Simplified version of a code bit in P_MobjFloorZ
static fixed_t K_BananaSlopeZ(pslope_t *slope, fixed_t x, fixed_t y, fixed_t z, fixed_t radius, boolean ceiling)
{
	fixed_t testx, testy;

	if (slope == NULL)
	{
		testx = x;
		testy = y;
	}
	else
	{
		if (slope->d.x < 0)
			testx = radius;
		else
			testx = -radius;

		if (slope->d.y < 0)
			testy = radius;
		else
			testy = -radius;

		if ((slope->zdelta > 0) ^ !!(ceiling))
		{
			testx = -testx;
			testy = -testy;
		}

		testx += x;
		testy += y;
	}

	return P_GetZAt(slope, testx, testy, z);
}

void K_CalculateBananaSlope(mobj_t *mobj, fixed_t x, fixed_t y, fixed_t z, fixed_t radius, fixed_t height, boolean flip, boolean player)
{
	fixed_t newz;
	sector_t *sec;
	pslope_t *slope = NULL;

	sec = R_PointInSubsector(x, y)->sector;

	if (flip)
	{
		slope = sec->c_slope;
		newz = K_BananaSlopeZ(slope, x, y, sec->ceilingheight, radius, true);
	}
	else
	{
		slope = sec->f_slope;
		newz = K_BananaSlopeZ(slope, x, y, sec->floorheight, radius, true);
	}

	// Check FOFs for a better suited slope
	if (sec->ffloors)
	{
		ffloor_t *rover;

		for (rover = sec->ffloors; rover; rover = rover->next)
		{
			fixed_t top, bottom;
			fixed_t d1, d2;

			if (!(rover->fofflags & FOF_EXISTS))
				continue;

			if ((!(((rover->fofflags & FOF_BLOCKPLAYER && player)
				|| (rover->fofflags & FOF_BLOCKOTHERS && !player))
				|| (rover->fofflags & FOF_QUICKSAND))
				|| (rover->fofflags & FOF_SWIMMABLE)))
				continue;

			top = K_BananaSlopeZ(*rover->t_slope, x, y, *rover->topheight, radius, false);
			bottom = K_BananaSlopeZ(*rover->b_slope, x, y, *rover->bottomheight, radius, true);

			if (flip)
			{
				if (rover->fofflags & FOF_QUICKSAND)
				{
					if (z < top && (z + height) > bottom)
					{
						if (newz > (z + height))
						{
							newz = (z + height);
							slope = NULL;
						}
					}
					continue;
				}

				d1 = (z + height) - (top + ((bottom - top)/2));
				d2 = z - (top + ((bottom - top)/2));

				if (bottom < newz && abs(d1) < abs(d2))
				{
					newz = bottom;
					slope = *rover->b_slope;
				}
			}
			else
			{
				if (rover->fofflags & FOF_QUICKSAND)
				{
					if (z < top && (z + height) > bottom)
					{
						if (newz < z)
						{
							newz = z;
							slope = NULL;
						}
					}
					continue;
				}

				d1 = z - (bottom + ((top - bottom)/2));
				d2 = (z + height) - (bottom + ((top - bottom)/2));

				if (top > newz && abs(d1) < abs(d2))
				{
					newz = top;
					slope = *rover->t_slope;
				}
			}
		}
	}

	//mobj->standingslope = slope;
	P_SetPitchRollFromSlope(mobj, slope);
}

// Move the hnext chain!
static void K_MoveHeldObjects(player_t *player)
{
	TryMoveResult_t result = {0};
	if (!player->mo)
		return;

	if (!player->mo->hnext)
	{
		player->bananadrag = 0;
		if (player->itemflags & IF_EGGMANOUT)
			player->itemflags &= ~IF_EGGMANOUT;
		else if (player->itemflags & IF_ITEMOUT)
		{
			player->itemamount = 0;
			K_UnsetItemOut(player);
			player->itemtype = KITEM_NONE;
		}
		return;
	}

	if (P_MobjWasRemoved(player->mo->hnext))
	{
		// we need this here too because this is done in afterthink - pointers are cleaned up at the START of each tic...
		P_SetTarget(&player->mo->hnext, NULL);
		player->bananadrag = 0;
		if (player->itemflags & IF_EGGMANOUT)
			player->itemflags &= ~IF_EGGMANOUT;
		else if (player->itemflags & IF_EGGMANOUT)
		{
			player->itemamount = 0;
			K_UnsetItemOut(player);
			player->itemtype = KITEM_NONE;
		}
		return;
	}

	switch (player->mo->hnext->type)
	{
		case MT_ORBINAUT_SHIELD: // Kart orbit items
		case MT_JAWZ_SHIELD:
			{
				mobj_t *cur = player->mo->hnext;
				fixed_t speed = ((8 - min(4, player->itemamount)) * cur->info->speed) / 7;

				player->bananadrag = 0; // Just to make sure

				while (cur && !P_MobjWasRemoved(cur))
				{
					const fixed_t radius = FixedHypot(player->mo->radius, player->mo->radius) + FixedHypot(cur->radius, cur->radius); // mobj's distance from its Target, or Radius.
					fixed_t z;

					if (!cur->health)
					{
						cur = cur->hnext;
						continue;
					}

					cur->color = player->skincolor;

					cur->angle -= ANGLE_90;
					cur->angle += FixedAngle(speed);

					if (cur->extravalue1 < radius)
						cur->extravalue1 += P_AproxDistance(cur->extravalue1, radius) / 12;
					if (cur->extravalue1 > radius)
						cur->extravalue1 = radius;

					// If the player is on the ceiling, then flip your items as well.
					if (player && player->mo->eflags & MFE_VERTICALFLIP)
						cur->eflags |= MFE_VERTICALFLIP;
					else
						cur->eflags &= ~MFE_VERTICALFLIP;

					// Shrink your items if the player shrunk too.
					P_SetScale(cur, (cur->destscale = FixedMul(FixedDiv(cur->extravalue1, radius), player->mo->scale)));

					if (P_MobjFlip(cur) > 0)
						z = player->mo->z;
					else
						z = player->mo->z + player->mo->height - cur->height;

					cur->flags |= MF_NOCLIPTHING; // temporarily make them noclip other objects so they can't hit anyone while in the player
					P_MoveOrigin(cur, player->mo->x, player->mo->y, z);
					cur->momx = FixedMul(FINECOSINE(cur->angle>>ANGLETOFINESHIFT), cur->extravalue1);
					cur->momy = FixedMul(FINESINE(cur->angle>>ANGLETOFINESHIFT), cur->extravalue1);
					cur->flags &= ~MF_NOCLIPTHING;

					if (!P_TryMove(cur, player->mo->x + cur->momx, player->mo->y + cur->momy, true, &result))
						P_SlideMove(cur,&result);

					if (P_IsObjectOnGround(player->mo))
					{
						if (P_MobjFlip(cur) > 0)
						{
							if (cur->floorz > player->mo->z - cur->height)
								z = cur->floorz;
						}
						else
						{
							if (cur->ceilingz < player->mo->z + player->mo->height + cur->height)
								z = cur->ceilingz - cur->height;
						}
					}

					// Center it during the scale up animation
					z += (FixedMul(mobjinfo[cur->type].height, player->mo->scale - cur->scale)>>1) * P_MobjFlip(cur);

					cur->z = z;
					cur->momx = cur->momy = 0;
					cur->angle += ANGLE_90;

					cur = cur->hnext;
				}
			}
			break;
		case MT_BANANA_SHIELD: // Kart trailing items
		case MT_SSMINE_SHIELD:
		case MT_EGGMANITEM_SHIELD:
		case MT_SINK_SHIELD:
			{
				mobj_t *cur = player->mo->hnext;
				mobj_t *curnext;
				mobj_t *targ = player->mo;

				if (P_IsObjectOnGround(player->mo) && player->speed > 0)
					player->bananadrag++;

				while (cur && !P_MobjWasRemoved(cur))
				{
					const fixed_t radius = FixedHypot(targ->radius, targ->radius) + FixedHypot(cur->radius, cur->radius);
					angle_t ang;
					fixed_t targx, targy, targz;
					fixed_t speed, dist;

					curnext = cur->hnext;

					if (cur->type == MT_EGGMANITEM_SHIELD)
					{
						// Decided that this should use their "canon" color.
						cur->color = SKINCOLOR_BLACK;
					}

					cur->flags &= ~MF_NOCLIPTHING;

					if ((player->mo->eflags & MFE_VERTICALFLIP) != (cur->eflags & MFE_VERTICALFLIP))
						K_FlipFromObject(cur, player->mo);

					if (!cur->health)
					{
						cur = curnext;
						continue;
					}

					if (cur->extravalue1 < radius)
						cur->extravalue1 += FixedMul(P_AproxDistance(cur->extravalue1, radius), FRACUNIT/12);
					if (cur->extravalue1 > radius)
						cur->extravalue1 = radius;

					if (cur != player->mo->hnext)
					{
						targ = cur->hprev;
						dist = cur->extravalue1/4;
					}
					else
						dist = cur->extravalue1/2;

					if (!targ || P_MobjWasRemoved(targ))
					{
						cur = curnext;
						continue;
					}

					// Shrink your items if the player shrunk too.
					P_SetScale(cur, (cur->destscale = FixedMul(FixedDiv(cur->extravalue1, radius), player->mo->scale)));

					ang = targ->angle;
					targx = targ->x + P_ReturnThrustX(cur, ang + ANGLE_180, dist);
					targy = targ->y + P_ReturnThrustY(cur, ang + ANGLE_180, dist);
					targz = targ->z;

					speed = FixedMul(R_PointToDist2(cur->x, cur->y, targx, targy), 3*FRACUNIT/4);
					if (P_IsObjectOnGround(targ))
						targz = cur->floorz;

					cur->angle = R_PointToAngle2(cur->x, cur->y, targx, targy);

					/*
					if (P_IsObjectOnGround(player->mo) && player->speed > 0 && player->bananadrag > TICRATE
						&& P_RandomChance(min(FRACUNIT/2, FixedDiv(player->speed, K_GetKartSpeed(player, false, false))/2)))
					{
						if (leveltime & 1)
							targz += 8*(2*FRACUNIT)/7;
						else
							targz -= 8*(2*FRACUNIT)/7;
					}
					*/

					if (speed > dist)
						P_InstaThrust(cur, cur->angle, speed-dist);

					P_SetObjectMomZ(cur, FixedMul(targz - cur->z, 7*FRACUNIT/8) - gravity, false);

					if (R_PointToDist2(cur->x, cur->y, targx, targy) > 768*FRACUNIT)
					{
						P_MoveOrigin(cur, targx, targy, cur->z);
						if (P_MobjWasRemoved(cur))
						{
							cur = curnext;
							continue;
						}
					}

					if (P_IsObjectOnGround(cur))
					{
						K_CalculateBananaSlope(cur, cur->x, cur->y, cur->z,
							cur->radius, cur->height, (cur->eflags & MFE_VERTICALFLIP), false);
					}

					cur = curnext;
				}
			}
			break;
		case MT_ROCKETSNEAKER: // Special rocket sneaker stuff
			{
				mobj_t *cur = player->mo->hnext;
				INT32 num = 0;

				while (cur && !P_MobjWasRemoved(cur))
				{
					const fixed_t radius = FixedHypot(player->mo->radius, player->mo->radius) + FixedHypot(cur->radius, cur->radius);
					boolean vibrate = ((leveltime & 1) && !cur->tracer);
					angle_t angoffset;
					fixed_t targx, targy, targz;

					cur->flags &= ~MF_NOCLIPTHING;

					if (player->rocketsneakertimer <= TICRATE && (leveltime & 1))
						cur->renderflags |= RF_DONTDRAW;
					else
						cur->renderflags &= ~RF_DONTDRAW;

					if (num & 1)
						P_SetMobjStateNF(cur, (vibrate ? S_ROCKETSNEAKER_LVIBRATE : S_ROCKETSNEAKER_L));
					else
						P_SetMobjStateNF(cur, (vibrate ? S_ROCKETSNEAKER_RVIBRATE : S_ROCKETSNEAKER_R));

					if (!player->rocketsneakertimer || cur->extravalue2 || !cur->health)
					{
						num = (num+1) % 2;
						cur = cur->hnext;
						continue;
					}

					if (cur->extravalue1 < radius)
						cur->extravalue1 += FixedMul(P_AproxDistance(cur->extravalue1, radius), FRACUNIT/12);
					if (cur->extravalue1 > radius)
						cur->extravalue1 = radius;

					// Shrink your items if the player shrunk too.
					P_SetScale(cur, (cur->destscale = FixedMul(FixedDiv(cur->extravalue1, radius), player->mo->scale)));

#if 1
					{
						angle_t input = player->drawangle - cur->angle;
						boolean invert = (input > ANGLE_180);
						if (invert)
							input = InvAngle(input);

						input = FixedAngle(AngleFixed(input)/4);
						if (invert)
							input = InvAngle(input);

						cur->angle = cur->angle + input;
					}
#else
					cur->angle = player->drawangle;
#endif

					angoffset = ANGLE_90 + (ANGLE_180 * num);

					targx = player->mo->x + P_ReturnThrustX(cur, cur->angle + angoffset, cur->extravalue1);
					targy = player->mo->y + P_ReturnThrustY(cur, cur->angle + angoffset, cur->extravalue1);

					{ // bobbing, copy pasted from my kimokawaiii entry
						fixed_t sine = FixedMul(player->mo->scale, 8 * FINESINE((((M_TAU_FIXED * (4*TICRATE)) * leveltime) >> ANGLETOFINESHIFT) & FINEMASK));
						targz = (player->mo->z + (player->mo->height/2)) + sine;
						if (player->mo->eflags & MFE_VERTICALFLIP)
							targz += (player->mo->height/2 - 32*player->mo->scale)*6;
					}

					if (cur->tracer && !P_MobjWasRemoved(cur->tracer))
					{
						fixed_t diffx, diffy, diffz;

						diffx = targx - cur->x;
						diffy = targy - cur->y;
						diffz = targz - cur->z;

						P_MoveOrigin(cur->tracer, cur->tracer->x + diffx + P_ReturnThrustX(cur, cur->angle + angoffset, 6*cur->scale),
							cur->tracer->y + diffy + P_ReturnThrustY(cur, cur->angle + angoffset, 6*cur->scale), cur->tracer->z + diffz);
						P_SetScale(cur->tracer, (cur->tracer->destscale = 3*cur->scale/4));
					}

					P_MoveOrigin(cur, targx, targy, targz);
					K_FlipFromObject(cur, player->mo);	// Update graviflip in real time thanks.

					cur->roll = player->mo->roll;
					cur->pitch = player->mo->pitch;

					num = (num+1) % 2;
					cur = cur->hnext;
				}
			}
			break;
		default:
			break;
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
		if (gametyperules & GTR_CIRCUIT)
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

// Engine Sounds.
static void K_UpdateEngineSounds(player_t *player)
{
	const INT32 numsnds = 13;

	const fixed_t closedist = 160*FRACUNIT;
	const fixed_t fardist = 1536*FRACUNIT;

	const UINT8 dampenval = 48; // 255 * 48 = close enough to FRACUNIT/6

	const UINT16 buttons = K_GetKartButtons(player);

	INT32 class, s, w; // engine class number

	UINT8 volume = 255;
	fixed_t volumedampen = FRACUNIT;

	INT32 targetsnd = 0;
	INT32 i;

	s = (player->kartspeed - 1) / 3;
	w = (player->kartweight - 1) / 3;

#define LOCKSTAT(stat) \
	if (stat < 0) { stat = 0; } \
	if (stat > 2) { stat = 2; }
	LOCKSTAT(s);
	LOCKSTAT(w);
#undef LOCKSTAT

	class = s + (3*w);

	if (leveltime < 8 || player->spectator)
	{
		// Silence the engines, and reset sound number while we're at it.
		player->karthud[khud_enginesnd] = 0;
		return;
	}

#if 0
	if ((leveltime % 8) != ((player-players) % 8)) // Per-player offset, to make engines sound distinct!
#else
	if (leveltime % 8)
#endif
	{
		// .25 seconds of wait time between each engine sound playback
		return;
	}

	if ((leveltime >= starttime-(2*TICRATE) && leveltime <= starttime) || player->respawn) // Startup boost and dropdashing
	{
		// Startup boosts only want to check for BT_ACCELERATE being pressed.
		targetsnd = ((buttons & BT_ACCELERATE) ? 12 : 0);
	}
	else
	{
		// Average out the value of forwardmove and the speed that you're moving at.
		targetsnd = (((6 * player->cmd.forwardmove) / 25) + ((player->speed / mapobjectscale) / 5)) / 2;
	}

	if (targetsnd < 0) { targetsnd = 0; }
	if (targetsnd > 12) { targetsnd = 12; }

	if (player->karthud[khud_enginesnd] < targetsnd) { player->karthud[khud_enginesnd]++; }
	if (player->karthud[khud_enginesnd] > targetsnd) { player->karthud[khud_enginesnd]--; }

	if (player->karthud[khud_enginesnd] < 0) { player->karthud[khud_enginesnd] = 0; }
	if (player->karthud[khud_enginesnd] > 12) { player->karthud[khud_enginesnd] = 12; }

	// This code calculates how many players (and thus, how many engine sounds) are within ear shot,
	// and rebalances the volume of your engine sound based on how far away they are.

	// This results in multiple things:
	// - When on your own, you will hear your own engine sound extremely clearly.
	// - When you were alone but someone is gaining on you, yours will go quiet, and you can hear theirs more clearly.
	// - When around tons of people, engine sounds will try to rebalance to not be as obnoxious.

	for (i = 0; i < MAXPLAYERS; i++)
	{
		UINT8 thisvol = 0;
		fixed_t dist;

		if (!playeringame[i] || !players[i].mo)
		{
			// This player doesn't exist.
			continue;
		}

		if (players[i].spectator)
		{
			// This player isn't playing an engine sound.
			continue;
		}

		if (P_IsDisplayPlayer(&players[i]))
		{
			// Don't dampen yourself!
			continue;
		}

		dist = P_AproxDistance(
			P_AproxDistance(
				player->mo->x - players[i].mo->x,
				player->mo->y - players[i].mo->y),
				player->mo->z - players[i].mo->z) / 2;

		dist = FixedDiv(dist, mapobjectscale);

		if (dist > fardist)
		{
			// ENEMY OUT OF RANGE !
			continue;
		}
		else if (dist < closedist)
		{
			// engine sounds' approx. range
			thisvol = 255;
		}
		else
		{
			thisvol = (15 * ((closedist - dist) / FRACUNIT)) / ((fardist - closedist) >> (FRACBITS+4));
		}

		volumedampen += (thisvol * dampenval);
	}

	if (volumedampen > FRACUNIT)
	{
		volume = FixedDiv(volume * FRACUNIT, volumedampen) / FRACUNIT;
	}

	if (volume <= 0)
	{
		// Don't need to play the sound at all.
		return;
	}

	S_StartSoundAtVolume(player->mo, (sfx_krta00 + player->karthud[khud_enginesnd]) + (class * numsnds), volume);
}
static void K_UpdateInvincibilitySounds(player_t *player)
{
	INT32 sfxnum = sfx_None;
	boolean localplayer = P_IsLocalPlayer(player);

	if (player->mo->health > 0)
	{
		if (player->growshrinktimer > 0 && (!localplayer || cv_growmusic.value == 2)) // Prioritize Grow
			sfxnum = cv_kartinvinsfx.value ? sfx_alarmg : sfx_kgrow;
		else if (player->invincibilitytimer > 0 && (!localplayer || cv_supermusic.value == 2))
			sfxnum = cv_kartinvinsfx.value ? sfx_alarmi : sfx_kinvnc;
	}

	if (sfxnum != sfx_None && !S_SoundPlaying(player->mo, sfxnum))
		S_StartSound(player->mo, sfxnum);

#define STOPTHIS(this) \
	if (sfxnum != this && S_SoundPlaying(player->mo, this)) \
		S_StopSoundByID(player->mo, this);
	STOPTHIS(sfx_alarmi);
	STOPTHIS(sfx_alarmg);
	STOPTHIS(sfx_kinvnc);
	STOPTHIS(sfx_kgrow);
#undef STOPTHIS
}

static boolean K_DisplayingLapEmblem(player_t *player)
{
	return ((cv_showlapemblem.value & 1) && (player->karthud[khud_lapanimation])) & 1;
}

void K_KartPlayerHUDUpdate(player_t *player)
{
	if (player->karthud[khud_lapanimation])
	{
		player->karthud[khud_lapanimation]--;
	}

	if (player->karthud[khud_splittimer] && (!K_DisplayingLapEmblem(player)))
	{
		player->karthud[khud_splittimer]--;
	}

	if (player->karthud[khud_yougotem])
		player->karthud[khud_yougotem]--;

	if (player->karthud[khud_voices])
		player->karthud[khud_voices]--;

	if (player->karthud[khud_tauntvoices])
		player->karthud[khud_tauntvoices]--;

	if (gametype == GT_RACE)
	{

		if ((K_RingsActive() == true))
		{
			if (player->pflags & PF_RINGLOCK)
			{
				player->karthud[khud_ringlock] = true;
			}
		}
	}

	if ((gametyperules & GTR_BUMPERS) && (player->exiting || player->karmadelay))
	{
		if (player->exiting)
		{
			if (player->exiting < 6*TICRATE)
				player->karthud[khud_cardanimation] += ((164-player->karthud[khud_cardanimation])/8)+1;
			else if (player->exiting == 6*TICRATE)
				player->karthud[khud_cardanimation] = 0;
			else if (player->karthud[khud_cardanimation] < 2*TICRATE)
				player->karthud[khud_cardanimation]++;
		}
		else
		{
			if (player->karmadelay < 6*TICRATE)
				player->karthud[khud_cardanimation] -= ((164-player->karthud[khud_cardanimation])/8)+1;
			else if (player->karmadelay < 9*TICRATE)
				player->karthud[khud_cardanimation] += ((164-player->karthud[khud_cardanimation])/8)+1;
		}

		if (player->karthud[khud_cardanimation] > 164)
			player->karthud[khud_cardanimation] = 164;
		if (player->karthud[khud_cardanimation] < 0)
			player->karthud[khud_cardanimation] = 0;
	}
	else if ((gametyperules & GTR_CIRCUIT) && player->exiting)
	{
		if (player->karthud[khud_cardanimation] < 2*TICRATE)
			player->karthud[khud_cardanimation]++;
	}
	else
		player->karthud[khud_cardanimation] = 0;

	if (player->karthud[khud_afterimagetime])
		player->karthud[khud_afterimagetime]--;
}

#undef RINGANIM_DELAYMAX

// SRB2Kart: blockmap iterate for attraction shield users
static mobj_t *attractmo;
static fixed_t attractdist;
static fixed_t attractzdist;

static inline BlockItReturn_t PIT_AttractingRings(mobj_t *thing)
{
	if (attractmo == NULL || P_MobjWasRemoved(attractmo) || attractmo->player == NULL)
	{
		return BMIT_ABORT;
	}

	if (thing == NULL || P_MobjWasRemoved(thing))
	{
		return BMIT_CONTINUE; // invalid
	}

	if (thing == attractmo)
	{
		return BMIT_CONTINUE; // invalid
	}

	if (!(thing->type == MT_RING || thing->type == MT_FLINGRING))
	{
		return BMIT_CONTINUE; // not a ring
	}

	if (thing->health <= 0)
	{
		return BMIT_CONTINUE; // dead
	}

	if (thing->extravalue1)
	{
		return BMIT_CONTINUE; // in special ring animation
	}

	if (thing->tracer != NULL && P_MobjWasRemoved(thing->tracer) == false)
	{
		return BMIT_CONTINUE; // already attracted
	}

	// see if it went over / under
	if (attractmo->z - attractzdist > thing->z + thing->height)
	{
		return BMIT_CONTINUE; // overhead
	}

	if (attractmo->z + attractmo->height + attractzdist < thing->z)
	{
		return BMIT_CONTINUE; // underneath
	}

	if (P_AproxDistance(attractmo->x - thing->x, attractmo->y - thing->y) > attractdist + thing->radius)
	{
		return BMIT_CONTINUE; // Too far away
	}

	if (RINGTOTAL(attractmo->player) >= attractmo->player->ringmax || (attractmo->player->pflags & PF_RINGLOCK))
	{
		// Already reached max -- just joustle rings around.

		// Regular ring -> fling ring
		if (thing->info->reactiontime && thing->type != (mobjtype_t)thing->info->reactiontime)
		{
			thing->type = thing->info->reactiontime;
			thing->info = &mobjinfo[thing->type];
			thing->flags = thing->info->flags;

			P_InstaThrust(thing, P_RandomRange(0,7) * ANGLE_45, 2 * thing->scale);
			P_SetObjectMomZ(thing, 8<<FRACBITS, false);
			thing->fuse = 120*TICRATE;

			thing->cusval = 0; // Reset attraction flag
		}
	}
	else
	{
		// set target
		P_SetTarget(&thing->tracer, attractmo);
	}

	return BMIT_CONTINUE; // find other rings
}

/** Looks for rings near a player in the blockmap.
  *
  * \param pmo Player object looking for rings to attract
  * \sa A_AttractChase
  */
static void K_LookForRings(mobj_t *pmo)
{
	INT32 bx, by, xl, xh, yl, yh;

	attractmo = pmo;
	attractdist = (400 * pmo->scale);
	attractzdist = attractdist >> 2;

	// Use blockmap to check for nearby rings
	yh = (unsigned)(pmo->y + (attractdist + MAXRADIUS) - bmaporgy)>>MAPBLOCKSHIFT;
	yl = (unsigned)(pmo->y - (attractdist + MAXRADIUS) - bmaporgy)>>MAPBLOCKSHIFT;
	xh = (unsigned)(pmo->x + (attractdist + MAXRADIUS) - bmaporgx)>>MAPBLOCKSHIFT;
	xl = (unsigned)(pmo->x - (attractdist + MAXRADIUS) - bmaporgx)>>MAPBLOCKSHIFT;

	BMBOUNDFIX(xl, xh, yl, yh);

	for (by = yl; by <= yh; by++)
		for (bx = xl; bx <= xh; bx++)
			P_BlockThingsIterator(bx, by, PIT_AttractingRings);
}

static void K_UpdateTripwire(player_t *player)
{
	fixed_t speedThreshold = (3*K_GetKartSpeed(player, false, true))/4;
	boolean goodSpeed = (player->speed >= speedThreshold);
	boolean boostExists = (player->tripwireLeniency > 0); // can't be checked later because of subtractions...
	tripwirepass_t triplevel = K_TripwirePassConditions(player);

	if (triplevel != TRIPWIRE_NONE)
	{
		player->tripwirePass = triplevel;
		player->tripwireLeniency = max(player->tripwireLeniency, TRIPWIRETIME);
	}
	else
	{
		if (boostExists)
		{
			player->tripwireLeniency--;
			if (goodSpeed == false && player->tripwireLeniency > 0)
			{
				// Decrease at double speed when your speed is bad.
				player->tripwireLeniency--;
			}
		}

		if (player->tripwireLeniency <= 0)
		{
			player->tripwirePass = TRIPWIRE_NONE;
		}
	}
}

static void K_RaceStart(player_t *player)
{
	// Start charging once you're given the opportunity.
	if (leveltime >= starttime-(2*TICRATE) && leveltime <= starttime)
	{
		if (player->cmd.buttons & BT_ACCELERATE)
		{
			if (player->boostcharge == 0)
				player->boostcharge = player->cmd.latency;

			// RA ez charge for less annoying starts.
			if (modeattacking != ATTACKING_NONE && !G_CompatLevel(0x0009))
			{
				SINT8 clampsize = 0;
				// Let players choose between startboost and sneaker boost by holding item.
				if (player->cmd.buttons & BT_ATTACK)
					clampsize = 37;
				else
					clampsize = 36;

				player->boostcharge++;
				player->boostcharge = min(player->boostcharge, clampsize);
			}
			else
				player->boostcharge++;
		}
		else
			player->boostcharge = 0;
	}

	// Increase your size while charging your engine.
	if (leveltime < starttime+10)
	{
		player->mo->scalespeed = mapobjectscale/12;
		player->mo->destscale = mapobjectscale + (FixedMul(mapobjectscale, player->boostcharge*131));
		if (K_PlayerShrinkCheat(player) && !modeattacking && !player->bot)
			player->mo->destscale = (6*player->mo->destscale)/8;
	}

	// Determine the outcome of your charge.
	if (leveltime > starttime && player->boostcharge)
	{
		// Not even trying?
		if (player->boostcharge < 35)
		{
			if (player->boostcharge > 17)
				S_StartSound(player->mo, sfx_cdfm00); // chosen instead of a conventional skid because it's more engine-like
		}
		// Get an instant boost!
		else if (player->boostcharge <= 50)
		{
			player->startboost = (50-player->boostcharge)+20;

			if (player->boostcharge <= 36)
			{
				player->startboost = 0;
				K_DoSneaker(player, SNEAKERTYPE_PANEL);
				player->sneakertimer = 70; // PERFECT BOOST!!
				player->realsneakertimer = 70;

				if (!player->floorboost || player->floorboost == 3) // Let everyone hear this one
					S_StartSound(player->mo, sfx_s25f);
			}
			else
			{
				K_SpawnDashDustRelease(player); // already handled for perfect boosts by K_DoSneaker
				if ((!player->floorboost || player->floorboost == 3) && P_IsLocalPlayer(player))
				{
					if (player->boostcharge <= 40)
						S_StartSound(player->mo, sfx_cdfm01); // You were almost there!
					else
						S_StartSound(player->mo, sfx_s23c); // Nope, better luck next time.
				}
			}
		}
		// You overcharged your engine? Those things are expensive!!!
		else if (player->boostcharge > 50)
		{
			player->nocontrol = 40;
			//S_StartSound(player->mo, sfx_kc34);
			S_StartSound(player->mo, sfx_s3k83);
			player->pflags |= PF_SKIDDOWN; // cheeky pflag reuse
		}

		player->boostcharge = 0;
	}

}

UINT8 K_RaceLapCount(INT16 mapNum)
{
	if (!(gametyperules & GTR_CIRCUIT))
	{
		// Not in Race mode
		return 0;
	}

	if (cv_numlaps.value == -1 || modeattacking != ATTACKING_NONE)
	{
		// Use map default
		return mapheaderinfo[mapNum]->numlaps;
	}

	return cv_numlaps.value;
}


static void K_TireGreaseEffect(player_t *player)
{
	const INT16 spawnrange = player->mo->radius>>FRACBITS;

	fixed_t spawnx = P_RandomRange(-spawnrange, spawnrange)<<FRACBITS;
	fixed_t spawny = P_RandomRange(-spawnrange, spawnrange)<<FRACBITS;
	INT32 speedrange = 2;
	mobj_t *dust = P_SpawnMobj(player->mo->x + spawnx, player->mo->y + spawny, player->mo->z, MT_DRIFTDUST);
	dust->momx = FixedMul(player->mo->momx + (P_RandomRange(-speedrange, speedrange)<<FRACBITS), 3*(player->mo->scale)/4);
	dust->momy = FixedMul(player->mo->momy + (P_RandomRange(-speedrange, speedrange)<<FRACBITS), 3*(player->mo->scale)/4);
	dust->momz = P_MobjFlip(player->mo) * (P_RandomRange(1, 4) * (player->mo->scale));
	P_SetScale(dust, player->mo->scale/2);
	dust->destscale = player->mo->scale * 3;
	dust->scalespeed = player->mo->scale/12;
	P_SetTarget(&dust->target, player->mo);

	if (leveltime % 6 == 0)
		S_StartSound(player->mo, sfx_screec);
}

boolean K_BoostChain(player_t *player, INT32 timer, boolean chainsound)
{
	if (!chainingactive)
	{
		// You can't chain why bother?
		return false;
	}

	if (timer > player->chaintimer)
	{
		// Just what I needed! - Toad
		if (cv_kartchainingsound.value && chainsound && player->chaintimer && player->sneakertimer)
		{
			if (player->mo)
			{
				// Chain Sound!
				S_StartSound(player->mo, sfx_bstchn);
			}
		}

		player->chaintimer = timer;
	}

	if (player->chaintimer)
	{
		// You can continue chaining!
		return true;
	}

	// Aw shucks, time to drop the chain....
	return false;
}

INT32 K_ChainOrDeincrementTime(player_t *player, INT32 timer, INT32 deincrement, boolean chainsound)
{
	timer -= deincrement;

	if (K_BoostChain(player, timer, chainsound))
	{
		// Its time to chain.
		return max(1, timer);
	}

	// Continue to drain the timer as normal.
	return timer;
}

// Get the tic inverse sum using kartspeed, kartweight and your number of boosts.
static INT32 K_TicInversesum(UINT8 kartspeed, UINT8 kartweight, UINT8 grade)
{
    return (TICRATE / CLAMP(kartspeed, 1, 5)) + (TICRATE / kartweight) + grade;
}

// Get the threshold for the ringnerf based on kartspeed and kartweight
static INT32 K_StackThreshold(UINT8 kartspeed, UINT8 kartweight)
{
	INT32 scaledsw = (9 - kartspeed) + (9 - kartweight);
    fixed_t result = 4*FRACUNIT - (FixedMul(scaledsw*FRACUNIT/16, 2*FRACUNIT));

    // Stay within range please!
	result = CLAMP(result, 2*FRACUNIT, 4*FRACUNIT);

    return result >> FRACBITS;
}

static void K_HandleRingDeincrement(player_t *player, boolean chainnerf)
{
	// Aggressively reduce extreme ringboost duration.
	// Less aggressive for accel types.
	UINT8 roller = TICRATE;
	UINT16 finalringtimer;
	roller += 4*(8-player->kartspeed);

	finalringtimer = max((player->ringboost / roller), 1);

	if (chainnerf)
	{
		UINT8 requiredgrade = K_StackThreshold(player->kartspeed, player->kartweight);

		if (player->numboosts >= requiredgrade)
		{
			INT32 insum = K_TicInversesum(player->kartspeed, player->kartweight, player->numboosts);
			INT32 subring = (player->ringboost*2)/insum;

			if (player->kartspeed == 1)
			{
				// fuck off chao you aren't chaining an entire race.
				subring = (player->ringboost*4)/insum;
			}

			finalringtimer += subring;
		}

		if (player->ringboost - finalringtimer > player->chaintimer)
		{
			// Cap this to prevent displacement between chaintimer and ringtimer.
			player->chaintimer = min(player->ringboost, (TICRATE - TICRATE/4));
		}
	}

	player->ringboost = max(0, player->ringboost - finalringtimer);
}

// Handles the gaining of rings in a scaled way based on player position
void K_AwardScaledPlayerRings(player_t *player, SINT8 mode)
{
	if (!player)
	{
		// There is no player here, don't award rings.
		return;
	}

	if ((mode == ASR_ITEMBOX) && (K_RingsActive() == false))
	{
		// Rings aren't enabled
		// (and this isn't a super ring)
		// don't award rings.
		return;
	}

	// Let Ring-less maps in on the rings fun as well!
	SINT8 awardamount = (mode == ASR_SUPERRING) ? 15 : 10;
	if (player->position == ASR_SUPERRING)
	{
		awardamount = (mode == ASR_SUPERRING) ? 5 : 3;
	}
	else if(!K_IsPlayerLosing(player))
	{
		awardamount = (mode == ASR_SUPERRING) ? 10 : 5;
	}

	K_AwardPlayerRings(player, awardamount, (mode == ASR_SUPERRING) ? true : false);
}

/**	\brief	Decreases various kart timers and powers per frame. Called in P_PlayerThink in p_user.c

	\param	player	player object passed from P_PlayerThink
	\param	cmd		control input from player

	\return	void
*/
void K_KartPlayerThink(player_t *player, ticcmd_t *cmd)
{
	/* reset sprite offsets :) */
	player->mo->sprxoff = 0;
	player->mo->spryoff = 0;
	player->mo->sprzoff = 0;
	player->mo->spritexoffset = 0;
	player->mo->spriteyoffset = 0;

	player->mo->bakexoff = 0;
	player->mo->bakeyoff = 0;
	player->mo->bakezoff = 0;
	player->mo->bakexpiv = 0;
	player->mo->bakeypiv = 0;
	player->mo->bakezpiv = 0;

	player->cameraOffset = 0;

	if (player->loop.radius)
	{
		// Offset sprite Z position so wheels touch top of
		// hitbox when rotated 180 degrees.
		// TODO: this should be generalized for pitch/roll
		angle_t pitch = FixedAngle(player->loop.revolution * 360) / 2;
		player->mo->sprzoff += FixedMul(player->mo->height, FSIN(pitch));
	}

	K_UpdateOffroad(player);
	K_UpdateDraft(player);

	if (player->draftpower > 0)
	{
		boolean fulldraft = player->draftpower >= FRACUNIT;
		// Draft lines
		K_SpawnDraftSpeedLines(player, FixedMul(player->draftpower, mapobjectscale), fulldraft ? player->skincolor : SKINCOLOR_WHITE, !fulldraft);
	}

	K_UpdateEngineSounds(player); // Thanks, VAda!

	// update boost angle if not spun out
	if (!K_IsPlayerDamaged(player) && !player->wipeoutslow)
		player->boostangle = player->mo->angle;

	K_UpdateSlopeBoost(player);
	K_GetKartBoostPower(player);

	// Special effect objects!
	if (player->mo && !player->spectator)
	{
		if (player->dashpadcooldown) // Twinkle Circuit afterimages
		{
			mobj_t *ghost;
			ghost = P_SpawnGhostMobj(player->mo);
			ghost->fuse = player->dashpadcooldown+1;
			ghost->momx = player->mo->momx / (player->dashpadcooldown+1);
			ghost->momy = player->mo->momy / (player->dashpadcooldown+1);
			ghost->momz = player->mo->momz / (player->dashpadcooldown+1);
			player->dashpadcooldown--;
		}

		if (player->speed > 0)
		{
			// Speed lines
			if (player->sneakertimer || player->ringboost
				|| player->driftboost || player->startboost)
			{
#if 0
				if (player->invincibilitytimer)
					K_SpawnInvincibilitySpeedLines(player->mo);
				else
#endif
					K_SpawnNormalSpeedLines(player);
			}

			// Could probably be moved somewhere else.
			K_HandleFootstepParticles(player->mo);

			if (P_IsObjectOnGround(player->mo))
			{
				if (player->tiregrease)
				{
					K_TireGreaseEffect(player);
				}

				// Draft dust
				if (player->draftpower >= FRACUNIT)
				{
					K_SpawnDraftDust(player->mo);
				}
			}
		}

		// Stacking Effect
		if (stackingactive && player->numboosts > 1)
		{
			K_SpawnStackingEffect(player);
		}
	}

	if (player->itemtype == KITEM_NONE && player->flametimer == 0)
		player->itemflags &= ~IF_HOLDREADY;

	if (player->driftlock)
		player->driftlock--;

	// DKR style camera for boosting
	if (player->karthud[khud_boostcam] != 0 || player->karthud[khud_destboostcam] != 0)
	{
		if (player->karthud[khud_boostcam] < player->karthud[khud_destboostcam]
			&& player->karthud[khud_destboostcam] != 0)
		{
			player->karthud[khud_boostcam] += FRACUNIT/(TICRATE/4);
			if (player->karthud[khud_boostcam] >= player->karthud[khud_destboostcam])
				player->karthud[khud_destboostcam] = 0;
		}
		else
		{
			player->karthud[khud_boostcam] -= FRACUNIT/TICRATE;
			if (player->karthud[khud_boostcam] < player->karthud[khud_destboostcam])
				player->karthud[khud_boostcam] = player->karthud[khud_destboostcam] = 0;
		}
		//CONS_Printf("cam: %d, dest: %d\n", player->karthud[khud_boostcam], player->karthud[khud_destboostcam]);
	}

	player->karthud[khud_timeovercam] = 0;

	// Make ABSOLUTELY SURE that your flashing tics don't get set WHILE you're still in hit animations.
	if (player->spinouttimer != 0
		|| player->wipeoutslow != 0
		|| player->squishedtimer != 0
		|| player->flipovertimer != 0)
	{
		player->flashing = K_GetKartFlashing(player);
	}
	else if (player->flashing >= K_GetKartFlashing(player))
	{
		player->flashing--;
	}

	if (player->flipovertimer)
	{
		player->flipovertimer++;

		// Kill off any extra damage values.
		player->spinouttimer = 0;
		player->wipeoutslow = 0;

		if (P_IsObjectOnGround(player->mo) &&
			((INT32)(player->flipovertimer - 1) > 2))
		{
			player->flipovertimer = 0;
			player->mo->rollangle = 0;
		}
	}

	if (player->spinouttimer)
	{
		if ((P_IsObjectOnGround(player->mo)
			|| ( player->spinouttype & KSPIN_AIRTIMER ))
			&& (!player->sneakertimer))
		{
			player->spinouttimer--;
			if (player->wipeoutslow > 1)
				player->wipeoutslow--;
		}
	}
	else
	{
		if (player->wipeoutslow >= 1)
			player->mo->friction = ORIG_FRICTION;
		player->wipeoutslow = 0;
	}


	if ((K_RingsActive() == false))
	{
		player->rings = 0;
	}
	else
	{
		if (player->rings > player->ringmax)
			player->rings = player->ringmax;
		else if (player->rings < player->ringmin)
			player->rings = player->ringmin;
	}

	if (comeback == false || !(gametyperules & GTR_KARMA) || (player->pflags & PF_ELIMINATED))
	{
		player->karmadelay = comebacktime;
	}
	else if (player->karmadelay > 0 && !P_PlayerInPain(player))
	{
		player->karmadelay--;
		if (P_IsDisplayPlayer(player) && player->bumper <= 0 && player->karmadelay <= 0)
			comebackshowninfo = true; // client has already seen the message
	}

	if (P_IsObjectOnGround(player->mo) && player->pogospring)
	{
		if (P_MobjFlip(player->mo)*player->mo->momz <= 0)
			player->pogospring = 0;
	}

	if (player->tripwireReboundDelay)
		player->tripwireReboundDelay--;

	if (player->ringdelay)
		player->ringdelay--;

	if (P_PlayerInPain(player))
	{
		player->ringboost = 0;
		player->ringtime = 0;
	}
	else if (player->ringboost)
	{
		K_HandleRingDeincrement(player, chainingactive);
	}

	if (!player->ringboost && !player->chaintimer)
		player->ringtime = 0;

	if (player->sneakertimer)
		player->sneakertimer = K_ChainOrDeincrementTime(player, player->sneakertimer, 1, false);

	if (player->sneakertimer <= 0)
	{
		player->mo->flags2 &= ~MF2_WATERRUN;
		player->numsneakers = 0;
	}

	if (player->realsneakertimer)
		player->realsneakertimer--;

	if (player->sneakertimer && player->wipeoutslow > 0 && player->wipeoutslow < wipeoutslowtime+1)
		player->wipeoutslow = wipeoutslowtime+1;

	if (player->floorboost > 0)
		player->floorboost--;

	if (player->driftboost)
		player->driftboost = K_ChainOrDeincrementTime(player, player->driftboost, 1, true);

	if (player->startboost > 0)
		player->startboost = K_ChainOrDeincrementTime(player, player->startboost, 1, false);

	if (player->invincibilitytimer)
	{
		INT16 invinfac = 1;
		if ((K_GetKartInvinType() == KARTINVIN_ALTERN) && ((INT16)(player->invincibilitybottleneck) != -1) && (player->invincibilitytimer > 2))
		{
			UINT32 invindist = INVINDIST >> 2;
			if (player->distancefromcluster < invindist)
			{
				player->invincibilitybottleneck = min(256, player->invincibilitybottleneck + 4);
				invinfac = FixedMul(8, max(min(FRACUNIT, FRACUNIT - (player->distancefromcluster / (INVINDIST >> 2))), 0));
			}
			else
			{
				player->invincibilitybottleneck = max(0, (INT32)(player->invincibilitybottleneck) - 2);
			}

			player->invincibilitytimer = (UINT16)(max(2, (INT32)(player->invincibilitytimer) - max(1, FixedMul((UINT16)invinfac, player->invincibilitybottleneck << 8))));
		}
		else
		{
			player->invincibilitytimer--;
			player->invincibilitybottleneck = 0;
		}
    }
	else
	{
		player->invincibilitybottleneck = 0; // No need for bottlenecking.
		player->maxinvincibilitytime = 0;
		player->invincibilitycancel = -1;
	}

	if (player->checkskip)
		player->checkskip--;

	if (player->bigwaypointgap && (player->bigwaypointgap > AUTORESPAWN_THRESHOLD || !P_PlayerInPain(player)))
	{
		player->bigwaypointgap--;
		if (!player->bigwaypointgap)
			P_KillMobj(player->mo, NULL, NULL, DMG_INSTAKILL);
		else if (player->bigwaypointgap == AUTORESPAWN_THRESHOLD)
		{
			S_StartSound(player->mo, sfx_s26d);
			if (player == &players[consoleplayer])
			{
				// Alert them.
				CONS_Printf("You are going the wrong way! You will automatically respawn in 7 seconds.\n");
			}
		}
	}

	if (player->growshrinktimer != 0)
	{
		if (player->growshrinktimer > 0)
			player->growshrinktimer--;
		if (player->growshrinktimer < 0)
			player->growshrinktimer++;

		// Back to normal
		if (player->growshrinktimer == 0)
			K_RemoveGrowShrink(player);
	}

	if (player->superring)
	{
		player->nextringaward++;
		UINT8 ringrate = 3 - min(2, player->superring / 20); // Used to consume fat stacks of cash faster.
		if (player->nextringaward >= ringrate)
		{
			mobj_t *ring = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_RING);
			ring->extravalue1 = 1; // Ring collect animation timer
			ring->angle = player->mo->angle; // animation angle
			P_SetTarget(&ring->target, player->mo); // toucher for thinker
			player->pickuprings++;
			if (player->superring == 1)
				ring->cvmem = 1; // play caching when collected
			player->nextringaward = 0;
			player->superring--;
		}
	}
	else
	{
		player->nextringaward = 99; // Next time we need to award superring, spawn the first one instantly.
	}

	// Start at lap 1 when using old checkpoint system just to be safe.
	if ((K_UsingLegacyCheckpoints()) && (player->laps == 0) && (numlaps > 0))
		player->laps = 1;

	if (player->stealingtimer == 0 && player->stolentimer == 0
		&& player->rocketsneakertimer)
		player->rocketsneakertimer--;

	if (player->hyudorotimer)
		player->hyudorotimer--;

	if (player->ringvolume < MINRINGVOLUME)
		player->ringvolume = MINRINGVOLUME;
	else if (MAXRINGVOLUME - player->ringvolume < RINGVOLUMEREGEN)
		player->ringvolume = MAXRINGVOLUME;
	else
		player->ringvolume += RINGVOLUMEREGEN;

	// :D
	if (player->ringtransparency < MINRINGTRANSPARENCY)
		player->ringtransparency = MINRINGTRANSPARENCY;
	else if (MAXRINGTRANSPARENCY - player->ringtransparency < RINGTRANSPARENCYREGEN)
		player->ringtransparency = MAXRINGTRANSPARENCY;
	else
		player->ringtransparency += RINGTRANSPARENCYREGEN;

	if (player->sadtimer)
		player->sadtimer--;

	if (player->stealingtimer)
		player->stealingtimer--;

	if (player->stolentimer)
		player->stolentimer--;

	if (player->squishedtimer > 0)
		player->squishedtimer--;

	if (player->justbumped > 0)
		player->justbumped--;

	if (player->outruntime > 0)
		player->outruntime--;

	if (player->tiregrease)
	{
		// Remove grease faster if players are moving slower; players that are recovering
		// from mistakes (or who got sprung purely for track traversal) need steering!
		// Up to 4x degrease speed below 10FU/t (speed at which you lose drift sparks).
		INT16 toDegrease = 1;
		INT16 driftSpeedIncrements = player->speed / (10 * player->mo->scale); // Same breakpoints used for driftcharge stages.
		toDegrease += max(3 - driftSpeedIncrements, 0);

		if (player->tiregrease <= toDegrease)
		{
			player->tiregrease = 0;
		}
		else
		{
			player->tiregrease -= toDegrease;
		}
	}

	if (player->chaintimer)
		player->chaintimer--;

	if (player->itemblink && player->itemblink-- <= 0)
	{
		player->itemblinkmode = KITEMBLINKMODE_NORMAL;
		player->itemblink = 0;
	}

	K_UpdateTripwire(player);

	K_KartPlayerHUDUpdate(player);

	if (P_IsObjectOnGround(player->mo))
		player->waterskip = 0;

	if (player->instashield)
		player->instashield--;

	if (player->eggmanexplode)
	{
		if (player->spectator || (gametype == GT_BATTLE && !player->bumper))
			player->eggmanexplode = 0;
		else
		{
			player->eggmanexplode--;
			if (player->eggmanexplode <= 0)
			{
				mobj_t *eggsexplode;

				K_KartResetPlayerColor(player);

				//player->flashing = 0;
				eggsexplode = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_SPBEXPLOSION);
				if (player->eggmanblame >= 0
				&& player->eggmanblame < MAXPLAYERS
				&& playeringame[player->eggmanblame]
				&& !players[player->eggmanblame].spectator
				&& players[player->eggmanblame].mo)
					P_SetTarget(&eggsexplode->target, players[player->eggmanblame].mo);
			}
		}
	}

	if (player->itemtype == KITEM_BUBBLESHIELD)
	{
		if (player->bubblecool)
			player->bubblecool--;
	}
	else
	{
		player->bubbleblowup = 0;
		player->bubblecool = 0;
		player->bubblehealth = 0;
	}

	if (player->flamedash)
		player->flamedash--;

	if (player->flamestore)
	{
		player->flamestore--;

		if (player->flametimer == 0 || player->flamestore == 0)
		{
			S_StopSoundByID(player->mo, sfx_s3kc2l);
		}
	}

	if (player->flametimer > 0)
	{
		if (player->stealingtimer == 0 && player->stolentimer == 0)
		{
			player->flametimer--;
		}

		if (player->flametimer == 0)
		{
			S_StopSoundByID(player->mo, sfx_s3kc2l);
			S_StartSound(player->mo, sfx_s3k47);
			K_PopPlayerShield(player);
		}
	}
	else
	{
		if (player->flamestore)
			K_FlameDashLeftoverSmoke(player->mo);
	}

	if (cmd->buttons & BT_DRIFT)
	{
		player->pflags |= PF_DRIFTINPUT;
	}
	else
	{
		player->pflags &= ~PF_DRIFTINPUT;
	}

	// Respawn Checker
	if (player->respawn)
		K_RespawnChecker(player);

	// Roulette Code
	K_KartItemRoulette(player, cmd);

	// Gain rings when roulette starts.
	if (player->itemroulette > KROULETTE_DISABLED
		&& player->previtemroulette == KROULETTE_DISABLED
		&& player->roulettetype != KROULETTETYPE_EGGMAN)
	{
		K_AwardScaledPlayerRings(player, ASR_ITEMBOX);
	}

	// set prev state for roulette gain effects
	player->previtemroulette = player->itemroulette;

	// Handle invincibility sfx
	K_UpdateInvincibilitySounds(player); // Also thanks, VAda!

	if (player->tripwireState != TRIPSTATE_NONE)
	{
		if (player->tripwireState == TRIPSTATE_PASSED)
			S_StartSound(player->mo, sfx_cdfm63);
		else if (player->tripwireState == TRIPSTATE_BLOCKED)
			S_StartSound(player->mo, sfx_kc4c);

		player->tripwireState = TRIPSTATE_NONE;
	}

	K_HandleDelayedHitByEm(player);

	// Squishing
	// If a Grow player or a sector crushes you, get flattened instead of being killed.
	if (player->squishedtimer <= 0)
	{
		if (!player->noclip)
		{
			// le no clipping cheat.
			player->mo->flags &= ~MF_NOCLIP;
		}
	}
	else
	{
		player->mo->flags |= MF_NOCLIP;
		player->mo->momx = 0;
		player->mo->momy = 0;
	}

	if (player->noclip)
	{
		// Might as well make it apply this as well.
		player->mo->flags |= MF_NOCLIP;
	}

	if ((player->mo->eflags & MFE_UNDERWATER) && player->curshield != KSHIELD_BUBBLE)
	{
		if (player->breathTimer < UINT16_MAX)
			player->breathTimer++;
	}

	// Do a funny hop!
	K_QuiteSaltyHop(player);

	player->prevonground = P_IsObjectOnGround(player->mo);
}

void K_KartResetPlayerColor(player_t *player)
{
	boolean fullbright = false;

	if (!player->mo || P_MobjWasRemoved(player->mo)) // Can't do anything
		return;

	if (player->mo->health <= 0 || player->playerstate == PST_DEAD) // Override everything
	{
		player->mo->colorized = (player->dye != 0);
		player->mo->color = player->dye ? player->dye : player->skincolor;
		goto finalise;
	}

	if (player->eggmanexplode) // You're gonna diiiiie
	{
		const INT32 flashtime = 4<<(player->eggmanexplode/TICRATE);
		if (player->eggmanexplode % (flashtime/2) != 0)
		{
			;
		}
		else if (player->eggmanexplode % flashtime == 0)
		{
			player->mo->colorized = true;
			player->mo->color = SKINCOLOR_BLACK;
			fullbright = true;
			goto finalise;
		}
		else
		{
			player->mo->colorized = true;
			player->mo->color = SKINCOLOR_CRIMSON;
			fullbright = true;
			goto finalise;
		}
	}

	if (player->invincibilitytimer || player->powers[pw_invulnerability]) // You're gonna kiiiiill
	{
		boolean skip = false;

		if (K_GetKartInvinType() == KARTINVIN_LEGACY)
		{
			fullbright = true;

			player->mo->color = K_RainbowColor(leveltime / 2);

			if (player->invincibilitytimer)
			{
				player->mo->colorized = true;
			}

			skip = true;
        }

		if (skip)
		{
			goto finalise;
		}
	}

	if (player->growshrinktimer) // Ditto, for grow/shrink
	{
		if (player->growshrinktimer % 5 == 0)
		{
			player->mo->colorized = true;
			player->mo->color = (player->growshrinktimer < 0 ? SKINCOLOR_CREAMSICLE : SKINCOLOR_PERIWINKLE);
			fullbright = true;
			goto finalise;
		}
	}

	if (player->ringboost && (leveltime & 1)) // ring boosting
	{
		player->mo->colorized = true;
		fullbright = true;
		goto finalise;
	}
	else
	{
		player->mo->colorized = (player->dye != 0);
		player->mo->color = player->dye ? player->dye : player->skincolor;
	}

finalise:

	if (player->curshield)
	{
		fullbright = true;
	}

	if (fullbright == true)
	{
		player->mo->frame |= FF_FULLBRIGHT;
	}
	else
	{
		if (!(player->mo->state->frame & FF_FULLBRIGHT))
			player->mo->frame &= ~FF_FULLBRIGHT;
	}
}

void K_KartPlayerAfterThink(player_t *player)
{
	K_KartResetPlayerColor(player);

	// Move held objects (Bananas, Orbinaut, etc)
	K_MoveHeldObjects(player);

	// Jawz reticule (seeking)
	if (player->itemtype == KITEM_JAWZ && (player->itemflags & IF_ITEMOUT))
	{
		INT32 lasttarg = player->lastjawztarget;
		player_t *targ;
		mobj_t *ret;

		if (player->jawztargetdelay && playeringame[lasttarg] && !players[lasttarg].spectator)
		{
			targ = &players[lasttarg];
			player->jawztargetdelay--;
		}
		else
			targ = K_FindJawzTarget(player->mo, player);

		if (!targ || !targ->mo || P_MobjWasRemoved(targ->mo))
		{
			player->lastjawztarget = -1;
			player->jawztargetdelay = 0;
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

			player->lastjawztarget = targ-players;
			player->jawztargetdelay = 5;
		}
	}
	else
	{
		player->lastjawztarget = -1;
		player->jawztargetdelay = 0;
	}

	if (player->itemtype == KITEM_THUNDERSHIELD)
	{
		K_LookForRings(player->mo);
	}
}

static int clostVal(fixed_t arr[], int N, fixed_t K)
{
	// Stores the closest
	// value to K
	fixed_t res = arr[0];

	// Traverse the array
	for (int i = 1; i < N; i++)
	{
		// If absolute difference
		// of K and res exceeds
		// absolute difference of K
		// and current element
		if (abs(K - res) > abs(K - arr[i]))
		{
			res = arr[i];
		}
	}

	// Return the closest
	// array element
	return res;
}

// Decides if respawning at this position is safe, used for nu waypoints.
boolean K_SafeRespawnPosition(mobj_t * mo)
{
	respawnresult_t result;
	const boolean flip = (mo->eflags & MFE_VERTICALFLIP) ? true : false;
	fixed_t mobjx = mo->x;
	fixed_t mobjy = mo->y;
	fixed_t mobjz = mo->z;

	result.cursec = R_PointInSubsector(mobjx, mobjy)->sector;
	result.bestfofheight = INT32_MAX;
	result.bestpoheight = INT32_MAX;
	result.bestrover = NULL;
	result.bestpo = NULL;

	if (!result.cursec) // How tf do you do this?????
		return false;

	if (flip)
		result.bestsectorheight = P_GetSectorCeilingZAt(result.cursec,mobjx,mobjy);
	else
		result.bestsectorheight = P_GetSectorFloorZAt(result.cursec,mobjx,mobjy);

	if (result.cursec->ffloors)
	{
		ffloor_t *rover;
		fixed_t curbest = INT32_MAX;
		for (rover = result.cursec->ffloors; rover; rover = rover->next)
		{
			fixed_t compareheight;
			fixed_t valuecompare[2];
			if (!(rover->fofflags & FOF_EXISTS))
				continue;
			if (!(rover->fofflags & FOF_SOLID))
				continue;
			if (!(rover->fofflags & FOF_BLOCKPLAYER)
				&& (rover->fofflags & FOF_BLOCKOTHERS))
				continue;

			if (flip)
				compareheight = P_GetFFloorBottomZAt(rover,mobjx,mobjy);
			else
				compareheight = P_GetFFloorTopZAt(rover,mobjx,mobjy);

			if (curbest == INT32_MAX)
				curbest = compareheight;

			valuecompare[0] = curbest;
			valuecompare[1] = compareheight;

			// compareheight is closer then curbest
			// probably should consider flip
			{
				int N = sizeof(valuecompare) / sizeof(valuecompare[0]);
				fixed_t closetvalue = clostVal(valuecompare, N, mobjz);

				if (closetvalue == valuecompare[1])
				{
					curbest = compareheight;
					result.bestrover = rover;
				}
			}
		}
		result.bestfofheight = curbest;
	}

	if (numPolyObjects)
	{
		fixed_t curbest = INT32_MAX;
		INT32 xl, xh, yl, yh, bx, by;

		yh = (unsigned)(mobjy - bmaporgy)>>MAPBLOCKSHIFT;
		yl = (unsigned)(mobjy - bmaporgy)>>MAPBLOCKSHIFT;
		xh = (unsigned)(mobjx - bmaporgx)>>MAPBLOCKSHIFT;
		xl = (unsigned)(mobjx - bmaporgx)>>MAPBLOCKSHIFT;

		BMBOUNDFIX(xl, xh, yl, yh);

		// Check polyobjects and see if bestpoheight need to be altered
		{
			validcount++;

			for (by = yl; by <= yh; by++)
			{
				for (bx = xl; bx <= xh; bx++)
				{
					INT32 offset;
					polymaplink_t *plink; // haleyjd 02/22/06

					if (bx < 0 || by < 0 || bx >= bmapwidth || by >= bmapheight)
						continue;

					offset = by*bmapwidth + bx;

					// haleyjd 02/22/06: consider polyobject lines
					plink = polyblocklinks[offset];

					while (plink)
					{
						polyobj_t *po = plink->po;

						if (po->validcount != validcount) // if polyobj hasn't been checked
						{
							sector_t *polysec;
							fixed_t compareheight;
							fixed_t valuecompare[2];

							po->validcount = validcount;

							if (!P_BBoxInsidePolyobj(po, g_tm.bbox)
								|| !(po->flags & POF_SOLID))
							{
								plink = (polymaplink_t *)(plink->link.next);
								continue;
							}

							if (!(po->flags & POF_CLIPPLANES))
							{
								plink = (polymaplink_t *)(plink->link.next);
								continue;
							}

							// We're inside it! Yess...
							polysec = po->lines[0]->backsector;

							if (flip)
								compareheight = polysec->ceilingheight;
							else
								compareheight = polysec->floorheight;

							if (curbest == INT32_MAX)
								curbest = compareheight;

							valuecompare[0] = curbest;
							valuecompare[1] = compareheight;

							// compareheight is closer then curbest
							// probably should consider flip
							{
								int N = sizeof(valuecompare) / sizeof(valuecompare[0]);
								fixed_t closetvalue = clostVal(valuecompare, N, mobjz);

								if (closetvalue == valuecompare[1])
								{
									curbest = compareheight;
									result.bestpo = plink->po;
								}
							}
						}

						plink = (polymaplink_t *)(plink->link.next);
					}
				}
			}
			result.bestpoheight = curbest;
		}
	}

	result.finaldistances[0] = result.bestsectorheight;
	result.finaldistances[1] = result.bestfofheight;
	result.finaldistances[2] = result.bestpoheight;

	{
		int N = sizeof(result.finaldistances) / sizeof(result.finaldistances[0]);

		fixed_t closetvalue = clostVal(result.finaldistances, N, mobjz);
		if (closetvalue == result.bestfofheight && (result.bestfofheight != INT32_MAX))
			result.result = RESPAWNRS_ROVER;
		else if (closetvalue == result.bestpoheight && (result.bestpoheight != INT32_MAX))
			result.result = RESPAWNRS_PO;
		else
			result.result = RESPAWNRS_SECTOR;
	}

	// if rover is valid and bestfofheight is closer then bestsectorheight and bestpoheight
	if (result.result == RESPAWNRS_ROVER)
	{
		// Check if rover is bad based on both its terrain, and sector specials.
		if (result.bestrover)
		{
			switch (result.bestrover->master->frontsector->damagetype)
			{
				case SD_GENERIC:
				case SD_DEATHPIT:
				case SD_INSTAKILL:
				case SD_LAVA:
				{
					//CONS_Printf("Dangerous rover!!!!\n");
					return false;
				}
				default:
					break;
			}

			if (result.bestrover->master->frontsector->offroad)
			{
				//CONS_Printf("Offroad rover!!!!\n");
				return false;
			}

			//CONS_Printf("Rover safe, allow respawn!!!!\n");
			return true;

		}
		else
		{
			//CONS_Printf("Nep Fucked up, yell at him! NO ROVER IN FOF CHECK\n");
			return false;
		}

	}
	// if po is valid and bestpoheight is closer then bestsectorheight and bestpoheight
	else if (result.result == RESPAWNRS_PO)
	{
		// Check if po is bad based on both its terrain, and sector specials.
		if (result.bestpo)
		{
			switch (result.bestpo->lines[0]->backsector->damagetype)
			{
				case SD_GENERIC:
				case SD_DEATHPIT:
				case SD_INSTAKILL:
				case SD_LAVA:
				{
					//CONS_Printf("Dangerous polyobj!!!!\n");
					return false;
				}
				default:
					break;
			}

			if (result.bestpo->lines[0]->backsector->offroad)
			{
				//CONS_Printf("Offroad polyobj!!!!\n");
				return false;
			}

			//CONS_Printf("Polyobj safe, allow respawn!!!!\n");
			return true;

		}
		else
		{
			//CONS_Printf("Nep Fucked up, yell at him! NO POLYOBJ IN PO CHECK\n");
			return false;
		}
	}
	// if everything else is not closest check sector
	else if (result.result == RESPAWNRS_SECTOR)
	{
		// Check if sector is bad based on both its terrain, and sector specials.

		switch (result.cursec->damagetype)
		{
			case SD_GENERIC:
			case SD_DEATHPIT:
			case SD_INSTAKILL:
			case SD_LAVA:
			{
				//CONS_Printf("Dangerous sector!!!!\n");
				return false;
			}
			default:
				break;
		}

		if (result.cursec->offroad)
		{
			//CONS_Printf("Offroad Sector!!!!\n");
			return false;
		}

		//CONS_Printf("Sector safe, allow respawn!!!!\n");
		return true;
	}

	CONS_Printf("Nep Fucked up, yell at him! BYPASSED ALL\n");
	return false; // fallback if I fuck up.

}

/*--------------------------------------------------
	size_t K_NextRespawnWaypointIndex(waypoint_t *waypoint)
		See header file for description.
--------------------------------------------------*/
size_t K_NextRespawnWaypointIndex(waypoint_t *waypoint)
{
	size_t           i = 0U;
	size_t newwaypoint = SIZE_MAX;

	// Set to the first valid nextwaypoint, for simplicity's sake.
	// If we reach the last waypoint and it's still not valid, just use it anyway. Someone needs to fix their map!
	for (i = 0U; i < waypoint->numnextwaypoints; i++)
	{
		newwaypoint = i;

		if ((i == waypoint->numnextwaypoints - 1U)
		|| ((K_GetWaypointIsEnabled(waypoint->nextwaypoints[newwaypoint]) == true)
		&& (K_GetWaypointIsSpawnpoint(waypoint->nextwaypoints[newwaypoint]) == true)))
		{
			break;
		}
	}

	return newwaypoint;
}

/*--------------------------------------------------
	static boolean K_SetPlayerNextWaypoint(player_t *player)

		Sets the next waypoint of a player, by finding their closest waypoint, then checking which of itself and next or
		previous waypoints are infront of the player.
		Also sets the current waypoint.

	Input Arguments:-
		player - The player the next waypoint is being found for

	Return:-
		Whether it is safe to update the respawn waypoint
--------------------------------------------------*/
static boolean K_SetPlayerNextWaypoint(player_t *player)
{
	waypoint_t *finishline = K_GetFinishLineWaypoint();
	waypoint_t *bestwaypoint = NULL;
	boolean    updaterespawn = false;

	if ((player != NULL) && (player->mo != NULL) && (P_MobjWasRemoved(player->mo) == false))
	{
		waypoint_t *waypoint     = K_GetBestWaypointForMobj(player->mo, player->currentwaypoint);

		// Our current waypoint.
		bestwaypoint = waypoint;

		if (bestwaypoint != NULL)
		{
			player->currentwaypoint = bestwaypoint;

			// if currentwaypoint is the finish line, don't set nextwaypoint to it!
			// fixes bigwaypointgap triggering if we're facing the back of the finish line
			if (bestwaypoint == finishline)
				bestwaypoint = NULL;
		}

		// check the waypoint's location in relation to the player
		// If it's generally in front, it's fine, otherwise, use the best next/previous waypoint.
		// EXCEPTION: If our best waypoint is the finishline AND we're facing towards it, don't do this.
		// Otherwise it breaks the distance calculations.
		if (waypoint != NULL)
		{
			angle_t playerangle     = player->mo->angle;
			angle_t momangle        = K_MomentumAngle(player->mo);
			angle_t angletowaypoint =
				R_PointToAngle2(player->mo->x, player->mo->y, waypoint->mobj->x, waypoint->mobj->y);
			angle_t angledelta      = ANGLE_180;
			angle_t momdelta        = ANGLE_180;

			angledelta = playerangle - angletowaypoint;
			if (angledelta > ANGLE_180)
			{
				angledelta = InvAngle(angledelta);
			}

			momdelta = momangle - angletowaypoint;
			if (momdelta > ANGLE_180)
			{
				momdelta = InvAngle(momdelta);
			}

			// We're using a lot of angle calculations here, because only using facing angle or only using momentum angle both have downsides.
			// nextwaypoints will be picked if you're facing OR moving forward.
			// prevwaypoints will be picked if you're facing AND moving backward.
#if 0
			if (angledelta > ANGLE_45 || momdelta > ANGLE_45)
#endif
			{
				angle_t nextbestdelta = ANGLE_90;
				angle_t nextbestmomdelta = ANGLE_90;
				angle_t nextbestanydelta = ANGLE_MAX;
				size_t i = 0U;

				if ((waypoint->nextwaypoints != NULL) && (waypoint->numnextwaypoints > 0U))
				{
					for (i = 0U; i < waypoint->numnextwaypoints; i++)
					{
						if (!K_GetWaypointIsEnabled(waypoint->nextwaypoints[i]))
						{
							continue;
						}

						if (K_PlayerUsesBotMovement(player) == true
						&& K_GetWaypointIsShortcut(waypoint->nextwaypoints[i]) == true
						&& K_BotCanTakeCut(player) == false)
						{
							// Bots that aren't able to take a shortcut will ignore shortcut waypoints.
							// (However, if they're already on a shortcut, then we want them to keep going.)

							if (player->nextwaypoint != NULL
							&& K_GetWaypointIsShortcut(player->nextwaypoint) == false)
							{
								continue;
							}
						}

						angletowaypoint = R_PointToAngle2(
							player->mo->x, player->mo->y,
							waypoint->nextwaypoints[i]->mobj->x, waypoint->nextwaypoints[i]->mobj->y);

						angledelta = playerangle - angletowaypoint;
						if (angledelta > ANGLE_180)
						{
							angledelta = InvAngle(angledelta);
						}

						momdelta = momangle - angletowaypoint;
						if (momdelta > ANGLE_180)
						{
							momdelta = InvAngle(momdelta);
						}

						if (angledelta < nextbestanydelta || momdelta < nextbestanydelta)
						{
							nextbestanydelta = min(angledelta, momdelta);

							if (nextbestanydelta >= ANGLE_90)
								continue;

							// Wanted to use a next waypoint, so remove WRONG WAY flag.
							// Done here instead of when set, because of finish line
							// hacks meaning we might not actually use this one, but
							// we still want to acknowledge we're facing the right way.
							player->pflags &= ~PF_WRONGWAY;

							if (waypoint->nextwaypoints[i] != finishline) // Allow finish line.
							{
								if (P_TraceWaypointTraversal(player->mo, waypoint->nextwaypoints[i]->mobj) == false)
								{
									// Save sight checks when all of the other checks pass, so we only do it if we have to
									continue;
								}
							}

							bestwaypoint = waypoint->nextwaypoints[i];

							if (angledelta < nextbestdelta)
							{
								nextbestdelta = angledelta;
							}
							if (momdelta < nextbestmomdelta)
							{
								nextbestmomdelta = momdelta;
							}

							updaterespawn = true;
						}
					}
				}

				if ((waypoint->prevwaypoints != NULL) && (waypoint->numprevwaypoints > 0U)
				&& !(K_PlayerUsesBotMovement(player)) // Bots do not need prev waypoints
				&& waypoint != finishline) // do not check finish line's prevwaypoints
				{
					for (i = 0U; i < waypoint->numprevwaypoints; i++)
					{
						if (!K_GetWaypointIsEnabled(waypoint->prevwaypoints[i]))
						{
							continue;
						}

						angletowaypoint = R_PointToAngle2(
							player->mo->x, player->mo->y,
							waypoint->prevwaypoints[i]->mobj->x, waypoint->prevwaypoints[i]->mobj->y);

						angledelta = playerangle - angletowaypoint;
						if (angledelta > ANGLE_180)
						{
							angledelta = InvAngle(angledelta);
						}

						momdelta = momangle - angletowaypoint;
						if (momdelta > ANGLE_180)
						{
							momdelta = InvAngle(momdelta);
						}

						if (angledelta < nextbestdelta && momdelta < nextbestmomdelta)
						{
							if (waypoint->prevwaypoints[i] == finishline) // NEVER allow finish line.
							{
								continue;
							}

							if (P_TraceWaypointTraversal(player->mo, waypoint->prevwaypoints[i]->mobj) == false)
							{
								// Save sight checks when all of the other checks pass, so we only do it if we have to
								continue;
							}


							bestwaypoint = waypoint->prevwaypoints[i];

							nextbestdelta = angledelta;
							nextbestmomdelta = momdelta;

							// Set wrong way flag if we're using prevwaypoints
							player->pflags |= PF_WRONGWAY;
							updaterespawn = false;
						}
					}
				}
			}
		}

		if (!P_IsObjectOnGround(player->mo))
		{
			updaterespawn = false;
		}

		 if (player->respawn
			|| P_CheckDeathPitCollide(player->mo)
			|| P_InQuicksand(player->mo)
			|| !player->mo->health)
		 {
			 updaterespawn = false;
		 }

		if (player->pflags & PF_UPDATEMYRESPAWN)
		{
			updaterespawn = true;
			player->pflags &= ~PF_UPDATEMYRESPAWN;
		}

		// If nextwaypoint is NULL, it means we don't want to update the waypoint until we touch another one.
		// player->nextwaypoint will keep its previous value in this case.
		if (bestwaypoint != NULL)
		{
			player->nextwaypoint = bestwaypoint;
		}
	}

	return updaterespawn;
}


/*--------------------------------------------------
	static void K_UpdateDistanceFromFinishLine(player_t *const player)

		Updates the distance a player has to the finish line.

	Input Arguments:-
		player - The player the distance is being updated for

	Return:-
		None
--------------------------------------------------*/
static void K_UpdateDistanceFromFinishLine(player_t *player)
{
	if ((player != NULL) && (player->mo != NULL))
	{
		waypoint_t *finishline   = K_GetFinishLineWaypoint();

		// nextwaypoint is now the waypoint that is in front of us
		if ((player->exiting && !(player->pflags & PF_NOCONTEST)) || player->spectator)
		{
			// Player has finished, we don't need to calculate this
			player->distancetofinish = 0U;
		}
		else if (player->pflags & PF_NOCONTEST)
		{
			// We also don't need to calculate this, but there's also no need to destroy the data...
			;
		}
		else if ((player->currentwaypoint != NULL) && (player->nextwaypoint != NULL) && (finishline != NULL))
		{
			const boolean useshortcuts = false;
			const boolean huntbackwards = false;
			boolean pathfindsuccess = false;
			path_t pathtofinish = {0};

			pathfindsuccess =
				K_PathfindToWaypoint(player->nextwaypoint, finishline, &pathtofinish, useshortcuts, huntbackwards);

			// Update the player's distance to the finish line if a path was found.
			// Using shortcuts won't find a path, so distance won't be updated until the player gets back on track
			if (pathfindsuccess == true)
			{
				const boolean pathBackwardsReverse = ((player->pflags & PF_WRONGWAY) == 0);
				boolean pathBackwardsSuccess = false;
				path_t pathBackwards = {0};

				fixed_t disttonext = 0;
				UINT32 traveldist = 0;
				UINT32 adddist = 0;

				disttonext =
					P_AproxDistance(
						(player->mo->x >> FRACBITS) - (player->nextwaypoint->mobj->x >> FRACBITS),
						(player->mo->y >> FRACBITS) - (player->nextwaypoint->mobj->y >> FRACBITS));
				disttonext = P_AproxDistance(disttonext, (player->mo->z >> FRACBITS) - (player->nextwaypoint->mobj->z >> FRACBITS));

				traveldist = ((UINT32)disttonext) * 2;
				pathBackwardsSuccess =
					K_PathfindThruCircuit(player->nextwaypoint, traveldist, &pathBackwards, false, pathBackwardsReverse);

				if (pathBackwardsSuccess == true)
				{
					if (pathBackwards.numnodes > 1)
					{
						// Find the closest segment, and add the distance to reach it.
						vector3_t point;
						size_t i;

						vector3_t best;
						fixed_t bestPoint = INT32_MAX;
						fixed_t bestDist = INT32_MAX;
						UINT32 bestGScore = UINT32_MAX;

						point.x = player->mo->x;
						point.y = player->mo->y;
						point.z = player->mo->z;

						best.x = point.x;
						best.y = point.y;
						best.z = point.z;

						for (i = 1; i < pathBackwards.numnodes; i++)
						{
							vector3_t line[2];
							vector3_t result;

							waypoint_t *pwp = (waypoint_t *)pathBackwards.array[i - 1].nodedata;
							waypoint_t *wp = (waypoint_t *)pathBackwards.array[i].nodedata;

							fixed_t pDist = 0;
							UINT32 g = pathBackwards.array[i - 1].gscore;

							line[0].x = pwp->mobj->x;
							line[0].y = pwp->mobj->y;
							line[0].z = pwp->mobj->z;

							line[1].x = wp->mobj->x;
							line[1].y = wp->mobj->y;
							line[1].z = wp->mobj->z;

							P_ClosestPointOnLine3D(&point, line, &result);

							pDist = P_AproxDistance(point.x - result.x, point.y - result.y);
							pDist = P_AproxDistance(pDist, point.z - result.z);

							if (pDist < bestPoint)
							{
								FV3_Copy(&best, &result);

								bestPoint = pDist;

								bestDist =
									P_AproxDistance(
										(result.x >> FRACBITS) - (line[0].x >> FRACBITS),
										(result.y >> FRACBITS) - (line[0].y >> FRACBITS));
								bestDist = P_AproxDistance(bestDist, (result.z >> FRACBITS) - (line[0].z >> FRACBITS));

								bestGScore = g + ((UINT32)bestDist);
							}
						}

#if 0
						if (cv_kartdebugwaypoints.value)
						{
							mobj_t *debugmobj = P_SpawnMobj(best.x, best.y, best.z, MT_SPARK);
							P_SetMobjState(debugmobj, S_WAYPOINTORB);

							debugmobj->frame &= ~FF_TRANSMASK;
							debugmobj->frame |= FF_FULLBRIGHT; //FF_TRANS20

							debugmobj->tics = 1;
							debugmobj->color = SKINCOLOR_BANANA;
						}
#endif

						adddist = bestGScore;
					}
					/*
					else
					{
						// Only one point to work with, so just add your euclidean distance to that.
						waypoint_t *wp = (waypoint_t *)pathBackwards.array[0].nodedata;
						fixed_t disttowaypoint =
							P_AproxDistance(
								(player->mo->x >> FRACBITS) - (wp->mobj->x >> FRACBITS),
								(player->mo->y >> FRACBITS) - (wp->mobj->y >> FRACBITS));
						disttowaypoint = P_AproxDistance(disttowaypoint, (player->mo->z >> FRACBITS) - (wp->mobj->z >> FRACBITS));

						adddist = (UINT32)disttowaypoint;
					}
					*/
					Z_Free(pathBackwards.array);
				}
				/*
				else
				{
					// Fallback to adding euclidean distance to the next waypoint to the distancetofinish
					adddist = (UINT32)disttonext;
				}
				*/

				if (pathBackwardsReverse == false)
				{
					if (pathtofinish.totaldist > adddist)
					{
						player->distancetofinish = pathtofinish.totaldist - adddist;
					}
					else
					{
						player->distancetofinish = 0;
					}
				}
				else
				{
					player->distancetofinish = pathtofinish.totaldist + adddist;
				}
				Z_Free(pathtofinish.array);

				// distancetofinish is currently a flat distance to the finish line, but in order to be fully
				// correct we need to add to it the length of the entire circuit multiplied by the number of laps
				// left after this one. This will give us the total distance to the finish line, and allow item
				// distance calculation to work easily
				const mapheader_t *mapheader = mapheaderinfo[gamemap - 1];
				if ((mapheader->levelflags & LF_SECTIONRACE) == 0U)
				{
					const UINT8 numfulllapsleft = ((UINT8)numlaps - player->laps) / mapheader->lapspersection;
					player->distancetofinish += numfulllapsleft * K_GetCircuitLength();
				}
			}
		}
	}
}

static UINT32 u32_delta(UINT32 x, UINT32 y)
{
	return x > y ? x - y : y - x;
}

/*--------------------------------------------------
 * static void K_FudgeRespawn(player_t *player, const waypoint_t *const waypoint)
 *
 *	Fudges respawn coordinates to slightly before the waypoint if it would
 *	be exactly on a line. See K_GetWaypointIsOnLine.
 - *-------------------------------------------------*/
static void K_FudgeRespawn(player_t *player, const waypoint_t *const waypoint)
{
	const angle_t from = R_PointToAngle2(waypoint->mobj->x, waypoint->mobj->y,
	player->mo->x, player->mo->y) >> ANGLETOFINESHIFT;

	player->starpostx += FixedMul(16, FINECOSINE(from));
	player->starposty += FixedMul(16, FINESINE(from));
}

void K_SetRespawnAtNextWaypoint(player_t * player)
{
	waypoint_t *currentwaypoint = NULL;
	waypoint_t *safewaypoint = NULL;
	angle_t respawnangle;
	size_t i;

	if (K_UsingLegacyCheckpoints())
	{
		// Woah what are you doing...
		return;
	}

	if ((player != NULL) && (player->mo != NULL) && (P_MobjWasRemoved(player->mo) == false))
	{
		// Safety :P
		if (!player->currentwaypoint || !player->nextwaypoint || !K_SafeRespawnPosition(player->nextwaypoint->mobj))
		{
			waypoint_t *oopisepoint = NULL;

			if (player->nextwaypoint)
			{
				oopisepoint = player->nextwaypoint;
			}
			else
			{
				oopisepoint = K_GetBestWaypointForMobj(player->mo, NULL);
			}

			if (!oopisepoint || !oopisepoint->mobj)
			{
				oopisepoint = K_GetClosestWaypointToMobj(player->mo);
			}

			if (oopisepoint && oopisepoint->numnextwaypoints > 0)
			{
				for (i = 0; i < oopisepoint->numnextwaypoints; i++)
				{
					if (!K_GetWaypointIsEnabled(oopisepoint->nextwaypoints[i]))
					{
						// Waypoint is not enabled.
						continue;
					}

					if (K_GetWaypointIsShortcut(oopisepoint->nextwaypoints[i]))
					{
						// Respawning into offroad/tripwire path would suck.
						continue;
					}

					if (!K_GetWaypointIsSpawnpoint(oopisepoint->nextwaypoints[i]))
					{
						// We can't spawn at these anyway.
						continue;
					}

					if (!oopisepoint->nextwaypoints[i]->mobj)
					{
						// No mobj? No Dice.
						continue;
					}

					if (!K_SafeRespawnPosition(oopisepoint->nextwaypoints[i]->mobj))
					{
						// This waypoint is not safe.
						continue;
					}

					if (K_GetWaypointIsFinishline(oopisepoint->nextwaypoints[i]))
					{
						// Please don't spawn over the line.
						safewaypoint = oopisepoint->nextwaypoints[i];
						break;
					}

					safewaypoint = oopisepoint->nextwaypoints[i];
					break;
				}

				if (safewaypoint)
				{
					oopisepoint = safewaypoint;
				}
				else
				{
					if (player->nextwaypoint)
					{
						oopisepoint = player->nextwaypoint;
					}
				}
			}

			if (oopisepoint && oopisepoint->numprevwaypoints > 0)
			{
				for (i = 0; i < oopisepoint->numprevwaypoints; i++)
				{
					if (!K_GetWaypointIsEnabled(oopisepoint->prevwaypoints[i]))
					{
						// Waypoint is not enabled.
						continue;
					}

					if (K_GetWaypointIsShortcut(oopisepoint->prevwaypoints[i]))
					{
						// Respawning into offroad/tripwire path would suck.
						continue;
					}

					if (!K_GetWaypointIsSpawnpoint(oopisepoint->prevwaypoints[i]))
					{
						// We can't spawn at these anyway.
						continue;
					}

					if (!oopisepoint->prevwaypoints[i]->mobj)
					{
						// No mobj? No Dice.
						continue;
					}

					currentwaypoint = oopisepoint->prevwaypoints[i];
					break;
				}
			}

			CONS_Debug(DBG_GAMELOGIC, M_GetText("Tried to respawn at invalid waypoint! Setting respawn to closest waypoint\n"));
		}
		else
		{
			currentwaypoint = player->currentwaypoint;
			safewaypoint = player->nextwaypoint;
		}

		// Better safe then sorry.
		if (!safewaypoint)
		{

			if (player->nextwaypoint)
			{
				safewaypoint = player->nextwaypoint;
			}
			else
			{
				// Oh shit, oh fuck.....
				CONS_Alert(CONS_WARNING, M_GetText("Tried to respawn at invalid waypoint!\n"));
			}

			return;
		}

		if (!currentwaypoint)
		{
			respawnangle = R_PointToAngle2(player->mo->x, player->mo->y, safewaypoint->mobj->x, safewaypoint->mobj->y);
		}
		else
		{
			respawnangle = R_PointToAngle2(currentwaypoint->mobj->x, currentwaypoint->mobj->y, safewaypoint->mobj->x, safewaypoint->mobj->y);
		}

		if (currentwaypoint && K_GetWaypointIsFinishline(safewaypoint))
		{
			// Please don't spawn after the line.
			player->pflags |= PF_TRUSTWAYPOINTS;
			player->starposttime = player->realtime;
			player->starpostz = currentwaypoint->mobj->z >> FRACBITS;
			player->starpostflip = (currentwaypoint->mobj->flags2 & MF2_OBJECTFLIP);
			player->starpostangle = player->mo->angle;

			// Then do x and y
			player->starpostx = currentwaypoint->mobj->x >> FRACBITS;
			player->starposty = currentwaypoint->mobj->y >> FRACBITS;
			return;
		}

		player->pflags |= PF_TRUSTWAYPOINTS;
		player->starposttime = player->realtime;
		player->starpostz = safewaypoint->mobj->z >> FRACBITS;
		player->starpostflip = (safewaypoint->mobj->flags2 & MF2_OBJECTFLIP);
		player->starpostangle = respawnangle;

		// Then do x and y
		player->starpostx = safewaypoint->mobj->x >> FRACBITS;
		player->starposty = safewaypoint->mobj->y >> FRACBITS;
	}
	else
	{
		CONS_Alert(CONS_WARNING, M_GetText("Tried to respawn invalid player or player mobj!\n"));
		return;
	}
}

static boolean K_MobjIsOnLine(mobj_t *const mobj)
{
	const fixed_t x = mobj->x;
	const fixed_t y = mobj->y;

	line_t *line = P_FindNearestLine(x, y,
			mobj->subsector->sector, -1);

	vertex_t point;

	if (line != NULL)
	{
		P_ClosestPointOnLine(x, y, line, &point);

		if (x == point.x && y == point.y)
			return true;
	}

	return false;
}

static void K_MobjFudgeRespawn(player_t *player, const mobj_t *const mobj)
{
	const angle_t from = R_PointToAngle2(mobj->x, mobj->y,
	player->mo->x, player->mo->y) >> ANGLETOFINESHIFT;

	player->starpostx += FixedMul(25, FINECOSINE(from));
	player->starposty += FixedMul(25, FINESINE(from));
}

/*--------------------------------------------------
	static void K_UpdatePlayerWaypoints(player_t *const player)

		Updates the player's waypoints and finish line distance.

	Input Arguments:-
		player - The player to update

	Return:-
		None
--------------------------------------------------*/
static void K_UpdatePlayerWaypoints(player_t *const player)
{
	if (player->pflags & PF_FREEZEWAYPOINTS)
	{
		player->pflags &= ~PF_FREEZEWAYPOINTS;
		return;
	}

	const UINT32 distance_threshold = FixedMul(32768, mapobjectscale);

	waypoint_t *const old_currentwaypoint = player->currentwaypoint;
	waypoint_t *const old_nextwaypoint = player->nextwaypoint;

	boolean updaterespawn = K_SetPlayerNextWaypoint(player);

	// Update prev value (used for grief prevention code)
	player->distancetofinishprev = player->distancetofinish;
	K_UpdateDistanceFromFinishLine(player);

	UINT32 delta = u32_delta(player->distancetofinish, player->distancetofinishprev);
	if (delta > distance_threshold &&
		!player->respawn && // Respawning should be a full reset.
		old_currentwaypoint != NULL && // So should touching the first waypoint ever.
		player->laps != 0 && // POSITION rooms may have unorthodox waypoints to guide bots.
		!(player->pflags & PF_TRUSTWAYPOINTS)) // Special exception.
	{
#define debug_args "Player %s: waypoint ID %d too far away (%u > %u)\n", \
		sizeu1(player - players), K_GetWaypointID(player->nextwaypoint), delta, distance_threshold
		if (cv_kartdebuglap.value)
			CONS_Printf(debug_args);
		else
			CONS_Debug(DBG_GAMELOGIC, debug_args);
#undef debug_args

		if (!cv_kartdebuglap.value && !player->exiting)
		{
			// Distance jump is too great, keep the old waypoints and old distance.
			player->currentwaypoint = old_currentwaypoint;
			player->nextwaypoint = old_nextwaypoint;
			player->distancetofinish = player->distancetofinishprev;

			// Start the auto respawn timer when the distance jumps.
			if (!player->bigwaypointgap)
			{
				player->bigwaypointgap = AUTORESPAWN_TIME;
			}
		}
	}
	else
	{
		// Reset the auto respawn timer if distance changes are back to normal.
		if (player->bigwaypointgap && player->bigwaypointgap <= AUTORESPAWN_THRESHOLD + 1)
		{
			player->bigwaypointgap = 0;

			// While the player was in the "bigwaypointgap" state, laps did not change from crossing finish lines.
			// So reset the lap back to normal, in case they were able to get behind the line.
			player->laps = player->lastsafelap;
			player->starpostnum = player->lastsafestarpost;
		}
	}

	// Respawn point should only be updated when we're going to a nextwaypoint
	if ((updaterespawn) &&
	(player->bigwaypointgap == 0) &&
	(!player->respawn) &&
	(player->nextwaypoint != old_nextwaypoint) &&
	(K_GetWaypointIsSpawnpoint(player->nextwaypoint)) &&
	(K_GetWaypointIsEnabled(player->nextwaypoint) == true))
	{
		if (!(player->pflags & PF_WRONGWAY))
			player->grieftime = 0;

		player->lastsafelap = player->laps;
		player->lastsafestarpost = player->starpostnum;

		// Check if respawn is safe. If not then goto next spawnpoint and respawn there.
		if (K_SafeRespawnPosition(player->mo))
		{
			// Set time, z, flip and angle first.
			player->starposttime = player->realtime;
			player->starpostz = player->mo->z >> FRACBITS;
			player->starpostflip = (player->mo->eflags & MFE_VERTICALFLIP) ? true : false;
			player->starpostangle = player->mo->angle;

			// Then do x and y
			player->starpostx = player->mo->x >> FRACBITS;
			player->starposty = player->mo->y >> FRACBITS;

			if (K_MobjIsOnLine(player->mo))
			{
				K_MobjFudgeRespawn(player, player->mo);
			}
		}
		else
		{
			// Now a clean function! Neat, eh?
			K_SetRespawnAtNextWaypoint(player);
		}

		if (player->nextwaypoint->onaline)
		{
			K_FudgeRespawn(player, player->nextwaypoint);
		}
	}

	player->pflags &= ~PF_TRUSTWAYPOINTS; // clear special exception
}

INT32 K_GetKartRingPower(const player_t *player, boolean boosted)
{
	fixed_t ringPower = ((9 - player->kartspeed) + (9 - player->kartweight)) * (FRACUNIT/2);
	UINT16 finalPower = 0;
	UINT8 ringcap = 18 - ((9 - player->kartspeed) + (9 - player->kartweight))/2;

	if (boosted == true)
	{
		ringPower = FixedMul(ringPower, K_RingDurationBoost(player));
	}

	if (player->kartspeed == 9 && player->kartweight == 9)
	{
		// Give the poor fuck an extra tic of ring duration so he isn't constantly dropping speed.
		ringPower += 1;
	}

	finalPower = max(ringPower / FRACUNIT, 1);

	// the base is 4 tics
	finalPower += 3;

	// If you use alot of rings at a time, you start gaining less ring timer...
	if (player->ringtime == finalPower*ringcap)
	{
		if (P_IsLocalPlayer(player))
			S_StartSound(NULL, sfx_cdfm66);
	}
	else if (player->ringtime > finalPower*(ringcap+(ringcap/2)))
	{
		finalPower = 1;
	}
	else if (player->ringtime > finalPower*ringcap)
	{
		finalPower = 2;
	}

	return finalPower;
}

// Returns false if this player being placed here causes them to collide with any other player
// Used in g_game.c for match etc. respawning
// This does not check along the z because the z is not correctly set for the spawnee at this point
boolean K_CheckPlayersRespawnColliding(INT32 playernum, fixed_t x, fixed_t y)
{
	INT32 i;
	fixed_t p1radius = players[playernum].mo->radius;
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (playernum == i || !playeringame[i] || players[i].spectator || !players[i].mo || players[i].mo->health <= 0
			|| players[i].playerstate != PST_LIVE || (players[i].mo->flags & MF_NOCLIP) || (players[i].mo->flags & MF_NOCLIPTHING))
			continue;

		if (abs(x - players[i].mo->x) < (p1radius + players[i].mo->radius)
			&& abs(y - players[i].mo->y) < (p1radius + players[i].mo->radius))
		{
			return false;
		}
	}
	return true;
}

// countersteer is how strong the controls are telling us we are turning
// turndir is the direction the controls are telling us to turn, -1 if turning right and 1 if turning left
static INT16 K_GetKartDriftValue(const player_t *player, fixed_t countersteer)
{
	INT16 basedrift, driftangle;
	fixed_t driftweight = player->kartweight*14; // 12

	if (player->drift == 0 || !P_IsObjectOnGround(player->mo))
	{
		// If they aren't drifting or on the ground this doesn't apply
		return 0;
	}

	if (player->pflags & PF_DRIFTEND)
	{
		return -266*player->drift; // Drift has ended and we are tweaking their angle back a bit
	}

	basedrift = 83*player->drift - (driftweight - 14)*player->drift/5; // 415 - 303
	driftangle = abs((252 - driftweight)*player->drift/5);

	if (player->tiregrease > 0) // Buff drift-steering while in greasemode
	{
		basedrift += (basedrift / greasetics) * player->tiregrease;
	}

	return basedrift + FixedMul(driftangle, countersteer);
}

INT16 K_GetKartTurnValue(const player_t *player, INT16 turnvalue)
{
	fixed_t p_topspeed = K_GetKartSpeed(player, false, false);
	fixed_t p_curspeed = min(player->speed, p_topspeed * 2);
	fixed_t p_maxspeed = p_topspeed * 3;
	fixed_t adjustangle = FixedDiv((p_maxspeed>>16) - (p_curspeed>>16), (p_maxspeed>>16) + player->kartweight);

	if (player->spectator)
		return turnvalue;

	if (player->drift != 0 && P_IsObjectOnGround(player->mo))
	{
		// If we're drifting we have a completely different turning value
		if (!(player->pflags & PF_DRIFTEND))
		{
			// 800 is the max set in g_game.c with angleturn
			fixed_t countersteer = FixedDiv(turnvalue*FRACUNIT, 800*FRACUNIT);
			turnvalue = K_GetKartDriftValue(player, countersteer);
		}
		else
			turnvalue = (INT16)(turnvalue + K_GetKartDriftValue(player, FRACUNIT));

		return turnvalue;
	}

	turnvalue = FixedMul(turnvalue, adjustangle); // Weight has a small effect on turning

	if (K_SlipdashActive() && K_Sliptiding(player)) // slight handling boost based on weight
		turnvalue = FixedMul(turnvalue, FRACUNIT + (10 - player->kartweight)*FRACUNIT/48);

	if (player->invincibilitytimer || player->sneakertimer || player->growshrinktimer > 0)
		turnvalue = FixedMul(turnvalue, FixedDiv(5*FRACUNIT, 4*FRACUNIT));

	if (player->flamedash && player->flamestore) // Reduce turning
	{
		fixed_t dashval = ((player->flamedash<<FRACBITS) / TICRATE) / 40; // 1 second = -2.5% handling
		if (dashval > FRACUNIT)
			return 0; // NO MORE TURNING!
		turnvalue = FixedMul(turnvalue, FRACUNIT-dashval);
		//CONS_Printf("dashval: %d\n",dashval);
		//CONS_Printf("turnval: %d\n",turnvalue);
	}

	return turnvalue;
}

INT32 K_GetKartDriftSparkValue(const player_t *player)
{
	UINT8 kartspeed = ((gametyperules & GTR_KARMA) && player->bumper <= 0)
		? 1
		: player->kartspeed;
	return (26*4 + kartspeed*2 + (9 - player->kartweight))*8;
}

INT32 K_GetKartDriftSparkValueForStage(const player_t *player, UINT8 stage)
{
	const INT32 dsv = K_GetKartDriftSparkValue(player);

	// This code is function is pretty much useless now that the timing changes are linear but bleh.
	// G: but it is somewhat useful for purple drift
	switch (stage)
	{
		case 0: return 0;
		default: return dsv;
		case 2: return dsv * 2;
		case 3: return dsv * (K_PurpleDriftActive() ? 3 : 4);
		case 4: return dsv * 4;
	}
}

UINT8 K_GetKartDriftSparkStageForValue(const player_t *player, INT32 value)
{
	const INT32 dsv = K_GetKartDriftSparkValue(player);

	if (value < 0)
		return 0;

	switch (value / dsv)
	{
		case 0: return 0;
		case 1: return 1;
		case 2: return 2;
		case 3: return K_PurpleDriftActive() ? 3 : 2;
		default: return 4;
	}
}

static void K_SpawnDriftEFX(player_t *player,SINT8 level)
{
	if (!cv_kartdriftefx.value)
	{
		// Not wanted sorry!
		return;
	}

	mobj_t *overlay = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_BOOSTFLAME);
	overlay->flags2 |= MF2_DONTSYNC; // No desyncs pls!
	P_SetMobjState(overlay, S_DRIFTBOOSTFLAME);
	P_SetTarget(&overlay->target, player->mo);
	P_SetScale(overlay, (overlay->destscale = player->mo->scale));
	K_FlipFromObject(overlay, player->mo);
	overlay->extravalue1 = level;

	if (level == 1)
		overlay->color = SKINCOLOR_SAPPHIRE;
	else if (level == 2)
		overlay->color = SKINCOLOR_KETCHUP;
	else if (level == 3)
		overlay->color = SKINCOLOR_PURPLE;
}

// Sliptide conditions for Alternative Invincibility.
static boolean K_AltInvinSliptideCondition(player_t *player)
{
	if (K_GetKartInvinType() != KARTINVIN_ALTERN)
	{
		// Not in Alternative, hit the bricks!
		return false;
	}

	// Allow sliptides if you're above half power.
	return (K_InvincibilityGradient(player->invincibilitytimer) > (FRACUNIT/2));
}

static void K_KartDrift(player_t *player, boolean onground)
{
	fixed_t minspeed = (10 * player->mo->scale);
	fixed_t driftadditive = 24;
	const INT32 dsv = K_GetKartDriftSparkValue(player);
	const UINT8 driftstage = K_GetKartDriftSparkStageForValue(player, player->driftcharge);
	UINT16 buttons = K_GetKartButtons(player);

	// Grown players taking yellow spring panels will go below minspeed for one tic,
	// and will then wrongdrift or have their sparks removed because of this.
	// This fixes this problem.
	if (player->pogospring == 2 && player->mo->scale > mapobjectscale)
		minspeed = FixedMul(10<<FRACBITS, mapobjectscale);

	// Drifting is actually straffing + automatic turning.
	// Holding the Jump button will enable drifting.

	// Drift Release (Moved here so you can't "chain" drifts)
	if (player->drift != -5 && player->drift != 5)
	{
		if (driftstage && onground)
		{
			UINT8 boost = 0;

			if (!cv_kartdriftsounds.value || driftstage < 3)
				S_StartSound(player->mo, sfx_s23c);

			switch (driftstage)
			{
			case 1:
				boost = 20;
				break;
			case 2:
				boost = 50;
				if (cv_kartdriftsounds.value)
					S_StartSound(player->mo, sfx_kc5b);
				break;
			case 3:
				boost = 80;
				if (cv_kartdriftsounds.value)
				{
					S_StartSound(player->mo, sfx_kc5b);
					S_StartSound(player->mo, sfx_kc3c);
					S_StartSound(player->mo, sfx_s3k47);
				}
				break;
			case 4:
				boost = 125;
				if (cv_kartdriftsounds.value)
				{
					S_StartSound(player->mo, sfx_kc5b);
					S_StartSound(player->mo, sfx_kc4d);
				}
				break;
			}

			if (player->driftboost < boost)
				player->driftboost = boost;

			K_SpawnDriftEFX(player, driftstage);
		}

		// afterimage, when you release the drift
		if (player->pflags & PF_DRIFTEND || (!onground && player->driftcharge))
		{
			if (!player->karthud[khud_afterimagetime])
				player->karthud[khud_afterimagevalue] = min(player->driftcharge, dsv*4);
			player->karthud[khud_afterimagetime] = 10;
		}

		if (onground)
			player->driftcharge = 0;
	}

	// Drifting: left or right?
	if ((player->cmd.turning > 0) && player->speed > minspeed && (player->pflags & PF_DRIFTINPUT) && (player->drift == 0 || (player->pflags & PF_DRIFTEND)))
	{
		// Starting left drift
		player->drift = 1;
		player->pflags &= ~PF_DRIFTEND;
	}
	else if ((player->cmd.turning < 0) && player->speed > minspeed && (player->pflags & PF_DRIFTINPUT) && (player->drift == 0 || (player->pflags & PF_DRIFTEND)))
	{
		// Starting right drift
		player->drift = -1;
		player->pflags &= ~PF_DRIFTEND;
	}
	else if (!(player->pflags & PF_DRIFTINPUT))
	{
		// drift is not being performed so if we're just finishing set driftend and decrement counters
		if (player->drift > 0)
		{
			player->drift--;
			player->pflags |= PF_DRIFTEND;
		}
		else if (player->drift < 0)
		{
			player->drift++;
			player->pflags |= PF_DRIFTEND;
		}
		else
			player->pflags &= ~PF_DRIFTEND;
	}


	// Incease/decrease the drift value to continue drifting in that direction
	if (!P_PlayerInPain(player) && (player->pflags & PF_DRIFTINPUT) && onground && player->drift != 0)
	{
		if (player->drift >= 1) // Drifting to the left
		{
			player->drift++;
			if (player->drift > 5)
				player->drift = 5;

			if (player->cmd.turning > 0) // Inward
				driftadditive += abs(player->cmd.turning)/100;
			if (player->cmd.turning < 0) // Outward
				driftadditive -= abs(player->cmd.turning)/75;
		}
		else if (player->drift <= -1) // Drifting to the right
		{
			player->drift--;
			if (player->drift < -5)
				player->drift = -5;

			if (player->cmd.turning < 0) // Inward
				driftadditive += abs(player->cmd.turning)/100;
			if (player->cmd.turning > 0) // Outward
				driftadditive -= abs(player->cmd.turning)/75;
		}

		// Disable drift-sparks until you're going fast enough
		if (!(player->pflags & PF_GETSPARKS)
			|| (player->offroad && K_ApplyOffroad(player)))
			driftadditive = 0;

		if (player->speed > minspeed*2)
			player->pflags |= PF_GETSPARKS;

		// Sound whenever you get a different tier of sparks
		if (driftstage < K_GetKartDriftSparkStageForValue(player, player->driftcharge+driftadditive))
		{
			if (P_IsDisplayPlayer(player)) // UGHGHGH...
				S_StartSoundAtVolume(player->mo, sfx_s3ka2, 192); // Ugh...

			player->driftsparkGrowTimer = DRIFTSPARKGROWTICS;
		}

		player->driftcharge += driftadditive;
		player->pflags &= ~PF_DRIFTEND;
	}

	// Spawn Sparks regardless of size
	if (!P_PlayerInPain(player) && player->drift != 0)
	{
		if (driftstage)
			K_SpawnDriftSparks(player);
	}

	if (player->driftsparkGrowTimer)
		player->driftsparkGrowTimer--;

	// Stop drifting
	if (P_PlayerInPain(player) || player->speed < minspeed)
	{
		player->drift = player->driftcharge = player->aizdriftstrat = 0;
		player->pflags &= ~(PF_BRAKEDRIFT|PF_GETSPARKS);
	}

	if ( (!(player->sneakertimer || player->flamestore || K_AltInvinSliptideCondition(player)))
	|| (!player->cmd.turning)
	|| (!player->aizdriftstrat)
	|| (player->cmd.turning > 0) != (player->aizdriftstrat > 0))
	{
		if (!player->drift)
			player->aizdriftstrat = 0;
		else
			player->aizdriftstrat = ((player->drift > 0) ? 1 : -1);
	}
	else if (K_Sliptiding(player))
	{
		K_SpawnAIZDust(player);

		if (abs(player->aizdrifttilt) < ANGLE_22h)
		{
			player->aizdrifttilt =
				(abs(player->aizdrifttilt) + ANGLE_11hh / 4) *
				player->aizdriftstrat;
		}

		if (abs(player->aizdriftturn) < ANGLE_112h)
		{
			player->aizdriftturn =
				(abs(player->aizdriftturn) + ANGLE_11hh) *
				player->aizdriftstrat;
		}
	}

	if (!K_Sliptiding(player))
	{
		player->aizdrifttilt -= player->aizdrifttilt / 4;
		player->aizdriftturn -= player->aizdriftturn / 4;

		if (abs(player->aizdrifttilt) < ANGLE_11hh / 4)
			player->aizdrifttilt = 0;
		if (abs(player->aizdriftturn) < ANGLE_11hh)
			player->aizdriftturn = 0;
	}

	if (player->drift
		&& ((buttons & BT_BRAKE)
		|| !(buttons & BT_ACCELERATE))
		&& P_IsObjectOnGround(player->mo))
	{
		if (!(player->pflags & PF_BRAKEDRIFT))
			K_SpawnBrakeDriftSparks(player);
		player->pflags |= PF_BRAKEDRIFT;
	}
	else
		player->pflags &= ~PF_BRAKEDRIFT;
}

static void K_KartSlipdash(player_t *player, boolean onground)
{
	boolean snaked = player->slipdashdir && player->aizdriftstrat && player->slipdashdir != player->aizdriftstrat;
	if (!K_SlipdashActive() || P_PlayerInPain(player))
	{
		player->slipdashcharge = 0;
		player->slipdashdir = 0;
	}
	else if ((K_Sliptiding(player) || player->aizdriftturn) && !snaked)
	{
		if (!player->slipdashdir) // anti-snaking
			player->slipdashdir = player->aizdriftstrat;

		if (onground && !player->drift)
		{
			fixed_t turn = max(0, player->cmd.turning * player->aizdriftstrat);
			turn = FINETANGENT((turn*(ANGLE_45/KART_FULLTURN) + ANGLE_90) >> ANGLETOFINESHIFT);
			player->slipdashcharge = min(player->slipdashcharge + turn/45, FRACUNIT);

			if (!(leveltime % 5))
				S_StartSoundAtVolume(player->mo, sfx_cdfm17, 133 + turn/555);
		}
	}
	else if (player->slipdashcharge && onground)
	{
		INT32 i;
		boolean driftbonus = player->driftcharge && !snaked;

		if (snaked)
			player->slipdashcharge /= 4;

		S_StopSoundByID(player->mo, sfx_cdfm17);
		S_StartSoundAtVolume(player->mo, player->slipdashcharge > FRACUNIT/3 ? sfx_cdfm62 : sfx_s23c, driftbonus ? 200 : 255);
		P_Thrust(player->mo, player->mo->angle, FixedMul(K_GetKartSpeed(player, false, false), 2*player->slipdashcharge/3));
		K_DoBoost(player, (2*player->slipdashcharge/3)/2, 0, false, false);

		for (i = -1; i <= 1; i += 2)
		{
			mobj_t *strat = P_SpawnMobjFromMobj(player->mo,
				P_ReturnThrustX(NULL, player->mo->angle + ANGLE_67h*i, -20*player->mo->scale),
				P_ReturnThrustY(NULL, player->mo->angle + ANGLE_67h*i, -20*player->mo->scale),
				0, MT_AIZDRIFTSTRAT
			);
			strat->angle = player->mo->angle + ANGLE_67h*i;
			P_SetScale(strat, FixedMul(player->slipdashcharge + FRACUNIT/4, mapobjectscale));
			strat->destscale = strat->scale;
			P_InstaThrust(strat, strat->angle, -8*mapobjectscale);
		}

		if (driftbonus)
		{
			INT32 dsv = K_GetKartDriftSparkValue(player);
			player->driftcharge = min(player->driftcharge + FixedMul(player->slipdashcharge, dsv), dsv-1);
			S_StartSound(player->mo, sfx_s3k45);
		}

		player->slipdashcharge = 0;
		player->slipdashdir = 0;
	}
	else
		player->slipdashdir = 0;

	if (player->slipdashcharge && leveltime & 1)
	{
		mobj_t *spark = P_SpawnMobjFromMobj(player->mo, 0, 0, -4*mapobjectscale, MT_DRIFTSPARK);
		P_SetMobjState(spark, S_SLIPSPARK1);
		spark->color = SKINCOLOR_GREEN;
		spark->angle = player->mo->angle - player->aizdriftstrat*ANGLE_90;
		P_SetScale(spark, FixedMul(5*player->slipdashcharge/4 + FRACUNIT/3, mapobjectscale));
		spark->destscale = spark->scale;
		P_InstaThrust(spark, player->mo->angle, 2*player->speed/3);
		spark->momz = player->mo->z - player->mo->old_z;
		spark->z += spark->momz;
	}
}

INT32 K_GetDriftAngleOffset(player_t *player)
{
	INT32 a = 0;
	boolean onground = P_IsObjectOnGround(player->mo);
	boolean compat = wadfiles[skins[player->skin].wadnum]->compatmode;
	if (player->aizdriftturn)
	{
		a = player->aizdriftturn;
		if (compat)
			a /= 2; // don't turn too much in compatmode
	}
	else if (player->drift != 0 && onground)
	{
		a = (ANGLE_45 / 5) * player->drift;
	}
	return a;
}

static fixed_t K_Distance3D(fixed_t x1, fixed_t y1, fixed_t z1, fixed_t x2, fixed_t y2, fixed_t z2)
{
    fixed_t dist_xy = R_PointToDist2(x1,y1,x2,y2);

    return R_PointToDist2(0,z1,dist_xy,z2);
}

static fixed_t K_PlayerDistance3D(player_t *source, player_t *destination)
{
    if ((!source->mo) || (!destination->mo))
        return INT32_MAX; // Return a garbage value.

    return K_Distance3D(source->mo->x,source->mo->y,source->mo->z,destination->mo->x,destination->mo->y,destination->mo->z);
}

#define MINCLUSTERPLAYERS 6

static UINT32 K_UpdateDistanceFromCluster(player_t *player)
{
	player_t *cluster_p;
	UINT32 i, pingame;
	SINT8 first, second, tinypoptarget;
	INT32 divmul;

	pingame = 0;

	first = -1;
	second = -1;
	tinypoptarget = -1;

	divmul = 1;
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator)
			continue;

		if (!(gametyperules & GTR_BUMPERS) || players[i].bumper)
			pingame++;

		if (players[i].mo && gametype == GT_RACE)
		{
			if (players[i].position == 1 && first == -1)
				first = i;
			else if (players[i].position == 2 && second == -1)
				second = i;
		}
	}

	if (pingame <= 1)
	{
		// There's only us around.
		player->invincibilitybottleneck = (UINT16)(-1); // No bottlenecking!
		return 0;
	}
	else if ((pingame > 3) && (pingame < MINCLUSTERPLAYERS))
	{
		// "Second place" in this case is actually second to last.
		// In very small lobbies, it's harder for players to cluster together.
		second = -1;
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i] || players[i].spectator)
				continue;

			if (players[i].mo && gametype == GT_RACE)
			{
				if (players[i].position == (pingame - 1) && second == -1)
				{
					second = i;
					break;
				}
			}
		}
	}

	tinypoptarget = (pingame < 3) ? first : second;

	if (K_UsingLegacyCheckpoints() && !(gametype == GT_BATTLE))
	{
		// Compare yourself against the cluster player to determine the distance.

		if ((pingame < MINCLUSTERPLAYERS) && (tinypoptarget != -1))
		{
			// In very small lobbies, the cluster player is first place/second to last.

			cluster_p = &players[tinypoptarget];

			// In 1v1s, half the cluster distance so that the losing player
			// doesn't get overly pitied.
			if (pingame < 3)
			{
				divmul = 2;
			}
		}
		else if (clusterid != UINT32_MAX)
		{
			cluster_p = &players[clusterid];
		}

		if ((clusterid == UINT32_MAX) || (!cluster_p))
			return 0; // No cluster player; not good!

		if (player == cluster_p)
			return 0; // We ARE the cluster player.

		if (player->position <= cluster_p->position)
			return 0; // Ahead, or tying.

		// Return the 3D distance from the cluster player.
		// Multiply by 2 to make up for shoddy euclidean distance checks.
		return (K_PlayerDistance3D(player, cluster_p) << 1) / (FRACUNIT*divmul);
	}
	else
	{
		if ((pingame < MINCLUSTERPLAYERS) && (tinypoptarget != -1))
		{
			// In very small lobbies, compare against first place/second to last's
			// distance to finish.

			// In a 1v1, this is theoretically impossible; but what do I know?
			if (player->distancetofinish <= players[tinypoptarget].distancetofinish)
				return 0; // Ahead, or tying.

			// In 1v1s, half the cluster distance so that the losing player
			// doesn't get overly pitied.
			// Otherwise, just use the raw distancing.
			if (pingame < 3)
			{
				return (player->distancetofinish - players[tinypoptarget].distancetofinish) / 2;
			}

			return (player->distancetofinish - players[tinypoptarget].distancetofinish);
		}

		UINT32 targetdtf;

		if (clusterid != UINT32_MAX)
		{
			// Use the cluster player's DtF.

			clusterdtf.x = (INT32)(players[clusterid].distancetofinish);
			targetdtf = players[clusterid].distancetofinish;
		}
		else
		{
			targetdtf = (UINT32)(clusterdtf.x);
		}

		if (player->distancetofinish <= targetdtf)
			return 0; // Ahead, or tying.

		// Return the difference between us and the cluster distance.
		return (player->distancetofinish - targetdtf);
	}
}

#undef MINCLUSTERPLAYERS

//
// K_KartUpdatePosition
//
void K_KartUpdatePosition(player_t *player)
{
	fixed_t position = 1;
	fixed_t oldposition = player->position;
	fixed_t i;

	if (player->spectator || !player->mo)
	{
		// Ensure these are reset for spectators
		player->position = 0;
		player->positiondelay = 0;
		return;
	}

	{
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i] || players[i].spectator || !players[i].mo)
				continue;

			if (gametyperules & GTR_CIRCUIT)
			{
				if (player->exiting) // End of match standings
				{
					// Only time matters
					if (players[i].realtime < player->realtime)
						position++;
				}
				else
				{
					// I'm a lap behind this player OR
					// My distance to the finish line is higher, so I'm behind
					if ((players[i].laps > player->laps)
						|| (players[i].distancetofinish < player->distancetofinish))
					{
						position++;
					}
				}
			}
			else
			{
				if (player->exiting) // End of match standings
				{
					// Only score matters
					if (players[i].roundscore > player->roundscore)
						position++;
				}
				else
				{
					if (player->exiting) // End of match standings
					{
						// Only score matters
						if (players[i].roundscore > player->roundscore)
							position++;
					}
					else
					{
						// I have less points than but the same bumpers as this player OR
						// I have less bumpers than this player
						if ((players[i].bumper == player->bumper && players[i].roundscore > player->roundscore)
							|| (players[i].bumper > player->bumper))
							position++;
					}
				}
			}
		}
	}

	if (leveltime < starttime || oldposition == 0)
		oldposition = position;

	if (position != oldposition) // Changed places?
	{
		player->positiondelay = 10; // Position number growth
	}

	player->position = position;
}

void K_KartLegacyUpdatePosition(player_t *player)
{
	fixed_t position = 1;
	fixed_t oldposition = player->position;
	fixed_t i, ppcd, pncd, ipcd, incd;
	fixed_t pmo, imo;
	mobj_t *mo;

	if (player->spectator || !player->mo)
		return;

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator || !players[i].mo)
			continue;

		if (gametyperules & GTR_CIRCUIT)
		{
			if ((((players[i].starpostnum) + (numstarposts + 1) * players[i].laps) >
				((player->starpostnum) + (numstarposts + 1) * player->laps)))
				position++;
			else if (((players[i].starpostnum) + (numstarposts+1)*players[i].laps) ==
				((player->starpostnum) + (numstarposts+1)*player->laps))
			{
				ppcd = pncd = ipcd = incd = 0;

				player->prevcheck = players[i].prevcheck = 0;
				player->nextcheck = players[i].nextcheck = 0;

				// This checks every thing on the map, and looks for MT_BOSS3WAYPOINT (the thing we're using for checkpoint wp's, for now)
				for (mo = boss3cap; mo != NULL; mo = mo->tracer)
				{
					const boolean isprevcheckpointp = mo->health == player->starpostnum;
					const boolean isnextcheckpointp = mo->health == (player->starpostnum + 1);

					if ((isprevcheckpointp || isnextcheckpointp) && (!mo->movecount || mo->movecount == player->laps))
					{
						pmo = P_AproxDistance(P_AproxDistance(	mo->x - player->mo->x,
																mo->y - player->mo->y),
							mo->z - player->mo->z) / FRACUNIT;

							if (isprevcheckpointp)
							{
								player->prevcheck += pmo;
								ppcd++;
							}

							if (isnextcheckpointp)
							{
								player->nextcheck += pmo;
								pncd++;
							}
					}

					const boolean isprevcheckpointi = mo->health == players[i].starpostnum;
					const boolean isnextcheckpointi = mo->health == (players[i].starpostnum + 1);

					if ((isprevcheckpointi || isnextcheckpointi) && (!mo->movecount || mo->movecount == players[i].laps))
					{
						imo = P_AproxDistance(P_AproxDistance(	mo->x - players[i].mo->x,
																mo->y - players[i].mo->y),
							mo->z - players[i].mo->z) / FRACUNIT;

							if (isprevcheckpointi)
							{
								players[i].prevcheck += imo;
								ipcd++;
							}
							if (isnextcheckpointi )
							{
								players[i].nextcheck += imo;
								incd++;
							}
					}
				}

				if (ppcd > 1) player->prevcheck /= ppcd;
				if (pncd > 1) player->nextcheck /= pncd;
				if (ipcd > 1) players[i].prevcheck /= ipcd;
				if (incd > 1) players[i].nextcheck /= incd;

				if ((players[i].nextcheck > 0 || player->nextcheck > 0) && !player->exiting)
				{
					if ((players[i].nextcheck - players[i].prevcheck) <
						(player->nextcheck - player->prevcheck))
						position++;
				}
				else if (!player->exiting)
				{
					if (players[i].prevcheck > player->prevcheck)
						position++;
				}
				else
				{
					if (players[i].starposttime < player->starposttime)
						position++;
				}
			}
		}
	}

	if (leveltime < starttime || oldposition == 0)
		oldposition = position;

	if (oldposition != position) // Changed places?
		player->positiondelay = 10; // Position number growth

	player->position = position;
}

/*--------------------------------------------------
	UINT32 K_UndoMapScaling(UINT32 distance)

		Takes a raw map distance and adjusts it to
		be in x1 scale.

	Input Arguments:-
		distance - Original distance.

	Return:-
		Distance unscaled by mapobjectscale.
--------------------------------------------------*/
static UINT32 K_UndoMapScaling(UINT32 distance)
{
	if (mapobjectscale != FRACUNIT)
	{
		// Bring back to normal scale.
		return FixedDiv(distance, mapobjectscale);
	}

	return distance;
}


// Brute-force finds the area on the course with the highest player density in a given radius.
// Based on DBSCAN, so it "chain-scans" neighboring players as well for a more accurate result.
UINT32 clusterid = 0;

static vector3_t *K_FindPlayerCluster(fixed_t eps, INT32 (*func)(player_t *, fixed_t, vector3_t *), vector3_t *out)
{
    INT32 bestdensity = 0; // Cluster counter
    vector3_t tempclusterpoint;
    player_t *findme;
    player_t *player;

    INT32 i;
    for (i = 0; i < MAXPLAYERS; i++)
	{
        clusterplayer[i] = false;

		if (!playeringame[i])
			continue;

		player = &players[i];

		if (player->spectator)
			continue; // spectator

		if (!player->mo)
			continue;

        // Find neighbors
        INT32 N = func(player, eps, &tempclusterpoint);

        // Double-remove the clusterplayer flag. Bad hack, I know...
        clusterplayer[i] = false;

        // Density check
        if (N > bestdensity)
        {
            bestdensity = N;

            findme = closesttocluster;

            out->x = tempclusterpoint.x;
            out->y = tempclusterpoint.y;
            out->z = tempclusterpoint.z;
            continue;
        }
    }

	if (bestdensity)
    {
		// Only find the cluster player if a cluster's populated.
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i])
				continue;

			player = &players[i];

			if (player == findme)
			{
				clusterid = i;
				break;
			}
		}
	}

    return out;
}

#define CLUSTER_EPSILON (160*FRACUNIT)
#define K_FindPlayerClusterLegacy() (K_FindPlayerCluster(CLUSTER_EPSILON, K_CountNeighboringPlayers, &clusterpoint))
#define K_FindPlayerClusterDTF() (K_FindPlayerCluster((CLUSTER_EPSILON / FRACUNIT), K_CountNeighboringPlayersByDTF, &clusterdtf))

void K_UpdateClusterPoints(void)
{
    // Get the player cluster.
    if (K_UsingLegacyCheckpoints() && !(gametype == GT_BATTLE))
    {
        K_FindPlayerClusterLegacy();
    }
    else
    {
        if (cv_kartdebugcluster.value)
        {
            K_FindPlayerClusterLegacy();
        }

        K_FindPlayerClusterDTF();
    }
}

void K_UpdateAllPlayerPositions(void)
{
	INT32 i;
	if (!K_UsingLegacyCheckpoints())
	{
		// First loop: Ensure all players' distance to the finish line are all accurate
		for (i = 0; i < MAXPLAYERS; i++)
		{
			player_t* player = &players[i];
			if (!playeringame[i] || player->spectator || !player->mo ||
			    P_MobjWasRemoved(player->mo))
			{
				continue;
			}

			K_UpdatePlayerWaypoints(player);
		}
	}

	// Second loop: Ensure all player positions reflect everyone's distances
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (playeringame[i] && players[i].mo && !P_MobjWasRemoved(players[i].mo))
		{
			if (K_UsingLegacyCheckpoints() && !(gametype == GT_BATTLE))
				K_KartLegacyUpdatePosition(&players[i]);
			else
				K_KartUpdatePosition(&players[i]);

			// Get the cluster distance.
			players[i].distancefromcluster =
			    K_UndoMapScaling(K_UpdateDistanceFromCluster(&players[i]));
		}
	}
}

//
// K_StripItems
//
void K_StripItems(player_t *player)
{
	K_DropRocketSneaker(player);
	K_DropKitchenSink(player);
	player->itemtype = KITEM_NONE;
	player->itemamount = 0;
	player->itemflags &= ~(IF_ITEMOUT|IF_EGGMANOUT);

	if (!player->itemroulette || player->roulettetype != 2)
	{
		player->itemroulette = KROULETTE_DISABLED;
		player->roulettetype = KROULETTETYPE_NORMAL;
	}

	player->hyudorotimer = 0;
	player->stealingtimer = 0;
	player->stolentimer = 0;

	player->curshield = KSHIELD_NONE;
	player->flametimer = 0;
	player->bananadrag = 0;

	player->sadtimer = 0;

	K_UpdateHnextList(player, true);
}

void K_StripOther(player_t *player)
{
	player->itemroulette = KROULETTE_DISABLED;
	player->roulettetype = KROULETTETYPE_NORMAL;

	player->invincibilitytimer = 0;
	player->invincibilitycancel = -1;

	if (player->growshrinktimer)
	{
		K_RemoveGrowShrink(player);
	}

	if (player->eggmanexplode)
	{
		player->eggmanexplode = 0;
		player->eggmanblame = -1;

		K_KartResetPlayerColor(player);
	}
}

SINT8 K_Sliptiding(const player_t *player)
{
	return player->drift ? 0 : player->aizdriftstrat;
}

static void K_AirFailsafe(player_t *player)
{
	const fixed_t maxSpeed = 1*player->mo->scale;
	const fixed_t thrustSpeed = 6*player->mo->scale; // 10*player->mo->scale

	if (leveltime < starttime)
		return;

	if (player->speed > maxSpeed // Above the max speed that you're allowed to use this technique.
		|| player->respawn // Respawning, you don't need this AND drop dash :V
		|| player->carry // Don't do in automated sections.
		|| player->bumpertime // Don't do from nights bumber.
		|| P_PlayerInPain(player)) // Don't assist people hit by mines.
	{
		player->pflags &= ~PF_AIRFAILSAFE;
		return;
	}

	if ((K_GetKartButtons(player) & BT_ACCELERATE) || player->cmd.forwardmove != 0)
	{
		// Queue up later
		player->pflags |= PF_AIRFAILSAFE;
		return;
	}

	if (player->pflags & PF_AIRFAILSAFE)
	{
		// Push the player forward
		P_Thrust(player->mo, K_MomentumAngle(player->mo), thrustSpeed);

		S_StartSound(player->mo, sfx_s23c);
		K_SpawnNormalSpeedLines(player);

		player->pflags &= ~PF_AIRFAILSAFE;
	}
}

//

//
// K_PlayerBaseFriction
//
fixed_t K_PlayerBaseFriction(const player_t *player, fixed_t original)
{
	const fixed_t factor = FixedMul(
		FixedDiv(FRACUNIT - original, FRACUNIT - ORIG_FRICTION),
		K_GetKartGameSpeedScalar(gamespeed)
	);
	fixed_t frict = original;

	if (player->dashpadcooldown == 0 && player->spinouttimer == 0) // attempt to fix Hot Shelter. Also Prevent spinout cheese.
	{
		if (K_PlayerUsesBotMovement(player) == true)
		{
			const fixed_t speedPercent = min(FRACUNIT, FixedDiv(player->speed, K_GetKartSpeed(player, false, false)));
			const fixed_t extraFriction = FixedMul(FixedMul(FRACUNIT >> 5, factor), speedPercent);

			// A bit extra friction to help them without drifting.
			// Remove this line once they can drift.
			frict -= extraFriction;

			// Bots gain more traction as they rubberband.
			const fixed_t traction_value = FixedMul(player->botvars.rubberband, max(FRACUNIT, K_BotMapModifier()));
			if (traction_value > FRACUNIT)
			{
				const fixed_t traction_mul = traction_value - FRACUNIT;
				frict -= FixedMul(extraFriction, traction_mul);
			}

			if (frict > FRACUNIT) { frict = FRACUNIT; }
			if (frict < 0) { frict = 0; }
		}
	}

	return frict;
}

static inline void K_RecalculateMovefactor(mobj_t *mobj)
{
	if (mobj->friction > FRACUNIT)
		mobj->friction = FRACUNIT;
	if (mobj->friction < 0)
		mobj->friction = 0;

	mobj->movefactor = FixedDiv(ORIG_FRICTION, mobj->friction);

	if (mobj->movefactor < FRACUNIT)
		mobj->movefactor = 19*mobj->movefactor - 18*FRACUNIT;
	else
		mobj->movefactor = FRACUNIT;

	if (mobj->movefactor < 32)
		mobj->movefactor = 32;
}

//
static void K_AdjustPlayerFriction(player_t *player)
{
	boolean onground = P_IsObjectOnGround(player->mo);
	const fixed_t prevfriction = K_PlayerBaseFriction(player, player->mo->friction);

	if (!onground)
	{
		// Don't Calculate friction in the air.
		return;

	}

	if (player->pogospring)
	{
		// JugadorXEI: Do *not* calculate friction when a player is pogo'd
		// because they'll be in the air and friction will not reset!
		return;
	}

	player->mo->friction = prevfriction;

	// Reduce friction after exiting a loop.
	if (player->tiregrease)
	{
		player->mo->friction += ((FRACUNIT - prevfriction) / greasetics) * player->tiregrease;

		K_RecalculateMovefactor(player->mo);
	}

	// Friction
	if (!player->offroad)
	{
		if (player->speed > 0 && player->cmd.forwardmove == 0 && player->mo->friction == 59392)
			player->mo->friction += 4608;
	}

	if (player->speed > 0 && player->cmd.forwardmove < 0)	// change friction while braking no matter what, otherwise it's not any more effective than just letting go off accel
		player->mo->friction -= 2048;

	// Karma ice physics
	if ((gametyperules & GTR_KARMA) && player->bumper <= 0)
	{
		player->mo->friction += 1228;

		K_RecalculateMovefactor(player->mo);
	}

	if (P_WaterRunning(player->mo))
	{
		player->mo->friction += 614;

		K_RecalculateMovefactor(player->mo);
	}

	// Wipeout slowdown, or getting flipped over
	if ((player->speed > 0 && player->spinouttimer && player->wipeoutslow) || (player->flipovertimer))
	{
		if (player->offroad)
			player->mo->friction -= 4912;
		if ((player->wipeoutslow == 1) || (player->flipovertimer))
			player->mo->friction -= 9824;
	}
}

void K_SetTireGrease(player_t *player, tic_t tics)
{
	if (player->pogospring > 0)
		return;

	player->tiregrease = tics;
}

boolean K_SlopeResistance(const player_t *player)
{
	if (player->tiregrease)
		return true;
	return false;
}

INT32 K_GetShieldFromPlayer(player_t *player)
{

	if (player->flametimer > 0)
	{
		return KSHIELD_FLAME;
	}

	switch (player->itemtype)
	{
		case KITEM_THUNDERSHIELD: return KSHIELD_THUNDER;
		case KITEM_BUBBLESHIELD: return KSHIELD_BUBBLE;
		//case KITEM_FLAMESHIELD: return KSHIELD_FLAME; not active until flametimer is active.
		default: return KSHIELD_NONE;
	}
}

INT32 K_GetShieldFromItem(INT32 item)
{
	switch (item)
	{
		case KITEM_THUNDERSHIELD: return KSHIELD_THUNDER;
		case KITEM_BUBBLESHIELD: return KSHIELD_BUBBLE;
		case KITEM_FLAMESHIELD: return KSHIELD_FLAME;
		default: return KSHIELD_NONE;
	}
}

void K_SetItemOut(player_t *player)
{
	player->itemflags |= IF_ITEMOUT;
}

void K_UnsetItemOut(player_t *player)
{
	player->itemflags &= ~IF_ITEMOUT;
	player->bananadrag = 0;
}

static boolean K_InvincibilitySlotHogging(player_t *player)
{
	if (K_GetKartInvinType() == KARTINVIN_LEGACY)
		return false;

	return ((player->invincibilitytimer) != 0);
}

//
// K_MoveKartPlayer
//
void K_MoveKartPlayer(player_t *player, boolean onground)
{
	UINT16 buttons = K_GetKartButtons(player);
	boolean ATTACK_IS_DOWN = ((buttons & BT_ATTACK) && !(player->oldcmd.buttons & BT_ATTACK));
	boolean HOLDING_ITEM = (player->itemflags & (IF_ITEMOUT|IF_EGGMANOUT));
	boolean NO_HYUDORO = (player->stealingtimer == 0 && player->stolentimer == 0);

	if (!player->exiting)
	{
		if (player->oldposition < player->position) // But first, if you lost a place,
		{
			player->oldposition = player->position; // then the other player taunts.
			K_RegularVoiceTimers(player); // and you can't for a bit
		}
		else if (player->oldposition > player->position) // Otherwise,
		{
			K_PlayOvertakeSound(player->mo); // Say "YOU'RE TOO SLOW!"
			player->oldposition = player->position; // Restore the old position,
		}
	}

	if (player->positiondelay)
		player->positiondelay--;

	// Prevent ring misfire
	if (!(buttons & BT_ATTACK))
	{
		if (player->itemtype == KITEM_NONE
			&& NO_HYUDORO && (K_RingsActive() == true) && !(HOLDING_ITEM
			|| player->itemamount
			|| player->itemroulette
			|| player->rocketsneakertimer
			|| player->eggmanexplode
			|| ((K_GetKartInvinType() == KARTINVIN_ALTERN) && player->invincibilitytimer)
			|| (player->growshrinktimer > 0)
			|| player->flametimer
			|| (leveltime < starttime)))
			player->itemflags |= IF_USERINGS;
		else
			player->itemflags &= ~IF_USERINGS;
	}

	if (player->exiting && player->bubbleblowup > 0)
		player->bubbleblowup--;

	if (player && player->mo && player->mo->health > 0 && !player->spectator && !P_PlayerInPain(player) && player->respawn == 0 && !(player->exiting || mapreset))
	{
		// First, the really specific, finicky items that function without the item being directly in your item slot.
		{
			// Karma item dropping
			if (ATTACK_IS_DOWN && player->karmamode && !player->karmadelay)
			{
				mobj_t *newitem;

				if (player->karmamode == 1)
				{
					newitem = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_RANDOMITEM);
					newitem->threshold = 69; // selected "randomly".
				}
				else
				{
					newitem = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_EGGMANITEM);
					if (player->eggmanblame >= 0
						&& player->eggmanblame < MAXPLAYERS
						&& playeringame[player->eggmanblame]
						&& !players[player->eggmanblame].spectator
						&& players[player->eggmanblame].mo)
						P_SetTarget(&newitem->target, players[player->eggmanblame].mo);
					player->eggmanblame = -1;
				}

				newitem->flags2 = (player->mo->flags2 & MF2_OBJECTFLIP);
				newitem->fuse = 15*TICRATE; // selected randomly.

				player->karmamode = 0;
				player->karmadelay = comebacktime;
				S_StartSound(player->mo, sfx_s254);
			}
			// Ring boosting
			else if (player->itemflags & IF_USERINGS)
			{
				if ((buttons & BT_ATTACK) && !player->ringdelay && player->rings > 0)
				{
					mobj_t *ring = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_RING);
					P_SetMobjState(ring, S_FASTRING1);

					if (P_IsDisplayPlayer(player))
					{
						UINT8 startfade = 220;
						UINT8 transfactor = 10 * (min(startfade, player->ringtransparency)) / startfade;
						if (transfactor < 10)
						{
							transfactor = max(transfactor, 4);
							ring->renderflags |= ((10-transfactor) << RF_TRANSSHIFT);
							ring->renderflags |= RF_ADD;
						}
					}
					player->ringtransparency -= RINGTRANSPARENCYUSEPENALTY;

					ring->extravalue1 = 1; // Ring use animation timer
					ring->extravalue2 = 1; // Ring use animation flag
					ring->shadowscale = 0;
					P_SetTarget(&ring->target, player->mo); // user
					player->rings--;
					player->ringdelay = 3;
				}
			}
			// Other items
			else
			{
				// Eggman Monitor exploding
				if (player->eggmanexplode)
				{
					if (ATTACK_IS_DOWN && player->eggmanexplode <= 3*TICRATE && player->eggmanexplode > 1)
					{
						player->eggmanexplode = 1;
						K_BotResetItemConfirm(player, false);
					}
				}
				// Eggman Monitor throwing
				else if (player->itemflags & IF_EGGMANOUT)
				{
					if (ATTACK_IS_DOWN)
					{
						K_ThrowKartItem(player, false, MT_EGGMANITEM, -1, 0);
						K_PlayAttackTaunt(player->mo);
						player->itemflags &= ~IF_EGGMANOUT;
						K_UpdateHnextList(player, true);
						K_BotResetItemConfirm(player, false);
					}
				}
				// Rocket Sneaker usage
				else if (player->rocketsneakertimer > 1)
				{
					if (ATTACK_IS_DOWN && !HOLDING_ITEM && onground && NO_HYUDORO)
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
				// Flame Shield Usage
				else if (player->flametimer > 0)
				{
					if (!HOLDING_ITEM && NO_HYUDORO)
					{
						if ((buttons & BT_ATTACK) && (player->itemflags & IF_HOLDREADY))
						{
							// TODO: gametyperules
							const SINT8 incr = gametype == GT_BATTLE ? 3 : 2;
							const SINT8 metincr = gametype == GT_BATTLE ? 4 : 3;
							const SINT8 comincr = gametype == GT_BATTLE ? 10 : 4;

							if (player->flamestore == 0)
							{
								S_StartSound(player->mo, sfx_s3k48);
								K_PlayBoostTaunt(player->mo);
							}

							// Usage sound
							if (!S_SoundPlaying(player->mo, sfx_s3kc2l) && player->flametimer != 0 && player->flamestore != 0)
								S_StartSound(player->mo, sfx_s3kc2l);

							if (onground)
							{
								player->flamedash += incr;
								player->flamestore = min(player->flamestore + metincr, FLAMESTOREMAX);
								player->flametimer -= comincr;
							}
							/*else
							{
								player->flamedash += incr;
								player->flamestore = min(player->flamestore + metincr, FLAMESTOREMAX);
								player->flametimer -= comincr;

								P_Thrust(
									player->mo, player->mo->angle,
									FixedMul(player->mo->scale, K_GetKartGameSpeedScalar(gamespeed))/4
									);
							}*/

							if (player->flametimer <= 0)
							{
								player->flametimer = 0;
								S_StartSound(player->mo, sfx_s3k47);
								K_PopPlayerShield(player);
							}
						}
						else
						{
							if (buttons & BT_ATTACK)
							{
								player->itemflags &= ~IF_HOLDREADY;
							}
							else
							{
								player->itemflags |= IF_HOLDREADY;
							}
						}
					}
				}
				// Grow Canceling
				else if (player->growshrinktimer > 0)
				{
					if (player->growcancel >= 0)
					{
						if (buttons & BT_ATTACK)
						{
							player->growcancel++;
							if (player->growcancel > 26)
								K_RemoveGrowShrink(player);
						}
						else
							player->growcancel = 0;
					}
					else
					{
						if ((buttons & BT_ATTACK) || (player->oldcmd.buttons & BT_ATTACK))
							player->growcancel = -1;
						else
							player->growcancel = 0;
					}
				}
				// Invincibility
				else if (K_InvincibilitySlotHogging(player))
				{
					// (Alternate) Cancel Invincibility
					if (player->invincibilitycancel >= 0)
					{
						if (buttons & BT_ATTACK)
						{
							player->invincibilitycancel++;
							if (player->invincibilitycancel > 25)
							{
								// Don't fully cancel due to how the music handling works.
								player->invincibilitytimer = 1;
							}
						}
						else
							player->invincibilitycancel = 0;
					}
					else
					{
						if ((buttons & BT_ATTACK) || (player->oldcmd.buttons & BT_ATTACK))
							player->invincibilitycancel = -1;
						else
							player->invincibilitycancel = 0;
					}
				}
				else if (player->itemamount == 0)
				{
					K_UnsetItemOut(player);
				}
				else
				{
					switch (player->itemtype)
					{
						case KITEM_SNEAKER:
							if (ATTACK_IS_DOWN && !HOLDING_ITEM && onground && NO_HYUDORO)
							{
								K_DoSneaker(player, SNEAKERTYPE_SNEAKER);
								K_PlayBoostTaunt(player->mo);
								player->itemamount--;
								K_BotResetItemConfirm(player, false);
							}
							break;
						case KITEM_ROCKETSNEAKER:
							if (ATTACK_IS_DOWN && !HOLDING_ITEM && onground && NO_HYUDORO
								&& player->rocketsneakertimer == 0)
							{
								INT32 moloop;
								mobj_t *mo = NULL;
								mobj_t *prev = player->mo;

								K_PlayBoostTaunt(player->mo);
								S_StartSound(player->mo, sfx_s3k3a);

								//K_DoSneaker(player, SNEAKERTYPE_ROCKETSNEAKER);

								player->rocketsneakertimer = (itemtime*3);
								player->itemamount--;
								K_UpdateHnextList(player, true);

								for (moloop = 0; moloop < 2; moloop++)
								{
									mo = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_ROCKETSNEAKER);
									K_MatchGenericExtraFlags(mo, player->mo);
									mo->flags |= MF_NOCLIPTHING;
									mo->angle = player->mo->angle;
									mo->threshold = 10;
									mo->movecount = moloop%2;
									mo->movedir = mo->lastlook = moloop+1;
									P_SetTarget(&mo->target, player->mo);
									P_SetTarget(&mo->hprev, prev);
									P_SetTarget(&prev->hnext, mo);
									prev = mo;
								}
								K_BotResetItemConfirm(player, false);
							}
							break;
						case KITEM_INVINCIBILITY:
							if (ATTACK_IS_DOWN && !HOLDING_ITEM && NO_HYUDORO) // Doesn't hold your item slot hostage in Legacy, so you're free to waste it if you have multiple
							{
								K_DoInvincibility(player, K_GetInvincibilityTime(player));
								K_PlayPowerGloatSound(player->mo);
								player->itemamount--;
								K_BotResetItemConfirm(player, false);
							}
							break;
						case KITEM_BANANA:
							if (ATTACK_IS_DOWN && !HOLDING_ITEM && NO_HYUDORO)
							{
								INT32 moloop;
								mobj_t *mo;
								mobj_t *prev = player->mo;

								//K_PlayAttackTaunt(player->mo);
								K_SetItemOut(player);
								S_StartSound(player->mo, sfx_s254);

								for (moloop = 0; moloop < player->itemamount; moloop++)
								{
									mo = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_BANANA_SHIELD);
									if (!mo)
									{
										player->itemamount = moloop;
										break;
									}
									mo->flags |= MF_NOCLIPTHING;
									mo->threshold = 10;
									mo->movecount = player->itemamount;
									mo->movedir = moloop+1;
									P_SetTarget(&mo->target, player->mo);
									P_SetTarget(&mo->hprev, prev);
									P_SetTarget(&prev->hnext, mo);
									prev = mo;
								}
								K_BotResetItemConfirm(player, false);

							}
							else if (ATTACK_IS_DOWN && (player->itemflags & IF_ITEMOUT)) // Banana x3 thrown
							{
								player->itemamount--;
								K_ThrowKartItem(player, false, MT_BANANA, -1, 0);
								K_PlayAttackTaunt(player->mo);
								K_UpdateHnextList(player, false);
								K_BotResetItemConfirm(player, false);
							}
							break;
						case KITEM_EGGMAN:
							if (ATTACK_IS_DOWN && !HOLDING_ITEM && NO_HYUDORO)
							{
								mobj_t *mo;
								player->itemamount--;
								player->itemflags |= IF_EGGMANOUT;
								S_StartSound(player->mo, sfx_s254);
								mo = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_EGGMANITEM_SHIELD);
								if (mo)
								{
									K_FlipFromObject(mo, player->mo);
									mo->flags |= MF_NOCLIPTHING;
									mo->threshold = 10;
									mo->movecount = 1;
									mo->movedir = 1;
									P_SetTarget(&mo->target, player->mo);
									P_SetTarget(&player->mo->hnext, mo);
								}
								K_BotResetItemConfirm(player, false);
							}
							break;
						case KITEM_ORBINAUT:
							if (ATTACK_IS_DOWN && !HOLDING_ITEM && NO_HYUDORO)
							{
								angle_t newangle;
								INT32 moloop;
								mobj_t *mo = NULL;
								mobj_t *prev = player->mo;

								//K_PlayAttackTaunt(player->mo);
								K_SetItemOut(player);
								S_StartSound(player->mo, sfx_s3k3a);

								for (moloop = 0; moloop < player->itemamount; moloop++)
								{
									newangle = (player->mo->angle + ANGLE_157h) + FixedAngle(((360 / player->itemamount) * moloop) << FRACBITS) + ANGLE_90;
									mo = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_ORBINAUT_SHIELD);
									if (!mo)
									{
										player->itemamount = moloop;
										break;
									}
									mo->flags |= MF_NOCLIPTHING;
									mo->angle = newangle;
									mo->threshold = 10;
									mo->movecount = player->itemamount;
									mo->movedir = mo->lastlook = moloop+1;
									mo->color = player->skincolor;
									P_SetTarget(&mo->target, player->mo);
									P_SetTarget(&mo->hprev, prev);
									P_SetTarget(&prev->hnext, mo);
									prev = mo;
								}
								K_BotResetItemConfirm(player, false);
							}
							else if (ATTACK_IS_DOWN && (player->itemflags & IF_ITEMOUT)) // Orbinaut x3 thrown
							{
								player->itemamount--;
								K_ThrowKartItem(player, true, MT_ORBINAUT, 1, 0);
								K_PlayAttackTaunt(player->mo);
								K_UpdateHnextList(player, false);
								K_BotResetItemConfirm(player, false);
							}
							break;
						case KITEM_JAWZ:
							if (ATTACK_IS_DOWN && !HOLDING_ITEM && NO_HYUDORO)
							{
								angle_t newangle;
								INT32 moloop;
								mobj_t *mo = NULL;
								mobj_t *prev = player->mo;

								//K_PlayAttackTaunt(player->mo);
								K_SetItemOut(player);
								S_StartSound(player->mo, sfx_s3k3a);

								for (moloop = 0; moloop < player->itemamount; moloop++)
								{
									newangle = (player->mo->angle + ANGLE_157h) + FixedAngle(((360 / player->itemamount) * moloop) << FRACBITS) + ANGLE_90;
									mo = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_JAWZ_SHIELD);
									if (!mo)
									{
										player->itemamount = moloop;
										break;
									}
									mo->flags |= MF_NOCLIPTHING;
									mo->angle = newangle;
									mo->threshold = 10;
									mo->movecount = player->itemamount;
									mo->movedir = mo->lastlook = moloop+1;
									P_SetTarget(&mo->target, player->mo);
									P_SetTarget(&mo->hprev, prev);
									P_SetTarget(&prev->hnext, mo);
									prev = mo;
								}
								K_BotResetItemConfirm(player, false);
							}
							else if (ATTACK_IS_DOWN && HOLDING_ITEM && (player->itemflags & IF_ITEMOUT)) // Jawz thrown
							{
								player->itemamount--;
								if (player->throwdir == 1 || player->throwdir == 0)
									K_ThrowKartItem(player, true, MT_JAWZ, 1, 0);
								else if (player->throwdir == -1) // Throwing backward gives you a dud that doesn't home in
									K_ThrowKartItem(player, true, MT_JAWZ_DUD, -1, 0);
								K_PlayAttackTaunt(player->mo);
								K_UpdateHnextList(player, false);
								K_BotResetItemConfirm(player, false);
							}
							break;
						case KITEM_MINE:
							if (ATTACK_IS_DOWN && !HOLDING_ITEM && NO_HYUDORO)
							{
								mobj_t *mo;
								K_SetItemOut(player);
								S_StartSound(player->mo, sfx_s254);
								mo = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_SSMINE_SHIELD);
								if (mo)
								{
									mo->flags |= MF_NOCLIPTHING;
									mo->threshold = 10;
									mo->movecount = 1;
									mo->movedir = 1;
									P_SetTarget(&mo->target, player->mo);
									P_SetTarget(&player->mo->hnext, mo);
								}
								K_BotResetItemConfirm(player, false);
							}
							else if (ATTACK_IS_DOWN && (player->itemflags & IF_ITEMOUT))
							{
								player->itemamount--;
								K_ThrowKartItem(player, false, MT_SSMINE, 1, 1);
								K_PlayAttackTaunt(player->mo);
								player->itemflags &= ~IF_ITEMOUT;
								K_UpdateHnextList(player, true);
								K_BotResetItemConfirm(player, false);
							}
							break;
						case KITEM_LANDMINE:
							if (ATTACK_IS_DOWN && !HOLDING_ITEM && NO_HYUDORO)
							{
								player->itemamount--;
								K_ThrowLandMine(player);
								K_PlayAttackTaunt(player->mo);
								K_BotResetItemConfirm(player, false);
							}
							break;
						case KITEM_BALLHOG:
							if (ATTACK_IS_DOWN && !HOLDING_ITEM && NO_HYUDORO)
							{
								player->itemamount--;
								K_ThrowKartItem(player, true, MT_BALLHOG, 1, 0);
								K_PlayAttackTaunt(player->mo);
								K_BotResetItemConfirm(player, false);
							}
							break;
						case KITEM_SPB:
							if (ATTACK_IS_DOWN && !HOLDING_ITEM && NO_HYUDORO)
							{
								player->itemamount--;
								K_ThrowKartItem(player, true, MT_SPB, 1, 0);
								K_PlayAttackTaunt(player->mo);
								K_BotResetItemConfirm(player, false);
							}
							break;
						case KITEM_GROW:
							if (ATTACK_IS_DOWN && !HOLDING_ITEM && NO_HYUDORO)
							{
								player->itemamount--;
								if (player->growshrinktimer < 0)
								{
									// If you're shrunk, then "grow" will just make you normal again.
									K_RemoveGrowShrink(player);
								}
								else
								{
									K_PlayPowerGloatSound(player->mo);

									player->mo->scalespeed = mapobjectscale/TICRATE;
									player->mo->destscale = FixedMul(mapobjectscale, GROW_SCALE);

									if (K_PlayerShrinkCheat(player) == true)
									{
										player->mo->destscale = FixedMul(player->mo->destscale, SHRINK_SCALE);
									}

									// TODO: gametyperules
									player->growshrinktimer = (gametype == GT_BATTLE ? 8 : 12) * TICRATE;

									if (player->invincibilitytimer > 0)
									{
										; // invincibility has priority in P_RestoreMusic, no point in starting here
									}
									else if (P_IsLocalPlayer(player) == true)
									{
										S_ChangeMusicSpecial("kgrow");
									}
									else //used to be "if (P_IsDisplayPlayer(player) == false)"
									{
										S_StartSound(player->mo, (cv_kartinvinsfx.value ? sfx_alarmg : sfx_kgrow));
									}

									P_RestoreMusic(player);
									S_StartSound(player->mo, sfx_kc5a);
								}

								K_BotResetItemConfirm(player, false);
							}
							break;
						case KITEM_SHRINK:
							if (ATTACK_IS_DOWN && !HOLDING_ITEM && NO_HYUDORO)
							{
								K_DoShrink(player);
								player->itemamount--;
								K_PlayPowerGloatSound(player->mo);
								K_BotResetItemConfirm(player, false);
							}
							break;
						case KITEM_THUNDERSHIELD:
							if (player->curshield != KSHIELD_THUNDER)
							{
								mobj_t *shield = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_THUNDERSHIELD);
								P_SetScale(shield, (shield->destscale = (5*shield->destscale)>>2));
								P_SetTarget(&shield->target, player->mo);
								S_StartSound(player->mo, sfx_s3k41);
								player->curshield = KSHIELD_THUNDER;
							}

							if (ATTACK_IS_DOWN && !HOLDING_ITEM && NO_HYUDORO)
							{
								K_DoThunderShield(player);
								if (player->itemamount > 0)
								{
									// Why is this a conditional?
									// Thunder shield: the only item that allows you to
									// activate a mine while you're out of its radius,
									// the SAME tic it sets your itemamount to 0
									// ...:dumbestass:
									player->itemamount--;
									K_PlayAttackTaunt(player->mo);
									K_BotResetItemConfirm(player, false);
								}
							}
							break;
						case KITEM_BUBBLESHIELD:
							if (player->curshield != KSHIELD_BUBBLE)
							{
								mobj_t *shield = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_BUBBLESHIELD);
								P_SetScale(shield, (shield->destscale = (5*shield->destscale)>>2));
								P_SetTarget(&shield->target, player->mo);
								S_StartSound(player->mo, sfx_s3k3f);
								player->curshield = KSHIELD_BUBBLE;

								player->bubblehealth = MAXBUBBLEHEALTH;
							}

							if (!HOLDING_ITEM && NO_HYUDORO)
							{
								if ((buttons & BT_ATTACK) && (player->itemflags & IF_HOLDREADY))
								{
									if (player->bubbleblowup == 0)
										S_StartSound(player->mo, sfx_s3k75);

									player->bubbleblowup++;

									// Reduce cooldown for easier itemset chaining.
									// (was blowup x4 before)
									player->bubblecool = player->bubbleblowup*2;

									if (player->bubbleblowup > bubbletime*2)
									{
										// Jump straight to deflating the bubble.
										goto bubbledeflate;
									}
								}
								else
								{
bubbledeflate:
									{
										boolean popped = false;
										if (player->bubbleblowup > bubbletime)
											player->bubbleblowup = bubbletime;

										if (player->bubbleblowup)
										{
											player->bubbleblowup--;

											if (!player->bubbleblowup)
											{
												// Each use costs some points of health.
												K_RemoveBubbleHealth(player, BUBBLEUSECOST);

												if (player->bubblehealth <= 0)
												{
													// Bubble popped!
													popped = true;
												}
											}
										}

										if (!popped)
										{
											if (buttons & BT_ATTACK || player->bubblecool)
											{
												player->itemflags &= ~IF_HOLDREADY;
											}
											else
											{
												player->itemflags |= IF_HOLDREADY;
											}
										}
									}
								}
							}
							break;
						case KITEM_FLAMESHIELD:

							if (ATTACK_IS_DOWN && !HOLDING_ITEM && NO_HYUDORO && player->flametimer == 0)
							{
								player->itemamount--;
								player->flametimer = (itemtime*3);
								mobj_t *shield = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_FLAMESHIELD);
								P_SetScale(shield, (shield->destscale = (5*shield->destscale)>>2));
								P_SetTarget(&shield->target, player->mo);
								S_StartSound(player->mo, sfx_s3k3e);
								player->curshield = KSHIELD_FLAME;

							}
							break;
						case KITEM_HYUDORO:
							if (ATTACK_IS_DOWN && !HOLDING_ITEM && NO_HYUDORO)
							{
								player->itemamount--;
								K_DoHyudoroSteal(player); // yes. yes they do.
								K_PlayAttackTaunt(player->mo);
								K_BotResetItemConfirm(player, false);
							}
							break;
						case KITEM_POGOSPRING:
							if (ATTACK_IS_DOWN && !HOLDING_ITEM && onground && NO_HYUDORO && player->pogospring == 0)
							{
								player->itemamount--;
								K_PlayBoostTaunt(player->mo);
								K_DoPogoSpring(player->mo, 32<<FRACBITS, 2);
								player->pogospring = 1;
								K_BotResetItemConfirm(player, false);
							}
							break;
						case KITEM_SUPERRING:
							if (ATTACK_IS_DOWN && !HOLDING_ITEM && NO_HYUDORO)
							{
								player->itemamount--;
								K_AwardScaledPlayerRings(player, ASR_SUPERRING);
								K_BotResetItemConfirm(player, false);
							}
							break;
						case KITEM_KITCHENSINK:
							if (ATTACK_IS_DOWN && !HOLDING_ITEM && NO_HYUDORO)
							{
								mobj_t *mo;
								K_SetItemOut(player);
								S_StartSound(player->mo, sfx_s254);
								mo = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_SINK_SHIELD);
								if (mo)
								{
									mo->flags |= MF_NOCLIPTHING;
									mo->threshold = 10;
									mo->movecount = 1;
									mo->movedir = 1;
									P_SetTarget(&mo->target, player->mo);
									P_SetTarget(&player->mo->hnext, mo);
								}
								K_BotResetItemConfirm(player, false);
							}
							else if (ATTACK_IS_DOWN && HOLDING_ITEM && (player->itemflags & IF_ITEMOUT)) // Sink thrown
							{
								player->itemamount--;
								K_ThrowKartItem(player, false, MT_SINK, 1, 2);
								K_PlayAttackTaunt(player->mo);
								player->itemflags &= ~IF_ITEMOUT;
								K_UpdateHnextList(player, true);
								K_BotResetItemConfirm(player, false);
							}
							break;
						case KITEM_SAD:
							if (ATTACK_IS_DOWN && !HOLDING_ITEM && NO_HYUDORO
								&& !player->sadtimer)
							{
								player->sadtimer = stealtime;
								player->itemamount--;
								K_BotResetItemConfirm(player, false);
							}
							break;
						default:
							break;
					}
				}
			}
		}
	}

		// No more!
		if (!player->itemamount)
		{
			player->itemflags &= ~IF_ITEMOUT;
			player->itemtype = KITEM_NONE;
		}

		if (K_GetShieldFromPlayer(player) == KSHIELD_NONE)
		{
			player->curshield = KSHIELD_NONE; // RESET shield type
			player->bubbleblowup = 0;
			player->bubblecool = 0;
			//player->flamedash = 0;
			player->flametimer = 0;
		}

		if (player->growshrinktimer <= 0)
			player->growcancel = -1;

		if (player->itemtype == KITEM_SPB
			|| player->itemtype == KITEM_SHRINK
			|| player->growshrinktimer < 0)
			indirectitemcooldown = 20*TICRATE;

		if (player->hyudorotimer > 0)
		{
			INT32 hyu = hyudorotime;

			if (leveltime & 1)
			{
				player->mo->renderflags |= RF_DONTDRAW;
			}
			else
			{
				if (player->hyudorotimer >= (TICRATE/2) && player->hyudorotimer <= hyu-(TICRATE/2))
					player->mo->renderflags &= ~K_GetPlayerDontDrawFlag(player);
				else
					player->mo->renderflags &= ~RF_DONTDRAW;
			}

			player->flashing = player->hyudorotimer; // We'll do this for now, let's people know about the invisible people through subtle hints
		}
		else if (player->hyudorotimer == 0)
		{
			player->mo->renderflags &= ~RF_DONTDRAW;
		}

		if (gametype == GT_BATTLE && player->bumper <= 0) // dead in match? you da bomb
		{
			K_DropItems(player); //K_StripItems(player);
			K_StripOther(player);
			player->mo->renderflags |= RF_GHOSTLY;
			player->flashing = player->karmadelay;
		}
		else if (gametype == GT_RACE || player->bumper > 0)
		{
			player->mo->renderflags &= ~(RF_TRANSMASK|RF_BRIGHTMASK);
		}

	K_AdjustPlayerFriction(player);

	K_KartDrift(player, onground);

	K_KartSlipdash(player, onground);

	// Quick Turning
	// You can't turn your kart when you're not moving.
	// So now it's time to burn some rubber!
	if (player->speed < 2 && leveltime > starttime && player->cmd.buttons & BT_ACCELERATE && player->cmd.buttons & BT_BRAKE && player->cmd.turning != 0)
	{
		if (leveltime % 8 == 0)
			S_StartSound(player->mo, sfx_s224);
	}

	if (!(gametyperules & GTR_NOCOUNTDOWN))
		K_RaceStart(player);

	if (onground == false)
	{
		K_AirFailsafe(player);
	}
	else
	{
		player->pflags &= ~PF_AIRFAILSAFE;
	}
}

void K_CheckSpectateStatus(boolean considermapreset)
{
	UINT8 respawnlist[MAXPLAYERS];
	UINT8 i, j, numingame = 0, numjoiners = 0;
	UINT8 numhumans = 0, numbots = 0;

	// Maintain spectate wait timer
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i])
		{
			continue;
		}

		if (!players[i].spectator)
		{
			numingame++;

			if (players[i].bot)
			{
				numbots++;
			}
			else
			{
				numhumans++;
			}

			players[i].spectatewait = 0;
			players[i].spectatorreentry = 0;
			continue;
		}

		if ((players[i].pflags & PF_WANTSTOJOIN))
		{
			players[i].spectatewait++;
		}
		else
		{
			players[i].spectatewait = 0;
		}

		if (gamestate != GS_LEVEL || considermapreset == false)
		{
			players[i].spectatorreentry = 0;
		}
		else if (players[i].spectatorreentry > 0)
		{
			players[i].spectatorreentry--;
		}
	}

	// No one's allowed to join
	if (!cv_allowteamchange.value)
		return;

	// DON'T allow if you've hit the in-game player cap
	if (cv_ingamecap.value && numhumans >= cv_ingamecap.value)
		return;

	// Get the number of players in game, and the players to be de-spectated.
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i])
			continue;

		if (!players[i].spectator)
		{
			// Allow if you're not in a level
			if (gamestate != GS_LEVEL)
				continue;

			// DON'T allow if anyone's exiting
			if (players[i].exiting)
				return;

			// Allow if the match hasn't started yet
			if (numingame < 2 || leveltime < starttime || mapreset)
				continue;

			// DON'T allow if the match is 20 seconds in
			if (leveltime > (starttime + 20*TICRATE))
				return;

			// DON'T allow if the race is at 2 laps
			if ((gametyperules & GTR_CIRCUIT) && players[i].laps >= 2)
				return;

			continue;
		}

		if (players[i].bot)
		{
			// Spectating bots are controlled by other mechanisms.
			continue;
		}

		if (!(players[i].pflags & PF_WANTSTOJOIN))
		{
			// This spectator does not want to join.
			continue;
		}

		if (netgame && numingame > 0 && players[i].spectatorreentry > 0)
		{
			// This person has their reentry cooldown active.
			continue;
		}

		respawnlist[numjoiners++] = i;
	}

	// Literally zero point in going any further if nobody is joining.
	if (!numjoiners)
		return;

	// Organize by spectate wait timer (if there's more than one to sort)
	if (cv_ingamecap.value && numjoiners > 1)
	{
		UINT8 oldrespawnlist[MAXPLAYERS];
		memcpy(oldrespawnlist, respawnlist, numjoiners);
		for (i = 0; i < numjoiners; i++)
		{
			UINT8 pos = 0;
			INT32 ispecwait = players[oldrespawnlist[i]].spectatewait;

			for (j = 0; j < numjoiners; j++)
			{
				INT32 jspecwait = players[oldrespawnlist[j]].spectatewait;
				if (j == i)
					continue;
				if (jspecwait > ispecwait)
					pos++;
				else if (jspecwait == ispecwait && j < i)
					pos++;
			}

			respawnlist[pos] = oldrespawnlist[i];
		}
	}

	const UINT8 previngame = numingame;
	INT16 removeBotID = MAXPLAYERS - 1;

	// Finally, we can de-spectate everyone!
	for (i = 0; i < numjoiners; i++)
	{
		// Hit the in-game player cap while adding people?
		if (cv_ingamecap.value && numingame >= cv_ingamecap.value)
		{
			if (numbots > 0)
			{
				// Find a bot to kill to make room
				while (removeBotID >= 0)
				{
					if (playeringame[removeBotID] && players[removeBotID].bot)
					{
						//CONS_Printf("bot %s kicked to make room on tic %d\n", player_names[removeBotID], leveltime);
						CL_RemovePlayer(removeBotID, KR_LEAVE);
						numbots--;
						numingame--;
						break;
					}

					removeBotID--;
				}

				if (removeBotID < 0)
				{
					break;
				}
			}
			else
			{
				break;
			}
		}

		//CONS_Printf("player %s is joining on tic %d\n", player_names[respawnlist[i]], leveltime);
		P_SpectatorJoinGame(&players[respawnlist[i]]);
		numhumans++;
		numingame++;
	}

	if (considermapreset == false)
		return;

	// Reset the match when 2P joins 1P, DUEL mode
	// Reset the match when 3P joins 1P and 2P, DUEL mode must be disabled
	if (!mapreset && gamestate == GS_LEVEL && (previngame < 2 && numingame >= 2))
	{
		S_ChangeMusicInternal("chalng", false); // COME ON
		mapreset = 3*TICRATE; // Even though only the server uses this for game logic, set for everyone for HUD
	}
}

UINT8 K_GetInvincibilityItemFrame(void)
{
	return ((leveltime % (7*3)) / 3);
}

UINT8 K_GetMultItemFrame(UINT8 count, UINT8 max)
{
	return min(count - 1, max);
}

boolean K_IsSPBInGame(void)
{
	UINT8 i;
	thinker_t *think;

	// is there an SPB chasing anyone?
	if (spbplace != -1)
		return true;

	// do any players have an SPB in their item slot?
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator)
			continue;

		if (players[i].itemtype == KITEM_SPB)
			return true;
	}

	// spbplace is still -1 until a fired SPB finds a target, so look for an in-map SPB just in case
	for (think = thlist[THINK_MOBJ].next; think != &thlist[THINK_MOBJ]; think = think->next)
	{
		if (think->function.acp1 == (actionf_p1)P_RemoveThinkerDelayed)
			continue;

		if (((mobj_t *)think)->type == MT_SPB)
			return true;
	}

	return false;
}

boolean K_RingsActive(void)
{
	if (!(gametyperules & GTR_RINGS))
	{
		// No Rings in this gametype.
		return false;
	}

	if (!ringsactive)
	{
		// Rings aren't enabled.
		return false;
	}

	return true;
}

boolean K_PurpleDriftActive(void)
{
	if (purpledriftactive)
	{
		// Purpledrift is enabled!
		return true;
	}

	return false;
}

boolean K_StackingActive(void)
{
	if (stackingactive)
	{
		// Booststacking is enabled!
		return true;
	}

	return false;
}

boolean K_ChainingActive(void)
{
	if (chainingactive)
	{
		// Boostchaining is enabled!
		return true;
	}

	return false;
}

boolean K_SlipdashActive(void)
{
	if (slipdashactive)
	{
		// Slipdash is enabled!
		return true;
	}

	return false;
}

boolean K_SlopeBoostActive(void)
{
	if (slopeboostactive)
	{
		// SlopeBoost is enabled!
		return true;
	}

	return false;
}

boolean K_DraftingActive(void)
{
	if (draftingactive)
	{
		// Drafting is enabled!
		return true;
	}

	return false;
}

boolean K_GetKartInvinType(void)
{
	return invintype;
}

boolean K_UsingLegacyCheckpoints(void)
{
	if (K_UsingPatchedMap() && waypointcap)
	{
		// we're presumably adding waypoints to an existing map
		return false;
	}
	if (numbosswaypoints > 0)
	{
		// We are using Kart V1 waypoints!
		return true;
	}

	return false;
}

boolean K_UsingPatchedMap(void)
{
	if (patch_version)
	{
		// This map has been patched!
		return true;
	}

	return false;
}

INT32 K_CheckpointThreshold(boolean roundup)
{
	if (K_UsingLegacyCheckpoints() || K_UsingPatchedMap())
		return roundup ? numstarposts - numstarposts/2 : numstarposts/2;
	else
		return numstarposts;
}

void K_UpdateMobjItemOverlay(mobj_t *part, SINT8 itemType, UINT8 itemCount)
{
	switch (itemType)
	{
		case KITEM_SNEAKER:
			part->sprite = SPR_ITSN;
			part->frame = FF_FULLBRIGHT|FF_PAPERSPRITE|K_GetMultItemFrame(itemCount, 2);
			break;
		case KITEM_ORBINAUT:
			part->sprite = SPR_ITMO;
			part->frame = FF_FULLBRIGHT|FF_PAPERSPRITE|K_GetMultItemFrame(itemCount, 3);
			break;
		case KITEM_BANANA:
			part->sprite = SPR_ITBA;
			part->frame = FF_FULLBRIGHT|FF_PAPERSPRITE|K_GetMultItemFrame(itemCount, 3);
			break;
		case KITEM_JAWZ:
			part->sprite = SPR_ITJA;
			part->frame = FF_FULLBRIGHT|FF_PAPERSPRITE|K_GetMultItemFrame(itemCount, 1);
			break;
		case KITEM_INVINCIBILITY:
			part->sprite = SPR_ITMI;
			part->frame = FF_FULLBRIGHT|FF_PAPERSPRITE|K_GetInvincibilityItemFrame();
			break;
		case KITEM_SAD:
			part->sprite = SPR_ITEM;
			part->frame = FF_FULLBRIGHT|FF_PAPERSPRITE;
			break;
		default:
			part->sprite = SPR_ITEM;
			part->frame = FF_FULLBRIGHT|FF_PAPERSPRITE|(itemType);
			break;
	}
}

boolean K_NotFreePlay(void)
{
	UINT8 i;
	UINT8 nump = 0;

	if (K_CanChangeRules() == false)
	{
		// Rounds with direction are never FREE PLAY.
		return true;
	}

	if (itembreaker)
	{
		// Prison Break is battle's FREE PLAY.
		return false;
	}

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (playeringame[i] == false || players[i].spectator == true)
		{
			continue;
		}

		nump++;

		if (nump > 1)
		{
			return true;
		}
	}

	return false;
}

void K_QuiteSaltyHop(player_t *player)
{
	if (player->mo == NULL || P_MobjWasRemoved(player->mo))
		return;

	if (cv_saltyhop.value == 0)
	{
		// reset before we leave
		player->salty.jump = player->salty.ready = player->salty.tapping = false;
		player->salty.momz = player->salty.zoffset = 0;
		return;
	}

	const boolean onground = P_IsObjectOnGround(player->mo);

	// k_jmp is gone so just check for drift here
	// TODO: Rework this shit. this is awful
	if (!(player->pflags & PF_DRIFTINPUT))
	{
		player->salty.ready = true;
		player->salty.tapping = false;
	}
	else if (player->salty.ready)
	{
		player->salty.ready = false;
		player->salty.tapping = true;
	}
	else
	{
		player->salty.tapping = false;
	}

	if (player->salty.jump)
	{
		if (player->mo->eflags & MFE_JUSTHITFLOOR)
			player->salty.zoffset = 0;
		else if (onground)
		{
			player->salty.zoffset += player->salty.momz;
			player->salty.momz -= (3*FRACUNIT)/2;
		}
		else
		{
			// Originally (49/50)*FRACUNIT. tf was i on
			player->salty.zoffset *= FRACUNIT;
			player->salty.momz = 0;
		}

		// zoffset is back to zero...
		if (player->salty.zoffset <= 0)
		{
			// play the sound
			if (!(player->mo->eflags & MFE_JUSTHITFLOOR) && onground)
				S_StartSound(player->mo, sfx_s268);
			player->salty.jump = false;
			player->salty.zoffset = 0;
			player->salty.momz = 0;
			// small squish upon landing
			//player->mo->spritexscale = FRACUNIT*14/10;
			//player->mo->spriteyscale = FRACUNIT*6/10;
		}
		else if (player->salty.zoffset > 0)
		{
			// stretch the funny
			//player->mo->spritexscale -= (FRACUNIT/30);
			//player->mo->spriteyscale += (FRACUNIT/30);
		}
		// Apply the z offset!
		player->mo->spriteyoffset = player->salty.zoffset;

		// Stop any and all drift sounds when hopping.
		if (S_SoundPlaying(player->mo, sfx_screec))
			S_StopSoundByID(player->mo, sfx_screec);
		if (S_SoundPlaying(player->mo, sfx_drift))
			S_StopSoundByID(player->mo, sfx_drift);
	}
	else if (player->salty.tapping && onground && !P_PlayerInPain(player))
	{
		player->salty.jump = true;
		player->salty.zoffset = 0;
		player->salty.momz = 6*FRACUNIT;
		S_StartSound(player->mo, sfx_s25a);
	}
}

//}
