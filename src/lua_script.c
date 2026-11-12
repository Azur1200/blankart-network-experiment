// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2012-2016 by John "JTE" Muniz.
// Copyright (C) 2012-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  lua_script.c
/// \brief Lua scripting basics

#include "doomdef.h"
#include "dehacked.h"
#include "deh_lua.h"
#include "p_mobj.h"
#include "z_zone.h"
#include "w_wad.h"
#include "p_setup.h"
#include "r_state.h"
#include "r_sky.h"
#include "g_game.h"
#include "f_finale.h"
#include "byteptr.h"
#include "p_saveg.h"
#include "p_local.h"
#include "p_slopes.h" // for P_SlopeById and slopelist
#include "p_polyobj.h" // polyobj_t, PolyObjects
#include "k_kart.h" // Rankscore macros
#include "k_battle.h"
#include "k_items.h"
#include "k_waypoint.h"
#ifdef LUA_ALLOW_BYTECODE
#include "d_netfil.h" // for LUA_DumpFile
#endif

#include "lua_script.h"
#include "lua_libs.h"
#include "lua_hook.h"

#include "doomstat.h"
#include "g_state.h"
#include "m_argv.h"

#include <tracy/tracy/TracyC.h>

lua_State *gL = NULL;

// Mathlib global state
static lua_State *mL = NULL;

dboolean lua_compatmode = false;

// List of internal libraries to load from SRB2
static lua_CFunction liblist[] = {
	LUA_EnumLib, 		// global metatable for enums
	LUA_SOCLib, 		// A_Action functions, freeslot
	LUA_BaseLib, 		// string concatination by +, CONS_Printf, p_local.h stuff (P_InstaThrust, P_Move), etc.
	LUA_MathLib, 		// fixed_t and angle_t math functions
	LUA_HookLib, 		// hookAdd and hook-calling functions
	LUA_ConsoleLib, 	// console command/variable functions and structs
	LUA_InfoLib, 		// info.h stuff: mobjinfo_t, mobjinfo[], state_t, states[]
	LUA_MobjLib, 		// mobj_t, mapthing_t
	LUA_PlayerLib, 		// player_t
	LUA_SkinLib, 		// skin_t, skins[]
	LUA_ThinkerLib, 	// thinker_t
	LUA_MapLib, 		// line_t, side_t, sector_t, subsector_t
	LUA_TagLib, 		// tags
	LUA_PolyObjLib, 	// polyobj_t
	LUA_BlockmapLib,	// blockmap stuff
	LUA_HudLib, 		// HUD stuff
	LUA_ColorLib, 		// general color functions
	LUA_FollowerLib, 	// follower_t, followers[]
	LUA_BotVarsLib, 	// botvars_t
	LUA_TerrainLib, 	// t_splash_t, t_footstep_t, t_overlay_t, terrain_t
	LUA_WaypointLib, 	// waypoint_t
	LUA_VectorLib, 		// vectors
	LUA_MatrixLib, 		// matrices
	LUA_QuaternionLib, 	// quaternions
	LUA_VoiceLib, 		// kartvoice_t, skinvoices[]
	LUA_ItemLib, 		// kartitem_t, kartresult_t, kartitemgraphics_t
	NULL
};

// Panic function Lua calls when there's an unprotected error.
// This function cannot return. Lua would kill the application anyway if it did.
FUNCNORETURN static int LUA_Panic(lua_State *L)
{
	CONS_Alert(CONS_ERROR,"LUA PANIC! %s\n",lua_tostring(L,-1));
	I_Error("An unfortunate Lua processing error occurred in the exe itself. This is not a scripting error on your part.");
#ifndef __GNUC__
	return -1;
#endif
}

#define LEVELS1 12 // size of the first part of the stack
#define LEVELS2 10 // size of the second part of the stack

// Error handler used with pcall() when loading scripts or calling hooks
// Takes a string with the original error message,
// appends the traceback to it, and return the result
int LUA_GetErrorMessage(lua_State *L)
{
	int level = 1;
	int firstpart = 1; // still before eventual `...'
	lua_Debug ar;

	lua_pushliteral(L, "\nstack traceback:");
	while (lua_getstack(L, level++, &ar))
	{
		if (level > LEVELS1 && firstpart)
		{
			// no more than `LEVELS2' more levels?
			if (!lua_getstack(L, level + LEVELS2, &ar))
				level--; // keep going
			else
			{
				lua_pushliteral(L, "\n    ..."); // too many levels
				while (lua_getstack(L, level + LEVELS2, &ar)) // find last levels
					level++;
			}
			firstpart = 0;
			continue;
		}
		lua_pushliteral(L, "\n    ");
		lua_getinfo(L, "Snl", &ar);
		lua_pushfstring(L, "%s:", ar.short_src);
		if (ar.currentline > 0)
			lua_pushfstring(L, "%d:", ar.currentline);
		if (*ar.namewhat != '\0') // is there a name?
			lua_pushfstring(L, " in function " LUA_QS, ar.name);
		else
		{
			if (*ar.what == 'm') // main?
				lua_pushfstring(L, " in main chunk");
			else if (*ar.what == 'C' || *ar.what == 't')
				lua_pushliteral(L, " ?"); // C function or tail call
			else
				lua_pushfstring(L, " in function <%s:%d>",
					ar.short_src, ar.linedefined);
		}
		lua_concat(L, lua_gettop(L));
	}
	lua_concat(L, lua_gettop(L));
	return 1;
}

int LUA_Call(lua_State *L, int nargs, int nresults, int errorhandlerindex, dboolean compat, dboolean printerror)
{
	dboolean oldcompat = lua_compatmode;
	lua_compatmode = compat;

	int err = lua_pcall(L, nargs, nresults, errorhandlerindex);

	if (err)
	{
		if (printerror)
			CONS_Alert(CONS_WARNING, "%s\n", lua_tostring(L, -1));
		lua_pop(L, 1);
	}

	lua_compatmode = oldcompat;
	return err;
}

// global fields
#define GLOBALFIELDS(X) \
	X(gamemap) \
	X(udmf) \
	X(maptol) \
	X(circuitmap) \
	X(stoppedclock) \
	X(netgame) \
	X(multiplayer) \
	X(modeattacking) \
	X(splitscreen) \
	X(gamecomplete) \
	X(marathonmode) \
	X(devparm) \
	X(modifiedgame) \
	X(menuactive) \
	X(paused) \
	X(bluescore) \
	X(redscore) \
	X(timelimit) \
	X(pointlimit) \
	X(titlemap) \
	X(titlemapinaction) \
	X(bootmap) \
	X(tutorialmap) \
	X(tutorialmode) \
	X(skincolor_redteam) \
	X(skincolor_blueteam) \
	X(skincolor_redring) \
	X(skincolor_bluering) \
	X(invulntics) \
	X(sneakertics) \
	X(flashingtics) \
	X(tailsflytics) \
	X(underwatertics) \
	X(spacetimetics) \
	X(extralifetics) \
	X(nightslinktics) \
	X(gameovertics) \
	X(ammoremovaltics) \
	X(gametype) \
	X(gametyperules) \
	X(leveltime) \
	X(introtime) \
	X(starttime) \
	X(curWeather) \
	X(globalweather) \
	X(levelskytexture) \
	X(globallevelskytexture) \
	X(mapmusname) \
	X(mapmusflags) \
	X(mapmusposition) \
	X(mapmusrng) \
	X(consoleplayer) \
	X(isserver) \
	X(isdedicatedserver) \
	X(server) \
	X(gravity) \
	X(VERSION) \
	X(SUBVERSION) \
	X(VERSIONSTRING) \
	X(MAXSKINCOLORS) \
	X(MAXTRANSLATIONS) \
	X(gamespeed) \
	X(nummapboxes) \
	X(numgotboxes) \
	X(itembreaker) \
	X(battleprisons) \
	X(ringsactive) \
	X(stackingactive) \
	X(chainingactive) \
	X(slipdashactive) \
	X(slopeboostactive) \
	X(draftingactive) \
	X(airdropactive) \
	X(bumpsparkactive) \
	X(purpledriftactive) \
	X(recoverydashactive) \
	X(keepstuffactive) \
	X(itemlittering) \
	X(itempushing) \
	X(trailslow_active) \
	X(trailslowactive) \
	X(playerbumpactive) \
	X(rankingactive) \
	X(ranking_penaltyactive) \
	X(ranking_driftbonusactive) \
	X(ranking_hitbonusactive) \
	X(cannotrank) \
	X(hyubgone) \
	X(encoremode) \
	X(franticitems) \
	X(comeback) \
	X(wantedcalcdelay) \
	X(indirectitemcooldown) \
	X(thwompsactive) \
	X(lastLowestLap) \
	X(spbplace) \
	X(mapobjectscale) \
	X(numlaps) \
	X(racecountdown) \
	X(exitcountdown) \
	X(replayplayback) \
	X(gamestate) \
	X(compatmode) \
	X(hyudorotime) \
	X(stealtime) \
	X(sneakertime) \
	X(waterpaneltime) \
	X(bubbletime) \
	X(flametime) \
	X(comebacktime) \
	X(initial_seed) \
	X(initialseed) \
	X(greasetics) \
	X(bestbeforeprank) \
	X(rankdivisor) \
	X(lowestrankscore) \
	X(highestrankscore) \
	X(wipeoutslowtime) \
	X(wantedreduce) \
	X(wantedfrequency) \
	X(mapnamespace) \
	X(emeralds) \
	X(token)

enum global_e
{
#define ENUMITEM(name) global_##name,
	GLOBALFIELDS(ENUMITEM)
};

const char *const global_opt[] = {
#define ENUMITEMNAME(name) #name,
	GLOBALFIELDS(ENUMITEMNAME)
	NULL,
};

