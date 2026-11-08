// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2026 by BlanKart Team.
// Copyright (C) 2014-2025 by Sonic Team Junior.
// Copyright (C) 2016 by John "JTE" Muniz.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  lua_itemlib.c
/// \brief item structures library for Lua scripting

#include "doomdef.h"
#include "k_items.h"

#include "lua_script.h"
#include "lua_libs.h"
#include "lua_hud.h" // hud_running errors
#include "lua_hook.h" // hook_cmd_running errors

// itemvars_ fields
#define ITEMVARSFIELDS(X) \
	X(equipstyle) \
	X(equipstylealt) \
	X(flags) \
	X(flagsalt) \
	/*X(graphics) see K_GetItemGraphics*/ \
	X(altcvar) \
	X(altenabled)

enum itemvars
{
#define ENUMITEM(name) itemvars_##name,
	ITEMVARSFIELDS(ENUMITEM)
	NUMITEMVARSFIELDS,
};

static const char *const itemvars_opt[] = {
#define ENUMITEMNAME(name) #name,
	ITEMVARSFIELDS(ENUMITEMNAME)
	NULL,
};
#undef ENUMITEM
#undef ENUMITEMNAME

int itemvars_fields_ref = LUA_NOREF;

static int item_get(lua_State *L)
{
	kartitem_t *item = *((kartitem_t**)luaL_checkudata(L, 1, META_KARTITEM));
	enum itemvars field = Lua_optoption(L, 2, -1, itemvars_fields_ref);

	// items are always valid, only added, never removed
	I_Assert(item != NULL);

	switch (field)
	{
	case itemvars_equipstyle:
		lua_pushinteger(L, item->equipstyle[0]);
		break;
	case itemvars_equipstylealt:
		lua_pushinteger(L, item->equipstyle[1]);
		break;
	case itemvars_flags:
		lua_pushinteger(L, item->flags[0]);
		break;
	case itemvars_flagsalt:
		lua_pushinteger(L, item->flags[1]);
		break;
	case itemvars_altcvar:
		LUA_PushUserdata(L, item->altcvar, META_CVAR);
		break;
	case itemvars_altenabled:
		lua_pushdboolean(L, item->altenabled);
		break;
	default:
		return luaL_error(L, LUA_QL("kartitem_t") " has no field named " LUA_QS, itemvars_opt[field]);
	}

	return 1;
}

static int item_set(lua_State *L)
{
	return luaL_error(L, LUA_QL("kartitem_t") " struct cannot be edited by Lua.");
}

static int item_num(lua_State *L)
{
	kartitem_t *item = *((kartitem_t**)luaL_checkudata(L, 1, META_KARTITEM));

	// items are always valid, only added, never removed
	I_Assert(item != NULL);

	lua_pushinteger(L, item - kartitems);
	return 1;
}

// resultvars_ fields
#define RESULTVARSFIELDS(X) \
	X(type) \
	X(amount) \
	X(displayname) \
	X(isalt) \
	X(flags) \
	/*X(odds)*/ \
	/*X(uniqueodds)*/ \
	X(basecooldown) \
	X(cooldown)

enum resultvars
{
#define ENUMITEM(name) resultvars_##name,
	RESULTVARSFIELDS(ENUMITEM)
	NUMRESULTVARSFIELDS,
};

static const char *const resultvars_opt[] = {
#define ENUMITEMNAME(name) #name,
	RESULTVARSFIELDS(ENUMITEMNAME)
	NULL,
};
#undef ENUMITEM
#undef ENUMITEMNAME

int resultvars_fields_ref = LUA_NOREF;

