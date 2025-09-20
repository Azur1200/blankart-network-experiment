// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2012-2016 by John "JTE" Muniz.
// Copyright (C) 2012-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  lua_playerlib.c
/// \brief player object library for Lua scripting

#include "blua/lua.h"
#include "doomdef.h"
#include "fastcmp.h"
#include "p_mobj.h"
#include "d_player.h"
#include "g_game.h"
#include "p_local.h"
#include "d_clisrv.h"
#include "k_kart.h" // K_GetDriftAngleOffset

#include "lua_script.h"
#include "lua_libs.h"
#include "lua_hud.h" // hud_running errors
#include "lua_hook.h" // hook_cmd_running errors

static int lib_iteratePlayers(lua_State *L)
{
	INT32 i = -1;
	if (lua_gettop(L) < 2)
	{
		//return luaL_error(L, "Don't call players.iterate() directly, use it as 'for player in players.iterate do <block> end'.");
		lua_pushcfunction(L, lib_iteratePlayers);
		return 1;
	}
	lua_settop(L, 2);
	lua_remove(L, 1); // state is unused.
	if (!lua_isnil(L, 1))
		i = (INT32)(*((player_t **)luaL_checkudata(L, 1, META_PLAYER)) - players);
	for (i++; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i])
			continue;
		if (!players[i].mo)
			continue;
		LUA_PushUserdata(L, &players[i], META_PLAYER);
		return 1;
	}
	return 0;
}

static int lib_getPlayer(lua_State *L)
{
	const char *field;
	// i -> players[i]
	if (lua_type(L, 2) == LUA_TNUMBER)
	{
		lua_Integer i = luaL_checkinteger(L, 2);
		if (i < 0 || i >= MAXPLAYERS)
			return luaL_error(L, "players[] index %d out of range (0 - %d)", i, MAXPLAYERS-1);

		if (!playeringame[i])
			return 0;
		if (!players[i].mo)
			return 0;
		LUA_PushUserdata(L, &players[i], META_PLAYER);
		return 1;
	}

	field = luaL_checkstring(L, 2);
	if (fastcmp(field,"iterate"))
	{
		lua_pushcfunction(L, lib_iteratePlayers);
		return 1;
	}
	return 0;
}

// #players -> MAXPLAYERS
static int lib_lenPlayer(lua_State *L)
{
	lua_pushinteger(L, MAXPLAYERS);
	return 1;
}

// Same deal as the three functions above but for displayplayers

static int lib_iterateDisplayplayers(lua_State *L)
{
	INT32 i = -1;
	INT32 temp = -1;
	INT32 iter = 0;

	if (lua_gettop(L) < 2)
	{
		//return luaL_error(L, "Don't call displayplayers.iterate() directly, use it as 'for player in displayplayers.iterate do <block> end'.");
		lua_pushcfunction(L, lib_iterateDisplayplayers);
		return 1;
	}
	lua_settop(L, 2);
	lua_remove(L, 1); // state is unused.
	if (!lua_isnil(L, 1))
	{
		temp = (INT32)(*((player_t **)luaL_checkudata(L, 1, META_PLAYER)) - players);	// get the player # of the last iterated player.

		// @FIXME:
		// I didn't quite find a better way for this; Here, we go back to which player in displayplayers we last iterated to resume the for loop below for this new function call
		// I don't understand enough about how the Lua stacks work to get this to work in possibly a single line.
		// So anyone feel free to correct this!

		for (; iter < MAXSPLITSCREENPLAYERS; iter++)
		{
			if (displayplayers[iter] == temp)
			{
				i = iter;
				break;
			}
		}
	}

	for (i++; i < MAXSPLITSCREENPLAYERS; i++)
	{
		if (i > r_splitscreen || !playeringame[displayplayers[i]])
			return 0;	// Stop! There are no more players for us to go through. There will never be a player gap in displayplayers.

		LUA_PushUserdata(L, &players[displayplayers[i]], META_PLAYER);
		lua_pushinteger(L, i);	// push this to recall what number we were on for the next function call. I suppose this also means you can retrieve the splitscreen player number with 'for p, n in displayplayers.iterate'!
		return 2;
	}
	return 0;
}

static int lib_getDisplayplayers(lua_State *L)
{
	const char *field;
	// i -> players[i]
	if (lua_type(L, 2) == LUA_TNUMBER)
	{
		lua_Integer i = luaL_checkinteger(L, 2);
		if (i < 0 || i >= MAXSPLITSCREENPLAYERS)
			return luaL_error(L, "displayplayers[] index %d out of range (0 - %d)", i, MAXSPLITSCREENPLAYERS-1);
		if (i > r_splitscreen)
			return 0;
		if (!playeringame[displayplayers[i]])
			return 0;
		LUA_PushUserdata(L, &players[displayplayers[i]], META_PLAYER);
		return 1;
	}

	field = luaL_checkstring(L, 2);
	if (fastcmp(field,"iterate"))
	{
		lua_pushcfunction(L, lib_iterateDisplayplayers);
		return 1;
	}
	return 0;
}

// #displayplayers -> MAXSPLITSCREENPLAYERS
static int lib_lenDisplayplayers(lua_State *L)
{
	lua_pushinteger(L, MAXSPLITSCREENPLAYERS);
	return 1;
}

enum player_e
{
	player_valid,
	player_name,
	player_realmo,
	player_mo,
	player_cmd,
	player_oldcmd,
	player_playerstate,
	player_health,
	player_viewz,
	player_viewheight,
	player_viewrollangle,
	player_aiming,
	player_drawangle,
	player_frameangle,
	player_powers,
	player_kartstuff,
	player_karthud,
	player_nocontrol,
	player_carry,
	player_dye,
	player_position,
	player_oldposition,
	player_positiondelay,
	player_prevcheck,
	player_nextcheck,
	player_distancetofinish,
	player_distancetofinishprev,
	player_currentwaypoint,
	player_nextwaypoint,
	player_airtime,
	player_bigwaypointgap,
	player_flashing,
	player_spinouttimer,
	player_spinouttype,
	player_flipovertimer,
	player_flipoverangle,
	player_instashield,
	player_wipeoutslow,
	player_justbumped,
	player_itemflags,
	player_outrun,
	player_outruntime,
	player_drift,
	player_driftcharge,
	player_driftboost,
	player_boostcharge,
	player_slopeboost,
	player_prevslopeboost,
	player_slopeaccel,
	player_startboost,
	player_dropdash,
	player_respawn,
	player_aizdriftstrat,
	player_aizdrifttilt,
	player_aizdriftturn,
	player_slipdashcharge,
	player_slipdashdir,
	player_offroad,
	player_pogospring,
	player_brakestop,
	player_waterskip,
	player_dashpadcooldown,
	player_boostpower,
	player_speedboost,
	player_accelboost,
	player_boostangle,
	player_boostinfo,
	player_numsneakers,
	player_numboosts,
	player_draftpower,
	player_draftleeway,
	player_lastdraft,
	player_tripwireState,
	player_tripwirePass,
	player_tripwireLeniency,
	player_tripwireReboundDelay,
	player_itemroulette,
	player_previtemroulette,
	player_itemblink,
	player_itemblinkmode,
	player_roulettetype,
	player_itemtype,
	player_itemamount,
	player_throwdir,
	player_sadtimer,
	player_rings,
	player_ringmin,
	player_ringmax,
	player_pickuprings,
	player_ringdelay,
	player_ringlock,
	player_ringboost,
	player_ringtime,
	player_superring,
	player_nextringaward,
	player_ringvolume,
	player_ringtransparency,
	player_airdroptime,
	player_curshield,
	player_bubblecool,
	player_bubbleblowup,
	player_bubblehealth,
	player_flamedash,
	player_flametimer,
	player_flamestore,
	player_hyudorotimer,
	player_stealingtimer,
	player_stolentimer,
	player_sneakertimer,
	player_realsneakertimer,
	player_floorboost,
	player_chaintimer,
	player_growshrinktimer,
	player_growcancel,
	player_squishedtimer,
	player_rocketsneakertimer,
	player_invincibilitytimer,
	player_eggmanexplode,
	player_eggmanblame,
	player_bananadrag,
	player_lastjawztarget,
	player_jawztargetdelay,
	player_confirmVictim,
	player_confirmVictimDelay,
	player_glanceDir,
	player_breathTimer,
	player_lastsafelap,
	player_lastsafestarpost,
	player_interpoints,
	player_roundscore,
	player_marescore,
	player_emeralds,
	player_bumpers,
	player_karmadelay,
	player_pflags,
	player_panim,
	player_flashcount,
	player_flashpal,
	player_skincolor,
	player_skin,
	player_availabilities,
	player_score,
	player_kartspeed,
	player_kartweight,
	player_followerskin,
	player_followerready,
	player_followercolor,
	player_follower,
	player_charflags,
	player_followitem,
	player_followmobj,
	player_lives,
	player_xtralife,
	player_speed,
	player_lastspeed,
	player_deadtimer,
	player_exiting,
	player_cmomx,
	player_cmomy,
	player_rmomx,
	player_rmomy,
	player_totalring,
	player_realtime,
	player_laps,
	player_latestlap,
	player_ctfteam,
	player_checkskip,
	player_starpostx,
	player_starposty,
	player_starpostz,
	player_starpostangle,
	player_starpostflip,
	player_starpostnum,
	player_starposttime,
	player_lastsidehit,
	player_lastlinehit,
	player_onconveyor,
	player_awayviewmobj,
	player_awayviewtics,
	player_awayviewaiming,
	player_spectator,
	player_spectatewait,
	player_bot,
	player_botvars,
	player_jointime,
	player_spectatorreentry,
	player_grieftime,
	player_griefstrikes,
	player_splitscreenindex,
	player_bumpertime,
	player_ping,
	player_packetloss,
	player_loop,
#ifdef HWRENDER
	player_fovadd,
#endif
};

