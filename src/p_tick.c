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
/// \file  p_tick.c
/// \brief Archiving: SaveGame I/O, Thinker, Ticker

#include "d_think.h"
#include "doomstat.h"
#include "g_game.h"
#include "g_input.h"
#include "g_state.h"
#include "m_menu.h"
#include "p_local.h"
#include "z_zone.h"
#include "s_sound.h"
#include "st_stuff.h"
#include "p_polyobj.h"
#include "m_random.h"
#include "lua_script.h"
#include "lua_hook.h"
#include "k_stats.h"
#include "m_perfstats.h"
#include "i_system.h" // I_GetPreciseTime
#include "i_video.h"
#include "r_fps.h"
#include "r_main.h"
#include "f_finale.h"

// Object place
#include "m_cheat.h"

// SRB2Kart
#include "k_kart.h"
#include "k_battle.h"
#include "k_boss.h"
#include "k_waypoint.h"
#include "k_director.h"
#include "acs/interface.h"
#include "k_bot.h" // K_BotTicker
#include "k_items.h" // ItemBGone
#include "k_specialstage.h"
#include "k_grandprix.h"

#include "radio_finish.h"

#include "i_gamepad.h"

#include <tracy/tracy/TracyC.h>

#ifdef PARANOIA
#include "deh_tables.h" // MOBJTYPE_LIST
#endif

tic_t leveltime;
boolean thinkersCompleted;

UINT32 thinker_era = 0;

INT32 P_AltFlip(INT32 n, tic_t tics)
{
	return leveltime % (2 * tics) < tics ? n : -(n);
}

//
// THINKERS
// All thinkers should be allocated by Z_Calloc
// so they can be operated on uniformly.
// The actual structures will vary in size,
// but the first element must be thinker_t.
//

// The entries will behave like both the head and tail of the lists.
thinker_t thlist[NUM_THINKERLISTS];

void Command_Numthinkers_f(void)
{
	INT32 num;
	INT32 count = 0;
	actionf_p1 action;
	thinker_t *think;
	thinklistnum_t start = 0;
	thinklistnum_t end = NUM_THINKERLISTS - 1;
	thinklistnum_t i;

	if (gamestate != GS_LEVEL)
	{
		CONS_Printf(M_GetText("You must be in a level to use this.\n"));
		return;
	}

	if (COM_Argc() < 2)
	{
		CONS_Printf(M_GetText("numthinkers <#>: Count number of thinkers\n"));
		CONS_Printf(
			"\t1: P_MobjThinker\n"
			"\t2: P_NullPrecipThinker\n"
			"\t3: T_Friction\n"
			"\t4: T_Pusher\n"
			"\t5: P_RemoveThinkerDelayed\n");
		return;
	}

	num = atoi(COM_Argv(1));

	switch (num)
	{
		case 1:
			start = end = THINK_MOBJ;
			action = (actionf_p1)P_MobjThinker;
			CONS_Printf(M_GetText("Number of %s: "), "P_MobjThinker");
			break;
		case 2:
			start = end = THINK_PRECIP;
			action = (actionf_p1)P_NullPrecipThinker;
			CONS_Printf(M_GetText("Number of %s: "), "P_NullPrecipThinker");
			break;
		case 3:
			start = end = THINK_MAIN;
			action = (actionf_p1)T_Friction;
			CONS_Printf(M_GetText("Number of %s: "), "T_Friction");
			break;
		case 4:
			start = end = THINK_MAIN;
			action = (actionf_p1)T_Pusher;
			CONS_Printf(M_GetText("Number of %s: "), "T_Pusher");
			break;
		case 5:
			action = (actionf_p1)P_RemoveThinkerDelayed;
			CONS_Printf(M_GetText("Number of %s: "), "P_RemoveThinkerDelayed");
			break;
		default:
			CONS_Printf(M_GetText("That is not a valid number.\n"));
			return;
	}

	for (i = start; i <= end; i++)
	{
		for (think = thlist[i].next; think != &thlist[i]; think = think->next)
		{
			if (think->function != action)
				continue;

			count++;
		}
	}

	CONS_Printf("%d\n", count);
}