static int result_get(lua_State *L)
{
	kartresult_t *result = *((kartresult_t**)luaL_checkudata(L, 1, META_KARTRESULT));
	enum resultvars field = Lua_optoption(L, 2, -1, resultvars_fields_ref);

	// results are always valid, only added, never removed
	I_Assert(result != NULL);

	switch (field)
	{
	case resultvars_type:
		lua_pushinteger(L, result->type);
		break;
	case resultvars_amount:
		lua_pushinteger(L, result->amount);
		break;
	case resultvars_displayname:
		lua_pushstring(L, result->displayname);
		break;
	case resultvars_isalt:
		lua_pushdboolean(L, result->isalt);
		break;
	case resultvars_flags:
		lua_pushinteger(L, result->flags);
		break;
	//case resultvars_odds:
		//i don't have time for this shit
		//break;
	//case resultvars_uniqueodds:
		//break;
	case resultvars_basecooldown:
		lua_pushinteger(L, result->basecooldown);
		break;
	case resultvars_cooldown:
		lua_pushinteger(L, result->cooldown);
		break;
	default:
		return luaL_error(L, LUA_QL("kartresult_t") " has no field named " LUA_QS, resultvars_opt[field]);
	}

	return 1;
}

static int result_set(lua_State *L)
{
	kartresult_t *result = *((kartresult_t**)luaL_checkudata(L, 1, META_KARTRESULT));
	enum resultvars field = Lua_optoption(L, 2, -1, resultvars_fields_ref);

	if (hud_running)
		return luaL_error(L, "Do not alter kartresult_t in HUD rendering code!");
	if (hook_cmd_running)
		return luaL_error(L, "Do not alter kartresult_t in BuildCMD code!");

	// results are always valid, only added, never removed
	I_Assert(result != NULL);

	switch (field)
	{
	case resultvars_type:
	case resultvars_amount:
	case resultvars_displayname:
	case resultvars_isalt:
	case resultvars_flags:
	case resultvars_basecooldown:
		return luaL_error(L, LUA_QL("kartresult_t") " field " LUA_QS " should not be set directly.", resultvars_opt[field]);
	case resultvars_cooldown:
		result->cooldown = luaL_checkinteger(L, 3);
		break;
	default:
		return luaL_error(L, LUA_QL("kartresult_t") " has no field named " LUA_QS, resultvars_opt[field]);
	}

	return 0;
}

static int result_num(lua_State *L)
{
	kartresult_t *result = *((kartresult_t**)luaL_checkudata(L, 1, META_KARTRESULT));

	// results are always valid, only added, never removed
	I_Assert(result != NULL);

	lua_pushinteger(L, result - kartresults);
	return 1;
}

static int graphics_get(lua_State *L)
{
	kartitemgraphics_t *graphics = *((kartitemgraphics_t**)luaL_checkudata(L, 1, META_KARTITEMGRAPHICS));
	int32_t index = luaL_checkinteger(L, 2);

	if (index < 0 || index >= graphics->numpatches)
		return luaL_error(L, "patch index %d out of range (0 - %d)", index, graphics->numpatches-1);

	LUA_PushUserdata(L, graphics->patches[index], META_PATCH);
	return 1;
}

static int graphics_set(lua_State *L)
{
	return luaL_error(L, LUA_QL("kartitemgraphics_t") " struct cannot be edited by Lua.");
}

static int graphics_num(lua_State *L)
{
	kartitemgraphics_t *graphics = *((kartitemgraphics_t**)luaL_checkudata(L, 1, META_KARTITEMGRAPHICS));
	lua_pushinteger(L, graphics->numpatches);
	return 1;
}

static int lib_iterateKartItems(lua_State *L)
{
	int32_t i;

	if (lua_gettop(L) < 2)
	{
		lua_pushcfunction(L, lib_iterateKartItems);
		return 1;
	}

	lua_settop(L, 2);
	lua_remove(L, 1); // state is unused.

	if (!lua_isnil(L, 1))
		i = (*(kartitem_t **)luaL_checkudata(L, 1, META_KARTITEM)) - kartitems + 1;
	else
		i = 1;

	if (i >= 1 && i < numkartitems)
	{
		LUA_PushUserdata(L, &kartitems[i], META_KARTITEM);
		return 1;
	}

	return 0;
}

