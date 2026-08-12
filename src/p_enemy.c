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
/// \file  p_enemy.c
/// \brief Enemy thinking, AI
///        Action Pointer Functions that are associated with states/frames

#include "dehacked.h"
#include "doomdef.h"
#include "g_game.h"
#include "i_sound.h"
#include "p_local.h"
#include "p_setup.h"
#include "r_main.h"
#include "r_state.h"
#include "s_sound.h"
#include "m_random.h"
#include "m_misc.h"
#include "r_skins.h"
#include "i_video.h"
#include "z_zone.h"
#include "lua_hook.h"
#include "k_kart.h" // SRB2kart
#include "k_waypoint.h"
#include "k_battle.h"
#include "k_collide.h"
#include "k_items.h"
#include "k_objects.h"

dboolean LUA_CallAction(enum actionnum actionnum, mobj_t *actor);

int32_t var1;
int32_t var2;
int32_t modulothing;

//
// P_NewChaseDir related LUT.
//
static dirtype_t opposite[] =
{
	DI_WEST, DI_SOUTHWEST, DI_SOUTH, DI_SOUTHEAST,
	DI_EAST, DI_NORTHEAST, DI_NORTH, DI_NORTHWEST, DI_NODIR
};

static dirtype_t diags[] =
{
	DI_NORTHWEST, DI_NORTHEAST, DI_SOUTHWEST, DI_SOUTHEAST
};

//
// ENEMY THINKING
// Enemies are always spawned with targetplayer = -1, threshold = 0
// Most monsters are spawned unaware of all players, but some can be made preaware.
//

//
// P_CheckMeleeRange
//
dboolean P_CheckMeleeRange(mobj_t *actor)
{
	mobj_t *pl;
	fixed_t dist;

	if (!actor->target)
		return false;

	pl = actor->target;
	dist = P_AproxDistance(pl->x-actor->x, pl->y-actor->y);

	if (dist >= FixedMul(MELEERANGE - 20*FRACUNIT, actor->scale) + pl->radius)
		return false;

	// check height now, so that damn crawlas cant attack
	// you if you stand on a higher ledge.
	if ((pl->z > actor->z + actor->height) || (actor->z > pl->z + pl->height))
		return false;

	if (!P_CheckSight(actor, actor->target))
		return false;

	return true;
}

// P_CheckMeleeRange for Jettysyn Bomber.
dboolean P_JetbCheckMeleeRange(mobj_t *actor)
{
	mobj_t *pl;
	fixed_t dist;

	if (!actor->target)
		return false;

	pl = actor->target;
	dist = P_AproxDistance(pl->x-actor->x, pl->y-actor->y);

	if (dist >= (actor->radius + pl->radius)*2)
		return false;

	if (actor->eflags & MFE_VERTICALFLIP)
	{
		if (pl->z < actor->z + actor->height + FixedMul(40<<FRACBITS, actor->scale))
			return false;
	}
	else
	{
		if (pl->z + pl->height > actor->z - FixedMul(40<<FRACBITS, actor->scale))
			return false;
	}

	return true;
}

// P_CheckMeleeRange for CastleBot FaceStabber.
dboolean P_FaceStabCheckMeleeRange(mobj_t *actor)
{
	mobj_t *pl;
	fixed_t dist;

	if (!actor->target)
		return false;

	pl = actor->target;
	dist = P_AproxDistance(pl->x-actor->x, pl->y-actor->y);

	if (dist >= (actor->radius + pl->radius)*4)
		return false;

	if ((pl->z > actor->z + actor->height) || (actor->z > pl->z + pl->height))
		return false;

	if (!P_CheckSight(actor, actor->target))
		return false;

	return true;
}

// P_CheckMeleeRange for Skim.
dboolean P_SkimCheckMeleeRange(mobj_t *actor)
{
	mobj_t *pl;
	fixed_t dist;

	if (!actor->target)
		return false;

	pl = actor->target;
	dist = P_AproxDistance(pl->x-actor->x, pl->y-actor->y);

	if (dist >= FixedMul(MELEERANGE - 20*FRACUNIT, actor->scale) + pl->radius)
		return false;

	if (actor->eflags & MFE_VERTICALFLIP)
	{
		if (pl->z < actor->z + actor->height + FixedMul(24<<FRACBITS, actor->scale))
			return false;
	}
	else
	{
		if (pl->z + pl->height > actor->z - FixedMul(24<<FRACBITS, actor->scale))
			return false;
	}

	return true;
}

//
// P_CheckMissileRange
//
dboolean P_CheckMissileRange(mobj_t *actor)
{
	fixed_t dist;

	if (!actor->target)
		return false;

	if (actor->reactiontime)
		return false; // do not attack yet

	if (!P_CheckSight(actor, actor->target))
		return false;

	// OPTIMIZE: get this from a global checksight
	dist = P_AproxDistance(actor->x-actor->target->x, actor->y-actor->target->y) - FixedMul(64*FRACUNIT, actor->scale);

	if (!actor->info->meleestate)
		dist -= FixedMul(128*FRACUNIT, actor->scale); // no melee attack, so fire more

	dist >>= FRACBITS;

	if (actor->type == MT_EGGMOBILE)
		dist >>= 1;

	if (dist > 200)
		dist = 200;

	if (actor->type == MT_EGGMOBILE && dist > 160)
		dist = 160;

	if (P_RandomByte(PR_ACTION) < dist)
		return false;

	return true;
}

/** Checks for water in a sector.
  * Used by Skim movements.
  *
  * \param x X coordinate on the map.
  * \param y Y coordinate on the map.
  * \return True if there's water at this location, false if not.
  * \sa ::MT_SKIM
  */
static dboolean P_WaterInSector(mobj_t *mobj, fixed_t x, fixed_t y)
{
	sector_t *sector;

	sector = R_PointInSubsector(x, y)->sector;

	if (sector->ffloors)
	{
		ffloor_t *rover;

		for (rover = sector->ffloors; rover; rover = rover->next)
		{
			if (!(rover->fofflags & FOF_EXISTS) || !(rover->fofflags & FOF_SWIMMABLE))
				continue;

			if (*rover->topheight >= mobj->floorz && *rover->topheight <= mobj->z)
				return true; // we found water!!
		}
	}

	return false;
}

static const fixed_t xspeed[NUMDIRS] = {FRACUNIT, 46341>>(16-FRACBITS), 0, -(46341>>(16-FRACBITS)), -FRACUNIT, -(46341>>(16-FRACBITS)), 0, 46341>>(16-FRACBITS)};
static const fixed_t yspeed[NUMDIRS] = {0, 46341>>(16-FRACBITS), FRACUNIT, 46341>>(16-FRACBITS), 0, -(46341>>(16-FRACBITS)), -FRACUNIT, -(46341>>(16-FRACBITS))};

/** Moves an actor in its current direction.
  *
  * \param actor Actor object to move.
  * \return False if the move is blocked, otherwise true.
  */
dboolean P_Move(mobj_t *actor, fixed_t speed)
{
	fixed_t tryx, tryy;
	dirtype_t movedir = actor->movedir;

	if (movedir == DI_NODIR || !actor->health)
		return false;

	I_Assert(movedir < NUMDIRS);

	tryx = actor->x + FixedMul(speed*xspeed[movedir], actor->scale);
	tryy = actor->y + FixedMul(speed*yspeed[movedir], actor->scale);

	if (actor->type == MT_SKIM && !P_WaterInSector(actor, tryx, tryy)) // bail out if sector lacks water
		return false;

	if (!P_TryMove(actor, tryx, tryy, false, NULL))
	{
		if (actor->flags & MF_FLOAT && g_tm.floatok)
		{
			// must adjust height
			if (actor->z < g_tm.floorz)
				actor->z += FixedMul(FLOATSPEED, actor->scale);
			else
				actor->z -= FixedMul(FLOATSPEED, actor->scale);

			if (actor->type == MT_JETJAW && actor->z + actor->height > actor->watertop)
				actor->z = actor->watertop - actor->height;

			actor->flags2 |= MF2_INFLOAT;
			return true;
		}

		return false;
	}
	else
		actor->flags2 &= ~MF2_INFLOAT;

	return true;
}

/** Attempts to move an actor on in its current direction.
  * If the move succeeds, the actor's move count is reset
  * randomly to a value from 0 to 15.
  *
  * \param actor Actor to move.
  * \return True if the move succeeds, false if the move is blocked.
  */
static dboolean P_TryWalk(mobj_t *actor)
{
	if (!P_Move(actor, actor->info->speed))
		return false;
	actor->movecount = P_RandomByte(PR_ACTION) & 15;
	return true;
}

void P_NewChaseDir(mobj_t *actor)
{
	fixed_t deltax, deltay;
	dirtype_t d[3];
	dirtype_t tdir = DI_NODIR, olddir, turnaround;

	I_Assert(actor->target != NULL);
	I_Assert(!P_MobjWasRemoved(actor->target));

	olddir = actor->movedir;

	if (olddir >= NUMDIRS)
		olddir = DI_NODIR;

	if (olddir != DI_NODIR)
		turnaround = opposite[olddir];
	else
		turnaround = olddir;

	deltax = actor->target->x - actor->x;
	deltay = actor->target->y - actor->y;

	if (deltax > FixedMul(10*FRACUNIT, actor->scale))
		d[1] = DI_EAST;
	else if (deltax < -FixedMul(10*FRACUNIT, actor->scale))
		d[1] = DI_WEST;
	else
		d[1] = DI_NODIR;

	if (deltay < -FixedMul(10*FRACUNIT, actor->scale))
		d[2] = DI_SOUTH;
	else if (deltay > FixedMul(10*FRACUNIT, actor->scale))
		d[2] = DI_NORTH;
	else
		d[2] = DI_NODIR;

	// try direct route
	if (d[1] != DI_NODIR && d[2] != DI_NODIR)
	{
		dirtype_t newdir = diags[((deltay < 0)<<1) + (deltax > 0)];

		actor->movedir = newdir;
		if ((newdir != turnaround) && P_TryWalk(actor))
			return;
	}

	// try other directions
	if (P_RandomChance(PR_ACTION, 25*FRACUNIT/32) || abs(deltay) > abs(deltax))
	{
		tdir = d[1];
		d[1] = d[2];
		d[2] = tdir;
	}

	if (d[1] == turnaround)
		d[1] = DI_NODIR;
	if (d[2] == turnaround)
		d[2] = DI_NODIR;

	if (d[1] != DI_NODIR)
	{
		actor->movedir = d[1];

		if (P_TryWalk(actor))
			return; // either moved forward or attacked
	}

	if (d[2] != DI_NODIR)
	{
		actor->movedir = d[2];

		if (P_TryWalk(actor))
			return;
	}

	// there is no direct path to the player, so pick another direction.
	if (olddir != DI_NODIR)
	{
		actor->movedir =olddir;

		if (P_TryWalk(actor))
			return;
	}

	// randomly determine direction of search
	if (P_RandomChance(PR_ACTION, FRACUNIT/2))
	{
		for (tdir = DI_EAST; tdir <= DI_SOUTHEAST; tdir++)
		{
			if (tdir != turnaround)
			{
				actor->movedir = tdir;

				if (P_TryWalk(actor))
					return;
			}
		}
	}
	else
	{
		for (tdir = DI_SOUTHEAST; tdir >= DI_EAST; tdir--)
		{
			if (tdir != turnaround)
			{
				actor->movedir = tdir;

				if (P_TryWalk(actor))
					return;
			}
		}
	}

	if (turnaround != DI_NODIR)
	{
		actor->movedir = turnaround;

		if (P_TryWalk(actor))
			return;
	}

	actor->movedir = (angle_t)DI_NODIR; // cannot move
}

/** Looks for players to chase after, aim at, or whatever.
  *
  * \param actor     The object looking for flesh.
  * \param allaround Look all around? If false, only players in a 180-degree
  *                  range in front will be spotted.
  * \param dist      If > 0, checks distance
  * \return True if a player is found, otherwise false.
  * \sa P_SupermanLook4Players
  */
dboolean P_LookForPlayers(mobj_t *actor, dboolean allaround, dboolean tracer, fixed_t dist)
{
	int32_t c = 0, stop;
	int32_t i, pcount = 0;
	player_t *player;
	angle_t an;

	// BP: first time init, this allow minimum lastlook changes
	if (actor->lastlook < 0)
		actor->lastlook = P_RandomByte(PR_MOVINGTARGET);

	actor->lastlook %= MAXPLAYERS;

	stop = (actor->lastlook - 1) % MAXPLAYERS;

	// No players? Don't bother.
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator)
			continue;

		pcount++;
	}

	if (!pcount)
		return false;

	for (; ; actor->lastlook = (actor->lastlook + 1) % MAXPLAYERS)
	{
		// done looking
		if (actor->lastlook == stop)
			return false;

		if (!playeringame[actor->lastlook])
			continue;

		if (c++ == 2)
			return false;

		player = &players[actor->lastlook];

		if ((netgame || multiplayer) && player->spectator)
			continue;

		if (!player->mo || P_MobjWasRemoved(player->mo))
			continue;

		if (player->mo->health <= 0)
			continue; // dead

		if (dist > 0
			&& P_AproxDistance(P_AproxDistance(player->mo->x - actor->x, player->mo->y - actor->y), player->mo->z - actor->z) > dist)
			continue; // Too far away

		if (!allaround)
		{
			an = R_PointToAngle2(actor->x, actor->y, player->mo->x, player->mo->y) - actor->angle;
			if (an > ANGLE_90 && an < ANGLE_270)
			{
				dist = P_AproxDistance(player->mo->x - actor->x, player->mo->y - actor->y);
				// if real close, react anyway
				if (dist > FixedMul(MELEERANGE, actor->scale))
					continue; // behind back
			}
		}

		if (!P_CheckSight(actor, player->mo))
			continue; // out of sight

		if (tracer)
			P_SetTarget(&actor->tracer, player->mo);
		else
			P_SetTarget(&actor->target, player->mo);
		return true;
	}

	//return false;
}

void P_HomingAttack(mobj_t *source, mobj_t *enemy) // Home in on your target
{
	fixed_t dist;
	fixed_t ns = 0;

	if (!enemy)
		return;

	if (!(enemy->health))
		return;

	// change angle
	source->angle = R_PointToAngle2(source->x, source->y, enemy->x, enemy->y);
	if (source->player)
	{
		for (int8_t j = 0; j <= r_splitscreen; j++)
		{
			if (source->player == &players[displayplayers[j]])
			{
				localangle[j] = source->angle;
				break;
			}
		}
	}

	// change slope
	dist = P_AproxDistance(P_AproxDistance(enemy->x - source->x, enemy->y - source->y),
		enemy->z - source->z);

	if (dist < 1)
		dist = 1;

	if (source->type == MT_DETON && enemy->player) // For Deton Chase (Unused)
		ns = FixedDiv(FixedMul(K_GetKartSpeed(enemy->player, false, true), enemy->scale), FixedDiv(20*FRACUNIT,17*FRACUNIT));
	else if (source->type != MT_PLAYER)
	{
		if (source->threshold == 32000)
			ns = FixedMul(source->info->speed/2, source->scale);
		else
			ns = FixedMul(source->info->speed, source->scale);
	}
	else if (source->player)
		ns = FixedDiv(FixedMul(K_GetKartSpeed(source->player, false, true), source->scale), 3*FRACUNIT/2);

	source->momx = FixedMul(FixedDiv(enemy->x - source->x, dist), ns);
	source->momy = FixedMul(FixedDiv(enemy->y - source->y, dist), ns);
	source->momz = FixedMul(FixedDiv(enemy->z - source->z, dist), ns);
}

//
// ACTION ROUTINES
//

// Function: A_Look
//
// Description: Look for a player and set your target to them.
//
// var1:
//		lower 16 bits = look all around
//		upper 16 bits = distance limit
// var2 = If 1, only change to seestate. If 2, only play seesound. If 0, do both.
//
void A_Look(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_LOOK, actor))
		return;

	if (leveltime < starttime) // SRB2kart - no looking before race starts
		return;

	if (!P_LookForPlayers(actor, locvar1 & 65535, false , FixedMul((locvar1 >> 16)*FRACUNIT, actor->scale)))
		return;

	// go into chase state
	if (!locvar2)
	{
		P_SetMobjState(actor, actor->info->seestate);
		A_PlaySeeSound(actor);
	}
	else if (locvar2 == 1) // Only go into seestate
		P_SetMobjState(actor, actor->info->seestate);
	else if (locvar2 == 2) // Only play seesound
		A_PlaySeeSound(actor);
}

// Function: A_Chase
//
// Description: Chase after your target.
//
// var1:
//		1 = don't check meleestate
//		2 = don't check missilestate
//		3 = don't check meleestate and missilestate
// var2 = unused
//
void A_Chase(void *thing)
{
	mobj_t *actor = thing;
	int32_t delta;
	int32_t locvar1 = var1;

	if (LUA_CallAction(A_CHASE, actor))
		return;

	I_Assert(actor != NULL);
	I_Assert(!P_MobjWasRemoved(actor));

	if (actor->reactiontime)
		actor->reactiontime--;

	// modify target threshold
	if (actor->threshold)
	{
		if (!actor->target || actor->target->health <= 0)
			actor->threshold = 0;
		else
			actor->threshold--;
	}

	// turn towards movement direction if not there yet
	if (actor->movedir < NUMDIRS)
	{
		actor->angle &= (7<<29);
		delta = actor->angle - (actor->movedir << 29);

		if (delta > 0)
			actor->angle -= ANGLE_45;
		else if (delta < 0)
			actor->angle += ANGLE_45;
	}

	if (!actor->target || !(actor->target->flags & MF_SHOOTABLE))
	{
		// look for a new target
		if (P_LookForPlayers(actor, true, false, 0))
			return; // got a new target

		P_SetMobjStateNF(actor, actor->info->spawnstate);
		return;
	}

	// do not attack twice in a row
	if (actor->flags2 & MF2_JUSTATTACKED)
	{
		actor->flags2 &= ~MF2_JUSTATTACKED;
		P_NewChaseDir(actor);
		return;
	}

	// check for melee attack
	if (!(locvar1 & 1) && actor->info->meleestate && P_CheckMeleeRange(actor))
	{
		if (actor->info->attacksound)
			S_StartSound(actor, actor->info->attacksound);

		P_SetMobjState(actor, actor->info->meleestate);
		return;
	}

	// check for missile attack
	if (!(locvar1 & 2) && actor->info->missilestate)
	{
		if (actor->movecount || !P_CheckMissileRange(actor))
			goto nomissile;

		P_SetMobjState(actor, actor->info->missilestate);
		actor->flags2 |= MF2_JUSTATTACKED;
		return;
	}

nomissile:
	// possibly choose another target
	if (multiplayer && !actor->threshold && (actor->target->health <= 0 || !P_CheckSight(actor, actor->target))
		&& P_LookForPlayers(actor, true, false, 0))
		return; // got a new target

	// chase towards player
	if (--actor->movecount < 0 || !P_Move(actor, actor->info->speed))
		P_NewChaseDir(actor);
}

// Function: A_FaceStabChase
//
// Description: Unused variant of A_Chase for Castlebot Facestabber.
//
// var1 = unused
// var2 = unused
//
void A_FaceStabChase(void *thing)
{
	mobj_t *actor = thing;
	int32_t delta;

	if (LUA_CallAction(A_FACESTABCHASE, actor))
		return;

	if (actor->reactiontime)
		actor->reactiontime--;

	// modify target threshold
	if (actor->threshold)
	{
		if (!actor->target || actor->target->health <= 0)
			actor->threshold = 0;
		else
			actor->threshold--;
	}

	// turn towards movement direction if not there yet
	if (actor->movedir < NUMDIRS)
	{
		actor->angle &= (7<<29);
		delta = actor->angle - (actor->movedir << 29);

		if (delta > 0)
			actor->angle -= ANGLE_45;
		else if (delta < 0)
			actor->angle += ANGLE_45;
	}

	if (!actor->target || !(actor->target->flags & MF_SHOOTABLE))
	{
		// look for a new target
		if (P_LookForPlayers(actor, true, false, 0))
			return; // got a new target

		P_SetMobjStateNF(actor, actor->info->spawnstate);
		return;
	}

	// do not attack twice in a row
	if (actor->flags2 & MF2_JUSTATTACKED)
	{
		actor->flags2 &= ~MF2_JUSTATTACKED;
		P_NewChaseDir(actor);
		return;
	}

	// check for melee attack
	if (actor->info->meleestate && P_FaceStabCheckMeleeRange(actor))
	{
		if (actor->info->attacksound)
			S_StartSound(actor, actor->info->attacksound);

		P_SetMobjState(actor, actor->info->meleestate);
		return;
	}

	// check for missile attack
	if (actor->info->missilestate)
	{
		if (actor->movecount || !P_CheckMissileRange(actor))
			goto nomissile;

		P_SetMobjState(actor, actor->info->missilestate);
		actor->flags2 |= MF2_JUSTATTACKED;
		return;
	}

nomissile:
	// possibly choose another target
	if (multiplayer && !actor->threshold && (actor->target->health <= 0 || !P_CheckSight(actor, actor->target))
		&& P_LookForPlayers(actor, true, false, 0))
		return; // got a new target

	// chase towards player
	if (--actor->movecount < 0 || !P_Move(actor, actor->info->speed))
		P_NewChaseDir(actor);
}

static void P_SharpDust(mobj_t *actor, mobjtype_t type, angle_t ang)
{
	mobj_t *dust;

	if (!type || !P_IsObjectOnGround(actor))
		return;

	dust = P_SpawnMobjFromMobj(actor,
			-P_ReturnThrustX(actor, ang, 16<<FRACBITS),
			-P_ReturnThrustY(actor, ang, 16<<FRACBITS),
			0, type);
	P_SetObjectMomZ(dust, P_RandomRange(PR_DECORATION, 1, 4)<<FRACBITS, false);
}

// Function: A_StatueBurst
//
// Description: For suspicious statues only...
//
// var1 = object to create
// var2 = effective nextstate for created object
//
void A_StatueBurst(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	mobjtype_t chunktype = (mobjtype_t)actor->info->raisestate;
	mobj_t *new;

	if (LUA_CallAction(A_STATUEBURST, actor))
		return;

	if (!locvar1 || !(new = P_SpawnMobjFromMobj(actor, 0, 0, 0, locvar1)))
		return;

	new->angle = actor->angle;
	P_SetTarget(&new->target, actor->target);
	if (locvar2)
		P_SetMobjState(new, (statenum_t)locvar2);
	S_StartSound(new, new->info->attacksound);
	S_StopSound(actor);
	S_StartSound(actor, sfx_s3k96);

	{
		fixed_t a, b;
		fixed_t c = (actor->height>>2) - FixedMul(actor->scale, mobjinfo[chunktype].height>>1);
		fixed_t v = 4<<FRACBITS;
		const fixed_t r = (actor->radius>>1);
		mobj_t *spawned;
		uint8_t i;
		for (i = 0; i < 8; i++)
		{
			a = ((i & 1) ? r : (-r));
			b = ((i & 2) ? r : (-r));
			if (i == 4)
			{
				c += (actor->height>>1);
				v = 8<<FRACBITS;
			}

			spawned = P_SpawnMobjFromMobj(actor, a, b, c, chunktype);

			P_InstaThrust(spawned, R_PointToAngle2(0, 0, a, b), 8<<FRACBITS);
			P_SetObjectMomZ(spawned, v, false);

			spawned->fuse = 3*TICRATE;
		}
	}
}

// Function: A_JetJawRoam
//
// Description: Roaming routine for JetJaw
//
// var1 = unused
// var2 = unused
//
void A_JetJawRoam(void *thing)
{
	mobj_t *actor = thing;
	if (LUA_CallAction(A_JETJAWROAM, actor))
		return;

	if (actor->reactiontime)
	{
		actor->reactiontime--;
		P_InstaThrust(actor, actor->angle, FixedMul(actor->info->speed*FRACUNIT/4, actor->scale));
	}
	else
	{
		actor->reactiontime = actor->info->reactiontime;
		actor->angle += ANGLE_180;
	}

	if (P_LookForPlayers(actor, false, false, actor->radius * 16))
		P_SetMobjState(actor, actor->info->seestate);
}

// Function: A_JetJawChomp
//
// Description: Chase and chomp at the target, as long as it is in view
//
// var1 = unused
// var2 = unused
//
void A_JetJawChomp(void *thing)
{
	mobj_t *actor = thing;
	int32_t delta;

	if (LUA_CallAction(A_JETJAWCHOMP, actor))
		return;

	// turn towards movement direction if not there yet
	if (actor->movedir < NUMDIRS)
	{
		actor->angle &= (7<<29);
		delta = actor->angle - (actor->movedir << 29);

		if (delta > 0)
			actor->angle -= ANGLE_45;
		else if (delta < 0)
			actor->angle += ANGLE_45;
	}

	// Stop chomping if target's dead or you can't see it
	if (!actor->target || !(actor->target->flags & MF_SHOOTABLE)
		|| actor->target->health <= 0 || !P_CheckSight(actor, actor->target))
	{
		P_SetMobjStateNF(actor, actor->info->spawnstate);
		return;
	}

	// chase towards player
	if (--actor->movecount < 0 || !P_Move(actor, actor->info->speed))
		P_NewChaseDir(actor);
}

// Function: A_PointyThink
//
// Description: Thinker function for Pointy
//
// var1 = unused
// var2 = unused
//
void A_PointyThink(void *thing)
{
	mobj_t *actor = thing;
	int32_t i;
	player_t *player = NULL;
	mobj_t *ball;
	TVector v;
	TVector *res;
	angle_t fa;
	fixed_t radius = FixedMul(actor->info->radius*actor->info->reactiontime, actor->scale);
	dboolean firsttime = true;
	int32_t sign;

	if (LUA_CallAction(A_POINTYTHINK, actor))
		return;

	actor->momx = actor->momy = actor->momz = 0;

	// Find nearest player
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator)
			continue;

		if (!players[i].mo)
			continue;

		if (!players[i].mo->health)
			continue;

		if (!P_CheckSight(actor, players[i].mo))
			continue;

		if (firsttime)
		{
			firsttime = false;
			player = &players[i];
		}
		else
		{
			if (P_AproxDistance(players[i].mo->x - actor->x, players[i].mo->y - actor->y) <
				P_AproxDistance(player->mo->x - actor->x, player->mo->y - actor->y))
				player = &players[i];
		}
	}

	if (!player)
		return;

	// Okay, we found the closest player. Let's move based on his movement.
	P_SetTarget(&actor->target, player->mo);
	A_FaceTarget(actor);

	if (P_AproxDistance(player->mo->x - actor->x, player->mo->y - actor->y) < P_AproxDistance(player->mo->x + player->mo->momx - actor->x, player->mo->y + player->mo->momy - actor->y))
		sign = -1; // Player is moving away
	else
		sign = 1; // Player is moving closer

	if (player->mo->momx || player->mo->momy)
	{
		P_InstaThrust(actor, R_PointToAngle2(actor->x, actor->y, player->mo->x, player->mo->y), FixedMul(actor->info->speed*sign, actor->scale));

		// Rotate our spike balls
		actor->lastlook += actor->info->damage;
		actor->lastlook %= FINEANGLES/4;
	}

	if (!actor->tracer) // For some reason we do not have spike balls...
		return;

	// Position spike balls relative to the value of 'lastlook'.
	ball = actor->tracer;

	i = 0;
	while (ball)
	{
		fa = actor->lastlook+i;
		v[0] = FixedMul(FINECOSINE(fa),radius);
		v[1] = 0;
		v[2] = FixedMul(FINESINE(fa),radius);
		v[3] = FRACUNIT;

		res = VectorMatrixMultiply(v, *RotateXMatrix(FixedAngle(actor->lastlook+i)));
		memcpy(&v, res, sizeof (v));
		res = VectorMatrixMultiply(v, *RotateZMatrix(actor->angle+ANGLE_180));
		memcpy(&v, res, sizeof (v));

		P_UnsetThingPosition(ball);
		ball->x = actor->x + v[0];
		ball->y = actor->y + v[1];
		ball->z = actor->z + (actor->height>>1) + v[2];
		P_SetThingPosition(ball);

		ball = ball->tracer;
		i += ANGLE_90 >> ANGLETOFINESHIFT;
	}
}

// Function: A_CheckBuddy
//
// Description: Checks if target/tracer exists/has health. If not, the object removes itself.
//
// var1:
//		0 = target
//		1 = tracer
// var2 = unused
//
void A_CheckBuddy(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;

	if (LUA_CallAction(A_CHECKBUDDY, actor))
		return;

	if (locvar1 && (!actor->tracer || actor->tracer->health <= 0))
		P_RemoveMobj(actor);
	else if (!locvar1 && (!actor->target || actor->target->health <= 0))
		P_RemoveMobj(actor);
}

// Function: A_HoodThink
//
// Description: Thinker for Robo-Hood
//
// var1 = unused
// var2 = unused
//
void A_HoodThink(void *thing)
{
	mobj_t *actor = thing;
	if (LUA_CallAction(A_HOODTHINK, actor))
		return;

	// Currently in the air...
	if (!(actor->eflags & MFE_VERTICALFLIP) && actor->z > actor->floorz)
	{
		if (actor->momz > 0)
			P_SetMobjStateNF(actor, actor->info->xdeathstate); // Rising
		else
			P_SetMobjStateNF(actor, actor->info->raisestate); // Falling

		return;
	}
	else if ((actor->eflags & MFE_VERTICALFLIP) && actor->z + actor->height < actor->ceilingz)
	{
		if (actor->momz < 0)
			P_SetMobjStateNF(actor, actor->info->xdeathstate); // Rising
		else
			P_SetMobjStateNF(actor, actor->info->raisestate); // Falling

		return;
	}

	if (actor->state == &states[actor->info->xdeathstate]
		|| actor->state == &states[actor->info->raisestate])
		P_SetMobjStateNF(actor, actor->info->seestate);

	if (!actor->target)
	{
		P_SetMobjState(actor, actor->info->spawnstate);
		return;
	}

	A_FaceTarget(actor); // Aiming... aiming...

	if (--actor->reactiontime > 0)
		return;

	// Shoot, if not too close (cheap shots are lame)
	if ((P_AproxDistance(actor->x-actor->target->x, actor->y-actor->target->y) > FixedMul(192*FRACUNIT, actor->scale))
	|| (actor->spawnpoint && (actor->spawnpoint->options & MTF_AMBUSH))) // If you can't jump, might as well shoot regardless of distance!
		P_SetMobjState(actor, actor->info->missilestate);
	else if (!(actor->spawnpoint && (actor->spawnpoint->options & MTF_AMBUSH)))// But we WILL jump!
		P_SetMobjState(actor, actor->info->painstate);

	actor->reactiontime = actor->info->reactiontime;
}

// Function: A_ArrowCheck
//
// Description: Checks arrow direction and adjusts sprite accordingly
//
// var1 = unused
// var2 = unused
//
void A_ArrowCheck(void *thing)
{
	mobj_t *actor = thing;
	fixed_t x,y,z;
	angle_t angle;
	fixed_t dist;

	if (LUA_CallAction(A_ARROWCHECK, actor))
		return;

	// Movement vector
	x = actor->momx;
	y = actor->momy;
	z = actor->momz;

	// Calculate the angle of movement.
	/*
	       Z
	     / |
	   /   |
	 /     |
	0------dist(X,Y)
	*/

	dist = P_AproxDistance(x, y);

	angle = R_PointToAngle2(0, 0, dist, z);

	if (angle > ANG20 && angle <= ANGLE_180)
		P_SetMobjStateNF(actor, actor->info->raisestate);
	else if (angle < ANG340 && angle > ANGLE_180)
		P_SetMobjStateNF(actor, actor->info->xdeathstate);
	else
		P_SetMobjStateNF(actor, actor->info->spawnstate);
}

// Function: A_SnailerThink
//
// Description: Thinker function for Snailer
//
// var1 = unused
// var2 = unused
//
void A_SnailerThink(void *thing)
{
	mobj_t *actor = thing;
	if (LUA_CallAction(A_SNAILERTHINK, actor))
		return;

	if (!actor->target || !(actor->target->flags & MF_SHOOTABLE))
	{
		// look for a new target
		if (!P_LookForPlayers(actor, true, false, 0))
			return;
	}

	// We now have a target. Oh bliss, rapture, and contentment!

	if (actor->target->z + actor->target->height > actor->z - FixedMul(32*FRACUNIT, actor->scale)
		&& actor->target->z < actor->z + actor->height + FixedMul(32*FRACUNIT, actor->scale)
		&& !(leveltime % (TICRATE*2)))
	{
		angle_t an;
		fixed_t z;

		// Actor shouldn't face target, so we'll do things a bit differently here

		an = R_PointToAngle2(actor->x, actor->y, actor->target->x, actor->target->y) - actor->angle;

		z = actor->z + actor->height/2;

		if (an > ANGLE_45 && an < ANGLE_315) // fire as close as you can to the target, even if too sharp an angle from your front
		{
			fixed_t dist;
			fixed_t dx, dy;

			dist = P_AproxDistance(actor->x - actor->target->x, actor->y - actor->target->y);

			if (an > ANGLE_45 && an <= ANGLE_90) // fire at 45 degrees to the left
			{
				dx = actor->x + P_ReturnThrustX(actor, actor->angle + ANGLE_45, dist);
				dy = actor->y + P_ReturnThrustY(actor, actor->angle + ANGLE_45, dist);
			}
			else if (an >= ANGLE_270 && an < ANGLE_315) // fire at 45 degrees to the right
			{
				dx = actor->x + P_ReturnThrustX(actor, actor->angle - ANGLE_45, dist);
				dy = actor->y + P_ReturnThrustY(actor, actor->angle - ANGLE_45, dist);
			}
			else // fire straight ahead
			{
				dx = actor->x + P_ReturnThrustX(actor, actor->angle, dist);
				dy = actor->y + P_ReturnThrustY(actor, actor->angle, dist);
			}

			P_SpawnPointMissile(actor, dx, dy, actor->target->z, MT_ROCKET, actor->x, actor->y, z);
		}
		else
			P_SpawnXYZMissile(actor, actor->target, MT_ROCKET, actor->x, actor->y, z);
	}

	if ((!(actor->eflags & MFE_VERTICALFLIP) && actor->target->z > actor->z)
	|| (actor->eflags & MFE_VERTICALFLIP && (actor->target->z + actor->target->height) > (actor->z + actor->height)))
		actor->momz += FixedMul(actor->info->speed, actor->scale);
	else if ((!(actor->eflags & MFE_VERTICALFLIP) && actor->target->z < actor->z)
	|| (actor->eflags & MFE_VERTICALFLIP && (actor->target->z + actor->target->height) < (actor->z + actor->height)))
		actor->momz -= FixedMul(actor->info->speed, actor->scale);

	actor->momz /= 2;
}

// Function: A_SharpChase
//
// Description: Thinker/Chase routine for Spincushions
//
// var1 = unused
// var2 = unused
//
void A_SharpChase(void *thing)
{
	mobj_t *actor = thing;
	if (LUA_CallAction(A_SHARPCHASE, actor))
		return;

	if (actor->reactiontime)
	{
		int32_t delta;

		actor->reactiontime--;

		// turn towards movement direction if not there yet
		if (actor->movedir < NUMDIRS)
		{
			actor->angle &= (7<<29);
			delta = actor->angle - (actor->movedir << 29);

			if (delta > 0)
				actor->angle -= ANGLE_45;
			else if (delta < 0)
				actor->angle += ANGLE_45;
		}

		if (!actor->target || !(actor->target->flags & MF_SHOOTABLE))
		{
			// look for a new target
			if (P_LookForPlayers(actor, true, false, 0))
				return; // got a new target

			P_SetMobjState(actor, actor->info->spawnstate);
			return;
		}

		// chase towards player
		if (--actor->movecount < 0 || !P_Move(actor, actor->info->speed))
			P_NewChaseDir(actor);
	}
	else
	{
		actor->threshold = actor->info->painchance;
		P_SetMobjState(actor, actor->info->missilestate);
		S_StartSound(actor, actor->info->attacksound);
	}
}

// Function: A_SharpSpin
//
// Description: Spin chase routine for Spincushions
//
// var1 = object # to spawn as dust (if not provided not done)
// var2 = if nonzero, do the old-style spinning using this as the angle difference
//
void A_SharpSpin(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	angle_t oldang = actor->angle;

	if (LUA_CallAction(A_SHARPSPIN, actor))
		return;

	if (actor->threshold && actor->target)
	{
		angle_t ang = R_PointToAngle2(actor->x, actor->y, actor->target->x, actor->target->y);
		P_Thrust(actor, ang, actor->info->speed*actor->scale);
		if (locvar2)
			actor->angle += locvar2; // ANGLE_22h;
		else
			actor->angle = ang;
		actor->threshold--;
		if (leveltime & 1)
			S_StartSound(actor, actor->info->painsound);
	}
	else
	{
		actor->reactiontime = actor->info->reactiontime;
		P_SetMobjState(actor, actor->info->meleestate);
	}

	P_SharpDust(actor, locvar1, oldang);
}

// Function: A_SharpDecel
//
// Description: Slow down the Spincushion
//
// var1 = unused
// var2 = unused
//
void A_SharpDecel(void *thing)
{
	mobj_t *actor = thing;
	if (LUA_CallAction(A_SHARPDECEL, actor))
		return;

	if (actor->momx > 2 || actor->momy > 2)
	{
		actor->momx >>= 1;
		actor->momy >>= 1;
	}
	else
		P_SetMobjState(actor, actor->info->xdeathstate);
}

// Function: A_CrushstaceanWalk
//
// Description: Crushstacean movement
//
// var1 = speed (actor info's speed if 0)
// var2 = state to switch to when blocked (spawnstate if 0)
//
void A_CrushstaceanWalk(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = (var1 ? var1 : (int32_t)actor->info->speed);
	int32_t locvar2 = (var2 ? var2 : (int32_t)actor->info->spawnstate);
	angle_t ang = actor->angle + ((actor->flags2 & MF2_AMBUSH) ? ANGLE_90 : ANGLE_270);

	if (LUA_CallAction(A_CRUSHSTACEANWALK, actor))
		return;

	actor->reactiontime--;

	if (!P_TryMove(actor,
		actor->x + P_ReturnThrustX(actor, ang, locvar1*actor->scale),
		actor->y + P_ReturnThrustY(actor, ang, locvar1*actor->scale),
		false, NULL)
	|| (actor->reactiontime-- <= 0))
	{
		actor->flags2 ^= MF2_AMBUSH;
		P_SetTarget(&actor->target, NULL);
		P_SetMobjState(actor, locvar2);
		actor->reactiontime = actor->info->reactiontime;
	}
}

// Function: A_CrushstaceanPunch
//
// Description: Crushstacean attack
//
// var1 = unused
// var2 = state to go to if unsuccessful (spawnstate if 0)
//
void A_CrushstaceanPunch(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar2 = (var2 ? var2 : (int32_t)actor->info->spawnstate);

	if (LUA_CallAction(A_CRUSHSTACEANPUNCH, actor))
		return;

	if (!actor->tracer)
		return;

	if (!actor->target)
	{
		P_SetMobjState(actor, locvar2);
		return;
	}

	actor->tracer->angle = R_PointToAngle2(actor->x, actor->y, actor->target->x, actor->target->y);
	P_SetMobjState(actor->tracer, actor->tracer->info->missilestate);
	actor->tracer->extravalue1 = actor->tracer->extravalue2 = 0;
	S_StartSound(actor, actor->info->attacksound);
}

// Function: A_CrushclawAim
//
// Description: Crushstacean claw aiming
//
// var1 = sideways offset
// var2 = vertical offset
//
void A_CrushclawAim(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	mobj_t *crab = actor->tracer;
	angle_t ang;

	if (LUA_CallAction(A_CRUSHCLAWAIM, actor))
		return;

	if (!crab)
	{
		P_RemoveMobj(actor);
		return; // there is only one step and it is crab
	}

	if (crab->target || P_LookForPlayers(crab, true, false, actor->info->speed*crab->scale))
		ang = R_PointToAngle2(crab->x, crab->y, crab->target->x, crab->target->y);
	else
		ang = crab->angle + ((crab->flags2 & MF2_AMBUSH) ? ANGLE_90 : ANGLE_270);
	ang -= actor->angle;

#define anglimit ANGLE_22h
#define angfactor 5
	if (ang < ANGLE_180)
	{
		if (ang > anglimit)
			ang = anglimit;
		ang /= angfactor;
	}
	else
	{
		ang = InvAngle(ang);
		if (ang > anglimit)
			ang = anglimit;
		ang = InvAngle(ang/angfactor);
	}
	actor->angle += ang;
#undef anglimit
#undef angfactor

	P_MoveOrigin(actor,
		crab->x + P_ReturnThrustX(actor, actor->angle, locvar1*crab->scale),
		crab->y + P_ReturnThrustY(actor, actor->angle, locvar1*crab->scale),
		crab->z + locvar2*crab->scale);

	if (!crab->target || !crab->info->missilestate || (statenum_t)(crab->state-states) == crab->info->missilestate)
		return;

	if (((ang + ANG1) < ANG2) || P_AproxDistance(crab->x - crab->target->x, crab->y - crab->target->y) < 333*crab->scale)
		P_SetMobjState(crab, crab->info->missilestate);
}

// Function: A_CrushclawLaunch
//
// Description: Crushstacean claw launching
//
// var1:
//		0 - forwards
//		anything else - backwards
// var2 = state to change to when done
//
void A_CrushclawLaunch(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	mobj_t *crab = actor->tracer;

	if (LUA_CallAction(A_CRUSHCLAWLAUNCH, actor))
		return;

	if (!crab)
	{
		mobj_t *chainnext;
		while (actor)
		{
			chainnext = actor->target;
			P_RemoveMobj(actor);
			actor = chainnext;
		}
		return; // there is only one step and it is crab
	}

	if (!actor->extravalue1)
	{
		S_StartSound(actor, actor->info->activesound);
		actor->extravalue1 = ((locvar1) ? -1 : 32);
	}
	else if (actor->extravalue1 != 1)
		actor->extravalue1 -= 1;

#define CSEGS 5
	if (!actor->target)
	{
		mobj_t *prevchain = actor;
		uint8_t i = 0;
		for (i = 0; (i < CSEGS); i++)
		{
			mobj_t *newchain = P_SpawnMobjFromMobj(actor, 0, 0, 0, (mobjtype_t)actor->info->raisestate);
			P_SetTarget(&prevchain->target, newchain);
			prevchain = newchain;
		}
		actor->target->angle = R_PointToAngle2(actor->target->x, actor->target->y, crab->target->x, crab->target->y);
	}

	if ((!locvar1) && crab->target)
	{
#define anglimit ANGLE_22h
#define angfactor 7
		angle_t ang = R_PointToAngle2(actor->target->x, actor->target->y, crab->target->x, crab->target->y) - actor->target->angle;
		if (ang < ANGLE_180)
		{
			if (ang > anglimit)
				ang = anglimit;
			ang /= angfactor;
		}
		else
		{
			ang = InvAngle(ang);
			if (ang > anglimit)
				ang = anglimit;
			ang /= angfactor;
			ang = InvAngle(ang);
		}
		actor->target->angle += ang;
		actor->angle = actor->target->angle;
	}

	actor->extravalue2 += actor->extravalue1;

	if (!P_TryMove(actor,
		actor->target->x + P_ReturnThrustX(actor, actor->target->angle, actor->extravalue2*actor->scale),
		actor->target->y + P_ReturnThrustY(actor, actor->target->angle, actor->extravalue2*actor->scale),
		true, NULL)
		&& !locvar1)
	{
		actor->extravalue1 = 0;
		actor->extravalue2 = FixedHypot(actor->x - actor->target->x, actor->y - actor->target->y)>>FRACBITS;
		P_SetMobjState(actor, locvar2);
		S_StopSound(actor);
		S_StartSound(actor, sfx_s3k49);
	}
	else
	{
		actor->z = actor->target->z;
		if ((!locvar1 && (actor->extravalue2 > 256)) || (locvar1 && (actor->extravalue2 < 16)))
		{
			if (locvar1) // In case of retracting, resume crab and remove the chain.
			{
				mobj_t *chain = actor->target, *chainnext;
				while (chain)
				{
					chainnext = chain->target;
					P_RemoveMobj(chain);
					chain = chainnext;
				}
				actor->extravalue2 = 0;
				actor->angle = R_PointToAngle2(crab->x, crab->y, actor->x, actor->y);
				P_SetTarget(&actor->target, NULL);
				P_SetTarget(&crab->target, NULL);
				P_SetMobjState(crab, crab->state->nextstate);
			}
			actor->extravalue1 = 0;
			P_SetMobjState(actor, locvar2);
			S_StopSound(actor);
			if (!locvar1)
				S_StartSound(actor, sfx_s3k64);
		}
	}

	if (!actor->target)
		return;

	{
		mobj_t *chain = actor->target->target;
		fixed_t dx = (actor->x - actor->target->x)/CSEGS, dy = (actor->y - actor->target->y)/CSEGS, dz = (actor->z - actor->target->z)/CSEGS;
		fixed_t idx = dx, idy = dy, idz = dz;
		while (chain)
		{
			P_MoveOrigin(chain, actor->target->x + idx, actor->target->y + idy, actor->target->z + idz);
			chain->movefactor = chain->z;
			idx += dx;
			idy += dy;
			idz += dz;
			chain = chain->target;
		}
	}
#undef CSEGS
}

// Function: A_VultureVtol
//
// Description: Vulture rising up to match target's height
//
// var1 = unused
// var2 = unused
//
void A_VultureVtol(void *thing)
{
	mobj_t *actor = thing;
	if (LUA_CallAction(A_VULTUREVTOL, actor))
		return;

	if (!actor->target)
		return;

	actor->flags |= MF_NOGRAVITY;
	actor->flags |= MF_FLOAT;

	A_FaceTarget(actor);

	S_StopSound(actor);

	if (actor->z < actor->target->z+(actor->target->height/4) && actor->z + actor->height < actor->ceilingz)
		actor->momz = FixedMul(2*FRACUNIT, actor->scale);
	else if (actor->z > (actor->target->z+(actor->target->height/4)*3) && actor->z > actor->floorz)
		actor->momz = FixedMul(-2*FRACUNIT, actor->scale);
	else
	{
		// Attack!
		actor->momz = 0;
		P_SetMobjState(actor, actor->info->missilestate);
		S_StartSound(actor, actor->info->activesound);
	}
}

// Function: A_VultureCheck
//
// Description: If the vulture is stopped, look for a new target
//
// var1 = unused
// var2 = unused
//
void A_VultureCheck(void *thing)
{
	mobj_t *actor = thing;
	if (LUA_CallAction(A_VULTURECHECK, actor))
		return;

	if (actor->momx || actor->momy)
		return;

	actor->flags &= ~MF_NOGRAVITY; // Fall down

	if (actor->z <= actor->floorz)
	{
		actor->angle -= ANGLE_180; // turn around
		P_SetMobjState(actor, actor->info->spawnstate);
	}
}

// Function: A_SkimChase
//
// Description: Thinker/Chase routine for Skims
//
// var1 = unused
// var2 = unused
//
void A_SkimChase(void *thing)
{
	mobj_t *actor = thing;
	int32_t delta;

	if (LUA_CallAction(A_SKIMCHASE, actor))
		return;

	if (actor->reactiontime)
		actor->reactiontime--;

	// modify target threshold
	if (actor->threshold)
	{
		if (!actor->target || actor->target->health <= 0)
			actor->threshold = 0;
		else
			actor->threshold--;
	}

	// turn towards movement direction if not there yet
	if (actor->movedir < NUMDIRS)
	{
		actor->angle &= (7<<29);
		delta = actor->angle - (actor->movedir << 29);

		if (delta > 0)
			actor->angle -= ANGLE_45;
		else if (delta < 0)
			actor->angle += ANGLE_45;
	}

	if (!actor->target || !(actor->target->flags & MF_SHOOTABLE))
	{
		// look for a new target
		P_LookForPlayers(actor, true, false, 0);

		// the spawnstate for skims already calls this function so just return either way
		// without changing state
		return;
	}

	// do not attack twice in a row
	if (actor->flags2 & MF2_JUSTATTACKED)
	{
		actor->flags2 &= ~MF2_JUSTATTACKED;
		P_NewChaseDir(actor);
		return;
	}

	// check for melee attack
	if (actor->info->meleestate && P_SkimCheckMeleeRange(actor))
	{
		if (actor->info->attacksound)
			S_StartSound(actor, actor->info->attacksound);

		P_SetMobjState(actor, actor->info->meleestate);
		return;
	}

	// check for missile attack
	if (actor->info->missilestate)
	{
		if (actor->movecount || !P_CheckMissileRange(actor))
			goto nomissile;

		P_SetMobjState(actor, actor->info->missilestate);
		actor->flags2 |= MF2_JUSTATTACKED;
		return;
	}

nomissile:
	// possibly choose another target
	if (multiplayer && !actor->threshold && (actor->target->health <= 0 || !P_CheckSight(actor, actor->target))
		&& P_LookForPlayers(actor, true, false, 0))
		return; // got a new target

	// chase towards player
	if (--actor->movecount < 0 || !P_Move(actor, actor->info->speed))
		P_NewChaseDir(actor);
}

// Function: A_FaceTarget
//
// Description: Immediately turn to face towards your target.
//
// var1 = unused
// var2 = unused
//
void A_FaceTarget(void *thing)
{
	mobj_t *actor = thing;
	if (LUA_CallAction(A_FACETARGET, actor))
		return;

	if (!actor->target)
		return;

	actor->angle = R_PointToAngle2(actor->x, actor->y, actor->target->x, actor->target->y);
}

// Function: A_FaceTracer
//
// Description: Immediately turn to face towards your tracer.
//
// var1 = unused
// var2 = unused
//
void A_FaceTracer(void *thing)
{
	mobj_t *actor = thing;
	if (LUA_CallAction(A_FACETRACER, actor))
		return;

	if (!actor->tracer)
		return;

	actor->angle = R_PointToAngle2(actor->x, actor->y, actor->tracer->x, actor->tracer->y);
}

// Function: A_LobShot
//
// Description: Lob an object at your target.
//
// var1 = object # to lob
// var2:
//		var2 >> 16 = height offset
//		var2 & 65535 = airtime
//
void A_LobShot(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2 >> 16;
	mobj_t *shot;
	angle_t an;
	fixed_t z;
	fixed_t dist;
	fixed_t vertical, horizontal;
	fixed_t airtime = var2 & 65535;

	if (LUA_CallAction(A_LOBSHOT, actor))
		return;

	if (!actor->target)
		return;

	A_FaceTarget(actor);

	if (actor->eflags & MFE_VERTICALFLIP)
	{
		z = actor->z + actor->height - FixedMul(locvar2*FRACUNIT, actor->scale);
		if (actor->type == MT_BLACKEGGMAN)
			z -= FixedMul(mobjinfo[locvar1].height, actor->scale/2);
		else
			z -= FixedMul(mobjinfo[locvar1].height, actor->scale);
	}
	else
		z = actor->z + FixedMul(locvar2*FRACUNIT, actor->scale);

	shot = P_SpawnMobj(actor->x, actor->y, z, locvar1);

	if (actor->type == MT_BLACKEGGMAN)
	{
		shot->destscale = actor->scale/2;
		P_SetScale(shot, actor->scale/2);
	}
	else
	{
		shot->destscale = actor->scale;
		P_SetScale(shot, actor->scale);
	}

	P_SetTarget(&shot->target, actor); // where it came from

	shot->angle = actor->angle;
	an = actor->angle >> ANGLETOFINESHIFT;

	dist = P_AproxDistance(actor->target->x - shot->x, actor->target->y - shot->y);

	horizontal = dist / airtime;
	vertical = FixedMul((gravity*airtime)/2, shot->scale);

	shot->momx = FixedMul(horizontal, FINECOSINE(an));
	shot->momy = FixedMul(horizontal, FINESINE(an));
	shot->momz = vertical;

/* Try to adjust when destination is not the same height
	if (actor->z != actor->target->z)
	{
		fixed_t launchhyp;
		fixed_t diff;
		fixed_t orig;

		diff = actor->z - actor->target->z;
		{
			launchhyp = P_AproxDistance(horizontal, vertical);

			orig = FixedMul(FixedDiv(vertical, horizontal), diff);

			CONS_Debug(DBG_GAMELOGIC, "orig: %d\n", (orig)>>FRACBITS);

			horizontal = dist / airtime;
			vertical = (gravity*airtime)/2;
		}
		dist -= orig;
		shot->momx = FixedMul(horizontal, FINECOSINE(an));
		shot->momy = FixedMul(horizontal, FINESINE(an));
		shot->momz = vertical;
*/

	if (shot->info->seesound)
		S_StartSound(shot, shot->info->seesound);

	if (!(actor->flags & MF_BOSS))
	{
		if (ultimatemode)
			actor->reactiontime = actor->info->reactiontime*TICRATE;
		else
			actor->reactiontime = actor->info->reactiontime*TICRATE*2;
	}
}

// Function: A_FireShot
//
// Description: Shoot an object at your target.
//
// var1 = object # to shoot
// var2 = height offset
//
void A_FireShot(void *thing)
{
	mobj_t *actor = thing;
	fixed_t z;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_FIRESHOT, actor))
		return;

	if (!actor->target)
		return;

	A_FaceTarget(actor);

	if (actor->eflags & MFE_VERTICALFLIP)
		z = actor->z + actor->height - FixedMul(48*FRACUNIT + locvar2*FRACUNIT, actor->scale);
	else
		z = actor->z + FixedMul(48*FRACUNIT + locvar2*FRACUNIT, actor->scale);

	P_SpawnXYZMissile(actor, actor->target, locvar1, actor->x, actor->y, z);

	if (!(actor->flags & MF_BOSS))
	{
		if (ultimatemode)
			actor->reactiontime = actor->info->reactiontime*TICRATE;
		else
			actor->reactiontime = actor->info->reactiontime*TICRATE*2;
	}
}

// Function: A_SuperFireShot
//
// Description: Shoot an object at your target that will even stall Super Sonic.
//
// var1 = object # to shoot
// var2 = height offset
//
void A_SuperFireShot(void *thing)
{
	mobj_t *actor = thing;
	fixed_t z;
	mobj_t *mo;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_SUPERFIRESHOT, actor))
		return;

	if (!actor->target)
		return;

	A_FaceTarget(actor);

	if (actor->eflags & MFE_VERTICALFLIP)
		z = actor->z + actor->height - FixedMul(48*FRACUNIT + locvar2*FRACUNIT, actor->scale);
	else
		z = actor->z + FixedMul(48*FRACUNIT + locvar2*FRACUNIT, actor->scale);

	mo = P_SpawnXYZMissile(actor, actor->target, locvar1, actor->x, actor->y, z);

	if (mo)
		mo->flags2 |= MF2_SUPERFIRE;

	if (!(actor->flags & MF_BOSS))
	{
		if (ultimatemode)
			actor->reactiontime = actor->info->reactiontime*TICRATE;
		else
			actor->reactiontime = actor->info->reactiontime*TICRATE*2;
	}
}

// Function: A_BossFireShot
//
// Description: Shoot an object at your target ala Bosses:
//
// var1 = object # to shoot
// var2:
//		0 - Boss 1 Left side
//		1 - Boss 1 Right side
//		2 - Boss 3 Left side upper
//		3 - Boss 3 Left side lower
//		4 - Boss 3 Right side upper
//		5 - Boss 3 Right side lower
//
void A_BossFireShot(void *thing)
{
	mobj_t *actor = thing;
	fixed_t x, y, z;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	mobj_t *missile;

	if (LUA_CallAction(A_BOSSFIRESHOT, actor))
		return;

	if (!actor->target)
		return;

	A_FaceTarget(actor);

	switch (locvar2)
	{
		case 0:
			x = actor->x + P_ReturnThrustX(actor, actor->angle+ANGLE_90, FixedMul(43*FRACUNIT, actor->scale));
			y = actor->y + P_ReturnThrustY(actor, actor->angle+ANGLE_90, FixedMul(43*FRACUNIT, actor->scale));
			if (actor->eflags & MFE_VERTICALFLIP)
				z = actor->z + actor->height - FixedMul(48*FRACUNIT, actor->scale);
			else
				z = actor->z + FixedMul(48*FRACUNIT, actor->scale);
			break;
		case 1:
			x = actor->x + P_ReturnThrustX(actor, actor->angle-ANGLE_90, FixedMul(43*FRACUNIT, actor->scale));
			y = actor->y + P_ReturnThrustY(actor, actor->angle-ANGLE_90, FixedMul(43*FRACUNIT, actor->scale));
			if (actor->eflags & MFE_VERTICALFLIP)
				z = actor->z + actor->height - FixedMul(48*FRACUNIT, actor->scale);
			else
				z = actor->z + FixedMul(48*FRACUNIT, actor->scale);
			break;
		case 2:
			x = actor->x + P_ReturnThrustX(actor, actor->angle-ANGLE_90, FixedMul(56*FRACUNIT, actor->scale));
			y = actor->y + P_ReturnThrustY(actor, actor->angle-ANGLE_90, FixedMul(56*FRACUNIT, actor->scale));
			if (actor->eflags & MFE_VERTICALFLIP)
				z = actor->z + actor->height - FixedMul(42*FRACUNIT, actor->scale);
			else
				z = actor->z + FixedMul(42*FRACUNIT, actor->scale);
			break;
		case 3:
			x = actor->x + P_ReturnThrustX(actor, actor->angle-ANGLE_90, FixedMul(58*FRACUNIT, actor->scale));
			y = actor->y + P_ReturnThrustY(actor, actor->angle-ANGLE_90, FixedMul(58*FRACUNIT, actor->scale));
			if (actor->eflags & MFE_VERTICALFLIP)
				z = actor->z + actor->height - FixedMul(30*FRACUNIT, actor->scale);
			else
				z = actor->z + FixedMul(30*FRACUNIT, actor->scale);
			break;
		case 4:
			x = actor->x + P_ReturnThrustX(actor, actor->angle+ANGLE_90, FixedMul(56*FRACUNIT, actor->scale));
			y = actor->y + P_ReturnThrustY(actor, actor->angle+ANGLE_90, FixedMul(56*FRACUNIT, actor->scale));
			if (actor->eflags & MFE_VERTICALFLIP)
				z = actor->z + actor->height - FixedMul(42*FRACUNIT, actor->scale);
			else
				z = actor->z + FixedMul(42*FRACUNIT, actor->scale);
			break;
		case 5:
			x = actor->x + P_ReturnThrustX(actor, actor->angle+ANGLE_90, FixedMul(58*FRACUNIT, actor->scale));
			y = actor->y + P_ReturnThrustY(actor, actor->angle+ANGLE_90, FixedMul(58*FRACUNIT, actor->scale));
			if (actor->eflags & MFE_VERTICALFLIP)
				z = actor->z + actor->height - FixedMul(30*FRACUNIT, actor->scale);
			else
				z = actor->z + FixedMul(30*FRACUNIT, actor->scale);
			break;
		default:
			x = actor->x;
			y = actor->y;
			z = actor->z + actor->height/2;
			break;
	}

	missile = P_SpawnXYZMissile(actor, actor->target, locvar1, x, y, z);

	if (missile && actor->tracer && (actor->tracer->flags & MF_BOSS)) // Don't harm your papa.
		P_SetTarget(&missile->target, actor->tracer);
}

// Function: A_Boss7FireMissiles
//
// Description: Shoot 4 missiles of a specific object type at your target ala Black Eggman
//
// var1 = object # to shoot
// var2 = firing sound
//
void A_Boss7FireMissiles(void *thing)
{
	mobj_t *actor = thing;
	mobj_t dummymo;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_BOSS7FIREMISSILES, actor))
		return;

	if (!actor->target)
	{
		P_SetMobjState(actor, actor->info->spawnstate);
		return;
	}

	A_FaceTarget(actor);

	S_StartSound(NULL, locvar2);

	// set dummymo's coordinates
	dummymo.x = actor->target->x;
	dummymo.y = actor->target->y;
	dummymo.z = actor->target->z + FixedMul(16*FRACUNIT, actor->scale); // raised height

	P_SpawnXYZMissile(actor, &dummymo, locvar1,
		actor->x + P_ReturnThrustX(actor, actor->angle-ANGLE_90, FixedDiv(actor->radius, 3*FRACUNIT/2)+FixedMul(4*FRACUNIT, actor->scale)),
		actor->y + P_ReturnThrustY(actor, actor->angle-ANGLE_90, FixedDiv(actor->radius, 3*FRACUNIT/2)+FixedMul(4*FRACUNIT, actor->scale)),
		actor->z + FixedDiv(actor->height, 3*FRACUNIT/2));

	P_SpawnXYZMissile(actor, &dummymo, locvar1,
		actor->x + P_ReturnThrustX(actor, actor->angle+ANGLE_90, FixedDiv(actor->radius, 3*FRACUNIT/2)+FixedMul(4*FRACUNIT, actor->scale)),
		actor->y + P_ReturnThrustY(actor, actor->angle+ANGLE_90, FixedDiv(actor->radius, 3*FRACUNIT/2)+FixedMul(4*FRACUNIT, actor->scale)),
		actor->z + FixedDiv(actor->height, 3*FRACUNIT/2));

	P_SpawnXYZMissile(actor, &dummymo, locvar1,
		actor->x + P_ReturnThrustX(actor, actor->angle-ANGLE_90, FixedDiv(actor->radius, 3*FRACUNIT/2)+FixedMul(4*FRACUNIT, actor->scale)),
		actor->y + P_ReturnThrustY(actor, actor->angle-ANGLE_90, FixedDiv(actor->radius, 3*FRACUNIT/2)+FixedMul(4*FRACUNIT, actor->scale)),
		actor->z + actor->height/2);

	P_SpawnXYZMissile(actor, &dummymo, locvar1,
		actor->x + P_ReturnThrustX(actor, actor->angle+ANGLE_90, FixedDiv(actor->radius, 3*FRACUNIT/2)+FixedMul(4*FRACUNIT, actor->scale)),
		actor->y + P_ReturnThrustY(actor, actor->angle+ANGLE_90, FixedDiv(actor->radius, 3*FRACUNIT/2)+FixedMul(4*FRACUNIT, actor->scale)),
		actor->z + actor->height/2);
}

// Function: A_Boss1Laser
//
// Description: Shoot an object at your target ala Bosses:
//
// var1 = object # to shoot
// var2:
//		0 - Boss 1 Left side
//		1 - Boss 1 Right side
//		2 - Triple laser
//		3 - Boss 1 Middle
//		>=3 - Generic middle
//
// Function: A_Boss1Laser
//
// Description: Shoot an object at your target ala Bosses:
//
// var1 = object # to shoot
// var2:
//		0 - Boss 1 Left side
//		1 - Boss 1 Right side
//
void A_Boss1Laser(void *thing)
{
	mobj_t *actor = thing;
	fixed_t x, y, z, floorz, speed;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	int32_t i;
	angle_t angle;
	mobj_t *point;

	if (LUA_CallAction(A_BOSS1LASER, actor))
		return;

	if (!actor->target)
		return;

	switch (locvar2)
	{
		case 0:
			x = actor->x + P_ReturnThrustX(actor, actor->angle+ANGLE_90, FixedMul(43*FRACUNIT, actor->scale));
			y = actor->y + P_ReturnThrustY(actor, actor->angle+ANGLE_90, FixedMul(43*FRACUNIT, actor->scale));
			if (actor->eflags & MFE_VERTICALFLIP)
				z = actor->z + actor->height - FixedMul(56*FRACUNIT, actor->scale) - mobjinfo[locvar1].height;
			else
				z = actor->z + FixedMul(56*FRACUNIT, actor->scale);
			break;
		case 1:
			x = actor->x + P_ReturnThrustX(actor, actor->angle-ANGLE_90, FixedMul(43*FRACUNIT, actor->scale));
			y = actor->y + P_ReturnThrustY(actor, actor->angle-ANGLE_90, FixedMul(43*FRACUNIT, actor->scale));
			if (actor->eflags & MFE_VERTICALFLIP)
				z = actor->z + actor->height - FixedMul(56*FRACUNIT, actor->scale) - mobjinfo[locvar1].height;
			else
				z = actor->z + FixedMul(56*FRACUNIT, actor->scale);
			break;
		default:
			x = actor->x;
			y = actor->y;
			z = actor->z + actor->height/2;
			break;
	}

	if (!(actor->flags2 & MF2_FIRING))
	{
		actor->angle = R_PointToAngle2(x, y, actor->target->x, actor->target->y);
		if (mobjinfo[locvar1].seesound)
			S_StartSound(actor, mobjinfo[locvar1].seesound);
		if (!(actor->spawnpoint && actor->spawnpoint->options & MTF_AMBUSH))
		{
			point = P_SpawnMobj(x + P_ReturnThrustX(actor, actor->angle, actor->radius), y + P_ReturnThrustY(actor, actor->angle, actor->radius), actor->z - actor->height / 2, MT_EGGMOBILE_TARGET);
			point->fuse = actor->tics+1;
			P_SetTarget(&point->target, actor->target);
			P_SetTarget(&actor->target, point);
		}
	}
	else if (actor->target && !(actor->spawnpoint && actor->spawnpoint->options & MTF_AMBUSH))
		actor->angle = R_PointToAngle2(x, y, actor->target->x, actor->target->y);

	if (actor->spawnpoint && actor->spawnpoint->options & MTF_AMBUSH)
		angle = FixedAngle(FixedDiv(actor->tics*160*FRACUNIT, actor->state->tics*FRACUNIT) + 10*FRACUNIT);
	else
		angle = R_PointToAngle2(z + (mobjinfo[locvar1].height>>1), 0, actor->target->z, R_PointToDist2(x, y, actor->target->x, actor->target->y));
	point = P_SpawnMobj(x, y, z, locvar1);
	P_SetTarget(&point->target, actor);
	point->angle = actor->angle;
	speed = point->radius*2;
	point->momz = FixedMul(FINECOSINE(angle>>ANGLETOFINESHIFT), speed);
	point->momx = FixedMul(FINESINE(angle>>ANGLETOFINESHIFT), FixedMul(FINECOSINE(point->angle>>ANGLETOFINESHIFT), speed));
	point->momy = FixedMul(FINESINE(angle>>ANGLETOFINESHIFT), FixedMul(FINESINE(point->angle>>ANGLETOFINESHIFT), speed));

	for (i = 0; i < 256; i++)
	{
		mobj_t *mo = P_SpawnMobj(point->x, point->y, point->z, point->type);
		mo->angle = point->angle;
		P_UnsetThingPosition(mo);
		mo->flags = MF_NOBLOCKMAP|MF_NOCLIP|MF_NOCLIPHEIGHT|MF_NOGRAVITY|MF_SCENERY;
		P_SetThingPosition(mo);

		x = point->x, y = point->y, z = point->z;
		if (P_RailThinker(point))
			break;
	}

	floorz = P_FloorzAtPos(x, y, z, mobjinfo[MT_EGGMOBILE_FIRE].height);
	if (z - floorz < mobjinfo[MT_EGGMOBILE_FIRE].height>>1)
	{
		point = P_SpawnMobj(x, y, floorz+1, MT_EGGMOBILE_FIRE);
		P_SetTarget(&point->target, actor);
		point->destscale = 3*FRACUNIT;
		point->scalespeed = FRACUNIT>>2;
		point->fuse = TICRATE;
	}

	if (actor->tics > 1)
		actor->flags2 |= MF2_FIRING;
	else
		actor->flags2 &= ~MF2_FIRING;
}

// Function: A_FocusTarget
//
// Description: Home in on your target.
//
// var1:
//		0 - accelerative focus with friction
//		1 - steady focus with fixed movement speed
// var2 = unused
//
void A_FocusTarget(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;

	if (LUA_CallAction(A_FOCUSTARGET, actor))
		return;

	if (actor->target)
	{
		fixed_t speed = FixedMul(actor->info->speed, actor->scale);
		fixed_t dist = R_PointToDist2(actor->x, actor->y, actor->target->x, actor->target->y);
		angle_t vangle = R_PointToAngle2(actor->z , 0, actor->target->z + (actor->target->height>>1), dist);
		angle_t hangle = R_PointToAngle2(actor->x, actor->y, actor->target->x, actor->target->y);
		switch(locvar1)
		{
		case 0:
			{
				actor->momx -= actor->momx>>4, actor->momy -= actor->momy>>4, actor->momz -= actor->momz>>4;
				actor->momz += FixedMul(FINECOSINE(vangle>>ANGLETOFINESHIFT), speed);
				actor->momx += FixedMul(FINESINE(vangle>>ANGLETOFINESHIFT), FixedMul(FINECOSINE(hangle>>ANGLETOFINESHIFT), speed));
				actor->momy += FixedMul(FINESINE(vangle>>ANGLETOFINESHIFT), FixedMul(FINESINE(hangle>>ANGLETOFINESHIFT), speed));
			}
			break;
		case 1:
			if (dist > speed)
			{
				actor->momz = FixedMul(FINECOSINE(vangle>>ANGLETOFINESHIFT), speed);
				actor->momx = FixedMul(FINESINE(vangle>>ANGLETOFINESHIFT), FixedMul(FINECOSINE(hangle>>ANGLETOFINESHIFT), speed));
				actor->momy = FixedMul(FINESINE(vangle>>ANGLETOFINESHIFT), FixedMul(FINESINE(hangle>>ANGLETOFINESHIFT), speed));
			}
			else
			{
				actor->momx = 0, actor->momy = 0, actor->momz = 0;
				actor->z = actor->target->z + (actor->target->height>>1);
				P_TryMove(actor, actor->target->x, actor->target->y, true, NULL);
			}
			break;
		default:
			break;
		}
	}
}

// Function: A_Boss4Reverse
//
// Description: Reverse arms direction.
//
// var1 = sfx to play
// var2 = sfx to play in pinch
//
void A_Boss4Reverse(void *thing)
{
	mobj_t *actor = thing;
	sfxenum_t locvar1 = (sfxenum_t)var1;
	sfxenum_t locvar2 = (sfxenum_t)var2;

	if (LUA_CallAction(A_BOSS4REVERSE, actor))
		return;

	actor->reactiontime = 0;
	if (actor->movedir < 3)
	{
		S_StartSound(NULL, locvar1);
		if (actor->movedir == 1)
			actor->movedir = 2;
		else
			actor->movedir = 1;
	}
	else
	{
		S_StartSound(NULL, locvar2);
		if (actor->movedir == 4)
			actor->movedir = 5;
		else
			actor->movedir = 4;
		actor->angle += ANGLE_180;
		actor->movefactor = -actor->movefactor;
	}
}

// Function: A_Boss4SpeedUp
//
// Description: Speed up arms
//
// var1 = sfx to play
// var2 = unused
//
void A_Boss4SpeedUp(void *thing)
{
	mobj_t *actor = thing;
	sfxenum_t locvar1 = (sfxenum_t)var1;

	if (LUA_CallAction(A_BOSS4SPEEDUP, actor))
		return;

	S_StartSound(NULL, locvar1);
	actor->reactiontime = 2;
}

// Function: A_Boss4Raise
//
// Description: Raise helmet
//
// var1 = sfx to play
// var2 = unused
//
void A_Boss4Raise(void *thing)
{
	mobj_t *actor = thing;
	sfxenum_t locvar1 = (sfxenum_t)var1;

	if (LUA_CallAction(A_BOSS4RAISE, actor))
		return;

	S_StartSound(NULL, locvar1);
	actor->reactiontime = 1;
}

// Function: A_SkullAttack
//
// Description: Fly at the player like a missile.
//
// var1:
//		0 - Fly at the player
//		1 - Fly away from the player
//		2 - Strafe in relation to the player
//      3 - Dynamic mode - don't get too close to walls
// var2:
//		0 - Fly horizontally and vertically
//		1 - Fly horizontal-only (momz = 0)
//
#define SKULLSPEED (20*FRACUNIT)

void A_SkullAttack(void *thing)
{
	mobj_t *actor = thing;
	mobj_t *dest;
	angle_t an;
	int32_t dist;
	int32_t speed;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_SKULLATTACK, actor))
		return;

	if (!actor->target)
		return;

	speed = FixedMul(SKULLSPEED, actor->scale);

	dest = actor->target;
	actor->flags2 |= MF2_SKULLFLY;
	if (actor->info->activesound)
		S_StartSound(actor, actor->info->activesound);
	A_FaceTarget(actor);

	dist = P_AproxDistance(dest->x - actor->x, dest->y - actor->y);

	if (locvar1 == 1)
		actor->angle += ANGLE_180;
	else if (locvar1 == 2)
		actor->angle += (P_RandomChance(PR_ACTION, FRACUNIT/2)) ? ANGLE_90 : -ANGLE_90;
	else if (locvar1 == 3)
	{
		statenum_t oldspawnstate = mobjinfo[MT_RAY].spawnstate;
		uint32_t oldflags = mobjinfo[MT_RAY].flags;
		fixed_t oldradius = mobjinfo[MT_RAY].radius;
		fixed_t oldheight = mobjinfo[MT_RAY].height;
		int32_t i, j;
		static int32_t k;/* static for (at least) GCC 9.1 weirdness */
		angle_t testang = 0;

		mobjinfo[MT_RAY].spawnstate = S_INVISIBLE;
		mobjinfo[MT_RAY].flags = MF_NOGRAVITY|MF_NOTHINK|MF_NOCLIPTHING|MF_NOBLOCKMAP;
		mobjinfo[MT_RAY].radius = mobjinfo[actor->type].radius;
		mobjinfo[MT_RAY].height = mobjinfo[actor->type].height;

		if (P_RandomChance(PR_ACTION, FRACUNIT/2)) // port priority 1?
		{
			i = 9;
			j = 27;
		}
		else
		{
			i = 27;
			j = 9;
		}

#define dostuff(q) \
			testang = actor->angle + ((i+(q))*ANG10);\
			if (P_CheckMove(actor,\
				P_ReturnThrustX(actor, testang, dist + 2*actor->radius),\
				P_ReturnThrustY(actor, testang, dist + 2*actor->radius),\
				true, NULL)) break;

		if (P_RandomChance(PR_ACTION, FRACUNIT/2)) // port priority 2?
		{
			for (k = 0; k < 9; k++)
			{
				dostuff(i+k)
				dostuff(i-k)
				dostuff(j+k)
				dostuff(j-k)
			}
		}
		else
		{
			for (k = 0; k < 9; k++)
			{
				dostuff(i-k)
				dostuff(i+k)
				dostuff(j-k)
				dostuff(j+k)
			}
		}
		actor->angle = testang;

#undef dostuff

		mobjinfo[MT_RAY].spawnstate = oldspawnstate;
		mobjinfo[MT_RAY].flags = oldflags;
		mobjinfo[MT_RAY].radius = oldradius;
		mobjinfo[MT_RAY].height = oldheight;
	}

	an = actor->angle >> ANGLETOFINESHIFT;

	actor->momx = FixedMul(speed, FINECOSINE(an));
	actor->momy = FixedMul(speed, FINESINE(an));
	dist = dist / speed;

	if (dist < 1)
		dist = 1;

	actor->momz = (dest->z + (dest->height>>1) - actor->z) / dist;

	if (locvar1 == 1)
		actor->momz = -actor->momz;
	if (locvar2 == 1)
		actor->momz = 0;
}

// Function: A_BossZoom
//
// Description: Like A_SkullAttack, but used by Boss 1.
//
// var1 = unused
// var2 = unused
//
void A_BossZoom(void *thing)
{
	mobj_t *actor = thing;
	mobj_t *dest;
	angle_t an;
	int32_t dist;

	if (LUA_CallAction(A_BOSSZOOM, actor))
		return;

	if (!actor->target)
		return;

	dest = actor->target;
	actor->flags2 |= MF2_SKULLFLY;
	if (actor->info->attacksound)
		S_StartSound(actor, actor->info->attacksound);
	A_FaceTarget(actor);
	an = actor->angle >> ANGLETOFINESHIFT;
	actor->momx = FixedMul(FixedMul(actor->info->speed*5*FRACUNIT, actor->scale), FINECOSINE(an));
	actor->momy = FixedMul(FixedMul(actor->info->speed*5*FRACUNIT, actor->scale), FINESINE(an));
	dist = P_AproxDistance(dest->x - actor->x, dest->y - actor->y);
	dist = dist / FixedMul(actor->info->speed*5*FRACUNIT, actor->scale);

	if (dist < 1)
		dist = 1;
	actor->momz = (dest->z + (dest->height>>1) - actor->z) / dist;
}

// Function: A_BossScream
//
// Description: Spawns explosions and plays appropriate sounds around the defeated boss.
//
// var1:
//		& 1 - Use P_Random to spawn explosions at complete random
//		& 2 - Use entire vertical range of object to spawn
// var2 = Object to spawn. Default is MT_SONIC3KBOSSEXPLODE.
//
void A_BossScream(void *thing)
{
	mobj_t *actor = thing;
	mobj_t *mo;
	fixed_t x, y, z;
	angle_t fa;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	mobjtype_t explodetype;

	if (LUA_CallAction(A_BOSSSCREAM, actor))
		return;

	if (locvar1 & 1)
		fa = (FixedAngle(P_RandomKey(PR_BOSSSCREAM, 360)*FRACUNIT)>>ANGLETOFINESHIFT) & FINEMASK;
	else
	{
		actor->movecount += 4*16;
		actor->movecount %= 360;
		fa = (FixedAngle(actor->movecount*FRACUNIT)>>ANGLETOFINESHIFT) & FINEMASK;
	}
	x = actor->x + FixedMul(FINECOSINE(fa),actor->radius);
	y = actor->y + FixedMul(FINESINE(fa),actor->radius);

	// Determine what mobj to spawn. If undefined or invalid, use MT_BOSSEXPLODE as default.
	if (locvar2 <= 0 || locvar2 >= NUMMOBJTYPES)
		explodetype = MT_SONIC3KBOSSEXPLODE; //MT_BOSSEXPLODE; -- piss to you, sonic 2
	else
		explodetype = (mobjtype_t)locvar2;

	if (locvar1 & 2)
		z = actor->z + (P_RandomKey(PR_BOSSSCREAM, (actor->height - mobjinfo[explodetype].height)>>FRACBITS)<<FRACBITS);
	else if (actor->eflags & MFE_VERTICALFLIP)
		z = actor->z + actor->height - mobjinfo[explodetype].height - FixedMul((P_RandomByte(PR_BOSSSCREAM)<<(FRACBITS-2)) - 8*FRACUNIT, actor->scale);
	else
		z = actor->z + FixedMul((P_RandomByte(PR_BOSSSCREAM)<<(FRACBITS-2)) - 8*FRACUNIT, actor->scale);

	mo = P_SpawnMobj(x, y, z, explodetype);
	if (actor->eflags & MFE_VERTICALFLIP)
		mo->flags2 |= MF2_OBJECTFLIP;
	mo->destscale = actor->scale;
	P_SetScale(mo, mo->destscale);
	if (actor->info->deathsound)
		S_StartSound(mo, actor->info->deathsound);
}

// Function: A_Scream
//
// Description: Starts the death sound of the object.
//
// var1 = unused
// var2 = unused
//
void A_Scream(void *thing)
{
	mobj_t *actor = thing;
	if (LUA_CallAction(A_SCREAM, actor))
		return;

	if (actor->tracer && (actor->tracer->type == MT_SHELL || actor->tracer->type == MT_FIREBALL))
		S_StartSound(actor, sfx_mario2);
	else if (actor->info->deathsound)
		S_StartSound(actor, actor->info->deathsound);
}

// Function: A_Pain
//
// Description: Starts the pain sound of the object.
//
// var1 = unused
// var2 = unused
//
void A_Pain(void *thing)
{
	mobj_t *actor = thing;
	if (LUA_CallAction(A_PAIN, actor))
		return;

	if (actor->info->painsound)
		S_StartSound(actor, actor->info->painsound);

	actor->flags2 &= ~MF2_FIRING;
	actor->flags2 &= ~MF2_SUPERFIRE;
}

// Function: A_Fall
//
// Description: Changes a dying object's flags to reflect its having fallen to the ground.
//
// var1 = value to set repeat to if nonzero
// var2 = unused
//
void A_Fall(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;

	if (LUA_CallAction(A_FALL, actor))
		return;

	// actor is on ground, it can be walked over
	actor->flags &= ~MF_SOLID;

	// fall through the floor
	actor->flags |= MF_NOCLIP|MF_NOCLIPHEIGHT|MF_NOGRAVITY;

	// So change this if corpse objects
	// are meant to be obstacles.

	if (locvar1)
		actor->extravalue2 = locvar1;
}

// Function: A_MonitorPop
//
// Description: Used by monitors when they explode.
//
// var1 = unused
// var2 = unused
//
void A_MonitorPop(void *thing)
{
	mobj_t *actor = thing;
	mobj_t *remains;
	mobjtype_t explode;
	mobjtype_t item = 0;
	//mobjtype_t newbox;

	if (LUA_CallAction(A_MONITORPOP, actor))
		return;

	// de-solidify
	actor->health = 0;
	P_UnsetThingPosition(actor);
	actor->flags &= ~MF_SOLID;
	actor->flags |= MF_NOCLIP;
	P_SetThingPosition(actor);

	// Monitor explosion
	explode = mobjinfo[actor->info->speed].mass;
	remains = P_SpawnMobj(actor->x, actor->y,
		((actor->eflags & MFE_VERTICALFLIP) ? (actor->z + 3*(actor->height/4) - FixedMul(mobjinfo[explode].height, actor->scale)) : (actor->z + actor->height/4)), explode);
	if (actor->eflags & MFE_VERTICALFLIP)
	{
		remains->eflags |= MFE_VERTICALFLIP;
		remains->flags2 |= MF2_OBJECTFLIP;
	}
	remains->destscale = actor->destscale;
	P_SetScale(remains, actor->scale);

	remains = P_SpawnMobj(actor->x, actor->y,
		((actor->eflags & MFE_VERTICALFLIP) ? (actor->z + actor->height - FixedMul(mobjinfo[actor->info->speed].height, actor->scale)) : actor->z),
		actor->info->speed);
	remains->type = actor->type; // Transfer type information
	P_UnsetThingPosition(remains);
	if (sector_list)
	{
		P_DelSeclist(sector_list);
		sector_list = NULL;
	}
	P_SetThingPosition(remains);
	remains->destscale = actor->destscale;
	P_SetScale(remains, actor->scale);
	remains->flags = actor->flags; // Transfer flags
	remains->flags2 = actor->flags2; // Transfer flags2
	remains->fuse = actor->fuse; // Transfer respawn timer
	remains->threshold = 68;
	remains->skin = NULL;

	P_SetTarget(&g_tm.thing, remains);

	if (actor->info->deathsound)
		S_StartSound(remains, actor->info->deathsound);

	switch (actor->type)
	{
		default:
			item = actor->info->damage;
			break;
	}

	if (item != 0)
	{
		mobj_t *newmobj;

		if (actor->eflags & MFE_VERTICALFLIP)
		{
			newmobj = P_SpawnMobj(actor->x, actor->y, actor->z + actor->height - FixedMul(13*FRACUNIT + mobjinfo[item].height, actor->scale), item);
			newmobj->eflags |= MFE_VERTICALFLIP;
		}
		else
			newmobj = P_SpawnMobj(actor->x, actor->y, actor->z + FixedMul(13*FRACUNIT, actor->scale), item);

		newmobj->destscale = actor->destscale;
		P_SetScale(newmobj, actor->scale);
		P_SetTarget(&newmobj->target, actor->target); // Transfer target
		if (item == MT_1UPICO && newmobj->target->player)
		{
			if (actor->tracer) // Remove the old lives icon.
				P_RemoveMobj(actor->tracer);

			newmobj->frame -= 2; // No lives icon for this player, use the default.
		}
	}
	else
		CONS_Debug(DBG_GAMELOGIC, "Powerup item not defined in 'damage' field for A_MonitorPop\n");

	P_RemoveMobj(actor);
}

// Function: A_Explode
//
// Description: Explodes an object, doing damage to any objects nearby. The target is used as the cause of the explosion. Damage value is used as explosion range.
//
// var1 = damagetype
// var2 = unused
//
void A_Explode(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;

	if (LUA_CallAction(A_EXPLODE, actor))
		return;

	P_RadiusAttack(actor, actor->target, actor->info->damage, locvar1, true);
}

static mobj_t *P_FindBossFlyPoint(mobj_t *mo, int32_t tag)
{
	int32_t i;
	mobj_t *closest = NULL;

	TAG_ITER_THINGS(tag, i)
	{
		mobj_t *mo2 = mapthings[i].mobj;

		if (!mo2)
			continue;

		if (mo2->type != MT_BOSSFLYPOINT)
			continue;

		// If this one's further than the last one, don't go for it.
		if (closest &&
			P_AproxDistance(P_AproxDistance(mo->x - mo2->x, mo->y - mo2->y), mo->z - mo2->z) >
			P_AproxDistance(P_AproxDistance(mo->x - closest->x, mo->y - closest->y), mo->z - closest->z))
			continue;

		closest = mo2;
	}

	return closest;
}

static void P_DoBossVictory(mobj_t *mo)
{
	thinker_t *th;
	mobj_t *mo2;

	// scan the remaining thinkers to see if all bosses are dead
	for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
	{
		if (th->function == (actionf_p1)P_RemoveThinkerDelayed)
			continue;

		mo2 = (mobj_t *)th;

		if (mo2 == mo)
			continue;

		if ((mo2->flags & MF_BOSS) && mo2->health > 0)
			return;
	}

	// victory!
	P_LinedefExecute(mo->spawnpoint->args[3], mo, NULL);

	if (stoppedclock && modeattacking) // if you're just time attacking, skip making the capsule appear since you don't need to step on it anyways.
		return;

	if (mo->flags2 & MF2_BOSSNOTRAP)
	{
		P_DoAllPlayersExit(0, false);
	}
	else
	{
		if (!udmf)
		{
			// Bring the egg trap up to the surface
			// Incredibly shitty code ahead
			EV_DoElevator(LE_CAPSULE0, NULL, elevateHighest);
			EV_DoElevator(LE_CAPSULE1, NULL, elevateUp);
			EV_DoElevator(LE_CAPSULE2, NULL, elevateHighest);
		}
	}
}

static void P_DoBossDefaultDeath(mobj_t *mo)
{
	int32_t bossid = mo->args[0];

	// Stop exploding and prepare to run.
	P_SetMobjState(mo, mo->info->xdeathstate);
	if (P_MobjWasRemoved(mo))
		return;

	P_SetTarget(&mo->target, P_FindBossFlyPoint(mo, bossid));

	mo->flags |= MF_NOGRAVITY|MF_NOCLIP;
	mo->flags |= MF_NOCLIPHEIGHT;

	mo->movedir = 0;
	mo->extravalue1 = 35;
	mo->flags2 |= MF2_BOSSFLEE;
	mo->momz = P_MobjFlip(mo)*2*mo->scale;

	if (mo->target)
	{
		angle_t diff = R_PointToAngle2(mo->x, mo->y, mo->target->x, mo->target->y) - mo->angle;
		if (diff)
		{
			if (diff > ANGLE_180)
				diff = InvAngle(InvAngle(diff)/mo->extravalue1);
			else
				diff /= mo->extravalue1;
			mo->movedir = diff;
		}
	}
}

// Function: A_BossDeath
//
// Description: Possibly trigger special effects when boss dies.
//
// var1 = unused
// var2 = unused
//
void A_BossDeath(void *thing)
{
	mobj_t *mo = thing;
	int32_t i;

	if (LUA_CallAction(A_BOSSDEATH, mo))
		return;

	P_LinedefExecute(mo->spawnpoint->args[2], mo, NULL);
	mo->health = 0;

	// Boss is dead (but not necessarily fleeing...)
	// Lua may use this to ignore bosses after they start fleeing
	mo->flags2 |= MF2_BOSSDEAD;

	// make sure there is a player alive for victory
	for (i = 0; i < MAXPLAYERS; i++)
		if (playeringame[i] && ((players[i].mo && players[i].mo->health)
			|| ((netgame || multiplayer) && players[i].lives)))
			break;

	if (i == MAXPLAYERS)
		return; // no one left alive, so do not end game

	P_DoBossVictory(mo);

	if (LUA_HookMobj(mo, MOBJ_HOOK(BossDeath)))
		return;
	else if (P_MobjWasRemoved(mo))
		return;

	// now do another switch case for escaping
	switch (mo->type)
	{
		case MT_BLACKEGGMAN:
		{
			mo->flags |= MF_NOCLIP;
			mo->flags &= ~MF_SPECIAL;

			S_StartSound(NULL, sfx_befall);
			break;
		}
		case MT_CYBRAKDEMON:
		{
			mo->flags |= MF_NOCLIP;
			mo->flags &= ~(MF_SPECIAL|MF_NOGRAVITY|MF_NOCLIPHEIGHT);

			S_StartSound(NULL, sfx_bedie2);
			P_SpawnMobjFromMobj(mo, 0, 0, 0, MT_CYBRAKDEMON_VILE_EXPLOSION);
			mo->z += P_MobjFlip(mo);
			P_SetObjectMomZ(mo, 12*FRACUNIT, false);
			S_StartSound(mo, sfx_bgxpld);
			if (mo->spawnpoint && !(mo->spawnpoint->options & MTF_EXTRA))
				P_InstaThrust(mo, R_PointToAngle2(0, 0, mo->x, mo->y), 14*FRACUNIT);
			break;
		}
		default: //eggmobiles
			P_DoBossDefaultDeath(mo);
			break;
	}
}

// Function: A_RingBox
//
// Description: Awards the player 10 rings.
//
// var1 = unused
// var2 = unused
//
void A_RingBox(void *thing)
{
	mobj_t *actor = thing;
	player_t *player;

	if (LUA_CallAction(A_RINGBOX, actor))
		return;

	if (!actor->target || !actor->target->player)
	{
		CONS_Debug(DBG_GAMELOGIC, "Powerup has no target.\n");
		return;
	}

	player = actor->target->player;

	K_AwardPlayerRings(player, actor->info->reactiontime, true);
	if (actor->info->seesound)
		S_StartSound(player->mo, actor->info->seesound);
}

// Function: A_AwardScore
//
// Description: Adds a set amount of points to the player's score.
//
// var1 = unused
// var2 = unused
//
void A_AwardScore(void *thing)
{
	mobj_t *actor = thing;
	player_t *player;

	if (LUA_CallAction(A_AWARDSCORE, actor))
		return;

	if (!actor->target || !actor->target->player)
	{
		CONS_Debug(DBG_GAMELOGIC, "Powerup has no target.\n");
		return;
	}

	player = actor->target->player;

	P_AddPlayerScore(player, actor->info->reactiontime);
	if (actor->info->seesound)
		S_StartSound(player->mo, actor->info->seesound);
}

// Function: A_ScoreRise
//
// Description: Makes the little score logos rise. Speed value sets speed.
//
// var1 = unused
// var2 = unused
//
void A_ScoreRise(void *thing)
{
	mobj_t *actor = thing;
	if (LUA_CallAction(A_SCORERISE, actor))
		return;

	// make logo rise!
	P_SetObjectMomZ(actor, actor->info->speed, false);
}

// Function: A_BunnyHop
//
// Description: Makes object hop like a bunny.
//
// var1 = jump strength
// var2 = horizontal movement
//
void A_BunnyHop(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_BUNNYHOP, actor))
		return;

	if (((actor->eflags & MFE_VERTICALFLIP) && actor->z + actor->height >= actor->ceilingz)
		|| (!(actor->eflags & MFE_VERTICALFLIP) && actor->z <= actor->floorz))
	{
		P_SetObjectMomZ(actor, locvar1*FRACUNIT, false);
		P_InstaThrust(actor, actor->angle, FixedMul(locvar2*FRACUNIT, actor->scale)); // Launch the hopping action! PHOOM!!
	}
}

// Function: A_BubbleSpawn
//
// Description: Spawns a randomly sized bubble from the object's location. Only works underwater.
//
// var1 = Distance to look for players.  If no player is in this distance, bubbles aren't spawned. (Ambush overrides)
// var2 = unused
//
void A_BubbleSpawn(void *thing)
{
	mobj_t *actor = thing;
	int32_t i, locvar1 = var1;
	uint8_t prandom;
	mobj_t *bubble = NULL;

	if (LUA_CallAction(A_BUBBLESPAWN, actor))
		return;

	if (!(actor->eflags & MFE_UNDERWATER))
	{
		// Don't draw or spawn bubbles above water
		actor->renderflags |= RF_DONTDRAW;
		return;
	}
	actor->renderflags &= ~RF_DONTDRAW;

	if (!(actor->flags2 & MF2_AMBUSH))
	{
		// Quick! Look through players!
		// Don't spawn bubbles unless a player is relatively close by (var1).
		for (i = 0; i < MAXPLAYERS; ++i)
			if (playeringame[i] && players[i].mo
			 && P_AproxDistance(actor->x - players[i].mo->x, actor->y - players[i].mo->y) < (locvar1<<FRACBITS))
				break; // Stop looking.
		if (i == MAXPLAYERS)
			return; // don't make bubble!
	}

	prandom = P_RandomByte(PR_BUBBLE);

	if (leveltime % (3*TICRATE) < 8)
		bubble = P_SpawnMobj(actor->x, actor->y, actor->z + (actor->height / 2), MT_EXTRALARGEBUBBLE);
	else if (prandom > 128)
		bubble = P_SpawnMobj(actor->x, actor->y, actor->z + (actor->height / 2), MT_SMALLBUBBLE);
	else if (prandom < 128 && prandom > 96)
		bubble = P_SpawnMobj(actor->x, actor->y, actor->z + (actor->height / 2), MT_MEDIUMBUBBLE);

	if (bubble)
	{
		bubble->destscale = actor->scale;
		P_SetScale(bubble, actor->scale);
	}
}

// Function: A_FanBubbleSpawn
//
// Description: Spawns bubbles from fans, if they're underwater.
//
// var1 = Distance to look for players.  If no player is in this distance, bubbles aren't spawned. (Ambush overrides)
// var2 = unused
//
void A_FanBubbleSpawn(void *thing)
{
	mobj_t *actor = thing;
	int32_t i, locvar1 = var1;
	uint8_t prandom;
	mobj_t *bubble = NULL;
	fixed_t hz = actor->z + (4*actor->height)/5;

	if (LUA_CallAction(A_FANBUBBLESPAWN, actor))
		return;

	if (!(actor->eflags & MFE_UNDERWATER))
		return;

	if (!(actor->flags2 & MF2_AMBUSH))
	{
	// Quick! Look through players!
	// Don't spawn bubbles unless a player is relatively close by (var2).
		for (i = 0; i < MAXPLAYERS; ++i)
			if (playeringame[i] && players[i].mo
			 && P_AproxDistance(actor->x - players[i].mo->x, actor->y - players[i].mo->y) < (locvar1<<FRACBITS))
				break; // Stop looking.
		if (i == MAXPLAYERS)
			return; // don't make bubble!
	}

	prandom = P_RandomByte(PR_BUBBLE);

	if ((prandom & 0x7) == 0x7)
		bubble = P_SpawnMobj(actor->x, actor->y, hz, MT_SMALLBUBBLE);
	else if ((prandom & 0xF0) == 0xF0)
		bubble = P_SpawnMobj(actor->x, actor->y, hz, MT_MEDIUMBUBBLE);

	if (bubble)
	{
		bubble->destscale = actor->scale;
		P_SetScale(bubble, actor->scale);
	}
}

// Function: A_BubbleRise
//
// Description: Raises a bubble
//
// var1:
//		0 = Bend around the water abit, looking more realistic
//		1 = Rise straight up
// var2 = rising speed
//
void A_BubbleRise(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_BUBBLERISE, actor))
		return;

	if (actor->type == MT_EXTRALARGEBUBBLE)
		P_SetObjectMomZ(actor, FixedDiv(6*FRACUNIT,5*FRACUNIT), false); // make bubbles rise!
	else
	{
		P_SetObjectMomZ(actor, locvar2, true); // make bubbles rise!

		// Move around slightly to make it look like it's bending around the water
		if (!locvar1)
		{
			uint8_t prandom = P_RandomByte(PR_BUBBLE);
			if (!(prandom & 0x7)) // *****000
			{
				P_InstaThrust(actor, prandom & 0x70 ? actor->angle + ANGLE_90 : actor->angle,
					FixedMul(prandom & 0xF0 ? FRACUNIT/2 : -FRACUNIT/2, actor->scale));
			}
			else if (!(prandom & 0x38)) // **000***
			{
				P_InstaThrust(actor, prandom & 0x70 ? actor->angle - ANGLE_90 : actor->angle - ANGLE_180,
					FixedMul(prandom & 0xF0 ? FRACUNIT/2 : -FRACUNIT/2, actor->scale));
			}
		}
	}
}

// Function: A_BubbleCheck
//
// Description: Checks if a bubble should be drawn or not. Bubbles are not drawn above water.
//
// var1 = unused
// var2 = unused
//
void A_BubbleCheck(void *thing)
{
	mobj_t *actor = thing;
	if (LUA_CallAction(A_BUBBLECHECK, actor))
		return;

	if (actor->eflags & MFE_UNDERWATER)
		actor->renderflags &= ~RF_DONTDRAW; // underwater so draw
	else
		actor->renderflags |= RF_DONTDRAW; // above water so don't draw
}

// Function: A_AttractChase
//
// Description: Makes a ring chase after a player with a ring shield and also causes spilled rings to flicker.
//
// var1 = unused
// var2 = unused
//
void A_AttractChase(void *thing)
{
	mobj_t *actor = thing;
	if (LUA_CallAction(A_ATTRACTCHASE, actor))
		return;

	if (actor->flags2 & MF2_NIGHTSPULL || !actor->health)
		return;

	if (actor->extravalue1) // SRB2Kart
	{
		if (!actor->target || P_MobjWasRemoved(actor->target) || !actor->target->player)
		{
			P_RemoveMobj(actor);
			return;
		}

		int32_t ringpower = K_GetKartBaseRingPower(actor->target->player, true);
		dboolean ringburnt = K_IsPlayerRingBurnt(actor->target->player, ringpower);

		if (ringburnt)
		{
			actor->colorized = true;
			actor->color = SKINCOLOR_ABYSS;
			actor->renderflags |= RF_ADD;
		}

		if (actor->extravalue2) // Using for ring boost
		{
			// Always fullbright
			actor->frame |= FF_FULLBRIGHT;

			if (actor->extravalue1 >= 21)
			{
				// Base add is 4 tics for 9,9, adds 1 tic for each point closer to the 1,1 end
				actor->target->player->ringboost += K_GetKartRingPower(actor->target->player, true);
				actor->target->player->ringtime += K_GetKartRingPower(actor->target->player, true);

				if (ringburnt)
				{
					// s3k55 s264
					S_StartSoundAtVolume(actor->target, sfx_s264, actor->target->player->ringvolume);
				}

				S_StartSoundAtVolume(actor->target, sfx_s1b5, actor->target->player->ringvolume);

				if (actor->target->player->rings <= 10)
				{
					S_StartSoundAtVolume(actor->target, sfx_ringlw, 255 - actor->target->player->rings*10);
				}

				if (actor->target->player->ringboost > (4*TICRATE + TICRATE/2))
					actor->target->player->ringboost = (4*TICRATE + TICRATE/2);

				actor->target->player->ringvolume -= RINGVOLUMEUSEPENALTY;

				P_KillMobj(actor, actor->target, actor->target, DMG_NORMAL);
				return;
			}
			else
			{
				fixed_t offz = FixedMul(80*actor->target->scale, FINESINE(FixedAngle((90 - (9 * abs(10 - actor->extravalue1))) << FRACBITS) >> ANGLETOFINESHIFT));
				//P_SetScale(actor, (actor->destscale = actor->target->scale));
				actor->z = actor->target->z;
				K_MatchGenericExtraFlags(actor, actor->target);
				P_MoveOrigin(actor, actor->target->x, actor->target->y,
						actor->z +
						( actor->target->height + offz )* P_MobjFlip(actor));
				actor->extravalue1++;
			}

		}
		else // Collecting
		{
			if (actor->extravalue1 >= 16)
			{
				if (!P_GivePlayerRings(actor->target->player, 1)) // returns 0 if addition failed
				{
					actor->target->player->ringboost += K_GetKartRingPower(actor->target->player, true);
					actor->target->player->ringtime += K_GetKartRingPower(actor->target->player, true);
				}

				if (actor->target->player->ringboost > (4*TICRATE + TICRATE/2))
					actor->target->player->ringboost = (4*TICRATE + TICRATE/2);

				if (actor->cvmem && P_IsLocalPlayer(actor->target->player)) // caching
					S_StartSound(NULL, sfx_s1c5);
				else
					S_StartSoundAtVolume(actor->target, sfx_s227, actor->target->player->ringvolume);

				actor->target->player->ringvolume -= RINGVOLUMECOLLECTPENALTY;
				actor->target->player->ringtransparency -= RINGTRANSPARENCYCOLLECTPENALTY;

				actor->target->player->pickuprings--;
				P_RemoveMobj(actor);
				return;
			}
			else
			{
				fixed_t dist = (4*actor->target->scale) * (16 - actor->extravalue1);

				P_SetScale(actor, (actor->destscale = mapobjectscale - ((mapobjectscale/14) * actor->extravalue1)));
				actor->z = actor->target->z;
				K_MatchGenericExtraFlags(actor, actor->target);
				P_MoveOrigin(actor,
					actor->target->x + FixedMul(dist, FINECOSINE(actor->angle >> ANGLETOFINESHIFT)),
					actor->target->y + FixedMul(dist, FINESINE(actor->angle >> ANGLETOFINESHIFT)),
					actor->z + actor->target->scale * 24 * P_MobjFlip(actor));

				actor->angle += ANG30;
				actor->extravalue1++;
			}
		}
	}
	else
	{
		// Don't immediately pick up spilled rings
		if (actor->threshold > 0)
			actor->threshold--;

		// Rings flicker before disappearing
		if (actor->fuse && actor->fuse < 2*TICRATE && (leveltime & 1))
			actor->renderflags |= RF_DONTDRAW;
		else
			actor->renderflags &= ~RF_DONTDRAW;

		// spilled rings get capped to a certain speed
		if (actor->info->reactiontime && actor->type == (mobjtype_t)actor->info->reactiontime)
		{
			const fixed_t maxspeed = 4 * actor->scale;
			fixed_t oldspeed = R_PointToDist2(0, 0, actor->momx, actor->momy);

			if (oldspeed > maxspeed)
			{
				fixed_t newspeed = max(maxspeed, oldspeed - actor->scale);
				actor->momx = FixedMul(FixedDiv(actor->momx, oldspeed), newspeed);
				actor->momy = FixedMul(FixedDiv(actor->momy, oldspeed), newspeed);
			}
		}

		if (actor->tracer != NULL && P_MobjWasRemoved(actor->tracer) == false)
		{
			if (actor->extravalue3 && spbplace != -1 && actor->tracer->player && (actor->tracer->player->position == spbplace))
				goto spbring;

			// Set attraction flag
			actor->cusval = 1;

			if (
				actor->tracer->player && actor->tracer->health
				&& ((gametypes[gametype]->rules & GTR_BUMPERS)
					|| (actor->tracer->player->itemtype == KITEM_THUNDERSHIELD
					&& RINGTOTAL(actor->tracer->player) < actor->tracer->player->ringmax
					&& !(actor->tracer->player->pflags & PF_RINGLOCK)))
				//&& P_CheckSight(actor, actor->tracer)
				)
			{
				fixed_t dist;
				angle_t hang, vang;

				// If a flung ring gets attracted by a shield, change it into a normal ring.
				if (actor->info->painchance && actor->type != (mobjtype_t)actor->info->painchance)
				{
#if 0 // old
					P_SpawnMobj(actor->x, actor->y, actor->z, actor->info->painchance);
					P_RemoveMobj(actor);
					return;
#else // new
					actor->type = actor->info->painchance;
					actor->info = &mobjinfo[actor->type];
					actor->flags = actor->info->flags;
#endif
				}

				// Keep stuff from going down inside floors and junk
				actor->flags &= ~MF_NOCLIPHEIGHT;

				// Let attracted rings move through walls and such.
				actor->flags |= MF_NOCLIP;

				// P_Attract is too "smart" for Kart; keep it simple, stupid!
				dist = P_AproxDistance(P_AproxDistance(actor->x - actor->tracer->x, actor->y - actor->tracer->y), actor->z - actor->tracer->z);
				hang = R_PointToAngle2(actor->x, actor->y, actor->tracer->x, actor->tracer->y);
				vang = R_PointToAngle2(actor->z, 0, actor->tracer->z, dist);

				actor->momx -= actor->momx>>4, actor->momy -= actor->momy>>4, actor->momz -= actor->momz>>4;
				actor->momx += FixedMul(FINESINE(vang>>ANGLETOFINESHIFT), FixedMul(FINECOSINE(hang>>ANGLETOFINESHIFT), 4*actor->scale));
				actor->momy += FixedMul(FINESINE(vang>>ANGLETOFINESHIFT), FixedMul(FINESINE(hang>>ANGLETOFINESHIFT), 4*actor->scale));
				actor->momz += FixedMul(FINECOSINE(vang>>ANGLETOFINESHIFT), 4*actor->scale);
			}
			else
			{
spbring:
				P_SetTarget(&actor->tracer, NULL);
			}
		}
		else
		{
			// Turn rings back into flung rings if lost
			if (actor->cusval && actor->info->reactiontime && actor->type != (mobjtype_t)actor->info->reactiontime)
			{
#if 0 // old
				mobj_t *newring;
				newring = P_SpawnMobj(actor->x, actor->y, actor->z, actor->info->reactiontime);
				P_InstaThrust(newring, P_RandomRange(PR_ITEM_RINGS,0,7) * ANGLE_45, 2<<FRACBITS);
				newring->momz = 8<<FRACBITS;
				newring->fuse = 120*TICRATE;
				P_RemoveMobj(actor);
				return;
#else // new
				actor->type = actor->info->reactiontime;
				actor->info = &mobjinfo[actor->type];
				actor->flags = actor->info->flags;

				P_InstaThrust(actor, P_RandomRange(PR_ITEM_RINGS,0,7) * ANGLE_45, 2 * actor->scale);
				P_SetObjectMomZ(actor, 8<<FRACBITS, false);
				actor->fuse = 120*TICRATE;
#endif
			}

			actor->cusval = 0; // Reset attraction flag
		}
	}
}

// Function: A_DropMine
//
// Description: Drops a mine. Raisestate specifies the object # to use for the mine.
//
// var1 = height offset
// var2:
//		lower 16 bits = proximity check distance (0 disables)
//		upper 16 bits = 0 to check proximity with target, 1 for tracer
//
void A_DropMine(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	fixed_t z;
	mobj_t *mine;

	if (LUA_CallAction(A_DROPMINE, actor))
		return;

	if (locvar2 & 65535)
	{
		fixed_t dist;
		mobj_t *target;

		if (locvar2 >> 16)
			target = actor->tracer;
		else
			target = actor->target;

		if (!target)
			return;

		dist = P_AproxDistance(actor->x-target->x, actor->y-target->y)>>FRACBITS;

		if (dist > FixedMul((locvar2 & 65535), actor->scale))
			return;
	}

	if (actor->eflags & MFE_VERTICALFLIP)
		z = actor->z + actor->height - mobjinfo[actor->info->raisestate].height - FixedMul((locvar1*FRACUNIT) - 12*FRACUNIT, actor->scale);
	else
		z = actor->z + FixedMul((locvar1*FRACUNIT) - 12*FRACUNIT, actor->scale);

	// Use raisestate instead of MT_MINE
	mine = P_SpawnMobj(actor->x, actor->y, z, (mobjtype_t)actor->info->raisestate);
	if (actor->eflags & MFE_VERTICALFLIP)
		mine->eflags |= MFE_VERTICALFLIP;
	mine->momz = actor->momz + actor->pmomz;

	S_StartSound(actor, actor->info->attacksound);
}

// Function: A_FishJump
//
// Description: Makes the stupid harmless fish in Greenflower Zone jump.
//
// var1 = Jump strength (in FRACBITS), if specified. Otherwise, uses the angle value.
// var2 = Trail object to spawn, if desired.
//
void A_FishJump(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_FISHJUMP, actor))
		return;

	if (locvar2)
	{
		uint8_t i;
		// Don't spawn trail unless a player is nearby.
		for (i = 0; i < MAXPLAYERS; ++i)
			if (playeringame[i] && players[i].mo
				&& P_AproxDistance(actor->x - players[i].mo->x, actor->y - players[i].mo->y) < (actor->info->speed))
				break; // Stop looking.
		if (i < MAXPLAYERS)
		{
			fixed_t rad = actor->radius>>FRACBITS;

			fixed_t rand_x;
			fixed_t rand_y;

			rand_y = P_RandomRange(PR_ACTION, rad, -rad)<<FRACBITS;
			rand_x = P_RandomRange(PR_ACTION, rad, -rad)<<FRACBITS;

			P_SpawnMobjFromMobj(actor, rand_x, rand_y, 0, (mobjtype_t)locvar2);
		}
	}

	if ((actor->z <= actor->floorz) || (actor->z <= actor->watertop - FixedMul((64 << FRACBITS), actor->scale)))
	{
		fixed_t jumpval;

		if (locvar1)
			jumpval = locvar1;
		else
		{
			if (actor->args[0])
				jumpval = actor->args[0] << (FRACBITS - 2);
			else
				jumpval = 44 << (FRACBITS - 2);
		}

		actor->momz = FixedMul(jumpval, actor->scale);
		P_SetMobjStateNF(actor, actor->info->seestate);
	}

	if (actor->momz < 0
		&& (actor->state < &states[actor->info->meleestate] || actor->state > &states[actor->info->xdeathstate]))
		P_SetMobjStateNF(actor, actor->info->meleestate);
}

// Function: A_SetSolidSteam
//
// Description: Makes steam solid so it collides with the player to boost them.
//
// var1 = unused
// var2 = unused
//
void A_SetSolidSteam(void *thing)
{
	mobj_t *actor = thing;
	if (LUA_CallAction(A_SETSOLIDSTEAM, actor))
		return;

	actor->flags &= ~MF_NOCLIP;
	actor->flags |= MF_SOLID;

	if (!(actor->flags2 & MF2_AMBUSH))
	{
		if (P_RandomChance(PR_DECORATION, FRACUNIT/8))
		{
			if (actor->info->deathsound)
				S_StartSound(actor, actor->info->deathsound); // Hiss!
		}
		else
		{
			if (actor->info->painsound)
				S_StartSound(actor, actor->info->painsound);
		}
	}

	P_SetObjectMomZ (actor, 1, true);
}

// Function: A_UnsetSolidSteam
//
// Description: Makes an object non-solid and also noclip. Used by the steam.
//
// var1 = unused
// var2 = unused
//
void A_UnsetSolidSteam(void *thing)
{
	mobj_t *actor = thing;
	if (LUA_CallAction(A_UNSETSOLIDSTEAM, actor))
		return;

	actor->flags &= ~MF_SOLID;
	actor->flags |= MF_NOCLIP;
}

// Function: A_SignPlayer
//
// Description: Changes the state of a level end sign to reflect the player that hit it.
//
// var1 = unused
// var2 = unused
//
void A_SignPlayer(void *thing)
{
	mobj_t *actor = thing;
	mobj_t *ov;
	if (LUA_CallAction(A_SIGNPLAYER, actor))
		return;
	if (!actor->target)
		return;

	if (!actor->target->player)
		return;

	// Set the sign to be an appropriate background color for this player's skincolor.
	actor->color = skincolors[actor->target->player->skincolor].invcolor;
	actor->frame += skincolors[actor->target->player->skincolor].invshade;

	// spawn an overlay of the player's face.
	ov = P_SpawnMobj(actor->x, actor->y, actor->z, MT_OVERLAY);
	P_SetTarget(&ov->target, actor);
	ov->color = actor->target->player->skincolor;
	ov->skin = skins[actor->target->player->skin];
	P_SetMobjState(ov, actor->info->seestate); // S_KART_SIGN
}

// Function: A_OverlayThink
//
// Description: Moves the overlay to the position of its target.
//
// var1 = unused
// var2 = invert, z offset
//
void A_OverlayThink(void *thing)
{
	mobj_t *actor = thing;
	fixed_t destx, desty;

	if (LUA_CallAction(A_OVERLAYTHINK, actor))
		return;

	if (!actor->target)
		return;

	if (!r_splitscreen && rendermode != render_soft)
	{
		angle_t viewingangle;

		if (players[displayplayers[0]].awayviewtics)
			viewingangle = R_PointToAngle2(actor->target->x, actor->target->y, players[displayplayers[0]].awayviewmobj->x, players[displayplayers[0]].awayviewmobj->y);
		else if (!camera[0].chase && players[displayplayers[0]].mo)
			viewingangle = R_PointToAngle2(actor->target->x, actor->target->y, players[displayplayers[0]].mo->x, players[displayplayers[0]].mo->y);
		else
			viewingangle = R_PointToAngle2(actor->target->x, actor->target->y, camera[0].x, camera[0].y);

		destx = actor->target->x + P_ReturnThrustX(actor->target, viewingangle, FixedMul(FRACUNIT, actor->scale));
		desty = actor->target->y + P_ReturnThrustY(actor->target, viewingangle, FixedMul(FRACUNIT, actor->scale));
	}
	else
	{
		destx = actor->target->x;
		desty = actor->target->y;
	}
	P_UnsetThingPosition(actor);
	actor->x = destx;
	actor->y = desty;
	P_SetThingPosition(actor);
	if (actor->eflags & MFE_VERTICALFLIP)
		actor->z = actor->target->z + actor->target->height - mobjinfo[actor->type].height  - ((var2>>16) ? -1 : 1)*(var2&0xFFFF)*FRACUNIT;
	else
		actor->z = actor->target->z + ((var2>>16) ? -1 : 1)*(var2&0xFFFF)*FRACUNIT;
	actor->angle = (actor->target->player ? actor->target->player->drawangle : actor->target->angle) + actor->movedir;
	actor->rollangle = actor->target->rollangle;
	actor->pitch = actor->target->pitch;
	actor->roll = actor->target->roll;
	actor->eflags = (actor->eflags & ~MFE_VERTICALFLIP) | (actor->target->eflags & MFE_VERTICALFLIP);

	actor->momx = actor->target->momx;
	actor->momy = actor->target->momy;
	actor->momz = actor->target->momz; // assume target has correct momz! Do not use P_SetObjectMomZ!
}

// Function: A_JetChase
//
// Description: A_Chase for Jettysyns
//
// var1 = unused
// var2 = unused
//
void A_JetChase(void *thing)
{
	mobj_t *actor = thing;
	fixed_t thefloor;

	if (LUA_CallAction(A_JETCHASE, actor))
		return;

	if (actor->flags2 & MF2_AMBUSH)
		return;

	if (actor->z >= actor->waterbottom && actor->watertop > actor->floorz
		&& actor->z > actor->watertop - FixedMul(256*FRACUNIT, actor->scale))
		thefloor = actor->watertop;
	else
		thefloor = actor->floorz;

	if (actor->reactiontime)
		actor->reactiontime--;

	if (P_RandomChance(PR_ACTION, FRACUNIT/32))
	{
		actor->momx = actor->momx / 2;
		actor->momy = actor->momy / 2;
		actor->momz = actor->momz / 2;
	}

	// Bounce if too close to floor or ceiling -
	// ideal for Jetty-Syns above you on 3d floors
	if (actor->momz && ((actor->z - FixedMul((32<<FRACBITS), actor->scale)) < thefloor) && !((thefloor + FixedMul(32*FRACUNIT, actor->scale) + actor->height) > actor->ceilingz))
		actor->momz = -actor->momz/2;

	if (!actor->target || !(actor->target->flags & MF_SHOOTABLE))
	{
		// look for a new target
		if (P_LookForPlayers(actor, true, false, 0))
			return; // got a new target

		actor->momx = actor->momy = actor->momz = 0;
		P_SetMobjState(actor, actor->info->spawnstate);
		return;
	}

	// modify target threshold
	if (actor->threshold)
	{
		if (!actor->target || actor->target->health <= 0)
			actor->threshold = 0;
		else
			actor->threshold--;
	}

	// turn towards movement direction if not there yet
	actor->angle = R_PointToAngle2(actor->x, actor->y, actor->target->x, actor->target->y);

	if ((multiplayer || netgame) && !actor->threshold && (actor->target->health <= 0 || !P_CheckSight(actor, actor->target)))
		if (P_LookForPlayers(actor, true, false, 0))
			return; // got a new target

	// If the player is over 3072 fracunits away, then look for another player
	if (P_AproxDistance(P_AproxDistance(actor->target->x - actor->x, actor->target->y - actor->y),
		actor->target->z - actor->z) > FixedMul(3072*FRACUNIT, actor->scale) && P_LookForPlayers(actor, true, false, FixedMul(3072*FRACUNIT, actor->scale)))
	{
		return; // got a new target
	}

	// chase towards player
	if (ultimatemode)
		P_Thrust(actor, actor->angle, FixedMul(actor->info->speed/2, actor->scale));
	else
		P_Thrust(actor, actor->angle, FixedMul(actor->info->speed/4, actor->scale));

	// must adjust height
	if (ultimatemode)
	{
		if (actor->z < (actor->target->z + actor->target->height + FixedMul((64<<FRACBITS), actor->scale)))
			actor->momz += FixedMul(FRACUNIT/2, actor->scale);
		else
			actor->momz -= FixedMul(FRACUNIT/2, actor->scale);
	}
	else
	{
		if (actor->z < (actor->target->z + actor->target->height + FixedMul((32<<FRACBITS), actor->scale)))
			actor->momz += FixedMul(FRACUNIT/2, actor->scale);
		else
			actor->momz -= FixedMul(FRACUNIT/2, actor->scale);
	}
}

// Function: A_JetbThink
//
// Description: Thinker for Jetty-Syn bombers
//
// var1 = unused
// var2 = unused
//
void A_JetbThink(void *thing)
{
	mobj_t *actor = thing;
	sector_t *nextsector;
	fixed_t thefloor;

	if (LUA_CallAction(A_JETBTHINK, actor))
		return;

	if (actor->z >= actor->waterbottom && actor->watertop > actor->floorz
		&& actor->z > actor->watertop - FixedMul(256*FRACUNIT, actor->scale))
		thefloor = actor->watertop;
	else
		thefloor = actor->floorz;

	if (actor->target)
	{
		A_JetChase(actor);
		// check for melee attack
		if (actor->info->raisestate
			&& (actor->z > (actor->floorz + FixedMul((32<<FRACBITS), actor->scale)))
			&& P_JetbCheckMeleeRange(actor) && !actor->reactiontime
			&& (actor->target->z >= actor->floorz))
		{
			mobj_t *bomb;
			if (actor->info->attacksound)
				S_StartSound(actor, actor->info->attacksound);

			// use raisestate instead of MT_MINE
			bomb = P_SpawnMobj(actor->x, actor->y, actor->z - FixedMul((32<<FRACBITS), actor->scale), (mobjtype_t)actor->info->raisestate);

			P_SetTarget(&bomb->target, actor);
			bomb->destscale = actor->scale;
			P_SetScale(bomb, actor->scale);
			actor->reactiontime = TICRATE; // one second
			S_StartSound(actor, actor->info->attacksound);
		}
	}
	else if (((actor->z - FixedMul((32<<FRACBITS), actor->scale)) < thefloor) && !((thefloor + FixedMul((32<<FRACBITS), actor->scale) + actor->height) > actor->ceilingz))
			actor->z = thefloor+FixedMul((32<<FRACBITS), actor->scale);

	if (!actor->target || !(actor->target->flags & MF_SHOOTABLE))
	{
		// look for a new target
		if (P_LookForPlayers(actor, true, false, 0))
			return; // got a new target

		P_SetMobjState(actor, actor->info->spawnstate);
		return;
	}

	nextsector = R_PointInSubsector(actor->x + actor->momx, actor->y + actor->momy)->sector;

	// Move downwards or upwards to go through a passageway.
	if (nextsector->ceilingheight < actor->z + actor->height)
		actor->momz -= FixedMul(5*FRACUNIT, actor->scale);
	else if (nextsector->floorheight > actor->z)
		actor->momz += FixedMul(5*FRACUNIT, actor->scale);
}

// Function: A_JetgShoot
//
// Description: Firing function for Jetty-Syn gunners.
//
// var1 = unused
// var2 = unused
//
void A_JetgShoot(void *thing)
{
	mobj_t *actor = thing;
	fixed_t dist;

	if (LUA_CallAction(A_JETGSHOOT, actor))
		return;

	if (!actor->target)
		return;

	if (actor->reactiontime)
		return;

	dist = P_AproxDistance(actor->target->x - actor->x, actor->target->y - actor->y);

	if (dist > FixedMul(actor->info->painchance*FRACUNIT, actor->scale))
		return;

	if (dist < FixedMul(64*FRACUNIT, actor->scale))
		return;

	A_FaceTarget(actor);
	P_SpawnMissile(actor, actor->target, (mobjtype_t)actor->info->raisestate);

	if (ultimatemode)
		actor->reactiontime = actor->info->reactiontime*TICRATE;
	else
		actor->reactiontime = actor->info->reactiontime*TICRATE*2;

	if (actor->info->attacksound)
		S_StartSound(actor, actor->info->attacksound);
}

// Function: A_ShootBullet
//
// Description: Shoots a bullet. Raisestate defines object # to use as projectile.
//
// var1 = unused
// var2 = unused
//
void A_ShootBullet(void *thing)
{
	mobj_t *actor = thing;
	fixed_t dist;

	if (LUA_CallAction(A_SHOOTBULLET, actor))
		return;

	if (!actor->target)
		return;

	dist = P_AproxDistance(P_AproxDistance(actor->target->x - actor->x, actor->target->y - actor->y), actor->target->z - actor->z);

	if (dist > FixedMul(actor->info->painchance*FRACUNIT, actor->scale))
		return;

	A_FaceTarget(actor);
	P_SpawnMissile(actor, actor->target, (mobjtype_t)actor->info->raisestate);

	if (actor->info->attacksound)
		S_StartSound(actor, actor->info->attacksound);
}

// Function: A_MinusDigging
//
// Description: Minus digging in the ground.
//
// var1 = unused
// var2 = unused
//
void A_MinusDigging(void *thing)
{
	mobj_t *actor = thing;
	if (LUA_CallAction(A_MINUSDIGGING, actor))
		return;

	actor->flags &= ~MF_SPECIAL;
	actor->flags &= ~MF_SHOOTABLE;

	if (!actor->target)
	{
		A_Look(actor);
		return;
	}

	if (actor->reactiontime)
	{
		actor->reactiontime--;
		return;
	}

	// Dirt trail
	P_SpawnGhostMobj(actor);

	actor->flags |= MF_NOCLIPTHING;
	var1 = 3;
	A_Chase(actor);
	actor->flags &= ~MF_NOCLIPTHING;

	// Play digging sound
	if (!(leveltime & 15))
		S_StartSound(actor, actor->info->activesound);

	// If we're close enough to our target, pop out of the ground
	if (P_AproxDistance(actor->target->x-actor->x, actor->target->y-actor->y) < actor->radius
		&& abs(actor->target->z - actor->z) < actor->height)
		P_SetMobjState(actor, actor->info->missilestate);

	// Snap to ground
	if (actor->eflags & MFE_VERTICALFLIP)
		actor->z = actor->ceilingz - actor->height;
	else
		actor->z = actor->floorz;
}

// Function: A_MinusPopup
//
// Description: Minus popping out of the ground.
//
// var1 = unused
// var2 = unused
//
void A_MinusPopup(void *thing)
{
	mobj_t *actor = thing;
	if (LUA_CallAction(A_MINUSPOPUP, actor))
		return;

	P_SetObjectMomZ(actor, 10*FRACUNIT, false);

	actor->flags |= MF_SPECIAL;
	actor->flags |= MF_SHOOTABLE;

	// Sound for busting out of the ground.
	S_StartSound(actor, actor->info->attacksound);
}

// Function: A_MinusCheck
//
// Description: If the minus hits the floor, dig back into the ground.
//
// var1 = unused
// var2 = unused
//
void A_MinusCheck(void *thing)
{
	mobj_t *actor = thing;
	if (LUA_CallAction(A_MINUSCHECK, actor))
		return;

	if ((!(actor->eflags & MFE_VERTICALFLIP) && actor->z <= actor->floorz)
	|| ((actor->eflags & MFE_VERTICALFLIP) && actor->z + actor->height >= actor->ceilingz))
	{
		actor->flags &= ~MF_SPECIAL;
		actor->flags &= ~MF_SHOOTABLE;
		actor->reactiontime = TICRATE;
		P_SetMobjState(actor, actor->info->seestate);
		return;
	}

	// 'Falling' animation
	if (P_MobjFlip(actor)*actor->momz < 0 && actor->state < &states[actor->info->meleestate])
		P_SetMobjState(actor, actor->info->meleestate);
}

// Function: A_ChickenCheck
//
// Description: Resets the chicken once it hits the floor again.
//
// var1 = unused
// var2 = unused
//
void A_ChickenCheck(void *thing)
{
	mobj_t *actor = thing;
	if (LUA_CallAction(A_CHICKENCHECK, actor))
		return;

	if ((!(actor->eflags & MFE_VERTICALFLIP) && actor->z <= actor->floorz)
	|| (actor->eflags & MFE_VERTICALFLIP && actor->z + actor->height >= actor->ceilingz))
	{
		if (!(actor->momx || actor->momy || actor->momz)
			&& actor->state > &states[actor->info->seestate])
		{
			A_Chase(actor);
			P_SetMobjState(actor, actor->info->seestate);
		}

		actor->momx >>= 2;
		actor->momy >>= 2;
	}
}

// Function: A_JetgThink
//
// Description: Thinker for Jetty-Syn Gunners
//
// var1 = unused
// var2 = unused
//
void A_JetgThink(void *thing)
{
	mobj_t *actor = thing;
	sector_t *nextsector;

	fixed_t thefloor;

	if (LUA_CallAction(A_JETGTHINK, actor))
		return;

	if (actor->z >= actor->waterbottom && actor->watertop > actor->floorz
		&& actor->z > actor->watertop - FixedMul(256*FRACUNIT, actor->scale))
		thefloor = actor->watertop;
	else
		thefloor = actor->floorz;

	if (actor->target)
	{
		if (P_RandomChance(PR_ACTION, FRACUNIT/8) && !actor->reactiontime)
			P_SetMobjState(actor, actor->info->missilestate);
		else
			A_JetChase (actor);
	}
	else if (actor->z - FixedMul((32<<FRACBITS), actor->scale) < thefloor && !(thefloor + FixedMul((32<<FRACBITS), actor->scale)
		+ actor->height > actor->ceilingz))
	{
		actor->z = thefloor + FixedMul((32<<FRACBITS), actor->scale);
	}

	if (!actor->target || !(actor->target->flags & MF_SHOOTABLE))
	{
		// look for a new target
		if (P_LookForPlayers(actor, true, false, 0))
			return; // got a new target

		P_SetMobjState(actor, actor->info->spawnstate);
		return;
	}

	nextsector = R_PointInSubsector(actor->x + actor->momx, actor->y + actor->momy)->sector;

	// Move downwards or upwards to go through a passageway.
	if (nextsector->ceilingheight < actor->z + actor->height)
		actor->momz -= FixedMul(5*FRACUNIT, actor->scale);
	else if (nextsector->floorheight > actor->z)
		actor->momz += FixedMul(5*FRACUNIT, actor->scale);
}

// Function: A_MouseThink
//
// Description: Thinker for scurrying mice.
//
// var1 = unused
// var2 = unused
//
void A_MouseThink(void *thing)
{
	mobj_t *actor = thing;
	if (LUA_CallAction(A_MOUSETHINK, actor))
		return;

	if (actor->reactiontime)
		actor->reactiontime--;

	if (((!(actor->eflags & MFE_VERTICALFLIP) && actor->z == actor->floorz)
		|| (actor->eflags & MFE_VERTICALFLIP && actor->z + actor->height == actor->ceilingz))
		&& !actor->reactiontime)
	{
		if (P_RandomChance(PR_ACTION, FRACUNIT/2))
			actor->angle += ANGLE_90;
		else
			actor->angle -= ANGLE_90;

		P_InstaThrust(actor, actor->angle, FixedMul(actor->info->speed, actor->scale));
		actor->reactiontime = TICRATE/5;
	}
}

// Function: A_DetonChase
//
// Description: Chases a Deton after a player.
//
// var1 = unused
// var2 = unused
//
void A_DetonChase(void *thing)
{
	mobj_t *actor = thing;
	angle_t exact;
	fixed_t xydist, dist;

	if (LUA_CallAction(A_DETONCHASE, actor))
		return;

	// modify tracer threshold
	if (!actor->tracer || actor->tracer->health <= 0)
		actor->threshold = 0;
	else
		actor->threshold = 1;

	if (!actor->tracer || !(actor->tracer->flags & MF_SHOOTABLE))
	{
		// look for a new target
		if (P_LookForPlayers(actor, true, true, 0))
			return; // got a new target

		actor->momx = actor->momy = actor->momz = 0;
		P_SetMobjState(actor, actor->info->spawnstate);
		return;
	}

	if (multiplayer && !actor->threshold && P_LookForPlayers(actor, true, true, 0))
		return; // got a new target

	// Face movement direction if not doing so
	exact = R_PointToAngle2(actor->x, actor->y, actor->tracer->x, actor->tracer->y);
	actor->angle = exact;
	/*if (exact != actor->angle)
	{
		if (exact - actor->angle > ANGLE_180)
		{
			actor->angle -= actor->info->raisestate;
			if (exact - actor->angle < ANGLE_180)
				actor->angle = exact;
		}
		else
		{
			actor->angle += actor->info->raisestate;
			if (exact - actor->angle > ANGLE_180)
				actor->angle = exact;
		}
	}*/
	// movedir is up/down angle: how much it has to go up as it goes over to the player
	xydist = P_AproxDistance(actor->tracer->x - actor->x, actor->tracer->y - actor->y);
	exact = R_PointToAngle2(0, 0, xydist, actor->tracer->z - actor->z);
	actor->movedir = exact;
	/*if (exact != actor->movedir)
	{
		if (exact - actor->movedir > ANGLE_180)
		{
			actor->movedir -= actor->info->raisestate;
			if (exact - actor->movedir < ANGLE_180)
				actor->movedir = exact;
		}
		else
		{
			actor->movedir += actor->info->raisestate;
			if (exact - actor->movedir > ANGLE_180)
				actor->movedir = exact;
		}
	}*/

	// check for melee attack
	if (actor->tracer)
	{
		if (P_AproxDistance(actor->tracer->x-actor->x, actor->tracer->y-actor->y) < actor->radius+actor->tracer->radius)
		{
			if (!((actor->tracer->z > actor->z + actor->height) || (actor->z > actor->tracer->z + actor->tracer->height)))
			{
				P_ExplodeMissile(actor);
				return;
			}
		}
	}

	// chase towards player
	if ((dist = P_AproxDistance(xydist, actor->tracer->z-actor->z))
		> FixedMul((actor->info->painchance << FRACBITS), actor->scale))
	{
		P_SetTarget(&actor->tracer, NULL); // Too far away
		return;
	}

	if (actor->reactiontime == 0)
	{
		actor->reactiontime = actor->info->reactiontime;
		return;
	}

	if (actor->reactiontime > 1)
	{
		actor->reactiontime--;
		return;
	}

	if (actor->reactiontime > 0)
	{
		actor->reactiontime = -42;

		if (actor->info->seesound)
			S_StartSound(actor, actor->info->seesound);
	}

	if (actor->reactiontime == -42)
	{
		fixed_t xyspeed, speed;

		if (actor->target->player)
			speed = K_GetKartSpeed(actor->tracer->player, false, false);
		else
			speed = actor->target->info->speed;

		actor->reactiontime = -42;

		exact = actor->movedir>>ANGLETOFINESHIFT;
		xyspeed = FixedMul(FixedMul(speed,3*FRACUNIT/4), FINECOSINE(exact));
		actor->momz = FixedMul(FixedMul(speed,3*FRACUNIT/4), FINESINE(exact));

		exact = actor->angle>>ANGLETOFINESHIFT;
		actor->momx = FixedMul(xyspeed, FINECOSINE(exact));
		actor->momy = FixedMul(xyspeed, FINESINE(exact));

		// Variable re-use
		xyspeed = (P_AproxDistance(actor->tracer->x - actor->x, P_AproxDistance(actor->tracer->y - actor->y, actor->tracer->z - actor->z))>>(FRACBITS+6));

		if (xyspeed < 1)
			xyspeed = 1;

		if (leveltime % xyspeed == 0)
			S_StartSound(actor, sfx_deton);
	}
}

// Function: A_CapeChase
//
// Description: Set an object's location to its target or tracer.
//
// var1:
//		0 = Use target
//		1 = Use tracer
//		upper 16 bits = Z offset
// var2:
//		upper 16 bits = forward/backward offset
//		lower 16 bits = sideways offset
//
void A_CapeChase(void *thing)
{
	mobj_t *actor = thing;
	mobj_t *chaser;
	fixed_t foffsetx, foffsety, boffsetx, boffsety;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	angle_t angle;

	if (LUA_CallAction(A_CAPECHASE, actor))
		return;

	CONS_Debug(DBG_GAMELOGIC, "A_CapeChase called from object type %d, var1: %d, var2: %d\n", actor->type, locvar1, locvar2);

	if (locvar1 & 65535)
		chaser = actor->tracer;
	else
		chaser = actor->target;

	if (!chaser || (chaser->health <= 0))
	{
		if (chaser)
			CONS_Debug(DBG_GAMELOGIC, "Hmm, the guy I'm chasing (object type %d) has no health.. so I'll die too!\n", chaser->type);

		P_RemoveMobj(actor);
		return;
	}

	angle = (chaser->player ? chaser->player->drawangle : chaser->angle);

	foffsetx = P_ReturnThrustX(chaser, angle, FixedMul((locvar2 >> 16)*FRACUNIT, actor->scale));
	foffsety = P_ReturnThrustY(chaser, angle, FixedMul((locvar2 >> 16)*FRACUNIT, actor->scale));

	boffsetx = P_ReturnThrustX(chaser, angle-ANGLE_90, FixedMul((locvar2 & 65535)*FRACUNIT, actor->scale));
	boffsety = P_ReturnThrustY(chaser, angle-ANGLE_90, FixedMul((locvar2 & 65535)*FRACUNIT, actor->scale));

	P_UnsetThingPosition(actor);
	actor->x = chaser->x + foffsetx + boffsetx;
	actor->y = chaser->y + foffsety + boffsety;
	if (chaser->eflags & MFE_VERTICALFLIP)
	{
		actor->eflags |= MFE_VERTICALFLIP;
		actor->flags2 |= MF2_OBJECTFLIP;
		actor->z = chaser->z + chaser->height - actor->height - FixedMul((locvar1 >> 16)*FRACUNIT, actor->scale);
	}
	else
	{
		actor->eflags &= ~MFE_VERTICALFLIP;
		actor->flags2 &= ~MF2_OBJECTFLIP;
		actor->z = chaser->z + FixedMul((locvar1 >> 16)*FRACUNIT, actor->scale);
	}
	actor->angle = angle;
	P_SetThingPosition(actor);
}

// Function: A_RotateSpikeBall
//
// Description: Rotates a spike ball around its target/tracer.
//
// var1:
//		0 = Use target
//		1 = Use tracer
// var2 = unused
//
void A_RotateSpikeBall(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	const fixed_t radius = FixedMul(12*actor->info->speed, actor->scale);

	if (LUA_CallAction(A_ROTATESPIKEBALL, actor))
		return;

	if (!((!locvar1 && (actor->target)) || (locvar1 && (actor->tracer))))// This should NEVER happen.
	{
		CONS_Debug(DBG_GAMELOGIC, "A_RotateSpikeBall: Spikeball has no target\n");
		P_RemoveMobj(actor);
		return;
	}

	if (!actor->info->speed)
	{
		CONS_Debug(DBG_GAMELOGIC, "A_RotateSpikeBall: Object has no speed.\n");
		return;
	}

	actor->angle += FixedAngle(actor->info->speed);
	P_UnsetThingPosition(actor);
	{
		const angle_t fa = actor->angle>>ANGLETOFINESHIFT;
		if (!locvar1)
		{
			actor->x = actor->target->x + FixedMul(FINECOSINE(fa),radius);
			actor->y = actor->target->y + FixedMul(FINESINE(fa),radius);
			actor->z = actor->target->z + actor->target->height/2;
		}
		else
		{
			actor->x = actor->tracer->x + FixedMul(FINECOSINE(fa),radius);
			actor->y = actor->tracer->y + FixedMul(FINESINE(fa),radius);
			actor->z = actor->tracer->z + actor->tracer->height/2;
		}
		P_SetThingPosition(actor);
	}
}

// Function: A_UnidusBall
//
// Description: Rotates a spike ball around its target.
//
// var1:
//		0 = Don't throw
//		1 = Throw
//		2 = Throw when target leaves MF2_SKULLFLY.
// var2 = unused
//
void A_UnidusBall(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	dboolean canthrow = false;

	if (LUA_CallAction(A_UNIDUSBALL, actor))
		return;

	actor->angle += ANGLE_11hh;

	if (actor->movecount)
	{
		if (P_AproxDistance(actor->momx, actor->momy) < FixedMul(actor->info->damage/2, actor->scale))
			P_ExplodeMissile(actor);
		return;
	}

	if (!actor->target || !actor->target->health)
	{
		CONS_Debug(DBG_GAMELOGIC, "A_UnidusBall: Removing unthrown spikeball from nonexistant Unidus\n");
		P_RemoveMobj(actor);
		return;
	}

	P_UnsetThingPosition(actor);
	{
		const angle_t angle = actor->movedir + FixedAngle(actor->info->speed*(leveltime%360));
		const uint16_t fa = angle>>ANGLETOFINESHIFT;

		actor->x = actor->target->x + FixedMul(FINECOSINE(fa),actor->threshold);
		actor->y = actor->target->y + FixedMul(  FINESINE(fa),actor->threshold);
		actor->z = actor->target->z + actor->target->height/2 - actor->height/2;

		if (locvar1 == 1 && actor->target->target)
		{
			const angle_t tang = R_PointToAngle2(actor->target->x, actor->target->y, actor->target->target->x, actor->target->target->y);
			const angle_t mina = tang-ANGLE_11hh;
			canthrow = (angle-mina < FixedAngle(actor->info->speed*3));
		}
	}
	P_SetThingPosition(actor);

	if (locvar1 == 1 && canthrow)
	{
		if (P_AproxDistance(actor->target->target->x - actor->target->x, actor->target->target->y - actor->target->y) > FixedMul(MISSILERANGE>>1, actor->scale)
		|| !P_CheckSight(actor, actor->target->target))
			return;

		actor->movecount = actor->info->damage>>FRACBITS;
		actor->flags &= ~(MF_NOCLIP|MF_NOCLIPHEIGHT|MF_NOCLIPTHING);
		P_InstaThrust(actor, R_PointToAngle2(actor->x, actor->y, actor->target->target->x, actor->target->target->y), FixedMul(actor->info->damage, actor->scale));
	}
	else if (locvar1 == 2)
	{
		dboolean skull = (actor->target->flags2 & MF2_SKULLFLY) == MF2_SKULLFLY;
		if (actor->target->state == &states[actor->target->info->painstate])
		{
			P_KillMobj(actor, NULL, NULL, DMG_NORMAL);
			return;
		}
		switch(actor->extravalue2)
		{
		case 0: // at least one frame where not dashing
			if (!skull) ++actor->extravalue2;
			else break;
			/* FALLTHRU */
		case 1: // at least one frame where ARE dashing
			if (skull) ++actor->extravalue2;
			else break;
			/* FALLTHRU */
		case 2: // not dashing again?
			if (skull) break;
			// launch.
		{
			mobj_t *target = actor->target;
			if (actor->target->target)
				target = actor->target->target;
			actor->movecount = actor->info->damage>>FRACBITS;
			actor->flags &= ~(MF_NOCLIP|MF_NOCLIPHEIGHT|MF_NOCLIPTHING);
			P_InstaThrust(actor, R_PointToAngle2(actor->x, actor->y, target->x, target->y), FixedMul(actor->info->damage, actor->scale));
		}
		default: // from our compiler appeasement program (CAP).
			break;
		}
	}
}

// Function: A_RockSpawn
//
// Spawns rocks at a specified interval
//
// var1 = unused
// var2 = unused
void A_RockSpawn(void *thing)
{
	mobj_t *actor = thing;
	mobj_t *mo;
	mobjtype_t type;
	fixed_t dist;

	if (LUA_CallAction(A_ROCKSPAWN, actor))
		return;

	if (mapnamespace == MNS_SRB2KART && !actor->args[1])
		return;

	type = actor->stringargs[0] ? get_number(actor->stringargs[0]) : MT_ROCKCRUMBLE1;

	if (type < MT_NULL || type >= NUMMOBJTYPES)
	{
		CONS_Debug(DBG_GAMELOGIC, "A_RockSpawn: Invalid mobj type %s!\n", udmf ? actor->stringargs[0] : va("%d",actor->args[3]));
		return;
	}

	dist = max(actor->args[0] << (FRACBITS - 4), 1);

	if (actor->args[2])
		dist += P_RandomByte(PR_ROCK_SPAWN) * (FRACUNIT/32); // random oomph

	mo = P_SpawnMobj(actor->x, actor->y, actor->z, MT_FALLINGROCK);
	P_SetMobjState(mo, mobjinfo[type].spawnstate);
	mo->angle = actor->angle;

	P_InstaThrust(mo, mo->angle, dist);
	mo->momz = dist;

	var1 = actor->args[1];
	A_SetTics(actor);
}

//
// Function: A_SlingAppear
//
// Appears a sling.
//
// var1 = unused
// var2 = unused
//
void A_SlingAppear(void *thing)
{
	mobj_t *actor = thing;
	uint8_t mlength = 4;
	mobj_t *spawnee, *hprev;

	if (LUA_CallAction(A_SLINGAPPEAR, actor))
		return;

	P_UnsetThingPosition(actor);
	actor->flags &= ~(MF_NOBLOCKMAP|MF_NOCLIP|MF_NOGRAVITY|MF_NOCLIPHEIGHT);
	P_SetThingPosition(actor);
	actor->lastlook = 128;
	actor->movecount = actor->lastlook;
	actor->threshold = 0;
	actor->movefactor = actor->threshold;
	actor->friction = 128;

	hprev = P_SpawnMobj(actor->x, actor->y, actor->z, MT_SMALLGRABCHAIN);
	P_SetTarget(&hprev->tracer, actor);
	P_SetTarget(&hprev->hprev, actor);
	P_SetTarget(&actor->hnext, hprev);
	hprev->flags |= MF_NOCLIP|MF_NOCLIPHEIGHT;
	hprev->movecount = mlength;

	mlength--;

	while (mlength > 0)
	{
		spawnee = P_SpawnMobj(actor->x, actor->y, actor->z, MT_SMALLMACECHAIN);
		P_SetTarget(&spawnee->tracer, actor);
		P_SetTarget(&spawnee->hprev, hprev);
		P_SetTarget(&hprev->hnext, spawnee);
		hprev = spawnee;

		spawnee->flags |= MF_NOCLIP|MF_NOCLIPHEIGHT;
		spawnee->movecount = mlength;

		mlength--;
	}
}

// Function: A_SetFuse
//
// Description: Sets the actor's fuse timer if not set already. May also change state when fuse reaches the last tic, otherwise by default the actor will die or disappear. (Replaces A_SnowBall)
//
// var1 = fuse timer duration (in tics).
// var2:
//		lower 16 bits = if > 0, state to change to when fuse = 1
//		upper 16 bits: 0 = (default) don't set fuse unless 0, 1 = force change, 2 = force no change
//
void A_SetFuse(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_SETFUSE, actor))
		return;

	if ((!actor->fuse || (locvar2 >> 16)) && (locvar2 >> 16) != 2) // set the actor's fuse value
		actor->fuse = locvar1;

	if (actor->fuse == 1 && (locvar2 & 65535)) // change state on the very last tic (fuse is handled before actions in P_MobjThinker)
	{
		actor->fuse = 0; // don't die/disappear the next tic!
		P_SetMobjState(actor, locvar2 & 65535);
	}
}

// Function: A_CrawlaCommanderThink
//
// Description: Thinker for Crawla Commander.
//
// var1 = shoot bullets?
// var2 = "pogo mode" speed
//
void A_CrawlaCommanderThink(void *thing)
{
	mobj_t *actor = thing;
	fixed_t dist;
	sector_t *nextsector;
	fixed_t thefloor;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	dboolean hovermode = (actor->health > 1 || actor->fuse);

	if (LUA_CallAction(A_CRAWLACOMMANDERTHINK, actor))
		return;

	if (actor->z >= actor->waterbottom && actor->watertop > actor->floorz
		&& actor->z > actor->watertop - FixedMul(256*FRACUNIT, actor->scale))
		thefloor = actor->watertop;
	else
		thefloor = actor->floorz;

	if (!actor->fuse && actor->flags2 & MF2_FRET)
	{
		if (actor->info->painsound)
			S_StartSound(actor, actor->info->painsound);

		actor->fuse = TICRATE/2;
		actor->momz = 0;

		P_InstaThrust(actor, actor->angle-ANGLE_180, FixedMul(5*FRACUNIT, actor->scale));
	}

	if (actor->reactiontime > 0)
		actor->reactiontime--;

	if (actor->fuse < 2)
	{
		actor->fuse = 0;
		actor->flags2 &= ~MF2_FRET;
	}

	// Hover mode
	if (hovermode)
	{
		if (actor->z < thefloor + FixedMul(16*FRACUNIT, actor->scale))
			actor->momz += FixedMul(FRACUNIT, actor->scale);
		else if (actor->z < thefloor + FixedMul(32*FRACUNIT, actor->scale))
			actor->momz += FixedMul(FRACUNIT/2, actor->scale);
		else
			actor->momz += FixedMul(16, actor->scale);
	}

	if (!actor->target)
	{
		// look for a new target
		if (P_LookForPlayers(actor, true, false, 0))
			return; // got a new target

		if (actor->state != &states[actor->info->spawnstate])
			P_SetMobjState(actor, actor->info->spawnstate);
		return;
	}

	dist = P_AproxDistance(actor->x - actor->target->x, actor->y - actor->target->y);

	if (locvar1)
	{
		if (actor->health < 2 && P_RandomChance(PR_ACTION, FRACUNIT/128))
			P_SpawnMissile(actor, actor->target, locvar1);
	}

	// Face the player
	actor->angle = R_PointToAngle2(actor->x, actor->y, actor->target->x, actor->target->y);

	if (actor->threshold && dist > FixedMul(256*FRACUNIT, actor->scale))
		actor->momx = actor->momy = 0;

	if (actor->reactiontime && actor->reactiontime <= 2*TICRATE && dist > actor->target->radius - FixedMul(FRACUNIT, actor->scale))
	{
		actor->threshold = 0;

		// Roam around, somewhat in the player's direction.
		actor->angle += (P_RandomByte(PR_ACTION)<<10);
		actor->angle -= (P_RandomByte(PR_ACTION)<<10);

		if (hovermode)
		{
			fixed_t mom;
			P_Thrust(actor, actor->angle, 2*actor->scale);
			mom = P_AproxDistance(actor->momx, actor->momy);
			if (mom > 20*actor->scale)
			{
				mom += 20*actor->scale;
				mom >>= 1;
				P_InstaThrust(actor, R_PointToAngle2(0, 0, actor->momx, actor->momy), mom);
			}
		}
	}
	else if (!actor->reactiontime)
	{
		if (hovermode && !(actor->flags2 & MF2_FRET)) // Hover Mode
		{
			if (dist < FixedMul(512*FRACUNIT, actor->scale))
			{
				actor->angle = R_PointToAngle2(actor->x, actor->y, actor->target->x, actor->target->y);
				P_InstaThrust(actor, actor->angle, FixedMul(40*FRACUNIT, actor->scale));
				actor->threshold = 1;
				if (actor->info->attacksound)
					S_StartSound(actor, actor->info->attacksound);
			}
		}
		actor->reactiontime = 3*TICRATE + (P_RandomByte(PR_ACTION)>>2);
	}

	if (actor->health == 1)
		P_Thrust(actor, actor->angle, 1);

	// Pogo Mode
	if (!hovermode && actor->z <= actor->floorz)
	{
		if (actor->info->activesound)
			S_StartSound(actor, actor->info->activesound);

		if (dist < FixedMul(256*FRACUNIT, actor->scale))
		{
			actor->momz = FixedMul(locvar2, actor->scale);
			actor->angle = R_PointToAngle2(actor->x, actor->y, actor->target->x, actor->target->y);
			P_InstaThrust(actor, actor->angle, FixedMul(locvar2/8, actor->scale));
			// pogo on player
		}
		else
		{
			uint8_t prandom = P_RandomByte(PR_MOVINGTARGET);
			actor->angle = R_PointToAngle2(actor->x, actor->y, actor->target->x, actor->target->y) + (P_RandomChance(PR_MOVINGTARGET, FRACUNIT/2) ? -prandom : +prandom);
			P_InstaThrust(actor, actor->angle, FixedDiv(FixedMul(locvar2, actor->scale), 3*FRACUNIT/2));
			actor->momz = FixedMul(locvar2, actor->scale); // Bounce up in air
		}
	}

	nextsector = R_PointInSubsector(actor->x + actor->momx, actor->y + actor->momy)->sector;

	// Move downwards or upwards to go through a passageway.
	if (nextsector->floorheight > actor->z && nextsector->floorheight - actor->z < FixedMul(128*FRACUNIT, actor->scale))
		actor->momz += (nextsector->floorheight - actor->z) / 4;
}

// Function: A_RingExplode
//
// Description: An explosion ring exploding
//
// var1 = unused
// var2 = unused
//
void A_RingExplode(void *thing)
{
	mobj_t *actor = thing;
	mobj_t *mo2;
	thinker_t *th;
	angle_t d;

	if (LUA_CallAction(A_RINGEXPLODE, actor))
		return;

	for (d = 0; d < 16; d++)
		P_SpawnParaloop(actor->x, actor->y, actor->z + actor->height, FixedMul(actor->info->painchance, actor->scale), 16, MT_NIGHTSPARKLE, S_NULL, d*(ANGLE_22h), true);

	S_StartSound(actor, sfx_prloop);

	for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
	{
		if (th->function == (actionf_p1)P_RemoveThinkerDelayed)
			continue;

		mo2 = (mobj_t *)th;

		if (mo2 == actor) // Don't explode yourself! Endless loop!
			continue;

		if (P_AproxDistance(P_AproxDistance(mo2->x - actor->x, mo2->y - actor->y), mo2->z - actor->z) > FixedMul(actor->info->painchance, actor->scale))
			continue;

		if (mo2->flags & MF_SHOOTABLE)
		{
			actor->flags2 |= MF2_DEBRIS;
			P_DamageMobj(mo2, actor, actor->target, 1, DMG_NORMAL);
			continue;
		}
	}
	return;
}

// Function: A_OldRingExplode
//
// Description: An explosion ring exploding, 1.09.4 style
//
// var1 = object # to explode as debris
// var2 = unused
//
void A_OldRingExplode(void *thing)
{
	mobj_t *actor = thing;
	uint8_t i;
	mobj_t *mo;
	const fixed_t ns = FixedMul(20 * FRACUNIT, actor->scale);
	int32_t locvar1 = var1;
	dboolean changecolor = (actor->target && actor->target->player);

	if (LUA_CallAction(A_OLDRINGEXPLODE, actor))
		return;

	for (i = 0; i < 32; i++)
	{
		const angle_t fa = (i*FINEANGLES/16) & FINEMASK;

		mo = P_SpawnMobj(actor->x, actor->y, actor->z, locvar1);
		P_SetTarget(&mo->target, actor->target); // Transfer target so player gets the points

		mo->momx = FixedMul(FINECOSINE(fa),ns);
		mo->momy = FixedMul(FINESINE(fa),ns);

		if (i > 15)
		{
			if (i & 1)
				mo->momz = ns;
			else
				mo->momz = -ns;
		}

		mo->flags2 |= MF2_DEBRIS;
		mo->fuse = TICRATE/5;

		if (changecolor)
		{
			if (!(gametypes[gametype]->rules & GTR_TEAMS))
				mo->color = actor->target->color; //copy color
			else if (actor->target->player->ctfteam == 2)
				mo->color = skincolor_bluering;
		}
	}

	mo = P_SpawnMobj(actor->x, actor->y, actor->z, locvar1);

	P_SetTarget(&mo->target, actor->target);
	mo->momz = ns;
	mo->flags2 |= MF2_DEBRIS;
	mo->fuse = TICRATE/5;

	if (changecolor)
	{
		if (!(gametypes[gametype]->rules & GTR_TEAMS))
			mo->color = actor->target->color; //copy color
		else if (actor->target->player->ctfteam == 2)
			mo->color = skincolor_bluering;
	}

	mo = P_SpawnMobj(actor->x, actor->y, actor->z, locvar1);

	P_SetTarget(&mo->target, actor->target);
	mo->momz = -ns;
	mo->flags2 |= MF2_DEBRIS;
	mo->fuse = TICRATE/5;

	if (changecolor)
	{
		if (!(gametypes[gametype]->rules & GTR_TEAMS))
			mo->color = actor->target->color; //copy color
		else if (actor->target->player->ctfteam == 2)
			mo->color = skincolor_bluering;
	}
}

// Function: A_MixUp
//
// Description: Mix up all of the player positions.
//
// var1 = unused
// var2 = unused
//
void A_MixUp(void *thing)
{
	mobj_t *actor = thing;
	dboolean teleported[MAXPLAYERS];
	int32_t i, numplayers = 0, prandom = 0;

	if (LUA_CallAction(A_MIXUP, actor))
		return;

	if (!multiplayer)
		return;

	// The random factor is okay for other game modes, but in these, it is cripplingly unfair.
	if (gametypes[gametype]->rules & GTR_CIRCUIT)
	{
		S_StartSound(actor, sfx_lose);
		return;
	}

	numplayers = 0;
	memset(teleported, 0, sizeof (teleported));

	// Count the number of players in the game
	// and grab their xyz coords
	for (i = 0; i < MAXPLAYERS; i++)
		if (playeringame[i] && players[i].mo && players[i].mo->health > 0 && players[i].playerstate == PST_LIVE && !players[i].exiting)
		{
			if ((netgame || multiplayer) && players[i].spectator) // Ignore spectators
				continue;

			numplayers++;
		}

	if (numplayers <= 1) // Not enough players to mix up.
	{
		S_StartSound(actor, sfx_lose);
		return;
	}
	else if (numplayers == 2) // Special case -- simple swap
	{
		fixed_t x, y, z;
		angle_t angle, drawangle;
		int32_t one = -1, two = 0; // default value 0 to make the compiler shut up

		// Zoom tube stuff
		mobj_t *tempthing = NULL; //tracer
		uint16_t carry1,carry2;     //carry
		int32_t transspeed;         //player speed

		// Starpost stuff
		fixed_t starpostx, starposty, starpostz;
		int32_t starpostnum;

		int32_t mflags2;

		for (i = 0; i < MAXPLAYERS; i++)
			if (playeringame[i] && players[i].mo && players[i].mo->health > 0 && players[i].playerstate == PST_LIVE
				&& !players[i].exiting)
			{
				if ((netgame || multiplayer) && players[i].spectator) // Ignore spectators
					continue;

				if (one == -1)
					one = i;
				else
				{
					two = i;
					break;
				}
			}

		//get this done first!
		tempthing = players[one].mo->tracer;
		P_SetTarget(&players[one].mo->tracer, players[two].mo->tracer);
		P_SetTarget(&players[two].mo->tracer, tempthing);

		//zoom tubes use player->speed to determine direction and speed
		transspeed = players[one].speed;
		players[one].speed = players[two].speed;
		players[two].speed = transspeed;

		//set flags variables now but DON'T set them.
		carry1 = players[one].carry;
		carry2 = players[two].carry;

		x = players[one].mo->x;
		y = players[one].mo->y;
		z = players[one].mo->z;
		angle = players[one].mo->angle;
		drawangle = players[one].drawangle;

		starpostx = players[one].starpostx;
		starposty = players[one].starposty;
		starpostz = players[one].starpostz;
		starpostnum = players[one].starpostnum;

		mflags2 = players[one].mo->flags2;

		P_MixUp(players[one].mo, players[two].mo->x, players[two].mo->y, players[two].mo->z, players[two].mo->angle,
				players[two].starpostx, players[two].starposty, players[two].starpostz,
				players[two].starpostnum, 0, 0,
				FRACUNIT, players[two].drawangle, players[two].mo->flags2);

		P_MixUp(players[two].mo, x, y, z, angle, starpostx, starposty, starpostz,
				starpostnum, 0, 0,
				FRACUNIT, drawangle, mflags2);

		//carry set after mixup.  Stupid P_ResetPlayer() takes away some of the stuff we look for...
		//but not all of it!  So we need to make sure they aren't set wrong or anything.
		players[one].carry = carry2;
		players[two].carry = carry1;

		teleported[one] = true;
		teleported[two] = true;
	}
	else
	{
		fixed_t position[MAXPLAYERS][3];
		angle_t anglepos[MAXPLAYERS][2];
		int32_t pindex[MAXPLAYERS], counter = 0, teleportfrom = 0;

		// Zoom tube stuff
		mobj_t *transtracer[MAXPLAYERS];  //tracer
		//pflags_t transflag[MAXPLAYERS]; //cyan pink white pink cyan
		uint16_t transcarry[MAXPLAYERS];    //player carry
		int32_t transspeed[MAXPLAYERS];     //player speed

		// Star post stuff
		fixed_t spposition[MAXPLAYERS][3];
		int32_t starpostnum[MAXPLAYERS];

		int32_t flags2[MAXPLAYERS];

		for (i = 0; i < MAXPLAYERS; i++)
		{
			position[i][0] = position[i][1] = position[i][2] = anglepos[i][0] = anglepos[i][1] = pindex[i] = -1;
			teleported[i] = false;
		}

		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (playeringame[i] && players[i].playerstate == PST_LIVE
				&& players[i].mo && players[i].mo->health > 0 && !players[i].exiting)
			{
				if ((netgame || multiplayer) && players[i].spectator)// Ignore spectators
					continue;

				position[counter][0] = players[i].mo->x;
				position[counter][1] = players[i].mo->y;
				position[counter][2] = players[i].mo->z;
				pindex[counter] = i;
				anglepos[counter][0] = players[i].mo->angle;
				anglepos[counter][1] = players[i].drawangle;
				players[i].mo->momx = players[i].mo->momy = players[i].mo->momz =
					players[i].rmomx = players[i].rmomy = 1;
				players[i].cmomx = players[i].cmomy = 0;

				transcarry[counter] = players[i].carry;
				transspeed[counter] = players[i].speed;
				transtracer[counter] = players[i].mo->tracer;

				spposition[counter][0] = players[i].starpostx;
				spposition[counter][1] = players[i].starposty;
				spposition[counter][2] = players[i].starpostz;
				starpostnum[counter] = players[i].starpostnum;

				flags2[counter] = players[i].mo->flags2;

				counter++;
			}
		}

		counter = 0;

		// Mix them up!
		for (;;)
		{
			if (counter > 255) // fail-safe to avoid endless loop
				break;
			prandom = P_RandomByte(PR_ACTION);
			prandom %= numplayers; // I love modular arithmetic, don't you?
			if (prandom) // Make sure it's not a useless mix
				break;
			counter++;
		}

		counter = 0;

		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (playeringame[i] && players[i].playerstate == PST_LIVE
				&& players[i].mo && players[i].mo->health > 0 && !players[i].exiting)
			{
				if ((netgame || multiplayer) && players[i].spectator)// Ignore spectators
					continue;

				teleportfrom = (counter + prandom) % numplayers;

				//speed and tracer come before...
				players[i].speed = transspeed[teleportfrom];
				P_SetTarget(&players[i].mo->tracer, transtracer[teleportfrom]);

				P_MixUp(players[i].mo, position[teleportfrom][0], position[teleportfrom][1], position[teleportfrom][2], anglepos[teleportfrom][0],
					spposition[teleportfrom][0], spposition[teleportfrom][1], spposition[teleportfrom][2],
					starpostnum[teleportfrom], 0, 0,
					FRACUNIT, anglepos[teleportfrom][1], flags2[teleportfrom]);

				//...carry after.  same reasoning.
				players[i].carry = transcarry[teleportfrom];

				teleported[i] = true;
				counter++;
			}
		}
	}

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (teleported[i])
		{
			if (playeringame[i] && players[i].playerstate == PST_LIVE
				&& players[i].mo && players[i].mo->health > 0 && !players[i].exiting)
			{
				if ((netgame || multiplayer) && players[i].spectator)// Ignore spectators
					continue;

				P_SetThingPosition(players[i].mo);

				players[i].mo->floorz = P_GetFloorZ(players[i].mo, players[i].mo->subsector->sector, players[i].mo->x, players[i].mo->y, NULL);
				players[i].mo->ceilingz = P_GetCeilingZ(players[i].mo, players[i].mo->subsector->sector, players[i].mo->x, players[i].mo->y, NULL);

				P_CheckPosition(players[i].mo, players[i].mo->x, players[i].mo->y, NULL);
			}
		}
	}

	// Play the 'bowrwoosh!' sound
	S_StartSound(NULL, sfx_mixup);
}

// Function: A_Boss1Chase
//
// Description: Like A_Chase, but for Boss 1.
//
// var1 = unused
// var2 = unused
//
void A_Boss1Chase(void *thing)
{
	mobj_t *actor = thing;
	int32_t delta;

	if (LUA_CallAction(A_BOSS1CHASE, actor))
		return;

	if (!actor->target || !(actor->target->flags & MF_SHOOTABLE))
	{
		// look for a new target
		if (P_LookForPlayers(actor, true, false, 0))
			return; // got a new target

		P_SetMobjStateNF(actor, actor->info->spawnstate);
		return;
	}

	if (actor->reactiontime)
		actor->reactiontime--;

	// turn towards movement direction if not there yet
	if (actor->movedir < NUMDIRS)
	{
		actor->angle &= (7<<29);
		delta = actor->angle - (actor->movedir << 29);

		if (delta > 0)
			actor->angle -= ANGLE_45;
		else if (delta < 0)
			actor->angle += ANGLE_45;
	}

	// do not attack twice in a row
	if (actor->flags2 & MF2_JUSTATTACKED)
	{
		actor->flags2 &= ~MF2_JUSTATTACKED;
		P_NewChaseDir(actor);
		return;
	}

	if (actor->movecount)
		goto nomissile;

	if (!P_CheckMissileRange(actor))
		goto nomissile;

	if (actor->reactiontime <= 0)
	{
		if (actor->health > actor->info->damage)
		{
			if (P_RandomChance(PR_ACTION, FRACUNIT/2))
				P_SetMobjState(actor, actor->info->missilestate);
			else
				P_SetMobjState(actor, actor->info->meleestate);
		}
		else
		{
			P_LinedefExecute(actor->args[4], actor, NULL);
			P_SetMobjState(actor, actor->info->raisestate);
		}

		actor->flags2 |= MF2_JUSTATTACKED;
		actor->reactiontime = actor->info->reactiontime;
		return;
	}

	// ?
nomissile:
	// possibly choose another target
	if (multiplayer && P_RandomChance(PR_ACTION, FRACUNIT/128))
	{
		if (P_LookForPlayers(actor, true, false, 0))
			return; // got a new target
	}

	if (actor->flags & MF_FLOAT && !(actor->flags2 & MF2_SKULLFLY))
	{ // Float up/down to your target's position. Stay above them, but not out of jump range.
		fixed_t target_min = actor->target->floorz+FixedMul(64*FRACUNIT, actor->scale);
		if (target_min < actor->target->z - actor->height)
			target_min = actor->target->z - actor->height;
		if (target_min < actor->floorz+FixedMul(33*FRACUNIT, actor->scale))
			target_min = actor->floorz+FixedMul(33*FRACUNIT, actor->scale);
		if (actor->z > target_min+FixedMul(16*FRACUNIT, actor->scale))
			actor->momz = FixedMul((-actor->info->speed<<(FRACBITS-1)), actor->scale);
		else if (actor->z < target_min)
			actor->momz = FixedMul(actor->info->speed<<(FRACBITS-1), actor->scale);
		else
			actor->momz = FixedMul(actor->momz,7*FRACUNIT/8);
	}

	// chase towards player
	if (P_AproxDistance(actor->target->x-actor->x, actor->target->y-actor->y) > actor->radius+actor->target->radius)
	{
		if (--actor->movecount < 0 || !P_Move(actor, actor->info->speed))
			P_NewChaseDir(actor);
	}
	// too close, don't want to chase.
	else if (--actor->movecount < 0)
	{
		// A mini-A_FaceTarget based on P_NewChaseDir.
		// Yes, it really is this simple when you get down to it.
		fixed_t deltax, deltay;

		deltax = actor->target->x - actor->x;
		deltay = actor->target->y - actor->y;

		actor->movedir = diags[((deltay < 0)<<1) + (deltax > 0)];
		actor->movecount = P_RandomByte(PR_ACTION) & 15;
	}
}

// Function: A_Boss2Chase
//
// Description: Really doesn't 'chase', but rather goes in a circle.
//
// var1 = unused
// var2 = unused
//
void A_Boss2Chase(void *thing)
{
	mobj_t *actor = thing;
	fixed_t radius;
	dboolean reverse = false;
	int32_t speedvar;

	if (LUA_CallAction(A_BOSS2CHASE, actor))
		return;

	if (actor->health <= 0)
		return;

	// Startup randomness
	if (actor->reactiontime <= -666)
		actor->reactiontime = 2*TICRATE + P_RandomByte(PR_ACTION);

	// When reactiontime hits zero, he will go the other way
	if (--actor->reactiontime <= 0)
	{
		reverse = true;
		actor->reactiontime = 2*TICRATE + P_RandomByte(PR_ACTION);
	}

	P_SetTarget(&actor->target, P_GetClosestAxis(actor));

	if (!actor->target) // This should NEVER happen.
	{
		CONS_Debug(DBG_GAMELOGIC, "Boss2 has no target!\n");
		A_BossDeath(actor);
		return;
	}

	radius = actor->target->radius;

	if (reverse)
	{
		actor->watertop = -actor->watertop;
		actor->extravalue1 = 18;
		if (actor->flags2 & MF2_AMBUSH)
			actor->extravalue1 -= (actor->info->spawnhealth - actor->health)*2;
		actor->extravalue2 = actor->extravalue1;
	}

	// Turnaround
	if (actor->extravalue1 > 0)
	{
		--actor->extravalue1;

		// Set base angle
		{
			const angle_t fa = (actor->target->angle + FixedAngle(actor->watertop))>>ANGLETOFINESHIFT;
			const fixed_t fc = FixedMul(FINECOSINE(fa),radius);
			const fixed_t fs = FixedMul(FINESINE(fa),radius);
			actor->angle = R_PointToAngle2(actor->x, actor->y, actor->target->x + fc, actor->target->y + fs);
		}

		// Now turn you around!
		// Note that the start position is the final position, we move it back around
		// to intermediary positions...
		actor->angle -= FixedAngle(FixedMul(FixedDiv(180<<FRACBITS, actor->extravalue2<<FRACBITS), actor->extravalue1<<FRACBITS));
	}
	else
	{
		// Only speed up if you have the ambush flag.
		if (actor->flags2 & MF2_AMBUSH)
			speedvar = actor->health;
		else
			speedvar = actor->info->spawnhealth;

		actor->target->angle += // Don't use FixedAngleC!
			FixedAngle(FixedDiv(FixedMul(actor->watertop, (actor->info->spawnhealth*(FRACUNIT/4)*3)), speedvar*FRACUNIT));

		P_UnsetThingPosition(actor);
		{
			const angle_t fa = actor->target->angle>>ANGLETOFINESHIFT;
			const fixed_t fc = FixedMul(FINECOSINE(fa),radius);
			const fixed_t fs = FixedMul(FINESINE(fa),radius);
			actor->angle = R_PointToAngle2(actor->x, actor->y, actor->target->x + fc, actor->target->y + fs);
			actor->x = actor->target->x + fc;
			actor->y = actor->target->y + fs;
		}
		P_SetThingPosition(actor);

		// Spray goo once every second
		if (leveltime % (speedvar*15/10)-1 == 0)
		{
			const fixed_t ns = FixedMul(3 * FRACUNIT, actor->scale);
			mobj_t *goop;
			fixed_t fz = actor->z+actor->height+FixedMul(24*FRACUNIT, actor->scale);
			angle_t fa;
			// actor->movedir is used to determine the last
			// direction goo was sprayed in. There are 8 possible
			// directions to spray. (45-degree increments)

			actor->movedir++;
			actor->movedir %= NUMDIRS;
			fa = (actor->movedir*FINEANGLES/8) & FINEMASK;

			goop = P_SpawnMobj(actor->x, actor->y, fz, actor->info->painchance);
			goop->momx = FixedMul(FINECOSINE(fa),ns);
			goop->momy = FixedMul(FINESINE(fa),ns);
			goop->momz = FixedMul(4*FRACUNIT, actor->scale);
			goop->fuse = 10*TICRATE;

			if (actor->info->attacksound)
				S_StartSound(actor, actor->info->attacksound);

			if (P_RandomChance(PR_ACTION, FRACUNIT/2))
			{
				goop->momx *= 2;
				goop->momy *= 2;
			}
			else if (P_RandomChance(PR_ACTION, 129*FRACUNIT/256))
			{
				goop->momx *= 3;
				goop->momy *= 3;
			}

			actor->flags2 |= MF2_JUSTATTACKED;
		}
	}
}

// Function: A_Boss2Pogo
//
// Description: Pogo part of Boss 2 AI.
//
// var1 = unused
// var2 = unused
//
void A_Boss2Pogo(void *thing)
{
	mobj_t *actor = thing;
	if (LUA_CallAction(A_BOSS2POGO, actor))
		return;

	if (actor->z <= actor->floorz + FixedMul(8*FRACUNIT, actor->scale) && actor->momz <= 0)
	{
		if (actor->state != &states[actor->info->raisestate])
			P_SetMobjState(actor, actor->info->raisestate);
		// Pogo Mode
	}
	else if (actor->momz < 0 && actor->reactiontime)
	{
		const fixed_t ns = FixedMul(3 * FRACUNIT, actor->scale);
		mobj_t *goop;
		fixed_t fz = actor->z+actor->height+FixedMul(24*FRACUNIT, actor->scale);
		angle_t fa;
		int32_t i;
		// spray in all 8 directions!
		for (i = 0; i < 8; i++)
		{
			actor->movedir++;
			actor->movedir %= NUMDIRS;
			fa = (actor->movedir*FINEANGLES/8) & FINEMASK;

			goop = P_SpawnMobj(actor->x, actor->y, fz, actor->info->painchance);
			goop->momx = FixedMul(FINECOSINE(fa),ns);
			goop->momy = FixedMul(FINESINE(fa),ns);
			goop->momz = FixedMul(4*FRACUNIT, actor->scale);

			goop->fuse = 10*TICRATE;
		}
		actor->reactiontime = 0; // we already shot goop, so don't do it again!
		if (actor->info->attacksound)
			S_StartSound(actor, actor->info->attacksound);
		actor->flags2 |= MF2_JUSTATTACKED;
	}
}

// Function: A_Boss2TakeDamage
//
// Description: Special function for Boss 2 so you can't just sit and destroy him.
//
// var1 = Invincibility duration
// var2 = unused
//
void A_Boss2TakeDamage(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;

	if (LUA_CallAction(A_BOSS2TAKEDAMAGE, actor))
		return;

	A_Pain(actor);
	actor->reactiontime = 1; // turn around
	if (locvar1 == 0) // old A_Invincibilerize behavior
		actor->movecount = TICRATE;
	else
		actor->movecount = locvar1; // become flashing invulnerable for this long.
}

// Function: A_Boss7Chase
//
// Description: Like A_Chase, but for Black Eggman
//
// var1 = unused
// var2 = unused
//
void A_Boss7Chase(void *thing)
{
	mobj_t *actor = thing;
	int32_t delta;
	int32_t i;

	if (LUA_CallAction(A_BOSS7CHASE, actor))
		return;

	if (actor->z != actor->floorz)
		return;

	// Self-adjust if stuck on the edge
	if (actor->tracer)
	{
		if (P_AproxDistance(actor->x - actor->tracer->x, actor->y - actor->tracer->y) > 128*FRACUNIT - actor->radius)
			P_InstaThrust(actor, R_PointToAngle2(actor->x, actor->y, actor->tracer->x, actor->tracer->y), FRACUNIT);
	}

	if (actor->flags2 & MF2_FRET)
	{
		P_SetMobjState(actor, S_BLACKEGG_DESTROYPLAT1);
		S_StartSound(0, sfx_s3k53);
		actor->flags2 &= ~MF2_FRET;
		return;
	}

	// turn towards movement direction if not there yet
	if (actor->movedir < NUMDIRS)
	{
		actor->angle &= (7<<29);
		delta = actor->angle - (actor->movedir << 29);

		if (delta > 0)
			actor->angle -= ANGLE_45;
		else if (delta < 0)
			actor->angle += ANGLE_45;
	}

	// Is a player on top of us?
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator)
			continue;

		if (!players[i].mo)
			continue;

		if (players[i].mo->health <= 0)
			continue;

		if (P_AproxDistance(players[i].mo->x - actor->x, players[i].mo->y - actor->y) > actor->radius)
			continue;

		if (players[i].mo->z > actor->z + actor->height - 2*FRACUNIT
			&& players[i].mo->z < actor->z + actor->height + 32*FRACUNIT)
		{
			// Punch him!
			P_SetMobjState(actor, actor->info->meleestate);
			S_StartSound(0, sfx_begrnd); // warning sound
			return;
		}
	}

	if (actor->reactiontime)
		actor->reactiontime--;

	if (actor->reactiontime <= 0 && actor->z == actor->floorz)
	{
		// Here, we'll call P_RandomByte() and decide what kind of attack to do
		switch(actor->threshold)
		{
			case 0: // Lob cannon balls
				if (actor->z < 1056*FRACUNIT)
				{
					A_FaceTarget(actor);
					P_SetMobjState(actor, actor->info->xdeathstate);
					actor->movecount = 7*TICRATE + P_RandomByte(PR_ACTION);
					break;
				}
				actor->threshold++;
				/* FALLTHRU */
			case 1: // Chaingun Goop
				A_FaceTarget(actor);
				P_SetMobjState(actor, S_BLACKEGG_SHOOT1);

				if (actor->health > actor->info->damage)
					actor->movecount = TICRATE + P_RandomByte(PR_ACTION)/3;
				else
					actor->movecount = TICRATE + P_RandomByte(PR_ACTION)/2;
				break;
			case 2: // Homing Missile
				A_FaceTarget(actor);
				P_SetMobjState(actor, actor->info->missilestate);
				S_StartSound(0, sfx_beflap);
				break;
		}

		actor->threshold++;
		actor->threshold %= 3;
		return;
	}

	// possibly choose another target
	if (multiplayer && (actor->target->health <= 0 || !P_CheckSight(actor, actor->target))
		&& P_BossTargetPlayer(actor, false))
		return; // got a new target

	if (leveltime & 1)
	{
		// chase towards player
		if (--actor->movecount < 0 || !P_Move(actor, actor->info->speed))
			P_NewChaseDir(actor);
	}
}

// Function: A_GoopSplat
//
// Description: Black Eggman goop hits a target and sticks around for awhile.
//
// var1 = unused
// var2 = unused
//
void A_GoopSplat(void *thing)
{
	mobj_t *actor = thing;
	if (LUA_CallAction(A_GOOPSPLAT, actor))
		return;

	P_UnsetThingPosition(actor);
	if (sector_list)
	{
		P_DelSeclist(sector_list);
		sector_list = NULL;
	}
	actor->flags = MF_SPECIAL; // Not a typo
	P_SetThingPosition(actor);
}

// Function: A_Boss2PogoSFX
//
// Description: Pogoing for Boss 2
//
// var1 = pogo jump strength
// var2 = idle pogo speed
//
void A_Boss2PogoSFX(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_BOSS2POGOSFX, actor))
		return;

	if (!actor->target || !(actor->target->flags & MF_SHOOTABLE))
	{
		// look for a new target
		if (P_LookForPlayers(actor, true, false, 0))
			return; // got a new target

		return;
	}

	// Boing!
	if (P_AproxDistance(actor->x-actor->target->x, actor->y-actor->target->y) < FixedMul(256*FRACUNIT, actor->scale))
	{
		actor->angle = R_PointToAngle2(actor->x, actor->y, actor->target->x, actor->target->y);
		P_InstaThrust(actor, actor->angle, FixedMul(actor->info->speed, actor->scale));
		// pogo on player
	}
	else
	{
		uint8_t prandom = P_RandomByte(PR_ACTION);
		actor->angle = R_PointToAngle2(actor->x, actor->y, actor->target->x, actor->target->y) + (P_RandomChance(PR_ACTION, FRACUNIT/2) ? -prandom : +prandom);
		P_InstaThrust(actor, actor->angle, FixedMul(FixedMul(actor->info->speed,(locvar2)), actor->scale));
	}
	if (actor->info->activesound) S_StartSound(actor, actor->info->activesound);
	actor->momz = FixedMul(locvar1, actor->scale); // Bounce up in air
	actor->reactiontime = 1;
}

// Function: A_Boss2PogoTarget
//
// Description: Pogoing for Boss 2, tries to actually land on the player directly.
//
// var1 = pogo jump strength
// var2 = idle pogo speed
//
void A_Boss2PogoTarget(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_BOSS2POGOTARGET, actor))
		return;

	if (!actor->target || !(actor->target->flags & MF_SHOOTABLE) || (actor->target->player && actor->target->player->flashing)
	|| P_AproxDistance(actor->x-actor->target->x, actor->y-actor->target->y) >= FixedMul(512*FRACUNIT, actor->scale))
	{
		// look for a new target
		if (P_LookForPlayers(actor, true, false, 512*FRACUNIT))
			; // got a new target
		else if (P_LookForPlayers(actor, true, false, 0))
			; // got a new target
		else
			return;
	}

	// Target hit, retreat!
	if ((actor->target->player && actor->target->player->flashing > TICRATE) || actor->flags2 & MF2_FRET)
	{
		uint8_t prandom = P_RandomByte(PR_ACTION);
		actor->z++; // unstick from the floor
		actor->momz = FixedMul(locvar1, actor->scale); // Bounce up in air
		actor->angle = R_PointToAngle2(actor->x, actor->y, actor->target->x, actor->target->y) + (P_RandomChance(PR_ACTION, FRACUNIT/2) ? -prandom : +prandom); // Pick a direction, and randomize it.
		P_InstaThrust(actor, actor->angle+ANGLE_180, FixedMul(FixedMul(actor->info->speed,(locvar2)), actor->scale)); // Move at wandering speed
	}
	// Try to land on top of the player.
	else if (P_AproxDistance(actor->x-actor->target->x, actor->y-actor->target->y) < FixedMul(512*FRACUNIT, actor->scale))
	{
		fixed_t airtime, gravityadd, zoffs;

		// check gravity in the sector (for later math)
		P_CheckGravity(actor, true);
		gravityadd = actor->momz;

		actor->z++; // unstick from the floor
		actor->momz = FixedMul(locvar1 + (locvar1>>2), actor->scale); // Bounce up in air

		/*badmath = 0;
		airtime = 0;
		do {
			badmath += momz;
			momz += gravityadd;
			airtime++;
		} while(badmath > 0);
		airtime = 2*airtime<<FRACBITS;
		*/

		// Remember, kids!
		// Reduced down Calculus lets you avoid bad 'logic math' loops!
		//airtime = FixedDiv(-actor->momz<<1, gravityadd)<<1; // going from 0 to 0 is much simpler
		zoffs = (actor->target->height>>1) + (actor->target->floorz - actor->floorz); // offset by the difference in floor height plus half the player height,
		airtime = FixedDiv((-actor->momz - FixedSqrt(FixedMul(actor->momz,actor->momz)+zoffs)), gravityadd)<<1; // to try and land on their head rather than on their feet

		actor->angle = R_PointToAngle2(actor->x, actor->y, actor->target->x, actor->target->y);
		P_InstaThrust(actor, actor->angle, FixedDiv(P_AproxDistance(actor->x - actor->target->x, actor->y - actor->target->y), airtime));
	}
	// Wander semi-randomly towards the player to get closer.
	else
	{
		uint8_t prandom = P_RandomByte(PR_ACTION);
		actor->z++; // unstick from the floor
		actor->momz = FixedMul(locvar1, actor->scale); // Bounce up in air
		actor->angle = R_PointToAngle2(actor->x, actor->y, actor->target->x, actor->target->y) + (P_RandomChance(PR_ACTION, FRACUNIT/2) ? -prandom : +prandom); // Pick a direction, and randomize it.
		P_InstaThrust(actor, actor->angle, FixedMul(FixedMul(actor->info->speed,(locvar2)), actor->scale)); // Move at wandering speed
	}
	// Boing!
	if (actor->info->activesound) S_StartSound(actor, actor->info->activesound);

	if (actor->info->missilestate) // spawn the pogo stick collision box
	{
		mobj_t *pogo = P_SpawnMobj(actor->x, actor->y, actor->z - mobjinfo[actor->info->missilestate].height, (mobjtype_t)actor->info->missilestate);
		P_SetTarget(&pogo->target, actor);
	}

	actor->reactiontime = 1;
}

// Function: A_EggmanBox
//
// Description: Harms the player
//
// var1 = unused
// var2 = unused
//
void A_EggmanBox(void *thing)
{
	mobj_t *actor = thing;
	if (LUA_CallAction(A_EGGMANBOX, actor))
		return;

	if (!actor->target || !actor->target->player)
	{
		CONS_Debug(DBG_GAMELOGIC, "Powerup has no target.\n");
		return;
	}

	P_DamageMobj(actor->target, actor, actor, 1, DMG_NORMAL); // Ow!
}

// Function: A_TurretFire
//
// Description: Initiates turret fire.
//
// var1 = object # to repeatedly fire
// var2 = distance threshold
//
void A_TurretFire(void *thing)
{
	mobj_t *actor = thing;
	int32_t count = 0;
	fixed_t dist;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_TURRETFIRE, actor))
		return;

	if (locvar2)
		dist = FixedMul(locvar2*FRACUNIT, actor->scale);
	else
		dist = FixedMul(2048*FRACUNIT, actor->scale);

	if (!locvar1)
		locvar1 = MT_TURRETLASER;

	while (P_SupermanLook4Players(actor) && count < MAXPLAYERS)
	{
		if (P_AproxDistance(actor->x - actor->target->x, actor->y - actor->target->y) < dist)
		{
			actor->flags2 |= MF2_FIRING;
			actor->extravalue1 = locvar1;
			break;
		}

		count++;
	}
}

// Function: A_SuperTurretFire
//
// Description: Initiates turret fire that even stops Super Sonic.
//
// var1 = object # to repeatedly fire
// var2 = distance threshold
//
void A_SuperTurretFire(void *thing)
{
	mobj_t *actor = thing;
	int32_t count = 0;
	fixed_t dist;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_SUPERTURRETFIRE, actor))
		return;

	if (locvar2)
		dist = FixedMul(locvar2*FRACUNIT, actor->scale);
	else
		dist = FixedMul(2048*FRACUNIT, actor->scale);

	if (!locvar1)
		locvar1 = MT_TURRETLASER;

	while (P_SupermanLook4Players(actor) && count < MAXPLAYERS)
	{
		if (P_AproxDistance(actor->x - actor->target->x, actor->y - actor->target->y) < dist)
		{
			actor->flags2 |= MF2_FIRING;
			actor->flags2 |= MF2_SUPERFIRE;
			actor->extravalue1 = locvar1;
			break;
		}

		count++;
	}
}

// Function: A_TurretStop
//
// Description: Stops the turret fire.
//
// var1 = Don't play activesound?
// var2 = unused
//
void A_TurretStop(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;

	if (LUA_CallAction(A_TURRETSTOP, actor))
		return;

	actor->flags2 &= ~MF2_FIRING;
	actor->flags2 &= ~MF2_SUPERFIRE;

	if (actor->target && actor->info->activesound && !locvar1)
		S_StartSound(actor, actor->info->activesound);
}

// Function: A_SparkFollow
//
// Description: Used by the hyper sparks to rotate around their target.
//
// var1 = unused
// var2 = unused
//
void A_SparkFollow(void *thing)
{
	mobj_t *actor = thing;
	if (LUA_CallAction(A_SPARKFOLLOW, actor))
		return;

	if (!actor->target || (actor->target->health <= 0))
	{
		P_RemoveMobj(actor);
		return;
	}

	actor->angle += FixedAngle(actor->info->damage*FRACUNIT);
	P_UnsetThingPosition(actor);
	{
		const angle_t fa = actor->angle>>ANGLETOFINESHIFT;
		actor->x = actor->target->x + FixedMul(FINECOSINE(fa),FixedMul(actor->info->speed, actor->scale));
		actor->y = actor->target->y + FixedMul(FINESINE(fa),FixedMul(actor->info->speed, actor->scale));
		if (actor->target->eflags & MFE_VERTICALFLIP)
			actor->z = actor->target->z + actor->target->height - FixedDiv(actor->target->height,3*FRACUNIT);
		else
			actor->z = actor->target->z + FixedDiv(actor->target->height,3*FRACUNIT) - actor->height;
	}
	P_SetThingPosition(actor);
}

// Function: A_BuzzFly
//
// Description: Makes an object slowly fly after a player, in the manner of a Buzz.
//
// var1 = sfx to play
// var2 = length of sfx, set to threshold if played
//
void A_BuzzFly(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_BUZZFLY, actor))
		return;

	if (actor->flags2 & MF2_AMBUSH)
		return;

	if (actor->reactiontime)
		actor->reactiontime--;

	// modify target threshold
	if (actor->threshold)
	{
		if (!actor->target || actor->target->health <= 0)
			actor->threshold = 0;
		else
			actor->threshold--;
	}

	if (!actor->target || !(actor->target->flags & MF_SHOOTABLE))
	{
		// look for a new target
		if (P_LookForPlayers(actor, true, false, 0))
			return; // got a new target

		actor->momz = actor->momy = actor->momx = 0;
		P_SetMobjState(actor, actor->info->spawnstate);
		return;
	}

	// turn towards movement direction if not there yet
	actor->angle = R_PointToAngle2(actor->x, actor->y, actor->target->x, actor->target->y);

	if (actor->target->health <= 0 || (!actor->threshold && !P_CheckSight(actor, actor->target)))
	{
		if ((multiplayer || netgame) && P_LookForPlayers(actor, true, false, FixedMul(3072*FRACUNIT, actor->scale)))
			return; // got a new target

		actor->momx = actor->momy = actor->momz = 0;
		P_SetMobjState(actor, actor->info->spawnstate); // Go back to looking around
		return;
	}

	// If the player is over 3072 fracunits away, then look for another player
	if (P_AproxDistance(P_AproxDistance(actor->target->x - actor->x, actor->target->y - actor->y),
		actor->target->z - actor->z) > FixedMul(3072*FRACUNIT, actor->scale))
	{
		if (multiplayer || netgame)
			P_LookForPlayers(actor, true, false, FixedMul(3072*FRACUNIT, actor->scale)); // maybe get a new target

		return;
	}

	// chase towards player
	{
		int32_t dist, realspeed;
		const fixed_t mf = 5*(FRACUNIT/4);

		if (ultimatemode)
			realspeed = FixedMul(FixedMul(actor->info->speed,mf), actor->scale);
		else
			realspeed = FixedMul(actor->info->speed, actor->scale);

		dist = P_AproxDistance(P_AproxDistance(actor->target->x - actor->x,
			actor->target->y - actor->y), actor->target->z - actor->z);

		if (dist < 1)
			dist = 1;

		actor->momx = FixedMul(FixedDiv(actor->target->x - actor->x, dist), realspeed);
		actor->momy = FixedMul(FixedDiv(actor->target->y - actor->y, dist), realspeed);
		actor->momz = FixedMul(FixedDiv(actor->target->z - actor->z, dist), realspeed);

		if (actor->z+actor->momz >= actor->waterbottom && actor->watertop > actor->floorz
			&& actor->z+actor->momz > actor->watertop - FixedMul(256*FRACUNIT, actor->scale)
			&& actor->z+actor->momz <= actor->watertop)
		{
			actor->momz = 0;
			actor->z = actor->watertop;
		}
	}

	if (locvar1 != sfx_None && !actor->threshold)
	{
		S_StartSound(actor, locvar1);
		actor->threshold = locvar2;
	}
}

// Function: A_GuardChase
//
// Description: Modified A_Chase for Egg Guard
//
// var1 = unused
// var2 = unused
//
void A_GuardChase(void *thing)
{
	mobj_t *actor = thing;
	int32_t delta;

	if (LUA_CallAction(A_GUARDCHASE, actor))
		return;

	if (actor->reactiontime)
		actor->reactiontime--;

	if ((!actor->tracer || !actor->tracer->health) && actor->threshold != 42)
	{
		P_SetTarget(&actor->tracer, NULL);
		actor->threshold = 42;
		P_SetMobjState(actor, actor->info->painstate);
		actor->flags |= MF_SPECIAL|MF_SHOOTABLE;
		return;
	}

	// turn towards movement direction if not there yet
	if (actor->movedir < NUMDIRS)
	{
		actor->angle &= (7<<29);
		delta = actor->angle - (actor->movedir << 29);

		if (delta > 0)
			actor->angle -= ANGLE_45;
		else if (delta < 0)
			actor->angle += ANGLE_45;
	}

	if (!actor->target || !(actor->target->flags & MF_SHOOTABLE))
	{
		// look for a new target
		if (P_LookForPlayers(actor, true, false, 0))
			return; // got a new target

		P_SetMobjStateNF(actor, actor->info->spawnstate);
		return;
	}

	// possibly choose another target
	if (multiplayer && (actor->target->health <= 0 || !P_CheckSight(actor, actor->target))
		&& P_LookForPlayers(actor, true, false, 0))
		return; // got a new target

	// chase towards player
	if (--actor->movecount < 0 || !P_Move(actor, (actor->flags2 & MF2_AMBUSH) ? actor->info->speed * 2 : actor->info->speed))
	{
		P_NewChaseDir(actor);
		actor->movecount += 5; // Increase tics before change in direction allowed.
	}

	// Now that we've moved, its time for our shield to move!
	// Otherwise it'll never act as a proper overlay.
	if (actor->tracer && actor->tracer->state
	&& actor->tracer->state->action)
	{
		var1 = actor->tracer->state->var1, var2 = actor->tracer->state->var2;
		actor->tracer->state->action(actor->tracer);
	}
}

// Function: A_EggShield
//
// Description: Modified A_Chase for Egg Guard's shield
//
// var1 = unused
// var2 = unused
//
void A_EggShield(void *thing)
{
	mobj_t *actor = thing;
	int32_t i;
	player_t *player;
	fixed_t blockdist;
	fixed_t newx, newy;
	fixed_t movex, movey;
	angle_t angle;

	if (LUA_CallAction(A_EGGSHIELD, actor))
		return;

	if (!actor->target || !actor->target->health)
	{
		P_RemoveMobj(actor);
		return;
	}

	newx = actor->target->x + P_ReturnThrustX(actor, actor->target->angle, FixedMul(FRACUNIT, actor->scale));
	newy = actor->target->y + P_ReturnThrustY(actor, actor->target->angle, FixedMul(FRACUNIT, actor->scale));

	movex = newx - actor->x;
	movey = newy - actor->y;

	actor->angle = actor->target->angle;
	if (actor->target->eflags & MFE_VERTICALFLIP)
	{
		actor->eflags |= MFE_VERTICALFLIP;
		actor->z = actor->target->z + actor->target->height - actor->height;
	}
	else
		actor->z = actor->target->z;

	actor->destscale = actor->target->destscale;
	P_SetScale(actor, actor->target->scale);

	actor->floorz = actor->target->floorz;
	actor->ceilingz = actor->target->ceilingz;

	if (!movex && !movey)
		return;

	P_UnsetThingPosition(actor);
	actor->x = newx;
	actor->y = newy;
	P_SetThingPosition(actor);

	// Search for players to push
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator)
			continue;

		player = &players[i];

		if (!player->mo)
			continue;

		if (player->mo->z > actor->z + actor->height)
			continue;

		if (player->mo->z + player->mo->height < actor->z)
			continue;

		blockdist = actor->radius + player->mo->radius;

		if (abs(actor->x - player->mo->x) >= blockdist || abs(actor->y - player->mo->y) >= blockdist)
			continue; // didn't hit it

		angle = R_PointToAngle2(actor->x, actor->y, player->mo->x, player->mo->y) - actor->angle;

		if (angle > ANGLE_90 && angle < ANGLE_270)
			continue;

		// Blocked by the shield
		player->mo->momx += movex;
		player->mo->momy += movey;
		return;
	}
}


// Function: A_SetReactionTime
//
// Description: Sets the object's reaction time.
//
// var1 = 1 (use value in var2); 0 (use info table value)
// var2 = if var1 = 1, then value to set
//
void A_SetReactionTime(void *thing)
{
	mobj_t *actor = thing;
	if (LUA_CallAction(A_SETREACTIONTIME, actor))
		return;

	if (var1)
		actor->reactiontime = var2;
	else
		actor->reactiontime = actor->info->reactiontime;
}

// Function: A_Boss1Spikeballs
//
// Description: Boss 1 spikeball spawning loop.
//
// var1 = ball number
// var2 = total balls
//
void A_Boss1Spikeballs(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	mobj_t *ball;

	if (LUA_CallAction(A_BOSS1SPIKEBALLS, actor))
		return;

	ball = P_SpawnMobj(actor->x, actor->y, actor->z, MT_EGGMOBILE_BALL);
	P_SetTarget(&ball->target, actor);
	ball->movedir = FixedAngle(FixedMul(FixedDiv(locvar1<<FRACBITS, locvar2<<FRACBITS), 360<<FRACBITS));
	ball->threshold = ball->radius + actor->radius + ball->info->painchance;

	S_StartSound(ball, ball->info->seesound);
	var1 = ball->state->var1, var2 = ball->state->var2;
	ball->state->action(ball);
}

// Function: A_Boss3TakeDamage
//
// Description: Called when Boss 3 takes damage.
//
// var1 = movecount value
// var2 = unused
//
void A_Boss3TakeDamage(void *thing)
{
	mobj_t *actor = thing;
	if (LUA_CallAction(A_BOSS3TAKEDAMAGE, actor))
		return;

	actor->movecount = var1;

	if (actor->target && actor->target->spawnpoint)
		actor->threshold = actor->target->spawnpoint->extrainfo;
}

// Function: A_Boss3Path
//
// Description: Does pathfinding along Boss 3's nodes.
//
// var1 = unused
// var2 = unused
//
void A_Boss3Path(void *thing)
{
	mobj_t *actor = thing;
	if (LUA_CallAction(A_BOSS3PATH, actor))
		return;

	if (actor->tracer && actor->tracer->health && actor->tracer->movecount)
		actor->movecount |= 1;
	else if (actor->movecount & 1)
		actor->movecount = 0;

	if (actor->movecount & 2) // We've reached a firing point?
	{
		// Wait here and pretend to be angry or something.
		actor->momx = 0;
		actor->momy = 0;
		actor->momz = 0;
		if (actor->tracer)
			P_SetTarget(&actor->target, actor->tracer->target);
		var1 = 0, var2 = 0;
		A_FaceTarget(actor);
		if (actor && actor->tracer && actor->tracer->state == &states[actor->tracer->info->missilestate])
			P_SetMobjState(actor, actor->info->missilestate);
		return;
	}
	else if (actor->threshold >= 0) // Traveling mode
	{
		thinker_t *th;
		mobj_t *mo2;
		fixed_t dist, dist2;
		fixed_t speed;

		P_SetTarget(&actor->target, NULL);

		// scan the thinkers
		// to find a point that matches
		// the number
		for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
		{
			if (th->function == (actionf_p1)P_RemoveThinkerDelayed)
				continue;

			mo2 = (mobj_t *)th;
			if (mo2->type == MT_BOSS3WAYPOINT && mo2->spawnpoint && mo2->spawnpoint->angle == actor->threshold)
			{
				P_SetTarget(&actor->target, mo2);
				break;
			}
		}

		if (!actor->target) // Should NEVER happen
		{
			CONS_Debug(DBG_GAMELOGIC, "Error: Boss 3 Dummy was unable to find specified waypoint: %d\n", actor->threshold);
			return;
		}

		dist = P_AproxDistance(P_AproxDistance(actor->target->x - actor->x, actor->target->y - actor->y), actor->target->z - actor->z);

		if (dist < 1)
			dist = 1;

		if (actor->tracer && ((actor->tracer->movedir)
		|| (actor->tracer->health <= actor->tracer->info->damage)))
			speed = actor->info->speed * 2;
		else
			speed = actor->info->speed;

		actor->momx = FixedMul(FixedDiv(actor->target->x - actor->x, dist), speed);
		actor->momy = FixedMul(FixedDiv(actor->target->y - actor->y, dist), speed);
		actor->momz = FixedMul(FixedDiv(actor->target->z - actor->z, dist), speed);

		if (actor->momx != 0 || actor->momy != 0)
			actor->angle = R_PointToAngle2(0, 0, actor->momx, actor->momy);

		dist2 = P_AproxDistance(P_AproxDistance(actor->target->x - (actor->x + actor->momx), actor->target->y - (actor->y + actor->momy)), actor->target->z - (actor->z + actor->momz));

		if (dist2 < 1)
			dist2 = 1;

		if ((dist >> FRACBITS) <= (dist2 >> FRACBITS))
		{
			// If further away, set XYZ of mobj to waypoint location
			P_UnsetThingPosition(actor);
			actor->x = actor->target->x;
			actor->y = actor->target->y;
			actor->z = actor->target->z;
			actor->momx = actor->momy = actor->momz = 0;
			P_SetThingPosition(actor);

			if (actor->threshold == 0)
			{
				P_RemoveMobj(actor); // Cycle completed. Dummy removed.
				return;
			}

			// Set to next waypoint in sequence
			if (actor->target->spawnpoint)
			{
				// From the center point, choose one of the five paths
				if (actor->target->spawnpoint->angle == 0)
				{
					P_RemoveMobj(actor); // Cycle completed. Dummy removed.
					return;
				}
				else
					actor->threshold = actor->target->spawnpoint->extrainfo;

				// If the deaf flag is set, go into firing mode
				if (actor->target->spawnpoint->options & MTF_AMBUSH)
					actor->movecount |= 2;
			}
			else // This should never happen, as well
				CONS_Debug(DBG_GAMELOGIC, "Error: Boss 3 Dummy waypoint has no spawnpoint associated with it.\n");
		}
	}
}

// Function: A_LinedefExecute
//
// Description: Object's location is used to set the calling sector. The tag used is var1. Optionally, if var2 is set, the actor's angle (multiplied by var2) is added to the tag number as well.
//
// var1 = tag
// var2 = add angle to tag (optional)
//
void A_LinedefExecute(void *thing)
{
	mobj_t *actor = thing;
	int32_t tagnum;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_LINEDEFEXECUTE, actor))
		return;

	tagnum = locvar1;
	// state numbers option is no more, custom states cannot be guaranteed to stay the same number anymore, now that they can be defined by names instead

	if (locvar2)
		tagnum += locvar2*(AngleFixed(actor->angle)>>FRACBITS);

	CONS_Debug(DBG_GAMELOGIC, "A_LinedefExecute: Running mobjtype %d's sector with tag %d\n", actor->type, tagnum);

	// tag 32768 displayed in map editors is actually tag -32768, tag 32769 is -32767, 65535 is -1 etc.
	P_LinedefExecute((int16_t)tagnum, actor, actor->subsector->sector);
}

// Function: A_LinedefExecuteFromArg
//
// Description: Object's location is used to set the calling sector. The tag used is the spawnpoint mapthing's args[var1].
//
// var1 = mapthing arg to take tag from
// var2 = unused
//
void A_LinedefExecuteFromArg(void *thing)
{
	mobj_t *actor = thing;
	int32_t tagnum;
	int32_t locvar1 = var1;

	if (LUA_CallAction(A_LINEDEFEXECUTEFROMARG, actor))
		return;

	if (locvar1 < 0 || locvar1 > NUM_MAPTHING_ARGS)
	{
		CONS_Debug(DBG_GAMELOGIC, "A_LinedefExecuteFromArg: Invalid mapthing arg %d\n", locvar1);
		return;
	}

	tagnum = actor->args[locvar1];

	CONS_Debug(DBG_GAMELOGIC, "A_LinedefExecuteFromArg: Running mobjtype %d's sector with tag %d\n", actor->type, tagnum);

	// tag 32768 displayed in map editors is actually tag -32768, tag 32769 is -32767, 65535 is -1 etc.
	P_LinedefExecute((int16_t)tagnum, actor, actor->subsector->sector);
}

// Function: A_PlaySeeSound
//
// Description: Plays the object's seesound.
//
// var1 = unused
// var2 = unused
//
void A_PlaySeeSound(void *thing)
{
	mobj_t *actor = thing;
	if (LUA_CallAction(A_PLAYSEESOUND, actor))
		return;

	if (actor->info->seesound)
		S_StartSound(actor, actor->info->seesound);
}

// Function: A_PlayAttackSound
//
// Description: Plays the object's attacksound.
//
// var1 = unused
// var2 = unused
//
void A_PlayAttackSound(void *thing)
{
	mobj_t *actor = thing;
	if (LUA_CallAction(A_PLAYATTACKSOUND, actor))
		return;

	if (actor->info->attacksound)
		S_StartSound(actor, actor->info->attacksound);
}

// Function: A_PlayActiveSound
//
// Description: Plays the object's activesound.
//
// var1 = unused
// var2 = unused
//
void A_PlayActiveSound(void *thing)
{
	mobj_t *actor = thing;
	if (LUA_CallAction(A_PLAYACTIVESOUND, actor))
		return;

	if (actor->info->activesound)
		S_StartSound(actor, actor->info->activesound);
}

// Function: A_SmokeTrailer
//
// Description: Adds smoke trails to an object.
//
// var1 = object # to spawn as smoke
// var2 = unused
//
void A_SmokeTrailer(void *thing)
{
	mobj_t *actor = thing;
	mobj_t *th;
	int32_t locvar1 = var1;

	if (LUA_CallAction(A_SMOKETRAILER, actor))
		return;

	if (leveltime % 4)
		return;

	// add the smoke behind the rocket
	if (actor->eflags & MFE_VERTICALFLIP)
	{
		th = P_SpawnMobj(actor->x-actor->momx, actor->y-actor->momy, actor->z + actor->height - FixedMul(mobjinfo[locvar1].height, actor->scale), locvar1);
		th->flags2 |= MF2_OBJECTFLIP;
	}
	else
		th = P_SpawnMobj(actor->x-actor->momx, actor->y-actor->momy, actor->z, locvar1);
	P_SetObjectMomZ(th, FRACUNIT, false);
	th->destscale = actor->scale;
	P_SetScale(th, actor->scale);
	th->tics -= P_RandomByte(PR_DECORATION) & 3;
	if (th->tics < 1)
		th->tics = 1;
}

// Function: A_SpawnObjectAbsolute
//
// Description: Spawns an object at an absolute position
//
// var1:
//		var1 >> 16 = x
//		var1 & 65535 = y
// var2:
//		var2 >> 16 = z
//		var2 & 65535 = type
//
void A_SpawnObjectAbsolute(void *thing)
{
	mobj_t *actor = thing;
	int16_t x, y, z; // Want to be sure we can use negative values
	mobjtype_t type;
	mobj_t *mo;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_SPAWNOBJECTABSOLUTE, actor))
		return;

	x = (int16_t)(locvar1>>16);
	y = (int16_t)(locvar1&65535);
	z = (int16_t)(locvar2>>16);
	type = (mobjtype_t)(locvar2&65535);

	mo = P_SpawnMobj(x<<FRACBITS, y<<FRACBITS, z<<FRACBITS, type);

	// Spawn objects with an angle matching the spawner's, rather than spawning Eastwards - Monster Iestyn
	mo->angle = actor->angle;

	if (actor->eflags & MFE_VERTICALFLIP)
		mo->flags2 |= MF2_OBJECTFLIP;
}

// Function: A_SpawnObjectRelative
//
// Description: Spawns an object relative to the location of the actor
//
// var1:
//		var1 >> 16 = x
//		var1 & 65535 = y
// var2:
//		var2 >> 16 = z
//		var2 & 65535 = type
//
void A_SpawnObjectRelative(void *thing)
{
	mobj_t *actor = thing;
	int16_t x, y, z; // Want to be sure we can use negative values
	mobjtype_t type;
	mobj_t *mo;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_SPAWNOBJECTRELATIVE, actor))
		return;

	CONS_Debug(DBG_GAMELOGIC, "A_SpawnObjectRelative called from object type %d, var1: %d, var2: %d\n", actor->type, locvar1, locvar2);

	x = (int16_t)(locvar1>>16);
	y = (int16_t)(locvar1&65535);
	z = (int16_t)(locvar2>>16);
	type = (mobjtype_t)(locvar2&65535);

	// Spawn objects correctly in reverse gravity.
	// NOTE: Doing actor->z + actor->height is the bottom of the object while the object has reverse gravity. - Flame
	mo = P_SpawnMobj(actor->x + FixedMul(x<<FRACBITS, actor->scale),
		actor->y + FixedMul(y<<FRACBITS, actor->scale),
		(actor->eflags & MFE_VERTICALFLIP) ? ((actor->z + actor->height - mobjinfo[type].height) - FixedMul(z<<FRACBITS, actor->scale)) : (actor->z + FixedMul(z<<FRACBITS, actor->scale)), type);

	// Spawn objects with an angle matching the spawner's, rather than spawning Eastwards - Monster Iestyn
	mo->angle = actor->angle;

	if (actor->eflags & MFE_VERTICALFLIP)
		mo->flags2 |= MF2_OBJECTFLIP;

}

// Function: A_ChangeAngleRelative
//
// Description: Changes the angle to a random relative value between the min and max. Set min and max to the same value to eliminate randomness
//
// var1 = min
// var2 = max
//
void A_ChangeAngleRelative(void *thing)
{
	mobj_t *actor = thing;
	// Oh god, the old code /sucked/. Changed this and the absolute version to get a random range using amin and amax instead of
	//  getting a random angle from the _entire_ spectrum and then clipping. While we're at it, do the angle conversion to the result
	//  rather than the ranges, so <0 and >360 work as possible values. -Red
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	//angle_t angle = (P_RandomByte(PR_UNDEFINED)+1)<<24;
	const fixed_t amin = locvar1*FRACUNIT;
	const fixed_t amax = locvar2*FRACUNIT;
	//const angle_t amin = FixedAngle(locvar1*FRACUNIT);
	//const angle_t amax = FixedAngle(locvar2*FRACUNIT);

	if (LUA_CallAction(A_CHANGEANGLERELATIVE, actor))
		return;

#ifdef PARANOIA
	if (amin > amax)
		I_Error("A_ChangeAngleRelative: var1 is greater than var2");
#endif
/*
	if (angle < amin)
		angle = amin;
	if (angle > amax)
		angle = amax;*/

	actor->angle += FixedAngle(P_RandomRange(PR_ACTION, amin, amax));
}

// Function: A_ChangeAngleAbsolute
//
// Description: Changes the angle to a random absolute value between the min and max. Set min and max to the same value to eliminate randomness
//
// var1 = min
// var2 = max
//
void A_ChangeAngleAbsolute(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	//angle_t angle = (P_RandomByte(PR_UNDEFINED)+1)<<24;
	const fixed_t amin = locvar1*FRACUNIT;
	const fixed_t amax = locvar2*FRACUNIT;
	//const angle_t amin = FixedAngle(locvar1*FRACUNIT);
	//const angle_t amax = FixedAngle(locvar2*FRACUNIT);

	if (LUA_CallAction(A_CHANGEANGLEABSOLUTE, actor))
		return;

#ifdef PARANOIA
	if (amin > amax)
		I_Error("A_ChangeAngleAbsolute: var1 is greater than var2");
#endif
/*
	if (angle < amin)
		angle = amin;
	if (angle > amax)
		angle = amax;*/

	actor->angle = FixedAngle(P_RandomRange(PR_ACTION, amin, amax));
}

// Function: A_RollAngle
//
// Description: Changes the roll angle.
//
// var1 = angle
// var2 = relative? (default)
//
void A_RollAngle(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	const angle_t angle = FixedAngle(locvar1*FRACUNIT);

	if (LUA_CallAction(A_ROLLANGLE, actor))
		return;

	// relative (default)
	if (!locvar2)
		actor->rollangle += angle;
	// absolute
	else
		actor->rollangle = angle;
}

// Function: A_ChangeRollAngleRelative
//
// Description: Changes the roll angle to a random relative value between the min and max. Set min and max to the same value to eliminate randomness
//
// var1 = min
// var2 = max
//
void A_ChangeRollAngleRelative(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	const fixed_t amin = locvar1*FRACUNIT;
	const fixed_t amax = locvar2*FRACUNIT;

	if (LUA_CallAction(A_CHANGEROLLANGLERELATIVE, actor))
		return;

#ifdef PARANOIA
	if (amin > amax)
		I_Error("A_ChangeRollAngleRelative: var1 is greater than var2");
#endif

	actor->rollangle += FixedAngle(P_RandomRange(PR_ACTION, amin, amax));
}

// Function: A_ChangeRollAngleAbsolute
//
// Description: Changes the roll angle to a random absolute value between the min and max. Set min and max to the same value to eliminate randomness
//
// var1 = min
// var2 = max
//
void A_ChangeRollAngleAbsolute(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	const fixed_t amin = locvar1*FRACUNIT;
	const fixed_t amax = locvar2*FRACUNIT;

	if (LUA_CallAction(A_CHANGEROLLANGLEABSOLUTE, actor))
		return;

#ifdef PARANOIA
	if (amin > amax)
		I_Error("A_ChangeRollAngleAbsolute: var1 is greater than var2");
#endif

	actor->rollangle = FixedAngle(P_RandomRange(PR_ACTION, amin, amax));
}

// Function: A_PlaySound
//
// Description: Plays a sound
//
// var1 = sound # to play
// var2:
//		lower 16 bits = If 1, play sound using calling object as origin. If 0, play sound without an origin
//		upper 16 bits = If 1, do not play sound during preticker.
//
void A_PlaySound(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_PLAYSOUND, actor))
		return;

	if (leveltime < 2 && (locvar2 >> 16))
		return;

	S_StartSound((locvar2 & 65535) ? actor : NULL, locvar1);
}

// Function: A_FindTarget
//
// Description: Finds the nearest/furthest mobj of the specified type and sets actor->target to it.
//
// var1 = mobj type
// var2 = if (0) nearest; else furthest;
//
void A_FindTarget(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	mobj_t *targetedmobj = NULL;
	thinker_t *th;
	mobj_t *mo2;
	fixed_t dist1 = 0, dist2 = 0;

	if (LUA_CallAction(A_FINDTARGET, actor))
		return;

	CONS_Debug(DBG_GAMELOGIC, "A_FindTarget called from object type %d, var1: %d, var2: %d\n", actor->type, locvar1, locvar2);

	// scan the thinkers
	for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
	{
		if (th->function == (actionf_p1)P_RemoveThinkerDelayed)
			continue;

		mo2 = (mobj_t *)th;

		if (mo2->type == (mobjtype_t)locvar1)
		{
			if (mo2->player && mo2->player->spectator)
				continue; // Ignore spectators
			if ((mo2->player || mo2->flags & MF_ENEMY) && mo2->health <= 0)
				continue; // Ignore dead things
			if (targetedmobj == NULL)
			{
				targetedmobj = mo2;
				dist2 = R_PointToDist2(actor->x, actor->y, mo2->x, mo2->y);
			}
			else
			{
				dist1 = R_PointToDist2(actor->x, actor->y, mo2->x, mo2->y);

				if ((!locvar2 && dist1 < dist2) || (locvar2 && dist1 > dist2))
				{
					targetedmobj = mo2;
					dist2 = dist1;
				}
			}
		}
	}

	if (!targetedmobj)
	{
		CONS_Debug(DBG_GAMELOGIC, "A_FindTarget: Unable to find the specified object to target.\n");
		return; // Oops, nothing found..
	}

	CONS_Debug(DBG_GAMELOGIC, "A_FindTarget: Found a target.\n");

	P_SetTarget(&actor->target, targetedmobj);
}

// Function: A_FindTracer
//
// Description: Finds the nearest/furthest mobj of the specified type and sets actor->tracer to it.
//
// var1 = mobj type
// var2 = if (0) nearest; else furthest;
//
void A_FindTracer(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	mobj_t *targetedmobj = NULL;
	thinker_t *th;
	mobj_t *mo2;
	fixed_t dist1 = 0, dist2 = 0;

	if (LUA_CallAction(A_FINDTRACER, actor))
		return;

	CONS_Debug(DBG_GAMELOGIC, "A_FindTracer called from object type %d, var1: %d, var2: %d\n", actor->type, locvar1, locvar2);

	// scan the thinkers
	for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
	{
		if (th->function == (actionf_p1)P_RemoveThinkerDelayed)
			continue;

		mo2 = (mobj_t *)th;

		if (mo2->type == (mobjtype_t)locvar1)
		{
			if (mo2->player && mo2->player->spectator)
				continue; // Ignore spectators
			if ((mo2->player || mo2->flags & MF_ENEMY) && mo2->health <= 0)
				continue; // Ignore dead things
			if (targetedmobj == NULL)
			{
				targetedmobj = mo2;
				dist2 = R_PointToDist2(actor->x, actor->y, mo2->x, mo2->y);
			}
			else
			{
				dist1 = R_PointToDist2(actor->x, actor->y, mo2->x, mo2->y);

				if ((!locvar2 && dist1 < dist2) || (locvar2 && dist1 > dist2))
				{
					targetedmobj = mo2;
					dist2 = dist1;
				}
			}
		}
	}

	if (!targetedmobj)
	{
		CONS_Debug(DBG_GAMELOGIC, "A_FindTracer: Unable to find the specified object to target.\n");
		return; // Oops, nothing found..
	}

	CONS_Debug(DBG_GAMELOGIC, "A_FindTracer: Found a target.\n");

	P_SetTarget(&actor->tracer, targetedmobj);
}

// Function: A_SetTics
//
// Description: Sets the animation tics of an object
//
// var1 = tics to set to
// var2 = if this is set, and no var1 is supplied, the mobj's threshold value will be used.
//
void A_SetTics(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_SETTICS, actor))
		return;

	if (locvar1)
		actor->tics = locvar1;
	else if (locvar2)
		actor->tics = actor->threshold;
}

// Function: A_SetRandomTics
//
// Description: Sets the animation tics of an object to a random value
//
// var1 = lower bound
// var2 = upper bound
//
void A_SetRandomTics(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_SETRANDOMTICS, actor))
		return;

	actor->tics = P_RandomRange(PR_RANDOMANIM, locvar1, locvar2);
}

// Function: A_ChangeColorRelative
//
// Description: Changes the color of an object
//
// var1 = if (var1 > 0), find target and add its color value to yours
// var2 = if (var1 = 0), color value to add
//
void A_ChangeColorRelative(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_CHANGECOLORRELATIVE, actor))
		return;

	if (locvar1)
	{
		// Have you ever seen anything so hideous?
		if (actor->target)
			actor->color = (uint16_t)(actor->color + actor->target->color);
	}
	else
		actor->color = (uint16_t)(actor->color + locvar2);
}

// Function: A_ChangeColorAbsolute
//
// Description: Changes the color of an object by an absolute value. Note: 0 is default colormap.
//
// var1 = if (var1 > 0), set your color to your target's color
// var2 = if (var1 = 0), color value to set to
//
void A_ChangeColorAbsolute(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_CHANGECOLORABSOLUTE, actor))
		return;

	if (locvar1)
	{
		if (actor->target)
			actor->color = actor->target->color;
	}
	else
		actor->color = (uint16_t)locvar2;
}

// Function: A_Dye
//
// Description: Colorizes an object.
//
// var1 = if (var1 != 0), dye your target instead of yourself
// var2 = color value to dye
//
void A_Dye(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	mobj_t *target = ((locvar1 && actor->target) ? actor->target : actor);
	uint16_t color = (uint16_t)locvar2;
	if (LUA_CallAction(A_DYE, actor))
		return;
	if (color >= numskincolors)
		return;

	// What if it's a player?
	if (target->player)
		target->player->dye = color;

	if (!color)
	{
		target->colorized = false;
		target->color = target->player ? target->player->skincolor : SKINCOLOR_NONE;
	}
	else if (!(target->player))
	{
		target->colorized = true;
		target->color = color;
	}
}

// Function: A_MoveRelative
//
// Description: Moves an object (wrapper for P_Thrust)
//
// var1 = angle
// var2 = force
//
void A_MoveRelative(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_MOVERELATIVE, actor))
		return;

	P_Thrust(actor, actor->angle+FixedAngle(locvar1*FRACUNIT), FixedMul(locvar2*FRACUNIT, actor->scale));
}

// Function: A_MoveAbsolute
//
// Description: Moves an object (wrapper for P_InstaThrust)
//
// var1 = angle
// var2 = force
//
void A_MoveAbsolute(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_MOVEABSOLUTE, actor))
		return;

	P_InstaThrust(actor, FixedAngle(locvar1*FRACUNIT), FixedMul(locvar2*FRACUNIT, actor->scale));
}

// Function: A_Thrust
//
// Description: Pushes the object horizontally at its current angle.
//
// var1 = amount of force
// var2 = If 1, xy momentum is lost. If 0, xy momentum is kept
//
void A_Thrust(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_THRUST, actor))
		return;

	if (!locvar1)
		CONS_Debug(DBG_GAMELOGIC, "A_Thrust: Var1 not specified!\n");

	if (locvar2)
		P_InstaThrust(actor, actor->angle, FixedMul(locvar1*FRACUNIT, actor->scale));
	else
		P_Thrust(actor, actor->angle, FixedMul(locvar1*FRACUNIT, actor->scale));
}

// Function: A_ZThrust
//
// Description: Pushes the object up or down.
//
// var1 = amount of force
// var2:
//		lower 16 bits = If 1, xy momentum is lost. If 0, xy momentum is kept
//		upper 16 bits = If 1, z momentum is lost. If 0, z momentum is kept
//
void A_ZThrust(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_ZTHRUST, actor))
		return;

	if (!locvar1)
		CONS_Debug(DBG_GAMELOGIC, "A_ZThrust: Var1 not specified!\n");

	if (locvar2 & 65535)
		actor->momx = actor->momy = 0;

	if (actor->eflags & MFE_VERTICALFLIP)
		actor->z--;
	else
		actor->z++;

	P_SetObjectMomZ(actor, locvar1*FRACUNIT, !(locvar2 >> 16));
}

// Function: A_SetTargetsTarget
//
// Description: Sets your target to the object who your target is targeting. Yikes! If it happens to be NULL, you're just out of luck.
//
// var1: (Your target)
//		0 = target
//		1 = tracer
// var2: (Your target's target)
//		0 = target/tracer's target
//		1 = target/tracer's tracer
//
void A_SetTargetsTarget(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	mobj_t *oldtarg = NULL, *newtarg = NULL;

	if (LUA_CallAction(A_SETTARGETSTARGET, actor))
		return;

	// actor's target
	if (locvar1) // or tracer
		oldtarg = actor->tracer;
	else
		oldtarg = actor->target;

	if (P_MobjWasRemoved(oldtarg))
		return;

	// actor's target's target!
	if (locvar2) // or tracer
		newtarg = oldtarg->tracer;
	else
		newtarg = oldtarg->target;

	if (P_MobjWasRemoved(newtarg))
		return;

	// set actor's new target
	if (locvar1) // or tracer
		P_SetTarget(&actor->tracer, newtarg);
	else
		P_SetTarget(&actor->target, newtarg);
}

// Function: A_SetObjectFlags
//
// Description: Sets the flags of an object
//
// var1 = flag value to set
// var2:
//		if var2 == 2, add the flag to the current flags
//		else if var2 == 1, remove the flag from the current flags
//		else if var2 == 0, set the flags to the exact value
//
void A_SetObjectFlags(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	dboolean unlinkthings = false;

	if (LUA_CallAction(A_SETOBJECTFLAGS, actor))
		return;

	if (locvar2 == 2)
		locvar1 = actor->flags | locvar1;
	else if (locvar2 == 1)
		locvar1 = actor->flags & ~locvar1;

	if ((uint32_t)(locvar1 & (MF_NOBLOCKMAP|MF_NOSECTOR)) != (actor->flags & (MF_NOBLOCKMAP|MF_NOSECTOR))) // Blockmap/sector status has changed, so reset the links
		unlinkthings = true;

	if (unlinkthings) {
		P_UnsetThingPosition(actor);
		if (sector_list)
		{
			P_DelSeclist(sector_list);
			sector_list = NULL;
		}
	}

	actor->flags = locvar1;

	if (unlinkthings)
		P_SetThingPosition(actor);
}

// Function: A_SetObjectFlags2
//
// Description: Sets the flags2 of an object
//
// var1 = flag value to set
// var2:
//		if var2 == 2, add the flag to the current flags
//		else if var2 == 1, remove the flag from the current flags
//		else if var2 == 0, set the flags to the exact value
//
void A_SetObjectFlags2(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_SETOBJECTFLAGS2, actor))
		return;

	if (locvar2 == 2)
		actor->flags2 |= locvar1;
	else if (locvar2 == 1)
		actor->flags2 &= ~locvar1;
	else
		actor->flags2 = locvar1;
}

// Function: A_BossJetFume
//
// Description: Spawns jet fumes/other attachment miscellany for the boss. To only be used when he is spawned.
//
// var1:
//		0 - Triple jet fume pattern
//		1 - Boss 3's propeller
//		2 - Metal Sonic jet fume
//		3 - Boss 4 jet flame
// var2 = unused
//
void A_BossJetFume(void *thing)
{
	mobj_t *actor = thing;
	mobj_t *filler;
	int32_t locvar1 = var1;

	if (LUA_CallAction(A_BOSSJETFUME, actor))
		return;

	if (locvar1 == 0) // Boss1 jet fumes
	{
		fixed_t jetx, jety, jetz;

		jetx = actor->x + P_ReturnThrustX(actor, actor->angle, -FixedMul(64*FRACUNIT, actor->scale));
		jety = actor->y + P_ReturnThrustY(actor, actor->angle, -FixedMul(64*FRACUNIT, actor->scale));
		if (actor->eflags & MFE_VERTICALFLIP)
			jetz = actor->z + actor->height - FixedMul(38*FRACUNIT + mobjinfo[MT_JETFUME1].height, actor->scale);
		else
			jetz = actor->z + FixedMul(38*FRACUNIT, actor->scale);

		filler = P_SpawnMobj(jetx, jety, jetz, MT_JETFUME1);
		P_SetTarget(&filler->target, actor);
		filler->destscale = actor->scale;
		P_SetScale(filler, filler->destscale);
		if (actor->eflags & MFE_VERTICALFLIP)
			filler->flags2 |= MF2_OBJECTFLIP;
		filler->fuse = 56;

		if (actor->eflags & MFE_VERTICALFLIP)
			jetz = actor->z + actor->height - FixedMul(12*FRACUNIT + mobjinfo[MT_JETFUME1].height, actor->scale);
		else
			jetz = actor->z + FixedMul(12*FRACUNIT, actor->scale);

		filler = P_SpawnMobj(jetx + P_ReturnThrustX(actor, actor->angle-ANGLE_90, FixedMul(24*FRACUNIT, actor->scale)),
				jety + P_ReturnThrustY(actor, actor->angle-ANGLE_90, FixedMul(24*FRACUNIT, actor->scale)),
				jetz, MT_JETFUME1);
		P_SetTarget(&filler->target, actor);
		filler->destscale = actor->scale;
		P_SetScale(filler, filler->destscale);
		if (actor->eflags & MFE_VERTICALFLIP)
			filler->flags2 |= MF2_OBJECTFLIP;
		filler->fuse = 57;

		filler = P_SpawnMobj(jetx + P_ReturnThrustX(actor, actor->angle+ANGLE_90, FixedMul(24*FRACUNIT, actor->scale)),
				jety + P_ReturnThrustY(actor, actor->angle+ANGLE_90, FixedMul(24*FRACUNIT, actor->scale)),
				jetz, MT_JETFUME1);
		P_SetTarget(&filler->target, actor);
		filler->destscale = actor->scale;
		P_SetScale(filler, filler->destscale);
		if (actor->eflags & MFE_VERTICALFLIP)
			filler->flags2 |= MF2_OBJECTFLIP;
		filler->fuse = 58;

		P_SetTarget(&actor->tracer, filler);
	}
	else if (locvar1 == 1) // Boss 3 propeller
	{
		fixed_t jetx, jety, jetz;

		jetx = actor->x + P_ReturnThrustX(actor, actor->angle, -FixedMul(60*FRACUNIT, actor->scale));
		jety = actor->y + P_ReturnThrustY(actor, actor->angle, -FixedMul(60*FRACUNIT, actor->scale));
		if (actor->eflags & MFE_VERTICALFLIP)
			jetz = actor->z + actor->height - FixedMul(17*FRACUNIT + mobjinfo[MT_PROPELLER].height, actor->scale);
		else
			jetz = actor->z + FixedMul(17*FRACUNIT, actor->scale);

		filler = P_SpawnMobj(jetx, jety, jetz, MT_PROPELLER);
		P_SetTarget(&filler->target, actor);
		filler->destscale = actor->scale;
		P_SetScale(filler, filler->destscale);
		if (actor->eflags & MFE_VERTICALFLIP)
			filler->flags2 |= MF2_OBJECTFLIP;
		filler->angle = actor->angle - ANGLE_180;

		P_SetTarget(&actor->tracer, filler);
	}
	else if (locvar1 == 2) // Metal Sonic jet fumes
	{
		filler = P_SpawnMobj(actor->x, actor->y, actor->z, MT_JETFUME1);
		P_SetTarget(&filler->target, actor);
		filler->fuse = 59;
		P_SetTarget(&actor->tracer, filler);
		filler->destscale = actor->scale/2;
		P_SetScale(filler, filler->destscale);
		if (actor->eflags & MFE_VERTICALFLIP)
			filler->flags2 |= MF2_OBJECTFLIP;
	}
	else if (locvar1 == 3) // Boss 4 jet flame
	{
		fixed_t jetz;
		if (actor->eflags & MFE_VERTICALFLIP)
			jetz = actor->z + actor->height + FixedMul(50*FRACUNIT - mobjinfo[MT_JETFLAME].height, actor->scale);
		else
			jetz = actor->z - FixedMul(50*FRACUNIT, actor->scale);
		filler = P_SpawnMobj(actor->x, actor->y, jetz, MT_JETFLAME);
		P_SetTarget(&filler->target, actor);
		// Boss 4 already uses its tracer for other things
		filler->destscale = actor->scale;
		P_SetScale(filler, filler->destscale);
		if (actor->eflags & MFE_VERTICALFLIP)
			filler->flags2 |= MF2_OBJECTFLIP;
	}
}

// Function: A_RandomState
//
// Description: Chooses one of the two state numbers supplied randomly.
//
// var1 = state number 1
// var2 = state number 2
//
void A_RandomState(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_RANDOMSTATE, actor))
		return;

	P_SetMobjState(actor, P_RandomChance(PR_RANDOMSTATE, FRACUNIT/2) ? locvar1 : locvar2);
}

// Function: A_RandomStateRange
//
// Description: Chooses a random state within the range supplied.
//
// var1 = Minimum state number to choose.
// var2 = Maximum state number to use.
//
void A_RandomStateRange(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_RANDOMSTATERANGE, actor))
		return;

	P_SetMobjState(actor, P_RandomRange(PR_RANDOMSTATE, locvar1, locvar2));
}

// Function: A_StateRangeByAngle
//
// Description: Chooses a state within the range supplied, depending on the actor's angle.
//
// var1 = Minimum state number to use.
// var2 = Maximum state number to use. The difference will act as a modulo operator.
//
void A_StateRangeByAngle(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_STATERANGEBYANGLE, actor))
		return;

	if (locvar2 - locvar1 < 0)
		return; // invalid range

	P_SetMobjState(actor, locvar1 + (AngleFixed(actor->angle)>>FRACBITS % (1 + locvar2 - locvar1)));
}

// Function: A_StateRangeByParameter
//
// Description: Chooses a state within the range supplied, depending on the actor's parameter/extrainfo value.
//
// var1 = Minimum state number to use.
// var2 = Maximum state number to use. The difference will act as a modulo operator.
//
void A_StateRangeByParameter(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	uint8_t parameter = 0;
	int32_t range = 0;

	if (LUA_CallAction(A_STATERANGEBYPARAMETER, actor))
		return;

	if (locvar2 - locvar1 < 0)
		return; // invalid range

	if (udmf)
	{
		parameter = actor->args[0];
	}
	else if (actor->spawnpoint != NULL)
	{
		// binary format backwards compatibility
		parameter = actor->spawnpoint->extrainfo;
	}

	range = locvar2 - locvar1;
	if (range < 0)
	{
		CONS_Debug(DBG_GAMELOGIC, "A_StateRangeByParameter: invalid range %d (var1: %d, var2: %d)\n", range, locvar1, locvar2);
		return;
	}

	P_SetMobjState(actor, locvar1 + (parameter % (range + 1)));

}

// Function: A_DualAction
//
// Description: Calls two actions. Be careful, if you reference the same state this action is called from, you can create an infinite loop.
//
// var1 = state # to use 1st action from
// var2 = state # to use 2nd action from
//
void A_DualAction(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_DUALACTION, actor))
		return;

	CONS_Debug(DBG_GAMELOGIC, "A_DualAction called from object type %d, var1: %d, var2: %d\n", actor->type, locvar1, locvar2);

	var1 = states[locvar1].var1;
	var2 = states[locvar1].var2;
	astate = &states[locvar1];

	CONS_Debug(DBG_GAMELOGIC, "A_DualAction: Calling First Action (state %d)...\n", locvar1);
	states[locvar1].action(actor);

	var1 = states[locvar2].var1;
	var2 = states[locvar2].var2;
	astate = &states[locvar2];

	CONS_Debug(DBG_GAMELOGIC, "A_DualAction: Calling Second Action (state %d)...\n", locvar2);
	states[locvar2].action(actor);
}

// Function: A_RemoteAction
//
// Description: var1 is the remote object. var2 is the state reference for calling the action called on var1. var1 becomes the actor's target, the action (var2) is called on var1. actor's target is restored
//
// var1 = remote object (-2 uses tracer, -1 uses target)
// var2 = state reference for calling an action
//
void A_RemoteAction(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	mobj_t *originaltarget = actor->target; // Hold on to the target for later.

	if (LUA_CallAction(A_REMOTEACTION, actor))
		return;

	// If >=0, find the closest target.
	if (locvar1 >= 0)
	{
		///* DO A_FINDTARGET STUFF *///
		mobj_t *targetedmobj = NULL;
		thinker_t *th;
		mobj_t *mo2;
		fixed_t dist1 = 0, dist2 = 0;

		// scan the thinkers
		for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
		{
			if (th->function == (actionf_p1)P_RemoveThinkerDelayed)
				continue;

			mo2 = (mobj_t *)th;

			if (mo2->type == (mobjtype_t)locvar1)
			{
				if (targetedmobj == NULL)
				{
					targetedmobj = mo2;
					dist2 = R_PointToDist2(actor->x, actor->y, mo2->x, mo2->y);
				}
				else
				{
					dist1 = R_PointToDist2(actor->x, actor->y, mo2->x, mo2->y);

					if ((locvar2 && dist1 < dist2) || (!locvar2 && dist1 > dist2))
					{
						targetedmobj = mo2;
						dist2 = dist1;
					}
				}
			}
		}

		if (!targetedmobj)
		{
			CONS_Debug(DBG_GAMELOGIC, "A_RemoteAction: Unable to find the specified object to target.\n");
			return; // Oops, nothing found..
		}

		CONS_Debug(DBG_GAMELOGIC, "A_RemoteAction: Found a target.\n");

		P_SetTarget(&actor->target, targetedmobj);

		///* END A_FINDTARGET STUFF *///
	}

	// If -2, use the tracer as the target
	else if (locvar1 == -2)
		P_SetTarget(&actor->target, actor->tracer);
	// if -1 or anything else, just use the target.

	if (actor->target)
	{
		// Steal the var1 and var2 from "locvar2"
		var1 = states[locvar2].var1;
		var2 = states[locvar2].var2;
		astate = &states[locvar2];

		CONS_Debug(DBG_GAMELOGIC, "A_RemoteAction: Calling action on %p\n"
				"var1 is %d\nvar2 is %d\n", (void*)actor->target, var1, var2);
		states[locvar2].action(actor->target);
	}

	P_SetTarget(&actor->target, originaltarget); // Restore the original target.
}

// Function: A_ToggleFlameJet
//
// Description: Turns a flame jet on and off.
//
// var1 = unused
// var2 = unused
//
void A_ToggleFlameJet(void* thing)
{
	mobj_t *actor = thing;
	if (LUA_CallAction(A_TOGGLEFLAMEJET, actor))
		return;

	// threshold - off delay
	// movecount - on timer

	if (actor->flags2 & MF2_FIRING)
	{
		actor->flags2 &= ~MF2_FIRING;

		if (actor->threshold)
			actor->tics = actor->threshold;
	}
	else
	{
		actor->flags2 |= MF2_FIRING;

		if (actor->movecount)
			actor->tics = actor->movecount;
	}
}

// Function: A_OrbitNights
//
// Description: Used by Chaos Emeralds to orbit around Nights (aka Super Sonic.)
//
// var1 = Angle adjustment (aka orbit speed)
// var2:
//        Bits 1-10: height offset, max 1023
//        Bits 11-16: X radius factor (max 63, default 20)
//        Bit 17: set if object is Nightopian Helper
//        Bit 18: set to define X/Y/Z rotation factor
//        Bits 19-20: Unused
//        Bits 21-26: Y radius factor (max 63, default 32)
//        Bits 27-32: Z radius factor (max 63, default 32)
//
// If MF_GRENADEBOUNCE is flagged on mobj, use actor->threshold to define X/Y/Z radius factor, max 1023 each:
//        Bits 1-10: X factor
//        Bits 11-20: Y factor
//        Bits 21-30: Z factor
void A_OrbitNights(void* thing)
{
	mobj_t *actor = thing;
	int32_t ofs = (var2 & 0x3FF);
	dboolean ishelper = (var2 & 0x10000);
	dboolean donotrescale = (var2 & 0x40000);
	int32_t xfactor = 32, yfactor = 32, zfactor = 20;

	(void)ishelper;

	if (LUA_CallAction(A_ORBITNIGHTS, actor))
		return;

	if (actor->flags & MF_GRENADEBOUNCE)
	{
		xfactor = (actor->threshold & 0x3FF);
		yfactor = (actor->threshold & 0xFFC00) >> 10;
		zfactor = (actor->threshold & 0x3FF00000) >> 20;
	}
	else if (var2 & 0x20000)
	{
		xfactor = (var2 & 0xFC00) >> 10;
		yfactor = (var2 & 0x3F00000) >> 20;
		zfactor = (var2 & 0xFC000000) >> 26;
	}

	if (!actor->target)
	{
		if (cht_debug && !(actor->target && actor->target->player))
			CONS_Printf("ERROR: Powerup has no target!\n");
		return;
	}
	else
	{
		actor->extravalue1 += var1;
		P_UnsetThingPosition(actor);
		{
			const angle_t fa  = (angle_t)actor->extravalue1 >> ANGLETOFINESHIFT;
			const angle_t ofa = ((angle_t)actor->extravalue1 + (ofs*ANG1)) >> ANGLETOFINESHIFT;

			const fixed_t fc = FixedMul(FINECOSINE(fa),FixedMul(xfactor*FRACUNIT, actor->scale));
			const fixed_t fh = FixedMul(FINECOSINE(ofa),FixedMul(zfactor*FRACUNIT, actor->scale));
			const fixed_t fs = FixedMul(FINESINE(fa),FixedMul(yfactor*FRACUNIT, actor->scale));

			actor->x = actor->target->x + fc;
			actor->y = actor->target->y + fs;
			actor->z = actor->target->z + fh + FixedMul(16*FRACUNIT, actor->scale);

			// Semi-lazy hack
			actor->angle = (angle_t)actor->extravalue1 + ANGLE_90;
		}
		P_SetThingPosition(actor);

		if (!donotrescale && actor->destscale != actor->target->destscale)
			actor->destscale = actor->target->destscale;
	}
}

// Function: A_GhostMe
//
// Description: Spawns a "ghost" mobj of this actor, ala spindash trails and the minus's digging "trails"
//
// var1 = duration in tics
// var2 = unused
//
void A_GhostMe(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	mobj_t *ghost;

	if (LUA_CallAction(A_GHOSTME, actor))
		return;

	ghost = P_SpawnGhostMobj(actor);
	if (ghost && locvar1 > 0)
		ghost->fuse = locvar1;
}

// Function: A_SetObjectState
//
// Description: Changes the state of an object's target/tracer.
//
// var1 = state number
// var2:
//		0 = target
//		1 = tracer
//
void A_SetObjectState(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	mobj_t *target;

	if (LUA_CallAction(A_SETOBJECTSTATE, actor))
		return;

	if ((!locvar2 && !actor->target) || (locvar2 && !actor->tracer))
	{
		if (cht_debug)
			CONS_Printf("A_SetObjectState: No target to change state!\n");
		return;
	}

	if (!locvar2) // target
		target = actor->target;
	else // tracer
		target = actor->tracer;

	if (target->health > 0)
	{
		if (!target->player)
			P_SetMobjState(target, locvar1);
		else
			P_SetPlayerMobjState(target, locvar1);
	}
}

// Function: A_SetObjectTypeState
//
// Description: Changes the state of all active objects of a certain type in a certain range of the actor.
//
// var1 = state number
// var2:
//		lower 16 bits = type
//		upper 16 bits = range (if == 0, across whole map)
//
void A_SetObjectTypeState(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	const uint16_t loc2lw = (uint16_t)(locvar2 & 65535);
	const uint16_t loc2up = (uint16_t)(locvar2 >> 16);

	thinker_t *th;
	mobj_t *mo2;
	fixed_t dist = 0;

	if (LUA_CallAction(A_SETOBJECTTYPESTATE, actor))
		return;

	for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
	{
		if (th->function == (actionf_p1)P_RemoveThinkerDelayed)
			continue;

		mo2 = (mobj_t *)th;

		if (mo2->type == (mobjtype_t)loc2lw)
		{
			dist = P_AproxDistance(mo2->x - actor->x, mo2->y - actor->y);

			if (mo2->health > 0)
			{
				if (loc2up == 0)
					P_SetMobjState(mo2, locvar1);
				else
				{
					if (dist <= FixedMul(loc2up*FRACUNIT, actor->scale))
						P_SetMobjState(mo2, locvar1);
				}
			}
		}
	}
}

// Function: A_KnockBack
//
// Description: Knocks back the object's target at its current speed.
//
// var1:
//		0 = target
//		1 = tracer
// var2 = unused
//
void A_KnockBack(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	mobj_t *target;

	if (LUA_CallAction(A_KNOCKBACK, actor))
		return;

	if (!locvar1)
		target = actor->target;
	else
		target = actor->tracer;

	if (!target)
	{
		if(cht_debug)
			CONS_Printf("A_KnockBack: No target!\n");
		return;
	}

	target->momx *= -1;
	target->momy *= -1;
}

// Function: A_PushAway
//
// Description: Pushes an object's target away from the calling object.
//
// var1 = amount of force
// var2:
//		lower 16 bits = If 1, xy momentum is lost. If 0, xy momentum is kept
//		upper 16 bits = 0 - target, 1 - tracer
//
void A_PushAway(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	mobj_t *target; // target
	angle_t an; // actor to target angle

	if (LUA_CallAction(A_PUSHAWAY, actor))
		return;

	if ((!(locvar2 >> 16) && !actor->target) || ((locvar2 >> 16) && !actor->tracer))
		return;

	if (!locvar1)
		CONS_Printf("A_Thrust: Var1 not specified!\n");

	if (!(locvar2 >> 16)) // target
		target = actor->target;
	else // tracer
		target = actor->tracer;

	an = R_PointToAngle2(actor->x, actor->y, target->x, target->y);

	if (locvar2 & 65535)
		P_InstaThrust(target, an, FixedMul(locvar1*FRACUNIT, actor->scale));
	else
		P_Thrust(target, an, FixedMul(locvar1*FRACUNIT, actor->scale));
}

// Function: A_RingDrain
//
// Description: Drain targeted player's rings.
//
// var1 = ammount of drained rings
// var2 = unused
//
void A_RingDrain(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	player_t *player;

	if (LUA_CallAction(A_RINGDRAIN, actor))
		return;

	if (!actor->target || !actor->target->player)
	{
		if(cht_debug)
			CONS_Printf("A_RingDrain: No player targeted!\n");
		return;
	}

	player = actor->target->player;
	P_GivePlayerRings(player, -min(locvar1, player->rings));
}

// Function: A_SplitShot
//
// Description: Shoots 2 missiles that hit next to the player.
//
// var1 = target x-y-offset
// var2:
//		lower 16 bits = missile type
//		upper 16 bits = height offset
//
void A_SplitShot(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	const uint16_t loc2lw = (uint16_t)(locvar2 & 65535);
	const uint16_t loc2up = (uint16_t)(locvar2 >> 16);
	const fixed_t offs = (fixed_t)(locvar1*FRACUNIT);
	const fixed_t hoffs = (fixed_t)(loc2up*FRACUNIT);

	if (LUA_CallAction(A_SPLITSHOT, actor))
		return;

	if (!actor->target)
		return;

	A_FaceTarget(actor);
	{
		const angle_t an = (actor->angle + ANGLE_90) >> ANGLETOFINESHIFT;
		const fixed_t fasin = FINESINE(an);
		const fixed_t facos = FINECOSINE(an);
		fixed_t xs = FixedMul(facos,FixedMul(offs, actor->scale));
		fixed_t ys = FixedMul(fasin,FixedMul(offs, actor->scale));
		fixed_t z;

		if (actor->eflags & MFE_VERTICALFLIP)
			z = actor->z + actor->height - FixedMul(hoffs, actor->scale);
		else
			z = actor->z + FixedMul(hoffs, actor->scale);

		P_SpawnPointMissile(actor, actor->target->x+xs, actor->target->y+ys, actor->target->z, loc2lw, actor->x, actor->y, z);
		P_SpawnPointMissile(actor, actor->target->x-xs, actor->target->y-ys, actor->target->z, loc2lw, actor->x, actor->y, z);
	}
}

// Function: A_MissileSplit
//
// Description: If the object is a missile it will create a new missile with an alternate flight path owned by the one who shot the former missile.
//
// var1 = splitting missile type
// var2 = splitting angle
//
void A_MissileSplit(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_MISSILESPLIT, actor))
		return;

	if (actor->eflags & MFE_VERTICALFLIP)
		P_SpawnAlteredDirectionMissile(actor, locvar1, actor->x, actor->y, actor->z+actor->height, locvar2);
	else
		P_SpawnAlteredDirectionMissile(actor, locvar1, actor->x, actor->y, actor->z, locvar2);
}

// Function: A_MultiShot
//
// Description: Shoots objects horizontally that spread evenly in all directions.
//
// var1:
//		lower 16 bits = number of missiles
//		upper 16 bits = missile type #
// var2 = height offset
//
void A_MultiShot(void *thing)
{
	mobj_t *actor = thing;
	fixed_t z, xr, yr;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	const uint16_t loc1lw = (uint16_t)(locvar1 & 65535);
	const uint16_t loc1up = (uint16_t)(locvar1 >> 16);
	int32_t count = 0;
	fixed_t ad;

	if (LUA_CallAction(A_MULTISHOT, actor))
		return;

	if (actor->target)
		A_FaceTarget(actor);

	if(loc1lw > 90)
		ad = FixedMul(90*FRACUNIT, actor->scale);
	else
		ad = FixedMul(loc1lw*FRACUNIT, actor->scale);

	if (actor->eflags & MFE_VERTICALFLIP)
		z = actor->z + actor->height - FixedMul(48*FRACUNIT + locvar2*FRACUNIT, actor->scale);
	else
		z = actor->z + FixedMul(48*FRACUNIT + locvar2*FRACUNIT, actor->scale);
	xr = FixedMul((P_SignedRandom(PR_ACTION)/3)<<FRACBITS, actor->scale); // please note p_mobj.c's P_Rand() abuse
	yr = FixedMul((P_SignedRandom(PR_ACTION)/3)<<FRACBITS, actor->scale); // of rand(), RAND_MAX and signness mess

	while(count <= loc1lw && loc1lw >= 1)
	{
		const angle_t fa = FixedAngleC(count*FRACUNIT*360, ad)>>ANGLETOFINESHIFT;
		const fixed_t rc = FINECOSINE(fa);
		const fixed_t rs = FINESINE(fa);
		const fixed_t xrc = FixedMul(xr, rc);
		const fixed_t yrs = FixedMul(yr, rs);
		const fixed_t xrs = FixedMul(xr, rs);
		const fixed_t yrc = FixedMul(yr, rc);

		P_SpawnPointMissile(actor, xrc-yrs+actor->x, xrs+yrc+actor->y, z, loc1up, actor->x, actor->y, z);
		count++;
	}

	if (!(actor->flags & MF_BOSS))
	{
		if (ultimatemode)
			actor->reactiontime = actor->info->reactiontime*TICRATE;
		else
			actor->reactiontime = actor->info->reactiontime*TICRATE*2;
	}
}

// Function: A_InstaLoop
//
// Description: Makes the object move along a 2d (view angle, z) polygon.
//
// var1:
//		lower 16 bits = current step
//		upper 16 bits = maximum step #
// var2 = force
//
void A_InstaLoop(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	fixed_t force = max(locvar2, 1)*FRACUNIT; // defaults to 1 if var2 < 1
	const uint16_t loc1lw = (uint16_t)(locvar1 & 65535);
	const uint16_t loc1up = (uint16_t)(locvar1 >> 16);
	const angle_t fa = FixedAngleC(loc1lw*FRACUNIT*360, loc1up*FRACUNIT)>>ANGLETOFINESHIFT;
	const fixed_t ac = FINECOSINE(fa);
	const fixed_t as = FINESINE(fa);

	if (LUA_CallAction(A_INSTALOOP, actor))
		return;

	P_InstaThrust(actor, actor->angle, FixedMul(ac, FixedMul(force, actor->scale)));
	P_SetObjectMomZ(actor, FixedMul(as, force), false);
}

// Function: A_Custom3DRotate
//
// Description: Rotates the actor around its target in 3 dimensions.
//
// var1:
//		lower 16 bits = radius in fracunits
//		upper 16 bits = vertical offset
// var2:
//		lower 16 bits = vertical rotation speed in 1/10 fracunits per tic
//		upper 16 bits = horizontal rotation speed in 1/10 fracunits per tic
//
void A_Custom3DRotate(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	const uint16_t loc1lw = (uint16_t)(locvar1 & 65535);
	const uint16_t loc1up = (uint16_t)(locvar1 >> 16);
	const uint16_t loc2lw = (uint16_t)(locvar2 & 65535);
	const uint16_t loc2up = (uint16_t)(locvar2 >> 16);

	const fixed_t radius = FixedMul(loc1lw*FRACUNIT, actor->scale);
	const fixed_t hOff = FixedMul(loc1up*FRACUNIT, actor->scale);
	const fixed_t hspeed = loc2up*FRACUNIT/10; // Monster's note (29/05/21): DO NOT SCALE, this is an angular speed!
	const fixed_t vspeed = loc2lw*FRACUNIT/10; // ditto

	if (LUA_CallAction(A_CUSTOM3DROTATE, actor))
		return;

	if (!actor->target) // Ensure we actually have a target first.
	{
		CONS_Printf("Error: A_Custom3DRotate: Object has no target.\n");
		P_RemoveMobj(actor);
		return;
	}

	if (actor->target->health == 0)
	{
		P_RemoveMobj(actor);
		return;
	}

	if (hspeed==0 && vspeed==0)
	{
		if (cht_debug)
			CONS_Printf("Error: A_Custom3DRotate: Object has no speed.\n");
		return;
	}

	actor->angle += FixedAngle(hspeed);
	actor->movedir += FixedAngle(vspeed);
	P_UnsetThingPosition(actor);
	{
		const angle_t fa = actor->angle>>ANGLETOFINESHIFT;

		if (vspeed == 0 && hspeed != 0)
		{
			actor->x = actor->target->x + FixedMul(FINECOSINE(fa),radius);
			actor->y = actor->target->y + FixedMul(FINESINE(fa),radius);
			actor->z = actor->target->z + actor->target->height/2 - actor->height/2 + hOff;
		}
		else
		{
			const angle_t md = actor->movedir>>ANGLETOFINESHIFT;
			actor->x = actor->target->x + FixedMul(FixedMul(FINESINE(md),FINECOSINE(fa)),radius);
			actor->y = actor->target->y + FixedMul(FixedMul(FINESINE(md),FINESINE(fa)),radius);
			actor->z = actor->target->z + FixedMul(FINECOSINE(md),radius) + actor->target->height/2 - actor->height/2 + hOff;
		}
	}
	P_SetThingPosition(actor);
}

// Function: A_SearchForPlayers
//
// Description: Checks if the actor has targeted a vulnerable player. If not a new player will be searched all around. If no players are available the object can call a specific state. (Useful for not moving enemies)
//
// var1:
//		if var1 == 0, if necessary call state with same state number as var2
//		else, do not call a specific state if no players are available
// var2 = state number
//
void A_SearchForPlayers(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_SEARCHFORPLAYERS, actor))
		return;

	if (!actor->target || !(actor->target->flags & MF_SHOOTABLE))
	{
		// look for a new target
		if (P_LookForPlayers(actor, true, false, 0))
			return; // got a new target

		if(locvar1==0)
		{
			P_SetMobjStateNF(actor, locvar2);
			return;
		}
	}
}

// Function: A_CheckRandom
//
// Description: Calls a state by chance.
//
// var1:
//		lower 16 bits = denominator
//		upper 16 bits = numerator (defaults to 1 if zero)
// var2 = state number
//
void A_CheckRandom(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	fixed_t chance = FRACUNIT;

	if (LUA_CallAction(A_CHECKRANDOM, actor))
		return;

	if ((locvar1 & 0xFFFF) == 0)
		return;

	// The PRNG doesn't suck anymore, OK?
	if (locvar1 >> 16)
		chance *= (locvar1 >> 16);
	chance /= (locvar1 & 0xFFFF);

	if (P_RandomChance(PR_ACTION, chance))
		P_SetMobjState(actor, locvar2);
}

// Function: A_CheckTargetRings
//
// Description: Calls a state depending on the ammount of rings currently owned by targeted players.
//
// var1 = if player rings >= var1 call state
// var2 = state number
//
void A_CheckTargetRings(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_CHECKTARGETRINGS, actor))
		return;

	if (!(actor->target) || !(actor->target->player))
	   return;

	if (actor->target->player->rings >= locvar1)
		P_SetMobjState(actor, locvar2);
}

// Function: A_CheckRings
//
// Description: Calls a state depending on the ammount of rings currently owned by all players.
//
// var1 = if player rings >= var1 call state
// var2 = state number
//
void A_CheckRings(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	int32_t i, cntr = 0;

	if (LUA_CallAction(A_CHECKRINGS, actor))
		return;

	for (i = 0; i < MAXPLAYERS; i++)
		cntr += players[i].rings;

	if (cntr >= locvar1)
		P_SetMobjState(actor, locvar2);
}

// Function: A_CheckTotalRings
//
// Description: Calls a state depending on the maximum ammount of rings owned by all players during this try.
//
// var1 = if total player rings >= var1 call state
// var2 = state number
//
void A_CheckTotalRings(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	int32_t i, cntr = 0;

	if (LUA_CallAction(A_CHECKTOTALRINGS, actor))
		return;

	for (i = 0; i < MAXPLAYERS; i++)
		cntr += players[i].totalring;

	if (cntr >= locvar1)
		P_SetMobjState(actor, locvar2);
}

// Function: A_CheckHealth
//
// Description: Calls a state depending on the object's current health.
//
// var1 = if health <= var1 call state
// var2 = state number
//
void A_CheckHealth(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_CHECKHEALTH, actor))
		return;

	if (actor->health <= locvar1)
		P_SetMobjState(actor, locvar2);
}

// Function: A_CheckRange
//
// Description: Calls a state if the object's target is in range.
//
// var1:
//		lower 16 bits = range
//		upper 16 bits = 0 - target, 1 - tracer
// var2 = state number
//
void A_CheckRange(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	fixed_t dist;

	if (LUA_CallAction(A_CHECKRANGE, actor))
		return;

	if ((!(locvar1 >> 16) && !actor->target) || ((locvar1 >> 16) && !actor->tracer))
		return;

	if (!(locvar1 >> 16)) //target
		dist = P_AproxDistance(actor->target->x - actor->x, actor->target->y - actor->y);
	else //tracer
		dist = P_AproxDistance(actor->tracer->x - actor->x, actor->tracer->y - actor->y);

	if (dist <= FixedMul((locvar1 & 65535)*FRACUNIT, actor->scale))
		P_SetMobjState(actor, locvar2);
}

// Function: A_CheckHeight
//
// Description: Calls a state if the object and it's target have a height offset <= var1 compared to each other.
//
// var1:
//		lower 16 bits = height offset
//		upper 16 bits = 0 - target, 1 - tracer
// var2 = state number
//
void A_CheckHeight(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	fixed_t height;

	if (LUA_CallAction(A_CHECKHEIGHT, actor))
		return;

	if ((!(locvar1 >> 16) && !actor->target) || ((locvar1 >> 16) && !actor->tracer))
		return;

	if (!(locvar1 >> 16)) // target
		height = abs(actor->target->z - actor->z);
	else // tracer
		height = abs(actor->tracer->z - actor->z);

	if (height <= FixedMul((locvar1 & 65535)*FRACUNIT, actor->scale))
		P_SetMobjState(actor, locvar2);
}

// Function: A_CheckTrueRange
//
// Description: Calls a state if the object's target is in true range. (Checks height, too.)
//
// var1:
//		lower 16 bits = range
//		upper 16 bits = 0 - target, 1 - tracer
// var2 = state number
//
void A_CheckTrueRange(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	fixed_t height; // vertical range
	fixed_t dist; // horizontal range
	fixed_t l; // true range

	if (LUA_CallAction(A_CHECKTRUERANGE, actor))
		return;

	if ((!(locvar1 >> 16) && !actor->target) || ((locvar1 >> 16) && !actor->tracer))
		return;

	if (!(locvar1 >> 16)) // target
	{
		height = actor->target->z - actor->z;
		dist = P_AproxDistance(actor->target->x - actor->x, actor->target->y - actor->y);

	}
	else // tracer
	{
		height = actor->tracer->z - actor->z;
		dist = P_AproxDistance(actor->tracer->x - actor->x, actor->tracer->y - actor->y);
	}

	l = P_AproxDistance(dist, height);

	if (l <= FixedMul((locvar1 & 65535)*FRACUNIT, actor->scale))
		P_SetMobjState(actor, locvar2);

}

// Function: A_CheckThingCount
//
// Description: Calls a state depending on the number of active things in range.
//
// var1:
//		lower 16 bits = number of things
//		upper 16 bits = thing type
// var2:
//		lower 16 bits = state to call
//		upper 16 bits = range (if == 0, check whole map)
//
void A_CheckThingCount(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	const uint16_t loc1lw = (uint16_t)(locvar1 & 65535);
	const uint16_t loc1up = (uint16_t)(locvar1 >> 16);
	const uint16_t loc2lw = (uint16_t)(locvar2 & 65535);
	const uint16_t loc2up = (uint16_t)(locvar2 >> 16);

	int32_t count = 0;
	thinker_t *th;
	mobj_t *mo2;
	fixed_t dist = 0;

	if (LUA_CallAction(A_CHECKTHINGCOUNT, actor))
		return;

	for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
	{
		if (th->function == (actionf_p1)P_RemoveThinkerDelayed)
			continue;

		mo2 = (mobj_t *)th;

		if (mo2->type == (mobjtype_t)loc1up)
		{
			dist = P_AproxDistance(mo2->x - actor->x, mo2->y - actor->y);

			if (loc2up == 0)
				count++;
			else
			{
				if (dist <= FixedMul(loc2up*FRACUNIT, actor->scale))
					count++;
			}
		}
	}

	if(loc1lw <= count)
		P_SetMobjState(actor, loc2lw);
}

// Function: A_CheckAmbush
//
// Description: Calls a state if the actor is behind its targeted player.
//
// var1:
//		0 = target
//		1 = tracer
// var2 = state number
//
void A_CheckAmbush(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	angle_t at; // angle target is currently facing
	angle_t atp; // actor to target angle
	angle_t an; // angle between at and atp

	if (LUA_CallAction(A_CHECKAMBUSH, actor))
		return;

	if ((!locvar1 && !actor->target) || (locvar1 && !actor->tracer))
		return;

	if (!locvar1) // target
	{
		at = actor->target->angle;
		atp = R_PointToAngle2(actor->x, actor->y, actor->target->x, actor->target->y);
	}
	else // tracer
	{
		at = actor->tracer->angle;
		atp = R_PointToAngle2(actor->x, actor->y, actor->tracer->x, actor->tracer->y);
	}

	an = atp - at;

	if (an > ANGLE_180) // flip angle if bigger than 180
		an = InvAngle(an);

	if (an < ANGLE_90+ANGLE_22h) // within an angle of 112.5 from each other?
		P_SetMobjState(actor, locvar2);
}

// Function: A_CheckCustomValue
//
// Description: Calls a state depending on the object's custom value.
//
// var1 = if custom value >= var1, call state
// var2 = state number
//
void A_CheckCustomValue(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_CHECKCUSTOMVALUE, actor))
		return;

	if (actor->cusval >= locvar1)
		P_SetMobjState(actor, locvar2);
}

// Function: A_CheckCusValMemo
//
// Description: Calls a state depending on the object's custom memory value.
//
// var1 = if memory value >= var1, call state
// var2 = state number
//
void A_CheckCusValMemo(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_CHECKCUSVALMEMO, actor))
		return;

	if (actor->cvmem >= locvar1)
		P_SetMobjState(actor, locvar2);
}

// Function: A_SetCustomValue
//
// Description: Changes the custom value of an object.
//
// var1 = manipulating value
// var2:
//      if var2 == 5, multiply the custom value by var1
//      else if var2 == 4, divide the custom value by var1
//      else if var2 == 3, apply modulo var1 to the custom value
//      else if var2 == 2, add var1 to the custom value
//      else if var2 == 1, substract var1 from the custom value
//      else if var2 == 0, replace the custom value with var1
//
void A_SetCustomValue(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_SETCUSTOMVALUE, actor))
		return;

	if (cht_debug)
		CONS_Printf("Init custom value is %d\n", actor->cusval);

	if (locvar1 == 0 && (locvar2 == 3 || locvar2 == 4))
		return; // DON'T DIVIDE BY ZERO

	// no need for a "temp" value here, just modify the cusval directly
	if (locvar2 == 5) // multiply
		actor->cusval *= locvar1;
	else if (locvar2 == 4) // divide
		actor->cusval /= locvar1;
	else if (locvar2 == 3) // modulo
		actor->cusval %= locvar1;
	else if (locvar2 == 2) // add
		actor->cusval += locvar1;
	else if (locvar2 == 1) // subtract
		actor->cusval -= locvar1;
	else // replace
		actor->cusval = locvar1;

	if(cht_debug)
		CONS_Printf("New custom value is %d\n", actor->cusval);
}

// Function: A_UseCusValMemo
//
// Description: Memorizes or recalls a current custom value.
//
// var1:
//      if var1 == 1, manipulate memory value
//      else, recall memory value replacing the custom value
// var2:
//      if var2 == 5, mem = mem*cv  ||  cv = cv*mem
//      else if var2 == 4,  mem = mem/cv  ||  cv = cv/mem
//      else if var2 == 3, mem = mem%cv  ||  cv = cv%mem
//      else if var2 == 2, mem += cv  ||  cv += mem
//      else if var2 == 1,  mem -= cv  ||  cv -= mem
//      else mem = cv  ||  cv = mem
//
void A_UseCusValMemo(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	int32_t temp = actor->cusval; // value being manipulated
	int32_t tempM = actor->cvmem; // value used to manipulate temp with

	if (LUA_CallAction(A_USECUSVALMEMO, actor))
		return;

	if (locvar1 == 1) // cvmem being changed using cusval
	{
		temp = actor->cvmem;
		tempM = actor->cusval;
	}
	else // cusval being changed with cvmem
	{
		temp = actor->cusval;
		tempM = actor->cvmem;
	}

	if (locvar1 == 0 && (locvar2 == 3 || locvar2 == 4))
		return; // DON'T DIVIDE BY ZERO

	// now get new value for cusval/cvmem using the other
	if (locvar2 == 5) // multiply
		temp *= tempM;
	else if (locvar2 == 4) // divide
		temp /= tempM;
	else if (locvar2 == 3) // modulo
		temp %= tempM;
	else if (locvar2 == 2) // add
		temp += tempM;
	else if (locvar2 == 1) // subtract
		temp -= tempM;
	else // replace
		temp = tempM;

	// finally, give cusval/cvmem the new value!
	if (locvar1 == 1)
		actor->cvmem = temp;
	else
		actor->cusval = temp;
}

// Function: A_RelayCustomValue
//
// Description: Manipulates the custom value of the object's target/tracer.
//
// var1:
//		lower 16 bits:
//					if var1 == 0, use own custom value
//					else, use var1 value
//		upper 16 bits = 0 - target, 1 - tracer
// var2:
//      if var2 == 5, multiply the target's custom value by var1
//      else if var2 == 4, divide the target's custom value by var1
//      else if var2 == 3, apply modulo var1 to the target's custom value
//      else if var2 == 2, add var1 to the target's custom value
//      else if var2 == 1, substract var1 from the target's custom value
//      else if var2 == 0, replace the target's custom value with var1
//
void A_RelayCustomValue(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	int32_t temp; // reference value - var1 lower 16 bits changes this
	int32_t tempT; // target's value - changed to tracer if var1 upper 16 bits set, then modified to become final value

	if (LUA_CallAction(A_RELAYCUSTOMVALUE, actor))
		return;

	if ((!(locvar1 >> 16) && !actor->target) || ((locvar1 >> 16) && !actor->tracer))
		return;

	// reference custom value
	if ((locvar1 & 65535) == 0)
		temp = actor->cusval; // your own custom value
	else
		temp = (locvar1 & 65535); // var1 value

	if (!(locvar1 >> 16)) // target's custom value
		tempT = actor->target->cusval;
	else // tracer's custom value
		tempT = actor->tracer->cusval;

	if (locvar1 == 0 && (locvar2 == 3 || locvar2 == 4))
		return; // DON'T DIVIDE BY ZERO

	// now get new cusval using target's and the reference
	if (locvar2 == 5) // multiply
		tempT *= temp;
	else if (locvar2 == 4) // divide
		tempT /= temp;
	else if (locvar2 == 3) // modulo
		tempT %= temp;
	else if (locvar2 == 2) // add
		tempT += temp;
	else if (locvar2 == 1) // subtract
		tempT -= temp;
	else // replace
		tempT = temp;

	// finally, give target/tracer the new cusval!
	if (!(locvar1 >> 16)) // target
		actor->target->cusval = tempT;
	else // tracer
		actor->tracer->cusval = tempT;
}

// Function: A_CusValAction
//
// Description: Calls an action from a reference state applying custom value parameters.
//
// var1 = state # to use action from
// var2:
//      if var2 == 5, only replace new action's var2 with memory value
//      else if var2 == 4, only replace new action's var1 with memory value
//      else if var2 == 3, replace new action's var2 with custom value and var1 with memory value
//      else if var2 == 2, replace new action's var1 with custom value and var2 with memory value
//      else if var2 == 1, only replace new action's var2 with custom value
//      else if var2 == 0, only replace new action's var1 with custom value
//
void A_CusValAction(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_CUSVALACTION, actor))
		return;

	if (locvar2 == 5)
	{
		var1 = states[locvar1].var1;
		var2 = (int32_t)actor->cvmem;
	}
	else if (locvar2 == 4)
	{
		var1 = (int32_t)actor->cvmem;
		var2 = states[locvar1].var2;
	}
	else if (locvar2 == 3)
	{
		var1 = (int32_t)actor->cvmem;
		var2 = (int32_t)actor->cusval;
	}
	else if (locvar2 == 2)
	{
		var1 = (int32_t)actor->cusval;
		var2 = (int32_t)actor->cvmem;
	}
	else if (locvar2 == 1)
	{
		var1 = states[locvar1].var1;
		var2 = (int32_t)actor->cusval;
	}
	else
	{
		var1 = (int32_t)actor->cusval;
		var2 = states[locvar1].var2;
	}

	astate = &states[locvar1];
	states[locvar1].action(actor);
}

// Function: A_ForceStop
//
// Description: Actor immediately stops its current movement.
//
// var1:
//      if var1 == 0, stop x-y-z-movement
//      else, stop x-y-movement only
// var2 = unused
//
void A_ForceStop(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;

	if (LUA_CallAction(A_FORCESTOP, actor))
		return;

	actor->momx = actor->momy = 0;
	if (locvar1 == 0)
		actor->momz = 0;
}

// Function: A_ForceWin
//
// Description: Makes all players win the level.
//
// var1 = unused
// var2 = unused
//
void A_ForceWin(void *thing)
{
	mobj_t *actor = thing;
	int32_t i;

	if (LUA_CallAction(A_FORCEWIN, actor))
		return;

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (playeringame[i] && ((players[i].mo && players[i].mo->health)
		    && !(players[i].pflags & PF_NOCONTEST)))
			break;
	}

	if (i == MAXPLAYERS)
		return;

	P_DoAllPlayersExit(0, false);
}

// Function: A_SpikeRetract
//
// Description: Toggles actor solid flag.
//
// var1:
//        if var1 == 0, actor no collide
//        else, actor solid
// var2 = unused
//
void A_SpikeRetract(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;

	if (LUA_CallAction(A_SPIKERETRACT, actor))
		return;

	if (actor->flags & MF_NOBLOCKMAP)
		return;

	if (locvar1 == 0)
	{
		actor->flags &= ~MF_SOLID;
		actor->flags |= MF_NOCLIPTHING;
	}
	else
	{
		actor->flags |= MF_SOLID;
		actor->flags &= ~MF_NOCLIPTHING;
	}
	if (actor->flags & MF_SOLID)
		P_CheckPosition(actor, actor->x, actor->y, NULL);
}

// Function: A_InfoState
//
// Description: Set mobj state to one predefined in mobjinfo.
//
// var1:
//        if var1 == 0, set actor to spawnstate
//        else if var1 == 1, set actor to seestate
//        else if var1 == 2, set actor to meleestate
//        else if var1 == 3, set actor to missilestate
//        else if var1 == 4, set actor to deathstate
//        else if var1 == 5, set actor to xdeathstate
//        else if var1 == 6, set actor to raisestate
// var2 = unused
//
void A_InfoState(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	switch (locvar1)
	{
	case 0:
		if (actor->state != &states[actor->info->spawnstate])
			P_SetMobjState(actor, actor->info->spawnstate);
		break;
	case 1:
		if (actor->state != &states[actor->info->seestate])
			P_SetMobjState(actor, actor->info->seestate);
		break;
	case 2:
		if (actor->state != &states[actor->info->meleestate])
			P_SetMobjState(actor, actor->info->meleestate);
		break;
	case 3:
		if (actor->state != &states[actor->info->missilestate])
			P_SetMobjState(actor, actor->info->missilestate);
		break;
	case 4:
		if (actor->state != &states[actor->info->deathstate])
			P_SetMobjState(actor, actor->info->deathstate);
		break;
	case 5:
		if (actor->state != &states[actor->info->xdeathstate])
			P_SetMobjState(actor, actor->info->xdeathstate);
		break;
	case 6:
		if (actor->state != &states[actor->info->raisestate])
			P_SetMobjState(actor, actor->info->raisestate);
		break;
	default:
		break;
	}
}

// Function: A_Repeat
//
// Description: Returns to state var2 until animation has been used var1 times, then continues to nextstate.
//
// var1 = repeat count
// var2 = state to return to if extravalue2 > 0
//
void A_Repeat(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_REPEAT, actor))
		return;

	if (locvar1 && (!actor->extravalue2 || actor->extravalue2 > locvar1))
		actor->extravalue2 = locvar1;

	if (--actor->extravalue2 > 0)
		P_SetMobjState(actor, locvar2);
}

// Function: A_SetScale
//
// Description: Changes the scale of the actor or its target/tracer
//
// var1 = new scale (1*FRACUNIT = 100%)
// var2:
//        upper 16 bits: 0 = actor, 1 = target, 2 = tracer
//        lower 16 bits: 0 = instant change, 1 = smooth change
//
void A_SetScale(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	mobj_t *target;

	if (LUA_CallAction(A_SETSCALE, actor))
		return;

	if (locvar1 <= 0)
	{
		if(cht_debug)
			CONS_Printf("A_SetScale: Valid scale not specified!\n");
		return;
	}

	if ((locvar2>>16) == 1)
		target = actor->target;
	else if ((locvar2>>16) == 2)
		target = actor->tracer;
	else // default to yourself!
		target = actor;

	if (!target)
	{
		if(cht_debug)
			CONS_Printf("A_SetScale: No target!\n");
		return;
	}

	locvar1 = FixedMul(locvar1, mapobjectscale); // SRB2Kart

	target->destscale = locvar1; // destination scale
	if (!(locvar2 & 65535))
		P_SetScale(target, locvar1); // this instantly changes current scale to var1 if used, if not destscale will alter scale to var1 anyway
}

// Function: A_RemoteDamage
//
// Description: Damages, kills or even removes either the actor or its target/tracer. Actor acts as the inflictor/source unless harming itself
//
// var1 = Mobj affected: 0 - actor, 1 - target, 2 - tracer
// var2 = Action: 0 - Damage, 1 - Kill, 2 - Remove
//
void A_RemoteDamage(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	mobj_t *target; // we MUST have a target
	mobj_t *source = NULL; // on the other hand we don't necessarily need a source

	if (LUA_CallAction(A_REMOTEDAMAGE, actor))
		return;

	if (locvar1 == 1)
		target = actor->target;
	else if (locvar1 == 2)
		target = actor->tracer;
	else // default to yourself!
		target = actor;

	if (locvar1 == 1 || locvar1 == 2)
		source = actor;

	if (!target)
	{
		if(cht_debug)
			CONS_Printf("A_RemoteDamage: No target!\n");
		return;
	}

	if (locvar2 == 1) // Kill mobj!
	{
		if (target->player) // players die using P_DamageMobj instead for some reason
			//K_DoIngameRespawn(target->player);
			P_DamageMobj(target, source, source, 1, DMG_INSTAKILL);
		else
			P_KillMobj(target, source, source, DMG_NORMAL);
	}
	else if (locvar2 == 2) // Remove mobj!
	{
		if (target->player) //don't remove players!
			return;

		P_RemoveMobj(target);
	}
	else // default: Damage mobj!
		P_DamageMobj(target, source, source, 1, DMG_NORMAL);
}

// Function: A_HomingChase
//
// Description: Actor chases directly towards its destination object
//
// var1 = speed multiple
// var2 = destination: 0 = target, 1 = tracer
//
void A_HomingChase(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	mobj_t *dest;
	fixed_t dist;
	fixed_t speedmul;

	if (LUA_CallAction(A_HOMINGCHASE, actor))
		return;

	if (locvar2 == 1)
		dest = actor->tracer;
	else //default
		dest = actor->target;

	if (!dest || !dest->health)
		return;

	actor->angle = R_PointToAngle2(actor->x, actor->y, dest->x, dest->y);

	dist = P_AproxDistance(P_AproxDistance(dest->x - actor->x, dest->y - actor->y), dest->z - actor->z);

	if (dist < 1)
		dist = 1;

	speedmul = FixedMul(locvar1, actor->scale);

	actor->momx = FixedMul(FixedDiv(dest->x - actor->x, dist), speedmul);
	actor->momy = FixedMul(FixedDiv(dest->y - actor->y, dist), speedmul);
	actor->momz = FixedMul(FixedDiv(dest->z - actor->z, dist), speedmul);
}

// Function: A_TrapShot
//
// Description: Fires a missile in a particular direction and angle rather than AT something, Trapgoyle-style!
//
// var1:
//        lower 16 bits = object # to fire
//        upper 16 bits = front offset
// var2:
//        lower 15 bits = vertical angle variable
//        16th bit:
//			- 0: use vertical angle variable as vertical angle in degrees
//			- 1: mimic P_SpawnXYZMissile
//				use z of actor minus z of missile as vertical distance to cover during momz calculation
//				use vertical angle variable as horizontal distance to cover during momz calculation
//        upper 16 bits = height offset
//
void A_TrapShot(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	dboolean oldstyle = (locvar2 & 32768) ? true : false;
	mobjtype_t type = (mobjtype_t)(locvar1 & 65535);
	mobj_t *missile;
	int16_t frontoff = (int16_t)(locvar1 >> 16);
	int16_t vertoff = (int16_t)(locvar2 >> 16);
	fixed_t x, y, z;
	fixed_t speed;

	if (LUA_CallAction(A_TRAPSHOT, actor))
		return;

	x = actor->x + P_ReturnThrustX(actor, actor->angle, FixedMul(frontoff*FRACUNIT, actor->scale));
	y = actor->y + P_ReturnThrustY(actor, actor->angle, FixedMul(frontoff*FRACUNIT, actor->scale));

	if (actor->eflags & MFE_VERTICALFLIP)
		z = actor->z + actor->height - FixedMul(vertoff*FRACUNIT, actor->scale) - FixedMul(mobjinfo[type].height, actor->scale);
	else
		z = actor->z + FixedMul(vertoff*FRACUNIT, actor->scale);

	CONS_Debug(DBG_GAMELOGIC, "A_TrapShot: missile no. = %d, front offset = %d, vertical angle = %d, z offset = %d\n",
		type, frontoff, (int16_t)(locvar2 & 65535), vertoff);

	missile = P_SpawnMobj(x, y, z, type);

	if (actor->eflags & MFE_VERTICALFLIP)
		missile->flags2 |= MF2_OBJECTFLIP;

	missile->destscale = actor->scale;
	P_SetScale(missile, actor->scale);

	if (missile->info->seesound)
		S_StartSound(missile, missile->info->seesound);

	P_SetTarget(&missile->target, actor);
	missile->angle = actor->angle;

	speed = FixedMul(missile->info->speed, missile->scale);

	if (oldstyle)
	{
		missile->momx = FixedMul(FINECOSINE(missile->angle>>ANGLETOFINESHIFT), speed);
		missile->momy = FixedMul(FINESINE(missile->angle>>ANGLETOFINESHIFT), speed);
		// The below line basically mimics P_SpawnXYZMissile's momz calculation.
		missile->momz = (actor->z + ((actor->eflags & MFE_VERTICALFLIP) ? actor->height : 0) - z) / ((fixed_t)(locvar2 & 32767)*FRACUNIT / speed);
		P_CheckMissileSpawn(missile);
	}
	else
	{
		angle_t vertang = FixedAngle(((int16_t)(locvar2 & 32767))*FRACUNIT);
		if (actor->eflags & MFE_VERTICALFLIP)
				vertang = InvAngle(vertang); // flip firing angle
		missile->momx = FixedMul(FINECOSINE(vertang>>ANGLETOFINESHIFT), FixedMul(FINECOSINE(missile->angle>>ANGLETOFINESHIFT), speed));
		missile->momy = FixedMul(FINECOSINE(vertang>>ANGLETOFINESHIFT), FixedMul(FINESINE(missile->angle>>ANGLETOFINESHIFT), speed));
		missile->momz = FixedMul(FINESINE(vertang>>ANGLETOFINESHIFT), speed);
	}
}

// Function: A_VileTarget
//
// Description: Spawns an object directly on the target, and sets this object as the actor's tracer.
//              Originally used by Archviles to summon a pillar of hellfire, hence the name.
//
// var1 = mobj to spawn
// var2 = If 0, target only the actor's target. Else, target every player, period.
//
void A_VileTarget(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	mobj_t *fog;
	mobjtype_t fogtype;
	int32_t i;

	if (LUA_CallAction(A_VILETARGET, actor))
		return;

	if (!actor->target)
		return;

	A_FaceTarget(actor);

	// Determine object to spawn
	if (locvar1 <= 0 || locvar1 >= NUMMOBJTYPES)
		fogtype = MT_CYBRAKDEMON_TARGET_RETICULE;
	else
		fogtype = (mobjtype_t)locvar1;

	if (!locvar2)
	{
		fog = P_SpawnMobj(actor->target->x,
							actor->target->y,
							actor->target->z + ((actor->target->eflags & MFE_VERTICALFLIP) ? actor->target->height - mobjinfo[fogtype].height : 0),
							fogtype);
		if (actor->target->eflags & MFE_VERTICALFLIP)
		{
			fog->eflags |= MFE_VERTICALFLIP;
			fog->flags2 |= MF2_OBJECTFLIP;
		}
		fog->destscale = actor->target->scale;
		P_SetScale(fog, fog->destscale);

		P_SetTarget(&actor->tracer, fog);
		P_SetTarget(&fog->target, actor);
		P_SetTarget(&fog->tracer, actor->target);
		A_VileFire(fog);
	}
	else
	{
		// Our "Archvile" here is actually Oprah. "YOU GET A TARGET! YOU GET A TARGET! YOU ALL GET A TARGET!"
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i] || players[i].spectator)
				continue;

			if (!players[i].mo)
				continue;

			if (!players[i].mo->health)
				continue;

			fog = P_SpawnMobj(players[i].mo->x,
							players[i].mo->y,
							players[i].mo->z + ((players[i].mo->eflags & MFE_VERTICALFLIP) ? players[i].mo->height - mobjinfo[fogtype].height : 0),
							fogtype);
			if (players[i].mo->eflags & MFE_VERTICALFLIP)
			{
				fog->eflags |= MFE_VERTICALFLIP;
				fog->flags2 |= MF2_OBJECTFLIP;
			}
			fog->destscale = players[i].mo->scale;
			P_SetScale(fog, fog->destscale);

			if (players[i].mo == actor->target) // We only care to track the fog targeting who we REALLY hate right now
				P_SetTarget(&actor->tracer, fog);
			P_SetTarget(&fog->target, actor);
			P_SetTarget(&fog->tracer, players[i].mo);
			A_VileFire(fog);
		}
	}
}

// Function: A_VileAttack
//
// Description: Instantly hurts the actor's target, if it's in the actor's line of sight.
//              Originally used by Archviles to cause explosions where their hellfire pillars were, hence the name.
//
// var1 = sound to play
// var2:
//		Lower 16 bits = optional explosion object
//		Upper 16 bits = If 0, attack only the actor's target. Else, attack all the players. All of them.
//
void A_VileAttack(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	sfxenum_t soundtoplay;
	mobjtype_t explosionType = MT_NULL;
	mobj_t *fire;
	int32_t i;

	if (LUA_CallAction(A_VILEATTACK, actor))
		return;

	if (!actor->target)
		return;

	A_FaceTarget(actor);

	if (locvar1 <= 0 || locvar1 >= NUMSFX)
		soundtoplay = sfx_brakrx;
	else
		soundtoplay = (sfxenum_t)locvar1;

	if ((locvar2 & 0xFFFF) > 0 && (locvar2 & 0xFFFF) <= NUMMOBJTYPES)
	{
		explosionType = (mobjtype_t)(locvar2 & 0xFFFF);
	}

	if (!(locvar2 & 0xFFFF0000)) {
		if (!P_CheckSight(actor, actor->target))
			return;

		S_StartSound(actor, soundtoplay);
		P_DamageMobj(actor->target, actor, actor, 1, DMG_NORMAL);
		//actor->target->momz = 1000*FRACUNIT/actor->target->info->mass; // How id did it
		actor->target->momz += FixedMul(10*FRACUNIT, actor->scale)*P_MobjFlip(actor->target); // How we're doing it
		if (explosionType != MT_NULL)
		{
			P_SpawnMobj(actor->target->x, actor->target->y, actor->target->z, explosionType);
		}

		// Extra attack. This was for additional damage in Doom. Doesn't really belong in SRB2, but the heck with it, it's here anyway.
		fire = actor->tracer;

		if (!fire)
			return;

		// move the fire between the vile and the player
		//fire->x = actor->target->x - FixedMul (24*FRACUNIT, finecosine[an]);
		//fire->y = actor->target->y - FixedMul (24*FRACUNIT, finesine[an]);
		P_MoveOrigin(fire,
						actor->target->x - P_ReturnThrustX(fire, actor->angle, FixedMul(24*FRACUNIT, fire->scale)),
						actor->target->y - P_ReturnThrustY(fire, actor->angle, FixedMul(24*FRACUNIT, fire->scale)),
						fire->z);
		P_RadiusAttack(fire, actor, 70*FRACUNIT, 0, true);
	}
	else
	{
		// Oprahvile strikes again, but this time, she brings HOT PAIN
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i] || players[i].spectator)
				continue;

			if (!players[i].mo)
				continue;

			if (!players[i].mo->health)
				continue;

			if (!P_CheckSight(actor, players[i].mo))
				continue;

			S_StartSound(actor, soundtoplay);
			P_DamageMobj(players[i].mo, actor, actor, 1, DMG_NORMAL);
			//actor->target->momz = 1000*FRACUNIT/actor->target->info->mass; // How id did it
			players[i].mo->momz += FixedMul(10*FRACUNIT, actor->scale)*P_MobjFlip(players[i].mo); // How we're doing it
			if (explosionType != MT_NULL)
			{
				P_SpawnMobj(players[i].mo->x, players[i].mo->y, players[i].mo->z, explosionType);
			}

			// Extra attack. This was for additional damage in Doom. Doesn't really belong in SRB2, but the heck with it, it's here anyway.
			// However, it ONLY applies to the actor's target. Nobody else matters!
			if (actor->target != players[i].mo)
				continue;

			fire = actor->tracer;

			if (!fire)
				continue;

			// move the fire between the vile and the player
			//fire->x = actor->target->x - FixedMul (24*FRACUNIT, finecosine[an]);
			//fire->y = actor->target->y - FixedMul (24*FRACUNIT, finesine[an]);
			P_MoveOrigin(fire,
							actor->target->x - P_ReturnThrustX(fire, actor->angle, FixedMul(24*FRACUNIT, fire->scale)),
							actor->target->y - P_ReturnThrustY(fire, actor->angle, FixedMul(24*FRACUNIT, fire->scale)),
							fire->z);
			P_RadiusAttack(fire, actor, 70*FRACUNIT, 0, true);
		}
	}

}

// Function: A_VileFire
//
// Description: Kind of like A_CapeChase; keeps this object in front of its tracer, unless its target can't see it.
//				Originally used by Archviles to keep their hellfire pillars on top of the player, hence the name (although it was just "A_Fire" there; added "Vile" to make it more specific).
//				Added some functionality to optionally draw a line directly to the enemy doing the targetting. Y'know, to hammer things in a bit.
//
// var1 = sound to play
// var2:
//		Lower 16 bits = mobj to spawn (0 doesn't spawn a line at all)
//		Upper 16 bits = # to spawn (default is 8)
//
void A_VileFire(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	mobj_t *dest;

	if (LUA_CallAction(A_VILEFIRE, actor))
		return;

	dest = actor->tracer;
	if (!dest)
		return;

	// don't move it if the vile lost sight
	if (!P_CheckSight(actor->target, dest))
		return;

	// keep to same scale and gravity as tracer ALWAYS
	actor->destscale = dest->scale;
	P_SetScale(actor, actor->destscale);
	if (dest->eflags & MFE_VERTICALFLIP)
	{
		actor->eflags |= MFE_VERTICALFLIP;
		actor->flags2 |= MF2_OBJECTFLIP;
	}
	else
	{
		actor->eflags &= ~MFE_VERTICALFLIP;
		actor->flags2 &= ~MF2_OBJECTFLIP;
	}

	P_UnsetThingPosition(actor);
	actor->x = dest->x + P_ReturnThrustX(actor, dest->angle, FixedMul(24*FRACUNIT, actor->scale));
	actor->y = dest->y + P_ReturnThrustY(actor, dest->angle, FixedMul(24*FRACUNIT, actor->scale));
	actor->z = dest->z + ((actor->eflags & MFE_VERTICALFLIP) ? dest->height-actor->height : 0);
	P_SetThingPosition(actor);

	// Play sound, if one's specified
	if (locvar1 > 0 && locvar1 < NUMSFX)
		S_StartSound(actor, (sfxenum_t)locvar1);

	// Now draw the line to the actor's target
	if (locvar2 & 0xFFFF)
	{
		mobjtype_t lineMobj;
		uint16_t numLineMobjs;
		fixed_t distX;
		fixed_t distY;
		fixed_t distZ;
		uint16_t i;

		lineMobj = (mobjtype_t)(locvar2 & 0xFFFF);
		numLineMobjs = (uint16_t)(locvar2 >> 16);
		if (numLineMobjs == 0) {
			numLineMobjs = 8;
		}

		// Get distance for each step
		distX = (actor->target->x - actor->x) / numLineMobjs;
		distY = (actor->target->y - actor->y) / numLineMobjs;
		distZ = ((actor->target->z + FixedMul(actor->target->height/2, actor->target->scale)) - (actor->z + FixedMul(actor->height/2, actor->scale))) / numLineMobjs;

		for (i = 1; i <= numLineMobjs; i++)
		{
			P_SpawnMobj(actor->x + (distX * i), actor->y + (distY * i), actor->z + (distZ * i) + FixedMul(actor->height/2, actor->scale), lineMobj);
		}
	}
}

// Function: A_BrakChase
//
// Description: Chase after your target, but speed and attack are tied to health.
//
// Every time this is called, generate a random number from a 1/4 to 3/4 of mobj's spawn health.
// If health is above that value, use missilestate to attack.
// If health is at or below that value, use meleestate to attack (default to missile state if not available).
//
// Likewise, state will linearly speed up as health goes down.
// Upper bound will be the frame's normal length.
// Lower bound defaults to 1 tic (technically 0, but we round up), unless a lower bound is specified in var1.
//
// var1 = lower-bound of frame length, in tics
// var2 = optional sound to play
//
void A_BrakChase(void *thing)
{
	mobj_t *actor = thing;
	int32_t delta;
	int32_t lowerbound;
	int32_t newtics;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_BRAKCHASE, actor))
		return;

	// Set new tics NOW, in case the state changes while we're doing this and we try applying this to the painstate or something silly
	if (actor->tics > 1 && locvar1 < actor->tics) // Not much point, otherwise
	{
		if (locvar1 < 0)
			lowerbound = 0;
		else
			lowerbound = locvar1;

		newtics = (((actor->tics - lowerbound) * actor->health) / actor->info->spawnhealth) + lowerbound;
		if (newtics < 1)
			newtics = 1;

		actor->tics = newtics;
	}

	if (actor->reactiontime)
	{
		actor->reactiontime--;
		if (actor->reactiontime == 0 && actor->type == MT_CYBRAKDEMON)
			S_StartSound(0, sfx_bewar1 + P_RandomKey(PR_BRAKEGGMAN, 4));
	}

	// modify target threshold
	if (actor->threshold)
	{
		if (!actor->target || actor->target->health <= 0)
			actor->threshold = 0;
		else
			actor->threshold--;
	}

	// turn towards movement direction if not there yet
	if (actor->movedir < NUMDIRS)
	{
		actor->angle &= (7<<29);
		delta = actor->angle - (actor->movedir << 29);

		if (delta > 0)
			actor->angle -= ANGLE_45;
		else if (delta < 0)
			actor->angle += ANGLE_45;
	}

	if (!actor->target || !(actor->target->flags & MF_SHOOTABLE))
	{
		// look for a new target
		if (P_LookForPlayers(actor, true, false, 0))
			return; // got a new target

		P_SetMobjStateNF(actor, actor->info->spawnstate);
		return;
	}

	// do not attack twice in a row
	if (actor->flags2 & MF2_JUSTATTACKED)
	{
		actor->flags2 &= ~MF2_JUSTATTACKED;
		P_NewChaseDir(actor);
		return;
	}

	// Check if we can attack
	if (P_CheckMissileRange(actor) && !actor->movecount)
	{
		// Check if we should use "melee" attack first. (Yes, this still runs outside of melee range. Quiet, you.)
		if (actor->info->meleestate
			&& actor->health <= P_RandomRange(PR_BRAKEGGMAN, actor->info->spawnhealth/4, (actor->info->spawnhealth * 3)/4)) // Guaranteed true if <= 1/4 health, guaranteed false if > 3/4 health
		{
			if (actor->info->attacksound)
				S_StartSound(actor, actor->info->attacksound);

			P_SetMobjState(actor, actor->info->meleestate);
			actor->flags2 |= MF2_JUSTATTACKED;
			return;
		}
		// Else, check for missile attack.
		else if (actor->info->missilestate)
		{
			P_SetMobjState(actor, actor->info->missilestate);
			actor->flags2 |= MF2_JUSTATTACKED;
			return;
		}
	}

	// possibly choose another target
	if (multiplayer && !actor->threshold && (actor->target->health <= 0 || !P_CheckSight(actor, actor->target))
		&& P_LookForPlayers(actor, true, false, 0))
		return; // got a new target

	// chase towards player
	if (--actor->movecount < 0 || !P_Move(actor, actor->info->speed))
		P_NewChaseDir(actor);

	// Optionally play a sound effect
	if (locvar2 > 0 && locvar2 < NUMSFX)
		S_StartSound(actor, (sfxenum_t)locvar2);

	// make active sound
	if (actor->type != MT_CYBRAKDEMON && actor->info->activesound && P_RandomChance(PR_BRAKEGGMAN, 3*FRACUNIT/256))
	{
		S_StartSound(actor, actor->info->activesound);
	}
}

// Function: A_BrakFireShot
//
// Description: Shoot an object at your target, offset to match where Brak's gun is.
// Also, sets Brak's reaction time; behaves normally otherwise.
//
// var1 = object # to shoot
// var2 = unused
//
void A_BrakFireShot(void *thing)
{
	mobj_t *actor = thing;
	fixed_t x, y, z;
	int32_t locvar1 = var1;

	if (LUA_CallAction(A_BRAKFIRESHOT, actor))
		return;

	if (!actor->target)
		return;

	A_FaceTarget(actor);

	x = actor->x
		+ P_ReturnThrustX(actor, actor->angle, FixedMul(64*FRACUNIT, actor->scale))
		+ P_ReturnThrustX(actor, actor->angle+ANGLE_270, FixedMul(32*FRACUNIT, actor->scale));
	y = actor->y
		+ P_ReturnThrustY(actor, actor->angle, FixedMul(64*FRACUNIT, actor->scale))
		+ P_ReturnThrustY(actor, actor->angle+ANGLE_270, FixedMul(32*FRACUNIT, actor->scale));
	if (actor->eflags & MFE_VERTICALFLIP)
		z = actor->z + actor->height - FixedMul(144*FRACUNIT, actor->scale);
	else
		z = actor->z + FixedMul(144*FRACUNIT, actor->scale);

	P_SpawnXYZMissile(actor, actor->target, locvar1, x, y, z);

	if (!(actor->flags & MF_BOSS))
	{
		if (ultimatemode)
			actor->reactiontime = actor->info->reactiontime*TICRATE;
		else
			actor->reactiontime = actor->info->reactiontime*TICRATE*2;
	}
}

// Function: A_BrakLobShot
//
// Description: Lobs an object at the floor about a third of the way toward your target.
//				Implication is it'll bounce the rest of the way.
//				(You can also just aim straight at the target, but whatever)
//				Formula grabbed from http://en.wikipedia.org/wiki/Trajectory_of_a_projectile#Angle_required_to_hit_coordinate_.28x.2Cy.29
//
// var1 = object # to lob
// var2:
//		Lower 16 bits: height offset to shoot from, from the actor's bottom (none that "airtime" malarky)
//		Upper 16 bits: if 0, aim 1/3 of the way. Else, aim directly at target.
//

void A_BrakLobShot(void *thing)
{
	mobj_t *actor = thing;
	fixed_t v; // Velocity to shoot object
	fixed_t a1, a2, aToUse; // Velocity squared
	fixed_t g; // Gravity
	fixed_t x; // Horizontal difference
	int32_t x_int; // x! But in integer form!
	fixed_t y; // Vertical difference (yes that's normally z in SRB2 shut up)
	int32_t y_int; // y! But in integer form!
	int32_t intHypotenuse; // x^2 + y^2. Frequently overflows fixed point, hence why we need integers proper.
	fixed_t fixedHypotenuse; // However, we can work around that and still get a fixed-point number.
	angle_t theta; // Angle of attack
	mobjtype_t typeOfShot;
	mobj_t *shot; // Object to shoot
	fixed_t newTargetX; // If not aiming directly
	fixed_t newTargetY; // If not aiming directly
	int32_t locvar1 = var1;
	int32_t locvar2 = var2 & 0x0000FFFF;
	int32_t aimDirect = var2 & 0xFFFF0000;

	if (LUA_CallAction(A_BRAKLOBSHOT, actor))
		return;

	if (!actor->target)
		return; // Don't even bother if we've got nothing to aim at.

	// Look up actor's current gravity situation
	g = FixedMul(gravity, P_GetSectorGravityFactor(actor->subsector->sector));

	// Look up distance between actor and its target
	x = P_AproxDistance(actor->target->x - actor->x, actor->target->y - actor->y);
	if (!aimDirect)
	{
		// Distance should actually be a third of the way over
		x = FixedDiv(x, 3<<FRACBITS);
		newTargetX = actor->x + P_ReturnThrustX(actor, actor->angle, x);
		newTargetY = actor->y + P_ReturnThrustY(actor, actor->angle, x);
		x = P_AproxDistance(newTargetX - actor->x, newTargetY - actor->y);
		// Look up height difference between actor and the ground 1/3 of the way to its target
		y = P_FloorzAtPos(newTargetX, newTargetY, actor->target->z, actor->target->height) - (actor->z + FixedMul(locvar2*FRACUNIT, actor->scale));
	}
	else
	{
		// Look up height difference between actor and its target
		y = actor->target->z - (actor->z + FixedMul(locvar2*FRACUNIT, actor->scale));
	}

	// Get x^2 + y^2. Have to do it in a roundabout manner, because this overflows fixed_t way too easily otherwise.
	x_int = x>>FRACBITS;
	y_int = y>>FRACBITS;
	intHypotenuse = (x_int*x_int) + (y_int*y_int);
	fixedHypotenuse = FixedSqrt(intHypotenuse) *256;

	// a = g(y+/-sqrt(x^2+y^2)). a1 can be +, a2 can be -.
	a1 = FixedMul(g,y+fixedHypotenuse);
	a2 = FixedMul(g,y-fixedHypotenuse);

	// Determine which one isn't actually an imaginary number (or the smaller of the two, if both are real), and use that for v.
	if (a1 < 0 || a2 < 0)
	{
		if (a1 < 0 && a2 < 0)
		{
			//Somehow, v^2 is negative in both cases. v is therefore imaginary and something is horribly wrong. Abort!
			return;
		}
		// Just find which one's NOT negative, and use that
		aToUse = max(a1,a2);
	}
	else
	{
		// Both are positive; use whichever's smaller so it can decay faster
		aToUse = min(a1,a2);
	}
	v = FixedSqrt(aToUse);
	// Okay, so we know the velocity. Let's actually find theta.
	// We can cut the "+/- sqrt" part out entirely, since v was calculated specifically for it to equal zero. So:
	//theta = tantoangle[FixedDiv(aToUse,FixedMul(g,x)) >> DBITS];
	theta = tantoangle[SlopeDiv(aToUse,FixedMul(g,x))];

	// Okay, complicated math done. Let's fire our object already, sheesh.
	A_FaceTarget(actor);
	if (locvar1 <= 0 || locvar1 >= NUMMOBJTYPES)
		typeOfShot = MT_CANNONBALL;
	else typeOfShot = (mobjtype_t)locvar1;
	shot = P_SpawnMobj(actor->x, actor->y, actor->z + FixedMul(locvar2*FRACUNIT, actor->scale), typeOfShot);
	if (shot->info->seesound)
		S_StartSound(shot, shot->info->seesound);
	P_SetTarget(&shot->target, actor); // where it came from

	shot->angle = actor->angle;

	// Horizontal axes first. First parameter is initial horizontal impulse, second is to correct its angle.
	shot->momx = FixedMul(FixedMul(v, FINECOSINE(theta >> ANGLETOFINESHIFT)), FINECOSINE(shot->angle >> ANGLETOFINESHIFT));
	shot->momy = FixedMul(FixedMul(v, FINECOSINE(theta >> ANGLETOFINESHIFT)), FINESINE(shot->angle >> ANGLETOFINESHIFT));
	// Then the vertical axis. No angle-correction needed here.
	shot->momz = FixedMul(v, FINESINE(theta >> ANGLETOFINESHIFT));
	// I hope that's all that's needed, ugh
}

// Function: A_NapalmScatter
//
// Description: Scatters a specific number of projectiles around in a circle.
//				Intended for use with objects that are affected by gravity; would be kind of silly otherwise.
//
// var1:
//		Lower 16 bits: object # to lob (TODO: come up with a default)
//		Upper 16 bits: Number to lob (default 8)
// var2:
//		Lower 16 bits: distance to toss them (No default - 0 does just that - but negatives will revert to 128)
//		Upper 16 bits: airtime in tics (default 16)
//
void A_NapalmScatter(void *thing)
{
	mobj_t *actor = thing;
	mobjtype_t typeOfShot = var1 & 0x0000FFFF; // Type
	int32_t numToShoot = (var1 & 0xFFFF0000) >> 16; // How many
	fixed_t distance = (var2 & 0x0000FFFF) << FRACBITS; // How far
	fixed_t airtime = var2 & 0xFFFF0000; // How long until impact (assuming no obstacles)
	fixed_t vx; // Horizontal momentum
	fixed_t vy; // Vertical momentum
	fixed_t g; // Gravity
	int32_t i; // for-loop cursor
	mobj_t *mo; // each and every spawned napalm burst

	if (LUA_CallAction(A_NAPALMSCATTER, actor))
		return;

	// Some quick sanity-checking
	if (typeOfShot >= NUMMOBJTYPES) // I'd add a <0 check, too, but 0x0000FFFF isn't negative in this case
		typeOfShot = MT_NULL;
	if (numToShoot <= 0) // Presumably you forgot to set var1 up; else, why are you calling this to shoot nothing?
		numToShoot = 8;
	else if (numToShoot > 8192) // If you seriously need this many objects spawned, stop and ask yourself "Why am I doing this?"
		numToShoot = 8192;
	if (distance < 0) // Presumably you thought this was an unsigned integer, you naive fool
		distance = 32767<<FRACBITS;
	if (airtime <= 0) // Same deal as distance I guess
		airtime = 16<<FRACBITS;

	// Look up actor's current gravity situation
	g = FixedMul(gravity, P_GetSectorGravityFactor(actor->subsector->sector));

	// vy = (g*(airtime-1))/2
	vy = FixedMul(g,(airtime-(1<<FRACBITS)))>>1;
	// vx = distance/airtime
	vx = FixedDiv(distance, airtime);

	for (i = 0; i<numToShoot; i++)
	{
		const angle_t fa = (i*FINEANGLES/numToShoot) & FINEMASK;

		mo = P_SpawnMobj(actor->x, actor->y, actor->z, typeOfShot);
		P_SetTarget(&mo->target, actor->target); // Transfer target so Brak doesn't hit himself like an idiot

		mo->angle = fa << ANGLETOFINESHIFT;
		mo->momx = FixedMul(FINECOSINE(fa),vx);
		mo->momy = FixedMul(FINESINE(fa),vx);
		mo->momz = vy;
	}
}

// Function: A_SpawnFreshCopy
//
// Description: Spawns a copy of the mobj. x, y, z, angle, scale, target and tracer carry over; everything else starts anew.
//				Mostly writing this because I want to do multiple actions to pass these along in a single frame instead of several.
//
// var1 = Boolean: copy spawnpoint?
// var2 = unused
//
void A_SpawnFreshCopy(void *thing)
{
	mobj_t *actor = thing;
	mobj_t *newObject;

	const dboolean copyspawn = (dboolean)(var1 != 0);

	if (LUA_CallAction(A_SPAWNFRESHCOPY, actor))
		return;

	newObject = P_SpawnMobjFromMobj(actor, 0, 0, 0, actor->type);
	newObject->flags2 = actor->flags2 & MF2_AMBUSH;

	int32_t i;
	for (i = 0; i < NUM_SCRIPT_ARGS; i++)
		newObject->script_args[i] = actor->script_args[i];

	newObject->angle = actor->angle;
	newObject->color = actor->color;
	P_SetTarget(&newObject->target, actor->target);
	P_SetTarget(&newObject->tracer, actor->tracer);

	if (copyspawn)
	{
		newObject->spawnpoint = actor->spawnpoint;
	}

	if (newObject->info->seesound)
		S_StartSound(newObject, newObject->info->seesound);

	newObject->color = actor->color; // SRB2Kart
}

// Function: A_WarpToSpawnpoint
//
// Description: Warps an object to its in-map spawnpoint, and resets all speed values in the process.
//				If a spawnpoint doesn't exist, the function does nothing.
//
// var1 = unused
// var2 = unused
//
void A_WarpToSpawnpoint(void *thing)
{
	mobj_t *actor = thing;

	if (LUA_CallAction(A_WARPTOSPAWNPOINT, actor))
		return;

	if (P_MobjWasRemoved(actor))
		return;

	if (!actor->spawnpoint)
		return;

	actor->momx = 0;
	actor->momy = 0;
	actor->momz = 0;

	vector3_t warp_pos = {0};
	mobjtype_t actor_type = P_GetMobjtype(actor->spawnpoint->type);

	warp_pos.x = actor->spawnpoint->x << FRACBITS;
	warp_pos.y = actor->spawnpoint->y << FRACBITS;
	warp_pos.z = P_GetMapThingSpawnHeight(actor_type, actor->spawnpoint, warp_pos.x, warp_pos.y);

	P_SetOrigin(actor, warp_pos.x, warp_pos.y, warp_pos.z);
}

// Internal Flicky spawning function.
mobj_t *P_InternalFlickySpawn(mobj_t *actor, mobjtype_t flickytype, fixed_t momz, dboolean lookforplayers, int8_t moveforward)
{
	mobj_t *flicky;
	fixed_t offsx = 0, offsy = 0;

	if (!flickytype)
	{
		if (!mapheaderinfo[gamemap-1] || !mapheaderinfo[gamemap-1]->numFlickies) // No mapheader, no shoes, no service.
			return NULL;
		else
		{
			int32_t prandom = P_RandomKey(PR_FLICKY, mapheaderinfo[gamemap-1]->numFlickies);
			flickytype = mapheaderinfo[gamemap-1]->flickies[prandom];
		}
	}

	if (moveforward)
	{
		fixed_t scal = mobjinfo[flickytype].radius*((fixed_t)moveforward);
		offsx = P_ReturnThrustX(actor, actor->angle, scal);
		offsy = P_ReturnThrustY(actor, actor->angle, scal);
	}

	flicky = P_SpawnMobjFromMobj(actor, offsx, offsy, 0, flickytype);
	flicky->angle = actor->angle;

	if (flickytype == MT_SEED)
		flicky->z += P_MobjFlip(actor)*(actor->height - flicky->height)/2;

	if (actor->eflags & MFE_UNDERWATER)
		momz = FixedDiv(momz, FixedSqrt(3*FRACUNIT));

	P_SetObjectMomZ(flicky, momz, false);
	flicky->movedir = (P_RandomChance(PR_FLICKY, FRACUNIT/2) ?  -1 : 1);
	flicky->fuse = P_RandomRange(PR_FLICKY, 595, 700);	// originally 300, 350
	flicky->threshold = 0;

	if (lookforplayers)
		P_LookForPlayers(flicky, true, false, 0);

	return flicky;
}

// Function: A_FlickySpawn
//
// Description: Flicky spawning function.
//
// var1:
//		lower 16 bits: if 0, spawns random flicky based on level header. Else, spawns the designated thing type.
//		bit 17: if 0, no sound is played. Else, A_Scream is called.
//		bit 18: if 1, spawn flicky slightly forward from spawn position, to avoid being stuck in wall. doesn't stack with 19. (snailers)
//		bit 19: if 1, spawn flicky slightly backward from spawn position. doesn't stack with 18.
// var2 = upwards thrust for spawned flicky. If zero, default value is provided.
//
void A_FlickySpawn(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1 & 65535;
	int32_t locvar2 = var2;
	int32_t test = (var1 >> 16);
	int8_t moveforward = 0;

	if (LUA_CallAction(A_FLICKYSPAWN, actor))
		return;

	if (test & 1)
		A_Scream(actor); // A shortcut for the truly lazy.
	if (test & 2)
		moveforward = 1;
	else if (test & 4)
		moveforward = -1;

	P_InternalFlickySpawn(actor, locvar1, ((locvar2) ? locvar2 : 8*FRACUNIT), true, moveforward);
}

// Internal Flicky color setting
void P_InternalFlickySetColor(mobj_t *actor, uint8_t color)
{
	uint8_t flickycolors[] = {
		SKINCOLOR_RED,
		SKINCOLOR_CYAN,
		SKINCOLOR_BLUE,
		SKINCOLOR_PASTEL,
		SKINCOLOR_PURPLE,
		SKINCOLOR_VIOLET,
		SKINCOLOR_FUCHSIA,
		SKINCOLOR_BLACK,
		SKINCOLOR_BEIGE,
		SKINCOLOR_LAVENDER,
		SKINCOLOR_RUBY,
		SKINCOLOR_BLOSSOM,
		SKINCOLOR_SUNSET,
		SKINCOLOR_ORANGE,
		SKINCOLOR_YELLOW,
	};

	if (color == 0)
		// until we can customize flicky colors by level header, just stick to SRB2's defaults
		actor->color = flickycolors[P_RandomKey(PR_FLICKY, 2)]; //flickycolors[P_RandomKey(sizeof(flickycolors))];
	else
		actor->color = flickycolors[min(color-1, 14)]; // sizeof(flickycolors)-1
}

// Function: A_FlickyCenter
//
// Description: Place flickies in-level.
//
// var1 = if 0, spawns random flicky based on level header. Else, spawns the designated thing type.
// var2 = maximum default distance away from spawn the flickies are allowed to travel. If args[0] != 0, then that's the radius.
//
// args[0] = Flicky radius
// args[1] = Behavior flags (see the list below)
// args[2] = Flicky color, up to 15. Applies to fish.
//
// If TMFF_AIMLESS (MF_SLIDEME): is flagged, Flickies move aimlessly. Else, orbit around the target.
// If TMFF_STATIONARY (MF_GRENADEBOUNCE): Flickies stand in-place without gravity (unless they hop, then gravity is applied.)
// If TMFF_HOP (MF_NOCLIPTHING): is flagged, Flickies hop.
//
void A_FlickyCenter(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	fixed_t homeRadius = INT32_MAX;

	if (LUA_CallAction(A_FLICKYCENTER, actor))
		return;

	homeRadius = locvar2 ? FixedMul(abs(locvar2), actor->scale) : 384*actor->scale;

	if (!actor->tracer)
	{
		mobj_t *flicky = P_InternalFlickySpawn(actor, locvar1, 1, false, 0);
		P_SetTarget(&flicky->target, actor);
		P_SetTarget(&actor->tracer, flicky);

		actor->flags &= ~(MF_SLIDEME|MF_GRENADEBOUNCE|MF_NOCLIPTHING);
		if (actor->args[1] & TMFF_AIMLESS)
			actor->flags |= MF_SLIDEME;
		if (actor->args[1] & TMFF_STATIONARY)
			actor->flags |= MF_GRENADEBOUNCE;
		if (actor->args[1] & TMFF_HOP)
			actor->flags |= MF_NOCLIPTHING;
		actor->extravalue1 = actor->args[0] ? abs(actor->args[0])*actor->scale : homeRadius;
		actor->extravalue2 = actor->args[2];
		actor->friction = actor->x;
		actor->movefactor = actor->y;
		actor->watertop = actor->z;

		if (actor->flags & MF_GRENADEBOUNCE) // in-place
			actor->tracer->fuse = 0;
		else if (actor->flags & MF_SLIDEME) // aimless
		{
			actor->tracer->fuse = 0; // less than 2*TICRATE means move aimlessly.
			actor->tracer->angle = P_RandomKey(PR_FLICKY, 180)*ANG2;
		}
		else //orbit
			actor->tracer->fuse = FRACUNIT;

		if (locvar1 == MT_FLICKY_08)
			P_InternalFlickySetColor(actor->tracer, actor->extravalue2);

		actor->extravalue2 = 0;
	}

	if (!(actor->flags & MF_SLIDEME) && !(actor->flags & MF_GRENADEBOUNCE))
	{
		fixed_t originx = actor->friction;
		fixed_t originy = actor->movefactor;
		fixed_t originz = actor->watertop;

		actor->tracer->fuse = FRACUNIT;

		// Impose default home radius if flicky orbits around player
		if (!actor->extravalue1)
			actor->extravalue1 = homeRadius;

		P_LookForPlayers(actor, true, false, actor->extravalue1);

		if (actor->target && P_AproxDistance(actor->target->x - originx, actor->target->y - originy) < actor->extravalue1)
		{
			actor->extravalue2 = 1;
		 	P_SetOrigin(actor, actor->target->x, actor->target->y, actor->target->z);
			P_SetTarget(&g_tm.thing, NULL);
		}
		else if(actor->extravalue2)
		{
			actor->extravalue2 = 0;
			P_SetOrigin(actor, originx, originy, originz);
			P_SetTarget(&g_tm.thing, NULL);
		}
	}
}

// Internal Flicky bubbling function.
void P_InternalFlickyBubble(mobj_t *actor)
{

	if (actor->eflags & MFE_UNDERWATER)
	{
		mobj_t *overlay;

		if (!((actor->z + 3*actor->height/2) < actor->watertop) || !mobjinfo[actor->type].raisestate || actor->tracer)
			return;

		overlay = P_SpawnMobj(actor->x, actor->y, actor->z, MT_OVERLAY);
		P_SetMobjStateNF(overlay, mobjinfo[actor->type].raisestate);
		P_SetTarget(&actor->tracer, overlay);
		P_SetTarget(&overlay->target, actor);
		return;
	}

	if (!actor->tracer || P_MobjWasRemoved(actor->tracer))
		return;

	P_RemoveMobj(actor->tracer);
	P_SetTarget(&actor->tracer, NULL);
}

// Function: A_FlickyAim
//
// Description: Flicky aiming function.
//
// var1 = how far around the target (in angle constants) the flicky should look
// var2 = distance from target to aim for
//
void A_FlickyAim(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	dboolean flickyhitwall = false;

	if (LUA_CallAction(A_FLICKYAIM, actor))
		return;

	if ((actor->momx == actor->momy && actor->momy == 0)
		|| (actor->target && P_IsFlickyCenter(actor->target->type)
			&& actor->target->extravalue1 && (actor->target->flags & MF_SLIDEME)
			&& P_AproxDistance(actor->x - actor->target->x, actor->y - actor->target->y) >= actor->target->extravalue1))
		flickyhitwall = true;

	P_InternalFlickyBubble(actor);
	P_InstaThrust(actor, 0, 0);

	if (!actor->target)
	{
		P_LookForPlayers(actor, true, false, 0);
		actor->angle = P_RandomKey(PR_FLICKY, 36)*ANG10;
		return;
	}

	if (actor->fuse > 2*TICRATE)
	{
		angle_t posvar;
		fixed_t chasevar, chasex, chasey;

		if (flickyhitwall)
			actor->movedir *= -1;

		posvar = ((R_PointToAngle2(actor->target->x, actor->target->y, actor->x, actor->y) + actor->movedir*locvar1) >> ANGLETOFINESHIFT) & FINEMASK;
		chasevar = FixedSqrt(max(FRACUNIT, P_AproxDistance(actor->target->x - actor->x, actor->target->y - actor->y) - locvar2)) + locvar2;

		chasex = actor->target->x + FixedMul(FINECOSINE(posvar), chasevar);
		chasey = actor->target->y + FixedMul(FINESINE(posvar), chasevar);

		if (P_AproxDistance(chasex - actor->x, chasey - actor->y))
			actor->angle = R_PointToAngle2(actor->x, actor->y, chasex, chasey);
	}
	else if (flickyhitwall)
	{
		if (actor->target && P_IsFlickyCenter(actor->target->type))
			actor->angle = R_PointToAngle2(actor->target->x, actor->target->y, actor->x, actor->y) + P_RandomRange(PR_FLICKY, 112, 248) * ANG1;
		else
			actor->angle += P_RandomRange(PR_FLICKY, 112, 248)*ANG1;
		actor->threshold = 0;
	}
}

//Internal Flicky flying function. Also usuable as an underwater swim thrust.
void P_InternalFlickyFly(mobj_t *actor, fixed_t flyspeed, fixed_t targetdist, fixed_t chasez)
{
	angle_t vertangle;

	flyspeed = FixedMul(flyspeed, actor->scale);
	actor->flags |= MF_NOGRAVITY;

	var1 = ANG30;
	var2 = 32*FRACUNIT;
	A_FlickyAim(actor);

	chasez *= 8;
	if (!actor->target || !(actor->fuse > 2*TICRATE))
		chasez += ((actor->eflags & MFE_VERTICALFLIP) ? actor->ceilingz - 24*FRACUNIT : actor->floorz + 24*FRACUNIT);
	else
	{
		fixed_t add = actor->target->z + (actor->target->height - actor->height)/2;
		if (add > (actor->ceilingz - 24*actor->scale - actor->height))
			add = actor->ceilingz - 24*actor->scale - actor->height;
		else if (add < (actor->floorz + 24*actor->scale))
			add = actor->floorz + 24*actor->scale;
		chasez += add;
	}

	if (!targetdist)
		targetdist = 16*FRACUNIT; //Default!

	if (actor->target && abs(chasez - actor->z) > targetdist)
		targetdist = P_AproxDistance(actor->target->x - actor->x, actor->target->y - actor->y);

	if (actor->target
		&& P_IsFlickyCenter(actor->target->type)
		&& (actor->target->flags & MF_SLIDEME))
		vertangle = 0;
	else
		vertangle = (R_PointToAngle2(0, actor->z, targetdist, chasez) >> ANGLETOFINESHIFT) & FINEMASK;

	P_InstaThrust(actor, actor->angle, FixedMul(FINECOSINE(vertangle), flyspeed));
	actor->momz = FixedMul(FINESINE(vertangle), flyspeed);
}

// Function: A_FlickyFly
//
// Description: Flicky flying function.
//
// var1 = how fast to fly
// var2 = how far ahead the target should be considered
//
void A_FlickyFly(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_FLICKYFLY, actor))
		return;

	P_InternalFlickyFly(actor, locvar1, locvar2,
	FINECOSINE((((actor->fuse % 36) * ANG10) >> ANGLETOFINESHIFT) & FINEMASK)
	);
}

// Function: A_FlickySoar
//
// Description: Flicky soaring function - specific to puffin.
//
// var1 = how fast to fly
// var2 = how far ahead the target should be considered
//
void A_FlickySoar(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_FLICKYSOAR, actor))
		return;

	P_InternalFlickyFly(actor, locvar1, locvar2,
	2*(FRACUNIT/2 - abs(FINECOSINE((((actor->fuse % 144) * 5*ANG1/2) >> ANGLETOFINESHIFT) & FINEMASK)))
	);

	if (P_MobjFlip(actor)*actor->momz > 0 && actor->frame == 1 && actor->sprite == SPR_FL10)
		actor->frame = 3;
}

//Function: A_FlickyCoast
//
// Description: Flicky swim-coasting function.
//
// var1 = speed to change state upon reaching
// var2 = state to change to upon slowing down
// the spawnstate of the mobj = state to change to when above water
//
void A_FlickyCoast(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_FLICKYCOAST, actor))
		return;

	if (actor->eflags & MFE_UNDERWATER)
	{
		actor->momx = (11*actor->momx)/12;
		actor->momy = (11*actor->momy)/12;
		actor->momz = (11*actor->momz)/12;

		if (P_AproxDistance(P_AproxDistance(actor->momx, actor->momy), actor->momz) < locvar1)
			P_SetMobjState(actor, locvar2);

		return;
	}

	actor->flags &= ~MF_NOGRAVITY;
	P_SetMobjState(actor, mobjinfo[actor->type].spawnstate);
}

// Internal Flicky hopping function.
void P_InternalFlickyHop(mobj_t *actor, fixed_t momz, fixed_t momh, angle_t angle)
{
	if (((!(actor->eflags & MFE_VERTICALFLIP) && actor->z <= actor->floorz)
	|| ((actor->eflags & MFE_VERTICALFLIP) && actor->z + actor->height >= actor->ceilingz)))
	{
		if (momz)
		{
			if (actor->eflags & MFE_UNDERWATER)
				momz = FixedDiv(momz, FixedSqrt(3*FRACUNIT));
			P_SetObjectMomZ(actor, momz, false);
		}
		P_InstaThrust(actor, angle, FixedMul(momh, actor->scale));
	}
}

// Function: A_FlickyHop
//
// Description: Flicky hopping function.
//
// var1 = vertical thrust
// var2 = horizontal thrust
//
void A_FlickyHop(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_FLICKYHOP, actor))
		return;

	P_InternalFlickyHop(actor, locvar1, locvar2, actor->angle);
}

// Function: A_FlickyFlounder
//
// Description: Flicky floundering function.
//
// var1 = intended vertical thrust
// var2 = intended horizontal thrust
//
void A_FlickyFlounder(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	angle_t hopangle;

	if (LUA_CallAction(A_FLICKYFLOUNDER, actor))
		return;

	locvar1 *= (P_RandomKey(PR_FLICKY, 2) + 1);
	locvar2 *= (P_RandomKey(PR_FLICKY, 2) + 1);
	hopangle = (actor->angle + (P_RandomKey(PR_FLICKY, 9) - 4)*ANG2);
	P_InternalFlickyHop(actor, locvar1, locvar2, hopangle);
}

// Function: A_FlickyCheck
//
// Description: Flicky airtime check function.
//
// var1 = state to change to upon touching the floor
// var2 = state to change to upon falling
// the meleestate of the mobj = state to change to when underwater
//
void A_FlickyCheck(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_FLICKYCHECK, actor))
		return;

	if (actor->target
		&& P_IsFlickyCenter(actor->target->type)
		&& (actor->target->flags & MF_GRENADEBOUNCE))
	{
		if (!(actor->target->flags & MF_NOCLIPTHING)) // no hopping
		{
			actor->momz = 0;
			actor->flags |= MF_NOGRAVITY;
		}
		actor->flags |= MF_NOCLIP | MF_NOBLOCKMAP | MF_SCENERY;
		P_SetMobjState(actor, mobjinfo[actor->type].seestate);
	}
	else if (locvar2 && P_MobjFlip(actor)*actor->momz < 1)
		P_SetMobjState(actor, locvar2);
	else if (locvar1 && ((!(actor->eflags & MFE_VERTICALFLIP) && actor->z <= actor->floorz)
	|| ((actor->eflags & MFE_VERTICALFLIP) && actor->z + actor->height >= actor->ceilingz)))
		P_SetMobjState(actor, locvar1);
	else if (mobjinfo[actor->type].meleestate && (actor->eflags & MFE_UNDERWATER))
		P_SetMobjState(actor, mobjinfo[actor->type].meleestate);
	P_InternalFlickyBubble(actor);
}

// Function: A_FlickyHeightCheck
//
// Description: Flicky height check function.
//
// var1 = state to change to when falling below height relative to target
// var2 = height relative to target to change state at
//
void A_FlickyHeightCheck(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_FLICKYHEIGHTCHECK, actor))
		return;

	if (actor->target
		&& P_IsFlickyCenter(actor->target->type)
		&& (actor->target->flags & MF_GRENADEBOUNCE))
	{
		if (!(actor->target->flags & MF_NOCLIPTHING)) // no hopping
		{
			actor->momz = 0;
			actor->flags |= MF_NOGRAVITY;
		}
		actor->flags |= MF_NOCLIP | MF_NOBLOCKMAP | MF_SCENERY;
		P_SetMobjState(actor, mobjinfo[actor->type].seestate);
	}
	else if (locvar1 && actor->target && P_MobjFlip(actor)*actor->momz < 1
	&& ((P_MobjFlip(actor)*((actor->z + actor->height/2) - (actor->target->z + actor->target->height/2)) < locvar2)
	|| (actor->z - actor->height < actor->floorz) || (actor->z + 2*actor->height > actor->ceilingz)))
		P_SetMobjState(actor, locvar1);
	P_InternalFlickyBubble(actor);
}

// Function: A_FlickyFlutter
//
// Description: Flicky fluttering function - specific to chicken.
//
// var1 = state to change to upon touching the floor
// var2 = state to change to upon falling
// the meleestate of the mobj = state to change to when underwater
//
void A_FlickyFlutter(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_FLICKYFLUTTER, actor))
		return;

	var1 = locvar1;
	var2 = locvar2;
	A_FlickyCheck(actor);

	var1 = ANG30;
	var2 = 32*FRACUNIT;
	A_FlickyAim(actor);

	P_InstaThrust(actor, actor->angle, 2*actor->scale);
	if (P_MobjFlip(actor)*actor->momz < -FRACUNIT/2)
		actor->momz = -P_MobjFlip(actor)*actor->scale/2;
}

#undef FLICKYHITWALL

// Function: A_FlameParticle
//
// Description: Creates the mobj's painchance at a random position around the object's radius.
//
// var1 = unused
// var2 = unused
//
void A_FlameParticle(void *thing)
{
	mobj_t *actor = thing;
	mobjtype_t type = (mobjtype_t)(mobjinfo[actor->type].painchance);
	fixed_t rad, hei;
	mobj_t *particle;

	if (LUA_CallAction(A_FLAMEPARTICLE, actor))
		return;

	if (!type)
		return;

	rad = actor->radius>>FRACBITS;
	hei = actor->height>>FRACBITS;

	fixed_t rand_x;
	fixed_t rand_y;
	fixed_t rand_z;

	rand_z = P_RandomRange(PR_DECORATION, hei/2, hei)<<FRACBITS;
	rand_y = P_RandomRange(PR_DECORATION, rad, -rad)<<FRACBITS;
	rand_x = P_RandomRange(PR_DECORATION, rad, -rad)<<FRACBITS;

	particle = P_SpawnMobjFromMobj(actor, rand_x, rand_y, rand_z, type);
	P_SetObjectMomZ(particle, 2<<FRACBITS, false);
}

// Function: A_FadeOverlay
//
// Description: Makes a pretty overlay (primarily for super/NiGHTS transformation).
//
// var1 = bit 1 = bit 1 = don't make fast, bit 2 = don't set tracer
// var2 = unused
//
void A_FadeOverlay(void *thing)
{
	mobj_t *actor = thing;
	mobj_t *fade;
	int32_t locvar1 = var1;

	if (LUA_CallAction(A_FADEOVERLAY, actor))
		return;

	fade = P_SpawnGhostMobj(actor);
	fade->renderflags = actor->renderflags;

	if (!(locvar1 & 1))
	{
		fade->fuse = 15;
		fade->flags2 |= MF2_BOSSNOTRAP;
	}
	else
		fade->fuse = 20;

	if (!(locvar1 & 2))
		P_SetTarget(&actor->tracer, fade);
}

// Function: A_ConnectToGround
// Description: Create a palm tree trunk/mine chain.
//
// var1 = Object type to connect to ground
// var2 = Object type to place on ground
//
void A_ConnectToGround(void *thing)
{
	mobj_t *actor = thing;
	mobj_t *work;
	fixed_t workz;
	fixed_t workh;
	angle_t ang;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_CONNECTTOGROUND, actor))
		return;

	if (actor->subsector->sector->ffloors)
		P_AdjustMobjFloorZ_FFloors(actor, actor->subsector->sector, 2);

	if (actor->flags2 & MF2_OBJECTFLIP)
		workz = (actor->z + actor->height) - actor->ceilingz;
	else
		workz = actor->floorz - actor->z;

	if (locvar2)
	{
		workh = FixedMul(mobjinfo[locvar2].height, actor->scale);
		if (actor->flags2 & MF2_OBJECTFLIP)
			workz += workh;
		work = P_SpawnMobjFromMobj(actor, 0, 0, workz, locvar2);
		workz += workh;
	}

	if (!locvar1)
		return;

	if (!(workh = FixedMul(mobjinfo[locvar1].height, actor->scale)))
		return;

	ang = actor->angle + ANGLE_45;
	while (workz < 0)
	{
		work = P_SpawnMobjFromMobj(actor, 0, 0, workz, locvar1);
		if (work)
			work->angle = ang;
		ang += ANGLE_90;
		workz += workh;
	}

	if (workz != 0)
		actor->z += P_MobjFlip(actor)*workz;
}

// Function: A_SpawnParticleRelative
//
// Description: Spawns a particle effect relative to the location of the actor
//
// var1:
//		var1 >> 16 = x
//		var1 & 65535 = y
// var2:
//		var2 >> 16 = z
//		var2 & 65535 = state
//
void A_SpawnParticleRelative(void *thing)
{
	mobj_t *actor = thing;
	int16_t x, y, z; // Want to be sure we can use negative values
	statenum_t state;
	mobj_t *mo;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_SPAWNPARTICLERELATIVE, actor))
		return;

	CONS_Debug(DBG_GAMELOGIC, "A_SpawnParticleRelative called from object type %d, var1: %d, var2: %d\n", actor->type, locvar1, locvar2);

	x = (int16_t)(locvar1>>16);
	y = (int16_t)(locvar1&65535);
	z = (int16_t)(locvar2>>16);
	state = (statenum_t)(locvar2&65535);

	// Spawn objects correctly in reverse gravity.
	// NOTE: Doing actor->z + actor->height is the bottom of the object while the object has reverse gravity. - Flame
	mo = P_SpawnMobj(actor->x + FixedMul(x<<FRACBITS, actor->scale),
		actor->y + FixedMul(y<<FRACBITS, actor->scale),
		(actor->eflags & MFE_VERTICALFLIP) ? ((actor->z + actor->height - mobjinfo[MT_PARTICLE].height) - FixedMul(z<<FRACBITS, actor->scale)) : (actor->z + FixedMul(z<<FRACBITS, actor->scale)), MT_PARTICLE);

	// Spawn objects with an angle matching the spawner's, rather than spawning Eastwards - Monster Iestyn
	mo->angle = actor->angle;

	if (actor->eflags & MFE_VERTICALFLIP)
		mo->flags2 |= MF2_OBJECTFLIP;

	P_SetMobjState(mo, state);
}

void A_ParticleSpawn(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	fixed_t speed;
	mobjtype_t type;
	mobj_t *spawn;

	if (udmf) // dont make this crap work in durr maps kek
		return;

	if (LUA_CallAction(A_PARTICLESPAWN, actor))
		return;

	if (!actor->spawnpoint)
	{
		P_RemoveMobj(actor);
		return;
	}

	if (locvar1)
		type = (mobjtype_t)locvar1;
	else
		type = MT_PARTICLE;

	speed = FixedMul((actor->spawnpoint->angle >> 12)<<FRACBITS, actor->scale);

	spawn = P_SpawnMobj(actor->x, actor->y, actor->z, type);
	P_SetScale(spawn, actor->scale);
	spawn->momz = speed;
	spawn->destscale = FixedDiv(spawn->scale<<FRACBITS, 100<<FRACBITS);
	spawn->scalespeed = FixedDiv(((actor->spawnpoint->angle >> 8) & 63) * actor->scale, 100<<FRACBITS);
	actor->tics = actor->spawnpoint->extrainfo + 1;
}

// Function: A_MultiShotDist
//
// Description: Spawns multiple shots based on player proximity
//
// var1 = same as A_MultiShot
// var2 = same as A_MultiShot
//
void A_MultiShotDist(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_MULTISHOTDIST, actor))
		return;

	{
		uint8_t i;
		// Quick! Look through players!
		// Don't spawn dust unless a player is relatively close by (var1).
		for (i = 0; i < MAXPLAYERS; ++i)
			if (playeringame[i] && players[i].mo
			 && P_AproxDistance(actor->x - players[i].mo->x, actor->y - players[i].mo->y) < (1600<<FRACBITS))
				break; // Stop looking.
		if (i == MAXPLAYERS)
			return; // don't make bubble!
	}

	var1 = locvar1;
	var2 = locvar2;
	A_MultiShot(actor);
}

// Function: A_CheckFlags2
//
// Description: If actor->flags2 & var1, goto var2.
//
// var1 = mask
// var2 = state to go
//
void A_CheckFlags2(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_CHECKFLAGS2, actor))
		return;

	if (actor->flags2 & locvar1)
		P_SetMobjState(actor, (statenum_t)locvar2);
}

// Function: A_CheckScriptArg
//
// Description: If actor->script_args[var1] != 0, goto var2.
//
// var1 = mask
// var2 = state to go
//
void A_CheckScriptArg(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (locvar1 >= NUM_SCRIPT_ARGS)
		return;

	if (LUA_CallAction(A_CHECKSCRIPTARG, actor))
		return;

	if (actor->script_args[locvar1])
		P_SetMobjState(actor, (statenum_t)locvar2);
}

// Function: A_DoNPCSkid
//
// Description: Something that looks like a player is skidding.
//
// var1 = state to change to upon being slow enough
// var2 = minimum speed
//
void A_DoNPCSkid(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	fixed_t x, y, z;

	if (LUA_CallAction(A_DONPCSKID, actor))
		return;

	x = actor->x;
	y = actor->y;
	z = actor->z;

	if (!locvar2)
		locvar2 = FRACUNIT/2;

	if ((FixedHypot(actor->momx, actor->momy) < locvar2)
	|| !P_TryMove(actor, actor->x + actor->momx, actor->y + actor->momy, false, NULL))
	{
		actor->momx = actor->momy = 0;
		P_SetMobjState(actor, locvar1);
		return;
	}
	else
	{
		actor->momx = (2*actor->momx)/3;
		actor->momy = (2*actor->momy)/3;
	}

	P_MoveOrigin(actor, x, y, z);

	// Spawn a particle every 3 tics.
	if (!(leveltime % 3))
	{
		mobj_t *particle = P_SpawnMobjFromMobj(actor, 0, 0, 0, MT_SPINDUST);
		particle->tics = 10;

		P_SetScale(particle, 2*actor->scale/3);
		particle->destscale = actor->scale;
		P_SetObjectMomZ(particle, FRACUNIT, false);
	}
}

// Function: A_DoNPCPain
//
// Description: Something that looks like a player was hit, put them in pain.
//
// var1 = If zero, always fling the same amount.
//        Otherwise, slowly reduce the vertical
//        and horizontal speed to the base value
//        multiplied by this the more damage is done.
// var2 = If zero, use default fling values.
//        Otherwise, vertical and horizontal speed
//        will be multiplied by this.
//
void A_DoNPCPain(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	fixed_t vspeed = 0;
	fixed_t hspeed = FixedMul(4*FRACUNIT, actor->scale);

	if (LUA_CallAction(A_DONPCPAIN, actor))
		return;

	actor->flags &= ~(MF_NOGRAVITY|MF_NOCLIP|MF_NOCLIPHEIGHT);

	var1 = var2 = 0;
	A_Pain(actor);

	actor->z += P_MobjFlip(actor);

	if (actor->eflags & MFE_UNDERWATER)
		vspeed = FixedDiv(10511*FRACUNIT,2600*FRACUNIT);
	else
		vspeed = FixedDiv(69*FRACUNIT,10*FRACUNIT);

	if (actor->target)
		actor->angle = R_PointToAngle2(actor->x, actor->y, actor->target->x + actor->target->momx, actor->target->y + actor->target->momy);

	if (locvar1)
	{
		if (!actor->info->spawnhealth)
			return; // there's something very wrong here if you're using this action on something with no starting health
		locvar1 += ((FRACUNIT - locvar1)/actor->info->spawnhealth)*actor->health;
		hspeed = FixedMul(hspeed, locvar1);
		vspeed = FixedMul(vspeed, locvar1);
	}

	if (locvar2)
	{
		hspeed = FixedMul(hspeed, locvar2);
		vspeed = FixedMul(vspeed, locvar2);
	}

	P_SetObjectMomZ(actor, vspeed, false);
	P_InstaThrust(actor, actor->angle, -hspeed);
}

// Function: A_PrepareRepeat
//
// Description: Simple way to prepare A_Repeat.
//
// var1 = value to set extravalue2 to
// var2 = unused
//
void A_PrepareRepeat(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;

	if (LUA_CallAction(A_PREPAREREPEAT, actor))
		return;

	actor->extravalue2 = locvar1;
}

// Function: A_LookForBetter
//
// Description: A_Look, except it finds a better target in multiplayer, and doesn't lose the target in singleplayer.
//
// var1 lower 16 bits = 0 - looks only in front, 1 - looks all around
// var1 upper 16 bits = distance limit
// var2 = unused
//
void A_LookForBetter(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;

	if (LUA_CallAction(A_LOOKFORBETTER, actor))
		return;

	P_LookForPlayers(actor, (locvar1 & 65535), false, FixedMul((locvar1 >> 16)*FRACUNIT, actor->scale));
	A_FaceTarget(actor);
}

// Function: A_ModuloToState
//
// Description: Modulo operation to state
//
// var1 = Modulo
// var2 = State
//
void A_ModuloToState(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_MODULOTOSTATE, actor))
		return;

	if ((modulothing % locvar1 == 0))
		P_SetMobjState(actor, (locvar2));
	modulothing++;
}

// Function: A_ChangeHeight
//
// Description: Changes the actor's height by var1
//
// var1 = height
// var2 =
//     &1: height is absolute
//     &2: scale with actor's scale
//
void A_ChangeHeight(void *thing)
{
	mobj_t *actor = thing;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;
	fixed_t height = locvar1;
	dboolean reverse;

	if (LUA_CallAction(A_CHANGEHEIGHT, actor))
		return;

	reverse = (actor->eflags & MFE_VERTICALFLIP) || (actor->flags2 & MF2_OBJECTFLIP);

	if (locvar2 & 2)
		height = FixedMul(height, actor->scale);

	P_UnsetThingPosition(actor);
	if (locvar2 & 1)
	{
		if (reverse)
			actor->z += actor->height - locvar1;
		actor->height = locvar1;
	}
	else
	{
		if (reverse)
			actor->z -= locvar1;
		actor->height += locvar1;
	}
	P_SetThingPosition(actor);
}

//
// SRB2Kart
//

void A_ItemPop(void *thing)
{
	mobj_t *actor = thing;
	mobj_t *remains;
	mobjtype_t explode;

	if (LUA_CallAction(A_ITEMPOP, actor))
		return;

	// de-solidify
	P_UnsetThingPosition(actor);
	actor->flags &= ~MF_SOLID;
	actor->flags |= MF_NOCLIP;
	P_SetThingPosition(actor);

	// item explosion
	explode = mobjinfo[actor->info->damage].mass;
	remains = P_SpawnMobj(actor->x, actor->y,
		((actor->eflags & MFE_VERTICALFLIP) ? (actor->z + 3*(actor->height/4) - FixedMul(mobjinfo[explode].height, actor->scale)) : (actor->z + actor->height/4)), explode);
	if (actor->eflags & MFE_VERTICALFLIP)
	{
		remains->eflags |= MFE_VERTICALFLIP;
		remains->flags2 |= MF2_OBJECTFLIP;
	}
	remains->destscale = actor->destscale;
	P_SetScale(remains, actor->scale);

	remains = P_SpawnMobj(actor->x, actor->y, actor->z, actor->info->damage);
	remains->type = actor->type; // Transfer type information
	P_UnsetThingPosition(remains);
	if (sector_list)
	{
		P_DelSeclist(sector_list);
		sector_list = NULL;
	}
	P_SetThingPosition(remains);
	remains->destscale = actor->destscale;
	P_SetScale(remains, actor->scale);
	remains->flags = actor->flags; // Transfer flags
	remains->flags2 = actor->flags2; // Transfer flags2
	remains->fuse = actor->fuse; // Transfer respawn timer
	remains->cvmem = leveltime;
	remains->threshold = (actor->threshold == 69 ? 69 : 68);
	// To insure this information doesn't have to be rediscovered every time you look at this function...
	// A threshold of 0 is for a "living", ordinary random item.
	// 68 means regular popped random item debris.
	// 69 means Karmabomb dropped Item behaviour
	remains->skin = NULL;
	remains->spawnpoint = actor->spawnpoint;

	// a pointless modification of global state causing bizarre collision problems... who could've thought?
	//P_SetTarget(&g_tm.thing, remains);

	//if (actor->info->deathsound)
		//S_StartSound(remains, actor->info->deathsound);

	if (actor->target && actor->target->player)
	{

		if (!((gametypes[gametype]->rules & GTR_BUMPERS) && actor->target->player->bumper <= 0) && !itembreaker)
		{
			K_StartRoulette(actor->target->player, KROULETTETYPE_NORMAL);
		}
		else if (itembreaker)
		{
			K_AwardScaledPlayerRings(actor->target->player, ASR_ITEMBOX);

			if (numtargets < 4)
			{
				actor->target->player->itemtype = KITEM_POGOSPRING;
				actor->target->player->itemamount += 1;
			}
		}
	}

	remains->flags2 &= ~MF2_AMBUSH;

	// Here at mapload in battle?
	if (gametypes[gametype]->rules & GTR_BUMPERS && actor->threshold != 69)
		numgotboxes++;

	P_RemoveMobj(actor);
}

void A_JawzChase(void *thing)
{
	mobj_t *actor = thing;
	player_t *player;

	if (LUA_CallAction(A_JAWZCHASE, actor))
		return;

	if (actor->tracer)
	{

		if (actor->tracer->health)
		{
			mobj_t *ret;

			ret = P_SpawnMobj(actor->tracer->x, actor->tracer->y, actor->tracer->z, MT_PLAYERRETICULE);
			P_SetTarget(&ret->target, actor->tracer);
			ret->old_x = actor->tracer->old_x;
			ret->old_y = actor->tracer->old_y;
			ret->old_z = actor->tracer->old_z;
			ret->frame |= ((leveltime % 10) / 2) + 5;
			ret->color = actor->cvmem;

			P_Thrust(actor, R_PointToAngle2(actor->x, actor->y, actor->tracer->x, actor->tracer->y), (7*actor->movefactor)/64);
			return;
		}
		else
			P_SetTarget(&actor->tracer, NULL);
	}

	if (actor->extravalue1) // Disable looking by setting this
		return;

	if (!actor->target || P_MobjWasRemoved(actor->target)) // No source!
		return;

	player = K_FindJawzTarget(actor, actor->target->player);
	if (player)
		P_SetTarget(&actor->tracer, player->mo);

	return;
}

void A_JawzExplode(void *thing)
{
	mobj_t *actor = thing;
	int32_t shrapnel = 2;
	mobj_t *truc;

	if (LUA_CallAction(A_JAWZEXPLODE, actor))
		return;

	truc = P_SpawnMobj(actor->x, actor->y, actor->z, MT_BOOMEXPLODE);
	truc->scale = actor->scale*2;
	truc->color = SKINCOLOR_KETCHUP;

	while (shrapnel)
	{
		int32_t speed, speed2;

		fixed_t rand_x;
		fixed_t rand_y;
		fixed_t rand_z;

		rand_z = actor->z + P_RandomRange(PR_EXPLOSION, 0, 8)*FRACUNIT;
		rand_y = actor->y + P_RandomRange(PR_EXPLOSION, -8, 8)*FRACUNIT;
		rand_x = actor->x + P_RandomRange(PR_EXPLOSION, -8, 8)*FRACUNIT;

		truc = P_SpawnMobj(rand_x, rand_y, rand_z, MT_BOOMPARTICLE);
		truc->scale = actor->scale*2;

		speed = FixedMul(7*FRACUNIT, actor->scale)>>FRACBITS;
		truc->momx = P_RandomRange(PR_EXPLOSION, -speed, speed)*FRACUNIT;
		truc->momy = P_RandomRange(PR_EXPLOSION, -speed, speed)*FRACUNIT;

		speed = FixedMul(5*FRACUNIT, actor->scale)>>FRACBITS;
		speed2 = FixedMul(15*FRACUNIT, actor->scale)>>FRACBITS;
		truc->momz = P_RandomRange(PR_EXPLOSION, speed, speed2)*FRACUNIT;
		truc->tics = TICRATE*2;
		truc->color = SKINCOLOR_KETCHUP;

		shrapnel--;
	}

	return;
}

void A_SPBChase(void *thing)
{
	mobj_t *actor = thing;
	player_t *player = NULL;
	uint8_t i;
	uint8_t bestrank = UINT8_MAX;
	fixed_t dist;
	angle_t hang, vang;
	fixed_t wspeed, xyspeed, zspeed;

	if (LUA_CallAction(A_SPBCHASE, actor))
		return;

	// Default speed
	wspeed = actor->movefactor;

	if (actor->threshold) // Just fired, go straight.
	{
		actor->lastlook = -1;
		spbplace = -1;
		P_InstaThrust(actor, actor->angle, wspeed);
		actor->flags &=  ~MF_NOCLIPTHING;	// just in case.
		return;
	}

	// Find the player with the best rank. In gametypes with SPB forcing on WANTED
	// players, find our most-wanted player.
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator || players[i].exiting)
			continue; // not in-game

		/*if (!players[i].mo)
			continue; // no mobj

		if (players[i].mo->health <= 0)
			continue; // dead

		if (players[i].respawn)
			continue;*/ // respawning

		if ((gametypes[gametype]->rules & GTR_WANTED) && (gametypes[gametype]->rules & GTR_WANTEDSPB))
		{
			if (K_IsPlayerMostWanted(&players[i]))
			{
				player = &players[i];
				break;
			}
		}
		else if (players[i].position < bestrank)
		{
			bestrank = players[i].position;
			player = &players[i];
		}
	}

	if (actor->extravalue1 == 1) // MODE: TARGETING
	{
		if (actor->tracer && actor->tracer->health && !(actor->tracer->player && actor->tracer->player->spectator))
		{

			fixed_t defspeed = wspeed;
			fixed_t range = (160*actor->tracer->scale);
			fixed_t cx = 0, cy =0;

			// we're tailing a player, now's a good time to regain our damage properties
			actor->flags &=  ~MF_NOCLIPTHING;

			// Play the intimidating gurgle
			if (!S_SoundPlaying(actor, actor->info->activesound))
				S_StartSound(actor, actor->info->activesound);

			// Maybe we want SPB to target an object later? IDK lol
			if (actor->tracer->player)
			{
				uint8_t fracmax = 32;
				uint8_t spark = ((10-actor->tracer->player->kartspeed) + actor->tracer->player->kartweight) / 2;
				fixed_t easiness = ((actor->tracer->player->kartspeed + (10-spark)) << FRACBITS) / 2;

				actor->lastlook = actor->tracer->player-players; // Save the player num for death scumming...

				if (!P_IsObjectOnGround(actor->tracer) /*&& !actor->tracer->player->pogospring*/)
				{
					// In the air you have no control; basically don't hit unless you make a near complete stop
					defspeed = (7 * actor->tracer->player->speed) / 8;
				}
				else
				{
					// 7/8ths max speed for Knuckles, 3/4ths max speed for min accel, exactly max speed for max accel
					defspeed = FixedMul(((fracmax+1)<<FRACBITS) - easiness, K_GetKartSpeed(actor->tracer->player, false, false)) / fracmax;

					// gradually scale this with dash area (but not outrun, it's in the name lol)
					if (actor->extravalue3 != actor->tracer->player->dashareapower)
					{
						actor->extravalue3 = ApproachFixed(actor->extravalue3, actor->tracer->player->dashareapower, actor->tracer->player->dashareapower/(2*TICRATE), actor->extravalue3);
					}
					defspeed = FixedMul(defspeed, FRACUNIT + actor->extravalue3);
				}

				// Be fairer on conveyors
				cx = actor->tracer->player->cmomx;
				cy = actor->tracer->player->cmomy;

				// Switch targets if you're no longer 1st for long enough
				if (actor->tracer->player->position <= bestrank)
					actor->extravalue2 = 7*TICRATE;
				else if (actor->extravalue2-- <= 0)
					actor->extravalue1 = 0; // back to SEEKING

				if ((gametypes[gametype]->rules & GTR_WANTED) && (gametypes[gametype]->rules & GTR_WANTEDSPB))
				{
					spbplace = (actor->tracer->player - players);
				}
				else
				{
					spbplace = actor->tracer->player->position;
				}
			}

			if ((K_RingsActive() == true) ) // Spawn rings to make up for the fact first has ring grabbing
			{
				if (leveltime % 10 == 0)
				{
					mobj_t *specialring = P_SpawnMobj(actor->x, actor->y, actor->z, MT_RING);
					specialring->colorized = true;
					specialring->color = SKINCOLOR_RED;
					specialring->flags |= MF_NOCLIPHEIGHT;

					// But don't let first pick these up.
					specialring->extravalue3 = 1;
				}

			}

			dist = P_AproxDistance(P_AproxDistance(actor->x-actor->tracer->x, actor->y-actor->tracer->y), actor->z-actor->tracer->z);

			wspeed = FixedMul(defspeed, FRACUNIT + FixedDiv(dist-range, range));
			if (wspeed < defspeed)
				wspeed = defspeed;
			if (wspeed > (3*defspeed)/2)
				wspeed = (3*defspeed)/2;
			if (wspeed < 20*actor->tracer->scale)
				wspeed = 20*actor->tracer->scale;
			if (actor->tracer->player->carry == CR_SLIDING)
				wspeed = actor->tracer->player->speed/2;
			//  ^^^^ current section: These are annoying, and grand metropolis in particular needs this.

			hang = R_PointToAngle2(actor->x, actor->y, actor->tracer->x, actor->tracer->y);
			vang = R_PointToAngle2(0, actor->z, dist, actor->tracer->z);

			// Modify stored speed
			if (wspeed > actor->cvmem)
				actor->cvmem += (wspeed - actor->cvmem) / TICRATE;
			else
				actor->cvmem = wspeed;

			{
				// Smoothly rotate horz angle
				angle_t input = hang - actor->angle;
				dboolean invert = (input > ANGLE_180);
				if (invert)
					input = InvAngle(input);

				// Slow down when turning; it looks better and makes U-turns not unfair
				xyspeed = FixedMul(actor->cvmem, max(0, (((180<<FRACBITS) - AngleFixed(input)) / 90) - FRACUNIT));

				input = FixedAngle(AngleFixed(input)/4);
				if (invert)
					input = InvAngle(input);

				actor->angle += input;

				// Smoothly rotate vert angle
				input = vang - actor->movedir;
				invert = (input > ANGLE_180);
				if (invert)
					input = InvAngle(input);

				// Slow down when turning; might as well do it for momz, since we do it above too
				zspeed = FixedMul(actor->cvmem, max(0, (((180<<FRACBITS) - AngleFixed(input)) / 90) - FRACUNIT));

				input = FixedAngle(AngleFixed(input)/4);
				if (invert)
					input = InvAngle(input);

				actor->movedir += input;
			}

			actor->momx = cx + FixedMul(FixedMul(xyspeed, FINECOSINE(actor->angle>>ANGLETOFINESHIFT)), FINECOSINE(actor->movedir>>ANGLETOFINESHIFT));
			actor->momy = cy + FixedMul(FixedMul(xyspeed, FINESINE(actor->angle>>ANGLETOFINESHIFT)), FINECOSINE(actor->movedir>>ANGLETOFINESHIFT));
			actor->momz = FixedMul(zspeed, FINESINE(actor->movedir>>ANGLETOFINESHIFT));

			// Red speed lines for when it's gaining on its target. A tell for when you're starting to lose too much speed!
			if (R_PointToDist2(0, 0, actor->momx, actor->momy) > (actor->tracer->player ? (16*actor->tracer->player->speed)/15
				: (16*R_PointToDist2(0, 0, actor->tracer->momx, actor->tracer->momy))/15) // Going faster than the target
				&& xyspeed > K_GetKartSpeed(actor->tracer->player, false, false)/4) // Don't display speedup lines at pitifully low speeds
			{
				fixed_t rand_x;
				fixed_t rand_y;
				fixed_t rand_z;

				rand_z = actor->z + (actor->height/2) + (P_RandomRange(PR_DECORATION,-24,24) * actor->scale);
				rand_y = actor->y + (P_RandomRange(PR_DECORATION,-24,24) * actor->scale);
				rand_x = actor->x + (P_RandomRange(PR_DECORATION,-24,24) * actor->scale);

				mobj_t *fast = P_SpawnMobj(rand_x, rand_y, rand_z, MT_FASTLINE);
				fast->angle = R_PointToAngle2(0, 0, actor->momx, actor->momy);
				//fast->momx = (3*actor->momx)/4;
				//fast->momy = (3*actor->momy)/4;
				//fast->momz = (3*actor->momz)/4;
				fast->color = SKINCOLOR_RED;
				fast->colorized = true;
				P_SetTarget(&fast->target, actor); // easier lua access
				K_MatchGenericExtraFlags(fast, actor);
			}

			return;
		}
		else // Target's gone, return to SEEKING
		{
			P_SetTarget(&actor->tracer, NULL);
			actor->extravalue1 = 2; // WAIT...
			actor->extravalue2 = 52; // Slightly over the respawn timer length
			return;
		}
	}
	else if (actor->extravalue1 == 2) // MODE: WAIT...
	{
		actor->momx = actor->momy = actor->momz = 0; // Stoooop

		// don't hurt players that have nothing to do with this:
		actor->flags |= MF_NOCLIPTHING;

		if (actor->lastlook != -1
			&& playeringame[actor->lastlook]
			&& !players[actor->lastlook].spectator
			&& !players[actor->lastlook].exiting)
		{
			if ((gametypes[gametype]->rules & GTR_WANTED) && (gametypes[gametype]->rules & GTR_WANTEDSPB))
			{
				spbplace = actor->lastlook;
			}
			else
			{
				spbplace = players[actor->lastlook].position;
			}

			if (actor->extravalue2-- <= 0 && players[actor->lastlook].mo)
			{
				P_SetTarget(&actor->tracer, players[actor->lastlook].mo);
				actor->extravalue1 = 1; // TARGET ACQUIRED
				actor->extravalue2 = 7*TICRATE;
				actor->cvmem = wspeed;
			}
		}
		else
		{
			actor->extravalue1 = 0; // SEEKING
			actor->extravalue2 = 0;
			spbplace = -1;
		}
	}
	else // MODE: SEEKING
	{
		actor->lastlook = -1; // Just make sure this is reset

		if (!player || !player->mo || player->mo->health <= 0 || player->respawn)
		{
			// No one there? Completely STOP.
			actor->momx = actor->momy = actor->momz = 0;
			if (!player)
				spbplace = -1;
			return;
		}

		// Found someone, now get close enough to initiate the slaughter...

		// don't hurt players that have nothing to do with this:
		actor->flags |= MF_NOCLIPTHING;

		P_SetTarget(&actor->tracer, player->mo);
		spbplace = bestrank;

		dist = P_AproxDistance(P_AproxDistance(actor->x-actor->tracer->x, actor->y-actor->tracer->y), actor->z-actor->tracer->z);

		hang = R_PointToAngle2(actor->x, actor->y, actor->tracer->x, actor->tracer->y);
		vang = R_PointToAngle2(0, actor->z, dist, actor->tracer->z);

		{
			// Smoothly rotate horz angle
			angle_t input = hang - actor->angle;
			dboolean invert = (input > ANGLE_180);
			if (invert)
				input = InvAngle(input);

			// Slow down when turning; it looks better and makes U-turns not unfair
			xyspeed = FixedMul(wspeed, max(0, (((180<<FRACBITS) - AngleFixed(input)) / 90) - FRACUNIT));

			input = FixedAngle(AngleFixed(input)/4);
			if (invert)
				input = InvAngle(input);

			actor->angle += input;

			// Smoothly rotate vert angle
			input = vang - actor->movedir;
			invert = (input > ANGLE_180);
			if (invert)
				input = InvAngle(input);

			// Slow down when turning; might as well do it for momz, since we do it above too
			zspeed = FixedMul(wspeed, max(0, (((180<<FRACBITS) - AngleFixed(input)) / 90) - FRACUNIT));

			input = FixedAngle(AngleFixed(input)/4);
			if (invert)
				input = InvAngle(input);

			actor->movedir += input;
		}

		actor->momx = FixedMul(FixedMul(xyspeed, FINECOSINE(actor->angle>>ANGLETOFINESHIFT)), FINECOSINE(actor->movedir>>ANGLETOFINESHIFT));
		actor->momy = FixedMul(FixedMul(xyspeed, FINESINE(actor->angle>>ANGLETOFINESHIFT)), FINECOSINE(actor->movedir>>ANGLETOFINESHIFT));
		actor->momz = FixedMul(zspeed, FINESINE(actor->movedir>>ANGLETOFINESHIFT));

		if (dist <= (3072*actor->tracer->scale)) // Close enough to target?
		{
			S_StartSound(actor, actor->info->attacksound); // Siren sound; might not need this anymore, but I'm keeping it for now just for debugging.
			actor->extravalue1 = 1; // TARGET ACQUIRED
			actor->extravalue2 = 7*TICRATE;
			actor->cvmem = wspeed;
		}
	}

	return;
}

static mobj_t *grenade;
static fixed_t explodedist;

static inline BlockItReturn_t PIT_SSMineSearch(mobj_t *thing)
{
	if (!grenade)
		return BMIT_ABORT;

	if (grenade->flags2 & MF2_DEBRIS)
		return BMIT_ABORT;

	if (thing->type != MT_PLAYER) // Don't explode for anything but an actual player.
		return BMIT_CONTINUE;

	if (!(thing->flags & MF_SHOOTABLE))
	{
		// didn't do any damage
		return BMIT_CONTINUE;
	}

	if (netgame && thing->player && thing->player->spectator)
		return BMIT_CONTINUE;

	if (thing == grenade->target && grenade->threshold != 0) // Don't blow up at your owner.
		return BMIT_CONTINUE;

	if (thing->player && (thing->player->hyudorotimer
		|| ((gametypes[gametype]->rules & GTR_BUMPERS) && thing->player && thing->player->bumper <= 0 && thing->player->karmadelay)))
		return BMIT_CONTINUE;

	// see if it went over / under
	if (grenade->z - explodedist > thing->z + thing->height)
		return BMIT_CONTINUE; // overhead
	if (grenade->z + grenade->height + explodedist < thing->z)
		return BMIT_CONTINUE; // underneath

	if (P_AproxDistance(P_AproxDistance(thing->x - grenade->x, thing->y - grenade->y),
		thing->z - grenade->z) > explodedist)
		return BMIT_CONTINUE; // Too far away

	// Explode!
	P_SetMobjState(grenade, grenade->info->deathstate);
	return BMIT_ABORT;
}

void A_SSMineSearch(void *thing)
{
	mobj_t *actor = thing;
	int32_t bx, by, xl, xh, yl, yh;
	explodedist = FixedMul(actor->info->painchance, mapobjectscale);

	if (LUA_CallAction(A_SSMINESEARCH, actor))
		return;

	if (actor->flags2 & MF2_DEBRIS)
		return;

	if (actor->state == &states[S_SSMINE_DEPLOY8])
		explodedist = (3*explodedist)/2;

	if (leveltime % 35 == 0)
		S_StartSound(actor, actor->info->activesound);

	// Use blockmap to check for nearby shootables
	yh = (unsigned)(actor->y + explodedist - bmaporgy)>>MAPBLOCKSHIFT;
	yl = (unsigned)(actor->y - explodedist - bmaporgy)>>MAPBLOCKSHIFT;
	xh = (unsigned)(actor->x + explodedist - bmaporgx)>>MAPBLOCKSHIFT;
	xl = (unsigned)(actor->x - explodedist - bmaporgx)>>MAPBLOCKSHIFT;

	grenade = actor;

	for (by = yl; by <= yh; by++)
		for (bx = xl; bx <= xh; bx++)
			P_BlockThingsIterator(bx, by, PIT_SSMineSearch);
}

static inline BlockItReturn_t PIT_MineExplode(mobj_t *thing)
{
	if (!grenade || P_MobjWasRemoved(grenade))
		return BMIT_ABORT; // There's the possibility these can chain react onto themselves after they've already died if there are enough all in one spot

	if (grenade->flags2 & MF2_DEBRIS) // don't explode twice
		return BMIT_ABORT;

	if (thing == grenade || thing->type == MT_MINEEXPLOSIONSOUND) // Don't explode yourself! Endless loop!
		return BMIT_CONTINUE;

	if (!(thing->flags & MF_SHOOTABLE) || (thing->flags & MF_SCENERY))
		return BMIT_CONTINUE;

	if (netgame && thing->player && thing->player->spectator)
		return BMIT_CONTINUE;

	if ((gametypes[gametype]->rules & GTR_BUMPERS) && grenade->target && grenade->target->player && grenade->target->player->bumper <= 0 && thing == grenade->target)
		return BMIT_CONTINUE;

	// see if it went over / under
	if (grenade->z - explodedist > thing->z + thing->height)
		return BMIT_CONTINUE; // overhead
	if (grenade->z + grenade->height + explodedist < thing->z)
		return BMIT_CONTINUE; // underneath

	if (P_AproxDistance(P_AproxDistance(thing->x - grenade->x, thing->y - grenade->y),
		thing->z - grenade->z) > explodedist)
		return BMIT_CONTINUE; // Too far away

	P_DamageMobj(thing, grenade, grenade->target, 1, DMG_EXPLODE);
	return BMIT_CONTINUE;
}

void A_SSMineExplode(void *thing)
{
	mobj_t *actor = thing;
	int32_t bx, by, xl, xh, yl, yh;
	int32_t d;
	int32_t locvar1 = var1;
	mobjtype_t type;
	skincolornum_t color = SKINCOLOR_KETCHUP;
	explodedist = FixedMul((3*actor->info->painchance)/2, mapobjectscale);

	if (LUA_CallAction(A_SSMINEEXPLODE, actor))
		return;

	if (actor->flags2 & MF2_DEBRIS)
		return;

	type = (mobjtype_t)locvar1;

	// Use blockmap to check for nearby shootables
	yh = (unsigned)(actor->y + explodedist - bmaporgy)>>MAPBLOCKSHIFT;
	yl = (unsigned)(actor->y - explodedist - bmaporgy)>>MAPBLOCKSHIFT;
	xh = (unsigned)(actor->x + explodedist - bmaporgx)>>MAPBLOCKSHIFT;
	xl = (unsigned)(actor->x - explodedist - bmaporgx)>>MAPBLOCKSHIFT;

	BMBOUNDFIX (xl, xh, yl, yh);

	grenade = actor;

	for (by = yl; by <= yh; by++)
		for (bx = xl; bx <= xh; bx++)
			P_BlockThingsIterator(bx, by, PIT_MineExplode);

	for (d = 0; d < 16; d++)
		K_SpawnKartExplosion(actor->x, actor->y, actor->z, explodedist + 32*mapobjectscale, 32, type, d*(ANGLE_45/4), true, false, actor->target); // 32 <-> 64

	if (!P_MobjWasRemoved(actor->target) && actor->target->player)
		color = actor->target->player->skincolor;

	K_SpawnMineExplosion(actor, color);

	P_SpawnMobj(actor->x, actor->y, actor->z, MT_MINEEXPLOSIONSOUND);

	actor->flags2 |= MF2_DEBRIS;	// Set this flag to ensure that the explosion won't be effective more than 1 frame.
}

void A_LandMineExplode(void *thing)
{
	mobj_t *actor = thing;
	mobj_t *expl;
	int32_t colour = SKINCOLOR_KETCHUP;	// we spell words properly here
	int32_t i;
	mobj_t *smoldering;

	if (LUA_CallAction(A_LANDMINEEXPLODE, actor))
		return;

	// we'll base the explosion "timer" off of some stupid variable like uh... cvmem!
	// Yeah let's use cvmem since nobody uses that

	if (actor->target && !P_MobjWasRemoved(actor->target))
		colour = actor->target->color;

	// Spawn smoke remains:
	smoldering = P_SpawnMobj(actor->x, actor->y, actor->z, MT_SMOLDERING);
	P_SetScale(smoldering, actor->scale);
	smoldering->tics = TICRATE*3;

	actor->fuse = actor->tics;	// disappear when this state ends.

	// spawn a few physics explosions
	for (i = 0; i < 15; i++)
	{
		expl = P_SpawnMobj(actor->x, actor->y, actor->z + actor->scale, MT_BOOMEXPLODE);
		expl->color = colour;
		expl->tics = (i+1);

		//K_MatchGenericExtraFlags(expl, actor);
		P_SetScale(expl, actor->scale*4);

		expl->momx = P_RandomRange(PR_EXPLOSION, -3, 3)*actor->scale/2;
		expl->momy = P_RandomRange(PR_EXPLOSION, -3, 3)*actor->scale/2;

		// 100/45 = 2.22 fu/t
		expl->momz = ((i+1)*actor->scale*5/2)*P_MobjFlip(expl);
	}
}

void A_EggMinePop(void *thing)
{
	mobj_t *actor = thing;
	mobj_t *poof;
	mobj_t *landMine;
	player_t *player;

	if (LUA_CallAction(A_EGGMINEPOP, actor))
		return;

	if (actor->lastlook == 0) return;

	poof = P_SpawnMobj(actor->x, actor->y, actor->z, MT_EXPLODE);
	S_StartSound(poof, actor->info->deathsound);

	if (!(actor->target && actor->target->player)) return;

	player = actor->target->player;

	landMine = P_SpawnMobj(actor->x, actor->y, actor->z + actor->height/2, MT_LANDMINE);
	K_FlipFromObject(landMine, actor);
	landMine->threshold = 10;
	landMine->target = player->mo;

	landMine->destscale = mapobjectscale;
	P_SetScale(landMine, 3*actor->scale/4);

	landMine->angle = actor->angle;

	landMine->momz = (28 * mapobjectscale * P_MobjFlip(actor));
	landMine->color = player->skincolor;
}

void A_BallhogExplode(void *thing)
{
	mobj_t *actor = thing;
	mobj_t *mo2;

	if (LUA_CallAction(A_BALLHOGEXPLODE, actor))
		return;

	mo2 = P_SpawnMobj(actor->x, actor->y, actor->z, MT_BALLHOGBOOM);
	P_SetScale(mo2, actor->scale*2);
	mo2->destscale = mo2->scale;
	S_StartSound(mo2, actor->info->deathsound);
	return;
}

// A_LightningFollowPlayer:
// Dumb simple function that gives a mobj its target's momentums without updating its angle.
void A_LightningFollowPlayer(void *thing)
{
	mobj_t *actor = thing;
	fixed_t sx, sy;

	if (LUA_CallAction(A_LIGHTNINGFOLLOWPLAYER, actor))
		return;

	if (!actor->target)
		return;

	if (actor->extravalue1)	// Make the radius also follow the player somewhat accuratly
	{
		sx = actor->target->x + FixedMul((actor->target->scale*actor->extravalue1), FINECOSINE((actor->angle)>>ANGLETOFINESHIFT));
		sy = actor->target->y + FixedMul((actor->target->scale*actor->extravalue1), FINESINE((actor->angle)>>ANGLETOFINESHIFT));
		P_MoveOrigin(actor, sx, sy, actor->target->z);
	}
	else	// else just teleport to player directly
	{
		P_MoveOrigin(actor, actor->target->x, actor->target->y, actor->target->z);
	}

	K_MatchGenericExtraFlags(actor, actor->target);	// copy our target for graviflip
	actor->momx = actor->target->momx;
	actor->momy = actor->target->momy;
	actor->momz = actor->target->momz;	// Give momentum since we don't teleport to our player literally every frame.
}

// A_FZBoomFlash:
// Flash everyone close enough to the boom
void A_FZBoomFlash(void *thing)
{
	mobj_t *actor = thing;
	uint8_t i;

	if (LUA_CallAction(A_FZBOOMFLASH, actor))
		return;

	for (i = 0; i < MAXPLAYERS; i++)
	{
		fixed_t dist;
		if (!playeringame[i] || !players[i].mo)
			continue;
		dist = P_AproxDistance(P_AproxDistance(actor->x-players[i].mo->x, actor->y-players[i].mo->y), actor->z-players[i].mo->z);
		if (dist < 1536<<FRACBITS)
			P_FlashPal(&players[i], PAL_WHITE, 2);
	}
	return;
}

// A_FZBoomSmoke:
// Spawns pinkish smoke around the object
// Var1 is radius add
void A_FZBoomSmoke(void *thing)
{
	mobj_t *actor = thing;
	int32_t i;
	int32_t rad = 47+(23*var1);

	if (LUA_CallAction(A_FZBOOMSMOKE, actor))
		return;

	for (i = 0; i < 8+(4*var1); i++)
	{
		fixed_t rand_x;
		fixed_t rand_y;
		fixed_t rand_z;

		rand_z = actor->z + (P_RandomRange(PR_EXPLOSION, 0, 72)*actor->scale);
		rand_y = actor->y + (P_RandomRange(PR_EXPLOSION, -rad, rad)*actor->scale);
		rand_x = actor->x + (P_RandomRange(PR_EXPLOSION, -rad, rad)*actor->scale);

		mobj_t *smoke = P_SpawnMobj(rand_x, rand_y, rand_z, MT_THOK);

		P_SetMobjState(smoke, S_FZEROSMOKE1);
		smoke->tics += P_RandomRange(PR_EXPLOSION, -3, 4);
		smoke->scale = actor->scale*3;
	}
	return;
}

// A_RandomShadowFrame
// Gives a random sprite for the Mayonaka static shadows. Dumb and simple.
void A_RandomShadowFrame(void *thing)
{
	mobj_t *actor = thing;
	mobj_t *fire;
	mobj_t *fake;

	if (LUA_CallAction(A_RANDOMSHADOWFRAME, (actor)))
		return;

	if (!actor->extravalue1)	// Hack that spawns thoks that look like random shadows. Otherwise the state would overwrite our frame and that's a pain.
	{
		fake = P_SpawnMobj(actor->x, actor->y, actor->z, MT_THOK);
		fake->sprite = SPR_ENM1;
		fake->frame = P_RandomRange(PR_DECORATION, 0, 6);
		P_SetScale(fake, FRACUNIT*3/2);
		fake->scale = FRACUNIT*3/2;
		fake->destscale = FRACUNIT*3/2;
		fake->angle = actor->angle;
		fake->tics = -1;
		actor->renderflags |= RF_DONTDRAW;
		actor->extravalue1 = 1;
	}

	P_SetScale(actor, FRACUNIT*3/2);

	// I have NO CLUE how to hardcode all of that fancy Linedef Executor shit so the fire spinout will be done by these entities directly.
	if (P_LookForPlayers(actor, false, false, 380<<FRACBITS))	// got target
	{
		if (actor->target && !actor->target->player->flashing
		&& !actor->target->player->invincibilitytimer
		&& !actor->target->player->smonitortimer
		&& !actor->target->player->growshrinktimer
		&& !K_IsPlayerDamaged(actor->target->player)
		&& P_IsObjectOnGround(actor->target)
		&& actor->z == actor->target->z)
		{
			P_DamageMobj(actor->target, actor, actor, 1, DMG_NORMAL);
			P_InstaThrust(actor->target, actor->angle, 16<<FRACBITS);
			fire = P_SpawnMobj(actor->target->x, actor->target->y, actor->target->z, MT_THOK);
			P_SetMobjStateNF(fire, S_QUICKBOOM1);
			P_SetScale(fire, 4<<FRACBITS);
			fire->color = SKINCOLOR_KETCHUP;
			S_StartSound(actor->target, sfx_fire2);
		}
	}
	return;
}

// A_RoamingShadowThinker
// Thinker for Midnight Channel's Roaming Shadows:
void A_RoamingShadowThinker(void *thing)
{
	mobj_t *actor = thing;
	mobj_t *wind;

	if (LUA_CallAction(A_ROAMINGSHADOWTHINKER, (actor)))
		return;

	// extravalue1 replaces "movetimer"
	// extravalue2 replaces "stoptimer"

	P_SetScale(actor, FRACUNIT*3/2);
	if (!actor->extravalue2)
	{
		P_InstaThrust(actor, actor->angle, 8<<FRACBITS);	// move at 8 fracs / sec
		actor->extravalue1 = ((actor->extravalue1) ? (actor->extravalue1-1) : (TICRATE*5+1));	// deplete timer if set, set to 5 ticrate otherwise.
		if (actor->extravalue1 == 1)	// if timer reaches 1, do a u-turn.
		{
			actor->extravalue1 = 0;
			actor->extravalue2 = 60;
		}
		// Search for and attack Players venturing too close in front of us.

		if (P_LookForPlayers(actor, false, false, 256<<FRACBITS))	// got target
		{
			if (actor->target && !actor->target->player->flashing
			&& !actor->target->player->invincibilitytimer
			&& !actor->target->player->smonitortimer
			&& !actor->target->player->growshrinktimer
			&& !K_IsPlayerDamaged(actor->target->player))
			{
				// send them flying and spawn the WIND!
				P_InstaThrust(actor->target, 0, 0);
				P_DamageMobj(actor->target, actor, actor, 1, DMG_NORMAL);
				P_SetObjectMomZ(actor->target, 16<<FRACBITS, false);
				S_StartSound(actor->target, sfx_wind1);

				// Spawn the WIND:
				wind = P_SpawnMobj(actor->target->x, actor->target->y, actor->target->z, MT_THOK);	// Opaque layer:
				P_SetMobjState(wind, S_GARU1);
				P_SetScale(wind, FRACUNIT*3/2);
				wind = P_SpawnMobj(actor->target->x, actor->target->y, actor->target->z, MT_THOK);	// Translucent layer:
				P_SetMobjState(wind, S_TGARU0);
				P_SetScale(wind, FRACUNIT*3/2);
				wind->destscale = 30<<FRACBITS;
			}
		}
	}
	else	// Handle U-Turn
	{
		actor->angle += ANG1*3;
		actor->extravalue2--;
	}
	return;
}

// A_MayonakaArrow
// Used for the arrow sprite animations in Mayonaka. It's only extra visual bullshit to make em more random.

void A_MayonakaArrow(void *thing)
{
	mobj_t *actor = thing;
	int32_t flip = 0;
	int32_t iswarning;

	if (LUA_CallAction(A_MAYONAKAARROW, (actor)))
		return;

	iswarning = (actor->args[0] == TMMA_WARN);	// is our object a warning sign?

	// "animtimer" is replaced by "extravalue1" here.
	actor->extravalue1 = ((actor->extravalue1) ? (actor->extravalue1+1) : (P_RandomRange(PR_ACTION, 0, (iswarning) ? (TICRATE/2) : TICRATE*3)));
	flip = ((actor->args[0] == TMMA_FLIP) ? (3) : (0));	// flip adds 3 frames, which is the flipped version of the sign.
	// special warning behavior:
	if (iswarning)
		flip = 6;

	actor->frame = flip + actor->extravalue2*3;

	if (actor->extravalue1 >= TICRATE*7/2)
	{
		actor->extravalue1 = 0;	// reset to 0 and start a new cycle.
		// special behavior for warning sign; swap from warning to sneaker & reverse
		if (iswarning)
			actor->extravalue2 = (actor->extravalue2) ? (0) : (1);
	}
	else if (actor->extravalue1 > TICRATE*7/2 -4)
		actor->frame = flip+2;
	else if (actor->extravalue1 > TICRATE*3 && leveltime%2 > 0)
		actor->frame = flip+1;

	actor->frame |= FF_PAPERSPRITE;
	actor->momz = 0;
	return;
}

// A_MementosTPParticles
// Mementos teleporters particles effects. Short and simple.

void A_MementosTPParticles(void *thing)
{
	mobj_t *actor = thing;
	mobj_t *particle;
	mobj_t *mo2;
	int i = 0;
	thinker_t *th;
	fixed_t rand_x;
	fixed_t rand_y;
	fixed_t rand_z;

	if (LUA_CallAction(A_MEMENTOSTPPARTICLES, (actor)))
		return;

	for (; i<4; i++)
	{
		rand_z = actor->z + (P_RandomRange(PR_DECORATION, 48, 256)<<FRACBITS);
		rand_y = actor->y + (P_RandomRange(PR_DECORATION, -256, 256)<<FRACBITS);
		rand_x = actor->x + (P_RandomRange(PR_DECORATION, -256, 256)<<FRACBITS);

		particle = P_SpawnMobj(rand_x, rand_y, rand_z, MT_MEMENTOSPARTICLE);
		particle->frame = 0;
		particle->color = ((i%2) ? (SKINCOLOR_RED) : (SKINCOLOR_BLACK));
		particle->destscale = 1;
		P_HomingAttack(particle, actor); // Really now, Lat...
	}

	// Although this is mostly used to spawn particles, we will also save the OTHER teleport inside actor->target. That way teleporting doesn't require a thinker iteration.
	// Doesn't seem like much given the small amount of mobjs this map has but heh.
	if (!actor->target)
	{
		for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
		{
			if (th->function == (actionf_p1)P_RemoveThinkerDelayed)
				continue;

			mo2 = (mobj_t *)th;
			if (mo2->type == MT_MEMENTOSTP && mo2 != actor)
			{
				P_SetTarget(&actor->target, mo2);	// The main target we're pursing.
				break;
			}
		}
	}
}

// A_ReaperThinker
// Mementos's Reaper's thinker. A huge pain in the Derek Bum to translate from Lua to this shite if you ask me.

void A_ReaperThinker(void *thing)
{
	mobj_t *actor = thing;
	mobj_t *particle;	// particles to spawn
	int i = 0;			// for loops
	angle_t an = ANGLE_22h;		// Reminder that angle constants suck.

	//Waypoint stuff:
	mobj_t *mo2;
	thinker_t *th;

	//Player targetting stuff:
	uint32_t maxscore = 0;	// we target the player with the highest score so yeah there you go.
	player_t *player;	// used as a shortcut in a loop.
	mobj_t *targetplayermo = NULL;	// the player mo we can eventually target, or whatever.

	if (LUA_CallAction(A_REAPERTHINKER, (actor)))
		return;

	// We don't have custom variables or whatever so we'll do with whatever the fuck we have left.

	if (actor->health == 1000)	// if health is 1000, set it to a small scale and have it start growing with destscale. Then set the health to uh, not 1000.
	{
		P_SetScale(actor, 1);
		actor->destscale = 2<<FRACBITS;
		actor->scalespeed = FRACUNIT/24;	// Should take a bit less than 2 seconds to fully grow.
		S_StartSound(NULL, sfx_chain);
		actor->health--;	// now we have 999 health, so that above won't happen again. Awesome.
	}

	if (actor->scale < 2<<FRACBITS)	// we haven't finished growing YET.
	{
		// Spawn particles as we grow out of the floor, ゴ ゴ ゴ ゴ
		for (; i < 16; i++)
		{
			fixed_t rand_x;
			fixed_t rand_y;

			rand_y = actor->y + (P_RandomRange(PR_DECORATION, -60, 60)<<FRACBITS);
			rand_x = actor->x + (P_RandomRange(PR_DECORATION, -60, 60)<<FRACBITS);

			particle = P_SpawnMobj(rand_x, rand_y, actor->z, MT_THOK);
			particle->momz = 20<<FRACBITS;
			particle->color = ((i%2 !=0) ? (SKINCOLOR_RED) : (SKINCOLOR_BLACK));
			particle->frame = 0;
			P_SetScale(particle, FRACUNIT/2);
		}

		// Spawn particles in some edgy circle or w/e.

		if (leveltime%5 != 0)	// spawn the thing under that every tic.
			return;

		i=0;
		for (; i<15; i++)	// spawn in a circle formation or w/e.
		{
			particle = P_SpawnMobj(actor->x, actor->y, actor->z, MT_THOK);
			particle->momz = 20<<FRACBITS;
			particle->color = ((i%2 !=0) ? (SKINCOLOR_RED) : (SKINCOLOR_BLACK));
			particle->frame = 0;
			P_SetScale(particle, FRACUNIT/2);
			P_InstaThrust(particle, an*i, 30<<FRACBITS);
		}
		return;	// don't continue, what lies beyond that is the movement code.
	}

	// We finished growing and can now be a dangerous piece o' garbage scaring the living heck outta players!

	actor->flags = MF_NOGRAVITY|MF_PAIN|MF_SPECIAL|MF_NOCLIP|MF_NOCLIPHEIGHT;	// set our flags to be a damaging thing.
	// Handle animation:
	if (!(leveltime%5))
		actor->extravalue2 = (actor->extravalue2 < 9) ? (actor->extravalue2+1) : (0);	// Ghetto animation, but hey it works for what it's worth

	// Chain sfx
	if (!S_SoundPlaying(actor, sfx_chain))
		S_StartSound(actor, sfx_chain);

	actor->frame = actor->extravalue2;	// yes i'm that bad at maths don't @ me.

	if (!actor->target)
	{
		if (actor->hnext)
		{
			P_SetTarget(&actor->target, actor->hnext);	// Default back to last waypoint.
			return;
		}

		// We have no target and oughta find one, so let's scan through thinkers for a waypoint of angle 0, or something.
		for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
		{
			if (th->function == (actionf_p1)P_RemoveThinkerDelayed)
				continue;

			mo2 = (mobj_t *)th;

			if (mo2->type != MT_REAPERWAYPOINT)
				continue;
			if (mo2->spawnpoint->angle != 0)
				continue;

			P_SetTarget(&actor->target, mo2);	// The main target we're pursing.
			P_SetTarget(&actor->hnext, mo2);	// The last waypoint we hit. We will default back to that if a player goes out of our range!
			actor->extravalue1 = 0;	// This will store the angle of the last waypoint we touched. This will essentially be useful later on.
			if (!actor->tracer)	// If we already have a tracer (Waypoint #0), don't do anything.
			{
				P_SetTarget(&actor->tracer, mo2);	// Because our target might be a player OR a waypoint, we need some sort of fallback option. This will always be waypoint 0.
				break;
			}
		}
	}
	else	// Awesome, we now have a target.
	{
		// Follow target:
		P_InstaThrust(actor, R_PointToAngle2(actor->x, actor->y, actor->target->x, actor->target->y), 20<<FRACBITS);
		actor->angle = R_PointToAngle2(actor->x, actor->y, actor->target->x, actor->target->y);

		// The player we should target if it's near us:
		for (i=0; i<MAXPLAYERS; i++)
		{

			if (!playeringame[i])
				continue;

			player = &players[i];
			if (player && player->mo && player->bumper && player->score >= maxscore)
			{
				targetplayermo = player->mo;
				maxscore = player->score;
			}
		}

		// Try to target that player:
		if (targetplayermo)
		{
			if (P_LookForPlayers(actor, false, false, 1024<<FRACBITS))	// got target
			{
				if (!(actor->target == targetplayermo && actor->target && !actor->target->player->flashing
				&& !actor->target->player->invincibilitytimer
				&& !actor->target->player->smonitortimer
				&& !actor->target->player->growshrinktimer
				&& !K_IsPlayerDamaged(actor->target->player)))
					P_SetTarget(&actor->target, actor->hnext);
					// if the above isn't correct, then we should go back to targetting waypoints or something.
			}
		}

		// Waypoint behavior.
		if (actor->target->type == MT_REAPERWAYPOINT)
		{
			if (R_PointToDist2(actor->x, actor->y, actor->target->x, actor->target->y) < 22<<FRACBITS)
			{
				P_SetTarget(&actor->target, NULL);	// remove target so we can default back to first waypoint if things go ham.

				// If we reach close to a waypoint, then we should go to the NEXT one.
				for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
				{
					if (th->function == (actionf_p1)P_RemoveThinkerDelayed)
						continue;

					mo2 = (mobj_t *)th;

					if (mo2->type != MT_REAPERWAYPOINT)
						continue;
					if (mo2->spawnpoint->angle != actor->extravalue1+1)
						continue;

					P_SetTarget(&actor->target, mo2);	// The main target we're pursing.
					P_SetTarget(&actor->hnext, mo2);	// The last waypoint we hit. We will default back to that if a player goes out of our range!
					actor->extravalue1++;	// This will store the angle of the last waypoint we touched. This will essentially be useful later on.
					break;
				}
			}


			if (!actor->target)	// If we have no target, revert back to waypoint 0.
			{
				actor->extravalue1 = 0;
				P_SetTarget(&actor->target, actor->tracer);
			}
		}
		else	// if our target ISN'T a waypoint, then it can only be a player.
		{
			if (!P_CheckSight(actor, actor->target) || R_PointToDist2(actor->x, actor->y, actor->target->x, actor->target->y) > 1024<<FRACBITS)
				P_SetTarget(&actor->target, actor->hnext);
		}
	}
}

// Function: A_SpawnSneakerPanel
//
// Description: Spawns a sneaker panel object relative to the location of the actor
//
// var1:
//		var1 >> 16 = x offset
//		var1 & 65535 = y offset
// var2:
//		var2 >> 16 = z
//		var2 & 65535 = unused
//
void A_SpawnSneakerPanel(void *thing)
{
	mobj_t *actor = thing;
	int16_t x, y, z;
	mobj_t *mo;
	int32_t locvar1 = var1;
	int32_t locvar2 = var2;

	if (LUA_CallAction(A_SPAWNSNEAKERPANEL, actor))
	{
		return;
	}

	x = (int16_t)(locvar1 >> 16);
	y = (int16_t)(locvar1 & 65535);
	z = (int16_t)(locvar2 >> 16);

	mo = P_SpawnMobjFromMobj(actor, x << FRACBITS, y << FRACBITS, z << FRACBITS, MT_SNEAKERPANEL);
	mo->angle = actor->angle;
	Obj_SneakerPanelSpriteScale(mo);
}

// spews the kart's gib particles
void A_KartGibs(void *thing)
{
	mobj_t *actor = thing;
	angle_t tireangle = actor->angle + ANGLE_90 - ANGLE_22h;
	int32_t i;

	if (LUA_CallAction(A_KARTGIBS, actor))
		return;

	for (i = 0; i < 4; i++, tireangle += ANGLE_45)
	{
		if (i == 2) tireangle += ANGLE_90;

		mobj_t *tire = P_SpawnMobjFromMobj(actor, 0, 0, 0, MT_KART_TIRE);
		tire->fuse = 3*TICRATE;
		tire->angle = tireangle;
		if (i == 0 || i == 3)
			P_SetScale(tire, (tire->destscale = 3*tire->scale/4));
		P_InstaThrust(tire, tireangle, 3 * tire->scale);
		P_SetObjectMomZ(tire, 9*FRACUNIT + 3*P_RandomFixed(PR_DECORATION), false);
	}
}