int global_fields_ref = LUA_NOREF;

#define NOSET luaL_error(L, "global field " LUA_QS " cannot be set.", field)

// Moved here from lib_getenum.
int LUA_PushGlobals(lua_State *L)
{
	enum global_e field = Lua_optoption(L, 2, -1, global_fields_ref);
	lua_settop(L, 2);

	switch (field)
	{
		case global_gamemap:
			lua_pushinteger(L, lua_compatmode ? G_NativeMapToKart(gamemap) : gamemap);
			return 1;
		case global_udmf:
			lua_pushdboolean(L, udmf);
			return 1;
		case global_maptol:
			lua_pushinteger(L, maptol);
			return 1;
		case global_circuitmap:
			lua_pushdboolean(L, circuitmap);
			return 1;
		case global_stoppedclock:
			lua_pushdboolean(L, stoppedclock);
			return 1;
		case global_netgame:
			lua_pushdboolean(L, netgame);
			return 1;
		case global_multiplayer:
			lua_pushdboolean(L, multiplayer);
			return 1;
		case global_modeattacking:
			lua_pushdboolean(L, modeattacking);
			return 1;
		case global_splitscreen:
		{
			// You may be wondering why?
			// Kart lua scripts expects splitscreen to mean "number of split views"
			// not "number of local players"
			if (lua_compatmode)
				lua_pushinteger(L, r_splitscreen);
			else
				lua_pushinteger(L, splitscreen);
			return 1;
		}
		case global_gamecomplete:
			lua_pushdboolean(L, (gamecomplete != 0));
			return 1;
		case global_marathonmode:
			lua_pushinteger(L, marathonmode);
			return 1;
		case global_devparm:
			lua_pushdboolean(L, devparm);
			return 1;
		case global_modifiedgame:
			lua_pushdboolean(L, modifiedgame && !savemoddata);
			return 1;
		case global_menuactive:
			lua_pushdboolean(L, menustack[0]);
			return 1;
		case global_paused:
			lua_pushdboolean(L, paused);
			return 1;
		case global_bluescore:
			lua_pushinteger(L, bluescore);
			return 1;
		case global_redscore:
			lua_pushinteger(L, redscore);
			return 1;
		case global_timelimit:
			lua_pushinteger(L, timelimitintics);
			return 1;
		case global_pointlimit:
			lua_pushinteger(L, cv_pointlimit.value);
			return 1;
		// begin map vars
		case global_titlemap:
			lua_pushstring(L, titlemap);
			return 1;
		case global_titlemapinaction:
			lua_pushdboolean(L, (titlemapinaction != TITLEMAP_OFF));
			return 1;
		case global_bootmap:
			lua_pushstring(L, bootmap);
			return 1;
		case global_tutorialmap:
			lua_pushstring(L, tutorialmap);
			return 1;
		case global_tutorialmode:
			lua_pushdboolean(L, tutorialmode);
			return 1;
		// end map vars
		// begin CTF colors
		case global_skincolor_redteam:
			lua_pushinteger(L, skincolor_redteam);
			return 1;
		case global_skincolor_blueteam:
			lua_pushinteger(L, skincolor_blueteam);
			return 1;
		case global_skincolor_redring:
			lua_pushinteger(L, skincolor_redring);
			return 1;
		case global_skincolor_bluering:
			lua_pushinteger(L, skincolor_bluering);
			return 1;
		// end CTF colors
		// begin timers
		case global_invulntics:
			lua_pushinteger(L, invulntics);
			return 1;
		case global_sneakertics:
			lua_pushinteger(L, sneakertics);
			return 1;
		case global_flashingtics:
			lua_pushinteger(L, flashingtics);
			return 1;
		case global_tailsflytics:
			lua_pushinteger(L, tailsflytics);
			return 1;
		case global_underwatertics:
			lua_pushinteger(L, underwatertics);
			return 1;
		case global_spacetimetics:
			lua_pushinteger(L, spacetimetics);
			return 1;
		case global_extralifetics:
			lua_pushinteger(L, extralifetics);
			return 1;
		case global_nightslinktics:
			lua_pushinteger(L, nightslinktics);
			return 1;
		case global_gameovertics:
			lua_pushinteger(L, gameovertics);
			return 1;
		case global_ammoremovaltics:
			lua_pushinteger(L, ammoremovaltics);
			return 1;
		// end timers
		case global_gametype:
			lua_pushinteger(L, gametype);
			return 1;
		case global_gametyperules:
			lua_pushinteger(L, gametypes[gametype]->rules);
			return 1;
		case global_leveltime:
			lua_pushinteger(L, leveltime);
			return 1;
		case global_introtime:
			lua_pushinteger(L, introtime);
			return 1;
		case global_starttime:
			lua_pushinteger(L, starttime);
			return 1;
		case global_curWeather:
			lua_pushinteger(L, curWeather);
			return 1;
		case global_globalweather:
			lua_pushinteger(L, globalweather);
			return 1;
		case global_levelskytexture:
			lua_pushstring(L, levelskytexture);
			return 1;
		case global_globallevelskytexture:
			lua_pushstring(L, globallevelskytexture);
			return 1;
		case global_mapmusname:
			lua_pushstring(L, mapmusname);
			return 1;
		case global_mapmusflags:
			lua_pushinteger(L, mapmusflags);
			return 1;
		case global_mapmusposition:
			lua_pushinteger(L, mapmusposition);
			return 1;
		case global_mapmusrng:
			lua_pushinteger(L, mapmusrng);
			return 1;
		// local player variables, by popular request
		case global_consoleplayer: // player controlling console (aka local player 1)
			if (!addedtogame || consoleplayer < 0 || !playeringame[consoleplayer] || dedicated)
				return 0;
			LUA_PushUserdata(L, &players[consoleplayer], META_PLAYER);
			return 1;
		case global_isserver:
			lua_pushdboolean(L, server);
			return 1;
		case global_isdedicatedserver:
			lua_pushdboolean(L, dedicated);
			return 1;
		// end local player variables
		case global_server:
			if ((!multiplayer || !netgame) && !playeringame[serverplayer])
				return 0;
			LUA_PushUserdata(L, &players[serverplayer], META_PLAYER);
			return 1;
		case global_gravity:
			lua_pushinteger(L, gravity);
			return 1;
		case global_VERSION:
			if (lua_compatmode)
				lua_pushinteger(L, 1);
			else
				lua_pushinteger(L, VERSION);
			return 1;
		case global_SUBVERSION:
			if (lua_compatmode)
				lua_pushinteger(L, 6);
			else
				lua_pushinteger(L, SUBVERSION);
			return 1;
		case global_VERSIONSTRING:
			lua_pushstring(L, VERSIONSTRING);
			return 1;
		case global_MAXSKINCOLORS:
			if (lua_compatmode)
				lua_pushinteger(L, numskincolors);
			else
				lua_pushinteger(L, MAXSKINCOLORS);
			return 1;
		case global_MAXTRANSLATIONS:
			if (lua_compatmode)
			{
				lua_pushinteger(L, numskincolors);
				return 1;
			}
			return 0;
		case global_gamespeed:
			lua_pushinteger(L, gamespeed);
			return 1;
		case global_nummapboxes:
			lua_pushinteger(L, nummapboxes);
			return 1;
		case global_numgotboxes:
			lua_pushinteger(L, numgotboxes);
			return 1;
		case global_itembreaker:
			lua_pushdboolean(L, itembreaker);
			return 1;
		case global_battleprisons:
			lua_pushdboolean(L, itembreaker);
			return 1;
		case global_ringsactive:
			lua_pushdboolean(L, ringsactive);
			return 1;
		case global_stackingactive:
			lua_pushdboolean(L, stackingactive);
			return 1;
		case global_chainingactive:
			lua_pushdboolean(L, chainingactive);
			return 1;
		case global_slipdashactive:
			lua_pushdboolean(L, slipdashactive);
			return 1;
		case global_slopeboostactive:
			lua_pushdboolean(L, slopeboostactive);
			return 1;
		case global_draftingactive:
			lua_pushdboolean(L, draftingactive);
			return 1;
		case global_airdropactive:
			lua_pushinteger(L, airdropactive);
			return 1;
		case global_bumpsparkactive:
			lua_pushinteger(L, bumpsparkactive);
			return 1;
		case global_purpledriftactive:
			lua_pushdboolean(L, purpledriftactive);
			return 1;
		case global_recoverydashactive:
			lua_pushdboolean(L, recoverydashactive);
			return 1;
		case global_keepstuffactive:
			lua_pushdboolean(L, keepstuffactive);
			return 1;
		case global_itemlittering:
			lua_pushdboolean(L, itemlittering);
			return 1;
		case global_itempushing:
			lua_pushdboolean(L, itempushing); // hmm... i think this should be a dboolean
			return 1;
		case global_trailslow_active:
		case global_trailslowactive:
			lua_pushdboolean(L, trailslow_active); // I agree!
			return 1;
		case global_playerbumpactive:
			lua_pushinteger(L, playerbumpactive);
			return 1;
		case global_rankingactive:
			lua_pushdboolean(L, rankingactive);
			return 1;
		case global_ranking_penaltyactive:
			lua_pushdboolean(L, ranking_penaltyactive);
			return 1;
		case global_ranking_hitbonusactive:
			lua_pushdboolean(L, ranking_hitbonusactive);
			return 1;
		case global_ranking_driftbonusactive:
			lua_pushdboolean(L, ranking_driftbonusactive);
			return 1;
		case global_cannotrank:
			lua_pushdboolean(L, cannotrank);
			return 1;
		case global_hyubgone:
			lua_pushinteger(L, K_GetKartResult("hyudoro")->cooldown);
			return 1;
		case global_encoremode:
			lua_pushdboolean(L, encoremode);
			return 1;
		case global_franticitems:
			lua_pushdboolean(L, franticitems);
			return 1;
		case global_comeback:
			lua_pushdboolean(L, comeback);
			return 1;
		case global_wantedcalcdelay:
			lua_pushinteger(L, wantedcalcdelay);
			return 1;
		case global_indirectitemcooldown:
			lua_pushinteger(L, K_GetIndirectItemCooldown());
			return 1;
		case global_thwompsactive:
			lua_pushdboolean(L, thwompsactive);
			return 1;
		case global_lastLowestLap:
			lua_pushinteger(L, lastLowestLap);
			return 1;
		case global_spbplace:
			lua_pushinteger(L, spbplace);
			return 1;
		case global_mapobjectscale:
			lua_pushinteger(L, mapobjectscale);
			return 1;
		case global_numlaps:
			lua_pushinteger(L, numlaps);
			return 1;
		case global_racecountdown:
			lua_pushinteger(L, racecountdown);
			return 1;
		case global_exitcountdown:
			lua_pushinteger(L, exitcountdown);	// This name is pretty dumb. Hence why we'll prefer more descriptive names at least in Lua...
			return 1;
		case global_replayplayback:
			lua_pushdboolean(L, demo.playback);
			return 1;
		case global_gamestate:
			lua_pushinteger(L, gamestate);
			return 1;
		case global_compatmode:
			lua_pushdboolean(L, lua_compatmode);
			return 1;
		case global_hyudorotime:
			lua_pushinteger(L, hyudorotime);
			return 1;
		case global_stealtime:
			lua_pushinteger(L, stealtime);
			return 1;
		case global_sneakertime:
			lua_pushinteger(L, sneakertime);
			return 1;
		case global_waterpaneltime:
			lua_pushinteger(L, waterpaneltime);
			return 1;
		case global_bubbletime:
			lua_pushinteger(L, bubbletime);
			return 1;
		case global_flametime:
			lua_pushinteger(L, flametime);
			return 1;
		case global_comebacktime:
			lua_pushinteger(L, comebacktime);
			return 1;
		case global_initial_seed:
		case global_initialseed:
			lua_pushinteger(L, initial_seed);
			return 1;
		case global_greasetics:
			lua_pushinteger(L, greasetics);
			return 1;
		case global_bestbeforeprank:
			lua_pushinteger(L, BEST_SCORE_BEFORE_P_RANK);
			return 1;
		case global_lowestrankscore:
			// "Couldn't you just get the cvar?"
			// You could, but convenience also matters :^)
			lua_pushinteger(L, LOWESTRANKSCORE);
			return 1;
		case global_highestrankscore:
			lua_pushinteger(L, HIGHESTRANKSCORE);
			return 1;
		case global_rankdivisor:
			lua_pushinteger(L, RANKSCORE_DIVISOR);
			return 1;
		case global_wipeoutslowtime:
			lua_pushinteger(L, wipeoutslowtime);
			return 1;
		case global_wantedreduce:
			lua_pushinteger(L, wantedreduce);
			return 1;
		case global_wantedfrequency:
			lua_pushinteger(L, wantedfrequency);
			return 1;
		case global_mapnamespace:
			lua_pushstring(L, P_MapNamespaceString(mapnamespace));
			return 1;
		default:
			return 0;
	}

	return 0;
}

