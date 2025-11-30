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
		lua_pushstring(L, voice->name);
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

	lua_pushinteger(L, voice->id);
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

	return 0;
}
