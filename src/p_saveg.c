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
/// \file  p_saveg.c
/// \brief Archiving: SaveGame I/O

#include "doomdata.h"
#include "doomdef.h"
#include "byteptr.h"
#include "d_main.h"
#include "doomstat.h"
#include "g_game.h"
#include "m_random.h"
#include "m_misc.h"
#include "p_local.h"
#include "p_mobj.h"
#include "p_setup.h"
#include "p_saveg.h"
#include "r_data.h"
#include "r_fps.h"
#include "r_textures.h"
#include "r_things.h"
#include "r_skins.h"
#include "r_state.h"
#include "w_wad.h"
#include "y_inter.h"
#include "z_zone.h"
#include "r_main.h"
#include "r_sky.h"
#include "p_polyobj.h"
#include "lua_script.h"
#include "p_slopes.h"

// SRB2Kart
#include "k_kart.h"
#include "k_battle.h"
#include "k_pwrlv.h"
#include "k_terrain.h"
#include "k_items.h"
#include "acs/interface.h"
#include "g_party.h"
#include "k_waypoint.h"
#include "k_itemlist.hpp"
#include "k_vote.h"

#include <tracy/tracy/TracyC.h>

savedata_t savedata;

static UINT8 P_SyncUINT8(savebuffer_t *save, UINT8 v)
{
	if (save->write)
	{
		WRITEUINT8(save->p,v);
		return v;
	}
	else
		return READUINT8(save->p);
}

static SINT8 P_SyncSINT8(savebuffer_t *save, SINT8 v)
{
	if (save->write)
	{
		WRITESINT8(save->p,v);
		return v;
	}
	else
		return READSINT8(save->p);
}

static UINT16 P_SyncUINT16(savebuffer_t *save, UINT16 v)
{
	if (save->write)
	{
		WRITEUINT16(save->p, v);
		return v;
	}
	else
		return READUINT16(save->p);
}

static INT16 P_SyncINT16(savebuffer_t *save, INT16 v)
{
	if (save->write)
	{
		WRITEINT16(save->p, v);
		return v;
	}
	else
		return READINT16(save->p);
}

static UINT32 P_SyncUINT32(savebuffer_t *save, UINT32 v)
{
	if (save->write)
	{
		WRITEUINT32(save->p, v);
		return v;
	}
	else
		return READUINT32(save->p);
}

static INT32 P_SyncINT32(savebuffer_t *save, INT32 v)
{
	if (save->write)
	{
		WRITEINT32(save->p, v);
		return v;
	}
	else
		return READINT32(save->p);
}

static UINT64 P_SyncUINT64(savebuffer_t *save, UINT32 v)
{
	if (save->write)
	{
		WRITEUINT64(save->p, v);
		return v;
	}
	else
		return READUINT64(save->p);
}

static INT64 P_SyncINT64(savebuffer_t *save, INT32 v)
{
	if (save->write)
	{
		WRITEINT64(save->p, v);
		return v;
	}
	else
		return READINT64(save->p);
}

/*
static char P_SyncChar(savebuffer_t *save, char v)
{
	if (save->write)
	{
		WRITECHAR(save->p, v);
		return v;
	}
	else
		return READCHAR(save->p);
}
*/

static fixed_t P_SyncFixed(savebuffer_t *save, fixed_t v)
{
	if (save->write)
	{
		WRITEFIXED(save->p, v);
		return v;
	}
	else
		return READFIXED(save->p);
}

static angle_t P_SyncAngle(savebuffer_t *save, angle_t v)
{
	if (save->write)
	{
		WRITEANGLE(save->p, v);
		return v;
	}
	else
		return READANGLE(save->p);
}

static void P_SyncStringN(savebuffer_t *save, char *s, size_t n)
{
	if (save->write)
		WRITESTRINGN(save->p, s, n);
	else
		READSTRINGN(save->p, s, n);
}

/*
static void P_SyncStringL(savebuffer_t *save, char *s, size_t n)
{
	if (save->write)
		WRITESTRINGL(save->p, s, n);
	else
		READSTRINGL(save->p, s, n);
}

static void P_SyncString(savebuffer_t *save, char *s)
{
	if (save->write)
		WRITESTRING(save->p, s);
	else
		READSTRING(save->p, s);
}
*/

static void P_SyncMem(savebuffer_t *save, void *s, size_t n)
{
	if (save->write)
		WRITEMEM(save->p, s, n);
	else
		READMEM(save->p, s, n);
}

static player_t *P_SyncPlayer(savebuffer_t *save, player_t *player)
{
	UINT8 playernum = P_SyncUINT8(save, player != NULL ? player - players : UINT8_MAX);

	if (save->write)
		return player;

	if (playernum == UINT8_MAX)
		return NULL;

	if (playernum >= MAXPLAYERS)
		I_Error("P_SyncPlayer: playernum %u >= MAXPLAYERS %u", playernum, MAXPLAYERS);
	return &players[playernum];
}

static state_t *P_SyncState(savebuffer_t *save, state_t *state)
{
	UINT16 statenum = P_SyncUINT16(save, state != NULL ? state - states : UINT16_MAX);

	if (save->write)
		return state;

	if (statenum == UINT16_MAX)
		return NULL;

	if (statenum >= numstates)
		I_Error("P_SyncState: statenum %u >= numstates %u", statenum, numstates);
	return &states[statenum];
}

static line_t *P_SyncLine(savebuffer_t *save, line_t *line)
{
	UINT32 linenum = P_SyncUINT32(save, line != NULL ? line - lines : UINT32_MAX);

	if (save->write)
		return line;

	if (linenum == UINT32_MAX)
		return NULL;

	if (linenum >= numlines)
		I_Error("P_SyncLine: linenum %u >= numlines %zu", linenum, numlines);
	return &lines[linenum];
}

static sector_t *P_SyncSector(savebuffer_t *save, sector_t *sector)
{
	UINT32 sectornum = P_SyncUINT32(save, sector != NULL ? sector - sectors : UINT32_MAX);

	if (save->write)
		return sector;

	if (sectornum == UINT32_MAX)
		return NULL;

	if (sectornum >= numsectors)
		I_Error("P_SyncSector: sectornum %u >= numsectors %zu", sectornum, numsectors);
	return &sectors[sectornum];
}

static pslope_t *P_SyncSlope(savebuffer_t *save, pslope_t *slope)
{
	UINT16 slopeid = P_SyncUINT16(save, slope != NULL ? slope->id : UINT16_MAX);

	if (save->write)
		return slope;

	if (slopeid == UINT16_MAX)
		return NULL;

	pslope_t *ret = P_SlopeById(slopeid);
	if (ret == NULL)
		I_Error("P_SyncSlope: Slope with ID %u not found", slopeid);
	return ret;
}

static ffloor_t *P_SyncFFloor(savebuffer_t *save, ffloor_t *fof)
{
	sector_t *sec = P_SyncSector(save, fof ? fof->target : NULL);
	UINT16 id = P_SyncUINT16(save, P_GetFFloorID(fof));

	if (save->write)
		return fof;

	if (sec == NULL)
		return NULL;

	ffloor_t *ret = P_GetFFloorByID(sec, id);
	if (ret == NULL)
		I_Error("P_SyncFFloor: FOF with ID %u not found (sector %td)", id, sec - sectors);
	return ret;
}

static terrain_t *P_SyncTerrain(savebuffer_t *save, terrain_t *terrain)
{
	UINT32 terrain_index = P_SyncUINT32(save, terrain != NULL ? K_GetTerrainHeapIndex(terrain) : UINT32_MAX);

	if (save->write)
		return terrain;

	if (terrain_index == UINT32_MAX)
		return NULL;

	terrain_t *ret = K_GetTerrainByIndex(terrain_index);
	if (ret == NULL)
		I_Error("P_SyncTerrain: Terrain with ID %u not found", terrain_index);
	return ret;
}

static mobj_t *P_SyncMobj(savebuffer_t *save, mobj_t *mo)
{
	if (save->write)
	{
		WRITEUINT32(save->p, mo != NULL ? mo->mobjnum : 0);
		return mo;
	}
	else
		return (mobj_t *)(uintptr_t)READUINT32(save->p);
}

static void P_RelinkMobj(savebuffer_t *save, mobj_t **ptr)
{
	if (save->write)
		return;

	UINT32 mobjnum = (UINT32)(uintptr_t)*ptr;
	*ptr = NULL;

	if (mobjnum == 0)
		return;

	mobj_t *new = P_FindNewPosition(mobjnum);
	if (new == NULL)
		// yeah, let's just silently remove an mobj pointer on the client's end
		// what could possibly go wrong?
		I_Error("mobjnum %u not found!", mobjnum);

	P_SetTarget(ptr, new);
}

static waypoint_t *P_SyncWaypoint(savebuffer_t *save, waypoint_t *wp)
{
	if (save->write)
	{
		WRITEUINT32(save->p, wp != NULL ? K_GetWaypointHeapIndex(wp) : UINT32_MAX);
		return wp;
	}
	else
		return (waypoint_t *)(uintptr_t)READUINT32(save->p);
}

static void P_RelinkWaypoint(savebuffer_t *save, waypoint_t **ptr)
{
	if (save->write)
		return;

	UINT32 index = (UINT32)(uintptr_t)*ptr;
	*ptr = NULL;

	if (index == UINT32_MAX)
		return;

	waypoint_t *new = K_GetWaypointFromIndex(index);
	if (new == NULL)
		I_Error("Waypoint index %u not found!", index);

	*ptr = new;
}

// STANDARD SYNC MACROS
// can be used without caveats (assuming world is synced before thinkers)

#define SYNC2(out, in) out = _Generic(in, \
	SINT8: P_SyncSINT8, \
	INT16: P_SyncINT16, \
	INT32: P_SyncINT32, \
	INT64: P_SyncINT64, \
	UINT8: P_SyncUINT8, \
	UINT16: P_SyncUINT16, \
	UINT32: P_SyncUINT32, \
	UINT64: P_SyncUINT64, \
	player_t *: P_SyncPlayer, \
	state_t *: P_SyncState, \
	ffloor_t *: P_SyncFFloor, \
	line_t *: P_SyncLine, \
	sector_t *: P_SyncSector, \
	pslope_t *: P_SyncSlope, \
	terrain_t *: P_SyncTerrain \
)(save, in)

#define SYNC(v) SYNC2(v, v)
#define SYNCAS(t, v) SYNC2(v, (t)v)

#define SYNCBOOLEAN(v) v = _Generic(v, \
	boolean: P_SyncUINT8 \
)(save, v)

// SYNC AND RELINK MACROS
// each use of RSYNC must be paired with a RELINK to restore the pointer

#define RSYNC(v) v = _Generic(v, \
	mobj_t *: P_SyncMobj, \
	waypoint_t *: P_SyncWaypoint \
)(save, v)

#define RELINK(v) _Generic(v, \
	mobj_t **: P_RelinkMobj, \
	waypoint_t **: P_RelinkWaypoint \
)(save, v)

// Block UINT32s to attempt to ensure that the correct data is
// being sent and received
#define ARCHIVEBLOCK_MISC		0x7FEEDEED
#define ARCHIVEBLOCK_PLAYERS	0x7F448008
#define ARCHIVEBLOCK_PARTIES	0x7F87AF0C
#define ARCHIVEBLOCK_ROUNDQUEUE	0x7F721331
#define ARCHIVEBLOCK_WORLD		0x7F8C08C0
#define ARCHIVEBLOCK_POBJS		0x7F928546
#define ARCHIVEBLOCK_THINKERS	0x7F37037C
#define ARCHIVEBLOCK_SPECIALS	0x7F228378
#define ARCHIVEBLOCK_WAYPOINTS	0x7F46498F

// Specialized netsynch markers
#define PLYRSYNC_ITEMLIST (1)

static inline void P_ArchivePlayer(savebuffer_t *save)
{
	const player_t *player = &players[consoleplayer];
	INT16 skininfo = player->skin;
	SINT8 pllives = player->lives;
	if (pllives < startinglivesbalance[numgameovers]) // Bump up to 3 lives if the player
		pllives = startinglivesbalance[numgameovers]; // has less than that.

	WRITEUINT16(save->p, skininfo);
	WRITEUINT8(save->p, numgameovers);
	WRITESINT8(save->p, pllives);
	WRITEUINT32(save->p, player->score);
}

static inline void P_UnArchivePlayer(savebuffer_t *save)
{
	INT16 skininfo = READUINT16(save->p);
	savedata.skin = skininfo;

	savedata.numgameovers = READUINT8(save->p);
	savedata.lives = READSINT8(save->p);
	savedata.score = READUINT32(save->p);
}