static const char *const player_opt[] = {
	"valid",
	"name",
	"realmo",
	"mo",
	"cmd",
	"oldcmd",
	"playerstate",
	"health",
	"viewz",
	"viewheight",
	"viewrollangle",
	"aiming",
	"drawangle",
	"frameangle",
	"powers",
	"kartstuff",
	"karthud",
	"nocontrol",
	"carry",
	"dye",
	"position",
	"oldposition",
	"positiondelay",
	"prevcheck",
	"nextcheck",
	"distancetofinish",
	"distancetofinishprev",
	"currentwaypoint",
	"nextwaypoint",
	"airtime",
	"bigwaypointgap",
	"flashing",
	"spinouttimer",
	"spinouttype",
	"flipovertimer",
	"flipoverangle",
	"instashield",
	"wipeoutslow",
	"justbumped",
	"itemflags",
	"outrun",
	"outruntime",
	"drift",
	"driftcharge",
	"driftboost",
	"boostcharge",
	"slopeboost",
	"prevslopeboost",
	"slopeaccel",
	"startboost",
	"dropdash",
	"respawn",
	"aizdriftstrat",
	"aizdrifttilt",
	"aizdriftturn",
	"slipdashcharge",
	"slipdashdir",
	"offroad",
	"pogospring",
	"brakestop",
	"waterskip",
	"dashpadcooldown",
	"boostpower",
	"speedboost",
	"accelboost",
	"boostangle",
	"boostinfo",
	"numsneakers",
	"numboosts",
	"draftpower",
	"draftleeway",
	"lastdraft",
	"tripwireState",
	"tripwirePass",
	"tripwireLeniency",
	"tripwireReboundDelay",
	"itemroulette",
	"previtemroulette",
	"itemblink",
	"itemblinkmode",
	"roulettetype",
	"itemtype",
	"itemamount",
	"throwdir",
	"sadtimer",
	"rings",
	"ringmin",
	"ringmax",
	"pickuprings",
	"ringdelay",
	"ringlock",
	"ringboost",
	"ringtime",
	"superring",
	"nextringaward",
	"ringvolume",
	"ringtransparency",
	"airdroptime",
	"curshield",
	"bubblecool",
	"bubbleblowup",
	"bubblehealth",
	"flamedash",
	"flametimer",
	"flamestore",
	"hyudorotimer",
	"stealingtimer",
	"stolentimer",
	"sneakertimer",
	"realsneakertimer",
	"floorboost",
	"chaintimer",
	"growshrinktimer",
	"growcancel",
	"squishedtimer",
	"rocketsneakertimer",
	"invincibilitytimer",
	"eggmanexplode",
	"eggmanblame",
	"bananadrag",
	"lastjawztarget",
	"jawztargetdelay",
	"confirmVictim",
	"confirmVictimDelay",
	"glanceDir",
	"breathTimer",
	"lastsafelap",
	"lastsafestarpost",
	"interpoints",
	"roundscore",
	"marescore",
	"emeralds",
	"bumpers",
	"karmadelay",
	"pflags",
	"panim",
	"flashcount",
	"flashpal",
	"skincolor",
	"skin",
	"availabilities",
	"score",
	"kartspeed",
	"kartweight",
	"followerskin",
	"followerready",
	"followercolor",
	"follower",
	"charflags",
	"followitem",
	"followmobj",
	"lives",
	"xtralife",
	"speed",
	"lastspeed",
	"deadtimer",
	"exiting",
	"cmomx",
	"cmomy",
	"rmomx",
	"rmomy",
	"totalring",
	"realtime",
	"laps",
	"latestlap",
	"ctfteam",
	"checkskip",
	"starpostx",
	"starposty",
	"starpostz",
	"starpostangle",
	"starpostflip",
	"starpostnum",
	"starposttime",
	"lastsidehit",
	"lastlinehit",
	"onconveyor",
	"awayviewmobj",
	"awayviewtics",
	"awayviewaiming",
	"spectator",
	"spectatewait",
	"bot",
	"botvars",
	"jointime",
	"spectatorreentry",
	"grieftime",
	"griefstrikes",
	"splitscreenindex",
	"bumpertime",
	"ping",
	"packetloss",
	"loop",
#ifdef HWRENDER
	"fovadd",
#endif
	NULL,
};

