// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2012-2016 by John "JTE" Muniz.
// Copyright (C) 2012-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  lua_baselib.c
/// \brief basic functions for Lua scripting

#include "blua/lua.h"
#include "doomdef.h"
#include "p_local.h"
#include "p_setup.h" // So we can have P_SetupLevelSky
#include "p_slopes.h" // P_GetSlopeZAt
#include "z_zone.h"
#include "r_main.h"
#include "r_draw.h"
#include "r_things.h" // R_Frame2Char etc
#include "m_random.h"
#include "s_sound.h"
#include "g_game.h"
#include "g_input.h"
#include "m_menu.h"
#include "y_inter.h"
#include "hu_stuff.h"	// HU_AddChatText
#include "console.h"
#include "k_kart.h" // SRB2Kart
#include "k_battle.h"
#include "k_boss.h"
#include "k_collide.h"
#include "k_color.h"
#include "k_hud.h"
#include "k_waypoint.h"
#include "k_items.h"
#include "k_cluster.hpp"
#include "k_itemlist.hpp"
#include "d_netcmd.h" // IsPlayerAdmin
#include "m_menu.h" // Player Setup menu color stuff
#include "p_spec.h" // P_StartQuake
#include "i_system.h" // I_GetPreciseTime, I_GetPrecisePrecision
#include "v_video.h"
#include "k_terrain.h"
#include "k_bot.h"

#include "lua_script.h"
#include "lua_libs.h"
#include "lua_hud.h" // hud_running errors
#include "taglist.h" // P_FindSpecialLineFromTag
#include "lua_hook.h" // hook_cmd_running errors

#define NOHUD if (hud_running)\
return luaL_error(L, "HUD rendering code should not call this function!");\
else if (hook_cmd_running)\
return luaL_error(L, "CMD building code should not call this function!");

dboolean luaL_checkdboolean(lua_State *L, int narg) {
	luaL_checktype(L, narg, LUA_TBOOLEAN);
	return lua_todboolean(L, narg);
}

// String concatination
static int lib_concat(lua_State *L)
{
	int n = lua_gettop(L);  /* number of arguments */
	int i;
	char *r = NULL;
	size_t rl = 0, sl;
	lua_getglobal(L, "tostring");
	for (i=1; i<=n; i++) {
		const char *s;
		lua_pushvalue(L, -1);  /* function to be called */
		lua_pushvalue(L, i);   /* value to print */
		lua_call(L, 1, 1);
		s = lua_tolstring(L, -1, &sl);  /* get result */

		if (sl == 0) {
			lua_pop(L, 1);  /* pop result */
			continue;
		}

		if (s == NULL)
			return luaL_error(L, LUA_QL("tostring") " must return a string to "
															LUA_QL("__add"));
		r = Z_Realloc(r, rl+sl, PU_STATIC, NULL);
		memcpy(r+rl, s, sl);
		rl += sl;
		lua_pop(L, 1);  /* pop result */
	}
	lua_pushlstring(L, r, rl);
	Z_Free(r);
	return 1;
}

// Wrapper for CONS_Printf
// Copied from base Lua code
static int lib_print(lua_State *L)
{
	int n = lua_gettop(L);  /* number of arguments */
	int i;
	//HUDSAFE
	lua_getglobal(L, "tostring");
	for (i=1; i<=n; i++) {
		const char *s;
		lua_pushvalue(L, -1);  /* function to be called */
		lua_pushvalue(L, i);   /* value to print */
		lua_call(L, 1, 1);
		s = lua_tostring(L, -1);  /* get result */

		if (s == NULL)
			return luaL_error(L, LUA_QL("tostring") " must return a string to "
															LUA_QL("print"));
		if (i>1) CONS_Printf("\n");
		CONS_Printf("%s", s);
		lua_pop(L, 1);  /* pop result */
	}
	CONS_Printf("\n");
	return 0;
}

// Print stuff in the chat, or in the console if we can't.
static int lib_chatprint(lua_State *L)
{
	const char *str = luaL_checkstring(L, 1);	// retrieve string
	dboolean sound = lua_optdboolean(L, 2);	// retrieve sound dboolean
	int len = strlen(str);

	if (str == NULL)	// error if we don't have a string!
		return luaL_error(L, LUA_QL("tostring") " must return a string to " LUA_QL("chatprint"));

	if (len > 255)	// string is too long!!!
		return luaL_error(L, "String exceeds the 255 characters limit of the chat buffer.");

	HU_AddChatText(str, sound);
	return 0;
}

// Same as above, but do it for only one player.
static int lib_chatprintf(lua_State *L)
{
	int n = lua_gettop(L);  /* number of arguments */
	const char *str = luaL_checkstring(L, 2);	// retrieve string
	dboolean sound = lua_optdboolean(L, 3);	// sound?
	int len = strlen(str);
	player_t *plr;

	if (n < 2)
		return luaL_error(L, "chatprintf requires at least two arguments: player and text.");

	plr = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));	// retrieve player
	if (!plr)
		return LUA_ErrInvalid(L, "player_t");
	if (plr != &players[consoleplayer])
		return 0;

	if (str == NULL)	// error if we don't have a string!
		return luaL_error(L, LUA_QL("tostring") " must return a string to " LUA_QL("chatprintf"));

	if (len > 255)	// string is too long!!!
		return luaL_error(L, "String exceeds the 255 characters limit of the chat buffer.");

	HU_AddChatText(str, sound);
	return 0;
}

static const struct {
	const char *meta;
	const char *utype;
} meta2utype[] = {
	{META_STATE,        "state_t"},
	{META_MOBJINFO,     "mobjinfo_t"},
	{META_SFXINFO,      "sfxinfo_t"},
	{META_SKINCOLOR,    "skincolor_t"},
	{META_COLORRAMP,    "skincolor_t.ramp"},
	{META_SPRITEINFO,   "spriteinfo_t"},
	{META_PIVOTLIST,    "spriteframepivot_t[]"},
	{META_FRAMEPIVOT,   "spriteframepivot_t"},
	{META_PRECIPPROPS,  "precipprops_t"},

	{META_TAGLIST,      "taglist"},

	{META_MOBJ,         "mobj_t"},
	{META_MAPTHING,     "mapthing_t"},

	{META_PLAYER,       "player_t"},
	{META_TICCMD,       "ticcmd_t"},
	{META_SKIN,         "skin_t"},
	{META_SOUNDSID,     "skin_t.soundsid"},
	{META_SKINSPRITES,  "skin_t.sprites"},
	{META_SKINSPRITESLIST,  "skin_t.sprites[]"},
	{META_SKINVOICES, "skin_t.voices"},

	{META_VERTEX,       "vertex_t"},
	{META_LINE,         "line_t"},
	{META_SIDE,         "side_t"},
	{META_SUBSECTOR,    "subsector_t"},
	{META_SECTOR,       "sector_t"},
	{META_FFLOOR,       "ffloor_t"},
#ifdef HAVE_LUA_SEGS
	{META_SEG,          "seg_t"},
	{META_NODE,         "node_t"},
#endif
	{META_SLOPE,        "slope_t"},
	{META_MAPHEADER,    "mapheader_t"},

	{META_POLYOBJ,      "polyobj_t"},
	{META_POLYOBJVERTICES, "polyobj_t.vertices"},
	{META_POLYOBJLINES, "polyobj_t.lines"},

	{META_CVAR,         "consvar_t"},

	{META_SECTORLINES,  "sector_t.lines"},
#ifdef MUTABLE_TAGS
	{META_SECTORTAGLIST, "sector_t.taglist"},
#endif
	{META_SIDENUM,      "line_t.sidenum"},
	{META_LINEARGS,     "line_t.args"},
	{META_LINESTRINGARGS, "line_t.stringargs"},

	{META_SECTORARGS,     "sector_t.args"},
	{META_SECTORSTRINGARGS, "sector_t.stringargs"},

	{META_THINGARGS,     "mapthing.args"},
	{META_THINGSTRINGARGS, "mapthing.stringargs"},
#ifdef HAVE_LUA_SEGS
	{META_NODEBBOX,     "node_t.bbox"},
	{META_NODECHILDREN, "node_t.children"},
#endif

	{META_BBOX,         "bbox"},

	{META_PATCH,        "patch_t"},
	{META_COLORMAP,     "colormap"},
	{META_EXTRACOLORMAP,"extracolormap_t"},
	{META_LIGHTTABLE,   "lighttable_t"},
	{META_CAMERA,       "camera_t"},

	{META_ACTION,       "action"},

	{META_LUABANKS,     "luabanks[]"},

	{META_ACTIVATOR,    "activator_t"},

	{META_FOLLOWER,    "follower_t"},

	{META_SONICLOOPVARS,	"sonicloopvars_t"},
	{META_SONICLOOPCAMVARS,	"sonicloopcamvars_t"},

	{META_BOTVARS,    "botvars_t"},
	{META_BOTCONTROLLER,    "botcontroller_t"},
	{META_BOTDATA,      "botdata_t"},

	{META_SPLASH,       "t_splash_t"},
	{META_FOOTSTEP,     "t_footstep_t"},
	{META_OVERLAY,      "t_overlay_t"},
	{META_TERRAIN,      "terrain_t"},

	{META_VECTOR2,      "vector2_t"},
	{META_VECTOR3,      "vector3_t"},
	{META_MATRIX,       "matrix_t"},
	{META_QUATERNION,   "quaternion_t"},
	{META_VOICE,        "kartvoice_t"},
	{META_VOICE_ARRAY,  "kartvoice_t.array[]"},
	{META_VOICE_ARRAY1, "kartvoice_t.array[1]"},
	{META_KARTITEM,     "kartitem_t"},
	{META_KARTRESULT,   "kartresult_t"},
	{META_KARTITEMGRAPHICS, "kartitemgraphics_t"},
	{META_BOOSTINFO,	"boostinfo_t"},
	{META_RANKLOG,      "ranklog_t"},
	{META_RANKLOG_DRIFTCOUNT,    "ranklog_t.driftboostcount[4]"},
	{META_RANKSCOREFAC, "rankscorefactor_t"},

	{NULL,              NULL}
};

// goes through the above list and returns the utype string for the userdata type
// returns "unknown" instead if we couldn't find the right userdata type
static const char *GetUserdataUType(lua_State *L)
{
	uint8_t i;
	lua_getmetatable(L, -1);

	for (i = 0; meta2utype[i].meta; i++)
	{
		luaL_getmetatable(L, meta2utype[i].meta);
		if (lua_rawequal(L, -1, -2))
		{
			lua_pop(L, 2);
			return meta2utype[i].utype;
		}
		lua_pop(L, 1);
	}

	lua_pop(L, 1);
	return "unknown";
}

// Return a string representing the type of userdata the given var is
// e.g. players[0] -> "player_t"
//   or players[0].powers -> "player_t.powers"
static int lib_userdataType(lua_State *L)
{
	lua_settop(L, 1); // pop everything except arg 1 (in case somebody decided to add more)
	luaL_checktype(L, 1, LUA_TUSERDATA);
	lua_pushstring(L, GetUserdataUType(L));
	return 1;
}

// Returns the output of a fixed-point value as a floating-point string.
static int lib_toFloatStr(lua_State *L)
{
	lua_settop(L, 1);
	fixed_t f = luaL_checkinteger(L, 1);
	lua_pushstring(L, va("%f", FIXED_TO_FLOAT(f)));
	return 1;
}

// Takes a metatable as first and only argument
// Only callable during script loading
static int lib_registerMetatable(lua_State *L)
{
	static uint16_t nextid = 1;

	if (!lua_lumploading)
		return luaL_error(L, "This function cannot be called from within a hook or coroutine!");
	luaL_checktype(L, 1, LUA_TTABLE);

	if (nextid == 0)
		return luaL_error(L, "Too many metatables registered?! Please consider rewriting your script once you are sober again.\n");

	lua_getfield(L, LUA_REGISTRYINDEX, LREG_METATABLES); // 2
		// registry.metatables[metatable] = nextid
		lua_pushvalue(L, 1); // 3
			lua_pushnumber(L, nextid); // 4
		lua_settable(L, 2);

		// registry.metatables[nextid] = metatable
		lua_pushnumber(L, nextid); // 3
			lua_pushvalue(L, 1); // 4
		lua_settable(L, 2);
	lua_pop(L, 1);

	nextid++;

	return 0;
}

// Takes a string as only argument and returns the metatable
// associated to the userdata type this string refers to
// Returns nil if the string does not refer to a valid userdata type
static int lib_userdataMetatable(lua_State *L)
{
	uint32_t i;
	const char *udname = luaL_checkstring(L, 1);

	// Find internal metatable name
	for (i = 0; meta2utype[i].meta; i++)
		if (fastcmp(udname, meta2utype[i].utype))
		{
			luaL_getmetatable(L, meta2utype[i].meta);
			return 1;
		}

	lua_pushnil(L);
	return 1;
}

static int lib_isPlayerAdmin(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	//HUDSAFE
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	lua_pushdboolean(L, IsPlayerAdmin(player-players));
	return 1;
}

static int lib_reserveLuabanks(lua_State *L)
{
	static dboolean reserved = false;
	if (!lua_lumploading)
		return luaL_error(L, "luabanks[] cannot be reserved from within a hook or coroutine!");
	if (reserved)
		return luaL_error(L, "luabanks[] has already been reserved! Only one savedata-enabled mod at a time may use this feature.");
	reserved = true;
	LUA_PushUserdata(L, &luabanks, META_LUABANKS);
	return 1;
}

static int lib_tofixed(lua_State *L)
{
	const char *arg = luaL_checkstring(L, 1);
	char *end;
	float f = strtof(arg, &end);
	if (*end != '\0')
		lua_pushnil(L);
	else
		lua_pushnumber(L, FLOAT_TO_FIXED(f));
	return 1;
}

// M_MENU
//////////////

static int lib_pMoveColorBefore(lua_State *L)
{
	uint16_t color = (uint16_t)luaL_checkinteger(L, 1);
	uint16_t targ = (uint16_t)luaL_checkinteger(L, 2);

	NOHUD
	M_MoveColorBefore(color, targ);
	return 0;
}

static int lib_pMoveColorAfter(lua_State *L)
{
	uint16_t color = (uint16_t)luaL_checkinteger(L, 1);
	uint16_t targ = (uint16_t)luaL_checkinteger(L, 2);

	NOHUD
	M_MoveColorAfter(color, targ);
	return 0;
}

static int lib_pGetColorBefore(lua_State *L)
{
	uint16_t color = (uint16_t)luaL_checkinteger(L, 1);
	lua_pushinteger(L, M_GetColorBefore(color));
	return 1;
}

static int lib_pGetColorAfter(lua_State *L)
{
	uint16_t color = (uint16_t)luaL_checkinteger(L, 1);
	lua_pushinteger(L, M_GetColorAfter(color));
	return 1;
}

// M_MISC
//////////////

static int lib_gMapNumber(lua_State *L)
{
	const char *arg = luaL_checkstring(L, 1);
	mapnum_t map;

	map = G_MapNumber(arg);

	if (map == NEXTMAP_INVALID)
		return 0;


	return map;
}

// M_RANDOM
//////////////

// FIXME: Allow RNG class freeslotting

static int lib_pRandomFixed(lua_State *L)
{
	int32_t seed_offset = lua_isnoneornil(L, 1) ? 0 : luaL_checkinteger(L, 1);
	pr_class_t use_seed = (pr_class_t)(PR_LUA_1 + seed_offset);

	if (use_seed > PRLASTLUACLASS || use_seed < PR_LUA_1 || seed_offset < 0)
	{
		return luaL_error(L, "prng class out of range (0 - %d)", (PRLASTLUACLASS - PR_LUA_1));
	}

	NOHUD
	lua_pushfixed(L, P_RandomFixed(use_seed));
	demo_writerng = 2;
	return 1;
}

static int lib_pRandomByte(lua_State *L)
{
	int32_t seed_offset = lua_isnoneornil(L, 1) ? 0 : luaL_checkinteger(L, 1);
	pr_class_t use_seed = (pr_class_t)(PR_LUA_1 + seed_offset);

	if (use_seed > PRLASTLUACLASS || use_seed < PR_LUA_1 || seed_offset < 0)
	{
		return luaL_error(L, "prng class out of range (0 - %d)", (PRLASTLUACLASS - PR_LUA_1));
	}

	NOHUD
	lua_pushinteger(L, P_RandomByte(use_seed));
	demo_writerng = 2;
	return 1;
}

static int lib_pRandomKey(lua_State *L)
{
	int32_t a = (int32_t)luaL_checkinteger(L, 1);

	int32_t seed_offset = lua_isnoneornil(L, 2) ? 0 : luaL_checkinteger(L, 2);
	pr_class_t use_seed = (pr_class_t)(PR_LUA_1 + seed_offset);

	if (use_seed > PRLASTLUACLASS || use_seed < PR_LUA_1 || seed_offset < 0)
	{
		return luaL_error(L, "prng class out of range (0 - %d)", (PRLASTLUACLASS - PR_LUA_1));
	}

	NOHUD
	lua_pushinteger(L, P_RandomKey(use_seed, a));
	demo_writerng = 2;
	return 1;
}

static int lib_pRandomRange(lua_State *L)
{
	int32_t a = (int32_t)luaL_checkinteger(L, 1);
	int32_t b = (int32_t)luaL_checkinteger(L, 2);

	int32_t seed_offset = lua_isnoneornil(L, 3) ? 0 : luaL_checkinteger(L, 3);
	pr_class_t use_seed = (pr_class_t)(PR_LUA_1 + seed_offset);

	if (use_seed > PRLASTLUACLASS || use_seed < PR_LUA_1 || seed_offset < 0)
	{
		return luaL_error(L, "prng class out of range (0 - %d)", (PRLASTLUACLASS - PR_LUA_1));
	}

	NOHUD
	if (b < a)
	{
		int32_t c = a;
		a = b;
		b = c;
	}
	lua_pushinteger(L, P_RandomRange(use_seed, a, b));
	demo_writerng = 2;
	return 1;
}

// Macros.
static int lib_pSignedRandom(lua_State *L)
{
	int32_t seed_offset = lua_isnoneornil(L, 1) ? 0 : luaL_checkinteger(L, 1);
	pr_class_t use_seed = (pr_class_t)(PR_LUA_1 + seed_offset);

	if (use_seed > PRLASTLUACLASS || use_seed < PR_LUA_1 || seed_offset < 0)
	{
		return luaL_error(L, "prng class out of range (0 - %d)", (PRLASTLUACLASS - PR_LUA_1));
	}

	NOHUD
	lua_pushinteger(L, P_SignedRandom(use_seed));
	demo_writerng = 2;
	return 1;
}

static int lib_pRandomChance(lua_State *L)
{
	fixed_t p = luaL_checkfixed(L, 1);
	int32_t seed_offset = lua_isnoneornil(L, 2) ? 0 : luaL_checkinteger(L, 2);
	pr_class_t use_seed = (pr_class_t)(PR_LUA_1 + seed_offset);

	if (use_seed > PRLASTLUACLASS || use_seed < PR_LUA_1 || seed_offset < 0)
	{
		return luaL_error(L, "prng class out of range (0 - %d)", (PRLASTLUACLASS - PR_LUA_1));
	}

	NOHUD
	lua_pushdboolean(L, P_RandomChance(use_seed, p));
	demo_writerng = 2;
	return 1;
}

// P_MAPUTIL
///////////////

static int lib_pAproxDistance(lua_State *L)
{
	fixed_t dx = luaL_checkfixed(L, 1);
	fixed_t dy = luaL_checkfixed(L, 2);
	//HUDSAFE
	lua_pushfixed(L, P_AproxDistance(dx, dy));
	return 1;
}

static int lib_pClosestPointOnLine(lua_State *L)
{
	int n = lua_gettop(L);
	fixed_t x = luaL_checkfixed(L, 1);
	fixed_t y = luaL_checkfixed(L, 2);
	vertex_t result;
	//HUDSAFE
	if (lua_isuserdata(L, 3)) // use a real linedef to get our points
	{
		line_t *line = *((line_t **)luaL_checkudata(L, 3, META_LINE));
		if (!line)
			return LUA_ErrInvalid(L, "line_t");
		P_ClosestPointOnLine(x, y, line, &result);
	}
	else // use custom coordinates of our own!
	{
		vertex_t v1, v2; // fake vertexes
		line_t junk; // fake linedef

		if (n < 6)
			return luaL_error(L, "arguments 3 to 6 not all given (expected 4 fixed-point integers)");

		v1.x = luaL_checkfixed(L, 3);
		v1.y = luaL_checkfixed(L, 4);
		v2.x = luaL_checkfixed(L, 5);
		v2.y = luaL_checkfixed(L, 6);

		junk.v1 = &v1;
		junk.v2 = &v2;
		junk.dx = v2.x - v1.x;
		junk.dy = v2.y - v1.y;
		P_ClosestPointOnLine(x, y, &junk, &result);
	}

	lua_pushfixed(L, result.x);
	lua_pushfixed(L, result.y);
	return 2;
}

static int lib_pPointOnLineSide(lua_State *L)
{
	int n = lua_gettop(L);
	fixed_t x = luaL_checkfixed(L, 1);
	fixed_t y = luaL_checkfixed(L, 2);
	//HUDSAFE
	if (lua_isuserdata(L, 3)) // use a real linedef to get our points
	{
		line_t *line = *((line_t **)luaL_checkudata(L, 3, META_LINE));
		if (!line)
			return LUA_ErrInvalid(L, "line_t");
		lua_pushinteger(L, P_PointOnLineSide(x, y, line));
	}
	else // use custom coordinates of our own!
	{
		vertex_t v1, v2; // fake vertexes
		line_t junk; // fake linedef

		if (n < 6)
			return luaL_error(L, "arguments 3 to 6 not all given (expected 4 fixed-point integers)");

		v1.x = luaL_checkfixed(L, 3);
		v1.y = luaL_checkfixed(L, 4);
		v2.x = luaL_checkfixed(L, 5);
		v2.y = luaL_checkfixed(L, 6);

		junk.v1 = &v1;
		junk.v2 = &v2;
		junk.dx = v2.x - v1.x;
		junk.dy = v2.y - v1.y;
		lua_pushinteger(L, P_PointOnLineSide(x, y, &junk));
	}
	return 1;
}

// P_ENEMY
/////////////

static int lib_pCheckMeleeRange(lua_State *L)
{
	mobj_t *actor = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	NOHUD
	INLEVEL
	if (!actor)
		return LUA_ErrInvalid(L, "mobj_t");
	lua_pushdboolean(L, P_CheckMeleeRange(actor));
	return 1;
}

static int lib_pJetbCheckMeleeRange(lua_State *L)
{
	mobj_t *actor = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	NOHUD
	INLEVEL
	if (!actor)
		return LUA_ErrInvalid(L, "mobj_t");
	lua_pushdboolean(L, P_JetbCheckMeleeRange(actor));
	return 1;
}

static int lib_pFaceStabCheckMeleeRange(lua_State *L)
{
	mobj_t *actor = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	NOHUD
	INLEVEL
	if (!actor)
		return LUA_ErrInvalid(L, "mobj_t");
	lua_pushdboolean(L, P_FaceStabCheckMeleeRange(actor));
	return 1;
}

static int lib_pSkimCheckMeleeRange(lua_State *L)
{
	mobj_t *actor = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	NOHUD
	INLEVEL
	if (!actor)
		return LUA_ErrInvalid(L, "mobj_t");
	lua_pushdboolean(L, P_SkimCheckMeleeRange(actor));
	return 1;
}

static int lib_pCheckMissileRange(lua_State *L)
{
	mobj_t *actor = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	NOHUD
	INLEVEL
	if (!actor)
		return LUA_ErrInvalid(L, "mobj_t");
	lua_pushdboolean(L, P_CheckMissileRange(actor));
	return 1;
}

static int lib_pNewChaseDir(lua_State *L)
{
	mobj_t *actor = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	NOHUD
	INLEVEL
	if (!actor)
		return LUA_ErrInvalid(L, "mobj_t");
	P_NewChaseDir(actor);
	return 0;
}

static int lib_pLookForPlayers(lua_State *L)
{
	mobj_t *actor = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	fixed_t dist = (fixed_t)luaL_optinteger(L, 2, 0);
	dboolean allaround = lua_optdboolean(L, 3);
	dboolean tracer = lua_optdboolean(L, 4);
	NOHUD
	INLEVEL
	if (!actor)
		return LUA_ErrInvalid(L, "mobj_t");
	lua_pushdboolean(L, P_LookForPlayers(actor, allaround, tracer, dist));
	return 1;
}

// P_MOBJ
////////////

static int lib_pSpawnMobj(lua_State *L)
{
	fixed_t x = luaL_checkfixed(L, 1);
	fixed_t y = luaL_checkfixed(L, 2);
	fixed_t z = luaL_checkfixed(L, 3);
	mobjtype_t type = luaL_checkinteger(L, 4);
	NOHUD
	INLEVEL
	if (type >= NUMMOBJTYPES)
		return luaL_error(L, "mobj type %d out of range (0 - %d)", type, NUMMOBJTYPES-1);
	LUA_PushUserdata(L, P_SpawnMobj(x, y, z, type), META_MOBJ);
	return 1;
}

static int lib_pSpawnMobjFromMobj(lua_State *L)
{
	mobj_t *actor = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	fixed_t x = luaL_checkfixed(L, 2);
	fixed_t y = luaL_checkfixed(L, 3);
	fixed_t z = luaL_checkfixed(L, 4);
	mobjtype_t type = luaL_checkinteger(L, 5);
	NOHUD
	INLEVEL
	if (!actor)
		return LUA_ErrInvalid(L, "mobj_t");
	if (type >= NUMMOBJTYPES)
		return luaL_error(L, "mobj type %d out of range (0 - %d)", type, NUMMOBJTYPES-1);
	LUA_PushUserdata(L, P_SpawnMobjFromMobj(actor, x, y, z, type), META_MOBJ);
	return 1;
}

static int lib_pSpawnMobjFromMobjUnscaled(lua_State *L)
{
	mobj_t *actor = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	fixed_t x = luaL_checkfixed(L, 2);
	fixed_t y = luaL_checkfixed(L, 3);
	fixed_t z = luaL_checkfixed(L, 4);
	mobjtype_t type = luaL_checkinteger(L, 5);
	NOHUD
	INLEVEL
	if (!actor)
		return LUA_ErrInvalid(L, "mobj_t");
	if (type >= NUMMOBJTYPES)
		return luaL_error(L, "mobj type %d out of range (0 - %d)", type, NUMMOBJTYPES-1);
	LUA_PushUserdata(L, P_SpawnMobjFromMobjUnscaled(actor, x, y, z, type), META_MOBJ);
	return 1;
}