// See the above.
int LUA_WriteGlobals(lua_State *L)
{
	enum global_e field = Lua_optoption(L, 1, -1, global_fields_ref);
	lua_settop(L, 2);

	switch (field)
	{
		case global_redscore:
			redscore = (uint32_t)luaL_checkinteger(L, 2);
			break;
		case global_bluescore:
			bluescore = (uint32_t)luaL_checkinteger(L, 2);
			break;
		case global_skincolor_redteam:
			skincolor_redteam = (uint16_t)luaL_checkinteger(L, 2);
			break;
		case global_skincolor_blueteam:
			skincolor_blueteam = (uint16_t)luaL_checkinteger(L, 2);
			break;
		case global_skincolor_redring:
			skincolor_redring = (uint16_t)luaL_checkinteger(L, 2);
			break;
		case global_skincolor_bluering:
			skincolor_bluering = (uint16_t)luaL_checkinteger(L, 2);
			break;
		case global_emeralds:
			emeralds = (uint16_t)luaL_checkinteger(L, 2);
			break;
		case global_token:
			token = (uint32_t)luaL_checkinteger(L, 2);
			break;
		case global_gravity:
			gravity = (fixed_t)luaL_checkinteger(L, 2);
			break;
		case global_stoppedclock:
			stoppedclock = luaL_checkdboolean(L, 2);
			break;
		case global_mapmusname:
		{
			size_t strlength;
			const char *str = luaL_checklstring(L, 2, &strlength);

			if (strlength > 6)
				return luaL_error(L, "string length out of range (maximum 6 characters)");

			if (strlen(str) < strlength)
				return luaL_error(L, "string must not contain embedded zeros!");

			strncpy(mapmusname, str, strlength);
			break;
		}
		case global_mapmusflags:
			mapmusflags = (uint16_t)luaL_checkinteger(L, 2);
			break;
		case global_mapmusrng:
			mapmusrng = (uint8_t)luaL_checkinteger(L, 2);
			break;
		// SRB2Kart
		case global_racecountdown:
			racecountdown = (tic_t)luaL_checkinteger(L, 2);
			break;
		case global_exitcountdown:
			exitcountdown = (tic_t)luaL_checkinteger(L, 2);
			break;
		case global_indirectitemcooldown:
			K_SetIndirectItemCooldown(luaL_checkinteger(L, 2));
			break;
		case global_hyubgone:
			K_GetKartResult("hyudoro")->cooldown = (tic_t)luaL_checkinteger(L, 2);
			break;
		case global_starttime:
			starttime = (tic_t)luaL_checkinteger(L, 2);
			break;
		case global_introtime:
			introtime = (tic_t)luaL_checkinteger(L, 2);
			break;
		case global_itembreaker:
		case global_battleprisons:
			itembreaker = luaL_checkdboolean(L, 2);
			break;
		case global_ringsactive:
			ringsactive = luaL_checkdboolean(L, 2);
			break;
		case global_stackingactive:
			stackingactive = luaL_checkdboolean(L, 2);
			break;
		case global_chainingactive:
			chainingactive = luaL_checkdboolean(L, 2);
			break;
		case global_slipdashactive:
			slipdashactive = luaL_checkdboolean(L, 2);
			break;
		case global_slopeboostactive:
			slopeboostactive = luaL_checkdboolean(L, 2);
			break;
		case global_draftingactive:
			draftingactive = luaL_checkdboolean(L, 2);
			break;
		case global_airdropactive:
			airdropactive = luaL_checkinteger(L, 2);
			break;
		case global_bumpsparkactive:
			bumpsparkactive = luaL_checkinteger(L, 2);
			break;
		case global_purpledriftactive:
			purpledriftactive = luaL_checkdboolean(L, 2);
			break;
		case global_recoverydashactive:
			recoverydashactive = luaL_checkdboolean(L, 2);
			break;
		case global_itemlittering:
			itemlittering = luaL_checkdboolean(L, 2);
			break;
		case global_keepstuffactive:
			keepstuffactive = luaL_checkdboolean(L, 2);
			break;
		case global_itempushing:
			itempushing = luaL_checkdboolean(L, 2);
			break;
		case global_trailslowactive:
			trailslow_active = luaL_checkdboolean(L, 2);
			break;
		case global_trailslow_active:
			trailslow_active = luaL_checkdboolean(L, 2);
			break;
		case global_playerbumpactive:
			playerbumpactive = luaL_checkinteger(L, 2);
			break;
		case global_rankingactive:
			rankingactive = luaL_checkdboolean(L, 2);
			break;
		case global_ranking_penaltyactive:
			ranking_penaltyactive = luaL_checkdboolean(L, 2);
			break;
		case global_ranking_hitbonusactive:
			ranking_hitbonusactive = luaL_checkdboolean(L, 2);
			break;
		case global_ranking_driftbonusactive:
			ranking_driftbonusactive = luaL_checkdboolean(L, 2);
			break;
		case global_cannotrank:
			return NOSET;
			break;
		case global_gamespeed:
			gamespeed = (uint8_t)luaL_checkinteger(L, 2);
			break;
		case global_nummapboxes:
			nummapboxes = (int32_t)luaL_checkinteger(L, 2);
			break;
		case global_numgotboxes:
			numgotboxes = (int32_t)luaL_checkinteger(L, 2);
			break;
		case global_hyudorotime:
			hyudorotime = (int32_t)luaL_checkinteger(L, 2);
			break;
		case global_stealtime:
			stealtime = (int32_t)luaL_checkinteger(L, 2);
			break;
		case global_sneakertime:
			sneakertime = (int32_t)luaL_checkinteger(L, 2);
			break;
		case global_waterpaneltime:
			waterpaneltime = (int32_t)luaL_checkinteger(L, 2);
			break;
		case global_bubbletime:
			bubbletime = (int32_t)luaL_checkinteger(L, 2);
			break;
		case global_flametime:
			flametime = (int32_t)luaL_checkinteger(L, 2);
			break;
		case global_comebacktime:
			comebacktime = (int32_t)luaL_checkinteger(L, 2);
			break;
		case global_greasetics:
			greasetics = (int32_t)luaL_checkinteger(L, 2);
			break;
		case global_wipeoutslowtime:
			wipeoutslowtime = (int32_t)luaL_checkinteger(L, 2);
			break;
		case global_wantedreduce:
			wantedreduce = (int32_t)luaL_checkinteger(L, 2);
			break;
		case global_wantedfrequency:
			wantedfrequency = (int32_t)luaL_checkinteger(L, 2);
			break;
		case global_compatmode:
			lua_compatmode = luaL_checkdboolean(L, 2); // you'd BETTER not forget to change it back afterwards!
			break;
		default:
			return 0;
	}


	// Global variable set, so return and don't error.
	return 1;
}