static void P_NetSyncPlayers(savebuffer_t *save)
{
	TracyCZone(__zone, true);

	INT32 i, j;

	if (P_SyncUINT32(save, ARCHIVEBLOCK_PLAYERS) != ARCHIVEBLOCK_PLAYERS)
		I_Error("Bad $$$.sav at archive block Players");

	for (i = 0; i < MAXPLAYERS; i++)
	{
		SYNCAS(SINT8, adminplayers[i]);

		for (j = 0; j < PWRLV_NUMTYPES; j++)
		{
			SYNC(clientpowerlevels[i][j]);
		}
		SYNC(clientPowerAdd[i]);

		if (!playeringame[i])
			continue;

		// no longer send ticcmds

		P_SyncStringN(save, player_names[i], MAXPLAYERNAME);

		SYNC(playerconsole[i]);
		SYNC(splitscreen_invitations[i]);

		// Item lists
		if (P_SyncUINT32(save, (K_GetItemListSize(i) ? PLYRSYNC_ITEMLIST : 0)))
		{
			K_SyncItemList(save, i);
		}

		SYNC(players[i].angleturn);
		SYNC(players[i].aiming);
		SYNC(players[i].drawangle);
		SYNC(players[i].viewrollangle);
		SYNC(players[i].tilt);
		SYNC(players[i].awayviewaiming);
		SYNC(players[i].awayviewtics);

		SYNC(players[i].playerstate);
		SYNC(players[i].pflags);
		SYNC(players[i].airdropflags);
		SYNC(players[i].panim);
		SYNCBOOLEAN(players[i].spectator);
		SYNC(players[i].spectatewait);

		SYNC(players[i].flashpal);
		SYNC(players[i].flashcount);

		SYNC(players[i].skincolor);
		SYNC(players[i].skin);
		SYNC(players[i].voice_id);

		for (j = 0; j < MAXAVAILABILITY; j++)
		{
			SYNC(players[i].availabilities[j]);
		}

		//////////////////////
		// Player Rank Data //
		//////////////////////
		SYNC(players[i].ranklog.numhitconfirms);
		// Not using a loop for driftboostcount for micro-optimization reasons
		SYNC(players[i].ranklog.driftboostcount[0]);
		SYNC(players[i].ranklog.driftboostcount[1]);
		SYNC(players[i].ranklog.driftboostcount[2]);
		SYNC(players[i].ranklog.driftboostcount[3]);
		SYNC(players[i].ranklog.leadertics);
		SYNC(players[i].ranklog.spbattack);
		SYNC(players[i].ranklog.startboostgrading);
		SYNC(players[i].ranklog.lapgrading);
		SYNC(players[i].ranklog.timeoffroadcurrent);
		SYNC(players[i].ranklog.timeoffroadtotal);
		SYNC(players[i].ranklog.wallbonks);
		SYNC(players[i].ranklog.hitdamagingsector);
		SYNC(players[i].ranklog.numrespawns);
		SYNC(players[i].ranklog.ijustgothitasshole);
		SYNC(players[i].ranklog.numlapgrades);
		SYNCBOOLEAN(players[i].ranklog.lapwasgraded);
		SYNCBOOLEAN(players[i].ranklog.prankeligible);
		SYNC(players[i].rankscore);
		SYNC(players[i].realrankscore);
		SYNC(players[i].ranktier);

		SYNC(players[i].score);
		SYNC(players[i].lives);
		SYNC(players[i].xtralife);
		SYNC(players[i].speed);
		SYNC(players[i].lastspeed);
		SYNC(players[i].deadtimer);
		SYNC(players[i].deadbounces);
		SYNCBOOLEAN(players[i].extendeddeath);
		SYNC(players[i].exiting);

		////////////////////////////
		// Conveyor Belt Movement //
		////////////////////////////
		SYNC(players[i].cmomx); // Conveyor momx
		SYNC(players[i].cmomy); // Conveyor momy
		SYNC(players[i].rmomx); // "Real" momx (momx - cmomx)
		SYNC(players[i].rmomy); // "Real" momy (momy - cmomy)

		SYNC(players[i].totalring);
		SYNC(players[i].realtime);
		for (j = 0; j < LAP__MAX; j++)
		{
			SYNC(players[i].laptime[j]);
		}
		SYNC(players[i].laps);
		SYNC(players[i].latestlap);

		SYNC(players[i].starposttime);
		SYNC(players[i].starpostx);
		SYNC(players[i].starposty);
		SYNC(players[i].starpostz);
		SYNC(players[i].starpostnum);
		SYNC(players[i].starpostangle);
		SYNCBOOLEAN(players[i].starpostflip);
		SYNC(players[i].prevcheck);
		SYNC(players[i].nextcheck);

		SYNC(players[i].ctfteam);

		SYNC(players[i].checkskip);

		SYNC(players[i].lastsidehit);
		SYNC(players[i].lastlinehit);

		SYNC(players[i].onconveyor);

		SYNC(players[i].jointime);
		SYNC(players[i].spectatorreentry);

		SYNC(players[i].grieftime);
		SYNC(players[i].griefstrikes);

		SYNC(players[i].splitscreenindex);

		RSYNC(players[i].awayviewmobj);
		RSYNC(players[i].followmobj);
		SYNC(players[i].followitem);

		SYNC(players[i].charflags);

		// SRB2kart
		SYNC(players[i].kartspeed);
		SYNC(players[i].kartweight);

		SYNC(players[i].kartspeedrestat);
		SYNC(players[i].kartweightrestat);
		SYNC(players[i].randomrestat);

		for (j = 0; j < MAXPREDICTTICS; j++)
		{
			SYNC(players[i].lturn_max[j]);
			SYNC(players[i].rturn_max[j]);
		}

		SYNC(players[i].followerskin);
		SYNCBOOLEAN(players[i].followerready);
		SYNC(players[i].followercolor);

		RSYNC(players[i].follower);

		for (j = 0; j < NUMPOWERS; j++)
			SYNC(players[i].powers[j]);
		for (j = 0; j < NUMKARTSTUFF; j++)
			SYNC(players[i].kartstuff[j]);

		SYNC(players[i].nocontrol);
		SYNC(players[i].carry);
		SYNC(players[i].dye);

		SYNC(players[i].position);
		SYNC(players[i].oldposition);
		SYNC(players[i].positiondelay);
		SYNC(players[i].distancetofinish);
		SYNC(players[i].distancetofinishprev);
		RSYNC(players[i].currentwaypoint);
		RSYNC(players[i].nextwaypoint);
		SYNCBOOLEAN(players[i].onbadwaypoint);
		SYNC(players[i].bigwaypointgap);
		SYNC(players[i].airtime);
		SYNC(players[i].startboost);

		SYNC(players[i].distancefromcluster);
		SYNC(players[i].distancefrombestfit);

		SYNC(players[i].dropdash);
		SYNC(players[i].respawn);

		SYNC(players[i].flashing);
		SYNC(players[i].spinouttimer);
		SYNC(players[i].spinoutrot);
		SYNC(players[i].spinouttype);
		SYNC(players[i].flipovertimer);
		SYNC(players[i].flipoverangle);
		SYNC(players[i].squishedtimer);
		SYNC(players[i].crash_state);
		SYNC(players[i].crash_rot);
		SYNC(players[i].crash_yaw);
		SYNC(players[i].crash_elapsed);
		SYNC(players[i].crash_reorient_start);
		SYNC(players[i].crash_zoffset);
		SYNC(players[i].crash_zoffset_delta);
		SYNC(players[i].crash_zoffset_lerp);
		SYNC(players[i].crash_side);
		SYNCBOOLEAN(players[i].crash_bumpme);
		SYNC(players[i].crashtimer);
		SYNC(players[i].instashield);
		SYNC(players[i].wipeoutslow);
		SYNC(players[i].justbumped);
		SYNCBOOLEAN(players[i].noclip);

		SYNC(players[i].driftmode);
		SYNC(players[i].drift);
		SYNC(players[i].driftturnsnapshot);
		SYNC(players[i].driftlock);
		SYNC(players[i].driftcharge);
		SYNC(players[i].driftboost);
		SYNC(players[i].airdriftspeed);
		SYNC(players[i].nulldrift);
		SYNC(players[i].karttilt);
		SYNC(players[i].karttiltmomentum);
		SYNC(players[i].nulldrifttime);

		SYNC(players[i].kartmagnet.factor);
		SYNC(players[i].kartmagnet.base_speed);
		SYNC(players[i].kartmagnet.target_speed);
		SYNC(players[i].kartmagnet.target_gradient);

		SYNC(players[i].recoverydashcharge);
		SYNC(players[i].recoverydash);

		SYNC(players[i].aizdriftstrat);
		SYNC(players[i].aizdrifttilt);
		SYNC(players[i].aizdriftturn);
		SYNC(players[i].slipdashcharge);
		SYNC(players[i].slipdashdir);
		SYNC(players[i].airthrustdir);

		SYNC(players[i].offroad);
		SYNC(players[i].tiregrease);
		SYNC(players[i].pogospring);
		SYNC(players[i].brakestop);
		SYNC(players[i].waterskip);

		SYNC(players[i].dashpadcooldown);

		SYNC(players[i].boostpower);
		SYNC(players[i].speedboost);
		SYNC(players[i].prevspeedboost);
		SYNC(players[i].accelboost);
		SYNC(players[i].handleboost);
		SYNC(players[i].forcedtopspeed);
		SYNC(players[i].boostangle);
		SYNC(players[i].numsneakers);
		SYNC(players[i].numpanels);
		SYNC(players[i].numboosts);

		SYNC(players[i].draftpower);
		SYNC(players[i].draftleeway);
		SYNC(players[i].lastdraft);

		// boostinfo_t
		SYNC(players[i].boostinfo.stackspeedboost);
		SYNC(players[i].boostinfo.nonstackspeedboost);
		SYNC(players[i].boostinfo.accelboost);
		SYNC(players[i].boostinfo.handleboost);
		SYNC(players[i].boostinfo.grade);

		SYNC(players[i].tripwireState);
		SYNC(players[i].tripwirePass);
		SYNC(players[i].tripwireLeniency);
		SYNC(players[i].tripwireReboundDelay);

		SYNC(players[i].itemroulette);
		SYNC(players[i].previtemroulette);
		SYNC(players[i].itemblink);
		SYNC(players[i].itemblinkmode);
		SYNC(players[i].roulettetype);

		SYNC(players[i].itemtype);
		SYNC(players[i].itemamount);
		SYNC(players[i].equippeditem);
		SYNC(players[i].throwdir);

		SYNC(players[i].sadtimer);
		SYNC(players[i].bricktimer);

		SYNC(players[i].rings);
		SYNC(players[i].ringmin);
		SYNC(players[i].ringmax);
		SYNC(players[i].pickuprings);
		SYNC(players[i].ringdelay);
		SYNC(players[i].ringlock);
		SYNC(players[i].ringboost);
		SYNC(players[i].ringtime);
		SYNC(players[i].superring);
		SYNC(players[i].superringleeway);
		SYNC(players[i].superringdrag);
		SYNC(players[i].nextringaward);
		SYNC(players[i].ringvolume);
		SYNC(players[i].ringtransparency);

		SYNC(players[i].airdroppredelay);
		SYNC(players[i].airdroptime);
		SYNC(players[i].airdropbuffer);
		SYNC(players[i].airdropheavydash);
		SYNC(players[i].airdropflags);

		RSYNC(players[i].shieldtracer);

		SYNC(players[i].attractioncharge);
		SYNC(players[i].attractionattack);
		SYNC(players[i].attractionattack_hipower);
		SYNC(players[i].attractionboost);

		SYNC(players[i].bubblecool);
		SYNC(players[i].bubbleblowup);
		SYNC(players[i].bubblehealth);
		SYNC(players[i].bubbleboost);
		SYNC(players[i].bubblegraze);
		SYNC(players[i].bubblebuffer);

		SYNC(players[i].flamedash);
		SYNC(players[i].flametimer);
		SYNC(players[i].flamestore);
		SYNC(players[i].flameburnstop);
		SYNC(players[i].flameoverheat);

		SYNC(players[i].hyudorotimer);
		SYNC(players[i].stealingtimer);

		SYNC(players[i].sneakertimer);
		SYNC(players[i].realsneakertimer);
		SYNC(players[i].floorboost);
		SYNC(players[i].chaintimer);

		SYNC(players[i].boostcharge);

		SYNC(players[i].slopeboost);
		SYNC(players[i].prevslopeboost);
		SYNC(players[i].slopeaccel);

		SYNC(players[i].growshrinktimer);
		SYNC(players[i].growcancel);
		SYNC(players[i].altshrinktimeshit);
		SYNCBOOLEAN(players[i].inarrowbullet);
		RSYNC(players[i].arrowbullet);

		SYNC(players[i].rocketsneakertimer);

		SYNC(players[i].invincibilitytimer);
		SYNC(players[i].smonitortimer);
		SYNC(players[i].maxsmonitortime);
		SYNC(players[i].smonitorexpiring);
		SYNC(players[i].smonitorcancel);
		SYNC(players[i].smonitorwarning);

		SYNC(players[i].eggmanexplode);
		SYNC(players[i].eggmanblame);

		SYNC(players[i].bananadrag);

		SYNC(players[i].lastitemtarget);
		SYNC(players[i].itemtargetdelay);

		SYNC(players[i].interpoints);
		SYNC(players[i].roundscore);
		SYNC(players[i].emeralds);
		SYNC(players[i].bumper);
		SYNC(players[i].karmadelay);
		SYNC(players[i].karmamode);
		SYNC(players[i].karmapoints);
		SYNC(players[i].wanted);

		SYNC(players[i].dashRingPullTics);
		SYNC(players[i].dashRingPushTics);
		SYNC(players[i].dashRainbowPogo);

		SYNC(players[i].glanceDir);

		SYNC(players[i].breathTimer);

		SYNC(players[i].lastsafelap);
		SYNC(players[i].lastsafestarpost);

		SYNC(players[i].tripwireUnstuck);
		SYNC(players[i].bumpUnstuck);

		SYNC(players[i].typing_timer);
		SYNC(players[i].typing_duration);

		SYNC(players[i].kickstartaccel);

		SYNC(players[i].itemflags);

		// botvars_t
		SYNCBOOLEAN(players[i].bot);
		SYNC(players[i].botvars.style);
		SYNC(players[i].botvars.difficulty);
		SYNC(players[i].botvars.diffincrease);
		SYNCBOOLEAN(players[i].botvars.rival);
		SYNC(players[i].botvars.rubberband);
		SYNC(players[i].botvars.controller);

		SYNC(players[i].outrun);
		SYNC(players[i].outruntime);

		// sonicloopsvars_t
		SYNC(players[i].loop.radius);
		SYNC(players[i].loop.revolution);
		SYNC(players[i].loop.min_revolution);
		SYNC(players[i].loop.max_revolution);
		SYNC(players[i].loop.yaw);
		SYNC(players[i].loop.origin.x);
		SYNC(players[i].loop.origin.y);
		SYNC(players[i].loop.origin.z);
		SYNC(players[i].loop.origin_shift.x);
		SYNC(players[i].loop.origin_shift.y);
		SYNC(players[i].loop.shift.x);
		SYNC(players[i].loop.shift.y);
		SYNCBOOLEAN(players[i].loop.flip);

		// sonicloopcamvars_t
		SYNC(players[i].loop.camera.enter_tic);
		SYNC(players[i].loop.camera.exit_tic);
		SYNC(players[i].loop.camera.zoom_in_speed);
		SYNC(players[i].loop.camera.zoom_out_speed);
		SYNC(players[i].loop.camera.dist);
		SYNC(players[i].loop.camera.pan);
		SYNC(players[i].loop.camera.pan_speed);
		SYNC(players[i].loop.camera.pan_accel);
		SYNC(players[i].loop.camera.pan_back);

		// Restored NiGHTS stuff
		SYNC(players[i].bumpertime);
		SYNC(players[i].linkcount);
		SYNC(players[i].linktimer);
		SYNC(players[i].maxlink);

		// Fix janky landing particle
		SYNCBOOLEAN(players[i].prevonground);

		// Wall Transfer bonus
		SYNCBOOLEAN(players[i].walltransfered);
		SYNC(players[i].walltransferboost);

		SYNC(players[i].itemusecooldown);
		SYNC(players[i].itemusecooldownmax);

		P_SyncMem(save, players[i].public_key, PUBKEYLENGTH);

		SYNC(players[i].bumpyroadside);

		// keepstuff_t
		SYNC(players[i].keepstuff.flags);
		SYNC(players[i].keepstuff.rocketsneakertimer);
		SYNC(players[i].keepstuff.invincibilitytimer);
		SYNC(players[i].keepstuff.smonitortimer);
		SYNC(players[i].keepstuff.smonitorexpiring);
		SYNC(players[i].keepstuff.maxsmonitortime);
		SYNC(players[i].keepstuff.growshrinktimer);
		SYNC(players[i].keepstuff.flametimer);

		SYNC(players[i].itemreservetype);
		SYNC(players[i].itemreserveamount);

		SYNC(players[i].dashareapower);
	}
	TracyCZoneEnd(__zone);
}

static void P_NetSyncParties(savebuffer_t *save)
{
	TracyCZone(__zone, true);

	INT32 i, k;
	UINT8 partySize;

	if (P_SyncUINT32(save, ARCHIVEBLOCK_PARTIES) != ARCHIVEBLOCK_PARTIES)
		I_Error("Bad $$$.sav at archive block Parties");

	if (!save->write)
	{
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i])
				continue;

			G_DestroyParty(i);
			G_BuildLocalSplitscreenParty(i);
		}
	}

	if (save->write)
	{
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i])
				continue;

			partySize = G_PartySize(i);

			WRITEUINT8(save->p, partySize);

			for (k = 0; k < partySize; ++k)
			{
				WRITEUINT8(save->p, G_PartyMember(i, k));
			}
		}
	}
	else
	{
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i])
				continue;

			partySize = READUINT8(save->p);

			for (k = 0; k < partySize; ++k)
			{
				G_JoinParty(i, READUINT8(save->p));
			}
		}
	}

	TracyCZoneEnd(__zone);
}

static void P_NetSyncRoundQueue(savebuffer_t *save)
{
	UINT8 i;

	if (P_SyncUINT32(save, ARCHIVEBLOCK_ROUNDQUEUE) != ARCHIVEBLOCK_ROUNDQUEUE)
		I_Error("Bad $$$.sav at archive block RoundQueue");

	if (!save->write)
		memset(&roundqueue, 0, sizeof(struct roundqueue));

	SYNC(roundqueue.position);
	SYNC(roundqueue.size);
	SYNC(roundqueue.roundnum);

	for (i = 0; i < roundqueue.size; i++)
	{
		//SYNC(roundqueue.entries[i].mapnum);
		/* NOPE! Clients do not need to know what is in the roundqueue.
		 *		This disincentivises cheaty clients in future tournament environments.
		 *		~toast 080423 */
		if (!save->write)
			roundqueue.entries[i].mapnum = 0; // TEST RUN -- dummy, has to be < nummapheaders
		SYNC(roundqueue.entries[i].gametype);
		SYNCBOOLEAN(roundqueue.entries[i].encore);
		SYNCBOOLEAN(roundqueue.entries[i].rankrestricted);
	}
}

///
/// Colormaps
///

static extracolormap_t *net_colormaps = NULL;
static UINT32 num_net_colormaps = 0;
static UINT32 num_ffloors = 0; // for loading

// Copypasta from r_data.c AddColormapToList
// But also check for equality and return the matching index
static UINT32 CheckAddNetColormapToList(extracolormap_t *extra_colormap)
{
	extracolormap_t *exc, *exc_prev = NULL;
	UINT32 i = 0;

	if (!net_colormaps)
	{
		net_colormaps = R_CopyColormap(extra_colormap, false);
		net_colormaps->next = 0;
		net_colormaps->prev = 0;
		num_net_colormaps = i+1;
		return i;
	}

	for (exc = net_colormaps; exc; exc_prev = exc, exc = exc->next)
	{
		if (R_CheckEqualColormaps(exc, extra_colormap, true, true, true))
			return i;
		i++;
	}

	exc_prev->next = R_CopyColormap(extra_colormap, false);
	extra_colormap->prev = exc_prev;
	extra_colormap->next = 0;

	num_net_colormaps = i+1;
	return i;
}

static extracolormap_t *GetNetColormapFromList(UINT32 index)
{
	// For loading, we have to be tricky:
	// We load the sectors BEFORE knowing the colormap values
	// So if an index doesn't exist, fill our list with dummy colormaps
	// until we get the index we want
	// Then when we load the color data, we set up the dummy colormaps

	extracolormap_t *exc, *last_exc = NULL;
	UINT32 i = 0;

	if (!net_colormaps) // initialize our list
		net_colormaps = R_CreateDefaultColormap(false);

	for (exc = net_colormaps; exc; last_exc = exc, exc = exc->next)
	{
		if (i++ == index)
			return exc;
	}


	// LET'S HOPE that index is a sane value, because we create up to [index]
	// entries in net_colormaps. At this point, we don't know
	// what the total colormap count is
	if (index >= numsectors*3 + num_ffloors)
		// if every sector had a unique colormap change AND a fade color thinker which has two colormap entries
		// AND every ffloor had a fade FOF thinker with one colormap entry
		I_Error("Colormap %d from server is too high for sectors %d", index, (UINT32)numsectors);

	// our index doesn't exist, so just make the entry
	for (; i <= index; i++)
	{
		exc = R_CreateDefaultColormap(false);
		if (last_exc)
			last_exc->next = exc;
		exc->prev = last_exc;
		exc->next = NULL;
		last_exc = exc;
	}
	return exc;
}

static void ClearNetColormaps(void)
{
	// We're actually Z_Freeing each entry here,
	// so don't call this in P_NetUnArchiveColormaps (where entries will be used in-game)
	extracolormap_t *exc, *exc_next;

	for (exc = net_colormaps; exc; exc = exc_next)
	{
		exc_next = exc->next;
		Z_Free(exc);
	}
	num_net_colormaps = 0;
	num_ffloors = 0;
	net_colormaps = NULL;
}

static void P_NetSyncColormaps(savebuffer_t *save)
{
	TracyCZone(__zone, true);

	// When we reach this point, we already populated our list with
	// dummy colormaps. Now that we are loading the color data,
	// set up the dummies.
	extracolormap_t *exc, *existing_exc = NULL, *exc_next = NULL;
	UINT32 i = 0;

	SYNC(num_net_colormaps);

	for (exc = net_colormaps; i < num_net_colormaps; i++, exc = exc_next)
	{
		// TODO: Merge these paths (got lazy since i've been at this for so long now)
		if (save->write)
		{
			// We must save num_net_colormaps worth of data
			// So fill non-existent entries with default.
			if (!exc)
				exc = R_CreateDefaultColormap(false);

			WRITEUINT8(save->p, exc->fadestart);
			WRITEUINT8(save->p, exc->fadeend);
			WRITEUINT8(save->p, exc->flags);

			WRITEINT32(save->p, exc->rgba);
			WRITEINT32(save->p, exc->fadergba);

#ifdef EXTRACOLORMAPLUMPS
			WRITESTRINGN(save->p, exc->lumpname, 9);
#endif

			exc_next = exc->next;
			Z_Free(exc); // don't need anymore
		}
		else
		{
			UINT8 fadestart, fadeend, flags;
			INT32 rgba, fadergba;
#ifdef EXTRACOLORMAPLUMPS
			char lumpname[9];
#endif

			fadestart = READUINT8(save->p);
			fadeend = READUINT8(save->p);
			flags = READUINT8(save->p);

			rgba = READINT32(save->p);
			fadergba = READINT32(save->p);

#ifdef EXTRACOLORMAPLUMPS
			READSTRINGN(save->p, lumpname, 9);

			if (lumpname[0])
			{
				if (!exc)
					// no point making a new entry since nothing points to it,
					// but we needed to read the data so now continue
					continue;

				exc_next = exc->next; // this gets overwritten during our operations here, so get it now
				existing_exc = R_ColormapForName(lumpname);
				*exc = *existing_exc;
				R_AddColormapToList(exc); // see HACK note below on why we're adding duplicates
				continue;
			}
#endif

			if (!exc)
				// no point making a new entry since nothing points to it,
				// but we needed to read the data so now continue
				continue;

			exc_next = exc->next; // this gets overwritten during our operations here, so get it now

			exc->fadestart = fadestart;
			exc->fadeend = fadeend;
			exc->flags = flags;

			exc->rgba = rgba;
			exc->fadergba = fadergba;

#ifdef EXTRACOLORMAPLUMPS
			exc->lump = LUMPERROR;
			exc->lumpname[0] = 0;
#endif

			existing_exc = R_GetColormapFromListByValues(rgba, fadergba, fadestart, fadeend, flags);

			if (existing_exc)
				exc->colormap = existing_exc->colormap;
			else
				// CONS_Debug(DBG_RENDER, "Creating Colormap: rgba(%d,%d,%d,%d) fadergba(%d,%d,%d,%d)\n",
				// 	R_GetRgbaR(rgba), R_GetRgbaG(rgba), R_GetRgbaB(rgba), R_GetRgbaA(rgba),
				//	R_GetRgbaR(fadergba), R_GetRgbaG(fadergba), R_GetRgbaB(fadergba), R_GetRgbaA(fadergba));
				exc->colormap = R_CreateLightTable(exc);

			// HACK: If this dummy is a duplicate, we're going to add it
			// to the extra_colormaps list anyway. I think this is faster
			// than going through every loaded sector and correcting their
			// colormap address to the pre-existing one, PER net_colormap entry
			R_AddColormapToList(exc);

			if (i < num_net_colormaps-1 && !exc_next)
				exc_next = R_CreateDefaultColormap(false);
		}
	}

	// if we still have a valid net_colormap after iterating up to num_net_colormaps,
	// some sector had a colormap index higher than num_net_colormaps. We done goofed or $$$ was corrupted.
	// In any case, add them to the colormap list too so that at least the sectors' colormap
	// addresses are valid and accounted properly
	if (!save->write && exc_next)
	{
		existing_exc = R_GetDefaultColormap();
		for (exc = exc_next; exc; exc = exc->next)
		{
			exc->colormap = existing_exc->colormap; // all our dummies are default values
			R_AddColormapToList(exc);
		}
	}

	// Don't need these anymore
	num_net_colormaps = 0;
	num_ffloors = 0;
	net_colormaps = NULL;

	TracyCZoneEnd(__zone);
}

///
/// World Archiving
///

// each byte contains 7 flags
// the most significant bit indicates a following extra byte
enum sectordiff_e
{
	SD_FLOORHT = 0<<3,
	SD_CEILHT,
	SD_FLOORPIC,
	SD_CEILPIC,
	SD_LIGHT,
	SD_SPECIAL,
	SD_FFLOORS,

