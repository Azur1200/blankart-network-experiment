// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2012-2016 by John "JTE" Muniz.
// Copyright (C) 2012-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  lua_infolib.c
/// \brief infotable editing library for Lua scripting

#include "doomdef.h"
#include "info.h"
#include "dehacked.h"
#include "deh_tables.h"
#include "deh_lua.h"
#include "p_mobj.h"
#include "p_local.h"
#include "z_zone.h"
#include "r_patch.h"
#include "r_picformats.h"
#include "r_things.h"
#include "r_draw.h" // R_GetColorByName
#include "doomstat.h" // luabanks[]

#include "lua_script.h"
#include "lua_libs.h"
#include "lua_hud.h" // hud_running errors
#include "lua_hook.h" // hook_cmd_running errors

extern CV_PossibleValue_t Color_cons_t[];
extern uint8_t skincolor_modified[];

dboolean LUA_CallAction(enum actionnum actionnum, mobj_t *actor);
state_t *astate = NULL;

// sfxinfor_ fields
#define SFXINFORFIELDS(X) \
	X(name) \
	X(singular) \
	X(priority) \
	X(flags) \
	X(volume) \
	X(caption) \
	X(skinsound) \
	X(string)

enum sfxinfo_read
{
#define ENUMITEM(name) sfxinfor_##name,
	SFXINFORFIELDS(ENUMITEM)
};

static const char *const sfxinfo_ropt[] = {
#define ENUMITEMNAME(name) #name,
	SFXINFORFIELDS(ENUMITEMNAME)
	NULL,
};
#undef ENUMITEM
#undef ENUMITEMNAME

// sfxinfow_ fields
#define SFXINFO_WRITEFIELDS(X) \
	X(singular) \
	X(priority) \
	X(flags) \
	X(volume) \
	X(caption)

enum sfxinfo_write
{
#define ENUMITEM(name) sfxinfow_##name,
	SFXINFO_WRITEFIELDS(ENUMITEM)
};

static const char *const sfxinfo_wopt[] = {
#define ENUMITEMNAME(name) #name,
	SFXINFO_WRITEFIELDS(ENUMITEMNAME)
	NULL,
};
#undef ENUMITEM
#undef ENUMITEMNAME

dboolean actionsoverridden[NUMACTIONS] = {false};

//
// Sprite Names
//

// push sprite name
static int lib_getSprname(lua_State *L)
{
	uint32_t i;

	lua_remove(L, 1); // don't care about sprnames[] dummy userdata.

	if (lua_isnumber(L, 1))
	{
		i = lua_tonumber(L, 1);
		if (i > NUMSPRITES)
			return 0;
		lua_pushlstring(L, sprnames[i], 4);
		return 1;
	}
	else if (lua_isstring(L, 1))
	{
		const char *name = lua_tostring(L, 1);
		for (i = 0; i < NUMSPRITES; i++)
			if (fastcmp(name, sprnames[i]))
			{
				lua_pushinteger(L, i);
				return 1;
			}
	}
	return 0;
}

/// \todo Maybe make it tally up the used_spr from dehacked?
static int lib_sprnamelen(lua_State *L)
{
	lua_pushinteger(L, NUMSPRITES);
	return 1;
}

//
// Player Sprite Names
//

// push sprite name
static int lib_getSpr2name(lua_State *L)
{
	playersprite_t i;

	lua_remove(L, 1); // don't care about spr2names[] dummy userdata.

	if (lua_isnumber(L, 1))
	{
		i = lua_tonumber(L, 1);
		if (i >= free_spr2)
			return 0;
		lua_pushlstring(L, spr2names[i], 4);
		return 1;
	}
	else if (lua_isstring(L, 1))
	{
		const char *name = lua_tostring(L, 1);
		for (i = 0; i < free_spr2; i++)
			if (fastcmp(name, spr2names[i]))
			{
				lua_pushinteger(L, i);
				return 1;
			}
	}
	return 0;
}

static int lib_getSpr2default(lua_State *L)
{
	playersprite_t i;

	lua_remove(L, 1); // don't care about spr2defaults[] dummy userdata.

	if (lua_isnumber(L, 1))
		i = lua_tonumber(L, 1);
	else if (lua_isstring(L, 1))
	{
		const char *name = lua_tostring(L, 1);
		for (i = 0; i < free_spr2; i++)
			if (fastcmp(name, spr2names[i]))
				break;
	}
	else
		return luaL_error(L, "spr2defaults[] invalid index");

	if (i >= free_spr2)
		return luaL_error(L, "spr2defaults[] index %d out of range (%d - %d)", i, 0, free_spr2-1);

	lua_pushinteger(L, spr2defaults[i]);
	return 1;
}

static int lib_setSpr2default(lua_State *L)
{
	playersprite_t i;
	uint8_t j = 0;

	if (hud_running)
		return luaL_error(L, "Do not alter spr2defaults[] in HUD rendering code!");
	if (hook_cmd_running)
		return luaL_error(L, "Do not alter spr2defaults[] in CMD building code!");

// todo: maybe allow setting below first freeslot..? step 1 is toggling this, step 2 is testing to see whether it's net-safe
#ifdef SETALLSPR2DEFAULTS
#define FIRSTMODIFY 0
#else
#define FIRSTMODIFY SPR2_FIRSTFREESLOT
	if (free_spr2 == SPR2_FIRSTFREESLOT)
		return luaL_error(L, "You can only modify the spr2defaults[] entries of sprite2 freeslots, and none are currently added.");
#endif

	lua_remove(L, 1); // don't care about spr2defaults[] dummy userdata.

	if (lua_isnumber(L, 1))
		i = lua_tonumber(L, 1);
	else if (lua_isstring(L, 1))
	{
		const char *name = lua_tostring(L, 1);
		for (i = 0; i < free_spr2; i++)
		{
			if (fastcmp(name, spr2names[i]))
				break;
		}
		if (i == free_spr2)
			return luaL_error(L, "spr2defaults[] invalid index");
	}
	else
		return luaL_error(L, "spr2defaults[] invalid index");

	if (i < FIRSTMODIFY || i >= free_spr2)
		return luaL_error(L, "spr2defaults[] index %d out of range (%d - %d)", i, FIRSTMODIFY, free_spr2-1);
#undef FIRSTMODIFY

	if (lua_isnumber(L, 2))
		j = lua_tonumber(L, 2);
	else if (lua_isstring(L, 2))
	{
		const char *name = lua_tostring(L, 2);
		for (j = 0; j < free_spr2; j++)
		{
			if (fastcmp(name, spr2names[j]))
				break;
		}
		if (j == free_spr2)
			return luaL_error(L, "spr2defaults[] invalid set");
	}
	else
		return luaL_error(L, "spr2defaults[] invalid set");

	if (j >= free_spr2)
		return luaL_error(L, "spr2defaults[] set %d out of range (%d - %d)", j, 0, free_spr2-1);

	spr2defaults[i] = j;
	return 0;
}

static int lib_spr2namelen(lua_State *L)
{
	lua_pushinteger(L, free_spr2);
	return 1;
}

/////////////////
// SPRITE INFO //
/////////////////

struct PivotFrame {
	spriteinfo_t *sprinfo;
	uint8_t frame;
};

static uint8_t GetPivotFrame(lua_State *L, int idx)
{
	const char *field = luaL_checkstring(L, idx);
	uint8_t frame;

	if (fastcmp("default", field))
	{
		frame = SPRINFO_DEFAULT_PIVOT;
	}
	else
	{
		frame = R_Char2Frame(field[0], false);
		if (frame == 255)
			luaL_error(L, "invalid frame %s", field);
	}

	return frame;
}

static int PushCast(lua_State *L, const char *meta)
{
	// bypass LUA_PushUserdata
	void **userdata = lua_newuserdata(L, sizeof(void *));

	*userdata = lua_touserdata(L, 1);

	luaL_getmetatable(L, meta);
	lua_setmetatable(L, -2);

	// stack is left with the userdata on top, as if getting it had originally succeeded.
	return 1;
}

// spriteinfo[]
static int lib_getSpriteInfo(lua_State *L)
{
	uint32_t i = NUMSPRITES;
	lua_remove(L, 1);

	if (lua_isstring(L, 1))
	{
		const char *name = lua_tostring(L, 1);
		int32_t spr;
		for (spr = 0; spr < NUMSPRITES; spr++)
		{
			if (fastcmp(name, sprnames[spr]))
			{
				i = spr;
				break;
			}
		}
		if (i == NUMSPRITES)
		{
			char *check;
			i = strtol(name, &check, 10);
			if (check == name || *check != '\0')
				return luaL_error(L, "unknown sprite name %s", name);
		}
	}
	else
		i = luaL_checkinteger(L, 1);

	if (i == 0 || i >= NUMSPRITES)
		return luaL_error(L, "spriteinfo[] index %d out of range (1 - %d)", i, NUMSPRITES-1);

	LUA_PushUserdata(L, &spriteinfo[i], META_SPRITEINFO);
	return 1;
}

#define FIELDERROR(f, e) luaL_error(L, "bad value for " LUA_QL(f) " in table passed to spriteinfo[] (%s)", e);
#define TYPEERROR(f, t1, t2) FIELDERROR(f, va("%s expected, got %s", lua_typename(L, t1), lua_typename(L, t2)))

static int PopPivotSubTable(spriteframepivot_t *pivot, lua_State *L, int stk, int idx)
{
	int okcool = 0;
	switch (lua_type(L, stk))
	{
		case LUA_TTABLE:
			lua_pushnil(L);
			while (lua_next(L, stk))
			{
				const char *key = NULL;
				lua_Integer ikey = -1;
				lua_Integer value = 0;
				// x or y?
				switch (lua_type(L, stk+1))
				{
					case LUA_TSTRING:
						key = lua_tostring(L, stk+1);
						break;
					case LUA_TNUMBER:
						ikey = lua_tointeger(L, stk+1);
						break;
					default:
						FIELDERROR("pivot key", va("string or number expected, got %s", luaL_typename(L, stk+1)))
				}
				// then get value
				switch (lua_type(L, stk+2))
				{
					case LUA_TNUMBER:
						value = lua_tonumber(L, stk+2);
						break;
					case LUA_TBOOLEAN:
						value = (uint8_t)lua_todboolean(L, stk+2);
						break;
					default:
						TYPEERROR("pivot value", LUA_TNUMBER, lua_type(L, stk+2))
				}
				// finally set omg!!!!!!!!!!!!!!!!!!
				if (ikey == 1 || (key && fastcmp(key, "x")))
					pivot[idx].x = (int32_t)value;
				else if (ikey == 2 || (key && fastcmp(key, "y")))
					pivot[idx].y = (int32_t)value;
				else if (ikey == -1 && (key != NULL))
					FIELDERROR("pivot key", va("invalid option %s", key));
				okcool = 1;
				lua_pop(L, 1);
			}
			break;
		default:
			TYPEERROR("sprite pivot", LUA_TTABLE, lua_type(L, stk))
	}
	return okcool;
}