void Command_CountMobjs_f(void)
{
	thinker_t *th;
	mobjtype_t i;
	INT32 count;

	if (gamestate != GS_LEVEL)
	{
		CONS_Printf(M_GetText("You must be in a level to use this.\n"));
		return;
	}

	if (COM_Argc() >= 2)
	{
		size_t j;
		for (j = 1; j < COM_Argc(); j++)
		{
			i = atoi(COM_Argv(j));
			if (i >= NUMMOBJTYPES)
			{
				CONS_Printf(M_GetText("Object number %d out of range (max %d).\n"), i, NUMMOBJTYPES-1);
				continue;
			}

			count = 0;

			for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
			{
				if (th->function == (actionf_p1)P_RemoveThinkerDelayed)
					continue;

				if (((mobj_t *)th)->type == i)
					count++;
			}

			CONS_Printf(M_GetText("There are %d objects of type %d currently in the level.\n"), count, i);
		}
		return;
	}

	CONS_Printf(M_GetText("Count of active objects in level:\n"));

	for (i = 0; i < NUMMOBJTYPES; i++)
	{
		count = 0;

		for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
		{
			if (th->function == (actionf_p1)P_RemoveThinkerDelayed)
				continue;

			if (((mobj_t *)th)->type == i)
				count++;
		}

		if (count > 0) // Don't bother displaying if there are none of this type!
			CONS_Printf(" * %d: %d\n", i, count);
	}
}

//
// P_InitThinkers
//
void P_InitThinkers(void)
{
	UINT8 i;

	P_InvalidateThinkersWithoutInit();

	for (i = 0; i < NUM_THINKERLISTS; i++)
	{
		thlist[i].prev = thlist[i].next = &thlist[i];
	}

	boss3cap = NULL;
	waypointcap = NULL;
	kitemcap = NULL;
	misccap = NULL;

	iquehead = iquetail = 0;

	for (i = 0; i <= 15; i++)
	{
		skyboxcenterpnts[i] = skyboxviewpnts[i] = NULL;
	}

}

//
// P_InvalidateThinkersWithoutInit
//

void P_InvalidateThinkersWithoutInit(void)
{
	thinker_era++;
}

// Adds a new thinker at the end of the list.
void P_AddThinker(const thinklistnum_t n, thinker_t *thinker)
{
#ifdef PARANOIA
	I_Assert(n < NUM_THINKERLISTS);
#endif

	thlist[n].prev->next = thinker;
	thinker->next = &thlist[n];
	thinker->prev = thlist[n].prev;
	thlist[n].prev = thinker;

	thinker->references = 0;    // killough 11/98: init reference counter to 0

#ifdef PARANOIA
	thinker->debug_mobjtype = MT_NULL;
#endif
}

#ifdef PARANOIA
const char *P_MobjTypeName(const mobj_t *mobj)
{
	actionf_p1 p1 = mobj->thinker.function;

	if (p1 == (actionf_p1)P_MobjThinker)
	{
		return DEH_MobjtypeName(mobj->type);
	}
	else if (p1 == (actionf_p1)P_RemoveThinkerDelayed)
	{
		if (mobj->thinker.debug_mobjtype != MT_NULL)
		{
			return DEH_MobjtypeName(mobj->thinker.debug_mobjtype);
		}
	}

	return "<Not a mobj>";
}

static const char *MobjThinkerName(const mobj_t *mobj)
{
	actionf_p1 p1 = mobj->thinker.function;

	if (p1 == (actionf_p1)P_MobjThinker)
	{
		return "P_MobjThinker";
	}
	else if (p1 == (actionf_p1)P_RemoveThinkerDelayed)
	{
		return "P_RemoveThinkerDelayed";
	}

	return "<Unknown Thinker>";
}
#endif

//
// killough 11/98:
//
// Make currentthinker external, so that P_RemoveThinkerDelayed
// can adjust currentthinker when thinkers self-remove.

static thinker_t *currentthinker;

//
// P_RemoveThinkerDelayed()
//
// Called automatically as part of the thinker loop in P_RunThinkers(),
// on nodes which are pending deletion.
//
// If this thinker has no more pointers referencing it indirectly,
// remove it, and set currentthinker to one node preceeding it, so
// that the next step in P_RunThinkers() will get its successor.
//
void P_RemoveThinkerDelayed(thinker_t *thinker)
{
	if (thinker->references != 0)
	{
#ifdef PARANOIA
		// Removed mobjs can be the target of another mobj. In
		// that case, the other mobj will manage its reference
		// to the removed mobj in P_MobjThinker. However, if
		// the removed mobj is removed after the other object
		// thinks, the reference management is delayed by one
		// tic (or two?)
		const UINT8 delay = 2;

		if (thinker->debug_time > leveltime)
		{
			thinker->debug_time = leveltime + delay + 1; // do not print errors again
		}
		else if ((thinker->debug_time + delay) <= leveltime)
		{
			CONS_Printf(
					"PARANOIA/P_RemoveThinkerDelayed: %p MT_%s references=%d\n",
					(void*)thinker,
					P_MobjTypeName((mobj_t*)thinker),
					thinker->references
			);

			thinker->debug_time = leveltime + delay + 1; // do not print this error again
		}
#endif
		return;
	}

	R_DestroyLevelInterpolators(thinker);

	/* Note that currentthinker is guaranteed to point to us,
	* and since we're freeing our memory, we had better change that. So
	* point it to thinker->prev, so the iterator will correctly move on to
	* thinker->prev->next = thinker->next */
	currentthinker = thinker->prev;

	/* Remove from main thinker list */
	P_UnlinkThinker(thinker);
}