	// diff2 flags
	SD_FXOFFS = 1<<3,
	SD_FYOFFS,
	SD_CXOFFS,
	SD_CYOFFS,
	SD_FLOORANG,
	SD_CEILANG,
	SD_TAG,

	// diff3 flags
	SD__UNUSED = 2<<3,
	SD_COLORMAP,
	SD_CRUMBLESTATE,
	SD_FLOORLIGHT,
	SD_CEILLIGHT,
	SD_FLAG,
	SD_SPECIALFLAG,

	//diff4 flags
	SD_DAMAGETYPE = 3<<3,
	SD_TRIGGERTAG,
	SD_TRIGGERER,
	SD_GRAVITY,
	SD_ACTION,
	SD_ARGS,
	SD_STRINGARGS,

	//diff5 flags
	SD_ACTIVATION = 4<<3,
	SD_BOTCONTROLLER,

	SD__MAX
};

enum ffloordiff_e
{
	FD_FLAGS,
	FD_ALPHA,
};

static boolean P_SectorArgsEqual(const sector_t *sc, const sector_t *spawnsc)
{
	UINT8 i;
	for (i = 0; i < NUM_SCRIPT_ARGS; i++)
		if (sc->args[i] != spawnsc->args[i])
			return false;

	return true;
}

static boolean P_SectorStringArgsEqual(const sector_t *sc, const sector_t *spawnsc)
{
	UINT8 i;
	for (i = 0; i < NUM_SCRIPT_STRINGARGS; i++)
	{
		if (sc->stringargs[i] == NULL || spawnsc->stringargs[i] == NULL)
		{
			if (sc->stringargs[i] != spawnsc->stringargs[i])
				return false;
		}
		else
		{
			if (strcmp(sc->stringargs[i], spawnsc->stringargs[i]))
				return false;
		}
	}

	return true;
}

enum linediff_e
{
	LD_FLAG = 0<<3,
	LD_SPECIAL,
	LD_TAG,
	LD_S1TEXOFF,
	LD_S1TOPTEX,
	LD_S1BOTTEX,
	LD_S1MIDTEX,

	// diff2 flags
	LD_S2TEXOFF = 1<<3,
	LD_S2TOPTEX,
	LD_S2BOTTEX,
	LD_S2MIDTEX,
	LD_ARGS,
	LD_STRINGARGS,
	LD__UNUSED,

	// diff3 flags
	LD_ACTIVATION = 2<<3,

	LD__MAX
};

// diff macros
#define SETB(b) (diff[b / (8*sizeof(*diff))] |= (1 << (b & (8*sizeof(*diff) - 1))))
#define DIFFNE(x, field, b) if (x->field != spawn##x->field) SETB(b)
#define DIFFIF(x, field, b) if (x->field) SETB(b)
#define DIFF(x, b) if (x) SETB(b)

// sync macros
#define GETB(b) (diff[b / (8*sizeof(*diff))] & (1 << (b & (8*sizeof(*diff) - 1))))
#define SYNCF(b, v) if (GETB(b)) SYNC(v)
#define RSYNCF(b, v) if (GETB(b)) RSYNC(v)
#define SYNCFB(b, v) if (GETB(b)) SYNCBOOLEAN(v)
// if diff includes b, sync v, else if in read mode, set v to d(efault)
#define SYNCDEF(b, v, d) (GETB(b) ? SYNC(v) : !save->write ? (v = d) : (void)0)

static boolean P_LineArgsEqual(const line_t *li, const line_t *spawnli)
{
	UINT8 i;
	for (i = 0; i < NUM_SCRIPT_ARGS; i++)
		if (li->args[i] != spawnli->args[i])
			return false;

	return true;
}

static boolean P_LineStringArgsEqual(const line_t *li, const line_t *spawnli)
{
	UINT8 i;
	for (i = 0; i < NUM_SCRIPT_STRINGARGS; i++)
	{
		if (li->stringargs[i] == NULL || spawnli->stringargs[i] == NULL)
		{
			if (li->stringargs[i] != spawnli->stringargs[i])
				return false;
		}
		else
		{
			if (strcmp(li->stringargs[i], spawnli->stringargs[i]))
				return false;
		}
	}

	return true;
}

// Check if any of the sector's FOFs differ from how they spawned
static boolean CheckFFloorDiff(const sector_t *ss)
{
	ffloor_t *rover;

	for (rover = ss->ffloors; rover; rover = rover->next)
	{
		if (rover->fofflags != rover->spawnflags
		|| rover->alpha != rover->spawnalpha)
			{
				return true; // we found an FOF that changed!
				// don't bother checking for more, we do that later
			}
	}
	return false;
}

// Special case: save the stats of all modified ffloors along with their ffloor "number"s
// we don't bother with ffloors that haven't changed, that would just add to savegame even more than is really needed
static void SyncFFloors(savebuffer_t *save, const sector_t *ss)
{
	size_t j = 0; // ss->ffloors is saved as ffloor #0, ss->ffloors->next is #1, etc
	ffloor_t *rover;
	UINT16 fflr_i; // saved ffloor "number" of next modified ffloor
	UINT8 diff[1];

	if (!save->write)
		fflr_i = READUINT16(save->p); // get first modified ffloor's number ready

	for (rover = ss->ffloors; rover; rover = rover->next, j++)
	{
		memset(diff, 0, sizeof(diff));
		if (save->write)
		{
			if (rover->fofflags != rover->spawnflags)
				SETB(FD_FLAGS);
			if (rover->alpha != rover->spawnalpha)
				SETB(FD_ALPHA);

			if (diff[0] == 0)
				continue;

			WRITEUINT16(save->p, j);
		}
		else
		{
			if (fflr_i == 0xffff) // end of modified ffloors list, let's stop already
				break;
			if (j != fflr_i) // this ffloor was not modified
				continue;
		}

		SYNC(diff[0]);
		SYNCF(FD_FLAGS, rover->fofflags);
		SYNCF(FD_ALPHA, rover->alpha);

		if (!save->write)
			fflr_i = READUINT16(save->p); // get next ffloor "number" ready
	}

	if (save->write)
		WRITEUINT16(save->p, 0xffff);
}

static void DiffSectors(const sector_t *ss, const sector_t *spawnss, UINT8 diff[])
{
	DIFFNE(ss, floorheight, SD_FLOORHT);
	DIFFNE(ss, ceilingheight, SD_CEILHT);
	DIFFNE(ss, floorpic, SD_FLOORPIC);
	DIFFNE(ss, ceilingpic, SD_CEILPIC);
	DIFFNE(ss, lightlevel, SD_LIGHT);
	DIFFNE(ss, special, SD_SPECIAL);
	if (ss->ffloors && CheckFFloorDiff(ss))
		SETB(SD_FFLOORS);

	DIFFNE(ss, floor_xoffs, SD_FXOFFS);
	DIFFNE(ss, floor_yoffs, SD_FYOFFS);
	DIFFNE(ss, ceiling_xoffs, SD_CXOFFS);
	DIFFNE(ss, ceiling_yoffs, SD_CYOFFS);
	DIFFNE(ss, floorpic_angle, SD_FLOORANG);
	DIFFNE(ss, ceilingpic_angle, SD_CEILANG);
	if (!Tag_Compare(&ss->tags, &spawnss->tags))
		SETB(SD_TAG);

	//DIFFNE(ss, ..., SD__UNUSED);
	if (ss->extra_colormap != ss->spawn_extra_colormap)
		SETB(SD_COLORMAP);
	DIFFIF(ss, crumblestate, SD_CRUMBLESTATE);
	if (ss->floorlightlevel != spawnss->floorlightlevel || ss->floorlightabsolute != spawnss->floorlightabsolute)
		SETB(SD_FLOORLIGHT);
	if (ss->ceilinglightlevel != spawnss->ceilinglightlevel || ss->ceilinglightabsolute != spawnss->ceilinglightabsolute)
		SETB(SD_CEILLIGHT);
	DIFFNE(ss, flags, SD_FLAG);
	DIFFNE(ss, specialflags, SD_SPECIALFLAG);

	DIFFNE(ss, damagetype, SD_DAMAGETYPE);
	DIFFNE(ss, triggertag, SD_TRIGGERTAG);
	DIFFNE(ss, triggerer, SD_TRIGGERER);
	DIFFNE(ss, gravity, SD_GRAVITY);
	DIFFNE(ss, action, SD_ACTION);
	if (!P_SectorArgsEqual(ss, spawnss))
		SETB(SD_ARGS);
	if (!P_SectorStringArgsEqual(ss, spawnss))
		SETB(SD_STRINGARGS);

	DIFFNE(ss, activation, SD_ACTIVATION);
	if (/*ss->botController.trick != spawnss->botController.trick
		||*/ss->botController.flags != spawnss->botController.flags
		|| ss->botController.forceAngle != spawnss->botController.forceAngle)
	{
		SETB(SD_BOTCONTROLLER);
	}
}

static void SyncSectors(savebuffer_t *save)
{
	size_t i = 0, j = 0;
	sector_t *sec;
	UINT8 diff[(SD__MAX>>3) + 1];

	for (i = 0;; i++)
	{
		memset(diff, 0, sizeof(diff));
		if (save->write)
		{
			if (i >= numsectors)
			{
				WRITEUINT16(save->p, 0xffff);
				break;
			}

			DiffSectors(&sectors[i], &spawnsectors[i], diff);
			for (j = sizeof(diff)-1; j > 0; j--)
				if (diff[j])
					diff[j-1] |= 0x80;
			if (diff[0] == 0)
				continue;
		}

		SYNCAS(UINT16, i);

		if (i == 0xffff)
			break;

		if (i >= numsectors)
			I_Error("Invalid sector number %zu from server (expected end at %zu)", i, numsectors);

		sec = &sectors[i];
		j = 0;
		do
			SYNC(diff[j]);
		while (diff[j++] & 0x80);

		SYNCF(SD_FLOORHT, sec->floorheight);
		SYNCF(SD_CEILHT, sec->ceilingheight);
		if (GETB(SD_FLOORPIC))
		{
			if (save->write)
				WRITEMEM(save->p, levelflats[sec->floorpic].name, 8);
			else
			{
				sectors[i].floorpic = P_AddLevelFlatRuntime((char *)save->p);
				save->p += 8;
			}
		}
		if (GETB(SD_CEILPIC))
		{
			if (save->write)
				WRITEMEM(save->p, levelflats[sec->ceilingpic].name, 8);
			else
			{
				sectors[i].ceilingpic = P_AddLevelFlatRuntime((char *)save->p);
				save->p += 8;
			}
		}
		SYNCF(SD_LIGHT, sec->lightlevel);
		SYNCF(SD_SPECIAL, sec->special);
		if (GETB(SD_FFLOORS))
			SyncFFloors(save, sec);

		SYNCF(SD_FXOFFS, sec->floor_xoffs);
		SYNCF(SD_FYOFFS, sec->floor_yoffs);
		SYNCF(SD_CXOFFS, sec->ceiling_xoffs);
		SYNCF(SD_CYOFFS, sec->ceiling_yoffs);
		SYNCF(SD_FLOORANG, sec->floorpic_angle);
		SYNCF(SD_CEILANG, sec->ceilingpic_angle);
		if (GETB(SD_TAG))
		{
			if (save->write)
			{
				WRITEUINT32(save->p, sec->tags.count);
				for (j = 0; j < sec->tags.count; j++)
					WRITEINT16(save->p, sec->tags.tags[j]);
			}
			else
			{
				size_t ncount = READUINT32(save->p);

				// Remove entries from global lists.
				for (j = 0; j < sectors[i].tags.count; j++)
					Taggroup_Remove(tags_sectors, sectors[i].tags.tags[j], i);

				// Reallocate if size differs.
				if (ncount != sectors[i].tags.count)
				{
					sectors[i].tags.count = ncount;
					sectors[i].tags.tags = Z_Realloc(sectors[i].tags.tags, ncount*sizeof(mtag_t), PU_LEVEL, NULL);
				}

				for (j = 0; j < ncount; j++)
					sectors[i].tags.tags[j] = READINT16(save->p);

				// Add new entries.
				for (j = 0; j < sectors[i].tags.count; j++)
					Taggroup_Add(tags_sectors, sectors[i].tags.tags[j], i);
			}
		}

		if (GETB(SD_COLORMAP))
		{
			if (save->write)
				WRITEUINT32(save->p, CheckAddNetColormapToList(sec->extra_colormap));
					// returns existing index if already added, or appends to net_colormaps and returns new index
			else
				sec->extra_colormap = GetNetColormapFromList(READUINT32(save->p));
		}
		SYNCF(SD_CRUMBLESTATE, sec->crumblestate);
		SYNCF(SD_FLOORLIGHT, sec->floorlightlevel);
		SYNCFB(SD_FLOORLIGHT, sec->floorlightabsolute);
		SYNCF(SD_CEILLIGHT, sec->ceilinglightlevel);
		SYNCFB(SD_CEILLIGHT, sec->ceilinglightabsolute);
		SYNCF(SD_FLAG, sec->flags);
		SYNCF(SD_SPECIALFLAG, sec->specialflags);

		if (!save->write && GETB(SD_FLAG))
			CheckForReverseGravity |= sec->flags & MSF_GRAVITYFLIP;

		SYNCF(SD_DAMAGETYPE, sec->damagetype);
		SYNCF(SD_TRIGGERTAG, sec->triggertag);
		SYNCF(SD_TRIGGERER, sec->triggerer);
		SYNCF(SD_GRAVITY, sec->gravity);
		SYNCF(SD_ACTION, sec->action);
		if (GETB(SD_ARGS))
		{
			for (j = 0; j < NUM_SCRIPT_ARGS; j++)
				SYNC(sec->args[j]);
		}
		if (GETB(SD_STRINGARGS))
		{
			for (j = 0; j < NUM_SCRIPT_STRINGARGS; j++)
			{
				size_t len = P_SyncINT32(save, sec->stringargs[j] ? strlen(sec->stringargs[j]) : 0);
				if (!len)
				{
					if (!save->write)
					{
						Z_Free(sec->stringargs[j]);
						sec->stringargs[j] = NULL;
					}
					continue;
				}

				if (!save->write)
					sec->stringargs[j] = Z_Realloc(sec->stringargs[j], len + 1, PU_LEVEL, NULL);
				P_SyncMem(save, sec->stringargs[j], len);
				sec->stringargs[j][len] = '\0';
			}
		}

		SYNCF(SD_ACTIVATION, sec->activation);
		if (GETB(SD_BOTCONTROLLER))
		{
			if (save->write)
			{
				//WRITEUINT8(save->p, sec->botController.trick);
				WRITEUINT32(save->p, sec->botController.flags);
				WRITEANGLE(save->p, sec->botController.forceAngle);
			}
			else
			{
				//sectors[i].botController.trick = READUINT8(save->p);
				sectors[i].botController.flags = READUINT32(save->p);
				sectors[i].botController.forceAngle = READANGLE(save->p);
			}
		}
	}
}

static void DiffLines(const line_t *li, const line_t *spawnli, UINT8 diff[])
{
	const side_t *si, *spawnsi;

	DIFFNE(li, flags, LD_FLAG);
	DIFFNE(li, special, LD_SPECIAL);
	if (!Tag_Compare(&li->tags, &spawnli->tags))
		SETB(LD_TAG);

	if (!P_LineArgsEqual(li, spawnli))
		SETB(LD_ARGS);

	if (!P_LineStringArgsEqual(li, spawnli))
		SETB(LD_STRINGARGS);

	DIFFNE(li, activation, LD_ACTIVATION);

	if (li->sidenum[0] != NO_INDEX)
	{
		si = &sides[li->sidenum[0]];
		spawnsi = &spawnsides[li->sidenum[0]];
		DIFFNE(si, textureoffset, LD_S1TEXOFF);
		//SoM: 4/1/2000: Some textures are colormaps. Don't worry about invalid textures.
		DIFFNE(si, toptexture, LD_S1TOPTEX);
		DIFFNE(si, bottomtexture, LD_S1BOTTEX);
		DIFFNE(si, midtexture, LD_S1MIDTEX);
	}
	if (li->sidenum[1] != NO_INDEX)
	{
		si = &sides[li->sidenum[1]];
		spawnsi = &spawnsides[li->sidenum[1]];
		DIFFNE(si, textureoffset, LD_S2TEXOFF);
		DIFFNE(si, toptexture, LD_S2TOPTEX);
		DIFFNE(si, bottomtexture, LD_S2BOTTEX);
		DIFFNE(si, midtexture, LD_S2MIDTEX);
	}
}

static void SyncLines(savebuffer_t *save)
{
	size_t i = 0, j = 0;
	line_t *li;
	side_t *si;
	UINT8 diff[(LD__MAX>>3)+1];

	for (i = 0;; i++)
	{
		memset(diff, 0, sizeof(diff));
		if (save->write)
		{
			if (i >= numlines)
			{
				WRITEUINT16(save->p, 0xffff);
				break;
			}

			DiffLines(&lines[i], &spawnlines[i], diff);
			for (j = sizeof(diff)-1; j > 0; j--)
				if (diff[j])
					diff[j-1] |= 0x80;
			if (diff[0] == 0)
				continue;
		}

		SYNCAS(UINT16, i);

		if (i == 0xffff)
			break;

		if (i >= numlines)
			I_Error("Invalid line number %zu from server (expected end at %zu)", i, numlines);

		li = &lines[i];
		j = 0;
		do
			SYNC(diff[j]);
		while (diff[j++] & 0x80);

		SYNCF(LD_FLAG, li->flags);
		SYNCF(LD_SPECIAL, li->special);
		if (GETB(LD_TAG))
		{
			if (save->write)
			{
				WRITEUINT32(save->p, li->tags.count);
				for (j = 0; j < li->tags.count; j++)
					WRITEINT16(save->p, li->tags.tags[j]);
			}
			else
			{
				size_t ncount = READUINT32(save->p);

				// Remove entries from global lists.
				for (j = 0; j < lines[i].tags.count; j++)
					Taggroup_Remove(tags_lines, lines[i].tags.tags[j], i);

				// Reallocate if size differs.
				if (ncount != lines[i].tags.count)
				{
					lines[i].tags.count = ncount;
					lines[i].tags.tags = (mtag_t*)Z_Realloc(lines[i].tags.tags, ncount*sizeof(mtag_t), PU_LEVEL, NULL);
				}

				for (j = 0; j < ncount; j++)
					lines[i].tags.tags[j] = READINT16(save->p);

				// Add new entries.
				for (j = 0; j < lines[i].tags.count; j++)
					Taggroup_Add(tags_lines, lines[i].tags.tags[j], i);
			}
		}

		si = &sides[li->sidenum[0]];
		SYNCF(LD_S1TEXOFF, si->textureoffset);
		SYNCF(LD_S1TOPTEX, si->toptexture);
		SYNCF(LD_S1BOTTEX, si->bottomtexture);
		SYNCF(LD_S1MIDTEX, si->midtexture);

		si = &sides[li->sidenum[1]];
		SYNCF(LD_S2TEXOFF, si->textureoffset);
		SYNCF(LD_S2TOPTEX, si->toptexture);
		SYNCF(LD_S2BOTTEX, si->bottomtexture);
		SYNCF(LD_S2MIDTEX, si->midtexture);
		if (GETB(LD_ARGS))
		{
			for (j = 0; j < NUM_SCRIPT_ARGS; j++)
				SYNC(li->args[j]);
		}
		if (GETB(LD_STRINGARGS))
		{
			for (j = 0; j < NUM_SCRIPT_STRINGARGS; j++)
			{
				size_t len = P_SyncINT32(save, li->stringargs[j] ? strlen(li->stringargs[j]) : 0);
				if (!len)
				{
					if (!save->write)
					{
						Z_Free(li->stringargs[j]);
						li->stringargs[j] = NULL;
					}
					continue;
				}

				if (!save->write)
					li->stringargs[j] = Z_Realloc(li->stringargs[j], len + 1, PU_LEVEL, NULL);
				P_SyncMem(save, li->stringargs[j], len);
				li->stringargs[j][len] = '\0';
			}
		}

		SYNCF(LD_ACTIVATION, li->activation);
	}
}