static int PopPivotTable(spriteinfo_t *info, lua_State *L, int stk)
{
	// Just in case?
	if (!lua_istable(L, stk))
		TYPEERROR("pivot table", LUA_TTABLE, lua_type(L, stk));

	lua_pushnil(L);
	// stk = 0 has the pivot table
	// stk = 1 has the frame key
	// stk = 2 has the frame table
	// stk = 3 has either a string or a number as key
	// stk = 4 has the value for the key mentioned above
	while (lua_next(L, stk))
	{
		int idx = 0;
		switch (lua_type(L, stk+1))
		{
			case LUA_TSTRING:
				idx = GetPivotFrame(L, stk+1);
				break;
			case LUA_TNUMBER:
				idx = lua_tonumber(L, stk+1);

				if ((idx < 0) || (idx >= 64))
					return luaL_error(L, "pivot frame %d out of range (0 - %d)", idx, 63);
				break;
			default:
				TYPEERROR("pivot frame", LUA_TNUMBER, lua_type(L, stk+1));
		}
		// the values in pivot[] are also tables
		if (PopPivotSubTable(info->pivot, L, stk+2, idx))
			set_bit_array(info->available, idx);
		lua_pop(L, 1);
	}

	return 0;
}

static int lib_setSpriteInfo(lua_State *L)
{
	spriteinfo_t *info;

	if (!lua_lumploading)
		return luaL_error(L, "Do not alter spriteinfo_t from within a hook or coroutine!");
	if (hud_running)
		return luaL_error(L, "Do not alter spriteinfo_t in HUD rendering code!");
	if (hook_cmd_running)
		return luaL_error(L, "Do not alter spriteinfo_t in CMD building code!");

	lua_remove(L, 1);
	{
		uint32_t i = luaL_checkinteger(L, 1);
		if (i == 0 || i >= NUMSPRITES)
			return luaL_error(L, "spriteinfo[] index %d out of range (1 - %d)", i, NUMSPRITES-1);
		info = &spriteinfo[i]; // get the spriteinfo to assign to.
	}
	luaL_checktype(L, 2, LUA_TTABLE); // check that we've been passed a table.
	lua_remove(L, 1); // pop sprite num, don't need it any more.
	lua_settop(L, 1); // cut the stack here. the only thing left now is the table of data we're assigning to the spriteinfo.

	lua_pushnil(L);
	while (lua_next(L, 1)) {
		lua_Integer i = 0;
		const char *str = NULL;
		if (lua_isnumber(L, 2))
			i = lua_tointeger(L, 2);
		else
			str = luaL_checkstring(L, 2);

		if (i == 1 || (str && fastcmp(str, "pivot")))
		{
			// pivot[] is a table
			if (lua_istable(L, 3))
				return PopPivotTable(info, L, 3);
			else
				FIELDERROR("pivot", va("%s expected, got %s", lua_typename(L, LUA_TTABLE), luaL_typename(L, -1)))
		}

		lua_pop(L, 1);
	}

	return 0;
}

#undef FIELDERROR
#undef TYPEERROR

static int lib_spriteinfolen(lua_State *L)
{
	lua_pushinteger(L, NUMSPRITES);
	return 1;
}

// spriteinfo_ fields
#define SPRITEINFOFIELDS(X) \
	X(pivot) \
	X(brightmap)

enum spriteinfo_e
{
#define ENUMITEM(name) spriteinfo_##name,
	SPRITEINFOFIELDS(ENUMITEM)
};

static const char *const spriteinfo_opt[] = {
#define ENUMITEMNAME(name) #name,
	SPRITEINFOFIELDS(ENUMITEMNAME)
	NULL,
};
#undef ENUMITEM
#undef ENUMITEMNAME

int spriteinfo_fields_ref = LUA_NOREF;

// spriteinfo_t
static int spriteinfo_get(lua_State *L)
{
	enum spriteinfo_e field = Lua_optoption(L, 2, -1, spriteinfo_fields_ref);

	luaL_checkudata(L, 1, META_SPRITEINFO);

	// push spriteframepivot_t userdata
	switch (field)
	{
		case spriteinfo_pivot:
			return PushCast(L, META_PIVOTLIST);
		case spriteinfo_brightmap:
			return PushCast(L, META_SPRITEBRIGHTLIST);
		default:
			return luaL_error(L, LUA_QL("spriteinfo_t") " has no field named " LUA_QS, lua_tostring(L, 2));
	}

	return 0;
}

static int spriteinfo_set(lua_State *L)
{
	spriteinfo_t *sprinfo = *((spriteinfo_t **)luaL_checkudata(L, 1, META_SPRITEINFO));
	enum spriteinfo_e field = Lua_optoption(L, 2, -1, spriteinfo_fields_ref);

	if (!lua_lumploading)
		return luaL_error(L, "Do not alter spriteinfo_t from within a hook or coroutine!");
	if (hud_running)
		return luaL_error(L, "Do not alter spriteinfo_t in HUD rendering code!");
	if (hook_cmd_running)
		return luaL_error(L, "Do not alter spriteinfo_t in CMD building code!");

	I_Assert(sprinfo != NULL);

	lua_remove(L, 1); // remove spriteinfo
	lua_remove(L, 1); // remove field
	lua_settop(L, 1); // leave only one value

	switch (field)
	{
		case spriteinfo_pivot:
		{
			// pivot[] is a table
			if (lua_istable(L, 1))
				return PopPivotTable(sprinfo, L, 1);
			// pivot[] is userdata
			else if (lua_isuserdata(L, 1))
			{
				spriteinfo_t *copyinfo = *((spriteinfo_t **)luaL_checkudata(L, 1, META_PIVOTLIST));
				memcpy(sprinfo, copyinfo, sizeof(spriteinfo_t));
			}
			break;
		}
		case spriteinfo_brightmap:
			return luaL_error(L, va("Field %s cannot be set in spriteinfo_t", lua_tostring(L, 2)));
		default:
			return luaL_error(L, va("Field %s does not exist in spriteinfo_t", lua_tostring(L, 2)));
	}

	return 0;
}

static int spriteinfo_num(lua_State *L)
{
	spriteinfo_t *sprinfo = *((spriteinfo_t **)luaL_checkudata(L, 1, META_SPRITEINFO));

	I_Assert(sprinfo != NULL);
	I_Assert(sprinfo >= spriteinfo);

	lua_pushinteger(L, (uint32_t)(sprinfo-spriteinfo));
	return 1;
}

// framepivot_t
static int pivotlist_get(lua_State *L)
{
	struct PivotFrame *container;
	spriteinfo_t *sprinfo = *((spriteinfo_t **)luaL_checkudata(L, 1, META_PIVOTLIST));
	uint8_t frame = GetPivotFrame(L, 2);

	// bypass LUA_PushUserdata
	container = lua_newuserdata(L, sizeof *container);
	container->sprinfo = sprinfo;
	container->frame = frame;
	luaL_getmetatable(L, META_FRAMEPIVOT);
	lua_setmetatable(L, -2);

	// stack is left with the userdata on top, as if getting it had originally succeeded.
	return 1;
}

static int pivotlist_set(lua_State *L)
{
	spriteinfo_t *sprinfo = *((spriteinfo_t **)luaL_checkudata(L, 1, META_PIVOTLIST));
	uint8_t frame;
	int okcool = 0;

	if (!lua_lumploading)
		return luaL_error(L, "Do not alter spriteframepivot_t from within a hook or coroutine!");
	if (hud_running)
		return luaL_error(L, "Do not alter spriteframepivot_t in HUD rendering code!");
	if (hook_cmd_running)
		return luaL_error(L, "Do not alter spriteframepivot_t in CMD building code!");

	frame = GetPivotFrame(L, 2);

	// pivot[] is a table
	if (lua_istable(L, 3))
		okcool = PopPivotSubTable(sprinfo->pivot, L, 3, frame);
	// pivot[] is userdata
	else if (lua_isuserdata(L, 3))
	{
		struct PivotFrame *container = luaL_checkudata(L, 3, META_FRAMEPIVOT);
		memcpy(&sprinfo->pivot[frame], &container->sprinfo->pivot[container->frame], sizeof(spriteframepivot_t));
		okcool = 1;
	}

	if (okcool)
		set_bit_array(sprinfo->available, frame);

	return 0;
}

static int pivotlist_num(lua_State *L)
{
	lua_pushinteger(L, 64);
	return 1;
}

// framepivot_ fields
#define FRAMEPIVOTFIELDS(X) \
	X(x) \
	X(y)

enum framepivot_e
{
#define ENUMITEM(name) framepivot_##name,
	FRAMEPIVOTFIELDS(ENUMITEM)
};

static const char *const framepivot_opt[] = {
#define ENUMITEMNAME(name) #name,
	FRAMEPIVOTFIELDS(ENUMITEMNAME)
	NULL,
};
#undef ENUMITEM
#undef ENUMITEMNAME

int framepivot_fields_ref = LUA_NOREF;

static int framepivot_get(lua_State *L)
{
	struct PivotFrame *container = luaL_checkudata(L, 1, META_FRAMEPIVOT);
	spriteframepivot_t *framepivot = &container->sprinfo->pivot[container->frame];
	enum framepivot_e field = Lua_optoption(L, 2, -1, framepivot_fields_ref);

	I_Assert(framepivot != NULL);

	switch (field)
	{
		case framepivot_x:
			lua_pushinteger(L, framepivot->x);
			break;
		case framepivot_y:
			lua_pushinteger(L, framepivot->y);
			break;
		default:
			return luaL_error(L, va("Field %s does not exist in spriteframepivot_t",  lua_tostring(L, 2)));
	}

	return 1;
}

static int framepivot_set(lua_State *L)
{
	struct PivotFrame *container = luaL_checkudata(L, 1, META_FRAMEPIVOT);
	spriteframepivot_t *framepivot = &container->sprinfo->pivot[container->frame];
	uint8_t *available = container->sprinfo->available;
	enum framepivot_e field = Lua_optoption(L, 2, -1, framepivot_fields_ref);

	if (!lua_lumploading)
		return luaL_error(L, "Do not alter spriteframepivot_t from within a hook or coroutine!");
	if (hud_running)
		return luaL_error(L, "Do not alter spriteframepivot_t in HUD rendering code!");
	if (hook_cmd_running)
		return luaL_error(L, "Do not alter spriteframepivot_t in CMD building code!");

	I_Assert(framepivot != NULL);

	switch (field)
	{
		case framepivot_x:
			framepivot->x = luaL_checkinteger(L, 3);
			set_bit_array(available, container->frame);
			break;
		case framepivot_y:
			framepivot->y = luaL_checkinteger(L, 3);
			set_bit_array(available, container->frame);
		default:
			return luaL_error(L, va("Field %s does not exist in spriteframepivot_t",  lua_tostring(L, 2)));
	}

	return 0;
}

static int framepivot_num(lua_State *L)
{
	lua_pushinteger(L, 2);
	return 1;
}

static int brightlist_get(lua_State *L)
{
	const spriteinfo_t *sprinfo = *((spriteinfo_t **)luaL_checkudata(L, 1, META_SPRITEBRIGHTLIST));
	const uint8_t frame = GetPivotFrame(L, 2);

	lua_pushstring(L, sprinfo->bright[frame]);

	return 1;
}

static int brightlist_set(lua_State *L)
{
	spriteinfo_t *sprinfo = *((spriteinfo_t **)luaL_checkudata(L, 1, META_SPRITEBRIGHTLIST));

	const uint8_t frame = GetPivotFrame(L, 2);
	const char *val = luaL_checkstring(L, 3);

	Z_Free(sprinfo->bright[frame]);
	sprinfo->bright[frame] = Z_StrDup(val);

	return 0;
}

////////////////
// STATE INFO //
////////////////

