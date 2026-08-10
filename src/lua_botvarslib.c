// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2024 by Kart Krew.
// Copyright (C) 2020 by Sonic Team Junior.
// Copyright (C) 2016 by John "JTE" Muniz.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  lua_botvarslib.c
/// \brief player botvars structure library for Lua scripting

#include "doomdef.h"
#include "lua_script.h"
#include "lua_libs.h"
#include "k_bot.h"

enum botvars {
	botvars_valid = 0,
	botvars_style,
	botvars_difficulty,
	botvars_diffincrease,
	botvars_rival,
	botvars_rubberband,
};

static const char *const botvars_opt[] = {
	"valid",
	"style",
	"difficulty",
	"diffincrease",
	"rival",
	"rubberband",
	NULL
};

#define UNIMPLEMENTED luaL_error(L, LUA_QL("botvars_t") " field " LUA_QS " is not implemented for Lua and cannot be accessed.", follower_opt[field])

int botvars_fields_ref = LUA_NOREF;

static int botvars_get(lua_State *L)
{
	botvars_t *botvars = *((botvars_t **)luaL_checkudata(L, 1, META_BOTVARS));
	enum botvars field = Lua_optoption(L, 2, -1, botvars_fields_ref);

	// This is a property that always exists in a player.
	I_Assert(botvars != NULL);

	switch (field)
	{
	case botvars_valid:
		lua_pushdboolean(L, botvars != NULL);
		break;
	case botvars_style:
		lua_pushinteger(L, botvars->style);
		break;
	case botvars_difficulty:
		lua_pushinteger(L, botvars->difficulty);
		break;
	case botvars_diffincrease:
		lua_pushinteger(L, botvars->diffincrease);
		break;
	case botvars_rival:
		lua_pushdboolean(L, botvars->rival);
		break;
	case botvars_rubberband:
		lua_pushfixed(L, botvars->rubberband);
		break;
	}
	return 1;
}

#define NOSET luaL_error(L, LUA_QL("botvars_t") " field " LUA_QS " should not be set directly.", botvars_opt[field])

static int botvars_set(lua_State *L)
{
	botvars_t *botvars = *((botvars_t **)luaL_checkudata(L, 1, META_BOTVARS));
	enum botvars field = Lua_optoption(L, 2, botvars_valid, botvars_fields_ref);

	// This is a property that always exists in a player.
	I_Assert(botvars != NULL);

	INLEVEL

	switch(field)
	{
	case botvars_valid:
		return NOSET;
	case botvars_style:
		botvars->style = luaL_checkinteger(L, 3);
		break;
	case botvars_difficulty:
		botvars->difficulty = luaL_checkinteger(L, 3);
		break;
	case botvars_diffincrease:
		botvars->diffincrease = luaL_checkinteger(L, 3);
		break;
	case botvars_rival:
		botvars->rival = luaL_checkdboolean(L, 3);
		break;
	case botvars_rubberband:
		botvars->rubberband = luaL_checkfixed(L, 3);
		break;
	}
	return 0;
}

#undef NOSET

enum botdata {
	botdata_valid,
	botdata_itemdelay,
	botdata_itemconfirm,
	botdata_turnconfirm,
	botdata_respawnconfirm,
	botdata_driftstate,
	botdata_driftturn,
	botdata_drifttime,
	botdata_driftlockout,
	botdata_driftmaxdist,
	botdata_driftpowerdiv,
	botdata_driftstatedelay,
	botdata_driftskill,
	botdata_acceldown,
	botdata_brakedown,
	botdata_driftdown,
	botdata_itemdown,
	botdata_dolookback,
	botdata_itemwasdown,
	botdata_itemthrow,
	botdata_turnamt,
};

static const char *const botdata_opt[] = {
	"valid",
	"itemdelay",
	"itemconfirm",
	"turnconfirm",
	"respawnconfirm",
	"driftstate",
	"driftturn",
	"drifttime",
	"driftlockout",
	"driftmaxdist",
	"driftpowerdiv",
	"driftstatedelay",
	"driftskill",
	"acceldown",
	"brakedown",
	"driftdown",
	"itemdown",
	"dolookback",
	"itemwasdown",
	"itemthrow",
	"turnamt",
	NULL
};

int botdata_fields_ref = LUA_NOREF;