//
// P_UnlinkThinker()
//
// Actually removes thinker from the list and frees its memory.
//
void P_UnlinkThinker(thinker_t *thinker)
{
	thinker_t *next = thinker->next;

	I_Assert(thinker->references == 0);

	(next->prev = thinker->prev)->next = next;
	if (thinker->alloctype == TAT_LEVELPOOL)
	{
		Z_LevelPoolFree(thinker, thinker->size);
	}
	else
	{
		Z_Free(thinker);
	}
}

//
// P_RemoveThinker
//
// Deallocation is lazy -- it will not actually be freed
// until its thinking turn comes up.
//
// killough 4/25/98:
//
// Instead of marking the function with -1 value cast to a function pointer,
// set the function to P_RemoveThinkerDelayed(), so that later, it will be
// removed automatically as part of the thinker process.
//
void P_RemoveThinker(thinker_t *thinker)
{
	LUA_InvalidateUserdata(thinker);
	thinker->function = (actionf_p1)P_RemoveThinkerDelayed;
}

/*
 * P_SetTarget
 *
 * This function is used to keep track of pointer references to mobj thinkers.
 * In Doom, objects such as lost souls could sometimes be removed despite
 * their still being referenced. In Boom, 'target' mobj fields were tested
 * during each gametic, and any objects pointed to by them would be prevented
 * from being removed. But this was incomplete, and was slow (every mobj was
 * checked during every gametic). Now, we keep a count of the number of
 * references, and delay removal until the count is 0.
 */

mobj_t *P_SetTarget2(mobj_t **mop, mobj_t *targ
#ifdef PARANOIA
		, const char *source_file, int source_line
#endif
)
{
	if (*mop) // If there was a target already, decrease its refcount
	{
		(*mop)->thinker.references--;

#ifdef PARANOIA
		if ((*mop)->thinker.references < 0)
		{
			CONS_Printf(
					"PARANOIA/P_SetTarget: %p MT_%s %s references=%d, references go negative! (%s:%d)\n",
					(void*)*mop,
					P_MobjTypeName(*mop),
					MobjThinkerName(*mop),
					(*mop)->thinker.references,
					source_file,
					source_line
			);
		}

		(*mop)->thinker.debug_time = leveltime;
#endif
	}

	if (targ != NULL) // Set new target and if non-NULL, increase its counter
	{
		targ->thinker.references++;

#ifdef PARANOIA
		targ->thinker.debug_time = leveltime;
#endif
	}

	*mop = targ;

	return targ;
}

//
// P_RunThinkers
//
// killough 4/25/98:
//
// Fix deallocator to stop using "next" pointer after node has been freed
// (a Doom bug).
//
// Process each thinker. For thinkers which are marked deleted, we must
// load the "next" pointer prior to freeing the node. In Doom, the "next"
// pointer was loaded AFTER the thinker was freed, which could have caused
// crashes.
//
// But if we are not deleting the thinker, we should reload the "next"
// pointer after calling the function, in case additional thinkers are
// added at the end of the list.
//
// killough 11/98:
//
// Rewritten to delete nodes implicitly, by making currentthinker
// external and using P_RemoveThinkerDelayed() implicitly.
//
static void P_RunThinkers(void)
{
	TracyCZone(__zone, true);
	size_t i;

	for (i = 0; i < NUM_ACTIVETHINKERLISTS; i++)
	{
		ps_thlist_times[i] = I_GetPreciseTime();
		for (currentthinker = thlist[i].next; currentthinker != &thlist[i]; currentthinker = currentthinker->next)
		{
#ifdef PARANOIA
			I_Assert(currentthinker->function != NULL);
#endif
			currentthinker->function(currentthinker);
		}
		ps_thlist_times[i] = I_GetPreciseTime() - ps_thlist_times[i];
	}

	if (gametypes[gametype]->rules & GTR_PAPERITEMS)
		K_RunPaperItemSpawners();

	ps_acs_time = I_GetPreciseTime();
	ACS_Tick();
	ps_acs_time = I_GetPreciseTime() - ps_acs_time;
	
	TracyCZoneEnd(__zone);
}