static int lib_pRemoveMobj(lua_State *L)
{
	mobj_t *th = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	NOHUD
	INLEVEL
	if (!th)
		return LUA_ErrInvalid(L, "mobj_t");
	if (th->player)
		return luaL_error(L, "Attempt to remove player mobj with P_RemoveMobj.");
	P_RemoveMobj(th);
	return 0;
}

// P_IsValidSprite2 technically doesn't exist, and probably never should... but too much would need to be exposed to allow this to be checked by other methods.

static int lib_pIsValidSprite2(lua_State *L)
{
	mobj_t *mobj = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	uint8_t spr2 = (uint8_t)luaL_checkinteger(L, 2);
	//HUDSAFE
	INLEVEL
	if (!mobj)
		return LUA_ErrInvalid(L, "mobj_t");
	lua_pushdboolean(L, (mobj->skin && (((skin_t *)mobj->skin)->sprites[spr2].numframes)));
	return 1;
}

// P_SpawnLockOn doesn't exist either, but we want to expose making a local mobj without encouraging hacks.

static int lib_pSpawnLockOn(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	mobj_t *lockon = *((mobj_t **)luaL_checkudata(L, 2, META_MOBJ));
	statenum_t state = luaL_checkinteger(L, 3);
	NOHUD
	INLEVEL
	if (!lockon)
		return LUA_ErrInvalid(L, "mobj_t");
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	if (state >= NUMSTATES)
		return luaL_error(L, "state %d out of range (0 - %d)", state, NUMSTATES-1);
#if 0
	if (P_IsLocalPlayer(player)) // Only display it on your own view.
	{
		mobj_t *visual = P_SpawnMobj(lockon->x, lockon->y, lockon->z, MT_LOCKON); // positioning, flip handled in P_SceneryThinker
		P_SetTarget(&visual->target, lockon);
		visual->renderflags |= RF_DONTDRAW;
		P_SetMobjStateNF(visual, state);
	}
#else
	CONS_Alert(CONS_WARNING, "TODO: P_SpawnLockOn is deprecated\n");
#endif
	return 0;
}

static int lib_pSpawnMissile(lua_State *L)
{
	mobj_t *source = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	mobj_t *dest = *((mobj_t **)luaL_checkudata(L, 2, META_MOBJ));
	mobjtype_t type = luaL_checkinteger(L, 3);
	NOHUD
	INLEVEL
	if (!source || !dest)
		return LUA_ErrInvalid(L, "mobj_t");
	if (type >= NUMMOBJTYPES)
		return luaL_error(L, "mobj type %d out of range (0 - %d)", type, NUMMOBJTYPES-1);
	LUA_PushUserdata(L, P_SpawnMissile(source, dest, type), META_MOBJ);
	return 1;
}

static int lib_pSpawnXYZMissile(lua_State *L)
{
	mobj_t *source = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	mobj_t *dest = *((mobj_t **)luaL_checkudata(L, 2, META_MOBJ));
	mobjtype_t type = luaL_checkinteger(L, 3);
	fixed_t x = luaL_checkfixed(L, 4);
	fixed_t y = luaL_checkfixed(L, 5);
	fixed_t z = luaL_checkfixed(L, 6);
	NOHUD
	INLEVEL
	if (!source || !dest)
		return LUA_ErrInvalid(L, "mobj_t");
	if (type >= NUMMOBJTYPES)
		return luaL_error(L, "mobj type %d out of range (0 - %d)", type, NUMMOBJTYPES-1);
	LUA_PushUserdata(L, P_SpawnXYZMissile(source, dest, type, x, y, z), META_MOBJ);
	return 1;
}

static int lib_pSpawnPointMissile(lua_State *L)
{
	mobj_t *source = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	fixed_t xa = luaL_checkfixed(L, 2);
	fixed_t ya = luaL_checkfixed(L, 3);
	fixed_t za = luaL_checkfixed(L, 4);
	mobjtype_t type = luaL_checkinteger(L, 5);
	fixed_t x = luaL_checkfixed(L, 6);
	fixed_t y = luaL_checkfixed(L, 7);
	fixed_t z = luaL_checkfixed(L, 8);
	NOHUD
	INLEVEL
	if (!source)
		return LUA_ErrInvalid(L, "mobj_t");
	if (type >= NUMMOBJTYPES)
		return luaL_error(L, "mobj type %d out of range (0 - %d)", type, NUMMOBJTYPES-1);
	LUA_PushUserdata(L, P_SpawnPointMissile(source, xa, ya, za, type, x, y, z), META_MOBJ);
	return 1;
}

static int lib_pSpawnAlteredDirectionMissile(lua_State *L)
{
	mobj_t *source = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	mobjtype_t type = luaL_checkinteger(L, 2);
	fixed_t x = luaL_checkfixed(L, 3);
	fixed_t y = luaL_checkfixed(L, 4);
	fixed_t z = luaL_checkfixed(L, 5);
	int32_t shiftingAngle = (int32_t)luaL_checkinteger(L, 5);
	NOHUD
	INLEVEL
	if (!source)
		return LUA_ErrInvalid(L, "mobj_t");
	if (type >= NUMMOBJTYPES)
		return luaL_error(L, "mobj type %d out of range (0 - %d)", type, NUMMOBJTYPES-1);
	LUA_PushUserdata(L, P_SpawnAlteredDirectionMissile(source, type, x, y, z, shiftingAngle), META_MOBJ);
	return 1;
}

static int lib_pColorTeamMissile(lua_State *L)
{
	mobj_t *missile = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	player_t *source = *((player_t **)luaL_checkudata(L, 2, META_PLAYER));
	NOHUD
	INLEVEL
	if (!missile)
		return LUA_ErrInvalid(L, "mobj_t");
	if (!source)
		return LUA_ErrInvalid(L, "player_t");
	P_ColorTeamMissile(missile, source);
	return 0;
}

static int lib_pSPMAngle(lua_State *L)
{
	mobj_t *source = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	mobjtype_t type = luaL_checkinteger(L, 2);
	angle_t angle = luaL_checkangle(L, 3);
	uint8_t allowaim = (uint8_t)luaL_optinteger(L, 4, 0);
	uint32_t flags2 = (uint32_t)luaL_optinteger(L, 5, 0);
	NOHUD
	INLEVEL
	if (!source)
		return LUA_ErrInvalid(L, "mobj_t");
	if (type >= NUMMOBJTYPES)
		return luaL_error(L, "mobj type %d out of range (0 - %d)", type, NUMMOBJTYPES-1);
	LUA_PushUserdata(L, P_SPMAngle(source, type, angle, allowaim, flags2), META_MOBJ);
	return 1;
}

static int lib_pSpawnPlayerMissile(lua_State *L)
{
	mobj_t *source = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	mobjtype_t type = luaL_checkinteger(L, 2);
	uint32_t flags2 = (uint32_t)luaL_optinteger(L, 3, 0);
	NOHUD
	INLEVEL
	if (!source)
		return LUA_ErrInvalid(L, "mobj_t");
	if (type >= NUMMOBJTYPES)
		return luaL_error(L, "mobj type %d out of range (0 - %d)", type, NUMMOBJTYPES-1);
	LUA_PushUserdata(L, P_SpawnPlayerMissile(source, type, flags2), META_MOBJ);
	return 1;
}

static int lib_pMobjFlip(lua_State *L)
{
	mobj_t *mobj = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	//HUDSAFE
	INLEVEL
	if (!mobj)
		return LUA_ErrInvalid(L, "mobj_t");
	lua_pushinteger(L, P_MobjFlip(mobj));
	return 1;
}

static int lib_pGetMobjGravity(lua_State *L)
{
	mobj_t *mobj = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	//HUDSAFE
	INLEVEL
	if (!mobj)
		return LUA_ErrInvalid(L, "mobj_t");

	// HACK: reset the gravity of the object to remain compatible with addons
	// that lack a MF_NOGRAVITY check around the call.
	fixed_t grav = mobj->gravity;
	mobj->gravity = FRACUNIT;
	lua_pushfixed(L, P_GetMobjGravity(mobj));
	mobj->gravity = grav;
	return 1;
}

static int lib_pFlashPal(lua_State *L)
{
	player_t *pl = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	uint16_t type = (uint16_t)luaL_checkinteger(L, 2);
	uint16_t duration = (uint16_t)luaL_checkinteger(L, 3);
	NOHUD
	INLEVEL
	if (!pl)
		return LUA_ErrInvalid(L, "player_t");
	P_FlashPal(pl, type, duration);
	return 0;
}

static int lib_pGetClosestAxis(lua_State *L)
{
	mobj_t *source = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	//HUDSAFE
	INLEVEL
	if (!source)
		return LUA_ErrInvalid(L, "mobj_t");
	LUA_PushUserdata(L, P_GetClosestAxis(source), META_MOBJ);
	return 1;
}

static int lib_pSpawnParaloop(lua_State *L)
{
	fixed_t x = luaL_checkfixed(L, 1);
	fixed_t y = luaL_checkfixed(L, 2);
	fixed_t z = luaL_checkfixed(L, 3);
	fixed_t radius = luaL_checkfixed(L, 4);
	int32_t number = (int32_t)luaL_checkinteger(L, 5);
	mobjtype_t type = luaL_checkinteger(L, 6);
	angle_t rotangle = luaL_checkangle(L, 7);
	statenum_t nstate = luaL_optinteger(L, 8, S_NULL);
	dboolean spawncenter = lua_optdboolean(L, 9);
	NOHUD
	INLEVEL
	if (type >= NUMMOBJTYPES)
		return luaL_error(L, "mobj type %d out of range (0 - %d)", type, NUMMOBJTYPES-1);
	if (nstate >= NUMSTATES)
		return luaL_error(L, "state %d out of range (0 - %d)", nstate, NUMSTATES-1);
	P_SpawnParaloop(x, y, z, radius, number, type, nstate, rotangle, spawncenter);
	return 0;
}

static int lib_pBossTargetPlayer(lua_State *L)
{
	mobj_t *actor = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	dboolean closest = lua_optdboolean(L, 2);
	NOHUD
	INLEVEL
	if (!actor)
		return LUA_ErrInvalid(L, "mobj_t");
	lua_pushdboolean(L, P_BossTargetPlayer(actor, closest));
	return 1;
}

static int lib_pSupermanLook4Players(lua_State *L)
{
	mobj_t *actor = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	NOHUD
	INLEVEL
	if (!actor)
		return LUA_ErrInvalid(L, "mobj_t");
	lua_pushdboolean(L, P_SupermanLook4Players(actor));
	return 1;
}

static int lib_pSetScale(lua_State *L)
{
	mobj_t *mobj = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	fixed_t newscale = luaL_checkfixed(L, 2);
	NOHUD
	INLEVEL
	if (!mobj)
		return LUA_ErrInvalid(L, "mobj_t");
	if (newscale < FRACUNIT/100)
		newscale = FRACUNIT/100;
	P_SetScale2(mobj, newscale, lua_compatmode);
	return 0;
}

static int lib_pInsideANonSolidFFloor(lua_State *L)
{
	mobj_t *mobj = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	ffloor_t *rover = *((ffloor_t **)luaL_checkudata(L, 2, META_FFLOOR));
	//HUDSAFE
	INLEVEL
	if (!mobj)
		return LUA_ErrInvalid(L, "mobj_t");
	if (!rover)
		return LUA_ErrInvalid(L, "ffloor_t");
	lua_pushdboolean(L, P_InsideANonSolidFFloor(mobj, rover));
	return 1;
}

static int lib_pCheckDeathPitCollide(lua_State *L)
{
	mobj_t *mo = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	//HUDSAFE
	INLEVEL
	if (!mo)
		return LUA_ErrInvalid(L, "mobj_t");
	lua_pushdboolean(L, P_CheckDeathPitCollide(mo));
	return 1;
}

static int lib_pCheckSolidLava(lua_State *L)
{
	ffloor_t *rover = *((ffloor_t **)luaL_checkudata(L, 2, META_FFLOOR));
	//HUDSAFE
	INLEVEL
	if (!rover)
		return LUA_ErrInvalid(L, "ffloor_t");
	lua_pushdboolean(L, P_CheckSolidLava(rover));
	return 1;
}

static int lib_pMaceRotate(lua_State *L)
{
	mobj_t *center = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	int32_t baserot = luaL_checkinteger(L, 2);
	int32_t baseprevrot = luaL_checkinteger(L, 3);
	NOHUD
	INLEVEL
	if (!center)
		return LUA_ErrInvalid(L, "mobj_t");
	P_MaceRotate(center, baserot, baseprevrot);
	return 0;
}

static int lib_pCreateFloorSpriteSlope(lua_State *L)
{
	mobj_t *mobj = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	NOHUD
	INLEVEL
	if (!mobj)
		return LUA_ErrInvalid(L, "mobj_t");
	LUA_PushUserdata(L, (pslope_t *)P_CreateFloorSpriteSlope(mobj), META_SLOPE);
	return 1;
}

static int lib_pRemoveFloorSpriteSlope(lua_State *L)
{
	mobj_t *mobj = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	NOHUD
	INLEVEL
	if (!mobj)
		return LUA_ErrInvalid(L, "mobj_t");
	P_RemoveFloorSpriteSlope(mobj);
	return 1;
}

static int lib_pRailThinker(lua_State *L)
{
	mobj_t *mobj = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	tm_t ptm = g_tm;

	NOHUD
	INLEVEL
	if (!mobj)
		return LUA_ErrInvalid(L, "mobj_t");
	lua_pushdboolean(L, P_RailThinker(mobj));
	P_RestoreTMStruct(ptm);
	return 1;
}

static int lib_pXYMovement(lua_State *L)
{
	mobj_t *actor = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	tm_t ptm = g_tm;

	NOHUD
	INLEVEL
	if (!actor)
		return LUA_ErrInvalid(L, "mobj_t");
	P_XYMovement(actor);
	P_RestoreTMStruct(ptm);
	return 0;
}

static int lib_pRingXYMovement(lua_State *L)
{
	mobj_t *actor = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	tm_t ptm = g_tm;

	NOHUD
	INLEVEL
	if (!actor)
		return LUA_ErrInvalid(L, "mobj_t");
	P_RingXYMovement(actor);
	P_RestoreTMStruct(ptm);
	return 0;
}

static int lib_pSceneryXYMovement(lua_State *L)
{
	mobj_t *actor = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	tm_t ptm = g_tm;

	NOHUD
	INLEVEL
	if (!actor)
		return LUA_ErrInvalid(L, "mobj_t");
	P_SceneryXYMovement(actor);
	P_RestoreTMStruct(ptm);
	return 0;
}

static int lib_pZMovement(lua_State *L)
{
	mobj_t *actor = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	tm_t ptm = g_tm;

	NOHUD
	INLEVEL
	if (!actor)
		return LUA_ErrInvalid(L, "mobj_t");
	lua_pushdboolean(L, P_ZMovement(actor));
	P_CheckPosition(actor, actor->x, actor->y, NULL);
	P_RestoreTMStruct(ptm);
	return 1;
}

static int lib_pRingZMovement(lua_State *L)
{
	mobj_t *actor = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	tm_t ptm = g_tm;

	NOHUD
	INLEVEL
	if (!actor)
		return LUA_ErrInvalid(L, "mobj_t");
	P_RingZMovement(actor);
	P_CheckPosition(actor, actor->x, actor->y, NULL);
	P_RestoreTMStruct(ptm);
	return 0;
}

static int lib_pSceneryZMovement(lua_State *L)
{
	mobj_t *actor = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	tm_t ptm = g_tm;

	NOHUD
	INLEVEL
	if (!actor)
		return LUA_ErrInvalid(L, "mobj_t");
	lua_pushdboolean(L, P_SceneryZMovement(actor));
	P_CheckPosition(actor, actor->x, actor->y, NULL);
	P_RestoreTMStruct(ptm);
	return 1;
}

static int lib_pPlayerZMovement(lua_State *L)
{
	mobj_t *actor = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	tm_t ptm = g_tm;

	NOHUD
	INLEVEL
	if (!actor)
		return LUA_ErrInvalid(L, "mobj_t");
	P_PlayerZMovement(actor);
	P_CheckPosition(actor, actor->x, actor->y, NULL);
	P_RestoreTMStruct(ptm);
	return 0;
}

static int lib_pSpawnShadowMobj(lua_State *L)
{
	mobj_t *caster = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	NOHUD
	if (!caster)
		return LUA_ErrInvalid(L, "mobj_t");
	caster->shadowscale = FRACUNIT;
	return 0;
}

// P_USER
////////////

static int lib_pGetPlayerHeight(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	//HUDSAFE
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	lua_pushfixed(L, P_GetPlayerHeight(player));
	return 1;
}

static int lib_pGetPlayerSpinHeight(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	//HUDSAFE
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	lua_pushfixed(L, P_GetPlayerSpinHeight(player));
	return 1;
}

static int lib_pAddPlayerScore(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	uint32_t amount = (uint32_t)luaL_checkinteger(L, 2);
	NOHUD
	INLEVEL
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	P_AddPlayerScore(player, amount);
	return 0;
}

static int lib_pPlayerInPain(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	//HUDSAFE
	INLEVEL
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	lua_pushdboolean(L, P_PlayerInPain(player));
	return 1;
}

static int lib_pResetPlayer(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	NOHUD
	INLEVEL
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	P_ResetPlayer(player);
	return 0;
}

static int lib_pPlayerFullbright(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	INLEVEL
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	lua_pushdboolean(L, P_PlayerFullbright(player));
	return 1;
}


static int lib_pIsObjectInGoop(lua_State *L)
{
	mobj_t *mo = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	//HUDSAFE
	INLEVEL
	if (!mo)
		return LUA_ErrInvalid(L, "mobj_t");
	lua_pushdboolean(L, P_IsObjectInGoop(mo));
	return 1;
}

static int lib_pIsObjectOnGround(lua_State *L)
{
	mobj_t *mo = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	//HUDSAFE
	INLEVEL
	if (!mo)
		return LUA_ErrInvalid(L, "mobj_t");
	lua_pushdboolean(L, P_IsObjectOnGround(mo));
	return 1;
}

static int lib_pInQuicksand(lua_State *L)
{
	mobj_t *mo = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	//HUDSAFE
	INLEVEL
	if (!mo)
		return LUA_ErrInvalid(L, "mobj_t");
	lua_pushdboolean(L, P_InQuicksand(mo));
	return 1;
}

static int lib_pSetObjectMomZ(lua_State *L)
{
	mobj_t *mo = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	fixed_t value = luaL_checkfixed(L, 2);
	dboolean relative = lua_optdboolean(L, 3);
	NOHUD
	INLEVEL
	if (!mo)
		return LUA_ErrInvalid(L, "mobj_t");
	P_SetObjectMomZ(mo, value, relative);
	return 0;
}

static int lib_pPlayJingle(lua_State *L)
{
	player_t *player = NULL;
	jingletype_t jingletype = luaL_checkinteger(L, 2);
	//NOHUD
	//INLEVEL
	if (!lua_isnone(L, 1) && lua_isuserdata(L, 1))
	{
		player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
		if (!player)
			return LUA_ErrInvalid(L, "player_t");
	}
	if (jingletype >= NUMJINGLES)
		return luaL_error(L, "jingletype %d out of range (0 - %d)", jingletype, NUMJINGLES-1);
	P_PlayJingle(player, jingletype);
	return 0;
}

static int lib_pPlayJingleMusic(lua_State *L)
{
	player_t *player = NULL;
	const char *musnamearg = luaL_checkstring(L, 2);
	char musname[7], *p = musname;
	uint16_t musflags = luaL_optinteger(L, 3, 0);
	dboolean looping = lua_opttruedboolean(L, 4);
	jingletype_t jingletype = luaL_optinteger(L, 5, JT_OTHER);
	//NOHUD
	//INLEVEL
	if (!lua_isnone(L, 1) && lua_isuserdata(L, 1))
	{
		player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
		if (!player)
			return LUA_ErrInvalid(L, "player_t");
	}
	if (jingletype >= NUMJINGLES)
		return luaL_error(L, "jingletype %d out of range (0 - %d)", jingletype, NUMJINGLES-1);

	musname[6] = '\0';
	strncpy(musname, musnamearg, 6);

	while (*p) {
		*p = tolower(*p);
		++p;
	}

	P_PlayJingleMusic(player, musname, musflags, looping, jingletype);
	return 0;
}

static int lib_pRestoreMusic(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	//NOHUD
	//INLEVEL
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	if (P_IsLocalPlayer(player))
	{
		P_RestoreMusic(player);
		lua_pushdboolean(L, true);
	}
	else
		lua_pushnil(L);
	return 1;
}

static int lib_pSpawnGhostMobj(lua_State *L)
{
	mobj_t *mobj = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	NOHUD
	INLEVEL
	if (!mobj)
		return LUA_ErrInvalid(L, "mobj_t");
	LUA_PushUserdata(L, P_SpawnGhostMobjEX(mobj, lua_compatmode), META_MOBJ);
	return 1;
}

static int lib_pGivePlayerRings(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	int32_t num_rings = (int32_t)luaL_checkinteger(L, 2);
	NOHUD
	INLEVEL
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	lua_pushinteger(L, P_GivePlayerRings(player, num_rings));
	return 1;
}

static int lib_pGivePlayerLives(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	int32_t numlives = (int32_t)luaL_checkinteger(L, 2);
	NOHUD
	INLEVEL
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	P_GivePlayerLives(player, numlives);
	return 0;
}

static int lib_pMovePlayer(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	tm_t ptm = g_tm;

	NOHUD
	INLEVEL
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	P_MovePlayer(player);
	P_RestoreTMStruct(ptm);
	return 0;
}

static int lib_pDoPlayerExit(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	pflags_t flags = lua_isnoneornil(L, 2) ? 0 : luaL_checkinteger(L, 2);
	NOHUD
	INLEVEL
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	P_DoPlayerExit(player, flags);
	return 0;
}

static int lib_pInstaThrust(lua_State *L)
{
	mobj_t *mo = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	angle_t angle = luaL_checkangle(L, 2);
	fixed_t move = luaL_checkfixed(L, 3);
	NOHUD
	INLEVEL
	if (!mo)
		return LUA_ErrInvalid(L, "mobj_t");
	P_InstaThrust(mo, angle, move);
	return 0;
}

static int lib_pReturnThrustX(lua_State *L)
{
	angle_t angle;
	fixed_t move;
	if (lua_isnil(L, 1) || lua_isuserdata(L, 1))
		lua_remove(L, 1); // ignore mobj as arg1
	angle = luaL_checkangle(L, 1);
	move = luaL_checkfixed(L, 2);
	//HUDSAFE
	lua_pushfixed(L, P_ReturnThrustX(NULL, angle, move));
	return 1;
}

static int lib_pReturnThrustY(lua_State *L)
{
	angle_t angle;
	fixed_t move;
	if (lua_isnil(L, 1) || lua_isuserdata(L, 1))
		lua_remove(L, 1); // ignore mobj as arg1
	angle = luaL_checkangle(L, 1);
	move = luaL_checkfixed(L, 2);
	//HUDSAFE
	lua_pushfixed(L, P_ReturnThrustY(NULL, angle, move));
	return 1;
}

static int lib_pNukeEnemies(lua_State *L)
{
	mobj_t *inflictor = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	mobj_t *source = *((mobj_t **)luaL_checkudata(L, 2, META_MOBJ));
	fixed_t radius = luaL_checkfixed(L, 3);
	NOHUD
	INLEVEL
	if (!inflictor || !source)
		return LUA_ErrInvalid(L, "mobj_t");
	P_NukeEnemies(inflictor, source, radius);
	return 0;
}

static int lib_pKartAltShrinkAuraScalar(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	//HUDSAFE
	if (!player)
		return LUA_ErrInvalid(L, "player_t");

	lua_pushinteger(L, P_KartAltShrinkAuraScalar(player));
	return 1;
}

// P_MAP
///////////

static int lib_pCheckPosition(lua_State *L)
{
	tm_t ptm = g_tm;

	mobj_t *thing = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	fixed_t x = luaL_checkfixed(L, 2);
	fixed_t y = luaL_checkfixed(L, 3);
	NOHUD
	INLEVEL
	if (!thing)
		return LUA_ErrInvalid(L, "mobj_t");
	lua_pushdboolean(L, P_CheckPosition(thing, x, y, NULL));
	LUA_PushUserdata(L, g_tm.thing, META_MOBJ);
	P_RestoreTMStruct(ptm);
	return 2;
}

static int lib_pTryMove(lua_State *L)
{
	tm_t ptm = g_tm;

	mobj_t *thing = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	fixed_t x = luaL_checkfixed(L, 2);
	fixed_t y = luaL_checkfixed(L, 3);
	dboolean allowdropoff = lua_optdboolean(L, 4);
	NOHUD
	INLEVEL
	if (!thing)
		return LUA_ErrInvalid(L, "mobj_t");
	lua_pushdboolean(L, P_TryMove(thing, x, y, allowdropoff, NULL));
	LUA_PushUserdata(L, g_tm.thing, META_MOBJ);
	P_RestoreTMStruct(ptm);
	return 2;
}

static int lib_pMove(lua_State *L)
{
	tm_t ptm = g_tm;

	mobj_t *actor = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	fixed_t speed = luaL_checkfixed(L, 2);
	NOHUD
	INLEVEL
	if (!actor)
		return LUA_ErrInvalid(L, "mobj_t");
	lua_pushdboolean(L, P_Move(actor, speed));
	LUA_PushUserdata(L, g_tm.thing, META_MOBJ);
	P_RestoreTMStruct(ptm);
	return 2;
}

static int lib_pTeleportMove(lua_State *L)
{
	tm_t ptm = g_tm;

	mobj_t *thing = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	fixed_t x = luaL_checkfixed(L, 2);
	fixed_t y = luaL_checkfixed(L, 3);
	fixed_t z = luaL_checkfixed(L, 4);
	NOHUD
	INLEVEL
	if (!thing)
		return LUA_ErrInvalid(L, "mobj_t");
	// Be quiet.
	//LUA_Deprecated(L, "P_TeleportMove", "P_SetOrigin\" or \"P_MoveOrigin");
	lua_pushdboolean(L, P_MoveOrigin(thing, x, y, z));
	LUA_PushUserdata(L, g_tm.thing, META_MOBJ);
	P_RestoreTMStruct(ptm);
	return 2;
}

static int lib_pSetOrigin(lua_State *L)
{
	tm_t ptm = g_tm;

	mobj_t *thing = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	fixed_t x = luaL_checkfixed(L, 2);
	fixed_t y = luaL_checkfixed(L, 3);
	fixed_t z = luaL_checkfixed(L, 4);
	NOHUD
	INLEVEL
	if (!thing)
		return LUA_ErrInvalid(L, "mobj_t");
	lua_pushdboolean(L, P_SetOrigin(thing, x, y, z));
	LUA_PushUserdata(L, g_tm.thing, META_MOBJ);
	P_RestoreTMStruct(ptm);
	return 2;
}

