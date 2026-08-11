// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  deh_lua.c
/// \brief Lua SOC library

#include "deh_lua.h"
#include "deh_tables.h"

// freeslot takes a name (string only!)
// and allocates it to the appropriate free slot.
// Returns the slot number allocated for it or nil if failed.
// ex. freeslot("MT_MYTHING","S_MYSTATE1","S_MYSTATE2")
// TODO: Error checking! @.@; There's currently no way to know which ones failed and why!
//
static inline int lib_freeslot(lua_State *L)
{
	int n = lua_gettop(L);
	int r = 0; // args returned
	char *s, *type,*word;

	if (!lua_lumploading)
		return luaL_error(L, "This function cannot be called from within a hook or coroutine!");

	while (n-- > 0)
	{
		s = Z_StrDup(luaL_checkstring(L,1));
		type = strtok(s, "_");
		if (type)
			strupr(type);
		else {
			Z_Free(s);
			return luaL_error(L, "Unknown enum type in '%s'\n", luaL_checkstring(L, 1));
		}

		word = strtok(NULL, "\n");
		if (word)
			strupr(word);
		else {
			Z_Free(s);
			return luaL_error(L, "Missing enum name in '%s'\n", luaL_checkstring(L, 1));
		}

		if (fastcmp(type, "SFX"))
			strlwr(word);

		INT32 index;
		if (!DEH_ReadFreeslot(type, word, &index))
		{
			if (lua_compatmode)
			{
				if (fastcmp(type, "MT"))
					index -= MT_FIRSTFREESLOT;
				if (fastcmp(type, "S"))
					index -= S_FIRSTFREESLOT;
			}

			lua_pushinteger(L, index);
			r++;
		}

		lua_remove(L, 1);
		continue;
	}
	return r;
}

// Wrapper for ALL A_Action functions.
// Arguments: mobj_t actor, int var1, int var2
static int action_call(lua_State *L)
{
	//actionf_p1 *action = lua_touserdata(L,lua_upvalueindex(1));
	actionf_p1 *action = *((actionf_p1 **)luaL_checkudata(L, 1, META_ACTION));
	mobj_t *actor = *((mobj_t **)luaL_checkudata(L, 2, META_MOBJ));
	var1 = (INT32)luaL_optinteger(L, 3, 0);
	var2 = (INT32)luaL_optinteger(L, 4, 0);
	if (!actor)
		return LUA_ErrInvalid(L, "mobj_t");
	(*action)(actor);
	return 0;
}

// Hardcoded A_Action name to call for super() or NULL if super() would be invalid.
// Set in lua_infolib.
const char *superactions[MAXRECURSION] = {};
UINT8 superstack = 0;

static int lib_dummysuper(lua_State *L)
{
	return luaL_error(L, "Can't call super() outside of hardcode-replacing A_Action functions being called by state changes!"); // convoluted, I know. @_@;;
}

static void CacheAndPushConstant(lua_State *L, const char *name, lua_Integer value)
{
	// "cache" into _G
	lua_pushstring(L, name);
	lua_pushinteger(L, value);
	lua_rawset(L, LUA_GLOBALSINDEX);
	// push
	lua_pushinteger(L, value);
}