// Uses astate to determine which state is calling it
// Then looks up which Lua action is assigned to that state and calls it
static void A_Lua(mobj_t *actor)
{
	dboolean found = false;
	I_Assert(actor != NULL);

	lua_settop(gL, 0); // Just in case...
	lua_pushcfunction(gL, LUA_GetErrorMessage);

	// get the action for this state
	lua_getfield(gL, LUA_REGISTRYINDEX, LREG_STATEACTION);
	I_Assert(lua_istable(gL, -1));
	lua_pushlightuserdata(gL, astate);
	lua_rawget(gL, -2);
	I_Assert(lua_isfunction(gL, -1));
	lua_remove(gL, -2); // pop LREG_STATEACTION

	// get the name for this action, if possible.
	lua_getfield(gL, LUA_REGISTRYINDEX, LREG_ACTIONS);
	lua_pushnil(gL);
	while (lua_next(gL, -2))
	{
		if (lua_rawequal(gL, -1, -4))
		{
			found = true;
			superactions[superstack] = lua_tostring(gL, -2); // "A_ACTION"
			++superstack;
			lua_pop(gL, 2); // pop the name and function
			break;
		}
		lua_pop(gL, 1);
	}
	lua_pop(gL, 1); // pop LREG_ACTION

	LUA_PushUserdata(gL, actor, META_MOBJ);
	lua_pushinteger(gL, var1);
	lua_pushinteger(gL, var2);
	LUA_Call(gL, 3, 0, 1, lua_compatmode, true); // TODO

	lua_pop(gL, 1); // pop error handler

	if (found)
	{
		--superstack;
		superactions[superstack] = NULL;
	}
}

// Arbitrary states[] table index -> state_t *
static int lib_getState(lua_State *L)
{
	uint32_t i;
	lua_remove(L, 1);

	i = luaL_checkinteger(L, 1);
	if (i >= NUMSTATES)
		return luaL_error(L, "states[] index %d out of range (0 - %d)", i, NUMSTATES-1);
	LUA_PushUserdata(L, &states[i], META_STATE);
	return 1;
}

// Lua table full of data -> states[] (set the values all at once! :D :D)
static int lib_setState(lua_State *L)
{
	state_t *state;
	lua_remove(L, 1); // don't care about states[] userdata.
	{
		uint32_t i = luaL_checkinteger(L, 1);
		if (i >= NUMSTATES)
			return luaL_error(L, "states[] index %d out of range (0 - %d)", i, NUMSTATES-1);
		state = &states[i]; // get the state to assign to.
	}
	luaL_checktype(L, 2, LUA_TTABLE); // check that we've been passed a table.
	lua_remove(L, 1); // pop state num, don't need it any more.
	lua_settop(L, 1); // cut the stack here. the only thing left now is the table of data we're assigning to the state.

	if (hud_running)
		return luaL_error(L, "Do not alter states in HUD rendering code!");
	if (hook_cmd_running)
		return luaL_error(L, "Do not alter states in CMD building code!");

	// clear the state to start with, in case of missing table elements
	memset((char *)state + sizeof(dehinfo_t), 0, sizeof(*state) - sizeof(dehinfo_t));
	state->tics = -1;

	lua_pushnil(L);
	while (lua_next(L, 1)) {
		lua_Integer i = 0;
		const char *str = NULL;
		lua_Integer value;
		if (lua_isnumber(L, 2))
			i = lua_tointeger(L, 2);
		else
			str = luaL_checkstring(L, 2);

		if (i == 1 || (str && fastcmp(str, "sprite"))) {
			value = luaL_checkinteger(L, 3);
			if (value < SPR_NULL || value >= NUMSPRITES)
				return luaL_error(L, "sprite number %d is invalid.", value);
			state->sprite = (spritenum_t)value;
		} else if (i == 2 || (str && fastcmp(str, "frame"))) {
			state->frame = (uint32_t)luaL_checkinteger(L, 3);
		} else if (i == 3 || (str && fastcmp(str, "tics"))) {
			state->tics = (int32_t)luaL_checkinteger(L, 3);
		} else if (i == 4 || (str && fastcmp(str, "action"))) {
			switch(lua_type(L, 3))
			{
			case LUA_TNIL: // Null? Set the action to nothing, then.
				state->action = NULL;
				break;
			case LUA_TSTRING: // It's a string, expect the name of a built-in action
				LUA_SetActionByName(state, lua_tostring(L, 3));
				break;
			case LUA_TUSERDATA: // It's a userdata, expect META_ACTION of a built-in action
			{
				actionf_p1 *action = *((actionf_p1 **)luaL_checkudata(L, 3, META_ACTION));

				if (!action)
					return luaL_error(L, "not a valid action?");

				state->action = *action;
				break;
			}
			case LUA_TFUNCTION: // It's a function (a Lua function or a C function? either way!)
				lua_getfield(L, LUA_REGISTRYINDEX, LREG_STATEACTION);
				I_Assert(lua_istable(L, -1));
				lua_pushlightuserdata(L, state); // We'll store this function by the state's pointer in the registry.
				lua_pushvalue(L, 3); // Bring it to the top of the stack
				lua_rawset(L, -3); // Set it in the registry
				lua_pop(L, 1); // pop LREG_STATEACTION
				state->action = (actionf_p1)A_Lua; // Set the action for the userdata.
				break;
			default: // ?!
				return luaL_typerror(L, 3, "function");
			}
		} else if (i == 5 || (str && fastcmp(str, "var1"))) {
			state->var1 = (int32_t)luaL_checkinteger(L, 3);
		} else if (i == 6 || (str && fastcmp(str, "var2"))) {
			state->var2 = (int32_t)luaL_checkinteger(L, 3);
		} else if (i == 7 || (str && fastcmp(str, "nextstate"))) {
			value = luaL_checkinteger(L, 3);
			if (value < S_NULL || value >= NUMSTATES)
				return luaL_error(L, "nextstate number %d is invalid.", value);
			state->nextstate = (statenum_t)value;
		}
		lua_pop(L, 1);
	}
	return 0;
}

// #states -> NUMSTATES
static int lib_statelen(lua_State *L)
{
	lua_pushinteger(L, NUMSTATES);
	return 1;
}

dboolean LUA_SetLuaAction(void *stv, const char *action)
{
	state_t *st = (state_t *)stv;

	I_Assert(st != NULL);
	//I_Assert(st >= states && st < states+NUMSTATES); // if you REALLY want to be paranoid...
	I_Assert(action != NULL);

	if (!gL) // Lua isn't loaded,
		return false; // action not set.

	// action is assumed to be in all-caps already !!
	// the registry is case-sensitive, so we strupr everything that enters it.

	lua_getfield(gL, LUA_REGISTRYINDEX, LREG_ACTIONS);
	lua_getfield(gL, -1, action);

	if (lua_isnil(gL, -1)) // no match
	{
		lua_pop(gL, 2); // pop nil and LREG_ACTIONS
		return false; // action not set.
	}

	lua_getfield(gL, LUA_REGISTRYINDEX, LREG_STATEACTION);
	I_Assert(lua_istable(gL, -1));
	lua_pushlightuserdata(gL, stv); // We'll store this function by the state's pointer in the registry.
	lua_pushvalue(gL, -3); // Bring it to the top of the stack
	lua_rawset(gL, -3); // Set it in the registry
	lua_pop(gL, 1); // pop LREG_STATEACTION

	lua_pop(gL, 2); // pop the function and LREG_ACTIONS
	st->action = (actionf_p1)A_Lua; // Set the action for the userdata.
	return true; // action successfully set.
}

dboolean LUA_CallAction(enum actionnum actionnum, mobj_t *actor)
{
	I_Assert(actor != NULL);

	if (!actionsoverridden[actionnum]) // The action is not overriden,
		return false; // action not called.

	if (superstack && fasticmp(actionpointers[actionnum].name, superactions[superstack-1])) // the action is calling itself,
		return false; // let it call the hardcoded function instead.

	lua_pushcfunction(gL, LUA_GetErrorMessage);

	// grab function by uppercase name.
	lua_getfield(gL, LUA_REGISTRYINDEX, LREG_ACTIONS);
	lua_getfield(gL, -1, actionpointers[actionnum].name);
	lua_remove(gL, -2); // pop LREG_ACTIONS

	if (lua_isnil(gL, -1)) // no match
	{
		lua_pop(gL, 2); // pop nil and error handler
		return false; // action not called.
	}

	if (superstack == MAXRECURSION)
	{
		CONS_Alert(CONS_WARNING, "Max Lua Action recursion reached! Cool it on the calling A_Action functions from inside A_Action functions!\n");
		lua_pop(gL, 2); // pop function and error handler
		return true;
	}

	// Found a function.
	// Call it with (actor, var1, var2)
	I_Assert(lua_isfunction(gL, -1));
	LUA_PushUserdata(gL, actor, META_MOBJ);
	lua_pushinteger(gL, var1);
	lua_pushinteger(gL, var2);

	superactions[superstack] = actionpointers[actionnum].name;
	++superstack;

	LUA_Call(gL, 3, 0, -(2 + 3), lua_compatmode, true); // TODO
	lua_pop(gL, -1); // Error handler

	--superstack;
	superactions[superstack] = NULL;
	return true; // action successfully called.
}

// state_ fields
#define STATEFIELDS(X) \
	X(sprite) \
	X(frame) \
	X(tics) \
	X(action) \
	X(actionname) \
	X(var1) \
	X(var2) \
	X(nextstate) \
	X(string) \

enum state_e
{
#define ENUMITEM(name) state_##name,
	STATEFIELDS(ENUMITEM)
};

static const char *const state_opt[] = {
#define ENUMITEMNAME(name) #name,
	STATEFIELDS(ENUMITEMNAME)
	NULL,
};
#undef ENUMITEM
#undef ENUMITEMNAME

int state_fields_ref = LUA_NOREF;

// state_t *, field -> number
static int state_get(lua_State *L)
{
	state_t *st = *((state_t **)luaL_checkudata(L, 1, META_STATE));
	enum state_e field = Lua_optoption(L, 2, -1, state_fields_ref);
	lua_Integer number;

	switch (field)
	{
		case state_sprite:
			number = st->sprite;
			break;
		case state_frame:
			number = st->frame;
			break;
		case state_tics:
			number = st->tics;
			break;
		case state_action:
		{
			const char *name;
			if (!st->action) // Action is NULL.
				return 0; // return nil.
			if (st->action == (actionf_p1)A_Lua) { // This is a Lua function?
				lua_getfield(L, LUA_REGISTRYINDEX, LREG_STATEACTION);
				I_Assert(lua_istable(L, -1));
				lua_pushlightuserdata(L, st); // Push the state pointer and
				lua_rawget(L, -2); // use it to get the actual Lua function.
				lua_remove(L, -2); // pop LREG_STATEACTION
				return 1; // Return the Lua function.
			}

			name = LUA_GetActionName(&st->action); // find a hardcoded function name

			if (!name) // If it's not a hardcoded function and it's not a Lua function...
				return 0; // Just what is this??

			// get the function from the global
			// because the metatable will trigger.
			lua_getglobal(L, name); // actually gets from LREG_ACTIONS if applicable, and pushes a META_ACTION userdata if not.
			return 1; // return just the function
		}
#ifdef DEVELOP
		case state_actionname:
		{
			if (!st->action) { // Action is NULL.
				lua_pushstring(L, "NULL");
			} else if (st->action == (actionf_p1)A_Lua) { // This is a Lua function?
				lua_Debug ar;
				lua_getfield(L, LUA_REGISTRYINDEX, LREG_STATEACTION);
				I_Assert(lua_istable(L, -1));
				lua_pushlightuserdata(L, st); // Push the state pointer and
				lua_rawget(L, -2); // use it to get the actual Lua function.
				lua_remove(L, -2); // pop LREG_STATEACTION
				// This normally doesn't get a function's name because the same Lua function can have many names.
				// It only works because of a hack called 'canonicalname' in blua/lparser.c, which records the
				// name a GLOBAL function was defined with. This does not work for local functions! (Lua actions
				// are almost always global functions though.)
				lua_getinfo(L, ">n", &ar);
				lua_pushstring(L, ar.name);
			} else {
				lua_pushstring(L, LUA_GetActionName(&st->action)); // find a hardcoded function name
			}
			return 1;
		}
#endif
		case state_var1:
			number = st->var1;
			break;
		case state_var2:
			number = st->var2;
			break;
		case state_nextstate:
			number = st->nextstate;
			break;
		case state_string:
		{
			statenum_t id = st-states;
			if (id < NUMSTATES)
			{
				lua_pushstring(L, DEH_StateName(id));
				return 1;
			}

			return 0;
		}
		default:
		{
			if (devparm)
				return luaL_error(L, LUA_QL("state_t") " has no field named " LUA_QS, lua_tostring(L, 2));
			else
				return 0;
			break;
		}
	}

	lua_pushinteger(L, number);
	return 1;
}