static void P_NetSyncWorld(savebuffer_t *save)
{
	TracyCZone(__zone, true);

	if (P_SyncUINT32(save, ARCHIVEBLOCK_WORLD) != ARCHIVEBLOCK_WORLD)
		I_Error("Bad $$$.sav at archive block World");

	// initialize colormap vars because paranoia
	ClearNetColormaps();

	// count the level's ffloors so that colormap loading can have an upper limit
	if (!save->write)
		for (size_t i = 0; i < numsectors; i++)
		{
			ffloor_t *rover;
			for (rover = sectors[i].ffloors; rover; rover = rover->next)
				num_ffloors++;
		}

	SyncSectors(save);
	SyncLines(save);
	if (save->write)
		R_ClearTextureNumCache(false);

	TracyCZoneEnd(__zone);
}

//
// Thinkers
//

static boolean P_ThingArgsEqual(const mobj_t *mobj, const mapthing_t *mapthing)
{
	UINT8 i;
	for (i = 0; i < NUM_MAPTHING_ARGS; i++)
		if (mobj->args[i] != mapthing->args[i])
			return false;

	for (i = 0; i < NUM_MAPTHING_STRINGARGS; i++)
	{
		if (mobj->stringargs[i] == NULL || mapthing->stringargs[i] == NULL)
		{
			if (mobj->stringargs[i] != mapthing->stringargs[i])
				return false;
		}
		else
		{
			if (strcmp(mobj->stringargs[i], mapthing->stringargs[i]))
				return false;
		}
	}

	return true;
}

static boolean P_ThingScriptEqual(const mobj_t *mobj, const mapthing_t *mapthing)
{
	UINT8 i;
	if (mobj->special != mapthing->special)
		return false;

	for (i = 0; i < NUM_SCRIPT_ARGS; i++)
		if (mobj->script_args[i] != mapthing->script_args[i])
			return false;

	for (i = 0; i < NUM_SCRIPT_STRINGARGS; i++)
	{
		if (!mobj->script_stringargs[i])
			return !mapthing->script_stringargs[i];

		if (!fastcmp(mobj->script_stringargs[i], mapthing->script_stringargs[i]))
			return false;
	}

	return true;
}

enum mobj_diff_t
{
	MD_SPAWNPOINT = 0<<5,
	MD_POS,
	MD_TYPE,
	MD_MOM,
	MD_RADIUS,
	MD_HEIGHT,
	MD_FLAGS,
	MD_HEALTH,
	MD_RTIME,
	MD_STATE,
	MD_TICS,
	MD_SPRITE,
	MD_FRAME,
	MD_EFLAGS,
	MD_PLAYER,
	MD_MOVEDIR,
	MD_MOVECOUNT,
	MD_THRESHOLD,
	MD_LASTLOOK,
	MD_TARGET,
	MD_TRACER,
	MD_FRICTION,
	MD_MOVEFACTOR,
	MD_FLAGS2,
	MD_FUSE,
	MD_WATERTOP,
	MD_WATERBOTTOM,
	MD_SCALE,
	MD_DSCALE,
	MD_ARGS,
	MD__UNUSED,

	MD2_CUSVAL = 1<<5,
	MD2_CVMEM,
	MD2_SKIN,
	MD2_COLOR,
	MD2_SCALESPEED,
	MD2_EXTVAL1,
	MD2_EXTVAL2,
	MD2_HNEXT,
	MD2_HPREV,
	MD2_FLOORROVER,
	MD2_CEILINGROVER,
	MD2_SLOPE,
	MD2_COLORIZED,
	MD2_MIRRORED,
	MD2_ROLLANGLE,
	MD2_SHADOWSCALE,
	MD2_RENDERFLAGS,
	MD2_TID,
	MD2_SPRITESCALE,
	MD2_SPRITEOFFSET,
	MD2_WORLDOFFSET,
	MD2_SPECIAL,
	MD2_FLOORSPRITESLOPE,
	MD2_DISPOFFSET,
	MD2_BOSS3CAP,
	MD2_WAYPOINTCAP,
	MD2_KITEMCAP,
	MD2_ITNEXT,
	MD2_LASTMOMZ,
	MD2_TERRAIN,
	MD2_LIGHTLEVEL,

	MD3_GRAVITY = 2<<5,
	MD3_MISCCAP,
	MD3_BAKEDOFFSET,
	MD3_EXTVAL3,
	MD3_LIFETIME,
	MD3_VOICE,

	MD__MAX
};

static mobjtype_t g_doomednum_to_mobjtype[UINT16_MAX+1];

static void CalculateDoomednumToMobjtype(void)
{
	memset(g_doomednum_to_mobjtype, MT_NULL, sizeof(g_doomednum_to_mobjtype));

	for (size_t i = 0; i < NUMMOBJTYPES; i++)
	{
		if (mobjinfo[i].doomednum > 0 && mobjinfo[i].doomednum <= UINT16_MAX)
		{
			g_doomednum_to_mobjtype[ mobjinfo[i].doomednum ] = i;
		}
	}
}

static void DiffMobj(const mobj_t *mobj, UINT32 diff[])
{
	if (mobj->spawnpoint && mobj->info->doomednum != -1)
	{
		// spawnpoint is not modified but we must save it since it is an identifier
		SETB(MD_SPAWNPOINT);

		if ((mobj->x != mobj->spawnpoint->x << FRACBITS) ||
			(mobj->y != mobj->spawnpoint->y << FRACBITS) ||
			(mobj->angle != FixedAngle(mobj->spawnpoint->angle*FRACUNIT)) ||
			(mobj->pitch != FixedAngle(mobj->spawnpoint->pitch*FRACUNIT)) ||
			(mobj->roll != FixedAngle(mobj->spawnpoint->roll*FRACUNIT)) )
			SETB(MD_POS);

		DIFF(mobj->info->doomednum != mobj->spawnpoint->type, MD_TYPE);
		DIFF(!P_ThingArgsEqual(mobj, mobj->spawnpoint), MD_ARGS);
		DIFF(!P_ThingScriptEqual(mobj, mobj->spawnpoint), MD2_SPECIAL);
	}
	else
	{
		size_t j;

		// not a map spawned thing, so make it from scratch
		SETB(MD_POS);
		SETB(MD_TYPE);

		for (j = 0; j < NUM_MAPTHING_ARGS; j++)
		{
			if (mobj->args[j] != 0)
			{
				SETB(MD_ARGS);
				break;
			}
		}

		for (j = 0; j < NUM_MAPTHING_STRINGARGS; j++)
		{
			if (mobj->stringargs[j] != NULL)
			{
				SETB(MD_ARGS);
				break;
			}
		}

		if (mobj->special != 0)
			SETB(MD2_SPECIAL);

		for (j = 0; j < NUM_SCRIPT_ARGS; j++)
		{
			if (mobj->script_args[j] != 0)
			{
				SETB(MD2_SPECIAL);
				break;
			}
		}

		for (j = 0; j < NUM_SCRIPT_STRINGARGS; j++)
		{
			if (mobj->stringargs[j] != NULL)
			{
				SETB(MD2_SPECIAL);
				break;
			}
		}
	}

	// not the default but the most probable
	DIFF(mobj->momx != 0 || mobj->momy != 0 || mobj->momz != 0 || mobj->pmomz != 0, MD_MOM);
	DIFF(mobj->radius != FixedMul(mapobjectscale, mobj->info->radius), MD_RADIUS);
	DIFF(mobj->height != FixedMul(mapobjectscale, mobj->info->height), MD_HEIGHT);
	DIFF(mobj->flags != mobj->info->flags, MD_FLAGS);
	DIFF(mobj->flags2, MD_FLAGS2);
	DIFF(mobj->health != mobj->info->spawnhealth, MD_HEALTH);
	DIFF(mobj->reactiontime != mobj->info->reactiontime, MD_RTIME);
	DIFF((statenum_t)(mobj->state-states) != mobj->info->spawnstate, MD_STATE);
	DIFF(mobj->tics != mobj->state->tics, MD_TICS);
	DIFF(mobj->sprite != mobj->state->sprite, MD_SPRITE);
	DIFF(mobj->sprite == SPR_PLAY && mobj->sprite2 != (mobj->state->frame&FF_FRAMEMASK), MD_SPRITE);
	DIFF(mobj->frame != mobj->state->frame, MD_FRAME);
	DIFF(mobj->anim_duration != (UINT16)mobj->state->var2, MD_FRAME);
	DIFF(mobj->eflags, MD_EFLAGS);
	DIFF(mobj->player, MD_PLAYER);

	DIFF(mobj->movedir, MD_MOVEDIR);
	DIFF(mobj->movecount, MD_MOVECOUNT);
	DIFF(mobj->threshold, MD_THRESHOLD);
	DIFF(mobj->lastlook != -1, MD_LASTLOOK);
	DIFF(mobj->target, MD_TARGET);
	DIFF(mobj->tracer, MD_TRACER);
	DIFF(mobj->friction != ORIG_FRICTION, MD_FRICTION);
	DIFF(mobj->movefactor != FRACUNIT, MD_MOVEFACTOR);
	DIFF(mobj->fuse, MD_FUSE);
	DIFF(mobj->watertop != INT32_MAX, MD_WATERTOP);
	DIFF(mobj->waterbottom, MD_WATERBOTTOM);
	DIFF(mobj->scale != mapobjectscale, MD_SCALE);
	DIFF(mobj->destscale != mobj->scale, MD_DSCALE);
	DIFF(mobj->scalespeed != mapobjectscale/12, MD2_SCALESPEED);

	DIFF(mobj->cusval, MD2_CUSVAL);
	DIFF(mobj->cvmem, MD2_CVMEM);
	DIFF(mobj->color, MD2_COLOR);
	DIFF(mobj->skin, MD2_SKIN);
	DIFF(mobj->extravalue1, MD2_EXTVAL1);
	DIFF(mobj->extravalue2, MD2_EXTVAL2);
	DIFF(mobj->hnext, MD2_HNEXT);
	DIFF(mobj->hprev, MD2_HPREV);
	DIFF(mobj->standingslope, MD2_SLOPE);
	DIFF(mobj->colorized, MD2_COLORIZED);
	DIFF(mobj->floorrover, MD2_FLOORROVER);
	DIFF(mobj->ceilingrover, MD2_CEILINGROVER);
	DIFF(mobj->mirrored, MD2_MIRRORED);
	DIFF(mobj->rollangle, MD2_ROLLANGLE);
	DIFF(mobj->shadowscale, MD2_SHADOWSCALE);
	DIFF(mobj->renderflags /*|| mobj->renderflags2*/, MD2_RENDERFLAGS);
	DIFF(mobj->tid != 0, MD2_TID);
	DIFF(mobj->spritexscale != FRACUNIT || mobj->spriteyscale != FRACUNIT, MD2_SPRITESCALE);
	DIFF(mobj->spritexoffset || mobj->spriteyoffset || mobj->rollingxoffset || mobj->rollingyoffset, MD2_SPRITEOFFSET);
	DIFF(mobj->sprxoff || mobj->spryoff || mobj->sprzoff, MD2_WORLDOFFSET);
	if (mobj->floorspriteslope)
	{
		pslope_t *slope = mobj->floorspriteslope;
		DIFF(slope->zangle || slope->zdelta || slope->xydirection
		|| slope->o.x || slope->o.y || slope->o.z
		|| slope->d.x || slope->d.y
		|| slope->normal.x || slope->normal.y
		|| (slope->normal.z != FRACUNIT),
			MD2_FLOORSPRITESLOPE);
	}
	DIFF(mobj->lightlevel, MD2_LIGHTLEVEL);
	DIFF(mobj->dispoffset, MD2_DISPOFFSET);
	DIFF(mobj == boss3cap, MD2_BOSS3CAP);
	DIFF(mobj == waypointcap, MD2_WAYPOINTCAP);
	DIFF(mobj == kitemcap, MD2_KITEMCAP);
	DIFF(mobj->itnext, MD2_ITNEXT);
	DIFF(mobj->lastmomz, MD2_LASTMOMZ);
	DIFF(mobj->terrain != NULL || mobj->terrainOverlay != NULL, MD2_TERRAIN);

	DIFF(mobj->gravity != FRACUNIT, MD3_GRAVITY);
	DIFF(mobj == misccap, MD3_MISCCAP);
	DIFF(mobj->bakexoff || mobj->bakeyoff || mobj->bakezoff || mobj->bakexpiv || mobj->bakeypiv || mobj->bakezpiv, MD3_BAKEDOFFSET);
	DIFF(mobj->extravalue3, MD3_EXTVAL3);
	DIFF(mobj->mobjlifetime, MD3_LIFETIME);
	DIFF(mobj->voice, MD3_VOICE);
}

boolean MobjIsNetSynced(mobj_t *mobj)
{
	// Ignore stationary hoops - these will be respawned from mapthings.
	if (mobj->type == MT_HOOP)
		return false;

	// These are NEVER saved.
	if (mobj->type == MT_HOOPCOLLIDE)
		return false;

	// This hoop has already been collected.
	if (mobj->type == MT_HOOPCENTER && mobj->threshold == 4242)
		return false;

	// MT_SPARK: used for debug stuff
	if (mobj->type == MT_SPARK)
		return false;

	// MT_FOLLOWERHORN: So it turns out hornmod is fundamentally incompatible with netsync
	if (mobj->type == MT_FOLLOWERHORN)
		return false;

	// This is a non-synched visual effect mobj
	if (mobj->flags2 & MF2_DONTSYNC)
		return false;

	return true;
}

static thinker_t *SyncMobjThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	mobj_t *mobj = (mobj_t *)th;
	UINT32 diff[(MD__MAX>>5)+1] = {0};
	size_t j;

	if (save->write)
	{
		if (!MobjIsNetSynced(mobj))
			return th;

		// Scrap all of that. If we're a hoop center, this is ALL we're saving.
		if (mobj->type == MT_HOOPCENTER)
			SETB(MD_SPAWNPOINT);
		else
			DiffMobj(mobj, diff);

		for (j = sizeof(diff)/sizeof(*diff)-1; j > 0; j--)
			if (diff[j])
				diff[j-1] |= 0x80000000;

		// already read by the client
		WRITEUINT8(save->p, type);
	}

	j = 0;
	do
		SYNC(diff[j]);
	while (diff[j++] & 0x80000000);

	if (GETB(MD_SPAWNPOINT))
	{
		if (save->write)
		{
			size_t z;

			for (z = 0; z < nummapthings; z++)
			{
				if (&mapthings[z] != mobj->spawnpoint)
					continue;
				WRITEUINT16(save->p, z);
				break;
			}
			if (mobj->type == MT_HOOPCENTER)
				return th;
		}
		else
		{
			UINT16 spawnpointnum = READUINT16(save->p);

			if (mapthings[spawnpointnum].type == 1713) // NiGHTS Hoop special case
			{
				P_SpawnHoop(&mapthings[spawnpointnum]);
				return NULL;
			}

			mobj = P_AllocateMobj();

			mobj->spawnpoint = &mapthings[spawnpointnum];
			mapthings[spawnpointnum].mobj = mobj;
		}
	}
	else if (!save->write)
		mobj = P_AllocateMobj();

	// declare this as a valid mobj as soon as possible.
	mobj->thinker.function = thinker;

	// manually link to thinkerlist, since the thinker isn't returned anymore
	if (!save->write)
		P_AddThinker(THINK_MOBJ, &mobj->thinker);

	SYNC(mobj->z); // Force this so 3dfloor problems don't arise.
	SYNC(mobj->floorz);
	SYNC(mobj->ceilingz);

	SYNCF(MD2_FLOORROVER, mobj->floorrover);
	SYNCF(MD2_CEILINGROVER, mobj->ceilingrover);

	if (GETB(MD_TYPE))
	{
		SYNC(mobj->type);
	}
	else if (!save->write)
	{
		mobjtype_t new_type = MT_NULL;
		if (mobj->spawnpoint)
		{
			new_type = g_doomednum_to_mobjtype[mobj->spawnpoint->type];
		}

		if (new_type <= MT_NULL || new_type >= NUMMOBJTYPES)
		{
			if (mobj->spawnpoint)
				CONS_Alert(CONS_ERROR, "Found mobj with unknown map thing doomednum %d\n", mobj->spawnpoint->type);
			else
				CONS_Alert(CONS_ERROR, "Found mobj with unknown map thing doomednum NULL\n");

			I_Error("Netsave corrupted");
		}

		mobj->type = new_type;
	}
	mobj->info = &mobjinfo[mobj->type];
	if (GETB(MD_POS))
	{
		SYNC(mobj->x);
		SYNC(mobj->y);
		SYNC(mobj->angle);
		SYNC(mobj->pitch);
		SYNC(mobj->roll);
	}
	else if (!save->write)
	{
		mobj->x = mobj->old_x = mobj->spawnpoint->x << FRACBITS;
		mobj->y = mobj->old_y = mobj->spawnpoint->y << FRACBITS;
		mobj->angle = mobj->old_angle = FixedAngle(mobj->spawnpoint->angle*FRACUNIT);
		mobj->pitch = mobj->old_pitch = FixedAngle(mobj->spawnpoint->pitch*FRACUNIT);
		mobj->roll = mobj->old_roll = FixedAngle(mobj->spawnpoint->roll*FRACUNIT);
	}

	SYNCF(MD_MOM, mobj->momx);
	SYNCF(MD_MOM, mobj->momy);
	SYNCF(MD_MOM, mobj->momz);
	SYNCF(MD_MOM, mobj->pmomz);

	SYNCDEF(MD_RADIUS, mobj->radius, FixedMul(mobj->info->radius, mapobjectscale));
	SYNCDEF(MD_HEIGHT, mobj->height, FixedMul(mobj->info->height, mapobjectscale));
	SYNCDEF(MD_FLAGS, mobj->flags, mobj->info->flags);
	SYNCF(MD_FLAGS2, mobj->flags2);
	SYNCDEF(MD_HEALTH, mobj->health, mobj->info->spawnhealth);
	SYNCDEF(MD_RTIME, mobj->reactiontime, mobj->info->reactiontime);
	SYNCDEF(MD_STATE, mobj->state, &states[mobj->info->spawnstate]);
	SYNCDEF(MD_TICS, mobj->tics, mobj->state->tics);
	SYNCDEF(MD_SPRITE, mobj->sprite, mobj->state->sprite);
	if (mobj->sprite == SPR_PLAY)
		SYNCDEF(MD_SPRITE, mobj->sprite2, mobj->state->frame & FF_FRAMEMASK);
	SYNCDEF(MD_FRAME, mobj->frame, mobj->state->frame);
	SYNCDEF(MD_FRAME, mobj->anim_duration, (UINT16)mobj->state->var2);
	SYNCF(MD_EFLAGS, mobj->eflags);
	if (GETB(MD_PLAYER))
	{
		SYNC(mobj->player);
		mobj->player->mo = mobj;
	}
	SYNCF(MD_MOVEDIR, mobj->movedir);
	SYNCF(MD_MOVECOUNT, mobj->movecount);
	SYNCF(MD_THRESHOLD, mobj->threshold);
	SYNCDEF(MD_LASTLOOK, mobj->lastlook, -1);
	RSYNCF(MD_TARGET, mobj->target);
	RSYNCF(MD_TRACER, mobj->tracer);
	SYNCDEF(MD_FRICTION, mobj->friction, ORIG_FRICTION);
	SYNCDEF(MD_MOVEFACTOR, mobj->movefactor, FRACUNIT);
	SYNCF(MD_FUSE, mobj->fuse);
	SYNCDEF(MD_WATERTOP, mobj->watertop, INT32_MAX);
	SYNCF(MD_WATERBOTTOM, mobj->waterbottom);
	SYNCDEF(MD_SCALE, mobj->scale, mapobjectscale);
	SYNCDEF(MD_DSCALE, mobj->destscale, mobj->scale);
	SYNCDEF(MD2_SCALESPEED, mobj->scalespeed, mapobjectscale/12);
	if (GETB(MD_ARGS))
	{
		for (j = 0; j < NUM_MAPTHING_ARGS; j++)
			SYNC(mobj->args[j]);

		for (j = 0; j < NUM_MAPTHING_STRINGARGS; j++)
		{
			size_t len = P_SyncINT32(save, mobj->stringargs[j] ? strlen(mobj->stringargs[j]) : 0);
			if (!len)
			{
				if (!save->write)
				{
					Z_Free(mobj->stringargs[j]);
					mobj->stringargs[j] = NULL;
				}
				continue;
			}

			if (!save->write)
				mobj->stringargs[j] = Z_Realloc(mobj->stringargs[j], len + 1, PU_LEVEL, NULL);
			P_SyncMem(save, mobj->stringargs[j], len);
			mobj->stringargs[j][len] = '\0';
		}
	}
	else if (!save->write && mobj->spawnpoint)
	{
		P_CopyMapThingBehaviorFieldsToMobj(mobj->spawnpoint, mobj);
	}

	SYNCF(MD2_CUSVAL, mobj->cusval);
	SYNCF(MD2_CVMEM, mobj->cvmem);
	if (GETB(MD2_SKIN))
	{
		if (save->write)
			WRITEUINT16(save->p, (UINT16)((skin_t *)mobj->skin - skins));
		else
			R_ApplySkin(mobj, &skins[READUINT16(save->p)]);
	}
	SYNCF(MD2_COLOR, mobj->color);
	SYNCF(MD2_EXTVAL1, mobj->extravalue1);
	SYNCF(MD2_EXTVAL2, mobj->extravalue2);
	RSYNCF(MD2_HNEXT, mobj->hnext);
	RSYNCF(MD2_HPREV, mobj->hprev);
	RSYNCF(MD2_ITNEXT, mobj->itnext);
	SYNCF(MD2_SLOPE, mobj->standingslope);
	SYNCFB(MD2_COLORIZED, mobj->colorized);
	SYNCFB(MD2_MIRRORED, mobj->mirrored);
	SYNCF(MD2_ROLLANGLE, mobj->rollangle);
	SYNCF(MD2_SHADOWSCALE, mobj->shadowscale);
	SYNCFB(MD2_SHADOWSCALE, mobj->whiteshadow);
	SYNCF(MD2_SHADOWSCALE, mobj->shadowcolor);
	if (GETB(MD2_RENDERFLAGS))
	{
		if (save->write)
		{
			UINT32 rf = mobj->renderflags;
			UINT32 q = rf & RF_DONTDRAW;

			if (q != RF_DONTDRAW // visible for more than one local player
			&& q != (RF_DONTDRAWP1|RF_DONTDRAWP2|RF_DONTDRAWP3)
			&& q != (RF_DONTDRAWP4|RF_DONTDRAWP1|RF_DONTDRAWP2)
			&& q != (RF_DONTDRAWP4|RF_DONTDRAWP1|RF_DONTDRAWP3)
			&& q != (RF_DONTDRAWP4|RF_DONTDRAWP2|RF_DONTDRAWP3))
				rf &= ~q;

			WRITEUINT32(save->p, rf);
		}
		else
		{
			mobj->renderflags = READUINT32(save->p);
		}
	}