static int player_get(lua_State *L)
{
	player_t *plr = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	enum player_e field = Lua_optoption(L, 2, NULL, player_opt);
	lua_settop(L, 2);

	if (!plr)
	{
		if (field == player_valid)
		{
			lua_pushboolean(L, false);
			return 1;
		}
		return LUA_ErrInvalid(L, "player_t");
	}

	switch (field)
	{
	case player_valid:
		lua_pushboolean(L, true);
		break;
	case player_name:
		lua_pushstring(L, player_names[plr-players]);
		break;
	case player_realmo:
		LUA_PushUserdata(L, plr->mo, META_MOBJ);
		break;
	case player_mo:
		if (plr->spectator)
			lua_pushnil(L); // sigh
		else
			LUA_PushUserdata(L, plr->mo, META_MOBJ);
		break;
	case player_cmd:
		LUA_PushUserdata(L, &plr->cmd, META_TICCMD);
		break;
	case player_oldcmd:
		LUA_PushUserdata(L, &plr->oldcmd, META_TICCMD);
		break;
	case player_playerstate:
		lua_pushinteger(L, plr->playerstate);
		break;
	case player_health:
	{
		if (plr->mo)
			lua_pushinteger(L, plr->mo->health);
		else
			lua_pushinteger(L, 0);
		break;
	}
	case player_viewz:
		lua_pushfixed(L, plr->viewz);
		break;
	case player_viewheight:
		lua_pushfixed(L, plr->viewheight);
		break;
	case player_viewrollangle:
		lua_pushangle(L, plr->viewrollangle);
		break;
	case player_aiming:
		lua_pushangle(L, plr->aiming);
		break;
	case player_drawangle:
	case player_frameangle:
		lua_pushangle(L, plr->drawangle);
		break;
	case player_powers:
		LUA_PushUserdata(L, plr->powers, META_POWERS);
		break;
	case player_kartstuff:
		LUA_PushUserdata(L, plr->kartstuff, META_KARTSTUFF);
		break;
	case player_karthud:
		LUA_PushUserdata(L, plr->karthud, META_KARTHUD);
		break;
	case player_nocontrol:
		lua_pushinteger(L, plr->nocontrol);
		break;
	case player_carry:
		lua_pushinteger(L, plr->carry);
		break;
	case player_dye:
		lua_pushinteger(L, plr->dye);
		break;
	case player_position:
		lua_pushinteger(L, plr->position);
		break;
	case player_oldposition:
		lua_pushinteger(L, plr->oldposition);
		break;
	case player_positiondelay:
		lua_pushinteger(L, plr->positiondelay);
		break;
	case player_prevcheck:
		lua_pushinteger(L, plr->prevcheck);
		break;
	case player_nextcheck:
		lua_pushinteger(L, plr->nextcheck);
		break;
	case player_distancetofinish:
		lua_pushinteger(L, plr->distancetofinish);
		break;
	case player_distancetofinishprev:
		lua_pushinteger(L, plr->distancetofinishprev);
		break;
	case player_currentwaypoint:
		LUA_PushUserdata(L, plr->currentwaypoint, META_WAYPOINT);
		break;
	case player_nextwaypoint:
		LUA_PushUserdata(L, plr->nextwaypoint, META_WAYPOINT);
		break;
	case player_airtime:
		lua_pushinteger(L, plr->airtime);
		break;
	case player_bigwaypointgap:
		lua_pushinteger(L, plr->bigwaypointgap);
		break;
	case player_flashing:
		lua_pushinteger(L, plr->flashing);
		break;
	case player_spinouttimer:
		lua_pushinteger(L, plr->spinouttimer);
		break;
	case player_spinouttype:
		lua_pushinteger(L, plr->spinouttype);
		break;
	case player_flipovertimer:
		lua_pushinteger(L, plr->flipovertimer);
		break;
	case player_flipoverangle:
		lua_pushinteger(L, plr->flipoverangle);
		break;
	case player_instashield:
		lua_pushinteger(L, plr->instashield);
		break;
	case player_wipeoutslow:
		lua_pushinteger(L, plr->wipeoutslow);
		break;
	case player_justbumped:
		lua_pushinteger(L, plr->justbumped);
		break;
	case player_itemflags:
		lua_pushinteger(L, plr->itemflags);
		break;
	case player_outrun:
		lua_pushinteger(L, plr->outrun);
		break;
	case player_outruntime:
		lua_pushinteger(L, plr->outruntime);
		break;
	case player_drift:
		lua_pushinteger(L, plr->drift);
		break;
	case player_driftcharge:
		lua_pushinteger(L, plr->driftcharge);
		break;
	case player_driftboost:
		lua_pushinteger(L, plr->driftboost);
		break;
	case player_boostcharge:
		lua_pushinteger(L, plr->boostcharge);
		break;
	case player_slopeboost:
		lua_pushinteger(L, plr->slopeboost);
		break;
	case player_prevslopeboost:
		lua_pushinteger(L, plr->prevslopeboost);
		break;
	case player_slopeaccel:
		lua_pushinteger(L, plr->slopeaccel);
		break;
	case player_startboost:
		lua_pushinteger(L, plr->startboost);
		break;
	case player_dropdash:
		lua_pushinteger(L, plr->dropdash);
		break;
	case player_respawn:
		lua_pushinteger(L, plr->respawn);
		break;
	case player_aizdriftstrat:
		lua_pushinteger(L, plr->aizdriftstrat);
		break;
	case player_aizdrifttilt:
		lua_pushinteger(L, plr->aizdrifttilt);
		break;
	case player_aizdriftturn:
		lua_pushinteger(L, plr->aizdriftturn);
		break;
	case player_slipdashcharge:
		lua_pushfixed(L, plr->slipdashcharge);
		break;
	case player_slipdashdir:
		lua_pushinteger(L, plr->slipdashdir);
		break;
	case player_offroad:
		lua_pushinteger(L, plr->offroad);
		break;
	case player_pogospring:
		lua_pushinteger(L, plr->pogospring);
		break;
	case player_brakestop:
		lua_pushinteger(L, plr->brakestop);
		break;
	case player_waterskip:
		lua_pushinteger(L, plr->waterskip);
		break;
	case player_dashpadcooldown:
		lua_pushinteger(L, plr->dashpadcooldown);
		break;
	case player_boostpower:
		lua_pushinteger(L, plr->boostpower);
		break;
	case player_speedboost:
		lua_pushinteger(L, plr->speedboost);
		break;
	case player_accelboost:
		lua_pushinteger(L, plr->accelboost);
		break;
	case player_boostangle:
		lua_pushangle(L, plr->boostangle);
		break;
	case player_boostinfo:
		luaL_error(L, "Don't directly read boostinfo. Use speedboost, accelboost and numboosts instead,\n");
		break;
	case player_numsneakers:
		lua_pushinteger(L, plr->numsneakers);
		break;
	case player_numboosts:
		lua_pushinteger(L, plr->numboosts);
		break;
	case player_draftpower:
		lua_pushinteger(L, plr->draftpower);
		break;
	case player_draftleeway:
		lua_pushinteger(L, plr->draftleeway);
		break;
	case player_lastdraft:
		lua_pushinteger(L, plr->lastdraft);
		break;
	case player_tripwireState:
		lua_pushinteger(L, plr->tripwireState);
		break;
	case player_tripwirePass:
		lua_pushinteger(L, plr->tripwirePass);
		break;
	case player_tripwireLeniency:
		lua_pushinteger(L, plr->tripwireLeniency);
		break;
	case player_tripwireReboundDelay:
		lua_pushinteger(L, plr->tripwireReboundDelay);
		break;
	case player_itemroulette:
		lua_pushinteger(L, plr->itemroulette);
		break;
	case player_previtemroulette:
		lua_pushinteger(L, plr->previtemroulette);
		break;
	case player_itemblink:
		lua_pushinteger(L, plr->itemblink);
		break;
	case player_itemblinkmode:
		lua_pushinteger(L, plr->itemblinkmode);
		break;
	case player_roulettetype:
		lua_pushinteger(L, plr->roulettetype);
		break;
	case player_itemtype:
		lua_pushinteger(L, plr->itemtype);
		break;
	case player_itemamount:
		lua_pushinteger(L, plr->itemamount);
		break;
	case player_throwdir:
		lua_pushinteger(L, plr->throwdir);
		break;
	case player_sadtimer:
		lua_pushinteger(L, plr->sadtimer);
		break;
	case player_rings:
		lua_pushinteger(L, plr->rings);
		break;
	case player_ringmin:
		lua_pushinteger(L, plr->ringmin);
		break;
	case player_ringmax:
		lua_pushinteger(L, plr->ringmax);
		break;
	case player_pickuprings:
		lua_pushinteger(L, plr->pickuprings);
		break;
	case player_ringdelay:
		lua_pushinteger(L, plr->ringdelay);
		break;
	case player_ringlock:
		lua_pushinteger(L, plr->ringlock);
		break;
	case player_ringboost:
		lua_pushinteger(L, plr->ringboost);
		break;
	case player_ringtime:
		lua_pushinteger(L, plr->ringtime);
		break;
	case player_superring:
		lua_pushinteger(L, plr->superring);
		break;
	case player_nextringaward:
		lua_pushinteger(L, plr->nextringaward);
		break;
	case player_ringvolume:
		lua_pushinteger(L, plr->ringvolume);
		break;
	case player_ringtransparency:
		lua_pushinteger(L, plr->ringtransparency);
		break;
	case player_airdroptime:
		lua_pushinteger(L, plr->airdroptime);
		break;
	case player_curshield:
		lua_pushinteger(L, plr->curshield);
		break;
	case player_bubblecool:
		lua_pushinteger(L, plr->bubblecool);
		break;
	case player_bubbleblowup:
		lua_pushinteger(L, plr->bubbleblowup);
		break;
	case player_bubblehealth:
		lua_pushinteger(L, plr->bubblehealth);
		break;
	case player_flamedash:
		lua_pushinteger(L, plr->flamedash);
		break;
	case player_flametimer:
		lua_pushinteger(L, plr->flametimer);
		break;
	case player_flamestore:
		lua_pushinteger(L, plr->flamestore);
		break;
	case player_hyudorotimer:
		lua_pushinteger(L, plr->hyudorotimer);
		break;
	case player_stealingtimer:
		lua_pushinteger(L, plr->stealingtimer);
		break;
	case player_stolentimer:
		lua_pushinteger(L, plr->stolentimer);
		break;
	case player_sneakertimer:
		lua_pushinteger(L, plr->sneakertimer);
		break;
	case player_realsneakertimer:
		lua_pushinteger(L, plr->realsneakertimer);
		break;
	case player_floorboost:
		lua_pushinteger(L, plr->floorboost);
		break;
	case player_chaintimer:
		lua_pushinteger(L, plr->chaintimer);
		break;
	case player_growshrinktimer:
		lua_pushinteger(L, plr->growshrinktimer);
		break;
	case player_growcancel:
		lua_pushinteger(L, plr->growcancel);
		break;
	case player_squishedtimer:
		lua_pushinteger(L, plr->squishedtimer);
		break;
	case player_rocketsneakertimer:
		lua_pushinteger(L, plr->rocketsneakertimer);
		break;
	case player_invincibilitytimer:
		lua_pushinteger(L, plr->invincibilitytimer);
		break;
	case player_eggmanexplode:
		lua_pushinteger(L, plr->eggmanexplode);
		break;
	case player_eggmanblame:
		lua_pushinteger(L, plr->eggmanblame);
		break;
	case player_bananadrag:
		lua_pushinteger(L, plr->bananadrag);
		break;
	case player_lastjawztarget:
		lua_pushinteger(L, plr->lastjawztarget);
		break;
	case player_jawztargetdelay:
		lua_pushinteger(L, plr->jawztargetdelay);
		break;
	case player_confirmVictim:
		lua_pushinteger(L, plr->confirmVictim);
		break;
	case player_confirmVictimDelay:
		lua_pushinteger(L, plr->confirmVictimDelay);
		break;
	case player_glanceDir:
		lua_pushinteger(L, plr->glanceDir);
		break;
	case player_breathTimer:
		lua_pushinteger(L, plr->breathTimer);
		break;
	case player_lastsafelap:
		lua_pushinteger(L, plr->lastsafelap);
		break;
	case player_lastsafestarpost:
		lua_pushinteger(L, plr->lastsafestarpost);
		break;
	case player_interpoints:
		lua_pushinteger(L, plr->interpoints);
		break;
	case player_roundscore:
	case player_marescore:
		lua_pushinteger(L, plr->roundscore);
		break;
	case player_emeralds:
		lua_pushinteger(L, plr->emeralds);
		break;
	case player_bumpers:
		lua_pushinteger(L, plr->bumper);
		break;
	case player_karmadelay:
		lua_pushinteger(L, plr->karmadelay);
		break;
	case player_pflags:
	{
		UINT32 pflags = plr->pflags;

		if (lua_compatmode && (plr->carry & CR_SLIDING) == CR_SLIDING)
			pflags |= PF_SLIDING;

		if (lua_compatmode && (plr->oldcmd.buttons & BT_ATTACK))
			pflags |= PF_ATTACKDOWN;

		lua_pushinteger(L, pflags);
		break;
	}
	case player_panim:
		lua_pushinteger(L, plr->panim);
		break;
	case player_flashcount:
		lua_pushinteger(L, plr->flashcount);
		break;
	case player_flashpal:
		lua_pushinteger(L, plr->flashpal);
		break;
	case player_skincolor:
		lua_pushinteger(L, plr->skincolor);
		break;
	case player_skin:
		lua_pushinteger(L, plr->skin);
		break;
	case player_availabilities:
		lua_pushinteger(L, plr->availabilities);
		break;
	case player_score:
		lua_pushinteger(L, plr->score);
		break;
	// SRB2kart
	case player_kartspeed:
		lua_pushinteger(L, plr->kartspeed);
		break;
	case player_kartweight:
		lua_pushinteger(L, plr->kartweight);
		break;
	case player_followerskin:
		lua_pushinteger(L, plr->followerskin);
		break;
	case player_followerready:
		lua_pushboolean(L, plr->followerready);
		break;
	case player_followercolor:
		lua_pushinteger(L, plr->followercolor);
		break;
	case player_follower:
		LUA_PushUserdata(L, plr->follower, META_MOBJ);
		break;
	//
	case player_charflags:
		lua_pushinteger(L, plr->charflags);
		break;
	case player_followitem:
		lua_pushinteger(L, plr->followitem);
		break;
	case player_followmobj:
		LUA_PushUserdata(L, plr->followmobj, META_MOBJ);
		break;
	case player_lives:
		lua_pushinteger(L, plr->lives);
		break;
	case player_xtralife:
		lua_pushinteger(L, plr->xtralife);
		break;
	case player_speed:
		lua_pushfixed(L, plr->speed);
		break;
	case player_lastspeed:
		lua_pushfixed(L, plr->lastspeed);
		break;
	case player_deadtimer:
		lua_pushinteger(L, plr->deadtimer);
		break;
	case player_exiting:
		lua_pushinteger(L, plr->exiting);
		break;
	case player_cmomx:
		lua_pushfixed(L, plr->cmomx);
		break;
	case player_cmomy:
		lua_pushfixed(L, plr->cmomy);
		break;
	case player_rmomx:
		lua_pushfixed(L, plr->rmomx);
		break;
	case player_rmomy:
		lua_pushfixed(L, plr->rmomy);
		break;
	case player_totalring:
		lua_pushinteger(L, plr->totalring);
		break;
	case player_realtime:
		lua_pushinteger(L, plr->realtime);
		break;
	case player_laps:
	{
		if (lua_compatmode)
			lua_pushinteger(L, max(plr->laps - 1, 0));
		else
			lua_pushinteger(L, plr->laps);
		break;
	}
	case player_latestlap:
	{
		if (lua_compatmode)
			lua_pushinteger(L, max(plr->latestlap - 1, 0));
		else
			lua_pushinteger(L, plr->latestlap);
		break;
	}
	case player_ctfteam:
		lua_pushinteger(L, plr->ctfteam);
		break;
	case player_checkskip:
		lua_pushinteger(L, plr->checkskip);
		break;
	case player_starpostx:
		lua_pushfixed(L, plr->starpostx);
		break;
	case player_starposty:
		lua_pushfixed(L, plr->starposty);
		break;
	case player_starpostz:
		lua_pushfixed(L, plr->starpostz);
		break;
	case player_starpostangle:
		lua_pushangle(L, plr->starpostangle);
		break;
	case player_starpostflip:
		lua_pushboolean(L, plr->starpostflip);
		break;
	case player_starpostnum:
		lua_pushinteger(L, plr->starpostnum);
		break;
	case player_starposttime:
		lua_pushinteger(L, plr->starposttime);
		break;
	case player_lastsidehit:
		lua_pushinteger(L, plr->lastsidehit);
		break;
	case player_lastlinehit:
		lua_pushinteger(L, plr->lastlinehit);
		break;
	case player_onconveyor:
		lua_pushinteger(L, plr->onconveyor);
		break;
	case player_awayviewmobj:
		LUA_PushUserdata(L, plr->awayviewmobj, META_MOBJ);
		break;
	case player_awayviewtics:
		lua_pushinteger(L, plr->awayviewtics);
		break;
	case player_awayviewaiming:
		lua_pushangle(L, plr->awayviewaiming);
		break;
	case player_spectator:
		lua_pushboolean(L, plr->spectator);
		break;
	case player_spectatewait:
		lua_pushinteger(L, plr->spectatewait);
		break;
	case player_bot:
		lua_pushboolean(L, plr->bot);
		break;
	case player_botvars:
		LUA_PushUserdata(L, &plr->botvars, META_BOTVARS);
		break;
	case player_jointime:
		lua_pushinteger(L, plr->jointime);
		break;
	case player_spectatorreentry:
		lua_pushinteger(L, plr->spectatorreentry);
		break;
	case player_grieftime:
		lua_pushinteger(L, plr->grieftime);
		break;
	case player_griefstrikes:
		lua_pushinteger(L, plr->griefstrikes);
		break;
	case player_splitscreenindex:
		lua_pushinteger(L, plr->splitscreenindex);
		break;
	case player_bumpertime:
		lua_pushinteger(L, plr->bumpertime);
		break;
	case player_ping:
		lua_pushinteger(L, playerpingtable[( plr - players )]);
		break;
	case player_packetloss:
		lua_pushinteger(L, playerpacketlosstable[plr - players]);
		break;
	case player_loop:
		LUA_PushUserdata(L, &plr->loop, META_SONICLOOPVARS);
		break;
#ifdef HWRENDER
	case player_fovadd:
		lua_pushfixed(L, plr->fovadd);
		break;
#endif
	default:
		lua_getfield(L, LUA_REGISTRYINDEX, LREG_EXTVARS);
		I_Assert(lua_istable(L, -1));
		lua_pushlightuserdata(L, plr);
		lua_rawget(L, -2);
		if (!lua_istable(L, -1)) { // no extra values table
			CONS_Debug(DBG_LUA, M_GetText("'%s' has no extvars table or field named '%s'; returning nil.\n"), "player_t", lua_tostring(L, 2));
			return 0;
		}
		lua_pushvalue(L, 2); // field name
		lua_gettable(L, -2);
		if (lua_isnil(L, -1)) // no value for this field
			CONS_Debug(DBG_LUA, M_GetText("'%s' has no field named '%s'; returning nil.\n"), "player_t", lua_tostring(L, 2));
		break;
	}

	return 1;
}