// This function decides which global variables you are allowed to set.
static int setglobals(lua_State *L)
{
	const char *csname;
	char *name;
	enum actionnum actionnum;

	lua_remove(L, 1); // we're not gonna be using _G
	csname = lua_tostring(L, 1);

	// make an uppercase copy of the name
	name = Z_StrDup(csname);
	strupr(name);

	if (fastncmp(name, "A_", 2) && lua_isfunction(L, 2))
	{
		// Accept new A_Action functions
		// Add the action to Lua actions refrence table
		lua_getfield(L, LUA_REGISTRYINDEX, LREG_ACTIONS);
		lua_pushstring(L, name); // "A_ACTION"
		lua_pushvalue(L, 2); // function
		lua_rawset(L, -3); // rawset doesn't trigger this metatable again.
		// otherwise we would've used setfield, obviously.

		actionnum = LUA_GetActionNumByName(name);
		if (actionnum < NUMACTIONS)
			actionsoverridden[actionnum] = true;

		Z_Free(name);
		return 0;
	}

	Z_Free(name);

	if (LUA_WriteGlobals(L))
		return 0;

	return luaL_error(L, "Implicit global " LUA_QS " prevented. Create a local variable instead.", csname);
}

// Clear and create a new Lua state, laddo!
// There's SCRIPTIN to be had!
void LUA_ClearState(void)
{
	lua_State *L;
	int i;

	// close previous state
	if (gL)
		lua_close(gL);
	gL = NULL;

	CONS_Printf(M_GetText("Pardon me while I initialize the Lua scripting interface...\n"));

	// allocate state
	L = luaL_newstate();
	lua_atpanic(L, LUA_Panic);

	// open base libraries
	luaL_openlibs(L);
	lua_settop(L, 0);

	// make LREG_VALID table for all pushed userdata cache.
	lua_newtable(L);
	lua_setfield(L, LUA_REGISTRYINDEX, LREG_VALID);

	// make LREG_METATABLES table for all registered metatables
	lua_newtable(L);
	lua_setfield(L, LUA_REGISTRYINDEX, LREG_METATABLES);

	// open srb2 libraries
	for (i = 0; liblist[i]; i++)
	{
		lua_pushcfunction(L, liblist[i]);
		lua_call(L, 0, 0);
	}

	// lock the global namespace
	lua_getmetatable(L, LUA_GLOBALSINDEX);
		lua_pushcfunction(L, setglobals);
		lua_setfield(L, -2, "__newindex");
		lua_newtable(L);
		lua_setfield(L, -2, "__metatable");
	lua_pop(L, 1);

	// lua state is ready!
	gL = L;
}

#ifdef _DEBUG
void LUA_ClearExtVars(void)
{
	if (!gL)
		return;
	lua_newtable(gL);
	lua_setfield(gL, LUA_REGISTRYINDEX, LREG_EXTVARS);
}
#endif

// Use this variable to prevent certain functions from running
// if they were not called on lump load
// (i.e. they were called in hooks or coroutines etc)
int32_t lua_lumploading = 0;

// Load a script from a MYFILE
static inline void LUA_LoadFile(MYFILE *f, char *name, dboolean noresults)
{
	int errorhandlerindex;

	if (!name)
		name = wadfiles[f->wad]->filename;

	CONS_Printf("Loading Lua script from %s\n", name);

	if (!gL) // Lua needs to be initialized
		LUA_ClearState();

	lua_lumploading++; // turn on loading flag

	lua_pushcfunction(gL, LUA_GetErrorMessage);
	errorhandlerindex = lua_gettop(gL);
	if (luaL_loadbuffer(gL, f->data, f->size, va("@%s",name)) || lua_pcall(gL, 0, noresults ? 0 : LUA_MULTRET, lua_gettop(gL) - 1)) {
		CONS_Alert(CONS_WARNING,"%s\n",lua_tostring(gL,-1));
		lua_pop(gL,1);
	}
	lua_gc(gL, LUA_GCCOLLECT, 0);
	lua_remove(gL, errorhandlerindex);

	lua_lumploading--; // turn off again
}

// Load a script from a lump
void LUA_LoadLump(uint16_t wad, uint16_t lump, dboolean noresults)
{
	MYFILE f;
	char *name;
	size_t len;

	if (M_CheckParm("-nolua"))
	{
		return;
	}

	f.wad = wad;
	f.size = W_LumpLengthPwad(wad, lump);
	f.data = Z_Malloc(f.size, PU_LUA, NULL);
	W_ReadLumpPwad(wad, lump, f.data);
	f.curpos = f.data;

	len = strlen(wadfiles[wad]->filename); // length of file name

	if (wadfiles[wad]->type == RET_LUA)
	{
		name = malloc(len+1);

		if (!name)
			I_Error("LUA_LoadLump: Out of memory!\n");

		strcpy(name, wadfiles[wad]->filename);
	}
	else // If it's not a .lua file, copy the lump name in too.
	{
		lumpinfo_t *lump_p = &wadfiles[wad]->lumpinfo[lump];
		len += 1 + strlen(lump_p->fullname); // length of file name, '|', and lump name
		name = malloc(len+1);

		if (!name)
			I_Error("LUA_LoadLump: Out of memory!\n");

		snprintf(name, len + 1, "%s|%s", wadfiles[wad]->filename, lump_p->fullname);
	}

	lua_compatmode = wadfiles[wad]->compatmode;

	LUA_LoadFile(&f, name, noresults); // actually load file!

	// Okay, we've modified the game beyond the point of no return.
	G_SetGameModified(multiplayer, true);

	free(name);
	Z_Free(f.data);
}

#ifdef LUA_ALLOW_BYTECODE
// must match lua_Writer
static int dumpWriter(lua_State *L, const void *p, size_t sz, void *ud)
{
	FILE *handle = (FILE*)ud;
	I_Assert(handle != NULL);
	(void)L;
	if (!sz) return 0; // nothing to write? can't fail that! :D
	return (fwrite(p, 1, sz, handle) != sz); // if fwrite != sz, we've failed.
}

// Compile a script by name and dump it back to disk.
void LUA_DumpFile(const char *filename)
{
	FILE *handle;
	char filenamebuf[MAX_WADPATH];

	if (!gL) // Lua needs to be initialized
		LUA_ClearState(false);

	// find the file the SRB2 way
	strncpy(filenamebuf, filename, MAX_WADPATH);
	filenamebuf[MAX_WADPATH - 1] = '\0';
	filename = filenamebuf;
	if ((handle = fopen(filename, "rb")) == NULL)
	{
		// If we failed to load the file with the path as specified by
		// the user, strip the directories and search for the file.
		nameonly(filenamebuf);

		// If findfile finds the file, the full path will be returned
		// in filenamebuf == filename.
		if (findfile(filenamebuf, NULL, true))
		{
			if ((handle = fopen(filename, "rb")) == NULL)
			{
				CONS_Alert(CONS_ERROR, M_GetText("Can't open %s\n"), filename);
				return;
			}
		}
		else
		{
			CONS_Alert(CONS_ERROR, M_GetText("File %s not found.\n"), filename);
			return;
		}
	}
	fclose(handle);

	// pass the path we found to Lua
	// luaL_loadfile will open and read the file in as a Lua function
	if (luaL_loadfile(gL, filename)) {
		CONS_Alert(CONS_ERROR,"%s\n",lua_tostring(gL,-1));
		lua_pop(gL, 1);
		return;
	}

	// dump it back to disk
	if ((handle = fopen(filename, "wb")) == NULL)
		CONS_Alert(CONS_ERROR, M_GetText("Can't write to %s\n"), filename);
	if (lua_dump(gL, dumpWriter, handle))
		CONS_Printf("Failed while writing %s to disk... Sorry!\n", filename);
	else
		CONS_Printf("Successfully compiled %s into bytecode.\n", filename);
	fclose(handle);
	lua_pop(gL, 1); // function is still on stack after lua_dump
	lua_gc(gL, LUA_GCCOLLECT, 0);
	return;
}
#endif

static void createMathLibState(void)
{
	mL = luaL_newstate();
	lua_atpanic(mL, LUA_Panic);

	// open only enum lib
	lua_pushcfunction(mL, LUA_EnumLib);
	lua_pushdboolean(mL, true);
	lua_call(mL, 1, 0);
}

fixed_t LUA_EvalMathEx(const char *word, const char **error)
{
	char buf[1024], *b;
	const char *p;
	fixed_t res = 0;

	if (mL == NULL)
		createMathLibState();

	// change ^ into ^^ for Lua.
	strcpy(buf, "return ");
	b = buf+strlen(buf);
	for (p = word; *p && b < &buf[1022]; p++)
	{
		*b++ = *p;
		if (*p == '^')
			*b++ = '^';
	}
	// length of word is zero!?
	if (p == word)
		return 0;
	*b = '\0';

	// eval string.
	lua_settop(mL, 0);
	if (luaL_dostring(mL, buf))
	{
		p = lua_tostring(mL, -1);

		// If there is [string "..."]:1: text, skip it
		if (strstr(p, ":") != NULL)
		{
			while (*p++ != ':' && *p);

			p += 3; // "1: "
		}
		if (error) *error = p;
	}
	else
		res = lua_tointeger(mL, -1);

	return res;
}