static int lib_pMoveOrigin(lua_State *L)
{
	tm_t ptm = g_tm;

	mobj_t *thing = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	fixed_t x = luaL_checkfixed(L, 2);
	fixed_t y = luaL_checkfixed(L, 3);
	fixed_t z = luaL_checkfixed(L, 4);
	NOHUD
	INLEVEL
	if (!thing)
		return LUA_ErrInvalid(L, "mobj_t");
	lua_pushdboolean(L, P_MoveOrigin(thing, x, y, z));
	LUA_PushUserdata(L, g_tm.thing, META_MOBJ);
	P_RestoreTMStruct(ptm);
	return 2;
}

static int lib_pSetAngle(lua_State *L)
{
	mobj_t *thing = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	angle_t newValue = luaL_checkangle(L, 2);
	NOHUD
	INLEVEL

	if (!thing)
		return LUA_ErrInvalid(L, "mobj_t");
	//P_SetAngle(thing, newValue);
	thing->angle = thing->old_angle = newValue;
	return 0;
}

static int lib_pSetPitch(lua_State *L)
{
	mobj_t *thing = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	angle_t newValue = luaL_checkangle(L, 2);
	NOHUD
	INLEVEL
	if (!thing)
		return LUA_ErrInvalid(L, "mobj_t");
	//P_SetPitch(thing, newValue);
	thing->pitch = thing->old_pitch = newValue;
	return 0;
}

static int lib_pSetRoll(lua_State *L)
{
	mobj_t *thing = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	angle_t newValue = luaL_checkangle(L, 2);
	NOHUD
	INLEVEL
	if (!thing)
		return LUA_ErrInvalid(L, "mobj_t");
	//P_SetRoll(thing, newValue);
	thing->roll = thing->old_roll = newValue;
	return 0;
}

static int lib_pSlideMove(lua_State *L)
{
	mobj_t *mo = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	TryMoveResult_t result = {0};
	NOHUD
	INLEVEL
	if (!mo)
		return LUA_ErrInvalid(L, "mobj_t");

	if (!P_TryMove(mo, mo->x, mo->y, true, &result))
		P_SlideMove(mo, &result);
	return 0;
}

static int lib_pBounceMove(lua_State *L)
{
	mobj_t *mo = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	TryMoveResult_t result = {0};
	NOHUD
	INLEVEL
	if (!mo)
		return LUA_ErrInvalid(L, "mobj_t");
	if (!P_TryMove(mo, mo->x, mo->y, true, &result))
		P_BounceMove(mo,&result);
	return 0;
}

static int lib_pCheckSight(lua_State *L)
{
	mobj_t *t1 = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	mobj_t *t2 = *((mobj_t **)luaL_checkudata(L, 2, META_MOBJ));
	//HUDSAFE?
	INLEVEL
	if (!t1 || !t2)
		return LUA_ErrInvalid(L, "mobj_t");
	lua_pushdboolean(L, P_CheckSight(t1, t2));
	return 1;
}

static int lib_pTraceBlockingLines(lua_State *L)
{
	mobj_t *t1 = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	mobj_t *t2 = *((mobj_t **)luaL_checkudata(L, 2, META_MOBJ));
	//HUDSAFE?
	INLEVEL
	if (!t1 || !t2)
		return LUA_ErrInvalid(L, "mobj_t");
	lua_pushdboolean(L, P_TraceBlockingLines(t1, t2));
	return 1;
}

static int lib_pCheckHoopPosition(lua_State *L)
{
	mobj_t *hoopthing = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	fixed_t x = luaL_checkfixed(L, 2);
	fixed_t y = luaL_checkfixed(L, 3);
	fixed_t z = luaL_checkfixed(L, 4);
	fixed_t radius = luaL_checkfixed(L, 5);
	NOHUD
	INLEVEL
	if (!hoopthing)
		return LUA_ErrInvalid(L, "mobj_t");
	P_CheckHoopPosition(hoopthing, x, y, z, radius);
	return 0;
}

static int lib_pRadiusAttack(lua_State *L)
{
	mobj_t *spot = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	mobj_t *source = *((mobj_t **)luaL_checkudata(L, 2, META_MOBJ));
	fixed_t damagedist = luaL_checkfixed(L, 3);
	uint8_t damagetype = luaL_optinteger(L, 4, 0);
	dboolean sightcheck = lua_opttruedboolean(L, 5);
	NOHUD
	INLEVEL
	if (!spot || !source)
		return LUA_ErrInvalid(L, "mobj_t");
	P_RadiusAttack(spot, source, damagedist, damagetype, sightcheck);
	return 0;
}

static int lib_pFloorzAtPos(lua_State *L)
{
	fixed_t x = luaL_checkfixed(L, 1);
	fixed_t y = luaL_checkfixed(L, 2);
	fixed_t z = luaL_checkfixed(L, 3);
	fixed_t height = luaL_checkfixed(L, 4);
	//HUDSAFE
	INLEVEL
	lua_pushfixed(L, P_FloorzAtPos(x, y, z, height));
	return 1;
}

static int lib_pCeilingzAtPos(lua_State *L)
{
	fixed_t x = luaL_checkfixed(L, 1);
	fixed_t y = luaL_checkfixed(L, 2);
	fixed_t z = luaL_checkfixed(L, 3);
	fixed_t height = luaL_checkfixed(L, 4);
	//HUDSAFE
	INLEVEL
	lua_pushfixed(L, P_CeilingzAtPos(x, y, z, height));
	return 1;
}

static int lib_pGetSectorLightLevelAt(lua_State *L)
{
	dboolean has_sector = false;
	sector_t *sector = NULL;
	if (!lua_isnoneornil(L, 1))
	{
		has_sector = true;
		sector = *((sector_t **)luaL_checkudata(L, 1, META_SECTOR));
	}
	fixed_t x = luaL_checkfixed(L, 2);
	fixed_t y = luaL_checkfixed(L, 3);
	fixed_t z = luaL_checkfixed(L, 4);
	INLEVEL
	if (has_sector && !sector)
		return LUA_ErrInvalid(L, "sector_t");
	if (sector)
		lua_pushinteger(L, P_GetLightLevelFromSectorAt(sector, x, y, z));
	else
		lua_pushinteger(L, P_GetSectorLightLevelAt(x, y, z));
	return 1;
}

static int lib_pGetSectorColormapAt(lua_State *L)
{
	dboolean has_sector = false;
	sector_t *sector = NULL;
	if (!lua_isnoneornil(L, 1))
	{
		has_sector = true;
		sector = *((sector_t **)luaL_checkudata(L, 1, META_SECTOR));
	}
	fixed_t x = luaL_checkfixed(L, 2);
	fixed_t y = luaL_checkfixed(L, 3);
	fixed_t z = luaL_checkfixed(L, 4);
	INLEVEL
	if (has_sector && !sector)
		return LUA_ErrInvalid(L, "sector_t");
	extracolormap_t *exc;
	if (sector)
		exc = P_GetColormapFromSectorAt(sector, x, y, z);
	else
		exc = P_GetSectorColormapAt(x, y, z);
	LUA_PushUserdata(L, exc, META_EXTRACOLORMAP);
	return 1;
}

static int lib_pDoSpring(lua_State *L)
{
	mobj_t *spring = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	mobj_t *object = *((mobj_t **)luaL_checkudata(L, 2, META_MOBJ));
	NOHUD
	INLEVEL
	if (!spring || !object)
		return LUA_ErrInvalid(L, "mobj_t");
	lua_pushdboolean(L, P_DoSpring(spring, object));
	return 1;
}

// P_INTER
////////////

static int lib_pDamageMobj(lua_State *L)
{
	mobj_t *target = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ)), *inflictor = NULL, *source = NULL;
	int32_t damage;
	uint8_t damagetype;
	NOHUD
	INLEVEL
	if (!target)
		return LUA_ErrInvalid(L, "mobj_t");
	if (!lua_isnone(L, 2) && lua_isuserdata(L, 2))
		inflictor = *((mobj_t **)luaL_checkudata(L, 2, META_MOBJ));
	if (!lua_isnone(L, 3) && lua_isuserdata(L, 3))
		source = *((mobj_t **)luaL_checkudata(L, 3, META_MOBJ));
	damage = (int32_t)luaL_optinteger(L, 4, 1);

	if ((damage == 10000) && lua_isnoneornil(L, 5))
	{
		// COMPATIBILITY: Check if the damage type is a time-over kill.
		// If it is, use DMG_TIMEOVER
		uint8_t killdmg = DMG_INSTAKILL;

		if (target->player && (target->player->pflags & PF_NOCONTEST))
			killdmg = DMG_TIMEOVER;

		damagetype = killdmg;
	}
	else if ((damage == 42000) && lua_isnoneornil(L, 5))
		damagetype = DMG_SPECTATOR;
	else
		damagetype = (uint8_t)luaL_optinteger(L, 5, 0);

	if (damagetype == DMG_TIMEOVER)
		S_StopSound(target);

	lua_pushdboolean(L, P_DamageMobj(target, inflictor, source, damage, damagetype));
	return 1;
}

static int lib_pKillMobj(lua_State *L)
{
	mobj_t *target = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ)), *inflictor = NULL, *source = NULL;
	uint8_t damagetype;
	NOHUD
	INLEVEL
	if (!target)
		return LUA_ErrInvalid(L, "mobj_t");
	if (!lua_isnone(L, 2) && lua_isuserdata(L, 2))
		inflictor = *((mobj_t **)luaL_checkudata(L, 2, META_MOBJ));
	if (!lua_isnone(L, 3) && lua_isuserdata(L, 3))
		source = *((mobj_t **)luaL_checkudata(L, 3, META_MOBJ));
	damagetype = (uint8_t)luaL_optinteger(L, 4, 0);
	P_KillMobj(target, inflictor, source, damagetype);
	return 0;
}

static int lib_pPlayerRingBurst(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	int32_t num_rings = (int32_t)luaL_optinteger(L, 2, -1);
	NOHUD
	INLEVEL
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	if (num_rings == -1)
		num_rings = player->rings;
	P_PlayerRingBurst(player, num_rings);
	return 0;
}

static int lib_pPlayRinglossSound(lua_State *L)
{
	mobj_t *source = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	player_t *player = NULL;
	NOHUD
	INLEVEL
	if (!source)
		return LUA_ErrInvalid(L, "mobj_t");
	if (!lua_isnone(L, 2) && lua_isuserdata(L, 2))
	{
		player = *((player_t **)luaL_checkudata(L, 2, META_PLAYER));
		if (!player)
			return LUA_ErrInvalid(L, "player_t");
	}
	if (!player || P_IsLocalPlayer(player))
		P_PlayRinglossSound(source);
	return 0;
}

static int lib_pPlayDeathSound(lua_State *L)
{
	mobj_t *source = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	player_t *player = NULL;
	NOHUD
	INLEVEL
	if (!source)
		return LUA_ErrInvalid(L, "mobj_t");
	if (!lua_isnone(L, 2) && lua_isuserdata(L, 2))
	{
		player = *((player_t **)luaL_checkudata(L, 2, META_PLAYER));
		if (!player)
			return LUA_ErrInvalid(L, "player_t");
	}
	if (!player || P_IsLocalPlayer(player))
		P_PlayDeathSound(source);
	return 0;
}

static int lib_pPlayVictorySound(lua_State *L)
{
	mobj_t *source = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	player_t *player = NULL;
	NOHUD
	INLEVEL
	if (!source)
		return LUA_ErrInvalid(L, "mobj_t");
	if (!lua_isnone(L, 2) && lua_isuserdata(L, 2))
	{
		player = *((player_t **)luaL_checkudata(L, 2, META_PLAYER));
		if (!player)
			return LUA_ErrInvalid(L, "player_t");
	}
	if (!player || P_IsLocalPlayer(player))
		P_PlayVictorySound(source);
	return 0;
}

static int lib_pPlayLivesJingle(lua_State *L)
{
	player_t *player = NULL;
	//NOHUD
	//INLEVEL
	if (!lua_isnone(L, 1) && lua_isuserdata(L, 1))
	{
		player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
		if (!player)
			return LUA_ErrInvalid(L, "player_t");
	}
	P_PlayLivesJingle(player);
	return 0;
}

static int lib_pCanPickupItem(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	uint8_t weapon = (uint8_t)luaL_optinteger(L, 2, 0);
	//HUDSAFE
	INLEVEL
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	lua_pushdboolean(L, P_CanPickupItem(player, weapon));
	return 1;
}

// P_SPEC
////////////

static int lib_pThrust(lua_State *L)
{
	mobj_t *mo = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	angle_t angle = luaL_checkangle(L, 2);
	fixed_t move = luaL_checkfixed(L, 3);
	NOHUD
	INLEVEL
	if (!mo)
		return LUA_ErrInvalid(L, "mobj_t");
	P_Thrust(mo, angle, move);
	return 0;
}

static int lib_pSetMobjStateNF(lua_State *L)
{
	mobj_t *mobj = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	statenum_t state = luaL_checkinteger(L, 2);
	NOHUD
	INLEVEL
	if (!mobj)
		return LUA_ErrInvalid(L, "mobj_t");
	if (state >= NUMSTATES)
		return luaL_error(L, "state %d out of range (0 - %d)", state, NUMSTATES-1);
	if (mobj->player && state == S_NULL)
		return luaL_error(L, "Attempt to remove player mobj with S_NULL.");
	lua_pushdboolean(L, P_SetMobjStateNF(mobj, state));
	return 1;
}

static int lib_pExplodeMissile(lua_State *L)
{
	mobj_t *mo = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	NOHUD
	INLEVEL
	if (!mo)
		return LUA_ErrInvalid(L, "mobj_t");
	P_ExplodeMissile(mo);
	return 0;
}

static int lib_pMobjTouchingSectorSpecial(lua_State *L)
{
	mobj_t *mo = *((mobj_t**)luaL_checkudata(L, 1, META_MOBJ));
	int32_t section = (int32_t)luaL_checkinteger(L, 2);
	int32_t number = (int32_t)luaL_checkinteger(L, 3);
	//HUDSAFE
	INLEVEL
	if (!mo)
		return LUA_ErrInvalid(L, "mobj_t");
	LUA_PushUserdata(L, P_MobjTouchingSectorSpecial(mo, section, number), META_SECTOR);
	return 1;
}

static int lib_pMobjTouchingSectorSpecialFlag(lua_State *L)
{
	mobj_t *mo = *((mobj_t**)luaL_checkudata(L, 1, META_MOBJ));
	sectorspecialflags_t flag = (int32_t)luaL_checkinteger(L, 2);
	//HUDSAFE
	INLEVEL
	if (!mo)
		return LUA_ErrInvalid(L, "mobj_t");
	LUA_PushUserdata(L, P_MobjTouchingSectorSpecialFlag(mo, flag), META_SECTOR);
	return 1;
}

static int lib_pFindLowestFloorSurrounding(lua_State *L)
{
	sector_t *sector = *((sector_t **)luaL_checkudata(L, 1, META_SECTOR));
	//HUDSAFE
	INLEVEL
	if (!sector)
		return LUA_ErrInvalid(L, "sector_t");
	lua_pushfixed(L, P_FindLowestFloorSurrounding(sector));
	return 1;
}

static int lib_pFindHighestFloorSurrounding(lua_State *L)
{
	sector_t *sector = *((sector_t **)luaL_checkudata(L, 1, META_SECTOR));
	//HUDSAFE
	INLEVEL
	if (!sector)
		return LUA_ErrInvalid(L, "sector_t");
	lua_pushfixed(L, P_FindHighestFloorSurrounding(sector));
	return 1;
}

static int lib_pFindNextHighestFloor(lua_State *L)
{
	sector_t *sector = *((sector_t **)luaL_checkudata(L, 1, META_SECTOR));
	fixed_t currentheight;
	//HUDSAFE
	INLEVEL
	if (!sector)
		return LUA_ErrInvalid(L, "sector_t");
	// defaults to floorheight of sector arg
	currentheight = (fixed_t)luaL_optinteger(L, 2, sector->floorheight);
	lua_pushfixed(L, P_FindNextHighestFloor(sector, currentheight));
	return 1;
}

static int lib_pFindNextLowestFloor(lua_State *L)
{
	sector_t *sector = *((sector_t **)luaL_checkudata(L, 1, META_SECTOR));
	fixed_t currentheight;
	//HUDSAFE
	INLEVEL
	if (!sector)
		return LUA_ErrInvalid(L, "sector_t");
	// defaults to floorheight of sector arg
	currentheight = (fixed_t)luaL_optinteger(L, 2, sector->floorheight);
	lua_pushfixed(L, P_FindNextLowestFloor(sector, currentheight));
	return 1;
}

static int lib_pFindLowestCeilingSurrounding(lua_State *L)
{
	sector_t *sector = *((sector_t **)luaL_checkudata(L, 1, META_SECTOR));
	//HUDSAFE
	INLEVEL
	if (!sector)
		return LUA_ErrInvalid(L, "sector_t");
	lua_pushfixed(L, P_FindLowestCeilingSurrounding(sector));
	return 1;
}

static int lib_pFindHighestCeilingSurrounding(lua_State *L)
{
	sector_t *sector = *((sector_t **)luaL_checkudata(L, 1, META_SECTOR));
	//HUDSAFE
	INLEVEL
	if (!sector)
		return LUA_ErrInvalid(L, "sector_t");
	lua_pushfixed(L, P_FindHighestCeilingSurrounding(sector));
	return 1;
}

static int lib_pPlayerTouchingSectorSpecial(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	int32_t section = (int32_t)luaL_checkinteger(L, 2);
	int32_t number = (int32_t)luaL_checkinteger(L, 3);
	//HUDSAFE
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	LUA_PushUserdata(L, P_MobjTouchingSectorSpecial(player->mo, section, number), META_SECTOR);
	return 1;
}

static int lib_pFindSpecialLineFromTag(lua_State *L)
{
	int16_t special = (int16_t)luaL_checkinteger(L, 1);
	int16_t line = (int16_t)luaL_checkinteger(L, 2);
	int32_t start = (int32_t)luaL_optinteger(L, 3, -1);
	NOHUD
	INLEVEL
	lua_pushinteger(L, P_FindSpecialLineFromTag(special, line, start));
	return 1;
}

static int lib_pSwitchWeather(lua_State *L)
{
	int32_t weathernum = (int32_t)luaL_checkinteger(L, 1);
	player_t *user = NULL;
	NOHUD
	INLEVEL
	if (!lua_isnone(L, 2) && lua_isuserdata(L, 2)) // if a player, setup weather for only the player, otherwise setup weather for all players
		user = *((player_t **)luaL_checkudata(L, 2, META_PLAYER));
	if (!user) // global
		globalweather = weathernum;
	if (!user || P_IsLocalPlayer(user))
		P_SwitchWeather(weathernum);
	return 0;
}

static int lib_pLinedefExecute(lua_State *L)
{
	int32_t tag = (int16_t)luaL_checkinteger(L, 1);
	mobj_t *actor = NULL;
	sector_t *caller = NULL;
	NOHUD
	INLEVEL
	if (!lua_isnone(L, 2) && lua_isuserdata(L, 2))
		actor = *((mobj_t **)luaL_checkudata(L, 2, META_MOBJ));
	if (!lua_isnone(L, 3) && lua_isuserdata(L, 3))
		caller = *((sector_t **)luaL_checkudata(L, 3, META_SECTOR));
	P_LinedefExecute(tag, actor, caller);
	return 0;
}

static int lib_pSpawnLightningFlash(lua_State *L)
{
	sector_t *sector = *((sector_t **)luaL_checkudata(L, 1, META_SECTOR));
	NOHUD
	INLEVEL
	if (!sector)
		return LUA_ErrInvalid(L, "sector_t");
	P_SpawnLightningFlash(sector);
	return 0;
}

static int lib_pFadeLight(lua_State *L)
{
	int16_t tag = (int16_t)luaL_checkinteger(L, 1);
	int32_t destvalue = (int32_t)luaL_checkinteger(L, 2);
	int32_t speed = (int32_t)luaL_checkinteger(L, 3);
	dboolean ticbased = lua_optdboolean(L, 4);
	dboolean force = lua_optdboolean(L, 5);
	dboolean relative = lua_optdboolean(L, 6);
	NOHUD
	INLEVEL
	P_FadeLight(tag, destvalue, speed, ticbased, force, relative);
	return 0;
}

static int lib_pThingOnSpecial3DFloor(lua_State *L)
{
	mobj_t *mo = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	NOHUD
	if (!mo)
		return LUA_ErrInvalid(L, "mobj_t");
	LUA_PushUserdata(L, P_ThingOnSpecial3DFloor(mo), META_SECTOR);
	return 1;
}

static int lib_pSetupLevelSky(lua_State *L)
{
	const char *skytexname;

	player_t *user = NULL;
	NOHUD
	INLEVEL

	if (lua_isnumber(L, 1) && lua_compatmode)
	{
		skytexname = luaL_checkstring(L, 1);
		skytexname = va("SKY%s",skytexname);
	}
	else if (lua_isnil(L, 1) && lua_compatmode)
		skytexname = mapheaderinfo[gamemap-1]->skytexture;
	else
		skytexname = luaL_checkstring(L, 1);

	if (!lua_isnone(L, 2) && lua_isuserdata(L, 2)) // if a player, setup sky for only the player, otherwise setup sky for all players
		user = *((player_t **)luaL_checkudata(L, 2, META_PLAYER));
	if (!user) // global
		P_SetupLevelSky(skytexname, true);
	else if (P_IsLocalPlayer(user))
		P_SetupLevelSky(skytexname, false);
	return 0;
}

// Shhh, P_SetSkyboxMobj doesn't actually exist yet.
static int lib_pSetSkyboxMobj(lua_State *L)
{
	int n = lua_gettop(L);
	mobj_t *mo = NULL;
	player_t *user = NULL;
	int w = 0;

	NOHUD
	INLEVEL
	if (!lua_isnil(L,1)) // nil leaves mo as NULL to remove the skybox rendering.
	{
		mo = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ)); // otherwise it is a skybox mobj.
		if (!mo)
			return LUA_ErrInvalid(L, "mobj_t");
	}

	if (n == 1)
		;
	else if (lua_isuserdata(L, 2))
		user = *((player_t **)luaL_checkudata(L, 2, META_PLAYER));
	else if (lua_isnil(L, 2))
		w = 0;
	else if (lua_isdboolean(L, 2))
	{
		if (lua_todboolean(L, 2))
			w = 1;
		else
			w = 0;
	}
	else
		w = luaL_optinteger(L, 2, 0);

	if (n > 2 && lua_isuserdata(L, 3))
	{
		user = *((player_t **)luaL_checkudata(L, 3, META_PLAYER));
		if (!user)
			return LUA_ErrInvalid(L, "player_t");
	}

	if (w > 1 || w < 0)
		return luaL_error(L, "skybox mobj index %d is out of range for P_SetSkyboxMobj argument #2 (expected 0 or 1)", w);

	if (!user || P_IsLocalPlayer(user))
		skyboxmo[w] = mo;
	return 0;
}

// Shhh, neither does P_StartQuake.
static int lib_pStartQuake(lua_State *L)
{
	fixed_t q_intensity = luaL_checkinteger(L, 1);
	uint16_t  q_time = (uint16_t)luaL_checkinteger(L, 2);
	static mappoint_t q_epicenter = {0,0,0};

	NOHUD
	INLEVEL

	// While technically we don't support epicenter and radius,
	// we get their values anyway if they exist.
	// This way when support is added we won't have to change anything.
	if (!lua_isnoneornil(L, 3))
	{
		luaL_checktype(L, 3, LUA_TTABLE);

		lua_getfield(L, 3, "x");
		if (lua_isnil(L, -1))
		{
			lua_pop(L, 1);
			lua_rawgeti(L, 3, 1);
		}
		if (!lua_isnil(L, -1))
			q_epicenter.x = luaL_checkinteger(L, -1);
		else
			q_epicenter.x = 0;
		lua_pop(L, 1);

		lua_getfield(L, 3, "y");
		if (lua_isnil(L, -1))
		{
			lua_pop(L, 1);
			lua_rawgeti(L, 3, 2);
		}
		if (!lua_isnil(L, -1))
			q_epicenter.y = luaL_checkinteger(L, -1);
		else
			q_epicenter.y = 0;
		lua_pop(L, 1);

		lua_getfield(L, 3, "z");
		if (lua_isnil(L, -1))
		{
			lua_pop(L, 1);
			lua_rawgeti(L, 3, 3);
		}
		if (!lua_isnil(L, -1))
			q_epicenter.z = luaL_checkinteger(L, -1);
		else
			q_epicenter.z = 0;
		lua_pop(L, 1);

		quake.epicenter = &q_epicenter;
	}
	else
		quake.epicenter = NULL;
	quake.radius = luaL_optinteger(L, 4, 512*FRACUNIT);

	// These things are actually used in 2.1.
	P_StartQuake(q_intensity, q_time);
	return 0;
}

static int lib_evCrumbleChain(lua_State *L)
{
	sector_t *sec = NULL;
	ffloor_t *rover = NULL;
	NOHUD
	INLEVEL
	if (!lua_isnone(L, 2))
	{
		if (!lua_isnil(L, 1))
		{
			sec = *((sector_t **)luaL_checkudata(L, 1, META_SECTOR));
			if (!sec)
				return LUA_ErrInvalid(L, "sector_t");
		}
		rover = *((ffloor_t **)luaL_checkudata(L, 2, META_FFLOOR));
	}
	else
		rover = *((ffloor_t **)luaL_checkudata(L, 1, META_FFLOOR));
	if (!rover)
		return LUA_ErrInvalid(L, "ffloor_t");
	EV_CrumbleChain(sec, rover);
	return 0;
}

static int lib_evStartCrumble(lua_State *L)
{
	sector_t *sec = *((sector_t **)luaL_checkudata(L, 1, META_SECTOR));
	ffloor_t *rover = *((ffloor_t **)luaL_checkudata(L, 2, META_FFLOOR));
	dboolean floating = lua_optdboolean(L, 3);
	player_t *player = NULL;
	fixed_t origalpha;
	dboolean crumblereturn = lua_optdboolean(L, 6);
	NOHUD
	if (!sec)
		return LUA_ErrInvalid(L, "sector_t");
	if (!rover)
		return LUA_ErrInvalid(L, "ffloor_t");
	if (!lua_isnone(L, 4) && lua_isuserdata(L, 4))
	{
		player = *((player_t **)luaL_checkudata(L, 4, META_PLAYER));
		if (!player)
			return LUA_ErrInvalid(L, "player_t");
	}
	if (!lua_isnone(L,5))
		origalpha = luaL_checkfixed(L, 5);
	else
		origalpha = rover->alpha;
	lua_pushdboolean(L, EV_StartCrumble(sec, rover, floating, player, origalpha, crumblereturn) != 0);
	return 0;
}

// P_SLOPES
////////////