#define NOSET luaL_error(L, LUA_QL("player_t") " field " LUA_QS " should not be set directly.", field)
static int player_set(lua_State *L)
{
	player_t *plr = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	enum player_e field = Lua_optoption(L, 2, player_opt[player_cmd], player_opt);
	if (!plr)
		return LUA_ErrInvalid(L, "player_t");

	if (hud_running)
		return luaL_error(L, "Do not alter player_t in HUD rendering code!");
	if (hook_cmd_running)
		return luaL_error(L, "Do not alter player_t in CMD building code!");

	switch (field)
	{
	case player_mo:
	case player_realmo:
	{
		mobj_t *newmo = *((mobj_t **)luaL_checkudata(L, 3, META_MOBJ));
		plr->mo->player = NULL; // remove player pointer from old mobj
		(newmo->player = plr)->mo = newmo; // set player pointer for new mobj, and set new mobj as the player's mobj
		break;
	}
	case player_cmd:
		return NOSET;
	case player_oldcmd:
		return NOSET;
	case player_playerstate:
		plr->playerstate = luaL_checkinteger(L, 3);
		break;
	case player_health:
		if (plr->mo)
			plr->mo->health = luaL_checkinteger(L, 3);
		break;
	case player_viewz:
		plr->viewz = luaL_checkfixed(L, 3);
		break;
	case player_viewheight:
		plr->viewheight = luaL_checkfixed(L, 3);
		break;
	case player_viewrollangle:
		plr->viewrollangle = luaL_checkangle(L, 3);
		break;
	case player_aiming:
	{
		UINT8 i;
		plr->aiming = luaL_checkangle(L, 3);
		for (i = 0; i <= r_splitscreen; i++)
		{
			if (plr == &players[displayplayers[i]])
			{
				localaiming[i] = plr->aiming;
				break;
			}
		}
		break;
	}
	case player_drawangle:
	case player_frameangle:
	{
		angle_t angle = luaL_checkangle(L, 3);
		if (lua_compatmode && plr->mo && angle == plr->mo->angle)
			// attempt to fix e.g. acrobasics not accounting for drift when resetting frameangle
			angle += K_GetDriftAngleOffset(plr);
		plr->drawangle = angle;
		break;
	}
	case player_pflags:
	{
		UINT32 pflags = luaL_checkinteger(L, 3);

		if (lua_compatmode)
		{
			if (pflags & PF_SLIDING)
				plr->carry |= CR_SLIDING;
			else
				plr->carry &= ~CR_SLIDING;

			if (pflags & PF_ATTACKDOWN)
				plr->oldcmd.buttons |= BT_ATTACK;
			else
				plr->oldcmd.buttons &= ~BT_ATTACK;
		}

		plr->pflags = pflags;
		break;
	}
	case player_panim:
		plr->panim = luaL_checkinteger(L, 3);
		break;
	case player_flashcount:
		plr->flashcount = luaL_checkinteger(L, 3);
		break;
	case player_flashpal:
		plr->flashpal = luaL_checkinteger(L, 3);
		break;
	case player_skincolor:
	{
		UINT16 newcolor = luaL_checkinteger(L,3);
		if (newcolor >= numskincolors)
			return luaL_error(L, "player.skincolor %d out of range (0 - %d).", newcolor, numskincolors-1);
		plr->skincolor = newcolor;
		break;
	}
	case player_skin:
		return NOSET;
	case player_availabilities:
		return NOSET;
	case player_score:
		plr->score = luaL_checkinteger(L, 3);
		break;
	// SRB2kart
	case player_kartstuff:
		return NOSET;
	case player_nocontrol:
		plr->nocontrol = luaL_checkinteger(L, 3);
		break;
	case player_carry:
		plr->carry = luaL_checkinteger(L, 3);
		break;
	case player_dye:
		plr->dye = luaL_checkinteger(L, 3);
		break;
	case player_position:
		plr->position = luaL_checkinteger(L, 3);
		break;
	case player_oldposition:
		plr->oldposition = luaL_checkinteger(L, 3);
		break;
	case player_positiondelay:
		plr->positiondelay = luaL_checkinteger(L, 3);
		break;
	case player_prevcheck:
		plr->prevcheck = luaL_checkinteger(L, 3);
		break;
	case player_nextcheck:
		plr->nextcheck = luaL_checkinteger(L, 3);
		break;
	case player_distancetofinish:
		plr->distancetofinish = luaL_checkfixed(L, 3);
		break;
	case player_distancetofinishprev:
		plr->distancetofinishprev = luaL_checkfixed(L, 3);
		break;
	case player_currentwaypoint:
		return NOSET;
		break;
	case player_nextwaypoint:
		return NOSET;
		break;
	case player_airtime:
		plr->airtime = luaL_checkinteger(L, 3);
		break;
	case player_bigwaypointgap:
		plr->bigwaypointgap = luaL_checkinteger(L, 3);
		break;
	case player_flashing:
		plr->flashing = luaL_checkinteger(L, 3);
		break;
	case player_spinouttimer:
		plr->spinouttimer = luaL_checkinteger(L, 3);
		break;
	case player_spinouttype:
		plr->spinouttype = luaL_checkinteger(L, 3);
		break;
	case player_flipovertimer:
		plr->flipovertimer = luaL_checkinteger(L, 3);
		break;
	case player_flipoverangle:
		plr->flipoverangle = luaL_checkinteger(L, 3);
		break;
	case player_instashield:
		plr->instashield = luaL_checkinteger(L, 3);
		break;
	case player_wipeoutslow:
		plr->wipeoutslow = luaL_checkinteger(L, 3);
		break;
	case player_justbumped:
		plr->justbumped = luaL_checkinteger(L, 3);
		break;
	case player_itemflags:
		plr->itemflags = luaL_checkinteger(L, 3);
		break;
	case player_outrun:
		plr->outrun = luaL_checkinteger(L, 3);
		break;
	case player_outruntime:
		plr->outruntime = luaL_checkinteger(L, 3);
		break;
	case player_drift:
		plr->drift = luaL_checkinteger(L, 3);
		break;
	case player_driftcharge:
		plr->driftcharge = luaL_checkinteger(L, 3);
		break;
	case player_driftboost:
		plr->driftboost = luaL_checkinteger(L, 3);
		break;
	case player_boostcharge:
		plr->boostcharge = luaL_checkinteger(L, 3);
		break;
	case player_slopeboost:
		plr->slopeboost = luaL_checkinteger(L, 3);
		break;
	case player_prevslopeboost:
		plr->prevslopeboost = luaL_checkinteger(L, 3);
		break;
	case player_slopeaccel:
		plr->slopeaccel = luaL_checkinteger(L, 3);
		break;
	case player_startboost:
		plr->startboost = luaL_checkinteger(L, 3);
		break;
	case player_dropdash:
		plr->dropdash = luaL_checkinteger(L, 3);
		break;
	case player_respawn:
		plr->respawn = luaL_checkinteger(L, 3);
		break;
	case player_aizdriftstrat:
		plr->aizdriftstrat = luaL_checkinteger(L, 3);
		break;
	case player_aizdrifttilt:
		plr->aizdrifttilt = luaL_checkinteger(L, 3);
		break;
	case player_aizdriftturn:
		plr->aizdriftturn = luaL_checkinteger(L, 3);
		break;
	case player_slipdashcharge:
		plr->slipdashcharge = luaL_checkfixed(L, 3);
		break;
	case player_slipdashdir:
		plr->slipdashdir = luaL_checkinteger(L, 3);
		break;
	case player_offroad:
		plr->offroad = luaL_checkinteger(L, 3);
		break;
	case player_pogospring:
		plr->pogospring = luaL_checkinteger(L, 3);
		break;
	case player_brakestop:
		plr->brakestop = luaL_checkinteger(L, 3);
		break;
	case player_waterskip:
		plr->waterskip = luaL_checkinteger(L, 3);
		break;
	case player_dashpadcooldown:
		plr->dashpadcooldown = luaL_checkinteger(L, 3);
		break;
	case player_boostpower:
		plr->boostpower = luaL_checkinteger(L, 3);
		break;
	case player_speedboost:
		plr->speedboost = luaL_checkinteger(L, 3);
		break;
	case player_accelboost:
		plr->accelboost = luaL_checkinteger(L, 3);
		break;
	case player_boostangle:
		plr->boostangle = luaL_checkangle(L, 3);
		break;
	case player_boostinfo:
		NOSET;
		break;
	case player_numsneakers:
		plr->numsneakers = luaL_checkinteger(L, 3);
		break;
	case player_numboosts:
		plr->numboosts = luaL_checkinteger(L, 3);
		break;
	case player_draftpower:
		plr->draftpower = luaL_checkinteger(L, 3);
		break;
	case player_draftleeway:
		plr->draftleeway = luaL_checkinteger(L, 3);
		break;
	case player_lastdraft:
		plr->lastdraft = luaL_checkinteger(L, 3);
		break;
	case player_tripwireState:
		plr->tripwireState = luaL_checkinteger(L, 3);
		break;
	case player_tripwirePass:
		plr->tripwirePass = luaL_checkinteger(L, 3);
		break;
	case player_tripwireLeniency:
		plr->tripwireLeniency = luaL_checkinteger(L, 3);
		break;
	case player_tripwireReboundDelay:
		plr->tripwireReboundDelay = luaL_checkinteger(L, 3);
		break;
	case player_itemroulette:
		plr->itemroulette = luaL_checkinteger(L, 3);
		break;
	case player_previtemroulette:
		plr->previtemroulette = luaL_checkinteger(L, 3);
		break;
	case player_itemblink:
		plr->itemblink = luaL_checkinteger(L, 3);
		break;
	case player_itemblinkmode:
		plr->itemblinkmode = luaL_checkinteger(L, 3);
		break;
	case player_roulettetype:
		plr->roulettetype = luaL_checkinteger(L, 3);
		break;
	case player_itemtype:
		plr->itemtype = luaL_checkinteger(L, 3);
		break;
	case player_itemamount:
		plr->itemamount = luaL_checkinteger(L, 3);
		break;
	case player_throwdir:
		plr->throwdir = luaL_checkinteger(L, 3);
		break;
	case player_sadtimer:
		plr->sadtimer = luaL_checkinteger(L, 3);
		break;
	case player_rings:
		plr->rings = luaL_checkinteger(L, 3);
		break;
	case player_ringmin:
		plr->ringmin = luaL_checkinteger(L, 3);
		break;
	case player_ringmax:
		plr->ringmax = luaL_checkinteger(L, 3);
		break;
	case player_pickuprings:
		plr->pickuprings = luaL_checkinteger(L, 3);
		break;
	case player_ringdelay:
		plr->ringdelay = luaL_checkinteger(L, 3);
		break;
	case player_ringlock:
		plr->ringlock = luaL_checkinteger(L, 3);
		break;
	case player_ringboost:
		plr->ringboost = luaL_checkinteger(L, 3);
		break;
	case player_ringtime:
		plr->ringtime = luaL_checkinteger(L, 3);
		break;
	case player_superring:
		plr->superring = luaL_checkinteger(L, 3);
		break;
	case player_nextringaward:
		plr->nextringaward = luaL_checkinteger(L, 3);
		break;
	case player_ringvolume:
		plr->ringvolume = luaL_checkinteger(L, 3);
		break;
	case player_ringtransparency:
		plr->ringtransparency = luaL_checkinteger(L, 3);
		break;
	case player_airdroptime:
		plr->airdroptime = luaL_checkinteger(L, 3);
		break;
	case player_curshield:
		plr->curshield = luaL_checkinteger(L, 3);
		break;
	case player_bubblecool:
		plr->bubblecool = luaL_checkinteger(L, 3);
		break;
	case player_bubbleblowup:
		plr->bubbleblowup = luaL_checkinteger(L, 3);
		break;
	case player_bubblehealth:
		plr->bubblehealth = luaL_checkinteger(L, 3);
		break;
	case player_flamedash:
		plr->flamedash = luaL_checkinteger(L, 3);
		break;
	case player_flametimer:
		plr->flametimer = luaL_checkinteger(L, 3);
		break;
	case player_flamestore:
		plr->flamestore = luaL_checkinteger(L, 3);
		break;
	case player_hyudorotimer:
		plr->hyudorotimer = luaL_checkinteger(L, 3);
		break;
	case player_stealingtimer:
		plr->stealingtimer = luaL_checkinteger(L, 3);
		break;
	case player_stolentimer:
		plr->stealingtimer = luaL_checkinteger(L, 3);
		break;
	case player_sneakertimer:
		plr->sneakertimer = luaL_checkinteger(L, 3);
		break;
	case player_realsneakertimer:
		plr->realsneakertimer = luaL_checkinteger(L, 3);
		break;
	case player_floorboost:
		plr->floorboost = luaL_checkinteger(L, 3);
		break;
	case player_chaintimer:
		plr->chaintimer = luaL_checkinteger(L, 3);
		break;
	case player_growshrinktimer:
		plr->growshrinktimer = luaL_checkinteger(L, 3);
		break;
	case player_growcancel:
		plr->growcancel = luaL_checkinteger(L, 3);
		break;
	case player_squishedtimer:
		plr->squishedtimer = luaL_checkinteger(L, 3);
		break;
	case player_rocketsneakertimer:
		plr->rocketsneakertimer = luaL_checkinteger(L, 3);
		break;
	case player_invincibilitytimer:
		plr->invincibilitytimer = luaL_checkinteger(L, 3);
		break;
	case player_eggmanexplode:
		plr->eggmanexplode = luaL_checkinteger(L, 3);
		break;
	case player_eggmanblame:
		plr->eggmanblame = luaL_checkinteger(L, 3);
		break;
	case player_bananadrag:
		plr->bananadrag = luaL_checkinteger(L, 3);
		break;
	case player_lastjawztarget:
		plr->lastjawztarget = luaL_checkinteger(L, 3);
		break;
	case player_jawztargetdelay:
		plr->jawztargetdelay = luaL_checkinteger(L, 3);
		break;
	case player_confirmVictim:
		plr->confirmVictim = luaL_checkinteger(L, 3);
		break;
	case player_confirmVictimDelay:
		plr->confirmVictimDelay = luaL_checkinteger(L, 3);
		break;
	case player_glanceDir:
		plr->glanceDir = luaL_checkinteger(L, 3);
		break;
	case player_breathTimer:
		plr->breathTimer = luaL_checkinteger(L, 3);
		break;
	case player_lastsafelap:
		plr->lastsafelap = luaL_checkinteger(L, 3);
		break;
	case player_lastsafestarpost:
		plr->lastsafestarpost = luaL_checkinteger(L, 3);
		break;
	case player_interpoints:
		plr->interpoints = luaL_checkinteger(L, 3);
		break;
	case player_roundscore:
	case player_marescore:
		plr->roundscore = luaL_checkinteger(L, 3);
		break;
	case player_emeralds:
		plr->emeralds = luaL_checkinteger(L, 3);
		break;
	case player_bumpers:
		plr->bumper = luaL_checkinteger(L, 3);
		break;
	case player_karmadelay:
		plr->karmadelay = luaL_checkinteger(L, 3);
		break;
	case player_kartspeed:
		plr->kartspeed = luaL_checkinteger(L, 3);
		break;
	case player_kartweight:
		plr->kartweight = luaL_checkinteger(L, 3);
		break;
	case player_followerskin:
		plr->followerskin = luaL_checkinteger(L, 3);
		break;
	case player_followercolor:
		plr->followercolor = luaL_checkinteger(L, 3);
		break;
	case player_followerready:
		plr->followerready = luaL_checkboolean(L, 3);
		break;
	case player_follower:	// it's probably best we don't allow the follower mobj to change.
		return NOSET;
	//
	case player_charflags:
		plr->charflags = (UINT32)luaL_checkinteger(L, 3);
		break;
	case player_followitem:
		plr->followitem = luaL_checkinteger(L, 3);
		break;
	case player_followmobj:
	{
		mobj_t *mo = NULL;
		if (!lua_isnil(L, 3))
			mo = *((mobj_t **)luaL_checkudata(L, 3, META_MOBJ));
		P_SetTarget(&plr->followmobj, mo);
		break;
	}
	case player_lives:
		plr->lives = (SINT8)luaL_checkinteger(L, 3);
		break;
	case player_xtralife:
		plr->xtralife = (SINT8)luaL_checkinteger(L, 3);
		break;
	case player_speed:
		plr->speed = luaL_checkfixed(L, 3);
		break;
	case player_lastspeed:
		plr->lastspeed = luaL_checkfixed(L, 3);
		break;
	case player_deadtimer:
		plr->deadtimer = (INT32)luaL_checkinteger(L, 3);
		break;
	case player_exiting:
		plr->exiting = (tic_t)luaL_checkinteger(L, 3);
		break;
	case player_cmomx:
		plr->cmomx = luaL_checkfixed(L, 3);
		break;
	case player_cmomy:
		plr->cmomy = luaL_checkfixed(L, 3);
		break;
	case player_rmomx:
		plr->rmomx = luaL_checkfixed(L, 3);
		break;
	case player_rmomy:
		plr->rmomy = luaL_checkfixed(L, 3);
		break;
	case player_totalring:
		plr->totalring = (INT16)luaL_checkinteger(L, 3);
		break;
	case player_realtime:
		plr->realtime = (tic_t)luaL_checkinteger(L, 3);
		break;
	case player_laps:
	{
		UINT8 laps = (UINT8)luaL_checkinteger(L, 3);
		if (lua_compatmode)
		{
			if (!K_UsingLegacyCheckpoints())
			{
				plr->pflags |= PF_TRUSTWAYPOINTS;
			}

			plr->laps = max(laps +1, 0);
		}
		else
			plr->laps = laps;
		break;
	}
	case player_latestlap:
	{
		UINT8 laps = (UINT8)luaL_checkinteger(L, 3);

		if (lua_compatmode)
			plr->latestlap = max(laps + 1, 0);
		else
			plr->latestlap = laps;
		break;
	}
	case player_ctfteam:
		plr->ctfteam = (INT32)luaL_checkinteger(L, 3);
		break;
	case player_checkskip:
		plr->checkskip = (INT32)luaL_checkinteger(L, 3);
		break;
	case player_starpostx:
		plr->starpostx = luaL_checkfixed(L, 3);
		break;
	case player_starposty:
		plr->starposty = luaL_checkfixed(L, 3);
		break;
	case player_starpostz:
		plr->starpostz = luaL_checkfixed(L, 3);
		break;
	case player_starpostangle:
		plr->starpostangle = luaL_checkangle(L, 3);
		break;
	case player_starpostflip:
		plr->starpostflip = luaL_checkboolean(L, 3);
		break;
	case player_starpostnum:
		plr->starpostnum = (INT32)luaL_checkinteger(L, 3);
		break;
	case player_starposttime:
		plr->starposttime = (tic_t)luaL_checkinteger(L, 3);
		break;
	case player_lastsidehit:
		plr->lastsidehit = (INT16)luaL_checkinteger(L, 3);
		break;
	case player_lastlinehit:
		plr->lastlinehit = (INT16)luaL_checkinteger(L, 3);
		break;
	case player_onconveyor:
		plr->onconveyor = (INT32)luaL_checkinteger(L, 3);
		break;
	case player_awayviewmobj:
	{
		mobj_t *mo = NULL;
		if (!lua_isnil(L, 3))
			mo = *((mobj_t **)luaL_checkudata(L, 3, META_MOBJ));
		P_SetTarget(&plr->awayviewmobj, mo);
		break;
	}
	case player_awayviewtics:
	{
		plr->awayviewtics = (INT32)luaL_checkinteger(L, 3);
		if (plr->awayviewtics && !plr->awayviewmobj) // awayviewtics must ALWAYS have an awayviewmobj set!!
			P_SetTarget(&plr->awayviewmobj, plr->mo); // but since the script might set awayviewmobj immediately AFTER setting awayviewtics, use player mobj as filler for now.
		break;
	}
	case player_awayviewaiming:
		plr->awayviewaiming = luaL_checkangle(L, 3);
		break;
	case player_spectator:
		plr->spectator = lua_toboolean(L, 3);
		break;
	case player_spectatewait:
		plr->spectatewait = luaL_checkinteger(L, 3);
		break;
	case player_bot:
		return NOSET;
	case player_botvars:
		return NOSET;
	case player_jointime:
		plr->jointime = (tic_t)luaL_checkinteger(L, 3);
		break;
	case player_spectatorreentry:
		plr->spectatorreentry = (tic_t)luaL_checkinteger(L, 3);
		break;
	case player_grieftime:
		plr->grieftime = (tic_t)luaL_checkinteger(L, 3);
		break;
	case player_griefstrikes:
		plr->griefstrikes = (UINT8)luaL_checkinteger(L, 3);
		break;
	case player_splitscreenindex:
		return NOSET;
	case player_bumpertime:
		plr->bumpertime = (tic_t)luaL_checkinteger(L, 3);
		break;
	case player_ping:
		return NOSET;
	case player_packetloss:
		return NOSET;
	case player_loop:
		return NOSET;
#ifdef HWRENDER
	case player_fovadd:
		plr->fovadd = luaL_checkfixed(L, 3);
		break;
#endif
	default:
		lua_getfield(L, LUA_REGISTRYINDEX, LREG_EXTVARS);
		I_Assert(lua_istable(L, -1));
		lua_pushlightuserdata(L, plr);
		lua_rawget(L, -2);
		if (lua_isnil(L, -1)) {
			// This index doesn't have a table for extra values yet, let's make one.
			lua_pop(L, 1);
			CONS_Debug(DBG_LUA, M_GetText("'%s' has no field named '%s'; adding it as Lua data.\n"), "player_t", lua_tostring(L, 2));
			lua_newtable(L);
			lua_pushlightuserdata(L, plr);
			lua_pushvalue(L, -2); // ext value table
			lua_rawset(L, -4); // LREG_EXTVARS table
		}
		lua_pushvalue(L, 2); // key
		lua_pushvalue(L, 3); // value to store
		lua_settable(L, -3);
		lua_pop(L, 2);
		break;
	}

	return 0;
}