// state_t *, field, number -> states[]
static int state_set(lua_State *L)
{
	state_t *st = *((state_t **)luaL_checkudata(L, 1, META_STATE));
	enum state_e field = Lua_optoption(L, 2, -1, state_fields_ref);
	lua_Integer value;

	if (hud_running)
		return luaL_error(L, "Do not alter states in HUD rendering code!");
	if (hook_cmd_running)
		return luaL_error(L, "Do not alter states in CMD building code!");

	switch (field)
	{
		case state_sprite:
		{
			value = luaL_checknumber(L, 3);
			if (value < SPR_NULL || value >= NUMSPRITES)
				return luaL_error(L, "sprite number %d is invalid.", value);
			st->sprite = (spritenum_t)value;
		}
		case state_frame:
			st->frame = (uint32_t)luaL_checknumber(L, 3);
			break;
		case state_tics:
			st->tics = (int32_t)luaL_checknumber(L, 3);
			break;
		case state_action:
		{
			switch(lua_type(L, 3))
			{
				case LUA_TNIL: // Null? Set the action to nothing, then.
					st->action = NULL;
					break;
				case LUA_TSTRING: // It's a string, expect the name of a built-in action
					LUA_SetActionByName(st, lua_tostring(L, 3));
					break;
				case LUA_TUSERDATA: // It's a userdata, expect META_ACTION of a built-in action
				{
					actionf_p1 *action = *((actionf_p1 **)luaL_checkudata(L, 3, META_ACTION));

					if (!action)
						return luaL_error(L, "not a valid action?");

					st->action = *action;
					break;
				}
				case LUA_TFUNCTION: // It's a function (a Lua function or a C function? either way!)
					lua_getfield(L, LUA_REGISTRYINDEX, LREG_STATEACTION);
					I_Assert(lua_istable(L, -1));
					lua_pushlightuserdata(L, st); // We'll store this function by the state's pointer in the registry.
					lua_pushvalue(L, 3); // Bring it to the top of the stack
					lua_rawset(L, -3); // Set it in the registry
					lua_pop(L, 1); // pop LREG_STATEACTION
					st->action = (actionf_p1)A_Lua; // Set the action for the userdata.
					break;
				default: // ?!
					return luaL_typerror(L, 3, "function");
			}
		}
		case state_var1:
			st->var1 = (int32_t)luaL_checknumber(L, 3);
			break;
		case state_var2:
			st->var2 = (int32_t)luaL_checknumber(L, 3);
			break;
		case state_nextstate:
		{
			value = luaL_checkinteger(L, 3);
			if (value < S_NULL || value >= NUMSTATES)
				return luaL_error(L, "nextstate number %d is invalid.", value);
			st->nextstate = (statenum_t)value;
			break;
		}
		default:
			return luaL_error(L, LUA_QL("state_t") " has no field named " LUA_QS, lua_tostring(L, 2));
	}

	return 0;
}

// state_t * -> S_*
static int state_num(lua_State *L)
{
	state_t *state = *((state_t **)luaL_checkudata(L, 1, META_STATE));
	lua_pushinteger(L, state-states);
	return 1;
}

///////////////
// MOBJ INFO //
///////////////

// Arbitrary mobjinfo[] table index -> mobjinfo_t *
static int lib_getMobjInfo(lua_State *L)
{
	uint32_t i;
	lua_remove(L, 1);

	i = luaL_checkinteger(L, 1);
	if (i >= NUMMOBJTYPES)
		return luaL_error(L, "mobjinfo[] index %d out of range (0 - %d)", i, NUMMOBJTYPES-1);
	LUA_PushUserdata(L, &mobjinfo[i], META_MOBJINFO);
	return 1;
}

// Lua table full of data -> mobjinfo[]
static int lib_setMobjInfo(lua_State *L)
{
	mobjinfo_t *info;
	lua_remove(L, 1); // don't care about mobjinfo[] userdata.
	{
		uint32_t i = luaL_checkinteger(L, 1);
		if (i >= NUMMOBJTYPES)
			return luaL_error(L, "mobjinfo[] index %d out of range (0 - %d)", i, NUMMOBJTYPES-1);
		info = &mobjinfo[i]; // get the mobjinfo to assign to.
	}
	luaL_checktype(L, 2, LUA_TTABLE); // check that we've been passed a table.
	lua_remove(L, 1); // pop mobjtype num, don't need it any more.
	lua_settop(L, 1); // cut the stack here. the only thing left now is the table of data we're assigning to the mobjinfo.

	if (hud_running)
		return luaL_error(L, "Do not alter mobjinfo in HUD rendering code!");
	if (hook_cmd_running)
		return luaL_error(L, "Do not alter mobjinfo in CMD building code!");

	// clear the mobjinfo to start with, in case of missing table elements
	memset((char *)info + sizeof(dehinfo_t), 0, sizeof(*info) - sizeof(dehinfo_t));
	info->doomednum = -1; // default to no editor value
	info->spawnhealth = 1; // avoid 'dead' noclip behaviors

	lua_pushnil(L);
	while (lua_next(L, 1)) {
		lua_Integer i = 0;
		const char *str = NULL;
		lua_Integer value;
		if (lua_isnumber(L, 2))
			i = lua_tointeger(L, 2);
		else
			str = luaL_checkstring(L, 2);

		if (i == 1 || (str && fastcmp(str,"doomednum")))
			info->doomednum = (int32_t)luaL_checkinteger(L, 3);
		else if (i == 2 || (str && fastcmp(str,"spawnstate"))) {
			value = luaL_checkinteger(L, 3);
			if (value < S_NULL || value >= NUMSTATES)
				return luaL_error(L, "spawnstate number %d is invalid.", value);
			info->spawnstate = (statenum_t)value;
		} else if (i == 3 || (str && fastcmp(str,"spawnhealth")))
			info->spawnhealth = (int32_t)luaL_checkinteger(L, 3);
		else if (i == 4 || (str && fastcmp(str,"seestate"))) {
			value = luaL_checkinteger(L, 3);
			if (value < S_NULL || value >= NUMSTATES)
				return luaL_error(L, "seestate number %d is invalid.", value);
			info->seestate = (statenum_t)value;
		} else if (i == 5 || (str && fastcmp(str,"seesound"))) {
			value = luaL_checkinteger(L, 3);
			if (value < sfx_None || value >= NUMSFX)
				return luaL_error(L, "seesound number %d is invalid.", value);
			info->seesound = (sfxenum_t)value;
		} else if (i == 6 || (str && fastcmp(str,"reactiontime")))
			info->reactiontime = (int32_t)luaL_checkinteger(L, 3);
		else if (i == 7 || (str && fastcmp(str,"attacksound")))
			info->attacksound = luaL_checkinteger(L, 3);
		else if (i == 8 || (str && fastcmp(str,"painstate")))
			info->painstate = luaL_checkinteger(L, 3);
		else if (i == 9 || (str && fastcmp(str,"painchance")))
			info->painchance = (int32_t)luaL_checkinteger(L, 3);
		else if (i == 10 || (str && fastcmp(str,"painsound")))
			info->painsound = luaL_checkinteger(L, 3);
		else if (i == 11 || (str && fastcmp(str,"meleestate")))
			info->meleestate = luaL_checkinteger(L, 3);
		else if (i == 12 || (str && fastcmp(str,"missilestate")))
			info->missilestate = luaL_checkinteger(L, 3);
		else if (i == 13 || (str && fastcmp(str,"deathstate")))
			info->deathstate = luaL_checkinteger(L, 3);
		else if (i == 14 || (str && fastcmp(str,"xdeathstate")))
			info->xdeathstate = luaL_checkinteger(L, 3);
		else if (i == 15 || (str && fastcmp(str,"deathsound")))
			info->deathsound = luaL_checkinteger(L, 3);
		else if (i == 16 || (str && fastcmp(str,"speed")))
			info->speed = luaL_checkfixed(L, 3);
		else if (i == 17 || (str && fastcmp(str,"radius")))
			info->radius = luaL_checkfixed(L, 3);
		else if (i == 18 || (str && fastcmp(str,"height")))
			info->height = luaL_checkfixed(L, 3);
		else if (i == 19 || (str && fastcmp(str,"dispoffset")))
			info->dispoffset = (int32_t)luaL_checkinteger(L, 3);
		else if (i == 20 || (str && fastcmp(str,"mass")))
			info->mass = (int32_t)luaL_checkinteger(L, 3);
		else if (i == 21 || (str && fastcmp(str,"damage")))
			info->damage = (int32_t)luaL_checkinteger(L, 3);
		else if (i == 22 || (str && fastcmp(str,"activesound")))
			info->activesound = luaL_checkinteger(L, 3);
		else if (i == 23 || (str && fastcmp(str,"flags")))
			info->flags = (int32_t)luaL_checkinteger(L, 3);
		else if (i == 24 || (str && fastcmp(str,"raisestate")))
			info->raisestate = luaL_checkinteger(L, 3);
		else if (i == 25 || (str && fastcmp(str,"flags2")))
			info->flags2 = (int32_t)luaL_checkinteger(L, 3);
		else if (i == 26 || (str && fastcmp(str,"eflags")))
			info->eflags = luaL_checkinteger(L, 3);
		else if (i == 27 || (str && fastcmp(str,"renderflags")))
			info->renderflags = (int32_t)luaL_checkinteger(L, 3);
		else if (i == 28 || (str && fastcmp(str,"renderflags2")))
			info->renderflags2 = (int32_t)luaL_checkinteger(L, 3);
		lua_pop(L, 1);
	}
	return 0;
}

// #mobjinfo -> NUMMOBJTYPES
static int lib_mobjinfolen(lua_State *L)
{
	lua_pushinteger(L, NUMMOBJTYPES);
	return 1;
}