fixed_t LUA_EvalMath(const char *word)
{
	const char *error = NULL;
	fixed_t res = LUA_EvalMathEx(word, &error);

	if (error)
		CONS_Alert(CONS_WARNING, "%s\n", error);

	return res;
}

// Takes a pointer, any pointer, and a metatable name
// Creates a userdata for that pointer with the given metatable
// Pushes it to the stack and stores it in the registry.
void LUA_PushUserdata(lua_State *L, void *data, const char *meta)
{
	if (LUA_RawPushUserdata(L, data) == LPUSHED_NEW)
	{
		luaL_getmetatable(L, meta);
		lua_setmetatable(L, -2);
	}
}

// Same as LUA_PushUserdata but don't set a metatable yet.
lpushed_t LUA_RawPushUserdata(lua_State *L, void *data)
{
	lpushed_t status = LPUSHED_NIL;

	void **userdata;

	if (!data) { // push a NULL
		lua_pushnil(L);
		return status;
	}

	lua_getfield(L, LUA_REGISTRYINDEX, LREG_VALID);
	I_Assert(lua_istable(L, -1));

	lua_pushlightuserdata(L, data);
	lua_rawget(L, -2);

	if (lua_isnil(L, -1)) { // no userdata? deary me, we'll have to make one.
		lua_pop(L, 1); // pop the nil

		// create the userdata
		userdata = lua_newuserdata(L, sizeof(void *));
		*userdata = data;

		// Set it in the registry so we can find it again
		lua_pushlightuserdata(L, data); // k (store the userdata via the data's pointer)
		lua_pushvalue(L, -2); // v (copy of the userdata)
		lua_rawset(L, -4);

		// stack is left with the userdata on top, as if getting it had originally succeeded.

		status = LPUSHED_NEW;
	}
	else
		status = LPUSHED_EXISTING;

	lua_remove(L, -2); // remove LREG_VALID

	return status;
}

// When userdata is freed, use this function to remove it from Lua.
void LUA_InvalidateUserdata(void *data)
{
	void **userdata;
	if (!gL)
		return;

	// fetch the userdata
	lua_getfield(gL, LUA_REGISTRYINDEX, LREG_VALID);
	I_Assert(lua_istable(gL, -1));
		lua_pushlightuserdata(gL, data);
		lua_rawget(gL, -2);
			if (lua_isnil(gL, -1)) { // not found, not in lua
				lua_pop(gL, 2); // pop nil and LREG_VALID
				return;
			}

			// nullify any additional data
			lua_getfield(gL, LUA_REGISTRYINDEX, LREG_EXTVARS);
			I_Assert(lua_istable(gL, -1));
				lua_pushlightuserdata(gL, data);
				lua_pushnil(gL);
				lua_rawset(gL, -3);
			lua_pop(gL, 1);

			// invalidate the userdata
			userdata = lua_touserdata(gL, -1);
			*userdata = NULL;
		lua_pop(gL, 1);

		// remove it from the registry
		lua_pushlightuserdata(gL, data);
		lua_pushnil(gL);
		lua_rawset(gL, -3);
	lua_pop(gL, 1); // pop LREG_VALID
}

// Invalidate level data arrays
void LUA_InvalidateLevel(void)
{
	thinker_t *th;
	size_t i;
	ffloor_t *rover = NULL;
	if (!gL)
		return;
	for (i = 0; i < NUM_THINKERLISTS; i++)
		for (th = thlist[i].next; th && th != &thlist[i]; th = th->next)
			LUA_InvalidateUserdata(th);

	LUA_InvalidateMapthings();

	for (i = 0; i < numsubsectors; i++)
		LUA_InvalidateUserdata(&subsectors[i]);
	for (i = 0; i < numsectors; i++)
	{
		LUA_InvalidateUserdata(&sectors[i]);
		LUA_InvalidateUserdata(&sectors[i].lines);
		LUA_InvalidateUserdata(&sectors[i].tags);
		if (sectors[i].extra_colormap)
			LUA_InvalidateUserdata(sectors[i].extra_colormap);
		LUA_InvalidateUserdata(&sectors[i].args);
		if (sectors[i].ffloors)
		{
			for (rover = sectors[i].ffloors; rover; rover = rover->next)
				LUA_InvalidateUserdata(rover);
		}
	}
	for (i = 0; i < numlines; i++)
	{
		LUA_InvalidateUserdata(&lines[i]);
		LUA_InvalidateUserdata(&lines[i].tags);
		LUA_InvalidateUserdata(lines[i].args);
		LUA_InvalidateUserdata(lines[i].stringargs);
		LUA_InvalidateUserdata(lines[i].sidenum);
	}
	for (i = 0; i < numsides; i++)
		LUA_InvalidateUserdata(&sides[i]);
	for (i = 0; i < numvertexes; i++)
		LUA_InvalidateUserdata(&vertexes[i]);
	for (i = 0; i < (size_t)numPolyObjects; i++)
	{
		LUA_InvalidateUserdata(&PolyObjects[i]);
		LUA_InvalidateUserdata(&PolyObjects[i].vertices);
		LUA_InvalidateUserdata(&PolyObjects[i].lines);
	}
	for (pslope_t *slope = slopelist; slope; slope = slope->next)
	{
		LUA_InvalidateUserdata(slope);
		LUA_InvalidateUserdata(&slope->normal);
		LUA_InvalidateUserdata(&slope->o);
		LUA_InvalidateUserdata(&slope->d);
	}
#ifdef HAVE_LUA_SEGS
	for (i = 0; i < numsegs; i++)
		LUA_InvalidateUserdata(&segs[i]);
	for (i = 0; i < numnodes; i++)
	{
		LUA_InvalidateUserdata(&nodes[i]);
		LUA_InvalidateUserdata(nodes[i].bbox);
		LUA_InvalidateUserdata(nodes[i].children);
	}
#endif
	for (i = 0; i < K_GetNumWaypoints(); i++)
	{
		LUA_InvalidateUserdata(K_GetWaypointFromIndex(i));
	}

	// We invalidated a lot of userdata objects, a lot of them probably ended up unreachable so they can be
	// GC'd right away
	lua_gc(gL, LUA_GCCOLLECT, 0);
}

void LUA_InvalidateMapthings(void)
{
	size_t i;
	if (!gL)
		return;

	for (i = 0; i < nummapthings; i++)
	{
		LUA_InvalidateUserdata(&mapthings[i]);
		LUA_InvalidateUserdata(mapthings[i].args);
		LUA_InvalidateUserdata(mapthings[i].stringargs);
		LUA_InvalidateUserdata(mapthings[i].script_args);
		LUA_InvalidateUserdata(mapthings[i].script_stringargs);
	}
}

void LUA_InvalidatePlayer(player_t *player)
{
	if (!gL)
		return;
	LUA_InvalidateUserdata(player);
	LUA_InvalidateUserdata(player->karthud);
	LUA_InvalidateUserdata(player->kartstuff);
	LUA_InvalidateUserdata(player->powers);
	LUA_InvalidateUserdata(&player->loop);
	LUA_InvalidateUserdata(&player->cmd);
	LUA_InvalidateUserdata(&player->botvars);
}

enum
{
	ARCH_NULL=0,
	ARCH_TRUE,
	ARCH_FALSE,
	ARCH_INT8,
	ARCH_INT16,
	ARCH_INT32,
	ARCH_SMALLSTRING,
	ARCH_LARGESTRING,
	ARCH_TABLE,

	ARCH_MOBJINFO,
	ARCH_STATE,
	ARCH_MOBJ,
	ARCH_PLAYER,
	ARCH_MAPTHING,
	ARCH_VERTEX,
	ARCH_LINE,
	ARCH_SIDE,
	ARCH_SUBSECTOR,
	ARCH_SECTOR,
#ifdef HAVE_LUA_SEGS
	ARCH_SEG,
	ARCH_NODE,
#endif
	ARCH_FFLOOR,
	ARCH_POLYOBJ,
	ARCH_SLOPE,
	ARCH_MAPHEADER,
	ARCH_SKINCOLOR,

	ARCH_TEND=0xFF,
};

static const struct {
	const char *meta;
	uint8_t arch;
} meta2arch[] = {
	{META_MOBJINFO, ARCH_MOBJINFO},
	{META_STATE,    ARCH_STATE},
	{META_MOBJ,     ARCH_MOBJ},
	{META_PLAYER,   ARCH_PLAYER},
	{META_MAPTHING, ARCH_MAPTHING},
	{META_VERTEX,   ARCH_VERTEX},
	{META_LINE,     ARCH_LINE},
	{META_SIDE,     ARCH_SIDE},
	{META_SUBSECTOR,ARCH_SUBSECTOR},
	{META_SECTOR,   ARCH_SECTOR},
#ifdef HAVE_LUA_SEGS
	{META_SEG,      ARCH_SEG},
	{META_NODE,     ARCH_NODE},
#endif
	{META_FFLOOR,	ARCH_FFLOOR},
	{META_POLYOBJ,  ARCH_POLYOBJ},
	{META_SLOPE,    ARCH_SLOPE},
	{META_MAPHEADER,ARCH_MAPHEADER},
	{META_SKINCOLOR,ARCH_SKINCOLOR},
	{NULL,          ARCH_NULL}
};

static uint8_t GetUserdataArchType(int index)
{
	uint8_t i;
	lua_getmetatable(gL, index);

	for (i = 0; meta2arch[i].meta; i++)
	{
		luaL_getmetatable(gL, meta2arch[i].meta);
		if (lua_rawequal(gL, -1, -2))
		{
			lua_pop(gL, 2);
			return meta2arch[i].arch;
		}
		lua_pop(gL, 1);
	}

	lua_pop(gL, 1);
	return ARCH_NULL;
}