#undef NOSET

static int player_num(lua_State *L)
{
	player_t *plr = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	if (!plr)
		return luaL_error(L, "accessed player_t doesn't exist anymore.");
	lua_pushinteger(L, plr-players);
	return 1;
}

// powers, p -> powers[p]
static int power_get(lua_State *L)
{
	UINT16 *powers = *((UINT16 **)luaL_checkudata(L, 1, META_POWERS));
	powertype_t p = luaL_checkinteger(L, 2);
	player_t *plr = (player_t*)((char*)powers - offsetof(player_t, powers));
	if (p >= NUMPOWERS)
		return luaL_error(L, LUA_QL("powertype_t") " cannot be %u", p);
	switch (p)
	{
		case pw_flashing:
			lua_pushinteger(L, plr->flashing);
			return 1;
		case pw_nocontrol:
			lua_pushinteger(L, plr->nocontrol);
			return 1;
		case pw_emeralds:
			lua_pushinteger(L, plr->emeralds);
			return 1;
		default:
			lua_pushinteger(L, powers[p]);
			return 1;
	}
	return 1;
}

#define NOSET luaL_error(L, LUA_QL("powertype_t") " field " LUA_QS " should not be set directly.", p)
// powers, p, value -> powers[p] = value
static int power_set(lua_State *L)
{
	UINT16 *powers = *((UINT16 **)luaL_checkudata(L, 1, META_POWERS));
	powertype_t p = luaL_checkinteger(L, 2);
	player_t *plr = (player_t*)((char*)powers - offsetof(player_t, powers));
	UINT16 i = (UINT16)luaL_checkinteger(L, 3);
	if (p >= NUMPOWERS)
		return luaL_error(L, LUA_QL("powertype_t") " cannot be %u", p);
	if (hud_running)
		return luaL_error(L, "Do not alter player_t in HUD rendering code!");
	if (hook_cmd_running)
		return luaL_error(L, "Do not alter player_t in BuildCMD code!");
	switch (p)
	{
		case pw_flashing:
			plr->flashing = i;
			break;
		case pw_nocontrol:
			plr->nocontrol = i;
			break;
		case pw_emeralds:
			plr->emeralds = i;
			break;
		default:
			powers[p] = i;
			break;
	}
	return 0;
}
#undef NOSET