static int lib_pGetZAt(lua_State *L)
{
	fixed_t x = luaL_checkfixed(L, 2);
	fixed_t y = luaL_checkfixed(L, 3);
	//HUDSAFE
	if (lua_isnil(L, 1))
	{
		fixed_t z = luaL_checkfixed(L, 4);
		lua_pushfixed(L, P_GetZAt(NULL, x, y, z));
	}
	else
	{
		pslope_t *slope = *((pslope_t **)luaL_checkudata(L, 1, META_SLOPE));
		lua_pushfixed(L, P_GetSlopeZAt(slope, x, y));
	}

	return 1;
}

static int lib_pButteredSlope(lua_State *L)
{
	mobj_t *mobj = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	NOHUD
	INLEVEL
	if (!mobj)
		return LUA_ErrInvalid(L, "mobj_t");
	P_ButteredSlope(mobj);
	return 0;
}

// R_DEFS
////////////

static int lib_rPointToAngle(lua_State *L)
{
	fixed_t x = luaL_checkfixed(L, 1);
	fixed_t y = luaL_checkfixed(L, 2);
	//HUDSAFE
	lua_pushangle(L, R_PointToAngle(x, y));
	return 1;
}

static int lib_rPointToAnglePlayer(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	fixed_t x = luaL_checkfixed(L, 2);
	fixed_t y = luaL_checkfixed(L, 3);
	//HUDSAFE
	lua_pushangle(L, R_PointToAnglePlayer(player, x, y));
	return 1;
}

static int lib_rPointToAngle2(lua_State *L)
{
	fixed_t px2 = luaL_checkfixed(L, 1);
	fixed_t py2 = luaL_checkfixed(L, 2);
	fixed_t px1 = luaL_checkfixed(L, 3);
	fixed_t py1 = luaL_checkfixed(L, 4);
	//HUDSAFE
	lua_pushangle(L, R_PointToAngle2(px2, py2, px1, py1));
	return 1;
}

static int lib_rPointToDist(lua_State *L)
{
	fixed_t x = luaL_checkfixed(L, 1);
	fixed_t y = luaL_checkfixed(L, 2);
	//HUDSAFE
	lua_pushfixed(L, R_PointToDist(x, y));
	return 1;
}

static int lib_rPointToDist2(lua_State *L)
{
	fixed_t px2 = luaL_checkfixed(L, 1);
	fixed_t py2 = luaL_checkfixed(L, 2);
	fixed_t px1 = luaL_checkfixed(L, 3);
	fixed_t py1 = luaL_checkfixed(L, 4);
	//HUDSAFE
	lua_pushfixed(L, R_PointToDist2(px2, py2, px1, py1));
	return 1;
}

static int lib_rPointInSubsector(lua_State *L)
{
	fixed_t x = luaL_checkfixed(L, 1);
	fixed_t y = luaL_checkfixed(L, 2);
	//HUDSAFE
	INLEVEL
	LUA_PushUserdata(L, R_PointInSubsector(x, y), META_SUBSECTOR);
	return 1;
}

static int lib_rPointInSubsectorOrNil(lua_State *L)
{
	fixed_t x = luaL_checkfixed(L, 1);
	fixed_t y = luaL_checkfixed(L, 2);
	subsector_t *sub = R_PointInSubsectorOrNull(x, y);
	//HUDSAFE
	INLEVEL
	if (sub)
		LUA_PushUserdata(L, sub, META_SUBSECTOR);
	else
		lua_pushnil(L);
	return 1;
}

// R_THINGS
////////////

static int lib_rChar2Frame(lua_State *L)
{
	const char *p = luaL_checkstring(L, 1);
	//HUDSAFE
	lua_pushinteger(L, R_Char2Frame(*p, lua_compatmode)); // first character only
	return 1;
}

static int lib_rFrame2Char(lua_State *L)
{
	uint8_t ch = (uint8_t)luaL_checkinteger(L, 1);
	char c[2] = "";
	//HUDSAFE

	c[0] = R_Frame2Char(ch, lua_compatmode);
	c[1] = 0;

	lua_pushstring(L, c);
	lua_pushinteger(L, c[0]);
	return 2;
}

// R_SetPlayerSkin technically doesn't exist either, although it's basically just SetPlayerSkin and SetPlayerSkinByNum handled in one place for convenience
static int lib_rSetPlayerSkin(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	int32_t i = -1, j = -1;
	NOHUD
	if (!player)
		return LUA_ErrInvalid(L, "player_t");

	j = (player-players);

	if (lua_isnoneornil(L, 2))
		return luaL_error(L, "argument #2 not given (expected number or string)");
	else if (lua_type(L, 2) == LUA_TNUMBER) // skin number
	{
		i = luaL_checkinteger(L, 2);
		if (i < 0 || i >= numskins)
			return luaL_error(L, "skin %d (argument #2) out of range (0 - %d)", i, numskins-1);
	}
	else // skin name
	{
		const char *skinname = luaL_checkstring(L, 2);
		i = R_SkinAvailable(skinname);
		if (i == -1)
			return luaL_error(L, "skin %s (argument 2) is not loaded", skinname);
	}

	if (!R_SkinUsable(j, i))
		return luaL_error(L, "skin %d (argument 2) not usable - check with R_SkinUsable(player_t, skin) first.", i);
	SetPlayerSkinByNum(j, i);
	return 0;
}

static int lib_rSkinUsable(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	int32_t i = -1, j = -1;
	if (player)
		j = (player-players);
	else if (netgame || multiplayer)
		return luaL_error(L, "player_t (argument #1) must be provided in multiplayer games");
	if (lua_isnoneornil(L, 2))
		return luaL_error(L, "argument #2 not given (expected number or string)");
	else if (lua_type(L, 2) == LUA_TNUMBER) // skin number
	{
		i = luaL_checkinteger(L, 2);
		if (i < 0 || i >= numskins)
			return luaL_error(L, "skin %d (argument #2) out of range (0 - %d)", i, numskins-1);
	}
	else // skin name
	{
		const char *skinname = luaL_checkstring(L, 2);
		i = R_SkinAvailable(skinname);
		if (i == -1)
			return luaL_error(L, "skin %s (argument 2) is not loaded", skinname);
	}

	lua_pushdboolean(L, R_SkinUsable(j, i));
	return 1;
}

// R_DATA
////////////

static int lib_rCheckTextureNumForName(lua_State *L)
{
	const char *name = luaL_checkstring(L, 1);
	//HUDSAFE
	int32_t num = R_CheckTextureNumForName(name, TEXTURETYPE_TEXTURE);
	if (num == -1)
		num = R_CheckTextureNumForName(name, TEXTURETYPE_FLAT);
	lua_pushinteger(L, num);
	return 1;
}

static int lib_rTextureNumForName(lua_State *L)
{
	const char *name = luaL_checkstring(L, 1);
	//HUDSAFE
	lua_pushinteger(L, R_TextureNumForNameEx(name, true));
	return 1;
}

// R_DRAW
////////////
static int lib_rGetColorByName(lua_State *L)
{
	const char* colorname = luaL_checkstring(L, 1);
	//HUDSAFE
	lua_pushinteger(L, R_GetColorByName(colorname));
	return 1;
}

static int lib_rGetSuperColorByName(lua_State *L)
{
	const char* colorname = luaL_checkstring(L, 1);
	//HUDSAFE
	lua_pushinteger(L, R_GetSuperColorByName(colorname));
	return 1;
}

// Lua exclusive function, returns the name of a color from the SKINCOLOR_ constant.
// SKINCOLOR_GREEN > "Green" for example
static int lib_rGetNameByColor(lua_State *L)
{
	uint16_t colornum = (uint16_t)luaL_checkinteger(L, 1);
	if (!colornum || colornum >= numskincolors)
		return luaL_error(L, "skincolor %d out of range (1 - %d).", colornum, numskincolors-1);
	lua_pushstring(L, skincolors[colornum].name);
	return 1;
}


// S_SOUND
////////////
static int GetValidSoundOrigin(lua_State *L, void **origin)
{
	const char *type;

	lua_settop(L, 1);
	type = GetUserdataUType(L);

	if (fasticmp(type, "mobj_t"))
	{
		*origin = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
		if (!(*origin))
			return LUA_ErrInvalid(L, "mobj_t");
		return 1;
	}
	else if (fasticmp(type, "sector_t"))
	{
		*origin = *((sector_t **)luaL_checkudata(L, 1, META_SECTOR));
		if (!(*origin))
			return LUA_ErrInvalid(L, "sector_t");

		*origin = &((sector_t *)(*origin))->soundorg;
		return 1;
	}

	return LUA_ErrInvalid(L, "mobj_t/sector_t");
}

static int lib_sStartSound(lua_State *L)
{
	void *origin = NULL;
	sfxenum_t sound_id = luaL_checkinteger(L, 2);
	player_t *player = NULL;
	//NOHUD

	if (sound_id >= NUMSFX)
		return luaL_error(L, "sfx %d out of range (0 - %d)", sound_id, NUMSFX-1);

	if (!lua_isnone(L, 3) && lua_isuserdata(L, 3))
	{
		player = *((player_t **)luaL_checkudata(L, 3, META_PLAYER));
		if (!player)
			return LUA_ErrInvalid(L, "player_t");
	}
	if (!lua_isnil(L, 1))
		if (!GetValidSoundOrigin(L, &origin))
			return 0;
	if (!player || P_IsLocalPlayer(player))
	{
		if (hud_running || hook_cmd_running)
			origin = NULL;	// HUD rendering and CMD building startsound shouldn't have an origin, just remove it instead of having a retarded error.

		S_StartSound(origin, sound_id);
	}
	return 0;
}

static int lib_sStartSoundAtVolume(lua_State *L)
{
	void *origin = NULL;
	sfxenum_t sound_id = luaL_checkinteger(L, 2);
	int32_t volume = (int32_t)luaL_checkinteger(L, 3);
	player_t *player = NULL;
	//NOHUD

	if (sound_id >= NUMSFX)
		return luaL_error(L, "sfx %d out of range (0 - %d)", sound_id, NUMSFX-1);
	if (!lua_isnone(L, 4) && lua_isuserdata(L, 4))
	{
		player = *((player_t **)luaL_checkudata(L, 4, META_PLAYER));
		if (!player)
			return LUA_ErrInvalid(L, "player_t");
	}
	if (!lua_isnil(L, 1))
		if (!GetValidSoundOrigin(L, &origin))
			return LUA_ErrInvalid(L, "mobj_t/sector_t");

	if (!player || P_IsLocalPlayer(player))
		S_StartSoundAtVolume(origin, sound_id, volume);
	return 0;
}

static int lib_sStopSound(lua_State *L)
{
	void *origin = NULL;
	//NOHUD
	if (!GetValidSoundOrigin(L, &origin))
		return LUA_ErrInvalid(L, "mobj_t/sector_t");

	S_StopSound(origin);
	return 0;
}

static int lib_sStopSoundByID(lua_State *L)
{
	void *origin = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	sfxenum_t sound_id = luaL_checkinteger(L, 2);
	NOHUD
	if (!origin)
		return LUA_ErrInvalid(L, "mobj_t");
	S_StopSoundByID(origin, sound_id);
	return 0;
}

static int lib_sChangeMusic(lua_State *L)
{
	uint32_t position, prefadems, fadeinms;

	const char *music_name = luaL_checkstring(L, 1);
	dboolean looping = (dboolean)lua_opttruedboolean(L, 2);
	player_t *player = NULL;
	uint16_t music_flags = 0;

	if (!lua_isnone(L, 3) && lua_isuserdata(L, 3))
	{
		player = *((player_t **)luaL_checkudata(L, 3, META_PLAYER));
		if (!player)
			return LUA_ErrInvalid(L, "player_t");
	}

	music_flags = (uint16_t)luaL_optinteger(L, 4, 0);
	position = (uint32_t)luaL_optinteger(L, 5, 0);
	prefadems = (uint32_t)luaL_optinteger(L, 6, 0);
	fadeinms = (uint32_t)luaL_optinteger(L, 7, 0);

	if (!player || P_IsLocalPlayer(player))
	{
		S_ChangeMusicEx(music_name, music_flags, looping, position, prefadems, fadeinms);
		lua_pushdboolean(L, true);
	}
	else
		lua_pushnil(L);
	return 1;
}

static int lib_sSpeedMusic(lua_State *L)
{
	fixed_t fixedspeed = luaL_checkfixed(L, 1);
	float speed = FIXED_TO_FLOAT(fixedspeed);
	player_t *player = NULL;
	//NOHUD
	if (!lua_isnone(L, 2) && lua_isuserdata(L, 2))
	{
		player = *((player_t **)luaL_checkudata(L, 2, META_PLAYER));
		if (!player)
			return LUA_ErrInvalid(L, "player_t");
	}
	if (!player || P_IsLocalPlayer(player))
		lua_pushdboolean(L, S_SpeedMusic(speed));
	else
		lua_pushnil(L);
	return 1;
}

static int lib_sMusicType(lua_State *L)
{
	player_t *player = NULL;
	//NOHUD
	if (!lua_isnone(L, 1) && lua_isuserdata(L, 1))
	{
		player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
		if (!player)
			return LUA_ErrInvalid(L, "player_t");
	}
	if (!player || P_IsLocalPlayer(player))
		lua_pushinteger(L, S_MusicType());
	else
		lua_pushnil(L);
	return 1;
}

static int lib_sMusicPlaying(lua_State *L)
{
	player_t *player = NULL;
	//NOHUD
	if (!lua_isnone(L, 2) && lua_isuserdata(L, 2))
	{
		player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
		if (!player)
			return LUA_ErrInvalid(L, "player_t");
	}
	if (!player || P_IsLocalPlayer(player))
		lua_pushdboolean(L, S_MusicPlaying());
	else
		lua_pushnil(L);
	return 1;
}

static int lib_sMusicPaused(lua_State *L)
{
	player_t *player = NULL;
	//NOHUD
	if (!lua_isnone(L, 1) && lua_isuserdata(L, 1))
	{
		player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
		if (!player)
			return LUA_ErrInvalid(L, "player_t");
	}
	if (!player || P_IsLocalPlayer(player))
		lua_pushdboolean(L, S_MusicPaused());
	else
		lua_pushnil(L);
	return 1;
}

static int lib_sMusicStopped(lua_State *L)
{
	player_t *player = NULL;
	//NOHUD
	if (!lua_isnone(L, 1) && lua_isuserdata(L, 1))
	{
		player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
		if (!player)
			return LUA_ErrInvalid(L, "player_t");
	}
	if (!player || P_IsLocalPlayer(player))
		lua_pushdboolean(L, S_MusicStopped());
	else
		lua_pushnil(L);
	return 1;
}

static int lib_sMusicName(lua_State *L)
{
	player_t *player = NULL;
	//NOHUD
	if (!lua_isnone(L, 3) && lua_isuserdata(L, 3))
	{
		player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
		if (!player)
			return LUA_ErrInvalid(L, "player_t");
	}
	if (!player || P_IsLocalPlayer(player))
		lua_pushstring(L, S_MusicName());
	else
		lua_pushnil(L);
	return 1;
}

static int lib_sSetMusicLoopPoint(lua_State *L)
{
	uint32_t looppoint = (uint32_t)luaL_checkinteger(L, 1);
	player_t *player = NULL;
	//NOHUD
	if (!lua_isnone(L, 2) && lua_isuserdata(L, 2))
	{
		player = *((player_t **)luaL_checkudata(L, 2, META_PLAYER));
		if (!player)
			return LUA_ErrInvalid(L, "player_t");
	}
	if (!player || P_IsLocalPlayer(player))
		lua_pushdboolean(L, S_SetMusicLoopPoint(looppoint));
	else
		lua_pushnil(L);
	return 1;
}

static int lib_sGetMusicLoopPoint(lua_State *L)
{
	lua_pushinteger(L, S_GetMusicLoopPoint());
	return 1;
}

static int lib_sGetMusicLength(lua_State *L)
{
	lua_pushinteger(L, S_GetMusicLength());
	return 1;
}

static int lib_sGetMusicPosition(lua_State *L)
{
	player_t *player = NULL;
	NOHUD
	if (!lua_isnone(L, 1) && lua_isuserdata(L, 1))
	{
		player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
		if (!player)
			return LUA_ErrInvalid(L, "player_t");
	}
	if (!player || P_IsLocalPlayer(player))
		lua_pushinteger(L, (int)S_GetMusicPosition());
	else
		lua_pushnil(L);
	return 1;
}

static int lib_sSetMusicPosition(lua_State *L)
{
	uint32_t pos = (uint32_t)luaL_checkinteger(L, 1);
	lua_pushdboolean(L, S_SetMusicPosition(pos));
	return 1;
}

static int lib_sStopMusic(lua_State *L)
{
	player_t *player = NULL;
	NOHUD
	if (!lua_isnone(L, 1) && lua_isuserdata(L, 1))
	{
		player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
		if (!player)
			return LUA_ErrInvalid(L, "player_t");
	}
	if (!player || P_IsLocalPlayer(player))
	{
		S_StopMusic();
		lua_pushdboolean(L, true);
	}
	else
		lua_pushnil(L);
	return 1;
}

static int lib_sPauseMusic(lua_State *L)
{
	player_t *player = NULL;
	NOHUD
	if (!lua_isnone(L, 1) && lua_isuserdata(L, 1))
	{
		player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
		if (!player)
			return LUA_ErrInvalid(L, "player_t");
	}
	if (!player || P_IsLocalPlayer(player))
	{
		S_PauseAudio();
		lua_pushdboolean(L, true);
	}
	else
		lua_pushnil(L);
	return 1;
}

static int lib_sResumeMusic(lua_State *L)
{
	player_t *player = NULL;
	NOHUD
	if (!lua_isnone(L, 1) && lua_isuserdata(L, 1))
	{
		player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
		if (!player)
			return LUA_ErrInvalid(L, "player_t");
	}
	if (!player || P_IsLocalPlayer(player))
	{
		S_ResumeAudio();
		lua_pushdboolean(L, true);
	}
	else
		lua_pushnil(L);
	return 1;
}

static int lib_sMusicExists(lua_State *L)
{
	const char *music_name = luaL_checkstring(L, 1);
	NOHUD
	lua_pushdboolean(L, S_MusicExists(music_name));
	return 1;
}

static int lib_sSetInternalMusicVolume(lua_State *L)
{
	uint32_t volume = (uint32_t)luaL_checkinteger(L, 1);
	player_t *player = NULL;
	NOHUD
	if (!lua_isnone(L, 2) && lua_isuserdata(L, 2))
	{
		player = *((player_t **)luaL_checkudata(L, 2, META_PLAYER));
		if (!player)
			return LUA_ErrInvalid(L, "player_t");
	}
	if (!player || P_IsLocalPlayer(player))
	{
		S_SetInternalMusicVolume(volume);
		lua_pushdboolean(L, true);
	}
	else
		lua_pushnil(L);
	return 1;
}

static int lib_sStopFadingMusic(lua_State *L)
{
	player_t *player = NULL;
	NOHUD
	if (!lua_isnone(L, 1) && lua_isuserdata(L, 1))
	{
		player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
		if (!player)
			return LUA_ErrInvalid(L, "player_t");
	}
	if (!player || P_IsLocalPlayer(player))
	{
		S_StopFadingMusic();
		lua_pushdboolean(L, true);
	}
	else
		lua_pushnil(L);
	return 1;
}

static int lib_sFadeMusic(lua_State *L)
{
	uint32_t target_volume = (uint32_t)luaL_checkinteger(L, 1);
	uint32_t ms;
	int32_t source_volume;
	player_t *player = NULL;
	NOHUD
	if (!lua_isnone(L, 3) && lua_isuserdata(L, 3))
	{
		player = *((player_t **)luaL_checkudata(L, 3, META_PLAYER));
		if (!player)
			return LUA_ErrInvalid(L, "player_t");
		ms = (uint32_t)luaL_checkinteger(L, 2);
		source_volume = -1;
	}
	else if (!lua_isnone(L, 4) && lua_isuserdata(L, 4))
	{
		player = *((player_t **)luaL_checkudata(L, 4, META_PLAYER));
		if (!player)
			return LUA_ErrInvalid(L, "player_t");
		source_volume = (int32_t)luaL_checkinteger(L, 2);
		ms = (uint32_t)luaL_checkinteger(L, 3);
	}
	else if (luaL_optinteger(L, 3, INT32_MAX) == INT32_MAX)
	{
		ms = (uint32_t)luaL_checkinteger(L, 2);
		source_volume = -1;
	}
	else
	{
		source_volume = (int32_t)luaL_checkinteger(L, 2);
		ms = (uint32_t)luaL_checkinteger(L, 3);
	}

	NOHUD

	if (!player || P_IsLocalPlayer(player))
		lua_pushdboolean(L, S_FadeMusicFromVolume(target_volume, source_volume, ms));
	else
		lua_pushnil(L);
	return 1;
}

static int lib_sFadeOutStopMusic(lua_State *L)
{
	uint32_t ms = (uint32_t)luaL_checkinteger(L, 1);
	player_t *player = NULL;
	NOHUD
	if (!lua_isnone(L, 2) && lua_isuserdata(L, 2))
	{
		player = *((player_t **)luaL_checkudata(L, 2, META_PLAYER));
		if (!player)
			return LUA_ErrInvalid(L, "player_t");
	}
	if (!player || P_IsLocalPlayer(player))
	{
		lua_pushdboolean(L, S_FadeOutStopMusic(ms));
	}
	else
		lua_pushnil(L);
	return 1;
}

static int lib_sOriginPlaying(lua_State *L)
{
	void *origin = NULL;
	//NOHUD
	INLEVEL
	if (!GetValidSoundOrigin(L, &origin))
		return LUA_ErrInvalid(L, "mobj_t/sector_t");
	lua_pushdboolean(L, S_OriginPlaying(origin));
	return 1;
}

static int lib_sIdPlaying(lua_State *L)
{
	sfxenum_t id = luaL_checkinteger(L, 1);
	//NOHUD
	if (id >= NUMSFX)
		return luaL_error(L, "sfx %d out of range (0 - %d)", id, NUMSFX-1);
	lua_pushdboolean(L, S_IdPlaying(id));
	return 1;
}

static int lib_sSoundPlaying(lua_State *L)
{
	void *origin = NULL;
	sfxenum_t id = luaL_checkinteger(L, 2);
	//NOHUD
	INLEVEL
	if (id >= NUMSFX)
		return luaL_error(L, "sfx %d out of range (0 - %d)", id, NUMSFX-1);
	if (!lua_isnil(L, 1))
		if (!GetValidSoundOrigin(L, &origin))
			return LUA_ErrInvalid(L, "mobj_t/sector_t");
	lua_pushdboolean(L, S_SoundPlaying(origin, id));
	return 1;
}

// This doesn't really exist, but we're providing it as a handy netgame-safe wrapper for stuff that should be locally handled.

static int lib_sStartMusicCaption(lua_State *L)
{
	player_t *player = NULL;
	const char *caption = luaL_checkstring(L, 1);
	uint16_t lifespan = (uint16_t)luaL_checkinteger(L, 2);
	//HUDSAFE
	//INLEVEL

	if (!lua_isnone(L, 3) && lua_isuserdata(L, 3))
	{
		player = *((player_t **)luaL_checkudata(L, 3, META_PLAYER));
		if (!player)
			return LUA_ErrInvalid(L, "player_t");
	}

	if (lifespan && (!player || P_IsLocalPlayer(player)))
	{
		strlcpy(S_sfx[sfx_None].caption, caption, sizeof(S_sfx[sfx_None].caption));
		S_StartCaption(sfx_None, -1, lifespan);
	}
	return 0;
}

static int lib_sShowMusicCredit(lua_State *L)
{
	player_t *player = NULL;
	//HUDSAFE
	if (!lua_isnone(L, 1) && lua_isuserdata(L, 1))
	{
		player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
		if (!player)
			return LUA_ErrInvalid(L, "player_t");
	}
	fixed_t yoffset = luaL_optfixed(L, 2, 0);
	uint16_t animtime = luaL_optinteger(L, 3, 5*TICRATE);
	int32_t snapflags = luaL_optinteger(L, 4, 0);
	if (!player || P_IsLocalPlayer(player))
		S_ShowMusicCredit(yoffset, animtime, snapflags);
	return 0;
}

static int lib_sChangeMusicCreditSettings(lua_State *L)
{
	player_t *player = NULL;
	//HUDSAFE
	if (!lua_isnone(L, 1) && lua_isuserdata(L, 1))
	{
		player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
		if (!player)
			return LUA_ErrInvalid(L, "player_t");
	}
	fixed_t yoffset = luaL_checkfixed(L, 2);
	int32_t snapflags = luaL_checkinteger(L, 3);
	if (!player || P_IsLocalPlayer(player))
		S_ChangeMusicCreditSettings(yoffset, snapflags);
	return 0;
}

static int lib_sStopMusicCredit(lua_State *L)
{
	player_t *player = NULL;
	//HUDSAFE
	if (!lua_isnone(L, 1) && lua_isuserdata(L, 1))
	{
		player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
		if (!player)
			return LUA_ErrInvalid(L, "player_t");
	}

	if (!player || P_IsLocalPlayer(player))
		S_StopMusicCredit();
	return 0;
}

// G_GAME
////////////

