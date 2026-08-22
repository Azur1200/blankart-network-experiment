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
/// \file  p_mobj.c
/// \brief Moving object handling. Spawn functions

#include "p_mobj.h"
#include "d_netcmd.h"
#include "d_player.h"
#include "d_think.h"
#include "dehacked.h"
#include "deh_tables.h"
#include "doomdef.h"
#include "doomstat.h"
#include "doomtype.h"
#include "g_game.h"
#include "g_input.h"
#include "st_stuff.h"
#include "hu_stuff.h"
#include "p_local.h"
#include "p_setup.h"
#include "r_fps.h"
#include "r_main.h"
#include "r_skins.h"
#include "r_sky.h"
#include "r_splats.h"
#include "s_sound.h"
#include "z_zone.h"
#include "m_random.h"
#include "m_cheat.h"
#include "m_misc.h"
#include "info.h"
#include "i_video.h"
#include "lua_hook.h"
#include "p_slopes.h"
#include "f_finale.h"
#include "m_cond.h"
#include "m_aatree.h"

// SRB2kart
#include "k_kart.h"
#include "k_battle.h"
#include "k_boss.h"
#include "k_color.h"
#include "k_bot.h"
#include "k_terrain.h"
#include "k_collide.h"
#include "k_objects.h"
#include "k_items.h"
#include "k_waypoint.h"
#include "k_grandprix.h"
#include "g_party.h"
#include "k_director.h"

// BlanKart
#include "blan/b_soc.h"

static CV_PossibleValue_t CV_BobSpeed[] = {{0, "MIN"}, {4*FRACUNIT, "MAX"}, {0, NULL}};
consvar_t cv_movebob = CVAR_INIT ("movebob", "1.0", CV_FLOAT|CV_SAVE, CV_BobSpeed, NULL);

actioncache_t actioncachehead;

static mobj_t *overlaycap = NULL;
mobj_t *kitemcap = NULL;	// Used for Kart offensive items (the ones that can get removed by sizedown)
mobj_t *misccap = NULL;		// Used for misc object tracking
mobj_t *waypointcap = NULL;
mobj_t *boss3cap = NULL;

void P_InitCachedActions(void)
{
	actioncachehead.prev = actioncachehead.next = &actioncachehead;
}

void P_RunCachedActions(void)
{
	actioncache_t *ac;
	actioncache_t *next;

	for (ac = actioncachehead.next; ac != &actioncachehead; ac = next)
	{
		var1 = states[ac->statenum].var1;
		var2 = states[ac->statenum].var2;
		astate = &states[ac->statenum];
		if (ac->mobj && !P_MobjWasRemoved(ac->mobj)) // just in case...
			states[ac->statenum].action(ac->mobj);
		next = ac->next;
		Z_Free(ac);
	}
}

void P_AddCachedAction(mobj_t *mobj, int32_t statenum)
{
	actioncache_t *newaction = Z_Calloc(sizeof(actioncache_t), PU_LEVEL, NULL);
	newaction->mobj = mobj;
	newaction->statenum = statenum;
	actioncachehead.prev->next = newaction;
	newaction->next = &actioncachehead;
	newaction->prev = actioncachehead.prev;
	actioncachehead.prev = newaction;
}

static inline int32_t randomframe (mobj_t *mobj, int32_t n)
{
	// Only mobj thinkers should use synced RNG
	if (mobj->thinker.function == (actionf_p1)P_MobjThinker)
		return P_RandomKey(PR_RANDOMANIM, n);
	else
		return M_RandomKey(n);
}

//
// P_SetupStateAnimation
//
static void P_SetupStateAnimation(mobj_t *mobj, state_t *st)
{
	int32_t animlength = (mobj->sprite == SPR_PLAY && mobj->skin)
		? (int32_t)(((skin_t *)mobj->skin)->sprites[mobj->sprite2].numframes) - 1
		: st->var1;

	if (!(st->frame & FF_ANIMATE))
		return;

	if (animlength <= 0 || st->var2 == 0)
	{
		mobj->frame &= ~FF_ANIMATE;
		return; // Crash/stupidity prevention
	}

	mobj->anim_duration = (uint16_t)st->var2;

	if (st->frame & FF_GLOBALANIM)
	{
		mobj->anim_duration -= (leveltime % st->var2);            // Duration synced to timer
		mobj->frame += (leveltime / st->var2) % (animlength + 1); // Frame synced to timer (duration taken into account)
		if (!thinkersCompleted)                                   // objects spawned BEFORE (or during) thinkers will think during this tic...
			mobj->anim_duration++;                                  // ...so increase the duration of their current frame by 1 to sync with objects spawned AFTER thinkers
	}
	else if (st->frame & FF_RANDOMANIM)
	{
		mobj->frame += randomframe(mobj, animlength + 1);     // Random starting frame
		mobj->anim_duration -= randomframe(mobj, st->var2); // Random duration for first frame
	}
}

//
// P_CycleStateAnimation
//
FUNCINLINE static ATTRINLINE void P_CycleStateAnimation(mobj_t *mobj)
{
	// var2 determines delay between animation frames
	if (!(mobj->frame & FF_ANIMATE) || --mobj->anim_duration != 0)
		return;

	mobj->anim_duration = (uint16_t)mobj->state->var2;

	if (mobj->sprite != SPR_PLAY)
	{
		const uint8_t start = mobj->state->frame & FF_FRAMEMASK;

		uint8_t frame = mobj->frame & FF_FRAMEMASK;

		// compare the current sprite frame to the one we started from
		// if more than var1 away from it, swap back to the original
		// else just advance by one
		if ((mobj->frame & FF_REVERSEANIM ? (start - (--frame)) : ((++frame) - start)) > mobj->state->var1)
			frame = start;

		mobj->frame = frame | (mobj->frame & ~FF_FRAMEMASK);

		return;
	}

	// sprite2 version of above
	if (mobj->skin && (((++mobj->frame) & FF_FRAMEMASK) >= (uint32_t)(((skin_t *)mobj->skin)->sprites[mobj->sprite2].numframes)))
		mobj->frame &= ~FF_FRAMEMASK;
}

//
// P_CycleMobjState
//
static void P_CycleMobjState(mobj_t *mobj)
{
	// state animations
	P_CycleStateAnimation(mobj);

	// cycle through states,
	// calling action functions at transitions
	if (mobj->tics != -1)
	{
		mobj->tics--;

		// you can cycle through multiple states in a tic
		if (!mobj->tics && mobj->state)
			if (!P_SetMobjState(mobj, mobj->state->nextstate))
				return; // freed itself
	}
}

//
// P_CycleMobjState for players.
//
static void P_CyclePlayerMobjState(mobj_t *mobj)
{
	// state animations
	P_CycleStateAnimation(mobj);

	// cycle through states,
	// calling action functions at transitions
	if (mobj->tics != -1)
	{
		mobj->tics--;

		// you can cycle through multiple states in a tic
		if (!mobj->tics && mobj->state)
			if (!P_SetPlayerMobjState(mobj, mobj->state->nextstate))
				return; // freed itself
	}
}

//
// P_SetPlayerMobjState
// Returns true if the mobj is still present.
//
// Separate from P_SetMobjState because of the flashing check and Super states
//
dboolean P_SetPlayerMobjState(mobj_t *mobj, statenum_t state)
{
	state_t *st;
	player_t *player = mobj->player;

	// remember states seen, to detect cycles:
	static statenum_t seenstate_tab[NUMSTATES]; // fast transition table
	statenum_t *seenstate = seenstate_tab; // pointer to table
	static int32_t recursion; // detects recursion
	statenum_t i; // initial state
	statenum_t tempstate[NUMSTATES]; // for use with recursion

#ifdef PARANOIA
	if (player == NULL)
		I_Error("P_SetPlayerMobjState used for non-player mobj. Use P_SetMobjState instead!\n(Mobj type: %d, State: %d)", mobj->type, state);
#endif

	// Set animation state
	// The pflags version of this was just as convoluted.
	switch(state)
	{
		case S_KART_STILL:
		case S_KART_STILL_L:
		case S_KART_STILL_R:
		case S_KART_STILL_GLANCE_L:
		case S_KART_STILL_GLANCE_R:
		case S_KART_STILL_LOOK_L:
		case S_KART_STILL_LOOK_R:
			player->panim = PA_STILL;
			break;
		case S_KART_SLOW:
		case S_KART_SLOW_L:
		case S_KART_SLOW_R:
		case S_KART_SLOW_GLANCE_L:
		case S_KART_SLOW_GLANCE_R:
		case S_KART_SLOW_LOOK_L:
		case S_KART_SLOW_LOOK_R:
			player->panim = PA_SLOW;
			break;
		case S_KART_FAST:
		case S_KART_FAST_L:
		case S_KART_FAST_R:
		case S_KART_FAST_GLANCE_L:
		case S_KART_FAST_GLANCE_R:
		case S_KART_FAST_LOOK_L:
		case S_KART_FAST_LOOK_R:
			player->panim = PA_FAST;
			break;
		case S_KART_DRIFT_L:
		case S_KART_DRIFT_L_OUT:
		case S_KART_DRIFT_L_IN:
		case S_KART_DRIFT_R:
		case S_KART_DRIFT_R_OUT:
		case S_KART_DRIFT_R_IN:
			player->panim = PA_DRIFT;
			break;
		case S_KART_SPINOUT:
		case S_KART_DEAD:
			player->panim = PA_HURT;
			break;
		default:
			player->panim = PA_ETC;
			break;
	}

	if (recursion++) // if recursion detected,
		memset(seenstate = tempstate, 0, sizeof tempstate); // clear state table

	i = state;

	do
	{
		if (state == S_NULL)
		{ // Bad SOC!
			CONS_Alert(CONS_ERROR, "Cannot remove player mobj by setting its state to S_NULL.\n");
			//P_RemoveMobj(mobj);
			return false;
		}

		st = &states[state];
		mobj->state = st;
		mobj->tics = st->tics;

		// Player animations
		if (st->sprite == SPR_PLAY)
		{
			skin_t *skin = ((skin_t *)mobj->skin);
			uint16_t frame = (mobj->frame & FF_FRAMEMASK)+1;
			uint8_t numframes, spr2;

			if (skin)
			{
				spr2 = P_GetSkinSprite2(skin, st->frame & FF_FRAMEMASK, mobj->player);
				numframes = skin->sprites[spr2].numframes;
			}
			else
			{
				spr2 = 0;
				frame = 0;
				numframes = 0;
			}

			if (mobj->sprite != SPR_PLAY)
			{
				mobj->sprite = SPR_PLAY;
				frame = 0;
			}
			else if (mobj->sprite2 != spr2)
			{
				if ((st->frame & FF_SPR2MIDSTART) && numframes && P_RandomChance(PR_RANDOMANIM, FRACUNIT/2))
					frame = numframes/2;
				else
					frame = 0;
			}

			if (frame >= numframes)
			{
				if (st->frame & FF_SPR2ENDSTATE) // no frame advancement
				{
					if (st->var1 == mobj->state-states)
						frame--;
					else
					{
						if (mobj->frame & FF_FRAMEMASK)
							mobj->frame--;
						return P_SetPlayerMobjState(mobj, st->var1);
					}
				}
				else
					frame = 0;
			}

			mobj->sprite2 = spr2;
			mobj->frame = frame|(st->frame&~FF_FRAMEMASK);
			if (P_PlayerFullbright(player))
				mobj->frame |= FF_FULLBRIGHT;
		}
		// Regular sprites
		else
		{
			mobj->sprite = st->sprite;
			mobj->frame = st->frame;
		}

		P_SetupStateAnimation(mobj, st);

		// Modified handling.
		// Call action functions when the state is set

		if (st->action)
		{
			var1 = st->var1;
			var2 = st->var2;
			astate = st;
			st->action(mobj);

			// woah. a player was removed by an action.
			// this sounds like a VERY BAD THING, but there's nothing we can do now...
			if (P_MobjWasRemoved(mobj))
				return false;
		}

		seenstate[state] = 1 + st->nextstate;

		state = st->nextstate;
	} while (!mobj->tics && !seenstate[state]);

	if (!mobj->tics)
		CONS_Alert(CONS_WARNING, M_GetText("State cycle detected, exiting.\n"));

	if (!--recursion)
		for (;(state = seenstate[i]) > S_NULL; i = state - 1)
			seenstate[i] = S_NULL; // erase memory of states

	return true;
}


dboolean P_SetMobjState(mobj_t *mobj, statenum_t state)
{
	state_t *st;

	// remember states seen, to detect cycles:
	static statenum_t seenstate_tab[NUMSTATES]; // fast transition table
	statenum_t *seenstate = seenstate_tab; // pointer to table
	static int32_t recursion; // detects recursion
	statenum_t i = state; // initial state
	statenum_t tempstate[NUMSTATES]; // for use with recursion

#ifdef PARANOIA
	if (mobj->player != NULL)
		I_Error("P_SetMobjState used for player mobj. Use P_SetPlayerMobjState instead!\n(State called: %d)", state);
#endif

	if (recursion++) // if recursion detected,
		memset(seenstate = tempstate, 0, sizeof tempstate); // clear state table

	do
	{
		if (state == S_NULL)
		{
			P_RemoveMobj(mobj);
			return false;
		}

		st = &states[state];
		mobj->state = st;
		mobj->tics = st->tics;

		// Player animations
		if (st->sprite == SPR_PLAY)
		{
			skin_t *skin = ((skin_t *)mobj->skin);
			uint16_t frame = (mobj->frame & FF_FRAMEMASK)+1;
			uint8_t numframes, spr2;

			if (skin)
			{
				spr2 = P_GetSkinSprite2(skin, st->frame & FF_FRAMEMASK, mobj->player);
				numframes = skin->sprites[spr2].numframes;
			}
			else
			{
				spr2 = 0;
				frame = 0;
				numframes = 0;
			}

			if (mobj->sprite != SPR_PLAY)
			{
				mobj->sprite = SPR_PLAY;
				frame = 0;
			}
			else if (mobj->sprite2 != spr2)
			{
				if ((st->frame & FF_SPR2MIDSTART) && numframes && P_RandomChance(PR_RANDOMANIM, FRACUNIT/2))
					frame = numframes/2;
				else
					frame = 0;
			}

			if (frame >= numframes)
			{
				if (st->frame & FF_SPR2ENDSTATE) // no frame advancement
				{
					if (st->var1 == mobj->state-states)
						frame--;
					else
					{
						if (mobj->frame & FF_FRAMEMASK)
							mobj->frame--;
						return P_SetMobjState(mobj, st->var1);
					}
				}
				else
					frame = 0;
			}

			mobj->sprite2 = spr2;
			mobj->frame = frame|(st->frame&~FF_FRAMEMASK);
		}
		// Regular sprites
		else
		{
			mobj->sprite = st->sprite;
			mobj->frame = st->frame;
		}

		P_SetupStateAnimation(mobj, st);

		// Modified handling.
		// Call action functions when the state is set

		if (st->action)
		{
			var1 = st->var1;
			var2 = st->var2;
			astate = st;
			st->action(mobj);
			if (P_MobjWasRemoved(mobj))
				return false;
		}

		seenstate[state] = 1 + st->nextstate;

		state = st->nextstate;
	} while (!mobj->tics && !seenstate[state]);

	if (!mobj->tics)
		CONS_Alert(CONS_WARNING, M_GetText("State cycle detected, exiting.\n"));

	if (!--recursion)
		for (;(state = seenstate[i]) > S_NULL; i = state - 1)
			seenstate[i] = S_NULL; // erase memory of states

	return true;
}

//----------------------------------------------------------------------------
//
// FUNC P_SetMobjStateNF
//
// Same as P_SetMobjState, but does not call the state function.
//
//----------------------------------------------------------------------------

dboolean P_SetMobjStateNF(mobj_t *mobj, statenum_t state)
{
	state_t *st;

	if (state == S_NULL)
	{ // Remove mobj
		P_RemoveMobj(mobj);
		return false;
	}
	st = &states[state];
	mobj->state = st;
	mobj->tics = st->tics;
	mobj->sprite = st->sprite;
	mobj->frame = st->frame;
	P_SetupStateAnimation(mobj, st);

	return true;
}

static dboolean P_SetPrecipMobjState(precipmobj_t *mobj, statenum_t state)
{
	state_t *st;

	if (state == S_NULL)
	{ // Remove mobj
		P_FreePrecipMobj(mobj);
		return false;
	}
	st = &states[state];
	mobj->state = st;
	mobj->tics = st->tics;
	mobj->sprite = st->sprite;
	mobj->frame = st->frame;
	P_SetupStateAnimation((mobj_t*)mobj, st);

	return true;
}

//
// P_MobjFlip
//
// Special utility to return +1 or -1 depending on mobj's gravity
//
int8_t P_MobjFlip(mobj_t *mobj)
{
	if (mobj && mobj->eflags & MFE_VERTICALFLIP)
		return -1;
	return 1;
}

//
// P_ExplodeMissile
//
void P_ExplodeMissile(mobj_t *mo)
{
	I_Assert(mo != NULL);
	I_Assert(!P_MobjWasRemoved(mo));

	mo->momx = mo->momy = mo->momz = 0;

	if (mo->flags & MF_NOCLIPTHING)
		return;

	mo->flags &= ~MF_MISSILE;

	mo->flags |= MF_NOGRAVITY; // Dead missiles don't need to sink anymore.
	mo->flags |= MF_NOCLIPTHING; // Dummy flag to indicate that this was already called.

	if (mo->info->deathsound && !(mo->flags2 & MF2_DEBRIS))
		S_StartSound(mo, mo->info->deathsound);

	P_SetMobjState(mo, mo->info->deathstate);
}

// P_InsideANonSolidFFloor
//
// Returns TRUE if mobj is inside a non-solid 3d floor.
dboolean P_InsideANonSolidFFloor(mobj_t *mobj, ffloor_t *rover)
{
	fixed_t topheight, bottomheight;
	if (!(rover->fofflags & FOF_EXISTS))
		return false;

	if ((((rover->fofflags & FOF_BLOCKPLAYER) && mobj->player)
		|| ((rover->fofflags & FOF_BLOCKOTHERS) && !mobj->player)))
		return false;

	topheight    = P_GetFFloorTopZAt   (rover, mobj->x, mobj->y);
	bottomheight = P_GetFFloorBottomZAt(rover, mobj->x, mobj->y);

	if (mobj->z > topheight)
		return false;

	if (mobj->z + mobj->height < bottomheight)
		return false;

	return true;
}

// P_GetFloorZ (and its ceiling counterpart)
// Gets the floor height (or ceiling height) of the mobj's contact point in sector, assuming object's center if moved to [x, y]
// If line is supplied, it's a divider line on the sector. Set it to NULL if you're not checking for collision with a line
// Supply boundsec ONLY when checking for specials! It should be the "in-level" sector, and sector the control sector (if separate).
// If set, then this function will iterate through boundsec's linedefs to find the highest contact point on the slope. Non-special-checking
// usage will handle that later.
static fixed_t HighestOnLine(fixed_t radius, fixed_t x, fixed_t y, const line_t *line, const pslope_t *slope, dboolean actuallylowest)
{
	// Alright, so we're sitting on a line that contains our slope sector, and need to figure out the highest point we're touching...
	// The solution is simple! Get the line's vertices, and pull each one in along its line until it touches the object's bounding box
	// (assuming it isn't already inside), then test each point's slope Z and return the higher of the two.
	vertex_t v1, v2;
	v1.x = line->v1->x;
	v1.y = line->v1->y;
	v2.x = line->v2->x;
	v2.y = line->v2->y;

	/*CONS_Printf("BEFORE: v1 = %f %f %f\n",
				FIXED_TO_FLOAT(v1.x),
				FIXED_TO_FLOAT(v1.y),
				FIXED_TO_FLOAT(P_GetSlopeZAt(slope, v1.x, v1.y))
				);
	CONS_Printf("        v2 = %f %f %f\n",
				FIXED_TO_FLOAT(v2.x),
				FIXED_TO_FLOAT(v2.y),
				FIXED_TO_FLOAT(P_GetSlopeZAt(slope, v2.x, v2.y))
				);*/

	if (abs(v1.x-x) > radius) {
		// v1's x is out of range, so rein it in
		fixed_t diff = abs(v1.x-x) - radius;

		if (v1.x < x) { // Moving right
			v1.x += diff;
			v1.y += FixedMul(diff, FixedDiv(line->dy, line->dx));
		} else { // Moving left
			v1.x -= diff;
			v1.y -= FixedMul(diff, FixedDiv(line->dy, line->dx));
		}
	}

	if (abs(v1.y-y) > radius) {
		// v1's y is out of range, so rein it in
		fixed_t diff = abs(v1.y-y) - radius;

		if (v1.y < y) { // Moving up
			v1.y += diff;
			v1.x += FixedMul(diff, FixedDiv(line->dx, line->dy));
		} else { // Moving down
			v1.y -= diff;
			v1.x -= FixedMul(diff, FixedDiv(line->dx, line->dy));
		}
	}

	if (abs(v2.x-x) > radius) {
		// v1's x is out of range, so rein it in
		fixed_t diff = abs(v2.x-x) - radius;

		if (v2.x < x) { // Moving right
			v2.x += diff;
			v2.y += FixedMul(diff, FixedDiv(line->dy, line->dx));
		} else { // Moving left
			v2.x -= diff;
			v2.y -= FixedMul(diff, FixedDiv(line->dy, line->dx));
		}
	}

	if (abs(v2.y-y) > radius) {
		// v2's y is out of range, so rein it in
		fixed_t diff = abs(v2.y-y) - radius;

		if (v2.y < y) { // Moving up
			v2.y += diff;
			v2.x += FixedMul(diff, FixedDiv(line->dx, line->dy));
		} else { // Moving down
			v2.y -= diff;
			v2.x -= FixedMul(diff, FixedDiv(line->dx, line->dy));
		}
	}

	/*CONS_Printf("AFTER:  v1 = %f %f %f\n",
				FIXED_TO_FLOAT(v1.x),
				FIXED_TO_FLOAT(v1.y),
				FIXED_TO_FLOAT(P_GetSlopeZAt(slope, v1.x, v1.y))
				);
	CONS_Printf("        v2 = %f %f %f\n",
				FIXED_TO_FLOAT(v2.x),
				FIXED_TO_FLOAT(v2.y),
				FIXED_TO_FLOAT(P_GetSlopeZAt(slope, v2.x, v2.y))
				);*/

	// Return the higher of the two points
	if (actuallylowest)
		return min(
			P_GetSlopeZAt(slope, v1.x, v1.y),
			P_GetSlopeZAt(slope, v2.x, v2.y)
		);
	else
		return max(
			P_GetSlopeZAt(slope, v1.x, v1.y),
			P_GetSlopeZAt(slope, v2.x, v2.y)
		);
}

fixed_t P_MobjFloorZ(const mobj_t *mobj, const sector_t *sector, const sector_t *boundsec, fixed_t x, fixed_t y, const line_t *line, dboolean lowest, dboolean perfect)
{
	I_Assert(mobj != NULL);
	I_Assert(sector != NULL);

	if (sector->f_slope) {
		fixed_t testx, testy;
		pslope_t *slope = sector->f_slope;

		// Get the corner of the object that should be the highest on the slope
		if (slope->d.x < 0)
			testx = mobj->radius;
		else
			testx = -mobj->radius;

		if (slope->d.y < 0)
			testy = mobj->radius;
		else
			testy = -mobj->radius;

		if ((slope->zdelta > 0) ^ !!(lowest)) {
			testx = -testx;
			testy = -testy;
		}

		testx += x;
		testy += y;

		// If the highest point is in the sector, then we have it easy! Just get the Z at that point
		if (R_PointInSubsector(testx, testy)->sector == (boundsec ? boundsec : sector))
			return P_GetSlopeZAt(slope, testx, testy);

		// If boundsec is set, we're looking for specials. In that case, iterate over every line in this sector to find the TRUE highest/lowest point
		if (perfect) {
			size_t i;
			line_t *ld;
			fixed_t bbox[4];
			fixed_t finalheight;

			if (lowest)
				finalheight = INT32_MAX;
			else
				finalheight = INT32_MIN;

			bbox[BOXLEFT] = x-mobj->radius;
			bbox[BOXRIGHT] = x+mobj->radius;
			bbox[BOXTOP] = y+mobj->radius;
			bbox[BOXBOTTOM] = y-mobj->radius;
			for (i = 0; i < boundsec->linecount; i++) {
				ld = boundsec->lines[i];

				if (bbox[BOXRIGHT] <= ld->bbox[BOXLEFT] || bbox[BOXLEFT] >= ld->bbox[BOXRIGHT]
				|| bbox[BOXTOP] <= ld->bbox[BOXBOTTOM] || bbox[BOXBOTTOM] >= ld->bbox[BOXTOP])
					continue;

				if (P_BoxOnLineSide(bbox, ld) != -1)
					continue;

				if (lowest)
					finalheight = min(finalheight, HighestOnLine(mobj->radius, x, y, ld, slope, true));
				else
					finalheight = max(finalheight, HighestOnLine(mobj->radius, x, y, ld, slope, false));
			}

			return finalheight;
		}

		// If we're just testing for base sector location (no collision line), just go for the center's spot...
		// It'll get fixed when we test for collision anyway, and the final result can't be lower than this
		if (line == NULL)
			return P_GetSlopeZAt(slope, x, y);

		return HighestOnLine(mobj->radius, x, y, line, slope, lowest);
	} else // Well, that makes it easy. Just get the floor height
		return sector->floorheight;
}

fixed_t P_MobjCeilingZ(const mobj_t *mobj, const sector_t *sector, const sector_t *boundsec, fixed_t x, fixed_t y, const line_t *line, dboolean lowest, dboolean perfect)
{
	I_Assert(mobj != NULL);
	I_Assert(sector != NULL);

	if (sector->c_slope) {
		fixed_t testx, testy;
		pslope_t *slope = sector->c_slope;

		// Get the corner of the object that should be the highest on the slope
		if (slope->d.x < 0)
			testx = mobj->radius;
		else
			testx = -mobj->radius;

		if (slope->d.y < 0)
			testy = mobj->radius;
		else
			testy = -mobj->radius;

		if ((slope->zdelta > 0) ^ !!(lowest)) {
			testx = -testx;
			testy = -testy;
		}

		testx += x;
		testy += y;

		// If the highest point is in the sector, then we have it easy! Just get the Z at that point
		if (R_PointInSubsector(testx, testy)->sector == (boundsec ? boundsec : sector))
			return P_GetSlopeZAt(slope, testx, testy);

		// If boundsec is set, we're looking for specials. In that case, iterate over every line in this sector to find the TRUE highest/lowest point
		if (perfect) {
			size_t i;
			line_t *ld;
			fixed_t bbox[4];
			fixed_t finalheight;

			if (lowest)
				finalheight = INT32_MAX;
			else
				finalheight = INT32_MIN;

			bbox[BOXLEFT] = x-mobj->radius;
			bbox[BOXRIGHT] = x+mobj->radius;
			bbox[BOXTOP] = y+mobj->radius;
			bbox[BOXBOTTOM] = y-mobj->radius;
			for (i = 0; i < boundsec->linecount; i++) {
				ld = boundsec->lines[i];

				if (bbox[BOXRIGHT] <= ld->bbox[BOXLEFT] || bbox[BOXLEFT] >= ld->bbox[BOXRIGHT]
				|| bbox[BOXTOP] <= ld->bbox[BOXBOTTOM] || bbox[BOXBOTTOM] >= ld->bbox[BOXTOP])
					continue;

				if (P_BoxOnLineSide(bbox, ld) != -1)
					continue;

				if (lowest)
					finalheight = min(finalheight, HighestOnLine(mobj->radius, x, y, ld, slope, true));
				else
					finalheight = max(finalheight, HighestOnLine(mobj->radius, x, y, ld, slope, false));
			}

			return finalheight;
		}

		// If we're just testing for base sector location (no collision line), just go for the center's spot...
		// It'll get fixed when we test for collision anyway, and the final result can't be lower than this
		if (line == NULL)
			return P_GetSlopeZAt(slope, x, y);

		return HighestOnLine(mobj->radius, x, y, line, slope, lowest);
	} else // Well, that makes it easy. Just get the ceiling height
		return sector->ceilingheight;
}

// Now do the same as all above, but for cameras because apparently cameras are special?
fixed_t P_CameraFloorZ(camera_t *mobj, sector_t *sector, sector_t *boundsec, fixed_t x, fixed_t y, line_t *line, dboolean lowest, dboolean perfect)
{
	I_Assert(mobj != NULL);
	I_Assert(sector != NULL);

	if (sector->f_slope) {
		fixed_t testx, testy;
		pslope_t *slope = sector->f_slope;

		// Get the corner of the object that should be the highest on the slope
		if (slope->d.x < 0)
			testx = mobj->radius;
		else
			testx = -mobj->radius;

		if (slope->d.y < 0)
			testy = mobj->radius;
		else
			testy = -mobj->radius;

		if ((slope->zdelta > 0) ^ !!(lowest)) {
			testx = -testx;
			testy = -testy;
		}

		testx += x;
		testy += y;

		// If the highest point is in the sector, then we have it easy! Just get the Z at that point
		if (R_PointInSubsector(testx, testy)->sector == (boundsec ? boundsec : sector))
			return P_GetSlopeZAt(slope, testx, testy);

		// If boundsec is set, we're looking for specials. In that case, iterate over every line in this sector to find the TRUE highest/lowest point
		if (perfect) {
			size_t i;
			line_t *ld;
			fixed_t bbox[4];
			fixed_t finalheight;

			if (lowest)
				finalheight = INT32_MAX;
			else
				finalheight = INT32_MIN;

			bbox[BOXLEFT] = x-mobj->radius;
			bbox[BOXRIGHT] = x+mobj->radius;
			bbox[BOXTOP] = y+mobj->radius;
			bbox[BOXBOTTOM] = y-mobj->radius;
			for (i = 0; i < boundsec->linecount; i++) {
				ld = boundsec->lines[i];

				if (bbox[BOXRIGHT] <= ld->bbox[BOXLEFT] || bbox[BOXLEFT] >= ld->bbox[BOXRIGHT]
				|| bbox[BOXTOP] <= ld->bbox[BOXBOTTOM] || bbox[BOXBOTTOM] >= ld->bbox[BOXTOP])
					continue;

				if (P_BoxOnLineSide(bbox, ld) != -1)
					continue;

				if (lowest)
					finalheight = min(finalheight, HighestOnLine(mobj->radius, x, y, ld, slope, true));
				else
					finalheight = max(finalheight, HighestOnLine(mobj->radius, x, y, ld, slope, false));
			}

			return finalheight;
		}

		// If we're just testing for base sector location (no collision line), just go for the center's spot...
		// It'll get fixed when we test for collision anyway, and the final result can't be lower than this
		if (line == NULL)
			return P_GetSlopeZAt(slope, x, y);

		return HighestOnLine(mobj->radius, x, y, line, slope, lowest);
	} else // Well, that makes it easy. Just get the floor height
		return sector->floorheight;
}

fixed_t P_CameraCeilingZ(camera_t *mobj, sector_t *sector, sector_t *boundsec, fixed_t x, fixed_t y, line_t *line, dboolean lowest, dboolean perfect)
{
	I_Assert(mobj != NULL);
	I_Assert(sector != NULL);

	if (sector->c_slope) {
		fixed_t testx, testy;
		pslope_t *slope = sector->c_slope;

		// Get the corner of the object that should be the highest on the slope
		if (slope->d.x < 0)
			testx = mobj->radius;
		else
			testx = -mobj->radius;

		if (slope->d.y < 0)
			testy = mobj->radius;
		else
			testy = -mobj->radius;

		if ((slope->zdelta > 0) ^ !!(lowest)) {
			testx = -testx;
			testy = -testy;
		}

		testx += x;
		testy += y;

		// If the highest point is in the sector, then we have it easy! Just get the Z at that point
		if (R_PointInSubsector(testx, testy)->sector == (boundsec ? boundsec : sector))
			return P_GetSlopeZAt(slope, testx, testy);

		// If boundsec is set, we're looking for specials. In that case, iterate over every line in this sector to find the TRUE highest/lowest point
		if (perfect) {
			size_t i;
			line_t *ld;
			fixed_t bbox[4];
			fixed_t finalheight;

			if (lowest)
				finalheight = INT32_MAX;
			else
				finalheight = INT32_MIN;

			bbox[BOXLEFT] = x-mobj->radius;
			bbox[BOXRIGHT] = x+mobj->radius;
			bbox[BOXTOP] = y+mobj->radius;
			bbox[BOXBOTTOM] = y-mobj->radius;
			for (i = 0; i < boundsec->linecount; i++) {
				ld = boundsec->lines[i];

				if (bbox[BOXRIGHT] <= ld->bbox[BOXLEFT] || bbox[BOXLEFT] >= ld->bbox[BOXRIGHT]
				|| bbox[BOXTOP] <= ld->bbox[BOXBOTTOM] || bbox[BOXBOTTOM] >= ld->bbox[BOXTOP])
					continue;

				if (P_BoxOnLineSide(bbox, ld) != -1)
					continue;

				if (lowest)
					finalheight = min(finalheight, HighestOnLine(mobj->radius, x, y, ld, slope, true));
				else
					finalheight = max(finalheight, HighestOnLine(mobj->radius, x, y, ld, slope, false));
			}

			return finalheight;
		}

		// If we're just testing for base sector location (no collision line), just go for the center's spot...
		// It'll get fixed when we test for collision anyway, and the final result can't be lower than this
		if (line == NULL)
			return P_GetSlopeZAt(slope, x, y);

		return HighestOnLine(mobj->radius, x, y, line, slope, lowest);
	} else // Well, that makes it easy. Just get the ceiling height
		return sector->ceilingheight;
}


int32_t P_FloorPicAtPos(fixed_t x, fixed_t y, fixed_t z, fixed_t height)
{
	sector_t *sector = R_PointInSubsector(x, y)->sector;

	int32_t floorpic = sector->floorpic;

	if (sector->ffloors)
	{
		ffloor_t *best = NULL;
		fixed_t thingtop = z + height;

		for (ffloor_t *rover = sector->ffloors; rover; rover = rover->next)
		{
			if (!(rover->fofflags & FOF_EXISTS))
				continue;

			fixed_t topheight = P_GetFFloorTopZAt(rover, x, y);
			fixed_t bottomheight = P_GetFFloorBottomZAt(rover, x, y);

			fixed_t delta1 = z - (bottomheight + ((topheight - bottomheight)/2));
			fixed_t delta2 = thingtop - (bottomheight + ((topheight - bottomheight)/2));
			if (topheight > P_GetFFloorTopZAt(best, x, y) && abs(delta1) < abs(delta2))
			{
				best = rover;
			}
		}

		if (best)
			floorpic = *best->toppic;
	}

	return floorpic;
}

static void P_PlayerFlip(mobj_t *mo)
{
	if (!mo->player)
		return;

	if (mo->player->pflags & PF_FLIPCAM)
	{
		uint8_t i;

		mo->player->aiming = InvAngle(mo->player->aiming);

		for (i = 0; i <= r_splitscreen; i++)
		{
			if (mo->player-players == displayplayers[i])
			{
				localaiming[i] = mo->player->aiming;
				if (camera[i].chase) {
					camera[i].aiming = InvAngle(camera[i].aiming);
					camera[i].z = mo->z - camera[i].z + mo->z;
					if (mo->eflags & MFE_VERTICALFLIP)
						camera[i].z += FixedMul(20*FRACUNIT, mo->scale);
				}
			}
		}
	}

	G_GhostAddFlip((int32_t) (mo->player - players));
	// Flip aiming to match!
}

fixed_t P_GetMobjGravity(mobj_t *mo)
{
	fixed_t gravityadd = 0;
	fixed_t gravitymul = FRACUNIT;
	sector_t *gravsector = NULL; // Custom gravity
	dboolean goopgravity = false;
	dboolean wasflip;

	I_Assert(mo != NULL);
	I_Assert(!P_MobjWasRemoved(mo));

	gravitymul = mo->scale;

	wasflip = (mo->eflags & MFE_VERTICALFLIP) != 0;
	mo->eflags &= ~MFE_VERTICALFLIP;

	if (mo->subsector->sector->ffloors) // Check for 3D floor gravity too.
	{
		ffloor_t *rover;

		for (rover = mo->subsector->sector->ffloors; rover; rover = rover->next)
		{
			if (!(rover->fofflags & FOF_EXISTS) || !P_InsideANonSolidFFloor(mo, rover)) // P_InsideANonSolidFFloor checks for FOF_EXISTS itself, but let's not always call this function
				continue;

			if ((rover->fofflags & (FOF_SWIMMABLE|FOF_GOOWATER)) == (FOF_SWIMMABLE|FOF_GOOWATER))
				goopgravity = true;

			if (P_GetSectorGravityFactor(rover->master->frontsector) == FRACUNIT)
				continue;

			gravsector = rover->master->frontsector;
			break;
		}
	}

	if (!gravsector) // If there is no 3D floor gravity, check sector's gravity
		gravsector = mo->subsector->sector;

	gravityadd = -FixedMul(FixedMul(gravity, mo->gravity), P_GetSectorGravityFactor(gravsector));

	if ((gravsector->flags & MSF_GRAVITYFLIP) && gravityadd > 0)
	{
		if (gravsector->specialflags & SSF_GRAVITYOVERRIDE)
			mo->flags2 &= ~MF2_OBJECTFLIP;
		mo->eflags |= MFE_VERTICALFLIP;
	}

	// Less gravity underwater.
	if (mo->eflags & MFE_UNDERWATER && !goopgravity)
		gravityadd = gravityadd/3;

	if (mo->player)
	{
		// MF2_OBJECTFLIP is relative -- flips sector reverse gravity back to normal
		if (mo->flags2 & MF2_OBJECTFLIP)
		{
			gravityadd = -gravityadd;
			mo->eflags ^= MFE_VERTICALFLIP;
		}

		if (wasflip == !(mo->eflags & MFE_VERTICALFLIP)) // note!! == ! is not equivalent to != here - turns numeric into bool this way
			P_PlayerFlip(mo);

		if (mo->player->pogospring && !mo->player->dashRainbowPogo)
		{
			gravityadd = (5*gravityadd)/2;
		}

		if (mo->player->carry == CR_DASHRING)
		{
			if (Obj_DashRingPlayerHasNoGravity(mo->player))
			{
				gravityadd = 0;
			}
			gravitymul = mapobjectscale;
		}

		if (K_IsAltShrunk(mo->player))
		{
			gravitymul = mapobjectscale;
		}
		
		if (mo->player->pflags & PF_NOSCALEGRAVITY)
		{
			gravitymul = mapobjectscale;
		}
	}
	else
	{
		// Objects with permanent reverse gravity (MF2_OBJECTFLIP)
		if (mo->flags2 & MF2_OBJECTFLIP)
		{
			mo->eflags |= MFE_VERTICALFLIP;
			if (mo->z + mo->height >= mo->ceilingz)
				gravityadd = 0;
			else if (gravityadd < 0) // Don't sink, only rise up
				gravityadd *= -1;
		}
		else //Otherwise, sort through the other exceptions.
		{
			switch (mo->type)
			{
				case MT_FLINGRING:
				case MT_FLINGCOIN:
				case MT_FLINGNIGHTSCHIP:
				case MT_FLINGEMERALD:
				case MT_BOUNCERING:
				case MT_RAILRING:
				case MT_INFINITYRING:
				case MT_AUTOMATICRING:
				case MT_EXPLOSIONRING:
				case MT_SCATTERRING:
				case MT_GRENADERING:
				case MT_BOUNCEPICKUP:
				case MT_RAILPICKUP:
				case MT_AUTOPICKUP:
				case MT_EXPLODEPICKUP:
				case MT_SCATTERPICKUP:
				case MT_GRENADEPICKUP:
				case MT_REDFLAG:
				case MT_BLUEFLAG:
					if (mo->target)
					{
						// Flung items copy the gravity of their tosser.
						if ((mo->target->eflags & MFE_VERTICALFLIP) && !(mo->eflags & MFE_VERTICALFLIP))
						{
							gravityadd = -gravityadd;
							mo->eflags |= MFE_VERTICALFLIP;
						}
					}
					break;
				case MT_WATERDROP:
				case MT_CYBRAKDEMON:
				case MT_BATTLEBUMPER:
					gravityadd /= 2;
					break;
				case MT_BANANA:
				case MT_EGGMANITEM:
				case MT_SSMINE:
				case MT_LANDMINE:
				case MT_EGGMINE:
				case MT_SINK:
					if (mo->extravalue2 > 0)
					{
						gravityadd *= mo->extravalue2;
					}

					gravityadd = (5*gravityadd)/2;
					break;
				case MT_ORBINAUT:
				case MT_JAWZ:
				case MT_JAWZ_DUD:
					gravityadd = (5*gravityadd)/2;
					break;
				case MT_SIGN:
					gravityadd /= 8;
					break;
				case MT_KARMAFIREWORK:
					gravityadd /= 3;
					break;
				default:
					break;
			}
		}
	}

	// Goop has slower, reversed gravity
	if (goopgravity)
		gravityadd = -gravityadd/5;

	gravityadd = FixedMul(gravityadd, gravitymul);

	return gravityadd;
}

//
// P_CheckGravity
//
// Checks the current gravity state
// of the object. If affect is true,
// a gravity force will be applied.
//
void P_CheckGravity(mobj_t *mo, dboolean affect)
{
	fixed_t gravityadd = P_GetMobjGravity(mo);

	if (!mo->momz) // mobj at stop, no floor, so feel the push of gravity!
		gravityadd <<= 1;

	if (affect)
		mo->momz += gravityadd;

	if (mo->type == MT_SKIM && mo->z + mo->momz <= mo->watertop && mo->z >= mo->watertop)
	{
		mo->momz = 0;
		mo->flags |= MF_NOGRAVITY;
	}
}

//
// P_SetPitchRollFromSlope
//
void P_SetPitchRollFromSlope(mobj_t *mo, pslope_t *slope)
{
	if (slope)
	{
		fixed_t tempz = slope->normal.z;
		fixed_t tempy = slope->normal.y;
		fixed_t tempx = slope->normal.x;

		mo->slopepitch = R_PointToAngle2(0, 0, FixedSqrt(FixedMul(tempy, tempy) + FixedMul(tempz, tempz)), tempx);
		mo->sloperoll = R_PointToAngle2(0, 0, tempz, tempy);
	}
	else
	{
		mo->slopepitch = mo->sloperoll = 0;
	}
	mo->pitch = mo->roll = 0;
}

//
// P_SetPitchRoll
//
void P_SetPitchRoll(mobj_t *mo, angle_t pitch, angle_t yaw)
{
	pitch = InvAngle(pitch);
	yaw >>= ANGLETOFINESHIFT;
	mo->roll  = FixedMul(pitch, FINESINE   (yaw));
	mo->pitch = FixedMul(pitch, FINECOSINE (yaw));
}

#define STOPSPEED (FRACUNIT)

//
// P_SceneryXYFriction
//
static void P_SceneryXYFriction(mobj_t *mo, fixed_t oldx, fixed_t oldy)
{
	I_Assert(mo != NULL);
	I_Assert(!P_MobjWasRemoved(mo));

	if (abs(mo->momx) < FixedMul(STOPSPEED/32, mo->scale)
		&& abs(mo->momy) < FixedMul(STOPSPEED/32, mo->scale))
	{
		mo->momx = 0;
		mo->momy = 0;
	}
	else
	{
		if ((oldx == mo->x) && (oldy == mo->y)) // didn't go anywhere
		{
			mo->momx = FixedMul(mo->momx,ORIG_FRICTION);
			mo->momy = FixedMul(mo->momy,ORIG_FRICTION);
		}
		else
		{
			mo->momx = FixedMul(mo->momx,mo->friction);
			mo->momy = FixedMul(mo->momy,mo->friction);
		}

		if (mo->type == MT_CANNONBALLDECOR)
		{
			// Stolen from P_SpawnFriction
			mo->friction = FRACUNIT - 0x100;
			//mo->movefactor = ((0x10092 - mo->friction)*(0x70))/0x158;
		}
		else
			mo->friction = ORIG_FRICTION;
	}
}

//
// P_XYFriction
//
// adds friction on the xy plane
//
static void P_XYFriction(mobj_t *mo, fixed_t oldx, fixed_t oldy)
{
	player_t *player;

	I_Assert(mo != NULL);
	I_Assert(!P_MobjWasRemoved(mo));

	player = mo->player;
	if (player) // valid only if player avatar
	{
		if (abs(player->rmomx) < FixedMul(STOPSPEED, mo->scale)
		    && abs(player->rmomy) < FixedMul(STOPSPEED, mo->scale)
		    && (!player->cmd.forwardmove && !player->cmd.sidemove)
			&& !(player->mo->standingslope && (!(player->mo->standingslope->flags & SL_NOPHYSICS)) && (abs(player->mo->standingslope->zdelta) >= FRACUNIT/2))
				)
		{
			// if in a walking frame, stop moving
			if (player->panim == PA_SLOW)
			{
				P_SetPlayerMobjState(mo, S_KART_STILL);
			}
			mo->momx = player->cmomx;
			mo->momy = player->cmomy;
		}
		else
		{
			if (oldx == mo->x && oldy == mo->y) // didn't go anywhere
			{
				mo->momx = FixedMul(mo->momx, ORIG_FRICTION);
				mo->momy = FixedMul(mo->momy, ORIG_FRICTION);
			}
			else
			{
				mo->momx = FixedMul(mo->momx, mo->friction);
				mo->momy = FixedMul(mo->momy, mo->friction);
			}

			mo->friction = ORIG_FRICTION;
		}
	}
	else
		P_SceneryXYFriction(mo, oldx, oldy);
}

static void P_PushableCheckBustables(mobj_t *mo)
{
	msecnode_t *node;
	fixed_t oldx;
	fixed_t oldy;

	I_Assert(mo != NULL);
	I_Assert(!P_MobjWasRemoved(mo));

	if (netgame && mo->player && mo->player->spectator)
		return;

	oldx = mo->x;
	oldy = mo->y;

	P_UnsetThingPosition(mo);
	mo->x += mo->momx;
	mo->y += mo->momy;
	P_SetThingPosition(mo);

	for (node = mo->touching_sectorlist; node; node = node->m_sectorlist_next)
	{
		ffloor_t *rover;
		fixed_t topheight, bottomheight;

		if (!node->m_sector)
			break;

		if (!node->m_sector->ffloors)
			continue;

		for (rover = node->m_sector->ffloors; rover; rover = rover->next)
		{
			if (!(rover->fofflags & FOF_EXISTS))
				continue;

			if (!(rover->fofflags & FOF_BUSTUP))
				continue;

			if (!(rover->bustflags & FB_PUSHABLES))
				continue;

			if (rover->master->frontsector->crumblestate != CRUMBLE_NONE)
				continue;

			topheight = P_GetFOFTopZ(mo, node->m_sector, rover, mo->x, mo->y, NULL);
			bottomheight = P_GetFOFBottomZ(mo, node->m_sector, rover, mo->x, mo->y, NULL);

			// Height checks
			if (rover->bustflags & FB_ONLYBOTTOM)
			{
				if (mo->z + mo->momz + mo->height < bottomheight)
					continue;

				if (mo->z + mo->height > bottomheight)
					continue;
			}
			else
			{
				switch (rover->busttype)
				{
				case BT_TOUCH:
					if (mo->z + mo->momz > topheight)
						continue;

					if (mo->z + mo->momz + mo->height < bottomheight)
						continue;

					break;
				case BT_SPINBUST:
					if (mo->z + mo->momz > topheight)
						continue;

					if (mo->z + mo->height < bottomheight)
						continue;

					break;
				default:
					if (mo->z >= topheight)
						continue;

					if (mo->z + mo->height < bottomheight)
						continue;

					break;
				}
			}

			EV_CrumbleChain(NULL, rover); // node->m_sector

			// Run a linedef executor??
			if (rover->bustflags & FB_EXECUTOR)
				P_LinedefExecute(rover->busttag, mo, node->m_sector);

			goto bustupdone;
		}
	}
bustupdone:
	P_UnsetThingPosition(mo);
	mo->x = oldx;
	mo->y = oldy;
	P_SetThingPosition(mo);
}

//
// P_CheckSkyHit
//
static dboolean P_CheckSkyHit(mobj_t *mo)
{
	if (g_tm.ceilingline && g_tm.ceilingline->backsector
		&& g_tm.ceilingline->backsector->ceilingpic == skyflatnum
		&& g_tm.ceilingline->frontsector
		&& g_tm.ceilingline->frontsector->ceilingpic == skyflatnum
		&& (mo->z >= g_tm.ceilingline->frontsector->ceilingheight
		|| mo->z >= g_tm.ceilingline->backsector->ceilingheight))
			return true;
	return false;
}

// P_LostSoulReset
static dboolean P_SkullFlyReset(mobj_t *mo)
{
	if (!mo->momx && !mo->momy && (mo->flags2 & MF2_SKULLFLY))
	{
		// the skull slammed into something
		mo->flags2 &= ~MF2_SKULLFLY;
		mo->momx = mo->momy = mo->momz = 0;

		// set in 'search new direction' state?
		if (mo->type != MT_EGGMOBILE)
			P_SetMobjState(mo, mo->info->spawnstate);
		
		return true;
	}
	return false;
}

//
// P_XYMovement
//
void P_XYMovement(mobj_t *mo)
{
	player_t *player;
	fixed_t xmove, ymove;
	fixed_t oldx, oldy; // reducing bobbing/momentum on ice when up against walls
	dboolean moved;
	pslope_t *oldslope = NULL;
	vector3_t slopemom = {0,0,0};
	fixed_t predictedz = 0;
	TryMoveResult_t result = {0};

	I_Assert(mo != NULL);
	I_Assert(!P_MobjWasRemoved(mo));

	// if it's stopped
	if (P_SkullFlyReset(mo)) return;

	player = mo->player; //valid only if player avatar

	xmove = mo->momx;
	ymove = mo->momy;

	oldx = mo->x;
	oldy = mo->y;

	if (mo->flags & MF_NOCLIPHEIGHT)
	{
		mo->standingslope = NULL;
		mo->terrain = NULL;
	}

	// adjust various things based on slope
	if (mo->standingslope && abs(mo->standingslope->zdelta) > FRACUNIT>>8) {
		if (!P_IsObjectOnGround(mo)) { // We fell off at some point? Do the twisty thing!
			P_SlopeLaunch(mo);
			xmove = mo->momx;
			ymove = mo->momy;
		} else { // Still on the ground.
			slopemom.x = xmove;
			slopemom.y = ymove;
			slopemom.z = 0;
			P_QuantizeMomentumToSlope(&slopemom, mo->standingslope);

			xmove = slopemom.x;
			ymove = slopemom.y;

			predictedz = mo->z + slopemom.z; // We'll use this later...

			oldslope = mo->standingslope;
		}
	} else if (P_IsObjectOnGround(mo) && !mo->momz)
		predictedz = mo->z;

	// Pushables can break some blocks
	if (CheckForBustableBlocks && ((mo->flags & MF_PUSHABLE) || ((mo->info->flags & MF_PUSHABLE) && mo->fuse)))
		P_PushableCheckBustables(mo);

	//{ SRB2kart - Ballhogs
	if (mo->type == MT_BALLHOG)
	{
		if (mo->health)
		{
			mo->health--;
			if (mo->health == 0)
				mo->destscale = 0;
		}
	}
	//}

	if (!P_TryMove(mo, mo->x + xmove, mo->y + ymove, true, &result)
		&& !(P_MobjWasRemoved(mo) || mo->eflags & MFE_SPRUNG))
	{
		// blocked move
		moved = false;

		if (LUA_HookMobjMoveBlocked(mo, result.mo, result.line))
		{
			if (P_MobjWasRemoved(mo))
				return;
		}
		else if (P_MobjWasRemoved(mo))
			return;

		P_PushSpecialLine(result.line, mo);

		//{ SRB2kart - Jawz
		if (mo->type == MT_JAWZ || mo->type == MT_JAWZ_DUD)
		{
			if (mo->health == 1)
			{
				// This Item Damage
				S_StartSound(mo, mo->info->deathsound);
				P_KillMobj(mo, NULL, NULL, DMG_NORMAL);

				P_SetObjectMomZ(mo, 8*FRACUNIT, false);
				P_InstaThrust(mo, R_PointToAngle2(mo->x, mo->y, mo->x + xmove, mo->y + ymove)+ANGLE_90, 16*FRACUNIT);
			}
		}
		//}
		else if (mo->flags & MF_MISSILE)
		{
			// explode a missile
			if (P_CheckSkyHit(mo))
			{
				// Hack to prevent missiles exploding
				// against the sky.
				// Does not handle sky floors.
				// Check frontsector as well.

				P_RemoveMobj(mo);
				return;
			}

			// draw damage on wall
			//SPLAT TEST ----------------------------------------------------------
#ifdef WALLSPLATS
			if (g_tm.blockingline && mo->type != MT_REDRING && mo->type != MT_FIREBALL
			&& !(mo->flags2 & (MF2_AUTOMATIC|MF2_RAILRING|MF2_BOUNCERING|MF2_EXPLOSION|MF2_SCATTER)))
				// set by last P_TryMove() that failed
			{
				divline_t divl;
				divline_t misl;
				fixed_t frac;

				P_MakeDivline(g_tm.blockingline, &divl);
				misl.x = mo->x;
				misl.y = mo->y;
				misl.dx = mo->momx;
				misl.dy = mo->momy;
				frac = P_InterceptVector(&divl, &misl);
				R_AddWallSplat(g_tm.blockingline, P_PointOnLineSide(mo->x,mo->y,g_tm.blockingline),
					"A_DMG3", mo->z, frac, SPLATDRAWMODE_SHADE);
			}
#endif
			// --------------------------------------------------------- SPLAT TEST

			P_ExplodeMissile(mo);
			return;
		}
		else if (mo->flags & MF_STICKY)
		{
			S_StartSound(mo, mo->info->activesound);
			mo->momx = mo->momy = mo->momz = 0; //Full stop!
			mo->flags |= MF_NOGRAVITY; //Stay there!
			mo->flags &= ~MF_STICKY; //Don't check again!

			// Check for hit against sky here
			if (P_CheckSkyHit(mo))
			{
				// Hack to prevent missiles exploding
				// against the sky.
				// Does not handle sky floors.
				// Check frontsector as well.

				P_RemoveMobj(mo);
				return;
			}
		}
		else
		{
			dboolean walltransfered = false;
			dboolean walltransferok = B_UseWallTransfer(mo);
			//if (player || mo->flags & MF_SLIDEME|MF_PUSHABLE)
			if (walltransferok)
			{ // try to slide along it

				pslope_t *transferslope = NULL;
				fixed_t transfermomz = 0;
				// Wall transfer part 1.
				if (oldslope && (P_MobjFlip(mo)*(predictedz - mo->z) > FRACUNIT>>8)) // Only for moving up (relative to gravity), otherwise there's a failed launch when going down slopes and hitting walls
				{
					transferslope = ((mo->standingslope) ? mo->standingslope : oldslope);
					if (((transferslope->zangle < ANGLE_180) ? transferslope->zangle : InvAngle(transferslope->zangle)) >= ANGLE_45) // Prevent some weird stuff going on on shallow slopes.
						transfermomz = P_GetWallTransferMomZ(mo, transferslope);
				}

				// Wall transfer part 2.
				if (transfermomz && transferslope) // Are we "transferring onto the wall" (really just a disguised vertical launch)?
				{
					angle_t relation; // Scale transfer momentum based on how head-on it is to the slope.

					P_SlideMove(mo, &result);
					xmove = ymove = 0;
					walltransfered = true;
					if (mo->momx || mo->momy) // "Guess" the angle of the wall you hit using new momentum
						relation = transferslope->xydirection - R_PointToAngle2(0, 0, mo->momx, mo->momy);
					else // Give it for free, I guess.
						relation = ANGLE_90;

					transfermomz = FixedMul(transfermomz,
						abs(FINESINE((relation >> ANGLETOFINESHIFT) & FINEMASK)));

					if (P_MobjFlip(mo)*(transfermomz - mo->momz) > 2*FRACUNIT) // Do the actual launch!
					{
						mo->momz = transfermomz;
						mo->standingslope = NULL;
						mo->terrain = NULL;

						if (mo->player)
						{
							S_StartSound(mo, sfx_kc5e);
							mo->player->walltransfered = true;
						}
					}
				}
			}

			if (walltransfered == false)
			{
				if (player)
				{
					P_BounceMove(mo,&result);
					if (P_MobjWasRemoved(mo))
						return;
					xmove = ymove = 0;
				}
				else if (mo->flags & MF_SLIDEME)
				{
					P_SlideMove(mo, &result);
					if (P_MobjWasRemoved(mo))
						return;
					xmove = ymove = 0;
				}
				else if (mo->flags & MF_BOUNCE)
				{
					P_BounceMove(mo, &result);
					if (P_MobjWasRemoved(mo))
						return;
					xmove = ymove = 0;
					S_StartSound(mo, mo->info->activesound);

					//{ SRB2kart - Orbinaut, Ballhog
					if (mo->type == MT_ORBINAUT || mo->type == MT_BALLHOG)
					{
						mobj_t *fx;
						fx = P_SpawnMobj(mo->x, mo->y, mo->z, MT_BUMP);
						if (mo->eflags & MFE_VERTICALFLIP)
							fx->eflags |= MFE_VERTICALFLIP;
						else
							fx->eflags &= ~MFE_VERTICALFLIP;
						fx->scale = mo->scale;
					}
					else if (mo->type == MT_EGGMINE)
					{
						K_SpawnEggMineBumpEffect(mo);
					}

					switch (mo->type)
					{
						case MT_ORBINAUT: // Orbinaut speed decreasing
							if (mo->health > 1)
							{
								S_StartSound(mo, mo->info->attacksound);
								mo->health--;
								// This prevents an item thrown at a wall from
								// phasing through you on its return.
								mo->threshold = 0;
							}
							/*FALLTHRU*/

						case MT_JAWZ:
							if (mo->health == 1)
							{
								// This Item Damage
								S_StartSound(mo, mo->info->deathsound);
								P_KillMobj(mo, NULL, NULL, DMG_NORMAL);

								P_SetObjectMomZ(mo, 8*FRACUNIT, false);
								P_InstaThrust(mo, R_PointToAngle2(mo->x, mo->y, mo->x + xmove, mo->y + ymove)+ANGLE_90, 16*FRACUNIT);
							}
							break;

						case MT_TRAPBUBBLE:
							S_StartSound(mo, sfx_s3k44); // Bubble bounce
							break;

						default:
							break;
					}
				}
				else
					mo->momx = mo->momy = 0;
			}
		}
		//
	}
	else
		moved = true;

	if (P_MobjWasRemoved(mo)) // MF_SPECIAL touched a player! O_o;;
		return;

	if (moved == true)
	{
		// TERRAIN footstep effects.
		K_HandleFootstepParticles(mo);
	}

	if (moved && oldslope && !(mo->flags & MF_NOCLIPHEIGHT))
	{
		// Check to see if we ran off
		if (oldslope != mo->standingslope)
		{
			// First, compare different slopes
			angle_t oldangle, newangle;
			angle_t moveangle = R_PointToAngle2(0, 0, mo->momx, mo->momy);

			oldangle = FixedMul((signed)oldslope->zangle, FINECOSINE((moveangle - oldslope->xydirection) >> ANGLETOFINESHIFT));

			if (mo->standingslope)
				newangle = FixedMul((signed)mo->standingslope->zangle, FINECOSINE((moveangle - mo->standingslope->xydirection) >> ANGLETOFINESHIFT));
			else
				newangle = 0;

			// Now compare the Zs of the different quantizations
			if (oldangle-newangle > ANG30 && oldangle-newangle < ANGLE_180)
			{
				// Allow for a bit of sticking - this value can be adjusted later
				mo->standingslope = oldslope;
				P_SetPitchRollFromSlope(mo, mo->standingslope);
				P_SlopeLaunch(mo);

				//CONS_Printf("launched off of slope - ");
			}

			/*
			CONS_Printf("old angle %f - new angle %f = %f\n",
				FIXED_TO_FLOAT(AngleFixed(oldangle)),
				FIXED_TO_FLOAT(AngleFixed(newangle)),
				FIXED_TO_FLOAT(AngleFixed(oldangle-newangle))
			);
			*/

		}
		else if (predictedz-mo->z > abs(slopemom.z/2)
		&& P_CanApplySlopePhysics(mo, mo->standingslope) == true)
		{
			// Now check if we were supposed to stick to this slope
			//CONS_Printf("%d-%d > %d\n", (predictedz), (mo->z), (slopemom.z/2));
			P_SlopeLaunch(mo);
		}
	}
	else if (moved && mo->standingslope && predictedz)
	{
		angle_t moveangle = K_MomentumAngle(mo);
		angle_t newangle = FixedMul((signed)mo->standingslope->zangle, FINECOSINE((moveangle - mo->standingslope->xydirection) >> ANGLETOFINESHIFT));

		/*
		CONS_Printf("flat to angle %f - predicted z of %f\n",
			FIXED_TO_FLOAT(AngleFixed(ANGLE_MAX-newangle)),
			FIXED_TO_FLOAT(predictedz)
		);
		*/

		if (ANGLE_MAX-newangle > ANG30 && newangle > ANGLE_180)
		{
			mo->momz = P_MobjFlip(mo)*FRACUNIT/2;
			mo->z = predictedz + P_MobjFlip(mo);
			mo->standingslope = NULL;
			mo->terrain = NULL;
			//CONS_Printf("Launched off of flat surface running into downward slope\n");
		}
	}

	// Check the gravity status.
	P_CheckGravity(mo, false);

	if (mo->flags & MF_NOCLIPHEIGHT)
		return; // no frictions for objects that can pass through floors

	if (mo->flags & MF_MISSILE || mo->flags2 & MF2_SKULLFLY || mo->type == MT_SHELL || mo->type == MT_VULTURE)
		return; // no friction for missiles ever

	if ((mo->type == MT_BIGTUMBLEWEED || mo->type == MT_LITTLETUMBLEWEED)
			&& (mo->standingslope && abs(mo->standingslope->zdelta) > FRACUNIT>>8)) // Special exception for tumbleweeds on slopes
		return;

	//{ SRB2kart stuff
	if (mo->type == MT_FLINGRING || mo->type == MT_BALLHOG)
		return;

	// If we're spinning out, cut friction at below 50%
	if (mo->player && (mo->player->spinouttimer && !mo->player->wipeoutslow) && mo->player->speed <= K_GetKartSpeed(mo->player, false, true)/2)
		return;

	if (mo->player && (mo->player->crash_state && !mo->player->wipeoutslow) && mo->player->speed <= FixedMul(K_GetKartSpeed(mo->player, false, true), 2 * FRACUNIT / 3))
	{
		// If we're crashing, cut friction at below 66%
		// Crashing should keep you moving a bit faster for the sake of game flow
		return;
	}
	//}

	if (((!(mo->eflags & MFE_VERTICALFLIP) && mo->z > mo->floorz) || (mo->eflags & MFE_VERTICALFLIP && mo->z+mo->height < mo->ceilingz))
		&& !(player && player->carry == CR_SLIDING))
		return; // no friction when airborne

	P_XYFriction(mo, oldx, oldy);
}

void P_RingXYMovement(mobj_t *mo)
{
	TryMoveResult_t result = {0};

	I_Assert(mo != NULL);
	I_Assert(!P_MobjWasRemoved(mo));

	if (!P_SceneryTryMove(mo, mo->x + mo->momx, mo->y + mo->momy, &result))
		P_BounceMove(mo, &result);
}

void P_SceneryXYMovement(mobj_t *mo)
{
	fixed_t oldx, oldy; // reducing bobbing/momentum on ice when up against walls
	TryMoveResult_t result = {0};

	I_Assert(mo != NULL);
	I_Assert(!P_MobjWasRemoved(mo));

	oldx = mo->x;
	oldy = mo->y;

	if (!P_SceneryTryMove(mo, mo->x + mo->momx, mo->y + mo->momy, &result))
		(mo->flags & MF_BOUNCE ? P_BounceMove : P_SlideMove)(mo, &result);

	if (P_MobjWasRemoved(mo))
		return;

	if ((!(mo->eflags & MFE_VERTICALFLIP) && mo->z > mo->floorz) || (mo->eflags & MFE_VERTICALFLIP && mo->z+mo->height < mo->ceilingz))
		return; // no friction when airborne

	if (mo->flags & MF_NOCLIPHEIGHT)
		return; // no frictions for objects that can pass through floors

	P_SceneryXYFriction(mo, oldx, oldy);
}

//
// P_AdjustMobjFloorZ_FFloors
//
// Utility function for P_ZMovement and related
// Adjusts mo->floorz/mo->ceiling accordingly for FFloors
//
// "motype" determines what behaviour to use exactly
// This is to keep things consistent in case these various object types NEED to be different
//
// motype options:
// 0 - normal
// 1 - forces false check for water (rings)
// 2 - forces false check for water + different quicksand behaviour (scenery)
//
void P_AdjustMobjFloorZ_FFloors(mobj_t *mo, sector_t *sector, uint8_t motype)
{
	ffloor_t *rover;
	fixed_t delta1, delta2, thingtop;
	fixed_t topheight, bottomheight;

	I_Assert(mo != NULL);
	I_Assert(!P_MobjWasRemoved(mo));

	thingtop = mo->z + mo->height;

	for (rover = sector->ffloors; rover; rover = rover->next)
	{
		if (!(rover->fofflags & FOF_EXISTS))
			continue;

		topheight = P_GetFOFTopZ(mo, sector, rover, mo->x, mo->y, NULL);
		bottomheight = P_GetFOFBottomZ(mo, sector, rover, mo->x, mo->y, NULL);

		if (P_CheckSolidFFloorSurface(mo, rover)) // only the player should stand on lava or run on water
			;
		else if (motype != 0 && rover->fofflags & FOF_SWIMMABLE) // "scenery" only
			continue;
		else if (rover->fofflags & FOF_QUICKSAND) // quicksand
			;
		else if (!( // if it's not either of the following...
				(rover->fofflags & (FOF_BLOCKPLAYER|FOF_MARIO) && mo->player) // ...solid to players? (mario blocks are always solid from beneath to players)
			    || (rover->fofflags & FOF_BLOCKOTHERS && !mo->player) // ...solid to others?
				)) // ...don't take it into account.
			continue;
		if (rover->fofflags & FOF_QUICKSAND)
		{
			switch (motype)
			{
				case 2: // scenery does things differently for some reason
					if (mo->z < topheight && bottomheight < thingtop)
					{
						mo->floorz = mo->z;
						continue;
					}
					break;
				default:
					if (mo->z < topheight && bottomheight < thingtop)
					{
						if (mo->floorz < mo->z)
							mo->floorz = mo->z;
					}
					continue; // This is so you can jump/spring up through quicksand from below.
			}
		}

		delta1 = mo->z - (bottomheight + ((topheight - bottomheight)/2));
		delta2 = thingtop - (bottomheight + ((topheight - bottomheight)/2));

		if (topheight > mo->floorz && abs(delta1) < abs(delta2)
			&& (rover->fofflags & FOF_SOLID) // Non-FOF_SOLID Mario blocks are only solid from bottom
			&& !(rover->fofflags & FOF_REVERSEPLATFORM)
			&& ((P_MobjFlip(mo)*mo->momz >= 0) || (!(rover->fofflags & FOF_PLATFORM)))) // In reverse gravity, only clip for FOFs that are intangible from their bottom (the "top" you're falling through) if you're coming from above ("below" in your frame of reference)
		{
			mo->floorz = topheight;
		}
		if (bottomheight < mo->ceilingz && abs(delta1) >= abs(delta2)
			&& !(rover->fofflags & FOF_PLATFORM)
			&& ((P_MobjFlip(mo)*mo->momz >= 0) || ((rover->fofflags & FOF_SOLID) && !(rover->fofflags & FOF_REVERSEPLATFORM)))) // In normal gravity, only clip for FOFs that are intangible from the top if you're coming from below
		{
			mo->ceilingz = bottomheight;
		}
	}
}

//
// P_AdjustMobjFloorZ_PolyObjs
//
// Utility function for P_ZMovement and related
// Adjusts mo->floorz/mo->ceiling accordingly for PolyObjs
//
static void P_AdjustMobjFloorZ_PolyObjs(mobj_t *mo, subsector_t *subsec)
{
	polyobj_t *po = subsec->polyList;
	sector_t *polysec;
	fixed_t delta1, delta2, thingtop;
	fixed_t polytop, polybottom;

	I_Assert(mo != NULL);
	I_Assert(!P_MobjWasRemoved(mo));

	thingtop = mo->z + mo->height;

	while(po)
	{
		if (!P_MobjInsidePolyobj(po, mo) || !(po->flags & POF_SOLID))
		{
			po = (polyobj_t *)(po->link.next);
			continue;
		}

		// We're inside it! Yess...
		polysec = po->lines[0]->backsector;

		if (po->flags & POF_CLIPPLANES)
		{
			polytop = polysec->ceilingheight;
			polybottom = polysec->floorheight;
		}
		else
		{
			polytop = INT32_MAX;
			polybottom = INT32_MIN;
		}

		delta1 = mo->z - (polybottom + ((polytop - polybottom)/2));
		delta2 = thingtop - (polybottom + ((polytop - polybottom)/2));

		if (polytop > mo->floorz && abs(delta1) < abs(delta2))
			mo->floorz = polytop;

		if (polybottom < mo->ceilingz && abs(delta1) >= abs(delta2))
			mo->ceilingz = polybottom;

		po = (polyobj_t *)(po->link.next);
	}
}

void P_RingZMovement(mobj_t *mo)
{
	I_Assert(mo != NULL);
	I_Assert(!P_MobjWasRemoved(mo));

	// Intercept the stupid 'fall through 3dfloors' bug
	if (mo->subsector->sector->ffloors)
		P_AdjustMobjFloorZ_FFloors(mo, mo->subsector->sector, 1);
	if (mo->subsector->polyList)
		P_AdjustMobjFloorZ_PolyObjs(mo, mo->subsector);

	// adjust height
	if (mo->eflags & MFE_APPLYPMOMZ && !P_IsObjectOnGround(mo))
	{
		mo->momz += mo->pmomz;
		mo->pmomz = 0;
		mo->eflags &= ~MFE_APPLYPMOMZ;
	}
	mo->z += mo->momz;

	// clip movement
	if (mo->z <= mo->floorz && !(mo->flags & MF_NOCLIPHEIGHT))
	{
		mo->z = mo->floorz;

		mo->momz = 0;
	}
	else if (mo->z + mo->height > mo->ceilingz && !(mo->flags & MF_NOCLIPHEIGHT))
	{
		mo->z = mo->ceilingz - mo->height;

		mo->momz = 0;
	}
}

dboolean P_CheckDeathPitCollide(const mobj_t *mo)
{
	I_Assert(mo != NULL);
	I_Assert(!P_MobjWasRemoved(mo));

	if (mo->player && mo->player->pflags & PF_GODMODE)
	{
		return false;
	}

	if (mo->subsector->sector->damagetype == SD_DEATHPIT)
	{
		const dboolean flipped = (mo->eflags & MFE_VERTICALFLIP);
		const sectorflags_t flags = mo->subsector->sector->flags;

		if (((flags & MSF_TRIGGERSPECIAL_HEADBUMP) || !flipped) && (flags & MSF_FLIPSPECIAL_FLOOR))
			return (mo->z <= P_GetSpecialBottomZ(mo, mo->subsector->sector, mo->subsector->sector));

		if (((flags & MSF_TRIGGERSPECIAL_HEADBUMP) || flipped) && (flags & MSF_FLIPSPECIAL_CEILING))
			return (mo->z + mo->height >= P_GetSpecialTopZ(mo, mo->subsector->sector, mo->subsector->sector));
	}

	return false;
}

dboolean P_CheckSolidLava(ffloor_t *rover)
{
	if ((rover->fofflags & FOF_SWIMMABLE) && (rover->master->frontsector->damagetype == SD_LAVA))
	{
		return true;
	}

	return false;
}

dboolean P_CheckDeathPitFOFs(const mobj_t *mo)
{
	if (P_CheckDeathPitCollide(mo))
		return true;

	const sector_t *sector = mo->subsector->sector;

	for (ffloor_t *rover = sector->ffloors; rover != NULL; rover = rover->next)
	{
		// i... think these are the right checks?
		if (rover->fofflags & FOF_EXISTS
		 && rover->master->frontsector->damagetype == SD_DEATHPIT
		 && P_IsMobjTouching3DFloor(mo, rover, sector))
			return true;
	}

	return false;
}

//
// P_ZMovement
// Returns false if the mobj was killed/exploded/removed, true otherwise.
//
dboolean P_ZMovement(mobj_t *mo)
{
	fixed_t dist, delta;
	dboolean onground;
	dboolean wasonground;

	I_Assert(mo != NULL);
	I_Assert(!P_MobjWasRemoved(mo));

	wasonground = P_IsObjectOnGround(mo);

	// Intercept the stupid 'fall through 3dfloors' bug
	if (mo->subsector->sector->ffloors)
		P_AdjustMobjFloorZ_FFloors(mo, mo->subsector->sector, 0);
	if (mo->subsector->polyList)
		P_AdjustMobjFloorZ_PolyObjs(mo, mo->subsector);

	// adjust height
	if (mo->eflags & MFE_APPLYPMOMZ && !P_IsObjectOnGround(mo))
	{
		mo->momz += mo->pmomz;
		mo->pmomz = 0;
		mo->eflags &= ~MFE_APPLYPMOMZ;
	}
	mo->z += mo->momz;
	onground = P_IsObjectOnGround(mo);

	if (mo->standingslope)
	{
		if (mo->flags & MF_NOCLIPHEIGHT)
			mo->standingslope = NULL;
		else if (!onground)
			P_SlopeLaunch(mo);
	}

	switch (mo->type)
	{
		case MT_SKIM:
			// skims don't bounce
			if (mo->z > mo->watertop && mo->z - mo->momz <= mo->watertop)
			{
				mo->z = mo->watertop;
				mo->momz = 0;
				mo->flags |= MF_NOGRAVITY;
			}
			break;
		case MT_SPINFIRE:
			if (P_CheckDeathPitCollide(mo))
			{
				P_RemoveMobj(mo);
				return false;
			}
			break;
		case MT_GOOP:
			if (P_CheckDeathPitCollide(mo))
			{
				P_RemoveMobj(mo);
				return false;
			}
			if (mo->z <= mo->floorz && mo->momz)
			{
				P_SetMobjState(mo, mo->info->meleestate);
				mo->momx = mo->momy = mo->momz = 0;
				mo->z = mo->floorz;
				if (mo->info->painsound)
					S_StartSound(mo, mo->info->painsound);
			}
			break;
		case MT_FALLINGROCK:
		case MT_BIGTUMBLEWEED:
		case MT_LITTLETUMBLEWEED:
		case MT_SHELL:
			// Remove stuff from death pits.
			if (P_CheckDeathPitCollide(mo))
			{
				P_RemoveMobj(mo);
				return false;
			}
			break;

		case MT_REDFLAG:
		case MT_BLUEFLAG:
			// Remove from death pits.  DON'T FUCKING DESPAWN IT DAMMIT
			if (P_CheckDeathPitCollide(mo))
			{
				mo->fuse = 1;
				return false;
			}
			break;

		case MT_RING: // Ignore still rings
		case MT_BLUEBALL:
			// Remove flinged stuff from death pits.
			if (P_CheckDeathPitCollide(mo))
			{
				P_RemoveMobj(mo);
				return false;
			}
			if (!(mo->momx || mo->momy || mo->momz))
				return true;
			break;
		case MT_FLINGRING:
			// Remove flinged rings from death pits.
			if (P_CheckDeathPitCollide(mo))
			{
				P_RemoveMobj(mo);
				return false;
			}
			break;
		case MT_BOUNCERING:
		case MT_INFINITYRING:
		case MT_AUTOMATICRING:
		case MT_RAILRING:
		case MT_EXPLOSIONRING:
		case MT_SCATTERRING:
		case MT_GRENADERING:
		case MT_BOUNCEPICKUP:
		case MT_RAILPICKUP:
		case MT_AUTOPICKUP:
		case MT_EXPLODEPICKUP:
		case MT_SCATTERPICKUP:
		case MT_GRENADEPICKUP:
			// Remove flinged stuff from death pits.
			if (P_CheckDeathPitCollide(mo) && (mo->flags2 & MF2_DONTRESPAWN))
			{
				P_RemoveMobj(mo);
				return false;
			}
			if (!(mo->momx || mo->momy || mo->momz))
				return true;
			break;
		case MT_FLAMEJET:
		case MT_VERTICALFLAMEJET:
			if (mo->flags & MF_BOUNCE)
				return true;
			break;
		case MT_SPIKE:
		case MT_WALLSPIKE:
			// Dead spike particles disappear upon ground contact
			if (!mo->health && (mo->z <= mo->floorz || mo->z + mo->height >= mo->ceilingz))
			{
				P_RemoveMobj(mo);
				return false;
			}
			break;
		default:
			// SRB2kart stuff that should die in pits
			// Shouldn't stop moving along the Z if there's no speed though!
			if (P_CanDeleteKartItem(mo->type))
			{
				// Remove stuff from death pits.
				if (P_CheckDeathPitCollide(mo))
				{
					P_RemoveMobj(mo);
					return false;
				}
			}
			break;
	}

	if (!mo->player && P_CheckDeathPitCollide(mo))
	{
		if (mo->flags & MF_ENEMY || mo->flags & MF_BOSS)
		{
			// Kill enemies and bosses.
			if (mo->health)
			{
				P_KillMobj(mo, NULL, NULL, DMG_NORMAL);
			}
			return false;
		}
	}

	if (P_MobjFlip(mo)*mo->momz < 0
	&& (mo->flags2 & MF2_CLASSICPUSH))
		mo->momx = mo->momy = 0;

	if (mo->flags & MF_FLOAT && mo->target && mo->health
	&& !(mo->type == MT_EGGMOBILE) && mo->target->health > 0)
	{
		// float down towards target if too close
		if (!(mo->flags2 & MF2_SKULLFLY) && !(mo->flags2 & MF2_INFLOAT))
		{
			dist = P_AproxDistance(mo->x - mo->target->x, mo->y - mo->target->y);

			delta = (mo->target->z + (mo->height>>1)) - mo->z;

			if (delta < 0 && dist < -(delta*3))
				mo->z -= FixedMul(FLOATSPEED, mo->scale);
			else if (delta > 0 && dist < (delta*3))
				mo->z += FixedMul(FLOATSPEED, mo->scale);

			if (mo->type == MT_JETJAW && mo->z + mo->height > mo->watertop)
				mo->z = mo->watertop - mo->height;
		}

	}

	// clip movement
	if (((mo->z <= mo->floorz && !(mo->eflags & MFE_VERTICALFLIP))
		|| (mo->z + mo->height >= mo->ceilingz && mo->eflags & MFE_VERTICALFLIP))
		&& !(mo->flags & MF_NOCLIPHEIGHT))
	{
		vector3_t mom;
		mom.x = mo->momx;
		mom.y = mo->momy;
		mom.z = mo->momz;

		fixed_t preclipz = mo->z;

		if (mo->eflags & MFE_VERTICALFLIP)
			mo->z = mo->ceilingz - mo->height;
		else
			mo->z = mo->floorz;

		if (!(mo->flags & MF_MISSILE) && mo->standingslope) // You're still on the ground; why are we here?
		{
			mo->momz = 0;
			return true;
		}

		P_CheckPosition(mo, mo->x, mo->y, NULL); // Sets mo->standingslope correctly
		if (P_MobjWasRemoved(mo)) // mobjs can be removed by P_CheckPosition -- Monster Iestyn 31/07/21
			return false;

		K_UpdateMobjTerrain(mo, ((mo->eflags & MFE_VERTICALFLIP) ? g_tm.ceilingpic : g_tm.floorpic));

		if (((mo->eflags & MFE_VERTICALFLIP) ? g_tm.ceilingslope : g_tm.floorslope) && (mo->type != MT_STEAM))
		{
			mo->standingslope = (mo->eflags & MFE_VERTICALFLIP) ? g_tm.ceilingslope : g_tm.floorslope;
			P_SetPitchRollFromSlope(mo, mo->standingslope);
			P_ReverseQuantizeMomentumToSlope(&mom, mo->standingslope);
		}

		// hit the floor
		if (mo->type == MT_FIREBALL) // special case for the fireball
			mom.z = P_MobjFlip(mo)*FixedMul(5*FRACUNIT, mo->scale);
		else if (mo->type == MT_SPINFIRE) // elemental shield fire is another exception here
			;
		else if (mo->type == MT_PLAYER) // only DEAD players
		{
			dboolean lastdeathbounce = false;

			if (!P_CheckDeathPitFOFs(mo))
				mom.z = max(56 * mo->scale / 5, -4*mom.z/5);
			else if (P_CheckDeathPitCollide(mo))
			{
				mo->z = preclipz;

				mo->player->extendeddeath = false;
				mo->flags |= (MF_NOCLIP|MF_NOCLIPHEIGHT); // Hit a deathpit, fall through the floor

				// Since we're falling into oblivion, NOW we can set the fuse!
				mo->fuse = 3 * TICRATE;

				return false;
			}

			if (mo->player)
			{
				if (mo->player->deadbounces == 0)
				{
					lastdeathbounce = true; // fall through floor next time
					mo->player->extendeddeath = false;
				}
				else if (mo->player->deadbounces > 0)
				{
					mo->player->deadbounces--; // Take away a bounce

					if (mo->player->deadbounces == 0)
					{
						// Our next bounce makes us fall off the screen; set the fuse.
						mo->fuse = 3 * TICRATE;
					}

					// FIXME: Slow players down when they bounce...
				}
			}

			if (lastdeathbounce)
			{
				mo->flags |= (MF_NOCLIP|MF_NOCLIPHEIGHT); // Bye bye!
			}
		}
		else if (mo->flags & MF_MISSILE)
		{
			if (!(mo->flags & MF_NOCLIP))
			{
				// This is a really ugly hard-coded hack to prevent grenades
				// from exploding the instant they hit the ground, and then
				// another to prevent them from turning into hockey pucks.
				// I'm sorry in advance. -SH
				// PS: Oh, and Brak's napalm bombs too, now.
				if (mo->flags & MF_GRENADEBOUNCE)
				{
					// Going down? (Or up in reverse gravity?)
					if (P_MobjFlip(mo)*mom.z < 0)
					{
						// If going slower than a fracunit, just stop.
						if (abs(mom.z) < mo->scale)
						{
							mom.x = mom.y = mom.z = 0;

							// Napalm hack
							if (mo->type == MT_CYBRAKDEMON_NAPALM_BOMB_LARGE && mo->fuse)
								mo->fuse = 1;
						}
						// Otherwise bounce up at half speed.
						else
							mom.z = -mom.z/2;
						S_StartSound(mo, mo->info->activesound);
					}
				}
				// Hack over. Back to your regularly scheduled detonation. -SH
				else
				{
					// Don't explode on the sky!
					if (!(mo->eflags & MFE_VERTICALFLIP)
					&& mo->subsector->sector->floorpic == skyflatnum
					&& mo->subsector->sector->floorheight == mo->floorz)
						P_RemoveMobj(mo);
					else if (mo->eflags & MFE_VERTICALFLIP
					&& mo->subsector->sector->ceilingpic == skyflatnum
					&& mo->subsector->sector->ceilingheight == mo->ceilingz)
						P_RemoveMobj(mo);
					else
						P_ExplodeMissile(mo);
					return false;
				}
			}
		}

		if (P_MobjFlip(mo)*mom.z < 0) // falling
		{
			mo->eflags |= MFE_JUSTHITFLOOR;
			K_SpawnSplashForMobj(mo, abs(mom.z));

			if (mo->flags2 & MF2_SKULLFLY) // the skull slammed into something
				mom.z = -mom.z;
			else if (mo->type == MT_BIGTUMBLEWEED
				|| mo->type == MT_LITTLETUMBLEWEED
				|| mo->type == MT_CANNONBALLDECOR
				|| mo->type == MT_FALLINGROCK)
			{
				mom.z = -FixedMul(mom.z, FixedDiv(17*FRACUNIT,20*FRACUNIT));

				if (mo->type == MT_BIGTUMBLEWEED || mo->type == MT_LITTLETUMBLEWEED)
				{
					if (abs(mom.x) < FixedMul(STOPSPEED, mo->scale)
						&& abs(mom.y) < FixedMul(STOPSPEED, mo->scale)
						&& abs(mom.z) < FixedMul(STOPSPEED*3, mo->scale))
					{
						if (mo->flags2 & MF2_AMBUSH)
						{
							// Give the tumbleweed another random kick if it runs out of steam.
							mom.z += P_MobjFlip(mo)*FixedMul(6*FRACUNIT, mo->scale);

							if (P_RandomChance(PR_FALLINGROCK, FRACUNIT/2))
								mom.x += FixedMul(6*FRACUNIT, mo->scale);
							else
								mom.x -= FixedMul(6*FRACUNIT, mo->scale);

							if (P_RandomChance(PR_FALLINGROCK, FRACUNIT/2))
								mom.y += FixedMul(6*FRACUNIT, mo->scale);
							else
								mom.y -= FixedMul(6*FRACUNIT, mo->scale);
						}
						else if (mo->standingslope && abs(mo->standingslope->zdelta) > FRACUNIT>>8)
						{
							// Pop the object up a bit to encourage bounciness
							//mom.z = P_MobjFlip(mo)*mo->scale;
						}
						else
						{
							mom.x = mom.y = mom.z = 0;
							P_SetMobjState(mo, mo->info->spawnstate);
						}
					}

					// Stolen from P_SpawnFriction
					mo->friction = FRACUNIT - 0x100;
				}
				else if (mo->type == MT_FALLINGROCK)
				{
					if (P_MobjFlip(mo)*mom.z > FixedMul(2*FRACUNIT, mo->scale))
						S_StartSound(mo, mo->info->activesound + P_RandomKey(PR_FALLINGROCK, mo->info->reactiontime));

					mom.z /= 2; // Rocks not so bouncy

					if (!mo->fuse
						&& abs(mom.x) < FixedMul(STOPSPEED*2, mo->scale)
						&& abs(mom.y) < FixedMul(STOPSPEED*2, mo->scale)
						&& abs(mom.z) < FixedMul(STOPSPEED*2*3, mo->scale))
					{
						//P_RemoveMobj(mo);
						//return false;
						mo->fuse = TICRATE;
					}
				}
				else if (mo->type == MT_CANNONBALLDECOR)
				{
					mom.z /= 2;
					if (abs(mom.z) < FixedMul(STOPSPEED*3, mo->scale))
						mom.z = 0;
				}
			}
			else
				mom.z = (g_tm.floorthing ? g_tm.floorthing->momz : 0);

		}
		else if (g_tm.floorthing)
			mom.z = g_tm.floorthing->momz;

		if (mo->standingslope) { // MT_STEAM will never have a standingslope, see above.
			P_QuantizeMomentumToSlope(&mom, mo->standingslope);
		}

		mo->momx = mom.x;
		mo->momy = mom.y;
		mo->momz = mom.z;

		if (mo->type == MT_STEAM)
			return true;
	}
	else
	{
		mo->terrain = NULL;

		if (!(mo->flags & MF_NOGRAVITY)) // Gravity here!
		{
			/// \todo may not be needed (done in P_MobjThinker normally)
			mo->eflags &= ~MFE_JUSTHITFLOOR;

			P_CheckGravity(mo, true);
		}
	}

	if (((mo->z + mo->height > mo->ceilingz && !(mo->eflags & MFE_VERTICALFLIP))
		|| (mo->z < mo->floorz && mo->eflags & MFE_VERTICALFLIP))
	&& !(mo->flags & MF_NOCLIPHEIGHT))
	{
		if (mo->eflags & MFE_VERTICALFLIP)
			mo->z = mo->floorz;
		else
			mo->z = mo->ceilingz - mo->height;

		if (mo->type == MT_SPINFIRE)
			;
		else if ((mo->flags & MF_MISSILE) && !(mo->flags & MF_NOCLIP))
		{
			// Hack 2: Electric Boogaloo -SH
			if (mo->flags & MF_GRENADEBOUNCE)
			{
				if (P_MobjFlip(mo)*mo->momz >= 0)
				{
					mo->momz = -mo->momz;
					S_StartSound(mo, mo->info->activesound);
				}
			}
			else
			{
				// Don't explode on the sky!
				if (!(mo->eflags & MFE_VERTICALFLIP)
				&& mo->subsector->sector->ceilingpic == skyflatnum
				&& mo->subsector->sector->ceilingheight == mo->ceilingz)
					P_RemoveMobj(mo);
				else if (mo->eflags & MFE_VERTICALFLIP
				&& mo->subsector->sector->floorpic == skyflatnum
				&& mo->subsector->sector->floorheight == mo->floorz)
					P_RemoveMobj(mo);
				else
					P_ExplodeMissile(mo);
				return false;
			}
		}

		if (P_MobjFlip(mo)*mo->momz > 0) // hit the ceiling
		{
			if (mo->flags2 & MF2_SKULLFLY) // the skull slammed into something
				mo->momz = -mo->momz;
			else
			// Flags bounce
			if (mo->type == MT_REDFLAG || mo->type == MT_BLUEFLAG)
			{
				mo->momz = -FixedMul(mo->momz, FixedDiv(17*FRACUNIT,20*FRACUNIT));
			}
			else
				mo->momz = 0;
		}
	}

	P_CheckSectorTransitionalEffects(mo, mo->subsector->sector, wasonground);

	return true;
}

// Check for "Mario" blocks to hit and bounce them
static void P_CheckMarioBlocks(mobj_t *mo)
{
	msecnode_t *node;

	if (netgame && mo->player->spectator)
		return;

	for (node = mo->touching_sectorlist; node; node = node->m_sectorlist_next)
	{
		ffloor_t *rover;

		if (!node->m_sector->ffloors)
			continue;

		for (rover = node->m_sector->ffloors; rover; rover = rover->next)
		{
			if (!(rover->fofflags & FOF_EXISTS))
				continue;

			if (!(rover->fofflags & FOF_MARIO))
				continue;

			if (mo->eflags & MFE_VERTICALFLIP)
				continue; // if you were flipped, your head isn't actually hitting your ceilingz is it?

			if (*rover->bottomheight != mo->ceilingz)
				continue;

			if (rover->fofflags & FOF_GOOWATER) // Brick block!
				EV_CrumbleChain(node->m_sector, rover);
			else // Question block!
				EV_MarioBlock(rover, node->m_sector, mo);
		}
	}
}

// Check if we're on a polyobject that triggers a linedef executor.
static dboolean P_PlayerPolyObjectZMovement(mobj_t *mo)
{
	msecnode_t *node;
	dboolean stopmovecut = false;

	if (!numPolyObjects)
		return false;

	for (node = mo->touching_sectorlist; node; node = node->m_sectorlist_next)
	{
		sector_t *sec = node->m_sector;
		subsector_t *newsubsec;
		size_t i;

		for (i = 0; i < numsubsectors; i++)
		{
			polyobj_t *po;
			sector_t *polysec;
			newsubsec = &subsectors[i];

			if (newsubsec->sector != sec)
				continue;

			for (po = newsubsec->polyList; po; po = (polyobj_t *)(po->link.next))
			{
				if (!(po->flags & POF_SOLID))
					continue;

				if (!P_MobjInsidePolyobj(po, mo))
					continue;

				polysec = po->lines[0]->backsector;

				// Moving polyobjects should act like conveyors if the player lands on one. (I.E. none of the momentum cut thing below) -Red
				if ((mo->z == polysec->ceilingheight || mo->z + mo->height == polysec->floorheight) && po->thinker)
					stopmovecut = true;

				if (!(po->flags & POF_LDEXEC))
					continue;

				if (mo->z != polysec->ceilingheight)
					continue;

				// We're landing on a PO, so check for a linedef executor.
				P_LinedefExecute(po->triggertag, mo, NULL);
			}
		}
	}

	return stopmovecut;
}

void P_PlayerZMovement(mobj_t *mo)
{
	dboolean onground;
	dboolean wasonground;

	I_Assert(mo != NULL);
	I_Assert(!P_MobjWasRemoved(mo));

	if (!mo->player)
		return;

	wasonground = P_IsObjectOnGround(mo);

	// Intercept the stupid 'fall through 3dfloors' bug
	if (mo->subsector->sector->ffloors)
		P_AdjustMobjFloorZ_FFloors(mo, mo->subsector->sector, 0);
	if (mo->subsector->polyList)
		P_AdjustMobjFloorZ_PolyObjs(mo, mo->subsector);

	// check for smooth step up
	if ((mo->eflags & MFE_VERTICALFLIP && mo->z + mo->height > mo->ceilingz)
		|| (!(mo->eflags & MFE_VERTICALFLIP) && mo->z < mo->floorz))
	{
		if (mo->eflags & MFE_VERTICALFLIP)
			mo->player->viewheight -= (mo->z+mo->height) - mo->ceilingz;
		else
			mo->player->viewheight -= mo->floorz - mo->z;

		mo->player->deltaviewheight =
			(P_GetPlayerViewHeight(mo->player) - mo->player->viewheight)>>3;
	}

	// adjust height
	if (mo->eflags & MFE_APPLYPMOMZ && !P_IsObjectOnGround(mo))
	{
		mo->momz += mo->pmomz;
		mo->pmomz = 0;
		mo->eflags &= ~MFE_APPLYPMOMZ;
	}

	mo->z += mo->momz;
	onground = P_IsObjectOnGround(mo);

	// Have player fall through floor?
	if (mo->player->playerstate == PST_DEAD
	|| mo->player->playerstate == PST_REBORN)
		return;

	// clip movement
	if (onground && !(mo->flags & MF_NOCLIPHEIGHT))
	{
		if (mo->eflags & MFE_VERTICALFLIP)
		{
			mo->z = mo->ceilingz - mo->height;
		}
		else
		{
			mo->z = mo->floorz;
		}

		if (mo->standingslope)
		{
			if (mo->flags & MF_NOCLIPHEIGHT)
				mo->standingslope = NULL;
			else if (!onground)
				P_SlopeLaunch(mo);
		}

		K_UpdateMobjTerrain(mo, (mo->eflags & MFE_VERTICALFLIP ? g_tm.ceilingpic : g_tm.floorpic));

		// Get up if you fell.
		if (mo->player->panim == PA_HURT && !K_IsPlayerDamaged(mo->player) && mo->player->squishedtimer == 0)
		{
			P_SetPlayerMobjState(mo, S_KART_STILL);
		}

		if (!mo->standingslope && (mo->eflags & MFE_VERTICALFLIP ? g_tm.ceilingslope : g_tm.floorslope))
		{
			// Handle landing on slope during Z movement
			P_HandleSlopeLanding(mo, (mo->eflags & MFE_VERTICALFLIP ? g_tm.ceilingslope : g_tm.floorslope));
		}

		if (P_MobjFlip(mo) * mo->momz < 0) // falling
		{
			dboolean clipmomz = !(P_CheckDeathPitCollide(mo));

			mo->pmomz = 0; // We're on a new floor, don't keep doing platform movement.
			mo->eflags |= MFE_JUSTHITFLOOR; // Spin Attack

			clipmomz = P_PlayerHitFloor(mo->player, true);
			P_PlayerPolyObjectZMovement(mo);

			if (clipmomz)
			{
				mo->momz = (g_tm.floorthing ? g_tm.floorthing->momz : 0);
			}
		}
		else if (g_tm.floorthing)
		{
			mo->momz = g_tm.floorthing->momz;
		}
	}
	else
	{
		mo->terrain = NULL;

		if (!(mo->flags & MF_NOGRAVITY)) // Gravity here!
		{
			if (P_IsObjectInGoop(mo) && !(mo->flags & MF_NOCLIPHEIGHT))
			{
				if (mo->z < mo->floorz)
				{
					mo->z = mo->floorz;
					mo->momz = 0;
				}
				else if (mo->z + mo->height > mo->ceilingz)
				{
					mo->z = mo->ceilingz - mo->height;
					mo->momz = 0;
				}
			}
			/// \todo may not be needed (done in P_MobjThinker normally)
			mo->eflags &= ~MFE_JUSTHITFLOOR;
			P_CheckGravity(mo, true);
		}
		
		if (mo->standingslope)
		{
			if (mo->flags & MF_NOCLIPHEIGHT)
				mo->standingslope = NULL;
			else if (!onground)
				P_SlopeLaunch(mo);
		}
	}

	if (((mo->eflags & MFE_VERTICALFLIP && mo->z < mo->floorz) || (!(mo->eflags & MFE_VERTICALFLIP) && mo->z + mo->height > mo->ceilingz))
		&& !(mo->flags & MF_NOCLIPHEIGHT))
	{
		if (mo->eflags & MFE_VERTICALFLIP)
			mo->z = mo->floorz;
		else
			mo->z = mo->ceilingz - mo->height;

		if (P_MobjFlip(mo)*mo->momz > 0)
		{
			if (CheckForMarioBlocks)
				P_CheckMarioBlocks(mo);

			mo->momz = 0;
		}
	}

	P_CheckSectorTransitionalEffects(mo, mo->subsector->sector, wasonground);
}

dboolean P_SceneryZMovement(mobj_t *mo)
{
	// Intercept the stupid 'fall through 3dfloors' bug
	if (mo->subsector->sector->ffloors)
		P_AdjustMobjFloorZ_FFloors(mo, mo->subsector->sector, 2);
	if (mo->subsector->polyList)
		P_AdjustMobjFloorZ_PolyObjs(mo, mo->subsector);

	// adjust height
	if (mo->eflags & MFE_APPLYPMOMZ && !P_IsObjectOnGround(mo))
	{
		mo->momz += mo->pmomz;
		mo->pmomz = 0;
		mo->eflags &= ~MFE_APPLYPMOMZ;
	}
	mo->z += mo->momz;

	switch (mo->type)
	{
		case MT_BOOMEXPLODE:
		case MT_BOOMPARTICLE:
			if (!(mo->flags & MF_BOUNCE) && (mo->z <= mo->floorz || mo->z+mo->height >= mo->ceilingz))
			{
				// set standingslope
				P_TryMove(mo, mo->x, mo->y, true, NULL);
				mo->momz = -mo->momz;
				if (mo->standingslope)
				{
					if (mo->flags & MF_NOCLIPHEIGHT)
						mo->standingslope = NULL;
					else if (!P_IsObjectOnGround(mo))
						P_SlopeLaunch(mo);
				}
				S_StartSound(mo, mo->info->activesound);
			}
			break;
		case MT_SMALLBUBBLE:
			if (mo->z <= mo->floorz || mo->z+mo->height >= mo->ceilingz) // Hit the floor, so POP!
			{
				// don't sounds stop when you kill the mobj..?
				// yes, they do, making this entirely redundant
				P_RemoveMobj(mo);
				return false;
			}
			break;
		case MT_MEDIUMBUBBLE:
			if (P_CheckDeathPitCollide(mo)) // Don't split if you fell in a pit
			{
				P_RemoveMobj(mo);
				return false;
			}
			if ((!(mo->eflags & MFE_VERTICALFLIP) && mo->z <= mo->floorz)
			|| (mo->eflags & MFE_VERTICALFLIP && mo->z+mo->height >= mo->ceilingz)) // Hit the floor, so split!
			{
				// split
				mobj_t *explodemo = NULL;
				uint8_t prandom, i;

				for (i = 0; i < 4; ++i) // split into four
				{
					prandom = P_RandomByte(PR_BUBBLE);
					explodemo = P_SpawnMobj(mo->x, mo->y, mo->z, MT_SMALLBUBBLE);
					explodemo->momx += ((prandom & 0x0F) << (FRACBITS-2)) * (i & 2 ? -1 : 1);
					explodemo->momy += ((prandom & 0xF0) << (FRACBITS-6)) * (i & 1 ? -1 : 1);
					explodemo->destscale = mo->scale;
					P_SetScale(explodemo, mo->scale);
				}

				if (mo->threshold != 42) // Don't make pop sound if threshold is 42.
					S_StartSound(explodemo, sfx_bubbl1 + P_RandomKey(PR_BUBBLE, 5));
				//note that we assign the bubble sound to one of the new bubbles.
				// in other words, IT ACTUALLY GETS USED YAAAAAAAY

				P_RemoveMobj(mo);
				return false;
			}
			else if (mo->z <= mo->floorz || mo->z+mo->height >= mo->ceilingz) // Hit the ceiling instead? Just disappear anyway
			{
				P_RemoveMobj(mo);
				return false;
			}
			break;
		case MT_SEED: // now scenery
			if (P_CheckDeathPitCollide(mo)) // No flowers for death pits
			{
				P_RemoveMobj(mo);
				return false;
			}
			// Soniccd seed turns into a flower!
			if ((!(mo->eflags & MFE_VERTICALFLIP) && mo->z <= mo->floorz)
			|| (mo->eflags & MFE_VERTICALFLIP && mo->z+mo->height >= mo->ceilingz))
			{
				mobjtype_t flowertype = ((P_RandomChance(PR_DECORATION, FRACUNIT/2)) ? MT_GFZFLOWER1 : MT_GFZFLOWER3);
				mobj_t *flower = P_SpawnMobjFromMobj(mo, 0, 0, 0, flowertype);
				if (flower)
				{
					P_SetScale(flower, mo->scale/16);
					flower->destscale = mo->scale;
					flower->scalespeed = mo->scale/8;
				}

				P_RemoveMobj(mo);
				return false;
			}
		default:
			break;
	}

	// clip movement
	if (((mo->z <= mo->floorz && !(mo->eflags & MFE_VERTICALFLIP))
		|| (mo->z + mo->height >= mo->ceilingz && mo->eflags & MFE_VERTICALFLIP))
	&& !(mo->flags & MF_NOCLIPHEIGHT))
	{
		if (mo->eflags & MFE_VERTICALFLIP)
			mo->z = mo->ceilingz - mo->height;
		else
			mo->z = mo->floorz;

		if (P_MobjFlip(mo)*mo->momz < 0) // falling
		{
			mo->eflags |= MFE_JUSTHITFLOOR; // Spin Attack

			if (mo->type == MT_KART_TIRE)
			{
				if (!P_CheckDeathPitFOFs(mo))
					mo->momz = -mo->momz;
				mo->flags |= MF_NOCLIPHEIGHT;
			}
			else if (g_tm.floorthing)
				mo->momz = g_tm.floorthing->momz;
			else if (!g_tm.floorthing)
				mo->momz = 0;
		}
	}
	else if (!(mo->flags & MF_NOGRAVITY)) // Gravity here!
	{
		/// \todo may not be needed (done in P_MobjThinker normally)
		mo->eflags &= ~MFE_JUSTHITFLOOR;

		P_CheckGravity(mo, true);
	}

	if (((mo->z + mo->height > mo->ceilingz && !(mo->eflags & MFE_VERTICALFLIP))
		|| (mo->z < mo->floorz && mo->eflags & MFE_VERTICALFLIP))
	&& !(mo->flags & MF_NOCLIPHEIGHT))
	{
		if (mo->eflags & MFE_VERTICALFLIP)
			mo->z = mo->floorz;
		else
			mo->z = mo->ceilingz - mo->height;

		if (P_MobjFlip(mo)*mo->momz > 0) // hit the ceiling
			mo->momz = 0;
	}

	return true;
}

//
// P_CanRunOnWater
//
// Returns true if mobj can water run on a 3D floor
//
dboolean P_CanRunOnWater(mobj_t *mobj, ffloor_t *rover)
{
	const dboolean flip = (mobj->eflags & MFE_VERTICALFLIP);
	fixed_t surfaceheight = INT32_MAX;
	fixed_t playerbottom = INT32_MAX;
	fixed_t surfDiff = INT32_MAX;
	fixed_t maxStep = INT32_MAX;
	dboolean doifit = false;

	pslope_t *waterSlope = NULL;
	angle_t ourZAng = 0;
	angle_t waterZAng = 0;

	if (rover == NULL)
	{
		// No rover.
		return false;
	}

	if (!(rover->fofflags & FOF_SWIMMABLE))
	{
		// It's not even a water FOF.
		return false;
	}

	if (mobj->player && mobj->player->carry != CR_NONE) // Special carry state.
	{
		// No good player state.
		return false;
	}

	if (P_IsObjectOnGround(mobj) == false && !K_ItemMobjAllowedtoWaterRun(mobj))
	{
		// Don't allow jumping onto water to start a water run.
		// (Unless you are some specfic item mobjs!)
		// (Already water running still counts as being on the ground.)
		return false;
	}

	if (K_WaterRun(mobj) == false)
	{
		// Basic conditions for enabling water run.
		return false;
	}

	if (mobj->standingslope != NULL)
	{
		ourZAng = mobj->standingslope->zangle;
	}

	waterSlope = (flip ? *rover->b_slope : *rover->t_slope);
	if (waterSlope != NULL)
	{
		waterZAng = waterSlope->zangle;
	}

	if (ourZAng != waterZAng)
	{
		// The surface slopes are different.
		return false;
	}

	surfaceheight = flip ? P_GetFFloorBottomZAt(rover, mobj->x, mobj->y) : P_GetFFloorTopZAt(rover, mobj->x, mobj->y);
	playerbottom = flip ? (mobj->z + mobj->height) : mobj->z;

	doifit = flip ? (surfaceheight - mobj->floorz >= mobj->height) : (mobj->ceilingz - surfaceheight >= mobj->height);

	if (!doifit)
	{
		// Player can't fit in this space.
		return false;
	}

	maxStep = P_GetThingStepUp(mobj, mobj->x, mobj->y);

	surfDiff = flip ? (surfaceheight - playerbottom) : (playerbottom - surfaceheight);
	if (surfDiff <= maxStep && surfDiff >= 0)
	{
		// We start water run IF we can step-down!
		return true;
	}

	return false;
}

// Of course this shit is different.
static dboolean P_HandleSolidLavaCompat(mobj_t *mobj, ffloor_t *rover)
{
	if (mapnamespace == MNS_SRB2KART)
	{
		I_Assert(mobj != NULL);
		I_Assert(!P_MobjWasRemoved(mobj));
		fixed_t topheight = P_GetFFloorTopZAt(rover, mobj->x, mobj->y);

		if (rover->fofflags & FOF_SWIMMABLE && GETSECSPECIAL(rover->master->frontsector->special, 1) == 3
			&& !(rover->master->flags & ML_BLOCKMONSTERS)
			&& ((rover->master->flags & ML_MIDPEG) || mobj->z-mobj->momz > topheight - FixedMul(16*FRACUNIT, mobj->scale)))
			return true;
	}

	return P_CheckSolidLava(rover);
}

dboolean P_CheckSolidFFloorSurface(mobj_t *mobj, ffloor_t *rover)
{
	if (!mobj->player)
	{
		// They can run on water now!
		return P_CanRunOnWater(mobj, rover);
	}



	return P_HandleSolidLavaCompat(mobj, rover) ||
		P_CanRunOnWater(mobj, rover);
}

//
// P_MobjCheckWater
//
// Check for water, set stuff in mobj_t struct for movement code later.
// This is called either by P_MobjThinker() or P_PlayerThink()
void P_MobjCheckWater(mobj_t *mobj)
{
	dboolean waterwasnotset = (mobj->watertop == INT32_MAX);
	dboolean wasinwater = (mobj->eflags & MFE_UNDERWATER) == MFE_UNDERWATER;
	dboolean wasingoo = (mobj->eflags & MFE_GOOWATER) == MFE_GOOWATER;
	fixed_t thingtop = mobj->z + mobj->height;
	sector_t *sector = mobj->subsector->sector;
	ffloor_t *rover;
	player_t *p = mobj->player; // Will just be null if not a player.
	fixed_t height = mobj->height;
	fixed_t halfheight = height / 2;
	dboolean wasgroundpounding = false;
	fixed_t top2 = P_GetSectorCeilingZAt(sector, mobj->x, mobj->y);
	fixed_t bot2 = P_GetSectorFloorZAt(sector, mobj->x, mobj->y);

	if (mobj->type == MT_WAYPOINT)
	{
		CONS_Printf("P_MobjCheckWater waypoint call (oh boy...)\n");
	}

	// Default if no water exists.
	mobj->watertop = mobj->waterbottom = mobj->z - 1000*FRACUNIT;

	// Reset water state.
	mobj->eflags &= ~(MFE_UNDERWATER|MFE_TOUCHWATER|MFE_GOOWATER|MFE_TOUCHLAVA);

	for (rover = sector->ffloors; rover; rover = rover->next)
	{
		fixed_t topheight, bottomheight;

		topheight = P_GetSpecialTopZ(mobj, sectors + rover->secnum, sector);
		bottomheight = P_GetSpecialBottomZ(mobj, sectors + rover->secnum, sector);

		if (!(rover->fofflags & FOF_EXISTS) || !(rover->fofflags & FOF_SWIMMABLE)
		 || (((rover->fofflags & FOF_BLOCKPLAYER) && mobj->player)
		 || ((rover->fofflags & FOF_BLOCKOTHERS) && !mobj->player)))
		{
			if (topheight < top2 && topheight > thingtop)
				top2 = topheight;
			if (bottomheight > bot2 && bottomheight < mobj->z)
				bot2 = bottomheight;
			continue;
		}

		if (mobj->eflags & MFE_VERTICALFLIP)
		{
			if (topheight < (thingtop - halfheight)
			 || bottomheight > (thingtop + halfheight))
				continue;
		}
		else
		{
			if (topheight < (mobj->z - halfheight)
			 || bottomheight > (mobj->z + halfheight))
				continue;
		}

		// Set the watertop and waterbottom
		mobj->watertop = topheight;
		mobj->waterbottom = bottomheight;

		// Just touching the water?
		if (((mobj->eflags & MFE_VERTICALFLIP) && thingtop - height < bottomheight)
		 || (!(mobj->eflags & MFE_VERTICALFLIP) && mobj->z + height > topheight))
			mobj->eflags |= MFE_TOUCHWATER;

		// Actually in the water?
		if (((mobj->eflags & MFE_VERTICALFLIP) && thingtop - (height>>1) > bottomheight)
		 || (!(mobj->eflags & MFE_VERTICALFLIP) && mobj->z + (height>>1) < topheight))
			mobj->eflags |= MFE_UNDERWATER;

		if (mobj->eflags & (MFE_TOUCHWATER|MFE_UNDERWATER))
		{
			if (rover->master->frontsector->damagetype == SD_LAVA)
				mobj->eflags |= MFE_TOUCHLAVA;

			if (rover->fofflags & FOF_GOOWATER && !(mobj->flags & MF_NOGRAVITY))
				mobj->eflags |= MFE_GOOWATER;
		}
	}

	if (mobj->terrain != NULL && K_AffectingTerrainActive())
	{
		if (mobj->terrain->flags & TRF_LIQUID)
		{
			// This floor is water.
			mobj->eflags |= MFE_TOUCHWATER;

			if (mobj->eflags & MFE_VERTICALFLIP)
			{
				mobj->watertop = thingtop + height;
				mobj->waterbottom = thingtop;
			}
			else
			{
				mobj->watertop = mobj->z;
				mobj->waterbottom = mobj->z - height;
			}
		}
	}

	if (mobj->watertop > top2)
		mobj->watertop = top2;

	if (mobj->waterbottom < bot2)
		mobj->waterbottom = bot2;

	// Spectators and dead players don't get to do any of the things after this.
	if (p && (p->spectator || p->playerstate != PST_LIVE))
	{
		return;
	}

	if (mobj->flags & MF_APPLYTERRAIN)
	{
		K_SpawnWaterTrail(mobj);
	}

	// The rest of this code only executes on a water state change.
	if (!!(mobj->eflags & MFE_UNDERWATER) == wasinwater)
		return;

	if (wasinwater && p != NULL)
	{
		if (K_GetShieldFromPlayer(p) != KSHIELD_BUBBLE
			&& mobj->player->waterskip == 0
			&& p->breathTimer > 15*TICRATE)
		{
			// Play the gasp sound
			S_StartSound(mobj, sfx_s3k38);
		}

		p->breathTimer = 0;
	}

	if ((p) // Players
	 || (mobj->flags & MF_PUSHABLE) // Pushables
	 || ((mobj->info->flags & MF_PUSHABLE) && mobj->fuse) // Previously pushable, might be moving still
	)
	{
		fixed_t waterZ = INT32_MAX;
		fixed_t solidZ = INT32_MAX;
		fixed_t diff = INT32_MAX;

		fixed_t thingZ = INT32_MAX;
		dboolean splashValid = false;

		if (mobj->eflags & MFE_VERTICALFLIP)
		{
			waterZ = mobj->waterbottom;
			solidZ = mobj->ceilingz;
		}
		else
		{
			waterZ = mobj->watertop;
			solidZ = mobj->floorz;
		}

		diff = waterZ - solidZ;
		if (mobj->eflags & MFE_VERTICALFLIP)
		{
			diff = -diff;
		}

		// Time to spawn the bubbles!
		{
			int32_t i;
			int32_t bubblecount;
			uint8_t prandom[4];
			mobj_t *bubble;
			mobjtype_t bubbletype;

			if (mobj->eflags & MFE_GOOWATER || wasingoo)
				S_StartSound(mobj, sfx_ghit);
			else if (mobj->eflags & MFE_TOUCHLAVA)
				S_StartSound(mobj, sfx_splash);
			else
				S_StartSound(mobj, sfx_splish); // And make a sound!

			bubblecount = FixedDiv(abs(mobj->momz), mobj->scale)>>(FRACBITS-1);
			// Max bubble count
			if (bubblecount > 128)
				bubblecount = 128;

			// Create tons of bubbles
			for (i = 0; i < bubblecount; i++)
			{
				// P_RandomByte()s are called individually to allow consistency
				// across various compilers, since the order of function calls
				// in C is not part of the ANSI specification.
				prandom[0] = P_RandomByte(PR_BUBBLE);
				prandom[1] = P_RandomByte(PR_BUBBLE);
				prandom[2] = P_RandomByte(PR_BUBBLE);
				prandom[3] = P_RandomByte(PR_BUBBLE);

				bubbletype = MT_SMALLBUBBLE;
				if (!(prandom[0] & 0x3)) // medium bubble chance up to 64 from 32
					bubbletype = MT_MEDIUMBUBBLE;

				bubble = P_SpawnMobj(
					mobj->x + FixedMul((prandom[1]<<(FRACBITS-3)) * (prandom[0]&0x80 ? 1 : -1), mobj->scale),
					mobj->y + FixedMul((prandom[2]<<(FRACBITS-3)) * (prandom[0]&0x40 ? 1 : -1), mobj->scale),
					mobj->z + FixedMul((prandom[3]<<(FRACBITS-2)), mobj->scale), bubbletype);

				if (bubble)
				{
					if (P_MobjFlip(mobj)*mobj->momz < 0)
						bubble->momz = mobj->momz >> 4;
					else
						bubble->momz = 0;

					bubble->destscale = mobj->scale;
					P_SetScale(bubble, mobj->scale);
				}
			}
		}

		if (waterwasnotset)
			return;

		// Check to make sure you didn't just cross into a sector to jump out of
		// that has shallower water than the block you were originally in.
		if (diff <= (height >> 1))
		{
			return;
		}

		if (!wasgroundpounding && (mobj->eflags & MFE_GOOWATER || wasingoo)) // Decide what happens to your momentum when you enter/leave goopy water.
		{
			if (P_MobjFlip(mobj)*mobj->momz < 0) // You are entering the goo?
				mobj->momz = FixedMul(mobj->momz, FixedDiv(2*FRACUNIT, 5*FRACUNIT)); // kill momentum significantly, to make the goo feel thick.

			// Kill heavy airdrop too so you don't get softlocked.
			K_KillAirDrop(mobj->player, PAF_AIRDROP_HEAVY);
		}
		else if (wasinwater && P_MobjFlip(mobj) * mobj->momz > 0)
		{
			// Give the mobj a little out-of-water boost.
			mobj->momz = FixedMul(mobj->momz, FixedDiv(780*FRACUNIT, 457*FRACUNIT));
		}

		if (mobj->eflags & MFE_VERTICALFLIP)
		{
			thingZ = thingtop - (height >> 1);
			splashValid = (thingZ - mobj->momz <= waterZ);
		}
		else
		{
			thingZ = mobj->z + (height >> 1);
			splashValid = (thingZ - mobj->momz >= waterZ);
		}

		if (P_MobjFlip(mobj) * mobj->momz <= 0)
		{
			if (splashValid == true)
			{
				// Spawn a splash
				mobj_t *splish;
				mobjtype_t splishtype = (mobj->eflags & MFE_TOUCHLAVA) ? MT_LAVASPLISH : MT_SPLISH;

				if (mobj->eflags & MFE_VERTICALFLIP)
				{
					splish = P_SpawnMobj(mobj->x, mobj->y, waterZ - FixedMul(mobjinfo[splishtype].height, mobj->scale), splishtype);
					splish->flags2 |= MF2_OBJECTFLIP;
					splish->eflags |= MFE_VERTICALFLIP;
				}
				else
				{
					splish = P_SpawnMobj(mobj->x, mobj->y, waterZ, splishtype);
				}

				splish->destscale = mobj->scale;
				P_SetScale(splish, mobj->scale);

				// skipping stone!
				if (p && K_CheckWaterskipLockout(p)
					&& p->waterskip < 2
					&& ((p->speed/3 > abs(mobj->momz)) // Going more forward than horizontal, so you can skip across the water.
					|| (p->speed > K_GetKartSpeed(p,false,true)/3 && p->waterskip)) // Already skipped once, so you can skip once more!
					&& ((!(mobj->eflags & MFE_VERTICALFLIP) && thingtop - mobj->momz > mobj->watertop)
					|| ((mobj->eflags & MFE_VERTICALFLIP) && mobj->z - mobj->momz < mobj->waterbottom)))
				{
					const fixed_t min = 6<<FRACBITS;

					mobj->momx = mobj->momx/2;
					mobj->momy = mobj->momy/2;
					mobj->momz = -mobj->momz/2;

					if (!(mobj->eflags & MFE_VERTICALFLIP) && mobj->momz < FixedMul(min, mobj->scale))
						mobj->momz = FixedMul(min, mobj->scale);
					else if (mobj->eflags & MFE_VERTICALFLIP && mobj->momz > FixedMul(-min, mobj->scale))
						mobj->momz = FixedMul(-min, mobj->scale);

					p->waterskip++;
				}
			}
		}
		else if (P_MobjFlip(mobj) * mobj->momz > 0)
		{
			if (splashValid == true && !(mobj->eflags & MFE_UNDERWATER)) // underwater check to prevent splashes on opposite side
			{
				// Spawn a splash
				mobj_t *splish;
				mobjtype_t splishtype = (mobj->eflags & MFE_TOUCHLAVA) ? MT_LAVASPLISH : MT_SPLISH;

				if (mobj->eflags & MFE_VERTICALFLIP)
				{
					splish = P_SpawnMobj(mobj->x, mobj->y, waterZ - FixedMul(mobjinfo[splishtype].height, mobj->scale), splishtype);
					splish->flags2 |= MF2_OBJECTFLIP;
					splish->eflags |= MFE_VERTICALFLIP;
				}
				else
				{
					splish = P_SpawnMobj(mobj->x, mobj->y, waterZ, splishtype);
				}

				splish->destscale = mobj->scale;
				P_SetScale(splish, mobj->scale);
			}
		}
	}
}

static void P_SceneryCheckWater(mobj_t *mobj)
{
	sector_t *sector;

	if (mobj->type == MT_WAYPOINT)
	{
		CONS_Printf("P_SceneryCheckWater waypoint call (oh boy...)\n");
	}

	// Default if no water exists.
	mobj->watertop = mobj->waterbottom = mobj->z - 1000*FRACUNIT;

	// see if we are in water, and set some flags for later
	sector = mobj->subsector->sector;

	if (sector->ffloors)
	{
		ffloor_t *rover;
		fixed_t topheight, bottomheight;

		mobj->eflags &= ~(MFE_UNDERWATER|MFE_TOUCHWATER);

		for (rover = sector->ffloors; rover; rover = rover->next)
		{
			if (!(rover->fofflags & FOF_EXISTS) || !(rover->fofflags & FOF_SWIMMABLE) || rover->fofflags & FOF_BLOCKOTHERS)
				continue;

			topheight    = P_GetFFloorTopZAt   (rover, mobj->x, mobj->y);
			bottomheight = P_GetFFloorBottomZAt(rover, mobj->x, mobj->y);

			if (topheight <= mobj->z
				|| bottomheight > (mobj->z + (mobj->height>>1)))
				continue;

			if (mobj->z + mobj->height > topheight)
				mobj->eflags |= MFE_TOUCHWATER;
			else
				mobj->eflags &= ~MFE_TOUCHWATER;

			// Set the watertop and waterbottom
			mobj->watertop = topheight;
			mobj->waterbottom = bottomheight;

			if (mobj->z + (mobj->height>>1) < topheight)
				mobj->eflags |= MFE_UNDERWATER;
			else
				mobj->eflags &= ~MFE_UNDERWATER;
		}
	}
	else
		mobj->eflags &= ~(MFE_UNDERWATER|MFE_TOUCHWATER);
}

static dboolean P_CameraCheckHeat(camera_t *thiscam)
{
	sector_t *sector;
	fixed_t halfheight = thiscam->z + (thiscam->height >> 1);

	// see if we are in water
	sector = thiscam->subsector->sector;

	if (sector->flags & MSF_HEATWAVE)
		return true;

	if (sector->ffloors)
	{
		ffloor_t *rover;

		for (rover = sector->ffloors; rover; rover = rover->next)
		{
			if (!(rover->fofflags & FOF_EXISTS))
				continue;

			if (halfheight >= P_GetFFloorTopZAt(rover, thiscam->x, thiscam->y))
				continue;
			if (halfheight <= P_GetFFloorBottomZAt(rover, thiscam->x, thiscam->y))
				continue;

			if (rover->master->frontsector->flags & MSF_HEATWAVE)
				return true;
		}
	}

	return false;
}

static dboolean P_CameraCheckWater(camera_t *thiscam)
{
	sector_t *sector;
	fixed_t halfheight = thiscam->z + (thiscam->height >> 1);

	// see if we are in water
	sector = thiscam->subsector->sector;

	if (sector->ffloors)
	{
		ffloor_t *rover;

		for (rover = sector->ffloors; rover; rover = rover->next)
		{
			if (!(rover->fofflags & FOF_EXISTS) || !(rover->fofflags & FOF_SWIMMABLE) || rover->fofflags & FOF_BLOCKOTHERS)
				continue;

			if (halfheight >= P_GetFFloorTopZAt(rover, thiscam->x, thiscam->y))
				continue;
			if (halfheight <= P_GetFFloorBottomZAt(rover, thiscam->x, thiscam->y))
				continue;

			return true;
		}
	}

	return false;
}

// the below is chasecam only, if you're curious. check out P_CalcPostImg in p_user.c for first person
void P_CalcChasePostImg(player_t *player, camera_t *thiscam)
{
	const dboolean flipcam = ((player->pflags & PF_FLIPCAM) && (player->mo->eflags & MFE_VERTICALFLIP));
	uint8_t postimgtype = 0;

	// This can happen when joining
	if (thiscam->subsector == NULL || thiscam->subsector->sector == NULL)
		return;

	if (encoremode)
		postimgtype |= POSTIMG_MIRROR;
	if (flipcam)
		postimgtype |= POSTIMG_FLIP;

#ifdef HWRENDER
	if (rendermode == render_opengl && splitscreen)
	{
		thiscam->postimgflags = postimgtype;
		return;
	}
#endif

	if (player->awayviewtics && player->awayviewmobj && !P_MobjWasRemoved(player->awayviewmobj)) // Camera must obviously exist
	{
		camera_t dummycam;

		dummycam.subsector = player->awayviewmobj->subsector;
		dummycam.x = player->awayviewmobj->x;
		dummycam.y = player->awayviewmobj->y;
		dummycam.z = player->awayviewmobj->z;
		//dummycam.height = 40*FRACUNIT; // alt view height is 20*FRACUNIT
		dummycam.height = 0;			 // Why? Remote viewpoint cameras have no height.

		// Are we in water?
		if (P_CameraCheckWater(&dummycam))
			postimgtype |= POSTIMG_WATER;
		else if (P_CameraCheckHeat(&dummycam))
			postimgtype |= POSTIMG_HEAT;
	}
	else
	{
		// Are we in water?
		if (P_CameraCheckWater(thiscam))
			postimgtype |= POSTIMG_WATER;
		else if (P_CameraCheckHeat(thiscam))
			postimgtype |= POSTIMG_HEAT;
	}

	thiscam->postimgflags = postimgtype;
}

// P_CameraThinker
//
// Process the mobj-ish required functions of the camera
dboolean P_CameraThinker(player_t *player, camera_t *thiscam, dboolean resetcalled)
{
	// This can happen when joining
	if (thiscam->subsector == NULL || thiscam->subsector->sector == NULL)
		return true;

	P_CalcChasePostImg(player, thiscam);

	if (thiscam->momx || thiscam->momy)
	{
		if (!P_TryCameraMove(thiscam->x + thiscam->momx, thiscam->y + thiscam->momy, thiscam)) // Thanks for the greatly improved camera, Lach -- Sev
		{
			mobj_t dummy;

			dummy.thinker.function = (actionf_p1)P_MobjThinker;
			dummy.subsector = thiscam->subsector;
			dummy.x = thiscam->x;
			dummy.y = thiscam->y;
			dummy.z = thiscam->z;
			dummy.height = thiscam->height;

			if (player->pflags & PF_NOCONTEST)
			{
				player->karthud[khud_timeovercam] = (2*TICRATE)+1;
			}

			if (!resetcalled && !(player->mo->flags & MF_NOCLIP || leveltime < introtime) && !P_CheckSight(&dummy, player->mo)) // TODO: "P_CheckCameraSight" instead.
			{
				P_ResetCamera(player, thiscam);
			}
			else
			{
				fixed_t camspeed = P_AproxDistance(thiscam->momx, thiscam->momy);

				if (!resetcalled && P_AproxDistance(thiscam->momx, thiscam->momy) == camspeed)
				{
					P_ResetCamera(player, thiscam);
					resetcalled = true;
				}
			}

			if (resetcalled) // Okay this means the camera is fully reset.
				return true;
		}
	}

	thiscam->subsector = R_PointInSubsectorFast(thiscam->x, thiscam->y);
	thiscam->floorz = g_tm.floorz;
	thiscam->ceilingz = g_tm.ceilingz;

	if (thiscam->momz || thiscam->pmomz)
	{
		// adjust height
		thiscam->z += thiscam->momz + thiscam->pmomz;
	}

	if (thiscam->ceilingz - thiscam->z < thiscam->height
		&& thiscam->ceilingz >= thiscam->z)
	{
		thiscam->ceilingz = thiscam->z + thiscam->height;
		thiscam->floorz = thiscam->z;
	}
	return false;
}

static void P_CheckCrumblingPlatforms(mobj_t *mobj)
{
	msecnode_t *node;

	if (netgame && mobj->player->spectator)
		return;

	for (node = mobj->touching_sectorlist; node; node = node->m_sectorlist_next)
	{
		ffloor_t *rover;

		for (rover = node->m_sector->ffloors; rover; rover = rover->next)
		{
			if (!(rover->fofflags & FOF_EXISTS))
				continue;

			if (!(rover->fofflags & FOF_CRUMBLE))
				continue;

			if (mobj->eflags & MFE_VERTICALFLIP)
			{
				if (P_GetSpecialBottomZ(mobj, sectors + rover->secnum, node->m_sector) != mobj->z + mobj->height)
					continue;
			}
			else
			{
				if (P_GetSpecialTopZ(mobj, sectors + rover->secnum, node->m_sector) != mobj->z)
					continue;
			}

			EV_StartCrumble(rover->master->frontsector, rover, (rover->fofflags & FOF_FLOATBOB), mobj->player, rover->alpha, !(rover->fofflags & FOF_NORETURN));
		}
	}
}

static dboolean P_MobjTouchesSectorWithWater(mobj_t *mobj)
{
	msecnode_t *node;

	for (node = mobj->touching_sectorlist; node; node = node->m_sectorlist_next)
	{
		ffloor_t *rover;

		if (!node->m_sector->ffloors)
			continue;

		for (rover = node->m_sector->ffloors; rover; rover = rover->next)
		{
			if (!(rover->fofflags & FOF_EXISTS))
				continue;

			if (!(rover->fofflags & FOF_SWIMMABLE))
				continue;

			return true;
		}
	}

	return false;
}

// Check for floating water platforms and bounce them
static void P_CheckFloatbobPlatforms(mobj_t *mobj)
{
	msecnode_t *node;

	// Can't land on anything if you're not moving downwards
	if (P_MobjFlip(mobj)*mobj->momz >= 0)
		return;

	if (!P_MobjTouchesSectorWithWater(mobj))
		return;

	for (node = mobj->touching_sectorlist; node; node = node->m_sectorlist_next)
	{
		ffloor_t *rover;

		if (!node->m_sector->ffloors)
			continue;

		for (rover = node->m_sector->ffloors; rover; rover = rover->next)
		{
			if (!(rover->fofflags & FOF_EXISTS))
				continue;

			if (!(rover->fofflags & FOF_FLOATBOB))
				continue;


			if (mobj->eflags & MFE_VERTICALFLIP)
			{
				if (abs(*rover->bottomheight - (mobj->z + mobj->height)) > abs(mobj->momz))
					continue;
			}
			else
			{
				if (abs(*rover->topheight - mobj->z) > abs(mobj->momz))
					continue;
			}

			// Initiate a 'bouncy' elevator function which slowly diminishes.
			EV_BounceSector(rover->master->frontsector, -mobj->momz, rover->master);
		}
	}
}

static void P_PlayerMobjThinker(mobj_t *mobj)
{
	I_Assert(mobj != NULL);
	I_Assert(mobj->player != NULL);
	I_Assert(!P_MobjWasRemoved(mobj));

	P_MobjCheckWater(mobj);

	P_ButteredSlope(mobj);

	// momentum movement
	mobj->eflags &= ~MFE_JUSTSTEPPEDDOWN;

	// Zoom tube
	if ((mobj->player->carry == CR_ZOOMTUBE && mobj->tracer && !P_MobjWasRemoved(mobj->tracer))
		|| mobj->player->loop.radius != 0)
	{
		P_HitSpecialLines(mobj, mobj->x, mobj->y, mobj->momx, mobj->momy);
		P_UnsetThingPosition(mobj);
		mobj->x += mobj->momx;
		mobj->y += mobj->momy;
		mobj->z += mobj->momz;
		P_SetThingPosition(mobj);
		P_CheckPosition(mobj, mobj->x, mobj->y, NULL);
		mobj->floorz = g_tm.floorz;
		mobj->ceilingz = g_tm.ceilingz;
		mobj->standingslope = NULL;
		mobj->terrain = NULL;
		goto animonly;
	}

	// Needed for gravity boots
	P_CheckGravity(mobj, false);

	if (mobj->momx || mobj->momy)
	{
		P_XYMovement(mobj);

		if (P_MobjWasRemoved(mobj))
			return;
	}
	else
		P_TryMove(mobj, mobj->x, mobj->y, true, NULL);

	P_CheckCrumblingPlatforms(mobj);

	if (CheckForFloatBob)
		P_CheckFloatbobPlatforms(mobj);

	// always do the gravity bit now, that's simpler
	// BUT CheckPosition only if wasn't done before.
	if (!(mobj->eflags & MFE_ONGROUND) || mobj->momz
		|| ((mobj->eflags & MFE_VERTICALFLIP) && mobj->z + mobj->height != mobj->ceilingz)
		|| (!(mobj->eflags & MFE_VERTICALFLIP) && mobj->z != mobj->floorz)
		|| P_IsObjectInGoop(mobj))
	{
		P_PlayerZMovement(mobj);
		P_CheckPosition(mobj, mobj->x, mobj->y, NULL); // Need this to pick up objects!

		if (P_MobjWasRemoved(mobj))
			return;
	}
	else
	{
		mobj->eflags &= ~MFE_JUSTHITFLOOR;
	}

	K_UpdateTerrainOverlay(mobj);

animonly:
	P_CyclePlayerMobjState(mobj);
}

void P_CalculatePrecipFloor(precipmobj_t *mobj)
{
	// recalculate floorz each time
	const sector_t *mobjsecsubsec;
	dboolean setWater = false;

	if (mobj && mobj->subsector && mobj->subsector->sector)
		mobjsecsubsec = mobj->subsector->sector;
	else
		return;

	mobj->precipflags &= ~PCF_INVISIBLE;
	mobj->floorz = P_GetSectorFloorZAt(mobjsecsubsec, mobj->x, mobj->y);
	mobj->ceilingz = P_GetSectorCeilingZAt(mobjsecsubsec, mobj->x, mobj->y);

	if (mobjsecsubsec->ffloors)
	{
		ffloor_t *rover;
		fixed_t height;

		for (rover = mobjsecsubsec->ffloors; rover; rover = rover->next)
		{
			// If it exists, it'll get rained on.
			if (!(rover->fofflags & FOF_EXISTS))
				continue;

			if (precipprops[curWeather].effects & PRECIPFX_WATERPARTICLES)
			{
				if (!(rover->fofflags & FOF_SWIMMABLE))
					continue;

				if (setWater == false)
				{
					mobj->ceilingz = P_GetFFloorTopZAt(rover, mobj->x, mobj->y);
					mobj->floorz = P_GetFFloorBottomZAt(rover, mobj->x, mobj->y);
					setWater = true;
				}
				else
				{
					height = P_GetFFloorTopZAt(rover, mobj->x, mobj->y);
					if (height > mobj->ceilingz)
						mobj->ceilingz = height;

					height = P_GetFFloorBottomZAt(rover, mobj->x, mobj->y);
					if (height < mobj->floorz)
						mobj->floorz = height;
				}
			}
			else
			{
				if (!(rover->fofflags & FOF_BLOCKOTHERS) && !(rover->fofflags & FOF_SWIMMABLE))
					continue;

				height = P_GetFFloorTopZAt(rover, mobj->x, mobj->y);
				if (height > mobj->floorz)
					mobj->floorz = height;
			}
		}
	}

	if ((precipprops[curWeather].effects & PRECIPFX_WATERPARTICLES) && setWater == false)
	{
		mobj->precipflags |= PCF_INVISIBLE;
	}
}

void P_RecalcPrecipInSector(sector_t *sector)
{
	mprecipsecnode_t *psecnode;

	if (!sector)
		return;

	sector->moved = true; // Recalc lighting and things too, maybe

	for (psecnode = sector->touching_preciplist; psecnode; psecnode = psecnode->m_thinglist_next)
		P_CalculatePrecipFloor(psecnode->m_thing);
}

//
// P_NullPrecipThinker
//
// Just the identification of a precip thinker. The thinker
// should never actually be called!

void P_NullPrecipThinker(precipmobj_t *mobj)
{
	(void)mobj;
	I_Assert("P_NullPrecipThinker should not be called" == 0);

}

dboolean P_PrecipThinker(precipmobj_t *mobj)
{
	dboolean flip = (mobj->precipflags & PCF_FLIP);

	if (mobj->lastThink == leveltime)
		return true; // already thinked this tick

	mobj->lastThink = leveltime;

	R_ResetPrecipitationMobjInterpolationState(mobj);

	P_CycleStateAnimation((mobj_t *)mobj);

	if (mobj->state == &states[S_RAINRETURN])
	{
		// Reset to ceiling!
		if (!P_SetPrecipMobjState(mobj, mobj->info->spawnstate))
			return false;
		mobj->z = (flip) ? (mobj->floorz) : (mobj->ceilingz);
		mobj->momz = FixedMul(-mobj->info->speed, mapobjectscale);
		mobj->precipflags &= ~PCF_SPLASH;
		R_ResetPrecipitationMobjInterpolationState(mobj);
	}

	if (mobj->tics != -1)
	{
		if (mobj->tics)
		{
			mobj->tics--;
		}

		if (mobj->tics == 0)
		{
			if ((mobj->precipflags & PCF_SPLASH) && (mobj->state->nextstate == S_NULL))
			{
				// HACK: sprite changes are 1 tic late, so you would see splashes on the ceiling if not for this state.
				// We need to use the settings from the previous state, since some of those are NOT 1 tic late.
				int32_t frame = (mobj->frame & ~FF_FRAMEMASK);
				if (!P_SetPrecipMobjState(mobj, S_RAINRETURN))
					return false;
				mobj->frame = frame;
				return true;
			}
			else
			{
				if (!P_SetPrecipMobjState(mobj, mobj->state->nextstate))
					return true;
			}
		}
	}

	if (mobj->precipflags & PCF_SPLASH)
		return true;

	mobj->z += mobj->momz;

	// adjust height
	if ((flip) ? (mobj->z >= mobj->ceilingz) : (mobj->z <= mobj->floorz))
	{
		if ((mobj->info->deathstate == S_NULL) || (mobj->precipflags & PCF_PIT)) // no splashes on sky or bottomless pits
		{
			mobj->z = (flip) ? (mobj->floorz) : (mobj->ceilingz);
			R_ResetPrecipitationMobjInterpolationState(mobj);
		}
		else
		{
			if (!P_SetPrecipMobjState(mobj, mobj->info->deathstate))
				return false;
			mobj->z = (flip) ? (mobj->ceilingz) : (mobj->floorz);
			mobj->precipflags |= PCF_SPLASH;
			R_ResetPrecipitationMobjInterpolationState(mobj);
		}
	}

	return true;
}

static void P_RingThinker(mobj_t *mobj)
{
	mobj_t *spark;	// Ring Fuse

	if (mobj->momx || mobj->momy)
	{
		P_RingXYMovement(mobj);

		if (P_MobjWasRemoved(mobj))
			return;
	}

	// always do the gravity bit now, that's simpler
	// BUT CheckPosition only if wasn't done before.
	if (mobj->momz)
	{
		P_RingZMovement(mobj);
		P_CheckPosition(mobj, mobj->x, mobj->y, NULL); // Need this to pick up objects!

		if (P_MobjWasRemoved(mobj))
			return;
	}

	// This thinker splits apart before the regular fuse handling so we need to handle it here instead.
	if (mobj->fuse)
	{
		mobj->fuse--;

		if (mobj->fuse < TICRATE*3)
		{
			if (leveltime & 1)
				mobj->renderflags |= RF_DONTDRAW;
			else
				mobj->renderflags &= ~RF_DONTDRAW;
		}

		if (!mobj->fuse)
		{
			if (!LUA_HookMobj(mobj, MOBJ_HOOK(MobjFuse)))
			{
				mobj->renderflags &= ~RF_DONTDRAW;
				spark = P_SpawnMobj(mobj->x, mobj->y, mobj->z, MT_SIGNSPARKLE);	// Spawn a fancy sparkle
				K_MatchGenericExtraFlags(spark, mobj);
				spark->colorized = true;
				spark->color = mobj->color ? mobj->color : SKINCOLOR_YELLOW;	// Use yellow if the ring doesn't use a skin color. (It should be red for SPB rings, but let normal rings look fancy too!)
				P_RemoveMobj(mobj);	// Adieu, monde cruel!
				return;
			}

		}

	}

	P_CycleMobjState(mobj);
}

static void P_ItemCapsulePartThinker(mobj_t *mobj)
{
	if (mobj->fuse > 0) // dead
	{
		mobj->fuse--;
		if (mobj->fuse == 0)
		{
			P_RemoveMobj(mobj);
			return;
		}
		mobj->renderflags ^= RF_DONTDRAW;
	}
	else // alive
	{
		mobj_t *target = mobj->target;
		fixed_t targetScale, z;

		if (P_MobjWasRemoved(target))
		{
			P_RemoveMobj(mobj);
			return;
		}

		// match the capsule's scale
		if (mobj->extravalue1)
			targetScale = FixedMul(mobj->extravalue1, target->scale);
		else
			targetScale = target->scale;

		if (mobj->scale != targetScale)
			P_SetScale(mobj, mobj->destscale = targetScale);

		// find z position
		if (mobj->flags2 & MF2_CLASSICPUSH) // centered items should not be flipped
			mobj->renderflags = (mobj->renderflags & ~RF_DONTDRAW) | (target->renderflags & RF_DONTDRAW);
		else
			K_GenericExtraFlagsNoZAdjust(mobj, target);

		if (mobj->eflags & MFE_VERTICALFLIP)
			z = target->z + target->height - mobj->height - FixedMul(mobj->scale, mobj->movefactor);
		else
			z = target->z + FixedMul(mobj->scale, mobj->movefactor);

		// rotate & move to capsule
		mobj->angle += mobj->movedir;
		if (mobj->flags2 & MF2_CLASSICPUSH) // centered
			P_MoveOrigin(mobj, target->x, target->y, z);
		else
			P_MoveOrigin(mobj,
				target->x + P_ReturnThrustX(mobj, mobj->angle + ANGLE_90, mobj->radius),
				target->y + P_ReturnThrustY(mobj, mobj->angle + ANGLE_90, mobj->radius),
				z);
	}
}

static void P_RefreshItemCapsuleParts(mobj_t *mobj)
{
	uint8_t numNumbers = 0;
	int32_t count = 0;
	int32_t itemType = mobj->threshold;
	mobj_t *part;
	uint32_t newRenderFlags = 0;

	if (itemType < 1 || itemType >= numkartitems)
		itemType = MAXKARTITEMS;

	part = mobj;
	while (!P_MobjWasRemoved(part->hnext))
	{
		part = part->hnext;
		part->renderflags = (part->renderflags & ~RF_BRIGHTMASK) | newRenderFlags;
	}

	// update inside item frame
	part = mobj->tracer;
	if (P_MobjWasRemoved(part))
		return;

	part->threshold = mobj->threshold;
	part->movecount = mobj->movecount;

	K_UpdateMobjItemOverlay(part, itemType, mobj->movecount);

	// update number frame
	if (mobj->movecount >= K_GetItemNumberDisplayMin(itemType, false))
		count = mobj->movecount;

	while (count > 0)
	{
		if (part->tracer == NULL || P_MobjWasRemoved(part->tracer))
		{
			P_SetTarget(&part->tracer, P_SpawnMobjFromMobj(mobj, 0, 0, 0, MT_OVERLAY));
			P_SetTarget(&part->tracer->target, part);
			P_SetMobjState(part->tracer, S_INVISIBLE);
			part->tracer->spriteyoffset = 10*FRACUNIT;
			part->tracer->spritexoffset = 13*numNumbers*FRACUNIT;
			part->tracer->threshold = OV_DONTSCREENOFFSET;
		}
		part = part->tracer;
		part->sprite = SPR_ITMN;
		part->frame = FF_FULLBRIGHT|(count % 10);
		count /= 10;
		numNumbers++;
	}

	// delete any extra overlays (I guess in case the number changes?)
	if (part->tracer)
	{
		P_RemoveMobj(part->tracer);
		P_SetTarget(&part->tracer, NULL);
	}
}

#define ROTATIONSPEED (2*ANG2)
static void P_SpawnItemCapsuleParts(mobj_t *mobj)
{
	mobj_t *part;

	// inside item
	part = P_SpawnMobjFromMobj(mobj, 0, 0, 0, MT_ITEMCAPSULE_PART);
	P_SetTarget(&part->target, mobj);
	P_SetMobjState(part, S_ITEMICON);
	part->movedir = ROTATIONSPEED; // rotation speed
	part->extravalue1 = 175*FRACUNIT/100; // relative scale
	part->flags2 |= MF2_CLASSICPUSH; // classicpush = centered horizontally
	part->flags2 &= ~MF2_OBJECTFLIP; // centered item should not be flipped
	part->eflags &= ~MFE_VERTICALFLIP;
	P_SetTarget(&mobj->tracer, part); // pointer to this item, so we can modify its sprite/frame

	P_RefreshItemCapsuleParts(mobj);
}
#undef ROTATIONSPEED

//
// P_BossTargetPlayer
// If closest is true, find the closest player.
// Returns true if a player is targeted.
//
dboolean P_BossTargetPlayer(mobj_t *actor, dboolean closest)
{
	int32_t stop = -1, c = 0;
	int32_t i, pcount = 0;
	player_t *player;
	fixed_t dist, lastdist = 0;

	// first time init, this allow minimum lastlook changes
	if (actor->lastlook < 0)
		actor->lastlook = P_RandomByte(PR_LOOKFORPLAYERS);
	actor->lastlook %= MAXPLAYERS;

	// No players? Don't bother.
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator)
			continue;

		pcount++;
	}

	if (!pcount)
		return false;

	for( ; ; actor->lastlook = (actor->lastlook+1) % MAXPLAYERS)
	{
		// save the first look so we stop next time.
		if (stop < 0)
			stop = actor->lastlook;
		// reached the beginning again, done looking.
		else if (actor->lastlook == stop)
			return (closest && lastdist > 0);

		if (!playeringame[actor->lastlook])
			continue;

		if (!closest && c++ == 2)
			return false;

		player = &players[actor->lastlook];

		if (player->spectator)
			continue; // ignore notarget

		if (!player->mo || P_MobjWasRemoved(player->mo))
			continue;

		if (player->mo->health <= 0)
			continue; //dead

		if (!P_CheckSight(actor, player->mo))
			continue; // out of sight

		if (closest)
		{
			dist = P_AproxDistance(actor->x - player->mo->x, actor->y - player->mo->y);
			if (!lastdist || dist < lastdist)
			{
				lastdist = dist+1;
				P_SetTarget(&actor->target, player->mo);
			}
			continue;
		}

		P_SetTarget(&actor->target, player->mo);
		return true;
	}
}

// Finds the player no matter what they're hiding behind (even lead!)
dboolean P_SupermanLook4Players(mobj_t *actor)
{
	int32_t c, stop = 0;
	player_t *playersinthegame[MAXPLAYERS];

	for (c = 0; c < MAXPLAYERS; c++)
	{
		if (playeringame[c] && !players[c].spectator)
		{
			if (!players[c].mo || players[c].bot)
				continue;

			if (players[c].mo->health <= 0)
				continue; // dead

			if ((gametypes[gametype]->rules & GTR_BUMPERS) && players[c].bumper <= 0)
				continue; // other dead

			playersinthegame[stop] = &players[c];
			stop++;
		}
	}

	if (!stop)
		return false;

	P_SetTarget(&actor->target, playersinthegame[P_RandomKey(PR_LOOKFORPLAYERS, stop)]->mo);
	return true;
}

// AI for a generic boss.
static void P_GenericBossThinker(mobj_t *mobj)
{
	if (mobj->state->nextstate == mobj->info->spawnstate && mobj->tics == 1)
		mobj->flags2 &= ~MF2_FRET;

	if (!mobj->target || !(mobj->target->flags & MF_SHOOTABLE))
	{
		if (mobj->health <= 0)
			return;

		// look for a new target
		if (P_BossTargetPlayer(mobj, false) && mobj->info->seesound)
			S_StartSound(mobj, mobj->info->seesound);

		return;
	}

	// Don't call A_ functions here, let the SOC do the AI!

	if (mobj->state == &states[mobj->info->meleestate]
		|| (mobj->state == &states[mobj->info->missilestate]
		&& mobj->health > mobj->info->damage))
	{
		mobj->angle = R_PointToAngle2(mobj->x, mobj->y, mobj->target->x, mobj->target->y);
	}
}

// AI for the first boss.
static void P_Boss1Thinker(mobj_t *mobj)
{
	if (mobj->flags2 & MF2_FRET && (statenum_t)(mobj->state-states) == mobj->info->spawnstate) {
		mobj->flags2 &= ~(MF2_FRET|MF2_SKULLFLY);
		mobj->momx = mobj->momy = mobj->momz = 0;
	}

	if (!mobj->tracer)
	{
		var1 = 0;
		A_BossJetFume(mobj);
	}

	if (!mobj->target || !(mobj->target->flags & MF_SHOOTABLE))
	{
		if (mobj->target && mobj->target->health
		&& mobj->target->type == MT_EGGMOBILE_TARGET) // Oh, we're just firing our laser.
			return; // It's okay, then.

		if (mobj->health <= 0)
		{
			if (P_BossTargetPlayer(mobj, false) && mobj->info->mass) // Bid farewell!
				S_StartSound(mobj, mobj->info->mass);
			return;
		}

		// look for a new target
		if (P_BossTargetPlayer(mobj, false) && mobj->info->seesound)
			S_StartSound(mobj, mobj->info->seesound);

		return;
	}

	if (mobj->state != &states[mobj->info->spawnstate] && mobj->health > 0 && mobj->flags & MF_FLOAT && !(mobj->flags2 & MF2_SKULLFLY))
		mobj->momz = FixedMul(mobj->momz,7*FRACUNIT/8);

	if (mobj->state == &states[mobj->info->meleestate]
		|| (mobj->state == &states[mobj->info->missilestate]
		&& mobj->health > mobj->info->damage))
	{
		mobj->angle = R_PointToAngle2(mobj->x, mobj->y, mobj->target->x, mobj->target->y);
	}
}

// AI for the second boss.
// No, it does NOT convert "Boss" to a "Thinker". =P
static void P_Boss2Thinker(mobj_t *mobj)
{
	if (mobj->movecount)
		mobj->movecount--;

	if (!mobj->movecount)
		mobj->flags2 &= ~MF2_FRET;

	if (!mobj->tracer)
	{
		var1 = 0;
		A_BossJetFume(mobj);
	}

	if (mobj->health <= mobj->info->damage && (!mobj->target || !(mobj->target->flags & MF_SHOOTABLE)))
	{
		if (mobj->health <= 0)
		{
			// look for a new target
			if (P_BossTargetPlayer(mobj, false) && mobj->info->mass) // Bid farewell!
				S_StartSound(mobj, mobj->info->mass);
			return;
		}

		// look for a new target
		if (P_BossTargetPlayer(mobj, false) && mobj->info->seesound)
			S_StartSound(mobj, mobj->info->seesound);

		return;
	}

	if (mobj->state == &states[mobj->info->spawnstate] && mobj->health > mobj->info->damage)
		A_Boss2Chase(mobj);
	else if (mobj->health > 0 && mobj->state != &states[mobj->info->painstate] && mobj->state != &states[mobjinfo[mobj->info->missilestate].raisestate])
	{
		mobj->flags &= ~MF_NOGRAVITY;
		A_Boss2Pogo(mobj);
		P_LinedefExecute(LE_PINCHPHASE, mobj, NULL);
	}
}

// AI for the third boss.
//
// Notes for reminders:
// movedir = move 2x fast?
// movecount = fire missiles?
// reactiontime = shock the water?
// threshold = next waypoint #
// extravalue1 = previous time shock sound was used
//
static void P_Boss3Thinker(mobj_t *mobj)
{
	if (mobj->state == &states[mobj->info->spawnstate])
		mobj->flags2 &= ~MF2_FRET;

	if (mobj->flags2 & MF2_FRET)
		mobj->movedir = 1;

	if (!mobj->tracer)
	{
		var1 = 1;
		A_BossJetFume(mobj);
	}

	if (mobj->health <= 0)
	{
		mobj->movecount = 0;
		mobj->reactiontime = 0;

		if (mobj->state < &states[mobj->info->xdeathstate])
			return;

		if (mobj->threshold == -1)
		{
			mobj->momz = mobj->info->speed;
			return;
		}
	}

	if (mobj->reactiontime) // Shock mode
	{
		uint32_t i;

		if (mobj->state != &states[mobj->info->spawnstate])
			P_SetMobjState(mobj, mobj->info->spawnstate);

		mobj->reactiontime--;
		if (!mobj->reactiontime)
		{
			ffloor_t *rover;

			// Shock the water
			for (i = 0; i < MAXPLAYERS; i++)
			{
				if (!playeringame[i] || players[i].spectator)
					continue;

				if (!players[i].mo)
					continue;

				if (players[i].mo->health <= 0)
					continue;

				if (players[i].mo->eflags & MFE_UNDERWATER)
					P_DamageMobj(players[i].mo, mobj, mobj, 1, DMG_VOLTAGE);
			}

			// Make the water flash
			for (i = 0; i < numsectors; i++)
			{
				if (!sectors[i].ffloors)
					continue;

				for (rover = sectors[i].ffloors; rover; rover = rover->next)
				{
					if (!(rover->fofflags & FOF_EXISTS))
						continue;

					if (!(rover->fofflags & FOF_SWIMMABLE))
						continue;

					P_SpawnLightningFlash(rover->master->frontsector);
					break;
				}
			}

			if ((uint32_t)mobj->extravalue1 + TICRATE*2 < leveltime)
			{
				mobj->extravalue1 = (int32_t)leveltime;
				S_StartSound(0, sfx_buzz1);
			}

			// If in the center, check to make sure
			// none of the players are in the water
			for (i = 0; i < MAXPLAYERS; i++)
			{
				if (!playeringame[i] || players[i].spectator)
					continue;

				if (!players[i].mo || players[i].bot)
					continue;

				if (players[i].mo->health <= 0)
					continue;

				if (players[i].mo->eflags & MFE_UNDERWATER)
				{ // Stay put
					mobj->reactiontime = 2*TICRATE;
					return;
				}
			}
		}

		if (!mobj->reactiontime && mobj->health <= mobj->info->damage)
		{ // Spawn pinch dummies from the center when we're leaving it.
			thinker_t *th;
			mobj_t *mo2;
			mobj_t *dummy;
			int8_t way = mobj->threshold - 1; // 0 through 4.
			int8_t way2;

			i = 0; // reset i to 0 so we can check how many clones we've removed

			// scan the thinkers to make sure all the old pinch dummies are gone before making new ones
			// this can happen if the boss was hurt earlier than expected
			for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
			{
				if (th->function == (actionf_p1)P_RemoveThinkerDelayed)
					continue;

				mo2 = (mobj_t *)th;
				if (mo2->type == (mobjtype_t)mobj->info->mass && mo2->tracer == mobj)
				{
					P_RemoveMobj(mo2);
					i++;
				}
				if (i == 2) // we've already removed 2 of these, let's stop now
					break;
			}

			way = (way + P_RandomRange(PR_BOSSBATTLE,1,3)) % 5; // dummy 1 at one of the first three options after eggmobile
			dummy = P_SpawnMobj(mobj->x, mobj->y, mobj->z, mobj->info->mass);
			dummy->angle = mobj->angle;
			dummy->threshold = way + 1;
			P_SetTarget(&dummy->tracer, mobj);

			do
				way2 = (way + P_RandomRange(PR_BOSSBATTLE,1,3)) % 5; // dummy 2 has to be careful,
			while (way2 == mobj->threshold - 1); // to make sure it doesn't try to go the Eggman Way if dummy 1 rolled high.
			dummy = P_SpawnMobj(mobj->x, mobj->y, mobj->z, mobj->info->mass);
			dummy->angle = mobj->angle;
			dummy->threshold = way2 + 1;
			P_SetTarget(&dummy->tracer, mobj);

			CONS_Debug(DBG_GAMELOGIC, "Eggman path %d - Dummy selected paths %d and %d\n", mobj->threshold, way + 1, dummy->threshold);
			P_LinedefExecute(LE_PINCHPHASE, mobj, NULL);
		}
	}
	else if (mobj->movecount) // Firing mode
	{
		uint32_t i;

		// look for a new target
		P_BossTargetPlayer(mobj, false);

		if (!mobj->target || !mobj->target->player)
			return;

		// Are there any players underwater? If so, shock them!
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i] || players[i].spectator)
				continue;

			if (!players[i].mo || players[i].bot)
				continue;

			if (players[i].mo->health <= 0)
				continue;

			if (players[i].mo->eflags & MFE_UNDERWATER)
			{
				mobj->movecount = 0;
				P_SetMobjState(mobj, mobj->info->spawnstate);
				return;
			}
		}

		// Always face your target.
		A_FaceTarget(mobj);

		// Check if the attack animation is running. If not, play it.
		if (mobj->state < &states[mobj->info->missilestate] || mobj->state > &states[mobj->info->raisestate])
		{
			if (mobj->health <= mobj->info->damage) // pinch phase
				mobj->movecount--; // limited number of shots before diving again
			if (mobj->movecount)
				P_SetMobjState(mobj, mobj->info->missilestate);
		}
	}
	else if (mobj->threshold >= 0) // Traveling mode
	{
		thinker_t *th;
		mobj_t *mo2;
		fixed_t dist, dist2;
		fixed_t speed;

		P_SetTarget(&mobj->target, NULL);

		if (mobj->state != &states[mobj->info->spawnstate] && mobj->health > 0
			&& !(mobj->flags2 & MF2_FRET))
			P_SetMobjState(mobj, mobj->info->spawnstate);

		// scan the thinkers
		// to find a point that matches
		// the number
		for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
		{
			if (th->function == (actionf_p1)P_RemoveThinkerDelayed)
				continue;

			mo2 = (mobj_t *)th;

			if (!mo2)
				continue;

			if (mo2->type != MT_BOSS3WAYPOINT)
				continue;

			if (!mo2->spawnpoint)
				continue;

			if (mo2->spawnpoint->angle != mobj->threshold)
				continue;

			P_SetTarget(&mobj->target, mo2);
			break;
		}

		if (!mobj->target) // Should NEVER happen
		{
			CONS_Debug(DBG_GAMELOGIC, "Error: Boss 3 was unable to find specified waypoint: %d\n", mobj->threshold);
			return;
		}

		dist = P_AproxDistance(P_AproxDistance(mobj->target->x - mobj->x, mobj->target->y - mobj->y), mobj->target->z - mobj->z);

		if (dist < 1)
			dist = 1;

		if ((mobj->movedir) || (mobj->health <= mobj->info->damage))
			speed = mobj->info->speed * 2;
		else
			speed = mobj->info->speed;

		mobj->momx = FixedMul(FixedDiv(mobj->target->x - mobj->x, dist), speed);
		mobj->momy = FixedMul(FixedDiv(mobj->target->y - mobj->y, dist), speed);
		mobj->momz = FixedMul(FixedDiv(mobj->target->z - mobj->z, dist), speed);

		if (mobj->momx != 0 || mobj->momy != 0)
			mobj->angle = R_PointToAngle2(0, 0, mobj->momx, mobj->momy);

		dist2 = P_AproxDistance(P_AproxDistance(mobj->target->x - (mobj->x + mobj->momx), mobj->target->y - (mobj->y + mobj->momy)), mobj->target->z - (mobj->z + mobj->momz));

		if (dist2 < 1)
			dist2 = 1;

		if ((dist >> FRACBITS) <= (dist2 >> FRACBITS))
		{
			// If further away, set XYZ of mobj to waypoint location
			P_UnsetThingPosition(mobj);
			mobj->x = mobj->target->x;
			mobj->y = mobj->target->y;
			mobj->z = mobj->target->z;
			mobj->momx = mobj->momy = mobj->momz = 0;
			P_SetThingPosition(mobj);

			if (mobj->threshold == 0)
			{
				mobj->reactiontime = 1; // Bzzt! Shock the water!
				mobj->movedir = 0;

				if (mobj->health <= 0)
				{
					mobj->flags |= MF_NOGRAVITY|MF_NOCLIP;
					mobj->flags |= MF_NOCLIPHEIGHT;
					mobj->threshold = -1;
					return;
				}
			}

			// Set to next waypoint in sequence
			if (mobj->target->spawnpoint)
			{
				// From the center point, choose one of the five paths
				if (mobj->target->spawnpoint->angle == 0)
					mobj->threshold = P_RandomRange(PR_BOSSBATTLE,1,5);
				else
					mobj->threshold = mobj->target->spawnpoint->extrainfo;

				// If the deaf flag is set, go into firing mode
				if (mobj->target->spawnpoint->options & MTF_AMBUSH)
					mobj->movecount = mobj->health+1;
			}
			else // This should never happen, as well
				CONS_Debug(DBG_GAMELOGIC, "Error: Boss 3 waypoint has no spawnpoint associated with it.\n");
		}
	}
}

// Move Boss4's sectors by delta.
static dboolean P_Boss4MoveCage(mobj_t *mobj, fixed_t delta)
{
	int32_t snum;
	sector_t *sector;
	dboolean gotcage = false;

	if (!mobj->spawnpoint)
		return false;

	TAG_ITER_SECTORS(mobj->spawnpoint->args[4], snum)
	{
		sector = &sectors[snum];
		sector->floorheight += delta;
		sector->ceilingheight += delta;
		P_CheckSector(sector, true);
		gotcage = true;
	}
	return gotcage;
}

// Move Boss4's arms to angle
static void P_Boss4MoveSpikeballs(mobj_t *mobj, angle_t angle, fixed_t fz)
{
	int32_t s;
	mobj_t *base = mobj, *seg;
	fixed_t dist, bz = mobj->watertop+(16<<FRACBITS);
	while ((base = base->tracer))
	{
		for (seg = base, dist = 172*FRACUNIT, s = 9; seg; seg = seg->hnext, dist += 124*FRACUNIT, --s)
			P_MoveOrigin(seg, mobj->x + P_ReturnThrustX(mobj, angle, dist), mobj->y + P_ReturnThrustY(mobj, angle, dist), bz + FixedMul(fz, FixedDiv(s<<FRACBITS, 9<<FRACBITS)));
		angle += ANGLE_MAX/3;
	}
}

// Pull them closer.
static void P_Boss4PinchSpikeballs(mobj_t *mobj, angle_t angle, fixed_t fz)
{
	int32_t s;
	mobj_t *base = mobj, *seg;
	fixed_t dist, bz = mobj->watertop+(16<<FRACBITS);
	while ((base = base->tracer))
	{
		for (seg = base, dist = 112*FRACUNIT, s = 9; seg; seg = seg->hnext, dist += 132*FRACUNIT, --s)
		{
			seg->z = bz + FixedMul(fz, FixedDiv(s<<FRACBITS, 9<<FRACBITS));
			P_TryMove(seg, mobj->x + P_ReturnThrustX(mobj, angle, dist), mobj->y + P_ReturnThrustY(mobj, angle, dist), true, NULL);
		}
		angle += ANGLE_MAX/3;
	}
}

// Destroy cage FOFs.
static void P_Boss4DestroyCage(mobj_t *mobj)
{
	int32_t snum;
	size_t a;
	sector_t *sector, *rsec;
	ffloor_t *rover;

	if (!mobj->spawnpoint)
		return;

	TAG_ITER_SECTORS(mobj->spawnpoint->args[4], snum)
	{
		sector = &sectors[snum];

		// Destroy the FOFs.
		for (a = 0; a < sector->numattached; a++)
		{
			rsec = &sectors[sector->attached[a]];
			for (rover = rsec->ffloors; rover; rover = rover->next)
				if (rover->fofflags & FOF_EXISTS && rover->secnum == (size_t)snum)
				{
					if (rover->fofflags & FOF_RENDERALL) // checking for FF_RENDERANY.
						EV_CrumbleChain(rsec, rover); // This FOF is visible to some extent? Crumble it.
					else // Completely invisible FOF
					{
						// no longer exists (can't collide with again)
						rover->fofflags &= ~FOF_EXISTS;
						sector->moved = true;
						rsec->moved = true;
					}
				}
		}
	}
}

// Destroy Boss4's arms
static void P_Boss4PopSpikeballs(mobj_t *mobj)
{
	mobj_t *base = mobj->tracer, *seg, *next;
	P_SetTarget(&mobj->tracer, NULL);
	while(base)
	{
		next = base->tracer;
		P_SetTarget(&base->tracer, NULL);
		for (seg = base; seg; seg = seg->hnext)
		{
			if (seg->health)
				P_KillMobj(seg, NULL, NULL, DMG_INSTAKILL);
		}
		base = next;
	}
}

//
// AI for the fourth boss.
//
static void P_Boss4Thinker(mobj_t *mobj)
{
	if ((statenum_t)(mobj->state-states) == mobj->info->spawnstate)
	{
		if (mobj->health > mobj->info->damage || mobj->movedir == 4)
			mobj->flags2 &= ~MF2_FRET;
		mobj->reactiontime = 0; // Drop the cage immediately.
	}

	// Oh no, we dead? D:
	if (!mobj->health)
	{
		if (mobj->tracer) // need to clean up!
		{
			P_Boss4DestroyCage(mobj); // Just in case pinch phase was skipped.
			P_Boss4PopSpikeballs(mobj);
		}
		return;
	}

	// movedir == battle stage:
	//   0: initialization
	//   1: phase 1 forward
	//   2: phase 1 reverse
	//   3: pinch rise
	//   4: pinch phase
	switch(mobj->movedir)
	{
	// WELCOME to your DOOM!
	case 0:
	{
		// For this stage only:
		// movecount == cage height
		// threshold == cage momz
		if (mobj->movecount == 0) // Initialize stage!
		{
			fixed_t z;
			int32_t i, arm;
			mobj_t *seg, *base = mobj;
			// First frame init, spawn all the things.
			mobj->watertop = mobj->z;
			z = mobj->z + mobj->height/2 - mobjinfo[MT_EGGMOBILE4_MACE].height/2;
			for (arm = 0; arm <3 ; arm++)
			{
				seg = P_SpawnMobj(mobj->x, mobj->y, z, MT_EGGMOBILE4_MACE);
				P_SetTarget(&base->tracer, seg);
				base = seg;
				P_SetTarget(&seg->target, mobj);
				for (i = 0; i < 9; i++)
				{
					P_SetTarget(&seg->hnext, P_SpawnMobj(mobj->x, mobj->y, z, MT_EGGMOBILE4_MACE));
					P_SetTarget(&seg->hnext->hprev, seg);
					seg = seg->hnext;
				}
			}
			// Move the cage up to the sky.
			mobj->movecount = 800*FRACUNIT;
			if (!P_Boss4MoveCage(mobj, mobj->movecount))
			{
				mobj->movecount = 0;
				mobj->threshold = 3*TICRATE;
				mobj->extravalue1 = 1;
				mobj->movedir++; // We don't have a cage, just continue.
			}
			else
				P_Boss4MoveSpikeballs(mobj, 0, mobj->movecount);
		}
		else // Cage slams down over Eggman's head!
		{
			fixed_t oldz = mobj->movecount;
			mobj->threshold -= 5*FRACUNIT;
			mobj->movecount += mobj->threshold;
			if (mobj->movecount < 0)
				mobj->movecount = 0;
			P_Boss4MoveCage(mobj, mobj->movecount - oldz);
			P_Boss4MoveSpikeballs(mobj, 0, mobj->movecount);
			if (mobj->movecount == 0)
			{
				mobj->threshold = 3*TICRATE;
				mobj->extravalue1 = 1;
				P_LinedefExecute(LE_BOSS4DROP, mobj, NULL);
				mobj->movedir++; // Initialization complete, next phase!
			}
		}
		return;
	}

	// Normal operation
	case 1:
	case 2:
		break;

	// Pinch phase init!
	case 3:
	{
		fixed_t z;
		if (mobj->z < mobj->watertop+(512<<FRACBITS))
			mobj->momz = 8*FRACUNIT;
		else
		{
			mobj->momz = 0;
			mobj->movedir++;
		}
		mobj->movecount += 400<<(FRACBITS>>1);
		mobj->movecount %= 360*FRACUNIT;
		z = mobj->z - mobj->watertop - mobjinfo[MT_EGGMOBILE4_MACE].height - mobj->height/2;
		if (z < 0) // We haven't risen high enough to pull the spikeballs along yet
			P_Boss4MoveSpikeballs(mobj, FixedAngle(mobj->movecount), 0); // So don't pull the spikeballs along yet.
		else
			P_Boss4PinchSpikeballs(mobj, FixedAngle(mobj->movecount), z);
		return;
	}
	// Pinch phase!
	case 4:
	{
		if (mobj->z < (mobj->watertop + ((512+128*(mobj->info->damage-mobj->health))<<FRACBITS)))
			mobj->momz = 8*FRACUNIT;
		else
			mobj->momz = 0;
		mobj->movecount += (800+800*(mobj->info->damage-mobj->health))<<(FRACBITS>>1);
		mobj->movecount %= 360*FRACUNIT;
		P_Boss4PinchSpikeballs(mobj, FixedAngle(mobj->movecount), mobj->z - mobj->watertop - mobjinfo[MT_EGGMOBILE4_MACE].height - mobj->height/2);

		if (!mobj->target || !mobj->target->health)
			P_SupermanLook4Players(mobj);
		A_FaceTarget(mobj);
		return;
	}

	default: // ?????
		return;
	}

	// Haahahahahaaa, and let the FUN.. BEGIN!
	// movedir   == arms direction
	// movecount == arms angle
	// threshold == countdown to next attack
	// reactiontime == cage raise, speed burst
	// movefactor == cage z
	// friction == turns until helm lift

	// Raise the cage!
	if (mobj->reactiontime == 1)
	{
		fixed_t oldz = mobj->movefactor;
		mobj->movefactor += 8*FRACUNIT;
		if (mobj->movefactor > 128*FRACUNIT)
			mobj->movefactor = 128*FRACUNIT;
		P_Boss4MoveCage(mobj, mobj->movefactor - oldz);
	}
	// Drop the cage!
	else if (mobj->movefactor)
	{
		fixed_t oldz = mobj->movefactor;
		mobj->movefactor -= 4*FRACUNIT;
		if (mobj->movefactor < 0)
			mobj->movefactor = 0;
		P_Boss4MoveCage(mobj, mobj->movefactor - oldz);
		if (!mobj->movefactor)
		{
			if (mobj->health <= mobj->info->damage)
			{ // Proceed to pinch phase!
				P_Boss4DestroyCage(mobj);
				mobj->movedir = 3;
				P_LinedefExecute(LE_PINCHPHASE, mobj, NULL);
				return;
			}
			P_LinedefExecute(LE_BOSS4DROP, mobj, NULL);
		}
	}

	{
		fixed_t movespeed = 170<<(FRACBITS>>1);
		if (mobj->reactiontime == 2)
			movespeed *= 3;
		if (mobj->movedir == 2)
			mobj->movecount -= movespeed;
		else
			mobj->movecount += movespeed;
	}
	mobj->movecount %= 360*FRACUNIT;
	P_Boss4MoveSpikeballs(mobj, FixedAngle(mobj->movecount), mobj->movefactor);

	// Check for attacks, always tick the timer even while animating!!
	if (!(mobj->flags2 & MF2_FRET) // but pause for pain so we don't interrupt pinch phase, eep!
	&& mobj->threshold-- == 0)
	{
		// 5 -> 2.5 second timer
		mobj->threshold = 5*TICRATE-(TICRATE/2)*(mobj->info->spawnhealth-mobj->health);
		if (mobj->threshold < 1)
			mobj->threshold = 1;

		if (mobj->extravalue1-- == 0)
		{
			P_SetMobjState(mobj, mobj->info->raisestate);
			mobj->extravalue1 = 3;
		}
		else
		{
			if (mobj->reactiontime == 1) // Cage is raised?
				mobj->reactiontime = 0; // Drop it!
			switch(P_RandomKey(PR_BOSSBATTLE, 10))
			{
				// Telegraph Right (Speed Up!!)
				case 1:
				case 3:
				case 4:
				case 5:
				case 6:
					P_SetMobjState(mobj, mobj->info->missilestate);
					break;
				// Telegraph Left (Reverse Direction)
				default:
					P_SetMobjState(mobj, mobj->info->meleestate);
					break;
			}
		}
	}

	// Leave if animating.
	if ((statenum_t)(mobj->state-states) != mobj->info->spawnstate)
		return;

	// Map allows us to get killed despite cage being down?
	if (mobj->health <= mobj->info->damage)
	{ // Proceed to pinch phase!
		P_Boss4DestroyCage(mobj);
		// spawn jet's flame now you're flying upwards
		// tracer is already used, so if this ever gets reached again we've got problems
		var1 = 3;
		A_BossJetFume(mobj);
		mobj->movedir = 3;
		P_LinedefExecute(LE_PINCHPHASE, mobj, NULL);
		return;
	}

	mobj->reactiontime = 0; // Drop the cage if it hasn't been dropped already.
	if (!mobj->target || !mobj->target->health)
		P_SupermanLook4Players(mobj);
	A_FaceTarget(mobj);
}

//
// AI for Black Eggman
// Note: You CANNOT have more than ONE Black Eggman
// in a level! Just don't try it!
//
static void P_Boss7Thinker(mobj_t *mobj)
{
	if (!mobj->target || !(mobj->target->flags & MF_SHOOTABLE))
	{
		// look for a new target
		if (P_BossTargetPlayer(mobj, false))
			return; // got a new target

		P_SetMobjStateNF(mobj, mobj->info->spawnstate);
		return;
	}

	if (mobj->health >= mobj->info->spawnhealth && (leveltime & 14) == 0)
	{
		mobj_t *smoke = P_SpawnMobj(mobj->x, mobj->y, mobj->z + mobj->height, MT_SMOKE);
		smoke->destscale = mobj->destscale;
		P_SetScale(smoke, smoke->destscale);
		smoke->momz = FixedMul(FRACUNIT, smoke->scale);
	}

	if (mobj->state == &states[S_BLACKEGG_STND] && mobj->tics == mobj->state->tics)
	{
		mobj->reactiontime += P_RandomByte(PR_BRAKEGGMAN);

		if (mobj->health <= mobj->info->damage)
			mobj->reactiontime /= 4;
	}
	else if (mobj->state == &states[S_BLACKEGG_DIE4] && mobj->tics == mobj->state->tics)
		A_BossDeath(mobj);
	else if (mobj->state >= &states[S_BLACKEGG_WALK1]
		&& mobj->state <= &states[S_BLACKEGG_WALK6])
		A_Boss7Chase(mobj);
	else if (mobj->state == &states[S_BLACKEGG_PAIN1] && mobj->tics == mobj->state->tics)
	{
		if (mobj->health > 0)
			mobj->health--;

		S_StartSound(0, (mobj->health) ? sfx_behurt : sfx_bedie2);

		mobj->reactiontime /= 3;

		if (mobj->health <= 0)
		{
			int32_t i;

			P_KillMobj(mobj, NULL, NULL, DMG_INSTAKILL);

			// It was a team effort
			for (i = 0; i < MAXPLAYERS; i++)
			{
				if (!playeringame[i])
					continue;

				P_AddPlayerScore(&players[i], 1000);
			}
		}
	}
	else if (mobj->state == &states[S_BLACKEGG_PAIN35] && mobj->tics == 1)
	{
		if (mobj->health == mobj->info->damage)
		{
			// Begin platform destruction
			mobj->flags2 |= MF2_FRET;
			P_SetMobjState(mobj, mobj->info->raisestate);
			P_LinedefExecute(LE_PINCHPHASE, mobj, NULL);
		}
	}
	else if (mobj->state == &states[S_BLACKEGG_HITFACE4] && mobj->tics == mobj->state->tics)
	{
		// This is where Black Eggman hits his face.
		// If a player is on top of him, the player gets hurt.
		// But, if the player has managed to escape,
		// Black Eggman gets hurt!
		int32_t i;
		mobj->state->nextstate = mobj->info->painstate; // Reset

		S_StartSound(0, sfx_bedeen);

		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i] || players[i].spectator)
				continue;

			if (!players[i].mo)
				continue;

			if (players[i].mo->health <= 0)
				continue;

			if (P_AproxDistance(players[i].mo->x - mobj->x, players[i].mo->y - mobj->y) > (mobj->radius + players[i].mo->radius))
				continue;

			if (players[i].mo->z > mobj->z + mobj->height - FRACUNIT
				&& players[i].mo->z < mobj->z + mobj->height + 128*FRACUNIT) // You can't be in the vicinity, either...
			{
				// Punch him!
				P_DamageMobj(players[i].mo, mobj, mobj, 1, DMG_NORMAL);
				mobj->state->nextstate = mobj->info->spawnstate;

				// Laugh
				S_StartSound(0, sfx_bewar1 + P_RandomKey(PR_BRAKEGGMAN, 4));
			}
		}
	}
	else if (mobj->state == &states[S_BLACKEGG_GOOP])
	{
		// Lob cannon balls
		if (mobj->movecount-- <= 0 || !mobj->target)
		{
			P_SetMobjState(mobj, mobj->info->spawnstate);
			return;
		}

		if ((leveltime & 15) == 0)
		{
			var1 = MT_CANNONBALL;

			var2 = 2*TICRATE + (80<<16);

			A_LobShot(mobj);
			S_StartSound(0, sfx_begoop);
		}
	}
	else if (mobj->state == &states[S_BLACKEGG_SHOOT2])
	{
		// Chaingun goop
		mobj_t *missile;

		if (mobj->movecount-- <= 0 || !mobj->target)
		{
			P_SetMobjState(mobj, mobj->info->spawnstate);
			return;
		}

		A_FaceTarget(mobj);

		missile = P_SpawnXYZMissile(mobj, mobj->target, MT_BLACKEGGMAN_GOOPFIRE,
			mobj->x + P_ReturnThrustX(mobj, mobj->angle-ANGLE_90, FixedDiv(mobj->radius, 3*FRACUNIT/2)+(4*FRACUNIT)),
			mobj->y + P_ReturnThrustY(mobj, mobj->angle-ANGLE_90, FixedDiv(mobj->radius, 3*FRACUNIT/2)+(4*FRACUNIT)),
			mobj->z + FixedDiv(mobj->height, 3*FRACUNIT/2));

		S_StopSound(missile);

		if (leveltime & 1)
			S_StartSound(0, sfx_beshot);
	}
	else if (mobj->state == &states[S_BLACKEGG_JUMP1] && mobj->tics == 1)
	{
		mobj_t *hitspot = NULL, *mo2;
		angle_t an;
		fixed_t dist, closestdist;
		fixed_t vertical, horizontal;
		fixed_t airtime = 5*TICRATE;
		int32_t waypointNum = 0;
		thinker_t *th;
		int32_t i;
		dboolean foundgoop = false;
		int32_t closestNum;

		// Looks for players in goop. If you find one, try to jump on him.
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i] || players[i].spectator)
				continue;

			if (!players[i].mo)
				continue;

			if (players[i].mo->health <= 0)
				continue;

			if (players[i].powers[pw_ingoop])
			{
				closestNum = -1;
				closestdist = 16384*FRACUNIT; // Just in case...

				// Find waypoint he is closest to
				for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
				{
					if (th->function == (actionf_p1)P_RemoveThinkerDelayed)
						continue;

					mo2 = (mobj_t *)th;
					if (mo2->type == MT_BOSS3WAYPOINT && mo2->spawnpoint)
					{
						dist = P_AproxDistance(players[i].mo->x - mo2->x, players[i].mo->y - mo2->y);

						if (closestNum == -1 || dist < closestdist)
						{
							closestNum = (mo2->spawnpoint->options & 7);
							closestdist = dist;
							foundgoop = true;
						}
					}
				}
				waypointNum = closestNum;
				break;
			}
		}

		if (!foundgoop)
		{
			if (mobj->z > 1056*FRACUNIT)
				waypointNum = 0;
			else
				waypointNum = 1 + P_RandomKey(PR_BRAKEGGMAN, 4);
		}

		// Don't jump to the center when health is low.
		// Force the player to beat you with missiles.
		if (mobj->health <= mobj->info->damage && waypointNum == 0)
			waypointNum = 1 + P_RandomKey(PR_BRAKEGGMAN, 4);

		if (mobj->tracer && mobj->tracer->type == MT_BOSS3WAYPOINT
			&& mobj->tracer->spawnpoint && (mobj->tracer->spawnpoint->options & 7) == waypointNum)
		{
			if (P_RandomChance(PR_BRAKEGGMAN, FRACUNIT/2))
				waypointNum++;
			else
				waypointNum--;

			waypointNum %= 5;

			if (waypointNum < 0)
				waypointNum = 0;
		}

		if (waypointNum == 0 && mobj->health <= mobj->info->damage)
			waypointNum = 1 + (P_RandomFixed(PR_BRAKEGGMAN) & 1);

		// scan the thinkers to find
		// the waypoint to use
		for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
		{
			if (th->function == (actionf_p1)P_RemoveThinkerDelayed)
				continue;

			mo2 = (mobj_t *)th;
			if (mo2->type == MT_BOSS3WAYPOINT && mo2->spawnpoint && (mo2->spawnpoint->options & 7) == waypointNum)
			{
				hitspot = mo2;
				break;
			}
		}

		if (hitspot == NULL)
		{
			CONS_Debug(DBG_GAMELOGIC, "BlackEggman unable to find waypoint #%d!\n", waypointNum);
			P_SetMobjState(mobj, mobj->info->spawnstate);
			return;
		}

		P_SetTarget(&mobj->tracer, hitspot);

		mobj->angle = R_PointToAngle2(mobj->x, mobj->y, hitspot->x, hitspot->y);

		an = mobj->angle;
		an >>= ANGLETOFINESHIFT;

		dist = P_AproxDistance(hitspot->x - mobj->x, hitspot->y - mobj->y);

		horizontal = dist / airtime;
		vertical = (gravity*airtime)/2;

		mobj->momx = FixedMul(horizontal, FINECOSINE(an));
		mobj->momy = FixedMul(horizontal, FINESINE(an));
		mobj->momz = vertical;

//		mobj->momz = 10*FRACUNIT;
	}
	else if (mobj->state == &states[S_BLACKEGG_JUMP2] && mobj->z <= mobj->floorz)
	{
		// BANG! onto the ground
		int32_t i,j;
		fixed_t ns;
		fixed_t x,y,z;
		mobj_t *mo2;

		S_StartSound(0, sfx_befall);

		z = mobj->floorz;
		for (j = 0; j < 2; j++)
		{
			for (i = 0; i < 32; i++)
			{
				const angle_t fa = (i*FINEANGLES/16) & FINEMASK;
				ns = 64 * FRACUNIT;
				x = mobj->x + FixedMul(FINESINE(fa),ns);
				y = mobj->y + FixedMul(FINECOSINE(fa),ns);

				mo2 = P_SpawnMobj(x, y, z, MT_EXPLODE);
				ns = 16 * FRACUNIT;
				mo2->momx = FixedMul(FINESINE(fa),ns);
				mo2->momy = FixedMul(FINECOSINE(fa),ns);
			}
			z -= 32*FRACUNIT;
		}

		// Hurt player??
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i] || players[i].spectator)
				continue;

			if (!players[i].mo)
				continue;

			if (players[i].mo->health <= 0)
				continue;

			if (P_AproxDistance(players[i].mo->x - mobj->x, players[i].mo->y - mobj->y) > mobj->radius*4)
				continue;

			if (players[i].mo->z > mobj->z + 128*FRACUNIT)
				continue;

			if (players[i].mo->z < mobj->z - 64*FRACUNIT)
				continue;

			P_DamageMobj(players[i].mo, mobj, mobj, 1, DMG_NORMAL);

			// Laugh
			S_StartSound(0, sfx_bewar1 + P_RandomKey(PR_BRAKEGGMAN, 4));
		}

		P_SetMobjState(mobj, mobj->info->spawnstate);
	}
	else if (mobj->state == &states[mobj->info->deathstate] && mobj->tics == mobj->state->tics)
		S_StartSound(0, sfx_bedie1 + (P_RandomFixed(PR_BRAKEGGMAN) & 1));

}

// Metal Sonic battle boss
// You CAN put multiple Metal Sonics in a single map
// because I am a totally competent programmer who can do shit right.
static void P_Boss9Thinker(mobj_t *mobj)
{
	TryMoveResult_t result = {0};

	if ((statenum_t)(mobj->state-states) == mobj->info->spawnstate)
		mobj->flags2 &= ~MF2_FRET;

	if (!mobj->tracer)
	{
		thinker_t *th;
		mobj_t *mo2;
		mobj_t *last=NULL;

		// Initialize the boss, spawn jet fumes, etc.
		mobj->threshold = 0;
		mobj->reactiontime = 0;
		mobj->watertop = mobj->floorz + 32*FRACUNIT;
		var1 = 2;
		A_BossJetFume(mobj);

		// Run through the thinkers ONCE and find all of the MT_BOSS9GATHERPOINT in the map.
		// Build a hoop linked list of 'em!
		for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
		{
			if (th->function == (actionf_p1)P_RemoveThinkerDelayed)
				continue;

			mo2 = (mobj_t *)th;
			if (mo2->type == MT_BOSS9GATHERPOINT)
			{
				if (last)
					P_SetTarget(&last->hnext, mo2);
				else
					P_SetTarget(&mobj->hnext, mo2);
				P_SetTarget(&mo2->hprev, last);
				last = mo2;
			}
		}
	}

	if (mobj->health <= 0)
		return;

	if ((!mobj->target || !(mobj->target->flags & MF_SHOOTABLE)))
	{
		P_BossTargetPlayer(mobj, false);
		if (mobj->target && !P_IsObjectOnGround(mobj->target))
			P_SetTarget(&mobj->target, NULL); // Wait for them to hit the ground first
		if (!mobj->target) // Still no target, aww.
		{
			// Reset the boss.
			P_SetMobjState(mobj, mobj->info->spawnstate);
			mobj->fuse = 0;
			mobj->momx = FixedDiv(mobj->momx, FRACUNIT + (FRACUNIT>>2));
			mobj->momy = FixedDiv(mobj->momy, FRACUNIT + (FRACUNIT>>2));
			mobj->momz = FixedDiv(mobj->momz, FRACUNIT + (FRACUNIT>>2));
			return;
		}
		else if (!mobj->fuse)
			mobj->fuse = 10*TICRATE;
	}

	// AI goes here.
	{
		dboolean danger = true;
		angle_t angle;
		if (mobj->threshold)
			mobj->momz = (mobj->watertop-mobj->z)/16; // Float to your desired position FASTER
		else
			mobj->momz = (mobj->watertop-mobj->z)/40; // Float to your desired position

		if (mobj->movecount == 2)
		{
			mobj_t *spawner;
			fixed_t dist = 0;
			angle = ANGLE_135*leveltime;

			// Alter your energy bubble's size/position
			if (mobj->health > 3)
			{
				mobj->tracer->destscale = FRACUNIT + (4*TICRATE - mobj->fuse)*(FRACUNIT/2)/TICRATE + FixedMul(FINECOSINE(angle>>ANGLETOFINESHIFT),FRACUNIT/2);
				P_SetScale(mobj->tracer, mobj->tracer->destscale);
				P_SetOrigin(mobj->tracer, mobj->x, mobj->y, mobj->z + mobj->height/2 - mobj->tracer->height/2);
				mobj->tracer->momx = mobj->momx;
				mobj->tracer->momy = mobj->momy;
				mobj->tracer->momz = mobj->momz;
			}

			// Face your target
			P_BossTargetPlayer(mobj, true);
			angle = R_PointToAngle2(mobj->x, mobj->y, mobj->target->x, mobj->target->y); // absolute angle
			angle = (angle-mobj->angle); // relative angle
			if (angle < ANGLE_180)
				mobj->angle += angle/8;
			else
				mobj->angle -= InvAngle(angle)/8;

			// Spawn energy particles
			for (spawner = mobj->hnext; spawner; spawner = spawner->hnext)
			{
				dist = P_AproxDistance(spawner->x - mobj->x, spawner->y - mobj->y);
				if (P_RandomRange(PR_BOSSBATTLE, 1,(dist>>FRACBITS)/16) == 1)
					break;
			}
			if (spawner && dist)
			{
				mobj_t *missile = P_SpawnMissile(spawner, mobj, MT_MSGATHER);
				missile->momz = FixedDiv(missile->momz, 7*FRACUNIT/4);
				missile->fuse = (dist/P_AproxDistance(missile->momx, missile->momy));
				if (missile->fuse > mobj->fuse)
					P_RemoveMobj(missile);
			}
		}

		// Pre-threshold reactiontime stuff for attack phases
		if (mobj->reactiontime && mobj->movecount == 3)
		{
			if (mobj->movedir == 0 || mobj->movedir == 2) // Pausing between bounces in the pinball phase
			{
				if (mobj->target->player->powers[pw_tailsfly]) // Trying to escape, eh?
					mobj->watertop = mobj->target->z + mobj->target->momz*6; // Readjust your aim. >:3
				else
					mobj->watertop = mobj->target->floorz + 16*FRACUNIT;
				if (!(mobj->threshold%4))
					mobj->angle = R_PointToAngle2(mobj->x, mobj->y, mobj->target->x + mobj->target->momx*4, mobj->target->y + mobj->target->momy*4);
			}
			// Pausing between energy ball shots
			mobj->reactiontime--;
			return;
		}

		// threshold is used for attacks/maneuvers.
		if (mobj->threshold)
		{
			fixed_t speed = 20*FRACUNIT + FixedMul(40*FRACUNIT, FixedDiv((mobj->info->spawnhealth - mobj->health)<<FRACBITS, mobj->info->spawnhealth<<FRACBITS));
			int tries = 0;

			// Firin' mah lazors
			if (mobj->movecount == 3 && mobj->movedir == 1)
			{
				if (!(mobj->threshold&1))
				{
					mobj_t *missile;
					if (mobj->info->seesound)
						S_StartSound(mobj, mobj->info->seesound);
					P_SetMobjState(mobj, mobj->info->missilestate);
					if (mobj->extravalue1 == 3)
						mobj->reactiontime = TICRATE/16;
					else
						mobj->reactiontime = TICRATE/8;

					A_FaceTarget(mobj);
					missile = P_SpawnMissile(mobj, mobj->target, mobj->info->speed);
					if (mobj->extravalue1 == 2 || mobj->extravalue1 == 3)
					{
						missile->destscale = FRACUNIT>>1;
						P_SetScale(missile, missile->destscale);
					}
					missile->fuse = 3*TICRATE;
					missile->z -= missile->height/2;

					if (mobj->extravalue1 == 2)
					{
						int i;
						mobj_t *spread;
						missile->flags |= MF_MISSILE;
						for (i = 0; i < 5; i++)
						{
							if (i == 2)
								continue;
							spread = P_SpawnMobj(missile->x, missile->y, missile->z, missile->type);
							spread->angle = missile->angle+(ANGLE_11hh/2)*(i-2);
							P_InstaThrust(spread,spread->angle,spread->info->speed);
							spread->momz = missile->momz;
							spread->destscale = FRACUNIT>>1;
							P_SetScale(spread, spread->destscale);
							spread->fuse = 3*TICRATE;
						}
						missile->flags &= ~MF_MISSILE;
					}
				}
				else
				{
					P_SetMobjState(mobj, mobj->state->nextstate);
					if (mobj->extravalue1 == 3)
						mobj->reactiontime = TICRATE/8;
					else
						mobj->reactiontime = TICRATE/4;
				}
				mobj->threshold--;
				return;
			}

			P_SpawnGhostMobj(mobj);

			// Pinball attack!
			if (mobj->movecount == 3 && (mobj->movedir == 0 || mobj->movedir == 2))
			{
				if ((statenum_t)(mobj->state-states) != mobj->info->seestate)
					P_SetMobjState(mobj, mobj->info->seestate);
				if (mobj->movedir == 0) // mobj health == 1
					P_InstaThrust(mobj, mobj->angle, 38*FRACUNIT);
				else if (mobj->health == 3)
					P_InstaThrust(mobj, mobj->angle, 22*FRACUNIT);
				else // mobj health == 2
					P_InstaThrust(mobj, mobj->angle, 30*FRACUNIT);

				if (!P_TryMove(mobj, mobj->x+mobj->momx, mobj->y+mobj->momy, true, &result)) // Hit a wall? Find a direction to bounce
				{
					mobj->threshold--;
					if (mobj->threshold)
					{
						P_SetMobjState(mobj, mobj->state->nextstate);
						if (mobj->info->mass)
							S_StartSound(mobj, mobj->info->mass);
						if (!(mobj->threshold%4)) // We've decided to lock onto the player this bounce.
						{
							mobj->angle = R_PointToAngle2(mobj->x, mobj->y, mobj->target->x + mobj->target->momx*4, mobj->target->y + mobj->target->momy*4);
							mobj->reactiontime = TICRATE; // targetting time
						}
						else // No homing, just use P_BounceMove
						{
							P_BounceMove(mobj, &result);
							mobj->angle = R_PointToAngle2(0,0,mobj->momx,mobj->momy);
							mobj->reactiontime = TICRATE/4; // just a pause before you bounce away
						}
						mobj->momx = mobj->momy = 0;
					}
				}
				return;
			}

			// Vector form dodge!
			mobj->angle += mobj->movedir;
			P_InstaThrust(mobj, mobj->angle, -speed);
			while (!P_TryMove(mobj, mobj->x+mobj->momx, mobj->y+mobj->momy, true, &result) && tries++ < 16)
			{
				mobj->angle += mobj->movedir;
				P_InstaThrust(mobj, mobj->angle, -speed);
			}
			mobj->momx = mobj->momy = 0;
			mobj->threshold--;
			if (!mobj->threshold)
			{ // Go into stun after dodge.
				// from 3*TICRATE down to 1.25*TICRATE
				//mobj->reactiontime = 5*TICRATE/4 + (FixedMul((7*TICRATE/4)<<FRACBITS, FixedDiv((mobj->health-1)<<FRACBITS, (mobj->info->spawnhealth-1)<<FRACBITS))>>FRACBITS);
				// from 3*TICRATE down to 2*TICRATE
				mobj->reactiontime = 2*TICRATE + (FixedMul((1*TICRATE)<<FRACBITS, FixedDiv((mobj->health-1)<<FRACBITS, (mobj->info->spawnhealth-1)<<FRACBITS))>>FRACBITS);
				mobj->flags |= MF_SPECIAL|MF_SHOOTABLE;
				P_SetMobjState(mobj, mobj->state->nextstate);
			}
			return;
		}

		angle = ANGLE_135*leveltime;
		mobj->momz += FixedMul(FINECOSINE(angle>>ANGLETOFINESHIFT),2*FRACUNIT); // Use that "angle" to bob gently in the air
		// This is below threshold because we don't want to bob while zipping around

		// Ohh you're in for it now..
		if (mobj->flags2 & MF2_FRET && mobj->health <= mobj->info->damage)
			mobj->fuse = 0;

		// reactiontime is used for delays.
		if (mobj->reactiontime)
		{
			// Stunned after vector form
			if (mobj->movedir > ANGLE_180)
				mobj->angle -= FixedAngle(FixedMul(AngleFixed(InvAngle(mobj->movedir)),FixedDiv(mobj->reactiontime<<FRACBITS,24<<FRACBITS)));
			else
				mobj->angle += FixedAngle(FixedMul(AngleFixed(mobj->movedir),FixedDiv(mobj->reactiontime<<FRACBITS,24<<FRACBITS)));
			mobj->reactiontime--;
			if (!mobj->reactiontime)
				// Out of stun.
				P_SetMobjState(mobj, mobj->state->nextstate);
			return;
		}

		// Not stunned? Can hit.
		// Here because stun won't always get the chance to complete due to pinch phase activating, being hit, etc.
		mobj->flags &= ~(MF_SPECIAL|MF_SHOOTABLE);

		if (mobj->health <= mobj->info->damage && mobj->fuse && !(mobj->fuse%TICRATE))
		{
			var1 = 1;
			var2 = 0;
			A_BossScream(mobj);
		}

		// Don't move if we're still in pain!
		if (mobj->flags2 & MF2_FRET)
			return;

		if (mobj->state == &states[mobj->info->raisestate]) // Charging energy
		{
			if (mobj->momx != 0 || mobj->momy != 0) // Apply the air breaks
			{
				if (abs(mobj->momx)+abs(mobj->momy) < FRACUNIT)
					mobj->momx = mobj->momy = 0;
				else
					P_Thrust(mobj, R_PointToAngle2(0, 0, mobj->momx, mobj->momy), -6*FRACUNIT/8);
			}
			return;
		}

		if (mobj->fuse == 0)
		{
			// It's time to attack! What are we gonna do?!
			switch(mobj->movecount)
			{
			case 0:
			default:
				// Fly up and prepare for an attack!
				// We have to charge up first, so let's go up into the air
				P_SetMobjState(mobj, mobj->info->raisestate);
				if (mobj->floorz >= mobj->target->floorz)
					mobj->watertop = mobj->floorz + 256*FRACUNIT;
				else
					mobj->watertop = mobj->target->floorz + 256*FRACUNIT;
				break;

			case 1:
			{
				// Okay, we're up? Good, time to gather energy...
				if (mobj->health > mobj->info->damage)
				{ // No more bubble if we're broken (pinch phase)
					mobj_t *shield = P_SpawnMobj(mobj->x, mobj->y, mobj->z, MT_MSSHIELD_FRONT);
					P_SetTarget(&mobj->tracer, shield);
					P_SetTarget(&shield->target, mobj);
				}
				else
					P_LinedefExecute(LE_PINCHPHASE, mobj, NULL);
				mobj->fuse = 4*TICRATE;
				mobj->flags |= MF_PAIN;
				if (mobj->info->attacksound)
					S_StartSound(mobj, mobj->info->attacksound);
				A_FaceTarget(mobj);
				break;
			}

			case 2:
				// We're all charged and ready now! Unleash the fury!!
				if (mobj->health > mobj->info->damage)
				{
					mobj_t *removemobj = mobj->tracer;
					P_SetTarget(&mobj->tracer, mobj->hnext);
					P_RemoveMobj(removemobj);
				}
				if (mobj->health <= mobj->info->damage)
				{
					// Attack 1: Pinball dash!
					if (mobj->health == 1)
						mobj->movedir = 0;
					else
						mobj->movedir = 2;
					if (mobj->info->seesound)
						S_StartSound(mobj, mobj->info->seesound);
					P_SetMobjState(mobj, mobj->info->seestate);
					if (mobj->movedir == 2)
						mobj->threshold = 16; // bounce 16 times
					else
						mobj->threshold = 32; // bounce 32 times
					mobj->watertop = mobj->target->floorz + 16*FRACUNIT;
					P_LinedefExecute(LE_PINCHPHASE, mobj, NULL);
				}
				else
				{
					// Attack 2: Energy shot!
					mobj->movedir = 1;

					if (mobj->health >= 8)
						mobj->extravalue1 = 0;
					else if (mobj->health >= 5)
						mobj->extravalue1 = 2;
					else if (mobj->health >= 4)
						mobj->extravalue1 = 1;
					else
						mobj->extravalue1 = 3;

					switch(mobj->extravalue1)
					{
					case 0: // shoot once
					case 2: // spread-shot
					default:
						mobj->threshold = 2;
						break;
					case 1: // shoot 3 times
						mobj->threshold = 3*2;
						break;
					case 3: // shoot like a goddamn machinegun
						mobj->threshold = 8*2;
						break;
					}
				}
				break;

			case 3:
				// Return to idle.
				mobj->watertop = mobj->target->floorz + 32*FRACUNIT;
				P_SetMobjState(mobj, mobj->info->spawnstate);
				mobj->flags &= ~MF_PAIN;
				mobj->fuse = 10*TICRATE;
				break;
			}
			mobj->movecount++;
			mobj->movecount %= 4;
			return;
		}

		// Idle AI
		if (mobj->state == &states[mobj->info->spawnstate])
		{
			fixed_t dist;

			// Target the closest player
			P_BossTargetPlayer(mobj, true);

			// Face your target
			angle = R_PointToAngle2(mobj->x, mobj->y, mobj->target->x, mobj->target->y); // absolute angle
			angle = (angle-mobj->angle); // relative angle
			if (angle < ANGLE_180)
				mobj->angle += angle/8;
			else
				mobj->angle -= InvAngle(angle)/8;
			//A_FaceTarget(mobj);

			// Check if we're being attacked
			if (mobj->target->x+mobj->target->radius+abs(mobj->target->momx*2) < mobj->x-mobj->radius)
				danger = false;
			if (mobj->target->x-mobj->target->radius-abs(mobj->target->momx*2) > mobj->x+mobj->radius)
				danger = false;
			if (mobj->target->y+mobj->target->radius+abs(mobj->target->momy*2) < mobj->y-mobj->radius)
				danger = false;
			if (mobj->target->y-mobj->target->radius-abs(mobj->target->momy*2) > mobj->y+mobj->radius)
				danger = false;
			if (mobj->target->z+mobj->target->height+mobj->target->momz*2 < mobj->z)
				danger = false;
			if (mobj->target->z+mobj->target->momz*2 > mobj->z+mobj->height)
				danger = false;
			if (danger)
			{
				// An incoming attack is detected! What should we do?!
				// Go into vector form!
				mobj->movedir = ANGLE_11hh - FixedAngle(FixedMul(AngleFixed(ANGLE_11hh), FixedDiv((mobj->info->spawnhealth - mobj->health)<<FRACBITS, (mobj->info->spawnhealth-1)<<FRACBITS)));
				if (P_RandomChance(PR_BOSSBATTLE, FRACUNIT/2))
					mobj->movedir = InvAngle(mobj->movedir);
				mobj->threshold = 6 + (FixedMul(24<<FRACBITS, FixedDiv((mobj->info->spawnhealth - mobj->health)<<FRACBITS, (mobj->info->spawnhealth-1)<<FRACBITS))>>FRACBITS);
				if (mobj->info->activesound)
					S_StartSound(mobj, mobj->info->activesound);
				if (mobj->info->painchance)
					P_SetMobjState(mobj, mobj->info->painchance);
				return;
			}

			// Move normally: Approach the player using normal thrust and simulated friction.
			dist = P_AproxDistance(mobj->x-mobj->target->x, mobj->y-mobj->target->y);
			P_Thrust(mobj, R_PointToAngle2(0, 0, mobj->momx, mobj->momy), -3*FRACUNIT/8);
			if (dist < 64*FRACUNIT)
				P_Thrust(mobj, mobj->angle, -4*FRACUNIT);
			else if (dist > 180*FRACUNIT)
				P_Thrust(mobj, mobj->angle, FRACUNIT);
			mobj->momz += P_AproxDistance(mobj->momx, mobj->momy)/12; // Move up higher the faster you're going.
		}
	}
}

//
// P_GetClosestAxis
//
// Finds the CLOSEST axis to the source mobj
mobj_t *P_GetClosestAxis(mobj_t *source)
{
	thinker_t *th;
	mobj_t *mo2;
	mobj_t *closestaxis = NULL;
	fixed_t dist1, dist2 = 0;

	// scan the thinkers to find the closest axis point
	for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
	{
		if (th->function == (actionf_p1)P_RemoveThinkerDelayed)
			continue;

		mo2 = (mobj_t *)th;

		if (mo2->type == MT_AXIS)
		{
			if (closestaxis == NULL)
			{
				closestaxis = mo2;
				dist2 = R_PointToDist2(source->x, source->y, mo2->x, mo2->y)-mo2->radius;
			}
			else
			{
				dist1 = R_PointToDist2(source->x, source->y, mo2->x, mo2->y)-mo2->radius;

				if (dist1 < dist2)
				{
					closestaxis = mo2;
					dist2 = dist1;
				}
			}
		}
	}

	if (closestaxis == NULL)
		CONS_Alert(CONS_ERROR, "No axis points found!\n");

	return closestaxis;
}

static void P_GimmeAxisXYPos(mobj_t *closestaxis, degenmobj_t *mobj)
{
	const angle_t fa = R_PointToAngle2(closestaxis->x, closestaxis->y, mobj->x, mobj->y)>>ANGLETOFINESHIFT;

	mobj->x = closestaxis->x + FixedMul(FINECOSINE(fa),closestaxis->radius);
	mobj->y = closestaxis->y + FixedMul(FINESINE(fa),closestaxis->radius);
}

static void P_MoveHoop(mobj_t *mobj)
{
	const fixed_t fuse = (mobj->fuse*mobj->extravalue2);
	const angle_t fa = mobj->movedir*(FINEANGLES/mobj->extravalue1);
	TVector v;
	TVector *res;
	fixed_t finalx, finaly, finalz;
	fixed_t x, y, z;

	//I_Assert(mobj->target != NULL);
	if (!mobj->target) /// \todo DEBUG ME! Target was P_RemoveMobj'd at some point, and therefore no longer valid!
		return;

	x = mobj->target->x;
	y = mobj->target->y;
	z = mobj->target->z+mobj->target->height/2;

	// Make the sprite travel towards the center of the hoop
	v[0] = FixedMul(FINECOSINE(fa),fuse);
	v[1] = 0;
	v[2] = FixedMul(FINESINE(fa),fuse);
	v[3] = FRACUNIT;

	res = VectorMatrixMultiply(v, *RotateXMatrix(FixedAngle(mobj->target->movedir*FRACUNIT)));
	memcpy(&v, res, sizeof (v));
	res = VectorMatrixMultiply(v, *RotateZMatrix(FixedAngle(mobj->target->movecount*FRACUNIT)));
	memcpy(&v, res, sizeof (v));

	finalx = x + v[0];
	finaly = y + v[1];
	finalz = z + v[2];

	P_UnsetThingPosition(mobj);
	mobj->x = finalx;
	mobj->y = finaly;
	P_SetThingPosition(mobj);
	mobj->z = finalz - mobj->height/2;
}

void P_SpawnHoopOfSomething(fixed_t x, fixed_t y, fixed_t z, fixed_t radius, int32_t number, mobjtype_t type, angle_t rotangle)
{
	mobj_t *mobj;
	int32_t i;
	TVector v;
	TVector *res;
	fixed_t finalx, finaly, finalz;
	mobj_t hoopcenter;
	mobj_t *axis;
	degenmobj_t xypos;
	angle_t degrees, fa, closestangle;

	hoopcenter.x = x;
	hoopcenter.y = y;
	hoopcenter.z = z;

	axis = P_GetClosestAxis(&hoopcenter);

	if (!axis)
	{
		CONS_Alert(CONS_WARNING, "You forgot to put axis points in the map!\n");
		return;
	}

	xypos.x = x;
	xypos.y = y;

	P_GimmeAxisXYPos(axis, &xypos);

	x = xypos.x;
	y = xypos.y;

	hoopcenter.z = z - mobjinfo[type].height/2;

	hoopcenter.x = x;
	hoopcenter.y = y;

	closestangle = R_PointToAngle2(x, y, axis->x, axis->y);

	degrees = FINEANGLES/number;

	radius >>= FRACBITS;

	// Create the hoop!
	for (i = 0; i < number; i++)
	{
		fa = (i*degrees);
		v[0] = FixedMul(FINECOSINE(fa),radius);
		v[1] = 0;
		v[2] = FixedMul(FINESINE(fa),radius);
		v[3] = FRACUNIT;

		res = VectorMatrixMultiply(v, *RotateXMatrix(rotangle));
		memcpy(&v, res, sizeof (v));
		res = VectorMatrixMultiply(v, *RotateZMatrix(closestangle));
		memcpy(&v, res, sizeof (v));

		finalx = x + v[0];
		finaly = y + v[1];
		finalz = z + v[2];

		mobj = P_SpawnMobj(finalx, finaly, finalz, type);
		mobj->z -= mobj->height/2;
	}
}

void P_SpawnParaloop(fixed_t x, fixed_t y, fixed_t z, fixed_t radius, int32_t number, mobjtype_t type, statenum_t nstate, angle_t rotangle, dboolean spawncenter)
{
	mobj_t *mobj;
	int32_t i;
	TVector v;
	TVector *res;
	fixed_t finalx, finaly, finalz, dist;
	angle_t degrees, fa, closestangle;
	fixed_t mobjx, mobjy, mobjz;

	degrees = FINEANGLES/number;

	radius = FixedDiv(radius,5*(FRACUNIT/4));

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
		memcpy(&v, res, sizeof (v));
		res = VectorMatrixMultiply(v, *RotateZMatrix(closestangle));
		memcpy(&v, res, sizeof (v));

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

		// set to special state
		if (nstate != S_NULL)
			P_SetMobjState(mobj, nstate);

		mobj->momx = FixedMul(FixedDiv(x - mobjx, dist), 5*FRACUNIT);
		mobj->momy = FixedMul(FixedDiv(y - mobjy, dist), 5*FRACUNIT);
		mobj->momz = FixedMul(FixedDiv(z - mobjz, dist), 5*FRACUNIT);
		mobj->fuse = (radius>>(FRACBITS+2)) + 1;

		if (spawncenter)
		{
			mobj->x = x;
			mobj->y = y;
			mobj->z = z;
		}

		if (mobj->fuse <= 1)
			mobj->fuse = 2;

		mobj->flags |= MF_NOCLIPTHING;
		mobj->flags &= ~MF_SPECIAL;

		if (mobj->fuse > 7)
			mobj->tics = mobj->fuse - 7;
		else
			mobj->tics = 1;
	}
}

//
// P_SetScale
//
// Sets the sprite scaling
//
void P_SetScale2(mobj_t *mobj, fixed_t newscale, dboolean compat)
{
	player_t *player;
	fixed_t oldscale;

	if (!mobj)
		return;

	oldscale = mobj->scale; //keep for adjusting stuff below

	if (LUA_HookMobjScaleChange(mobj, newscale, oldscale) || P_MobjWasRemoved(mobj))
		return;

	mobj->scale = newscale;

	if (compat)
	{
		mobj->radius = FixedMul(mobj->info->radius, newscale);
		mobj->height = FixedMul(mobj->info->height, newscale);
	}
	else
	{
		mobj->radius = FixedMul(FixedDiv(mobj->radius, oldscale), newscale);
		mobj->height = FixedMul(FixedDiv(mobj->height, oldscale), newscale);
	}

	player = mobj->player;

	if (player)
	{
		G_GhostAddScale((int32_t) (player - players), newscale);
		player->viewheight = FixedMul(FixedDiv(player->viewheight, oldscale), newscale); // Nonono don't calculate viewheight elsewhere, this is the best place for it!
	}
}

void P_Attract(mobj_t *source, mobj_t *dest, dboolean nightsgrab) // Home in on your target
{
	fixed_t dist, ndist, speedmul;
	angle_t vangle;
	fixed_t tx = dest->x;
	fixed_t ty = dest->y;
	fixed_t tz = dest->z + (dest->height/2); // Aim for center
	fixed_t xydist = P_AproxDistance(tx - source->x, ty - source->y);

	if (!dest || dest->health <= 0 || !dest->player || !source->tracer)
		return;

	// change angle
	//source->angle = R_PointToAngle2(source->x, source->y, tx, ty);

	// change slope
	dist = P_AproxDistance(xydist, tz - source->z);

	if (dist < 1)
		dist = 1;

	if (nightsgrab && source->movefactor)
	{
		source->movefactor += FRACUNIT/2;

		if (dist < source->movefactor)
		{
			source->momx = source->momy = source->momz = 0;
			P_MoveOrigin(source, tx, ty, tz);
		}
		else
		{
			vangle = R_PointToAngle2(source->z, 0, tz, xydist);

			source->momx = FixedMul(FINESINE(vangle >> ANGLETOFINESHIFT), FixedMul(FINECOSINE(source->angle >> ANGLETOFINESHIFT), source->movefactor));
			source->momy = FixedMul(FINESINE(vangle >> ANGLETOFINESHIFT), FixedMul(FINESINE(source->angle >> ANGLETOFINESHIFT), source->movefactor));
			source->momz = FixedMul(FINECOSINE(vangle >> ANGLETOFINESHIFT), source->movefactor);
		}
	}
	else
	{
		if (nightsgrab)
			speedmul = P_AproxDistance(dest->momx, dest->momy) + FixedMul(8*FRACUNIT, source->scale);
		else
			speedmul = P_AproxDistance(dest->momx, dest->momy) + FixedMul(source->info->speed, source->scale);

		source->momx = FixedMul(FixedDiv(tx - source->x, dist), speedmul);
		source->momy = FixedMul(FixedDiv(ty - source->y, dist), speedmul);
		source->momz = FixedMul(FixedDiv(tz - source->z, dist), speedmul);
	}

	// Instead of just unsetting NOCLIP like an idiot, let's check the distance to our target.
	ndist = P_AproxDistance(P_AproxDistance(tx - (source->x+source->momx),
	                                        ty - (source->y+source->momy)),
	                                        tz - (source->z+source->momz));

	if (ndist > dist) // gone past our target
	{
		// place us on top of them then.
		source->momx = source->momy = source->momz = 0;
		P_UnsetThingPosition(source);
		source->x = tx;
		source->y = ty;
		source->z = tz;
		P_SetThingPosition(source);
	}
}

//
// P_MaceRotate
// Spins a hnext-chain of objects around its centerpoint, side to side or periodically.
//
void P_MaceRotate(mobj_t *center, int32_t baserot, int32_t baseprevrot)
{
	TVector unit_lengthways, unit_sideways, pos_lengthways, pos_sideways;
	TVector *res;
	fixed_t radius, dist, zstore;
	angle_t fa;
	dboolean dosound = false;
	mobj_t *mobj = center->hnext, *hnext = NULL;

	int32_t lastthreshold = -1; // needs to never be equal at start of loop
	fixed_t lastfriction = INT32_MIN; // ditto; almost certainly never, but...

	int32_t rot;
	int32_t prevrot;

	dist = pos_sideways[0] = pos_sideways[1] = pos_sideways[2] = pos_sideways[3] = unit_sideways[3] =\
	 pos_lengthways[0] = pos_lengthways[1] = pos_lengthways[2] = pos_lengthways[3] = 0;

	while (mobj)
	{
		if (P_MobjWasRemoved(mobj) || !mobj->health)
		{
			mobj = mobj->hnext;
			continue;
		}

		mobj->momx = mobj->momy = mobj->momz = 0;

		if (mobj->threshold != lastthreshold
		|| mobj->friction != lastfriction)
		{
			rot = (baserot + mobj->threshold) & FINEMASK;
			prevrot = (baseprevrot + mobj->threshold) & FINEMASK;

			pos_lengthways[0] = pos_lengthways[1] = pos_lengthways[2] = pos_lengthways[3] = 0;

			dist = ((mobj->info->speed) ? mobj->info->speed : mobjinfo[MT_SMALLMACECHAIN].speed);
			dist = ((center->scale == FRACUNIT) ? dist : FixedMul(dist, center->scale));

			fa = (FixedAngle(center->movefactor*FRACUNIT) >> ANGLETOFINESHIFT);
			radius = FixedMul(dist, FINECOSINE(fa));
			unit_lengthways[1] = -FixedMul(dist, FINESINE(fa));
			unit_lengthways[3] = FRACUNIT;

			// Swinging Chain.
			if (center->flags2 & MF2_STRONGBOX)
			{
				fixed_t swingmag = FixedMul(FINECOSINE(rot), center->lastlook << FRACBITS);
				fixed_t prevswingmag = FINECOSINE(prevrot);

				if ((prevswingmag > 0) != (swingmag > 0)) // just passed its lowest point
					dosound = true;

				fa = ((FixedAngle(swingmag) >> ANGLETOFINESHIFT) + mobj->friction) & FINEMASK;

				unit_lengthways[0] = FixedMul(FINESINE(fa), -radius);
				unit_lengthways[2] = FixedMul(FINECOSINE(fa), -radius);
			}
			// Rotating Chain.
			else
			{
				angle_t prevfa = (prevrot + mobj->friction) & FINEMASK;
				fa = (rot + mobj->friction) & FINEMASK;

				// completed a half-spin
				dosound = ((prevfa > (FINEMASK/2)) != (fa > (FINEMASK/2)));

				unit_lengthways[0] = FixedMul(FINECOSINE(fa), radius);
				unit_lengthways[2] = FixedMul(FINESINE(fa), radius);
			}

			// Calculate the angle matrixes for the link.
			res = VectorMatrixMultiply(unit_lengthways, *RotateXMatrix(center->threshold << ANGLETOFINESHIFT));
			memcpy(&unit_lengthways, res, sizeof(unit_lengthways));
			res = VectorMatrixMultiply(unit_lengthways, *RotateZMatrix(center->angle));
			memcpy(&unit_lengthways, res, sizeof(unit_lengthways));

			lastthreshold = mobj->threshold;
			lastfriction = mobj->friction;
		}

		if (dosound && (mobj->flags2 & MF2_BOSSNOTRAP))
		{
			S_StartSound(mobj, mobj->info->activesound);
			dosound = false;
		}

		if (pos_sideways[3] != mobj->movefactor)
		{
			if (!unit_sideways[3])
			{
				unit_sideways[1] = dist;
				unit_sideways[0] = unit_sideways[2] = 0;
				unit_sideways[3] = FRACUNIT;

				res = VectorMatrixMultiply(unit_sideways, *RotateXMatrix(center->threshold << ANGLETOFINESHIFT));
				memcpy(&unit_sideways, res, sizeof(unit_sideways));
				res = VectorMatrixMultiply(unit_sideways, *RotateZMatrix(center->angle));
				memcpy(&unit_sideways, res, sizeof(unit_sideways));
			}

			if (pos_sideways[3] > mobj->movefactor)
			{
				do
				{
					pos_sideways[0] -= unit_sideways[0];
					pos_sideways[1] -= unit_sideways[1];
					pos_sideways[2] -= unit_sideways[2];
				}
				while ((--pos_sideways[3]) != mobj->movefactor);
			}
			else
			{
				do
				{
					pos_sideways[0] += unit_sideways[0];
					pos_sideways[1] += unit_sideways[1];
					pos_sideways[2] += unit_sideways[2];
				}
				while ((++pos_sideways[3]) != mobj->movefactor);
			}
		}

		hnext = mobj->hnext; // just in case the mobj is removed

		if (pos_lengthways[3] > mobj->movecount)
		{
			do
			{
				pos_lengthways[0] -= unit_lengthways[0];
				pos_lengthways[1] -= unit_lengthways[1];
				pos_lengthways[2] -= unit_lengthways[2];
			}
			while ((--pos_lengthways[3]) != mobj->movecount);
		}
		else if (pos_lengthways[3] < mobj->movecount)
		{
			do
			{
				pos_lengthways[0] += unit_lengthways[0];
				pos_lengthways[1] += unit_lengthways[1];
				pos_lengthways[2] += unit_lengthways[2];
			}
			while ((++pos_lengthways[3]) != mobj->movecount);
		}

		P_UnsetThingPosition(mobj);

		mobj->x = center->x;
		mobj->y = center->y;
		mobj->z = center->z;

		// Add on the appropriate distances to the center's co-ordinates.
		if (pos_lengthways[3])
		{
			mobj->x += pos_lengthways[0];
			mobj->y += pos_lengthways[1];
			zstore = pos_lengthways[2] + pos_sideways[2];
		}
		else
			zstore = pos_sideways[2];

		mobj->x += pos_sideways[0];
		mobj->y += pos_sideways[1];

		// Cut the height to align the link with the axis.
		if (mobj->type == MT_SMALLMACECHAIN || mobj->type == MT_BIGMACECHAIN || mobj->type == MT_SMALLGRABCHAIN || mobj->type == MT_BIGGRABCHAIN)
			zstore -= P_MobjFlip(mobj)*mobj->height/4;
		else
			zstore -= P_MobjFlip(mobj)*mobj->height/2;

		mobj->z += zstore;

#if 0 // toaster's testing flashie!
		if (!(mobj->movecount & 1) && !(leveltime & TICRATE)) // I had a brainfart and the flashing isn't exactly what I expected it to be, but it's actually much more useful.
			mobj->renderflags ^= RF_DONTDRAW;
#endif

		P_SetThingPosition(mobj);

#if 0 // toaster's height-clipping dealie!
		if (!pos_lengthways[3] || P_MobjWasRemoved(mobj) || (mobj->flags & MF_NOCLIPHEIGHT))
			goto cont;

		if ((fa = ((center->threshold & (FINEMASK/2)) << ANGLETOFINESHIFT)) > ANGLE_45 && fa < ANGLE_135) // only move towards center when the motion is towards/away from the ground, rather than alongside it
			goto cont;

		if (mobj->subsector->sector->ffloors)
			P_AdjustMobjFloorZ_FFloors(mobj, mobj->subsector->sector, 2);

		if (mobj->floorz > mobj->z)
			zstore = (mobj->floorz - zstore);
		else if (mobj->ceilingz < mobj->z)
			zstore = (mobj->ceilingz - mobj->height - zstore);
		else
			goto cont;

		zstore = FixedDiv(zstore, dist); // Still needs work... scaling factor is wrong!

		P_UnsetThingPosition(mobj);

		mobj->x -= FixedMul(unit_lengthways[0], zstore);
		mobj->y -= FixedMul(unit_lengthways[1], zstore);

		P_SetThingPosition(mobj);

cont:
#endif
		mobj = hnext;
	}
}

// Kartitem stuff.

// This item is never attached to a player -- it can DIE
// unconditionally in death sectors.
dboolean P_IsKartFieldItem(int32_t type)
{
	switch (type)
	{
		case MT_BANANA:
		case MT_EGGMANITEM:
		case MT_ORBINAUT:
		case MT_JAWZ:
		case MT_JAWZ_DUD:
		case MT_SSMINE:
		case MT_LANDMINE:
		case MT_EGGMINE:
		case MT_BALLHOG:
		case MT_SINK:
			return true;

		default:
			return false;
	}
}

dboolean P_IsKartEquippedItem(int32_t type)
{
	switch (type)
	{
		case MT_EGGMANITEM_SHIELD:
		case MT_BANANA_SHIELD:
		case MT_ORBINAUT_SHIELD:
		case MT_JAWZ_SHIELD:
		case MT_SSMINE_SHIELD:
		case MT_SINK_SHIELD:
		case MT_EGGMINE_SHIELD:
			return true;

		default:
			return false;
	}
}

dboolean P_IsKartItem(int32_t type)
{
	switch (type)
	{
		// Primarily for minimap data, handle with care
		case MT_SPB:
			return true;
		// FALLTHRU

		default:
			return P_IsKartFieldItem(type) || P_IsKartEquippedItem(type);
	}
}

dboolean K_IsMissileOrKartItem(mobj_t *mo)
{
	if (mo->flags & MF_MISSILE)
	{
		// It's already a missile!
		return true;
	}

	if (mo->type == MT_SPB)
	{
		// Not considered a field item, so manually include.
		return true;
	}

	return P_IsKartFieldItem(mo->type);
}

// This item can die in death sectors. There may be some
// special conditions for items that don't switch types...
// TODO: just make a general function for things that should
// die like this?
dboolean P_CanDeleteKartItem(int32_t type)
{
	return P_IsKartFieldItem(type);
}

// Called when a kart item "thinks"
void P_AddKartItem(mobj_t *thing)
{
	I_Assert(thing != NULL);

	if (kitemcap == NULL)
		P_SetTarget(&kitemcap, thing);
	else {
		mobj_t *mo;
		for (mo = kitemcap; mo && mo->itnext; mo = mo->itnext)
			;

		I_Assert(mo != NULL);
		I_Assert(mo->itnext == NULL);

		P_SetTarget(&mo->itnext, thing);
	}
	P_SetTarget(&thing->itnext, NULL);
}

// Called only when a kart item is removed
// Keeps the hnext list from corrupting.
static void P_RemoveKartItem(mobj_t *thing)
{
	mobj_t *mo, **p;
	for (mo = *(p = &kitemcap); mo; mo = *(p = &mo->itnext))
	{
		if (mo != thing)
			continue;

		P_SetTarget(p, thing->itnext);
		P_SetTarget(&thing->itnext, NULL);
		return;
	}
}

static dboolean P_IsMiscCapObject(int32_t type)
{
	switch (type)
	{

		case MT_RING:
		case MT_FLINGRING:
			if (cv_kartdebugrings.value)
				return true;
			break;

		case MT_RANDOMITEM:
				return true;
			break;

		case MT_PLAYER:
				return true;

		default:
			break;
	}

	return false;
}

// Called when a misc mobj "thinks"
void P_AddMiscCap(mobj_t *thing)
{
	I_Assert(thing != NULL);

	if (misccap == NULL)
		P_SetTarget(&misccap, thing);
	else {
		mobj_t *mo;
		for (mo = misccap; mo && mo->itnext; mo = mo->itnext)
			;

		I_Assert(mo != NULL);
		I_Assert(mo->itnext == NULL);

		P_SetTarget(&mo->itnext, thing);
	}
	P_SetTarget(&thing->itnext, NULL);
}

// Called only when a kart item is removed
// Keeps the hnext list from corrupting.
static void P_RemoveMiscCap(mobj_t *thing)
{
	mobj_t *mo, **p;
	for (mo = *(p = &misccap); mo; mo = *(p = &mo->itnext))
	{
		if (mo != thing)
			continue;

		P_SetTarget(p, thing->itnext);
		P_SetTarget(&thing->itnext, NULL);
		return;
	}
}

void P_RunOverlays(void)
{
	// run overlays
	mobj_t *mo, *next = NULL;
	fixed_t destx,desty,zoffs;

	for (mo = overlaycap; mo; mo = next)
	{
		I_Assert(!P_MobjWasRemoved(mo));

		// grab next in chain, then unset the chain target
		next = mo->hnext;
		P_SetTarget(&mo->hnext, NULL);

		if (!mo->target)
			continue;

		if (P_MobjWasRemoved(mo->target))
		{
			P_RemoveMobj(mo);
			continue;
		}

		destx = mo->target->x;
		desty = mo->target->y;

		mo->eflags = (mo->eflags & ~MFE_VERTICALFLIP) | (mo->target->eflags & MFE_VERTICALFLIP);
		mo->scale = mo->destscale = FixedMul(mo->target->scale, mo->movefactor);

		if (!(mo->threshold & OV_DONTCOPYANGLE))
		{
			mo->angle = (mo->target->player ? mo->target->player->drawangle : mo->target->angle) + mo->movedir;
		}

		if (!(mo->threshold & OV_DONTSCREENOFFSET))
		{
			mo->spritexoffset = mo->target->spritexoffset;
			mo->spriteyoffset = mo->target->spriteyoffset;
		}

		if (!(mo->threshold & OV_DONT3DOFFSET))
		{
			mo->sprxoff = mo->target->sprxoff;
			mo->spryoff = mo->target->spryoff;
			mo->sprzoff = mo->target->sprzoff;
		}

		if (!(mo->threshold & OV_DONTXYSCALE))
		{
			mo->spritexscale = mo->target->spritexscale;
			mo->spriteyscale = mo->target->spriteyscale;
		}

		if (!(mo->threshold & OV_DONTROLL))
		{
			mo->slopepitch = mo->target->slopepitch;
			mo->sloperoll = mo->target->sloperoll;
			mo->rollangle = mo->target->rollangle;
			mo->pitch = mo->target->pitch;
			mo->roll = mo->target->roll;
		}

		if (!(mo->threshold & OV_DONTBAKEOFFSET))
		{
			// offsets
			mo->bakexoff = mo->target->bakexoff;
			mo->bakeyoff = mo->target->bakeyoff;
			mo->bakezoff = mo->target->bakezoff;
			// pivots
			mo->bakexpiv = mo->target->bakexpiv;
			mo->bakeypiv = mo->target->bakeypiv;
			mo->bakezpiv = mo->target->bakezpiv;
		}

		if ((mo->flags & MF_DONTENCOREMAP) != (mo->target->flags & MF_DONTENCOREMAP))
			mo->flags ^= MF_DONTENCOREMAP;

		mo->dispoffset = mo->target->dispoffset + mo->info->dispoffset;

		if (!(mo->state->frame & FF_ANIMATE))
		{
			zoffs = FixedMul(((signed)mo->state->var2)*FRACUNIT, mo->scale);

			if (mo->state->var1)
			{
				mo->dispoffset--;
			}
			else
			{
				mo->dispoffset++;
			}
		}
		else
		{
			// if you're using FF_ANIMATE on an overlay,
			// then you're on your own.
			zoffs = 0;
		}

		P_UnsetThingPosition(mo);
		mo->x = destx;
		mo->y = desty;
		mo->radius = mo->target->radius;
		mo->height = mo->target->height;
		if (mo->eflags & MFE_VERTICALFLIP)
			mo->z = (mo->target->z + mo->target->height - mo->height) - zoffs;
		else
			mo->z = mo->target->z + zoffs;
		if (mo->state->var1)
			P_SetUnderlayPosition(mo);
		else
			P_SetThingPosition(mo);
		P_CheckPosition(mo, mo->x, mo->y, NULL);
	}
	P_SetTarget(&overlaycap, NULL);
}

// Called only when MT_OVERLAY thinks.
static void P_AddOverlay(mobj_t *thing)
{
	I_Assert(thing != NULL);

	if (overlaycap == NULL)
		P_SetTarget(&overlaycap, thing);
	else {
		mobj_t *mo;
		for (mo = overlaycap; mo && mo->hnext; mo = mo->hnext)
			;

		I_Assert(mo != NULL);
		I_Assert(mo->hnext == NULL);

		P_SetTarget(&mo->hnext, thing);
	}
	P_SetTarget(&thing->hnext, NULL);
}

// Called only when MT_OVERLAY (or anything else in the overlaycap list) is removed.
// Keeps the hnext list from corrupting.
static void P_RemoveOverlay(mobj_t *thing)
{
	mobj_t *mo, **p;
	for (mo = *(p = &overlaycap); mo; mo = *(p = &mo->hnext))
	{
		if (mo != thing)
			continue;

		P_SetTarget(p, thing->hnext);
		P_SetTarget(&thing->hnext, NULL);
		return;
	}
}

static uint32_t P_GetTranslucencyForSMonitor(mobj_t *mo)
{
	if (!mo->player)
	{
		// Get out.
		return 0;
	}

	return max(0, min(9, 10 - FixedMul(10, K_SMonitorGradient(mo->player->smonitortimer))))<<RF_TRANSSHIFT;
}

// Called only when MT_OVERLAY thinks.
static void P_PlayerSMonitorOverlay(mobj_t *thing)
{
	I_Assert(thing->target != NULL);
	I_Assert(thing->target->player != NULL);

	if (!thing->target->player->smonitortimer)
	{
		P_SetTarget(&thing->target, NULL);

		I_Assert(thing->target == NULL);
		return;
	}

	thing->destscale = thing->target->scale;

	if (thing->target->eflags & MFE_VERTICALFLIP)
	{
		thing->eflags |= MFE_VERTICALFLIP;
		thing->z += thing->target->height - thing->height;
	}

	thing->color = K_SMonitorColor(leveltime / 2);
	thing->colorized = true;
	thing->dispoffset = min(2, thing->target->dispoffset + 1);

	thing->angle = (thing->target->player ? thing->target->player->drawangle : thing->target->angle);

	// Rotation is handled in r_patchrotation

	thing->sprite = thing->target->sprite;
	thing->sprite2 = thing->target->sprite2;
	thing->frame = thing->target->frame | FF_FULLBRIGHT | FF_ADD;
	thing->tics = -1;
	thing->renderflags = (thing->target->renderflags & ~RF_TRANSMASK)|P_GetTranslucencyForSMonitor(thing->target);
	thing->skin = thing->target->skin;
	thing->standingslope = thing->target->standingslope;

	thing->sprxoff = thing->target->sprxoff;
	thing->spryoff = thing->target->spryoff;
	thing->sprzoff = thing->target->sprzoff;

	thing->spritexscale = thing->target->spritexscale;
	thing->spriteyscale = thing->target->spriteyscale;
	thing->spritexoffset = thing->target->spritexoffset;
	thing->spriteyoffset = thing->target->spriteyoffset;

	if (thing->target->flags2 & MF2_OBJECTFLIP)
		thing->flags2 |= MF2_OBJECTFLIP;

	if (!(thing->target->flags & MF_DONTENCOREMAP))
		thing->flags &= ~MF_DONTENCOREMAP;
}

static void P_MobjScaleThink(mobj_t *mobj)
{
	fixed_t oldheight = mobj->height;
	uint8_t correctionType = 0; // Don't correct Z position, just gain height

	if (mobj->flags & MF_NOCLIPHEIGHT || (mobj->z > mobj->floorz && mobj->z + mobj->height < mobj->ceilingz))
		correctionType = 1; // Correct Z position by centering
	else if (mobj->eflags & MFE_VERTICALFLIP)
		correctionType = 2; // Correct Z position by moving down

	if (abs(mobj->scale - mobj->destscale) < mobj->scalespeed)
		P_SetScale(mobj, mobj->destscale);
	else if (mobj->scale < mobj->destscale)
		P_SetScale(mobj, mobj->scale + mobj->scalespeed);
	else if (mobj->scale > mobj->destscale)
		P_SetScale(mobj, mobj->scale - mobj->scalespeed);

	if (correctionType == 1)
		mobj->z -= (mobj->height - oldheight)/2;
	else if (correctionType == 2)
		mobj->z -= mobj->height - oldheight;

	if (mobj->scale == mobj->destscale)
	{
		/// \todo Lua hook for "reached destscale"?
		switch (mobj->type)
		{
			default:
				if (mobj->player)
				{
					if (mobj->scale <= 1)
					{
						mobj->renderflags |= RF_DONTDRAW;
					}
				}
				else
				{
					if (!mobj->player && mobj->scale == 0)
					{
						P_RemoveMobj(mobj);
						return;
					}
				}
				break;
		}
	}
}

static void P_MaceSceneryThink(mobj_t *mobj)
{
	angle_t oldmovedir = mobj->movedir;

	// Always update movedir to prevent desyncing (in the traditional sense, not the netplay sense).
	mobj->movedir = (mobj->movedir + mobj->lastlook) & FINEMASK;

	// If too far away and not deliberately spitting in the face of optimisation, don't think!
	if (!(mobj->flags2 & MF2_BOSSNOTRAP))
	{
		uint8_t i;
		// Quick! Look through players! Don't move unless a player is relatively close by.
		// The below is selected based on CEZ2's first room. I promise you it is a coincidence that it looks like the weed number.
		for (i = 0; i < MAXPLAYERS; ++i)
			if (playeringame[i] && players[i].mo
				&& P_AproxDistance(P_AproxDistance(mobj->x - players[i].mo->x, mobj->y - players[i].mo->y), mobj->z - players[i].mo->z) < (4200 << FRACBITS))
				break; // Stop looking.
		if (i == MAXPLAYERS)
		{
			if (!(mobj->flags2 & MF2_BEYONDTHEGRAVE))
			{
				mobj_t *ref = mobj;

				// stop/hide all your babies
				while ((ref = ref->hnext))
				{
					ref->eflags = (((ref->flags & MF_NOTHINK) ? 0 : 1)
						| ((ref->flags & MF_NOCLIPTHING) ? 0 : 2)
						| ((ref->renderflags & RF_DONTDRAW) ? 0 : 4)); // oh my god this is nasty.
					ref->flags |= MF_NOTHINK|MF_NOCLIPTHING;
					ref->renderflags |= RF_DONTDRAW;
					ref->momx = ref->momy = ref->momz = 0;
				}

				mobj->flags2 |= MF2_BEYONDTHEGRAVE;
			}
			return; // don't make bubble!
		}
		else if (mobj->flags2 & MF2_BEYONDTHEGRAVE)
		{
			mobj_t *ref = mobj;

			// start/show all your babies
			while ((ref = ref->hnext))
			{
				if (ref->eflags & 1)
					ref->flags &= ~MF_NOTHINK;
				if (ref->eflags & 2)
					ref->flags &= ~MF_NOCLIPTHING;
				if (ref->eflags & 4)
					ref->renderflags &= ~RF_DONTDRAW;
				ref->eflags = 0; // le sign
			}

			mobj->flags2 &= ~MF2_BEYONDTHEGRAVE;
		}
	}

	// Okay, time to MOVE
	P_MaceRotate(mobj, mobj->movedir, oldmovedir);
}

static void P_FlameJetSceneryThink(mobj_t *mobj)
{
	mobj_t *flame;
	fixed_t strength;

	if (!(mobj->flags2 & MF2_FIRING))
		return;

	if ((leveltime & 3) != 0)
		return;

	// Wave the flames back and forth. Reactiontime determines which direction it's going.
	if (mobj->fuse <= -16)
		mobj->reactiontime = 1;
	else if (mobj->fuse >= 16)
		mobj->reactiontime = 0;

	if (mobj->reactiontime)
		mobj->fuse += 2;
	else
		mobj->fuse -= 2;

	flame = P_SpawnMobj(mobj->x, mobj->y, mobj->z, MT_FLAMEJETFLAME);
	P_SetMobjState(flame, S_FLAMEJETFLAME4);

	flame->renderflags |= RF_TRANS40;

	flame->angle = mobj->angle;

	if (mobj->flags2 & MF2_AMBUSH) // Wave up and down instead of side-to-side
		flame->momz = mobj->fuse << (FRACBITS - 2);
	else
		flame->angle += FixedAngle(mobj->fuse<<FRACBITS);

	strength = 20*FRACUNIT;
	strength -= ((20*FRACUNIT)/16)*mobj->movedir;

	P_InstaThrust(flame, flame->angle, strength);
	S_StartSound(flame, sfx_fire);
}

static void P_VerticalFlameJetSceneryThink(mobj_t *mobj)
{
	mobj_t *flame;
	fixed_t strength;

	if (!(mobj->flags2 & MF2_FIRING))
		return;

	if ((leveltime & 3) != 0)
		return;

	// Wave the flames back and forth. Reactiontime determines which direction it's going.
	if (mobj->fuse <= -16)
		mobj->reactiontime = 1;
	else if (mobj->fuse >= 16)
		mobj->reactiontime = 0;

	if (mobj->reactiontime)
		mobj->fuse++;
	else
		mobj->fuse--;

	flame = P_SpawnMobj(mobj->x, mobj->y, mobj->z, MT_FLAMEJETFLAME);

	flame->renderflags |= RF_TRANS40;

	strength = 20*FRACUNIT;
	strength -= ((20*FRACUNIT)/16)*mobj->movedir;

	// If deaf'd, the object spawns on the ceiling.
	if (mobj->flags2 & MF2_AMBUSH)
	{
		mobj->z = mobj->ceilingz - mobj->height;
		flame->momz = -strength;
	}
	else
	{
		flame->momz = strength;
		P_SetMobjState(flame, S_FLAMEJETFLAME7);
	}

	P_InstaThrust(flame, mobj->angle, FixedDiv(mobj->fuse*FRACUNIT, 3*FRACUNIT));
	S_StartSound(flame, sfx_fire);
}

static dboolean P_ParticleGenSceneryThink(mobj_t *mobj)
{
	if (!udmf)
		return false;

	if (!mobj->lastlook)
		return false;

	if (!mobj->threshold)
		return false;

	if (--mobj->fuse <= 0)
	{
		int32_t i = 0;
		mobj_t *spawn;
		fixed_t bottomheight, topheight;
		int32_t type = mobj->threshold, line = mobj->cvmem;

		mobj->fuse = (tic_t)mobj->reactiontime;

		bottomheight = lines[line].frontsector->floorheight;
		topheight = lines[line].frontsector->ceilingheight - mobjinfo[(mobjtype_t)type].height;

		if (mobj->waterbottom != bottomheight || mobj->watertop != topheight)
		{
			if (mobj->movefactor && (topheight > bottomheight))
				mobj->health = (tic_t)(FixedDiv((topheight - bottomheight), abs(mobj->movefactor)) >> FRACBITS);
			else
				mobj->health = 0;

			mobj->z = ((mobj->flags2 & MF2_OBJECTFLIP) ? topheight : bottomheight);
		}

		if (!mobj->health)
			return false;

		for (i = 0; i < mobj->lastlook; i++)
		{
			spawn = P_SpawnMobj(
				mobj->x + FixedMul(FixedMul(mobj->friction, mobj->scale), FINECOSINE(mobj->angle >> ANGLETOFINESHIFT)),
				mobj->y + FixedMul(FixedMul(mobj->friction, mobj->scale), FINESINE(mobj->angle >> ANGLETOFINESHIFT)),
				mobj->z,
				(mobjtype_t)mobj->threshold);
			P_SetScale(spawn, mobj->scale);
			spawn->momz = FixedMul(mobj->movefactor, spawn->scale);
			spawn->destscale = spawn->scale/100;
			spawn->scalespeed = spawn->scale/mobj->health;
			spawn->tics = (tic_t)mobj->health;
			spawn->flags2 |= (mobj->flags2 & MF2_OBJECTFLIP);
			spawn->angle += P_RandomKey(PR_DECORATION, 36)*ANG10; // irrelevant for default objects but might make sense for some custom ones

			mobj->angle += mobj->movedir;
		}
	}

	return true;
}

static void P_MobjSceneryThink(mobj_t *mobj)
{
	if (LUA_HookMobj(mobj, MOBJ_HOOK(MobjThinker)))
		return;
	if (P_MobjWasRemoved(mobj))
		return;

	switch (mobj->type)
	{
	case MT_SHADOW:
		if (mobj->tracer)
		{
			P_MoveOrigin(mobj,
					mobj->tracer->x,
					mobj->tracer->y,
					mobj->tracer->z);
		}
		else
		{
			P_RemoveMobj(mobj);
			return;
		}
		break;
	case MT_MACEPOINT:
	case MT_CHAINMACEPOINT:
	case MT_CHAINPOINT:
	case MT_CUSTOMMACEPOINT:
	case MT_HIDDEN_SLING:
		P_MaceSceneryThink(mobj);
		break;
	case MT_SMALLMACE:
	case MT_BIGMACE:
		P_SpawnGhostMobj(mobj)->tics = 8;
		break;
	case MT_HOOP:
		if (mobj->fuse > 1)
			P_MoveHoop(mobj);
		else if (mobj->fuse == 1)
			mobj->movecount = 1;

		if (mobj->movecount)
		{
			mobj->fuse++;

			if (mobj->fuse > 32)
			{
				// Don't kill the hoop center. For the sake of respawning.
				//if (mobj->target)
				//	P_RemoveMobj(mobj->target);

				P_RemoveMobj(mobj);
			}
		}
		else
			mobj->fuse--;
		return;
	case MT_NIGHTSPARKLE:
		if (mobj->tics != -1)
		{
			mobj->tics--;

			// you can cycle through multiple states in a tic
			if (!mobj->tics)
				if (!P_SetMobjState(mobj, mobj->state->nextstate))
					return; // freed itself
		}

		P_UnsetThingPosition(mobj);
		mobj->x += mobj->momx;
		mobj->y += mobj->momy;
		mobj->z += mobj->momz;
		P_SetThingPosition(mobj);
		return;
	case MT_NIGHTSLOOPHELPER:
		if (--mobj->tics <= 0)
			P_RemoveMobj(mobj);

		// Don't touch my fuse!
		return;
	case MT_OVERLAY:
		if (!mobj->target)
		{
			P_RemoveMobj(mobj);
			return;
		}
		else
		{
			P_AddOverlay(mobj);
			if ((mobj->extravalue2) && (mobj->target->player))
			{
				// Could be overlaying a player using an S-Monitor.
				P_PlayerSMonitorOverlay(mobj);
			}
		}
		break;
	case MT_WATERDROP:
		P_SceneryCheckWater(mobj);
		if ((mobj->z <= mobj->floorz || mobj->z <= mobj->watertop)
			&& mobj->health > 0)
		{
			mobj->health = 0;
			P_SetMobjState(mobj, mobj->info->deathstate);
			S_StartSound(mobj, mobj->info->deathsound + P_RandomKey(PR_SFX_ALT, mobj->info->mass));
			return;
		}
		break;
	case MT_BUBBLES:
		P_SceneryCheckWater(mobj);
		break;
	case MT_SMALLBUBBLE:
	case MT_MEDIUMBUBBLE:
	case MT_EXTRALARGEBUBBLE:	// start bubble dissipate
		P_SceneryCheckWater(mobj);
		if (P_MobjWasRemoved(mobj)) // bubble was removed by not being in water
			return;
		if (!(mobj->eflags & MFE_UNDERWATER)
			|| (!(mobj->eflags & MFE_VERTICALFLIP) && mobj->z + mobj->height >= mobj->ceilingz)
			|| (mobj->eflags & MFE_VERTICALFLIP && mobj->z <= mobj->floorz)
			|| (P_CheckDeathPitCollide(mobj))
			|| --mobj->fuse <= 0) // Bubbles eventually dissipate if they can't reach the surface.
		{
			// no playing sound: no point; the object is being removed
			P_RemoveMobj(mobj);
			return;
		}
		break;
	case MT_LOCKON:
		if (!mobj->target)
		{
			P_RemoveMobj(mobj);
			return;
		}

		mobj->renderflags &= ~RF_DONTDRAW;

		mobj->x = mobj->target->x;
		mobj->y = mobj->target->y;

		mobj->eflags |= (mobj->target->eflags & MFE_VERTICALFLIP);

		mobj->destscale = mobj->target->destscale;
		P_SetScale(mobj, mobj->target->scale);

		if (!(mobj->eflags & MFE_VERTICALFLIP))
			mobj->z = mobj->target->z + mobj->target->height + FixedMul((16 + abs((signed)(leveltime % TICRATE) - TICRATE/2))*FRACUNIT, mobj->target->scale);
		else
			mobj->z = mobj->target->z - FixedMul((16 + abs((signed)(leveltime % TICRATE) - TICRATE/2))*FRACUNIT, mobj->target->scale) - mobj->height;

		mobj->old_z = mobj->z;

		break;
	case MT_LOCKONINF:
		if (!(mobj->flags2 & MF2_STRONGBOX))
		{
			mobj->threshold = mobj->z;
			mobj->flags2 |= MF2_STRONGBOX;
		}
		if (!(mobj->eflags & MFE_VERTICALFLIP))
			mobj->z = mobj->threshold + FixedMul((16 + abs((signed)(leveltime % TICRATE) - TICRATE/2))*FRACUNIT, mobj->scale);
		else
			mobj->z = mobj->threshold - FixedMul((16 + abs((signed)(leveltime % TICRATE) - TICRATE/2))*FRACUNIT, mobj->scale);

		mobj->old_z = mobj->z;

		break;
	case MT_FLAMEJET:
		P_FlameJetSceneryThink(mobj);
		break;
	case MT_VERTICALFLAMEJET:
		P_VerticalFlameJetSceneryThink(mobj);
		break;
	case MT_FLICKY_01_CENTER:
	case MT_FLICKY_02_CENTER:
	case MT_FLICKY_03_CENTER:
	case MT_FLICKY_04_CENTER:
	case MT_FLICKY_05_CENTER:
	case MT_FLICKY_06_CENTER:
	case MT_FLICKY_07_CENTER:
	case MT_FLICKY_08_CENTER:
	case MT_FLICKY_09_CENTER:
	case MT_FLICKY_10_CENTER:
	case MT_FLICKY_11_CENTER:
	case MT_FLICKY_12_CENTER:
	case MT_FLICKY_13_CENTER:
	case MT_FLICKY_14_CENTER:
	case MT_FLICKY_15_CENTER:
	case MT_FLICKY_16_CENTER:
	case MT_SECRETFLICKY_01_CENTER:
	case MT_SECRETFLICKY_02_CENTER:
		if (mobj->tracer && (mobj->flags & MF_NOCLIPTHING)
			&& (mobj->flags & MF_GRENADEBOUNCE))
			// for now: only do this bounce routine if flicky is in-place. \todo allow in all movements
		{
			if (!(mobj->tracer->flags2 & MF2_OBJECTFLIP) && mobj->tracer->z <= mobj->tracer->floorz)
				mobj->tracer->momz = 7*FRACUNIT;
			else if ((mobj->tracer->flags2 & MF2_OBJECTFLIP) && mobj->tracer->z >= mobj->tracer->ceilingz - mobj->tracer->height)
				mobj->tracer->momz = -7*FRACUNIT;
		}
		break;
	case MT_SEED:
		if (P_MobjFlip(mobj)*mobj->momz < mobj->info->speed)
			mobj->momz = P_MobjFlip(mobj)*mobj->info->speed;
		break;
	case MT_ROCKCRUMBLE1:
	case MT_ROCKCRUMBLE2:
	case MT_ROCKCRUMBLE3:
	case MT_ROCKCRUMBLE4:
	case MT_ROCKCRUMBLE5:
	case MT_ROCKCRUMBLE6:
	case MT_ROCKCRUMBLE7:
	case MT_ROCKCRUMBLE8:
	case MT_ROCKCRUMBLE9:
	case MT_ROCKCRUMBLE10:
	case MT_ROCKCRUMBLE11:
	case MT_ROCKCRUMBLE12:
	case MT_ROCKCRUMBLE13:
	case MT_ROCKCRUMBLE14:
	case MT_ROCKCRUMBLE15:
	case MT_ROCKCRUMBLE16:
		if (mobj->z <= P_FloorzAtPos(mobj->x, mobj->y, mobj->z, mobj->height)
        && mobj->state != &states[mobj->info->deathstate])
		{
			P_SetMobjState(mobj, mobj->info->deathstate);
			return;
		}
		break;
	case MT_PARTICLEGEN:
		if (!P_ParticleGenSceneryThink(mobj))
			return;
		break;
	case MT_ORBINAUT_SHIELD: // Kart orbit/trail items
	case MT_JAWZ_SHIELD:
	case MT_BANANA_SHIELD:
	case MT_SSMINE_SHIELD:
	case MT_EGGMANITEM_SHIELD:
	case MT_EGGMINE_SHIELD:
	case MT_SINK_SHIELD:
		if ((mobj->health > 0
			&& (!mobj->target || !mobj->target->player || mobj->target->health <= 0 || mobj->target->player->spectator))
			|| (mobj->health <= 0 && P_IsObjectOnGround(mobj))
			|| P_CheckDeathPitCollide(mobj)) // When in death state
		{
			P_RemoveMobj(mobj);
			return;
		}
		break;
	case MT_BUBBLESHIELD_DEBRIS:
		mobj->rollangle += ANG1 * 4;

		if (P_IsObjectOnGround(mobj) && mobj->health > 0)
		{
			// "Despawn" and play the glass landing sound.
			if (mobj->extravalue1 == 0)
				S_StartSoundAtVolume(mobj, mobj->info->deathsound, 192);
			P_KillMobj(mobj, NULL, NULL, DMG_NORMAL);
			mobj->fuse = TICRATE;
		}

		goto dofuse;
	case MT_SMOLDERING:
		if (leveltime % 2 == 0)
		{
			fixed_t x = P_RandomRange(PR_SMOLDERING, -35, 35)*mobj->scale;
			fixed_t y = P_RandomRange(PR_SMOLDERING, -35, 35)*mobj->scale;
			fixed_t z = P_RandomRange(PR_SMOLDERING, 0, 70)*mobj->scale;
			mobj_t *smoke = P_SpawnMobj(mobj->x + x, mobj->y + y, mobj->z + z, MT_SMOKE);
			P_SetMobjState(smoke, S_OPAQUESMOKE1);
			K_MatchGenericExtraFlags(smoke, mobj);
			smoke->scale = mobj->scale * 2;
			smoke->destscale = mobj->scale * 6;
			smoke->momz = P_RandomRange(PR_SMOLDERING, 4, 9)*FRACUNIT*P_MobjFlip(smoke);
		}
		break;
	case MT_BOOMPARTICLE:
		{
			fixed_t x = P_RandomRange(PR_EXPLOSION, -16, 16)*mobj->scale;
			fixed_t y = P_RandomRange(PR_EXPLOSION, -16, 16)*mobj->scale;
			fixed_t z = P_RandomRange(PR_EXPLOSION, 0, 32)*mobj->scale*P_MobjFlip(mobj);
			if (leveltime % 2 == 0)
			{
				mobj_t *smoke = P_SpawnMobj(mobj->x + x, mobj->y + y, mobj->z + z, MT_BOSSEXPLODE);
				K_MatchGenericExtraFlags(smoke, mobj);
				P_SetMobjState(smoke, S_QUICKBOOM1);
				smoke->scale = mobj->scale/2;
				smoke->destscale = mobj->scale;
				smoke->color = mobj->color;
			}
			else
			{
				mobj_t *smoke = P_SpawnMobj(mobj->x + x, mobj->y + y, mobj->z + z, MT_SMOKE);
				P_SetMobjState(smoke, S_OPAQUESMOKE1);
				K_MatchGenericExtraFlags(smoke, mobj);
				smoke->scale = mobj->scale;
				smoke->destscale = mobj->scale*2;
			}
			if (mobj->tics <= TICRATE)
			{
				mobj->destscale = FixedDiv(mobj->scale, 100*FRACUNIT);
			}
		}
		break;
	case MT_BATTLEBUMPER:
		if (mobj->target && !P_MobjWasRemoved(mobj->target) && mobj->target->player
			&& mobj->target->health > 0 && !mobj->target->player->spectator)
		{
			fixed_t rad = 32*mobj->target->scale;
			fixed_t offz;
			angle_t ang, diff;

			if (!((mobj->target->player-players) & 1))
				ang = -FixedAngle(mobj->info->speed);
			else
				ang = FixedAngle(mobj->info->speed);

			if (mobj->target->player->bumper <= 1)
				diff = 0;
			else
				diff = FixedAngle(360*FRACUNIT/mobj->target->player->bumper);

			ang = (ang*leveltime) + (diff * (mobj->threshold-1));

			// If the player is on the ceiling, then flip your items as well.
			if (mobj->target->eflags & MFE_VERTICALFLIP)
			{
				mobj->eflags |= MFE_VERTICALFLIP;
				offz = mobj->target->height / 2;
			}
			else
			{
				mobj->eflags &= ~MFE_VERTICALFLIP;
				offz = mobj->target->height / 5;
			}

			mobj->renderflags = (mobj->target->renderflags & RF_DONTDRAW);

			if (mobj->target->eflags & MFE_VERTICALFLIP)
			{
				offz += 4*FRACUNIT;
			}
			else
			{
				offz -= 4*FRACUNIT;
			}

			if (mobj->tracer && !P_MobjWasRemoved(mobj->tracer) && mobj->tracer->player
				&& mobj->tracer->health > 0 && !mobj->tracer->player->spectator) // STOLEN
			{
				mobj->color = mobj->tracer->color;
			}
			else
			{
				mobj->color = mobj->target->color;
			}

			if (mobj->target->player->bumper < 2)
				P_SetMobjState(mobj, S_BATTLEBUMPER3);
			else if (mobj->target->player->bumper < 3)
				P_SetMobjState(mobj, S_BATTLEBUMPER2);
			else
				P_SetMobjState(mobj, S_BATTLEBUMPER1);

			// Shrink your items if the player shrunk too.
			P_SetScale(mobj, mobj->target->scale);

			P_UnsetThingPosition(mobj);
			{
				const angle_t fa = ang >> ANGLETOFINESHIFT;
				mobj->x = mobj->target->x + FixedMul(FINECOSINE(fa), rad);
				mobj->y = mobj->target->y + FixedMul(FINESINE(fa), rad);
				mobj->z = mobj->target->z + offz;
				P_SetThingPosition(mobj);
			}

			// Was this so hard?
			if (mobj->target->player->bumper <= mobj->threshold)
			{
				P_RemoveMobj(mobj);
				return;
			}
		}
		else if ((mobj->health > 0
			&& (!mobj->target || !mobj->target->player || !mobj->target->player->mo || mobj->target->health <= 0 || mobj->target->player->spectator))
			|| (mobj->health <= 0 && P_IsObjectOnGround(mobj))
			|| P_CheckDeathPitCollide(mobj)) // When in death state
		{
			P_RemoveMobj(mobj);
			return;
		}

		break;

	// see also K_drawKartItem in k_hud.c
	case MT_PLAYERARROW:
		if (mobj->target && mobj->target->health
			&& mobj->target->player && !mobj->target->player->spectator
			&& mobj->target->health && mobj->target->player->playerstate != PST_DEAD)
		{
			fixed_t scale = 3*mobj->target->scale;
			mobj->color = mobj->target->color;
			K_MatchGenericExtraFlags(mobj, mobj->target);

			if ((!(gametypes[gametype]->rules & GTR_ITEMARROWS) || (gametypes[gametype]->rules & GTR_BUMPERS && mobj->target->player->bumper <= 0))
#if 1 // Set to 0 to test without needing to host
				|| (P_IsDisplayPlayer(mobj->target->player))
#endif
				)
				mobj->renderflags |= RF_DONTDRAW;

			P_UnsetThingPosition(mobj);
			mobj->x = mobj->target->x;
			mobj->y = mobj->target->y;

			mobj->angle = R_PointToAngle(mobj->x, mobj->y) + ANGLE_90; // literally only happened because i wanted to ^L^R the SPR_ITEM's

			if (!r_splitscreen && players[displayplayers[0]].mo)
			{
				scale = mobj->target->scale + FixedMul(FixedDiv(abs(P_AproxDistance(players[displayplayers[0]].mo->x-mobj->target->x,
					players[displayplayers[0]].mo->y-mobj->target->y)), RING_DIST), mobj->target->scale);
				if (scale > 16*mobj->target->scale)
					scale = 16*mobj->target->scale;
			}
			mobj->destscale = scale;

			if (!(mobj->target->eflags & MFE_VERTICALFLIP))
			{
				mobj->z = mobj->target->z + mobj->target->height + (16*mobj->target->scale);
				mobj->eflags &= ~MFE_VERTICALFLIP;
			}
			else
			{
				mobj->z = mobj->target->z - mobj->target->height - (16*mobj->target->scale);
				mobj->eflags |= MFE_VERTICALFLIP;
			}
			P_SetThingPosition(mobj);

			if (!mobj->tracer)
			{
				mobj_t *overlay = P_SpawnMobj(mobj->x, mobj->y, mobj->z, MT_OVERLAY);
				P_SetTarget(&mobj->tracer, overlay);
				P_SetTarget(&mobj->tracer->target, mobj);
				P_SetMobjState(mobj->tracer, S_PLAYERARROW_ITEM);
				P_SetScale(mobj->tracer, (mobj->tracer->destscale = mobj->scale));
			}

			// Do this in an easy way
			if (mobj->target->player->itemroulette)
			{
				mobj->tracer->color = mobj->target->player->skincolor;
				mobj->tracer->colorized = true;
			}
			else
			{
				mobj->tracer->color = SKINCOLOR_NONE;
				mobj->tracer->colorized = false;
			}

			if (!(mobj->renderflags & RF_DONTDRAW))
			{
				statenum_t statenum = S_PLAYERARROW_BOX;
				kartitemtype_e itemtype = mobj->target->player->itemtype;
				uint8_t itemamount = 0;
				dboolean hide = false;

				// Set it to use the correct states for its condition
				if (mobj->target->player->itemroulette)
				{
					itemtype = K_GetRollingRouletteItem(mobj->target->player);
				}
				else if (mobj->target->player->stealingtimer < 0)
				{
					itemtype = KITEM_HYUDORO;
					hide = leveltime & 2;
				}
				else if ((mobj->target->player->stealingtimer > 0) && (leveltime & 2))
				{
					itemtype = KITEM_HYUDORO;
				}
				else if (mobj->target->player->eggmanexplode > 1)
				{
					itemtype = KITEM_EGGMAN;
					hide = leveltime & 1;
				}
				else if (mobj->target->player->rocketsneakertimer > 1)
				{
					//itembar = mobj->target->player->rocketsneakertimer; -- not today satan
					itemtype = KITEM_ROCKETSNEAKER;
					hide = leveltime & 1;
				}
				else if (mobj->target->player->flametimer > 1)
				{
					//itembar = mobj->target->player->flametimer; -- not today satan
					itemtype = KITEM_FLAMESHIELD;
					hide = leveltime & 1;
				}
				else if (mobj->target->player->growshrinktimer > 0)
				{
					itemtype = KITEM_GROW;
					hide = leveltime & 1;
				}
				else if (mobj->target->player->itemtype && mobj->target->player->itemamount > 0)
				{
					itemtype = mobj->target->player->itemtype;
					itemamount = mobj->target->player->itemamount;
					hide = mobj->target->player->itemflags & IF_ITEMOUT && leveltime & 1;
				}
				else
				{
					statenum = S_PLAYERARROW;
				}

				P_SetMobjState(mobj, statenum);
				if (statenum == S_PLAYERARROW_BOX)
				{
					mobj->tracer->sprite = SPR_ITEM;
					K_UpdateMobjItemOverlay(mobj->tracer, itemtype, itemamount);
					mobj->tracer->threshold = itemtype; // sorry, i have to
					mobj->tracer->frame &= ~FF_PAPERSPRITE;
					if (hide)
						mobj->tracer->renderflags |= RF_DONTDRAW;
					else
						mobj->tracer->renderflags &= ~RF_DONTDRAW;
				}
				else
				{
					P_SetMobjState(mobj->tracer, S_PLAYERARROW_ITEM);
					mobj->tracer->renderflags &= ~RF_DONTDRAW;
				}

				mobj->tracer->destscale = scale;

				if (mobj->target->player->itemamount >= K_GetItemNumberDisplayMin(mobj->target->player->itemtype, false)
					&& mobj->target->player->itemamount <= 10) // Meh, too difficult to support greater than this; convert this to a decent HUD object and then maybe :V
				{
					mobj_t *number = P_SpawnMobj(mobj->x, mobj->y, mobj->z, MT_OVERLAY);
					mobj_t *numx = P_SpawnMobj(mobj->x, mobj->y, mobj->z, MT_OVERLAY);

					P_SetTarget(&number->target, mobj);
					P_SetMobjState(number, S_PLAYERARROW_NUMBER);
					P_SetScale(number, mobj->scale);
					number->destscale = scale;
					number->frame = FF_FULLBRIGHT|(mobj->target->player->itemamount);

					P_SetTarget(&numx->target, mobj);
					P_SetMobjState(numx, S_PLAYERARROW_X);
					P_SetScale(numx, mobj->scale);
					numx->destscale = scale;

					// ugh... overlays....
					number->old_z = number->z += FixedMul(number->state->var2 * FRACUNIT, mobj->scale);
					numx->old_z = numx->z += FixedMul(number->state->var2 * FRACUNIT, mobj->scale);
				}

				if (K_IsPlayerWanted(mobj->target->player) && mobj->movecount != 1)
				{
					mobj_t *wanted = P_SpawnMobj(mobj->x, mobj->y, mobj->z, MT_PLAYERWANTED);
					P_SetTarget(&wanted->target, mobj->target);
					P_SetTarget(&wanted->tracer, mobj);
					P_SetScale(wanted, mobj->scale);
					wanted->destscale = scale;
					mobj->movecount = 1;
				}
				else if (!K_IsPlayerWanted(mobj->target->player))
					mobj->movecount = 0;
			}
			else
				mobj->tracer->renderflags |= RF_DONTDRAW;
		}
		else if (mobj->health > 0)
		{
			P_KillMobj(mobj, NULL, NULL, DMG_NORMAL);
			return;
		}
		break;
	case MT_PLAYERWANTED:
		if (mobj->target && mobj->target->health && mobj->tracer
			&& mobj->target->player && !mobj->target->player->spectator
			&& mobj->target->health && mobj->target->player->playerstate != PST_DEAD
			&& players[g_localplayers[0]].mo && !players[g_localplayers[0]].spectator)
		{
			fixed_t scale = 3*mobj->target->scale;

			if (!K_IsPlayerWanted(mobj->target->player))
			{
				mobj->tracer->movecount = 0;
				P_RemoveMobj(mobj);
				return;
			}

			if (mobj->tracer->renderflags & RF_DONTDRAW)
				mobj->renderflags |= RF_DONTDRAW;
			else
				mobj->renderflags &= ~RF_DONTDRAW;

			P_UnsetThingPosition(mobj);
			mobj->x = mobj->target->x;
			mobj->y = mobj->target->y;

			if (!r_splitscreen && players[displayplayers[0]].mo)
			{
				scale = mobj->target->scale + FixedMul(FixedDiv(abs(P_AproxDistance(players[displayplayers[0]].mo->x-mobj->target->x,
					players[displayplayers[0]].mo->y-mobj->target->y)), RING_DIST), mobj->target->scale);
				if (scale > 16*mobj->target->scale)
					scale = 16*mobj->target->scale;
			}
			mobj->destscale = scale;

			if (!(mobj->target->eflags & MFE_VERTICALFLIP))
			{
				mobj->z = mobj->target->z + mobj->target->height + (16*mobj->target->scale) + (64*scale);
				mobj->eflags &= ~MFE_VERTICALFLIP;
			}
			else
			{
				mobj->z = mobj->target->z - mobj->target->height - (16*mobj->target->scale) - (64*scale);
				mobj->eflags |= MFE_VERTICALFLIP;
			}
			P_SetThingPosition(mobj);
		}
		else if (mobj->health > 0)
		{
			P_KillMobj(mobj, NULL, NULL, DMG_NORMAL);
			return;
		}
		break;
	case MT_PETSMOKER:
		if (!(leveltime % 10))
		{
			mobj_t *smok = P_SpawnMobj(mobj->x, mobj->y, mobj->z, MT_PETSMOKE);
			if (mobj->args[0])
				P_SetMobjStateNF(smok, smok->info->painstate); // same function, diff sprite
		}
		break;
	case MT_ITEMCAPSULE_PART:
		P_ItemCapsulePartThinker(mobj);

		if (P_MobjWasRemoved(mobj))
			return;
		break;
	case MT_SCRIPT_THING:
	{
		if (mobj->spawnpoint->args[2] != 0)
		{
			// turned off
			break;
		}

		uint8_t i;
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (playeringame[i] == false)
			{
				continue;
			}

			player_t *player = &players[i];
			if (P_MobjWasRemoved(player->mo) == true)
			{
				continue;
			}

			fixed_t dist = R_PointToDist2(
				mobj->x, mobj->y,
				player->mo->x, player->mo->y
			);

			if (dist < mobj->spawnpoint->args[0] * FRACUNIT)
			{
				P_ActivateThingSpecial(mobj, player->mo);

				if (mobj->spawnpoint->args[1] == 0)
				{
					P_RemoveMobj(mobj);
					return;
				}

				break;
			}
		}

		break;
	}
	/* FALLTHRU */
	default:
	dofuse:
		if (mobj->fuse)
		{ // Scenery object fuse! Very basic!
			mobj->fuse--;
			if (!mobj->fuse)
			{
				if (!LUA_HookMobj(mobj, MOBJ_HOOK(MobjFuse)))
					P_RemoveMobj(mobj);
				return;
			}
		}
		break;
	}

	P_SceneryThinker(mobj);
}

static dboolean P_MobjPushableThink(mobj_t *mobj)
{
	P_MobjCheckWater(mobj);
	P_PushableThinker(mobj);

	// Extinguish fire objects in water. (Yes, it's extraordinarily rare to have a pushable flame object, but Brak uses such a case.)
	if (mapnamespace != MNS_RINGRACERS
		&& mobj->flags & MF_FIRE
		&& (mapnamespace == MNS_SRB2KART ? mobj->type != MT_PUMA && mobj->type != MT_FIREBALL : !(mobj->eflags & MFE_TOUCHLAVA))
		&& (mobj->eflags & (MFE_UNDERWATER | MFE_TOUCHWATER)))
	{
		P_KillMobj(mobj, NULL, NULL, 0);
		return false;
	}

	return true;
}

static dboolean P_MobjBossThink(mobj_t *mobj)
{
	if (LUA_HookMobj(mobj, MOBJ_HOOK(BossThinker)))
	{
		if (P_MobjWasRemoved(mobj))
			return false;
	}
	else if (P_MobjWasRemoved(mobj))
		return false;
	else
		switch (mobj->type)
		{
		case MT_EGGMOBILE:
			if (mobj->health < mobj->info->damage+1 && leveltime & 1 && mobj->health > 0)
				P_SpawnMobj(mobj->x, mobj->y, mobj->z, MT_SMOKE);
			if (mobj->flags2 & MF2_SKULLFLY)
				P_SpawnGhostMobj(mobj);
			P_Boss1Thinker(mobj);
			break;
		case MT_EGGMOBILE2:
			P_Boss2Thinker(mobj);
			break;
		case MT_EGGMOBILE3:
			P_Boss3Thinker(mobj);
			break;
		case MT_EGGMOBILE4:
			P_Boss4Thinker(mobj);
			break;
		case MT_BLACKEGGMAN:
			P_Boss7Thinker(mobj);
			break;
		case MT_METALSONIC_BATTLE:
			P_Boss9Thinker(mobj);
			break;
		// No SRB2Kart bosses... yet :)
		default: // Generic SOC-made boss
			if (mobj->flags2 & MF2_SKULLFLY)
				P_SpawnGhostMobj(mobj);
			P_GenericBossThinker(mobj);
			break;
		}
	if (mobj->flags2 & MF2_BOSSFLEE)
	{
		if (mobj->extravalue1)
		{
			if (!(--mobj->extravalue1))
			{
				if (mobj->target)
				{
					mobj->momz = FixedMul(FixedDiv(mobj->target->z - mobj->z, P_AproxDistance(mobj->x - mobj->target->x, mobj->y - mobj->target->y)), mobj->scale << 1);
					mobj->angle = R_PointToAngle2(mobj->x, mobj->y, mobj->target->x, mobj->target->y);
				}
				else
					mobj->momz = 8*mobj->scale;
			}
			else
				mobj->angle += mobj->movedir;
		}
		else if (mobj->target)
			P_InstaThrust(mobj, mobj->angle, FixedMul(12*FRACUNIT, mobj->scale));
	}
	return true;
}

static dboolean P_MobjDeadThink(mobj_t *mobj)
{
	switch (mobj->type)
	{
	case MT_BLUEBALL:
		if ((mobj->tics>>2)+1 > 0 && (mobj->tics>>2)+1 <= tr_trans60) // tr_trans50 through tr_trans90, shifting once every second frame
			mobj->frame = (NUMTRANSMAPS-((mobj->tics>>2)+1))<<FF_TRANSSHIFT;
		else // tr_trans60 otherwise
			mobj->frame = tr_trans60<<FF_TRANSSHIFT;
		break;
	case MT_PLAYER:
		/// \todo Have the player's dead body completely finish its animation even if they've already respawned.

		if (!mobj->fuse)
		{ // Go away.
		  /// \todo Actually go ahead and remove mobj completely, and fix any bugs and crashes doing this creates. Chasecam should stop moving, and F12 should never return to it.
			mobj->momz = 0;

			if (mobj->player)
			{
				mobj->renderflags |= RF_DONTDRAW;
			}
			else // safe to remove, nobody's going to complain!
			{
				P_RemoveMobj(mobj);
				return false;
			}
		}
		// Apply gravity to fall downwards.
		else if (mobj->player == NULL || skins[mobj->player->skin]->flags & SF_OLDDEATH)
		{
			P_SetObjectMomZ(mobj, -3*FRACUNIT/2, true);
			if (mobj->player != NULL)
				mobj->player->drawangle -= ANGLE_22h;
		}
		else
		{
			P_SetObjectMomZ(mobj, -7*FRACUNIT/8, true);
		}
		break;
	case MT_KART_LEFTOVER:
		mobj->angle += ANG15;
		P_SetObjectMomZ(mobj, -2*FRACUNIT/3, true);
		break;
	case MT_METALSONIC_RACE:
	{
		if (!(mobj->fuse % 8))
		{
			fixed_t r = mobj->radius >> FRACBITS;

			fixed_t rand_x;
			fixed_t rand_y;
			fixed_t rand_z;

			rand_z = mobj->z + (P_RandomKey(PR_EXPLOSION, mobj->height >> FRACBITS) << FRACBITS);
			rand_y = mobj->y + (P_RandomRange(PR_EXPLOSION, r, -r) << FRACBITS);
			rand_x = mobj->x + (P_RandomRange(PR_EXPLOSION, r, -r) << FRACBITS);

			mobj_t *explosion = P_SpawnMobj(rand_x, rand_y, rand_z, MT_SONIC3KBOSSEXPLODE);
			S_StartSound(explosion, sfx_s3kb4);
		}
		P_SetObjectMomZ(mobj, -2*FRACUNIT/3, true);
	}
	break;
	case MT_ORBINAUT:
	case MT_BANANA:
	case MT_EGGMANITEM:
	case MT_LANDMINE:
	case MT_EGGMINE:
	case MT_SPB:
		if (P_IsObjectOnGround(mobj))
		{
			P_RemoveMobj(mobj);
			return false;
		}
		/* FALLTHRU */
	case MT_ORBINAUT_SHIELD:
	case MT_EGGMINE_SHIELD:
	case MT_BANANA_SHIELD:
	case MT_EGGMANITEM_SHIELD:
		mobj->renderflags ^= RF_DONTDRAW;
		break;
	case MT_JAWZ:
	case MT_JAWZ_DUD:
		if (P_IsObjectOnGround(mobj))
			P_SetMobjState(mobj, mobj->info->xdeathstate);
		/* FALLTHRU */
	case MT_JAWZ_SHIELD:
		mobj->renderflags ^= RF_DONTDRAW;
		break;
	case MT_SSMINE:
	case MT_SPBEXPLOSION:
		if (mobj->extravalue2 != -100)
		{
			P_SetMobjState(mobj, mobj->info->deathstate);
			mobj->extravalue2 = -100;
		}
		else
		{
			P_RemoveMobj(mobj);
			return false;
		}
		break;
	case MT_MINEEXPLOSIONSOUND:
		P_RemoveMobj(mobj);
		return false;
	case MT_CDUFO:
		if (mobj->fuse > TICRATE)
			mobj->renderflags ^= RF_DONTDRAW; // only by good fortune does this end with it having RF_DONTDRAW... don't touch!
		break;
	case MT_SMK_PIPE:
		if (mobj->flags2 & MF2_AMBUSH)
			P_SetMobjStateNF(mobj, mobj->info->seestate);
		else
			P_SetMobjStateNF(mobj, mobj->info->spawnstate);
		/* FALLTHRU */
	case MT_SMK_MOLE:
		mobj->renderflags ^= RF_DONTDRAW;
		if (P_IsObjectOnGround(mobj))
		{
			P_RemoveMobj(mobj);
			return false;
		}
		break;
	case MT_SMK_THWOMP:
		if (mobj->flags2 & MF2_AMBUSH)
		{
			mobj->colorized = true;
			mobj->color = K_RainbowColor(leveltime);
			mobj->frame |= FF_FULLBRIGHT;
		}
		else
		{
			mobj->colorized = false;
			mobj->color = SKINCOLOR_NONE;
			mobj->frame &= (~FF_FULLBRIGHT);
		}

		mobj->renderflags ^= RF_DONTDRAW;
		if (P_IsObjectOnGround(mobj))
		{
			P_RemoveMobj(mobj);
			return false;
		}
		break;
	default:
		break;
	}
	return true;
}

// Angle-to-tracer to trigger a linedef exec
// See Linedef Exec 457 (Track mobj angle to point)
static void P_TracerAngleThink(mobj_t *mobj)
{
	angle_t looking;
	angle_t ang;

	if (!mobj->tracer)
		return;

	// mobj->lastlook - Don't disable behavior after first failure
	// mobj->extravalue1 - Angle tolerance
	// mobj->cvval - Allowable failure delay
	// mobj->cvmem - Failure timer

	if (mobj->player)
		looking = mobj->player->angleturn;
	else
		looking = mobj->angle;

	ang = looking - R_PointToAngle2(mobj->x, mobj->y, mobj->tracer->x, mobj->tracer->y);

	// \todo account for distance between mobj and tracer
	// Because closer mobjs can be facing beyond the angle tolerance
	// yet tracer is still in the camera view

	// failure state: mobj is not facing tracer
	// Reasaonable defaults: ANGLE_67h, ANGLE_292h
	if (ang >= (angle_t)mobj->extravalue1 && ang <= ANGLE_MAX - (angle_t)mobj->extravalue1)
	{
		if (mobj->cvmem)
			mobj->cvmem--;
		else
		{
			if (mobj->lastlook)
				mobj->cvmem = mobj->cusval; // reset timer for next failure
			else
			{
				// disable after first failure
				mobj->eflags &= ~MFE_TRACERANGLE;
				mobj->lastlook = mobj->extravalue1 = mobj->extravalue2 = mobj->cvmem = mobj->cusval = 0;
			}

			P_ActivateThingSpecial(mobj->tracer, mobj);
		}
	}
	else
		mobj->cvmem = mobj->cusval; // reset failure timer
}

static void P_CleanTrapBubble(mobj_t *trapbbl)
{
	if (P_MobjWasRemoved(trapbbl))
		return; // *loud coughing*

	// Reset movecount time limit
	trapbbl->movecount = 0;

	if (!P_MobjWasRemoved(trapbbl->tracer))
	{
		// Remove our tracer, since SpawnFreshCopy will preserve that
		// Don't want players getting trapped all over again, now do we?
		P_SetTarget(&trapbbl->tracer, NULL);
	}

	if (!P_MobjWasRemoved(trapbbl->target))
	{
		// Same for targets
		P_SetTarget(&trapbbl->target, NULL);
	}
}

static void P_TrapBubbleThink(mobj_t* mobj)
{
	if ((leveltime % 180 == 0) && (!mobj->extravalue3))
		S_StartSound(mobj, sfx_s3kbfl);

	if (mobj->tracer && !P_MobjWasRemoved(mobj->tracer) && mobj->tracer->player)
	{
		player_t* player = mobj->tracer->player;
		fixed_t destx, desty, curfz, destfz;
		dboolean blockmove = false;

		mobj->flags =
		    MF_NOCLIP | MF_NOCLIPHEIGHT | MF_NOCLIPTHING | MF_NOGRAVITY | MF_DONTENCOREMAP;
		mobj->extravalue1 = 1;

		mobj->cvmem /= 2;
		mobj->momz = 0;
		mobj->destscale = ((5 * mobj->tracer->scale) >> 2) + (mobj->tracer->scale >> 3);

		fixed_t extrarise = 0;
		extrarise = (fixed_t)(mobj->script_args[1]);

		// Rise really fast!
		fixed_t risespeed = 16 + extrarise;

		mobj->tracer->momz = (risespeed * max(mapobjectscale, mobj->tracer->scale)) *
				     P_MobjFlip(mobj->tracer);

		const dboolean slowme = ((mobj->script_args[3] & 1) == 1);

		if (slowme)
		{
			const fixed_t min_speed = (mobj->script_args[4] * FRACUNIT);
			fixed_t my_speed = K_Momentum2D(mobj->tracer);

			if (my_speed != min_speed)
			{
				fixed_t slow_delta = R_PointToDist2((31 * mobj->tracer->momx) / 32,
								    (31 * mobj->tracer->momy) / 32,
								    mobj->tracer->momx,
								    mobj->tracer->momy);

				my_speed = ApproachOneWayFixed(my_speed, min_speed, slow_delta);

				P_InstaThrust(mobj->tracer, mobj->angle, my_speed);
			}
		}

		destx = mobj->x + mobj->tracer->momx;
		desty = mobj->y + mobj->tracer->momy;

		if (mobj->tracer->eflags & MFE_VERTICALFLIP)
		{
			curfz = P_GetCeilingZ(mobj->tracer,
					      mobj->tracer->subsector->sector,
					      mobj->tracer->x,
					      mobj->tracer->y,
					      NULL);
			destfz = P_GetCeilingZ(mobj->tracer,
					       R_PointInSubsector(destx, desty)->sector,
					       destx,
					       desty,
					       NULL);
			blockmove = (curfz - destfz >= 24 * mobj->scale);
		}
		else
		{
			curfz = P_GetFloorZ(mobj->tracer,
					    mobj->tracer->subsector->sector,
					    mobj->tracer->x,
					    mobj->tracer->y,
					    NULL);
			destfz = P_GetFloorZ(mobj->tracer,
					     R_PointInSubsector(destx, desty)->sector,
					     destx,
					     desty,
					     NULL);
			blockmove = (destfz - curfz >= 24 * mobj->scale);
		}

		if (blockmove)
		{
			mobj->tracer->momx = mobj->tracer->momy = 0;
		}

		P_MoveOrigin(mobj,
			     mobj->tracer->x + P_ReturnThrustX(NULL,
							       mobj->tracer->angle + ANGLE_90,
							       (mobj->cvmem) << FRACBITS),
			     mobj->tracer->y + P_ReturnThrustY(NULL,
							       mobj->tracer->angle + ANGLE_90,
							       (mobj->cvmem) << FRACBITS),
			     mobj->tracer->z - (4 * mobj->tracer->scale) +
				 (P_RandomRange(PR_ITEM_BUBBLE, -abs(mobj->cvmem), abs(mobj->cvmem))
				  << FRACBITS));

		if (P_PlayerInPain(player))
		{
			// If the player is feeling pain, give em a break. tick their spinout timer
			// regardless
			player->spinouttype |= KSPIN_AIRTIMER;
		}

		if (mobj->movecount > 5 * TICRATE)
		{
			S_StartSound(mobj->tracer, sfx_s3k77);
			mobj->tracer->flags &= ~MF_NOGRAVITY;
			P_KillMobj(mobj, mobj->tracer, mobj->tracer, DMG_NORMAL);

			P_CleanTrapBubble(mobj);

			mobj->flags |= (MF_NOCLIP | MF_NOCLIPTHING);
			mobj->extravalue3 = 1;

			return;
		}
		else
		{
			// Check if the player hits Accel; if they have, pop and launch 'em out with
			// a boost!
			// FIXME: A "HIT THE GAS!" prompt to tell players what to do in the moment
			uint16_t buttons = K_GetKartButtons(player);
			dboolean pressing_accel = ((buttons & BT_ACCELERATE) &&
						   !(player->oldcmd.buttons & BT_ACCELERATE));

			if (pressing_accel)
			{
				// Play the sound of us popping, first
				S_StartSound(mobj->tracer, sfx_s3k77);

				// Have the player announce their boost!
				S_StartSoundAtVolume(mobj->tracer, sfx_cdfm57, 170);
				K_PlayBoostTaunt(mobj->tracer);

				const fixed_t speedscalar = K_GetKartGameSpeedScalar(gamespeed);

				// Unlike the normal Bubble Shield boost, this sould fling you at a
				// constant midair speed
				P_InstaThrust(mobj->tracer,
					      mobj->tracer->angle,
					      FixedMul(45 * mobj->tracer->scale, speedscalar));

				// Just in case...
				player->pogospring = 1;

				// Set generic popboost values
				// EVER SO SLIGHTLY longer, in case you're in the air for a while
				player->bubbleboost = 3 * BUBBLEBOOSTTIME / 2;
				player->flashing = 6 * TICRATE / 7;

				// Finally, exit stage left
				mobj->tracer->flags &= ~MF_NOGRAVITY;
				P_KillMobj(mobj, mobj->tracer, mobj->tracer, DMG_NORMAL);

				// Clean up after ourselves
				P_CleanTrapBubble(mobj);

				mobj->flags |= (MF_NOCLIP | MF_NOCLIPTHING);
				mobj->extravalue3 = 1;

				return;
			}
		}

		mobj->movecount++;
	}
	else if (mobj->extravalue1)  // lost your player somehow, DIE
	{
		P_CleanTrapBubble(mobj);  // ...I'm paranoid, okay?!
		P_KillMobj(mobj, NULL, NULL, DMG_NORMAL);
		mobj->flags |= (MF_NOCLIP | MF_NOCLIPTHING);
		mobj->extravalue3 = 1;
		return;
	}
	else
	{
		mobj->destscale = (5 * mapobjectscale) >> 2;

		if (mobj->threshold > 0)
			mobj->threshold--;

		if (abs(mobj->momx) < 8 * mobj->destscale && abs(mobj->momy) < 8 * mobj->destscale)
		{
			// Stop, give light gravity
			mobj->momx = mobj->momy = 0;

			const dboolean float_down = ((mobj->script_args[2] & 1) == 1);
			if (float_down)
				mobj->momz = -(mobj->scale * P_MobjFlip(mobj));
		}
		else
		{
			uint8_t i;
			mobj_t* ghost = P_SpawnGhostMobj(mobj);

			if (mobj->target && !P_MobjWasRemoved(mobj->target) && mobj->target->player)
			{
				ghost->color = mobj->target->player->skincolor;
				ghost->colorized = true;
			}

			mobj->momx = (23 * mobj->momx) / 24;
			mobj->momy = (23 * mobj->momy) / 24;

			mobj->angle = K_MomentumAngle(mobj);

			if ((mobj->z - mobj->floorz) < (24 * mobj->scale) && (leveltime % 3 != 0))
			{
				// Cool wave effects!
				for (i = 0; i < 2; i++)
				{
					angle_t aoff;
					int8_t sign = 1;
					mobj_t* wave;

					if (i & 1)
						sign = -1;
					else
						sign = 1;

					aoff = (mobj->angle + ANGLE_180) + (ANGLE_45 * sign);

					wave = P_SpawnMobj(
					    mobj->x +
						FixedMul(mobj->radius,
							 FINECOSINE(aoff >> ANGLETOFINESHIFT)),
					    mobj->y + FixedMul(mobj->radius,
							       FINESINE(aoff >> ANGLETOFINESHIFT)),
					    mobj->z,
					    MT_THOK);

					wave->colorized = true;
					wave->color = SKINCOLOR_PERIWINKLE;
					wave->flags &= ~(MF_NOCLIPHEIGHT | MF_NOGRAVITY);
					P_SetScale(wave, (wave->destscale = mobj->scale / 2));

					P_SetMobjState(wave, S_SPLISH1);
					if (leveltime & 1)
						wave->tics++;

					P_SetTarget(&wave->target, mobj);

					// point completely perpendicular from the bubble
					wave->angle =
					    mobj->angle -
					    (ANGLE_90 * sign);

					K_FlipFromObject(wave, mobj);

					P_Thrust(wave, wave->angle, 4 * mobj->scale);
				}
			}
		}
	}
}

static dboolean P_MobjRegularThink(mobj_t *mobj)
{
	if ((mobj->flags & MF_ENEMY) && (mobj->state->nextstate == mobj->info->spawnstate && mobj->tics == 1))
		mobj->flags2 &= ~MF2_FRET;

	if (mobj->eflags & MFE_TRACERANGLE)
		P_TracerAngleThink(mobj);

	switch (mobj->type)
	{
	case MT_WALLSPIKEBASE:
		if (!mobj->target) {
			P_RemoveMobj(mobj);
			return false;
		}
		mobj->frame = (mobj->frame & ~FF_FRAMEMASK)|(mobj->target->frame & FF_FRAMEMASK);
#if 0
		if (mobj->angle != mobj->target->angle + ANGLE_90) // reposition if not the correct angle
		{
			mobj_t* target = mobj->target; // shortcut
			const fixed_t baseradius = target->radius - (target->scale/2); //FixedMul(FRACUNIT/2, target->scale);
			P_UnsetThingPosition(mobj);
			mobj->x = target->x - P_ReturnThrustX(target, target->angle, baseradius);
			mobj->y = target->y - P_ReturnThrustY(target, target->angle, baseradius);
			P_SetThingPosition(mobj);
			mobj->angle = target->angle + ANGLE_90;
		}
#endif
		break;
	case MT_FALLINGROCK:
		// Despawn rocks here in case zmovement code can't do so (blame slopes)
		if (!mobj->fuse && !mobj->momx && !mobj->momy && !mobj->momz
			&& ((mobj->eflags & MFE_VERTICALFLIP) ?
			mobj->z + mobj->height >= mobj->ceilingz
			: mobj->z <= mobj->floorz))
		{
			mobj->fuse = TICRATE;
		}
		P_MobjCheckWater(mobj);
		break;
	case MT_AQUABUZZ:
	case MT_BIGAIRMINE:
		{
			if (mobj->tracer && mobj->tracer->player && mobj->tracer->health > 0
				&& P_AproxDistance(P_AproxDistance(mobj->tracer->x - mobj->x, mobj->tracer->y - mobj->y), mobj->tracer->z - mobj->z) <= mobj->radius * 16)
			{
				// Home in on the target.
				P_HomingAttack(mobj, mobj->tracer);

				if (mobj->z < mobj->floorz)
					mobj->z = mobj->floorz;

				if (leveltime % mobj->info->painchance == 0)
					S_StartSound(mobj, mobj->info->activesound);
			}
			else
			{
				// Try to find a player
				P_LookForPlayers(mobj, true, true, mobj->radius * 16);
				mobj->momx >>= 1;
				mobj->momy >>= 1;
				mobj->momz >>= 1;
			}
		}
		break;
	case MT_BIGMINE:
		{
			if (mobj->tracer && mobj->tracer->player && mobj->tracer->health > 0
				&& P_AproxDistance(P_AproxDistance(mobj->tracer->x - mobj->x, mobj->tracer->y - mobj->y), mobj->tracer->z - mobj->z) <= mobj->radius * 16)
			{
				P_MobjCheckWater(mobj);

				// Home in on the target.
				P_HomingAttack(mobj, mobj->tracer);

				// Don't let it go out of water
				if (mobj->z + mobj->height > mobj->watertop)
					mobj->z = mobj->watertop - mobj->height;

				if (mobj->z < mobj->floorz)
					mobj->z = mobj->floorz;

				if (leveltime % mobj->info->painchance == 0)
					S_StartSound(mobj, mobj->info->activesound);
			}
			else
			{
				// Try to find a player
				P_LookForPlayers(mobj, true, true, mobj->radius * 16);
				mobj->momx >>= 1;
				mobj->momy >>= 1;
				mobj->momz >>= 1;
			}
		}
		break;
	case MT_FLAME:
		if (mobj->flags2 & MF2_BOSSNOTRAP)
		{
			if (!mobj->target || P_MobjWasRemoved(mobj->target))
			{
				if (mobj->tracer && !P_MobjWasRemoved(mobj->tracer))
					P_RemoveMobj(mobj->tracer);
				P_RemoveMobj(mobj);
				return false;
			}
			mobj->z = mobj->target->z + mobj->target->momz;
			if (!(mobj->eflags & MFE_VERTICALFLIP))
				mobj->z += mobj->target->height;
		}
		if (mobj->tracer && !P_MobjWasRemoved(mobj->tracer))
		{
			mobj->tracer->z = mobj->z + P_MobjFlip(mobj)*20*mobj->scale;
			if (mobj->eflags & MFE_VERTICALFLIP)
				mobj->tracer->z += mobj->height;
		}
		break;
	case MT_NIGHTSCORE:
			mobj->color = (uint16_t)(leveltime % SKINCOLOR_WHITE);
			break;
	case MT_JETFUME1:
		{
			fixed_t jetx, jety;

			if (!mobj->target // if you have no target
			|| (!(mobj->target->flags & MF_BOSS) && mobj->target->health <= 0)) // or your target isn't a boss and it's popped now
			{ // then remove yourself as well!
				P_RemoveMobj(mobj);
				return false;
			}

			jetx = mobj->target->x + P_ReturnThrustX(mobj->target, mobj->target->angle, FixedMul(-64*FRACUNIT, mobj->target->scale));
			jety = mobj->target->y + P_ReturnThrustY(mobj->target, mobj->target->angle, FixedMul(-64*FRACUNIT, mobj->target->scale));

			if (mobj->fuse == 56) // First one
			{
				P_UnsetThingPosition(mobj);
				mobj->x = jetx;
				mobj->y = jety;
				if (mobj->target->eflags & MFE_VERTICALFLIP)
					mobj->z = mobj->target->z + mobj->target->height - mobj->height - FixedMul(38*FRACUNIT, mobj->target->scale);
				else
					mobj->z = mobj->target->z + FixedMul(38*FRACUNIT, mobj->target->scale);
				mobj->floorz = mobj->z;
				mobj->ceilingz = mobj->z+mobj->height;
				P_SetThingPosition(mobj);
			}
			else if (mobj->fuse == 57)
			{
				P_UnsetThingPosition(mobj);
				mobj->x = jetx + P_ReturnThrustX(mobj->target, mobj->target->angle-ANGLE_90, FixedMul(24*FRACUNIT, mobj->target->scale));
				mobj->y = jety + P_ReturnThrustY(mobj->target, mobj->target->angle-ANGLE_90, FixedMul(24*FRACUNIT, mobj->target->scale));
				if (mobj->target->eflags & MFE_VERTICALFLIP)
					mobj->z = mobj->target->z + mobj->target->height - mobj->height - FixedMul(12*FRACUNIT, mobj->target->scale);
				else
					mobj->z = mobj->target->z + FixedMul(12*FRACUNIT, mobj->target->scale);
				mobj->floorz = mobj->z;
				mobj->ceilingz = mobj->z+mobj->height;
				P_SetThingPosition(mobj);
			}
			else if (mobj->fuse == 58)
			{
				P_UnsetThingPosition(mobj);
				mobj->x = jetx + P_ReturnThrustX(mobj->target, mobj->target->angle+ANGLE_90, FixedMul(24*FRACUNIT, mobj->target->scale));
				mobj->y = jety + P_ReturnThrustY(mobj->target, mobj->target->angle+ANGLE_90, FixedMul(24*FRACUNIT, mobj->target->scale));
				if (mobj->target->eflags & MFE_VERTICALFLIP)
					mobj->z = mobj->target->z + mobj->target->height - mobj->height - FixedMul(12*FRACUNIT, mobj->target->scale);
				else
					mobj->z = mobj->target->z + FixedMul(12*FRACUNIT, mobj->target->scale);
				mobj->floorz = mobj->z;
				mobj->ceilingz = mobj->z+mobj->height;
				P_SetThingPosition(mobj);
			}
			else if (mobj->fuse == 59)
			{
				jetx = mobj->target->x + P_ReturnThrustX(mobj->target, mobj->target->angle, -mobj->target->radius);
				jety = mobj->target->y + P_ReturnThrustY(mobj->target, mobj->target->angle, -mobj->target->radius);
				P_UnsetThingPosition(mobj);
				mobj->x = jetx;
				mobj->y = jety;
				if (mobj->target->eflags & MFE_VERTICALFLIP)
					mobj->z = mobj->target->z + mobj->target->height/2 + mobj->height/2;
				else
					mobj->z = mobj->target->z + mobj->target->height/2 - mobj->height/2;
				mobj->floorz = mobj->z;
				mobj->ceilingz = mobj->z+mobj->height;
				P_SetThingPosition(mobj);
			}
			mobj->fuse++;
		}
		break;
	case MT_PROPELLER:
		{
			fixed_t jetx, jety;

			if (!mobj->target // if you have no target
			|| (!(mobj->target->flags & MF_BOSS) && mobj->target->health <= 0)) // or your target isn't a boss and it's popped now
			{ // then remove yourself as well!
				P_RemoveMobj(mobj);
				return false;
			}

			jetx = mobj->target->x + P_ReturnThrustX(mobj->target, mobj->target->angle, FixedMul(-60*FRACUNIT, mobj->target->scale));
			jety = mobj->target->y + P_ReturnThrustY(mobj->target, mobj->target->angle, FixedMul(-60*FRACUNIT, mobj->target->scale));

			P_UnsetThingPosition(mobj);
			mobj->x = jetx;
			mobj->y = jety;
			mobj->z = mobj->target->z + FixedMul(17*FRACUNIT, mobj->target->scale);
			mobj->angle = mobj->target->angle - ANGLE_180;
			mobj->floorz = mobj->z;
			mobj->ceilingz = mobj->z+mobj->height;
			P_SetThingPosition(mobj);
		}
		break;
	case MT_JETFLAME:
		{
			if (!mobj->target // if you have no target
			|| (!(mobj->target->flags & MF_BOSS) && mobj->target->health <= 0)) // or your target isn't a boss and it's popped now
			{ // then remove yourself as well!
				P_RemoveMobj(mobj);
				return false;
			}

			P_UnsetThingPosition(mobj);
			mobj->x = mobj->target->x;
			mobj->y = mobj->target->y;
			mobj->z = mobj->target->z - FixedMul(50*FRACUNIT, mobj->target->scale);
			mobj->floorz = mobj->z;
			mobj->ceilingz = mobj->z+mobj->height;
			P_SetThingPosition(mobj);
		}
		break;
	case MT_PLAYER:
		if (mobj->player)
			P_PlayerMobjThinker(mobj);
		return false;
	case MT_RING:
		if (P_MobjWasRemoved(mobj))
			return false;

		// No need to check water. Who cares?
		P_RingThinker(mobj);

		A_AttractChase(mobj);
		return false;
		// Flung items
	case MT_FLINGRING:
		if (P_MobjWasRemoved(mobj))
			return false;

		A_AttractChase(mobj);
		break;
	case MT_FLOATINGITEM:
	{
		if (mobj->flags & MF_NOCLIPTHING)
		{
			if (P_CheckDeathPitCollide(mobj))
			{
				P_RemoveMobj(mobj);
				return false;
			}
			else if (P_IsObjectOnGround(mobj))
			{
				mobj->momx = 1;
				mobj->momy = 0;
				mobj->flags &= ~MF_NOCLIPTHING;
				mobj->flags |= MF_NOGRAVITY;
			}
		}
		else
		{
			mobj->angle += 2*ANG2;
			if (mobj->flags2 & MF2_NIGHTSPULL)
			{
				if (!mobj->tracer || !mobj->tracer->health
				|| mobj->scale <= mapobjectscale>>4)
				{
					P_RemoveMobj(mobj);
					return false;
				}
				P_Attract(mobj, mobj->tracer, true);
			}
			else
			{
				fixed_t adj = FixedMul(FRACUNIT - FINECOSINE((mobj->movedir>>ANGLETOFINESHIFT) & FINEMASK), (mapobjectscale<<3));
				mobj->movedir += 2*ANG2;
				if (mobj->eflags & MFE_VERTICALFLIP)
					mobj->z = mobj->ceilingz - mobj->height - adj;
				else
					mobj->z = mobj->floorz + adj;
			}
		}

		K_UpdateMobjItemOverlay(mobj, mobj->threshold, mobj->movecount);
		break;
	}
	case MT_ITEMCAPSULE:
		// scale the capsule
		if (mobj->scale < mobj->extravalue1)
		{
			fixed_t oldHeight = mobj->height;

			if ((mobj->extravalue1 - mobj->scale) < mobj->scalespeed)
				P_SetScale(mobj, mobj->destscale = mobj->extravalue1);
			else
				P_SetScale(mobj, mobj->destscale = mobj->scale + mobj->scalespeed);

			if (mobj->eflags & MFE_VERTICALFLIP)
				mobj->z -= (mobj->height - oldHeight);
		}

		// spawn parts if not done yet
		// (this SHOULD be done when the capsule is spawned, but gravflip isn't set up at that point)
		if (!(mobj->flags2 & MF2_JUSTATTACKED))
		{
			mobj->flags2 |= MF2_JUSTATTACKED;
			P_SpawnItemCapsuleParts(mobj);
		}

		// update & animate capsule
		if (!P_MobjWasRemoved(mobj->tracer))
		{
			mobj_t *part = mobj->tracer;

			if (mobj->threshold != part->threshold
			 || mobj->movecount != part->movecount) // change the capsule properties if the item type or amount is updated
				P_RefreshItemCapsuleParts(mobj);

			K_UpdateMobjItemOverlay(part, mobj->threshold, mobj->movecount);

			if (mobj->threshold == KITEM_INVINCIBILITY)
				mobj->color = K_RainbowColor(leveltime);
		}
		break;
	case MT_ORBINAUT:
	{
		dboolean grounded = P_IsObjectOnGround(mobj);
		sector_t *sec2;

		if (mobj->flags2 & MF2_AMBUSH)
		{
			if (grounded && (mobj->flags & MF_NOCLIPTHING))
			{
				mobj->momx = 1;
				mobj->momy = 0;
				mobj->frame = 3;
				S_StartSound(mobj, mobj->info->activesound);
				mobj->flags &= ~MF_NOCLIPTHING;
			}
			else if (mobj->movecount)
				mobj->movecount--;
			else if (mobj->frame < 3)
			{
				mobj->movecount = 2;
				mobj->frame++;
			}
			mobj->flags2 &= ~MF2_WATERRUN;
		}
		else
		{
			fixed_t finalspeed = mobj->movefactor;
			const fixed_t currentspeed = R_PointToDist2(0, 0, mobj->momx, mobj->momy);
			fixed_t thrustamount = 0;
			fixed_t frictionsafety = (mobj->friction == 0) ? 1 : mobj->friction;
			mobj_t *ghost = P_SpawnGhostMobj(mobj);
			ghost->colorized = true; // already has color!

			if (!grounded)
			{
				// No friction in the air
				frictionsafety = FRACUNIT;
			}

			mobj->angle = K_MomentumAngle(mobj);
			if (mobj->health <= 5)
			{
				int32_t i;
				for (i = 5; i >= mobj->health; i--)
					finalspeed = FixedMul(finalspeed, FRACUNIT-FRACUNIT/4);
			}

			if (currentspeed >= finalspeed)
			{
				// Thrust as if you were at top speed, slow down naturally
				thrustamount = FixedDiv(finalspeed, frictionsafety) - finalspeed;
			}
			else
			{
				const fixed_t beatfriction = FixedDiv(currentspeed, frictionsafety) - currentspeed;
				// Thrust to immediately get to top speed
				thrustamount = beatfriction + FixedDiv(finalspeed - currentspeed, frictionsafety);
			}

			P_Thrust(mobj, mobj->angle, thrustamount);

			if (P_AproxDistance(mobj->momx, mobj->momy) <= 14*mobj->scale)
			{
				// Not moving fast enough to water run.
				mobj->flags2 &= ~MF2_WATERRUN;
			}

			sec2 = P_ThingOnSpecial3DFloor(mobj);
			if ((K_AffectingTerrainActive() && mobj->terrain && mobj->terrain->pogoSpring > 0) || (sec2 && (sec2->specialflags & (SSF_YELLOWPOGOSPRING|SSF_REDPOGOSPRING)))
				|| (P_IsObjectOnRealGround(mobj, mobj->subsector->sector)
				&& (mobj->subsector->sector->specialflags & (SSF_YELLOWPOGOSPRING|SSF_REDPOGOSPRING))))
				K_DoPogoSpring(mobj, 0, 1);

			if (mobj->threshold > 0)
				mobj->threshold--;

			if (leveltime % 6 == 0)
				S_StartSound(mobj, mobj->info->activesound);
		}
		P_MobjCheckWater(mobj);
		break;
	}
	case MT_JAWZ:
		{
			sector_t *sec2;
			fixed_t topspeed = mobj->movefactor;
			fixed_t distbarrier = 512*mapobjectscale;
			fixed_t distaway;

			P_SpawnGhostMobj(mobj);

			if (mobj->threshold > 0)
				mobj->threshold--;
			if (leveltime % TICRATE == 0)
				S_StartSound(mobj, mobj->info->activesound);

			if (gamespeed == KARTSPEED_EASY)
				distbarrier = FixedMul(distbarrier, FRACUNIT-FRACUNIT/4);
			else if (gamespeed == KARTSPEED_HARD)
				distbarrier = FixedMul(distbarrier, FRACUNIT+FRACUNIT/4);
			else if (gamespeed == KARTSPEED_EXPERT)
				distbarrier = FixedMul(distbarrier, FRACUNIT+FRACUNIT/2);

			if ((gametypes[gametype]->rules & GTR_CIRCUIT) && mobj->tracer)
			{
				distaway = P_AproxDistance(mobj->tracer->x - mobj->x, mobj->tracer->y - mobj->y);
				if (distaway < distbarrier)
				{
					if (mobj->tracer->player)
					{
						fixed_t speeddifference = abs(topspeed - min(mobj->tracer->player->speed, K_GetKartSpeed(mobj->tracer->player, false,false)));
						topspeed = topspeed - FixedMul(speeddifference, FRACUNIT-FixedDiv(distaway, distbarrier));
					}
				}
			}

			if (gametype == GT_BATTLE)
			{
				mobj->friction -= 1228;
				if (mobj->friction > FRACUNIT)
					mobj->friction = FRACUNIT;
				if (mobj->friction < 0)
					mobj->friction = 0;
			}

			mobj->angle = R_PointToAngle2(0, 0, mobj->momx, mobj->momy);
			P_InstaThrust(mobj, mobj->angle, topspeed);

			if (mobj->tracer)
				mobj->angle = R_PointToAngle2(mobj->x, mobj->y, mobj->tracer->x, mobj->tracer->y);
			else
				mobj->angle = R_PointToAngle2(0, 0, mobj->momx, mobj->momy);

			K_DriftDustHandling(mobj);

			if (P_AproxDistance(mobj->momx, mobj->momy) <= 14*mobj->scale)
			{
				// Not moving fast enough to water run.
				mobj->flags2 &= ~MF2_WATERRUN;
			}

			sec2 = P_ThingOnSpecial3DFloor(mobj);
			if ((K_AffectingTerrainActive() && mobj->terrain && mobj->terrain->pogoSpring > 0) || (sec2 && (sec2->specialflags & (SSF_YELLOWPOGOSPRING|SSF_REDPOGOSPRING)))
				|| (P_IsObjectOnRealGround(mobj, mobj->subsector->sector)
				&& (mobj->subsector->sector->specialflags & (SSF_YELLOWPOGOSPRING|SSF_REDPOGOSPRING))))
				K_DoPogoSpring(mobj, 0, 1);

			P_MobjCheckWater(mobj);
			break;
		}
		case MT_JAWZ_DUD:
		{
			dboolean grounded = P_IsObjectOnGround(mobj);
			if (mobj->flags2 & MF2_AMBUSH)
			{
				if (grounded && (mobj->flags & MF_NOCLIPTHING))
				{
					mobj->momx = 1;
					mobj->momy = 0;
					S_StartSound(mobj, mobj->info->deathsound);
					mobj->flags &= ~MF_NOCLIPTHING;
				}
				mobj->flags2 &= ~MF2_WATERRUN;
			}
			else
			{
				P_MobjCheckWater(mobj);
				P_SpawnGhostMobj(mobj);
				mobj->angle = R_PointToAngle2(0, 0, mobj->momx, mobj->momy);
				P_InstaThrust(mobj, mobj->angle, mobj->movefactor);

				if (grounded)
				{
					sector_t *sec2 = P_ThingOnSpecial3DFloor(mobj);
					if ((K_AffectingTerrainActive() && mobj->terrain && mobj->terrain->pogoSpring > 0) || (sec2 && (sec2->specialflags & (SSF_YELLOWPOGOSPRING|SSF_REDPOGOSPRING)))
						|| (P_IsObjectOnRealGround(mobj, mobj->subsector->sector)
						&& (mobj->subsector->sector->specialflags & (SSF_YELLOWPOGOSPRING|SSF_REDPOGOSPRING))))
						K_DoPogoSpring(mobj, 0, 1);
				}

				if (P_AproxDistance(mobj->momx, mobj->momy) <= 14*mobj->scale)
				{
					// Not moving fast enough to water run.
					mobj->flags2 &= ~MF2_WATERRUN;
				}

				if (mobj->threshold > 0)
					mobj->threshold--;

				if (leveltime % TICRATE == 0)
					S_StartSound(mobj, mobj->info->activesound);


			}
			P_MobjCheckWater(mobj);
			break;
		}
	case MT_EGGMANITEM:
		/* FALLTHRU */
	case MT_BANANA:
		mobj->friction = ORIG_FRICTION/4;
		P_MobjCheckWater(mobj);

		if (mobj->momx || mobj->momy)
		{
			mobj_t *ghost = P_SpawnGhostMobj(mobj);

			if (mobj->target && !P_MobjWasRemoved(mobj->target) && mobj->target->player)
			{
				ghost->color = mobj->target->player->skincolor;
				ghost->colorized = true;
			}
		}

		if (P_IsObjectOnGround(mobj) && mobj->health > 1)
		{
			S_StartSound(mobj, mobj->info->activesound);
			mobj->momx = mobj->momy = 0;
			mobj->health = 1;
		}

		P_MobjCheckWater(mobj);

		if (mobj->threshold > 0)
			mobj->threshold--;
		break;
	case MT_SPB:
	case MT_BALLHOG:
		{
			mobj_t *ghost = P_SpawnGhostMobj(mobj);
			ghost->fuse = 3;

			if (mobj->target && !P_MobjWasRemoved(mobj->target) && mobj->target->player)
			{
				ghost->color = mobj->target->player->skincolor;
				ghost->colorized = true;
			}

			if (mobj->threshold > 0)
				mobj->threshold--;
			P_MobjCheckWater(mobj);
		}
		break;
	case MT_SINK:
		if (mobj->momx || mobj->momy)
		{
			mobj_t *ghost = P_SpawnGhostMobj(mobj);

			if (mobj->target && !P_MobjWasRemoved(mobj->target) && mobj->target->player)
			{
				ghost->color = mobj->target->player->skincolor;
				ghost->colorized = true;
			}
		}

		if (P_IsObjectOnGround(mobj))
		{
			S_StartSound(mobj, mobj->info->deathsound);
			P_SetMobjState(mobj, S_NULL);
		}

		if (mobj->threshold > 0)
			mobj->threshold--;
		break;
	case MT_SSMINE:
		if (mobj->target && mobj->target->player)
			mobj->color = mobj->target->player->skincolor;
		else
			mobj->color = SKINCOLOR_KETCHUP;

		if (mobj->momx || mobj->momy)
		{
			mobj_t *ghost = P_SpawnGhostMobj(mobj);
			ghost->colorized = true; // already has color!
		}

		if (P_IsObjectOnGround(mobj) && (mobj->state == &states[S_SSMINE_AIR1] || mobj->state == &states[S_SSMINE_AIR2]))
		{
			if (mobj->extravalue1 > 0)
				mobj->extravalue1--;
			else
			{
				mobj->momx = mobj->momy = 0;
				S_StartSound(mobj, mobj->info->activesound);
				P_SetMobjState(mobj, S_SSMINE_DEPLOY1);
			}
		}

		if ((mobj->state >= &states[S_SSMINE1] && mobj->state <= &states[S_SSMINE4])
			|| (mobj->state >= &states[S_SSMINE_DEPLOY8] && mobj->state <= &states[S_SSMINE_DEPLOY13]))
			A_SSMineSearch(mobj);

		if (mobj->threshold > 0)
			mobj->threshold--;
		break;
	case MT_LANDMINE:
		mobj->friction = ORIG_FRICTION/4;

		if (mobj->momx || mobj->momy || mobj->momz)
		{
			mobj_t *ghost = P_SpawnGhostMobj(mobj);
			ghost->colorized = true; // already has color!
		}

		if (P_IsObjectOnGround(mobj) && mobj->health > 1)
		{
			S_StartSound(mobj, mobj->info->activesound);
			mobj->momx = mobj->momy = 0;
			mobj->health = 1;
		}

		if (mobj->threshold > 0)
			mobj->threshold--;
		break;
	case MT_EGGMINE:
		mobj->friction = 95*ORIG_FRICTION/100;
		if (mobj->threshold > 0 || FixedHypot(mobj->momx, mobj->momy) >= 12*mobj->scale)
		{
			mobj_t *ghost = P_SpawnGhostMobj(mobj);

			if (mobj->target && mobj->target->player)
			{
				ghost->color = mobj->target->player->skincolor;
				ghost->colorized = true;
			}
		}

		if (P_IsObjectOnGround(mobj))
		{
			if (mobj->threshold == 0 && mobj->health > 1 && FixedHypot(mobj->momx, mobj->momy) < 12*mobj->scale)
			{
				mobj->health = 1;
				mobj->whiteshadow = false;
			}
			if (!S_SoundPlaying(mobj, sfx_s3kd2l))
			{
				S_StartSound(mobj, sfx_s3kd2l);
			}
			mobj->movefactor -= 1;
			if (mobj->movefactor <= 0)
			{
				P_KillMobj(mobj, mobj, NULL, DMG_INSTAKILL);
			}
		}
		else if (S_SoundPlaying(mobj, sfx_s3kd2l))
		{
			S_StopSoundByID(mobj, sfx_s3kd2l);
		}

		if (mobj->threshold > 0)
		{
			mobj->threshold--;
		}
		if (mobj->extravalue1 > 0)
		{
			mobj->extravalue1--;
		}
		break;
	case MT_SPBEXPLOSION:
		mobj->health--;
		break;
	case MT_MINEEXPLOSION:
		if ((mobj->z < mobj->floorz - mobj->height)
			|| (mobj->z > mobj->ceilingz + mobj->height)
			|| (cv_kartexplosion_limitlifetime.value && mobj->mobjlifetime > cv_kartexplosion_limitlifetime_cap.value))
		{
			P_KillMobj(mobj, NULL, NULL, DMG_NORMAL);
			break;
		}

		if (mobj->tics != -1)
		{
			mobj->tics--;

			// you can cycle through multiple states in a tic
			if (!mobj->tics)
				if (!P_SetMobjState(mobj, mobj->state->nextstate))
					return false; // freed itself
		}

		P_UnsetThingPosition(mobj);
		mobj->x += mobj->momx;
		mobj->y += mobj->momy;
		mobj->z += mobj->momz;
		P_SetThingPosition(mobj);
		return false;
	case MT_MINEEXPLOSIONSOUND:
		if (mobj->health == 100)
			S_StartSound(mobj, sfx_s3k4e);
		mobj->health--;
		break;
	case MT_BOOSTFLAME:
		if (!mobj->target || !mobj->target->health)
		{
			P_RemoveMobj(mobj);
			return false;
		}

		{
			player_t *p = NULL;
			if (mobj->target->target && mobj->target->target->player)
				p = mobj->target->target->player;
			else if (mobj->target->player)
				p = mobj->target->player;

			mobj->angle = mobj->extravalue1 && p ? p->drawangle : mobj->target->angle;
			P_MoveOrigin(mobj, mobj->target->x + P_ReturnThrustX(mobj, mobj->angle+ANGLE_180, mobj->target->radius),
				mobj->target->y + P_ReturnThrustY(mobj, mobj->angle+ANGLE_180, mobj->target->radius), mobj->target->z);
			K_FlipFromObject(mobj, mobj->target);
			P_SetScale(mobj, mobj->target->scale);

			mobj->roll = mobj->target->roll;
			mobj->pitch = mobj->target->pitch;

			mobj->sloperoll = mobj->target->sloperoll;
			mobj->slopepitch = mobj->target->slopepitch;

			if (p)
			{
				if (mobj->extravalue1 > 0)
				{
					if (p->driftboost > mobj->movecount && !mobj->extravalue2)
					{
						P_SetMobjState(mobj, S_DRIFTBOOSTFLAME);
						if (mobj->extravalue1 == 1)
							mobj->tics /= 2; // Less time for blue drift
						else if (mobj->extravalue1 == 3)
							mobj->tics = 3*mobj->tics/2; // A little longer for purple drift
						else if (mobj->extravalue1 == 4)
							mobj->tics *= 2; // More time for rainbow drift
					}
					mobj->movecount = p->driftboost;

					if (mobj->extravalue2)
						P_SetScale(mobj, FixedMul(5*FRACUNIT * mobj->tics / TICRATE, mobj->scale));
					else if (mobj->extravalue1 == 4)
						P_SetScale(mobj, max(10, mobj->tics - 3*TICRATE/2)*mobj->scale/10);

					// larger karts need larger flames
					if (mobj->target->skin && !wadfiles[((skin_t *)mobj->target->skin)->wadnum]->compatmode)
					{
						P_SetScale(mobj, 9*mobj->scale/8);
						mobj->spriteyoffset = -6*FRACUNIT;
					}

					if (p->driftboost == 0 && !mobj->extravalue2)
					{
						P_RemoveMobj(mobj);
						return false;
					}
				}
				else
				{
                    if (stackingactive && cv_kartstacking_colorflame.value)
					{
						switch(p->numsneakers)
						{
							case 0:
							case 1:
								mobj->colorized = false;
								break;
							case 2:
								mobj->colorized = true;
								mobj->color = SKINCOLOR_BLUEBERRY;
								break;
							case 3:
								mobj->colorized = true;
								mobj->color = SKINCOLOR_PURPLE;
								break;
							case 4:
								mobj->colorized = true;
								mobj->color = SKINCOLOR_MOONSLAM;
								break;
							case 5:
								mobj->colorized = true;
								mobj->color = SKINCOLOR_WHITE;
								break;
							default:
								mobj->colorized = true;
								mobj->color = SKINCOLOR_WHITE;
								break;

						}
					}

					if (p->sneakertimer > mobj->movecount)
						P_SetMobjState(mobj, S_BOOSTFLAME);
					mobj->movecount = p->sneakertimer;
				}
			}
		}

		if (mobj->extravalue1)
		{
			if (leveltime % 2 == 1)
			{
				mobj->frame |= FF_TRANS60;
			}
			else
			{
				mobj->frame &= ~FF_TRANS60;
			}

			if (mobj->extravalue1 == 4)
			{
				mobj->color = K_RainbowColor(leveltime);
			}
		}

		if (mobj->state == &states[S_BOOSTSMOKESPAWNER] || mobj->extravalue2)
		{
			mobj_t *smoke = P_SpawnMobj(mobj->x, mobj->y, mobj->z+(8<<FRACBITS), MT_BOOSTSMOKE);

			P_SetScale(smoke, mobj->target->scale/2);
			smoke->destscale = 3*mobj->target->scale/2;
			smoke->scalespeed = mobj->target->scale/12;

			smoke->momx = mobj->target->momx/2;
			smoke->momy = mobj->target->momy/2;
			smoke->momz = mobj->target->momz/2;

			fixed_t rand_angle;
			fixed_t rand_move;

			rand_move = P_RandomRange(PR_ITEM_BOOST, 0, 8) * mobj->target->scale;
			rand_angle = mobj->angle+FixedAngle(P_RandomRange(PR_ITEM_BOOST, 135, 225)<<FRACBITS);

			P_Thrust(smoke, rand_angle, rand_move);

			if (mobj->extravalue2)
			{
				smoke->colorized = true;
				smoke->color = mobj->color;
			}
		}
		break;
	case MT_SPARKLETRAIL:
		if (!mobj->target)
		{
			P_RemoveMobj(mobj);
			return false;
		}
		mobj->color = mobj->target->color;
		mobj->colorized = mobj->target->colorized;
		break;
	case MT_INVULNFLASH:
		if (!mobj->target ||
			!mobj->target->health ||
			(mobj->target->player &&
				!(mobj->target->player->invincibilitytimer ||
					mobj->target->player->smonitortimer)))
		{
			P_RemoveMobj(mobj);
			return false;
		}
		P_MoveOrigin(mobj, mobj->target->x, mobj->target->y, mobj->target->z);
		break;
	case MT_BRAKEDRIFT:
	{
		// Letting go of accel functions about the same as brake-drifting
		dboolean nullDrift = (mobj->target && mobj->target->player && !(K_GetKartButtons(mobj->target->player) & BT_ACCELERATE));

		if ((!mobj->target || !mobj->target->health || !mobj->target->player || !P_IsObjectOnGround(mobj->target))
			|| !mobj->target->player->drift || !(mobj->target->player->pflags & PF_BRAKEDRIFT)
			|| !((mobj->target->player->cmd.buttons & BT_BRAKE)
			|| nullDrift))
		{
			P_RemoveMobj(mobj);
			return false;
		}
		else
		{
			uint8_t driftcolor = K_DriftSparkColor(mobj->target->player, mobj->target->player->driftcharge);
			fixed_t newx, newy;
			angle_t travelangle;

			travelangle = mobj->target->angle - ((ANGLE_45/5)*mobj->target->player->drift);

			newx = mobj->target->x + P_ReturnThrustX(mobj->target, travelangle+ANGLE_180, 24*mobj->target->scale);
			newy = mobj->target->y + P_ReturnThrustY(mobj->target, travelangle+ANGLE_180, 24*mobj->target->scale);
			P_MoveOrigin(mobj, newx, newy, mobj->target->z);

			mobj->angle = travelangle - ((ANGLE_90/5)*mobj->target->player->drift);
			P_SetScale(mobj, (mobj->destscale = mobj->target->scale));

			if (driftcolor != SKINCOLOR_NONE)
				mobj->color = driftcolor;
			else
				mobj->color = SKINCOLOR_SILVER;

			if (nullDrift)
			{
				if (cv_nulldriftefx.value)
				{
					// Play a harsher sound when you null-drift.
					if (mobj->target->player->nulldrifttime % 4 == 0)
						S_StartSound(mobj, sfx_s3k67);
				}
				else if (!S_SoundPlaying(mobj, sfx_cdfm17))
				{
					S_StartSound(mobj, sfx_cdfm17);
				}

				// Set the player's nulldrift variables for any relevant VFX.
				mobj->target->player->nulldrift = intsign(mobj->target->player->drift);
			}
			else
			{
				if (!S_SoundPlaying(mobj, sfx_cdfm17))
					S_StartSound(mobj, sfx_cdfm17);
			}

			K_MatchGenericExtraFlags(mobj, mobj->target);
			if (leveltime & 1)
				mobj->renderflags |= RF_DONTDRAW;
		}
		break;
	}
	case MT_PLAYERRETICULE:
		if (!mobj->target || !mobj->target->health)
		{
			P_RemoveMobj(mobj);
			return false;
		}
		P_MoveOrigin(mobj, mobj->target->x, mobj->target->y, mobj->target->z);
		mobj->old_x = mobj->target->old_x;
		mobj->old_y = mobj->target->old_y;
		mobj->old_z = mobj->target->old_z;
		break;
	case MT_INSTASHIELDB:
		mobj->renderflags ^= RF_DONTDRAW;
		/* FALLTHRU */
	case MT_INSTASHIELDA:
		if (!mobj->target || P_MobjWasRemoved(mobj->target) || !mobj->target->health || (mobj->target->player && !mobj->target->player->instashield))
		{
			P_RemoveMobj(mobj);
			return false;
		}
		P_MoveOrigin(mobj, mobj->target->x, mobj->target->y, mobj->target->z);
		mobj->old_x = mobj->target->old_x;
		mobj->old_y = mobj->target->old_y;
		mobj->old_z = mobj->target->old_z;
		K_MatchGenericExtraFlags(mobj, mobj->target);
		break;
	case MT_BATTLEPOINT:
		if (!mobj->target || P_MobjWasRemoved(mobj->target))
		{
			P_RemoveMobj(mobj);
			return false;
		}

		if (mobj->movefactor < 48*mobj->target->scale)
		{
			mobj->movefactor += (48*mobj->target->scale)/6;
			if (mobj->movefactor > mobj->target->height)
				mobj->movefactor = mobj->target->height;
		}
		else if (mobj->movefactor > 48*mobj->target->scale)
		{
			mobj->movefactor -= (48*mobj->target->scale)/6;
			if (mobj->movefactor < mobj->target->height)
				mobj->movefactor = mobj->target->height;
		}
		K_MatchGenericExtraFlags(mobj, mobj->target);
		P_MoveOrigin(mobj, mobj->target->x, mobj->target->y, mobj->target->z + (mobj->target->height/2) + mobj->movefactor);
		break;
	case MT_GAINAX:
		if (!mobj->target || P_MobjWasRemoved(mobj->target) // sanity
			|| !mobj->target->player // ditto
			|| !mobj->target->player->glanceDir // still glancing?
			|| mobj->target->player->aizdriftturn // only other circumstance where can glance
			|| ((K_GetKartButtons(mobj->target->player) & BT_LOOKBACK) != BT_LOOKBACK)) // it's a lookback indicator...
		{
			P_RemoveMobj(mobj);
			return false;
		}

		mobj->angle = mobj->target->player->drawangle;
		mobj->z = mobj->target->z;

		K_MatchGenericExtraFlags(mobj, mobj->target);
		mobj->renderflags = (mobj->renderflags & ~RF_DONTDRAW)|K_GetPlayerDontDrawFlag(mobj->target->player);

		P_MoveOrigin(mobj, mobj->target->x + FixedMul(34 * mapobjectscale, FINECOSINE((mobj->angle + mobj->movedir) >> ANGLETOFINESHIFT)),
				mobj->target->y + FixedMul(34 * mapobjectscale, FINESINE((mobj->angle + mobj->movedir) >> ANGLETOFINESHIFT)),
				mobj->z + (32 * mapobjectscale * P_MobjFlip(mobj)));

		{
			statenum_t gainaxstate = mobj->state-states;
			if (gainaxstate == S_GAINAX_TINY)
			{
				if (abs(mobj->target->player->glanceDir) > 1)
				{
					if (mobj->target->player->itemamount && mobj->target->player->itemtype)
						gainaxstate = S_GAINAX_HUGE;
					else
						gainaxstate = S_GAINAX_MID1;
					P_SetMobjState(mobj, gainaxstate);
				}
			}
			else if (abs(mobj->target->player->glanceDir) <= 1)
			{
				if (mobj->flags2 & MF2_AMBUSH)
					mobj->flags2 &= ~MF2_AMBUSH;
				else
					P_SetMobjState(mobj, S_GAINAX_TINY);
			}
		}

		break;
	case MT_BOOSTSTACK:
	{
		if (!mobj->target || P_MobjWasRemoved(mobj->target) || !mobj->target->health || (mobj->target->player && !mobj->target->player->numboosts))
		{
			P_RemoveMobj(mobj);
			return false;
		}

		mobj->roll = mobj->target->roll;
		mobj->pitch = mobj->target->pitch;

		mobj->sloperoll = mobj->target->sloperoll;
		mobj->slopepitch = mobj->target->slopepitch;

		// Thx Indev! (taken and modified from BoostStack)
		P_MoveOrigin(mobj, mobj->target->x + FixedMul(cos(mobj->target->angle), FixedMul(30*FRACUNIT, mapobjectscale)), mobj->target->y + FixedMul(sin(mobj->target->angle), FixedMul(30*FRACUNIT, mapobjectscale)), mobj->target->z);
		K_FlipFromObject(mobj, mobj->target);

		// Leveltime based animation
		mobj->frame = states[S_BOOSTSTACK].frame + (leveltime / 3) % 5;
		break;
	}
	case MT_THUNDERSHIELD:
	{
		if (!mobj->target || !mobj->target->health || !mobj->target->player
			|| K_GetShieldFromPlayer(mobj->target->player) != KSHIELD_THUNDER)
		{
			P_RemoveMobj(mobj);
			return false;
		}
		P_SetScale(mobj, (mobj->destscale = (5*mobj->target->scale)>>2));

		// Don't sloperoll Shields
		mobj->pitch = 0;
		mobj->roll = 0;

		mobj->slopepitch = 0;
		mobj->sloperoll = 0;

		P_MoveOrigin(mobj, mobj->target->x, mobj->target->y, mobj->target->z);

		break;
	}
	case MT_ATTRACTIONSHIELD:
	{
		if (!mobj->target || !mobj->target->health || !mobj->target->player
			|| K_GetShieldFromPlayer(mobj->target->player) != KSHIELD_ATTRACTION)
		{
			P_RemoveMobj(mobj);
			return false;
		}
		P_SetScale(mobj, (mobj->destscale = (5*mobj->target->scale)>>2));

		// Don't sloperoll Shields
		mobj->pitch = 0;
		mobj->roll = 0;

		mobj->slopepitch = 0;
		mobj->sloperoll = 0;

		P_MoveOrigin(mobj, mobj->target->x, mobj->target->y, mobj->target->z);

		break;
	}
	case MT_BUBBLESHIELD:
	{
		const player_t *player = P_MobjWasRemoved(mobj->target) ? NULL : mobj->target->player;

		if (player == NULL || player->mo->health == 0 || K_GetShieldFromPlayer(player) != KSHIELD_BUBBLE)
		{
			P_RemoveMobj(mobj);
			return false;
		}

		fixed_t scale = 5*player->mo->scale/4;
		if (cv_kartbubble_defense_canidle.value && player->bubbleblowup == 0 && player->bubblecool > 0)
			mobj->renderflags |= RF_GHOSTLY;
		else if ((!cv_kartbubble_defense_canidle.value) && !K_IsBubbleDefending(player))
			mobj->renderflags |= (RF_TRANS40 | RF_FULLBRIGHT);
		else
			mobj->renderflags &= ~RF_GHOSTLYMASK;

		if (player->bubblecool)
		{
			int32_t blow = player->bubbleblowup + min(player->bubblecool, bubbletime)*10;
			scale += (blow * scale) / (bubbletime * 10);

			if (mobj->state - states != S_BUBBLESHIELDBLOWUP)
				P_SetMobjState(mobj, S_BUBBLESHIELDBLOWUP);

			mobj->angle += ANGLE_22h;

			fixed_t stretch = 0;
			if (player->bubblecool >= bubbletime) // overcharge
				stretch = ((player->bubblecool - bubbletime) % 4) * FRACUNIT/4;
			else if (player->bubbleblowup > 0 && player->bubblecool <= 4) // inflate 1
				stretch = player->bubblecool*FRACUNIT/5;
			else if (player->bubbleblowup >= 5 && player->bubblecool < 8) // inflate 2
				stretch = (8 - player->bubblecool)*FRACUNIT/5;
			else if (player->bubblehealth == 0 && player->bubbleblowup == 0) // decaying
				stretch = (player->bubblecool - bubbletime)*FRACUNIT/bubbletime;

			mobj->spritexscale = FRACUNIT + FTAN(FixedAngle(stretch*30) + ANGLE_90);
			mobj->spriteyscale = FRACUNIT - stretch/5;

			mobj->colorized = true;
			if (player->bubbleblowup > 0 && leveltime & 1)
				mobj->color = SKINCOLOR_WHITE;
			else
				mobj->color = SKINCOLOR_BLUE;

			if (P_IsObjectOnGround(player->mo) && player->bubbleblowup > 0)
			{
				uint8_t i;

				for (i = 0; i < 2; i++)
				{
					angle_t a = mobj->angle + ((i & 1) ? ANGLE_180 : 0);
					mobj_t *wave;

					wave = P_SpawnMobj(
						(player->mo->x - player->mo->momx) + P_ReturnThrustX(NULL, a, mobj->radius - (5*scale)),
						(player->mo->y - player->mo->momy) + P_ReturnThrustY(NULL, a, mobj->radius - (5*scale)),
						(player->mo->z - player->mo->momz), MT_THOK);

					wave->colorized = true;
					wave->color = SKINCOLOR_BLUE;
					wave->flags &= ~(MF_NOCLIPHEIGHT|MF_NOGRAVITY);
					P_SetScale(wave, (wave->destscale = scale/3));

					P_SetMobjState(wave, S_SPLISH1);

					wave->momx = player->mo->momx;
					wave->momy = player->mo->momy;
					wave->momz = player->mo->momz;
				}
			}
		}
		else
		{
			mobj->angle = player->mo->angle;
			// mobj->renderflags &= ~RF_GHOSTLYMASK;
			mobj->colorized = false;
			mobj->color = SKINCOLOR_BLUE;

			if (mobj->state - states == S_BUBBLESHIELDBLOWUP)
				P_SetMobjState(mobj, S_BUBBLESHIELD1);
		}

		P_SetScale(mobj, (mobj->destscale = scale));

		if (mobj->tracer && (!P_MobjWasRemoved(mobj->tracer)))
		{
			if (mobj->tracer->type == MT_OVERLAY)
			{
				fixed_t health_scalar;

				P_SetScale(mobj->tracer, mobj->destscale);

				mobj->tracer->threshold |= OV_DONTXYSCALE;

				mobj->tracer->spritexscale = mobj->spritexscale;
				mobj->tracer->spriteyscale = mobj->spriteyscale;

				health_scalar = player->bubblehealth * FRACUNIT / MAXBUBBLEHEALTH;
				health_scalar = 5 - CLAMP(health_scalar / (FRACUNIT / 5), 1, 5);

				statenum_t damagestate = health_scalar == 0 ? S_INVISIBLE : S_BUBLSHLD_DMG_1 - 1 + health_scalar;

				// Depending on the level of damage done to the shield, show some cracks.
				if (mobj->tracer->state - states != damagestate)
					P_SetMobjState(mobj->tracer, damagestate);
			}
			else
			{
				// Very silently detach ourselves from our tracer and try a
				// damage-control fix.
				P_SetTarget(&mobj->tracer, NULL);

				mobj_t *spawn = P_SpawnMobj(mobj->x, mobj->z, mobj->z, MT_OVERLAY);
				spawn->destscale = mobj->scale;
				P_SetScale(spawn, mobj->scale);
				P_SetTarget(&mobj->tracer, spawn);
				P_SetTarget(&spawn->target, mobj);
			}
		}

		// Don't sloperoll Bubble Shields since it messes with the shatter VFX
		mobj->pitch = 0;
		mobj->roll = 0;

		mobj->slopepitch = 0;
		mobj->sloperoll = 0;

		mobj->flags &= ~(MF_NOCLIP|MF_NOCLIPTHING);
		P_MoveOrigin(mobj, player->mo->x, player->mo->y, player->mo->z);
		mobj->flags |= MF_NOCLIP|MF_NOCLIPTHING;
		break;
	}
	case MT_FLAMESHIELD:
		{
			statenum_t curstate;
			uint8_t flamestore = 0;
			if (!mobj->target || !mobj->target->health || !mobj->target->player
				|| K_GetShieldFromPlayer(mobj->target->player) != KSHIELD_FLAME)
			{
				P_RemoveMobj(mobj);
				return false;
			}
			flamestore = mobj->target->player->flamestore;
			// P_SetScale(mobj, (mobj->destscale = ((flamestore >= FLAMESTOREMAX-1) ? (2*mobj->target->scale) : (5*mobj->target->scale)>>2)));
			mobj->destscale = ((flamestore >= FLAMESTOREMAX-1) ? (7*mobj->target->scale/4) : (5*mobj->target->scale)>>2);
			curstate = ((mobj->tics == 1) ? (mobj->state->nextstate) : ((statenum_t)(mobj->state-states)));
			if (flamestore)
			{
				if (curstate != S_FLAMESHIELDDASH)
					P_SetMobjState(mobj, S_FLAMESHIELDDASH);
				mobj->renderflags ^= RF_DONTDRAW;
			}
			else
			{
				if (curstate == S_FLAMESHIELDDASH)
					P_SetMobjState(mobj, S_FLAMESHIELD1);
				mobj->renderflags &= ~RF_DONTDRAW;
			}

			if (flamestore >= FLAMESTOREMAX-1)
			{
				mobj->renderflags |= RF_ADD;
			}
			else
			{
				mobj->renderflags &= ~RF_ADD;
			}

			// Don't sloperoll Shields
			mobj->pitch = 0;
			mobj->roll = 0;

			mobj->slopepitch = 0;
			mobj->sloperoll = 0;

			P_MoveOrigin(mobj, mobj->target->x, mobj->target->y, mobj->target->z);
			mobj->angle = K_MomentumAngle(mobj->target);
			break;
		}
	case MT_ROCKETSNEAKER:
		if (!mobj->target || !mobj->target->health)
		{
			P_RemoveMobj(mobj);
			return false;
		}
		if (mobj->target->player && !mobj->target->player->rocketsneakertimer)
		{
			mobj->flags &= ~MF_NOGRAVITY;
			mobj->angle += ANGLE_45;

			if (!mobj->extravalue2)
			{
				K_DropRocketSneaker(mobj->target->player);
				mobj->target->player->equippeditem = KITEM_NONE;
			}
			else if (P_IsObjectOnGround(mobj))
			{
				P_RemoveMobj(mobj);
				return false;
			}
		}
		break;
		case MT_KARMAHITBOX:
		{
			statenum_t state = (mobj->state-states);

			if (!mobj->target || !mobj->target->health || !mobj->target->player || mobj->target->player->spectator
				|| mobj->target->player->bumper || !(gametypes[gametype]->rules & GTR_KARMA))
			{
				P_RemoveMobj(mobj);
				return false;
			}

			P_MoveOrigin(mobj, mobj->target->x, mobj->target->y, mobj->target->z);
			mobj->angle = mobj->target->angle;
			mobj->scalespeed = mobj->target->scalespeed;
			mobj->destscale = mobj->target->destscale;
			P_SetScale(mobj, mobj->target->scale);
			mobj->color = mobj->target->color;
			mobj->colorized = true;

			// Give items an item-sized hitbox
			if (mobj->target->player->karmamode == KARMAMODE_ITEM)
				mobj->radius = 48*mobj->target->scale;
			else
				mobj->radius = 24*mobj->target->scale;
			mobj->height = 2*mobj->radius;

			if (mobj->target->player->karmadelay > 0)
			{
				if (state < mobj->info->spawnstate || state > mobj->info->spawnstate+19)
					P_SetMobjState(mobj, mobj->info->spawnstate);
				if (mobj->target->player->karmadelay < TICRATE && (leveltime & 1))
					mobj->renderflags &= ~RF_DONTDRAW;
				else
					mobj->renderflags |= RF_DONTDRAW;
			}
			else
			{
				if (!mobj->target->player->karmamode
					&& (state < mobj->info->spawnstate || state > mobj->info->spawnstate+19))
					P_SetMobjState(mobj, mobj->info->spawnstate);
				else if (mobj->target->player->karmamode == KARMAMODE_ITEM
					&& state != mobj->info->seestate)
					P_SetMobjState(mobj, mobj->info->seestate);
				else if (mobj->target->player->karmamode == KARMAMODE_EGGMAN
					&& state != mobj->info->painstate)
					P_SetMobjState(mobj, mobj->info->painstate);

				if (mobj->target->player->flashing && (leveltime & 1))
					mobj->renderflags |= RF_DONTDRAW;
				else
					mobj->renderflags &= ~RF_DONTDRAW;
			}

			// Update mobj antigravity status:
			mobj->eflags = (mobj->eflags & ~MFE_VERTICALFLIP)|(mobj->target->eflags & MFE_VERTICALFLIP);
			mobj->flags2 = (mobj->flags2 & ~MF2_OBJECTFLIP)|(mobj->target->flags2 & MF2_OBJECTFLIP);

			// Now for the wheels
			{
				const fixed_t rad = FixedMul(mobjinfo[MT_PLAYER].radius, mobj->target->scale);
				mobj_t *cur = mobj->hnext;

				while (cur && !P_MobjWasRemoved(cur))
				{
					fixed_t offx = rad;
					fixed_t offy = rad;

					if (cur->lastlook == 1 || cur->lastlook == 3)
						offx *= -1;
					if (cur->lastlook == 2 || cur->lastlook == 3)
						offy *= -1;

					P_MoveOrigin(cur, mobj->x + offx, mobj->y + offy, mobj->z);
					cur->scalespeed = mobj->target->scalespeed;
					cur->destscale = mobj->target->destscale;
					P_SetScale(cur, mobj->target->scale);
					cur->color = mobj->target->color;
					cur->colorized = true;
					K_FlipFromObject(cur, mobj->target);

					if (mobj->renderflags & RF_DONTDRAW)
						cur->renderflags |= RF_DONTDRAW;
					else
						cur->renderflags &= ~RF_DONTDRAW;

					cur = cur->hnext;
				}
			}
		}
		break;
		case MT_SIGN: // Kart's unique sign behavior
			if (mobj->movecount)
			{
				if (mobj->z <= mobj->movefactor)
				{
					P_SetMobjState(mobj, S_SIGN_END);
					if (mobj->info->attacksound)
						S_StartSound(mobj, mobj->info->attacksound);
					mobj->flags |= MF_NOGRAVITY; // ?
					mobj->flags &= ~MF_NOCLIPHEIGHT;
					mobj->z = mobj->movefactor;
					mobj->movecount = 0;
				}
				else
				{
					fixed_t rand_x;
					fixed_t rand_y;
					fixed_t rand_z;

					rand_z = mobj->z + (24*mobj->scale) + (P_RandomRange(PR_SPARKLE,-8,8)*mobj->scale);
					rand_y = mobj->y + (P_RandomRange(PR_SPARKLE,-48,48)*mobj->scale);
					rand_x = mobj->x + (P_RandomRange(PR_SPARKLE,-48,48)*mobj->scale);

					P_SpawnMobj(rand_x, rand_y, rand_z, MT_SIGNSPARKLE);

					mobj->flags &= ~MF_NOGRAVITY;

					if (abs(mobj->z - mobj->movefactor) <= (512*mobj->scale) && !mobj->cvmem)
					{
						if (mobj->info->seesound)
							S_StartSound(mobj, mobj->info->seesound);
						mobj->cvmem = 1;
					}
				}
			}
			break;
	case MT_CDUFO:
		if (!mobj->spawnpoint || mobj->fuse)
			break;

		if (mobj->movecount)
		{
			mobj->movecount--;
			break;
		}
		else if (P_AproxDistance(mobj->x - (mobj->spawnpoint->x<<FRACBITS), mobj->y - (mobj->spawnpoint->y<<FRACBITS)) < (420<<FRACBITS))
			break;

		mobj->movecount = 3;

		{
			angle_t facing = P_RandomRange(PR_MOVINGTARGET, 0, 90);
			if (facing >= 45)
				facing = InvAngle((facing - 45)*ANG1);
			else
				facing *= ANG1;

			mobj->angle = R_PointToAngle2(mobj->x, mobj->y, mobj->spawnpoint->x<<FRACBITS, mobj->spawnpoint->y<<FRACBITS) + facing;
		}
		break;
	case MT_FZEROBOOM: // F-Zero explosion
		if (!mobj->extravalue1)
		{
			fixed_t mx = P_ReturnThrustX(NULL, mobj->angle, 32*mobj->scale);
			fixed_t my = P_ReturnThrustY(NULL, mobj->angle, 32*mobj->scale);
			mobj_t *explosion = P_SpawnMobj(mobj->x + (2*mx), mobj->y + (2*my), mobj->z+(mobj->height/2), MT_THOK);

			P_SetMobjState(explosion, S_FZEROBOOM1);
			explosion->scale = mobj->scale*2;
			explosion->momx = mx;
			explosion->momy = my;

			S_StartSound(mobj, mobj->info->seesound);
			mobj->extravalue1 = 1;
		}

		if (mobj->extravalue1 != 2 && !S_SoundPlaying(mobj, mobj->info->attacksound))
			S_StartSound(mobj, mobj->info->attacksound);

		if (mobj->extravalue2 <= 8) // Short delay
			mobj->extravalue2++; // flametimer
		else // fire + smoke pillar
		{
			uint8_t i;

			fixed_t rand_x;
			fixed_t rand_y;

			rand_y = mobj->y + (P_RandomRange(PR_EXPLOSION, -32, 32)*mobj->scale);
			rand_x = mobj->x + (P_RandomRange(PR_EXPLOSION, -32, 32)*mobj->scale);

			mobj_t *fire = P_SpawnMobj(rand_x, rand_y, mobj->z, MT_THOK);

			fire->sprite = SPR_FPRT;
			fire->frame = FF_FULLBRIGHT|FF_TRANS30;
			fire->scale = mobj->scale*4;
			fire->momz = P_RandomRange(PR_SMOLDERING, 2, 3)*mobj->scale;
			fire->scalespeed = mobj->scale/12;
			fire->destscale = 1;
			fire->tics = TICRATE;

			for (i = 0; i < 2; i++)
			{
				rand_y = mobj->y + (P_RandomRange(PR_SMOLDERING, -16, 16)*mobj->scale);
				rand_x = mobj->x + (P_RandomRange(PR_SMOLDERING, -16, 16)*mobj->scale);

				mobj_t *smoke = P_SpawnMobj(rand_x, rand_y, mobj->z, MT_SMOKE);

				P_SetMobjState(smoke, S_FZSLOWSMOKE1);
				smoke->scale = mobj->scale;
				smoke->momz = P_RandomRange(PR_SMOLDERING, 3, 10)*mobj->scale;
				smoke->destscale = mobj->scale*4;
				smoke->scalespeed = mobj->scale/24;
			}
		}
		break;
	case MT_EZZPROPELLER:
		if (mobj->hnext)
		{
			mobj_t *cur = mobj->hnext;

			while (cur && !P_MobjWasRemoved(cur))
			{
				cur->angle += FixedAngle(mobj->info->speed);
				P_MoveOrigin(cur, mobj->x + FINECOSINE((cur->angle*8)>>ANGLETOFINESHIFT),
					mobj->y + FINESINE((cur->angle*8)>>ANGLETOFINESHIFT), mobj->z);
				//P_SpawnGhostMobj(cur)->tics = 2;

				cur = cur->hnext;
			}
		}
		if (!S_SoundPlaying(mobj, mobj->info->seesound))
			S_StartSound(mobj, mobj->info->seesound);
		break;
	case MT_FROGGER:
		{
			statenum_t frogstate = (mobj->state-states);

			// FROG ATTACK VALUES:
			// threshold: distance
			// movecount: time
			// lastlook: direction
			// extravalue1: x step
			// extravalue2: y step
			// cusval: z step

			if (frogstate == S_FROGGER)
			{
				mobj->threshold = mobj->movecount = mobj->lastlook = 0; // clear tongue attack
				mobj->extravalue1 = mobj->extravalue2 = mobj->cusval = 0;
				if (mobj->hnext) // Clean hnext list
				{
					mobj_t *cur = mobj->hnext;
					while (cur && !P_MobjWasRemoved(cur))
					{
						mobj_t *next = cur->hnext;
						P_RemoveMobj(cur);
						cur = next;
					}
				}

				if (mobj->reactiontime)
					mobj->reactiontime--;
				else
				{
					if (mobj->flags2 & MF2_AMBUSH)
					{
						mobj->momz = P_RandomRange(PR_SRB2KART, 12, 16)<<FRACBITS;
						S_StartSound(mobj, sfx_s3kb1);
						P_SetMobjState(mobj, S_FROGGER_JUMP);
					}
					else
					{
						mobj_t *tongue = P_SpawnMobj(mobj->x, mobj->y, mobj->z + (mobj->height/2), MT_FROGTONGUE);
						P_SetTarget(&mobj->tracer, tongue);
						P_SetMobjState(mobj, S_FROGGER_ATTACK);
					}
				}
			}
			else if (frogstate == S_FROGGER_ATTACK)
			{
				if (!mobj->tracer || P_MobjWasRemoved(mobj->tracer))
				{
					mobj->reactiontime = mobj->info->reactiontime;
					P_SetMobjState(mobj, S_FROGGER);
					break;
				}

				if (mobj->threshold == 0)
				{
					fixed_t targetz = mobj->tracer->z; //mobj->z + (mobj->height/2)

					mobj->threshold = 256;
					mobj->movecount = 1;
					mobj->lastlook = 1;

					mobj->tracer->angle = mobj->angle;

					mobj->extravalue1 = FixedMul(FixedMul((mobj->threshold/16)<<FRACBITS,
						FINECOSINE(((angle_t)targetz)>>ANGLETOFINESHIFT)),
						FINECOSINE(mobj->angle>>ANGLETOFINESHIFT)) >> FRACBITS;

					mobj->extravalue2 = FixedMul(FixedMul((mobj->threshold/16)<<FRACBITS,
						FINECOSINE(((angle_t)targetz)>>ANGLETOFINESHIFT)),
						FINESINE(mobj->angle>>ANGLETOFINESHIFT)) >> FRACBITS;

					mobj->cusval = FixedMul((mobj->threshold/16)<<FRACBITS,
						FINESINE(((angle_t)targetz)>>ANGLETOFINESHIFT)) >> FRACBITS;

					S_StartSound(mobj, sfx_s3k8c); // Play that tongue-y sound.
				}

				mobj->movecount += mobj->lastlook;

				if (!(P_TryMove(mobj->tracer, mobj->x + ((mobj->extravalue1<<FRACBITS) * mobj->movecount), mobj->y + ((mobj->extravalue2<<FRACBITS) * mobj->movecount), true, NULL))
					|| (mobj->movecount >= 16) // maximum travel time
					|| (mobj->tracer->z <= mobj->tracer->floorz) // Through the floor
					|| ((mobj->tracer->z + mobj->tracer->height) >= mobj->tracer->ceilingz)) // Through the ceiling
				{
					mobj->lastlook = -1; // Reverse direction.
				}

				if (mobj->movecount == 0) // It's back to its source, time to reset.
				{
					mobj->threshold = mobj->lastlook = 0;

					P_RemoveMobj(mobj->tracer);

					if (mobj->hnext) // Clean hnext list
					{
						mobj_t *cur = mobj->hnext;
						while (cur && !P_MobjWasRemoved(cur))
						{
							mobj_t *next = cur->hnext;
							P_RemoveMobj(cur);
							cur = next;
						}
					}

					mobj->reactiontime = mobj->info->reactiontime;
					P_SetMobjState(mobj, S_FROGGER);
				}
				else
				{
					const uint8_t numjoints = 11;
					uint8_t joint = numjoints;
					mobj_t *cur = mobj->hnext, *prev = mobj;

					mobj->angle = R_PointToAngle2(mobj->x, mobj->y, mobj->tracer->x, mobj->tracer->y);

					for (; joint > 0; joint--)
					{
						fixed_t wx = mobj->tracer->x + (joint * (mobj->x - mobj->tracer->x) / (numjoints+1));
						fixed_t wy = mobj->tracer->y + (joint * (mobj->y - mobj->tracer->y) / (numjoints+1));
						fixed_t wz = mobj->tracer->z + (joint * ((mobj->z + (mobj->height/2)) - mobj->tracer->z) / (numjoints+1));

						if (cur && !P_MobjWasRemoved(cur))
							P_MoveOrigin(cur, wx, wy, wz);
						else
							cur = P_SpawnMobj(wx, wy, wz, MT_FROGTONGUE_JOINT);

						P_SetTarget(&cur->target, mobj);

						P_SetTarget(&prev->hnext, cur);
						P_SetTarget(&cur->hprev, prev);

						prev = cur;
						cur = cur->hnext;
					}
				}
			}
			else if (frogstate == S_FROGGER_JUMP)
			{
				if (P_IsObjectOnGround(mobj))
				{
					mobj->reactiontime = mobj->info->reactiontime;
					P_SetMobjState(mobj, S_FROGGER);
				}
			}
		}
		break;
	case MT_ROBRA:
	case MT_BLUEROBRA:
		if (mobj->health)
		{
			dboolean blue = (mobj->type == MT_BLUEROBRA);

			if (blue)
			{
				if (mobj->spawnpoint)
					mobj->extravalue2 = mobj->spawnpoint->angle;
				else
					mobj->extravalue2 = 128;
			}
			else
			{
				if (!mobj->extravalue2)
					mobj->extravalue2 = P_RandomRange(PR_SRB2KART, 64, 192);
			}

			if (mobj->reactiontime)
				mobj->reactiontime--;
			else
			{
				if (!mobj->extravalue1)
				{
					mobj_t *head = P_SpawnMobj(mobj->x, mobj->y, mobj->z, (blue ? MT_BLUEROBRA_HEAD : MT_ROBRA_HEAD));
					P_SetTarget(&mobj->tracer, head);

					mobj->destscale = mapobjectscale;
					P_SetTarget(&mobj->tracer->target, mobj->target);
					P_SetTarget(&mobj->tracer->tracer, mobj);
					mobj->tracer->extravalue2 = mobj->extravalue2;

					if (!blue)
						mobj->tracer->angle = mobj->angle;

					mobj->extravalue1 = 1;
				}
			}

			if ((mobj->extravalue1) && !(mobj->tracer && !P_MobjWasRemoved(mobj->tracer)))
			{
				mobj->reactiontime = 20*mobj->info->reactiontime;
				P_SetTarget(&mobj->target, NULL);
				mobj->extravalue1 = 0;
			}

			if ((mobj->tracer && !P_MobjWasRemoved(mobj->tracer)) && !(leveltime % 10))
			{
				fixed_t rand_x;
				fixed_t rand_y;
				fixed_t rand_z;

				rand_z = mobj->z + (P_RandomRange(PR_DECORATION, 0, 2)<<FRACBITS);
				rand_y = mobj->y + (P_RandomRange(PR_DECORATION, -4, 4)<<FRACBITS);
				rand_x = mobj->x + (P_RandomRange(PR_DECORATION, -4, 4)<<FRACBITS);

				mobj_t *dust = P_SpawnMobj(rand_x, rand_y, rand_z, MT_BBZDUST);
				P_SetScale(dust, mobj->scale/2);
				P_InstaThrust(dust, FixedAngle(P_RandomRange(PR_DECORATION,0,359)<<FRACBITS), abs(mobj->tracer->momz)/2);

				if (abs(mobj->tracer->momz) >= 2<<FRACBITS)
					S_StartSound(mobj, sfx_s3k7e);
			}
		}
		break;
	case MT_ROBRA_HEAD:
	case MT_BLUEROBRA_HEAD:
		if (mobj->health)
		{
			dboolean blue = (mobj->type == MT_BLUEROBRA_HEAD);
			uint8_t locnumsegs = abs(mobj->z - mobj->floorz) / (32 * mobj->scale);
			uint8_t i;
			mobj_t *cur = mobj->hnext, *prev = mobj;

			if (blue)
				mobj->angle = (angle_t)mobj->extravalue1;
			mobj->extravalue1 += (FixedAngle(2*mobj->momz) * (blue ? -1 : 1));

			for (i = 0; i < locnumsegs*2; i++) // *2 to check for any extra segs still present
			{
				fixed_t segz = mobj->z - ((i+1) * (32 * mobj->scale));

				if (cur && !P_MobjWasRemoved(cur))
				{
					if (i >= locnumsegs) // Remove extras
					{
						mobj_t *next = cur->hnext;
						P_RemoveMobj(cur);
						cur = next;
						continue;
					}
					else // Move into place
						P_MoveOrigin(cur, mobj->x, mobj->y, segz);
				}
				else
				{
					if (i >= locnumsegs) // We're done with this list
						continue; //break;
					else // Need another here!
						cur = P_SpawnMobj(mobj->x, mobj->y, segz, (blue ? MT_BLUEROBRA_JOINT : MT_ROBRA_JOINT));
				}

				P_SetTarget(&cur->target, mobj);
				P_SetScale(cur, (7*mobj->scale)/8);

				cur->angle = mobj->extravalue1;
				mobj->extravalue1 += (FixedAngle(2*mobj->momz) * (blue ? -1 : 1));

				P_SetTarget(&prev->hnext, cur);
				P_SetTarget(&cur->hprev, prev);

				prev = cur;
				cur = cur->hnext;
			}

			{
				//fixed_t ceilingheight = mobj->ceilingz - (72<<FRACBITS);
				fixed_t floorheight = mobj->floorz + (72<<FRACBITS);
				fixed_t maxheight = mobj->floorz + (mobj->extravalue2<<FRACBITS);
				fixed_t targetheight = maxheight;

				if (mobj->z < targetheight)
				{
					mobj->momz += mobj->info->speed;
					if ((mobj->z < floorheight) && (mobj->momz < 0))
						mobj->momz /= 2;
				}
				else
				{
					mobj->momz -= mobj->info->speed;
					if ((mobj->z > (targetheight + (64<<FRACBITS))) && (mobj->momz > 0))
						mobj->momz /= 2;
				}
			}
		}
		break;
	case MT_ROBRA_JOINT:
	case MT_BLUEROBRA_JOINT:
		if (!mobj->target || P_MobjWasRemoved(mobj->target))
		{
			P_RemoveMobj(mobj);
			return false;
		}
		break;
	case MT_SMK_PIPE:
		if (mobj->flags2 & MF2_AMBUSH)
			P_SetMobjStateNF(mobj, mobj->info->seestate);
		else
			P_SetMobjStateNF(mobj, mobj->info->spawnstate);
		break;
	case MT_SMK_MOLESPAWNER:
		if (!mobj->target || P_MobjWasRemoved(mobj->target))
		{
			mobj_t *newmole = P_SpawnMobj(mobj->x, mobj->y, mobj->z, MT_SMK_MOLE);
			P_SetTarget(&mobj->target, newmole);
			return false;
		}
		break;
	case MT_SMK_MOLE:
		if (mobj->target && !P_MobjWasRemoved(mobj->target) && mobj->target->player)
		{
			player_t *player = mobj->target->player;

			mobj->extravalue1 = 1;
			player->offroad += 2<<FRACBITS;

			P_MoveOrigin(mobj,
				player->mo->x + P_ReturnThrustX(NULL, player->mo->angle, player->mo->radius)
					+ P_ReturnThrustX(NULL, player->mo->angle+ANGLE_90, (mobj->threshold)<<FRACBITS),
				player->mo->y + P_ReturnThrustY(NULL, player->mo->angle, player->mo->radius)
					+ P_ReturnThrustY(NULL, player->mo->angle+ANGLE_90, (mobj->threshold)<<FRACBITS),
				player->mo->z + (player->mo->height/2 * P_MobjFlip(player->mo))
					+ (P_RandomRange(PR_SRB2KART, -abs(mobj->threshold), abs(mobj->threshold))<<FRACBITS));

			mobj->threshold /= 2;
			mobj->momz = 0;

			if (mobj->movecount > 8*TICRATE)
			{
				P_KillMobj(mobj, mobj->target, mobj->target, DMG_NORMAL);
				break;
			}

			if (abs(player->cmd.turning) > 100)
			{
				int32_t lastsign = 0;
				if (mobj->lastlook > 0)
					lastsign = 1;
				else if (mobj->lastlook < 0)
					lastsign = -1;

				if ((player->cmd.turning > 0 && lastsign < 0)
					|| (player->cmd.turning < 0 && lastsign > 0))
				{
					mobj->movecount += (TICRATE/2);
					mobj->threshold = 16*lastsign;
					S_StartSound(mobj, sfx_s1ab);
				}

				mobj->lastlook = player->cmd.turning;
			}

			/*
			if (player->cmd.shake > GAMEPADSHAKETHRESHOLD)
			{
				if (mobj->extravalue3 == 0) mobj->extravalue3 = 1;
				
				if (mobj->extravalue2 == 0)
				{
					mobj->movecount += (TICRATE/2);
					mobj->threshold = 16*mobj->extravalue3;
					S_StartSound(mobj, sfx_s1ab);
				}
				mobj->extravalue2 = 1;
				mobj->extravalue3 *= -1;
			}
			else
			{
				mobj->extravalue2 = 0;
			}
			*/

			mobj->movecount++;
		}
		else if (mobj->extravalue1) // lost your player somehow, DIE
		{
			P_KillMobj(mobj, NULL, NULL, DMG_NORMAL);
			break;
		}
		else
		{
			if (P_IsObjectOnGround(mobj))
			{
				if (mobj->reactiontime)
					mobj->reactiontime--;
				else
				{
					mobj->momz = (mobj->info->speed * P_MobjFlip(mobj));
					mobj->reactiontime = mobj->info->reactiontime;
				}
			}
		}
		break;
	case MT_SMK_THWOMP:
		if (mobj->flags2 & MF2_AMBUSH)
		{
			mobj->colorized = true;
			mobj->color = K_RainbowColor(leveltime);
			mobj->frame |= FF_FULLBRIGHT;
		}
		else
		{
			mobj->colorized = false;
			mobj->color = SKINCOLOR_NONE;
			mobj->frame &= (~FF_FULLBRIGHT);
		}

		if (!thwompsactive)
			break;

		if (mobj->reactiontime)
			mobj->reactiontime--;
		else
		{
			if (mobj->extravalue1)
			{
				P_SpawnGhostMobj(mobj)->tics = 3;

				if (mobj->z == mobj->floorz)
				{
					uint8_t i;

					mobj->extravalue1 = 0;
					mobj->reactiontime = mobj->info->reactiontime;
					S_StartSound(mobj, sfx_s1bd);

					for (i = 0; i < 8; i++)
					{
						mobj_t *dust = P_SpawnMobj(mobj->x, mobj->y, mobj->z, MT_DRIFTDUST);
						P_InstaThrust(dust, FixedAngle(((360*FRACUNIT)/8) * i), mobj->info->speed/8);
						dust->momz = P_MobjFlip(mobj) * (P_RandomRange(PR_THWOMP, 1,4)<<FRACBITS);
						dust->scale = mobj->scale/2;
						dust->destscale = mobj->scale*3;
					}
				}
				else
					mobj->momz = (-mobj->info->speed) * P_MobjFlip(mobj);
			}
			else
			{
				if (mobj->z > mobj->movefactor)
					mobj->z = mobj->movefactor;

				if (mobj->z == mobj->movefactor)
				{
					mobj->extravalue1 = 1;
					//S_StartSound(mobj, sfx_s1bb);
				}
				else
					mobj->momz = (mobj->info->speed/16) * P_MobjFlip(mobj);
			}
		}
		break;
	case MT_TRAPBUBBLE:
		P_TrapBubbleThink(mobj);
		break;
	case MT_TUMBLEGEM:
	case MT_TUMBLECOIN:
		mobj->friction = 95*FRACUNIT/100;
		if ((((statenum_t)(mobj->state-states)) == mobj->info->seestate)
			&& abs(mobj->momx) < mobj->scale && abs(mobj->momy) < mobj->scale && P_IsObjectOnGround(mobj))
		{
			mobj->momx = mobj->momy = 0;
			P_SetMobjState(mobj, mobj->info->spawnstate);
		}
		break;
	case MT_KARMAFIREWORK:
		if (mobj->flags & MF_NOGRAVITY)
			break;

		if (mobj->momz == 0)
		{
			P_RemoveMobj(mobj);
			return false;
		}
		else
		{
			mobj_t *trail = P_SpawnMobj(mobj->x, mobj->y, mobj->z, MT_THOK);
			P_SetMobjState(trail, S_KARMAFIREWORKTRAIL);
			P_SetScale(trail, mobj->scale);
			trail->destscale = 1;
			trail->scalespeed = mobj->scale/12;
			trail->color = mobj->color;
		}
		break;
	case MT_RAINBOWDASHRING:
		Obj_RainbowDashRingThink(mobj);
		break;
	default:
		// check mobj against possible water content, before movement code
		P_MobjCheckWater(mobj);
		// Extinguish fire objects in water
		if (mapnamespace != MNS_RINGRACERS
			&& mobj->flags & MF_FIRE
			&& (mapnamespace == MNS_SRB2KART ? mobj->type != MT_PUMA && mobj->type != MT_FIREBALL : !(mobj->eflags & MFE_TOUCHLAVA))
			&& (mobj->eflags & (MFE_UNDERWATER|MFE_TOUCHWATER)))
		{
			P_KillMobj(mobj, NULL, NULL, 0);
			return false;
		}
		break;
	}
	return true;
}

static void P_FiringThink(mobj_t *mobj)
{
	if (!mobj->target)
		return;

	if (mobj->health <= 0)
		return;

	if (mobj->state->action == (actionf_p1)A_Boss1Laser)
	{
		if (mobj->state->tics > 1)
		{
			var1 = mobj->state->var1;
			var2 = mobj->state->var2 & 65535;
			mobj->state->action(mobj);
		}
	}
	else if (leveltime & 1) // Fire mode
	{
		mobj_t *missile;

		mobj->angle = R_PointToAngle2(mobj->x, mobj->y, mobj->target->x, mobj->target->y);
		missile = P_SpawnMissile(mobj, mobj->target, mobj->extravalue1);

		if (missile)
		{
			if (mobj->flags2 & MF2_SUPERFIRE)
				missile->flags2 |= MF2_SUPERFIRE;

			if (mobj->info->attacksound)
				S_StartSound(missile, mobj->info->attacksound);
		}
	}
	else
		mobj->angle = R_PointToAngle2(mobj->x, mobj->y, mobj->target->x, mobj->target->y);
}

static void P_MonitorFuseThink(mobj_t *mobj)
{
	mobj_t *newmobj;

	// Special case for ALL monitors.
	// If a box's speed is nonzero, it's allowed to respawn as a WRM/SRM.
	if (mobj->info->speed != 0 && (mobj->flags2 & (MF2_AMBUSH|MF2_STRONGBOX)))
	{
		mobjtype_t spawnchance[64];
		int32_t numchoices = 0, i = 0;

		// This define should make it a lot easier to organize and change monitor weights
#define SETMONITORCHANCES(type, strongboxamt, weakboxamt) \
	for (i = ((mobj->flags2 & MF2_STRONGBOX) ? strongboxamt : weakboxamt); i; --i) spawnchance[numchoices++] = type

		//                Type            SRM WRM
		SETMONITORCHANCES(MT_SNEAKERTV,     0, 10); // Super Sneakers
		SETMONITORCHANCES(MT_INV,           2,  0); // Invincibility
		SETMONITORCHANCES(MT_WHITETV,       3,  8); // Whirlwind Shield
		SETMONITORCHANCES(MT_GREENTV,       3,  8); // Elemental Shield
		SETMONITORCHANCES(MT_YELLOWTV,      2,  0); // Attraction Shield
		SETMONITORCHANCES(MT_BLUETV,        3,  3); // Force Shield
		SETMONITORCHANCES(MT_BLACKTV,       2,  0); // Armageddon Shield
		SETMONITORCHANCES(MT_MIXUPBOX,      0,  1); // Teleporters
		SETMONITORCHANCES(MT_RECYCLETV,     0,  1); // Recycler
		SETMONITORCHANCES(MT_PRUP,          1,  1); // 1-Up
		// ======================================
		//                Total            16  32

#undef SETMONITORCHANCES

		i = P_RandomKey(PR_SRB2KART, numchoices); // Gotta love those random numbers!
		newmobj = P_SpawnMobj(mobj->x, mobj->y, mobj->z, spawnchance[i]);
	}
	else
		newmobj = P_SpawnMobj(mobj->x, mobj->y, mobj->z, mobj->type);

	// Transfer flags2 (ambush, strongbox, objectflip)
	newmobj->flags2 = mobj->flags2;
	P_RemoveMobj(mobj); // make sure they disappear
}

static dboolean P_FuseThink(mobj_t *mobj)
{
	if (mobj->fuse <= TICRATE && ((mobj->type == MT_RANDOMITEM && mobj->threshold == 69) || mobj->type == MT_EGGMANITEM || mobj->type == MT_FALLINGROCK))
		mobj->renderflags ^= RF_DONTDRAW;

	mobj->fuse--;

	if (mobj->fuse)
		return true;

	if (LUA_HookMobj(mobj, MOBJ_HOOK(MobjFuse)) || P_MobjWasRemoved(mobj))
		;
	else if (mobj->info->flags & MF_MONITOR)
	{
		P_MonitorFuseThink(mobj);
		return false;
	}
	else switch (mobj->type)
	{
		// gargoyle and snowman handled in P_PushableThinker, not here
	case MT_SPIKE:
	case MT_WALLSPIKE:
		P_SetMobjState(mobj, mobj->state->nextstate);
		mobj->fuse = mobj->args[0];
		break;
	case MT_RANDOMITEM:
		if (mobj->flags2 & MF2_DONTRESPAWN)
		{
			;
		}
		else if (gametypes[gametype]->rules & GTR_BATTLEBOXES)
		{
			if (mobj->threshold != 69)
				break;
		}
		else
		{
			mobj_t *newmobj;

			// Respawn from mapthing if you have one!
			if (mobj->spawnpoint)
			{
				P_SpawnMapThing(mobj->spawnpoint);
				newmobj = mobj->spawnpoint->mobj; // this is set to the new mobj in P_SpawnMapThing
			}
			else
				newmobj = P_SpawnMobj(mobj->x, mobj->y, mobj->z, mobj->type);

			// Transfer flags2 (strongbox, objectflip, bossnotrap)
			newmobj->flags2 = mobj->flags2;
		}

		P_RemoveMobj(mobj); // make sure they disappear
		return false;
	case MT_ITEMCAPSULE:
		if (mobj->spawnpoint)
			P_SpawnMapThing(mobj->spawnpoint);
		else
		{
			mobj_t *newMobj = P_SpawnMobj(mobj->x, mobj->y, mobj->z, mobj->type);
			newMobj->threshold = mobj->threshold;
			newMobj->movecount = mobj->movecount;
		}
		P_RemoveMobj(mobj);
		return false;
	case MT_SMK_ICEBLOCK:
		{
			mobj_t *cur = mobj->hnext, *next;
			uint8_t i;

			for (i = 0; i < 5; i++)
			{
				mobj_t *debris = P_SpawnMobj(mobj->x, mobj->y, mobj->z, MT_SMK_ICEBLOCK_DEBRIS);
				debris->angle = FixedAngle(P_RandomRange(PR_DECORATION,0,360)<<FRACBITS);
				P_InstaThrust(debris, debris->angle, P_RandomRange(PR_DECORATION,3,18)*(FRACUNIT/4));
				debris->momz = P_RandomRange(PR_DECORATION,4,8)<<FRACBITS;
				if (!i) // kinda hacky :V
					S_StartSound(debris, sfx_s3k82);
			}

			while (cur && !P_MobjWasRemoved(cur))
			{
				next = cur->hnext;
				P_RemoveMobj(cur);
				cur = next;
			}

			P_RemoveMobj(mobj);
			return false;
		}
	case MT_NIGHTSCORE:
		P_RemoveMobj(mobj);
		return false;
	case MT_PLAYER:
		break; // don't remove
	case MT_SNEAKERPANELSPAWNER:
	{
		Obj_SneakerPanelSpawnerFuse(mobj);
		break;
	}
	default:
		P_SetMobjState(mobj, mobj->info->xdeathstate); // will remove the mobj if S_NULL.
		break;
		// Looking for monitors? They moved to a special condition above.
	}

	return !P_MobjWasRemoved(mobj);
}

//
// P_MobjThinker
//
void P_MobjThinker(mobj_t *mobj)
{
	I_Assert(mobj != NULL);
	I_Assert(!P_MobjWasRemoved(mobj));

	// Remove dead target/tracer.
	if (mobj->target && P_MobjWasRemoved(mobj->target))
		P_SetTarget(&mobj->target, NULL);
	if (mobj->tracer && P_MobjWasRemoved(mobj->tracer))
		P_SetTarget(&mobj->tracer, NULL);
	if (mobj->hnext && P_MobjWasRemoved(mobj->hnext))
		P_SetTarget(&mobj->hnext, NULL);
	if (mobj->hprev && P_MobjWasRemoved(mobj->hprev))
		P_SetTarget(&mobj->hprev, NULL);
	if (mobj->itnext && P_MobjWasRemoved(mobj->itnext))
		P_SetTarget(&mobj->itnext, NULL);

	// Tick lifetime of mobj
	mobj->mobjlifetime++;

	if (mobj->flags & MF_NOTHINK)
		return;

	if ((mobj->flags & MF_BOSS) && (bossdisabled & (1 << mobj->args[0])))
		return;

	mobj->eflags &= ~(MFE_PUSHED|MFE_SPRUNG|MFE_JUSTBOUNCEDWALL|MFE_SLOPELAUNCHED);

	// sal: what the hell? is there any reason this isn't done, like, literally ANYWHERE else?
	P_SetTarget(&g_tm.floorthing, NULL);
	P_SetTarget(&g_tm.hitthing, NULL);

	if (udmf)
	{
		// Check for continuous sector special actions
		P_CheckMobjTouchingSectorActions(mobj, true, true);
	}
	else
	{
		// Sector flag MSF_TRIGGERLINE_MOBJ allows ANY mobj to trigger a linedef exec
		P_CheckMobjTrigger(mobj, false);
	}

	I_Assert(!P_MobjWasRemoved(mobj));

	if (mobj->scale != mobj->destscale)
	{
		P_MobjScaleThink(mobj); // Slowly scale up/down to reach your destscale.

		if (P_MobjWasRemoved(mobj))
			return;
	}

	if (mobj->type == MT_GHOST && mobj->fuse > 0) // Not guaranteed to be MF_SCENERY or not MF_SCENERY!
	{
		if (mobj->extravalue1 > 0) // Sonic Advance 2 mode
		{
			if (mobj->extravalue2 >= 2)
			{
				uint32_t dontdraw = RF_DONTDRAW;

				if (mobj->tracer)
					dontdraw &= ~(mobj->tracer->renderflags);

				if (mobj->extravalue2 == 2) // I don't know why the normal logic doesn't work for this.
					mobj->renderflags ^= dontdraw;
				else
				{
					if (mobj->fuse == mobj->extravalue2)
						mobj->renderflags &= ~(dontdraw);
					else
						mobj->renderflags |= dontdraw;
				}
			}
		}
		else if (mobj->extravalue3 <= 0) // Legacy mode bypasses this visibility system.
		{
			uint32_t dur = (mobj->flags2 & MF2_BOSSNOTRAP)
				? (2*mobj->fuse)/3
				: mobj->fuse/2;
			if (((mobj->renderflags & RF_TRANSMASK) >> RF_TRANSSHIFT) < ((NUMTRANSMAPS-1) - dur))
				// fade out when nearing the end of fuse...
				mobj->renderflags = (mobj->renderflags & ~RF_TRANSMASK) | (((NUMTRANSMAPS-1) - dur) << RF_TRANSSHIFT);
		}
	}

	// Special thinker for scenery objects
	if (mobj->flags & MF_SCENERY && !mobj->player)
	{
		P_MobjSceneryThink(mobj);
		return;
	}

	// Check for a Lua thinker first
	if (!mobj->player)
	{
		if (LUA_HookMobj(mobj, MOBJ_HOOK(MobjThinker)) || P_MobjWasRemoved(mobj))
			return;
	}
	else if (!mobj->player->spectator)
	{
		// You cannot short-circuit the player thinker like you can other thinkers.
		LUA_HookMobj(mobj, MOBJ_HOOK(MobjThinker));
		if (P_MobjWasRemoved(mobj))
			return;
	}

	// if it's pushable, or if it would be pushable other than temporary disablement, use the
	// separate thinker
	if (mobj->flags & MF_PUSHABLE || (mobj->info->flags & MF_PUSHABLE && mobj->fuse))
	{
		if (!P_MobjPushableThink(mobj))
			return;
	}
	else if (mobj->flags & MF_BOSS)
	{
		if (!P_MobjBossThink(mobj))
			return;
	}
	else if (mobj->health <= 0) // Dead things think differently than the living.
	{
		if (!P_MobjDeadThink(mobj))
			return;
	}
	else
	{
		if (!P_MobjRegularThink(mobj))
			return;
	}
	if (P_MobjWasRemoved(mobj))
		return;

	// Destroy items sector special
	if (P_CanDeleteKartItem(mobj->type))
	{
		if (mobj->health > 0 && P_MobjTouchingSectorSpecialFlag(mobj, SSF_DELETEITEMS))
		{
			if (mobj->type == MT_SSMINE
			|| mobj->type == MT_BALLHOG)
			{
				S_StartSound(mobj, mobj->info->deathsound);
				P_KillMobj(mobj, NULL, NULL, DMG_NORMAL);
			}
			else
			{
				// This Item Damage
				if (mobj->eflags & MFE_VERTICALFLIP)
					mobj->z -= mobj->height;
				else
					mobj->z += mobj->height;

				S_StartSound(mobj, mobj->info->deathsound);
				P_KillMobj(mobj, NULL, NULL, DMG_NORMAL);

				P_SetObjectMomZ(mobj, 8*FRACUNIT, false);
				P_InstaThrust(mobj, R_PointToAngle2(0, 0, mobj->momx, mobj->momy) + ANGLE_90, 16*FRACUNIT);
			}

			return;
		}
	}

	if (mobj->flags2 & MF2_FIRING)
		P_FiringThink(mobj);

	if (mobj->flags & MF_AMBIENT)
	{
		if (leveltime % mobj->health)
			return;
		if (mobj->threshold)
			S_StartSound(mobj, mobj->threshold);
		return;
	}

	// Check fuse
	if (mobj->fuse && !P_FuseThink(mobj))
		return;

	I_Assert(mobj != NULL);
	I_Assert(!P_MobjWasRemoved(mobj));

	if (mobj->momx || mobj->momy || (mobj->flags2 & MF2_SKULLFLY))
	{
		P_XYMovement(mobj);
		if (P_MobjWasRemoved(mobj))
			return;
	}

	// always do the gravity bit now, that's simpler
	// BUT CheckPosition only if wasn't done before.
	if (!(mobj->eflags & MFE_ONGROUND) || mobj->momz
		|| ((mobj->eflags & MFE_VERTICALFLIP) && mobj->z + mobj->height != mobj->ceilingz)
		|| (!(mobj->eflags & MFE_VERTICALFLIP) && mobj->z != mobj->floorz)
		|| P_IsObjectInGoop(mobj))
	{
		if (!P_ZMovement(mobj))
			return; // mobj was removed
		P_CheckPosition(mobj, mobj->x, mobj->y, NULL); // Need this to pick up objects!
		if (P_MobjWasRemoved(mobj))
			return;
	}
	else
	{
		mobj->pmomz = 0; // to prevent that weird rocketing gargoyle bug
		mobj->eflags &= ~MFE_JUSTHITFLOOR;
	}

	// Sliding physics for slidey mobjs!
	if (mobj->type == MT_FLINGRING
		|| mobj->type == MT_FLINGCOIN
		|| mobj->type == MT_FLINGNIGHTSCHIP
		|| mobj->type == MT_FLINGEMERALD
		|| mobj->type == MT_BIGTUMBLEWEED
		|| mobj->type == MT_LITTLETUMBLEWEED
		|| mobj->type == MT_CANNONBALLDECOR
		|| mobj->type == MT_FALLINGROCK)
	{
		P_TryMove(mobj, mobj->x, mobj->y, true, NULL); // Sets mo->standingslope correctly

		if (P_MobjWasRemoved(mobj)) // anything that calls checkposition can be lethal
			return;

		//if (mobj->standingslope) CONS_Printf("slope physics on mobj\n");
		P_ButteredSlope(mobj);
	}

	K_UpdateTerrainOverlay(mobj);

	if (mobj->flags & (MF_ENEMY|MF_BOSS) && mobj->health
		&& P_CheckDeathPitCollide(mobj)) // extra pit check in case these didn't have momz
	{
		P_KillMobj(mobj, NULL, NULL, DMG_DEATHPIT);
		return;
	}

	// Crush enemies!
	if (mobj->ceilingz - mobj->floorz < mobj->height)
	{
		if ((
		(mobj->flags & (MF_ENEMY|MF_BOSS)
			&& mobj->flags & MF_SHOOTABLE)
		|| mobj->type == MT_EGGSHIELD)
		&& !(mobj->flags & MF_NOCLIPHEIGHT)
		&& mobj->health > 0)
		{
			P_KillMobj(mobj, NULL, NULL, DMG_CRUSHED);
			return;
		}
	}

	if (P_MobjWasRemoved(mobj))
		return; // obligatory paranoia check

	// Can end up here if a player dies.
	if (mobj->player)
		P_CyclePlayerMobjState(mobj);
	else
		P_CycleMobjState(mobj);

	if (P_MobjWasRemoved(mobj))
		return;

	switch (mobj->type)
	{
		case MT_BOUNCEPICKUP:
		case MT_RAILPICKUP:
		case MT_AUTOPICKUP:
		case MT_EXPLODEPICKUP:
		case MT_SCATTERPICKUP:
		case MT_GRENADEPICKUP:
			if (mobj->health == 0) // Fading tile
			{
				int32_t value = mobj->info->damage/10;
				value = mobj->fuse/value;
				value = 10-value;
				value--;

				if (value <= 0)
					value = 1;

				mobj->frame &= ~FF_TRANSMASK;
				mobj->frame |= value << FF_TRANSSHIFT;
			}
			break;
		default:
			break;
	}
}

// Quick, optimized function for the Rail Rings
// Returns true if move failed or mobj was removed by movement (death pit, missile hits wall, etc.)
dboolean P_RailThinker(mobj_t *mobj)
{
	fixed_t x, y, z;

	I_Assert(mobj != NULL);
	I_Assert(!P_MobjWasRemoved(mobj));

	x = mobj->x, y = mobj->y, z = mobj->z;

	if (mobj->momx || mobj->momy)
	{
		P_XYMovement(mobj);
		if (P_MobjWasRemoved(mobj))
			return true;
	}

	if (mobj->momz)
	{
		if (!P_ZMovement(mobj))
			return true; // mobj was removed
		//P_CheckPosition(mobj, mobj->x, mobj->y, NULL);
	}

	return P_MobjWasRemoved(mobj) || (x == mobj->x && y == mobj->y && z == mobj->z);
}

// Unquick, unoptimized function for pushables
void P_PushableThinker(mobj_t *mobj)
{
	I_Assert(mobj != NULL);
	I_Assert(!P_MobjWasRemoved(mobj));

	P_CheckMobjTrigger(mobj, true);

	// it has to be pushable RIGHT NOW for this part to happen
	if (mobj->flags & MF_PUSHABLE && !(mobj->momx || mobj->momy))
		P_TryMove(mobj, mobj->x, mobj->y, true, NULL);

	if (mobj->fuse == 1) // it would explode in the MobjThinker code
	{
		mobj_t *spawnmo;
		fixed_t x, y, z;
		subsector_t *ss;

		// Left here just in case we'd
		// want to make pushable bombs
		// or something in the future.
		switch (mobj->type)
		{
			case MT_SNOWMAN:
			case MT_GARGOYLE:
				x = mobj->spawnpoint->x << FRACBITS;
				y = mobj->spawnpoint->y << FRACBITS;

				ss = R_PointInSubsector(x, y);

				if (mobj->spawnpoint->z != 0)
					z = mobj->spawnpoint->z << FRACBITS;
				else
					z = ss->sector->floorheight;

				spawnmo = P_SpawnMobj(x, y, z, mobj->type);
				spawnmo->spawnpoint = mobj->spawnpoint;
				P_UnsetThingPosition(spawnmo);
				spawnmo->flags = mobj->flags;
				P_SetThingPosition(spawnmo);
				spawnmo->flags2 = mobj->flags2;
				spawnmo->flags |= MF_PUSHABLE;
				P_RemoveMobj(mobj);
				break;
			default:
				break;
		}
	}
}

// Quick, optimized function for scenery
void P_SceneryThinker(mobj_t *mobj)
{
	if (mobj->flags & MF_BOXICON)
	{
		if (!(mobj->eflags & MFE_VERTICALFLIP))
		{
			if (mobj->z < mobj->floorz + FixedMul(mobj->info->damage, mobj->scale))
				mobj->momz = FixedMul(mobj->info->speed, mobj->scale);
			else
				mobj->momz = 0;
		}
		else
		{
			if (mobj->z + FixedMul(mobj->info->height, mobj->scale) > mobj->ceilingz - FixedMul(mobj->info->damage, mobj->scale))
				mobj->momz = -FixedMul(mobj->info->speed, mobj->scale);
			else
				mobj->momz = 0;
		}
	}

	// momentum movement
	if (mobj->momx || mobj->momy)
	{
		P_SceneryXYMovement(mobj);

		if (P_MobjWasRemoved(mobj))
			return;
	}

	// always do the gravity bit now, that's simpler
	// BUT CheckPosition only if wasn't done before.
	if (!(mobj->eflags & MFE_ONGROUND) || mobj->momz
		|| ((mobj->eflags & MFE_VERTICALFLIP) && mobj->z + mobj->height != mobj->ceilingz)
		|| (!(mobj->eflags & MFE_VERTICALFLIP) && mobj->z != mobj->floorz)
		|| P_IsObjectInGoop(mobj))
	{
		if (!P_SceneryZMovement(mobj))
			return; // mobj was removed
		P_CheckPosition(mobj, mobj->x, mobj->y, NULL); // Need this to pick up objects!
		if (P_MobjWasRemoved(mobj))
			return;
		mobj->floorz = g_tm.floorz;
		mobj->ceilingz = g_tm.ceilingz;
		mobj->floorrover = g_tm.floorrover;
		mobj->ceilingrover = g_tm.ceilingrover;
	}
	else
	{
		mobj->pmomz = 0; // to prevent that weird rocketing gargoyle bug
		mobj->eflags &= ~MFE_JUSTHITFLOOR;
	}

	P_CycleMobjState(mobj);

	if (mobj->type != MT_RANDOMAUDIENCE)
		return;

	{
		if (!mobj->colorized) // a fan of someone?
			return;

		if (mobj->threshold >= 0) // not already happy or sad?
		{
			if (!playeringame[mobj->threshold] || players[mobj->threshold].spectator) // focused on a valid player?
				return;

			if (!(players[mobj->threshold].exiting) && !(players[mobj->threshold].pflags & PF_NOCONTEST)) // not finished yet?
				return;

			if (K_IsPlayerLosing(&players[mobj->threshold]))
				mobj->threshold = -2;
			else
			{
				mobj->threshold = -1;
				S_StartSound(mobj, sfx_chaooo);
			}
		}

		if (mobj->threshold == -1)
			mobj->angle += ANGLE_22h;

		if (((statenum_t)(mobj->state-states) != S_AUDIENCE_CHAO_CHEER2) || (mobj->tics != states[S_AUDIENCE_CHAO_CHEER2].tics)) // not at the start of your cheer jump?
			return;

		mobj->momz = 0;

		P_SetMobjState(mobj, ((mobj->threshold == -1) ? S_AUDIENCE_CHAO_WIN2 : S_AUDIENCE_CHAO_LOSE));
	}
}

//
// GAME SPAWN FUNCTIONS
//

static void P_DefaultMobjShadowScale(mobj_t *thing)
{
	thing->shadowscale = 0;
	thing->whiteshadow = ((thing->frame & FF_BRIGHTMASK) == FF_FULLBRIGHT);
	thing->shadowcolor = 15;

	// Those have shadow by default
	switch (thing->type)
	{
		case MT_PLAYER:
		case MT_KART_LEFTOVER:
		case MT_SMALLMACE:
		case MT_BIGMACE:
		case MT_PUMA:
		case MT_BIGPUMA:
		case MT_FALLINGROCK:
		case MT_SMK_MOLE:
		case MT_SMK_THWOMP:
		case MT_BATTLEBUMPER:
		case MT_BANANA:
		case MT_ORBINAUT:
		case MT_ORBINAUT_SHIELD:
		case MT_JAWZ:
		case MT_JAWZ_DUD:
		case MT_JAWZ_SHIELD:
		case MT_SSMINE:
		case MT_SSMINE_SHIELD:
		case MT_LANDMINE:
		case MT_EGGMINE:
		case MT_EGGMINE_SHIELD:
		case MT_BALLHOG:
		case MT_SINK:
		case MT_ROCKETSNEAKER:
		case MT_SPB:
		case MT_ADVENTURESPIKEA:
		case MT_ADVENTURESPIKEB:
		case MT_ADVENTURESPIKEC:
		case MT_CDUFO:
			thing->shadowscale = 4*FRACUNIT/3;
			break;
		case MT_BANANA_SHIELD:
			thing->shadowscale = 12*FRACUNIT/5;
			break;
		case MT_RANDOMITEM:
			thing->shadowscale = FRACUNIT/2;
			thing->whiteshadow = false;
			break;
		case MT_EGGMANITEM:
			thing->shadowscale = FRACUNIT;
			thing->whiteshadow = false;
			break;
		case MT_EGGMANITEM_SHIELD:
			thing->shadowscale = 3*FRACUNIT/2;
			thing->whiteshadow = false;
			break;
		case MT_FLOATINGITEM:
		case MT_ITEMCAPSULE:
			thing->shadowscale = FRACUNIT/2;
			break;
		case MT_THUNDERSHIELD:
		case MT_BUBBLESHIELD:
		case MT_FLAMESHIELD:
		case MT_ATTRACTIONSHIELD:
		case MT_BLUEFRUIT:
		case MT_ORANGEFRUIT:
		case MT_REDFRUIT:
		case MT_PINKFRUIT:
		case MT_RANDOMAUDIENCE:
		case MT_BLUECRAWLA:
		case MT_REDCRAWLA:
		case MT_GFZFISH: // Greenflower Fish
		case MT_GOLDBUZZ:
		case MT_REDBUZZ:
		case MT_AQUABUZZ: // AquaBuzz for ATZ
		case MT_JETTBOMBER: // Jetty-Syn Bomber
		case MT_JETTGUNNER: // Jetty-Syn Gunner
		case MT_CRAWLACOMMANDER: // Crawla Commander
		case MT_DETON: // Deton
		case MT_SKIM: // Skim mine dropper
		case MT_TURRET:
		case MT_POPUPTURRET:
		case MT_SHARP: // Sharp
		case MT_JETJAW: // Jet Jaw
		case MT_SNAILER: // Snailer
		case MT_VULTURE: // Vulture
		case MT_POINTY: // Pointy
		case MT_POINTYBALL: // Pointy Ball
		case MT_ROBOHOOD: // Robo-Hood
		case MT_FACESTABBER: // CastleBot FaceStabber
		case MT_EGGGUARD: // Egg Guard
		case MT_EGGSHIELD: // Egg Shield for Egg Guard
		case MT_GSNAPPER: // Green Snapper
		case MT_MINUS: // Minus
		case MT_SPRINGSHELL: // Spring Shell
		case MT_YELLOWSHELL: // Spring Shell (yellow)
		case MT_UNIDUS: // Unidus
		case MT_UNIBALL: // Unidus Ball
		case MT_GOOMBA:
		case MT_BLUEGOOMBA:
		case MT_KOOPA:
		case MT_MONOKUMA:
		case MT_ARIDTOAD:
		case MT_SRB1_CRAWLA:
		case MT_SRB1_BAT:
		case MT_SRB1_ROBOFISH:
		case MT_SRB1_VOLCANOGUY:
		case MT_SRB1_HOPPY:
		case MT_SRB1_HOPPYWATER:
		case MT_SRB1_HOPPYSKYLAB:
		case MT_SRB1_MMZFLYING:
		case MT_SRB1_UFO:
		case MT_SRB1_GRAYBOT:
		case MT_SRB1_ROBOTOPOLIS:
		case MT_SRB1_RBZBUZZ:
		case MT_SRB1_RBZSPIKES:
		case MT_SRB1_METALSONIC:
		case MT_SRB1_GOLDBOT:
		case MT_SRB1_GENREX:
		case MT_BOWLINGPIN:
		case MT_EXPLODINGBARREL:
		case MT_FROGGER:
		case MT_ROBRA:
		case MT_CHOMPER:
		case MT_BUZZBOMBER:
		case MT_GARGOYLE:
		case MT_GBA_BOO:
		case MT_FLYINGGARG:
			thing->shadowscale = FRACUNIT;
			break;
		case MT_RING:
		case MT_FLINGRING:
		case MT_COIN:
		case MT_FLINGCOIN:
		case MT_REDTEAMRING:
		case MT_BLUETEAMRING:
		case MT_REDFLAG:
		case MT_BLUEFLAG:
		case MT_BOUNCERING:
		case MT_AUTOMATICRING:
		case MT_INFINITYRING:
		case MT_RAILRING:
		case MT_EXPLOSIONRING:
		case MT_SCATTERRING:
		case MT_GRENADERING:
		case MT_BOUNCEPICKUP:
		case MT_RAILPICKUP:
		case MT_AUTOPICKUP:
		case MT_EXPLODEPICKUP:
		case MT_SCATTERPICKUP:
		case MT_GRENADEPICKUP:
		case MT_REDRING:
		case MT_THROWNBOUNCE:
		case MT_THROWNINFINITY:
		case MT_THROWNAUTOMATIC:
		case MT_THROWNSCATTER:
		case MT_THROWNEXPLOSION:
		case MT_THROWNGRENADE:
		case MT_EMBLEM:
		case MT_DOOD_BOX:
		case MT_DOOD_BALLOON:
			thing->shadowscale = FRACUNIT/3;
			break;
		case MT_SNEAKERPANEL:
			thing->shadowscale = 0;
			break;
		default:
			if (thing->flags & (MF_ENEMY|MF_BOSS))
				thing->shadowscale = FRACUNIT;
			break;
	}
}

mobj_t *P_AllocateMobj(void)
{
	mobj_t* mobj = (mobj_t*)Z_LevelPoolCalloc(sizeof(mobj_t));
	mobj->thinker.alloctype = TAT_LEVELPOOL;
	mobj->thinker.size = sizeof(mobj_t);
	return mobj;
}

//
// P_SpawnMobj
//
mobj_t *P_SpawnMobj(fixed_t x, fixed_t y, fixed_t z, mobjtype_t type)
{
	const mobjinfo_t *info = &mobjinfo[type];
	int8_t sc = -1;
	state_t *st;
	mobj_t *mobj;

	if (type == MT_NULL)
	{
#if 0
#ifdef PARANOIA
		I_Error("Tried to spawn MT_NULL\n");
#endif
		return NULL;
#endif
		// Hack: Some code assumes that P_SpawnMobj can never return NULL
		// So replace MT_NULL with MT_RAY in the meantime
		// Remove when dealt properly
		CONS_Debug(DBG_GAMELOGIC, "Tried to spawn MT_NULL, using MT_RAY\n");
		type = MT_RAY;
	}

	mobj = P_AllocateMobj();

	// this is officially a mobj, declared as soon as possible.
	mobj->thinker.function = (actionf_p1)P_MobjThinker;
	mobj->type = type;
	mobj->info = info;

	mobj->x = x;
	mobj->y = y;

	mobj->radius = info->radius;
	mobj->height = info->height;
	mobj->flags = info->flags;

	mobj->health = (info->spawnhealth ? info->spawnhealth : 1);

	mobj->reactiontime = info->reactiontime;

	mobj->lastlook = -1; // stuff moved in P_enemy.P_LookForPlayer

	// do not set the state with P_SetMobjState,
	// because action routines can not be called yet
	st = &states[info->spawnstate];

	mobj->state = st;
	mobj->tics = st->tics;
	mobj->sprite = st->sprite;
	mobj->frame = st->frame; // FF_FRAMEMASK for frame, and other bits..
	P_SetupStateAnimation(mobj, st);

	mobj->friction = ORIG_FRICTION;

	mobj->movefactor = FRACUNIT;
	mobj->gravity = FRACUNIT;

	// All mobjs are created at 100% scale.
	mobj->scale = FRACUNIT;
	mobj->destscale = mobj->scale;
	mobj->scalespeed = FRACUNIT/12;

	if (mapobjectscale != FRACUNIT) //&& !(mobj->type == MT_BLACKEGGMAN)
	{
		mobj->destscale = mapobjectscale;
		mobj->scalespeed = mapobjectscale/12;
	}

	// Sprite rendering
	mobj->spritexscale = mobj->spriteyscale = mobj->scale;
	mobj->spritexoffset = mobj->spriteyoffset = 0;
	mobj->dispoffset = info->dispoffset;
	mobj->floorspriteslope = NULL;

	// set subsector and/or block links
	P_SetThingPosition(mobj);
	I_Assert(mobj->subsector != NULL);

	// Make sure scale matches destscale immediately when spawned
	P_SetScale(mobj, mobj->destscale);

	mobj->floorz   = P_GetSectorFloorZAt  (mobj->subsector->sector, x, y);
	mobj->ceilingz = P_GetSectorCeilingZAt(mobj->subsector->sector, x, y);

	mobj->floorrover = NULL;
	mobj->ceilingrover = NULL;

	// Tells MobjCheckWater that the water height was not set.
	mobj->watertop = INT32_MAX;

	if (z == ONFLOORZ)
	{
		mobj->z = mobj->floorz;

		if (mobj->type == MT_UNIDUS)
			mobj->z += FixedMul(mobj->info->mass, mobj->scale);

		// defaults onground
		if (mobj->z == mobj->floorz)
			mobj->eflags |= MFE_ONGROUND;
	}
	else if (z == ONCEILINGZ)
	{
		mobj->z = mobj->ceilingz - mobj->height;

		if (mobj->type == MT_UNIDUS)
			mobj->z -= FixedMul(mobj->info->mass, mobj->scale);

		// defaults onground
		if (mobj->z + mobj->height == mobj->ceilingz)
			mobj->eflags |= MFE_ONGROUND;
	}
	else
		mobj->z = z;

	mobj->colorized = false;

	// Set shadowscale here, before spawn hook so that Lua can change it
	P_DefaultMobjShadowScale(mobj);

	if (!(mobj->flags & MF_NOTHINK))
		P_AddThinker(THINK_MOBJ, &mobj->thinker);

	// DANGER! This can cause P_SpawnMobj to return NULL!
	// Avoid using P_RemoveMobj on the newly created mobj in "MobjSpawn" Lua hooks!
	if (LUA_HookMobj(mobj, MOBJ_HOOK(MobjSpawn)))
	{
		if (P_MobjWasRemoved(mobj))
			return NULL;
	}
	else if (P_MobjWasRemoved(mobj))
		return NULL;
	else
	switch (mobj->type)
	{
		case MT_ALTVIEWMAN:
			if (titlemapinaction) mobj->flags &= ~MF_NOTHINK;
			break;
		case MT_LOCKONINF:
			P_SetScale(mobj, (mobj->destscale = 3*mobj->scale));
			break;
		case MT_CYBRAKDEMON_NAPALM_BOMB_LARGE:
			mobj->fuse = mobj->info->painchance;
			break;
		case MT_BLACKEGGMAN:
			{
				mobj_t *spawn = P_SpawnMobj(mobj->x, mobj->z, mobj->z+mobj->height-16*FRACUNIT, MT_BLACKEGGMAN_HELPER);
				spawn->destscale = mobj->scale;
				P_SetScale(spawn, mobj->scale);
				P_SetTarget(&spawn->target, mobj);
			}
			break;
		case MT_FAKEMOBILE:
		case MT_EGGSHIELD:
			mobj->flags2 |= MF2_INVERTAIMABLE;
			break;
		case MT_DETON:
			mobj->movedir = 0;
			break;
		case MT_EGGGUARD:
			{
				mobj_t *spawn = P_SpawnMobj(x, y, z, MT_EGGSHIELD);
				spawn->destscale = mobj->scale;
				P_SetScale(spawn, mobj->scale);
				P_SetTarget(&mobj->tracer, spawn);
				P_SetTarget(&spawn->target, mobj);
			}
			break;
		case MT_UNIDUS:
			{
				int32_t i;
				mobj_t *ball;
				// Spawn "damage" number of "painchance" spikeball mobjs
				// threshold is the distance they should keep from the MT_UNIDUS (touching radius + ball painchance)
				for (i = 0; i < mobj->info->damage; i++)
				{
					ball = P_SpawnMobj(x, y, z, mobj->info->painchance);
					ball->destscale = mobj->scale;
					P_SetScale(ball, mobj->scale);
					P_SetTarget(&ball->target, mobj);
					ball->movedir = FixedAngle(FixedMul(FixedDiv(i<<FRACBITS, mobj->info->damage<<FRACBITS), 360<<FRACBITS));
					ball->threshold = ball->radius + mobj->radius + FixedMul(ball->info->painchance, ball->scale);

					var1 = ball->state->var1, var2 = ball->state->var2;
					ball->state->action(ball);
				}
			}
			break;
		case MT_POINTY:
			{
				int32_t q;
				mobj_t *ball, *lastball = mobj;

				for (q = 0; q < mobj->info->painchance; q++)
				{
					ball = P_SpawnMobj(x, y, z, mobj->info->mass);
					ball->destscale = mobj->scale;
					P_SetScale(ball, mobj->scale);
					P_SetTarget(&lastball->tracer, ball);
					P_SetTarget(&ball->target, mobj);
					lastball = ball;
				}
			}
			break;
		case MT_BIGMINE:
			mobj->extravalue1 = FixedHypot(mobj->x, mobj->y)>>FRACBITS;
			break;
		case MT_EGGMOBILE2:
			// Special condition for the 2nd boss.
			mobj->watertop = mobj->info->speed;
			break;
		case MT_EGGMOBILE3:
			mobj->movefactor = -512*FRACUNIT;
			mobj->flags2 |= MF2_CLASSICPUSH;
			break;
		case MT_EGGMOBILE4:
			mobj->flags2 |= MF2_INVERTAIMABLE;
			break;
		case MT_FLICKY_08:
			mobj->color = (P_RandomChance(PR_FLICKY, FRACUNIT/2) ? SKINCOLOR_RED : SKINCOLOR_AQUAMARINE);
			break;
		case MT_BALLOON:
			{
				static const uint8_t BALLOONCOLORS[] = {
					// Carnival Night balloon colors
					SKINCOLOR_KETCHUP,
					SKINCOLOR_SAPPHIRE,
					SKINCOLOR_TANGERINE,
					SKINCOLOR_JET
				};

				mobj->color = BALLOONCOLORS[P_RandomKey(PR_SRB2KART, sizeof(BALLOONCOLORS))];
			}
			break;
		case MT_KART_LEFTOVER:
			mobj->color = SKINCOLOR_RED;
			break;
		case MT_REDRING: // Make MT_REDRING red by default
			mobj->color = skincolor_redring;
			break;
		case MT_SMALLBUBBLE: // Bubbles eventually dissipate, in case they get caught somewhere.
		case MT_MEDIUMBUBBLE:
		case MT_EXTRALARGEBUBBLE:
			mobj->fuse += 30 * TICRATE;
			break;
		case MT_NIGHTSDRONE:
			nummaprings = -1; // no perfect bonus, rings are free
			break;
		case MT_EGGCAPSULE:
			mobj->reactiontime = 0;
			mobj->extravalue1 = mobj->cvmem =\
			 mobj->cusval = mobj->movecount =\
			 mobj->lastlook = mobj->extravalue2 = -1;
			break;
		case MT_REDTEAMRING:
			mobj->color = skincolor_redteam;
			break;
		case MT_BLUETEAMRING:
			mobj->color = skincolor_blueteam;
			break;
		case MT_RING:
		case MT_COIN:
		case MT_BLUEBALL:
			if (nummaprings >= 0)
				nummaprings++;
			break;
		// SRB2Kart
		case MT_EGGMINE:
		case MT_EGGMINE_SHIELD:
		{
			mobj_t *overlay = P_SpawnMobj(mobj->x, mobj->y, mobj->z, MT_OVERLAY);
			P_SetMobjState(overlay, S_EGGMINE_OVERLAY);
			overlay->destscale = mobj->scale;
			P_SetScale(overlay, mobj->scale);
			P_SetTarget(&overlay->target, mobj);
			P_SetTarget(&overlay->tracer, mobj);
			overlay->flags2 |= MF2_LINKDRAW;
			overlay->dispoffset = -1;

			mobj->movefactor = TICRATE;
			mobj->threshold = 6;
			break;
		}
		case MT_BUBBLESHIELD:
		{
			// Spawn in the damage overlay
			mobj_t *spawn = P_SpawnMobj(mobj->x, mobj->z, mobj->z, MT_OVERLAY);
			spawn->destscale = mobj->scale;
			P_SetScale(spawn, mobj->scale);
			P_SetTarget(&mobj->tracer, spawn);
			P_SetTarget(&spawn->target, mobj);
			break;
		}
		case MT_BUBBLESHIELD_DEBRIS:
		{
			statenum_t shardset[] = {S_BUBBLEDEBRIS_1, S_BUBBLEDEBRIS_2, S_BUBBLEDEBRIS_3, S_BUBBLEDEBRIS_4, S_BUBBLEDEBRIS_5};
			P_SetMobjState(mobj, shardset[P_RandomRange(PR_ITEM_BUBBLE, 0, 4)]);

			break;
		}
		case MT_ITEMCAPSULE:
		{
			// set default item & count
#if 0 // set to 1 to test capsules with random items, e.g. with objectplace
			if (P_RandomChance(PR_ITEM_CAPSULE, FRACUNIT/3))
				mobj->threshold = KITEM_SUPERRING;
			else if (P_RandomChance(PR_ITEM_CAPSULE, FRACUNIT/3))
				mobj->threshold = KITEM_SPB;
			else if (P_RandomChance(PR_ITEM_CAPSULE, FRACUNIT/3))
				mobj->threshold = KITEM_ORBINAUT;
			else
				mobj->threshold = P_RandomRange(PR_ITEM_CAPSULE, 1, numkartitems - 1);
			mobj->movecount = P_RandomChance(PR_ITEM_CAPSULE, FRACUNIT/3) ? 1 : P_RandomKey(PR_ITEM_CAPSULE, 32) + 1;
#else
			mobj->threshold = KITEM_SUPERRING; // default item is super ring
			mobj->movecount = 1;
#endif

			// set starting scale
			mobj->extravalue1 = mobj->scale; // this acts as the capsule's destscale; we're avoiding P_MobjScaleThink because we want aerial capsules not to scale from their center
			mobj->scalespeed >>= 1;
			P_SetScale(mobj, mobj->destscale = mapobjectscale >> 4);

			break;
		}
		case MT_KARMAHITBOX:
			{
				const fixed_t rad = FixedMul(mobjinfo[MT_PLAYER].radius, mobj->scale);
				mobj_t *cur, *prev = mobj;
				int32_t i;

				for (i = 0; i < 4; i++)
				{
					fixed_t offx = rad;
					fixed_t offy = rad;

					if (i == 1 || i == 3)
						offx *= -1;
					if (i == 2 || i == 3)
						offy *= -1;

					cur = P_SpawnMobj(mobj->x + offx, mobj->y + offy, mobj->z, MT_KARMAWHEEL);
					cur->destscale = mobj->scale;
					P_SetScale(cur, mobj->scale);

					cur->lastlook = i;

					P_SetTarget(&cur->hprev, prev);
					P_SetTarget(&prev->hnext, cur);

					prev = cur;
				}
			}
			break;
		case MT_BIGRING:
			P_SetScale(mobj, (mobj->destscale = 3*FRACUNIT));
			break;
		case MT_RANDOMAUDIENCE:
		{
			fixed_t randu = P_RandomFixed(PR_AUDIENCE);
			P_SetScale(mobj, (mobj->destscale <<= 1));
			if (randu < (FRACUNIT/9)) // a fan of someone?
			{
				uint8_t i, pcount = 0;
				uint8_t pnum[MAXPLAYERS];

				for (i = 0; i < MAXPLAYERS; i++)
				{
					if (!playeringame[i])
						continue;
					pnum[pcount] = i;
					pcount++;
				}

				if (pcount)
				{
					mobj->threshold = pnum[P_RandomKey(PR_AUDIENCE, pcount)];
					mobj->color = players[mobj->threshold].skincolor;
					mobj->colorized = true;
					break;
				}
			}

			if (randu > (FRACUNIT/2))
			{
				mobj->color = P_RandomKey(PR_AUDIENCE, numskincolors-1)+1;
				break;
			}

			mobj->color = SKINCOLOR_CYAN;
			break;
		}
		case MT_MARBLETORCH:
			P_SpawnMobj(mobj->x, mobj->y, mobj->z + (29*mobj->scale), MT_MARBLELIGHT);
			break;
		case MT_RUSTYLAMP_ORANGE:
			P_SpawnMobj(mobj->x, mobj->y, mobj->z + (69*mobj->scale), MT_MARBLELIGHT);
			break;
		case MT_PINETREE:
		{
			angle_t diff = FixedAngle((360/mobj->info->mass)*FRACUNIT);
			uint8_t i;

			for (i = 0; i < mobj->info->mass; i++)
			{
				angle_t ang = i * diff;
				mobj_t *side = P_SpawnMobj(mobj->x + FINECOSINE((ang>>ANGLETOFINESHIFT) & FINEMASK),
					mobj->y + FINESINE((ang>>ANGLETOFINESHIFT) & FINEMASK), mobj->z, MT_PINETREE_SIDE);
				side->angle = ang;
				side->target = mobj;
				side->threshold = i;
			}
			break;
		}
		case MT_EZZPROPELLER:
		{
			mobj_t *cur, *prev = mobj;
			uint8_t i;

			for (i = 0; i < mobj->info->mass; i++)
			{
				mobj->angle = FixedAngle((i * (360/mobj->info->mass))<<FRACBITS);

				cur = P_SpawnMobj(mobj->x + FINECOSINE(((mobj->angle*8)>>ANGLETOFINESHIFT) & FINEMASK),
					mobj->y + FINESINE(((mobj->angle*8)>>ANGLETOFINESHIFT) & FINEMASK), mobj->z, MT_EZZPROPELLER_BLADE);
				cur->angle = mobj->angle;

				P_SetTarget(&cur->hprev, prev);
				P_SetTarget(&prev->hnext, cur);

				prev = cur;
			}
			break;
		}
		case MT_ROBRA:
		case MT_BLUEROBRA:
			P_SetScale(mobj, (mobj->destscale = 1));
			break;
		case MT_ROBRA_HEAD:
			{
				mobj_t *shell;

				shell = P_SpawnMobj(mobj->x, mobj->y, mobj->z, MT_OVERLAY);
				P_SetTarget(&shell->target, mobj);
				P_SetMobjState(shell, S_ROBRASHELL_INSIDE);

				shell = P_SpawnMobj(mobj->x, mobj->y, mobj->z, MT_OVERLAY);
				P_SetTarget(&shell->target, mobj);
				P_SetMobjState(shell, S_ROBRASHELL_OUTSIDE);
			}
			break;
		case MT_EERIEFOGGEN:
			{
				uint16_t i;
				for (i = 0; i < mobj->info->mass; i++)
				{
					fixed_t newx = mobj->x + (P_RandomRange(PR_DECORATION, -mobj->info->mass, mobj->info->mass)<<FRACBITS);
					fixed_t newy = mobj->y + (P_RandomRange(PR_DECORATION, -mobj->info->mass, mobj->info->mass)<<FRACBITS);

					if (P_FloorzAtPos(newx, newy, mobj->z, 8<<FRACBITS) == mobj->z)
						P_SpawnMobj(newx, newy, mobj->z, MT_EERIEFOG);
				}
			}
			break;
		case MT_SMK_MOLE:
			mobj->reactiontime = P_RandomRange(PR_SRB2KART, 0, 3*mobj->info->reactiontime/2); // Random delay on start of level
			break;
		case MT_SMK_THWOMP:
			mobj->reactiontime = P_RandomRange(PR_THWOMP, 0, 3*mobj->info->reactiontime); // Random delay on start of level
			if (mobj->z == mobj->floorz)
				mobj->z += (256<<FRACBITS);
			mobj->movefactor = mobj->z + (256<<FRACBITS);
			break;
		case MT_SMK_ICEBLOCK:
			{
				mobj_t *cur, *prev = mobj;
				uint8_t i;

				for (i = 0; i < 4; i++)
				{
					cur = P_SpawnMobj(mobj->x, mobj->y, mobj->z, MT_SMK_ICEBLOCK_SIDE);
					P_SetTarget(&cur->target, mobj);
					cur->threshold = i;
					P_MoveOrigin(cur, cur->x + ((cur->radius>>FRACBITS) * FINECOSINE((FixedAngle((90*cur->threshold)<<FRACBITS)>>ANGLETOFINESHIFT) & FINEMASK)),
						cur->y + ((cur->radius>>FRACBITS) * FINESINE((FixedAngle((90*cur->threshold)<<FRACBITS)>>ANGLETOFINESHIFT) & FINEMASK)), cur->z);
					cur->angle = ANGLE_90*(cur->threshold+1);

					P_SetTarget(&cur->hprev, prev);
					P_SetTarget(&prev->hnext, cur);

					prev = cur;
				}
			}
			break;
		case MT_TUMBLEGEM:
			mobj->color = P_RandomKey(PR_DECORATION, numskincolors - 1) + 1;
			break;
		case MT_DASHRING:
			Obj_RegularDashRingSpawn(mobj);
			break;
		case MT_RAINBOWDASHRING:
			Obj_RainbowDashRingSpawn(mobj);
			break;
		case MT_SNEAKERPANEL:
			Obj_SneakerPanelSpawn(mobj);
			break;
		case MT_SNEAKERPANELSPAWNER:
			Obj_SneakerPanelSpawnerSpawn(mobj);
			break;
		default:
			break;
	}

	if (sc != -1 && !(mobj->flags2 & MF2_SLIDEPUSH))
	{
		uint8_t i;
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i] || players[i].spectator)
				continue;

			if (players[i].skin == sc)
			{
				mobj->color = SKINCOLOR_SILVER;
				mobj->colorized = true;
				mobj->flags2 |= MF2_SLIDEPUSH;
				break;
			}
		}
	}

	if (mobj->skin) // correct inadequecies above.
	{
		mobj->sprite2 = P_GetSkinSprite2(mobj->skin, (mobj->frame & FF_FRAMEMASK), NULL);
		mobj->frame &= ~FF_FRAMEMASK;
	}

	// Call action functions when the state is set
	if (st->action && (mobj->flags & MF_RUNSPAWNFUNC))
	{
		if (levelloading == true)
		{
			// Cache actions in a linked list
			// with function pointer, and
			// var1 & var2, which will be executed
			// when the level finishes loading.
			P_AddCachedAction(mobj, mobj->info->spawnstate);
		}
		else
		{
			var1 = st->var1;
			var2 = st->var2;
			astate = st;
			st->action(mobj);
			// DANGER! This can cause P_SpawnMobj to return NULL!
			// Avoid using MF_RUNSPAWNFUNC on mobjs whose spawn state expects target or tracer to already be set!
			if (P_MobjWasRemoved(mobj))
				return NULL;
		}
	}

	if (CheckForReverseGravity && !(mobj->flags & MF_NOBLOCKMAP))
		P_CheckGravity(mobj, false);

	R_AddMobjInterpolator(mobj);

	if (P_IsKartItem(mobj->type))	// mobj is a kart item we want on the list:
		P_AddKartItem(mobj);		// add to kitem list

	if (P_IsMiscCapObject(mobj->type)) // mobj is object we want on the list:
		P_AddMiscCap(mobj);			   // add to misccap list

	return mobj;
}

static precipmobj_t *P_SpawnPrecipMobj(fixed_t x, fixed_t y, fixed_t z, mobjtype_t type)
{
	const mobjinfo_t *info = &mobjinfo[type];
	state_t *st;
	fixed_t start_z = INT32_MIN;
	precipmobj_t *mobj = Z_LevelPoolCalloc(sizeof(precipmobj_t));
	mobj->thinker.alloctype = TAT_LEVELPOOL;
	mobj->thinker.size = sizeof(precipmobj_t);

	mobj->type = type;
	mobj->info = info;

	mobj->x = x;
	mobj->y = y;
	mobj->flags = info->flags;

	// do not set the state with P_SetMobjState,
	// because action routines can not be called yet
	st = &states[info->spawnstate];

	mobj->state = st;
	mobj->tics = st->tics;
	mobj->sprite = st->sprite;
	mobj->frame = st->frame; // FF_FRAMEMASK for frame, and other bits..
	P_SetupStateAnimation((mobj_t*)mobj, st);

	// set subsector and/or block links
	P_SetPrecipitationThingPosition(mobj);

	mobj->floorz   = P_GetSectorFloorZAt  (mobj->subsector->sector, x, y);
	mobj->ceilingz = P_GetSectorCeilingZAt(mobj->subsector->sector, x, y);

	mobj->floorrover = NULL;
	mobj->ceilingrover = NULL;

	mobj->z = z;
	mobj->momz = FixedMul(-info->speed, mapobjectscale);

	if (info->speed < 0)
	{
		mobj->precipflags |= PCF_FLIP;
	}

	start_z = mobj->floorz;

	mobj->thinker.function = (actionf_p1)P_NullPrecipThinker;
	P_AddThinker(THINK_PRECIP, &mobj->thinker);

	P_CalculatePrecipFloor(mobj);

	if (mobj->floorz != start_z)
	{
		; //mobj->precipflags |= PCF_FOF;
	}
	else
	{
		int32_t dmg = mobj->subsector->sector->damagetype;
		dboolean sFlag = (mobj->precipflags & PCF_FLIP) ? (mobj->subsector->sector->flags & MSF_FLIPSPECIAL_CEILING) : (mobj->subsector->sector->flags & MSF_FLIPSPECIAL_FLOOR);
		dboolean pitFloor = ((dmg == SD_DEATHPIT) && sFlag);
		dboolean skyFloor = (mobj->precipflags & PCF_FLIP) ? (mobj->subsector->sector->ceilingpic == skyflatnum) : (mobj->subsector->sector->floorpic == skyflatnum);

		if (pitFloor || skyFloor)
		{
			mobj->precipflags |= PCF_PIT;
		}
	}

	R_ResetPrecipitationMobjInterpolationState(mobj);

	return mobj;
}

void *P_CreateFloorSpriteSlope(mobj_t *mobj)
{
	if (mobj->floorspriteslope)
		Z_Free(mobj->floorspriteslope);
	mobj->floorspriteslope = Z_Calloc(sizeof(pslope_t), PU_LEVEL, NULL);
	mobj->floorspriteslope->normal.z = FRACUNIT;
	return (void *)mobj->floorspriteslope;
}

void P_RemoveFloorSpriteSlope(mobj_t *mobj)
{
	if (mobj->floorspriteslope)
		Z_Free(mobj->floorspriteslope);
	mobj->floorspriteslope = NULL;
}

//
// P_RemoveMobj
//
mapthing_t *itemrespawnque[ITEMQUESIZE] = {};
tic_t itemrespawntime[ITEMQUESIZE] = {};
size_t iquehead = 0, iquetail = 0;

#ifdef PARANOIA
//#define SCRAMBLE_REMOVED // Force debug build to crash when Removed mobj is accessed
#endif
void P_RemoveMobj(mobj_t *mobj)
{
	I_Assert(mobj != NULL);
	if (P_MobjWasRemoved(mobj))
		return; // something already removing this mobj.

	mobj->thinker.function = (actionf_p1)P_RemoveThinkerDelayed; // shh. no recursing.
	LUA_HookMobj(mobj, MOBJ_HOOK(MobjRemoved));
	mobj->thinker.function = (actionf_p1)P_MobjThinker; // needed for P_UnsetThingPosition, etc. to work.

	// Rings only, please!
	if (mobj->spawnpoint &&
		(mobj->type == MT_RING
		|| mobj->type == MT_BLUEBALL)
		&& !(mobj->flags2 & MF2_DONTRESPAWN))
	{
		//CONS_Printf("added to queue at tic %d\n", leveltime);
		itemrespawnque[iquehead] = mobj->spawnpoint;
		itemrespawntime[iquehead] = leveltime;
		iquehead = (iquehead+1)&(ITEMQUESIZE-1);
		// lose one off the end?
		if (iquehead == iquetail)
			iquetail = (iquetail+1)&(ITEMQUESIZE-1);
	}

	if (mobj->type == MT_KARMAHITBOX) // Remove linked list objects for certain types
	{
		mobj_t *cur = mobj->hnext;

		while (cur && !P_MobjWasRemoved(cur))
		{
			mobj_t *prev = cur; // Kind of a dumb var, but we need to set cur before we remove the mobj
			cur = cur->hnext;
			P_RemoveMobj(prev);
		}
	}

	if (mobj->type == MT_OVERLAY)
		P_RemoveOverlay(mobj);

	if (mobj->type == MT_SPB)
		spbplace = -1;

	if (mobj->type == MT_BUBBLESHIELD && mobj->tracer && !mobj->tracer->player)
	{
		// Remove the tracer (overlay)
		if (!P_MobjWasRemoved(mobj->tracer))
			P_RemoveMobj(mobj->tracer);

		P_SetTarget(&mobj->tracer, NULL);
	}

	if (P_IsKartItem(mobj->type))
		P_RemoveKartItem(mobj);

	if (P_IsMiscCapObject(mobj->type))
		P_RemoveMiscCap(mobj);

	if (mobj->player && mobj->player->followmobj)
	{
		P_RemoveMobj(mobj->player->followmobj);
		P_SetTarget(&mobj->player->followmobj, NULL);
	}

	mobj->health = 0; // Just because

	// unlink from sector and block lists
	P_UnsetThingPosition(mobj);
	if (sector_list)
	{
		P_DelSeclist(sector_list);
		sector_list = NULL;
	}

	mobj->flags |= MF_NOSECTOR|MF_NOBLOCKMAP;
	mobj->subsector = NULL;
	mobj->state = NULL;
	mobj->player = NULL;

	P_RemoveFloorSpriteSlope(mobj);

	// stop any playing sound
	S_StopSound(mobj);

	// killough 11/98:
	//
	// Remove any references to other mobjs.
	P_SetTarget(&mobj->target, NULL);
	P_SetTarget(&mobj->tracer, NULL);

	// repair hnext chain
	{
		mobj_t *cachenext = mobj->hnext;

		if (mobj->hnext && !P_MobjWasRemoved(mobj->hnext))
		{
			P_SetTarget(&mobj->hnext->hprev, mobj->hprev);
			P_SetTarget(&mobj->hnext, NULL);
		}

		if (mobj->hprev && !P_MobjWasRemoved(mobj->hprev))
		{
			P_SetTarget(&mobj->hprev->hnext, cachenext);
			P_SetTarget(&mobj->hprev, NULL);
		}
	}

	P_SetTarget(&mobj->itnext, NULL);

	P_RemoveThingTID(mobj);
	P_DeleteMobjStringArgs(mobj);
	R_RemoveMobjInterpolator(mobj);

	// free block
	if (!mobj->thinker.next)
	{ // Uh-oh, the mobj doesn't think, P_RemoveThinker would never go through!
		int32_t prevreferences;

		prevreferences = mobj->thinker.references;
		P_AddThinker(THINK_MOBJ, (thinker_t *)mobj);
		mobj->thinker.references = prevreferences;
	}

	P_RemoveThinker((thinker_t *)mobj);

#ifdef PARANOIA
	// Saved to avoid being scrambled like below...
	mobj->thinker.debug_mobjtype = mobj->type;
#endif

	// DBG: set everything in mobj_t to 0xFF instead of leaving it. debug memory error.
#ifdef SCRAMBLE_REMOVED
	// Invalidate mobj_t data to cause crashes if accessed!
	memset((uint8_t *)mobj + sizeof(thinker_t), 0xff, sizeof(mobj_t) - sizeof(thinker_t));
#endif

}

void P_FreePrecipMobj(precipmobj_t *mobj)
{
	// unlink from sector and block lists
	P_UnsetPrecipThingPosition(mobj);

	if (precipsector_list)
	{
		P_DelPrecipSeclist(precipsector_list);
		precipsector_list = NULL;
	}

	// free block
	// Precipmobjs don't actually think using their thinker,
	// so the free cannot be delayed.
	P_UnlinkThinker((thinker_t*)mobj);

}

// Clearing out stuff for savegames
void P_RemoveSavegameMobj(mobj_t *mobj)
{
	if (((thinker_t *)mobj)->function == (actionf_p1)P_NullPrecipThinker)
	{
		// unlink from sector and block lists
		P_UnsetPrecipThingPosition((precipmobj_t *)mobj);

		if (precipsector_list)
		{
			P_DelPrecipSeclist(precipsector_list);
			precipsector_list = NULL;
		}
	}
	else
	{
		// unlink from tid chains
		P_RemoveThingTID(mobj);

		// unlink from sector and block lists
		P_UnsetThingPosition(mobj);

		// Remove touching_sectorlist from mobj.
		if (sector_list)
		{
			P_DelSeclist(sector_list);
			sector_list = NULL;
		}

		P_DeleteMobjStringArgs(mobj);
	}

	// stop any playing sound
	S_StopSound(mobj);

	R_RemoveMobjInterpolator(mobj);

	// free block
	// Here we use the same code as R_RemoveThinkerDelayed, but without reference counting (we're removing everything so it shouldn't matter) and without touching currentthinker since we aren't in P_RunThinkers
	P_UnlinkThinker((thinker_t*)mobj);
}

static CV_PossibleValue_t respawnitemtime_cons_t[] = {{1, "MIN"}, {300, "MAX"}, {0, NULL}};
consvar_t cv_itemrespawntime = CVAR_INIT ("respawnitemtime", "2", CV_NETVAR|CV_CHEAT, respawnitemtime_cons_t, NULL);
consvar_t cv_itemrespawn = CVAR_INIT ("respawnitem", "On", CV_NETVAR, CV_OnOff, NULL);

static void P_SpawnPrecipitationAt(fixed_t basex, fixed_t basey)
{
	int32_t j, k;

	const mobjtype_t type = precipprops[curWeather].type;
	const uint8_t randomstates = (uint8_t)mobjinfo[type].damage;
	const dboolean flip = (mobjinfo[type].speed < 0);

	fixed_t i, x, y, z, height;

	uint16_t numparticles = 0;
	dboolean condition = false;

	subsector_t *precipsector = NULL;
	precipmobj_t *rainmo = NULL;

	// If mobjscale < FRACUNIT, each blockmap cell covers
	// more area so spawn more precipitation in that area.
	for (i = 0; i < FRACUNIT; i += mapobjectscale)
	{
		x = basex + ((M_RandomKey(MAPBLOCKUNITS << 3) << FRACBITS) >> 3);
		y = basey + ((M_RandomKey(MAPBLOCKUNITS << 3) << FRACBITS) >> 3);

		precipsector = R_PointInSubsectorOrNull(x, y);

		// No sector? Stop wasting time,
		// move on to the next entry in the blockmap
		if (!precipsector)
			continue;

		// Not in a sector with visible sky?
		if (precipprops[curWeather].effects & PRECIPFX_WATERPARTICLES)
		{
			condition = false;

			if (precipsector->sector->ffloors)
			{
				ffloor_t *rover;

				for (rover = precipsector->sector->ffloors; rover; rover = rover->next)
				{
					if (!(rover->fofflags & FOF_EXISTS))
						continue;

					if (!(rover->fofflags & FOF_SWIMMABLE))
						continue;

					condition = true;
					break;
				}
			}
		}
		else
		{
			condition = (precipsector->sector->ceilingpic == skyflatnum);
		}

		if (precipsector->sector->flags & MSF_INVERTPRECIP)
		{
			condition = !condition;
		}

		if (!condition)
		{
			continue;
		}

		height = precipsector->sector->ceilingheight - precipsector->sector->floorheight;
		height = FixedDiv(height, mapobjectscale);

		// Exists, but is too small for reasonable precipitation.
		if (height < 64<<FRACBITS)
			continue;

		// Hack around a quirk of this entire system, where taller sectors look like they get less precipitation.
		numparticles = 1 + (height / (MAPBLOCKUNITS<<4<<FRACBITS));

		// Don't set z properly yet...
		z = (flip) ? (precipsector->sector->floorheight) : (precipsector->sector->ceilingheight);

		for (j = 0; j < numparticles; j++)
		{
			int32_t floorz;
			int32_t ceilingz;

			rainmo = P_SpawnPrecipMobj(x, y, z, type);

			if (randomstates > 0)
			{
				uint8_t mrand = M_RandomByte();
				uint8_t threshold = UINT8_MAX / (randomstates + 1);
				statenum_t st = mobjinfo[type].spawnstate;

				for (k = 0; k < randomstates; k++)
				{
					if (mrand < (threshold * (k+1)))
					{
						P_SetPrecipMobjState(rainmo, st+k+1);
						break;
					}
				}
			}

			floorz = rainmo->floorz >> FRACBITS;
			ceilingz = rainmo->ceilingz >> FRACBITS;

			if (floorz < ceilingz)
			{
				// Randomly assign a height, now that floorz is set.
				rainmo->z = M_RandomRange(floorz, ceilingz) << FRACBITS;
			}
			else
			{
				// ...except if the floor is above the ceiling.
				rainmo->z = ceilingz << FRACBITS;
			}
		}
	}
}

void P_SpawnPrecipitation(void)
{
	int32_t i;

	const mobjtype_t type = precipprops[curWeather].type;

	fixed_t basex, basey;

	if (dedicated || !cv_drawdist_precip.value || type == MT_NULL)
		return;

	// Use the blockmap to narrow down our placing patterns
	for (i = 0; i < bmapwidth*bmapheight; ++i)
	{
		basex = bmaporgx + (i % bmapwidth) * MAPBLOCKSIZE;
		basey = bmaporgy + (i / bmapwidth) * MAPBLOCKSIZE;

		P_SpawnPrecipitationAt(basex, basey);
	}
}

//
// P_PrecipitationEffects
//
void P_PrecipitationEffects(void)
{
	int16_t thunderchance = INT16_MAX;
	int32_t volume;
	size_t i;

	int32_t rainsfx = mobjinfo[precipprops[curWeather].type].seesound;
	int32_t rainfreq = mobjinfo[precipprops[curWeather].type].mass;

	dboolean sounds_thunder = (precipprops[curWeather].effects & PRECIPFX_THUNDER);
	dboolean effects_lightning = (precipprops[curWeather].effects & PRECIPFX_LIGHTNING);
	dboolean lightningStrike = false;

	dboolean sounds_rain = (rainsfx != sfx_None && (!leveltime || leveltime % rainfreq == 1));

	// No thunder except every other tic.
	if (!(leveltime & 1))
	{
		if ((precipprops[globalweather].effects & PRECIPFX_THUNDER)
		|| (precipprops[globalweather].effects & PRECIPFX_LIGHTNING))
		{
			// Before, consistency failures were possible if a level started
			// with global rain and switched players to anything else ...
			// If the global weather has lightning strikes,
			// EVERYONE gets them at the SAME time!
			thunderchance = (P_RandomKey(PR_DECORATION, 8192));
		}
		else if (sounds_thunder || effects_lightning)
		{
			// But on the other hand, if the global weather is ANYTHING ELSE,
			// don't sync lightning strikes.
			// While we'll only use the variable if we care about it, it's
			// nice to save on RNG calls when we don't need it.
			thunderchance = (M_RandomKey(8192));
		}
	}

	if (thunderchance < 70)
		lightningStrike = true;

	// Currently thunderstorming with lightning, and we're sounding the thunder...
	// and where there's thunder, there's gotta be lightning!
	if (effects_lightning && lightningStrike)
	{
		sector_t *ss = sectors;

		for (i = 0; i < numsectors; i++, ss++)
			if (ss->ceilingpic == skyflatnum) // Only for the sky.
				P_SpawnLightningFlash(ss); // Spawn a quick flash thinker
	}

	// Local effects from here on out!
	// If we're not in game fully yet, we don't worry about them.
	if (!playeringame[g_localplayers[0]] || !players[g_localplayers[0]].mo)
		return;

	if (sound_disabled)
		return; // Sound off? D'aw, no fun.

	if (!sounds_rain && !sounds_thunder)
		return; // no need to calculate volume at ALL

	if (players[g_localplayers[0]].mo->subsector->sector->ceilingpic == skyflatnum)
		volume = 255; // Sky above? We get it full blast.
	else
	{
		int64_t x, y, yl, yh, xl, xh;
		fixed_t closedist, newdist;

		// Essentially check in a 1024 unit radius of the player for an outdoor area.
#define RADIUSSTEP (64*FRACUNIT)
#define SEARCHRADIUS (16*RADIUSSTEP)
		yl = yh = players[g_localplayers[0]].mo->y;
		yl -= SEARCHRADIUS;
		while (yl < INT32_MIN)
			yl += RADIUSSTEP;
		yh += SEARCHRADIUS;
		while (yh > INT32_MAX)
			yh -= RADIUSSTEP;

		xl = xh = players[g_localplayers[0]].mo->x;
		xl -= SEARCHRADIUS;
		while (xl < INT32_MIN)
			xl += RADIUSSTEP;
		xh += SEARCHRADIUS;
		while (xh > INT32_MAX)
			xh -= RADIUSSTEP;

		closedist = SEARCHRADIUS*2;
#undef SEARCHRADIUS
		for (y = yl; y >= yl && y <= yh; y += RADIUSSTEP)
			for (x = xl; x >= xl && x <= xh; x += RADIUSSTEP)
			{
				if (R_PointInSubsectorFast((fixed_t)x, (fixed_t)y)->sector->ceilingpic != skyflatnum) // Found the outdoors!
					continue;

				newdist = S_CalculateSoundDistance(players[g_localplayers[0]].mo->x, players[g_localplayers[0]].mo->y, 0, (fixed_t)x, (fixed_t)y, 0);
				if (newdist < closedist)
					closedist = newdist;
			}
#undef RADIUSSTEP

		volume = 255 - (closedist>>(FRACBITS+2));
	}

	if (volume < 0)
		volume = 0;
	else if (volume > 255)
		volume = 255;

	if (sounds_rain)
		S_StartSoundAtVolume(players[g_localplayers[0]].mo, rainsfx, volume);

	if (!sounds_thunder)
		return;

	if (effects_lightning && lightningStrike && volume)
	{
		// Large, close thunder sounds to go with our lightning.
		S_StartSoundAtVolume(players[g_localplayers[0]].mo, sfx_litng1 + M_RandomKey(4), volume);
	}
	else if (thunderchance < 20)
	{
		// You can always faintly hear the thunder...
		if (volume < 80)
			volume = 80;

		S_StartSoundAtVolume(players[g_localplayers[0]].mo, sfx_athun1 + M_RandomKey(2), volume);
	}
}

/** Returns corresponding mobj type from mapthing number.
 * \param mthingtype Mapthing number in question.
 * \return Mobj type; MT_UNKNOWN if nothing found.
 */
mobjtype_t P_GetMobjtype(uint16_t mthingtype)
{
	mobjtype_t i;
	for (i = 0; i < NUMMOBJTYPES; i++)
		if (mthingtype == mobjinfo[i].doomednum)
			return i;
	return MT_UNKNOWN;
}

//
// P_RespawnSpecials
//
void P_RespawnSpecials(void)
{
	uint8_t p, pcount = 0;
	int32_t time = 30*TICRATE; // Respawn things in empty dedicated servers
	mapthing_t *mthing = NULL;

	// wait time depends on player count
	for (p = 0; p < MAXPLAYERS; p++)
	{
		if (playeringame[p] && !players[p].spectator)
			pcount++;
	}

#if 0 // set to 1 to enable quick respawns for testing
	if (true)
		time = 5*TICRATE;
	else
#endif
	{
		if (pcount == 1) // No respawn when alone
		{
			return;
		}
		else if (pcount > 1)
		{
			time = (120 * TICRATE) / (pcount - 1);

			// If the map is longer or shorter than 3 laps, then adjust ring respawn to account for this.
			// 5 lap courses would have more retreaded ground, while 2 lap courses would have less.
			if ((mapheaderinfo[gamemap-1]->numlaps != 3)
				&& !(mapheaderinfo[gamemap-1]->levelflags & LF_SECTIONRACE))
			{
				time = (time * 3) / max(1, mapheaderinfo[gamemap-1]->numlaps);
			}

			if (time < 10*TICRATE)
			{
				// Ensure it doesn't go into absurdly low values
				time = 10*TICRATE;
			}
		}
	}

	// only respawn items when cv_itemrespawn is on
	if (!cv_itemrespawn.value)
		return;

	// nothing left to respawn?
	if (iquehead == iquetail)
		return;

	// the first item in the queue is the first to respawn
	if (leveltime - itemrespawntime[iquetail] < (tic_t)time)
		return;

	mthing = itemrespawnque[iquetail];

#ifdef PARANOIA
	if (!mthing)
		I_Error("itemrespawnque[iquetail] is NULL!");
#endif

	if (mthing)
		P_SpawnMapThing(mthing);

	//CONS_Printf("respawn happened on tic %d, irt %d, t %d\n", leveltime, itemrespawntime[iquetail], time);

	// pull it from the que
	iquetail = (iquetail+1)&(ITEMQUESIZE-1);
}

//
// P_SpawnPlayer
// Called when a player is spawned on the level.
// Most of the player structure stays unchanged between levels.
//
void P_SpawnPlayer(int32_t playernum)
{
	player_t *p = &players[playernum];
	mobj_t *mobj;

	dboolean justjoined = (p->jointime <= 1);

	if (p->playerstate == PST_REBORN)
		G_PlayerReborn(playernum, justjoined);

	if (G_GametypeHasTeams())
	{
		// If you're in a team game and you don't have a team assigned yet...
		if (!p->spectator && p->ctfteam == 0)
		{
			changeteam_union NetPacket;
			uint16_t usvalue;
			NetPacket.value.l = NetPacket.value.b = 0;

			// Spawn as a spectator,
			// yes even in splitscreen mode
			p->spectator = true;
			p->spectatorreentry = 0; //(cv_spectatorreentry.value * TICRATE);

			if (playernum&1) p->skincolor = skincolor_redteam;
			else             p->skincolor = skincolor_blueteam;

			// but immediately send a team change packet.
			NetPacket.packet.playernum = playernum;
			NetPacket.packet.verification = true;
			NetPacket.packet.newteam = !(playernum&1) + 1;

			usvalue = LSBF_SHORT(NetPacket.value.l|NetPacket.value.b);
			SendNetXCmd(XD_TEAMCHANGE, &usvalue, sizeof(usvalue));
		}

		// Fix team colors.
		// This code isn't being done right somewhere else. Oh well.
		if (p->ctfteam == 1)
			p->skincolor = skincolor_redteam;
		else if (p->ctfteam == 2)
			p->skincolor = skincolor_blueteam;
	}

	if (leveltime > introtime)
		p->flashing = K_GetKartFlashing(p); // Babysitting deterrent

	mobj = P_SpawnMobj(0, 0, 0, MT_PLAYER);

	mobj->player = p;
	P_SetTarget(&p->mo, mobj);

	mobj->angle = mobj->old_angle = 0;

	// set color translations for player sprites
	mobj->color = p->skincolor;

	// set 'spritedef' override in mobj for player skins.. (see ProjectSprite)
	// (usefulness: when body mobj is detached from player (who respawns),
	// the dead body mobj retains the skin through the 'spritedef' override).
	R_ApplySkin(mobj, skins[p->skin]);
	P_SetupStateAnimation(mobj, mobj->state);

	if (p->voice_id)
	{
		// During respawns, do a quick check on our voice ID to make sure
		// our skin can use it.
		// This should prevent bots from always having Sonic's voice.
		mobj->voice = &skins[p->skin]->voices[p->voice_id];
	}

	mobj->health = 1;
	p->playerstate = PST_LIVE;

	if (!p->exiting || !p->realtime)
	{
		p->realtime = leveltime;
	}

	p->followitem = skins[p->skin]->followitem;

	if (K_PlayerShrinkCheat(p) == true)
	{
		mobj->destscale = FixedMul(mobj->destscale, SHRINK_SCALE);
	}

	// set the scale to the mobj's destscale so settings get correctly set.  if we don't, they sometimes don't.
	P_SetScale(mobj, mobj->destscale);
	P_FlashPal(p, 0, 0); // Resets

	p->grieftime = 0;
	p->spinoutrot = 0;
	p->postspawnsectordamage = SD_UNDEFINED;

	if (gametypes[gametype]->rules & GTR_ITEMARROWS)
	{
		mobj_t *overheadarrow = P_SpawnMobj(mobj->x, mobj->y, mobj->z + mobj->height + 16*FRACUNIT, MT_PLAYERARROW);
		P_SetTarget(&overheadarrow->target, mobj);
		overheadarrow->renderflags |= RF_DONTDRAW;
		P_SetScale(overheadarrow, mobj->destscale);
	}

	if (gametypes[gametype]->rules & GTR_BUMPERS)
	{
		if (p->spectator)
		{
			// HEY! No being cheap...
			p->bumper = 0;
		}
		else if ((p->bumper > 0) || (leveltime < starttime))
		{
			if ((leveltime < starttime)) // Start of the map?
			{
				p->bumper = K_StartingBumperCount();
			}

			K_SpawnPlayerBattleBumpers(p);
		}
		else if (p->bumper <= 0 && (gametypes[gametype]->rules & GTR_KARMA))
		{
			mobj_t *karmahitbox = P_SpawnMobj(mobj->x, mobj->y, mobj->z, MT_KARMAHITBOX); // Player hitbox is too small!!
			P_SetTarget(&karmahitbox->target, mobj);
			karmahitbox->destscale = mobj->scale;
			P_SetScale(karmahitbox, mobj->scale);
		}
	}
}

void P_AfterPlayerSpawn(int32_t playernum, dboolean starpost)
{
	player_t *p = &players[playernum];
	mobj_t *mobj = p->mo;
	uint8_t i;

	// fix the verticalflip flag to prevent camera jank (the player mobj initially spawns at the origin)
	P_CheckGravity(players[playernum].mo, false);

	// Update interpolation
	mobj->old_x = mobj->x;
	mobj->old_y = mobj->y;
	mobj->old_z = mobj->z;
	mobj->old_angle = mobj->angle;

	P_SetPlayerAngle(p, mobj->angle);

	p->viewheight = P_GetPlayerViewHeight(p);

	if (p->mo->eflags & MFE_VERTICALFLIP)
		p->viewz = p->mo->z + p->mo->height - p->viewheight;
	else
		p->viewz = p->mo->z + p->viewheight;

	if (playernum == consoleplayer)
	{
		// wake up the status bar
		ST_Start();
		// wake up the heads up text
		HU_Start();
	}

	p->drawangle = mobj->angle;

	if (p->spectator == false)
	{
		for (i = 0; i <= r_splitscreen; i++)
		{
			if (camera[i].chase)
			{
				if (displayplayers[i] == playernum)
					P_ResetCamera(p, &camera[i]);
			}
		}
	}

	if (CheckForReverseGravity)
		P_CheckGravity(mobj, false);

	// set the player's starting waypoint
	if (!K_UsingLegacyCheckpoints() && (gametypes[gametype]->rules & GTR_CIRCUIT) && waypointcap != NULL)
	{
		waypoint_t *spawn_wp = K_GetSpawnWaypointForMobj(p->mo);

		p->currentwaypoint = spawn_wp;

		if (p->onbadwaypoint)
		{
			K_MarkAndNotifyBadWaypoint(spawn_wp);

			p->onbadwaypoint = false;

			if (p->currentwaypoint)
			{
				// We need a new waypoint RIGHT NOW, so search for the next waypoint once we've flagged this one
				K_SetRespawnAtNextWaypoint(p);

				// Play it again, Sam!
				G_MovePlayerToSpawnOrStarpost((int32_t)(p - players), starpost);
			}
		}
	}
}

// spawn it at a playerspawn mapthing
void P_MovePlayerToSpawn(int32_t playernum, mapthing_t *mthing)
{
	fixed_t x = 0, y = 0;
	angle_t angle = 0;

	fixed_t z;
	sector_t *sector;
	fixed_t floor, ceiling, ceilingspawn;

	player_t *p = &players[playernum];
	mobj_t *mobj = p->mo;
	I_Assert(mobj != NULL);

	if (mthing)
	{
		x = mthing->x << FRACBITS;
		y = mthing->y << FRACBITS;
		angle = FixedAngle(mthing->angle*FRACUNIT);
	}
	//spawn at the origin as a desperation move if there is no mapthing

	// set Z height
	sector = R_PointInSubsector(x, y)->sector;

	floor =
	sector->f_slope ? P_GetSlopeZAt(sector->f_slope, x, y) :
	sector->floorheight;
	ceiling =
	sector->c_slope ? P_GetSlopeZAt(sector->c_slope, x, y) :
	sector->ceilingheight;
	ceilingspawn = ceiling - mobjinfo[MT_PLAYER].height;

	if (mthing)
	{
		fixed_t offset = udmf ? mthing->z << FRACBITS : (mthing->options >> ZSHIFT) << FRACBITS;

		if (p->respawn)
			offset += 128*mapobjectscale;

		// Setting the spawnpoint's args[0] will make the player start on the ceiling
		// Objectflip inverts
		if (!!(mthing->args[0]) ^ !!(mthing->options & MTF_OBJECTFLIP))
			z = ceilingspawn - offset;
		else
			z = floor + offset;

		if (mthing->options & MTF_OBJECTFLIP) // flip the player!
		{
			mobj->eflags |= MFE_VERTICALFLIP;
			mobj->flags2 |= MF2_OBJECTFLIP;
		}

		if (mthing->args[0])
			P_SetPlayerMobjState(mobj, S_KART_SPINOUT);
	}
	else
		z = floor;

	if (z < floor)
		z = floor;
	else if (z > ceilingspawn)
		z = ceilingspawn;

	mobj->floorz = floor;
	mobj->ceilingz = ceiling;

	P_UnsetThingPosition(mobj);
	mobj->x = x;
	mobj->y = y;
	P_SetThingPosition(mobj);

	mobj->z = z;
	if (mobj->z == mobj->floorz)
		mobj->eflags |= MFE_ONGROUND;

	mobj->angle = angle;

	P_AfterPlayerSpawn(playernum, false);
}

void P_MovePlayerToStarpost(int32_t playernum)
{
	fixed_t z;
	sector_t *sector;
	fixed_t floor, ceiling;

	player_t *p = &players[playernum];
	mobj_t *mobj = p->mo;
	I_Assert(mobj != NULL);

	P_UnsetThingPosition(mobj);
	mobj->x = p->starpostx;
	mobj->y = p->starposty;
	P_SetThingPosition(mobj);
	sector = R_PointInSubsector(mobj->x, mobj->y)->sector;

	floor =
	sector->f_slope ? P_GetSlopeZAt(sector->f_slope, mobj->x, mobj->y) :
	sector->floorheight;
	ceiling =
	sector->c_slope ? P_GetSlopeZAt(sector->c_slope, mobj->x, mobj->y) :
	sector->ceilingheight;

	z = CLAMP(p->starpostz, floor, ceiling); // no more copyslope floorspawning jank

	if (mobj->player->starpostflip)
		z -= 128*mapobjectscale + mobj->height;
	else
		z += 128*mapobjectscale;

	z = CLAMP(z, floor, ceiling - mobjinfo[MT_PLAYER].height);
	//mobj->player->starpostflip = 0; // why???

	mobj->floorz = floor;
	mobj->ceilingz = ceiling;

	mobj->z = z;
	if (mobj->z == mobj->floorz)
		mobj->eflags |= MFE_ONGROUND;

	mobj->angle = p->starpostangle;

	//p->kartstuff[k_waypoint] = p->kartstuff[k_starpostwp]; // SRB2kart

	if (P_MobjTouchingInstakillSector(mobj))
	{
		// Respawn is bad, attempt to force the player ahead
		// while flagging the bad waypoint
		p->onbadwaypoint = true;
	}

	P_AfterPlayerSpawn(playernum, true);

	//if (!(netgame || multiplayer))
	//	leveltime = p->starposttime;
}

fixed_t P_GetMapThingSpawnHeight(const mobjtype_t mobjtype, const mapthing_t* mthing, const fixed_t x, const fixed_t y)
{
	fixed_t dz = mthing->z << FRACBITS; // Base offset from the floor.
	fixed_t offset = 0; // Specific scaling object offset.
	dboolean flip = (!!(mobjinfo[mobjtype].flags & MF_SPAWNCEILING) ^ !!(mthing->options & MTF_OBJECTFLIP));

	switch (mobjtype)
	{
	// Objects with a non-zero default height.
	// Horizontal springs, float additional units unless args[0] is set.
	case MT_YELLOWHORIZ:
	case MT_REDHORIZ:
	case MT_BLUEHORIZ:
	case MT_GRAYHORIZ:
	case MT_NEOHORIZ:
		offset += mthing->args[0] ? 0 : 16*FRACUNIT;
		break;

	// Ring-like items, float additional units unless args[0] is set.
	case MT_SPIKEBALL:
	case MT_EMBLEM:
	case MT_RING:
	case MT_BLUEBALL:
	case MT_EMMY:
		offset += mthing->args[0] ? 0 : 24*FRACUNIT;
		break;

	// This object does not have an offset
	default:
		break;
	}

	if (!(dz + offset) && mthing->layer == 0) // Snap to the surfaces when there's no offset set.
	{
		if (flip)
			return ONCEILINGZ;
		else
			return ONFLOORZ;
	}

	return P_GetMobjSpawnHeight(mobjtype, x, y, dz, offset, mthing->layer, flip, mthing->scale);
}

static void SpreadFinishLine(line_t *line, line_t *prev, int32_t args)
{
	size_t i;
	for (i = 0; i < numlines; i++)
	{
		line_t *line2 = &lines[i];
		if (line2 != line && line2 != prev && (line2->slopetype == ST_HORIZONTAL || line2->slopetype == ST_VERTICAL) && line2->slopetype == line->slopetype &&
		   (line->v1 == line2->v1 || line->v2 == line2->v2 || line->v1 == line2->v2 || line->v2 == line2->v1))
		{
			dboolean flipped = line->v1 == line2->v1 || line->v2 == line2->v2 ? TMCFF_FLIP : 0;
			line2->special = 2001; // Finish Line
			line2->activation = SPAC_CROSS|SPAC_REPEATSPECIAL;
			line2->args[0] = args ^ flipped;
			SpreadFinishLine(line2, line, args ^ flipped);
		}
	}
}

static dboolean P_SpawnNonMobjMapThing(mapthing_t *mthing)
{
#if MAXPLAYERS > 32
	You should think about modifying the deathmatch starts to take full advantage of this!
#endif
	if (mthing->type <= MAXPLAYERS) // Player starts
	{
		// save spots for respawning in network games
		if (!metalrecording)
			playerstarts[mthing->type - 1] = mthing;
		return true;
	}
	else if (mthing->type == 33) // Match starts
	{
		if (numdmstarts < MAX_DM_STARTS)
		{
			deathmatchstarts[numdmstarts] = mthing;
			mthing->type = 0;
			numdmstarts++;
		}
		return true;
	}
	else if (mthing->type == 34) // Red CTF starts
	{
		if (numredctfstarts < MAXPLAYERS)
		{
			redctfstarts[numredctfstarts] = mthing;
			mthing->type = 0;
			numredctfstarts++;
		}
		return true;
	}
	else if (mthing->type == 35) // Blue CTF starts
	{
		if (numbluectfstarts < MAXPLAYERS)
		{
			bluectfstarts[numbluectfstarts] = mthing;
			mthing->type = 0;
			numbluectfstarts++;
		}
		return true;
	}
	else if (metalrecording && mthing->type == mobjinfo[MT_METALSONIC_RACE].doomednum)
	{
		// If recording, you ARE Metal Sonic. Do not spawn it, do not save normal spawnpoints.
		playerstarts[0] = mthing;
		return true;
	}
	else if (mthing->type == 750 // Slope vertex point (formerly chaos spawn)
		     || (mthing->type == FLOOR_SLOPE_THING || mthing->type == CEILING_SLOPE_THING) // Slope anchors
		     || (mthing->type >= 600 && mthing->type <= 611) // Special placement patterns
		     || mthing->type == 1713) // Hoops
	{
		return true; // These are handled elsewhere.
	}
	else if (K_UsingPatchedMap() && mthing->type >= 6502 && mthing->type <= 6503) // mappatch things!
	{
		fixed_t x = mthing->x << FRACBITS, y = mthing->y << FRACBITS;
		sector_t *sec = R_PointInSubsector(x, y)->sector;
		line_t *line = P_FindNearestLine(x, y, sec, -1);

		if (line != NULL) switch (mthing->type)
		{
		case 6502: // Spawn Finish Line
			usedspawnfinishline = true;
			line->special = 2001;
			line->activation = SPAC_CROSS|SPAC_REPEATSPECIAL;
			line->args[0] = mthing->args[0];
			if (mthing->args[1] == 0) // don't spread?
				SpreadFinishLine(line, NULL, mthing->args[0]);
			break;

		case 6503:
			break;
		}

		return true;
	}

	return false;
}

static dboolean P_AllowMobjSpawn(mapthing_t* mthing, mobjtype_t i)
{
	(void)mthing;

	switch (i)
	{
		case MT_EMBLEM:
		{

			if (netgame || multiplayer)
				return false; // Single player only

			if (modifiedgame && !savemoddata)
				return false; // No cheating!!
			break;
		}
		case MT_RING:
		{
			if ((K_RingsActive() == false) )
				return false;
			break;
		}
		case MT_EMERALD1:
		case MT_EMERALD2:
		case MT_EMERALD3:
		case MT_EMERALD4:
		case MT_EMERALD5:
		case MT_EMERALD6:
		case MT_EMERALD7:
		{
			if (gametype == GT_RACE || gametype == GT_BATTLE) // they don't spawn in kart so I guess i'll do this?
				return false;

			if (metalrecording)
				return false; // Metal Sonic isn't for collecting emeralds.

			if (emeralds & mobjinfo[i].speed) // You already have this emerald!
				return false;
			break;
		}
		case MT_ITEMCAPSULE:
			{
				dboolean isRingCapsule = mthing->args[0] < 1 || mthing->args[0] == KITEM_SUPERRING || mthing->args[0] >= numkartitems;

				// don't spawn ring capsules in with rings off.
				if (isRingCapsule && (K_RingsActive() ==  false))
					return false;

				// in record attack, only spawn ring capsules
				// (behavior can be inverted with the Extra flag, i.e. item capsule spawns and ring capsule does not)
				if (modeattacking
				&& (!(mthing->args[2] & TMICF_INVERTTIMEATTACK) == !isRingCapsule))
					return false;
			}
			break;
		case MT_COIN:
		case MT_BLUEBALL:
			{
				if (gametype == GT_RACE || gametype == GT_BATTLE) // they don't spawn in kart so I guess i'll do this?
					return false;

				break;
			}
		case MT_NIGHTSWING:
			{
				if (gametype == GT_RACE || gametype == GT_BATTLE) // they don't spawn in kart so I guess i'll do this?
					return false;
				break;
			}

		default:
			break;
	}

	// No bosses outside of a combat situation.
	// (just in case we want boss arenas to do double duty as battle maps)
	// TODO: Look at this latter. Kart maps use stuff like brak eggman.
	//if (!bossinfo.boss && (mobjinfo[i].flags & MF_BOSS))
		//return false;

	if (metalrecording) // Metal Sonic can't use these things.
	{
		if (mobjinfo[i].flags & (MF_ENEMY|MF_BOSS))
			return false;
	}

	return true;
}

static mobjtype_t P_GetMobjtypeSubstitute(mapthing_t *mthing, mobjtype_t i)
{
	(void)mthing;

	if ((i == MT_RANDOMITEM) && (gametypes[gametype]->rules & (GTR_PAPERITEMS|GTR_CIRCUIT)) == (GTR_PAPERITEMS|GTR_CIRCUIT) && !bossinfo.boss)
		return MT_PAPERITEMSPOT;

	return i;
}

static dboolean P_SetupEmblem(mapthing_t *mthing, mobj_t *mobj)
{
	int32_t j;
	emblem_t* emblem = M_GetLevelEmblems(gamemap);
	skincolornum_t emcolor;
	int16_t tagnum = mthing->tid;

	while (emblem)
	{
		if (emblem->type == ET_GLOBAL && emblem->tag == tagnum)
			break;

		emblem = M_GetLevelEmblems(NEXTMAP_INVALID);
	}

	if (!emblem)
	{
		CONS_Debug(DBG_GAMELOGIC, "No map emblem for map %d with tag %d found!\n", gamemap, tagnum);
		return false;
	}

	j = emblem - emblemlocations;

	I_Assert(emblemlocations[j].sprite >= 'A' && emblemlocations[j].sprite <= 'Z');
	P_SetMobjState(mobj, mobj->info->spawnstate + (emblemlocations[j].sprite - 'A'));

	mobj->health = j + 1;
	emcolor = M_GetEmblemColor(&emblemlocations[j]); // workaround for compiler complaint about bad function casting
	mobj->color = (uint16_t)emcolor;

	if (emblemlocations[j].collected)
	{
		P_UnsetThingPosition(mobj);
		mobj->flags |= MF_NOCLIP;
		mobj->flags &= ~MF_SPECIAL;
		mobj->flags |= MF_NOBLOCKMAP;
		mobj->frame |= (tr_trans50 << FF_TRANSSHIFT);
		P_SetThingPosition(mobj);
	}
	else
	{
		mobj->frame &= ~FF_TRANSMASK;

		if (emblemlocations[j].type == ET_GLOBAL)
		{
			mobj->reactiontime = emblemlocations[j].var;
		}
	}
	return true;
}

static dboolean P_SetupMace(mapthing_t *mthing, mobj_t *mobj, dboolean *doangle)
{
	fixed_t mlength, mmaxlength, mlengthset, mspeed, mphase, myaw, mpitch, mminlength, mnumspokes, mpinch, mroll, mnumnospokes, mwidth, mwidthset, mmin, msound, radiusfactor, widthfactor;
	angle_t mspokeangle;
	mobjtype_t chainlink, macetype, firsttype, linktype;
	dboolean mdosound, mdocenter, mchainlike = false;
	mobj_t *spawnee = NULL, *hprev = mobj;
	mobjflag_t mflagsapply;
	mobjflag2_t mflags2apply;
	mobjeflag_t meflagsapply;
	const size_t mthingi = (size_t)(mthing - mapthings);

	mlength = abs(mthing->args[0]);
	mnumspokes = mthing->args[1] + 1;
	mspokeangle = FixedAngle((360*FRACUNIT)/mnumspokes) >> ANGLETOFINESHIFT;
	mwidth = max(0, mthing->args[2]);
	mspeed = abs(mthing->args[3] << (mapnamespace == MNS_SRB2KART ? 0 : 4));
	mphase = mthing->args[4] % 360;
	mpinch = mthing->args[5] % 360;
	mnumnospokes = mthing->args[6];
	mminlength = max(0, min(mlength - 1, mthing->args[7]));
	mpitch = mthing->pitch % 360;
	myaw = mthing->angle % 360;
	mroll = mthing->roll % 360;

	CONS_Debug(DBG_GAMELOGIC, "Mace/Chain (mapthing #%s):\n"
		"Length is %d (minus %d)\n"
		"Speed is %d\n"
		"Phase is %d\n"
		"Yaw is %d\n"
		"Pitch is %d\n"
		"No. of spokes is %d (%d antispokes)\n"
		"Pinch is %d\n"
		"Roll is %d\n"
		"Width is %d\n",
		sizeu1(mthingi), mlength, mminlength, mspeed, mphase, myaw, mpitch, mnumspokes, mnumnospokes, mpinch, mroll, mwidth);

	if (mnumnospokes > 0 && (mnumnospokes < mnumspokes))
		mnumnospokes = mnumspokes/mnumnospokes;
	else
		mnumnospokes = ((mobj->type == MT_CHAINMACEPOINT) ? (mnumspokes) : 0);

	mobj->lastlook = mspeed;
	mobj->movecount = mobj->lastlook;
	mobj->angle = FixedAngle(myaw << FRACBITS);
	*doangle = false;
	mobj->threshold = (FixedAngle(mpitch << FRACBITS) >> ANGLETOFINESHIFT);
	mobj->movefactor = mpinch;
	mobj->movedir = 0;

	// Mobjtype selection
	switch (mobj->type)
	{
	case MT_CUSTOMMACEPOINT:
		macetype = mthing->stringargs[0] ? get_number(mthing->stringargs[0]) : MT_NULL;
		chainlink = mthing->stringargs[1] ? get_number(mthing->stringargs[1]) : MT_NULL;
		break;
	case MT_CHAINPOINT:
		if (mthing->args[8] & TMM_DOUBLESIZE)
		{
			macetype = MT_BIGGRABCHAIN;
			chainlink = MT_BIGMACECHAIN;
		}
		else
		{
			macetype = MT_SMALLGRABCHAIN;
			chainlink = MT_SMALLMACECHAIN;
		}
		mchainlike = true;
		break;
	default:
		if (mthing->args[8] & TMM_DOUBLESIZE)
		{
			macetype = MT_BIGMACE;
			chainlink = MT_BIGMACECHAIN;
		}
		else
		{
			macetype = MT_SMALLMACE;
			chainlink = MT_SMALLMACECHAIN;
		}
		break;
	}

	if (!macetype && !chainlink)
		return true;

	if (mobj->type == MT_CHAINPOINT)
	{
		if (!mlength)
			return true;
	}
	else
		mlength++;

	firsttype = macetype;

	// Adjustable direction
	if (mthing->args[8] & TMM_ALLOWYAWCONTROL)
		mobj->flags |= MF_SLIDEME;

	// Swinging
	if (mthing->args[8] & TMM_SWING)
	{
		mobj->flags2 |= MF2_STRONGBOX;
		mmin = ((mnumnospokes > 1) ? 1 : 0);
	}
	else
		mmin = mnumspokes;

	// If over distance away, don't move UNLESS this flag is applied
	if (mthing->args[8] & TMM_ALWAYSTHINK)
		mobj->flags2 |= MF2_BOSSNOTRAP;

	// Make the links the same type as the end - repeated below
	if ((mobj->type != MT_CHAINPOINT) && ((mthing->args[8] & TMM_MACELINKS) == TMM_MACELINKS)) // exclusive or
	{
		linktype = macetype;
		radiusfactor = 2; // Double the radius.
	}
	else
		radiusfactor = (((linktype = chainlink) == MT_NULL) ? 2 : 1);

	if (!mchainlike)
		mchainlike = (firsttype == chainlink);
	widthfactor = (mchainlike ? 1 : 2);

	mflagsapply = (mthing->args[8] & TMM_CLIP) ? 0 : (MF_NOCLIP|MF_NOCLIPHEIGHT);
	mflags2apply = ((mthing->options & MTF_OBJECTFLIP) ? MF2_OBJECTFLIP : 0);
	meflagsapply = ((mthing->options & MTF_OBJECTFLIP) ? MFE_VERTICALFLIP : 0);

	msound = (mchainlike ? 0 : (mwidth & 1));

	// Quick and easy preparatory variable setting
	mphase = (FixedAngle(mphase << FRACBITS) >> ANGLETOFINESHIFT);
	mroll = (FixedAngle(mroll << FRACBITS) >> ANGLETOFINESHIFT);

#define makemace(mobjtype, dist, moreflags2) {\
	spawnee = P_SpawnMobj(mobj->x, mobj->y, mobj->z, mobjtype);\
	P_SetTarget(&spawnee->tracer, mobj);\
	spawnee->threshold = mphase;\
	spawnee->friction = mroll;\
	spawnee->movefactor = mwidthset;\
	spawnee->movecount = dist;\
	spawnee->angle = myaw;\
	spawnee->flags |= (MF_NOGRAVITY|mflagsapply);\
	spawnee->flags2 |= (mflags2apply|moreflags2);\
	spawnee->eflags |= meflagsapply;\
	P_SetTarget(&hprev->hnext, spawnee);\
	P_SetTarget(&spawnee->hprev, hprev);\
	hprev = spawnee;\
}

	mdosound = (mspeed && !(mthing->args[8] & TMM_SILENT));
	mdocenter = (macetype && (mthing->args[8] & TMM_CENTERLINK));

	// The actual spawning of spokes
	while (mnumspokes-- > 0)
	{
		// Offsets
		if (mthing->args[8] & TMM_SWING) // Swinging
			mroll = (mroll - mspokeangle) & FINEMASK;
		else // Spinning
			mphase = (mphase - mspokeangle) & FINEMASK;

		if (mnumnospokes && !(mnumspokes % mnumnospokes)) // Skipping a "missing" spoke
		{
			if (mobj->type != MT_CHAINMACEPOINT)
				continue;

			linktype = chainlink;
			firsttype = ((mthing->args[8] & TMM_DOUBLESIZE) ? MT_BIGGRABCHAIN : MT_SMALLGRABCHAIN);
			mmaxlength = 1 + (mlength - 1) * radiusfactor;
			radiusfactor = widthfactor = 1;
		}
		else
		{
			if (mobj->type == MT_CHAINMACEPOINT)
			{
				// Make the links the same type as the end - repeated above
				if (mthing->args[8] & TMM_MACELINKS)
				{
					linktype = macetype;
					radiusfactor = 2;
				}
				else
					radiusfactor = (((linktype = chainlink) == MT_NULL) ? 2 : 1);

				firsttype = macetype;
				widthfactor = 2;
			}

			mmaxlength = mlength;
		}

		mwidthset = mwidth;
		mlengthset = mminlength;

		if (mdocenter) // Innermost link
			makemace(linktype, 0, 0);

		// Out from the center...
		if (linktype)
		{
			while ((++mlengthset) < mmaxlength)
				makemace(linktype, radiusfactor*mlengthset, 0);
		}
		else
			mlengthset = mmaxlength;

		// Outermost mace/link
		if (firsttype)
			makemace(firsttype, radiusfactor*mlengthset, MF2_AMBUSH);

		if (!mwidth)
		{
			if (mdosound && mnumspokes <= mmin) // Can it make a sound?
				spawnee->flags2 |= MF2_BOSSNOTRAP;
		}
		else
		{
			// Across the bar!
			if (!firsttype)
				mwidthset = -mwidth;
			else if (mwidth > 0)
			{
				while ((mwidthset -= widthfactor) > -mwidth)
				{
					makemace(firsttype, radiusfactor*mlengthset, MF2_AMBUSH);
					if (mdosound && (mwidthset == msound) && mnumspokes <= mmin) // Can it make a sound?
						spawnee->flags2 |= MF2_BOSSNOTRAP;
				}
			}
			else
			{
				while ((mwidthset += widthfactor) < -mwidth)
				{
					makemace(firsttype, radiusfactor*mlengthset, MF2_AMBUSH);
					if (mdosound && (mwidthset == msound) && mnumspokes <= mmin) // Can it make a sound?
						spawnee->flags2 |= MF2_BOSSNOTRAP;
				}
			}
			mwidth = -mwidth;

			// Outermost mace/link again!
			if (firsttype)
				makemace(firsttype, radiusfactor*(mlengthset--), MF2_AMBUSH);

			// ...and then back into the center!
			if (linktype)
				while (mlengthset > mminlength)
					makemace(linktype, radiusfactor*(mlengthset--), 0);

			if (mdocenter) // Innermost link
				makemace(linktype, 0, 0);
		}
	}
#undef makemace
	return true;
}

static dboolean P_SetupParticleGen(mapthing_t *mthing, mobj_t *mobj)
{
	fixed_t radius, speed, zdist;
	int32_t type, numdivisions, anglespeed, ticcount;
	angle_t angledivision;
	int32_t line;
	const size_t mthingi = (size_t)(mthing - mapthings);

	if (!udmf)
		return false;

	// Find the corresponding linedef special, using args[6] as tag
	line = mthing->args[6] ? Tag_FindLineSpecial(15, mthing->args[6]) : -1;

	type = mthing->stringargs[0] ? get_number(mthing->stringargs[0]) : MT_PARTICLE;

	ticcount = mthing->args[4];
	if (ticcount < 1)
		ticcount = 3;

	numdivisions = mthing->args[0];

	if (numdivisions)
	{
		radius = mthing->args[1] << FRACBITS;
		anglespeed = (mthing->args[3]) % 360;
		angledivision = 360/numdivisions;
	}
	else
	{
		numdivisions = 1; // Simple trick to make P_ParticleGenSceneryThink simpler.
		radius = 0;
		anglespeed = 0;
		angledivision = 0;
	}

	speed = abs(mthing->args[2]) << FRACBITS;
	if (mthing->options & MTF_OBJECTFLIP)
		speed *= -1;

	zdist = abs(mthing->args[5]) << FRACBITS;

	CONS_Debug(DBG_GAMELOGIC, "Particle Generator (mapthing #%s):\n"
		"Radius is %d\n"
		"Speed is %d\n"
		"Anglespeed is %d\n"
		"Numdivisions is %d\n"
		"Angledivision is %d\n"
		"Type is %d\n"
		"Tic separation is %d\n",
		sizeu1(mthingi), radius, speed, anglespeed, numdivisions, angledivision, type, ticcount);

	if (line == -1)
		CONS_Debug(DBG_GAMELOGIC, "Spawn Z is %d\nZ dist is %d\n", mobj->z, zdist);
	else
		CONS_Debug(DBG_GAMELOGIC, "Heights are taken from control sector\n");

	mobj->angle = 0;
	mobj->movefactor = speed;
	mobj->lastlook = numdivisions;
	mobj->movedir = angledivision*ANG1;
	mobj->movecount = anglespeed*ANG1;
	mobj->friction = radius;
	mobj->threshold = type;
	mobj->reactiontime = ticcount;
	mobj->extravalue1 = zdist;
	mobj->cvmem = line;
	mobj->watertop = mobj->waterbottom = 0;
	return true;
}

static mobj_t *P_MakeSoftwareCorona(mobj_t *mo, int32_t height)
{
	mobj_t *corona = P_SpawnMobjFromMobj(mo, 0, 0, height<<FRACBITS, MT_PARTICLE);
	corona->sprite = SPR_FLAM;
	corona->frame = (FF_FULLBRIGHT|FF_TRANS90|12);
	corona->tics = -1;
	return corona;
}

void P_InitSkyboxPoint(mobj_t *mobj, mapthing_t *mthing)
{
	mtag_t tag = mthing->tid;
	if (tag < 0 || tag > 15)
	{
		CONS_Debug(DBG_GAMELOGIC, "P_InitSkyboxPoint: Skybox ID %d of mapthing %s is not between 0 and 15!\n", tag, sizeu1((size_t)(mthing - mapthings)));
		return;
	}

	if (mthing->args[0])
		P_SetTarget(&skyboxcenterpnts[tag], mobj);
	else
		P_SetTarget(&skyboxviewpnts[tag], mobj);
}

static dboolean P_MapAlreadyHasStarPost(mobj_t *mobj)
{
	thinker_t *th;
	mobj_t *mo2;

	for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
	{
		if (th->function == (actionf_p1)P_RemoveThinkerDelayed)
			continue;

		mo2 = (mobj_t *)th;

		if (mo2 == mobj)
			continue;

		if (mo2->type == MT_STARPOST && mo2->health == mobj->health)
			return true;
	}

	return false;
}

static skincolornum_t P_HandleNewSpringColor(fixed_t speedval)
{
	if (speedval >= 70 *FRACUNIT)
	{
		return SKINCOLOR_RED;
	}
	else if (speedval > 40*FRACUNIT && speedval < 70*FRACUNIT)
	{
		return SKINCOLOR_YELLOW;
	}
	else if (speedval > 30*FRACUNIT && speedval <= 40*FRACUNIT)
	{
		return SKINCOLOR_BLUEBERRY;
	}
	else if (speedval > 0 && speedval <= 30 *FRACUNIT)
	{
		return SKINCOLOR_GREY;
	}
	else
	{
		return SKINCOLOR_WHITE;
	}
}

static dboolean P_SetupSpawnedMapThing(mapthing_t *mthing, mobj_t *mobj, dboolean *doangle)
{
	dboolean override = LUA_HookMapThingSpawn(mobj, mthing);

	if (P_MobjWasRemoved(mobj))
		return false;

	if (override)
		return true;

	switch (mobj->type)
	{
	case MT_EMBLEM:
	{
		if (!P_SetupEmblem(mthing, mobj))
			return false;
		break;
	}
	case MT_SKYBOX:
	{
		P_InitSkyboxPoint(mobj, mthing);
		break;
	}
	case MT_EGGSTATUE:
		if (mthing->args[1])
		{
			mobj->color = SKINCOLOR_GOLD;
			mobj->colorized = true;
		}
		break;
	case MT_FAN:
		if (mthing->args[1] & TMF_INVISIBLE)
		{
			P_UnsetThingPosition(mobj);
			if (sector_list)
			{
				P_DelSeclist(sector_list);
				sector_list = NULL;
			}
			mobj->flags |= MF_NOSECTOR; // this flag basically turns it invisible
			P_SetThingPosition(mobj);
		}
		if (mthing->args[1] & TMF_NODISTANCECHECK)
			mobj->flags2 |= MF2_AMBUSH;
		if (mthing->args[0])
			mobj->health = mthing->args[0];
		else
			mobj->health = FixedMul(mobj->subsector->sector->ceilingheight - mobj->subsector->sector->floorheight, 3*(FRACUNIT/4)) >> FRACBITS;
		break;
	case MT_BALLOON:
		if (mthing->stringargs[0])
			mobj->color = get_number(mthing->stringargs[0]);
		if (mthing->args[0])
			mobj->flags2 |= MF2_AMBUSH;
		break;
	case MT_FLAME:
		if (mthing->args[0])
		{
			mobj_t *corona = P_MakeSoftwareCorona(mobj, 20);
			P_SetScale(corona, (corona->destscale = mobj->scale*3));
			P_SetTarget(&mobj->tracer, corona);
		}
		break;
	case MT_WATERDRIP:
		mobj->tics = 3*TICRATE + mthing->args[0];
		break;
	case MT_FLAMEJET:
	case MT_VERTICALFLAMEJET:
		mobj->movecount = mthing->args[0];
		mobj->threshold = mthing->args[1];
		mobj->movedir = mthing->args[2];
		if (mthing->args[3])
			mobj->flags2 |= MF2_AMBUSH;
		break;
	case MT_MACEPOINT:
	case MT_CHAINMACEPOINT:
	case MT_CHAINPOINT:
	case MT_CUSTOMMACEPOINT:
		if (!P_SetupMace(mthing, mobj, doangle))
			return false;
		break;
	case MT_PARTICLEGEN:
		if (!P_SetupParticleGen(mthing, mobj))
			return false;
		break;
	case MT_NIGHTSBUMPER:
		// Pitch of the bumper is set in 30 degree increments.
		mobj->threshold = ((mthing->pitch/30) + 3) % 12;
		P_SetMobjState(mobj, mobj->info->spawnstate + mobj->threshold);
		break;
	case MT_TUBEWAYPOINT:
	{
		uint8_t sequence = mthing->args[0];
		uint8_t id = mthing->args[1];
		mobj->health = id;
		mobj->threshold = sequence;
		P_AddTubeWaypoint(sequence, id, mobj);
		break;
	}
	break;
	case MT_AXIS:
		// Inverted if args[3] is set
		if (mthing->args[3])
			mobj->flags2 |= MF2_AMBUSH;

		mobj->radius = abs(mthing->args[2]) << FRACBITS;
		// FALLTHRU
	case MT_AXISTRANSFER:
	case MT_AXISTRANSFERLINE:
		// Mare it belongs to
		mobj->threshold = min(mthing->args[0], 7);

		// # in the mare
		mobj->health = mthing->args[1];

		mobj->flags2 |= MF2_AXIS;
		break;
	case MT_STARPOST:
		mobj->health = mthing->args[0] + 1;
		if (!P_MapAlreadyHasStarPost(mobj))
			numstarposts++;
		break;
	case MT_BOSS3WAYPOINT:
		mobj->health = mthing->angle;
		mobj->movecount = mthing->extrainfo;
		P_SetTarget(&mobj->tracer, boss3cap);
		P_SetTarget(&boss3cap, mobj);
		numbosswaypoints++;
		break;
	case MT_SPIKE:
		// Pop up spikes!
		if (mthing->args[0])
		{
			mobj->flags &= ~MF_SCENERY;
			mobj->fuse = mthing->args[1];
		}
		if (mthing->args[2] & TMSF_RETRACTED)
			P_SetMobjState(mobj, mobj->info->meleestate);
		// Use per-thing collision for spikes unless the intangible flag is checked.
		if (!(mthing->args[2] & TMSF_INTANGIBLE) && !metalrecording)
		{
			P_UnsetThingPosition(mobj);
			mobj->flags &= ~(MF_NOBLOCKMAP|MF_NOGRAVITY|MF_NOCLIPHEIGHT);
			mobj->flags |= MF_SOLID;
			P_SetThingPosition(mobj);
		}
		break;
	case MT_WALLSPIKE:
		// Pop up spikes!
		if (mthing->args[0])
		{
			mobj->flags &= ~MF_SCENERY;
			mobj->fuse = mthing->args[1];
		}
		if (mthing->args[2] & TMSF_RETRACTED)
			P_SetMobjState(mobj, mobj->info->meleestate);
		// Use per-thing collision for spikes unless the intangible flag is checked.
		if (!(mthing->args[2] & TMSF_INTANGIBLE) && !metalrecording)
		{
			P_UnsetThingPosition(mobj);
			mobj->flags &= ~(MF_NOBLOCKMAP | MF_NOCLIPHEIGHT);
			mobj->flags |= MF_SOLID;
			P_SetThingPosition(mobj);
		}
		// spawn base
		{
			const angle_t mobjangle = FixedAngle(mthing->angle << FRACBITS); // the mobj's own angle hasn't been set quite yet so...
			const fixed_t baseradius = mobj->radius - mobj->scale;
			mobj_t* base = P_SpawnMobj(
				mobj->x - P_ReturnThrustX(mobj, mobjangle, baseradius),
				mobj->y - P_ReturnThrustY(mobj, mobjangle, baseradius),
				mobj->z, MT_WALLSPIKEBASE);
			base->angle = mobjangle + ANGLE_90;
			base->destscale = mobj->destscale;
			P_SetScale(base, mobj->scale);
			P_SetTarget(&base->target, mobj);
			P_SetTarget(&mobj->tracer, base);
		}
		break;
	case MT_BIGTUMBLEWEED:
	case MT_LITTLETUMBLEWEED:
		if (mthing->args[0])
		{
			fixed_t offset = FixedMul(16*FRACUNIT, mobj->scale);
			mobj->momx += P_RandomChance(PR_DECORATION, FRACUNIT/2) ? offset : -offset;
			mobj->momy += P_RandomChance(PR_DECORATION, FRACUNIT/2) ? offset : -offset;
			mobj->momz += offset;
			mobj->flags2 |= MF2_AMBUSH;
		}
		break;
	// SRB2Kart
	case MT_WAYPOINT:
	{
		const fixed_t mobjscale =
			mapheaderinfo[gamemap-1]->default_waypoint_radius;
		mtag_t tag = mthing->tid;

		if (mthing->args[1] > 0)
			mobj->radius = (mthing->args[1]) * FRACUNIT;
		else if (mobjscale > 0)
			mobj->radius = mobjscale;
		else
			mobj->radius = DEFAULT_WAYPOINT_RADIUS * mapobjectscale;

		// Use threshold to store the next waypoint ID
		// movecount is being used for the current waypoint ID
		// reactiontime lets us know if we can respawn at it
		// lastlook is used for indicating the waypoint is a shortcut
		// extravalue1 is used for indicating the waypoint is disabled
		// extravalue2 is used for indicating the waypoint is the finishline
		// watertop is the waypoint's drift settings
		mobj->threshold = mthing->args[0];

		mobj->movecount = tag;

		if (mthing->args[2] & TMWPF_DISABLED)
		{
			mobj->extravalue1 = 0; // The waypoint is disabled if extra is on
		}
		else
		{
			mobj->extravalue1 = 1;
		}
		if (mthing->args[2] & TMWPF_SHORTCUT)
		{
			mobj->lastlook = 1; // the waypoint is a shortcut if objectspecial is on
		}
		else
		{
			mobj->lastlook = 0;
		}
		if (mthing->args[2] & TMWPF_NORESPAWN)
		{
			mobj->reactiontime = 0; // Can't respawn at if Ambush is on
		}
		else
		{
			mobj->reactiontime = 1;
		}

		if (mthing->args[2] & TMWPF_FINISHLINE)
		{
			mobj->extravalue2 = 1; // args[2] of 1 means the waypoint is at the finish line
			mobj->reactiontime = 0; // Also don't respawn at finish lines

			line_t *finishline = P_FindNearestLine(mobj->x, mobj->y,
					mobj->subsector->sector, K_UsingPatchedMap() ? -1 : 2001); // case 2001: Finish Line
			if (finishline != NULL)
			{
				P_UnsetThingPosition(mobj);
				P_ClosestPointOnLine(mobj->x, mobj->y, finishline, (vertex_t *)&mobj->x);
				P_SetThingPosition(mobj);

				if (K_UsingPatchedMap() && !usedspawnfinishline)
				{
					CONS_Alert(CONS_WARNING, "Using waypoints to generate finish lines is deprecated, please add mapthing type 6502 to your patch. Read the wiki article for details\n");
					finishline->special = 2001; // Finish Line
					finishline->activation = SPAC_CROSS|SPAC_REPEATSPECIAL;
					finishline->args[0] = mthing->args[4];
					if (!(mthing->args[4] & TMCFF_NOSPREAD))
						SpreadFinishLine(finishline, NULL, mthing->args[4]);
				}
			}
		}
		else
		{
			mobj->extravalue2 = 0;
		}

		mobj->watertop = mthing->args[3];

		// Sryder 2018-12-7: Grabbed this from the old MT_BOSS3WAYPOINT section so they'll be in the waypointcap instead
		P_SetTarget(&mobj->tracer, waypointcap);
		P_SetTarget(&waypointcap, mobj);
		break;
	}
	case MT_WAYPOINT_ANCHOR:
	{
		// Get the drift parameters from the anchor's spawnpoint.

		// Drift left
		mobj->extravalue1 = ((mthing->options & MTF_OBJECTSPECIAL) >> 1);

		if (mthing->options & MTF_AMBUSH)
		{
			// Drift right
			mobj->extravalue1 = ((mthing->options & MTF_AMBUSH) >> 1);
		}

		// Powerslide offset
		mobj->extravalue1 = max(0, min(4, mobj->extravalue1 - (mthing->options & 1)));

		if (mthing->extrainfo)
		{
			// If the parameter is set at all, this anchor ends the drift.
			mobj->extravalue1 = 5;
		}
		break;
	}
	case MT_BOTHINT:
	{
		// Change size
		if (mthing->args[0] > 0)
		{
			mobj->radius = mthing->args[0] * FRACUNIT;
		}
		else
		{
			mobj->radius = 32 * mapobjectscale;
		}

		// Steer away instead of towards
		if (mthing->args[2])
		{
			mobj->extravalue1 = 0;
		}
		else
		{
			mobj->extravalue1 = 1;
		}

		// Steering amount
		if (mthing->args[1] == 0)
		{
			mobj->extravalue2 = 4;
		}
		else
		{
			mobj->extravalue2 = mthing->args[1];
		}
		break;
	}
	case MT_RANDOMITEM:
	{
		mobj->flags2 |= MF2_BOSSNOTRAP; // mark as here on map start
		break;
	}
	case MT_ITEMCAPSULE:
	{
		// we have to adjust for reverse gravity early so that the below grounded checks work
		if (mthing->options & MTF_OBJECTFLIP)
		{
			mobj->eflags |= MFE_VERTICALFLIP;
			mobj->flags2 |= MF2_OBJECTFLIP;
			mobj->z += FixedMul(mobj->extravalue1, mobj->info->height) - mobj->height;
		}

		// determine whether this capsule is grounded or aerial
		if (mobj->subsector->sector->ffloors)
			P_AdjustMobjFloorZ_FFloors(mobj, mobj->subsector->sector, 0);
		if (mobj->subsector->polyList)
			P_AdjustMobjFloorZ_PolyObjs(mobj, mobj->subsector);
		if (!P_IsObjectOnGround(mobj))
			mobj->flags |= MF_NOGRAVITY;

		// Item type
		static kartitemtype_e rrtypes[] = {
			KITEM_SNEAKER,
			KITEM_ROCKETSNEAKER,
			KITEM_INVINCIBILITY,
			KITEM_BANANA,
			KITEM_EGGMAN,
			KITEM_ORBINAUT,
			KITEM_JAWZ,
			KITEM_MINE,
			MAXKARTITEMS, // LANDMINE
			KITEM_BALLHOG,
			KITEM_SPB,
			KITEM_GROW,
			KITEM_SHRINK,
			KITEM_THUNDERSHIELD,
			KITEM_BUBBLESHIELD,
			KITEM_FLAMESHIELD,
			KITEM_HYUDORO,
			KITEM_POGOSPRING,
			KITEM_SUPERRING,
			KITEM_KITCHENSINK,
			MAXKARTITEMS, // DROPTARGET
			MAXKARTITEMS, // GARDENTOP
			MAXKARTITEMS, // GACHABOM
			MAXKARTITEMS, // STONESHOE
			MAXKARTITEMS, // TOXOMISTER
		};

		if (mthing->stringargs[0] != NULL)
			mobj->threshold = DEH_FindKartItem(mthing->stringargs[0]);
		else if (mapnamespace == MNS_RINGRACERS && mthing->args[0] > 0 && (size_t)mthing->args[0] < sizeof(rrtypes)/sizeof(*rrtypes))
			mobj->threshold = rrtypes[mthing->args[0] - 1];

		// Parameter = extra items (x5 for rings)
		mobj->movecount += mthing->args[1];

		// Ambush = double size (grounded) / half size (aerial)
		if (!(mthing->args[2] & TMICF_INVERTSIZE) == !P_IsObjectOnGround(mobj))
		{
			mobj->extravalue1 <<= 1;
			mobj->scalespeed <<= 1;
		}

		const fixed_t blimit = FixedDiv(MAPBLOCKSIZE, mobj->info->radius);
		if (mobj->extravalue1 > blimit)
		{
			 // don't make them larger than the blockmap can handle
			mobj->extravalue1 = blimit;
		}

		break;
	}
	case MT_AAZTREE_HELPER:
	{
		fixed_t top = mobj->z;
		uint8_t i;
		uint8_t locnumsegs = abs(mthing->args[0])+2;
		uint8_t numleaves = max(3, (abs(mthing->args[1]+1) % 6) + 3);
		mobj_t *coconut;

		// Spawn tree segments
		for (i = 0; i < locnumsegs; i++)
		{
			P_SpawnMobj(mobj->x, mobj->y, top, MT_AAZTREE_SEG);
			top += FixedMul(mobjinfo[MT_AAZTREE_SEG].height, mobj->scale);
		}

		// Big coconut topper
		coconut = P_SpawnMobj(mobj->x, mobj->y, top - (8<<FRACBITS), MT_AAZTREE_COCONUT);
		P_SetScale(coconut, (coconut->destscale = (2*mobj->scale)));

		// Spawn all of the papersprite leaves
		for (i = 0; i < numleaves; i++)
		{
			mobj_t *leaf;

			mobj->angle = FixedAngle((i * (360/numleaves))<<FRACBITS);

			leaf = P_SpawnMobj(mobj->x + FINECOSINE((mobj->angle>>ANGLETOFINESHIFT) & FINEMASK),
				mobj->y + FINESINE((mobj->angle>>ANGLETOFINESHIFT) & FINEMASK), top, MT_AAZTREE_LEAF);
			leaf->angle = mobj->angle;

			// Small coconut for each leaf
			P_SpawnMobj(mobj->x + (32 * FINECOSINE((mobj->angle>>ANGLETOFINESHIFT) & FINEMASK)),
				mobj->y + (32 * FINESINE((mobj->angle>>ANGLETOFINESHIFT) & FINEMASK)), top - (24<<FRACBITS), MT_AAZTREE_COCONUT);
		}

		P_RemoveMobj(mobj); // Don't need this helper obj anymore
		return false;
		break;
	}
	case MT_LOOPCENTERPOINT:
	{
		Obj_InitLoopCenter(mobj);
		break;
	}
	case MT_EMMY:
	{
		if (mthing->options & MTF_OBJECTSPECIAL) // Mario Block version
			mobj->flags &= ~(MF_NOGRAVITY|MF_NOCLIPHEIGHT);
		else
		{
			fixed_t zheight = mobj->z;
			mobj_t *tokenobj;

			if (mthing->options & MTF_OBJECTFLIP)
				zheight += mobj->height-FixedMul(mobjinfo[MT_TOKEN].height, mobj->scale); // align with emmy properly!

			tokenobj = P_SpawnMobj(mobj->x, mobj->y, zheight, MT_TOKEN);
			P_SetTarget(&mobj->tracer, tokenobj);
			tokenobj->destscale = mobj->scale;
			P_SetScale(tokenobj, mobj->scale);
			if (mthing->options & MTF_OBJECTFLIP) // flip token to match emmy
			{
				tokenobj->eflags |= MFE_VERTICALFLIP;
				tokenobj->flags2 |= MF2_OBJECTFLIP;
			}
		}

		// We advanced tokenbits earlier due to the return check.
		// Subtract 1 here for the correct value.
		mobj->health = 1 << (tokenbits - 1);
		break;

	}
	case MT_REDSPRING:
	case MT_BLUESPRING:
	case MT_GRAYSPRING:
	case MT_YELLOWSPRING:
	case MT_INVISSPRING:
	{
		if (mthing->args[0] & TMSPR_NOGRAVITY)
			mobj->gravity = 0;
		break;
	}
	case MT_YELLOWDIAG:
	case MT_REDDIAG:
	case MT_BLUEDIAG:
	case MT_GRAYDIAG:
	{
		mobj->angle = FixedAngle(mthing->angle << FRACBITS);
		if (mthing->args[0] & TMSPR_NOGRAVITY)
			mobj->gravity = 0;
		if (mthing->args[0] & TMSPR_ROTATEEXTRA)
			mobj->angle += ANGLE_22h;
		*doangle = false;
		break;
	}
	case MT_AMBIENT:
		if (mthing->stringargs[0])
			mobj->threshold = get_number(mthing->stringargs[0]);
		mobj->health = mthing->args[0] ? mthing->args[0] : TICRATE;
		break;
	case MT_DASHRING:
	case MT_RAINBOWDASHRING:
	{
		Obj_DashRingSetup(mobj, mthing);
		break;
	}
	case MT_SNEAKERPANEL:
	{
		Obj_SneakerPanelSetup(mobj, mthing);
		break;
	}
	case MT_SNEAKERPANELSPAWNER:
	{
		Obj_SneakerPanelSpawnerSetup(mobj, mthing);
		break;
	}
	case MT_NEOSPRING:
		mobj->extravalue1 = mthing->args[2] > 0 ? mthing->args[2]*FRACUNIT : mobj->info->mass;
		mobj->extravalue2 = 0;

		if (mthing->args[0] & TMSPR_NOGRAVITY)
			mobj->flags |= MF_NOGRAVITY;

		mobj->color = P_HandleNewSpringColor(mobj->extravalue1);
		break;
	case MT_NEODIAG:
		mobj->extravalue1 = mthing->args[2]> 0 ? mthing->args[2]*FRACUNIT : mobj->info->mass;
		mobj->extravalue2 = mthing->args[2] > 0 ? mthing->args[2]*FRACUNIT : mobj->info->damage;

		mobj->angle = FixedAngle(mthing->angle << FRACBITS);
		if (mthing->args[0] & TMSPR_NOGRAVITY)
			mobj->flags |= MF_NOGRAVITY;
		if (mthing->args[0] & TMSPR_ROTATEEXTRA)
			mobj->angle += ANGLE_22h;

		mobj->color = P_HandleNewSpringColor(max(mobj->extravalue1, mobj->extravalue2));

		*doangle = false;
		break;
	case MT_NEOHORIZ:
	{
		mobj->extravalue1 = 0;
		mobj->extravalue2 = mthing->args[2] > 0 ? mthing->args[2]*FRACUNIT : mobj->info->damage;

		// Offset sprite of horizontal springs
		angle_t a = mobj->angle + ANGLE_180;
		mobj->sprxoff = FixedMul(mobj->radius, FINECOSINE(a >> ANGLETOFINESHIFT));
		mobj->spryoff = FixedMul(mobj->radius, FINESINE(a >> ANGLETOFINESHIFT));

		if (mthing->args[0] & TMSPR_NOGRAVITY)
			mobj->flags |= MF_NOGRAVITY;

		mobj->color = P_HandleNewSpringColor(mobj->extravalue2);
		break;
	}
	default:
		break;
	}

	if (P_MobjWasRemoved(mobj))
		return false;

	if (mobj->flags & MF_BOSS)
	{
		if (mthing->args[1]) // No egg trap for this boss
			mobj->flags2 |= MF2_BOSSNOTRAP;
	}
	if (mobj->flags & MF_PUSHABLE)
	{
		switch (mthing->args[0])
		{
			case TMP_NORMAL:
			default:
				break;
			case TMP_SLIDE:
				mobj->flags2 |= MF2_SLIDEPUSH;
				mobj->flags |= MF_BOUNCE;
				break;
			case TMP_IMMOVABLE:
				mobj->flags &= ~MF_PUSHABLE;
				break;
			case TMP_CLASSIC:
				mobj->flags2 |= MF2_CLASSICPUSH;
				break;
		}
	}

	if (mobj->flags & MF_SPRING && mobj->info->painchance == 3)
	{
		if (mthing->args[0])
			mobj->flags2 |= MF2_AMBUSH;
	}
	if ((mobj->flags & MF_MONITOR) && mobj->info->speed != 0)
	{
		switch (mthing->args[1])
		{
			case TMMR_SAME:
			default:
				break;
			case TMMR_WEAK:
				mobj->flags2 |= MF2_AMBUSH;
				break;
			case TMMR_STRONG:
				mobj->flags2 |= MF2_STRONGBOX;
		}
	}
	// Custom ambient sounds
	if ((mobj->flags & MF_AMBIENT) && mobj->type != MT_AMBIENT)
	{
		mobj->threshold = mobj->info->seesound;
		mobj->health = mobj->info->spawnhealth;
	}

	return true;
}

// TODO: 2.3: Delete (Pre-UDMF backwards compatibility stuff)
static void P_SetAmbush(mapthing_t *mthing, mobj_t *mobj)
{
	if (mobj->type == MT_NIGHTSBUMPER
		|| mobj->type == MT_AXIS
		|| mobj->type == MT_AXISTRANSFER
		|| mobj->type == MT_AXISTRANSFERLINE
		|| mobj->type == MT_NIGHTSBUMPER
		|| mobj->type == MT_STARPOST)
		return;

	if ((mthing->options & MTF_OBJECTSPECIAL) && (mobj->flags & MF_PUSHABLE))
		return;

	mobj->flags2 |= MF2_AMBUSH;
}

void P_CopyMapThingBehaviorFieldsToMobj(const mapthing_t *mthing, mobj_t *mobj)
{
	size_t arg = SIZE_MAX;

	for (arg = 0; arg < NUM_MAPTHING_ARGS; arg++)
	{
		mobj->args[arg] = mthing->args[arg];
	}

	for (arg = 0; arg < NUM_MAPTHING_STRINGARGS; arg++)
	{
		size_t len = 0;

		if (mthing->stringargs[arg])
		{
			len = strlen(mthing->stringargs[arg]);
		}

		if (len == 0)
		{
			Z_Free(mobj->stringargs[arg]);
			mobj->stringargs[arg] = NULL;
			continue;
		}

		mobj->stringargs[arg] = Z_Realloc(mobj->stringargs[arg], len + 1, PU_LEVEL, NULL);
		memcpy(mobj->stringargs[arg], mthing->stringargs[arg], len + 1);
	}
}

void P_CopyMapThingSpecialFieldsToMobj(const mapthing_t *mthing, mobj_t *mobj)
{
	size_t arg = SIZE_MAX;

	mobj->special = mthing->special;

	for (arg = 0; arg < NUM_SCRIPT_ARGS; arg++)
	{
		mobj->script_args[arg] = mthing->args[arg];
	}

	for (arg = 0; arg < NUM_SCRIPT_STRINGARGS; arg++)
	{
		size_t len = 0;

		if (mthing->script_stringargs[arg])
		{
			len = strlen(mthing->script_stringargs[arg]);
		}

		if (len == 0)
		{
			Z_Free(mobj->script_stringargs[arg]);
			mobj->script_stringargs[arg] = NULL;
			continue;
		}

		mobj->script_stringargs[arg] = Z_Realloc(mobj->script_stringargs[arg], len + 1, PU_LEVEL, NULL);
		memcpy(mobj->script_stringargs[arg], mthing->script_stringargs[arg], len + 1);
	}
}

static mobj_t *P_SpawnMobjFromMapThing(mapthing_t *mthing, fixed_t x, fixed_t y, fixed_t z, mobjtype_t i)
{
	mobj_t *mobj = NULL;
	dboolean doangle = true;

	mobj = P_SpawnMobj(x, y, z, i);

	if (mobj == NULL || P_MobjWasRemoved(mobj))
		return NULL;

	mobj->spawnpoint = mthing;

	if (doangle)
	{
		mobj->angle = FixedAngle(mthing->angle << FRACBITS);
	}

	mobj->pitch = FixedAngle(mthing->pitch << FRACBITS);
	mobj->roll = FixedAngle(mthing->roll << FRACBITS);

	P_SetScale(mobj, FixedMul(mobj->scale, mthing->scale));
	mobj->destscale = FixedMul(mobj->destscale, mthing->scale);

	mobj->spritexscale = mthing->spritexscale;
	mobj->spriteyscale = mthing->spriteyscale;

	mobj->tid = mthing->tid;
	P_AddThingTID(mobj);

	P_CopyMapThingBehaviorFieldsToMobj(mthing, mobj);
	P_CopyMapThingSpecialFieldsToMobj(mthing, mobj);

	if (!P_SetupSpawnedMapThing(mthing, mobj, &doangle))
	{
		if (P_MobjWasRemoved(mobj))
			return NULL;

		return mobj;
	}

	mthing->mobj = mobj;

	if ((mapnamespace == MNS_SRB2KART) && (mthing->options & MTF_AMBUSH))
		P_SetAmbush(mthing, mobj);

	// Generic reverse gravity for individual objects flag.
	if (mthing->options & MTF_OBJECTFLIP)
	{
		mobj->eflags |= MFE_VERTICALFLIP;
		mobj->flags2 |= MF2_OBJECTFLIP;
	}

	return mobj;
}

//
// P_SpawnMapThing
// The fields of the mapthing should
// already be in host byte order.
//
mobj_t *P_SpawnMapThing(mapthing_t *mthing)
{
	mobjtype_t i;
	mobj_t *mobj = NULL;
	fixed_t x, y, z;

	if (!mthing->type)
		return mobj; // Ignore type-0 things as NOPs

	if (mthing->type == 3328) // 3D Mode start Thing
		return mobj;

	if (!objectplacing && P_SpawnNonMobjMapThing(mthing))
		return mobj;

	i = P_GetMobjtype(mthing->type);
	if (i == MT_UNKNOWN)
		CONS_Alert(CONS_WARNING, M_GetText("Unknown thing type %d placed at (%d, %d)\n"), mthing->type, mthing->x, mthing->y);

	// Skip all returning/substitution code in objectplace.
	if (!objectplacing)
	{
		if (!P_AllowMobjSpawn(mthing, i))
			return mobj;

		i = P_GetMobjtypeSubstitute(mthing, i);
		if (i == MT_NULL) // Don't spawn mobj
			return mobj;
	}

	x = mthing->x << FRACBITS;
	y = mthing->y << FRACBITS;
	z = P_GetMapThingSpawnHeight(i, mthing, x, y);
	return P_SpawnMobjFromMapThing(mthing, x, y, z, i);
}

void P_SpawnHoop(mapthing_t *mthing)
{
	if (metalrecording)
		return;

	mobj_t *mobj = NULL;
	mobj_t *nextmobj = NULL;
	mobj_t *hoopcenter;
	TMatrix *pitchmatrix, *yawmatrix;
	fixed_t radius = mthing->args[0] << FRACBITS;
	fixed_t sizefactor = 4*FRACUNIT;
	fixed_t hoopsize = radius/sizefactor;
	int32_t i;
	angle_t fa;
	TVector v, *res;
	fixed_t x = mthing->x << FRACBITS;
	fixed_t y = mthing->y << FRACBITS;
	fixed_t z = P_GetMobjSpawnHeight(MT_HOOP, x, y, mthing->z << FRACBITS, 0, mthing->layer, false, mthing->scale);

	hoopcenter = P_SpawnMobj(x, y, z, MT_HOOPCENTER);
	hoopcenter->spawnpoint = mthing;
	hoopcenter->z -= hoopcenter->height/2;

	P_UnsetThingPosition(hoopcenter);
	hoopcenter->x = x;
	hoopcenter->y = y;
	P_SetThingPosition(hoopcenter);

	hoopcenter->movedir = mthing->pitch;
	pitchmatrix = RotateXMatrix(FixedAngle(hoopcenter->movedir << FRACBITS));
	hoopcenter->movecount = mthing->angle;
	yawmatrix = RotateZMatrix(FixedAngle(hoopcenter->movecount << FRACBITS));

	// For the hoop when it flies away
	hoopcenter->extravalue1 = hoopsize;
	hoopcenter->extravalue2 = radius/12;

	// Create the hoop!
	for (i = 0; i < hoopsize; i++)
	{
		fa = i*(FINEANGLES/hoopsize);
		v[0] = FixedMul(FINECOSINE(fa), radius);
		v[1] = 0;
		v[2] = FixedMul(FINESINE(fa), radius);
		v[3] = FRACUNIT;

		res = VectorMatrixMultiply(v, *pitchmatrix);
		memcpy(&v, res, sizeof(v));
		res = VectorMatrixMultiply(v, *yawmatrix);
		memcpy(&v, res, sizeof(v));

		mobj = P_SpawnMobj(x + v[0], y + v[1], z + v[2], MT_HOOP);
		mobj->z -= mobj->height/2;

		P_SetTarget(&mobj->target, hoopcenter); // Link the sprite to the center.
		mobj->fuse = 0;

		// Link all the sprites in the hoop together
		if (nextmobj)
		{
			P_SetTarget(&mobj->hprev, nextmobj);
			P_SetTarget(&mobj->hprev->hnext, mobj);
		}
		else
			P_SetTarget(&mobj->hprev, P_SetTarget(&mobj->hnext, NULL));

		nextmobj = mobj;
	}

	// Create the collision detectors!
	// Create them until the size is less than 8
	// But always create at least ONE set of collision detectors
	do
	{
		if (hoopsize >= 32)
			hoopsize -= 16;
		else
			hoopsize /= 2;

		radius = hoopsize*sizefactor;

		for (i = 0; i < hoopsize; i++)
		{
			fa = i*(FINEANGLES/hoopsize);
			v[0] = FixedMul(FINECOSINE(fa), radius);
			v[1] = 0;
			v[2] = FixedMul(FINESINE(fa), radius);
			v[3] = FRACUNIT;

			res = VectorMatrixMultiply(v, *pitchmatrix);
			memcpy(&v, res, sizeof(v));
			res = VectorMatrixMultiply(v, *yawmatrix);
			memcpy(&v, res, sizeof(v));

			mobj = P_SpawnMobj(x + v[0], y + v[1], z + v[2], MT_HOOPCOLLIDE);
			mobj->z -= mobj->height/2;

			// Link all the collision sprites together.
			P_SetTarget(&mobj->hnext, NULL);
			P_SetTarget(&mobj->hprev, nextmobj);
			P_SetTarget(&mobj->hprev->hnext, mobj);

			nextmobj = mobj;
		}
	} while (hoopsize >= 8);
}

static void P_SpawnItemRow(mapthing_t *mthing, mobjtype_t *itemtypes, uint8_t numitemtypes, int32_t numitems, fixed_t horizontalspacing, fixed_t verticalspacing, int16_t fixedangle)
{
	mapthing_t dummything;
	mobj_t *mobj = NULL;
	fixed_t x = mthing->x << FRACBITS;
	fixed_t y = mthing->y << FRACBITS;
	fixed_t z = mthing->z << FRACBITS;
	int32_t r;
	angle_t angle = FixedAngle(fixedangle << FRACBITS);
	angle_t fineangle = (angle >> ANGLETOFINESHIFT) & FINEMASK;

	dboolean isloopend = (mthing->type == mobjinfo[MT_LOOPENDPOINT].doomednum);
	mobj_t *loopanchor = NULL;

	dboolean inclusive = isloopend;

	for (r = 0; r < numitemtypes; r++)
	{
		dummything = *mthing;
		dummything.type = mobjinfo[itemtypes[r]].doomednum;
		// Skip all returning/substitution code in objectplace.
		if (!objectplacing)
		{
			if (!P_AllowMobjSpawn(&dummything, itemtypes[r]))
			{
				itemtypes[r] = MT_NULL;
				continue;
			}

			itemtypes[r] = P_GetMobjtypeSubstitute(&dummything, itemtypes[r]);
		}
	}

	z = P_GetMobjSpawnHeight(itemtypes[0], x, y, z, 0, mthing->layer, mthing->options & MTF_OBJECTFLIP, mthing->scale);

	if (isloopend)
	{
		const fixed_t length = (numitems - 1) * horizontalspacing / 2;

		mobj_t *loopcenter = Obj_FindLoopCenter(mthing->tid);

		// Spawn the anchor at the middle point of the line
		loopanchor = P_SpawnMobjFromMapThing(&dummything,
				x + FixedMul(length, FINECOSINE(fineangle)),
				y + FixedMul(length, FINESINE(fineangle)),
				z, MT_LOOPCENTERPOINT);

		if (P_MobjWasRemoved(loopanchor))
		{
			// No recovery.
			return;
		}

		loopanchor->spawnpoint = NULL;

		if (!P_MobjWasRemoved(loopanchor))
			Obj_LinkLoopAnchor(loopanchor, loopcenter, mthing->args[0]);
	}

	for (r = 0; r < numitems; r++)
	{
		mobjtype_t itemtype = itemtypes[r % numitemtypes];
		if (itemtype == MT_NULL)
			continue;
		dummything.type = mobjinfo[itemtype].doomednum;

		if (inclusive)
			mobj = P_SpawnMobjFromMapThing(&dummything, x, y, z, itemtype);

		x += FixedMul(horizontalspacing, FINECOSINE(fineangle));
		y += FixedMul(horizontalspacing, FINESINE(fineangle));
		z += (mthing->options & MTF_OBJECTFLIP) ? -verticalspacing : verticalspacing;

		if (!inclusive)
			mobj = P_SpawnMobjFromMapThing(&dummything, x, y, z, itemtype);

		if (P_MobjWasRemoved(mobj))
			continue;

		mobj->spawnpoint = NULL;

		if (!isloopend)
			continue;

		Obj_InitLoopEndpoint(mobj, loopanchor);
	}
}

static void P_SpawnSingularItemRow(mapthing_t *mthing, mobjtype_t itemtype, int32_t numitems, fixed_t horizontalspacing, fixed_t verticalspacing, int16_t fixedangle)
{
	mobjtype_t itemtypes[1] = { itemtype };
	P_SpawnItemRow(mthing, itemtypes, 1, numitems, horizontalspacing, verticalspacing, fixedangle);
}

static void P_SpawnItemCircle(mapthing_t *mthing, mobjtype_t *itemtypes, uint8_t numitemtypes, int32_t numitems, fixed_t size)
{
	mapthing_t dummything;
	mobj_t* mobj = NULL;
	fixed_t x = mthing->x << FRACBITS;
	fixed_t y = mthing->y << FRACBITS;
	fixed_t z = mthing->z << FRACBITS;
	angle_t angle = FixedAngle(mthing->angle << FRACBITS);
	angle_t fa;
	int32_t i;
	TVector v, *res;

	for (i = 0; i < numitemtypes; i++)
	{
		dummything = *mthing;
		dummything.type = mobjinfo[itemtypes[i]].doomednum;
		// Skip all returning/substitution code in objectplace.
		if (!objectplacing)
		{
			if (!P_AllowMobjSpawn(&dummything, itemtypes[i]))
			{
				itemtypes[i] = MT_NULL;
				continue;
			}

			itemtypes[i] = P_GetMobjtypeSubstitute(&dummything, itemtypes[i]);
		}
	}
	z = P_GetMobjSpawnHeight(itemtypes[0], x, y, z, 0, mthing->layer, false, mthing->scale);

	for (i = 0; i < numitems; i++)
	{
		mobjtype_t itemtype = itemtypes[i % numitemtypes];
		if (itemtype == MT_NULL)
			continue;
		dummything.type = mobjinfo[itemtype].doomednum;

		fa = i*FINEANGLES/numitems;
		v[0] = FixedMul(FINECOSINE(fa), size);
		v[1] = 0;
		v[2] = FixedMul(FINESINE(fa), size);
		v[3] = FRACUNIT;

		res = VectorMatrixMultiply(v, *RotateZMatrix(angle));
		memcpy(&v, res, sizeof(v));

		mobj = P_SpawnMobjFromMapThing(&dummything, x + v[0], y + v[1], z + v[2], itemtype);

		if (P_MobjWasRemoved(mobj))
			continue;

		mobj->spawnpoint = NULL;

		mobj->z -= mobj->height/2;
	}
}

static void P_ParseItemTypes(char *itemstring, mobjtype_t *itemtypes, uint8_t *numitemtypes)
{
	char *tok;

	*numitemtypes = 0;
	if (itemstring)
	{
		char *stringcopy = Z_Malloc(strlen(itemstring) + 1, PU_LEVEL, NULL);
		memcpy(stringcopy, itemstring, strlen(itemstring));
		stringcopy[strlen(itemstring)] = '\0';

		tok = strtok(stringcopy, " ");
		while (tok && *numitemtypes < 128)
		{
			itemtypes[*numitemtypes] = get_number(tok);
			tok = strtok(NULL, " ");
			(*numitemtypes)++;
		}

		Z_Free(stringcopy);
	}
	else
	{
		//If no types are supplied, default to ring
		itemtypes[0] = MT_RING;
		*numitemtypes = 1;
	}
}

void P_SpawnItemPattern(mapthing_t *mthing)
{
	switch (mthing->type)
	{
	// Special placement patterns
	case 600: // 5 vertical rings (yellow spring)
		P_SpawnSingularItemRow(mthing, MT_RING, 5, 0, 64*FRACUNIT, 0);
		return;
	case 601: // 5 vertical rings (red spring)
		P_SpawnSingularItemRow(mthing, MT_RING, 5, 0, 128*FRACUNIT, 0);
		return;
	case 602: // 5 diagonal rings (yellow spring)
		P_SpawnSingularItemRow(mthing, MT_RING, 5, 64*FRACUNIT, 64*FRACUNIT, mthing->angle);
		return;
	case 603: // 10 diagonal rings (red spring)
		P_SpawnSingularItemRow(mthing, MT_RING, 10, 64*FRACUNIT, 64*FRACUNIT, mthing->angle);
		return;
	case 604: // Circle of rings (8 items)
	case 605: // Circle of rings (16 items)
	{
		int32_t numitems = (mthing->type & 1) ? 16 : 8;
		fixed_t size = (mthing->type & 1) ? 192*FRACUNIT : 96*FRACUNIT;
		mobjtype_t itemtypes[1] = { MT_RING };
		P_SpawnItemCircle(mthing, itemtypes, 1, numitems, size);
		return;
	}
	case 610: // Generic item row
	{
		mobjtype_t itemtypes[128]; //If you want to have a row with more than 128 different object types, you're crazy.
		uint8_t numitemtypes;
		if (!udmf)
			return;
		P_ParseItemTypes(mthing->stringargs[0], itemtypes, &numitemtypes);
		P_SpawnItemRow(mthing, itemtypes, numitemtypes, mthing->args[0], mthing->args[1] << FRACBITS, mthing->args[2] << FRACBITS, mthing->angle);
		return;
	}
	case 611: // Generic item circle
	{
		mobjtype_t itemtypes[128]; //If you want to have a circle with more than 128 different object types, you're crazy.
		uint8_t numitemtypes;
		if (!udmf)
			return;
		CONS_Printf("Itemstring: %s\n", mthing->stringargs[0]);
		P_ParseItemTypes(mthing->stringargs[0], itemtypes, &numitemtypes);
		P_SpawnItemCircle(mthing, itemtypes, numitemtypes, mthing->args[0], mthing->args[1] << FRACBITS);
		return;
	}
	default:
		return;
	}
}

void P_SpawnItemLine(mapthing_t *mt1, mapthing_t *mt2)
{
	const mobjtype_t type = P_GetMobjtype(mt1->type);
	const fixed_t diameter = 2 * FixedMul(mobjinfo[type].radius, mapobjectscale);
	const fixed_t dx = (mt2->x - mt1->x) * FRACUNIT;
	const fixed_t dy = (mt2->y - mt1->y) * FRACUNIT;
	const fixed_t dist = FixedHypot(dx, dy);
	const angle_t angle = R_PointToAngle2(0, 0, dx, dy);

	P_SpawnSingularItemRow(mt1, type, (dist / diameter) + 1, diameter, 0, AngleFixed(angle) / FRACUNIT);
}

//
// P_CheckMissileSpawn
// Moves the missile forward a bit and possibly explodes it right there.
//
dboolean P_CheckMissileSpawn(mobj_t *th)
{
	// move a little forward so an angle can be computed if it immediately explodes
	if (!(th->flags & MF_GRENADEBOUNCE)) // hack: bad! should be a flag.
	{
		th->x += th->momx>>1;
		th->y += th->momy>>1;
		th->z += th->momz>>1;
	}

	if (!P_TryMove(th, th->x, th->y, true, NULL))
	{
		P_ExplodeMissile(th);
		return false;
	}
	return true;
}

//
// P_SpawnXYZMissile
//
// Spawns missile at specific coords
//
mobj_t *P_SpawnXYZMissile(mobj_t *source, mobj_t *dest, mobjtype_t type,
	fixed_t x, fixed_t y, fixed_t z)
{
	mobj_t *th;
	angle_t an;
	int32_t dist;
	fixed_t speed;

	I_Assert(source != NULL);
	I_Assert(dest != NULL);

	if (source->eflags & MFE_VERTICALFLIP)
		z -= FixedMul(mobjinfo[type].height, source->scale);

	th = P_SpawnMobj(x, y, z, type);

	if (source->eflags & MFE_VERTICALFLIP)
		th->flags2 |= MF2_OBJECTFLIP;

	th->destscale = source->scale;
	P_SetScale(th, source->scale);

	speed = FixedMul(th->info->speed, th->scale);

	if (speed == 0)
	{
		CONS_Debug(DBG_GAMELOGIC, "P_SpawnXYZMissile - projectile has 0 speed! (mobj type %d)\n", type);
		speed = mobjinfo[MT_ROCKET].speed;
	}

	if (th->info->seesound)
		S_StartSound(th, th->info->seesound);

	P_SetTarget(&th->target, source); // where it came from
	an = R_PointToAngle2(x, y, dest->x, dest->y);

	th->angle = an;
	an >>= ANGLETOFINESHIFT;
	th->momx = FixedMul(speed, FINECOSINE(an));
	th->momy = FixedMul(speed, FINESINE(an));

	dist = P_AproxDistance(dest->x - x, dest->y - y);
	dist = dist / speed;

	if (dist < 1)
		dist = 1;

	th->momz = (dest->z - z) / dist;

	if (th->flags & MF_MISSILE)
		dist = P_CheckMissileSpawn(th);
	else
		dist = 1;

	return dist ? th : NULL;
}

//
// P_SpawnAlteredDirectionMissile
//
// Spawns a missile with same source as spawning missile yet an altered direction
//
mobj_t *P_SpawnAlteredDirectionMissile(mobj_t *source, mobjtype_t type, fixed_t x, fixed_t y, fixed_t z, int32_t shiftingAngle)
{
	mobj_t *th;
	angle_t an;
	fixed_t dist, speed;

	I_Assert(source != NULL);

	if (!(source->target) || !(source->flags & MF_MISSILE))
		return NULL;

	if (source->eflags & MFE_VERTICALFLIP)
		z -= FixedMul(mobjinfo[type].height, source->scale);

	th = P_SpawnMobj(x, y, z, type);

	if (source->eflags & MFE_VERTICALFLIP)
		th->flags2 |= MF2_OBJECTFLIP;

	th->destscale = source->scale;
	P_SetScale(th, source->scale);

	speed = FixedMul(th->info->speed, th->scale);

	if (speed == 0) // Backwards compatibility with 1.09.2
	{
		CONS_Printf("P_SpawnAlteredDirectionMissile - projectile has 0 speed! (mobj type %d)\nPlease update this SOC.", type);
		speed = mobjinfo[MT_ROCKET].speed;
	}

	if (th->info->seesound)
		S_StartSound(th, th->info->seesound);

	P_SetTarget(&th->target, source->target); // where it came from
	an = R_PointToAngle2(0, 0, source->momx, source->momy) + (ANG1*shiftingAngle);

	th->angle = an;
	an >>= ANGLETOFINESHIFT;
	th->momx = FixedMul(speed, FINECOSINE(an));
	th->momy = FixedMul(speed, FINESINE(an));

	dist = P_AproxDistance(source->momx*800, source->momy*800);
	dist = dist / speed;

	if (dist < 1)
		dist = 1;

	th->momz = (source->momz*800) / dist;

	if (th->flags & MF_MISSILE)
	{
		dist = P_CheckMissileSpawn(th);
		th->x -= th->momx>>1;
		th->y -= th->momy>>1;
		th->z -= th->momz>>1;
	}
	else
		dist = 1;

	return dist ? th : NULL;
}

//
// P_SpawnPointMissile
//
// Spawns a missile at specific coords
//
mobj_t *P_SpawnPointMissile(mobj_t *source, fixed_t xa, fixed_t ya, fixed_t za, mobjtype_t type,
	fixed_t x, fixed_t y, fixed_t z)
{
	mobj_t *th;
	angle_t an;
	fixed_t dist, speed;

	I_Assert(source != NULL);

	if (source->eflags & MFE_VERTICALFLIP)
		z -= FixedMul(mobjinfo[type].height, source->scale);

	th = P_SpawnMobj(x, y, z, type);

	if (source->eflags & MFE_VERTICALFLIP)
		th->flags2 |= MF2_OBJECTFLIP;

	th->destscale = source->scale;
	P_SetScale(th, source->scale);

	speed = FixedMul(th->info->speed, th->scale);

	if (speed == 0) // Backwards compatibility with 1.09.2
	{
		CONS_Printf("P_SpawnPointMissile - projectile has 0 speed! (mobj type %d)\nPlease update this SOC.", type);
		speed = mobjinfo[MT_ROCKET].speed;
	}

	if (th->info->seesound)
		S_StartSound(th, th->info->seesound);

	P_SetTarget(&th->target, source); // where it came from
	an = R_PointToAngle2(x, y, xa, ya);

	th->angle = an;
	an >>= ANGLETOFINESHIFT;
	th->momx = FixedMul(speed, FINECOSINE(an));
	th->momy = FixedMul(speed, FINESINE(an));

	dist = P_AproxDistance(xa - x, ya - y);
	dist = dist / speed;

	if (dist < 1)
		dist = 1;

	th->momz = (za - z) / dist;

	if (th->flags & MF_MISSILE)
		dist = P_CheckMissileSpawn(th);
	else
		dist = 1;

	return dist ? th : NULL;
}

//
// P_SpawnMissile
//
mobj_t *P_SpawnMissile(mobj_t *source, mobj_t *dest, mobjtype_t type)
{
	mobj_t *th;
	angle_t an;
	int32_t dist;
	fixed_t z;
	const fixed_t gsf = (fixed_t)6;
	fixed_t speed;

	I_Assert(source != NULL);
	I_Assert(dest != NULL);
	if (source->type == MT_JETTGUNNER)
	{
		if (source->eflags & MFE_VERTICALFLIP)
			z = source->z + source->height - FixedMul(4*FRACUNIT, source->scale);
		else
			z = source->z + FixedMul(4*FRACUNIT, source->scale);
	}
	else
		z = source->z + source->height/2;

	if (source->eflags & MFE_VERTICALFLIP)
		z -= FixedMul(mobjinfo[type].height, source->scale);

	th = P_SpawnMobj(source->x, source->y, z, type);

	if (source->eflags & MFE_VERTICALFLIP)
		th->flags2 |= MF2_OBJECTFLIP;

	th->destscale = source->scale;
	P_SetScale(th, source->scale);

	if (source->type == MT_METALSONIC_BATTLE && source->health < 4)
		speed = FixedMul(FixedMul(th->info->speed, 3*FRACUNIT/2), th->scale);
	else
		speed = FixedMul(th->info->speed, th->scale);

	if (speed == 0)
	{
		CONS_Debug(DBG_GAMELOGIC, "P_SpawnMissile - projectile has 0 speed! (mobj type %d)\n", type);
		speed = FixedMul(mobjinfo[MT_TURRETLASER].speed, th->scale);
	}

	if (th->info->seesound)
		S_StartSound(source, th->info->seesound);

	P_SetTarget(&th->target, source); // where it came from

	if (type == MT_TURRETLASER || type == MT_ENERGYBALL) // More accurate!
		an = R_PointToAngle2(source->x, source->y,
			dest->x + (dest->momx*gsf),
			dest->y + (dest->momy*gsf));
	else
		an = R_PointToAngle2(source->x, source->y, dest->x, dest->y);

	th->angle = an;
	an >>= ANGLETOFINESHIFT;
	th->momx = FixedMul(speed, FINECOSINE(an));
	th->momy = FixedMul(speed, FINESINE(an));

	if (type == MT_TURRETLASER || type == MT_ENERGYBALL) // More accurate!
		dist = P_AproxDistance(dest->x+(dest->momx*gsf) - source->x, dest->y+(dest->momy*gsf) - source->y);
	else
		dist = P_AproxDistance(dest->x - source->x, dest->y - source->y);

	dist = dist / speed;

	if (dist < 1)
		dist = 1;

	if (type == MT_TURRETLASER || type == MT_ENERGYBALL) // More accurate!
		th->momz = (dest->z + (dest->momz*gsf) - z) / dist;
	else
		th->momz = (dest->z - z) / dist;

	if (th->flags & MF_MISSILE)
		dist = P_CheckMissileSpawn(th);
	else
		dist = 1;
	return dist ? th : NULL;
}

//
// P_ColorTeamMissile
// Colors a player's ring based on their team
//
void P_ColorTeamMissile(mobj_t *missile, player_t *source)
{
	if (G_GametypeHasTeams())
	{
		if (source->ctfteam == 2)
			missile->color = skincolor_bluering;
		else if (source->ctfteam == 1)
			missile->color = skincolor_redring;
	}
	/*
	else
		missile->color = player->mo->color; //copy color
	*/
}

//
// P_SPMAngle
// Fires missile at angle from what is presumably a player
//
mobj_t *P_SPMAngle(mobj_t *source, mobjtype_t type, angle_t angle, uint8_t allowaim, uint32_t flags2)
{
	mobj_t *th;
	angle_t an;
	fixed_t x, y, z, slope = 0, speed;

	// angle at which you fire, is player angle
	an = angle;

	if (source->player && allowaim) // aiming allowed?
		slope = AIMINGTOSLOPE(source->player->aiming);

	x = source->x;
	y = source->y;

	if (source->eflags & MFE_VERTICALFLIP)
		z = source->z + 2*source->height/3 - FixedMul(mobjinfo[type].height, source->scale);
	else
		z = source->z + source->height/3;

	th = P_SpawnMobj(x, y, z, type);

	if (source->eflags & MFE_VERTICALFLIP)
		th->flags2 |= MF2_OBJECTFLIP;

	th->destscale = source->scale;
	P_SetScale(th, source->scale);

	th->flags2 |= flags2;

	// The rail ring has no unique thrown object, so we must do this.
	if (th->info->seesound && !(th->flags2 & MF2_RAILRING))
		S_StartSound(source, th->info->seesound);

	P_SetTarget(&th->target, source);

	speed = th->info->speed;

	th->angle = an;
	th->momx = FixedMul(speed, FINECOSINE(an>>ANGLETOFINESHIFT));
	th->momy = FixedMul(speed, FINESINE(an>>ANGLETOFINESHIFT));

	if (source->player && allowaim)
	{
		th->momx = FixedMul(th->momx,FINECOSINE(source->player->aiming>>ANGLETOFINESHIFT));
		th->momy = FixedMul(th->momy,FINECOSINE(source->player->aiming>>ANGLETOFINESHIFT));
	}

	th->momz = FixedMul(speed, slope);

	//scaling done here so it doesn't clutter up the code above
	th->momx = FixedMul(th->momx, th->scale);
	th->momy = FixedMul(th->momy, th->scale);
	th->momz = FixedMul(th->momz, th->scale);

	slope = P_CheckMissileSpawn(th);

	return slope ? th : NULL;
}

//
// P_FlashPal
// Flashes a player's palette.  ARMAGEDDON BLASTS!
//
void P_FlashPal(player_t *pl, uint16_t type, uint16_t duration)
{
	if (!pl)
		return;
	pl->flashcount = duration;
	pl->flashpal = type;
}

//
// P_ScaleFromMap
// Scales a number relative to the mapobjectscale.
//
fixed_t P_ScaleFromMap(fixed_t n, fixed_t scale)
{
	return FixedMul(n, FixedDiv(scale, mapobjectscale));
}

//
// P_SpawnMobjFromMobjUnscaled
// Spawns an object with offsets relative to the position of another object.
// Scale, gravity flip, etc. is taken into account automatically.
//
mobj_t *P_SpawnMobjFromMobjUnscaled(mobj_t *mobj, fixed_t xofs, fixed_t yofs, fixed_t zofs, mobjtype_t type)
{
	mobj_t *newmobj;

	newmobj = P_SpawnMobj(mobj->x + xofs, mobj->y + yofs, mobj->z + zofs, type);
	if (!newmobj)
		return NULL;

	newmobj->destscale = P_ScaleFromMap(mobj->destscale, newmobj->destscale);
	P_SetScale(newmobj, P_ScaleFromMap(mobj->scale, newmobj->scale));

	if (mobj->eflags & MFE_VERTICALFLIP)
	{
		newmobj->eflags |= MFE_VERTICALFLIP;
		newmobj->flags2 |= MF2_OBJECTFLIP;
		newmobj->z = mobj->z + mobj->height - zofs - newmobj->height;

		newmobj->old_z = mobj->old_z + mobj->height - zofs - newmobj->height;
		newmobj->old_z2 = mobj->old_z2 + mobj->height - zofs - newmobj->height;
	}
	else
	{
		newmobj->old_z = mobj->old_z + zofs;
		newmobj->old_z2 = mobj->old_z2 + zofs;
	}

	newmobj->old_x2 = mobj->old_x2 + xofs;
	newmobj->old_y2 = mobj->old_y2 + yofs;
	newmobj->old_x = mobj->old_x + xofs;
	newmobj->old_y = mobj->old_y + yofs;

	// This angle hack is needed for Lua scripts that set the angle after
	// spawning, to avoid erroneous interpolation.
	if (mobj->player)
	{
		newmobj->old_angle2 = mobj->player->old_drawangle2;
		newmobj->old_angle = mobj->player->old_drawangle;
	}
	else
	{
		newmobj->old_angle2 = mobj->old_angle2;
		newmobj->old_angle = mobj->old_angle;
	}

	newmobj->old_scale2 = mobj->old_scale2;
	newmobj->old_scale = mobj->old_scale;
	newmobj->old_spritexscale = mobj->old_spritexscale;
	newmobj->old_spriteyscale = mobj->old_spriteyscale;
	newmobj->old_spritexoffset = mobj->old_spritexoffset;
	newmobj->old_spriteyoffset = mobj->old_spriteyoffset;

	return newmobj;
}

//
// P_SpawnMobjFromMobj
// Spawns an object with offsets relative to the position of another object.
// Scale, gravity flip, etc. is taken into account automatically.
//
mobj_t *P_SpawnMobjFromMobj(mobj_t *mobj, fixed_t xofs, fixed_t yofs, fixed_t zofs, mobjtype_t type)
{
	mobj_t *newmobj;

	xofs = FixedMul(xofs, mobj->scale);
	yofs = FixedMul(yofs, mobj->scale);
	zofs = FixedMul(zofs, mobj->scale);

	newmobj = P_SpawnMobj(mobj->x + xofs, mobj->y + yofs, mobj->z + zofs, type);
	if (!newmobj)
		return NULL;

	newmobj->destscale = P_ScaleFromMap(mobj->destscale, newmobj->destscale);
	P_SetScale(newmobj, P_ScaleFromMap(mobj->scale, newmobj->scale));

	if (mobj->eflags & MFE_VERTICALFLIP)
	{
		newmobj->eflags |= MFE_VERTICALFLIP;
		newmobj->flags2 |= MF2_OBJECTFLIP;
		newmobj->z = mobj->z + mobj->height - zofs - newmobj->height;

		newmobj->old_z = mobj->old_z + mobj->height - zofs - newmobj->height;
		newmobj->old_z2 = mobj->old_z2 + mobj->height - zofs - newmobj->height;
	}
	else
	{
		newmobj->old_z = mobj->old_z + zofs;
		newmobj->old_z2 = mobj->old_z2 + zofs;
	}

	newmobj->old_x2 = mobj->old_x2 + xofs;
	newmobj->old_y2 = mobj->old_y2 + yofs;
	newmobj->old_x = mobj->old_x + xofs;
	newmobj->old_y = mobj->old_y + yofs;

	// This angle hack is needed for Lua scripts that set the angle after
	// spawning, to avoid erroneous interpolation.
	if (mobj->player)
	{
		newmobj->old_angle2 = mobj->player->old_drawangle2;
		newmobj->old_angle = mobj->player->old_drawangle;
	}
	else
	{
		newmobj->old_angle2 = mobj->old_angle2;
		newmobj->old_angle = mobj->old_angle;
	}

	newmobj->old_scale2 = mobj->old_scale2;
	newmobj->old_scale = mobj->old_scale;
	newmobj->old_spritexscale = mobj->old_spritexscale;
	newmobj->old_spriteyscale = mobj->old_spriteyscale;
	newmobj->old_spritexoffset = mobj->old_spritexoffset;
	newmobj->old_spriteyoffset = mobj->old_spriteyoffset;

	return newmobj;
}

//
// P_GetMobjHead & P_GetMobjFeet
// Returns the top and bottom of an object, follows appearance, not physics,
// in reverse gravity.
//

fixed_t P_GetMobjHead(const mobj_t *mobj)
{
	return P_IsObjectFlipped(mobj) ? mobj->z : mobj->z + mobj->height;
}

fixed_t P_GetMobjFeet(const mobj_t *mobj)
{
	/*
	          |     |
	          |     |
	/--\------/     |
	|               |
	-----------------
	*/

	return P_IsObjectFlipped(mobj) ? mobj->z + mobj->height : mobj->z;
}

//
// P_GetMobjGround
// Returns the object's floor, or ceiling in reverse gravity.
//
fixed_t P_GetMobjGround(const mobj_t *mobj)
{
	return P_IsObjectFlipped(mobj) ? mobj->ceilingz : mobj->floorz;
}

//
// P_GetMobjZMovement
// Returns the Z momentum of the object, accounting for slopes if the object is grounded
//
fixed_t P_GetMobjZMovement(mobj_t *mo)
{
	pslope_t *slope = mo->standingslope;
	angle_t angDiff;
	fixed_t speed;

	if (!P_IsObjectOnGround(mo))
		return mo->momz;

	if (!slope)
		return 0;

	angDiff = R_PointToAngle2(0, 0, mo->momx, mo->momy) - slope->xydirection;
	speed = FixedHypot(mo->momx, mo->momy);

	return P_ReturnThrustY(mo, slope->zangle, P_ReturnThrustX(mo, angDiff, speed));
}

kartvoice_t *P_GetMobjVoice(const mobj_t *mo)
{
	if (P_MobjWasRemoved(mo))
	{
		// Nonexistent or NULL object.
		return NULL;
	}

	if (mo->voice)
	{
		return mo->voice;
	}

#ifdef PARANOIA
	// Scream at the idiot that let this happen :^)
	CONS_Printf(
		"PARANOIA/P_GetMobjVoice: %p MT_%s passed to function with NULL voice\n",
		(const void *)mo,
		P_MobjTypeName(mo)
	);
#endif

	return NULL;
}

//
// Thing IDs / tags
//
// TODO: Replace this system with taglist_t instead.
// The issue is that those require a static numbered ID
// to determine which struct it belongs to, which mobjs
// don't really have.
//

#define TID_HASH_CHAINS (131)
static mobj_t *TID_Hash[TID_HASH_CHAINS];

//
// P_InitTIDHash
// Initializes mobj tag hash array
//
void P_InitTIDHash(void)
{
	memset(TID_Hash, 0, TID_HASH_CHAINS * sizeof(mobj_t *));
}

//
// P_AddThingTID
// Adds a mobj to the hash array
//
void P_AddThingTID(mobj_t *mo)
{
	I_Assert(!P_MobjWasRemoved(mo));

	if (mo->tid <= 0)
	{
		// 0 is no TID, and negative
		// values are reserved.
		mo->tid = 0;
		mo->tid_next = NULL;
		mo->tid_prev = NULL;
		return;
	}

	// Insert at the head of this chain
	int32_t key = mo->tid % TID_HASH_CHAINS;

	mo->tid_next = TID_Hash[key];
	mo->tid_prev = &TID_Hash[key];
	TID_Hash[key] = mo;

	// Connect to any existing things in chain
	if (mo->tid_next != NULL)
	{
		mo->tid_next->tid_prev = &(mo->tid_next);
	}
}

//
// P_RemoveThingTID
// Removes a mobj from the hash array
//
void P_RemoveThingTID(mobj_t *mo)
{
	if (mo->tid != 0 && mo->tid_prev != NULL)
	{
		// Fix the gap this would leave.
		*(mo->tid_prev) = mo->tid_next;

		if (mo->tid_next != NULL)
		{
			mo->tid_next->tid_prev = mo->tid_prev;
		}

		mo->tid_prev = NULL;
		mo->tid_next = NULL;
	}

	// Remove TID.
	mo->tid = 0;
}

//
// P_SetThingTID
// Changes a mobj's TID
//
void P_SetThingTID(mobj_t *mo, mtag_t tid)
{
	P_RemoveThingTID(mo);

	if (P_MobjWasRemoved(mo))
	{
		// Do not assign if it is going to be removed.
		return;
	}

	mo->tid = tid;
	P_AddThingTID(mo);
}

//
// P_FindMobjFromTID
// Mobj tag search function.
//
mobj_t *P_FindMobjFromTID(mtag_t tid, mobj_t *i, mobj_t *activator)
{
	if (tid <= 0)
	{
		if (tid == 0)
		{
			// 0 grabs the activator, if applicable,
			// for some ACS functions.

			if (i != NULL)
			{
				// Don't do more than once.
				return NULL;
			}

			return activator;
		}
		else if (tid >= -MAXPLAYERS)
		{
			// -1 to -MAXPLAYERS returns an arbritrary player's object.
			int32_t playerID = -tid - 1;
			player_t *player = &players[ playerID ];

			if (playeringame[playerID] == true && player->spectator == false)
			{
				return player->mo;
			}

			return NULL;
		}

		// Invalid input, return NULL.
		return NULL;
	}

	i = (i != NULL) ? i->tid_next : TID_Hash[tid % TID_HASH_CHAINS];

	while (i != NULL && i->tid != tid)
	{
		i = i->tid_next;
	}

	return i;
}

void P_DeleteMobjStringArgs(mobj_t *mobj)
{
	size_t i = SIZE_MAX;

	for (i = 0; i < NUM_MAPTHING_STRINGARGS; i++)
	{
		Z_Free(mobj->stringargs[i]);
		mobj->stringargs[i] = NULL;
	}

	for (i = 0; i < NUM_SCRIPT_STRINGARGS; i++)
	{
		Z_Free(mobj->script_stringargs[i]);
		mobj->script_stringargs[i] = NULL;
	}
}