#if 0
	SYNCF(MD2_RENDERFLAGS, mobj->renderflags2);
#endif
	SYNCF(MD2_TID, mobj->tid);
	SYNCDEF(MD2_SPRITESCALE, mobj->spritexscale, FRACUNIT);
	SYNCDEF(MD2_SPRITESCALE, mobj->spriteyscale, FRACUNIT);
	SYNCF(MD2_SPRITEOFFSET, mobj->spritexoffset);
	SYNCF(MD2_SPRITEOFFSET, mobj->spriteyoffset);
	SYNCF(MD2_SPRITEOFFSET, mobj->rollingxoffset);
	SYNCF(MD2_SPRITEOFFSET, mobj->rollingyoffset);
	SYNCF(MD2_WORLDOFFSET, mobj->sprxoff);
	SYNCF(MD2_WORLDOFFSET, mobj->spryoff);
	SYNCF(MD2_WORLDOFFSET, mobj->sprzoff);

	if (GETB(MD2_SPECIAL))
	{
		SYNC(mobj->special);

		for (j = 0; j < NUM_SCRIPT_ARGS; j++)
			SYNC(mobj->script_args[j]);

		for (j = 0; j < NUM_SCRIPT_STRINGARGS; j++)
		{
			size_t len = P_SyncINT32(save, mobj->script_stringargs[j] ? strlen(mobj->script_stringargs[j]) : 0);
			if (!len)
			{
				if (!save->write)
				{
					Z_Free(mobj->script_stringargs[j]);
					mobj->script_stringargs[j] = NULL;
				}
				continue;
			}

			if (!save->write)
				mobj->script_stringargs[j] = Z_Realloc(mobj->script_stringargs[j], len + 1, PU_LEVEL, NULL);
			P_SyncMem(save, mobj->script_stringargs[j], len);
			mobj->script_stringargs[j][len] = '\0';
		}
	}
	else if (!save->write && mobj->spawnpoint)
	{
		P_CopyMapThingSpecialFieldsToMobj(mobj->spawnpoint, mobj);
	}
	if (GETB(MD2_FLOORSPRITESLOPE))
	{
		pslope_t *slope = mobj->floorspriteslope;
		if (!save->write)
			slope = (pslope_t *)P_CreateFloorSpriteSlope(mobj);

		SYNC(slope->zdelta);
		SYNC(slope->zangle);
		SYNC(slope->xydirection);

		SYNC(slope->o.x);
		SYNC(slope->o.y);
		SYNC(slope->o.z);

		SYNC(slope->d.x);
		SYNC(slope->d.y);

		SYNC(slope->normal.x);
		SYNC(slope->normal.y);
		SYNC(slope->normal.z);

		if (!save->write)
			P_UpdateSlopeLightOffset(slope);
	}
	SYNCF(MD2_LIGHTLEVEL, mobj->lightlevel);
	SYNCDEF(MD2_DISPOFFSET, mobj->dispoffset, mobj->info->dispoffset);
	SYNCF(MD2_LASTMOMZ, mobj->lastmomz);

	SYNCF(MD2_TERRAIN, mobj->terrain);
	RSYNCF(MD2_TERRAIN, mobj->terrainOverlay);
	SYNCDEF(MD3_GRAVITY, mobj->gravity, FRACUNIT);
	SYNCF(MD3_BAKEDOFFSET, mobj->bakexoff);
	SYNCF(MD3_BAKEDOFFSET, mobj->bakeyoff);
	SYNCF(MD3_BAKEDOFFSET, mobj->bakezoff);
	SYNCF(MD3_BAKEDOFFSET, mobj->bakexpiv);
	SYNCF(MD3_BAKEDOFFSET, mobj->bakeypiv);
	SYNCF(MD3_BAKEDOFFSET, mobj->bakezpiv);
	SYNCF(MD3_EXTVAL3, mobj->extravalue3);
	SYNCF(MD3_LIFETIME, mobj->mobjlifetime);

	if (GETB(MD3_VOICE) && mobj->skin)
	{
		if (save->write)
			WRITEUINT16(save->p, (UINT16)(mobj->voice - &((skin_t *)(mobj->skin))->voices[0]));
		else
		{
			UINT16 savedvoice = READUINT16(save->p);

			if (savedvoice > ((skin_t *)(mobj->skin))->numvoices)
				savedvoice = 0;

			mobj->voice = &((skin_t *)(mobj->skin))->voices[savedvoice];
		}
	}

	if (!save->write)
	{
		// Reset some non-synch values
		mobj->sloperoll = 0;
		mobj->slopepitch = 0;

		// link tid set earlier
		P_AddThingTID(mobj);

		// set sprev, snext, bprev, bnext, subsector
		P_SetThingPosition(mobj);
	}

	SYNC(mobj->mobjnum);

	if (!save->write)
	{
		if (mobj->player)
		{
			if (mobj->eflags & MFE_VERTICALFLIP)
				mobj->player->viewz = mobj->z + mobj->height - mobj->player->viewheight;
			else
				mobj->player->viewz = mobj->player->mo->z + mobj->player->viewheight;
		}

		if (mobj->type == MT_SKYBOX && mobj->spawnpoint)
		{
			P_InitSkyboxPoint(mobj, mobj->spawnpoint);
		}

		if (GETB(MD2_BOSS3CAP))
			P_SetTarget(&boss3cap, mobj);

		if (GETB(MD2_WAYPOINTCAP))
			P_SetTarget(&waypointcap, mobj);

		if (GETB(MD2_KITEMCAP))
			P_SetTarget(&kitemcap, mobj);

		if (GETB(MD3_MISCCAP))
			P_SetTarget(&misccap, mobj);

		R_AddMobjInterpolator(mobj);
	}

	// don't allow the mobj's refcount to be reset by P_AddThinker
	// we might've already called P_SetTarget!
	return &mobj->thinker;
}

static thinker_t *SyncNoEnemiesThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	noenemies_t *ht = (void *)th;
	if (save->write)
	{
		WRITEUINT8(save->p, type);
	}
	else
	{
		ht = (noenemies_t*)Z_LevelPoolCalloc(sizeof (*ht));
		ht->thinker.alloctype = TAT_LEVELPOOL;
		ht->thinker.size = sizeof (*ht);
		ht->thinker.function = thinker;
	}
	SYNC(ht->sourceline);
	return &ht->thinker;
}

static thinker_t *SyncBounceCheeseThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	bouncecheese_t *ht = (void *)th;
	if (save->write)
	{
		WRITEUINT8(save->p, type);
	}
	else
	{
		ht = (bouncecheese_t*)Z_LevelPoolCalloc(sizeof (*ht));
		ht->thinker.alloctype = TAT_LEVELPOOL;
		ht->thinker.size = sizeof (*ht);
		ht->thinker.function = thinker;
	}

	SYNC(ht->sourceline);
	SYNC(ht->sector);
	SYNC(ht->speed);
	SYNC(ht->distance);
	SYNC(ht->floorwasheight);
	SYNC(ht->ceilingwasheight);
	SYNCBOOLEAN(ht->low);
	if (!save->write && ht->sector)
		ht->sector->ceilingdata = ht;
	return &ht->thinker;
}

static thinker_t *SyncContinuousFallThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	continuousfall_t *ht  = (void *)th;
	if (save->write)
	{
		WRITEUINT8(save->p, type);
	}
	else
	{
		ht = (continuousfall_t*)Z_LevelPoolCalloc(sizeof (*ht));
		ht->thinker.alloctype = TAT_LEVELPOOL;
		ht->thinker.size = sizeof (*ht);
		ht->thinker.function = thinker;
	}

	SYNC(ht->sector);
	SYNC(ht->speed);
	SYNC(ht->direction);
	SYNC(ht->floorstartheight);
	SYNC(ht->ceilingstartheight);
	SYNC(ht->destheight);
	if (!save->write && ht->sector)
	{
		ht->sector->ceilingdata = ht;
		ht->sector->floordata = ht;
	}
	return &ht->thinker;
}

static thinker_t *SyncMarioBlockThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	mariothink_t *ht = (void *)th;
	if (save->write)
	{
		WRITEUINT8(save->p, type);
	}
	else
	{
		ht = (mariothink_t*)Z_LevelPoolCalloc(sizeof (*ht));
		ht->thinker.alloctype = TAT_LEVELPOOL;
		ht->thinker.size = sizeof (*ht);
		ht->thinker.function = thinker;
	}

	SYNC(ht->sector);
	SYNC(ht->speed);
	SYNC(ht->direction);
	SYNC(ht->floorstartheight);
	SYNC(ht->ceilingstartheight);
	SYNC(ht->tag);
	if (!save->write && ht->sector)
	{
		ht->sector->ceilingdata = ht;
		ht->sector->floordata = ht;
	}
	return &ht->thinker;
}

static thinker_t *SyncMarioCheckThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	mariocheck_t *ht = (void *)th;
	if (save->write)
	{
		WRITEUINT8(save->p, type);
	}
	else
	{
		ht = (mariocheck_t*)Z_LevelPoolCalloc(sizeof (*ht));
		ht->thinker.alloctype = TAT_LEVELPOOL;
		ht->thinker.size = sizeof (*ht);
		ht->thinker.function = thinker;
	}

	SYNC(ht->sourceline);
	SYNC(ht->sector);
	return &ht->thinker;
}

static thinker_t *SyncThwompThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	thwomp_t *ht = (void *)th;
	if (save->write)
	{
		WRITEUINT8(save->p, type);
	}
	else
	{
		ht = (thwomp_t*)Z_LevelPoolCalloc(sizeof (*ht));
		ht->thinker.alloctype = TAT_LEVELPOOL;
		ht->thinker.size = sizeof (*ht);
		ht->thinker.function = thinker;
	}

	SYNC(ht->sourceline);
	SYNC(ht->sector);
	SYNC(ht->crushspeed);
	SYNC(ht->retractspeed);
	SYNC(ht->direction);
	SYNC(ht->floorstartheight);
	SYNC(ht->ceilingstartheight);
	SYNC(ht->delay);
	SYNC(ht->tag);
	SYNC(ht->sound);
	SYNC(ht->initDelay);
	if (ht->sector)
	{
		ht->sector->ceilingdata = ht;
		ht->sector->floordata = ht;
	}
	return &ht->thinker;
}

static thinker_t *SyncFloatThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	floatthink_t *ht = (void *)th;
	if (save->write)
	{
		WRITEUINT8(save->p, type);
	}
	else
	{
		ht = (floatthink_t*)Z_LevelPoolCalloc(sizeof (*ht));
		ht->thinker.alloctype = TAT_LEVELPOOL;
		ht->thinker.size = sizeof (*ht);
		ht->thinker.function = thinker;
	}

	SYNC(ht->sourceline);
	SYNC(ht->sector);
	SYNC(ht->tag);
	return &ht->thinker;
}

static thinker_t *SyncEachTimeThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	eachtime_t *ht = (void *)th;
	if (save->write)
	{
		WRITEUINT8(save->p, type);
	}
	else
	{
		ht = (eachtime_t*)Z_LevelPoolCalloc(sizeof (*ht));
		ht->thinker.alloctype = TAT_LEVELPOOL;
		ht->thinker.size = sizeof (*ht);
		ht->thinker.function = thinker;
	}

	size_t i;
	SYNC(ht->sourceline);
	for (i = 0; i < MAXPLAYERS; i++)
	{
		SYNCBOOLEAN(ht->playersInArea[i]);
	}
	SYNCBOOLEAN(ht->triggerOnExit);
	return &ht->thinker;
}

static thinker_t *SyncRaiseThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	raise_t *ht = (void *)th;
	if (save->write)
	{
		WRITEUINT8(save->p, type);
	}
	else
	{
		ht = (raise_t*)Z_LevelPoolCalloc(sizeof (*ht));
		ht->thinker.alloctype = TAT_LEVELPOOL;
		ht->thinker.size = sizeof (*ht);
		ht->thinker.function = thinker;
	}

	SYNC(ht->tag);
	SYNC(ht->sector);
	SYNC(ht->ceilingbottom);
	SYNC(ht->ceilingtop);
	SYNC(ht->basespeed);
	SYNC(ht->extraspeed);
	SYNC(ht->shaketimer);
	SYNC(ht->flags);
	return &ht->thinker;
}

static thinker_t *SyncCeilingThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	ceiling_t *ht = (void *)th;
	if (save->write)
	{
		WRITEUINT8(save->p, type);
	}
	else
	{
		ht = (ceiling_t*)Z_LevelPoolCalloc(sizeof (*ht));
		ht->thinker.alloctype = TAT_LEVELPOOL;
		ht->thinker.size = sizeof (*ht);
		ht->thinker.function = thinker;
	}

	SYNC(ht->type);
	SYNC(ht->sector);
	SYNC(ht->bottomheight);
	SYNC(ht->topheight);
	SYNC(ht->speed);
	SYNC(ht->delay);
	SYNC(ht->delaytimer);
	SYNC(ht->crush);
	SYNC(ht->texture);
	SYNC(ht->direction);
	SYNC(ht->tag);
	SYNC(ht->sourceline);
	SYNC(ht->origspeed);
	SYNC(ht->crushHeight);
	SYNC(ht->crushSpeed);
	SYNC(ht->returnHeight);
	SYNC(ht->returnSpeed);
	if (!save->write && ht->sector)
		ht->sector->ceilingdata = ht;

	return &ht->thinker;
}

static thinker_t *SyncFloormoveThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	floormove_t *ht = (void *)th;
	if (save->write)
	{
		WRITEUINT8(save->p, type);
	}
	else
	{
		ht = (floormove_t*)Z_LevelPoolCalloc(sizeof (*ht));
		ht->thinker.alloctype = TAT_LEVELPOOL;
		ht->thinker.size = sizeof (*ht);
		ht->thinker.function = thinker;
	}

	SYNC(ht->type);
	SYNC(ht->crush);
	SYNC(ht->sector);
	SYNC(ht->direction);
	SYNC(ht->texture);
	SYNC(ht->floordestheight);
	SYNC(ht->speed);
	SYNC(ht->origspeed);
	SYNC(ht->delay);
	SYNC(ht->delaytimer);
	SYNC(ht->tag);
	SYNC(ht->sourceline);
	SYNC(ht->crushHeight);
	SYNC(ht->crushSpeed);
	SYNC(ht->returnHeight);
	SYNC(ht->returnSpeed);
	return &ht->thinker;
}

static thinker_t *SyncLightflashThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	lightflash_t *ht = (void *)th;
	if (save->write)
	{
		WRITEUINT8(save->p, type);
	}
	else
	{
		ht = (lightflash_t*)Z_LevelPoolCalloc(sizeof (*ht));
		ht->thinker.alloctype = TAT_LEVELPOOL;
		ht->thinker.size = sizeof (*ht);
		ht->thinker.function = thinker;
	}
	SYNC(ht->sector);
	SYNC(ht->maxlight);
	SYNC(ht->minlight);
	if (!save->write && ht->sector)
		ht->sector->lightingdata = ht;
	return &ht->thinker;
}

static thinker_t *SyncStrobeThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	strobe_t *ht = (void *)th;
	if (save->write)
	{
		WRITEUINT8(save->p, type);
	}
	else
	{
		ht = (strobe_t*)Z_LevelPoolCalloc(sizeof (*ht));
		ht->thinker.alloctype = TAT_LEVELPOOL;
		ht->thinker.size = sizeof (*ht);
		ht->thinker.function = thinker;
	}

	SYNC(ht->sector);
	SYNC(ht->count);
	SYNC(ht->minlight);
	SYNC(ht->maxlight);
	SYNC(ht->darktime);
	SYNC(ht->brighttime);
	if (!save->write && ht->sector)
		ht->sector->lightingdata = ht;
	return &ht->thinker;
}