// mobjinfo_ fields
#define MOBJINFOFIELDS(X) \
	X(doomednum) \
	X(spawnstate) \
	X(spawnhealth) \
	X(seestate) \
	X(seesound) \
	X(reactiontime) \
	X(attacksound) \
	X(painstate) \
	X(painchance) \
	X(painsound) \
	X(meleestate) \
	X(missilestate) \
	X(deathstate) \
	X(xdeathstate) \
	X(deathsound) \
	X(speed) \
	X(radius) \
	X(height) \
	X(dispoffset) \
	X(mass) \
	X(damage) \
	X(activesound) \
	X(flags) \
	X(flags2) \
	X(eflags) \
	X(renderflags) \
	X(renderflags2) \
	X(raisestate) \
	X(string)

enum mobjinfo_e
{
#define ENUMITEM(name) mobjinfo_##name,
	MOBJINFOFIELDS(ENUMITEM)
};

static const char *const mobjinfo_opt[] = {
#define ENUMITEMNAME(name) #name,
	MOBJINFOFIELDS(ENUMITEMNAME)
	NULL,
};
#undef ENUMITEM
#undef ENUMITEMNAME

int mobjinfo_fields_ref = LUA_NOREF;

// mobjinfo_t *, field -> number
static int mobjinfo_get(lua_State *L)
{
	mobjinfo_t *info = *((mobjinfo_t **)luaL_checkudata(L, 1, META_MOBJINFO));
	enum mobjinfo_e field = Lua_optoption(L, 2, -1, mobjinfo_fields_ref);

	I_Assert(info != NULL);
	I_Assert(info >= mobjinfo);

	switch (field)
	{
		case mobjinfo_doomednum:
			lua_pushinteger(L, info->doomednum);
			break;
		case mobjinfo_spawnstate:
			lua_pushinteger(L, info->spawnstate);
			break;
		case mobjinfo_spawnhealth:
			lua_pushinteger(L, info->spawnhealth);
			break;
		case mobjinfo_seestate:
			lua_pushinteger(L, info->seestate);
			break;
		case mobjinfo_seesound:
			lua_pushinteger(L, info->seesound);
			break;
		case mobjinfo_reactiontime:
			lua_pushinteger(L, info->reactiontime);
			break;
		case mobjinfo_attacksound:
			lua_pushinteger(L, info->attacksound);
			break;
		case mobjinfo_painstate:
			lua_pushinteger(L, info->painstate);
			break;
		case mobjinfo_painchance:
			lua_pushinteger(L, info->painchance);
			break;
		case mobjinfo_painsound:
			lua_pushinteger(L, info->painsound);
			break;
		case mobjinfo_meleestate:
			lua_pushinteger(L, info->meleestate);
			break;
		case mobjinfo_missilestate:
			lua_pushinteger(L, info->missilestate);
			break;
		case mobjinfo_deathstate:
			lua_pushinteger(L, info->deathstate);
			break;
		case mobjinfo_xdeathstate:
			lua_pushinteger(L, info->xdeathstate);
			break;
		case mobjinfo_deathsound:
			lua_pushinteger(L, info->deathsound);
			break;
		case mobjinfo_speed:
			lua_pushinteger(L, info->speed); // sometimes it's fixed_t, sometimes it's not...
			break;
		case mobjinfo_radius:
			lua_pushfixed(L, info->radius);
			break;
		case mobjinfo_height:
			lua_pushfixed(L, info->height);
			break;
		case mobjinfo_dispoffset:
			lua_pushinteger(L, info->dispoffset);
			break;
		case mobjinfo_mass:
			lua_pushinteger(L, info->mass);
			break;
		case mobjinfo_damage:
			lua_pushinteger(L, info->damage);
			break;
		case mobjinfo_activesound:
			lua_pushinteger(L, info->activesound);
			break;
		case mobjinfo_flags:
			lua_pushinteger(L, info->flags);
			break;
		case mobjinfo_flags2:
			lua_pushinteger(L, info->flags2);
			break;
		case mobjinfo_eflags:
			lua_pushinteger(L, info->eflags);
			break;
		case mobjinfo_renderflags:
			lua_pushinteger(L, info->renderflags);
			break;
		case mobjinfo_renderflags2:
			lua_pushinteger(L, info->renderflags2);
			break;
		case mobjinfo_raisestate:
			lua_pushinteger(L, info->raisestate);
			break;
		case mobjinfo_string:
		{
			mobjtype_t id = info-mobjinfo;
			if (id < NUMMOBJTYPES)
			{
				lua_pushstring(L, DEH_MobjtypeName(id));
				return 1;
			}

			return 0;
		}
		default:
		{
			lua_getfield(L, LUA_REGISTRYINDEX, LREG_EXTVARS);
			I_Assert(lua_istable(L, -1));
			lua_pushlightuserdata(L, info);
			lua_rawget(L, -2);
			if (!lua_istable(L, -1)) { // no extra values table
				CONS_Debug(DBG_LUA, M_GetText("'%s' has no field named '%s'; returning nil.\n"), "mobjinfo_t", lua_tostring(L, 2));
				return 0;
			}
			lua_getfield(L, -1, lua_tostring(L, 2));
			if (lua_isnil(L, -1)) // no value for this field
				CONS_Debug(DBG_LUA, M_GetText("'%s' has no field named '%s'; returning nil.\n"), "mobjinfo_t", lua_tostring(L, 2));
			break;
		}
	}

	return 1;
}

// mobjinfo_t *, field, number -> mobjinfo[]
static int mobjinfo_set(lua_State *L)
{
	mobjinfo_t *info = *((mobjinfo_t **)luaL_checkudata(L, 1, META_MOBJINFO));
	enum mobjinfo_e field = Lua_optoption(L, 2, -1, mobjinfo_fields_ref);

	if (hud_running)
		return luaL_error(L, "Do not alter mobjinfo in HUD rendering code!");
	if (hook_cmd_running)
		return luaL_error(L, "Do not alter mobjinfo in CMD building code!");

	I_Assert(info != NULL);
	I_Assert(info >= mobjinfo);

	switch (field)
	{
		case mobjinfo_doomednum:
			info->doomednum = (int32_t)luaL_checkinteger(L, 3);
			break;
		case mobjinfo_spawnstate:
			info->spawnstate = luaL_checkinteger(L, 3);
			break;
		case mobjinfo_spawnhealth:
			info->spawnhealth = (int32_t)luaL_checkinteger(L, 3);
			break;
		case mobjinfo_seestate:
			info->seestate = luaL_checkinteger(L, 3);
			break;
		case mobjinfo_seesound:
			info->seesound = luaL_checkinteger(L, 3);
			break;
		case mobjinfo_reactiontime:
			info->reactiontime = (int32_t)luaL_checkinteger(L, 3);
			break;
		case mobjinfo_attacksound:
			info->attacksound = luaL_checkinteger(L, 3);
			break;
		case mobjinfo_painstate:
			info->painstate = luaL_checkinteger(L, 3);
			break;
		case mobjinfo_painchance:
			info->painchance = (int32_t)luaL_checkinteger(L, 3);
			break;
		case mobjinfo_painsound:
			info->painsound = luaL_checkinteger(L, 3);
			break;
		case mobjinfo_meleestate:
			info->meleestate = luaL_checkinteger(L, 3);
			break;
		case mobjinfo_missilestate:
			info->missilestate = luaL_checkinteger(L, 3);
			break;
		case mobjinfo_deathstate:
			info->deathstate = luaL_checkinteger(L, 3);
			break;
		case mobjinfo_xdeathstate:
			info->xdeathstate = luaL_checkinteger(L, 3);
			break;
		case mobjinfo_deathsound:
			info->deathsound = luaL_checkinteger(L, 3);
			break;
		case mobjinfo_speed:
			info->speed = luaL_checkfixed(L, 3);
			break;
		case mobjinfo_radius:
			info->radius = luaL_checkfixed(L, 3);
			break;
		case mobjinfo_height:
			info->height = luaL_checkfixed(L, 3);
			break;
		case mobjinfo_dispoffset:
			info->dispoffset = (int32_t)luaL_checkinteger(L, 3);
			break;
		case mobjinfo_mass:
			info->mass = (int32_t)luaL_checkinteger(L, 3);
			break;
		case mobjinfo_damage:
			info->damage = (int32_t)luaL_checkinteger(L, 3);
			break;
		case mobjinfo_activesound:
			info->activesound = luaL_checkinteger(L, 3);
			break;
		case mobjinfo_flags:
			info->flags = (int32_t)luaL_checkinteger(L, 3);
			break;
		case mobjinfo_flags2:
			info->flags2 = (int32_t)luaL_checkinteger(L, 3);
			break;
		case mobjinfo_eflags:
			info->eflags = (int32_t)luaL_checkinteger(L, 3);
			break;
		case mobjinfo_renderflags:
			info->renderflags = (int32_t)luaL_checkinteger(L, 3);
			break;
		case mobjinfo_renderflags2:
			info->renderflags2 = (int32_t)luaL_checkinteger(L, 3);
			break;
		case mobjinfo_raisestate:
			info->raisestate = luaL_checkinteger(L, 3);
			break;
		default:
		{
			lua_getfield(L, LUA_REGISTRYINDEX, LREG_EXTVARS);
			I_Assert(lua_istable(L, -1));
			lua_pushlightuserdata(L, info);
			lua_rawget(L, -2);
			if (lua_isnil(L, -1)) {
				// This index doesn't have a table for extra values yet, let's make one.
				lua_pop(L, 1);
				CONS_Debug(DBG_LUA, M_GetText("'%s' has no field named '%s'; adding it as Lua data.\n"), "mobjinfo_t", lua_tostring(L, 2));
				lua_newtable(L);
				lua_pushlightuserdata(L, info);
				lua_pushvalue(L, -2); // ext value table
				lua_rawset(L, -4); // LREG_EXTVARS table
			}
			lua_pushvalue(L, 3); // value to store
			lua_setfield(L, -2, lua_tostring(L, 2));
			lua_pop(L, 2);
			break;
		}
	}

	return 0;
}

// mobjinfo_t * -> MT_*
static int mobjinfo_num(lua_State *L)
{
	mobjinfo_t *info = *((mobjinfo_t **)luaL_checkudata(L, 1, META_MOBJINFO));

	I_Assert(info != NULL);
	I_Assert(info >= mobjinfo);

	lua_pushinteger(L, info-mobjinfo);
	return 1;
}

//////////////
// SFX INFO //
//////////////

// Arbitrary S_sfx[] table index -> sfxinfo_t *
static int lib_getSfxInfo(lua_State *L)
{
	uint32_t i;
	lua_remove(L, 1);

	i = luaL_checkinteger(L, 1);
	if ((!lua_compatmode && i == 0) || i >= NUMSFX)
		return luaL_error(L, "sfxinfo[] index %d out of range (1 - %d)", i, NUMSFX-1);
	LUA_PushUserdata(L, &S_sfx[i], META_SFXINFO);
	return 1;
}

int sfxinfow_fields_ref = LUA_NOREF;