static int botdata_get(lua_State *L)
{
	botdata_t *botdata = *((botdata_t **)luaL_checkudata(L, 1, META_BOTDATA));
	enum botdata field = Lua_optoption(L, 2, -1, botdata_fields_ref);

	// it's a static array... i think...
	I_Assert(botdata != NULL);

	switch (field)
	{
	case botdata_valid:
		lua_pushdboolean(L, botdata != NULL);
		break;
	case botdata_itemdelay:
		lua_pushinteger(L, botdata->itemdelay);
		break;
	case botdata_itemconfirm:
		lua_pushinteger(L, botdata->itemconfirm);
		break;
	case botdata_turnconfirm:
		lua_pushinteger(L, botdata->turnconfirm);
		break;
	case botdata_respawnconfirm:
		lua_pushinteger(L, botdata->respawnconfirm);
		break;
	case botdata_driftstate:
		lua_pushinteger(L, botdata->driftstate);
		break;
	case botdata_driftturn:
		lua_pushinteger(L, botdata->driftturn);
		break;
	case botdata_drifttime:
		lua_pushinteger(L, botdata->drifttime);
		break;
	case botdata_driftlockout:
		lua_pushinteger(L, botdata->driftlockout);
		break;
	case botdata_driftmaxdist:
		lua_pushinteger(L, botdata->driftmaxdist);
		break;
	case botdata_driftpowerdiv:
		lua_pushinteger(L, botdata->driftpowerdiv);
		break;
	case botdata_driftstatedelay:
		lua_pushinteger(L, botdata->driftstatedelay);
		break;
	case botdata_driftskill:
		lua_pushfixed(L, botdata->driftskill);
		break;
	case botdata_acceldown:
		lua_pushdboolean(L, botdata->acceldown);
		break;
	case botdata_brakedown:
		lua_pushdboolean(L, botdata->brakedown);
		break;
	case botdata_driftdown:
		lua_pushdboolean(L, botdata->driftdown);
		break;
	case botdata_itemdown:
		lua_pushdboolean(L, botdata->itemdown);
		break;
	case botdata_dolookback:
		lua_pushdboolean(L, botdata->dolookback);
		break;
	case botdata_itemwasdown:
		lua_pushdboolean(L, botdata->itemwasdown);
		break;
	case botdata_itemthrow:
		lua_pushinteger(L, botdata->itemthrow);
		break;
	case botdata_turnamt:
		lua_pushinteger(L, botdata->turnamt);
		break;
	default:
		lua_getfield(L, LUA_REGISTRYINDEX, LREG_EXTVARS);
		I_Assert(lua_istable(L, -1));
		lua_pushlightuserdata(L, botdata);
		lua_rawget(L, -2);
		if (!lua_istable(L, -1)) { // no extra values table
			CONS_Debug(DBG_LUA, M_GetText("'%s' has no extvars table or field named '%s'; returning nil.\n"), "botdata_t", lua_tostring(L, 2));
			return 0;
		}
		lua_pushvalue(L, 2); // field name
		lua_gettable(L, -2);
		if (lua_isnil(L, -1)) // no value for this field
			CONS_Debug(DBG_LUA, M_GetText("'%s' has no field named '%s'; returning nil.\n"), "botdata_t", lua_tostring(L, 2));
		break;
	}

	return 1;
}

#define NOSET luaL_error(L, LUA_QL("botdata_t") " field " LUA_QS " should not be set directly.", botdata_opt[field])