//
// P_DoAutobalanceTeams()
//
// Determine if the teams are unbalanced, and if so, move a player to the other team.
//
static void P_DoAutobalanceTeams(void)
{
	changeteam_union NetPacket;
	UINT16 usvalue;
	INT32 i=0;
	INT32 red=0, blue=0;
	INT32 redarray[MAXPLAYERS], bluearray[MAXPLAYERS];
	//INT32 redflagcarrier = 0, blueflagcarrier = 0;
	INT32 totalred = 0, totalblue = 0;

	NetPacket.value.l = NetPacket.value.b = 0;
	memset(redarray, 0, sizeof(redarray));
	memset(bluearray, 0, sizeof(bluearray));

	// Only do it if we have enough room in the net buffer to send it.
	// Otherwise, come back next time and try again.
	if (sizeof(usvalue) > GetFreeXCmdSize(0))
		return;

	//We have to store the players in an array with the rest of their team.
	//We can then pick a random player to be forced to change teams.
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (playeringame[i] && players[i].ctfteam)
		{
			if (players[i].ctfteam == 1)
			{
				//if (!players[i].gotflag)
				{
					redarray[red] = i; //store the player's node.
					red++;
				}
				/*else
					redflagcarrier++;*/
			}
			else
			{
				//if (!players[i].gotflag)
				{
					bluearray[blue] = i; //store the player's node.
					blue++;
				}
				/*else
					blueflagcarrier++;*/
			}
		}
	}

	totalred = red;// + redflagcarrier;
	totalblue = blue;// + blueflagcarrier;

	if ((abs(totalred - totalblue) > max(1, (totalred + totalblue) / 8)))
	{
		if (totalred > totalblue)
		{
			i = M_RandomKey(red);
			NetPacket.packet.newteam = 2;
			NetPacket.packet.playernum = redarray[i];
			NetPacket.packet.verification = true;
			NetPacket.packet.autobalance = true;

			usvalue  = SHORT(NetPacket.value.l|NetPacket.value.b);
			SendNetXCmd(XD_TEAMCHANGE, &usvalue, sizeof(usvalue));
		}
		else //if (totalblue > totalred)
		{
			i = M_RandomKey(blue);
			NetPacket.packet.newteam = 1;
			NetPacket.packet.playernum = bluearray[i];
			NetPacket.packet.verification = true;
			NetPacket.packet.autobalance = true;

			usvalue  = SHORT(NetPacket.value.l|NetPacket.value.b);
			SendNetXCmd(XD_TEAMCHANGE, &usvalue, sizeof(usvalue));
		}
	}
}

//
// P_DoTeamscrambling()
//
// If a team scramble has been started, scramble one person from the
// pre-made scramble array. Said array is created in TeamScramble_OnChange()
//
void P_DoTeamscrambling(void)
{
	changeteam_union NetPacket;
	UINT16 usvalue;
	NetPacket.value.l = NetPacket.value.b = 0;

	// Only do it if we have enough room in the net buffer to send it.
	// Otherwise, come back next time and try again.
	if (sizeof(usvalue) > GetFreeXCmdSize(0))
		return;

	if (scramblecount < scrambletotal)
	{
		if (players[scrambleplayers[scramblecount]].ctfteam != scrambleteams[scramblecount])
		{
			NetPacket.packet.newteam = scrambleteams[scramblecount];
			NetPacket.packet.playernum = scrambleplayers[scramblecount];
			NetPacket.packet.verification = true;
			NetPacket.packet.scrambled = true;

			usvalue = SHORT(NetPacket.value.l|NetPacket.value.b);
			SendNetXCmd(XD_TEAMCHANGE, &usvalue, sizeof(usvalue));
		}

		scramblecount++; //Increment, and get to the next player when we come back here next time.
	}
	else
		CV_SetValue(&cv_teamscramble, 0);
}

static inline void P_DoTeamStuff(void)
{
	// Automatic team balance for CTF and team match
	if (leveltime % (TICRATE * 5) == 0) //only check once per five seconds for the sake of CPU conservation.
	{
		// Do not attempt to autobalance and scramble teams at the same time.
		// Only the server should execute this. No verified admins, please.
		if ((cv_autobalance.value && !cv_teamscramble.value) && cv_allowteamchange.value && server)
			P_DoAutobalanceTeams();
	}

	// Team scramble code for team match and CTF.
	if ((leveltime % (TICRATE/7)) == 0)
	{
		// If we run out of time in the level, the beauty is that
		// the Y_Ticker() team scramble code will pick it up.
		if (cv_teamscramble.value && server)
			P_DoTeamscrambling();
	}
}