static thinker_t *SyncGlowThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	glow_t *ht = (void *)th;
	if (save->write)
	{
		WRITEUINT8(save->p, type);
	}
	else
	{
		ht = (glow_t*)Z_LevelPoolCalloc(sizeof (*ht));
		ht->thinker.alloctype = TAT_LEVELPOOL;
		ht->thinker.size = sizeof (*ht);
		ht->thinker.function = thinker;
	}

	SYNC(ht->sector);
	SYNC(ht->minlight);
	SYNC(ht->maxlight);
	SYNC(ht->direction);
	SYNC(ht->speed);
	if (!save->write && ht->sector)
		ht->sector->lightingdata = ht;
	return &ht->thinker;
}

static thinker_t *SyncFireflickerThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	fireflicker_t *ht = (void *)th;
	if (save->write)
	{
		WRITEUINT8(save->p, type);
	}
	else
	{
		ht = (fireflicker_t*)Z_LevelPoolCalloc(sizeof (*ht));
		ht->thinker.alloctype = TAT_LEVELPOOL;
		ht->thinker.size = sizeof (*ht);
		ht->thinker.function = thinker;
	}

	SYNC(ht->sector);
	SYNC(ht->count);
	SYNC(ht->resetcount);
	SYNC(ht->maxlight);
	SYNC(ht->minlight);
	if (!save->write && ht->sector)
		ht->sector->lightingdata = ht;
	return &ht->thinker;
}

static thinker_t *SyncElevatorThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	boolean setplanedata = thinker == (actionf_p1)T_MoveElevator;
	elevator_t *ht = (void *)th;
	if (save->write)
	{
		WRITEUINT8(save->p, type);
	}
	else
	{
		ht = (elevator_t*)Z_LevelPoolCalloc(sizeof (*ht));
		ht->thinker.alloctype = TAT_LEVELPOOL;
		ht->thinker.size = sizeof (*ht);
		ht->thinker.function = thinker;
	}

	SYNC(ht->type);
	SYNC(ht->sector);
	SYNC(ht->actionsector);
	SYNC(ht->direction);
	SYNC(ht->floordestheight);
	SYNC(ht->ceilingdestheight);
	SYNC(ht->speed);
	SYNC(ht->origspeed);
	SYNC(ht->low);
	SYNC(ht->high);
	SYNC(ht->distance);
	SYNC(ht->delay);
	SYNC(ht->delaytimer);
	SYNC(ht->floorwasheight);
	SYNC(ht->ceilingwasheight);

	if (!save->write && ht->sector && setplanedata)
	{
		ht->sector->ceilingdata = ht;
		ht->sector->floordata = ht;
	}
	return &ht->thinker;
}

static thinker_t *SyncCrumbleThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	crumble_t *ht = (void *)th;
	if (save->write)
	{
		WRITEUINT8(save->p, type);
	}
	else
	{
		ht = (crumble_t*)Z_LevelPoolCalloc(sizeof (*ht));
		ht->thinker.alloctype = TAT_LEVELPOOL;
		ht->thinker.size = sizeof (*ht);
		ht->thinker.function = thinker;
	}

	SYNC(ht->sourceline);
	SYNC(ht->sector);
	SYNC(ht->actionsector);
	SYNC(ht->player);
	SYNC(ht->direction);
	SYNC(ht->origalpha);
	SYNC(ht->timer);
	SYNC(ht->speed);
	SYNC(ht->floorwasheight);
	SYNC(ht->ceilingwasheight);
	SYNC(ht->flags);
	if (!save->write && ht->sector)
		ht->sector->lightingdata = ht;
	if (!save->write && ht->actionsector)
		ht->actionsector->lightingdata = ht;
	return &ht->thinker;
}

static thinker_t *SyncScrollThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	scroll_t *ht = (void *)th;
	if (save->write)
	{
		WRITEUINT8(save->p, type);
	}
	else
	{
		ht = (scroll_t*)Z_LevelPoolCalloc(sizeof (*ht));
		ht->thinker.alloctype = TAT_LEVELPOOL;
		ht->thinker.size = sizeof (*ht);
		ht->thinker.function = thinker;
	}

	SYNC(ht->dx);
	SYNC(ht->dy);
	SYNC(ht->affectee);
	SYNC(ht->control);
	SYNC(ht->last_height);
	SYNC(ht->vdx);
	SYNC(ht->vdy);
	SYNC(ht->accel);
	SYNC(ht->exclusive);
	SYNC(ht->type);
	return &ht->thinker;
}

static thinker_t *SyncFrictionThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	friction_t *ht = (void *)th;
	if (save->write)
	{
		WRITEUINT8(save->p, type);
	}
	else
	{
		ht = (friction_t*)Z_LevelPoolCalloc(sizeof (*ht));
		ht->thinker.alloctype = TAT_LEVELPOOL;
		ht->thinker.size = sizeof (*ht);
		ht->thinker.function = thinker;
	}

	SYNC(ht->friction);
	SYNC(ht->movefactor);
	SYNC(ht->affectee);
	SYNC(ht->referrer);
	SYNC(ht->roverfriction);
	return &ht->thinker;
}

static thinker_t *SyncPusherThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	pusher_t *ht = (void *)th;
	if (save->write)
	{
		WRITEUINT8(save->p, type);
	}
	else
	{
		ht = (pusher_t*)Z_LevelPoolCalloc(sizeof (*ht));
		ht->thinker.alloctype = TAT_LEVELPOOL;
		ht->thinker.size = sizeof (*ht);
		ht->thinker.function = thinker;
	}

	SYNC(ht->type);
	SYNC(ht->x_mag);
	SYNC(ht->y_mag);
	SYNC(ht->z_mag);
	SYNC(ht->affectee);
	SYNC(ht->roverpusher);
	SYNC(ht->referrer);
	SYNC(ht->exclusive);
	SYNC(ht->slider);
	return &ht->thinker;
}

static thinker_t *SyncLaserThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	laserthink_t *ht = (void *)th;
	if (save->write)
	{
		WRITEUINT8(save->p, type);
	}
	else
	{
		ht = (laserthink_t*)Z_LevelPoolCalloc(sizeof (*ht));
		ht->thinker.alloctype = TAT_LEVELPOOL;
		ht->thinker.size = sizeof (*ht);
		ht->thinker.function = thinker;
	}

	SYNC(ht->tag);
	SYNC(ht->sourceline);
	SYNC(ht->nobosses);
	return &ht->thinker;
}

static thinker_t *SyncLightlevelThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	lightlevel_t *ht = (void *)th;
	if (save->write)
	{
		WRITEUINT8(save->p, type);
	}
	else
	{
		ht = (lightlevel_t*)Z_LevelPoolCalloc(sizeof (*ht));
		ht->thinker.alloctype = TAT_LEVELPOOL;
		ht->thinker.size = sizeof (*ht);
		ht->thinker.function = thinker;
	}

	SYNC(ht->sector);
	SYNC(ht->sourcelevel);
	SYNC(ht->destlevel);
	SYNC(ht->fixedcurlevel);
	SYNC(ht->fixedpertic);
	SYNC(ht->timer);
	if (!save->write && ht->sector)
		ht->sector->lightingdata = ht;
	return &ht->thinker;
}

static thinker_t *SyncExecutorThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	executor_t *ht = (void *)th;
	if (save->write)
	{
		WRITEUINT8(save->p, type);
	}
	else
	{
		ht = (executor_t*)Z_LevelPoolCalloc(sizeof (*ht));
		ht->thinker.alloctype = TAT_LEVELPOOL;
		ht->thinker.size = sizeof (*ht);
		ht->thinker.function = thinker;
	}

	SYNC(ht->line);
	RSYNC(ht->caller);
	SYNC(ht->sector);
	SYNC(ht->timer);
	return &ht->thinker;
}

static thinker_t *SyncDisappearThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	disappear_t *ht = (void *)th;
	if (save->write)
	{
		WRITEUINT8(save->p, type);
	}
	else
	{
		ht = (disappear_t*)Z_LevelPoolCalloc(sizeof (*ht));
		ht->thinker.alloctype = TAT_LEVELPOOL;
		ht->thinker.size = sizeof (*ht);
		ht->thinker.function = thinker;
	}

	SYNC(ht->appeartime);
	SYNC(ht->disappeartime);
	SYNC(ht->offset);
	SYNC(ht->timer);
	SYNC(ht->affectee);
	SYNC(ht->sourceline);
	SYNC(ht->exists);
	return &ht->thinker;
}

static thinker_t *SyncFadeThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	fade_t *ht = (void *)th;
	if (save->write)
	{
		WRITEUINT8(save->p, type);
	}
	else
	{
		ht = (fade_t*)Z_LevelPoolCalloc(sizeof (*ht));
		ht->thinker.alloctype = TAT_LEVELPOOL;
		ht->thinker.size = sizeof (*ht);
		ht->thinker.function = thinker;
	}

	ht->dest_exc = GetNetColormapFromList(P_SyncUINT32(save, CheckAddNetColormapToList(ht->dest_exc)));
	SYNC(ht->sectornum);
	SYNC(ht->ffloornum);
	SYNC(ht->alpha);
	SYNC(ht->sourcevalue);
	SYNC(ht->destvalue);
	SYNC(ht->destlightlevel);
	SYNC(ht->speed);
	SYNCBOOLEAN(ht->ticbased);
	SYNC(ht->timer);
	SYNC(ht->doexists);
	SYNC(ht->dotranslucent);
	SYNC(ht->dolighting);
	SYNC(ht->docolormap);
	SYNCBOOLEAN(ht->docollision);
	SYNCBOOLEAN(ht->doghostfade);
	SYNC(ht->exactalpha);

	if (!save->write)
	{
		sector_t *ss = &sectors[ht->sectornum];
		if (ss)
		{
			size_t j = 0; // ss->ffloors is saved as ffloor #0, ss->ffloors->next is #1, etc
			ffloor_t *rover;
			for (rover = ss->ffloors; rover; rover = rover->next)
			{
				if (j == ht->ffloornum)
				{
					ht->rover = rover;
					rover->fadingdata = ht;
					break;
				}
				j++;
			}
		}
	}
	return &ht->thinker;
}

static thinker_t *SyncFadeColormapThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	fadecolormap_t *ht = (void *)th;
	if (save->write)
	{
		WRITEUINT8(save->p, type);
	}
	else
	{
		ht = (fadecolormap_t*)Z_LevelPoolCalloc(sizeof (*ht));
		ht->thinker.alloctype = TAT_LEVELPOOL;
		ht->thinker.size = sizeof (*ht);
		ht->thinker.function = thinker;
	}

	SYNC(ht->sector);
	ht->source_exc = GetNetColormapFromList(P_SyncUINT32(save, CheckAddNetColormapToList(ht->source_exc)));
	ht->dest_exc = GetNetColormapFromList(P_SyncUINT32(save, CheckAddNetColormapToList(ht->dest_exc)));
	SYNCBOOLEAN(ht->ticbased);
	SYNC(ht->duration);
	SYNC(ht->timer);
	if (ht->sector)
		ht->sector->fadecolormapdata = ht;
	return &ht->thinker;
}

static thinker_t *SyncPlaneDisplaceThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	planedisplace_t *ht = (void *)th;
	if (save->write)
	{
		WRITEUINT8(save->p, type);
	}
	else
	{
		ht = (planedisplace_t*)Z_LevelPoolCalloc(sizeof (*ht));
		ht->thinker.alloctype = TAT_LEVELPOOL;
		ht->thinker.size = sizeof (*ht);
		ht->thinker.function = thinker;
	}

	SYNC(ht->affectee);
	SYNC(ht->control);
	SYNC(ht->last_height);
	SYNC(ht->speed);
	SYNC(ht->type);
	return &ht->thinker;
}

static thinker_t *SyncDynamicLineSlopeThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	dynlineplanethink_t *ht = (void *)th;
	if (save->write)
	{
		WRITEUINT8(save->p, type);
	}
	else
	{
		ht = (dynlineplanethink_t*)Z_LevelPoolCalloc(sizeof (*ht));
		ht->thinker.alloctype = TAT_LEVELPOOL;
		ht->thinker.size = sizeof (*ht);
		ht->thinker.function = thinker;
	}

	SYNC(ht->type);
	SYNC(ht->slope);
	SYNC(ht->sourceline);
	SYNC(ht->extent);
	return &ht->thinker;
}

static thinker_t *SyncDynamicVertexSlopeThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	size_t i;
	dynvertexplanethink_t *ht = (void *)th;
	if (save->write)
	{
		WRITEUINT8(save->p, type);
	}
	else
	{
		ht = (dynvertexplanethink_t*)Z_LevelPoolCalloc(sizeof (*ht));
		ht->thinker.alloctype = TAT_LEVELPOOL;
		ht->thinker.size = sizeof (*ht);
		ht->thinker.function = thinker;
	}

	SYNC(ht->slope);
	for (i = 0; i < 3; i++)
		SYNC(ht->secs[i]);
	P_SyncMem(save, ht->vex, sizeof(ht->vex));
	P_SyncMem(save, ht->origsecheights, sizeof(ht->origsecheights));
	P_SyncMem(save, ht->origvecheights, sizeof(ht->origvecheights));
	SYNC(ht->relative);
	return &ht->thinker;
}

static thinker_t *SyncPolyrotatetThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	polyrotate_t *ht = (void *)th;
	if (save->write)
	{
		WRITEUINT8(save->p, type);
	}
	else
	{
		ht = (polyrotate_t*)Z_LevelPoolCalloc(sizeof (*ht));
		ht->thinker.alloctype = TAT_LEVELPOOL;
		ht->thinker.size = sizeof (*ht);
		ht->thinker.function = thinker;
	}

	SYNC(ht->polyObjNum);
	SYNC(ht->speed);
	SYNC(ht->distance);
	SYNC(ht->turnobjs);
	return &ht->thinker;
}

static thinker_t *SyncPolymoveThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	polymove_t *ht = (void *)th;
	if (save->write)
	{
		WRITEUINT8(save->p, type);
	}
	else
	{
		ht = (polymove_t*)Z_LevelPoolCalloc(sizeof (*ht));
		ht->thinker.alloctype = TAT_LEVELPOOL;
		ht->thinker.size = sizeof (*ht);
		ht->thinker.function = thinker;
	}

	SYNC(ht->polyObjNum);
	SYNC(ht->speed);
	SYNC(ht->momx);
	SYNC(ht->momy);
	SYNC(ht->distance);
	SYNC(ht->angle);
	return &ht->thinker;
}

static thinker_t *SyncPolywaypointThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	polywaypoint_t *ht = (void *)th;
	if (save->write)
	{
		WRITEUINT8(save->p, type);
	}
	else
	{
		ht = (polywaypoint_t*)Z_LevelPoolCalloc(sizeof (*ht));
		ht->thinker.alloctype = TAT_LEVELPOOL;
		ht->thinker.size = sizeof (*ht);
		ht->thinker.function = thinker;
	}

	SYNC(ht->polyObjNum);
	SYNC(ht->speed);
	SYNC(ht->sequence);
	SYNC(ht->pointnum);
	SYNC(ht->direction);
	SYNC(ht->returnbehavior);
	SYNC(ht->continuous);
	SYNC(ht->stophere);
	return &ht->thinker;
}

static thinker_t *SyncPolyslidedoorThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	polyslidedoor_t *ht = (void *)th;
	if (save->write)
	{
		WRITEUINT8(save->p, type);
	}
	else
	{
		ht = (polyslidedoor_t*)Z_LevelPoolCalloc(sizeof (*ht));
		ht->thinker.alloctype = TAT_LEVELPOOL;
		ht->thinker.size = sizeof (*ht);
		ht->thinker.function = thinker;
	}

	SYNC(ht->polyObjNum);
	SYNC(ht->delay);
	SYNC(ht->delayCount);
	SYNC(ht->initSpeed);
	SYNC(ht->speed);
	SYNC(ht->initDistance);
	SYNC(ht->distance);
	SYNC(ht->initAngle);
	SYNC(ht->angle);
	SYNC(ht->revAngle);
	SYNC(ht->momx);
	SYNC(ht->momy);
	SYNC(ht->closing);
	return &ht->thinker;
}

static thinker_t *SyncPolyswingdoorThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	polyswingdoor_t *ht = (void *)th;
	if (save->write)
	{
		WRITEUINT8(save->p, type);
	}
	else
	{
		ht = (polyswingdoor_t*)Z_LevelPoolCalloc(sizeof (*ht));
		ht->thinker.alloctype = TAT_LEVELPOOL;
		ht->thinker.size = sizeof (*ht);
		ht->thinker.function = thinker;
	}

	SYNC(ht->polyObjNum);
	SYNC(ht->delay);
	SYNC(ht->delayCount);
	SYNC(ht->initSpeed);
	SYNC(ht->speed);
	SYNC(ht->initDistance);
	SYNC(ht->distance);
	SYNC(ht->closing);
	return &ht->thinker;
}

static thinker_t *SyncPolydisplaceThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	polydisplace_t *ht = (void *)th;
	if (save->write)
	{
		WRITEUINT8(save->p, type);
	}
	else
	{
		ht = (polydisplace_t*)Z_LevelPoolCalloc(sizeof (*ht));
		ht->thinker.alloctype = TAT_LEVELPOOL;
		ht->thinker.size = sizeof (*ht);
		ht->thinker.function = thinker;
	}

	SYNC(ht->polyObjNum);
	SYNC(ht->controlSector);
	SYNC(ht->dx);
	SYNC(ht->dy);
	SYNC(ht->oldHeights);
	return &ht->thinker;
}

static thinker_t *SyncPolyrotdisplaceThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	polyrotdisplace_t *ht = (void *)th;
	if (save->write)
	{
		WRITEUINT8(save->p, type);
	}
	else
	{
		ht = (polyrotdisplace_t*)Z_LevelPoolCalloc(sizeof (*ht));
		ht->thinker.alloctype = TAT_LEVELPOOL;
		ht->thinker.size = sizeof (*ht);
		ht->thinker.function = thinker;
	}

	SYNC(ht->polyObjNum);
	SYNC(ht->controlSector);
	SYNC(ht->rotscale);
	SYNC( ht->turnobjs);
	SYNC(ht->oldHeights);
	return &ht->thinker;
}

static thinker_t *SyncPolyfadeThinker(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type)
{
	polyfade_t *ht = (void *)th;
	if (save->write)
	{
		WRITEUINT8(save->p, type);
	}
	else
	{
		ht = (polyfade_t*)Z_LevelPoolCalloc(sizeof (*ht));
		ht->thinker.alloctype = TAT_LEVELPOOL;
		ht->thinker.size = sizeof (*ht);
		ht->thinker.function = thinker;
	}

	SYNC(ht->polyObjNum);
	SYNC(ht->sourcevalue);
	SYNC(ht->destvalue);
	SYNCBOOLEAN(ht->docollision);
	SYNCBOOLEAN(ht->doghostfade);
	SYNCBOOLEAN(ht->ticbased);
	SYNC(ht->duration);
	SYNC(ht->timer);
	return &ht->thinker;
}

typedef thinker_t *thinkersync_f(savebuffer_t *save, actionf_p1 thinker, thinker_t *th, UINT8 type);