// stack: dummy, S_sfx[] table index, table of values to set.
static int lib_setSfxInfo(lua_State *L)
{
	sfxinfo_t *info;

	lua_remove(L, 1);
	{
		uint32_t i = luaL_checkinteger(L, 1);
		if ((!lua_compatmode && i == 0) || i >= NUMSFX)
			return luaL_error(L, "sfxinfo[] index %d out of range (1 - %d)", i, NUMSFX-1);
		info = &S_sfx[i]; // get the sfxinfo to assign to.
	}
	luaL_checktype(L, 2, LUA_TTABLE); // check that we've been passed a table.
	lua_remove(L, 1); // pop sfx num, don't need it any more.
	lua_settop(L, 1); // cut the stack here. the only thing left now is the table of data we're assigning to the sfx.

	if (hud_running)
		return luaL_error(L, "Do not alter sfxinfo in HUD rendering code!");
	if (hook_cmd_running)
		return luaL_error(L, "Do not alter sfxinfo in CMD building code!");

	lua_pushnil(L);
	while (lua_next(L, 1)) {
		enum sfxinfo_write i;

		if (lua_isnumber(L, 2))
			i = lua_tointeger(L, 2) - 1; // lua is one based, this enum is zero based.
		else
			i = Lua_optoption(L, 2, -1, sfxinfow_fields_ref);

		switch(i)
		{
		case sfxinfow_singular:
			info->singularity = luaL_checkdboolean(L, 3);
			break;
		case sfxinfow_priority:
			info->priority = (int32_t)luaL_checkinteger(L, 3);
			break;
		case sfxinfow_flags:
			info->flags = (int32_t)luaL_checkinteger(L, 3);
			break;
		case sfxinfow_volume:
			info->volume = (int32_t)luaL_checkinteger(L, 3);
			break;
		case sfxinfow_caption:
			strlcpy(info->caption, luaL_checkstring(L, 3), sizeof(info->caption));
			break;
		default:
			break;
		}
		lua_pop(L, 1);
	}

	return 0;
}

static int lib_sfxlen(lua_State *L)
{
	lua_pushinteger(L, NUMSFX);
	return 1;
}

int sfxinfor_fields_ref = LUA_NOREF;

// sfxinfo_t *, field
static int sfxinfo_get(lua_State *L)
{
	sfxinfo_t *sfx = *((sfxinfo_t **)luaL_checkudata(L, 1, META_SFXINFO));
	enum sfxinfo_read field = Lua_optoption(L, 2, -1, sfxinfor_fields_ref);

	I_Assert(sfx != NULL);

	switch (field)
	{
	case sfxinfor_name:
		lua_pushstring(L, sfx->name);
		return 1;
	case sfxinfor_singular:
		lua_pushdboolean(L, sfx->singularity);
		return 1;
	case sfxinfor_priority:
		lua_pushinteger(L, sfx->priority);
		return 1;
	case sfxinfor_flags:
		lua_pushinteger(L, sfx->flags);
		return 1;
	case sfxinfor_volume:
		lua_pushinteger(L, sfx->volume);
		return 1;
	case sfxinfor_caption:
		lua_pushstring(L, sfx->caption);
		return 1;
	case sfxinfor_skinsound:
		if (!lua_compatmode)
			goto nope;
		sfxenum_t sound = sfx - S_sfx;
		if (sound >= sfx_kwin && sound <= sfx_kgloat)
			lua_pushinteger(L, sound - sfx_kwin);
		else
			lua_pushinteger(L, -1);
		return 1;
	case sfxinfor_string:
		lua_pushstring(L, sfx->name);
		return 1;
	nope:
	default:
		return luaL_error(L, "Field does not exist in sfxinfo_t");
	}
	return 0;
}

// sfxinfo_t *, field, value
static int sfxinfo_set(lua_State *L)
{
	sfxinfo_t *sfx = *((sfxinfo_t **)luaL_checkudata(L, 1, META_SFXINFO));
	enum sfxinfo_write field = Lua_optoption(L, 2, -1, sfxinfow_fields_ref);

	if (hud_running)
		return luaL_error(L, "Do not alter S_sfx in HUD rendering code!");
	if (hook_cmd_running)
		return luaL_error(L, "Do not alter S_sfx in CMD building code!");

	I_Assert(sfx != NULL);

	lua_remove(L, 1); // remove sfxinfo
	lua_remove(L, 1); // remove field
	lua_settop(L, 1); // leave only one value

	switch (field)
	{
	case sfxinfow_singular:
		sfx->singularity = luaL_checkdboolean(L, 1);
		break;
	case sfxinfow_priority:
		sfx->priority = luaL_checkinteger(L, 1);
		break;
	case sfxinfow_flags:
		sfx->flags = luaL_checkinteger(L, 1);
		break;
	case sfxinfow_volume:
		sfx->volume = luaL_checkinteger(L, 1);
		break;
	case sfxinfow_caption:
		strlcpy(sfx->caption, luaL_checkstring(L, 1), sizeof(sfx->caption));
		break;
	default:
		return luaL_error(L, "Field does not exist in sfxinfo_t");
	}
	return 0;
}

static int sfxinfo_num(lua_State *L)
{
	sfxinfo_t *sfx = *((sfxinfo_t **)luaL_checkudata(L, 1, META_SFXINFO));

	I_Assert(sfx != NULL);
	I_Assert(sfx >= S_sfx);

	lua_pushinteger(L, (uint32_t)(sfx-S_sfx));
	return 1;
}

//////////////
// LUABANKS //
//////////////

static int lib_getluabanks(lua_State *L)
{
	uint8_t i;

	lua_remove(L, 1); // don't care about luabanks[] dummy userdata.

	if (lua_isnumber(L, 1))
		i = lua_tonumber(L, 1);
	else
		return luaL_error(L, "luabanks[] invalid index");

	if (i >= NUM_LUABANKS)
		luaL_error(L, "luabanks[] index %d out of range (%d - %d)", i, 0, NUM_LUABANKS-1);

	lua_pushinteger(L, luabanks[i]);
	return 1;
}

static int lib_setluabanks(lua_State *L)
{
	uint8_t i;
	int32_t j = 0;

	if (hud_running)
		return luaL_error(L, "Do not alter luabanks[] in HUD rendering code!");
	if (hook_cmd_running)
		return luaL_error(L, "Do not alter luabanks[] in CMD building code!");

	lua_remove(L, 1); // don't care about luabanks[] dummy userdata.

	if (lua_isnumber(L, 1))
		i = lua_tonumber(L, 1);
	else
		return luaL_error(L, "luabanks[] invalid index");

	if (i >= NUM_LUABANKS)
		luaL_error(L, "luabanks[] index %d out of range (%d - %d)", i, 0, NUM_LUABANKS-1);

	if (lua_isnumber(L, 2))
		j = lua_tonumber(L, 2);
	else
		return luaL_error(L, "luabanks[] invalid set");

	luabanks[i] = j;
	return 0;
}

static int lib_luabankslen(lua_State *L)
{
	lua_pushinteger(L, NUM_LUABANKS);
	return 1;
}

////////////////////
// SKINCOLOR INFO //
////////////////////

// Arbitrary skincolors[] table index -> skincolor_t *
static int lib_getSkinColor(lua_State *L)
{
	uint32_t i;
	lua_remove(L, 1);

	i = luaL_checkinteger(L, 1);
	if (!i || i >= numskincolors)
		return luaL_error(L, "skincolors[] index %d out of range (1 - %d)", i, numskincolors-1);
	LUA_PushUserdata(L, &skincolors[i], META_SKINCOLOR);
	return 1;
}

//Set the entire c->ramp array
static void setRamp(lua_State *L, skincolor_t* c) {
	uint32_t i;
	lua_pushnil(L);
	for (i=0; i<COLORRAMPSIZE; i++) {
		if (lua_objlen(L,-2)!=COLORRAMPSIZE) {
			luaL_error(L, LUA_QL("skincolor_t") " field 'ramp' must be %d entries long; got %d.", COLORRAMPSIZE, lua_objlen(L,-2));
			break;
		}
		if (lua_next(L, -2) != 0) {
			c->ramp[i] = lua_isnumber(L,-1) ? (uint8_t)luaL_checkinteger(L,-1) : 120;
			lua_pop(L, 1);
		} else
			c->ramp[i] = 120;
	}
	lua_pop(L,1);
}

// Lua table full of data -> skincolors[]
static int lib_setSkinColor(lua_State *L)
{
	uint32_t j;
	skincolor_t *info;
	uint16_t cnum; //skincolor num
	lua_remove(L, 1); // don't care about skincolors[] userdata.
	{
		cnum = (uint16_t)luaL_checkinteger(L, 1);
		if (!cnum || cnum >= numskincolors)
			return luaL_error(L, "skincolors[] index %d out of range (1 - %d)", cnum, numskincolors-1);
		info = &skincolors[cnum]; // get the skincolor to assign to.
	}
	luaL_checktype(L, 2, LUA_TTABLE); // check that we've been passed a table.
	lua_remove(L, 1); // pop skincolor num, don't need it any more.
	lua_settop(L, 1); // cut the stack here. the only thing left now is the table of data we're assigning to the skincolor.

	if (hud_running)
		return luaL_error(L, "Do not alter skincolors in HUD rendering code!");
	if (hook_cmd_running)
		return luaL_error(L, "Do not alter skincolors in CMD building code!");

	// clear the skincolor to start with, in case of missing table elements
	memset((char *)info + sizeof(dehinfo_t), 0, sizeof(*info) - sizeof(dehinfo_t));

	Color_cons_t[cnum].value = cnum;
	lua_pushnil(L);
	while (lua_next(L, 1)) {
		lua_Integer i = 0;
		const char *str = NULL;
		if (lua_isnumber(L, 2))
			i = lua_tointeger(L, 2);
		else
			str = luaL_checkstring(L, 2);

		if (i == 1 || (str && fastcmp(str,"name"))) {
			const char* n = luaL_checkstring(L, 3);
			strlcpy(info->name, n, MAXCOLORNAME+1);
			if (strlen(n) > MAXCOLORNAME)
				CONS_Alert(CONS_WARNING, "skincolor_t field 'name' ('%s') longer than %d chars; clipped to %s.\n", n, MAXCOLORNAME, info->name);
#if 0
			if (strchr(info->name, ' ') != NULL)
				CONS_Alert(CONS_WARNING, "skincolor_t field 'name' ('%s') contains spaces.\n", info->name);
#endif

			if (info->name[0] != '\0') // don't check empty string for dupe
			{
				uint16_t dupecheck = R_GetColorByName(info->name);
				if (fasticmp(info->name, skincolors[SKINCOLOR_NONE].name) || (dupecheck && (dupecheck != info-skincolors)))
					CONS_Alert(CONS_WARNING, "skincolor_t field 'name' ('%s') is a duplicate of another skincolor's name.\n", info->name);
			}
		} else if (i == 2 || (str && fastcmp(str,"ramp"))) {
			if (!lua_istable(L, 3) && luaL_checkudata(L, 3, META_COLORRAMP) == NULL)
				return luaL_error(L, LUA_QL("skincolor_t") " field 'ramp' must be a table or array.");
			else if (lua_istable(L, 3))
				setRamp(L, info);
			else
				for (j=0; j<COLORRAMPSIZE; j++)
					info->ramp[j] = (*((uint8_t **)luaL_checkudata(L, 3, META_COLORRAMP)))[j];
			skincolor_modified[cnum] = true;
		} else if (i == 3 || (str && fastcmp(str,"invcolor"))) {
			uint16_t v = (uint16_t)luaL_checkinteger(L, 3);
			if (v >= numskincolors)
				return luaL_error(L, "skincolor_t field 'invcolor' out of range (1 - %d)", numskincolors-1);
			info->invcolor = v;
		} else if (i == 4 || (str && fastcmp(str,"invshade")))
			info->invshade = (uint8_t)luaL_checkinteger(L, 3)%COLORRAMPSIZE;
		else if (i == 5 || (str && fastcmp(str,"chatcolor")))
			info->chatcolor = (uint16_t)luaL_checkinteger(L, 3);
		else if (i == 6 || (str && fastcmp(str,"accessible"))) {
			dboolean v = lua_todboolean(L, 3);
			/*if (cnum < FIRSTSUPERCOLOR && v != skincolors[cnum].accessible)
				return luaL_error(L, "skincolors[] index %d is a standard color; accessibility changes are prohibited.", cnum);
			else*/
				info->accessible = v;
		}
		lua_pop(L, 1);
	}
	return 0;
}