// Search for a matching constant variable.
// Result is stored into _G for faster subsequent use. (Except for SPR_ in the SOC parser)
static int ScanConstants(lua_State *L, dboolean mathlib, const char *word)
{
	const char *p;
	size_t i;

	if (strlen(word) == 1) { // Assume sprite frame if length 1.
		if (*word >= 'A' && *word <= '~')
		{
			CacheAndPushConstant(L, word, *word-'A');
			return 1;
		}
		if (mathlib) return luaL_error(L, "constant '%s' could not be parsed.\n", word);
		return 0;
	}
	else if (fastncmp("MF_", word, 3)) {
		p = word+3;
		for (i = 0; MOBJFLAG_LIST[i]; i++)
			if (fastcmp(p, MOBJFLAG_LIST[i])) {
				CacheAndPushConstant(L, word, ((lua_Integer)1<<i));
				return 1;
			}
		if (mathlib) return luaL_error(L, "mobjflag '%s' could not be found.\n", word);
		return 0;
	}
	else if (fastncmp("MF2_", word, 4)) {
		p = word+4;
		for (i = 0; MOBJFLAG2_LIST[i]; i++)
			if (fastcmp(p, MOBJFLAG2_LIST[i])) {
				CacheAndPushConstant(L, word, ((lua_Integer)1<<i));
				return 1;
			}
		if (mathlib) return luaL_error(L, "mobjflag2 '%s' could not be found.\n", word);
		return 0;
	}
	else if (fastncmp("MFE_", word, 4)) {
		p = word+4;
		for (i = 0; MOBJEFLAG_LIST[i]; i++)
			if (fastcmp(p, MOBJEFLAG_LIST[i])) {
				CacheAndPushConstant(L, word, ((lua_Integer)1<<i));
				return 1;
			}
		if (mathlib) return luaL_error(L, "mobjeflag '%s' could not be found.\n", word);
		return 0;
	}
	else if (fastncmp("MTF_", word, 4)) {
		p = word+4;
		for (i = 0; i < 4; i++)
			if (MAPTHINGFLAG_LIST[i] && fastcmp(p, MAPTHINGFLAG_LIST[i])) {
				CacheAndPushConstant(L, word, ((lua_Integer)1<<i));
				return 1;
			}
		if (mathlib) return luaL_error(L, "mapthingflag '%s' could not be found.\n", word);
		return 0;
	}
	else if (fastncmp("PF_", word, 3)) {
		p = word+3;
		for (i = 0; PLAYERFLAG_LIST[i]; i++)
			if (fastcmp(p, PLAYERFLAG_LIST[i])) {
				CacheAndPushConstant(L, word, ((lua_Integer)1<<i));
				return 1;
			}
		if (lua_compatmode)
			for (i = 0; PLAYERFLAG_ALIASES[i].n; i++)
				if (fastcmp(p, PLAYERFLAG_ALIASES[i].n)) {
					CacheAndPushConstant(L, word, PLAYERFLAG_ALIASES[i].v);
					return 1;
				}
		if (mathlib) return luaL_error(L, "playerflag '%s' could not be found.\n", word);
		return 0;
	}
	else if (fastncmp("PAF_", word, 4)) {
		p = word+4;
		for (i = 0; AIRDROPFLAG_LIST[i]; i++)
		{
			if (fastcmp(p, AIRDROPFLAG_LIST[i])) {
				CacheAndPushConstant(L, word, ((lua_Integer)1<<i));
				return 1;
			}
		}
		if (mathlib) return luaL_error(L, "airdropflag '%s' could not be found.\n", word);
		return 0;
	}
	else if (fastncmp("IF_", word, 3)) {
		p = word+3;
		for (i = 0; ITEMFLAG_LIST[i]; i++)
			if (fastcmp(p, ITEMFLAG_LIST[i])) {
				CacheAndPushConstant(L, word, ((lua_Integer)1<<i));
				return 1;
			}
		if (mathlib) return luaL_error(L, "itemflag '%s' could not be found.\n", word);
		return 0;
	}
	else if (fastncmp("GT_", word, 3)) {
		p = word;
		i = 0;
		INT32 j = 0;
		while (gametypes[i] != NULL)
		{
			if (fastcmp(p, gametypes[i]->constant))
			{
				CacheAndPushConstant(L, word, i);
				return 1;
			}

			if (lua_compatmode)
				for (j = 0; GAMETYPE_ALIASES[j].n; j++)
					if (fastcmp(p, GAMETYPE_ALIASES[j].n)) {
						CacheAndPushConstant(L, word, GAMETYPE_ALIASES[j].v);
						return 1;
					}

			i++;
		}
		if (mathlib) return luaL_error(L, "gametype '%s' could not be found.\n", word);
		return 0;
	}
	else if (fastncmp("GTR_", word, 4)) {
		p = word+4;
		for (i = 0; GAMETYPERULE_LIST[i]; i++)
			if (fastcmp(p, GAMETYPERULE_LIST[i])) {
				CacheAndPushConstant(L, word, ((lua_Integer)1<<i));
				return 1;
			}
		if (mathlib) return luaL_error(L, "game type rule '%s' could not be found.\n", word);
		return 0;
	}
	else if (fastncmp("TOL_", word, 4)) {
		p = word+4;
		for (i = 0; TYPEOFLEVEL[i].name; i++)
			if (fastcmp(p, TYPEOFLEVEL[i].name)) {
				CacheAndPushConstant(L, word, TYPEOFLEVEL[i].flag);
				return 1;
			}
		if (mathlib) return luaL_error(L, "typeoflevel '%s' could not be found.\n", word);
		return 0;
	}
	else if (fastncmp("ML_", word, 3)) {
		p = word+3;
		const char *const *list = lua_compatmode ? ML_LIST_KART : ML_LIST;
		for (i = 0; list[i]; i++)
			if (fastcmp(p, list[i])) {
				CacheAndPushConstant(L, word, ((lua_Integer)1<<i));
				return 1;
			}
		if (mathlib) return luaL_error(L, "linedef flag '%s' could not be found.\n", word);
		return 0;
	}
	else if (fastncmp("MSF_", word, 4)) {
		p = word + 4;
		for (i = 0; MSF_LIST[i]; i++)
			if (fastcmp(p, MSF_LIST[i])) {
				lua_pushinteger(L, ((lua_Integer)1 << i));
				return 1;
			}
		if (fastcmp(p, "FLIPSPECIAL_BOTH"))
		{
			lua_pushinteger(L, (lua_Integer)MSF_FLIPSPECIAL_BOTH);
			return 1;
		}
		if (mathlib) return luaL_error(L, "sector flag '%s' could not be found.\n", word);
		return 0;
	}
	else if (fastncmp("SSF_", word, 4)) {
		p = word + 4;
		for (i = 0; SSF_LIST[i]; i++)
			if (fastcmp(p, SSF_LIST[i])) {
				lua_pushinteger(L, ((lua_Integer)1 << i));
				return 1;
			}
		if (mathlib) return luaL_error(L, "sector special flag '%s' could not be found.\n", word);
		return 0;
	}
	else if (fastncmp("SD_", word, 3)) {
		p = word + 3;
		for (i = 0; SD_LIST[i]; i++)
			if (fastcmp(p, SD_LIST[i])) {
				lua_pushinteger(L, i);
				return 1;
			}
		if (mathlib) return luaL_error(L, "sector damagetype '%s' could not be found.\n", word);
		return 0;
	}
	else if (fastncmp("TO_", word, 3)) {
		p = word + 3;
		for (i = 0; TO_LIST[i]; i++)
			if (fastcmp(p, TO_LIST[i])) {
				lua_pushinteger(L, i);
				return 1;
			}
		if (mathlib) return luaL_error(L, "sector triggerer '%s' could not be found.\n", word);
		return 0;
	}
	else if (fastncmp("S_",word,2)) {
		p = word+2;
		if (fastcmp(p, "FIRSTFREESLOT"))
		{
			lua_pushinteger(L, S_FIRSTFREESLOT);
			return 1;
		}
		i = DEH_FindState(p);
		if (i != NUMSTATES)
		{
			CacheAndPushConstant(L, word, i);
			return 1;
		}
		if (lua_compatmode)
			for (i = 0; STATE_ALIASES[i].n; i++)
				if (fastcmp(p, STATE_ALIASES[i].n)) {
					CacheAndPushConstant(L, word, STATE_ALIASES[i].v);
					return 1;
				}
		return luaL_error(L, "state '%s' does not exist.\n", word);
	}
	else if (fastncmp("MT_",word,3)) {
		p = word+3;
		if (fastcmp(p, "FIRSTFREESLOT"))
		{
			lua_pushinteger(L, MT_FIRSTFREESLOT);
			return 1;
		}
		i = DEH_FindMobjtype(p);
		if (i != NUMMOBJTYPES)
		{
			CacheAndPushConstant(L, word, i);
			return 1;
		}
		if (lua_compatmode)
			for (i = 0; MOBJ_ALIASES[i].n; i++)
				if (fastcmp(p, MOBJ_ALIASES[i].n)) {
					CacheAndPushConstant(L, word, MOBJ_ALIASES[i].v);
					return 1;
				}
		return luaL_error(L, "mobjtype '%s' does not exist.\n", word);
	}
	else if (fastncmp("SPR_",word,4)) {
		p = word+4;
		if (fastcmp(p, "FIRSTFREESLOT"))
		{
			lua_pushinteger(L, SPR_FIRSTFREESLOT);
			return 1;
		}
		for (i = 0; i < NUMSPRITES; i++)
			if (fastncmp(p,sprnames[i],4)) {
				// updating overridden sprnames is not implemented for soc parser,
				// so don't use cache
				if (mathlib)
					lua_pushinteger(L, i);
				else
					CacheAndPushConstant(L, word, i);
				return 1;
			}
		if (mathlib) return luaL_error(L, "sprite '%s' could not be found.\n", word);
		return 0;
	}
	else if (fastncmp("SPR2_",word,5)) {
		p = word+5;
		for (i = 0; i < free_spr2; i++)
			if (!spr2names[i][4])
			{
				// special 3-char cases, e.g. SPR2_RUN
				// the spr2names entry will have "_" on the end, as in "RUN_"
				if (spr2names[i][3] == '_' && !p[3]) {
					if (fastncmp(p,spr2names[i],3)) {
						CacheAndPushConstant(L, word, i);
						return 1;
					}
				}
				else if (fastncmp(p,spr2names[i],4)) {
					CacheAndPushConstant(L, word, i);
					return 1;
				}
			}
		if (mathlib) return luaL_error(L, "player sprite '%s' could not be found.\n", word);
		return 0;
	}
	else if (fastncmp("sfx_",word,4)) {
		p = word+4;
		for (i = 0; i < NUMSFX; i++)
			if (S_sfx[i].name && fasticmp(p, S_sfx[i].name)) {
				CacheAndPushConstant(L, word, i);
				return 1;
			}
		return 0;
	}
	else if (mathlib && fastncmp("SFX_",word,4)) { // SOCs are ALL CAPS!
		p = word+4;
		for (i = 0; i < NUMSFX; i++)
			if (S_sfx[i].name && fasticmp(p, S_sfx[i].name)) {
				CacheAndPushConstant(L, word, i);
				return 1;
			}
		return luaL_error(L, "sfx '%s' could not be found.\n", word);
	}
	else if (mathlib && fastncmp("DS",word,2)) {
		p = word+2;
		for (i = 0; i < NUMSFX; i++)
			if (S_sfx[i].name && fasticmp(p, S_sfx[i].name)) {
				CacheAndPushConstant(L, word, i);
				return 1;
			}
		if (mathlib) return luaL_error(L, "sfx '%s' could not be found.\n", word);
		return 0;
	}
	else if (!mathlib && fastncmp("pw_",word,3)) {
		p = word+3;
		for (i = 0; i < NUMPOWERS; i++)
			if (fasticmp(p, POWERS_LIST[i])) {
				lua_pushinteger(L, i);
				return 1;
			}
		return 0;
	}
	else if (mathlib && fastncmp("PW_",word,3)) { // SOCs are ALL CAPS!
		p = word+3;
		for (i = 0; i < NUMPOWERS; i++)
			if (fastcmp(p, POWERS_LIST[i])) {
				lua_pushinteger(L, i);
				return 1;
			}
		return luaL_error(L, "powers '%s' could not be found.\n", word);
	}
	else if (!mathlib && fastncmp("k_",word,2)) {
		p = word+2;
		for (i = 0; i < NUMKARTSTUFF; i++)
			if (fasticmp(p, KARTSTUFF_LIST[i])) {
				lua_pushinteger(L, i);
				return 1;
			}
		return 0;
	}
	else if (mathlib && fastncmp("K_",word,2)) { // SOCs are ALL CAPS!
		p = word+2;
		for (i = 0; i < NUMKARTSTUFF; i++)
			if (fastcmp(p, KARTSTUFF_LIST[i])) {
				lua_pushinteger(L, i);
				return 1;
			}
		return luaL_error(L, "kartstuff '%s' could not be found.\n", word);
	}
	else if (!mathlib && fastncmp("khud_",word,5)) {
		p = word+5;
		for (i = 0; i < NUMKARTHUD; i++)
			if (fasticmp(p, KARTHUD_LIST[i])) {
				lua_pushinteger(L, i);
				return 1;
			}
		return 0;
	}
	else if (mathlib && fastncmp("KHUD_",word,5)) { // SOCs are ALL CAPS!
		p = word+5;
		for (i = 0; i < NUMKARTHUD; i++)
			if (fastcmp(p, KARTHUD_LIST[i])) {
				lua_pushinteger(L, i);
				return 1;
			}
		return luaL_error(L, "karthud '%s' could not be found.\n", word);
	}
	else if (fastncmp("KITEM_",word,6)) {
		p = word+6;
		if (fastcmp(p, "LIGHTNINGSHIELD"))
		{
			lua_pushinteger(L, KITEM_THUNDERSHIELD);
			return 1;
		}
		if (fastcmp(p, "SAD"))
		{
			lua_pushinteger(L, MAXKARTITEMS);
			return 1;
		}
		i = DEH_FindKartItem(p);
		if (i != MAXKARTITEMS)
		{
			CacheAndPushConstant(L, word, i);
			return 1;
		}
		return luaL_error(L, "kartitem '%s' could not be found.\n", word);
	}
	else if (fastncmp("KRITEM_", word, 7)) {
		if (!lua_compatmode)
			return luaL_error(L, "KRITEM constants are not supported in BlanKart. See the wiki for information about the new item system.\n");

		p = word+7;
		if (fastcmp(p, "TRIPLESNEAKER"))
			lua_pushinteger(L, 17);
		else if (fastcmp(p, "TRIPLEBANANA"))
			lua_pushinteger(L, 18);
		else if (fastcmp(p, "TENFOLDBANANA"))
			lua_pushinteger(L, 19);
		else if (fastcmp(p, "TRIPLEORBINAUT"))
			lua_pushinteger(L, 20);
		else if (fastcmp(p, "QUADORBINAUT"))
			lua_pushinteger(L, 21);
		else if (fastcmp(p, "DUALJAWZ"))
			lua_pushinteger(L, 22);
		else
			return 0;
		return 1;
	}
	else if (fastncmp("SKINCOLOR_",word,10)) {
		p = word+10;
		i = DEH_FindSkincolor(p);
		if (i != MAXSKINCOLORS)
		{
			CacheAndPushConstant(L, word, i);
			return 1;
		}
		return luaL_error(L, "skincolor '%s' could not be found.\n", word);
	}
	else if (fastncmp("MN_",word,3)) {
		p = word+3;
		i = DEH_FindMenutype(p);
		if (i != MAXMENUTYPES)
		{
			CacheAndPushConstant(L, word, i);
			return 1;
		}
		if (mathlib) return luaL_error(L, "menutype '%s' could not be found.\n", word);
		return 0;
	}
	else if (fastncmp("PRECIP_",word,7)) {
		p = word+7;
		for (i = 0; i < MAXPRECIP; i++)
		{
			if (precipprops[i].name == NULL)
				break;

			if (fastcmp(p, precipprops[i].name))
			{
				lua_pushinteger(L, PRECIP_NONE + i);
				return 1;
			}
		}
		return luaL_error(L, "weather type '%s' does not exist.\n", word);
	}

	for (i = 0; INT_CONST[i].n; i++)
		if (fastcmp(word,INT_CONST[i].n)) {
			CacheAndPushConstant(L, word, INT_CONST[i].v);
			return 1;
		}

	return 0;
}