// Copypasted from lib_cvRegisterVar :]
static int lib_gAddGametype(lua_State *L)
{
	const char *k;
	lua_Integer i;

	gametype_t *newgametype = NULL;

	const char *gtname = NULL;
	const char *gtconst = NULL;
	uint32_t newgtrules = 0;
	uint32_t newgttol = 0;
	int32_t newgtpointlimit = 0;
	int32_t newgttimelimit = 0;
	int32_t newgtcolor = V_YELLOWMAP;
	uint8_t newgtinttype = 0;

	luaL_checktype(L, 1, LUA_TTABLE);
	lua_settop(L, 1); // Clear out all other possible arguments, leaving only the first one.

	if (!lua_lumploading)
		return luaL_error(L, "This function cannot be called from within a hook or coroutine!");

	// Ran out of gametype slots
	if (numgametypes == GT_LASTFREESLOT)
	{
		I_Error("Out of Gametype Freeslots while allocating \"%s\"\nLoad less addons to fix this.", gtname);
	}

#define FIELDERROR(f, e) luaL_error(L, "bad value for " LUA_QL(f) " in table passed to " LUA_QL("G_AddGametype") " (%s)", e);
#define TYPEERROR(f, t) FIELDERROR(f, va("%s expected, got %s", lua_typename(L, t), luaL_typename(L, -1)))

	lua_pushnil(L);
	while (lua_next(L, 1)) {
		// stack: gametype table, key/index, value
		//               1            2        3
		i = 0;
		k = NULL;
		if (lua_isnumber(L, 2))
			i = lua_tointeger(L, 2);
		else if (lua_isstring(L, 2))
			k = lua_tostring(L, 2);

		// Sorry, no gametype rules as key names.
		if (i == 1 || (k && fasticmp(k, "name"))) {
			if (!lua_isstring(L, 3))
				TYPEERROR("name", LUA_TSTRING)
			gtname = Z_StrDup(lua_tostring(L, 3));
		} else if (i == 2 || (k && fasticmp(k, "identifier"))) {
			if (!lua_isstring(L, 3))
				TYPEERROR("identifier", LUA_TSTRING)
			gtconst = Z_StrDup(lua_tostring(L, 3));
		} else if (i == 3 || (k && fasticmp(k, "rules"))) {
			if (!lua_isnumber(L, 3))
				TYPEERROR("rules", LUA_TNUMBER)
			newgtrules = (uint32_t)lua_tointeger(L, 3);
		} else if (i == 4 || (k && fasticmp(k, "typeoflevel"))) {
			if (!lua_isnumber(L, 3))
				TYPEERROR("typeoflevel", LUA_TNUMBER)
			newgttol = (uint32_t)lua_tointeger(L, 3);
		} else if (i == 5 || (k && fasticmp(k, "intermissiontype"))) {
			if (!lua_isnumber(L, 3))
				TYPEERROR("intermissiontype", LUA_TNUMBER)
			newgtinttype = (int)lua_tointeger(L, 3);
		} else if (i == 6 || (k && fasticmp(k, "defaultpointlimit"))) {
			if (!lua_isnumber(L, 3))
				TYPEERROR("defaultpointlimit", LUA_TNUMBER)
			newgtpointlimit = (int32_t)lua_tointeger(L, 3);
		} else if (i == 7 || (k && fasticmp(k, "defaulttimelimit"))) {
			if (!lua_isnumber(L, 3))
				TYPEERROR("defaulttimelimit", LUA_TNUMBER)
			newgttimelimit = (int32_t)lua_tointeger(L, 3);
		} else if (i == 8 || (k && fasticmp(k, "menucolor"))) {
			if (!lua_isnumber(L, 3))
				TYPEERROR("menucolor", LUA_TNUMBER)
			{
				int32_t color = (int32_t)lua_tointeger(L, 3);
				// Mask out other flags so they aren't passed into menu code.
				newgtcolor = color & V_CHARCOLORMASK; // V_
			}
		}
		lua_pop(L, 1);
	}

#undef FIELDERROR
#undef TYPEERROR

	// pop gametype table
	lua_pop(L, 1);

	// Set defaults
	if (gtname == NULL)
		gtname = Z_StrDup("Unnamed gametype");

	// Add the new gametype
	newgametype = Z_Calloc(sizeof (gametype_t), PU_STATIC, NULL);
	if (!newgametype)
	{
		I_Error("Out of memory allocating gametype \"%s\"", gtname);
	}

	// Write the constant name.
	if (gtconst == NULL)
		gtconst = gtname;

	newgametype->name = gtname;
	newgametype->rules = newgtrules;
	newgametype->constant = G_PrepareGametypeConstant(gtconst);
	newgametype->tol = newgttol;
	newgametype->intermission = newgtinttype;
	newgametype->pointlimit = newgtpointlimit;
	newgametype->timelimit = newgttimelimit;
	newgametype->color = newgtcolor;

	gametypes[numgametypes++] = newgametype;

	// Update gametype_cons_t accordingly.
	G_UpdateGametypeSelections();

	// done
	CONS_Printf("Added gametype %s\n", gtname);
	return 0;
}

static int Lcheckmapnumber (lua_State *L, int idx, const char *fun)
{
	if (G_GamestateUsesLevel())
		return luaL_optinteger(L, idx, lua_compatmode ? G_NativeMapToKart(gamemap) : gamemap);
	else
	{
		if (lua_isnoneornil(L, idx))
		{
			return luaL_error(L,
					"%s can only be used without a parameter while in a level.",
					fun
			);
		}
		else
			return luaL_checkinteger(L, idx);
	}
}

static int lib_gBuildMapName(lua_State *L)
{
	mapnum_t map = Lcheckmapnumber(L, 1, "G_BuildMapName");

	if (lua_compatmode && map == 0) // v1 has undefined behaviour if you return 0, who knew.
	{
		map = G_NativeMapToKart(G_RandMap(G_TOLFlag(gametype), gamemap-1, true, 0, false, NULL) + 1);
	}

	//HUDSAFE
	lua_pushstring(L, G_BuildMapName(lua_compatmode ? G_KartMapToNative(map) : map));
	return 1;
}

static int lib_gBuildMapTitle(lua_State *L)
{
	mapnum_t map = Lcheckmapnumber(L, 1, "G_BuildMapTitle");
	char *name;
	if (map < 1 || map > nummapheaders)
	{
		return luaL_error(L,
				"map ID %d out of range (1 - %d)",
				map,
				nummapheaders
		);
	}
	name = G_BuildMapTitle(map);
	lua_pushstring(L, name);
	Z_Free(name);
	return 1;
}

static void
Lpushdim (lua_State *L, int c, struct searchdim *v)
{
	int i;
	lua_createtable(L, c, 0);/* I guess narr is numeric indices??? */
	for (i = 0; i < c; ++i)
	{
		lua_createtable(L, 0, 2);/* and hashed indices (field)... */
			lua_pushnumber(L, v[i].pos);
			lua_setfield(L, -2, "pos");

			lua_pushnumber(L, v[i].siz);
			lua_setfield(L, -2, "siz");
		lua_rawseti(L, -2, 1 + i);
	}
}

/*
I decided to make this return a table because userdata
is scary and tables let the user set their own fields.
*/
/*
Returns:

[1] => map number
[2] => map title
[3] => search frequency table

The frequency table is unsorted. It has the following format:

{
	['mapnum'],

	['matchd'] => matches in map title string
	['keywhd'] => matches in map keywords

	The above two tables have the following format:

	{
		['pos'] => offset from start of string
		['siz'] => length of match
	}...

	['total'] => the total matches
}...
*/
static int lib_gFindMap(lua_State *L)
{
	const char *query = luaL_checkstring(L, 1);

	mapnum_t map;
	char *realname;
	int32_t frc;
	mapsearchfreq_t *frv;

	int32_t i;

	map = G_FindMap(query, &realname, &frv, &frc);

	lua_settop(L, 0);

	lua_pushnumber(L, map);
	lua_pushstring(L, realname);

	lua_createtable(L, frc, 0);
	for (i = 0; i < frc; ++i)
	{
		lua_createtable(L, 0, 4);
			lua_pushnumber(L, frv[i].mapnum);
			lua_setfield(L, -2, "mapnum");

			Lpushdim(L, frv[i].matchc, frv[i].matchd);
			lua_setfield(L, -2, "matchd");

			Lpushdim(L, frv[i].keywhc, frv[i].keywhd);
			lua_setfield(L, -2, "keywhd");

			lua_pushnumber(L, frv[i].total);
			lua_setfield(L, -2, "total");
		lua_rawseti(L, -2, 1 + i);
	}

	G_FreeMapSearch(frv, frc);
	Z_Free(realname);

	return 3;
}

/*
Returns:

[1] => map number
[2] => map title
*/
static int lib_gFindMapByNameOrCode(lua_State *L)
{
	const char *query = luaL_checkstring(L, 1);
	mapnum_t map;
	char *realname;
	map = G_FindMapByNameOrCode(query, &realname);
	lua_pushnumber(L, map);
	if (map)
	{
		lua_pushstring(L, realname);
		Z_Free(realname);
		return 2;
	}
	else
		return 1;
}

static int lib_gDoReborn(lua_State *L)
{
	int32_t playernum = luaL_checkinteger(L, 1);
	NOHUD
	INLEVEL
	if (playernum >= MAXPLAYERS)
		return luaL_error(L, "playernum %d out of range (0 - %d)", playernum, MAXPLAYERS-1);
	G_DoReborn(playernum);
	return 0;
}

// Another Lua function that doesn't actually exist!
// Sets nextmapoverride & skipstats without instantly ending the level, for instances where other
// sources should be exiting the level, like normal signposts.
static int lib_gSetCustomExitVars(lua_State* L)
{
	int n = lua_gettop(L);  // Num arguments
	NOHUD
	INLEVEL

	// LUA EXTENSION: Custom exit like support
	// Supported:
	//	G_SetCustomExitVars();			[reset to defaults]
	//	G_SetCustomExitVars(int)		[nextmap override only]
	//	G_SetCustomExitVars(nil, int)	[skipstats only]
	//	G_SetCustomExitVars(int, int)	[both of the above]

	mapnum_t potential_override = 0;

	nextmapoverride = 0;
	skipstats = 0;

	if (n >= 1)
	{
		potential_override = luaL_optinteger(L, 1, 0);
		skipstats = (int16_t)luaL_optinteger(L, 2, 0);
		if (lua_compatmode && potential_override)
			potential_override = G_KartMapToNative(potential_override);

		if (potential_override != 0)
		{
			// Prevent crashes by making sure the override is safe
			if (!G_MapIsSafe(potential_override))
			{
				// Override is very definitively NOT safe; error out
				return luaL_error(
				    L,
				    "provided map override %d is invalid (nummapheaders = %d)",
				    potential_override,
				    nummapheaders);
			}
			else
			{
				// Allow the override
				nextmapoverride = potential_override;
			}
		}
	}

	return 0;
}

static int lib_gExitLevel(lua_State *L)
{
	int n = lua_gettop(L); // Num arguments
	NOHUD
	// Moved this bit to G_SetCustomExitVars
	if (n >= 1) // Don't run the reset to defaults option
		lib_gSetCustomExitVars(L);
	G_BeginLevelExit();
	G_FinishExitLevel();
	return 0;
}

static int lib_gIsSpecialStage(lua_State *L)
{
	mapnum_t mapnum = Lcheckmapnumber(L, 1, "G_IsSpecialStage");
	//HUDSAFE
	INLEVEL
	lua_pushdboolean(L, G_IsSpecialStage(lua_compatmode ? G_KartMapToNative(mapnum) : mapnum));
	return 1;
}

static int lib_gGametypeUsesLives(lua_State *L)
{
	//HUDSAFE
	INLEVEL
	lua_pushdboolean(L, G_GametypeUsesLives());
	return 1;
}

static int lib_gGametypeHasTeams(lua_State *L)
{
	//HUDSAFE
	INLEVEL
	lua_pushdboolean(L, G_GametypeHasTeams());
	return 1;
}

static int lib_gGametypeHasSpectators(lua_State *L)
{
	//HUDSAFE
	INLEVEL
	lua_pushdboolean(L, G_GametypeHasSpectators());
	return 1;
}

static int lib_gBattleGametype(lua_State *L)
{
	//HUDSAFE
	lua_pushdboolean(L, (gametype == GT_BATTLE));
	return 1;
}

static int lib_gRaceGametype(lua_State *L)
{
	//HUDSAFE
	lua_pushdboolean(L, (gametype == GT_RACE));
	return 1;
}

static int lib_gTicsToHours(lua_State *L)
{
	tic_t rtic = luaL_checkinteger(L, 1);
	//HUDSAFE
	lua_pushinteger(L, G_TicsToHours(rtic));
	return 1;
}

static int lib_gTicsToMinutes(lua_State *L)
{
	tic_t rtic = luaL_checkinteger(L, 1);
	dboolean rfull = lua_optdboolean(L, 2);
	//HUDSAFE
	lua_pushinteger(L, G_TicsToMinutes(rtic, rfull));
	return 1;
}

static int lib_gTicsToSeconds(lua_State *L)
{
	tic_t rtic = luaL_checkinteger(L, 1);
	//HUDSAFE
	lua_pushinteger(L, G_TicsToSeconds(rtic));
	return 1;
}

static int lib_gTicsToCentiseconds(lua_State *L)
{
	tic_t rtic = luaL_checkinteger(L, 1);
	//HUDSAFE
	lua_pushinteger(L, G_TicsToCentiseconds(rtic));
	return 1;
}

static int lib_gTicsToMilliseconds(lua_State *L)
{
	tic_t rtic = luaL_checkinteger(L, 1);
	//HUDSAFE
	lua_pushinteger(L, G_TicsToMilliseconds(rtic));
	return 1;
}

static int lib_gMapIsSafe(lua_State *L)
{
	mapnum_t map = luaL_checkinteger(L, 1);

	if (lua_compatmode && map)
		map = G_KartMapToNative(map);

	//HUDSAFE
	lua_pushdboolean(L, G_MapIsSafe(map));
	return 1;
}

// K_KART
////////////

// Seriously, why weren't those exposed before?
static int lib_kAttackSound(lua_State *L)
{
	mobj_t *mobj = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	NOHUD
	if (!mobj->player)
		return luaL_error(L, "K_PlayAttackTaunt: mobj_t isn't a player object.");	//Nothing bad would happen if we let it run the func, but telling why it ain't doing anything is helpful.
	K_PlayAttackTaunt(mobj);
	return 0;
}

static int lib_kBoostSound(lua_State *L)
{
	mobj_t *mobj = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	NOHUD
	if (!mobj->player)
		return luaL_error(L, "K_PlayBoostTaunt: mobj_t isn't a player object.");	//Nothing bad would happen if we let it run the func, but telling why it ain't doing anything is helpful.
	K_PlayBoostTaunt(mobj);
	return 0;
}

static int lib_kOvertakeSound(lua_State *L)
{
	mobj_t *mobj = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	NOHUD
	if (!mobj->player)
		return luaL_error(L, "K_PlayOvertakeSound: mobj_t isn't a player object.");	//Nothing bad would happen if we let it run the func, but telling why it ain't doing anything is helpful.
	K_PlayOvertakeSound(mobj);
	return 0;
}

static int lib_kPainSound(lua_State *L)
{
	mobj_t *mobj = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	mobj_t *other = NULL;
	NOHUD
	if (!mobj->player)
		return luaL_error(L, "K_PlayPainSound: mobj_t isn't a player object.");	//Nothing bad would happen if we let it run the func, but telling why it ain't doing anything is helpful.
	if (!lua_isnone(L, 2) && lua_isuserdata(L, 2))
		other = *((mobj_t **)luaL_checkudata(L, 2, META_MOBJ));
	K_PlayPainSound(mobj, other);
	return 0;
}

static int lib_kHitEmSound(lua_State *L)
{
	mobj_t *mobj = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	mobj_t *other = NULL;
	NOHUD
	if (!mobj->player)
		return luaL_error(L, "K_PlayHitEmSound: mobj_t isn't a player object.");	//Nothing bad would happen if we let it run the func, but telling why it ain't doing anything is helpful.
	if (!lua_isnone(L, 2) && lua_isuserdata(L, 2))
		other = *((mobj_t **)luaL_checkudata(L, 2, META_MOBJ));
	K_PlayHitEmSound(mobj, other);
	return 0;
}

static int lib_kTryHurtSoundExchange(lua_State *L)
{
	mobj_t *victim = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	mobj_t *attacker = *((mobj_t **)luaL_checkudata(L, 2, META_MOBJ));
	NOHUD
	if (!victim->player)
		return luaL_error(L, "K_TryHurtSoundExchange: mobj_t isn't a player object.");	//Nothing bad would happen if we let it run the func, but telling why it ain't doing anything is helpful.
	K_TryHurtSoundExchange(victim, attacker);
	return 0;
}

static int lib_kGloatSound(lua_State *L)
{
	mobj_t *mobj = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	NOHUD
	if (!mobj->player)
		return luaL_error(L, "K_PlayPowerGloatSound: mobj_t isn't a player object.");	//Nothing bad would happen if we let it run the func, but telling why it ain't doing anything is helpful.
	K_PlayPowerGloatSound(mobj);
	return 0;
}

static int lib_kLossSound(lua_State *L)
{
	mobj_t *mobj = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));	// let's require a mobj for consistency with the other functions
	kartvoice_t *mo_voice;
	sfxenum_t sfx_id;
	NOHUD
	if (!mobj->player)
		return luaL_error(L, "K_PlayLossSound: mobj_t isn't a player object.");

	mo_voice = P_GetMobjVoice(mobj);

	if (!mo_voice)
	{
		// No voice!
		return 0;
	}

	sfx_id = mo_voice->lose;
	S_StartSound(mobj, sfx_id);
	return 0;
}

// Note: Pain, Death and Victory are already exposed.

static int lib_kIsPlayerLosing(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	//HUDSAFE
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	lua_pushdboolean(L, K_IsPlayerLosing(player));
	return 1;
}

static int lib_kIsPlayerWanted(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	//HUDSAFE
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	lua_pushdboolean(L, K_IsPlayerWanted(player));
	return 1;
}

static int lib_kKartBouncing(lua_State *L)
{
	mobj_t *mobj1 = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	mobj_t *mobj2 = *((mobj_t **)luaL_checkudata(L, 2, META_MOBJ));
	dboolean bounce = lua_optdboolean(L, 3);
	dboolean solid = lua_optdboolean(L, 4);
	NOHUD
	if (!mobj1)
		return LUA_ErrInvalid(L, "mobj_t");
	if (!mobj2)
		return LUA_ErrInvalid(L, "mobj_t");
	K_KartBouncing(mobj1, mobj2, bounce, solid);
	return 0;
}

static int lib_kMatchGenericExtraFlags(lua_State *L)
{
	mobj_t *mo = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	mobj_t *master = *((mobj_t **)luaL_checkudata(L, 2, META_MOBJ));
	NOHUD
	if (!mo)
		return LUA_ErrInvalid(L, "mobj_t");
	if (!master)
		return LUA_ErrInvalid(L, "mobj_t");
	K_MatchGenericExtraFlags(mo, master);
	return 0;
}

static int lib_kGenericExtraFlagsNoZAdjust(lua_State *L)
{
	mobj_t *mo = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	mobj_t *master = *((mobj_t **)luaL_checkudata(L, 2, META_MOBJ));
	NOHUD
	if (!mo)
		return LUA_ErrInvalid(L, "mobj_t");
	if (!master)
		return LUA_ErrInvalid(L, "mobj_t");
	K_GenericExtraFlagsNoZAdjust(mo, master);
	return 0;
}

static int lib_kSpawnDashDustRelease(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	NOHUD
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	K_SpawnDashDustRelease(player, true);
	return 0;
}

static int lib_kSpawnNormalSpeedLines(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	NOHUD
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	K_SpawnNormalSpeedLines(player);
	return 0;
}

static int lib_kSpawnBumpEffect(lua_State *L)
{
	mobj_t *mobj = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	NOHUD
	if (!mobj)
		return LUA_ErrInvalid(L, "mobj_t");
	K_SpawnBumpEffect(mobj);
	return 0;
}

static int lib_kDoInstashield(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	NOHUD
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	K_DoInstashield(player);
	return 0;
}

static int lib_kSpawnBattlePoints(lua_State *L)
{
	player_t *source = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	player_t *victim = NULL;
	uint8_t amount = (uint8_t)luaL_checkinteger(L, 3);
	NOHUD
	if (!source)
		return LUA_ErrInvalid(L, "player_t");
	if (!lua_isnone(L, 2) && lua_isuserdata(L, 2))
		victim = *((player_t **)luaL_checkudata(L, 2, META_PLAYER));
	K_SpawnBattlePoints(source, victim, amount);
	return 0;
}

static int lib_kSpinPlayer(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	mobj_t *inflictor = NULL;
	mobj_t *source = NULL;
	int32_t type = (int32_t)luaL_optinteger(L, 3, 0);
	NOHUD
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	if (!lua_isnone(L, 2) && lua_isuserdata(L, 2))
		inflictor = *((mobj_t **)luaL_checkudata(L, 2, META_MOBJ));
	if (!lua_isnone(L, 3) && lua_isuserdata(L, 3))
		source = *((mobj_t **)luaL_checkudata(L, 3, META_MOBJ));
	K_SpinPlayer(player, inflictor, source, type);
	return 0;
}

static int lib_kExplodePlayer(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	mobj_t *source = NULL;
	mobj_t *inflictor = NULL;
	NOHUD
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	if (!lua_isnone(L, 2) && lua_isuserdata(L, 2))
		inflictor = *((mobj_t **)luaL_checkudata(L, 2, META_MOBJ));
	if (!lua_isnone(L, 3) && lua_isuserdata(L, 3))
		source = *((mobj_t **)luaL_checkudata(L, 3, META_MOBJ));
	lua_pushinteger(L, K_ExplodePlayer(player, inflictor, source));
	return 1;
}

static int lib_kFlipPlayer(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	mobj_t *source = NULL;
	mobj_t *inflictor = NULL;
	NOHUD
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	if (!lua_isnone(L, 2) && lua_isuserdata(L, 2))
		inflictor = *((mobj_t **)luaL_checkudata(L, 2, META_MOBJ));
	if (!lua_isnone(L, 3) && lua_isuserdata(L, 3))
		source = *((mobj_t **)luaL_checkudata(L, 3, META_MOBJ));
	K_FlipPlayer(player, inflictor, source);
	return 0;
}

static int lib_kTumblePlayer(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	mobj_t *source = NULL;
	mobj_t *inflictor = NULL;
	NOHUD
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	if (!lua_isnone(L, 2) && lua_isuserdata(L, 2))
		inflictor = *((mobj_t **)luaL_checkudata(L, 2, META_MOBJ));
	if (!lua_isnone(L, 3) && lua_isuserdata(L, 3))
		source = *((mobj_t **)luaL_checkudata(L, 3, META_MOBJ));
	K_TumblePlayer(player, inflictor, source);
	return 0;
}

static int lib_kResetCrashData(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));

	NOHUD
	if (!player)
		return LUA_ErrInvalid(L, "player_t");

	K_ResetCrashData(player);
	return 0;
}

static int lib_kResetCrashOffsetData(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));

	NOHUD
	if (!player)
		return LUA_ErrInvalid(L, "player_t");

	K_ResetCrashOffsetData(player);
	return 0;
}

static int lib_kTakeBumpersFromPlayer(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	player_t *victim = *((player_t **)luaL_checkudata(L, 2, META_PLAYER));
	uint8_t amount = (uint8_t)luaL_optinteger(L, 3, 1);
	NOHUD
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	if (!victim)
		return LUA_ErrInvalid(L, "player_t");
	K_TakeBumpersFromPlayer(player, victim, amount);
	return 0;
}

static int lib_kGetOrbiJawzDamage(lua_State *L)
{
	lua_pushinteger(L, K_GetOrbiJawzDamage());
	return 1;
}

static int lib_kSpawnKartExplosion(lua_State *L)
{
	fixed_t x = luaL_checkfixed(L, 1);
	fixed_t y = luaL_checkfixed(L, 2);
	fixed_t z = luaL_checkfixed(L, 3);
	fixed_t radius = (fixed_t)luaL_optinteger(L, 4, 32*FRACUNIT);
	int32_t number = (int32_t)luaL_optinteger(L, 5, 32);
	mobjtype_t type = luaL_optinteger(L, 6, MT_MINEEXPLOSION);
	angle_t rotangle = luaL_optinteger(L, 7, 0);
	dboolean spawncenter = lua_opttruedboolean(L, 8);
	dboolean ghostit = lua_optdboolean(L, 9);
	mobj_t *source = NULL;
	NOHUD
	if (!lua_isnone(L, 10) && lua_isuserdata(L, 10))
		source = *((mobj_t **)luaL_checkudata(L, 10, META_MOBJ));
	K_SpawnKartExplosion(x, y, z, radius, number, type, rotangle, spawncenter, ghostit, source);
	return 0;
}

static int lib_kSpawnMineExplosion(lua_State *L)
{
	mobj_t *source = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	uint8_t color = (uint8_t)luaL_optinteger(L, 2, SKINCOLOR_KETCHUP);
	NOHUD
	if (!source)
		return LUA_ErrInvalid(L, "mobj_t");
	K_SpawnMineExplosion(source, color);
	return 0;
}

static int lib_kSpawnBoostTrail(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	NOHUD
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	K_SpawnBoostTrail(player);
	return 0;
}

static int lib_kSpawnSparkleTrail(lua_State *L)
{
	mobj_t *mo = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	NOHUD
	if (!mo)
		return LUA_ErrInvalid(L, "mobj_t");
	K_SpawnSparkleTrail(mo);
	return 0;
}

static int lib_kSpawnWipeoutTrail(lua_State *L)
{
	mobj_t *mo = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	NOHUD
	if (!mo)
		return LUA_ErrInvalid(L, "mobj_t");
	K_SpawnWipeoutTrail(mo, false);
	return 0;
}

static int lib_kDriftDustHandling(lua_State *L)
{
	mobj_t *spawner = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	NOHUD
	if (!spawner)
		return LUA_ErrInvalid(L, "mobj_t");
	K_DriftDustHandling(spawner);
	return 0;
}

static int lib_kThrowKartItem(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	dboolean missile = luaL_checkdboolean(L, 2);
	mobjtype_t thingtype = luaL_checkinteger(L, 3);
	int32_t defaultdir = luaL_optinteger(L, 4, 0);
	int32_t altthrow = luaL_optinteger(L, 5, 0);
	NOHUD
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	mobj_t *mo = K_ThrowKartItem(player, missile, thingtype, defaultdir, altthrow);
	if (mo)
		LUA_PushUserdata(L, mo, META_MOBJ);
	else
		lua_pushnil(L);

	return 1;
}

static int lib_kDoSneaker(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	int32_t type = luaL_optinteger(L, 2, 0);
	NOHUD
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	K_DoSneaker(player, type);
	return 0;
}

static int lib_kDoPogoSpring(lua_State *L)
{
	mobj_t *mo = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	fixed_t vertispeed = (fixed_t)luaL_optinteger(L, 2, 0);
	uint8_t sound = (uint8_t)luaL_optinteger(L, 3, 1);
	NOHUD
	if (!mo)
		return LUA_ErrInvalid(L, "mobj_t");
	K_DoPogoSpring(mo, vertispeed, sound);
	return 0;
}

static int lib_kKillBananaChain(lua_State *L)
{
	mobj_t *banana = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	mobj_t *inflictor = NULL;
	mobj_t *source = NULL;
	NOHUD
	if (!banana)
		return LUA_ErrInvalid(L, "mobj_t");
	if (!lua_isnone(L, 2) && lua_isuserdata(L, 2))
		inflictor = *((mobj_t **)luaL_checkudata(L, 2, META_MOBJ));
	if (!lua_isnone(L, 3) && lua_isuserdata(L, 3))
		source = *((mobj_t **)luaL_checkudata(L, 3, META_MOBJ));
	K_KillBananaChain(banana, inflictor, source);
	return 0;
}

static int lib_kRepairOrbitChain(lua_State *L)
{
	mobj_t *orbit = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	NOHUD
	if (!orbit)
		return LUA_ErrInvalid(L, "mobj_t");
	K_RepairOrbitChain(orbit);
	return 0;
}

static int lib_kFindJawzTarget(lua_State *L)
{
	mobj_t *actor = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	player_t *source = *((player_t **)luaL_checkudata(L, 2, META_PLAYER));
	//HUDSAFE
	if (!actor)
		return LUA_ErrInvalid(L, "mobj_t");
	if (!source)
		return LUA_ErrInvalid(L, "player_t");
	LUA_PushUserdata(L, K_FindJawzTarget(actor, source), META_PLAYER);
	return 1;
}