static uint8_t ArchiveValue(uint8_t **p, int TABLESINDEX, int myindex)
{
	if (myindex < 0)
		myindex = lua_gettop(gL)+1+myindex;
	switch (lua_type(gL, myindex))
	{
	case LUA_TNONE:
	case LUA_TNIL:
		WRITEUINT8(*p, ARCH_NULL);
		break;
	// This might be a problem. D:
	case LUA_TLIGHTUSERDATA:
	case LUA_TTHREAD:
	case LUA_TFUNCTION:
		WRITEUINT8(*p, ARCH_NULL);
		return 2;
	case LUA_TBOOLEAN:
		WRITEUINT8(*p, lua_todboolean(gL, myindex) ? ARCH_TRUE : ARCH_FALSE);
		break;
	case LUA_TNUMBER:
	{
		lua_Integer number = lua_tointeger(gL, myindex);
		if (number >= INT8_MIN && number <= INT8_MAX)
		{
			WRITEUINT8(*p, ARCH_INT8);
			WRITEINT8(*p, number);
		}
		else if (number >= INT16_MIN && number <= INT16_MAX)
		{
			WRITEUINT8(*p, ARCH_INT16);
			WRITEINT16(*p, number);
		}
		else
		{
			WRITEUINT8(*p, ARCH_INT32);
			WRITEFIXED(*p, number);
		}
		break;
	}
	case LUA_TSTRING:
	{
		uint32_t len = (uint32_t)lua_objlen(gL, myindex); // get length of string, including embedded zeros
		const char *s = lua_tostring(gL, myindex);
		uint32_t i = 0;
		// if you're wondering why we're writing a string to save_p this way,
		// it turns out that Lua can have embedded zeros ('\0') in the strings,
		// so we can't use WRITESTRING as that cuts off when it finds a '\0'.
		// Saving the size of the string also allows us to get the size of the string on the other end,
		// fixing the awful crashes previously encountered for reading strings longer than 1024
		// (yes I know that's kind of a stupid thing to care about, but it'd be evil to trim or ignore them?)
		// -- Monster Iestyn 05/08/18
		if (len < 255)
		{
			WRITEUINT8(*p, ARCH_SMALLSTRING);
			WRITEUINT8(*p, len); // save size of string
		}
		else
		{
			WRITEUINT8(*p, ARCH_LARGESTRING);
			WRITEUINT32(*p, len); // save size of string
		}
		while (i < len)
			WRITECHAR(*p, s[i++]); // write chars individually, including the embedded zeros
		break;
	}
	case LUA_TTABLE:
	{
		dboolean found = false;
		int32_t i;
		uint16_t t = (uint16_t)lua_objlen(gL, TABLESINDEX);

		for (i = 1; i <= t && !found; i++)
		{
			lua_rawgeti(gL, TABLESINDEX, i);
			if (lua_rawequal(gL, myindex, -1))
			{
				t = i;
				found = true;
			}
			lua_pop(gL, 1);
		}
		if (!found)
		{
			t++;

			if (t == 0)
			{
				CONS_Alert(CONS_ERROR, "Too many tables to archive!\n");
				WRITEUINT8(*p, ARCH_NULL);
				return 0;
			}
		}

		WRITEUINT8(*p, ARCH_TABLE);
		WRITEUINT16(*p, t);

		if (!found)
		{
			lua_pushvalue(gL, myindex);
			lua_rawseti(gL, TABLESINDEX, t);
			return 1;
		}
		break;
	}
	case LUA_TUSERDATA:
		switch (GetUserdataArchType(myindex))
		{
		case ARCH_MOBJINFO:
		{
			mobjinfo_t *info = *((mobjinfo_t **)lua_touserdata(gL, myindex));
			WRITEUINT8(*p, ARCH_MOBJINFO);
			WRITEUINT16(*p, info - mobjinfo);
			break;
		}
		case ARCH_STATE:
		{
			state_t *state = *((state_t **)lua_touserdata(gL, myindex));
			WRITEUINT8(*p, ARCH_STATE);
			WRITEUINT16(*p, state - states);
			break;
		}
		case ARCH_MOBJ:
		{
			mobj_t *mobj = *((mobj_t **)lua_touserdata(gL, myindex));
			if (!mobj)
			{
				WRITEUINT8(*p, ARCH_NULL);
				return 3;
			}
			else
			{
				WRITEUINT8(*p, ARCH_MOBJ);
				WRITEUINT32(*p, mobj->mobjnum);
			}
			break;
		}
		case ARCH_PLAYER:
		{
			player_t *player = *((player_t **)lua_touserdata(gL, myindex));
			if (!player)
			{
				WRITEUINT8(*p, ARCH_NULL);
				return 3;
			}
			else
			{
				WRITEUINT8(*p, ARCH_PLAYER);
				WRITEUINT8(*p, player - players);
			}
			break;
		}
		case ARCH_MAPTHING:
		{
			mapthing_t *mapthing = *((mapthing_t **)lua_touserdata(gL, myindex));
			if (!mapthing)
			{
				WRITEUINT8(*p, ARCH_NULL);
				return 3;
			}
			else
			{
				WRITEUINT8(*p, ARCH_MAPTHING);
				WRITEUINT16(*p, mapthing - mapthings);
			}
			break;
		}
		case ARCH_VERTEX:
		{
			vertex_t *vertex = *((vertex_t **)lua_touserdata(gL, myindex));
			if (!vertex)
			{
				WRITEUINT8(*p, ARCH_NULL);
				return 3;
			}
			else
			{
				WRITEUINT8(*p, ARCH_VERTEX);
				WRITEUINT16(*p, vertex - vertexes);
			}
			break;
		}
		case ARCH_LINE:
		{
			line_t *line = *((line_t **)lua_touserdata(gL, myindex));
			if (!line)
			{
				WRITEUINT8(*p, ARCH_NULL);
				return 3;
			}
			else
			{
				WRITEUINT8(*p, ARCH_LINE);
				WRITEUINT16(*p, line - lines);
			}
			break;
		}
		case ARCH_SIDE:
		{
			side_t *side = *((side_t **)lua_touserdata(gL, myindex));
			if (!side)
			{
				WRITEUINT8(*p, ARCH_NULL);
				return 3;
			}
			else
			{
				WRITEUINT8(*p, ARCH_SIDE);
				WRITEUINT16(*p, side - sides);
			}
			break;
		}
		case ARCH_SUBSECTOR:
		{
			subsector_t *subsector = *((subsector_t **)lua_touserdata(gL, myindex));
			if (!subsector)
			{
				WRITEUINT8(*p, ARCH_NULL);
				return 3;
			}
			else
			{
				WRITEUINT8(*p, ARCH_SUBSECTOR);
				WRITEUINT16(*p, subsector - subsectors);
			}
			break;
		}
		case ARCH_SECTOR:
		{
			sector_t *sector = *((sector_t **)lua_touserdata(gL, myindex));
			if (!sector)
			{
				WRITEUINT8(*p, ARCH_NULL);
				return 3;
			}
			else
			{
				WRITEUINT8(*p, ARCH_SECTOR);
				WRITEUINT16(*p, sector - sectors);
			}
			break;
		}
#ifdef HAVE_LUA_SEGS
		case ARCH_SEG:
		{
			seg_t *seg = *((seg_t **)lua_touserdata(gL, myindex));
			if (!seg)
				WRITEUINT8(*p, ARCH_NULL);
			else {
				WRITEUINT8(*p, ARCH_SEG);
				WRITEUINT16(*p, seg - segs);
			}
			break;
		}
		case ARCH_NODE:
		{
			node_t *node = *((node_t **)lua_touserdata(gL, myindex));
			if (!node)
				WRITEUINT8(*p, ARCH_NULL);
			else {
				WRITEUINT8(*p, ARCH_NODE);
				WRITEUINT16(*p, node - nodes);
			}
			break;
		}
#endif
		case ARCH_FFLOOR:
		{
			ffloor_t *rover = *((ffloor_t **)lua_touserdata(gL, myindex));
			if (!rover)
				WRITEUINT8(*p, ARCH_NULL);
			else {
				uint16_t i = P_GetFFloorID(rover);
				if (i == UINT16_MAX) // invalid ID
					WRITEUINT8(*p, ARCH_NULL);
				else
				{
					WRITEUINT8(*p, ARCH_FFLOOR);
					WRITEUINT16(*p, rover->target - sectors);
					WRITEUINT16(*p, i);
				}
			}
			break;
		}
		case ARCH_POLYOBJ:
		{
			polyobj_t *polyobj = *((polyobj_t **)lua_touserdata(gL, myindex));
			if (!polyobj)
				WRITEUINT8(*p, ARCH_NULL);
			else {
				WRITEUINT8(*p, ARCH_POLYOBJ);
				WRITEUINT16(*p, polyobj-PolyObjects);
			}
			break;
		}
		case ARCH_SLOPE:
		{
			pslope_t *slope = *((pslope_t **)lua_touserdata(gL, myindex));
			if (!slope)
			{
				WRITEUINT8(*p, ARCH_NULL);
				return 3;
			}
			else
			{
				WRITEUINT8(*p, ARCH_SLOPE);
				WRITEUINT16(*p, slope->id);
			}
			break;
		}
		case ARCH_MAPHEADER:
		{
			mapheader_t *header = *((mapheader_t **)lua_touserdata(gL, myindex));
			if (!header)
			{
				WRITEUINT8(*p, ARCH_NULL);
				return 3;
			}
			else
			{
				WRITEUINT8(*p, ARCH_MAPHEADER);
				WRITEUINT16(*p, header - *mapheaderinfo);
			}
			break;
		}

		case ARCH_SKINCOLOR:
		{
			skincolor_t *info = *((skincolor_t **)lua_touserdata(gL, myindex));
			WRITEUINT8(*p, ARCH_SKINCOLOR);
			WRITEUINT16(*p, info - skincolors);
			break;
		}
		default:
			WRITEUINT8(*p, ARCH_NULL);
			return 2;
		}
		break;
	}
	return 0;
}