static inline fixed_t WrapFixed(fixed_t value, fixed_t min, fixed_t max)
{
	return ((value - min) % (max - min)) + min;
}
static fixed_t FixedAngleDifference(fixed_t angle1, fixed_t angle2 )
{
    fixed_t diff = ( angle2 - angle1 + (180<<FRACBITS) ) % (360<<FRACBITS) - (180<<FRACBITS);
    return diff < -(180<<FRACBITS) ? diff + (360<<FRACBITS) : diff;
}

static void P_HandleCameraFreelookYaw(UINT8 pnum, player_t *player, camera_t *cam)
{
	if (menustack[0])
		return;

	if (P_IsDisplayPlayer(player))
	{
		fixed_t angle = 0;

		if (cv_freelookrequiresbutton[pnum].value && (!cam->freelook_held))
		{
			angle = -cam->freelook_yaw;
		}
		else
		{
			switch (cv_freelookstyle[pnum].value)
			{
				case FREELOOKSTYLE_FLICK:
					{
						INT32 flickstick_xaxis = G_PlayerInputAnalog(pnum, gc_flickstick_right, false, DEADZONE_NONE) - G_PlayerInputAnalog(pnum, gc_flickstick_left, false, DEADZONE_NONE);
						INT32 flickstick_yaxis = G_PlayerInputAnalog(pnum, gc_flickstick_back, false, DEADZONE_NONE) - G_PlayerInputAnalog(pnum, gc_flickstick_forward, false, DEADZONE_NONE);
						fixed_t tilt = FixedHypot(abs(flickstick_xaxis) * FRACUNIT, abs(flickstick_yaxis) * FRACUNIT);

						if (abs(tilt) >= ((JOYAXISRANGE * G_GetDeadZoneType(pnum, DEADZONE_2D))))
						{
							double radians = atan2(-flickstick_xaxis, -flickstick_yaxis);
							angle = WrapFixed(FixedAngleDifference(cam->freelook_yaw, FloatToFixed(radians * (180.0 / M_PI))), 0<<FRACBITS, 360<<FRACBITS);
						}
						else
						{
							angle = -cam->freelook_yaw;
						}
					}
					break;
				case FREELOOKSTYLE_ROTATIONAL:
					{
						INT32 flickstick_xaxis = G_PlayerInputAnalog(pnum, gc_flickstick_right, false, DEADZONE_X) - G_PlayerInputAnalog(pnum, gc_flickstick_left, false, DEADZONE_X);
						if (flickstick_xaxis == 0 && 
							(((player->cmd.buttons & (BT_ACCELERATE|BT_BRAKE)) != 0 && !(player->spectator || cam->freecam)) ||
							((player->spectator || cam->freecam) && G_PlayerInputDown(pnum, gc_camreset, false, DEADZONE_BUTTON)))
						)
						{
							angle = -cam->freelook_yaw;
						}
						else
						{
							angle = FixedMul((TICRATE<<FRACBITS)/8, FixedDiv(abs(flickstick_xaxis), JOYAXISRANGE)) * -intsign(flickstick_xaxis);
						}
					}
					break;
				default:
					angle = -cam->freelook_yaw;
					break;
			}
		}
		angle = min(abs(angle), TICRATE<<FRACBITS) * intsign(angle);

		// gyro aiming
		if (G_GetGamepadCanUseTilt(pnum)
			&& (cv_tiltcontrol[pnum].value == TILTCONTROL_CAME || cv_tiltcontrol[pnum].value == TILTCONTROL_ON)
			&& G_PlayerInputDown(pnum, gc_freelook, false, DEADZONE_BUTTON)
		)
		{
			fixed_t deltaseconds = FixedDiv(FRACUNIT, max(cv_timescale.value, FRACUNIT/20))/TICRATE;
			fixed_t yawRelaxFactor = 141*FRACUNIT/100;
			vector3_t gyro = G_GetGamepadCalibratedGyro(pnum);
			vector3_t gravnorm = G_GetGamepadGravity(pnum);
			vector2_t gyroYZ = {gyro.y, gyro.z};
			
			// use world yaw for yaw direction, local combined yaw for magnitude
			FV3_Normalize(&gravnorm);
			fixed_t worldYaw = FixedMul(gyro.y, gravnorm.y) + FixedMul(gyro.z, gravnorm.z); // dot product but just yaw and roll
			
			fixed_t yaw = intsign(worldYaw) * (encoremode ? -1 : 1) *
			min(FixedMul(-abs(worldYaw), yawRelaxFactor), FV2_Magnitude(&gyroYZ));
			
			cam->freelook_yaw_add = (cam->freelook_yaw_add - FixedMul(yaw, deltaseconds)) % (360<<FRACBITS);
		}
		else
		{
			cam->freelook_yaw_add = 0;
		}

		if (cam->freelook_yaw+angle < -180*FRACUNIT)
		{
			cam->freelook_yaw += 360*FRACUNIT;
		}
		else if (cam->freelook_yaw+angle > 180*FRACUNIT)
		{
			cam->freelook_yaw -= 360*FRACUNIT;
		}
		cam->freelook_yaw = (cam->freelook_yaw + angle) % (360<<FRACBITS);
	}
}

