// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2014-2016 by John "JTE" Muniz.
// Copyright (C) 2014-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  lua_skinlib.c
/// \brief player skin structure library for Lua scripting

#include "doomdef.h"
#include "r_skins.h"
#include "sounds.h"

#include "lua_script.h"
#include "lua_libs.h"

// skin_ fields
#define SKINFIELDS(X) \
	X(valid) \
	X(name) \
	X(wadnum) \
	X(flags) \
	X(realname) \
	X(facerank) \
	X(facewant) \
	X(facemmap) \
	X(kartspeed) \
	X(kartweight) \
	X(followitem) \
	X(starttranscolor) \
	X(prefcolor) \
	X(supercolor) \
	X(prefoppositecolor) \
	X(highresscale) \
	X(rivals) \
	X(soundsid) \
	X(voices) \
	X(sprites) \
	X(compatmode) \

enum skin
{
#define ENUMITEM(name) skin_##name,
	SKINFIELDS(ENUMITEM)
	NUMSKINFIELDS,
};

static const char *const skin_opt[] = {
#define ENUMITEMNAME(name) #name,
	SKINFIELDS(ENUMITEMNAME)
	NULL,
};
#undef ENUMITEM
#undef ENUMITEMNAME

#define UNIMPLEMENTED luaL_error(L, LUA_QL("skin_t") " field " LUA_QS " is not implemented for Lua and cannot be accessed.", skin_opt[field])

// absolutely hilarious hack to get these old fields working with SPR2
// see libd_cachePatch
#define FACE(f) { \
	if (!lua_compatmode) \
		return UNIMPLEMENTED; \
	char name[SKINNAMESIZE+2]; \
	name[0] = f+1; \
	strcpy(name+1, skin->name); \
	lua_pushstring(L, name); \
	break; \
}

int skin_fields_ref = LUA_NOREF;

static int skin_get(lua_State *L)
{
	skin_t *skin = *((skin_t **)luaL_checkudata(L, 1, META_SKIN));
	enum skin field = Lua_optoption(L, 2, -1, skin_fields_ref);

	// skins are always valid, only added, never removed
	I_Assert(skin != NULL);

	switch (field)
	{
	case skin_valid:
		lua_pushdboolean(L, skin != NULL);
		break;
	case skin_name:
		lua_pushstring(L, skin->name);
		break;
	case skin_wadnum:
		// !!WARNING!! May differ between clients due to music wads, therefore NOT NETWORK SAFE
		return UNIMPLEMENTED;
	case skin_flags:
		lua_pushinteger(L, skin->flags);
		break;
	case skin_realname:
		lua_pushstring(L, skin->realname);
		break;
	case skin_facerank:
		FACE(0)
	case skin_facewant:
		FACE(1)
	case skin_facemmap:
		FACE(2)
	// SRB2kart
	case skin_kartspeed:
		lua_pushinteger(L, skin->kartspeed);
		break;
	case skin_kartweight:
		lua_pushinteger(L, skin->kartweight);
		break;
	// 
	case skin_followitem:
		lua_pushinteger(L, skin->followitem);
		break;
	case skin_starttranscolor:
		lua_pushinteger(L, skin->starttranscolor);
		break;
	case skin_prefcolor:
		lua_pushinteger(L, skin->prefcolor);
		break;
	case skin_supercolor:
		lua_pushinteger(L, skin->supercolor);
		break;
	case skin_prefoppositecolor:
		lua_pushinteger(L, skin->prefoppositecolor);
		break;
	case skin_highresscale:
		lua_pushinteger(L, skin->highresscale);
		break;
	case skin_rivals:
		// This would be pretty cool to push
		return UNIMPLEMENTED;
	case skin_soundsid:
		if (!lua_compatmode)
			return UNIMPLEMENTED; // close enough
		LUA_PushUserdata(L, &skin->soundsid[0], META_SOUNDSID);
		break;
	case skin_voices:
		// ...uuuuuuughhhhh...
		LUA_PushUserdata(L, &skin->soundsid[1], META_SKINVOICES);
		break;
	case skin_sprites:
		LUA_PushUserdata(L, skin->sprites, META_SKINSPRITES);
		break;
	case skin_compatmode:
		lua_pushdboolean(L, wadfiles[skin->wadnum]->compatmode);
		break;
	default:
		break;
	}
	return 1;
}