static int lib_kGetKartDriftSparkValue(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	//HUDSAFE
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	lua_pushinteger(L, K_GetKartDriftSparkValue(player));
	return 1;
}

static int lib_kKartUpdatePosition(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	NOHUD
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	K_KartUpdatePosition(player);
	return 0;
}

static int lib_kUpdateHnextList(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	dboolean clean = lua_optdboolean(L, 2);
	NOHUD
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	K_UpdateHnextList(player, clean);
	return 0;
}

static int lib_kSetItemOut(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	kartitemtype_e itemtype = luaL_checkint(L, 2);
	itemflags_t flags = luaL_optint(L, 3, 0);
	NOHUD
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	K_SetItemOut(player, itemtype, flags);
	return 0;
}

static int lib_kUnsetItemOut(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	NOHUD
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	K_UnsetItemOut(player);
	return 0;
}

static int lib_kUnsetItemOutEx(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	itemflags_t itemflags = luaL_checkint(L, 2);
	NOHUD
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	K_UnsetItemOutEx(player, itemflags);
	return 0;
}

static int lib_kDropItems(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	NOHUD
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	K_DropItems(player);
	return 0;
}

static int lib_kStripItems(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	NOHUD
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	K_StripItems(player);
	return 0;
}

static int lib_kStripOther(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	NOHUD
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	K_StripOther(player);
	return 0;
}

static int lib_kMomentumToFacing(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	NOHUD
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	K_MomentumToFacing(player);
	return 0;
}

static int lib_kGetKartSpeed(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	dboolean doboostpower = lua_optdboolean(L, 2);
	dboolean dorubberbanding = lua_optdboolean(L, 3);
	//HUDSAFE
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	lua_pushfixed(L, K_GetKartSpeed(player, doboostpower, dorubberbanding));
	return 1;
}

static int lib_kGetKartAccel(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	//HUDSAFE
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	lua_pushfixed(L, K_GetKartAccel(player));
	return 1;
}

static int lib_kGetKartFlashing(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	//HUDSAFE
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	lua_pushinteger(L, K_GetKartFlashing(player));
	return 1;
}

static int lib_kGetSpeedPercentage(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	//HUDSAFE
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	lua_pushinteger(L, K_GetSpeedPercentage(player));
	return 1;
}

static int lib_kGetKartBaseRingPower(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	dboolean boosted = lua_optdboolean(L, 2);
	//HUDSAFE
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	lua_pushinteger(L, K_GetKartBaseRingPower(player, boosted));
	return 1;
}

static int lib_kGetKartRingCap(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	//HUDSAFE
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	lua_pushinteger(L, K_GetKartRingCap(player));
	return 1;
}

static int lib_kIsPlayerRingBurnt(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	uint16_t ringPower = luaL_checkinteger(L, 2);
	//HUDSAFE
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	lua_pushdboolean(L, K_IsPlayerRingBurnt(player, ringPower));
	return 1;
}

static int lib_kGetKartRingPower(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	dboolean boosted = lua_optdboolean(L, 2);
	//HUDSAFE
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	lua_pushinteger(L, K_GetKartRingPower(player, boosted));
	return 1;
}

static int lib_kKartPlayerBumpyRoad(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	NOHUD
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	K_KartPlayerBumpyRoad(player);
	return 0;
}

static int lib_kHandleAirdrag(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	dboolean onground = lua_optdboolean(L, 2);
	fixed_t speed = luaL_checkfixed(L, 3);
	fixed_t airdrag = luaL_checkfixed(L, 4);
	NOHUD
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	lua_pushfixed(L, K_HandleAirDrag(player, onground, speed, airdrag));
	return 1;
}

// this function is for compatibility only; do not add new functionality here
// see K_GetItemGraphics
static int lib_kGetItemPatch(lua_State *L)
{
	kartitemtype_e item = luaL_optinteger(L, 1, KITEM_NONE);
	dboolean tiny = lua_optdboolean(L, 2);
	uint8_t index = 0;
	//HUDSAFE

	if (!lua_compatmode)
		LUA_Deprecated(L, "K_GetItemPatch", "K_GetItemGraphics");

	switch ((uint8_t)item) // dualjawz moment
	{
	case 17: // KRITEM_TRIPLESNEAKER
		item = KITEM_SNEAKER;
		break;
	case 18: // KRITEM_TRIPLEBANANA
	case 19: // KRITEM_TENFOLDBANANA
		item = KITEM_BANANA;
		break;
	case 20: // KRITEM_TRIPLEORBINAUT
	case 21: // KRITEM_QUADORBINAUT
		index = item - 18;
		item = KITEM_ORBINAUT;
		break;
	case 22: // KRITEM_DUALJAWZ
		item = KITEM_JAWZ;
		break;
	default:
		break;
	}

	const char *sad = tiny ? "K_ISSAD" : "K_ITSAD";
	if (item <= 0 || item >= numkartitems)
	{
		lua_pushstring(L, sad);
		return 1;
	}

	kartitemgraphics_t *graphics = &kartitems[item].graphics[(tiny ? 1 : 0) + (K_IsKartItemAlternate(item) ? 2 : 0)];
	lua_pushstring(L, index >= graphics->numpatches ? sad : graphics->patchnames[index]);
	return 1;
}

static int lib_kGetItemGraphics(lua_State *L)
{
	kartitemtype_e item = luaL_checkinteger(L, 1);
	dboolean tiny = lua_optdboolean(L, 2);
	dboolean alt = lua_optdboolean(L, 3);
	//HUDSAFE

	if (item < 1 || item >= numkartitems)
		return luaL_error(L, "item number %d out of range (1 - %d)", item, numkartitems-1);

	kartitemgraphics_t *graphics = &kartitems[item].graphics[(tiny ? 1 : 0) + (alt ? 2 : 0)];
	if (graphics->numpatches == 0)
		return 0;

	LUA_PushUserdata(L, graphics, META_KARTITEMGRAPHICS);
	return 1;
}

static int lib_kIsKartItemAlternate(lua_State *L)
{
	kartitemtype_e item = luaL_optinteger(L, 1, KITEM_NONE);
	// HUDSAFE

	if (item <= 0 || item >= numkartitems)
		return luaL_error(L, "item number %d out of range (0 - %d)", item, numkartitems-1);

	lua_pushdboolean(L, K_IsKartItemAlternate(item));
	return 1;
}

static int lib_kIsAltShrunk(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));

	//HUDSAFE
	if (!player)
		return LUA_ErrInvalid(L, "player_t");

	lua_pushdboolean(L, K_IsAltShrunk(player));
	return 1;
}

static int lib_kGetCollideAngle(lua_State *L)
{
	mobj_t *t1 = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	mobj_t *t2 = *((mobj_t **)luaL_checkudata(L, 2, META_MOBJ));
	//HUDSAFE
	if (!t1)
		return LUA_ErrInvalid(L, "mobj_t");
	if (!t2)
		return LUA_ErrInvalid(L, "mobj_t");
	lua_pushinteger(L, K_GetCollideAngle(t1, t2));
	return 1;
}

static int lib_kInitBossHealthBar(lua_State *L)
{
	const char *enemyname = luaL_checkstring(L, 1);
	const char *subtitle = luaL_checkstring(L, 2);
	sfxenum_t titlesound = luaL_checkinteger(L, 3);
	fixed_t pinchmagnitude = luaL_checkfixed(L, 4);
	uint8_t divisions = (uint8_t)luaL_checkinteger(L, 5);
	NOHUD
	K_InitBossHealthBar(enemyname, subtitle, titlesound, pinchmagnitude, divisions);
	return 0;
}

static int lib_kUpdateBossHealthBar(lua_State *L)
{
	fixed_t magnitude = luaL_checkfixed(L, 1);
	tic_t jitterlen = (tic_t)luaL_checkinteger(L, 2);
	NOHUD
	K_UpdateBossHealthBar(magnitude, jitterlen);
	return 0;
}

static int lib_kDeclareWeakspot(lua_State *L)
{
	mobj_t *spot = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	spottype_t spottype = luaL_checkinteger(L, 2);
	uint16_t color = luaL_checkinteger(L, 3);
	dboolean minimap = lua_optdboolean(L, 4);
	NOHUD
	if (!spot)
		return LUA_ErrInvalid(L, "mobj_t");
	K_DeclareWeakspot(spot, spottype, color, minimap);
	return 0;
}

// sets the remaining time before players blow up
static int lib_kSetRaceCountdown(lua_State *L)
{
	tic_t c = (tic_t)luaL_checkinteger(L, 1);
	racecountdown = c;
	return 0;
}

// sets the remaining time before the race ends after everyone finishes
static int lib_kSetExitCountdown(lua_State *L)
{
	tic_t c = (tic_t)luaL_checkinteger(L, 1);
	NOHUD
	exitcountdown = c;
	return 0;
}

// Sets the item cooldown before another shrink / SPB can be rolled
static int lib_kSetIndirectItemCountdown(lua_State *L)
{
	tic_t c = (tic_t)luaL_checkinteger(L, 1);
	NOHUD
	K_SetIndirectItemCooldown(c);
	return 0;
}

// Sets the item cooldown before another hyudoro can be rolled
static int lib_kSetHyuCountdown(lua_State *L)
{
	tic_t c = (tic_t)luaL_checkinteger(L, 1);
	NOHUD
	K_GetKartResult("hyudoro")->cooldown = c;
	return 0;
}

// Checks if the floor closet floor under an object would be safe to respawn/land on.
static int lib_kSafeRespawnPosition(lua_State *L)
{
	mobj_t *mobj = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	//HUDSAFE
	if (!mobj)
		return LUA_ErrInvalid(L, "mobj_t");
	lua_pushdboolean(L, K_SafeRespawnPosition(mobj));
	return 1;
}

// Checks if the floor closet floor under an object would be safe to respawn/land on.
static int lib_kSafeRespawnPositionEx(lua_State *L)
{
	fixed_t x = luaL_checkfixed(L, 1);
	fixed_t y = luaL_checkfixed(L, 2);
	fixed_t z = luaL_checkfixed(L, 3);
	dboolean flip = lua_optdboolean(L, 4);

	//HUDSAFE
	lua_pushdboolean(L, K_SafeRespawnPositionEx(x, y, z , flip));
	return 1;
}

static int lib_kKillAirDrop(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	p_airdropflags_t airdropflags = luaL_checkinteger(L, 2);

	if (!player)
		return LUA_ErrInvalid(L, "player_t");

	K_KillAirDrop(player, airdropflags);
	return 0;
}

// Checks if Rings are applicable.
static int lib_kRingsActive(lua_State *L)
{
	lua_pushdboolean(L, K_RingsActive());
	return 1;
}

// Checks if Purple Drift is active.
static int lib_kPurpleDriftActive(lua_State *L)
{
	lua_pushdboolean(L, K_PurpleDriftActive());
	return 1;
}

// Checks if Stacking is active.
static int lib_kStackingActive(lua_State *L)
{
	lua_pushdboolean(L, K_StackingActive());
	return 1;
}

// Checks if Chaining is active.
static int lib_kChainingActive(lua_State *L)
{
	lua_pushdboolean(L, K_ChainingActive());
	return 1;
}

// Checks if Slipdash is active.
static int lib_kSlipdashActive(lua_State *L)
{
	lua_pushdboolean(L, K_SlipdashActive());
	return 1;
}

// Checks if Slope Boost is active.
static int lib_kSlopeBoostActive(lua_State *L)
{
	lua_pushdboolean(L, K_SlopeBoostActive());
	return 1;
}

// Checks if Drafting is active.
static int lib_kDraftingActive(lua_State *L)
{
	lua_pushdboolean(L, K_DraftingActive());
	return 1;
}

// Checks if Air Drop is active.
static int lib_kAirDropActive(lua_State *L)
{
	lua_pushinteger(L, K_AirDropActive());
	return 1;
}

// Checks if Air Thrust is active.
static int lib_kAirThrustActive(lua_State *L)
{
	lua_pushinteger(L, K_AirThrustActive());
	return 1;
}

// Checks if Recovery Dash is active.
static int lib_kRecoveryDashActive(lua_State *L)
{
	lua_pushdboolean(L, K_RecoveryDashActive());
	return 1;
}

// Checks if Keep Stuff is active.
static int lib_kKeepStuffActive(lua_State *L)
{
	lua_pushdboolean(L, K_KeepStuffActive());
	return 1;
}

// Checks if item littering is active.
static int lib_kItemLitterActive(lua_State *L)
{
	lua_pushdboolean(L, K_ItemLitterActive());
	return 1;
}

static int lib_kItemListActive(lua_State *L)
{
	// Use the y_inter version so that Lua scripters don't have to do a
	// useless player headcount check.
	lua_pushdboolean(L, Y_ItemListActive());
	return 1;
}

// Checks if trailing slowdown is active.
static int lib_kTrailSlowActive(lua_State *L)
{
	lua_pushdboolean(L, K_TrailSlowActive());
	return 1;
}


// Checks if rankings are active.
static int lib_kRankingsActive(lua_State *L)
{
	lua_pushdboolean(L, K_RankingsActive());
	return 1;
}

// Gets the currently active bumpspark type.
static int lib_kGetBumpSpark(lua_State *L)
{
	lua_pushinteger(L, K_GetBumpSpark());
	return 1;
}

// Gets the currently active playerbump type.
static int lib_kGetPlayerBump(lua_State *L)
{
	lua_pushinteger(L, K_GetPlayerBump());
	return 1;
}

// Checks if current map is using legacy boss3 bassed checkpoints. Useful for map compat.
static int lib_kUsingLegacyCheckpoints(lua_State *L)
{
	lua_pushdboolean(L, K_UsingLegacyCheckpoints());
	return 1;
}

static int lib_kDoBoost(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	fixed_t speedboost = luaL_checkfixed(L, 2);
	fixed_t accelboost = luaL_checkfixed(L, 3);
	fixed_t handleboost = luaL_checkfixed(L, 4);
	dboolean stacking = lua_optdboolean(L, 5);
	dboolean visible = lua_isnoneornil(L, 6) ? true : luaL_checkdboolean(L, 6);

	//HUDSAFE
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	K_DoBoost(player, speedboost, accelboost, handleboost, stacking, visible);
	return 0;
}

static int lib_kClearBoost(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));

	//HUDSAFE
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	K_ClearBoost(player);
	return 0;
}

static int lib_kBoostChain(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	int32_t timer = luaL_checkinteger(L, 2);
	int32_t chainsound = lua_optdboolean(L, 3);

	//HUDSAFE
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	lua_pushdboolean(L, K_BoostChain(player, timer, chainsound));
	return 1;
}

static int lib_kChainOrDeincrementTime(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	int32_t timer = luaL_checkinteger(L, 2);
	int32_t deincrement = luaL_checkinteger(L, 3);
	int32_t chainsound = lua_optdboolean(L, 4);

	//HUDSAFE
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	lua_pushinteger(L, K_ChainOrDeincrementTime(player, timer, deincrement, chainsound));
	return 1;
}

static int lib_kGetNewSpeed(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));

	//HUDSAFE
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	lua_pushinteger(L, K_GetNewSpeed(player));
	return 1;
}

static int lib_kSMonitorGradient(lua_State *L)
{
	uint16_t time = (uint16_t)luaL_checkinteger(L, 1);
	// HUDSAFE

	lua_pushinteger(L, K_SMonitorGradient(time));
	return 1;
}

static int lib_kGetSMonitorSpeed(lua_State *L)
{
	uint16_t time = (uint16_t)luaL_checkinteger(L, 1);
	// HUDSAFE

	lua_pushinteger(L, K_GetSMonitorSpeed(time));
	return 1;
}

static int lib_kGetSMonitorAccel(lua_State *L)
{
	uint16_t time = (uint16_t)luaL_checkinteger(L, 1);
	// HUDSAFE

	lua_pushinteger(L, K_GetSMonitorAccel(time));
	return 1;
}

static int lib_kGetSMonitorHandling(lua_State *L)
{
	uint16_t time = (uint16_t)luaL_checkinteger(L, 1);
	// HUDSAFE

	lua_pushinteger(L, K_GetSMonitorHandling(time));
	return 1;
}

static int lib_k3dKartMovement(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	dboolean onground = lua_optdboolean(L, 2);
	int8_t fowardmove = luaL_checkinteger(L, 3);

	//HUDSAFE
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	if (lua_isnil(L, 3))
		fowardmove = K_GetForwardMove(player);
	lua_pushinteger(L, K_3dKartMovement(player, onground, fowardmove));
	return 1;
}

static int lib_kMomentumAngle(lua_State *L)
{
	mobj_t *mo = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));

	//HUDSAFE
	if (!mo)
		return LUA_ErrInvalid(L, "mobj_t");
	lua_pushinteger(L, K_MomentumAngle(mo));
	return 1;
}

static int lib_kGetKartSpeedFromStat(lua_State *L)
{
	uint16_t kartspeed = luaL_checkinteger(L, 1);

	//HUDSAFE
	lua_pushinteger(L, K_GetKartSpeedFromStat(kartspeed, false,  0));
	return 1;
}

static int lib_kGetKartSpeedFromStatEx(lua_State *L)
{
	uint16_t kartspeed = luaL_checkinteger(L, 1);
	dboolean karmabomb = lua_optdboolean(L, 2);
	fixed_t extrapct = lua_isnoneornil(L, 3) ? 0 : luaL_checkinteger(L, 3);

	//HUDSAFE
	lua_pushinteger(L, K_GetKartSpeedFromStat(kartspeed, karmabomb, extrapct));
	return 1;
}


static int lib_kKartKickstart(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));

	//HUDSAFE
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	lua_pushdboolean(L, K_KartKickstart(player));
	return 1;
}

static int lib_kGetKartButtons(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));

	//HUDSAFE
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	lua_pushinteger(L, K_GetKartButtons(player));
	return 1;
}

static int lib_kGetForwardMove(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));

	//HUDSAFE
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	lua_pushinteger(L, K_GetForwardMove(player));
	return 1;
}

static int lib_kAwardPlayerRings(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	uint16_t rings = luaL_checkinteger(L, 2);
	dboolean overload = lua_optdboolean(L, 3);

	//HUDSAFE
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	K_AwardPlayerRings(player, rings, overload);
	return 0;
}

static int lib_kAwardScaledPlayerRings(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	int8_t mode = luaL_checkinteger(L, 2);

	//HUDSAFE
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	K_AwardScaledPlayerRings(player, mode);
	return 0;
}

static int lib_kGetShieldFromPlayer(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));

	//HUDSAFE
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	lua_pushinteger(L, K_GetShieldFromPlayer(player));
	return 1;
}

static int lib_kPlayerCanPunt(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));

	//HUDSAFE
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	lua_pushdboolean(L, K_PlayerCanPunt(player));
	return 1;
}

static int lib_kMomentum3D(lua_State *L)
{
	mobj_t *mo = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));

	//HUDSAFE
	if (!mo)
		return LUA_ErrInvalid(L, "mobj_t");

	lua_pushinteger(L, K_Momentum3D(mo));
	return 1;
}

static int lib_kGetClusterDistanceForPlayer(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));

	//HUDSAFE
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	
	lua_pushinteger(L, K_GetClusterDistanceForPlayer(player));
	return 1;
}

static int lib_kAddTiltImpulse(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	fixed_t distance = luaL_checkinteger(L, 2);
	fixed_t force = luaL_checkinteger(L, 3);
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	K_AddTiltImpulse(player, distance, force);
	return 0;
}

static int lib_kAddTiltMomentum(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	fixed_t angularvelocity = luaL_checkinteger(L, 2);
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	K_AddTiltMomentum(player, angularvelocity);
	return 0;
}

static int lib_kShoveObject(lua_State *L)
{
	mobj_t *victim = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	mobj_t *source = *((mobj_t **)luaL_checkudata(L, 2, META_MOBJ));
	fixed_t force = luaL_checkinteger(L, 3);
	if (!victim)
		return LUA_ErrInvalid(L, "mobj_t");
	if (!source)
		return LUA_ErrInvalid(L, "mobj_t");
	K_ShoveObject(victim, source, force);
	return 0;
}

static int lib_kGetRankTier(lua_State *L)
{
	int32_t rank_score = luaL_checkinteger(L, 1);
	int8_t p_rank_override = -1;

	//HUDSAFE

	if (!lua_isnoneornil(L, 2))
	{
		p_rank_override = (int8_t)(luaL_checkdboolean(L, 2));
	}

	lua_pushinteger(L, K_GetRankTier(rank_score, p_rank_override));
	return 1;
}

static int lib_kGetScoreForRankTier(lua_State *L)
{
	ranktier_e rank_tier = luaL_checkinteger(L, 1);

	//HUDSAFE

	if (rank_tier < RTIER_E || rank_tier >= NUMRANKTIERS)
		return luaL_error(L, "ranktier index %d out of range (%d - %d)", rank_tier, RTIER_E, NUMRANKTIERS-1);

	// Likely impossible after that safety check, but better safe than sorry...
	rank_tier = (ranktier_e)(CLAMP(rank_tier, RTIER_E, NUMRANKTIERS - 1));

	lua_pushinteger(L, K_GetScoreForRankTier(rank_tier));
	return 1;
}

// A static value in k_kart...
static int lib_kGetDriftBoostRankScore(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));

	//HUDSAFE
	if (!player)
		return LUA_ErrInvalid(L, "player_t");

	const int32_t bluepoints = (player->ranklog.driftboostcount[0] / 2); // 0.5
	const int32_t redpoints = (player->ranklog.driftboostcount[1]); // 1.0
	const int32_t purplepoints = (3 * player->ranklog.driftboostcount[2] / 2); // 1.5
	const int32_t rainbowpoints = (2 * player->ranklog.driftboostcount[3]); // 2.0
	const int32_t total = bluepoints + redpoints + purplepoints + rainbowpoints;

	lua_pushinteger(L, total);
	return 1;
}

static int lib_kCalculateLapGrading(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));

	NOHUD
	INLEVEL

	if (!player)
		return LUA_ErrInvalid(L, "player_t");

	K_CalculateLapGrading(player);
	return 0;
}

static int lib_kUpdateIndividualPlayerRank(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));

	NOHUD
	INLEVEL

	if (!player)
		return LUA_ErrInvalid(L, "player_t");

	K_UpdateIndividualPlayerRank(player);
	return 0;
}

static int lib_kMissileOrKartItem(lua_State *L)
{
	mobj_t *mo = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	
	if (!mo)
		return LUA_ErrInvalid(L, "mobj_t");

	lua_pushdboolean(L, K_IsMissileOrKartItem(mo));
	return 1;
}

static int lib_kAddNewScoreboardMod(lua_State *L)
{
	const char *modname = luaL_checkstring(L, 1);
	const char *cvarname =  lua_isnil(L, 2) ? NULL : luaL_checkstring(L, 2);
	const consvar_t *cvar = lua_isnil(L, 2) ? NULL : CV_FindVar(cvarname);
	int8_t active = SCOREBOARDMOD_NOTUSED;

	if (!cvarname || !cvar)
		active = SCOREBOARDMOD_ACTIVE;

	K_AddNewScoreboardMod(modname, cvar, active);
	return 0;
}

static int lib_kSetScoreboardModStatus(lua_State *L)
{
	const char *modname = luaL_checkstring(L, 1);
	int8_t active = CLAMP(luaL_checkinteger(L, 2), SCOREBOARDMOD_NOTUSED, SCOREBOARDMOD_ACTIVE);

	K_SetScoreboardModStatus(modname, active);
	return 0;
}

static int lib_kPlayerUsesBotMovement(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	INLEVEL
	if (!player)
		return LUA_ErrInvalid(L, "player_t");

	lua_pushdboolean(L, K_PlayerUsesBotMovement(player));
	return 1;
}

static int lib_kBotCanTakeCut(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	INLEVEL
	if (!player)
		return LUA_ErrInvalid(L, "player_t");

	lua_pushdboolean(L, K_BotCanTakeCut(player));
	return 1;
}

static int lib_kGetBotController(lua_State *L)
{
	mobj_t *mobj = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	INLEVEL
	if (!mobj)
		return LUA_ErrInvalid(L, "mobj_t");

	botcontroller_t *botController = K_GetBotController(mobj);
	if (botController != NULL)
		LUA_PushUserdata(L, botController, META_BOTCONTROLLER);
	else
		lua_pushnil(L);

	return 1;
}

static int lib_kBotMapModifier(lua_State *L)
{
	INLEVEL
	lua_pushfixed(L, K_BotMapModifier());
	return 1;
}

static int lib_kBotRubberband(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	INLEVEL
	if (!player)
		return LUA_ErrInvalid(L, "player_t");

	lua_pushfixed(L, K_BotRubberband(player));
	return 1;
}

static int lib_kUpdateRubberband(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	INLEVEL
	if (!player)
		return LUA_ErrInvalid(L, "player_t");

	lua_pushfixed(L, K_UpdateRubberband(player));
	return 1;
}

static int lib_kDistanceOfLineFromPoint(lua_State *L)
{
	fixed_t v1x = luaL_checkfixed(L, 1);
	fixed_t v1y = luaL_checkfixed(L, 2);
	fixed_t v2x = luaL_checkfixed(L, 3);
	fixed_t v2y = luaL_checkfixed(L, 4);
	fixed_t cx = luaL_checkfixed(L, 5);
	fixed_t cy = luaL_checkfixed(L, 6);
	lua_pushfixed(L, K_DistanceOfLineFromPoint(v1x, v1y, v2x, v2y, cx, cy));
	return 1;
}

static int lib_kAddBot(lua_State *L)
{
	int32_t skinid = -1;
	uint8_t difficulty = luaL_checkinteger(L, 2);
	botStyle_e style = luaL_checkinteger(L, 3);
	uint8_t newplayernum = 0;

	// Copypaste of libd_getSprite2Patch, but fails loudly on each fail state instead.
	// get skin first!
	if (lua_isnumber(L, 1)) // find skin by number
	{
		skinid = lua_tonumber(L, 1);
		if (skinid < 0 || skinid >= numskins)
			return luaL_error(L, "skin number %d out of range (0 - %d)", skinid, numskins-1);
	}
	else // find skin by name
	{
		const char *name = luaL_checkstring(L, 1);
		skinid = R_SkinAvailable(name);
		if (skinid == -1)
			return luaL_error(L, "could not find skin %s by name", name);
	}

	INLEVEL

	dboolean success = K_AddBot(skinid, difficulty, style, &newplayernum);
	lua_pushdboolean(L, success);
	if (success)
		LUA_PushUserdata(L, &players[newplayernum - 1], META_PLAYER);
	else
		lua_pushnil(L);

	return 2;
}

static int lib_kSetNameForBot(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	const char *realname = luaL_checkstring(L, 2);
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	if (!player->bot)
		return luaL_error(L, "You may only change bot names.");

	K_SetNameForBot(player-players, realname);

	return 0;
}