void P_RunChaseCameras(void)
{
	UINT8 i;

	for (i = 0; i <= r_splitscreen; i++)
	{
		if (camera[i].chase)
		{
			INT32 forplayer = displayplayers[i];
			player_t *p = &players[forplayer];
			camera_t *cam = &camera[i];

			// client sided
			if (P_IsDisplayPlayer(p) && G_PlayerInputDown(i, gc_freelook, false, DEADZONE_BUTTON))
			{
				// instantly able to move camera
				cam->freelook_held = true;

				// gamecontrol aiming
				cam->freelook_pitch = (45*FixedDiv(p->cmd.throwdir, KART_FULLTURN));
				// gyro aiming
				if (G_GetGamepadCanUseTilt(i) && (cv_tiltcontrol[i].value == TILTCONTROL_CAME || cv_tiltcontrol[i].value == TILTCONTROL_ON))
				{
					fixed_t deltaseconds = FixedDiv(FRACUNIT, max(cv_timescale.value, FRACUNIT/20))/TICRATE;
					vector3_t gyro = G_GetGamepadCalibratedGyro(i);
					cam->freelook_pitch_add -= FixedMul(gyro.x, deltaseconds);
				}
			}
			else
			{
				cam->freelook_held = false;
				cam->freelook_pitch = 0;
				cam->freelook_pitch_add = 0;
			}
			P_HandleCameraFreelookYaw(i, p, cam);

			P_MoveChaseCamera(p, cam, false);
		}
	}
}