// #skincolors -> numskincolors
static int lib_skincolorslen(lua_State *L)
{
	lua_pushinteger(L, numskincolors);
	return 1;
}

// skincolor_ fields
#define SKINCOLORFIELDS(X) \
	X(name) \
	X(ramp) \
	X(invcolor) \
	X(invshade) \
	X(chatcolor) \
	X(accessible)

enum skincolor_e
{
#define ENUMITEM(name) skincolor_##name,
	SKINCOLORFIELDS(ENUMITEM)
};

static const char *const skincolor_opt[] = {
#define ENUMITEMNAME(name) #name,
	SKINCOLORFIELDS(ENUMITEMNAME)
	NULL,
};
#undef ENUMITEM
#undef ENUMITEMNAME

int skincolors_fields_ref = LUA_NOREF;

// skincolor_t *, field -> number
static int skincolor_get(lua_State *L)
{
	skincolor_t *info = *((skincolor_t **)luaL_checkudata(L, 1, META_SKINCOLOR));
	enum skincolor_e field = Lua_optoption(L, 2, -1, skincolors_fields_ref);
	lua_settop(L, 2);

	I_Assert(info != NULL);
	I_Assert(info >= skincolors);

	switch (field)
	{
		case skincolor_name:
			lua_pushstring(L, info->name);
			break;
		case skincolor_ramp:
			LUA_PushUserdata(L, info->ramp, META_COLORRAMP);
			break;
		case skincolor_invcolor:
			lua_pushinteger(L, info->invcolor);
			break;
		case skincolor_invshade:
			lua_pushinteger(L, info->invshade);
			break;
		case skincolor_chatcolor:
			lua_pushinteger(L, info->chatcolor);
			break;
		case skincolor_accessible:
			lua_pushdboolean(L, info->accessible);
			break;
		default:
			CONS_Debug(DBG_LUA, M_GetText("'%s' has no field named '%s'; returning nil.\n"), "skincolor_t", lua_tostring(L, 2));
			return 0;
			break;
	}

	return 1;
}

// skincolor_t *, field, number -> skincolors[]
static int skincolor_set(lua_State *L)
{
	uint32_t i;
	skincolor_t *info = *((skincolor_t **)luaL_checkudata(L, 1, META_SKINCOLOR));
	enum skincolor_e field = Lua_optoption(L, 2, -1, skincolors_fields_ref);
	uint16_t cnum = (uint16_t)(info-skincolors);

	I_Assert(info != NULL);
	I_Assert(info >= skincolors);

	if (!cnum || cnum >= numskincolors)
		return luaL_error(L, "skincolors[] index %d out of range (1 - %d)", cnum, numskincolors-1);

	switch (field)
	{
		case skincolor_name:
		{
			const char* n = luaL_checkstring(L, 3);
			strlcpy(info->name, n, MAXCOLORNAME+1);
			if (strlen(n) > MAXCOLORNAME)
				CONS_Alert(CONS_WARNING, "skincolor_t field 'name' ('%s') longer than %d chars; clipped to %s.\n", n, MAXCOLORNAME, info->name);
			#if 0
			if (strchr(info->name, ' ') != NULL)
				CONS_Alert(CONS_WARNING, "skincolor_t field 'name' ('%s') contains spaces.\n", info->name);
			#endif

			if (info->name[0] != '\0') // don't check empty string for dupe
			{
				uint16_t dupecheck = R_GetColorByName(info->name);
				if (fasticmp(info->name, skincolors[SKINCOLOR_NONE].name) || (dupecheck && (dupecheck != cnum)))
					CONS_Alert(CONS_WARNING, "skincolor_t field 'name' ('%s') is a duplicate of another skincolor's name.\n", info->name);
			}
			break;
		}
		case skincolor_ramp:
		{
			if (!lua_istable(L, 3) && luaL_checkudata(L, 3, META_COLORRAMP) == NULL)
				return luaL_error(L, LUA_QL("skincolor_t") " field 'ramp' must be a table or array.");
			else if (lua_istable(L, 3))
				setRamp(L, info);
			else
				for (i=0; i<COLORRAMPSIZE; i++)
					info->ramp[i] = (*((uint8_t **)luaL_checkudata(L, 3, META_COLORRAMP)))[i];
			skincolor_modified[cnum] = true;
			break;
		}
		case skincolor_invcolor:
		{
			uint16_t v = (uint16_t)luaL_checkinteger(L, 3);
			if (v >= numskincolors)
				return luaL_error(L, "skincolor_t field 'invcolor' out of range (1 - %d)", numskincolors-1);
			info->invcolor = v;
			break;
		}
		case skincolor_invshade:
			info->invshade = (uint8_t)luaL_checkinteger(L, 3)%COLORRAMPSIZE;
			break;
		case skincolor_chatcolor:
			info->chatcolor = (uint16_t)luaL_checkinteger(L, 3);
			break;
		case skincolor_accessible:
		{
			dboolean v = lua_todboolean(L, 3);
			if (cnum < FIRSTSUPERCOLOR && v != skincolors[cnum].accessible)
				return luaL_error(L, "skincolors[] index %d is a standard color; accessibility changes are prohibited.", cnum);
			else
				info->accessible = v;
			break;
		}
		default:
			CONS_Debug(DBG_LUA, M_GetText("'%s' has no field named '%s'; returning nil.\n"), "skincolor_t", lua_tostring(L, 2));
			break;
	}

	return 1;
}

// skincolor_t * -> SKINCOLOR_*
static int skincolor_num(lua_State *L)
{
	skincolor_t *info = *((skincolor_t **)luaL_checkudata(L, 1, META_SKINCOLOR));

	I_Assert(info != NULL);
	I_Assert(info >= skincolors);

	lua_pushinteger(L, info-skincolors);
	return 1;
}

// ramp, n -> ramp[n]
static int colorramp_get(lua_State *L)
{
	uint8_t *colorramp = *((uint8_t **)luaL_checkudata(L, 1, META_COLORRAMP));
	uint32_t n = luaL_checkinteger(L, 2);
	if (n >= COLORRAMPSIZE)
		return luaL_error(L, LUA_QL("skincolor_t") " field 'ramp' index %d out of range (0 - %d)", n, COLORRAMPSIZE-1);
	lua_pushinteger(L, colorramp[n]);
	return 1;
}

// ramp, n, value -> ramp[n] = value
static int colorramp_set(lua_State *L)
{
	uint8_t *colorramp = *((uint8_t **)luaL_checkudata(L, 1, META_COLORRAMP));
	uint16_t cnum = (uint16_t)(((uint8_t*)colorramp - (uint8_t*)(skincolors[0].ramp))/sizeof(skincolor_t));
	uint32_t n = luaL_checkinteger(L, 2);
	uint8_t i = (uint8_t)luaL_checkinteger(L, 3);
	if (!cnum || cnum >= numskincolors)
		return luaL_error(L, "skincolors[] index %d out of range (1 - %d)", cnum, numskincolors-1);
	if (n >= COLORRAMPSIZE)
		return luaL_error(L, LUA_QL("skincolor_t") " field 'ramp' index %d out of range (0 - %d)", n, COLORRAMPSIZE-1);
	if (hud_running)
		return luaL_error(L, "Do not alter skincolor_t in HUD rendering code!");
	if (hook_cmd_running)
		return luaL_error(L, "Do not alter skincolor_t in CMD building code!");
	colorramp[n] = i;
	skincolor_modified[cnum] = true;
	return 0;
}

// #ramp -> COLORRAMPSIZE
static int colorramp_len(lua_State *L)
{
	lua_pushinteger(L, COLORRAMPSIZE);
	return 1;
}

//////////////////
// PRECIP PROPS //
//////////////////

// Arbitrary precipprops[] table index -> precipprops_t *
static int lib_getPrecipProps(lua_State *L)
{
	int32_t i;
	lua_remove(L, 1);

	i = luaL_checkinteger(L, 1);
	if (i <= 0 || i >= MAXPRECIP)
		return luaL_error(L, "precipprops[] index %d out of range (1 - %d)", i, MAXPRECIP-1);
	LUA_PushUserdata(L, &precipprops[i], META_PRECIPPROPS);
	return 1;
}

// Lua table full of data -> precipprops[]
static int lib_setPrecipProps(lua_State *L)
{
	precipprops_t *props;
	lua_remove(L, 1); // don't care about precipprops[] userdata.
	{
		int32_t i = luaL_checkinteger(L, 1);
		if (i <= 0 || i >= MAXPRECIP)
			return luaL_error(L, "precipprops[] index %d out of range (1 - %d)", i, MAXPRECIP-1);
		props = &precipprops[i]; // get the precipprops to assign to.
	}

	luaL_checktype(L, 2, LUA_TTABLE); // check that we've been passed a table.
	lua_remove(L, 1); // pop preciptype num, don't need it any more.
	lua_settop(L, 1); // cut the stack here. the only thing left now is the table of data we're assigning to the precipprops.

	if (hud_running)
		return luaL_error(L, "Do not alter precipprops in HUD rendering code!");
	if (hook_cmd_running)
		return luaL_error(L, "Do not alter precipprops in CMD building code!");

	// clear the precipprops to start with, in case of missing table elements
	// make sure we do not clear the name
	memset((char *)props + sizeof(props->name), 0, sizeof(*props) - sizeof(props->name));

	lua_pushnil(L);
	while (lua_next(L, 1)) {
		lua_Integer i = 0;
		const char *str = NULL;
		lua_Integer value;

		if (lua_isnumber(L, 2))
			i = lua_tointeger(L, 2);
		else
			str = luaL_checkstring(L, 2);

		if (i == 1 || (str && fastcmp(str, "type")))
		{
			value = luaL_checkinteger(L, 3);
			if (value < MT_NULL || value >= NUMMOBJTYPES)
				return luaL_error(L, "type number %d is invalid.", value);
			props->type = luaL_checkinteger(L, 3);
		}
		else if (i == 2 || (str && fastcmp(str, "effects")))
		{
			props->effects = luaL_checkinteger(L, 3);
		}

		lua_pop(L, 1);
	}
	return 0;
}

// #precipprops -> MAXPRECIP
static int lib_precippropslen(lua_State *L)
{
	lua_pushinteger(L, MAXPRECIP);
	return 1;
}

// mobjinfo_ fields
#define PRECIPPROPSFIELDS(X) \
	X(type) \
	X(effects)

enum precipprops_e
{
#define ENUMITEM(name) precipprops_##name,
	PRECIPPROPSFIELDS(ENUMITEM)
};

static const char *const precipprops_opt[] = {
#define ENUMITEMNAME(name) #name,
	PRECIPPROPSFIELDS(ENUMITEMNAME)
	NULL,
};
#undef ENUMITEM
#undef ENUMITEMNAME

int precipprops_fields_ref = LUA_NOREF;