static int lib_kRemoveBot(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	INLEVEL
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	if (!player->bot)
		return luaL_error(L, "You may only remove bots.");

	CL_RemovePlayer(player-players, KR_LEAVE);

	return 0;
}

static int lib_kNextRespawnWaypointIndex(lua_State *L)
{
	waypoint_t *waypoint = *((waypoint_t **)luaL_checkudata(L, 1, META_WAYPOINT));

	INLEVEL
	if (!waypoint)
		return LUA_ErrInvalid(L, "waypoint_t");

	lua_pushinteger(L, K_NextRespawnWaypointIndex(waypoint));
	return 1;
}

static int lib_kGetFinishLineWaypoint(lua_State *L)
{
	INLEVEL
	LUA_PushUserdata(L, K_GetFinishLineWaypoint(), META_WAYPOINT);
	return 1;
}

static int lib_kGetStartingWaypoint(lua_State *L)
{
	INLEVEL
	LUA_PushUserdata(L, K_GetStartingWaypoint(), META_WAYPOINT);
	return 1;
}

static int lib_kGetWaypointIsFinishline(lua_State *L)
{
	waypoint_t *waypoint = *((waypoint_t **)luaL_checkudata(L, 1, META_WAYPOINT));
	INLEVEL

	if (!waypoint)
		return LUA_ErrInvalid(L, "waypoint_t");

	lua_pushdboolean(L, K_GetWaypointIsFinishline(waypoint));
	return 1;
}

static int lib_kGetWaypointIsShortcut(lua_State *L)
{
	waypoint_t *waypoint = *((waypoint_t **)luaL_checkudata(L, 1, META_WAYPOINT));
	INLEVEL

	if (!waypoint)
		return LUA_ErrInvalid(L, "waypoint_t");

	lua_pushdboolean(L, K_GetWaypointIsShortcut(waypoint));
	return 1;
}

static int lib_kGetWaypointIsEnabled(lua_State *L)
{
	waypoint_t *waypoint = *((waypoint_t **)luaL_checkudata(L, 1, META_WAYPOINT));
	INLEVEL

	if (!waypoint)
		return LUA_ErrInvalid(L, "waypoint_t");

	lua_pushdboolean(L, K_GetWaypointIsEnabled(waypoint));
	return 1;
}

static int lib_kSetWaypointIsEnabled(lua_State *L)
{
	waypoint_t *waypoint = *((waypoint_t **)luaL_checkudata(L, 1, META_WAYPOINT));
	dboolean enabled = luaL_checkdboolean(L, 2);
	INLEVEL

	if (!waypoint)
		return LUA_ErrInvalid(L, "waypoint_t");

	K_SetWaypointIsEnabled(waypoint, enabled);
	return 0;
}

static int lib_kGetWaypointIsSpawnpoint(lua_State *L)
{
	waypoint_t *waypoint = *((waypoint_t **)luaL_checkudata(L, 1, META_WAYPOINT));
	INLEVEL

	if (!waypoint)
		return LUA_ErrInvalid(L, "waypoint_t");

	lua_pushdboolean(L, K_GetWaypointIsSpawnpoint(waypoint));
	return 1;
}

static int lib_kGetWaypointNextID(lua_State *L)
{
	waypoint_t *waypoint = *((waypoint_t **)luaL_checkudata(L, 1, META_WAYPOINT));
	INLEVEL

	if (!waypoint)
		return LUA_ErrInvalid(L, "waypoint_t");

	lua_pushinteger(L, K_GetWaypointNextID(waypoint));
	return 1;
}

static int lib_kGetWaypointID(lua_State *L)
{
	waypoint_t *waypoint = *((waypoint_t **)luaL_checkudata(L, 1, META_WAYPOINT));
	INLEVEL

	if (!waypoint)
		return LUA_ErrInvalid(L, "waypoint_t");

	lua_pushinteger(L, K_GetWaypointID(waypoint));
	return 1;
}

static int lib_kGetWaypointFromID(lua_State *L)
{
	int32_t waypointId = luaL_checkinteger(L, 1);
	INLEVEL

	LUA_PushUserdata(L, K_GetWaypointFromID(waypointId), META_WAYPOINT);
	return 1;
}

static int lib_kGetCircuitLength(lua_State *L)
{
	INLEVEL
	lua_pushinteger(L, K_GetCircuitLength());
	return 1;
}

static int lib_kGetTrackComplexity(lua_State *L)
{
	INLEVEL
	lua_pushinteger(L, K_GetTrackComplexity());
	return 1;
}

static int lib_kGetClosestWaypointToMobj(lua_State *L)
{
	mobj_t *mobj = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	INLEVEL
	if (!mobj)
		return LUA_ErrInvalid(L, "mobj_t");

	LUA_PushUserdata(L, K_GetClosestWaypointToMobj(mobj), META_WAYPOINT);
	return 1;
}

static int lib_kGetBestWaypointForMobj(lua_State *L)
{
	mobj_t *mobj = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	waypoint_t *hint = NULL;
	INLEVEL
	if (!mobj)
		return LUA_ErrInvalid(L, "mobj_t");
	// Optional waypoint parameter:
	if (lua_isuserdata(L, 2) && lua_getmetatable(L, 2))
	{
		lua_getfield(L, LUA_REGISTRYINDEX, META_WAYPOINT);
		int result = lua_rawequal(L, -1, -2);
		lua_pop(L, 2);

		if (!result)
		{
			return LUA_ErrInvalid(L, "waypoint_t");
		}
		else




			hint = *((waypoint_t **)lua_touserdata(L, 2));
	}
	else if (!lua_isnoneornil(L, 2))
	{
		// If we reach this point and it isn't an userdata,
		// the scripter used a basic data type. Let them know
		// they messed up. (Just use nil or nothing, please.)
		return LUA_ErrInvalid(L, "waypoint_t");
	}

	LUA_PushUserdata(L, K_GetBestWaypointForMobj(mobj, hint), META_WAYPOINT);
	return 1;
}

/*
 *	JugadorXEI @ 01/11/2025 (MM/DD/AAAA)
 *	This was my way to work around giving path_t and pathfindnode_t objects
 *	to Lua, as usually these are dynamically allocated. We give them a deep
 *	copy of the values and then we free this memory after the fact.
 *	Lua can manage its own copy itself.
 */
static void pushDeepCopyOfPathTypeAsTable(lua_State *L, path_t *const path)
{
	lua_createtable(L, 0, 3);

	lua_pushinteger(L, path->numnodes);
	lua_setfield(L, -2, "numnodes");

	lua_createtable(L, path->numnodes, 0);
	for (size_t i = 0; i < path->numnodes; i++)
	{
		lua_createtable(L, 0, 3);

		// It doesn't make sense for heap-related stuff to be exposed to Lua.
		// lua_pushinteger(L, path->array[i].heapindex);
		// lua_setfield(L, -2, "heapindex");

		LUA_PushUserdata(L, (waypoint_t *)path->array[i].nodedata, META_WAYPOINT);
		lua_setfield(L, -2, "nodedata");

		lua_pushinteger(L, path->array[i].gscore);
		lua_setfield(L, -2, "gscore");

		lua_pushinteger(L, path->array[i].hscore);
		lua_setfield(L, -2, "hscore");

		lua_rawseti(L, -2, 1 + i);
	}
	lua_setfield(L, -2, "array");

	lua_pushinteger(L, path->totaldist);
	lua_setfield(L, -2, "totaldist");
}

static int lib_kPathfindToWaypoint(lua_State *L)
{
	waypoint_t *sourcewaypoint = *((waypoint_t **)luaL_checkudata(L, 1, META_WAYPOINT));
	waypoint_t *destinationwaypoint = *((waypoint_t **)luaL_checkudata(L, 2, META_WAYPOINT));
	dboolean useshortcuts = lua_optdboolean(L, 3);
	dboolean huntbackwards = lua_optdboolean(L, 4);

	INLEVEL
	if (!sourcewaypoint || !destinationwaypoint)
		return LUA_ErrInvalid(L, "waypoint_t");

	path_t returnpath = {0};
	dboolean success = K_PathfindToWaypoint(sourcewaypoint, destinationwaypoint, &returnpath, useshortcuts, huntbackwards);

	lua_pushdboolean(L, success);
	if (success)
	{
		pushDeepCopyOfPathTypeAsTable(L, &returnpath);
	}
	else
		lua_pushnil(L);

	Z_Free(returnpath.array);
	return 2;
}

static int lib_kPathfindThruCircuit(lua_State *L)
{
	waypoint_t *sourcewaypoint = *((waypoint_t **)luaL_checkudata(L, 1, META_WAYPOINT));
	fixed_t traveldistance = luaL_checkfixed(L, 2);
	dboolean useshortcuts = lua_optdboolean(L, 3);
	dboolean huntbackwards = lua_optdboolean(L, 4);

	INLEVEL
	if (!sourcewaypoint)
		return LUA_ErrInvalid(L, "waypoint_t");

	path_t returnpath = {0};
	dboolean success = K_PathfindThruCircuit(sourcewaypoint, traveldistance, &returnpath, useshortcuts, huntbackwards);

	lua_pushdboolean(L, success);
	if (success)
	{
		pushDeepCopyOfPathTypeAsTable(L, &returnpath);
	}
	else
		lua_pushnil(L);

	Z_Free(returnpath.array);
	return 2;
}

static int lib_kPathfindThruCircuitSpawnable(lua_State *L)
{
	waypoint_t *sourcewaypoint = *((waypoint_t **)luaL_checkudata(L, 1, META_WAYPOINT));
	fixed_t traveldistance = luaL_checkfixed(L, 2);
	dboolean useshortcuts = lua_optdboolean(L, 3);
	dboolean huntbackwards = lua_optdboolean(L, 4);

	INLEVEL
	if (!sourcewaypoint)
		return LUA_ErrInvalid(L, "waypoint_t");

	path_t returnpath = {0};
	dboolean success = K_PathfindThruCircuitSpawnable(sourcewaypoint, traveldistance, &returnpath, useshortcuts, huntbackwards);

	lua_pushdboolean(L, success);
	if (success)
	{
		pushDeepCopyOfPathTypeAsTable(L, &returnpath);
	}
	else
		lua_pushnil(L);

	Z_Free(returnpath.array);
	return 2;
}

static int lib_kGetNextWaypointToDestination(lua_State *L)
{
	waypoint_t *sourcewaypoint = *((waypoint_t **)luaL_checkudata(L, 1, META_WAYPOINT));
	waypoint_t *destinationwaypoint = *((waypoint_t **)luaL_checkudata(L, 2, META_WAYPOINT));
	dboolean useshortcuts = lua_optdboolean(L, 3);
	dboolean huntbackwards = lua_optdboolean(L, 4);

	INLEVEL
	if (!sourcewaypoint || !destinationwaypoint)
		return LUA_ErrInvalid(L, "waypoint_t");

	LUA_PushUserdata(L, K_GetNextWaypointToDestination(sourcewaypoint, destinationwaypoint, useshortcuts, huntbackwards), META_WAYPOINT);
	return 1;
}

static int lib_kSearchWaypointGraphForMobj(lua_State *L)
{
	mobj_t *mobj = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));

	INLEVEL
	if (!mobj)
		return LUA_ErrInvalid(L, "mobj_t");

	LUA_PushUserdata(L, K_SearchWaypointGraphForMobj(mobj), META_WAYPOINT);
	return 1;
}

static int lib_kSearchWaypointHeapForMobj(lua_State *L)
{
	mobj_t *mobj = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));

	INLEVEL
	if (!mobj)
		return LUA_ErrInvalid(L, "mobj_t");

	LUA_PushUserdata(L, K_SearchWaypointHeapForMobj(mobj), META_WAYPOINT);
	return 1;
}

static int lib_getTimeMicros(lua_State *L)
{
	lua_pushinteger(L, I_GetPreciseTime() / (I_GetPrecisePrecision() / 1000000));
	return 1;
}

static int lib_iGetPreciseTime(lua_State *L)
{
	lua_pushinteger(L, I_GetPreciseTime());
	return 1;
}

static int lib_kAffectingTerrainActive(lua_State *L)
{
	lua_pushdboolean(L, K_AffectingTerrainActive());
	return 1;
}

static int lib_kGetDefaultTerrain(lua_State *L)
{
	LUA_PushUserdata(L, K_GetDefaultTerrain(), META_TERRAIN);
	return 1;
}

static int lib_kGetTerrainForTextureName(lua_State *L)
{
	const char *str = luaL_checkstring(L, 1);
	LUA_PushUserdata(L, K_GetTerrainForTextureName(str), META_TERRAIN);
	return 1;
}

static int lib_kGetTerrainForTextureNum(lua_State *L)
{
	int32_t id = luaL_checkinteger(L, 1);
	LUA_PushUserdata(L, K_GetTerrainForTextureNum(id), META_TERRAIN);
	return 1;
}

static int lib_kProcessTerrainEffect(lua_State *L)
{
	mobj_t *mo = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));

	NOHUD
	INLEVEL

	if (!mo)
		return LUA_ErrInvalid(L, "mobj_t");

	K_ProcessTerrainEffect(mo);
	return 0;
}

static int lib_kSpawnSplashForMobj(lua_State *L)
{
	mobj_t *mo = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));
	fixed_t impact = luaL_optinteger(L, 2, FRACUNIT);

	NOHUD
	INLEVEL

	if (!mo)
		return LUA_ErrInvalid(L, "mobj_t");

	K_SpawnSplashForMobj(mo, impact);
	return 0;
}

static int lib_kHandleFootstepParticles(lua_State *L)
{
	mobj_t *mo = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));

	NOHUD
	INLEVEL

	if (!mo)
		return LUA_ErrInvalid(L, "mobj_t");

	K_HandleFootstepParticles(mo);
	return 0;
}

static int lib_kUpdateTerrainOverlay(lua_State *L)
{
	mobj_t *mo = *((mobj_t **)luaL_checkudata(L, 1, META_MOBJ));

	NOHUD
	INLEVEL

	if (!mo)
		return LUA_ErrInvalid(L, "mobj_t");

	K_UpdateTerrainOverlay(mo);
	return 0;
}

static int lib_kTerrainHasAffect(lua_State *L)
{
	terrain_t *terrain = *((terrain_t **)luaL_checkudata(L, 1, META_TERRAIN));
	dboolean badonly = lua_optdboolean(L, 2);
	dboolean followmaprules = lua_optdboolean(L, 3);

	NOHUD
	INLEVEL

	if (!terrain)
		return LUA_ErrInvalid(L, "terrain_t");

	lua_pushdboolean(L, K_TerrainHasAffect(terrain, badonly, followmaprules));
	return 1;
}

static int lib_kAwardPlayerItem(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	kartitemtype_e type = luaL_checkinteger(L, 2);
	uint8_t amount = luaL_checkinteger(L, 3);
	dboolean nodoubles = lua_optdboolean(L, 4);
	kartitemblink_e blink = luaL_optint(L, 5, KITEMBLINK_NORMAL);
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	K_AwardPlayerItem(player, type, amount, nodoubles, blink);
	return 0;
}

static int lib_kAwardPlayerItemOrReserve(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	kartitemtype_e type = luaL_checkinteger(L, 2);
	uint8_t amount = luaL_checkinteger(L, 3);
	dboolean nodoubles = lua_optdboolean(L, 4);
	kartitemblink_e blink = luaL_optint(L, 5, KITEMBLINK_NORMAL);
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	K_AwardPlayerItemOrReserve(player, type, amount, nodoubles, blink);
	return 0;
}

static int lib_kAddPlayerReserveItem(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	kartitemtype_e type = luaL_checkinteger(L, 2);
	uint8_t amount = luaL_checkinteger(L, 3);
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	K_AddPlayerReserveItem(player, type, amount);
	return 0;
}

static int lib_kStartRoulette(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	kartroulettetype_e type = luaL_checkinteger(L, 2);
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	K_StartRoulette(player, type);
	return 0;
}

static int lib_kSetPlayerItemCooldown(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	tic_t c = (tic_t)luaL_checkinteger(L, 2);
	dboolean force = lua_optdboolean(L, 3);
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	K_SetPlayerItemCooldown(player, c, force);
	return 0;
}

static int lib_kSpawnEquippedItem(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	kartitemequip_e equipstyle = luaL_checkint(L, 2);
	mobjtype_t type = luaL_checkint(L, 3);
	int32_t moloop = luaL_optinteger(L, 4, 0);
	int32_t totalamount = 1;
	mobj_t *prev = NULL;
	mobj_t *mo = NULL;
	NOHUD
	if (!player)
		return LUA_ErrInvalid(L, "player_t");

	totalamount = luaL_optinteger(L, 5, player->itemamount);

	if ((!lua_isnone(L, 6)) && lua_isuserdata(L, 6))
		prev = *((mobj_t **)luaL_checkudata(L, 6, META_MOBJ));

	mo = K_SpawnEquippedItem(player, equipstyle, type, moloop, totalamount, prev);
	if (mo)
		LUA_PushUserdata(L, mo, META_MOBJ);
	else
		lua_pushnil(L);
	return 1;
}

static int lib_kEnableSeekingReticule(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	dboolean passive = lua_optdboolean(L, 2);
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	K_EnableSeekingReticule(player, passive);
	return 0;
}

static int lib_kDisableSeekingReticule(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	K_DisableSeekingReticule(player);
	return 0;
}

static int lib_kDecrementAndCheckCurrentItem(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	if (!player)
		return LUA_ErrInvalid(L, "player_t");
	K_DecrementAndCheckCurrentItem(player);
	return 0;
}

static int lib_kDontDoubleMyItems(lua_State *L)
{
	kartresult_t *res = *((kartresult_t **)luaL_checkudata(L, 1, META_KARTRESULT));
	//HUDSAFE
	if (!res)
		return LUA_ErrInvalid(L, "kartresult_t");

	lua_pushdboolean(L, K_DontDoubleMyItems(res));
	return 1;
}

static int lib_kLegacyOddsMode(lua_State *L)
{
	//HUDSAFE

	lua_pushdboolean(L, K_LegacyOddsMode());
	return 1;
}

static int lib_kIntDistanceForMap(lua_State *L)
{
	fixed_t curx = luaL_checkfixed(L, 1);
	fixed_t cury = luaL_checkfixed(L, 2);
	fixed_t curz = luaL_checkfixed(L, 3);
	fixed_t destx = luaL_checkfixed(L, 4);
	fixed_t desty = luaL_checkfixed(L, 5);
	fixed_t destz = luaL_checkfixed(L, 6);

	//HUDSAFE

	lua_pushinteger(L, K_IntDistanceForMap(curx, cury, curz, destx, desty, destz));
	return 1;
}

static int lib_kGetCongaLineDistance(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	uint8_t target_pos = (uint8_t)(luaL_checkint(L, 2));

	//HUDSAFE
	if (!player)
		return LUA_ErrInvalid(L, "player_t");

	lua_pushinteger(L, K_GetCongaLineDistance(player, target_pos));
	return 1;
}

static int lib_kGetDTFDifference(lua_State *L)
{
	player_t *src = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	player_t *dest = *((player_t **)luaL_checkudata(L, 2, META_PLAYER));

	//HUDSAFE
	if (!src)
		return LUA_ErrInvalid(L, "player_t");

	if (!dest)
		return LUA_ErrInvalid(L, "player_t");

	lua_pushinteger(L, K_GetDTFDifference(src, dest));
	return 1;
}

// Not an actual hardcode function, but good for convenience!
static int lib_kGetDistanceBetweenPlayers(lua_State *L)
{
	player_t *src = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	player_t *dest = *((player_t **)luaL_checkudata(L, 2, META_PLAYER));

	//HUDSAFE
	if (!src)
		return LUA_ErrInvalid(L, "player_t");

	if (!dest)
		return LUA_ErrInvalid(L, "player_t");

	uint32_t distance = 0;

	if (dest->position < src->position)
	{
		if (K_LegacyOddsMode())
		{
			distance = K_GetCongaLineDistance(src, dest->position);
		}
		else
		{
			distance = K_GetDTFDifference(src, dest);

			if (mapobjectscale != FRACUNIT)
			{
				// Rescale to better fit smaller-scale maps
				distance = FixedDiv(distance, mapobjectscale);
			}
		}
	}

	lua_pushinteger(L, distance);
	return 1;
}

static int lib_kAddItemRollToList(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	kartitemtype_e item_id = luaL_checkinteger(L, 2);
	int32_t amt = luaL_checkinteger(L, 3);
	//HUDSAFE

	if (!player)
		return LUA_ErrInvalid(L, "player_t");

	if (item_id < 1 || item_id >= numkartitems)
		return luaL_error(L, "item number %d out of range (1 - %d)", item_id, numkartitems-1);

	int32_t pid = (int32_t)(player - players);

	K_AddItemRollToList(pid, item_id, amt);
	return 0;
}

static int lib_kGetItemListSize(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	//HUDSAFE

	if (!player)
		return LUA_ErrInvalid(L, "player_t");

	int32_t pid = (int32_t)(player - players);

	lua_pushinteger(L, K_GetItemListSize(pid));
	return 1;
}

static int lib_kSetItemListEntry(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	kartitemtype_e item_id = luaL_checkinteger(L, 2);
	int32_t num = luaL_checkinteger(L, 3);
	//HUDSAFE

	if (!player)
		return LUA_ErrInvalid(L, "player_t");

	if (item_id < 1 || item_id >= numkartitems)
		return luaL_error(L, "item number %d out of range (1 - %d)", item_id, numkartitems-1);

	int32_t pid = (int32_t)(player - players);

	K_SetItemListEntry(pid, item_id, num);
	return 0;
}

static int lib_kGetItemListEntry(lua_State *L)
{
	player_t *player = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));
	kartitemtype_e item_id = luaL_checkinteger(L, 2);
	//HUDSAFE

	if (!player)
		return LUA_ErrInvalid(L, "player_t");

	if (item_id < 1 || item_id >= numkartitems)
		return luaL_error(L, "item number %d out of range (1 - %d)", item_id, numkartitems-1);

	int32_t pid = (int32_t)(player - players);
	lua_pushinteger(L, K_GetItemListEntry(pid, item_id));
	return 1;
}



// G_INPUT
////////////

static int lib_gSetPlayerControllerIndicatorColor(lua_State *L)
{
	int32_t player = -1;
	player_t *plr = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));    // retrieve player
	uint16_t color = (uint16_t)luaL_checkinteger(L, 2); // skincolor

	if (!plr)
		return LUA_ErrInvalid(L, "player_t");

	if (color >= numskincolors)
		return luaL_error(L, "color %d out of range (0 - %d).", color, numskincolors-1);

	for (int i = 0; i < MAXSPLITSCREENPLAYERS; ++i)
	{
		if (plr - players == displayplayers[i])
		{
			player = i;
			break;
		}
	}

	// Not a local player
	if (player == -1) return 0;

	// pls update with color 0 when youre done with changing led stuff so it can get player color again
	G_SetPlayerControllerIndicatorColor(player, color);

	return 0;
}

static int lib_gPlayerControllerRumble(lua_State *L)
{
	int32_t player = -1;
	player_t *plr = *((player_t **)luaL_checkudata(L, 1, META_PLAYER));    // retrieve player
	uint16_t low_strength = (uint16_t)luaL_checkinteger(L, 2); // low frequency rumble motor strenght
	uint16_t high_strength = (uint16_t)luaL_checkinteger(L, 3); // high frequency rumble motor strenght
	uint32_t duration = (uint32_t)luaL_optinteger(L, 4, 84); // duration of rumble in ms

	if (!plr)
		return LUA_ErrInvalid(L, "player_t");

	if (demo.playback) return 0;

	for (int i = 0; i < MAXSPLITSCREENPLAYERS; ++i)
	{
		if (plr - players == displayplayers[i])
		{
			player = i;
			break;
		}
	}

	// Not a local player
	if (player == -1) return 0;

	G_PlayerControllerRumble(player, low_strength, high_strength, duration);

	return 0;
}