#undef FACE

static int skin_set(lua_State *L)
{
	return luaL_error(L, LUA_QL("skin_t") " struct cannot be edited by Lua.");
}

static int skin_num(lua_State *L)
{
	skin_t *skin = *((skin_t **)luaL_checkudata(L, 1, META_SKIN));

	// skins are always valid, only added, never removed
	I_Assert(skin != NULL);

	lua_pushinteger(L, skin->skinnum);
	return 1;
}

static int lib_iterateSkins(lua_State *L)
{
	int32_t i;

	if (lua_gettop(L) < 2)
	{
		//return luaL_error(L, "Don't call skins.iterate() directly, use it as 'for skin in skins.iterate do <block> end'.");
		lua_pushcfunction(L, lib_iterateSkins);
		return 1;
	}

	lua_settop(L, 2);
	lua_remove(L, 1); // state is unused.

	if (!lua_isnil(L, 1))
		i = (int32_t)((*((skin_t **)luaL_checkudata(L, 1, META_SKIN)))->skinnum) + 1;
	else
		i = 0;

	// skins are always valid, only added, never removed
	if (i < numskins)
	{
		LUA_PushUserdata(L, skins[i], META_SKIN);
		return 1;
	}

	return 0;
}

static int lib_getSkin(lua_State *L)
{
	const char *field;
	int32_t i;

	// find skin by number
	if (lua_type(L, 2) == LUA_TNUMBER)
	{
		i = luaL_checkinteger(L, 2);
		if (i < 0 || i >= MAXSKINS)
			return luaL_error(L, "skins[] index %d out of range (0 - %d)", i, MAXSKINS-1);
		if (i >= numskins)
			return 0;
		LUA_PushUserdata(L, skins[i], META_SKIN);
		return 1;
	}

	field = luaL_checkstring(L, 2);

	// special function iterate
	if (fastcmp(field,"iterate"))
	{
		lua_pushcfunction(L, lib_iterateSkins);
		return 1;
	}

	uint32_t hash = HASH32(field, strlen(field));

	// find skin by name
	for (i = 0; i < numskins; i++)
	{
		if (skins[i]->namehash != hash)
			continue;

		if (fastcmp(skins[i]->name, field))
		{
			LUA_PushUserdata(L, skins[i], META_SKIN);
			return 1;
		}
	}


	return 0;
}

static int lib_numSkins(lua_State *L)
{
	lua_pushinteger(L, numskins);
	return 1;
}

// soundsid, i -> soundsid[i]
static int soundsid_get(lua_State *L)
{
	// soundsid is a route to the voice struct. That's how we're doing this.
	sfxenum_t *soundsid = *((sfxenum_t **)luaL_checkudata(L, 1, META_SOUNDSID));
	skin_t *s = (skin_t*)((char*)soundsid - offsetof(skin_t, soundsid[0]));

	int32_t i = luaL_checkinteger(L, 2);

	if (i < 0 || i >= NUMSKINSOUNDS)
		return luaL_error(L, "%s cannot be %s", LUA_QL("skinsound_t"), va("%u", i));

	lua_pushinteger(L, R_GetLegacySkinSound(&s->voices[0], sfx_kwin + i));
	return 1;
}

// #soundsid -> NUMSKINSOUNDS
static int soundsid_num(lua_State *L)
{
	lua_pushinteger(L, NUMSKINSOUNDS);
	return 1;
}