// #powers -> NUMPOWERS
static int power_len(lua_State *L)
{
	lua_pushinteger(L, NUMPOWERS);
	return 1;
}


// ???, ks -> kartstuff[ks]
static int kartstuff_get(lua_State *L)
{
	INT32 *kartstuff = *((INT32 **)luaL_checkudata(L, 1, META_KARTSTUFF));
	player_t *plr = (player_t*)((char*)kartstuff - offsetof(player_t, kartstuff));
	kartstufftype_t ks = luaL_checkinteger(L, 2);

	if (ks >= NUMKARTSTUFF)
		return luaL_error(L, LUA_QL("kartstufftype_t") " cannot be %u", ks);

	switch (ks)
	{
		case k_position:
			lua_pushinteger(L, plr->position);
			return 1;
		case k_oldposition:
			lua_pushinteger(L, plr->oldposition);
			return 1;
		case k_positiondelay:
			lua_pushinteger(L, plr->positiondelay);
			return 1;
		case k_prevcheck:
			lua_pushinteger(L, plr->prevcheck);
			return 1;
		case k_nextcheck:
			lua_pushinteger(L, plr->nextcheck);
			return 1;
		/*case k_waypoint:
			lua_pushinteger(L, plr->waypoint);
			return 1;*/
		/*case k_starpostwp:
			lua_pushinteger(L, plr->starpostwp);
			return 1;*/
		case k_starpostflip:
			lua_pushinteger(L, plr->starpostflip);
			return 1;
		case k_respawn:
			lua_pushinteger(L, plr->respawn);
			return 1;
		case k_dropdash:
			lua_pushinteger(L, plr->dropdash);
			return 1;
		case k_throwdir:
			lua_pushinteger(L, plr->throwdir);
			return 1;

		case k_lapanimation:
			lua_pushinteger(L, plr->karthud[khud_lapanimation]);
			return 1;
		case k_laphand:
			lua_pushinteger(L, plr->karthud[khud_laphand]);
			return 1;
		case k_cardanimation:
			lua_pushinteger(L, plr->karthud[khud_cardanimation]);
			return 1;
		case k_voices:
			lua_pushinteger(L, plr->karthud[khud_voices]);
			return 1;
		case k_tauntvoices:
			lua_pushinteger(L, plr->karthud[khud_tauntvoices]);
			return 1;
		case k_instashield:
			lua_pushinteger(L, plr->instashield);
			return 1;
		case k_enginesnd:
			lua_pushinteger(L, plr->karthud[khud_enginesnd]);
			return 1;
		case k_floorboost:
			lua_pushinteger(L, plr->floorboost);
			return 1;
		case k_spinouttype:
			lua_pushinteger(L, plr->spinouttype);
			return 1;
		case k_drift:
			lua_pushinteger(L, plr->drift);
			return 1;
		case k_driftend:
			lua_pushinteger(L, (plr->pflags & PF_DRIFTEND));
			return 1;
		case k_driftcharge:
			lua_pushinteger(L, plr->driftcharge);
			return 1;
		case k_driftboost:
			lua_pushinteger(L, plr->driftboost);
			return 1;
		case k_boostcharge:
			lua_pushinteger(L, plr->boostcharge);
			return 1;
		case k_startboost:
			lua_pushinteger(L, plr->startboost);
			return 1;
		case k_jmp:
			lua_pushinteger(L, (plr->pflags & PF_DRIFTINPUT));
			return 1;
		case k_offroad:
			lua_pushinteger(L, plr->offroad);
			return 1;
		case k_pogospring:
			lua_pushinteger(L, plr->pogospring);
			return 1;
		case k_brakestop:
			lua_pushinteger(L, plr->brakestop);
			return 1;
		case k_waterskip:
			lua_pushinteger(L, plr->waterskip);
			return 1;
		case k_dashpadcooldown:
			lua_pushinteger(L, plr->dashpadcooldown);
			return 1;
		case k_boostpower:
			lua_pushinteger(L, plr->boostpower);
			return 1;
		case k_speedboost:
			lua_pushinteger(L, plr->speedboost);
			return 1;
		case k_accelboost:
			lua_pushinteger(L, plr->accelboost);
			return 1;
		case k_boostangle:
			lua_pushinteger(L, plr->boostangle);
			return 1;
		case k_boostcam:
			lua_pushinteger(L, plr->karthud[khud_boostcam]);
			return 1;
		case k_destboostcam:
			lua_pushinteger(L, plr->karthud[khud_destboostcam]);
			return 1;
		case k_timeovercam:
			lua_pushinteger(L, plr->karthud[khud_timeovercam]);
			return 1;
		case k_aizdriftstrat:
			lua_pushinteger(L, plr->aizdriftstrat);
			return 1;
		case k_brakedrift:
			lua_pushinteger(L, (plr->pflags & PF_BRAKEDRIFT));
			return 1;
		case k_itemroulette:
			lua_pushinteger(L, plr->itemroulette);
			return 1;
		case k_roulettetype:
			lua_pushinteger(L, plr->roulettetype);
			return 1;
		case k_itemtype:
			lua_pushinteger(L, plr->itemtype);
			return 1;
		case k_itemamount:
			lua_pushinteger(L, plr->itemamount);
			return 1;
		case k_itemheld:
			lua_pushinteger(L, (plr->itemflags & IF_ITEMOUT));
			return 1;
		case k_curshield:
			lua_pushinteger(L, plr->curshield);
			return 1;
		case k_hyudorotimer:
			lua_pushinteger(L, plr->hyudorotimer);
			return 1;
		case k_stealingtimer:
			lua_pushinteger(L, plr->stealingtimer);
			return 1;
		case k_stolentimer:
			lua_pushinteger(L, plr->stolentimer);
			return 1;
		case k_sneakertimer:
			lua_pushinteger(L, plr->sneakertimer);
			return 1;
		case k_growshrinktimer:
			lua_pushinteger(L, plr->growshrinktimer);
			return 1;
		case k_squishedtimer:
			lua_pushinteger(L, plr->squishedtimer);
			return 1;
		case k_rocketsneakertimer:
			lua_pushinteger(L, plr->rocketsneakertimer);
			return 1;
		case k_invincibilitytimer:
			lua_pushinteger(L, plr->invincibilitytimer);
			return 1;
		case k_eggmanheld:
			lua_pushinteger(L, (plr->itemflags & IF_EGGMANOUT));
			return 1;
		case k_eggmanexplode:
			lua_pushinteger(L, plr->eggmanexplode);
			return 1;
		case k_eggmanblame:
			lua_pushinteger(L, plr->eggmanblame);
			return 1;
		case k_lastjawztarget:
			lua_pushinteger(L, plr->lastjawztarget);
			return 1;
		case k_bananadrag:
			lua_pushinteger(L, plr->bananadrag);
			return 1;
		case k_spinouttimer:
			lua_pushinteger(L, plr->spinouttimer);
			return 1;
		case k_wipeoutslow:
			lua_pushinteger(L, plr->wipeoutslow);
			return 1;
		case k_justbumped:
			lua_pushinteger(L, plr->justbumped);
			return 1;
		case k_comebacktimer:
			lua_pushinteger(L, plr->karmadelay);
			return 1;
		case k_sadtimer:
			lua_pushinteger(L, plr->sadtimer);
			return 1;

		// Battle Mode vars
		case k_bumper:
			lua_pushinteger(L, plr->bumper);
			return 1;
		case k_comebackpoints:
			lua_pushinteger(L, plr->karmapoints);
			return 1;
		case k_comebackmode:
			lua_pushinteger(L, plr->karmamode);
			return 1;
		case k_wanted:
			lua_pushinteger(L, plr->wanted);
			return 1;
		case k_yougotem:
			lua_pushinteger(L, plr->karthud[khud_yougotem]);
			return 1;

		// v1.0.2+ vars
		case k_itemblink:
			lua_pushinteger(L, plr->itemblink);
			return 1;
		case k_itemblinkmode:
			lua_pushinteger(L, plr->itemblinkmode);
			return 1;
		case k_getsparks:
			lua_pushinteger(L, (plr->pflags & PF_GETSPARKS));
			return 1;
		case k_jawztargetdelay:
			lua_pushinteger(L, plr->jawztargetdelay);
			return 1;
		case k_spectatewait:
			lua_pushinteger(L, plr->spectatewait);
			return 1;
		case k_growcancel:
			lua_pushinteger(L, plr->growcancel);
			return 1;
		default:
			lua_pushinteger(L, kartstuff[ks]);
			return 1;
	}

	return 1;
}

