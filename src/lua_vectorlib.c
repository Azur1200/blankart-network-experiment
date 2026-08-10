// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2025 by LJ Sonic
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  lua_vectorlib.c
/// \brief vector library for Lua scripting

#include "m_fixed.h"
#include "lua_script.h"
#include "lua_libs.h"

// shared by both Vector2D and Vector3D
enum vectorfield_e {
	vectorfield_clone = 0,
	vectorfield_opposite,
	vectorfield_x,
	vectorfield_y,
	vectorfield_z,
	vectorfield_length,
	vectorfield_normalized,
};

static const char *const vectorfield_opt[] = {
	"vector_clone",
	"vector_opposite",
	"x",
	"y",
	"z",
	"vector_length",
	"vector_normalized",
	NULL};

////////////////////////////
// VECTOR2 STATIC MEMBERS //
////////////////////////////

/////////////////////
// VECTOR2 MEMBERS //
/////////////////////

int vectorfield_fields_ref = LUA_NOREF;

static int vector2d_get(lua_State *L)
{
	vector2_t *vec = luaL_checkudata(L, 1, META_VECTOR2);
	enum vectorfield_e field = Lua_optoption(L, 2, vectorfield_clone, vectorfield_fields_ref);

	switch(field)
	{
		case vectorfield_x: lua_pushfixed(L, vec->x); return 1;
		case vectorfield_y: lua_pushfixed(L, vec->y); return 1;
		default: break;
	}

	return 0;
}

/////////////
// VECTOR3 //
/////////////

static vector3_t *NewVector3(lua_State *L)
{
	vector3_t *vec = lua_newuserdata(L, sizeof(*vec));
	luaL_getmetatable(L, META_VECTOR3);
	lua_setmetatable(L, -2);
	return vec;
}

////////////////////////////
// VECTOR3 STATIC MEMBERS //
////////////////////////////

static int vector3d_new(lua_State *L)
{
	fixed_t x = luaL_checkfixed(L, 1);
	fixed_t y = luaL_checkfixed(L, 2);
	fixed_t z = luaL_optfixed(L, 3, 0);

	FV3_Load(NewVector3(L), x, y, z);
	return 1;
}

static luaL_Reg vector3d[] = {
	{"new", vector3d_new},
	{NULL, NULL}
};

/////////////////////
// VECTOR3 MEMBERS //
/////////////////////

static int vector3d_clone(lua_State *L)
{
	vector3_t *vec = luaL_checkudata(L, 1, META_VECTOR3);
	FV3_Copy(NewVector3(L), vec);
	return 1;
}

static int vector3d_opposite(lua_State *L)
{
	vector3_t *vec = luaL_checkudata(L, 1, META_VECTOR3);
	FV3_NegateEx(vec, NewVector3(L));
	return 1;
}

static int vector3d_get(lua_State *L)
{
	vector3_t *vec = luaL_checkudata(L, 1, META_VECTOR3);
	enum vectorfield_e field = Lua_optoption(L, 2, vectorfield_clone, vectorfield_fields_ref);

	if (!vec)
		return luaL_error(L, "accessed vector3_t doesn't exist anymore.");

	vector3_t unit = { 0, 0, 0 };
	switch(field)
	{
		case vectorfield_clone: lua_pushcfunction(L, vector3d_clone); return 1;
		case vectorfield_opposite: lua_pushcfunction(L, vector3d_opposite); return 1;

		case vectorfield_x: lua_pushfixed(L, vec->x); return 1;
		case vectorfield_y: lua_pushfixed(L, vec->y); return 1;
		case vectorfield_z: lua_pushfixed(L, vec->z); return 1;
		case vectorfield_length: lua_pushfixed(L, FV3_Distance(&unit, vec)); return 1;
		case vectorfield_normalized: FV3_NormalizeEx(vec, NewVector3(L)); return 1;

		default: break;
	}

	return 0;
}

///////////////////////
// VECTOR3 OPERATORS //
///////////////////////

static int vector3d_eq(lua_State *L)
{
	vector3_t *vec1 = luaL_checkudata(L, 1, META_VECTOR3);
	vector3_t *vec2 = luaL_checkudata(L, 2, META_VECTOR3);
	lua_pushdboolean(L, FV3_Equal(vec1, vec2));
	return 1;
}

static int vector3d_op(
	lua_State *L,
	vector3_t *(*opvector)(const vector3_t*, const vector3_t*, vector3_t*)
)
{
	if (lua_isnumber(L, 1) && (opvector == FV3_AddEx || opvector == FV3_MulEx))
	{
		fixed_t n1 = lua_tofixed(L, 1);
		vector3_t vec1 = { n1, n1, n1 };
		vector3_t *vec2 = luaL_checkudata(L, 2, META_VECTOR3);
		opvector(NewVector3(L), vec2, &vec1);
	}
	else if (lua_isnumber(L, 2))
	{
		vector3_t *vec1 = luaL_checkudata(L, 1, META_VECTOR3);
		fixed_t n2 = lua_tofixed(L, 2);
		vector3_t vec2 = { n2, n2, n2 };
		opvector(NewVector3(L), vec1, &vec2);
	}
	else
	{
		vector3_t *vec1 = luaL_checkudata(L, 1, META_VECTOR3);
		vector3_t *vec2 = luaL_checkudata(L, 2, META_VECTOR3);
		opvector(NewVector3(L), vec1, vec2);
	}

	return 1;
}

static int vector3d_add(lua_State *L)
{
	return vector3d_op(L, FV3_AddEx);
}

static int vector3d_sub(lua_State *L)
{
	return vector3d_op(L, FV3_SubEx);
}

static int vector3d_mul(lua_State *L)
{
	return vector3d_op(L, FV3_MulEx);
}

static int vector3d_div(lua_State *L)
{
	return vector3d_op(L, FV3_DivideEx);
}

static int vector3d_unm(lua_State *L)
{
	vector3_t *vec = luaL_checkudata(L, 1, META_VECTOR3);
	FV3_NegateEx(vec, NewVector3(L));
	return 1;
}

int LUA_VectorLib(lua_State *L)
{
	vectorfield_fields_ref = Lua_CreateFieldTable(L, vectorfield_opt);

	LUA_RegisterUserdataMetatable(L, META_VECTOR2, vector2d_get, NULL, NULL);

	luaL_newmetatable(L, META_VECTOR3);
	LUA_SetCFunctionField(L, "__index", vector3d_get);
	LUA_SetCFunctionField(L, "__eq", vector3d_eq);
	LUA_SetCFunctionField(L, "__add", vector3d_add);
	LUA_SetCFunctionField(L, "__sub", vector3d_sub);
	LUA_SetCFunctionField(L, "__mul", vector3d_mul);
	LUA_SetCFunctionField(L, "__div", vector3d_div);
	LUA_SetCFunctionField(L, "__unm", vector3d_unm);
	lua_pop(L, 1);

	luaL_register(L, "Vector3D", vector3d);
	return 0;
}