static void ArchiveExtVars(uint8_t **p, void *pointer, const char *ptype)
{
	int TABLESINDEX;
	uint16_t i;

	if (!gL) {
		if (fastcmp(ptype,"player")) // players must always be included, even if no vars
			WRITEUINT16(*p, 0);
		return;
	}

	TABLESINDEX = lua_gettop(gL);

	lua_getfield(gL, LUA_REGISTRYINDEX, LREG_EXTVARS);
	I_Assert(lua_istable(gL, -1));
	lua_pushlightuserdata(gL, pointer);
	lua_rawget(gL, -2);
	lua_remove(gL, -2); // pop LREG_EXTVARS

	if (!lua_istable(gL, -1))
	{ // no extra values table
		lua_pop(gL, 1);
		if (fastcmp(ptype,"player")) // players must always be included, even if no vars
			WRITEUINT16(*p, 0);
		return;
	}

	lua_pushnil(gL);
	for (i = 0; lua_next(gL, -2); i++)
		lua_pop(gL, 1);

	// skip anything that has an empty table and isn't a player.
	if (i == 0)
	{
		if (fastcmp(ptype,"player")) // always include players even if they have no extra variables
			WRITEUINT16(*p, 0);
		lua_pop(gL, 1);
		return;
	}

	if (fastcmp(ptype,"mobj")) // mobjs must write their mobjnum as a header
		WRITEUINT32(*p, ((mobj_t *)pointer)->mobjnum);
	WRITEUINT16(*p, i);
	lua_pushnil(gL);
	while (lua_next(gL, -2))
	{
		I_Assert(lua_type(gL, -2) == LUA_TSTRING);
		WRITESTRING(*p, lua_tostring(gL, -2));
		if (ArchiveValue(p, TABLESINDEX, -1) == 2)
			CONS_Alert(CONS_ERROR, "Type of value for %s entry '%s' (%s) could not be archived!\n", ptype, lua_tostring(gL, -2), luaL_typename(gL, -1));
		lua_pop(gL, 1);
	}

	lua_pop(gL, 1);
}

static int NetArchive(lua_State *L)
{
	int TABLESINDEX = lua_upvalueindex(1);
	savebuffer_t *save = lua_touserdata(L, lua_upvalueindex(2));
	int i, n = lua_gettop(L);
	for (i = 1; i <= n; i++)
		ArchiveValue(&save->p, TABLESINDEX, i);
	return n;
}

static void ArchiveTables(uint8_t **p)
{
	int TABLESINDEX;
	uint16_t i, n;
	uint8_t e;

	if (!gL)
		return;

	TABLESINDEX = lua_gettop(gL);

	n = (uint16_t)lua_objlen(gL, TABLESINDEX);
	for (i = 1; i <= n; i++)
	{
		lua_rawgeti(gL, TABLESINDEX, i);
		lua_pushnil(gL);
		while (lua_next(gL, -2))
		{
			// Write key
			e = ArchiveValue(p, TABLESINDEX, -2); // key should be either a number or a string, ArchiveValue can handle this.

			if (e == 1)
				n++; // the table contained a new table we'll have to archive. :(
			else if (e == 2) // invalid key type (function, thread, lightuserdata, or anything we don't recognise)
			{
				CONS_Alert(CONS_ERROR, "Index '%s' (%s) of table %d could not be archived!\n", lua_tostring(gL, -2), luaL_typename(gL, -2), i);
			}
			else if (e == 3) // nil key due to invalid userdata. NOT an error.
			{
				lua_pop(gL, 1);
				continue;
			}

			// Write value
			e = ArchiveValue(p, TABLESINDEX, -1);
			if (e == 1)
			{
				n++; // the table contained a new table we'll have to archive. :(
			}
			else if (e == 2) // invalid value type
				CONS_Alert(CONS_ERROR, "Type of value for table %d entry '%s' (%s) could not be archived!\n", i, lua_tostring(gL, -2), luaL_typename(gL, -1));

			lua_pop(gL, 1);
		}
		WRITEUINT8(*p, ARCH_TEND);

		// Write metatable ID
		if (lua_getmetatable(gL, -1))
		{
			// registry.metatables[metatable]
			lua_getfield(gL, LUA_REGISTRYINDEX, LREG_METATABLES);
			lua_pushvalue(gL, -2);
			lua_gettable(gL, -2);
			WRITEUINT16(*p, lua_isnil(gL, -1) ? 0 : lua_tointeger(gL, -1));
			lua_pop(gL, 3);
		}
		else
			WRITEUINT16(*p, 0);

		lua_pop(gL, 1);
	}
}

static uint8_t UnArchiveValue(uint8_t **p, int TABLESINDEX, dboolean compat)
{
	uint8_t type = READUINT8(*p);

	if (compat) switch (type)
	{
	case 0: // ARCH_NULL
	case ARCH_TEND:
		break;
	case 1: // ARCH_BOOLEAN
		lua_pushdboolean(gL, READUINT8(*p));
		return 0;
	case 2: // ARCH_SIGNED
		type = ARCH_INT32;
		break;
	case 3: // ARCH_STRING
	{
		uint16_t len = READUINT16(*p);
		char *value = malloc(len);

		if (value == NULL)
			I_Error("UnArchiveValue: Out of memory!\n");

		READMEM(*p, value, len);
		lua_pushlstring(gL, value, len);
		free(value);
		return 0;
	}
	default:
		type += 4; // old ARCH_TABLE is 4, new ARCH_TABLE is 8
		// ARCH_FFLOOR and ARCH_POLYOBJ inserted between ARCH_SECTOR and ARCH_SLOPE
		if (type > ARCH_SECTOR)
#ifdef HAVE_LUA_SEGS
			type += 4;
#else
			type += 2;
#endif
		break;
	}

	switch (type)
	{
	case ARCH_NULL:
		lua_pushnil(gL);
		break;
	case ARCH_TRUE:
		lua_pushdboolean(gL, true);
		break;
	case ARCH_FALSE:
		lua_pushdboolean(gL, false);
		break;
	case ARCH_INT8:
		lua_pushinteger(gL, READINT8(*p));
		break;
	case ARCH_INT16:
		lua_pushinteger(gL, READINT16(*p));
		break;
	case ARCH_INT32:
		lua_pushinteger(gL, READFIXED(*p));
		break;
	case ARCH_SMALLSTRING:
	case ARCH_LARGESTRING:
	{
		uint32_t len;
		char *value;
		uint32_t i = 0;

		// See my comments in the ArchiveValue function;
		// it's much the same for reading strings as writing them!
		// (i.e. we can't use READSTRING either)
		// -- Monster Iestyn 05/08/18
		if (type == ARCH_SMALLSTRING)
			len = READUINT8(*p); // length of string, including embedded zeros
		else
			len = READUINT32(*p); // length of string, including embedded zeros
		value = malloc(len); // make temp buffer of size len

		if (value == NULL)
			I_Error("UnArchiveValue: Out of memory!\n");

		// now read the actual string
		while (i < len)
			value[i++] = READCHAR(*p); // read chars individually, including the embedded zeros
		lua_pushlstring(gL, value, len); // push the string (note: this function supports embedded zeros)
		free(value); // free the buffer
		break;
	}
	case ARCH_TABLE:
	{
		uint16_t tid = READUINT16(*p);
		lua_rawgeti(gL, TABLESINDEX, tid);
		if (lua_isnil(gL, -1))
		{
			lua_pop(gL, 1);
			lua_newtable(gL);
			lua_pushvalue(gL, -1);
			lua_rawseti(gL, TABLESINDEX, tid);
			return 2;
		}
		break;
	}
	case ARCH_MOBJINFO:
		LUA_PushUserdata(gL, &mobjinfo[READUINT16(*p)], META_MOBJINFO);
		break;
	case ARCH_STATE:
		LUA_PushUserdata(gL, &states[READUINT16(*p)], META_STATE);
		break;
	case ARCH_MOBJ:
		if (compat)
		{
			// Skip this data, we can't read a mobj here, it'd point to garbage and crash the game.
			//CONS_Alert(CONS_WARNING, "Couldn't read mobj_t\n");
			*p += sizeof(uint32_t);
			lua_pushnil(gL);
		}
		else
			LUA_PushUserdata(gL, P_FindNewPosition(READUINT32(*p)), META_MOBJ);
		break;
	case ARCH_PLAYER:
		LUA_PushUserdata(gL, &players[READUINT8(*p)], META_PLAYER);
		break;
	case ARCH_MAPTHING:
		LUA_PushUserdata(gL, &mapthings[READUINT16(*p)], META_MAPTHING);
		break;
	case ARCH_VERTEX:
		LUA_PushUserdata(gL, &vertexes[READUINT16(*p)], META_VERTEX);
		break;
	case ARCH_LINE:
		LUA_PushUserdata(gL, &lines[READUINT16(*p)], META_LINE);
		break;
	case ARCH_SIDE:
		LUA_PushUserdata(gL, &sides[READUINT16(*p)], META_SIDE);
		break;
	case ARCH_SUBSECTOR:
		LUA_PushUserdata(gL, &subsectors[READUINT16(*p)], META_SUBSECTOR);
		break;
	case ARCH_SECTOR:
		LUA_PushUserdata(gL, &sectors[READUINT16(*p)], META_SECTOR);
		break;
#ifdef HAVE_LUA_SEGS
	case ARCH_SEG:
		LUA_PushUserdata(gL, &segs[READUINT16(*p)], META_SEG);
		break;
	case ARCH_NODE:
		LUA_PushUserdata(gL, &nodes[READUINT16(*p)], META_NODE);
		break;
#endif
	case ARCH_FFLOOR:
	{
		sector_t *sector = &sectors[READUINT16(*p)];
		uint16_t id = READUINT16(*p);
		ffloor_t *rover = P_GetFFloorByID(sector, id);
		if (rover)
			LUA_PushUserdata(gL, rover, META_FFLOOR);
		break;
	}
	case ARCH_POLYOBJ:
		LUA_PushUserdata(gL, &PolyObjects[READUINT16(*p)], META_POLYOBJ);
		break;
	case ARCH_SLOPE:
		LUA_PushUserdata(gL, P_SlopeById(READUINT16(*p)), META_SLOPE);
		break;
	case ARCH_MAPHEADER:
		LUA_PushUserdata(gL, mapheaderinfo[READUINT16(*p)], META_MAPHEADER);
		break;
	case ARCH_SKINCOLOR:
		LUA_PushUserdata(gL, &skincolors[READUINT16(*p)], META_SKINCOLOR);
		break;
	case ARCH_TEND:
		return 1;
	}
	return 0;
}