static int lib_iterateKartResults(lua_State *L)
{
	int32_t i;

	if (lua_gettop(L) < 2)
	{
		lua_pushcfunction(L, lib_iterateKartResults);
		return 1;
	}

	lua_settop(L, 2);
	lua_remove(L, 1); // state is unused.

	if (!lua_isnil(L, 1))
		i = (*(kartresult_t **)luaL_checkudata(L, 1, META_KARTRESULT)) - kartresults + 1;
	else
		i = 0;

	if (i >= 0 && i < numkartresults)
	{
		LUA_PushUserdata(L, &kartresults[i], META_KARTRESULT);
		return 1;
	}

	return 0;
}

static int lib_getKartItem(lua_State *L)
{
	if (lua_type(L, 2) == LUA_TNUMBER)
	{
		int32_t i = luaL_checkinteger(L, 2);
		if (i < 1 || i >= numkartitems)
			return luaL_error(L, "kartitem index %d out of range (1 - %d)", i, numkartitems-1);
		LUA_PushUserdata(L, &kartitems[i], META_KARTITEM);
		return 1;
	}

	const char *field = luaL_checkstring(L, 2);
	if (fastcmp(field, "iterate"))
	{
		lua_pushcfunction(L, lib_iterateKartItems);
		return 1;
	}
	return 0;
}

static int lib_numKartItems(lua_State *L)
{
	lua_pushinteger(L, numkartitems);
	return 1;
}

static int lib_getKartResult(lua_State *L)
{
	if (lua_type(L, 2) == LUA_TNUMBER)
	{
		int32_t i = luaL_checkinteger(L, 2);
		if (i < 0 || i >= numkartresults)
			return luaL_error(L, "kartresult index %d out of range (0 - %d)", i, numkartresults-1);
		LUA_PushUserdata(L, &kartresults[i], META_KARTRESULT);
		return 1;
	}

	const char *field = luaL_checkstring(L, 2);
	if (fastcmp(field, "iterate"))
	{
		lua_pushcfunction(L, lib_iterateKartResults);
		return 1;
	}
	return 0;
}

static int lib_numKartResults(lua_State *L)
{
	lua_pushinteger(L, numkartresults);
	return 1;
}


int LUA_ItemLib(lua_State *L)
{
	itemvars_fields_ref = Lua_CreateFieldTable(L, itemvars_opt);
	resultvars_fields_ref = Lua_CreateFieldTable(L, resultvars_opt);

	luaL_newmetatable(L, META_KARTITEM);
		lua_pushcfunction(L, item_get);
		lua_setfield(L, -2, "__index");
		lua_pushcfunction(L, item_set);
		lua_setfield(L, -2, "__newindex");
		lua_pushcfunction(L, item_num);
		lua_setfield(L, -2, "__len");
	lua_pop(L, 1);

	luaL_newmetatable(L, META_KARTRESULT);
		lua_pushcfunction(L, result_get);
		lua_setfield(L, -2, "__index");
		lua_pushcfunction(L, result_set);
		lua_setfield(L, -2, "__newindex");
		lua_pushcfunction(L, result_num);
		lua_setfield(L, -2, "__len");
	lua_pop(L, 1);

	luaL_newmetatable(L, META_KARTITEMGRAPHICS);
		lua_pushcfunction(L, graphics_get);
		lua_setfield(L, -2, "__index");
		lua_pushcfunction(L, graphics_set);
		lua_setfield(L, -2, "__newindex");
		lua_pushcfunction(L, graphics_num);
		lua_setfield(L, -2, "__len");
	lua_pop(L, 1);

	lua_newuserdata(L, 0);
		lua_createtable(L, 0, 2);
			lua_pushcfunction(L, lib_getKartItem);
			lua_setfield(L, -2, "__index");
			lua_pushcfunction(L, lib_numKartItems);
			lua_setfield(L, -2, "__len");
		lua_setmetatable(L, -2);
	lua_setglobal(L, "kartitems");

	lua_newuserdata(L, 0);
		lua_createtable(L, 0, 2);
			lua_pushcfunction(L, lib_getKartResult);
			lua_setfield(L, -2, "__index");
			lua_pushcfunction(L, lib_numKartResults);
			lua_setfield(L, -2, "__len");
		lua_setmetatable(L, -2);
	lua_setglobal(L, "kartresults");

	return 0;
}
