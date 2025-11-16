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
#include "lua_hud.h" // hud_running errors
#include "lua_hook.h" // hook_cmd_running errors

enum voicevars
{
	voicevars_id = 0,
	voicevars_name, // Not actually in kartvoice_t; returns the name of the voice
	voicevars_parent,
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
#define NOSET luaL_error(L, LUA_QL("kartvoice_t") " field " LUA_QS " should not be set directly.", field)

#define UNIMPLEMENTED luaL_error(L, LUA_QL("kartvoice_t") " field " LUA_QS " is not implemented for Lua and cannot be accessed.", voicevars_opt[field])

static int voice_get(lua_State* L)
{
	kartvoice_t* voice = *((kartvoice_t**)luaL_checkudata(L, 1, META_VOICE));
	enum voicevars field = luaL_checkoption(L, 2, NULL, voicevars_opt);

	// voices are always valid, only added, never removed
	I_Assert(voice != NULL);

	switch (field)
	{
	case voicevars_id:
		return RNOGET;
	case voicevars_name:
		lua_pushstring(L, DEH_KartVoiceName((kartvoicetype_e)(voice - skinvoices)));
		break;
	case voicevars_parent:
		LUA_PushUserdata(L, &skins[voice->parent], META_SKIN);
		break;
	case voicevars_win:
		LUA_PushUserdata(L, &S_sfx[voice->win], META_SFXINFO);
		break;
	case voicevars_lose:
		LUA_PushUserdata(L, &S_sfx[voice->lose], META_SFXINFO);
		break;
	case voicevars_pain:
		LUA_PushUserdata(L, voice->pain, META_VOICE_ARRAY);
		break;
	case voicevars_attack:
		LUA_PushUserdata(L, voice->attack, META_VOICE_ARRAY);
		break;
	case voicevars_boost:
		LUA_PushUserdata(L, voice->boost, META_VOICE_ARRAY);
		break;
	case voicevars_overtake:
		LUA_PushUserdata(L, &S_sfx[voice->overtake], META_SFXINFO);
		break;
	case voicevars_hitem:
		LUA_PushUserdata(L, &S_sfx[voice->hitem], META_SFXINFO);
		break;
	case voicevars_power:
		LUA_PushUserdata(L, &S_sfx[voice->power], META_SFXINFO);
		break;
	default:
		return RNOFIELD;
	}

	return 1;
}

static int voice_set(lua_State* L)
{
	kartvoice_t* voice = *((kartvoice_t**)luaL_checkudata(L, 1, META_VOICE));
	enum voicevars field = luaL_checkoption(L, 2, voicevars_opt[0], voicevars_opt);

	// voices are always valid, only added, never removed
	I_Assert(voice != NULL);

	if (hud_running)
		return luaL_error(L, "Do not alter kartvoice_t in HUD rendering code!");
	if (hook_cmd_running)
		return luaL_error(L, "Do not alter kartvoice_t in CMD building code!");

	lua_Integer i, k;

	i = 0;
	k = sfx_thok;

	switch (field)
	{
	case voicevars_id:
		return RNOSET;
	case voicevars_name:
		return RNOSET;
	case voicevars_parent:
	{
		// It would make no sense not to accept a skin struct here.
		skin_t* my_new_parent = *((skin_t**)luaL_checkudata(L, 3, META_SKIN));

		if (!my_new_parent)
		{
			luaL_error(L, "skin does not exist.");
		}
		else
		{
			voice->parent = (UINT16)(my_new_parent - skins);
		}
		break;
	}
	case voicevars_win:
		// For the actual sound values, you pass the actual sfxenum_t enums.
		// Yes, I know what I said about skin structs literally just above.
		voice->win = (sfxenum_t)luaL_checkinteger(L, 3);
		break;
	case voicevars_lose:
		voice->lose = (sfxenum_t)luaL_checkinteger(L, 3);
		break;
	case voicevars_pain:
		return NOSET;
	case voicevars_attack:
		return NOSET;
	case voicevars_boost:
		return NOSET;
	case voicevars_overtake:
		voice->overtake = (sfxenum_t)luaL_checkinteger(L, 3);
		break;
	case voicevars_hitem:
		voice->hitem = (sfxenum_t)luaL_checkinteger(L, 3);
		break;
	case voicevars_power:
		voice->power = (sfxenum_t)luaL_checkinteger(L, 3);
		break;
	default:
		return RNOFIELD;
	}

	return 0;
}

#undef RNOSET
#undef RNOFIELD
#undef UNIMPLEMENTED

static int voice_num(lua_State* L)
{
	kartvoice_t* voice = *((kartvoice_t**)luaL_checkudata(L, 1, META_VOICE));

	// voices are always valid, only added, never removed
	I_Assert(voice != NULL);

	lua_pushinteger(L, voice - skinvoices);
	return 1;
}

// voice_array, i -> voice.array[i]
static int voice_array_get(lua_State *L)
{
	sfxenum_t *voice_array = *((sfxenum_t **)luaL_checkudata(L, 1, META_VOICE_ARRAY));
	INT32 i = luaL_checkinteger(L, 2);

	if (i < 0 || i >= VOICEARRAYSIZE)
		return luaL_error(L, "index %d out of range (0 - %d)", i, VOICEARRAYSIZE);

	LUA_PushUserdata(L, &S_sfx[voice_array[i]], META_SFXINFO);
	return 1;
}

static int voice_array_set(lua_State *L)
{
	sfxenum_t *voice_array = *((sfxenum_t **)luaL_checkudata(L, 1, META_VOICE_ARRAY));
	INT32 i = luaL_checkinteger(L, 2);

	if (i < 0 || i >= VOICEARRAYSIZE)
		return luaL_error(L, "index %d out of range (0 - %d)", i, VOICEARRAYSIZE);

	voice_array[i] = (sfxenum_t)luaL_checkinteger(L, 3);
	return 0;
}

// #voice.array -> VOICEARRAYSIZE
static int voice_array_num(lua_State *L)
{
	lua_pushinteger(L, VOICEARRAYSIZE);
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

	luaL_newmetatable(L, META_VOICE_ARRAY);
	lua_pushcfunction(L, voice_array_get);
	lua_setfield(L, -2, "__index");

	lua_pushcfunction(L, voice_array_set);
	lua_setfield(L, -2, "__newindex");

	lua_pushcfunction(L, voice_array_num);
	lua_setfield(L, -2, "__len");
	lua_pop(L,1);


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