#define ITER_THINKERS \
	_(P_MobjThinker,        SyncMobjThinker,               tc_mobj) \
	_(T_MoveCeiling,        SyncCeilingThinker,            tc_ceiling) \
	_(T_MoveFloor,          SyncFloormoveThinker,          tc_floor) \
	_(T_LightningFlash,     SyncLightflashThinker,         tc_flash) \
	_(T_StrobeFlash,        SyncStrobeThinker,             tc_strobe) \
	_(T_Glow,               SyncGlowThinker,               tc_glow) \
	_(T_FireFlicker,        SyncFireflickerThinker,        tc_fireflicker) \
	_(T_ThwompSector,       SyncThwompThinker,             tc_thwomp) \
	_(T_CameraScanner,      SyncElevatorThinker,           tc_camerascanner) \
	_(T_MoveElevator,       SyncElevatorThinker,           tc_elevator) \
	_(T_ContinuousFalling,  SyncContinuousFallThinker,     tc_continuousfalling) \
	_(T_BounceCheese,       SyncBounceCheeseThinker,       tc_bouncecheese) \
	_(T_StartCrumble,       SyncCrumbleThinker,            tc_startcrumble) \
	_(T_MarioBlock,         SyncMarioBlockThinker,         tc_marioblock) \
	_(T_MarioBlockChecker,  SyncMarioCheckThinker,         tc_marioblockchecker) \
	_(T_FloatSector,        SyncFloatThinker,              tc_floatsector) \
	_(T_CrushCeiling,       SyncCeilingThinker,            tc_crushceiling) \
	_(T_Scroll,             SyncScrollThinker,             tc_scroll) \
	_(T_Friction,           SyncFrictionThinker,           tc_friction) \
	_(T_Pusher,             SyncPusherThinker,             tc_pusher) \
	_(T_LaserFlash,         SyncLaserThinker,              tc_laserflash) \
	_(T_LightFade,          SyncLightlevelThinker,         tc_lightfade) \
	_(T_ExecutorDelay,      SyncExecutorThinker,           tc_executor) \
	_(T_RaiseSector,        SyncRaiseThinker,              tc_raisesector) \
	_(T_NoEnemiesSector,    SyncNoEnemiesThinker,          tc_noenemies) \
	_(T_EachTimeThinker,    SyncEachTimeThinker,           tc_eachtime) \
	_(T_Disappear,          SyncDisappearThinker,          tc_disappear) \
	_(T_Fade,               SyncFadeThinker,               tc_fade) \
	_(T_FadeColormap,       SyncFadeColormapThinker,       tc_fadecolormap) \
	_(T_PlaneDisplace,      SyncPlaneDisplaceThinker,      tc_planedisplace) \
	_(T_DynamicSlopeLine,   SyncDynamicLineSlopeThinker,   tc_dynslopeline) \
	_(T_DynamicSlopeVert,   SyncDynamicVertexSlopeThinker, tc_dynslopevert) \
	_(T_PolyObjRotate,      SyncPolyrotatetThinker,        tc_polyrotate) \
	_(T_PolyObjMove,        SyncPolymoveThinker,           tc_polymove) \
	_(T_PolyObjWaypoint,    SyncPolywaypointThinker,       tc_polywaypoint) \
	_(T_PolyDoorSlide,      SyncPolyslidedoorThinker,      tc_polyslidedoor) \
	_(T_PolyDoorSwing,      SyncPolyswingdoorThinker,      tc_polyswingdoor) \
	_(T_PolyObjFlag,        SyncPolymoveThinker,           tc_polyflag) \
	_(T_PolyObjDisplace,    SyncPolydisplaceThinker,       tc_polydisplace) \
	_(T_PolyObjRotDisplace, SyncPolyrotdisplaceThinker,    tc_polyrotdisplace) \
	_(T_PolyObjFade,        SyncPolyfadeThinker,           tc_polyfade ) \

typedef enum
{
#define _(think, sync, tc) tc,
	ITER_THINKERS
#undef _
	tc_end
} specials_e;

static const actionf_p1 actionspecials[tc_end] =
{
#define _(think, sync, tc) (actionf_p1)think,
	ITER_THINKERS
#undef _
};

static void P_NetSyncThinkers(savebuffer_t *save)
{
	TracyCZone(__zone, true);

	thinker_t *currentthinker;
	thinker_t *next;
	boolean restoreNum = false;
	UINT32 i;

	if (P_SyncUINT32(save, ARCHIVEBLOCK_THINKERS) != ARCHIVEBLOCK_THINKERS)
		I_Error("Bad $$$.sav at archive block Thinkers");

	if (!save->write)
	{
		// Pre-calculate this lookup, because it was wasting
		// a shit ton of time loading mobj thinkers.
		CalculateDoomednumToMobjtype();

		// remove all the current thinkers
		for (i = 0; i < NUM_THINKERLISTS; i++)
		{
			for (currentthinker = thlist[i].next; currentthinker != &thlist[i]; currentthinker = next)
			{
				next = currentthinker->next;

				currentthinker->references = 0; // Heinous but this is the only place the assertion in P_UnlinkThinkers is wrong

				if (currentthinker->function == (actionf_p1)P_MobjThinker || currentthinker->function == (actionf_p1)P_NullPrecipThinker)
					P_RemoveSavegameMobj((mobj_t *)currentthinker); // item isn't saved, don't remove it
				else
				{
					(next->prev = currentthinker->prev)->next = next;
					R_DestroyLevelInterpolators(currentthinker);
					if (currentthinker->alloctype == TAT_LEVELPOOL)
					{
						Z_LevelPoolFree(currentthinker, currentthinker->size);
					}
					else
					{
						Z_Free(currentthinker);
					}
				}
			}
		}

		// we don't want the removed mobjs to come back
		P_InitThinkers();

		// Oh my god don't blast random memory with our reference counts.
		waypointcap = kitemcap = NULL;
		for (i = 0; i <= 15; i++)
		{
			skyboxcenterpnts[i] = skyboxviewpnts[i] = NULL;
		}

		// clear sector thinker pointers so they don't point to non-existant thinkers for all of eternity
		for (i = 0; i < numsectors; i++)
		{
			sectors[i].floordata = sectors[i].ceilingdata = sectors[i].lightingdata = sectors[i].fadecolormapdata = NULL;
		}
	}

	for (i = 0; i < NUM_THINKERLISTS; i++)
	{
		thinker_t* th = &thlist[i];
		UINT32 numloaded = 0;
		for (;;)
		{
			actionf_p1 acp;
			if (save->write)
			{
				th = th->next;
				if (th == &thlist[i])
				{
					WRITEUINT8(save->p, tc_end);
					break;
				}

				acp = th->function;
				if (acp == (actionf_p1)P_NullPrecipThinker || acp == (actionf_p1)P_RemoveThinkerDelayed)
					continue;
			}
			else
			{
				UINT8 tclass = READUINT8(save->p); // NOTE: this is normally written within the sync functions
				if (tclass == tc_end)
					break;
				else if (tclass > tc_end)
					I_Error("Invalid thinker class %d", tclass);

				th = NULL;
				acp = actionspecials[tclass];
				if (acp == (actionf_p1)T_ExecutorDelay)
					restoreNum = true;
				else if (acp == (actionf_p1)P_MobjThinker && i != THINK_MOBJ)
					I_Error("P_MobjThinker in non-THINK_MOBJ list");
			}

			if (false);
#define _(think, sync, tc) else if (acp == (actionf_p1)think) th = sync(save, acp, th, tc);
			ITER_THINKERS
#undef _
			else
				I_Error("Unknown thinker type");

			numloaded++;

			if (!save->write && th && acp != (actionf_p1)P_MobjThinker)
				P_AddThinker(i, th);
		}

		CONS_Debug(DBG_NETPLAY, "%u thinkers synchronized in list %d\n", numloaded, i);
	}

	if (!save->write)
	{
		// Set each skyboxmo to the first skybox (or NULL)
		skyboxmo[0] = skyboxviewpnts[0];
		skyboxmo[1] = skyboxcenterpnts[0];

		if (restoreNum)
		{
			executor_t *delay;
			for (currentthinker = thlist[THINK_MAIN].next; currentthinker != &thlist[THINK_MAIN]; currentthinker = currentthinker->next)
			{
				if (currentthinker->function != (actionf_p1)T_ExecutorDelay)
					continue;
				delay = (executor_t *)currentthinker;
				RELINK(&delay->caller);
			}
		}
	}

	TracyCZoneEnd(__zone);
}

static void P_NetSyncWaypoints(savebuffer_t *save)
{
	TracyCZone(__zone, true);

	waypoint_t *waypoint;
	UINT32 i;
	UINT32 numWaypoints = K_GetNumWaypoints();

	if (P_SyncUINT32(save, ARCHIVEBLOCK_WAYPOINTS) != ARCHIVEBLOCK_WAYPOINTS)
		I_Error("Bad $$$.sav at archive block Waypoints");

	if (P_SyncUINT32(save, numWaypoints) != numWaypoints)
		I_Error("Bad $$$.sav: More saved waypoints than created!");

	// The only thing we save for each waypoint is the mobj.
	// Waypoints should NEVER be completely created or destroyed mid-race as a result of this
	for (i = 0U; i < numWaypoints; i++)
	{
		waypoint = K_GetWaypointFromIndex(i);
		RSYNC(waypoint->mobj);
		RELINK(&waypoint->mobj);
	}

	TracyCZoneEnd(__zone);
}

static void P_NetSyncTubeWaypoints(savebuffer_t *save)
{
	TracyCZone(__zone, true);
	INT32 i, j;

	for (i = 0; i < NUMTUBEWAYPOINTSEQUENCES; i++)
	{
		SYNC(numtubewaypoints[i]);
		for (j = 0; j < numtubewaypoints[i]; j++)
		{
			RSYNC(tubewaypoints[i][j]);
			RELINK(&tubewaypoints[i][j]);
		}
	}
	TracyCZoneEnd(__zone);
}

// Now save the pointers, tracer and target, but at load time we must
// relink to this; the savegame contains the old position in the pointer
// field copyed in the info field temporarily, but finally we just search
// for the old position and relink to it.
mobj_t *P_FindNewPosition(UINT32 oldposition)
{
	thinker_t *th;
	mobj_t *mobj;

	for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
	{
		if (th->function == (actionf_p1)P_RemoveThinkerDelayed)
			continue;

		mobj = (mobj_t *)th;
		if (mobj->mobjnum != oldposition)
			continue;

		return mobj;
	}
	CONS_Debug(DBG_GAMELOGIC, "mobj %d not found\n", oldposition);
	return NULL;
}

///////////////////////////////////////////////////////////////////////////////
//
// haleyjd 03/26/06: PolyObject saving code
//

enum pobj_diff_t
{
	PD_FLAGS,
	PD_TRANS,
	PD__MAX
};

static inline void P_SynchPolyObj(savebuffer_t *save, polyobj_t *po)
{
	INT32 id;
	UINT32 angle;
	fixed_t x, y;
	UINT8 diff[(PD__MAX>>3) + 1] = {0};
	INT32 j = 0;

	if (save->write)
	{
		DIFF(po->flags != po->spawnflags, PD_FLAGS);
		DIFF(po->translucency != po->spawntrans, PD_FLAGS);
	}
	else
	{
		// nullify all polyobject thinker pointers;
		// the thinkers themselves will fight over who gets the field
		// when they first start to run.
		po->thinker = NULL;
	}

	id = P_SyncINT32(save, po->id);

	angle = P_SyncAngle(save, po->angle);

	x = P_SyncFixed(save, po->spawnSpot.x);
	y = P_SyncFixed(save, po->spawnSpot.y);

	do
		SYNC(diff[j]);
	while (diff[j++] & 0x80);

	SYNCF(PD_FLAGS, po->flags);
	SYNCF(PD_TRANS, po->translucency);

	if (!save->write)
	{
		// if the object is bad or isn't in the id hash, we can do nothing more
		// with it, so return now
		if (po->isBad || po != Polyobj_GetForNum(id))
			return;

		// rotate and translate polyobject
		Polyobj_MoveOnLoad(po, angle, x, y);
	}
}

static inline void P_SyncPolyObjects(savebuffer_t *save)
{
	INT32 i, numSavedPolys;

	if (P_SyncUINT32(save, ARCHIVEBLOCK_POBJS) != ARCHIVEBLOCK_POBJS)
		I_Error("Bad $$$.sav at archive block Pobjs");

	numSavedPolys = P_SyncINT32(save, numPolyObjects);

	if (numSavedPolys != numPolyObjects)
		I_Error("P_SynchronizePolyObjects: polyobj count inconsistency\n");

	for (i = 0; i < numSavedPolys; ++i)
		P_SynchPolyObj(save, &PolyObjects[i]);
}

static void P_RelinkPointers(savebuffer_t *save)
{
	thinker_t *currentthinker;
	mobj_t *mobj;
	UINT32 i;

	// use info field (value = oldposition) to relink mobjs
	for (currentthinker = thlist[THINK_MOBJ].next; currentthinker != &thlist[THINK_MOBJ];
		currentthinker = currentthinker->next)
	{
		if (currentthinker->function == (actionf_p1)P_RemoveThinkerDelayed)
			continue;

		mobj = (mobj_t *)currentthinker;

		if (!MobjIsNetSynced(mobj))
			continue;

		RELINK(&mobj->tracer);
		RELINK(&mobj->target);
		RELINK(&mobj->hnext);
		RELINK(&mobj->hprev);
		RELINK(&mobj->itnext);
		RELINK(&mobj->terrainOverlay);
	}

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i])
			continue;

		RELINK(&players[i].awayviewmobj);
		RELINK(&players[i].followmobj);
		RELINK(&players[i].follower);
		RELINK(&players[i].currentwaypoint);
		RELINK(&players[i].nextwaypoint);
		RELINK(&players[i].shieldtracer);
		RELINK(&players[i].arrowbullet);
	}
}

static inline void P_NetSyncSpecials(savebuffer_t *save)
{
	TracyCZone(__zone, true);

	size_t i, z;
	char skytex[9];

	if (P_SyncUINT32(save, ARCHIVEBLOCK_SPECIALS) != ARCHIVEBLOCK_SPECIALS)
		I_Error("Bad $$$.sav at archive block Specials");

	if (save->write)
	{
		// itemrespawn queue for deathmatch
		i = iquetail;
		while (iquehead != i)
		{
			for (z = 0; z < nummapthings; z++)
			{
				if (&mapthings[z] == itemrespawnque[i])
				{
					WRITEUINT32(save->p, z);
					break;
				}
			}
			WRITEUINT32(save->p, itemrespawntime[i]);
			i = (i + 1) & (ITEMQUESIZE-1);
		}

		// end delimiter
		WRITEUINT32(save->p, 0xffffffff);
	}
	else
	{
		// BP: added save itemrespawn queue for deathmatch
		iquetail = iquehead = 0;
		while ((i = READUINT32(save->p)) != 0xffffffff)
		{
			itemrespawnque[iquehead] = &mapthings[i];
			itemrespawntime[iquehead++] = READINT32(save->p);
		}
	}

	// Sky texture
	strcpy(skytex, globallevelskytexture);
	P_SyncStringN(save, skytex, 9);
	if (!fastcmp(skytex, globallevelskytexture))
		P_SetupLevelSky(skytex, true);

	// Current global weather type
	SYNC(globalweather);

	if (!save->write)
	{
		if (globalweather)
		{
			if (curWeather == globalweather)
				curWeather = PRECIP_NONE;

			P_SwitchWeather(globalweather);
		}
		else // PRECIP_NONE
		{
			if (curWeather != PRECIP_NONE)
				P_SwitchWeather(globalweather);
		}
	}

	if (save->write)
	{
		if (metalplayback) // Is metal sonic running?
		{
			WRITEUINT8(save->p, 0x01);
			G_SaveMetal(&save->p);
		}
		else
			WRITEUINT8(save->p, 0x00);
	}
	else
	{
		if (READUINT8(save->p) == 0x01) // metal sonic
			G_LoadMetal(&save->p);
	}
	TracyCZoneEnd(__zone);
}

// =======================================================================
//          Misc
// =======================================================================
static inline void P_ArchiveMisc(savebuffer_t *save, mapnum_t mapnum)
{
	TracyCZone(__zone, true);

	//lastmapsaved = mapnum;
	lastmaploaded = mapnum;

	if (gamecomplete)
		mapnum |= 8192;

	WRITEUINT16(save->p, mapnum);
	WRITEUINT16(save->p, emeralds+357);
	WRITESTRINGN(save->p, timeattackfolder, sizeof(timeattackfolder));

	TracyCZoneEnd(__zone);
}

static inline void P_UnArchiveSPGame(savebuffer_t *save, mapnum_t mapoverride)
{
	TracyCZone(__zone, true);

	char testname[sizeof(timeattackfolder)];

	gamemap = READUINT16(save->p);

	if (mapoverride != 0)
	{
		gamemap = mapoverride;
		gamecomplete = 1;
	}
	else
		gamecomplete = 0;

	// gamemap changed; we assume that its map header is always valid,
	// so make it so
	if (!gamemap || gamemap > nummapheaders || !mapheaderinfo[gamemap-1])
		I_Error("P_UnArchiveSPGame: Internal map ID %d not found (nummapheaders = %d)", gamemap-1, nummapheaders);

	//lastmapsaved = gamemap;
	lastmaploaded = gamemap;

	tokenlist = 0;
	token = 0;

	savedata.emeralds = READUINT16(save->p)-357;

	READSTRINGN(save->p, testname, sizeof(testname));

	if (!fastcmp(testname, timeattackfolder))
	{
		if (modifiedgame)
			I_Error("Save game not for this modification.");
		else
			I_Error("This save file is for a particular mod, it cannot be used with the regular game.");
	}

	memset(playeringame, 0, sizeof(*playeringame));
	playeringame[consoleplayer] = true;

	TracyCZoneEnd(__zone);
}

static void P_NetSyncRNG(savebuffer_t *save)
{
	size_t i;
	UINT32 init = 0;
	UINT32 seed = 0;

	const boolean reading_netsave = (!save->write);

	for (i = 0; i < PRNUMCLASS; i++)
	{
		init = P_SyncUINT32(save, P_GetInitSeed(i));
		seed = P_SyncUINT32(save, P_GetRandSeed(i));

		if (reading_netsave)
			P_SetRandSeedNet(i, init, seed);
	}
}