static int skinvoices_get(lua_State *L)
{
	// Use soundsid to route to the voice array so that voices[0] isn't considered THE VOICE ARRAY
	sfxenum_t *svoices = *((sfxenum_t **)luaL_checkudata(L, 1, META_SKINVOICES));
	skin_t *s = (skin_t*)((char*)svoices - offsetof(skin_t, soundsid[1]));

	int32_t i = luaL_checkinteger(L, 2);

	if (i < 0 || i >= s->numvoices)
		return luaL_error(L, "skin.voices[] index %s out of range (0 - %d)", va("%u", i), s->numvoices - 1);

	LUA_PushUserdata(L, &s->voices[i], META_VOICE);
	return 1;
}

// #voice -> MAXSKINVOICES
static int skinvoices_num(lua_State *L)
{
	lua_pushinteger(L, MAXSKINVOICES);
	return 1;
}

enum spritesopt {
	numframes = 0
};

static const char *const sprites_opt[] = {
	"numframes",
	NULL};

// skin.sprites[i] -> sprites[i]
static int lib_getSkinSprite(lua_State *L)
{
	spritedef_t *sprites = *(spritedef_t **)luaL_checkudata(L, 1, META_SKINSPRITES);
	playersprite_t i = luaL_checkinteger(L, 2);

	if (i < 0 || i >= NUMPLAYERSPRITES*2)
		return luaL_error(L, LUA_QL("skin_t") " field 'sprites' index %d out of range (0 - %d)", i, (NUMPLAYERSPRITES*2)-1);

	LUA_PushUserdata(L, &sprites[i], META_SKINSPRITESLIST);
	return 1;
}

// #skin.sprites -> NUMPLAYERSPRITES*2
static int lib_numSkinsSprites(lua_State *L)
{
	lua_pushinteger(L, NUMPLAYERSPRITES*2);
	return 1;
}

int sprites_fields_ref = LUA_NOREF;

static int sprite_get(lua_State *L)
{
	spritedef_t *sprite = *(spritedef_t **)luaL_checkudata(L, 1, META_SKINSPRITESLIST);
	enum spritesopt field = Lua_optoption(L, 2, -1, sprites_fields_ref);

	switch (field)
	{
	case numframes:
		lua_pushinteger(L, sprite->numframes);
		break;
	}
	return 1;
}


int LUA_SkinLib(lua_State *L)
{
	skin_fields_ref = Lua_CreateFieldTable(L, skin_opt);
	sprites_fields_ref = Lua_CreateFieldTable(L, sprites_opt);

	luaL_newmetatable(L, META_SKIN);
		lua_pushcfunction(L, skin_get);
		lua_setfield(L, -2, "__index");

		lua_pushcfunction(L, skin_set);
		lua_setfield(L, -2, "__newindex");

		lua_pushcfunction(L, skin_num);
		lua_setfield(L, -2, "__len");
	lua_pop(L,1);

	luaL_newmetatable(L, META_SOUNDSID);
		lua_pushcfunction(L, soundsid_get);
		lua_setfield(L, -2, "__index");

		lua_pushcfunction(L, soundsid_num);
		lua_setfield(L, -2, "__len");
	lua_pop(L,1);


	luaL_newmetatable(L, META_SKINSPRITES);
		lua_pushcfunction(L, lib_getSkinSprite);
		lua_setfield(L, -2, "__index");

		lua_pushcfunction(L, lib_numSkinsSprites);
		lua_setfield(L, -2, "__len");
	lua_pop(L,1);

	luaL_newmetatable(L, META_SKINSPRITESLIST);
		lua_pushcfunction(L, sprite_get);
		lua_setfield(L, -2, "__index");
	lua_pop(L,1);

	luaL_newmetatable(L, META_SKINVOICES);
		lua_pushcfunction(L, skinvoices_get);
		lua_setfield(L, -2, "__index");

		lua_pushcfunction(L, skinvoices_num);
		lua_setfield(L, -2, "__len");
	lua_pop(L,1);

	lua_newuserdata(L, 0);
		lua_createtable(L, 0, 2);
			lua_pushcfunction(L, lib_getSkin);
			lua_setfield(L, -2, "__index");

			lua_pushcfunction(L, lib_numSkins);
			lua_setfield(L, -2, "__len");
		lua_setmetatable(L, -2);
	lua_setglobal(L, "skins");

	return 0;
}