static luaL_Reg lib[] = {
	{"print", lib_print},
	{"chatprint", lib_chatprint},
	{"chatprintf", lib_chatprintf},
	{"userdataType", lib_userdataType},
	{"registerMetatable", lib_registerMetatable},
	{"userdataMetatable", lib_userdataMetatable},
	{"IsPlayerAdmin", lib_isPlayerAdmin},
	{"reserveLuabanks", lib_reserveLuabanks},
	{"tofixed", lib_tofixed},
	{"tofloatstr", lib_toFloatStr},

	// m_menu
	{"M_MoveColorAfter",lib_pMoveColorAfter},
	{"M_MoveColorBefore",lib_pMoveColorBefore},
	{"M_GetColorAfter",lib_pGetColorAfter},
	{"M_GetColorBefore",lib_pGetColorBefore},

	// m_random
	{"P_RandomFixed",lib_pRandomFixed},
	{"P_RandomByte",lib_pRandomByte},
	{"P_RandomKey",lib_pRandomKey},
	{"P_RandomRange",lib_pRandomRange},
	{"P_SignedRandom",lib_pSignedRandom}, // MACRO
	{"P_RandomChance",lib_pRandomChance}, // MACRO

	// p_maputil
	{"P_AproxDistance",lib_pAproxDistance},
	{"P_ClosestPointOnLine",lib_pClosestPointOnLine},
	{"P_PointOnLineSide",lib_pPointOnLineSide},

	// p_enemy
	{"P_CheckMeleeRange", lib_pCheckMeleeRange},
	{"P_JetbCheckMeleeRange", lib_pJetbCheckMeleeRange},
	{"P_FaceStabCheckMeleeRange", lib_pFaceStabCheckMeleeRange},
	{"P_SkimCheckMeleeRange", lib_pSkimCheckMeleeRange},
	{"P_CheckMissileRange", lib_pCheckMissileRange},
	{"P_NewChaseDir", lib_pNewChaseDir},
	{"P_LookForPlayers", lib_pLookForPlayers},

	// p_mobj
	// don't add P_SetMobjState or P_SetPlayerMobjState, use "mobj.state = S_NEWSTATE" instead.
	{"P_SpawnMobj",lib_pSpawnMobj},
	{"P_SpawnMobjFromMobj",lib_pSpawnMobjFromMobj},
	{"P_SpawnMobjFromMobjUnscaled",lib_pSpawnMobjFromMobjUnscaled},
	{"P_RemoveMobj",lib_pRemoveMobj},
	{"P_IsValidSprite2", lib_pIsValidSprite2},
	{"P_SpawnLockOn", lib_pSpawnLockOn},
	{"P_SpawnMissile",lib_pSpawnMissile},
	{"P_SpawnXYZMissile",lib_pSpawnXYZMissile},
	{"P_SpawnPointMissile",lib_pSpawnPointMissile},
	{"P_SpawnAlteredDirectionMissile",lib_pSpawnAlteredDirectionMissile},
	{"P_ColorTeamMissile",lib_pColorTeamMissile},
	{"P_SPMAngle",lib_pSPMAngle},
	{"P_SpawnPlayerMissile",lib_pSpawnPlayerMissile},
	{"P_MobjFlip",lib_pMobjFlip},
	{"P_GetMobjGravity",lib_pGetMobjGravity},
	{"P_FlashPal",lib_pFlashPal},
	{"P_GetClosestAxis",lib_pGetClosestAxis},
	{"P_SpawnParaloop",lib_pSpawnParaloop},
	{"P_BossTargetPlayer",lib_pBossTargetPlayer},
	{"P_SupermanLook4Players",lib_pSupermanLook4Players},
	{"P_SetScale",lib_pSetScale},
	{"P_InsideANonSolidFFloor",lib_pInsideANonSolidFFloor},
	{"P_CheckDeathPitCollide",lib_pCheckDeathPitCollide},
	{"P_CheckSolidLava",lib_pCheckSolidLava},
	{"P_MaceRotate",lib_pMaceRotate},
	{"P_CreateFloorSpriteSlope",lib_pCreateFloorSpriteSlope},
	{"P_RemoveFloorSpriteSlope",lib_pRemoveFloorSpriteSlope},
	{"P_RailThinker",lib_pRailThinker},
	{"P_XYMovement",lib_pXYMovement},
	{"P_RingXYMovement",lib_pRingXYMovement},
	{"P_SceneryXYMovement",lib_pSceneryXYMovement},
	{"P_ZMovement",lib_pZMovement},
	{"P_RingZMovement",lib_pRingZMovement},
	{"P_SceneryZMovement",lib_pSceneryZMovement},
	{"P_PlayerZMovement",lib_pPlayerZMovement},
	{"P_SpawnShadowMobj",lib_pSpawnShadowMobj},

	// p_user
	{"P_GetPlayerHeight",lib_pGetPlayerHeight},
	{"P_GetPlayerSpinHeight",lib_pGetPlayerSpinHeight},
	{"P_AddPlayerScore",lib_pAddPlayerScore},
	{"P_PlayerInPain",lib_pPlayerInPain},
	{"P_ResetPlayer",lib_pResetPlayer},
	{"P_PlayerFullbright",lib_pPlayerFullbright},
	{"P_IsObjectInGoop",lib_pIsObjectInGoop},
	{"P_IsObjectOnGround",lib_pIsObjectOnGround},
	{"P_InQuicksand",lib_pInQuicksand},
	{"P_SetObjectMomZ",lib_pSetObjectMomZ},
	{"P_PlayJingle",lib_pPlayJingle},
	{"P_PlayJingleMusic",lib_pPlayJingleMusic},
	{"P_RestoreMusic",lib_pRestoreMusic},
	{"P_SpawnGhostMobj",lib_pSpawnGhostMobj},
	{"P_GivePlayerRings",lib_pGivePlayerRings},
	{"P_GivePlayerLives",lib_pGivePlayerLives},
	{"P_MovePlayer",lib_pMovePlayer},
	{"P_DoPlayerExit",lib_pDoPlayerExit},
	{"P_InstaThrust",lib_pInstaThrust},
	{"P_ReturnThrustX",lib_pReturnThrustX},
	{"P_ReturnThrustY",lib_pReturnThrustY},
	{"P_NukeEnemies",lib_pNukeEnemies},
	{"P_KartAltShrinkAuraScalar", lib_pKartAltShrinkAuraScalar},

	// p_map
	{"P_CheckPosition",lib_pCheckPosition},
	{"P_TryMove",lib_pTryMove},
	{"P_Move",lib_pMove},
	{"P_TeleportMove",lib_pTeleportMove},
	{"P_SetOrigin",lib_pSetOrigin},
	{"P_MoveOrigin",lib_pMoveOrigin},
	{"P_SetAngle", lib_pSetAngle},
	{"P_SetPitch", lib_pSetPitch},
	{"P_SetRoll", lib_pSetRoll},
	{"P_SlideMove",lib_pSlideMove},
	{"P_BounceMove",lib_pBounceMove},
	{"P_CheckSight", lib_pCheckSight},
	{"P_TraceBlockingLines", lib_pTraceBlockingLines},
	{"P_CheckHoopPosition",lib_pCheckHoopPosition},
	{"P_RadiusAttack",lib_pRadiusAttack},
	{"P_FloorzAtPos",lib_pFloorzAtPos},
	{"P_CeilingzAtPos",lib_pCeilingzAtPos},
	{"P_GetSectorLightLevelAt",lib_pGetSectorLightLevelAt},
	{"P_GetSectorColormapAt",lib_pGetSectorColormapAt},
	{"P_DoSpring",lib_pDoSpring},

	// p_inter
	{"P_DamageMobj",lib_pDamageMobj},
	{"P_KillMobj",lib_pKillMobj},
	{"P_PlayerRingBurst",lib_pPlayerRingBurst},
	{"P_PlayRinglossSound",lib_pPlayRinglossSound},
	{"P_PlayDeathSound",lib_pPlayDeathSound},
	{"P_PlayVictorySound",lib_pPlayVictorySound},
	{"P_PlayLivesJingle",lib_pPlayLivesJingle},
	{"P_CanPickupItem",lib_pCanPickupItem},

	// p_spec
	{"P_Thrust",lib_pThrust},
	{"P_SetMobjStateNF",lib_pSetMobjStateNF},
	{"P_ExplodeMissile",lib_pExplodeMissile},
	{"P_MobjTouchingSectorSpecial",lib_pMobjTouchingSectorSpecial},
	{"P_MobjTouchingSectorSpecialFlag",lib_pMobjTouchingSectorSpecialFlag},
	{"P_ThingOnSpecial3DFloor",lib_pThingOnSpecial3DFloor},
	{"P_FindLowestFloorSurrounding",lib_pFindLowestFloorSurrounding},
	{"P_FindHighestFloorSurrounding",lib_pFindHighestFloorSurrounding},
	{"P_FindNextHighestFloor",lib_pFindNextHighestFloor},
	{"P_FindNextLowestFloor",lib_pFindNextLowestFloor},
	{"P_FindLowestCeilingSurrounding",lib_pFindLowestCeilingSurrounding},
	{"P_FindHighestCeilingSurrounding",lib_pFindHighestCeilingSurrounding},
	{"P_PlayerTouchingSectorSpecial",lib_pPlayerTouchingSectorSpecial},
	{"P_FindSpecialLineFromTag",lib_pFindSpecialLineFromTag},
	{"P_SwitchWeather",lib_pSwitchWeather},
	{"P_LinedefExecute",lib_pLinedefExecute},
	{"P_SpawnLightningFlash",lib_pSpawnLightningFlash},
	{"P_FadeLight",lib_pFadeLight},
	{"P_SetupLevelSky",lib_pSetupLevelSky},
	{"P_SetSkyboxMobj",lib_pSetSkyboxMobj},
	{"P_StartQuake",lib_pStartQuake},
	{"EV_CrumbleChain",lib_evCrumbleChain},
	{"EV_StartCrumble",lib_evStartCrumble},

	// p_slopes
	{"P_GetZAt",lib_pGetZAt},
	{"P_ButteredSlope",lib_pButteredSlope},

	// r_defs
	{"R_PointToAngle",lib_rPointToAngle},
	{"R_PointToAnglePlayer", lib_rPointToAnglePlayer},
	{"R_PointToAngle2",lib_rPointToAngle2},
	{"R_PointToDist",lib_rPointToDist},
	{"R_PointToDist2",lib_rPointToDist2},
	{"R_PointInSubsector",lib_rPointInSubsector},
	{"R_PointInSubsectorOrNil",lib_rPointInSubsectorOrNil},

	// r_things (sprite)
	{"R_Char2Frame",lib_rChar2Frame},
	{"R_Frame2Char",lib_rFrame2Char},
	{"R_SetPlayerSkin",lib_rSetPlayerSkin},
	{"R_SkinUsable",lib_rSkinUsable},

	// r_data
	{"R_CheckTextureNumForName",lib_rCheckTextureNumForName},
	{"R_TextureNumForName",lib_rTextureNumForName},

	// r_draw
	{"R_GetColorByName", lib_rGetColorByName},
	{"R_GetSuperColorByName", lib_rGetSuperColorByName},
	{"R_GetNameByColor", lib_rGetNameByColor},

	// s_sound
	{"S_StartSound",lib_sStartSound},
	{"S_StartSoundAtVolume",lib_sStartSoundAtVolume},
	{"S_StopSound",lib_sStopSound},
	{"S_StopSoundByID",lib_sStopSoundByID},
	{"S_ChangeMusic",lib_sChangeMusic},
	{"S_SpeedMusic",lib_sSpeedMusic},
	{"S_MusicType",lib_sMusicType},
	{"S_MusicPlaying",lib_sMusicPlaying},
	{"S_MusicPaused",lib_sMusicPaused},
	{"S_MusicStopped", lib_sMusicStopped},
	{"S_MusicName",lib_sMusicName},
	{"S_MusicExists",lib_sMusicExists},
	{"S_GetMusicLength",lib_sGetMusicLength},
	{"S_SetMusicLoopPoint",lib_sSetMusicLoopPoint},
	{"S_GetMusicLoopPoint",lib_sGetMusicLoopPoint},
	{"S_SetMusicPosition",lib_sSetMusicPosition},
	{"S_GetMusicPosition",lib_sGetMusicPosition},
	{"S_PauseMusic",lib_sPauseMusic},
	{"S_ResumeMusic",lib_sResumeMusic},
	{"S_StopMusic",lib_sStopMusic},
	{"S_SetInternalMusicVolume", lib_sSetInternalMusicVolume},
	{"S_StopFadingMusic",lib_sStopFadingMusic},
	{"S_FadeMusic",lib_sFadeMusic},
	{"S_FadeOutStopMusic",lib_sFadeOutStopMusic},
	{"S_GetMusicLength",lib_sGetMusicLength},
	{"S_GetMusicPosition",lib_sGetMusicPosition},
	{"S_SetMusicPosition",lib_sSetMusicPosition},
	{"S_OriginPlaying",lib_sOriginPlaying},
	{"S_IdPlaying",lib_sIdPlaying},
	{"S_SoundPlaying",lib_sSoundPlaying},
	{"S_StartMusicCaption", lib_sStartMusicCaption},
	{"S_ShowMusicCredit",lib_sShowMusicCredit},
	{"S_ChangeMusicCreditSettings",lib_sChangeMusicCreditSettings},
	{"S_StopMusicCredit",lib_sStopMusicCredit},

	// g_game
	{"G_AddGametype", lib_gAddGametype},
	{"G_BuildMapName",lib_gBuildMapName},
	{"G_MapNumber",lib_gMapNumber},
	{"G_MapIsSafe",lib_gMapIsSafe},
	{"G_BuildMapTitle",lib_gBuildMapTitle},
	{"G_FindMap",lib_gFindMap},
	{"G_FindMapByNameOrCode",lib_gFindMapByNameOrCode},
	{"G_DoReborn",lib_gDoReborn},
	{"G_SetCustomExitVars",lib_gSetCustomExitVars},
	{"G_ExitLevel",lib_gExitLevel},
	{"G_IsSpecialStage",lib_gIsSpecialStage},
	{"G_GametypeUsesLives",lib_gGametypeUsesLives},
	{"G_GametypeHasTeams",lib_gGametypeHasTeams},
	{"G_GametypeHasSpectators",lib_gGametypeHasSpectators},
	{"G_TicsToHours",lib_gTicsToHours},
	{"G_TicsToMinutes",lib_gTicsToMinutes},
	{"G_TicsToSeconds",lib_gTicsToSeconds},
	{"G_TicsToCentiseconds",lib_gTicsToCentiseconds},
	{"G_TicsToMilliseconds",lib_gTicsToMilliseconds},
	{"getTimeMicros",lib_getTimeMicros},
	{"G_RaceGametype",lib_gRaceGametype},
	{"G_BattleGametype",lib_gBattleGametype},

	// k_kart
	{"K_PlayAttackTaunt", lib_kAttackSound},
	{"K_PlayBoostTaunt", lib_kBoostSound},
	{"K_PlayPowerGloatSound", lib_kGloatSound},
	{"K_PlayOvertakeSound", lib_kOvertakeSound},
	{"K_PlayLossSound", lib_kLossSound},
	{"K_PlayPainSound", lib_kPainSound},
	{"K_PlayHitEmSound", lib_kHitEmSound},
	{"K_TryHurtSoundExchange", lib_kTryHurtSoundExchange},
	{"K_IsPlayerLosing",lib_kIsPlayerLosing},
	{"K_IsPlayerWanted",lib_kIsPlayerWanted},
	{"K_KartBouncing",lib_kKartBouncing},
	{"K_MatchGenericExtraFlags",lib_kMatchGenericExtraFlags},
	{"K_GenericExtraFlagsNoZAdjust",lib_kGenericExtraFlagsNoZAdjust},
	{"K_SpawnDashDustRelease",lib_kSpawnDashDustRelease},
	{"K_SpawnNormalSpeedLines",lib_kSpawnNormalSpeedLines},
	{"K_SpawnBumpEffect",lib_kSpawnBumpEffect},
	{"K_DoInstashield",lib_kDoInstashield},
	{"K_SpawnBattlePoints",lib_kSpawnBattlePoints},
	{"K_SpinPlayer",lib_kSpinPlayer},
	{"K_ExplodePlayer",lib_kExplodePlayer},
	{"K_FlipPlayer",lib_kFlipPlayer},
	{"K_TumblePlayer",lib_kTumblePlayer},
	{"K_ResetCrashData",lib_kResetCrashData},
	{"K_ResetCrashOffsetData",lib_kResetCrashOffsetData},
	{"K_GetOrbiJawzDamage",lib_kGetOrbiJawzDamage},
	{"K_TakeBumpersFromPlayer",lib_kTakeBumpersFromPlayer},
	{"K_SpawnKartExplosion",lib_kSpawnKartExplosion},
	{"K_SpawnMineExplosion",lib_kSpawnMineExplosion},
	{"K_SpawnBoostTrail",lib_kSpawnBoostTrail},
	{"K_SpawnSparkleTrail",lib_kSpawnSparkleTrail},
	{"K_SpawnWipeoutTrail",lib_kSpawnWipeoutTrail},
	{"K_DriftDustHandling",lib_kDriftDustHandling},
	{"K_ThrowKartItem",lib_kThrowKartItem},
	{"K_DoSneaker",lib_kDoSneaker},
	{"K_DoPogoSpring",lib_kDoPogoSpring},
	{"K_KillBananaChain",lib_kKillBananaChain},
	{"K_RepairOrbitChain",lib_kRepairOrbitChain},
	{"K_FindJawzTarget",lib_kFindJawzTarget},
	{"K_GetKartDriftSparkValue",lib_kGetKartDriftSparkValue},
	{"K_KartUpdatePosition",lib_kKartUpdatePosition},
	{"K_UpdateHnextList",lib_kUpdateHnextList},
	{"K_SetItemOut",lib_kSetItemOut},
	{"K_UnsetItemOut",lib_kUnsetItemOut},
	{"K_UnsetItemOutEx",lib_kUnsetItemOutEx},
	{"K_DropItems",lib_kDropItems},
	{"K_StripItems",lib_kStripItems},
	{"K_StripOther",lib_kStripOther},
	{"K_MomentumToFacing",lib_kMomentumToFacing},
	{"K_GetKartSpeed",lib_kGetKartSpeed},
	{"K_GetKartAccel",lib_kGetKartAccel},
	{"K_GetKartFlashing",lib_kGetKartFlashing},
	{"K_GetSpeedPercentage", lib_kGetSpeedPercentage},
	{"K_GetItemPatch",lib_kGetItemPatch},
	{"K_GetItemGraphics",lib_kGetItemGraphics},
	{"K_IsKartItemAlternate",lib_kIsKartItemAlternate},
	{"K_IsAltShrunk", lib_kIsAltShrunk},
	{"K_SetRaceCountdown",lib_kSetRaceCountdown},
	{"K_SetExitCountdown",lib_kSetExitCountdown},
	{"K_SetIndirectItemCooldown",lib_kSetIndirectItemCountdown},
	{"K_SetHyudoroCooldown",lib_kSetHyuCountdown},
	{"K_SafeRespawnPosition",lib_kSafeRespawnPosition},
	{"K_SafeRespawnPositionEx",lib_kSafeRespawnPositionEx},
	{"K_KillAirDrop", lib_kKillAirDrop},

	{"K_GetCollideAngle",lib_kGetCollideAngle},

	{"K_RingsActive",lib_kRingsActive},
	{"K_PurpleDriftActive",lib_kPurpleDriftActive},
	{"K_StackingActive",lib_kStackingActive},
	{"K_ChainingActive",lib_kChainingActive},
	{"K_SlipdashActive",lib_kSlipdashActive},
	{"K_SlopeBoostActive",lib_kSlopeBoostActive},
	{"K_DraftingActive",lib_kDraftingActive},
	{"K_AirDropActive",lib_kAirDropActive},
	{"K_AirThrustActive",lib_kAirThrustActive},
	{"K_RecoveryDashActive",lib_kRecoveryDashActive},
	{"K_KeepStuffActive",lib_kKeepStuffActive},
	{"K_ItemLitterActive",lib_kItemLitterActive},
	{"K_ItemListActive", lib_kItemListActive},
	{"K_TrailSlowActive", lib_kTrailSlowActive},
	{"K_RankingsActive", lib_kRankingsActive},
	{"K_GetBumpSpark",lib_kGetBumpSpark},
	{"K_GetPlayerBump",lib_kGetPlayerBump},
	{"K_UsingLegacyCheckpoints",lib_kUsingLegacyCheckpoints},
	{"K_DoBoost",lib_kDoBoost},
	{"K_ClearBoost",lib_kClearBoost},
	{"K_BoostChain",lib_kBoostChain},
	{"K_ChainOrDeincrementTime",lib_kChainOrDeincrementTime},
	{"K_GetNewSpeed", lib_kGetNewSpeed},
	{"K_SMonitorGradient", lib_kSMonitorGradient},
	{"K_GetSMonitorSpeed", lib_kGetSMonitorSpeed},
	{"K_GetSMonitorAccel", lib_kGetSMonitorAccel},
	{"K_GetSMonitorHandling", lib_kGetSMonitorHandling},
	{"K_3dKartMovement", lib_k3dKartMovement},
	{"K_MomentumAngle", lib_kMomentumAngle},
	{"K_GetKartSpeedFromStat", lib_kGetKartSpeedFromStat},
	{"K_GetKartSpeedFromStatEx", lib_kGetKartSpeedFromStatEx},
	{"K_KartKickstart", lib_kKartKickstart},
	{"K_GetKartButtons", lib_kGetKartButtons},
	{"K_GetForwardMove", lib_kGetForwardMove},
	{"K_AwardPlayerRings", lib_kAwardPlayerRings},
	{"K_AwardScaledPlayerRings", lib_kAwardScaledPlayerRings},
	{"K_GetShieldFromPlayer", lib_kGetShieldFromPlayer},
	{"K_PlayerCanPunt", lib_kPlayerCanPunt},
	{"K_Momentum3D", lib_kMomentum3D},
	{"K_GetClusterDistanceForPlayer", lib_kGetClusterDistanceForPlayer},
	{"K_GetKartBaseRingPower", lib_kGetKartBaseRingPower},
	{"K_GetKartRingCap", lib_kGetKartRingCap},
	{"K_IsPlayerRingBurnt", lib_kIsPlayerRingBurnt},
	{"K_GetKartRingPower", lib_kGetKartRingPower},
	{"K_KartPlayerBumpyRoad", lib_kKartPlayerBumpyRoad},
	{"K_HandleAirDrag",lib_kHandleAirdrag},
	
	{"K_AddTiltImpulse", lib_kAddTiltImpulse},
	{"K_AddTiltMomentum", lib_kAddTiltMomentum},

	{"K_ShoveObject", lib_kShoveObject},

	{"K_GetRankTier", lib_kGetRankTier},
	{"K_GetScoreForRankTier", lib_kGetScoreForRankTier},
	{"K_GetDriftBoostRankScore", lib_kGetDriftBoostRankScore},
	{"K_CalculateLapGrading", lib_kCalculateLapGrading},
	{"K_UpdateIndividualPlayerRank", lib_kUpdateIndividualPlayerRank},

	// kart stuff that's in other places for some reason
	{"K_IsMissileOrKartItem", lib_kMissileOrKartItem},

	// k_hud
	{"K_AddNewScoreboardMod", lib_kAddNewScoreboardMod},
	{"K_SetScoreboardModStatus", lib_kSetScoreboardModStatus},

	// k_waypoint
	{"K_NextRespawnWaypointIndex", lib_kNextRespawnWaypointIndex},
	{"K_GetFinishLineWaypoint", lib_kGetFinishLineWaypoint},
	{"K_GetStartingWaypoint", lib_kGetStartingWaypoint},
	{"K_GetWaypointIsFinishline", lib_kGetWaypointIsFinishline},
	{"K_GetWaypointIsShortcut", lib_kGetWaypointIsShortcut},
	{"K_GetWaypointIsEnabled", lib_kGetWaypointIsEnabled},
	{"K_SetWaypointIsEnabled", lib_kSetWaypointIsEnabled},
	{"K_GetWaypointIsSpawnpoint", lib_kGetWaypointIsSpawnpoint},
	{"K_GetWaypointNextID", lib_kGetWaypointNextID},
	{"K_GetWaypointID", lib_kGetWaypointID},
	{"K_GetWaypointFromID", lib_kGetWaypointFromID},
	{"K_GetCircuitLength", lib_kGetCircuitLength},
	{"K_GetTrackComplexity", lib_kGetTrackComplexity},
	{"K_GetClosestWaypointToMobj", lib_kGetClosestWaypointToMobj},
	{"K_GetBestWaypointForMobj", lib_kGetBestWaypointForMobj},
	{"K_PathfindToWaypoint", lib_kPathfindToWaypoint},
	{"K_PathfindThruCircuit", lib_kPathfindThruCircuit},
	{"K_PathfindThruCircuitSpawnable", lib_kPathfindThruCircuitSpawnable},
	{"K_GetNextWaypointToDestination", lib_kGetNextWaypointToDestination},
	{"K_SearchWaypointGraphForMobj", lib_kSearchWaypointGraphForMobj},
	{"K_SearchWaypointHeapForMobj", lib_kSearchWaypointHeapForMobj},

	// k_boss
	{"K_InitBossHealthBar", lib_kInitBossHealthBar},
	{"K_UpdateBossHealthBar", lib_kUpdateBossHealthBar},
	{"K_DeclareWeakspot", lib_kDeclareWeakspot},

	// I_System
	{"I_GetPreciseTime",lib_iGetPreciseTime},

	// k_bot
	{"K_PlayerUsesBotMovement", lib_kPlayerUsesBotMovement},
	{"K_BotCanTakeCut", lib_kBotCanTakeCut},
	{"K_GetBotController", lib_kGetBotController},
	{"K_BotMapModifier", lib_kBotMapModifier},
	{"K_BotRubberband", lib_kBotRubberband},
	{"K_UpdateRubberband", lib_kUpdateRubberband},
	{"K_DistanceOfLineFromPoint", lib_kDistanceOfLineFromPoint},
	{"K_AddBot", lib_kAddBot},
	{"K_SetNameForBot", lib_kSetNameForBot},
	// Lua-only function to allow safely removing bots.
	{"K_RemoveBot", lib_kRemoveBot},

	// k_terrain
	{"K_AffectingTerrainActive", lib_kAffectingTerrainActive},
	{"K_GetDefaultTerrain", lib_kGetDefaultTerrain},
	{"K_GetTerrainForTextureName", lib_kGetTerrainForTextureName},
	{"K_GetTerrainForTextureNum", lib_kGetTerrainForTextureNum},
	{"K_ProcessTerrainEffect", lib_kProcessTerrainEffect},
	{"K_SpawnSplashForMobj", lib_kSpawnSplashForMobj},
	{"K_HandleFootstepParticles", lib_kHandleFootstepParticles},
	{"K_UpdateTerrainOverlay", lib_kUpdateTerrainOverlay},
	{"K_TerrainHasAffect", lib_kTerrainHasAffect},

	// k_items
	{"K_AwardPlayerItem", lib_kAwardPlayerItem},
	{"K_AwardPlayerItemOrReserve", lib_kAwardPlayerItemOrReserve},
	{"K_AddPlayerReserveItem", lib_kAddPlayerReserveItem},
	{"K_StartRoulette", lib_kStartRoulette},
	{"K_SetPlayerItemCooldown", lib_kSetPlayerItemCooldown},
	{"K_SpawnEquippedItem",lib_kSpawnEquippedItem},
	{"K_EnableSeekingReticule",lib_kEnableSeekingReticule},
	{"K_DisableSeekingReticule",lib_kDisableSeekingReticule},
	{"K_DecrementAndCheckCurrentItem",lib_kDecrementAndCheckCurrentItem},
	{"K_DontDoubleMyItems",lib_kDontDoubleMyItems},
	{"K_LegacyOddsMode",lib_kLegacyOddsMode},
	{"K_IntDistanceForMap",lib_kIntDistanceForMap},
	{"K_GetCongaLineDistance",lib_kGetCongaLineDistance},
	{"K_GetDTFDifference",lib_kGetDTFDifference},
	{"K_GetDistanceBetweenPlayers",lib_kGetDistanceBetweenPlayers},

	// k_itemlist
	{"K_AddItemRollToList", lib_kAddItemRollToList},
	{"K_GetItemListSize", lib_kGetItemListSize},
	{"K_SetItemListEntry", lib_kSetItemListEntry},
	{"K_GetItemListEntry", lib_kGetItemListEntry},

	//g_input
	//{"G_SetPlayerControllerIndicatorColor", lib_gSetPlayerControllerIndicatorColor},
	//{"G_PlayerControllerRumble", lib_gPlayerControllerRumble},
	// gotta keep backwards compat :chaosleep:
	{"G_SetPlayerGamepadIndicatorColor", lib_gSetPlayerControllerIndicatorColor},
	{"G_PlayerDeviceRumble", lib_gPlayerControllerRumble},

	{NULL, NULL}
};

int LUA_BaseLib(lua_State *L)
{
	// Set metatable for string
	lua_pushliteral(L, "");  // dummy string
	lua_getmetatable(L, -1);  // get string metatable
	lua_pushcfunction(L,lib_concat); // push concatination function
	lua_setfield(L,-2,"__add"); // ... store it as mathematical addition
	lua_pop(L, 2); // pop metatable and dummy string

	lua_newtable(L);
	lua_setfield(L, LUA_REGISTRYINDEX, LREG_EXTVARS);

	// Set global functions
	lua_pushvalue(L, LUA_GLOBALSINDEX);
	luaL_register(L, NULL, lib);
	return 0;
}