static void UnArchiveExtVars(uint8_t **p, void *pointer, dboolean compat)
{
	int TABLESINDEX;
	uint16_t field_count = READUINT16(*p);
	uint16_t i;
	char field[1024];

	if (field_count == 0)
		return;
	I_Assert(gL != NULL);

	TABLESINDEX = lua_gettop(gL);
	lua_createtable(gL, 0, field_count); // pointer's ext vars subtable

	for (i = 0; i < field_count; i++)
	{
		READSTRING(*p, field);
		UnArchiveValue(p, TABLESINDEX, compat);
		lua_setfield(gL, -2, field);
	}

	lua_getfield(gL, LUA_REGISTRYINDEX, LREG_EXTVARS);
	I_Assert(lua_istable(gL, -1));
	lua_pushlightuserdata(gL, pointer);
	lua_pushvalue(gL, -3); // pointer's ext vars subtable
	lua_rawset(gL, -3);
	lua_pop(gL, 2); // pop LREG_EXTVARS and pointer's subtable
}

static int NetUnArchive(lua_State *L)
{
	int TABLESINDEX = lua_upvalueindex(1);
	savebuffer_t *save = lua_touserdata(L, lua_upvalueindex(2));
	int i, n = lua_gettop(L);
	for (i = 1; i <= n; i++)
		UnArchiveValue(&save->p, TABLESINDEX, false);
	return n;
}

static void UnArchiveTables(uint8_t **p, dboolean compat)
{
	int TABLESINDEX;
	uint16_t i, n;
	uint16_t metatableid;

	if (!gL)
		return;

	TABLESINDEX = lua_gettop(gL);

	n = (uint16_t)lua_objlen(gL, TABLESINDEX);
	for (i = 1; i <= n; i++)
	{
		lua_rawgeti(gL, TABLESINDEX, i);
		while (true)
		{
			uint8_t e = UnArchiveValue(p, TABLESINDEX, compat); // read key
			if (e == 1) // End of table
				break;
			else if (!compat && e == 2) // Key contains a new table
				n++;

			if (UnArchiveValue(p, TABLESINDEX, compat) == 2) // read value
				n++;

			if (lua_isnil(gL, -2)) // if key is nil (if a function etc was accidentally saved)
			{
				CONS_Alert(CONS_ERROR, "A nil key in table %d was found! (Invalid key type or corrupted save?)\n", i);
				lua_pop(gL, 2); // pop key and value instead of setting them in the table, to prevent Lua panic errors
			}
			else
				lua_rawset(gL, -3);
		}

		metatableid = compat ? 0 : READUINT16(*p);
		if (metatableid)
		{
			// setmetatable(table, registry.metatables[metatableid])
			lua_getfield(gL, LUA_REGISTRYINDEX, LREG_METATABLES);
				lua_rawgeti(gL, -1, metatableid);
				if (lua_isnil(gL, -1))
					I_Error("Unknown metatable ID %d\n", metatableid);
				lua_setmetatable(gL, -3);
			lua_pop(gL, 1);
		}

		lua_pop(gL, 1);
	}
}

// void LUA_Step(void)
// {
// 	if (!gL)
// 		return;

// 	if (lua_gettop(gL) != 0)
// 	{
// 		// TODO: Fix lua stack garbage so print can be always on.
// 		TracyCMessage("garbage on lua stack!", strlen("garbage on lua stack!"));
// 		CONS_Alert(CONS_WARNING, "Eek, there is garbage on lua stack!\n");
// 		lua_settop(gL, 0);
// 		lua_gc(gL, LUA_GCSTEP, 1);
// 	}
// }

void LUA_Sync(savebuffer_t *save, dboolean network, dboolean compat)
{
	uint32_t mobjnum;
	int32_t i;
	thinker_t *th;

	if (gL)
		lua_newtable(gL); // tables to be archived.

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] && i > 0)	// NEVER skip player 0, this is for dedi servs.
			continue;

		// all players in game will be archived, even if they just add a 0.
		if (save->write)
			ArchiveExtVars(&save->p, &players[i], "player");
		else
			UnArchiveExtVars(&save->p, &players[i], compat);
	}

	if (save->write)
	{
		if (network == true)
		{
			if (gamestate == GS_LEVEL)
			{
				for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
				{
					if (th->function == (actionf_p1)P_RemoveThinkerDelayed)
						continue;

					// archive function will determine when to skip mobjs,
					// and write mobjnum in otherwise.
					ArchiveExtVars(&save->p, th, "mobj");
				}
			}

			WRITEUINT32(save->p, UINT32_MAX); // end of mobjs marker, replaces mobjnum.

			LUA_HookNetArchive(NetArchive, save); // call the NetArchive hook in archive mode
		}

		ArchiveTables(&save->p);
	}
	else
	{
		if (network == true)
		{
			do {
				mobjnum = READUINT32(save->p); // read a mobjnum
				for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
				{
					if (th->function == (actionf_p1)P_RemoveThinkerDelayed)
						continue;
					if (((mobj_t *)th)->mobjnum != mobjnum) // find matching mobj
						continue;
					UnArchiveExtVars(&save->p, th, false); // apply variables
				}
			} while(mobjnum != UINT32_MAX); // repeat until end of mobjs marker.

			LUA_HookNetArchive(NetUnArchive, save); // call the NetArchive hook in unarchive mode
		}

		UnArchiveTables(&save->p, compat);
	}

	if (gL)
		lua_pop(gL, 1); // pop tables
}

static void SetBasicMetamethods(
	lua_State *L,
	lua_CFunction get,
	lua_CFunction set,
	lua_CFunction len
)
{
	if (get)
		LUA_SetCFunctionField(L, "__index", get);
	if (set)
		LUA_SetCFunctionField(L, "__newindex", set);
	if (len)
		LUA_SetCFunctionField(L, "__len", len);
}

void LUA_SetCFunctionField(lua_State *L, const char *name, lua_CFunction value)
{
	lua_pushcfunction(L, value);
	lua_setfield(L, -2, name);
}

void LUA_RegisterUserdataMetatable(
	lua_State *L,
	const char *name,
	lua_CFunction get,
	lua_CFunction set,
	lua_CFunction len
)
{
	luaL_newmetatable(L, name);
	SetBasicMetamethods(L, get, set, len);
	lua_pop(L, 1);
}

// For mobj_t, player_t, etc. to take custom variables.
int Lua_optoption(lua_State *L, int narg, int def, int list_ref)
{
	int result = -1;

	if (lua_isnoneornil(L, narg))
		return def;

	I_Assert(lua_checkstack(L, 2));
	luaL_checkstring(L, narg);

	lua_rawgeti(L, LUA_REGISTRYINDEX, list_ref);
	I_Assert(lua_istable(L, -1));
	lua_pushvalue(L, narg);
	lua_rawget(L, -2);

	if (lua_isnumber(L, -1))
		result = lua_tointeger(L, -1);

	lua_pop(L, 2); // Pop result and fields table

	return result;
}


int Lua_CreateFieldTable(lua_State *L, const char *const lst[])
{
	int i;

	lua_newtable(L);
	for (i = 0; lst[i] != NULL; i++)
	{
		lua_pushstring(L, lst[i]);
		lua_pushinteger(L, i);
		lua_settable(L, -3);
	}

	return luaL_ref(L, LUA_REGISTRYINDEX);
}

size_t LUA_MemoryUsage(void)
{
	size_t usage = 0;

	if (gL)
		usage += lua_gc(gL, LUA_GCCOUNT, 0);

	if (mL)
		usage += lua_gc(mL, LUA_GCCOUNT, 0);

	return usage;
}

void LUA_PushTaggableObjectArray
(		lua_State *L,
		const char *field,
		lua_CFunction iterator,
		lua_CFunction indexer,
		lua_CFunction counter,
		taggroup_t *garray[],
		size_t * max_elements,
		void * element_array,
		size_t sizeof_element,
		const char *meta)
{
	lua_newuserdata(L, 0);
		lua_createtable(L, 0, 2);
			lua_createtable(L, 0, 2);
				lua_pushcfunction(L, iterator);
				lua_setfield(L, -2, "iterate");

				LUA_InsertTaggroupIterator(L, garray,
						max_elements, element_array, sizeof_element, meta);

				lua_createtable(L, 0, 1);
					lua_pushcfunction(L, indexer);
					lua_setfield(L, -2, "__index");
				lua_setmetatable(L, -2);
			lua_setfield(L, -2, "__index");

			lua_pushcfunction(L, counter);
			lua_setfield(L, -2, "__len");
		lua_setmetatable(L, -2);
	lua_setglobal(L, field);
}