static inline int lib_getenum(lua_State *L)
{
	const char *word;
	fixed_t i;
	dboolean mathlib = lua_todboolean(L, lua_upvalueindex(1));
	if (lua_type(L,2) != LUA_TSTRING)
		return 0;
	word = lua_tostring(L,2);

	// check actions, super and globals first, as they don't have _G caching implemented
	// so they benefit from being checked first

	if (!mathlib && fastncmp("A_",word,2)) {
		char *caps;
		// Try to get a Lua action first.
		/// \todo Push a closure that sets superactions[] and superstack.
		lua_getfield(L, LUA_REGISTRYINDEX, LREG_ACTIONS);
		// actions are stored in all uppercase.
		caps = Z_StrDup(word);
		strupr(caps);
		lua_getfield(L, -1, caps);
		Z_Free(caps);
		if (!lua_isnil(L, -1))
			return 1; // Success! :D That was easy.
		// Welp, that failed.
		lua_pop(L, 2); // pop nil and LREG_ACTIONS

		// Hardcoded actions as callable Lua functions!
		// Retrieving them from this metatable allows them to be case-insensitive!
		for (i = 0; actionpointers[i].name; i++)
			if (fasticmp(word, actionpointers[i].name)) {
				// We push the actionf_p1* itself as userdata!
				LUA_PushUserdata(L, &actionpointers[i].action, META_ACTION);
				return 1;
			}
		return 0;
	}
	else if (!mathlib && fastcmp("super",word))
	{
		if (!superstack)
		{
			lua_pushcfunction(L, lib_dummysuper);
			return 1;
		}
		for (i = 0; actionpointers[i].name; i++)
			if (fasticmp(superactions[superstack-1], actionpointers[i].name)) {
				LUA_PushUserdata(L, &actionpointers[i].action, META_ACTION);
				return 1;
			}
		return 0;
	}
	else if ((!mathlib && LUA_PushGlobals(L)) || ScanConstants(L, mathlib, word))
		return 1;

	if (mathlib) return luaL_error(L, "constant '%s' could not be parsed.\n", word);

	return 0;
}