// kartstuff, ks, value -> plt->??? = value
static int kartstuff_set(lua_State *L)
{
	INT32 *kartstuff = *((INT32 **)luaL_checkudata(L, 1, META_KARTSTUFF));
	player_t *plr = (player_t*)((char*)kartstuff - offsetof(player_t, kartstuff));
	kartstufftype_t ks = luaL_checkinteger(L, 2);
	INT32 i = (INT32)luaL_checkinteger(L, 3);

	if (ks >= NUMKARTSTUFF)
		return luaL_error(L, LUA_QL("kartstufftype_t") " cannot be %u", ks);
	if (hud_running)
		return luaL_error(L, "Do not alter player_t in HUD rendering code!");
	if (hook_cmd_running)
		return luaL_error(L, "Do not alter player_t in CMD building code!");

	switch (ks)
	{
		case k_position:
			plr->position = i;
			break;
		case k_oldposition:
			plr->oldposition = i;
			break;
		case k_positiondelay:
			plr->positiondelay = i;
			break;
		case k_prevcheck:
			plr->prevcheck = i;
			break;
		case k_nextcheck:
			plr->nextcheck = i;
			break;
			/*case k_waypoint:
			 *	lua_pushinteger(L, plr->waypoint);
			 *	break;*/
			/*case k_starpostwp:
			 *	lua_pushinteger(L, plr->starpostwp);
			 *	break;*/
			case k_starpostflip:
				plr->starpostflip = i;
				break;
			case k_respawn:
				plr->respawn = i;
				break;
			case k_dropdash:
				plr->dropdash = i;
				break;
			case k_throwdir:
				plr->throwdir = i;
				break;

			case k_lapanimation:
				plr->karthud[khud_lapanimation] = i;
				break;
			case k_laphand:
				plr->karthud[khud_laphand] = i;
				break;
			case k_cardanimation:
				plr->karthud[khud_cardanimation] = i;
				break;
			case k_voices:
				plr->karthud[khud_voices] = i;
				break;
			case k_tauntvoices:
				plr->karthud[khud_tauntvoices] = i;
				break;
			case k_instashield:
				plr->instashield = i;
				break;
			case k_enginesnd:
				plr->karthud[khud_enginesnd] = i;
				break;

			case k_floorboost:
				plr->floorboost = i;
				break;
			case k_spinouttype:
				plr->spinouttype = i;
				break;
			case k_drift:
				plr->drift = i;
				break;
			case k_driftend:
				if (i > 0)
					plr->pflags |= PF_DRIFTEND;
				break;
			case k_driftcharge:
				plr->driftcharge  = i;
				break;
			case k_driftboost:
				plr->driftboost = CLAMP(i, 0, UINT8_MAX);
				break;
			case k_boostcharge:
				plr->boostcharge = i;
				break;
			case k_startboost:
				plr->startboost = CLAMP(i, 0, UINT8_MAX);
				break;
			/*case k_jmp:
				plr->pflags |= PF_DRIFTINPUT;
				return 1;*/
			case k_offroad:
				plr->offroad = i;
				break;
			case k_pogospring:
				plr->pogospring = i;
				break;
			case k_brakestop:
				plr->brakestop = i;
				break;
			case k_waterskip:
				plr->waterskip = i;
				break;
			case k_dashpadcooldown:
				plr->dashpadcooldown = i;
				break;
			case k_boostpower:
				plr->boostpower = i;
				break;
			case k_speedboost:
				plr->speedboost = i;
				break;
			case k_accelboost:
				plr->accelboost = i;
				break;
			case k_boostangle:
				plr->boostangle = i;
				break;
			case k_boostcam:
				plr->karthud[khud_boostcam] = i;
				break;
			case k_destboostcam:
				plr->karthud[khud_destboostcam] = i;
				break;
			case k_timeovercam:
				plr->karthud[khud_timeovercam] = i;
				break;
			case k_aizdriftstrat:
				plr->aizdriftstrat = i;
				break;
			case k_brakedrift:
				if (i > 0)
					plr->pflags |= PF_BRAKEDRIFT;
				break;
			case k_itemroulette:
				plr->itemroulette = i;
				break;
			case k_roulettetype:
				plr->roulettetype = i;
				break;
			case k_itemtype:
				plr->itemtype = i;
				break;
			case k_itemamount:
				plr->itemamount = i;
				break;
			case k_itemheld:
				if (i > 0)
					plr->itemflags |= IF_ITEMOUT;
				break;
			case k_curshield:
				plr->curshield = i;
				break;
			case k_hyudorotimer:
				plr->hyudorotimer = i;
				break;
			case k_stealingtimer:
				plr->stealingtimer = i;
				break;
			case k_stolentimer:
				plr->stolentimer = i;
				break;
			case k_sneakertimer:
				plr->sneakertimer = CLAMP(i, 0, UINT16_MAX);
				break;
			case k_growshrinktimer:
				plr->growshrinktimer = CLAMP(i, INT16_MIN, INT16_MAX);
				break;
			case k_squishedtimer:
				plr->squishedtimer = CLAMP(i, INT16_MIN, INT16_MAX);
				break;
			case k_rocketsneakertimer:
				plr->rocketsneakertimer = CLAMP(i, 0, UINT16_MAX);
				break;
			case k_invincibilitytimer:
				plr->invincibilitytimer = CLAMP(i, 0, UINT16_MAX);
				break;
			case k_eggmanheld:
				if (i > 0)
					plr->itemflags |= IF_EGGMANOUT;
				break;
			case k_eggmanexplode:
				plr->eggmanexplode = CLAMP(i, 0, UINT8_MAX);
				break;
			case k_eggmanblame:
				plr->eggmanblame = i;
				break;
			case k_lastjawztarget:
				plr->lastjawztarget = i;
				break;
			case k_bananadrag:
				plr->bananadrag = CLAMP(i, 0, UINT16_MAX);
				break;
			case k_spinouttimer:
				plr->spinouttimer = CLAMP(i, 0, UINT16_MAX);
				break;
			case k_wipeoutslow:
				plr->wipeoutslow = CLAMP(i, 0, UINT8_MAX);
				break;
			case k_justbumped:
				plr->justbumped = i;
				break;;
			case k_comebacktimer:
				plr->karmadelay = i;
				break;
			case k_sadtimer:
				plr->sadtimer = i;
				break;

				// Battle Mode vars
			case k_bumper:
				plr->bumper = i;
				break;
			case k_comebackpoints:
				plr->karmapoints = i;
				break;
			case k_comebackmode:
				plr->karmamode = i;
				break;
			case k_wanted:
				plr->wanted = i;
				break;
			case k_yougotem:
				plr->karthud[khud_yougotem] = i;
				break;

				// v1.0.2+ vars
			case k_itemblink:
				plr->itemblink = i;
				break;
			case k_itemblinkmode:
				plr->itemblinkmode = i;
				break;
			case k_getsparks:
				if (i > 0)
					plr->pflags |= PF_GETSPARKS;
				break;
			case k_jawztargetdelay:
				plr->jawztargetdelay = i;
				break;
			case k_spectatewait:
				plr->spectatewait = i;
				break;
			case k_growcancel:
				plr->growcancel = i;
				break;
			default:
				kartstuff[ks] = i;
				break;
	}

	return 0;
}

// #kartstuff -> NUMKARTSTUFF
static int kartstuff_len(lua_State *L)
{
	lua_pushinteger(L, NUMKARTSTUFF);
	return 1;
}

// karthud, ks -> karthud[ks]
static int karthud_get(lua_State *L)
{
	INT32 *karthud = *((INT32 **)luaL_checkudata(L, 1, META_KARTHUD));
	karthudtype_t ks = luaL_checkinteger(L, 2);
	if (ks >= NUMKARTHUD)
		return luaL_error(L, LUA_QL("karthudtype_t") " cannot be %u", ks);
	lua_pushinteger(L, karthud[ks]);
	return 1;
}

// karthud, ks, value -> karthud[ks] = value
static int karthud_set(lua_State *L)
{
	INT32 *karthud = *((INT32 **)luaL_checkudata(L, 1, META_KARTHUD));
	karthudtype_t ks = luaL_checkinteger(L, 2);
	INT32 i = (INT32)luaL_checkinteger(L, 3);
	if (ks >= NUMKARTHUD)
		return luaL_error(L, LUA_QL("karthudtype_t") " cannot be %u", ks);
	if (hud_running)
		return luaL_error(L, "Do not alter player_t in HUD rendering code!");
	if (hook_cmd_running)
		return luaL_error(L, "Do not alter player_t in CMD building code!");
	karthud[ks] = i;
	return 0;
}

// #karthud -> NUMKARTHUD
static int karthud_len(lua_State *L)
{
	lua_pushinteger(L, NUMKARTHUD);
	return 1;
}

// player.cmd get/set
#define NOFIELD luaL_error(L, LUA_QL("ticcmd_t") " has no field named " LUA_QS, field)
#define NOSET luaL_error(L, LUA_QL("ticcmd_t") " field " LUA_QS " cannot be set.", field)

enum ticcmd_e
{
	ticcmd_forwardmove,
	ticcmd_sidemove,
	ticcmd_turning,
	ticcmd_driftturn,
	ticcmd_angle,
	ticcmd_angleturn,
	ticcmd_throwdir,
	ticcmd_aiming,
	ticcmd_buttons,
	ticcmd_latency,
	ticcmd_flags,
};

static const char *const ticcmd_opt[] = {
	"forwardmove",
	"sidemove",
	"turning",
	"driftturn",
	"angle",
	"angleturn",
	"throwdir",
	"aiming",
	"buttons",
	"latency",
	"flags",
	NULL,
};