//
// P_Ticker
//
void P_Ticker(boolean run)
{
	TracyCZone(__zone, true);

	INT32 i;

	thinkersCompleted = false;

	// Increment jointime and quittime even if paused
	for (i = 0; i < MAXPLAYERS; i++)
		if (playeringame[i])
		{
			players[i].jointime++;
		}

	if (run)
	{
		// Update old view state BEFORE ticking so resetting
		// the old interpolation state from game logic works.
		R_UpdateViewInterpolation();
	}

	if (objectplacing)
	{
		if (OP_FreezeObjectplace())
		{
			P_MapStart();
			R_UpdateMobjInterpolators();
			R_UpdateLevelInterpolators();
			OP_ObjectplaceMovement(&players[0]);
			P_MoveChaseCamera(&players[0], &camera[0], false);
			R_UpdateViewInterpolation();
			P_MapEnd();
			S_SetStackAdjustmentStart();

			TracyCZoneEnd(__zone);
			return;
		}
	}

	// Check for pause or menu up in single player
	if (paused || P_AutoPause())
	{
		if (demo.rewinding && leveltime > 0)
		{
			leveltime = (leveltime-1) & ~3;
			if (timeinmap > 0)
				timeinmap = (timeinmap-1) & ~3;
			G_PreviewRewind(leveltime);
		}
		else
			P_RunChaseCameras();	// special case: allow freecam to MOVE during pause!
		
			
		TracyCZoneEnd(__zone);
		return;
	}

	if (!S_MusicPaused())
		S_AdjustMusicStackTics();

	P_MapStart();

	if (run)
	{
		R_UpdateMobjInterpolators();

		if (demo.recording)
		{
			G_WriteDemoExtraData();
			for (i = 0; i < MAXPLAYERS; i++)
				if (playeringame[i])
					G_WriteDemoTiccmd(&players[i].cmd, i);
		}
		if (demo.playback)
		{
			if (G_CompatLevel(0x0001) && !multiplayer)
				G_ReadDemoTiccmd(&players[0].cmd, 0);
			else
			{
				G_ReadDemoExtraData();
				for (i = 0; i < MAXPLAYERS; i++)
					if (playeringame[i])
						G_ReadDemoTiccmd(&players[i].cmd, i);
			}
		}

		ps_lua_mobjhooks = 0;
		ps_checkposition_calls = 0;

		{
			TracyCZoneN(__zone, "LUA_PreThinkFrame", true);
			LUA_HOOK(PreThinkFrame);
			TracyCZoneEnd(__zone);
		}

		ps_playerthink_time = I_GetPreciseTime();

		K_UpdateClusterPoints();
		K_UpdateAllPlayerPositions();

		// OK! Now that we got all of that sorted, players can think!
		UINT8 p = 0;
		{
			TracyCZoneN(__zone, "PlayerThink", true);
			for (i = 0; i < MAXPLAYERS; i++)
			{
				if (playeringame[i] && players[i].mo && !P_MobjWasRemoved(players[i].mo))
				{
					TracyCMessage(player_names[i], strlen(player_names[i]));
					P_PlayerThink(&players[i]);
					K_KartPlayerHUDUpdate(&players[i]);
					
					if (!players[i].spectator)
					p++;
				}
			}
			TracyCZoneEnd(__zone);
		}

		if (p > (MAXPLAYERS / 2))
		{
			// At the point where players spawn on top of one another, start silently
			// adding a minimum to the antibump timer.
			// This prevents annoying race-start bonks.
			antibumptime = max(p, antibumptime);
		}

		ps_playerthink_time = I_GetPreciseTime() - ps_playerthink_time;
	}

	// Keep track of how long they've been playing!
	K_StatTicker();

	if (run)
	{
		ps_thinkertime = I_GetPreciseTime();
		P_RunThinkers();
		ps_thinkertime = I_GetPreciseTime() - ps_thinkertime;
		thinkersCompleted = true;

		// Run any "after all the other thinkers" stuff
		for (i = 0; i < MAXPLAYERS; i++)
			if (playeringame[i] && players[i].mo && !P_MobjWasRemoved(players[i].mo))
				P_PlayerAfterThink(&players[i]);

		// Apply rumble to local players
		if (!demo.playback)
		{
			G_ControllerRumbleTick();
		}

		// run all the bot tickers
		if (server)
		{
			PS_ResetBotInfo();
			for (i = 0; i < MAXPLAYERS; i++)
			{
				player_t *player = &players[i];
				if (playeringame[i] && player->mo && !P_MobjWasRemoved(player->mo) && K_PlayerUsesBotMovement(player))
				{
					const precise_t t = I_GetPreciseTime();

					K_BotTicker(player);

					ps_bots[i].total = I_GetPreciseTime() - t;
					ps_bots[i].isBot = true;
					ps_botticcmd_time += ps_bots[i].total;
				}
			}
		}

		if (gamestate == GS_LEVEL)
		{
			// Bosses have a punchy start, so no position.
			if (bossinfo.boss == true)
			{
				if (leveltime == 0)
				{
					S_ChangeMusicEx(mapmusname, mapmusflags, true, mapmusposition, 0, 0);
					S_ShowMusicCredit(0, 5*TICRATE, 0);
				}
			}
			// Plays the music after the starting countdown.
			else
			{
				// Change timing of start music based on gametypes[gametype]->rules
				tic_t startingtime = (gametypes[gametype]->rules & GTR_NOCOUNTDOWN) ? introtime : starttime;

				if (!(titlemapinaction == TITLEMAP_RUNNING))
				{
					if (leveltime == (startingtime + (TICRATE/2)))
					{
						// Plays the music after the starting countdown.
						S_ChangeMusicEx(mapmusname, mapmusflags, true, mapmusposition, 0, 0);
						S_ShowMusicCredit(0, 5*TICRATE, 0);
					}
					else if (startingtime != introtime)
					{
						if (!(gametypes[gametype]->rules & GTR_NOCOUNTDOWN))
						{
							if (leveltime == startingtime-(3*TICRATE))
							{
								S_StartSound(NULL, sfx_s3ka7); // 3,
							}
							else if ((leveltime == startingtime-(2*TICRATE)) || (leveltime == starttime-TICRATE))
							{
								S_StartSound(NULL, sfx_s3ka7); // 2, 1,
							}
							else if (leveltime == startingtime)
							{
								S_StartSound(NULL, sfx_s3kad); // GO!
							}
						}

						if (leveltime < startingtime) // SRB2Kart
							S_ChangeMusicInternal(((grandprixinfo.gp == true
							) ? "gpstrt" : (encoremode ? "estart" : "kstart")), false); // yes this will be spammed otherwise encore and some stuff WILL overwrite it
							else if (leveltime == startingtime) // The GO! sound stops the level start ambience
								S_StopMusic();
					}
				}
			}
		}

		ps_lua_thinkframe_time = I_GetPreciseTime();
		LUA_HookThinkFrame();
		ps_lua_thinkframe_time = I_GetPreciseTime() - ps_lua_thinkframe_time;
	}

	// Run shield positioning
	P_RunOverlays();

	P_UpdateSpecials();
	P_RespawnSpecials();
	K_RespawnBattleBoxes();

	// Lightning, rain sounds, etc.
	P_PrecipitationEffects();

	if (run)
	{
		leveltime++;

		if (starttime > introtime && leveltime == starttime)
		{
			ACS_RunRaceStartScript();
		}

		if (timelimitintics > 0 && leveltime == (timelimitintics + starttime + 1))
		{
			ACS_RunOvertimeScript();
		}
	}

	timeinmap++;

	if (G_GametypeHasTeams())
		P_DoTeamStuff();

	if (run)
	{
		if (racecountdown > 1)
			racecountdown--;

		if (exitcountdown > 1)
			exitcountdown--;

		K_UpdateItemCooldown();

		K_BossInfoTicker();

		K_TickSpecialStage();

		if (gametypes[gametype]->rules & GTR_WANTED)
		{
			if (wantedcalcdelay && --wantedcalcdelay <= 0)
				K_CalculateBattleWanted();
		}

		if (bombflashtimer)
			bombflashtimer--;	// Bomb seizure prevention

		if (quake.time)
		{
			fixed_t ir = quake.intensity>>1;
			/// \todo Calculate distance from epicenter if set and modulate the intensity accordingly based on radius.
			quake.x = M_RandomRange(-ir,ir);
			quake.y = M_RandomRange(-ir,ir);
			quake.z = M_RandomRange(-ir,ir);
			--quake.time;
		}
		else
			quake.x = quake.y = quake.z = 0;

		if (metalplayback)
			G_ReadMetalTic(metalplayback);
		if (metalrecording)
			G_WriteMetalTic(players[consoleplayer].mo);

		if (demo.recording)
		{
			G_WriteAllGhostTics();

			if (cv_recordmultiplayerdemos.value && (demo.savemode == DSM_NOTSAVING || demo.savemode == DSM_WILLAUTOSAVE))
				if (demo.savebutton && demo.savebutton + 3*TICRATE < leveltime && G_PlayerInputDown(0, gc_lookback, false, DEADZONE_BUTTON) && !menustack[0])
				{
					demo.savemode = DSM_TITLEENTRY;
					M_OpenVirtualKeyboard(&demo.titlenameinput, demo.titlename, &replaysave);
					M_StartControlPanel();
					M_EnterMenu(MN_TYPINGDUMMY, false, 0);
				}
		}
		else if (demo.playback) // Use Ghost data for consistency checks.
		{
			if (!multiplayer && G_CompatLevel(0x0001))
				G_ConsGhostTic(0);
			else
				G_ConsAllGhostTics();
		}

		if (modeattacking)
			G_GhostTicker();

		if (mapreset > 1
			&& --mapreset <= 1
			&& server) // Remember: server uses it for mapchange, but EVERYONE ticks down for the animation
				D_MapChange(gamemap, gametype, encoremode, true, 0, false, false);

		if (cv_kartdebugwaypoints.value != 0)
		{
			K_DebugWaypointsVisualise();
		}
	}

	if (gamestate == GS_LEVEL)
	{
		// Move the camera during levels.
		K_UpdateDirector();
		P_RunChaseCameras();
	}

	if (run && !levelloading && leveltime && (!dedicated))
	{
		RR_ridersFinishTick();
	}

	if (run)
	{
		LUA_HOOK(PostThinkFrame);

		R_UpdateLevelInterpolators();

		// Hack: ensure newview is assigned every tic.
		// Ensures view interpolation is T-1 to T in poor network conditions
		// We need a better way to assign view state decoupled from game logic
		if (rendermode != render_none)
		{
			for (i = 0; i <= r_splitscreen; i++)
			{
				player_t *player = &players[displayplayers[i]];

				if (!player->mo)
					continue;

				const boolean skybox = (skyboxmo[0] && cv_skybox.value); // True if there's a skybox object and skyboxes are on

				if (skyVisiblePerPlayer[i] && skybox)
				{
					R_SkyboxFrame(i);
				}
				R_SetupFrame(i, skybox);
			}
		}
	}

	P_MapEnd();

	if (demo.playback)
		G_StoreRewindInfo();

	for (i = 0; i < MAXPLAYERS; i++)
	{
		G_CopyTiccmd(&players[i].oldcmd, &players[i].cmd, 1);
	}

//	Z_CheckMemCleanup();
	TracyCZoneEnd(__zone);
}