// If a sprname has been "cached" to _G, update it to a new value.
void LUA_UpdateSprName(const char *name, lua_Integer value)
{
	char fullname[9] = "SPR_XXXX";

	if (!gL)
		return;

	strncpy(&fullname[4], name, 4);
	lua_pushstring(gL, fullname);
	lua_rawget(gL, LUA_GLOBALSINDEX);

	if (!lua_isnil(gL, -1))
	{
		lua_pushstring(gL, fullname);
		lua_pushinteger(gL, value);
		lua_rawset(gL, LUA_GLOBALSINDEX);
	}

	lua_pop(gL, 1); // pop the rawget result
}

int LUA_EnumLib(lua_State *L)
{
	if (lua_gettop(L) == 0)
		lua_pushdboolean(L, 0);

	global_fields_ref = Lua_CreateFieldTable(L, global_opt);

	// Set the global metatable
	lua_createtable(L, 0, 1);
	lua_pushvalue(L, 1); // dboolean passed to LUA_EnumLib as first argument.
	lua_pushcclosure(L, lib_getenum, 1);
	lua_setfield(L, -2, "__index");
	lua_setmetatable(L, LUA_GLOBALSINDEX);
	return 0;
}

// getActionName(action) -> return action's string name
static int lib_getActionName(lua_State *L)
{
	if (lua_isuserdata(L, 1)) // arg 1 is built-in action, expect action userdata
	{
		actionf_p1 *action = *((actionf_p1 **)luaL_checkudata(L, 1, META_ACTION));
		const char *name = NULL;
		if (!action)
			return luaL_error(L, "not a valid action?");
		name = LUA_GetActionName(action);
		if (!name) // that can't be right?
			return luaL_error(L, "no name string could be found for this action");
		lua_pushstring(L, name);
		return 1;
	}
	else if (lua_isfunction(L, 1)) // arg 1 is a function (either C or Lua)
	{
		lua_settop(L, 1); // set top of stack to 1 (removing any extra args, which there shouldn't be)
		// get the name for this action, if possible.
		lua_getfield(L, LUA_REGISTRYINDEX, LREG_ACTIONS);
		lua_pushnil(L);
		// Lua stack at this point:
		//  1   ...       -2              -1
		// arg  ...   LREG_ACTIONS        nil
		while (lua_next(L, -2))
		{
			// Lua stack at this point:
			//  1   ...       -3              -2           -1
			// arg  ...   LREG_ACTIONS    "A_ACTION"    function
			if (lua_rawequal(L, -1, 1)) // is this the same as the arg?
			{
				// make sure the key (i.e. "A_ACTION") is a string first
				// (note: we don't use lua_isstring because it also returns true for numbers)
				if (lua_type(L, -2) == LUA_TSTRING)
				{
					lua_pushvalue(L, -2); // push "A_ACTION" string to top of stack
					return 1;
				}
				lua_pop(L, 2); // pop the name and function
				break; // probably should have succeeded but we didn't, so end the loop
			}
			lua_pop(L, 1);
		}
		lua_pop(L, 1); // pop LREG_ACTIONS
		return 0; // return nothing (don't error)
	}

	return luaL_typerror(L, 1, "action userdata or Lua function");
}



int LUA_SOCLib(lua_State *L)
{
	lua_register(L,"freeslot",lib_freeslot);
	lua_register(L,"getActionName",lib_getActionName);

	luaL_newmetatable(L, META_ACTION);
		lua_pushcfunction(L, action_call);
		lua_setfield(L, -2, "__call");
	lua_pop(L, 1);

	return 0;
}

const char *LUA_GetActionName(void *action)
{
	actionf_p1 *act = (actionf_p1 *)action;
	size_t z;
	for (z = 0; actionpointers[z].name; z++)
	{
		if (actionpointers[z].action == *act)
			return actionpointers[z].name;
	}
	return NULL;
}

void LUA_SetActionByName(void *state, const char *actiontocompare)
{
	state_t *st = (state_t *)state;
	size_t z;
	for (z = 0; actionpointers[z].name; z++)
	{
		if (fasticmp(actiontocompare, actionpointers[z].name))
		{
			st->action = actionpointers[z].action;
			return;
		}
	}
}

size_t LUA_GetActionNumByName(const char *actiontocompare)
{
	size_t z;
	for (z = 0; actionpointers[z].name; z++)
		if (fasticmp(actiontocompare, actionpointers[z].name))
			return z;
	return z;
}