static int ticcmd_get(lua_State *L)
{
	ticcmd_t *cmd = *((ticcmd_t **)luaL_checkudata(L, 1, META_TICCMD));
	enum ticcmd_e field = Lua_optoption(L, 2, NULL, ticcmd_opt);
	if (!cmd)
		return LUA_ErrInvalid(L, "player_t");

	if (field == (enum ticcmd_e)-1)
		return LUA_ErrInvalid(L, "fields");

	switch (field)
	{
	case ticcmd_forwardmove:
		lua_pushinteger(L, cmd->forwardmove);
		break;
	case ticcmd_sidemove:
		lua_pushinteger(L, cmd->sidemove);
		break;
	case ticcmd_turning:
	case ticcmd_driftturn:
		lua_pushinteger(L, cmd->turning);
		break;
	case ticcmd_angle:
	case ticcmd_angleturn:
		lua_pushinteger(L, cmd->angle);
		break;
	case ticcmd_throwdir:
		lua_pushinteger(L, cmd->throwdir);
		break;
	case ticcmd_aiming:
		lua_pushinteger(L, cmd->aiming);
		break;
	case ticcmd_buttons:
		lua_pushinteger(L, cmd->buttons);
		break;
	case ticcmd_latency:
		lua_pushinteger(L, cmd->latency);
		break;
	case ticcmd_flags:
		lua_pushinteger(L, cmd->flags);
		break;
	default:
		return NOFIELD;
	}

	return 1;
}

static int ticcmd_set(lua_State *L)
{
	ticcmd_t *cmd = *((ticcmd_t **)luaL_checkudata(L, 1, META_TICCMD));
	enum ticcmd_e field = Lua_optoption(L, 2, ticcmd_opt[0], ticcmd_opt);
	if (!cmd)
		return LUA_ErrInvalid(L, "ticcmd_t");

	if (hud_running)
		return luaL_error(L, "Do not alter player_t in HUD rendering code!");

	switch (field)
	{
	case ticcmd_forwardmove:
		cmd->forwardmove = (SINT8)luaL_checkinteger(L, 3);
		break;
	case ticcmd_sidemove:
		cmd->sidemove = (SINT8)luaL_checkinteger(L, 3);
		break;
	case ticcmd_turning:
	case ticcmd_driftturn:
		cmd->turning = (INT16)luaL_checkinteger(L, 3);
		break;
	case ticcmd_angle:
	case ticcmd_angleturn:
		cmd->angle = (INT16)luaL_checkinteger(L, 3);
		break;
	case ticcmd_throwdir:
		cmd->throwdir = (INT16)luaL_checkinteger(L, 3);
		break;
	case ticcmd_aiming:
		cmd->aiming = (INT16)luaL_checkinteger(L, 3);
		break;
	case ticcmd_buttons:
		cmd->buttons = (UINT16)luaL_checkinteger(L, 3);
		break;
	case ticcmd_latency:
		return NOSET;
	case ticcmd_flags:
		return NOSET;
	default:
		return NOFIELD;
	}

	return 0;
}

#undef NOFIELD

enum sonicloopvars {
	sonicloopvars_radius = 0,
	sonicloopvars_revolution,
	sonicloopvars_min_revolution,
	sonicloopvars_max_revolution,
	sonicloopvars_yaw,
	sonicloopvars_origin_x,
	sonicloopvars_origin_y,
	sonicloopvars_origin_z,
	sonicloopvars_origin_shift_x,
	sonicloopvars_origin_shift_y,
	sonicloopvars_shift_x,
	sonicloopvars_shift_y,
	sonicloopvars_flip,
	sonicloopvars_camera,
};

static const char *const sonicloopvars_opt[] = {
	"radius",
	"revolution",
	"min_revolution",
	"max_revolution",
	"yaw",
	"origin_x",
	"origin_y",
	"origin_z",
	"origin_shift_x",
	"origin_shift_y",
	"shift_x",
	"shift_y",
	"flip",
	"camera",
	NULL
};

static int sonicloopvars_get(lua_State *L)
{
	sonicloopvars_t *sonicloopvars = *((sonicloopvars_t **)luaL_checkudata(L, 1, META_SONICLOOPVARS));
	enum sonicloopvars field = luaL_checkoption(L, 2, NULL, sonicloopvars_opt);

	// This should always be valid.
	I_Assert(sonicloopvars != NULL);

	switch (field)
	{
	case sonicloopvars_radius:
		lua_pushfixed(L, sonicloopvars->radius);
		break;
	case sonicloopvars_revolution:
		lua_pushfixed(L, sonicloopvars->revolution);
		break;
	case sonicloopvars_min_revolution:
		lua_pushfixed(L, sonicloopvars->min_revolution);
		break;
	case sonicloopvars_max_revolution:
		lua_pushfixed(L, sonicloopvars->max_revolution);
		break;
	case sonicloopvars_yaw:
		lua_pushangle(L, sonicloopvars->yaw);
		break;
	case sonicloopvars_origin_x:
		lua_pushfixed(L, sonicloopvars->origin.x);
		break;
	case sonicloopvars_origin_y:
		lua_pushfixed(L, sonicloopvars->origin.y);
		break;
	case sonicloopvars_origin_z:
		lua_pushfixed(L, sonicloopvars->origin.z);
		break;
	case sonicloopvars_origin_shift_x:
		lua_pushfixed(L, sonicloopvars->origin_shift.x);
		break;
	case sonicloopvars_origin_shift_y:
		lua_pushfixed(L, sonicloopvars->origin_shift.y);
		break;
	case sonicloopvars_shift_x:
		lua_pushfixed(L, sonicloopvars->shift.x);
		break;
	case sonicloopvars_shift_y:
		lua_pushfixed(L, sonicloopvars->shift.y);
		break;
	case sonicloopvars_flip:
		lua_pushboolean(L, sonicloopvars->flip);
		break;
	case sonicloopvars_camera:
		LUA_PushUserdata(L, &sonicloopvars->camera, META_SONICLOOPCAMVARS);
		break;
	}
	return 1;
}

enum sonicloopcamvars {
	sonicloopcamvars_enter_tic = 0,
	sonicloopcamvars_exit_tic,
	sonicloopcamvars_zoom_in_speed,
	sonicloopcamvars_zoom_out_speed,
	sonicloopcamvars_dist,
	sonicloopcamvars_pan,
	sonicloopcamvars_pan_speed,
	sonicloopcamvars_pan_accel,
	sonicloopcamvars_pan_back,
};

static const char *const sonicloopcamvars_opt[] = {
	"enter_tic",
	"exit_tic",
	"zoom_in_speed",
	"zoom_out_speed",
	"dist",
	"pan",
	"pan_speed",
	"pan_accel",
	"pan_back",
	NULL
};

static int sonicloopcamvars_get(lua_State *L)
{
	sonicloopcamvars_t *sonicloopcamvars = *((sonicloopcamvars_t **)luaL_checkudata(L, 1, META_SONICLOOPCAMVARS));
	enum sonicloopcamvars field = luaL_checkoption(L, 2, NULL, sonicloopcamvars_opt);

	// This should always be valid.
	I_Assert(sonicloopcamvars != NULL);

	switch (field)
	{
	case sonicloopcamvars_enter_tic:
		lua_pushinteger(L, sonicloopcamvars->enter_tic);
		break;
	case sonicloopcamvars_exit_tic:
		lua_pushinteger(L, sonicloopcamvars->exit_tic);
		break;
	case sonicloopcamvars_zoom_in_speed:
		lua_pushinteger(L, sonicloopcamvars->zoom_in_speed);
		break;
	case sonicloopcamvars_zoom_out_speed:
		lua_pushinteger(L, sonicloopcamvars->zoom_out_speed);
		break;
	case sonicloopcamvars_dist:
		lua_pushfixed(L, sonicloopcamvars->dist);
		break;
	case sonicloopcamvars_pan:
		lua_pushangle(L, sonicloopcamvars->pan);
		break;
	case sonicloopcamvars_pan_speed:
		lua_pushfixed(L, sonicloopcamvars->pan_speed);
		break;
	case sonicloopcamvars_pan_accel:
		lua_pushinteger(L, sonicloopcamvars->pan_accel);
		break;
	case sonicloopcamvars_pan_back:
		lua_pushinteger(L, sonicloopcamvars->pan_back);
		break;
	}
	return 1;
}

int LUA_PlayerLib(lua_State *L)
{
	luaL_newmetatable(L, META_PLAYER);
		lua_pushcfunction(L, player_get);
		lua_setfield(L, -2, "__index");

		lua_pushcfunction(L, player_set);
		lua_setfield(L, -2, "__newindex");

		lua_pushcfunction(L, player_num);
		lua_setfield(L, -2, "__len");
	lua_pop(L,1);

	luaL_newmetatable(L, META_POWERS);
		lua_pushcfunction(L, power_get);
		lua_setfield(L, -2, "__index");

		lua_pushcfunction(L, power_set);
		lua_setfield(L, -2, "__newindex");

		lua_pushcfunction(L, power_len);
		lua_setfield(L, -2, "__len");
	lua_pop(L,1);

	luaL_newmetatable(L, META_KARTSTUFF);
		lua_pushcfunction(L, kartstuff_get);
		lua_setfield(L, -2, "__index");

		lua_pushcfunction(L, kartstuff_set);
		lua_setfield(L, -2, "__newindex");

		lua_pushcfunction(L, kartstuff_len);
		lua_setfield(L, -2, "__len");
	lua_pop(L,1);

	luaL_newmetatable(L, META_KARTHUD);
		lua_pushcfunction(L, karthud_get);
		lua_setfield(L, -2, "__index");

		lua_pushcfunction(L, karthud_set);
		lua_setfield(L, -2, "__newindex");

		lua_pushcfunction(L, karthud_len);
		lua_setfield(L, -2, "__len");
	lua_pop(L,1);

	luaL_newmetatable(L, META_TICCMD);
		lua_pushcfunction(L, ticcmd_get);
		lua_setfield(L, -2, "__index");

		lua_pushcfunction(L, ticcmd_set);
		lua_setfield(L, -2, "__newindex");
	lua_pop(L,1);
	
	luaL_newmetatable(L, META_SONICLOOPVARS);
		lua_pushcfunction(L, sonicloopvars_get);
		lua_setfield(L, -2, "__index");
	lua_pop(L,1);
	
	luaL_newmetatable(L, META_SONICLOOPCAMVARS);
		lua_pushcfunction(L, sonicloopcamvars_get);
		lua_setfield(L, -2, "__index");
	lua_pop(L,1);

	lua_newuserdata(L, 0);
		lua_createtable(L, 0, 2);
			lua_pushcfunction(L, lib_getPlayer);
			lua_setfield(L, -2, "__index");

			lua_pushcfunction(L, lib_lenPlayer);
			lua_setfield(L, -2, "__len");
		lua_setmetatable(L, -2);
	lua_setglobal(L, "players");

	// push displayplayers in the same fashion
	lua_newuserdata(L, 0);
		lua_createtable(L, 0, 2);
			lua_pushcfunction(L, lib_getDisplayplayers);
			lua_setfield(L, -2, "__index");

			lua_pushcfunction(L, lib_lenDisplayplayers);
			lua_setfield(L, -2, "__len");
		lua_setmetatable(L, -2);
	lua_setglobal(L, "displayplayers");

	return 0;
}