static int botdata_set(lua_State *L)
{
	botdata_t *botdata = *((botdata_t **)luaL_checkudata(L, 1, META_BOTDATA));
	enum botdata field = Lua_optoption(L, 2, -1, botdata_fields_ref);

	I_Assert(botdata != NULL);

	switch (field)
	{
	case botdata_valid:
		return NOSET;
	case botdata_itemdelay:
		botdata->itemdelay = luaL_checkinteger(L, 3);
		break;
	case botdata_itemconfirm:
		botdata->itemconfirm = luaL_checkinteger(L, 3);
		break;
	case botdata_turnconfirm:
		botdata->turnconfirm = luaL_checkinteger(L, 3);
		break;
	case botdata_respawnconfirm:
		botdata->respawnconfirm = luaL_checkinteger(L, 3);
		break;
	case botdata_driftstate:
		botdata->driftstate = luaL_checkinteger(L, 3);
		break;
	case botdata_driftturn:
		botdata->driftturn = luaL_checkinteger(L, 3);
		break;
	case botdata_drifttime:
		botdata->drifttime = luaL_checkinteger(L, 3);
		break;
	case botdata_driftlockout:
		botdata->driftlockout = luaL_checkinteger(L, 3);
		break;
	case botdata_driftmaxdist:
		botdata->driftmaxdist = luaL_checkinteger(L, 3);
		break;
	case botdata_driftpowerdiv:
		botdata->driftpowerdiv = luaL_checkinteger(L, 3);
		break;
	case botdata_driftstatedelay:
		botdata->driftstatedelay = luaL_checkinteger(L, 3);
		break;
	case botdata_driftskill:
		botdata->driftskill = luaL_checkfixed(L, 3);
		break;
	case botdata_acceldown:
		botdata->acceldown = luaL_checkdboolean(L, 3);
		break;
	case botdata_brakedown:
		botdata->brakedown = luaL_checkdboolean(L, 3);
		break;
	case botdata_driftdown:
		botdata->driftdown = luaL_checkdboolean(L, 3);
		break;
	case botdata_itemdown:
		botdata->itemdown = luaL_checkdboolean(L, 3);
		break;
	case botdata_dolookback:
		botdata->dolookback = luaL_checkdboolean(L, 3);
		break;
	case botdata_itemwasdown:
		botdata->itemwasdown = luaL_checkdboolean(L, 3);
		break;
	case botdata_itemthrow:
		botdata->itemthrow = luaL_checkinteger(L, 3);
		break;
	case botdata_turnamt:
		botdata->turnamt = luaL_checkinteger(L, 3);
		break;
	default:
		lua_getfield(L, LUA_REGISTRYINDEX, LREG_EXTVARS);
		I_Assert(lua_istable(L, -1));
		lua_pushlightuserdata(L, botdata);
		lua_rawget(L, -2);
		if (lua_isnil(L, -1)) {
			// This index doesn't have a table for extra values yet, let's make one.
			lua_pop(L, 1);
			CONS_Debug(DBG_LUA, M_GetText("'%s' has no field named '%s'; adding it as Lua data.\n"), "botdata_t", lua_tostring(L, 2));
			lua_newtable(L);
			lua_pushlightuserdata(L, botdata);
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

static int lib_iterateBotData(lua_State *L)
{
	INT32 i;

	if (lua_gettop(L) < 2)
	{
		lua_pushcfunction(L, lib_iterateBotData);
		return 1;
	}

	lua_settop(L, 2);
	lua_remove(L, 1); // state is unused.

	if (!lua_isnil(L, 1))
		i = (INT32)(*((botdata_t **)luaL_checkudata(L, 1, META_BOTDATA)) - K_GetBotData(0)) + 1;
	else
		i = 0;

	if (i < MAXPLAYERS)
	{
		LUA_PushUserdata(L, K_GetBotData(i), META_BOTDATA);
		return 1;
	}

	return 0;
}

static int lib_getBotData(lua_State *L)
{
	INT32 i;

	if (lua_type(L, 2) == LUA_TNUMBER)
	{
		i = luaL_checkinteger(L, 2);
		if (i < 0 || i >= MAXPLAYERS)
			return luaL_error(L, "botdata[] index %d out of range (0 - %d)", i, MAXPLAYERS-1);
		LUA_PushUserdata(L, K_GetBotData(i), META_BOTDATA);
		return 1;
	}

	if (fastcmp(luaL_checkstring(L, 2), "iterate"))
	{
		lua_pushcfunction(L, lib_iterateBotData);
		return 1;
	}

	return 0;
}

static int lib_numBotData(lua_State *L)
{
	lua_pushinteger(L, MAXPLAYERS);
	return 1;
}

int LUA_BotVarsLib(lua_State *L)
{
	botvars_fields_ref = Lua_CreateFieldTable(L, botvars_opt);
	botdata_fields_ref = Lua_CreateFieldTable(L, botdata_opt);

	luaL_newmetatable(L, META_BOTVARS);
		lua_pushcfunction(L, botvars_get);
		lua_setfield(L, -2, "__index");

		lua_pushcfunction(L, botvars_set);
		lua_setfield(L, -2, "__newindex");
	lua_pop(L,1);

	luaL_newmetatable(L, META_BOTDATA);
		lua_pushcfunction(L, botdata_get);
		lua_setfield(L, -2, "__index");

		lua_pushcfunction(L, botdata_set);
		lua_setfield(L, -2, "__newindex");
	lua_pop(L,1);

	lua_newuserdata(L, 0);
		lua_createtable(L, 0, 2);
			lua_pushcfunction(L, lib_getBotData);
			lua_setfield(L, -2, "__index");

			lua_pushcfunction(L, lib_numBotData);
			lua_setfield(L, -2, "__len");
		lua_setmetatable(L, -2);
	lua_setglobal(L, "botdata");

	return 0;
}
