// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2025 by "yama".
// Copyright (C) 2025 by BlanKart Team.
// Copyright (C) 2014-2025 by Sonic Team Junior.
// Copyright (C) 2016 by John "JTE" Muniz.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  lua_waypointslib.c
/// \brief wapoint structure library for Lua scripting

#include "doomdef.h"
#include "fastcmp.h"
#include "r_skins.h"
#include "sounds.h"
#include "deh_tables.h"

#include "lua_script.h"
#include "lua_libs.h"

enum voicevars
{
	voicevars_id = 0,
	voicevars_name, // Not actually in kartvoice_t; returns the name of the voice
	voicevars_parent,
	voicevars_parentname,  // Not actually in kartvoice_t; returns the name of the parent skin
	voicevars_win,
	voicevars_lose,
	voicevars_pain,
	voicevars_attack,
	voicevars_boost,
	voicevars_overtake,
	voicevars_hitem,
	voicevars_power
};

static const char *const voicevars_opt[] = {
	"id",
	"name",
	"parent",
	"parentname",
	"win",
	"lose",
	"pain",
	"attack",
	"boost",
	"overtake",
	"hitem",
	"power",
	NULL
};

#define RNOFIELD luaL_error(L, LUA_QL("kartvoice_t") " has no field named " LUA_QS, field)
#define RNOSET luaL_error(L, LUA_QL("kartvoice_t") " field " LUA_QS " cannot be set.", field)
#define RNOGET luaL_error(L, LUA_QL("kartvoice_t") " field " LUA_QS " cannot be get.", field)

static int voice_get(lua_State* L)
{
	kartvoice_t* voice = *((kartvoice_t**)luaL_checkudata(L, 1, META_VOICE));
	enum voicevars field = luaL_checkoption(L, 2, NULL, voicevars_opt);

	// voices are always valid, only added, never removed
	I_Assert(voice != NULL);

	switch (field)
	{
	case voicevars_id:
		lua_pushinteger(L, voice->id);
		break;
	case voicevars_name:
		lua_pushstring(L, DEH_VoiceName((kartvoicetype_e)(voice - skinvoices)));
		break;
	case voicevars_parent:
		lua_pushinteger(L, voice->parent);
		break;
	case voicevars_parentname:
		lua_pushstring(L, skins[voice->parent].name);
		break;
	case voicevars_win:
		lua_pushinteger(L, voice->win);
		break;
	case voicevars_lose:
		lua_pushinteger(L, voice->lose);
		break;
	case voicevars_pain:
		// Wasteful and shitty, I'll wager, but it's better than risking segfaults
		const size_t painarraysize = (sizeof voice->pain) / (sizeof voice->pain[0]);

		lua_createtable(L, painarraysize, 0);
		for (size_t i = 0; i < painarraysize; i++)
		{
			lua_pushinteger(L, voice->pain[i]);
			lua_rawseti(L, -2, 1 + i);
		}
		break;
	case voicevars_attack:
		const size_t atkarraysize = (sizeof voice->attack) / (sizeof voice->attack[0]);

		lua_createtable(L, atkarraysize, 0);
		for (size_t i = 0; i < atkarraysize; i++)
		{
			lua_pushinteger(L, voice->attack[i]);
			lua_rawseti(L, -2, 1 + i);
		}
		break;
	case voicevars_boost:
		const size_t bstarraysize = (sizeof voice->boost) / (sizeof voice->boost[0]);

		lua_createtable(L, bstarraysize, 0);
		for (size_t i = 0; i < bstarraysize; i++)
		{
			lua_pushinteger(L, voice->boost[i]);
			lua_rawseti(L, -2, 1 + i);
		}
		break;
	case voicevars_overtake:
		lua_pushinteger(L, voice->overtake);
		break;
	case voicevars_hitem:
		lua_pushinteger(L, voice->hitem);
		break;
	case voicevars_power:
		lua_pushinteger(L, voice->power);
		break;
	default:
		return RNOFIELD;
	}

	return 1;
}

static int voice_set(lua_State* L)
{
	// ...let's just operate by skin rules. So much for random-line voice dubs...
	return luaL_error(L, LUA_QL("kartvoice_t") " struct cannot be edited by Lua.");
}

#undef RNOSET
#undef RNOFIELD

static int voice_num(lua_State* L)
{
	kartvoice_t* voice = *((kartvoice_t**)luaL_checkudata(L, 1, META_VOICE));

	// voices are always valid, only added, never removed
	I_Assert(voice != NULL);

	lua_pushinteger(L, voice - skinvoices);
	return 1;
}

static int lib_iterateVoices(lua_State* L)
{
	INT32 i;

	if (lua_gettop(L) < 2)
	{
		lua_pushcfunction(L, lib_iterateVoices);
		return 1;
	}

	lua_settop(L, 2);
	lua_remove(L, 1);  // state is unused.

	if (!lua_isnil(L, 1))
		i = (INT32)(*((kartvoice_t**)luaL_checkudata(L, 1, META_VOICE)) - skinvoices) + 1;
	else
		i = 0;

	// voices are always valid, only added, never removed
	if (i < numskinvoices)
	{
		LUA_PushUserdata(L, &skinvoices[i], META_VOICE);
		return 1;
	}

	return 0;
}

static int lib_getVoice(lua_State* L)
{
	const char* field;
	INT32 i;

	// find voice by number
	if (lua_type(L, 2) == LUA_TNUMBER)
	{
		i = luaL_checkinteger(L, 2);
		if (i < 0 || i >= MAXSKINVOICES)
		{
			return luaL_error(
			    L, "skinvoices[] index %d out of range (0 - %d)", i, MAXSKINVOICES - 1);
		}
		else if (i >= numskinvoices)
		{
			return 0;
		}

		LUA_PushUserdata(L, &skinvoices[i], META_VOICE);
		return 1;
	}

	field = luaL_checkstring(L, 2);

	// special function iterate
	if (fastcmp(field, "iterate"))
	{
		lua_pushcfunction(L, lib_iterateVoices);
		return 1;
	}

	// find voice by name
	// Glad I made a function for this...
	i = R_FindIDForVoice(field);

	if (i != MAXSKINVOICES)
	{
		// Found a voice.
		LUA_PushUserdata(L, &skinvoices[i], META_VOICE);
		return 1;
	}

	return 0;
}

// In this case: returns TOTAL voices, and not individual table counts.
static int lib_numVoices(lua_State* L)
{
	lua_pushinteger(L, numskinvoices);
	return 1;
}

int LUA_VoiceLib(lua_State* L)
{
	luaL_newmetatable(L, META_VOICE);
	lua_pushcfunction(L, voice_get);
	lua_setfield(L, -2, "__index");

	lua_pushcfunction(L, voice_set);
	lua_setfield(L, -2, "__newindex");

	lua_pushcfunction(L, voice_num);
	lua_setfield(L, -2, "__len");
	lua_pop(L, 1);

	lua_newuserdata(L, 0);
	lua_createtable(L, 0, 2);
	lua_pushcfunction(L, lib_getVoice);
	lua_setfield(L, -2, "__index");

	lua_pushcfunction(L, lib_numVoices);
	lua_setfield(L, -2, "__len");
	lua_setmetatable(L, -2);
	lua_setglobal(L, "skinvoices");

	return 0;
}