// precipprops_t *, field -> number
static int precipprops_get(lua_State *L)
{
	precipprops_t *props = *((precipprops_t **)luaL_checkudata(L, 1, META_PRECIPPROPS));
	enum precipprops_e field = Lua_optoption(L, 2, -1, precipprops_fields_ref);

	I_Assert(props != NULL);
	I_Assert(props >= precipprops);

	switch (field)
	{
		case precipprops_type:
			lua_pushinteger(L, props->type);
			break;
		case precipprops_effects:
			lua_pushinteger(L, props->effects);
			break;
		default:
			CONS_Debug(DBG_LUA, M_GetText("'%s' has no field named '%s'; returning nil.\n"), "precipprops_t", lua_tostring(L, 2));
			return 0;
	}

	return 1;
}

// precipprops_t *, field, number -> precipprops[]
static int precipprops_set(lua_State *L)
{
	precipprops_t *props = *((precipprops_t **)luaL_checkudata(L, 1, META_PRECIPPROPS));
	enum precipprops_e field = Lua_optoption(L, 2, -1, precipprops_fields_ref);

	if (hud_running)
		return luaL_error(L, "Do not alter precipprops in HUD rendering code!");
	if (hook_cmd_running)
		return luaL_error(L, "Do not alter precipprops in CMD building code!");

	I_Assert(props != NULL);
	I_Assert(props >= precipprops);

	switch (field)
	{
		case precipprops_type:
			props->type = luaL_checkinteger(L, 3);
			break;
		case precipprops_effects:
			props->effects = luaL_checkinteger(L, 3);
			break;
		default:
			return luaL_error(L, LUA_QL("precipprops_t") " has no field named " LUA_QS, lua_tostring(L, 2));
			return 0;
	}

	return 0;
}

// precipprops_t * -> PRECIP_*
static int precipprops_num(lua_State *L)
{
	precipprops_t *props = *((precipprops_t **)luaL_checkudata(L, 1, META_PRECIPPROPS));

	I_Assert(props != NULL);
	I_Assert(props >= precipprops);

	lua_pushinteger(L, props - precipprops);
	return 1;
}

//////////////////////////////
//
// Now push all these functions into the Lua state!
//
//
int LUA_InfoLib(lua_State *L)
{
	// index of A_Lua actions to run for each state
	lua_newtable(L);
	lua_setfield(L, LUA_REGISTRYINDEX, LREG_STATEACTION);

	// index of globally available Lua actions by function name
	lua_newtable(L);
	lua_setfield(L, LUA_REGISTRYINDEX, LREG_ACTIONS);

	skincolors_fields_ref = Lua_CreateFieldTable(L, skincolor_opt);
	spriteinfo_fields_ref = Lua_CreateFieldTable(L, spriteinfo_opt);
	framepivot_fields_ref = Lua_CreateFieldTable(L, framepivot_opt);
	state_fields_ref = Lua_CreateFieldTable(L, state_opt);
	mobjinfo_fields_ref = Lua_CreateFieldTable(L, mobjinfo_opt);
	sfxinfow_fields_ref = Lua_CreateFieldTable(L, sfxinfo_wopt);
	sfxinfor_fields_ref = Lua_CreateFieldTable(L, sfxinfo_ropt);
	precipprops_fields_ref = Lua_CreateFieldTable(L, precipprops_opt);

	luaL_newmetatable(L, META_STATE);
		lua_pushcfunction(L, state_get);
		lua_setfield(L, -2, "__index");

		lua_pushcfunction(L, state_set);
		lua_setfield(L, -2, "__newindex");

		lua_pushcfunction(L, state_num);
		lua_setfield(L, -2, "__len");
	lua_pop(L, 1);

	luaL_newmetatable(L, META_MOBJINFO);
		lua_pushcfunction(L, mobjinfo_get);
		lua_setfield(L, -2, "__index");

		lua_pushcfunction(L, mobjinfo_set);
		lua_setfield(L, -2, "__newindex");

		lua_pushcfunction(L, mobjinfo_num);
		lua_setfield(L, -2, "__len");
	lua_pop(L, 1);

	luaL_newmetatable(L, META_SKINCOLOR);
		lua_pushcfunction(L, skincolor_get);
		lua_setfield(L, -2, "__index");

		lua_pushcfunction(L, skincolor_set);
		lua_setfield(L, -2, "__newindex");

		lua_pushcfunction(L, skincolor_num);
		lua_setfield(L, -2, "__len");
	lua_pop(L, 1);

	luaL_newmetatable(L, META_COLORRAMP);
		lua_pushcfunction(L, colorramp_get);
		lua_setfield(L, -2, "__index");

		lua_pushcfunction(L, colorramp_set);
		lua_setfield(L, -2, "__newindex");

		lua_pushcfunction(L, colorramp_len);
		lua_setfield(L, -2, "__len");
	lua_pop(L,1);

	luaL_newmetatable(L, META_SFXINFO);
		lua_pushcfunction(L, sfxinfo_get);
		lua_setfield(L, -2, "__index");

		lua_pushcfunction(L, sfxinfo_set);
		lua_setfield(L, -2, "__newindex");

		lua_pushcfunction(L, sfxinfo_num);
		lua_setfield(L, -2, "__len");
	lua_pop(L, 1);

	luaL_newmetatable(L, META_SPRITEINFO);
		lua_pushcfunction(L, spriteinfo_get);
		lua_setfield(L, -2, "__index");

		lua_pushcfunction(L, spriteinfo_set);
		lua_setfield(L, -2, "__newindex");

		lua_pushcfunction(L, spriteinfo_num);
		lua_setfield(L, -2, "__len");
	lua_pop(L, 1);

	luaL_newmetatable(L, META_PIVOTLIST);
		lua_pushcfunction(L, pivotlist_get);
		lua_setfield(L, -2, "__index");

		lua_pushcfunction(L, pivotlist_set);
		lua_setfield(L, -2, "__newindex");

		lua_pushcfunction(L, pivotlist_num);
		lua_setfield(L, -2, "__len");
	lua_pop(L, 1);

	luaL_newmetatable(L, META_FRAMEPIVOT);
		lua_pushcfunction(L, framepivot_get);
		lua_setfield(L, -2, "__index");

		lua_pushcfunction(L, framepivot_set);
		lua_setfield(L, -2, "__newindex");

		lua_pushcfunction(L, framepivot_num);
		lua_setfield(L, -2, "__len");
	lua_pop(L, 1);

	luaL_newmetatable(L, META_SPRITEBRIGHTLIST);
		lua_pushcfunction(L, brightlist_get);
		lua_setfield(L, -2, "__index");

		lua_pushcfunction(L, brightlist_set);
		lua_setfield(L, -2, "__newindex");

		lua_pushcfunction(L, pivotlist_num);
		lua_setfield(L, -2, "__len");
	lua_pop(L, 1);

	luaL_newmetatable(L, META_PRECIPPROPS);
		lua_pushcfunction(L, precipprops_get);
		lua_setfield(L, -2, "__index");

		lua_pushcfunction(L, precipprops_set);
		lua_setfield(L, -2, "__newindex");

		lua_pushcfunction(L, precipprops_num);
		lua_setfield(L, -2, "__len");
	lua_pop(L, 1);

	lua_newuserdata(L, 0);
		lua_createtable(L, 0, 2);
			lua_pushcfunction(L, lib_getSprname);
			lua_setfield(L, -2, "__index");

			lua_pushcfunction(L, lib_sprnamelen);
			lua_setfield(L, -2, "__len");
		lua_setmetatable(L, -2);
	lua_setglobal(L, "sprnames");

	lua_newuserdata(L, 0);
		lua_createtable(L, 0, 2);
			lua_pushcfunction(L, lib_getSpr2name);
			lua_setfield(L, -2, "__index");

			lua_pushcfunction(L, lib_spr2namelen);
			lua_setfield(L, -2, "__len");
		lua_setmetatable(L, -2);
	lua_setglobal(L, "spr2names");

	lua_newuserdata(L, 0);
		lua_createtable(L, 0, 2);
			lua_pushcfunction(L, lib_getSpr2default);
			lua_setfield(L, -2, "__index");

			lua_pushcfunction(L, lib_setSpr2default);
			lua_setfield(L, -2, "__newindex");

			lua_pushcfunction(L, lib_spr2namelen);
			lua_setfield(L, -2, "__len");
		lua_setmetatable(L, -2);
	lua_setglobal(L, "spr2defaults");

	lua_newuserdata(L, 0);
		lua_createtable(L, 0, 2);
			lua_pushcfunction(L, lib_getState);
			lua_setfield(L, -2, "__index");

			lua_pushcfunction(L, lib_setState);
			lua_setfield(L, -2, "__newindex");

			lua_pushcfunction(L, lib_statelen);
			lua_setfield(L, -2, "__len");
		lua_setmetatable(L, -2);
	lua_setglobal(L, "states");

	lua_newuserdata(L, 0);
		lua_createtable(L, 0, 2);
			lua_pushcfunction(L, lib_getMobjInfo);
			lua_setfield(L, -2, "__index");

			lua_pushcfunction(L, lib_setMobjInfo);
			lua_setfield(L, -2, "__newindex");

			lua_pushcfunction(L, lib_mobjinfolen);
			lua_setfield(L, -2, "__len");
		lua_setmetatable(L, -2);
	lua_setglobal(L, "mobjinfo");

	lua_newuserdata(L, 0);
		lua_createtable(L, 0, 2);
			lua_pushcfunction(L, lib_getSkinColor);
			lua_setfield(L, -2, "__index");

			lua_pushcfunction(L, lib_setSkinColor);
			lua_setfield(L, -2, "__newindex");

			lua_pushcfunction(L, lib_skincolorslen);
			lua_setfield(L, -2, "__len");
		lua_setmetatable(L, -2);
	lua_setglobal(L, "skincolors");

	lua_newuserdata(L, 0);
		lua_createtable(L, 0, 2);
			lua_pushcfunction(L, lib_getSfxInfo);
			lua_setfield(L, -2, "__index");

			lua_pushcfunction(L, lib_setSfxInfo);
			lua_setfield(L, -2, "__newindex");

			lua_pushcfunction(L, lib_sfxlen);
			lua_setfield(L, -2, "__len");
		lua_setmetatable(L, -2);
	lua_pushvalue(L, -1);
	lua_setglobal(L, "S_sfx");
	lua_setglobal(L, "sfxinfo");

	lua_newuserdata(L, 0);
		lua_createtable(L, 0, 2);
			lua_pushcfunction(L, lib_getSpriteInfo);
			lua_setfield(L, -2, "__index");

			lua_pushcfunction(L, lib_setSpriteInfo);
			lua_setfield(L, -2, "__newindex");

			lua_pushcfunction(L, lib_spriteinfolen);
			lua_setfield(L, -2, "__len");
		lua_setmetatable(L, -2);
	lua_setglobal(L, "spriteinfo");

	lua_newuserdata(L, 0);
		lua_createtable(L, 0, 2);
			lua_pushcfunction(L, lib_getPrecipProps);
			lua_setfield(L, -2, "__index");

			lua_pushcfunction(L, lib_setPrecipProps);
			lua_setfield(L, -2, "__newindex");

			lua_pushcfunction(L, lib_precippropslen);
			lua_setfield(L, -2, "__len");
		lua_setmetatable(L, -2);
	lua_setglobal(L, "precipprops");

	luaL_newmetatable(L, META_LUABANKS);
		lua_pushcfunction(L, lib_getluabanks);
		lua_setfield(L, -2, "__index");

		lua_pushcfunction(L, lib_setluabanks);
		lua_setfield(L, -2, "__newindex");

		lua_pushcfunction(L, lib_luabankslen);
		lua_setfield(L, -2, "__len");
	lua_pop(L, 1);

	return 0;
}