static boolean P_NetSyncMisc(savebuffer_t *save, boolean resending)
{
	TracyCZone(__zone, true);

	size_t i, j;
	size_t numTasks;
	const INT16 prevgamemap = gamemap;

	if (P_SyncUINT32(save, ARCHIVEBLOCK_MISC) != ARCHIVEBLOCK_MISC)
		I_Error("Bad $$$.sav at archive block Misc");

	if (resending)
		SYNC(gametic);

	SYNC(gamemap);

	if (save->write)
	{
		if (gamestate != GS_LEVEL)
			WRITEINT16(save->p, GS_WAITINGPLAYERS); // nice hack to put people back into waitingplayers
		else
			WRITEINT16(save->p, gamestate);
	}
	else
	{
		g_reloadingMap = false;

		if (!gamemap || gamemap > nummapheaders || !mapheaderinfo[gamemap-1])
			I_Error("P_NetUnArchiveMisc: Internal map ID %d not found (nummapheaders = %d)", gamemap-1, nummapheaders);

		// tell the sound code to reset the music since we're skipping what
		// normally sets this flag
		if (!resending)
			mapmusflags |= MUSIC_RELOADRESET;
		G_SetGamestate(READINT16(save->p));
	}

	SYNC(gametype);

#if MAXPLAYERS > 32
#error Update playeringame syncing please
#endif

	if (save->write)
	{
		UINT32 pig = 0;
		for (i = 0; i < MAXPLAYERS; i++)
			pig |= (playeringame[i] != 0)<<i;
		WRITEUINT32(save->p, pig);
	}
	else
	{
		UINT32 pig = READUINT32(save->p);
		for (i = 0; i < MAXPLAYERS; i++)
		{
			playeringame[i] = (pig & (1<<i)) != 0;
			// playerstate is set in unarchiveplayers
		}
	}

	SYNC(tokenlist);

	SYNCBOOLEAN(encoremode);

	SYNC(initial_seed);
	SYNC(mapmusrng);

	if (!save->write)
	{
		// Regardless of if the RNG has already been initialized or not, initialize
		// the seeds *right now* to prevent infinite-loops.
		I_OutputMsg("[NetSyncMisc] initial seed: %u\n", initial_seed);
		P_ClearRandom(initial_seed);

		// Only reload the level during a gamestate reload
		// if the map is horribly mismatched somehow. Minor
		// differences in level state are already handled
		// by other parts of the reload, so doing this
		// on *every* reload wastes lots of time that we
		// will need for rollback down the road.
		if (!resending || prevgamemap != gamemap)
		{
			if (!P_LoadLevel(true, resending))
			{
				CONS_Alert(CONS_ERROR, M_GetText("Can't load the level!\n"));
				TracyCZoneEnd(__zone);
				return false;
			}
		}
		else
		{
			// Only reload stuff that can we modify in the save states themselves.
			// This is still orders of magnitude faster than a full level reload.
			// Considered memcpy, but it's complicated -- save that for local saves.

			sector_t *ss = sectors;
			sector_t *spawnss = spawnsectors;
			for (i = 0; i < numsectors; i++, ss++, spawnss++)
			{
				ss->floorheight = spawnss->floorheight;
				ss->ceilingheight = spawnss->ceilingheight;
				ss->floorpic = spawnss->floorpic;
				ss->ceilingpic = spawnss->ceilingpic;
				ss->lightlevel = spawnss->lightlevel;
				ss->special = spawnss->special;
				ss->floor_xoffs = spawnss->floor_xoffs;
				ss->floor_yoffs = spawnss->floor_yoffs;
				ss->ceiling_xoffs = spawnss->ceiling_xoffs;
				ss->ceiling_yoffs = spawnss->ceiling_yoffs;
				ss->floorpic_angle = spawnss->floorpic_angle;
				ss->ceilingpic_angle = spawnss->ceilingpic_angle;

				if (Tag_Compare(&ss->tags, &spawnss->tags) == false)
				{
					if (spawnss->tags.count)
					{
						ss->tags.count = spawnss->tags.count;
						ss->tags.tags =
							memcpy(
								Z_Realloc(
									ss->tags.tags,
									spawnss->tags.count * sizeof(mtag_t),
									PU_LEVEL,
									NULL
								),
								spawnss->tags.tags,
								spawnss->tags.count * sizeof(mtag_t)
							);
					}
					else
					{
						ss->tags.count = 0;
						Z_Free(ss->tags.tags);
					}
				}

				ss->extra_colormap = ss->spawn_extra_colormap;
				ss->crumblestate = CRUMBLE_NONE;
				ss->floorlightlevel = spawnss->floorlightlevel;
				ss->floorlightabsolute = spawnss->floorlightabsolute;
				ss->ceilinglightlevel = spawnss->ceilinglightlevel;
				ss->ceilinglightabsolute = spawnss->ceilinglightabsolute;
				ss->flags = spawnss->flags;
				ss->specialflags = spawnss->specialflags;
				ss->damagetype = spawnss->damagetype;
				ss->triggertag = spawnss->triggertag;
				ss->triggerer = spawnss->triggerer;
				ss->gravity = spawnss->gravity;
				ss->action = spawnss->action;

				memcpy(ss->args, spawnss->args, NUM_SCRIPT_ARGS * sizeof(*ss->args));

				for (j = 0; j < NUM_SCRIPT_STRINGARGS; j++)
				{
					size_t len = 0;

					if (spawnss->stringargs[j])
					{
						len = strlen(spawnss->stringargs[j]);
					}

					if (!len)
					{
						Z_Free(ss->stringargs[j]);
						ss->stringargs[j] = NULL;
					}
					else
					{
						ss->stringargs[j] = Z_Realloc(ss->stringargs[j], len + 1, PU_LEVEL, NULL);
						memcpy(ss->stringargs[j], spawnss->stringargs[j], len);
						ss->stringargs[j][len] = '\0';
					}
				}

				ss->activation = spawnss->activation;
				ss->botController.flags = spawnss->botController.flags;
				ss->botController.forceAngle = spawnss->botController.forceAngle;

				if (ss->ffloors)
				{
					ffloor_t *rover;
					for (rover = ss->ffloors; rover; rover = rover->next)
					{
						rover->fofflags = rover->spawnflags;
						rover->alpha = rover->spawnalpha;
					}
				}
			}

			line_t *li = lines;
			line_t *spawnli = spawnlines;
			side_t *si = NULL;
			side_t *spawnsi = NULL;
			for (i = 0; i < numlines; i++, spawnli++, li++)
			{
				li->flags = spawnli->flags;
				li->special = spawnli->special;
				li->callcount = 0;

				if (Tag_Compare(&li->tags, &spawnli->tags) == false)
				{
					if (spawnli->tags.count)
					{
						li->tags.count = spawnli->tags.count;
						li->tags.tags =
							memcpy(
								Z_Realloc(
									li->tags.tags,
									spawnli->tags.count * sizeof(mtag_t),
									PU_LEVEL,
									NULL
								),
								spawnli->tags.tags,
								spawnli->tags.count * sizeof(mtag_t)
							);

					}
					else
					{
						li->tags.count = 0;
						Z_Free(li->tags.tags);
					}
				}

				memcpy(li->args, spawnli->args, NUM_SCRIPT_ARGS * sizeof(*li->args));

				for (j = 0; j < NUM_SCRIPT_STRINGARGS; j++)
				{
					size_t len = 0;

					if (spawnli->stringargs[j])
					{
						len = strlen(spawnli->stringargs[j]);
					}

					if (!len)
					{
						Z_Free(li->stringargs[j]);
						li->stringargs[j] = NULL;
					}
					else
					{
						li->stringargs[j] = Z_Realloc(li->stringargs[j], len + 1, PU_LEVEL, NULL);
						memcpy(li->stringargs[j], spawnli->stringargs[j], len);
						li->stringargs[j][len] = '\0';
					}
				}

				li->executordelay = spawnli->executordelay;
				li->activation = spawnli->activation;

				if (li->sidenum[0] != NO_INDEX)
				{
					si = &sides[li->sidenum[0]];
					spawnsi = &spawnsides[li->sidenum[0]];

					si->textureoffset = spawnsi->textureoffset;
					si->toptexture = spawnsi->toptexture;
					si->bottomtexture = spawnsi->bottomtexture;
					si->midtexture = spawnsi->midtexture;
				}

				if (li->sidenum[1] != NO_INDEX)
				{
					si = &sides[li->sidenum[1]];
					spawnsi = &spawnsides[li->sidenum[1]];

					si->textureoffset = spawnsi->textureoffset;
					si->toptexture = spawnsi->toptexture;
					si->bottomtexture = spawnsi->bottomtexture;
					si->midtexture = spawnsi->midtexture;
				}
			}

			Taglist_InitGlobalTables();
		}
	}

	// yama: Sync RNG *diirectly after* the map loads in, to prevent on-spawn desynchs
	P_NetSyncRNG(save);

	SYNC(leveltime);
	SYNC(lastmap);
	SYNC(bossdisabled);
	SYNCBOOLEAN(ringsactive);
	SYNCBOOLEAN(stackingactive);
	SYNCBOOLEAN(chainingactive);
	SYNCBOOLEAN(slipdashactive);
	SYNCBOOLEAN(purpledriftactive);
	SYNCBOOLEAN(slopeboostactive);
	SYNCBOOLEAN(draftingactive);
	SYNC(airthrustactive);
	SYNCBOOLEAN(recoverydashactive);
	SYNCBOOLEAN(keepstuffactive);
	SYNCBOOLEAN(waterskipbricks);
	SYNCBOOLEAN(itemlittering);
	SYNCBOOLEAN(itempushing);
	SYNCBOOLEAN(itemlistactive);
	SYNCBOOLEAN(trailslow_active);
	SYNCBOOLEAN(rankingactive);
	SYNCBOOLEAN(ranking_penaltyactive);
	SYNCBOOLEAN(cannotrank);
	SYNC(airdropactive);
	SYNC(bumpsparkactive);
	SYNC(antibumptime);
	SYNC(playerbumpactive);

	SYNC(clusterid);
	SYNC(clustermode);
	SYNC(clusterjustchanged);

	for (i = 0; i < sizeof(g_voteLevels)/sizeof(*g_voteLevels); i++)
	{
		SYNC(g_voteLevels[i][0]);
		SYNC(g_voteLevels[i][1]);
	}

	for (i = 0; i < MAXPLAYERS; i++)
		SYNC(g_votes[i]);

	SYNC(g_pickedVote);

	SYNC(emeralds);
	if (save->write)
	{
		UINT8 globools = 0;
		if (stagefailed)
			globools |= 1;
		if (stoppedclock)
			globools |= (1<<1);
		WRITEUINT8(save->p, globools);
	}
	else
	{
		UINT8 globools = READUINT8(save->p);
		stagefailed = !!(globools & 1);
		stoppedclock = !!(globools & (1<<1));
	}

	SYNC(token);
	SYNC(bluescore);
	SYNC(redscore);

	SYNC(skincolor_redteam);
	SYNC(skincolor_blueteam);
	SYNC(skincolor_redring);
	SYNC(skincolor_bluering);

	SYNC(modulothing);

	SYNC(autobalance);
	SYNC(teamscramble);

	for (i = 0; i < MAXPLAYERS; i++)
		SYNC(scrambleplayers[i]);

	for (i = 0; i < MAXPLAYERS; i++)
		SYNC(scrambleteams[i]);

	SYNC(scrambletotal);
	SYNC(scramblecount);

	SYNC(racecountdown);
	SYNC(exitcountdown);

	// exitcondition_t
	SYNCBOOLEAN(g_exit.losing);
	SYNCBOOLEAN(g_exit.retry);
	SYNCBOOLEAN(g_exit.hasfinished);

	SYNC(gravity);
	SYNC(mapobjectscale);

	// SRB2kart
	SYNC(nummapboxes);
	SYNC(numgotboxes);
	SYNC(numtargets);
	SYNCBOOLEAN(itembreaker);

	SYNC(gamespeed);
	SYNC(numlaps);
	SYNC(autopartime);
	SYNCBOOLEAN(franticitems);
	SYNCBOOLEAN(comeback);

	SYNC(speedscramble);
	SYNC(encorescramble);

	// WANTED system
	SYNC(mostwanted);
	for (i = 0; i < sizeof(battlewanted)/sizeof(*battlewanted); i++)
		SYNC(battlewanted[i]);

	SYNC(wantedcalcdelay);

	for (i = 0; i < numkartitems; i++)
		SYNC(kartitems[i].altenabled);

	for (i = 0; i < numkartresults; i++)
		SYNC(kartresults[i].cooldown);

	SYNC(mapreset);

	SYNC(spectateGriefed);

	SYNCBOOLEAN(thwompsactive);
	SYNC(lastLowestLap);
	SYNC(spbplace);
	SYNCBOOLEAN(startedInFreePlay);

	SYNC(introtime);
	SYNC(starttime);

	SYNC(hyudorotime);
	SYNC(stealtime);
	SYNC(sneakertime);
	SYNC(waterpaneltime);
	SYNC(itemtime);
	SYNC(bubbletime);
	SYNC(comebacktime);
	SYNC(bumptime);
	SYNC(greasetics);
	SYNC(wipeoutslowtime);
	SYNC(wantedreduce);
	SYNC(wantedfrequency);

	SYNC(timelimitintics);
	SYNC(extratimeintics);
	SYNC(secretextratime);

	SYNC(paused); // surprisingly not a boolean

	if (save->write)
	{
		// Only the server uses this, but it
		// needs synched for remote admins anyway.
		WRITEUINT32(save->p, schedule_len);
		for (i = 0; i < schedule_len; i++)
		{
			scheduleTask_t *task = schedule[i];
			WRITEINT16(save->p, task->basetime);
			WRITEINT16(save->p, task->timer);
			WRITESTRING(save->p, task->command);
		}
	}
	else
	{
		// Only the server uses this, but it
		// needs synched for remote admins anyway.
		Schedule_Clear();

		numTasks = READUINT32(save->p);
		for (i = 0; i < numTasks; i++)
		{
			INT16 basetime;
			INT16 timer;
			char command[MAXTEXTCMD];

			basetime = READINT16(save->p);
			timer = READINT16(save->p);
			READSTRING(save->p, command);

			Schedule_Add(basetime, timer, command);
		}
	}

	SYNC(cht_debug);

	TracyCZoneEnd(__zone);
	return true;
}

static inline void P_ArchiveLuabanksAndConsistency(savebuffer_t *save)
{
	TracyCZone(__zone, true);

	UINT8 i, banksinuse = NUM_LUABANKS;

	while (banksinuse && !luabanks[banksinuse-1])
		banksinuse--; // get the last used bank

	if (banksinuse)
	{
		WRITEUINT8(save->p, 0xb7); // luabanks marker
		WRITEUINT8(save->p, banksinuse);
		for (i = 0; i < banksinuse; i++)
			WRITEINT32(save->p, luabanks[i]);
	}

	WRITEUINT8(save->p, 0x1d); // consistency marker

	TracyCZoneEnd(__zone);
}

static inline boolean P_UnArchiveLuabanksAndConsistency(savebuffer_t *save)
{
	TracyCZone(__zone, true);

	boolean ret = true;
	switch (READUINT8(save->p))
	{
		case 0xb7: // luabanks marker
			{
				UINT8 i, banksinuse = READUINT8(save->p);
				if (banksinuse > NUM_LUABANKS)
				{
					CONS_Alert(CONS_ERROR, M_GetText("Corrupt Luabanks! (Too many banks in use)\n"));
					ret = false;
					break;
				}
				for (i = 0; i < banksinuse; i++)
					luabanks[i] = READINT32(save->p);
				if (READUINT8(save->p) != 0x1d) // consistency marker
				{
					CONS_Alert(CONS_ERROR, M_GetText("Corrupt Luabanks! (Failed consistency check)\n"));
					ret = false;
					break;
				}
			}
		case 0x1d: // consistency marker
			break;
		default: // anything else is nonsense
			CONS_Alert(CONS_ERROR, M_GetText("Failed consistency check (???)\n"));
			ret = false;
			break;
	}

	TracyCZoneEnd(__zone);
	return ret;
}

void P_SaveGame(savebuffer_t *save, mapnum_t mapnum)
{
	P_ArchiveMisc(save, mapnum);
	P_ArchivePlayer(save);
	P_ArchiveLuabanksAndConsistency(save);
}

void P_SaveNetGame(savebuffer_t *save, boolean resending)
{
	TracyCZone(__zone, true);

	thinker_t *th;
	mobj_t *mobj;
	UINT32 i = 1; // don't start from 0, it'd be confused with a blank pointer otherwise

	save->write = true;

	CV_SaveNetVars(&save->p);
	P_NetSyncMisc(save, resending);

	// Assign the mobjnumber for pointer tracking
	if (gamestate == GS_LEVEL)
	{
		for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
		{
			if (th->function == (actionf_p1)P_RemoveThinkerDelayed)
				continue;

			mobj = (mobj_t *)th;
			if (!MobjIsNetSynced(mobj))
				continue;
			mobj->mobjnum = i++;
		}
	}

	P_NetSyncPlayers(save);
	P_NetSyncParties(save);
	P_NetSyncRoundQueue(save);

	if (gamestate == GS_LEVEL)
	{
		P_NetSyncWorld(save);
		P_SyncPolyObjects(save);
		P_NetSyncThinkers(save);
		P_NetSyncSpecials(save);
		P_NetSyncColormaps(save);
		P_NetSyncTubeWaypoints(save);
		P_NetSyncWaypoints(save);
	}

	ACS_Archive(save);
	LUA_Sync(save, true, false);

	P_ArchiveLuabanksAndConsistency(save);

	TracyCZoneEnd(__zone);
}

boolean P_LoadGame(savebuffer_t *save, mapnum_t mapoverride)
{
	if (gamestate == GS_INTERMISSION)
		Y_EndIntermission();
	if (gamestate == GS_VOTING)
		Y_EndVote();
	G_SetGamestate(GS_NULL); // should be changed in P_UnArchiveMisc

	P_UnArchiveSPGame(save, mapoverride);
	P_UnArchivePlayer(save);

	if (!P_UnArchiveLuabanksAndConsistency(save))
		return false;

	// Only do this after confirming savegame is ok
	G_DeferedInitNew(false, gamemap, savedata.skin, 0, true);
	COM_BufAddText("dummyconsvar 1\n"); // G_DeferedInitNew doesn't do this

	return true;
}

boolean P_LoadNetGame(savebuffer_t *save, boolean reloading)
{
	TracyCZone(__zone, true);

	save->write = false;

	CV_LoadNetVars(&save->p);
	if (!P_NetSyncMisc(save,reloading))
		return false;
	P_NetSyncPlayers(save);
	P_NetSyncParties(save);
	P_NetSyncRoundQueue(save);

	if (gamestate == GS_LEVEL)
	{
		P_NetSyncWorld(save);
		P_SyncPolyObjects(save);
		P_NetSyncThinkers(save);
		P_NetSyncSpecials(save);
		P_NetSyncColormaps(save);
		P_NetSyncTubeWaypoints(save);
		P_NetSyncWaypoints(save);
		P_RelinkPointers(save);
	}

	ACS_UnArchive(save);
	LUA_Sync(save, true, false);

	// The precipitation would normally be spawned in P_SetupLevel, which is called by
	// P_NetUnArchiveMisc above. However, that would place it up before P_NetUnArchiveThinkers,
	// so the thinkers would be deleted later. Therefore, P_SetupLevel will *not* spawn
	// precipitation when loading a netgame save. Instead, precip has to be spawned here.
	// This is done in P_NetUnArchiveSpecials now.
	boolean ret = P_UnArchiveLuabanksAndConsistency(save);

	TracyCZoneEnd(__zone);
	return ret;
}

boolean P_SaveBufferZAlloc(savebuffer_t *save, size_t alloc_size, INT32 tag, void *user)
{
	I_Assert(save->buffer == NULL);
	save->buffer = (UINT8 *)Z_Malloc(alloc_size, tag, user);

	if (save->buffer == NULL)
	{
		return false;
	}

	save->size = alloc_size;
	save->p = save->buffer;
	save->end = save->buffer + save->size;

	return true;
}

boolean P_SaveBufferFromExisting(savebuffer_t *save, UINT8 *existing_buffer, size_t existing_size)
{
	I_Assert(save->buffer == NULL);

	if (existing_buffer == NULL || existing_size == 0)
	{
		return false;
	}

	save->buffer = existing_buffer;
	save->size = existing_size;

	save->p = save->buffer;
	save->end = save->buffer + save->size;

	return true;
}

boolean P_SaveBufferFromLump(savebuffer_t *save, lumpnum_t lump)
{
	I_Assert(save->buffer == NULL);

	if (lump == LUMPERROR)
	{
		return false;
	}

	save->buffer = (UINT8 *)W_CacheLumpNum(lump, PU_STATIC);

	if (save->buffer == NULL)
	{
		return false;
	}

	save->size = W_LumpLength(lump);

	save->p = save->buffer;
	save->end = save->buffer + save->size;

	return true;
}

size_t P_SaveBufferFromFile(savebuffer_t *save, char const *name)
{
	size_t len = 0;

	I_Assert(save->buffer == NULL);
	len = FIL_ReadFile(name, &save->buffer);

	if (len != 0)
	{
		save->size = len;

		save->p = save->buffer;
		save->end = save->buffer + save->size;
	}

	return len;
}

static void P_SaveBufferInvalidate(savebuffer_t *save)
{
	save->buffer = save->p = save->end = NULL;
	save->size = 0;
}

void P_SaveBufferFree(savebuffer_t *save)
{
	//I_Assert(save->buffer != NULL); // makes it a hassle to use with CLEANUP
	Z_Free(save->buffer);
	P_SaveBufferInvalidate(save);
}
