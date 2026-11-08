// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2025 by LJ Sonic
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  lua_matrixlib.c
/// \brief matrix library for Lua scripting

#include "m_fixed.h"
#include "lua_script.h"
#include "lua_libs.h"

static vector3_t *GetVector(lua_State *L, int index)
{
	vector3_t *vec = lua_touserdata(L, index);

	if (!vec)
		return NULL;

	if (!lua_getmetatable(L, index))
		return NULL;

	lua_getfield(L, LUA_REGISTRYINDEX, META_VECTOR3);
	if (!lua_rawequal(L, -1, -2))
		return NULL;

	lua_pop(L, 2);

	return vec;
}

static void GetVectorOrXYZ(lua_State *L, int index, vector3_t *result)
{
	vector3_t *vec = GetVector(L, index);
	if (vec)
	{
		*result = *vec;
	}
	else
	{
		result->x = luaL_checkfixed(L, 1);
		result->y = luaL_checkfixed(L, 2);
		result->z = luaL_checkfixed(L, 3);
	}
}

static matrix_t *NewMatrix(lua_State *L)
{
	matrix_t *mat = lua_newuserdata(L, sizeof(*mat));
	luaL_getmetatable(L, META_MATRIX);
	lua_setmetatable(L, -2);
	return mat;
}

////////////////////
// STATIC MEMBERS //
////////////////////

static int matrix_new(lua_State *L)
{
	FM_LoadIdentity(NewMatrix(L));
	return 1;
}

static int matrix_translation(lua_State *L)
{
	vector3_t translation;
	GetVectorOrXYZ(L, 1, &translation);

	matrix_t *mat = NewMatrix(L);
	FM_LoadIdentity(mat);
	FM_Translate(mat, translation.x, translation.y, translation.z);
	return 1;
}

static int matrix_scaling(lua_State *L)
{
	vector3_t scaling;
	GetVectorOrXYZ(L, 1, &scaling);
	FM_Scale(NewMatrix(L), scaling.x, scaling.y, scaling.z);
	return 1;
}

static luaL_Reg matrix[] = {
	{"new", matrix_new},
	{"translation", matrix_translation},
	{"scaling", matrix_scaling},
	{NULL, NULL}
};

/////////////
// MEMBERS //
/////////////

// matrixfield_ fields
#define MATRIXFIELDS(X) \
	X(clone) \
	X(getvalue) \
	X(setvalue) \
	X(mulXYZ)

enum matrixfield_e
{
#define ENUMITEM(name) matrixfield_##name,
	MATRIXFIELDS(ENUMITEM)
};

static const char *const matrixfield_opt[] = {
#define ENUMITEMNAME(name) #name,
	MATRIXFIELDS(ENUMITEMNAME)
	NULL,
};
#undef ENUMITEM
#undef ENUMITEMNAME

static int matrix_clone(lua_State *L)
{
	matrix_t *mat = luaL_checkudata(L, 1, META_MATRIX);
	memcpy(NewMatrix(L), mat, sizeof(matrix_t));
	return 1;
}

static int matrix_getvalue(lua_State *L)
{
	matrix_t *mat = luaL_checkudata(L, 1, META_MATRIX);
	int32_t row = luaL_checkinteger(L, 2);
	int32_t col = luaL_checkinteger(L, 3);

	if (row < 1 || row > 4)
		return luaL_error(L, "matrix row %d out of range (1 - 4)", row);
	if (col < 1 || col > 4)
		return luaL_error(L, "matrix column %d out of range (1 - 4)", col);

	lua_pushfixed(L, mat->m[(col - 1) * 4 + row - 1]);
	return 1;
}

static int matrix_setvalue(lua_State *L)
{
	matrix_t *mat = luaL_checkudata(L, 1, META_MATRIX);
	int32_t row = luaL_checkinteger(L, 2);
	int32_t col = luaL_checkinteger(L, 3);
	fixed_t value = luaL_checkfixed(L, 4);

	if (row < 1 || row > 4)
		return luaL_error(L, "matrix row %d out of range (1 - 4)", row);
	if (col < 1 || col > 4)
		return luaL_error(L, "matrix column %d out of range (1 - 4)", col);

	mat->m[(col - 1) * 4 + row - 1] = value;
	return 0;
}

static int matrix_mulXYZ(lua_State *L)
{
	matrix_t *mat = luaL_checkudata(L, 1, META_MATRIX);
	fixed_t x = luaL_checkfixed(L, 2);
	fixed_t y = luaL_checkfixed(L, 3);
	fixed_t z = luaL_checkfixed(L, 4);

	vector3_t vec;
	vector3_t result;
	FV3_Load(&vec, x, y, z);
	FM_MultMatrixVec3(mat, &vec, &result);

	lua_pushfixed(L, result.x);
	lua_pushfixed(L, result.y);
	lua_pushfixed(L, result.z);
	return 3;
}

int matrixfield_fields_ref = LUA_NOREF;

static int matrix_get(lua_State *L)
{
	enum matrixfield_e field = Lua_optoption(L, 2, matrixfield_clone, matrixfield_fields_ref);

	switch(field)
	{
		case matrixfield_clone: lua_pushcfunction(L, matrix_clone); return 1;
		case matrixfield_getvalue: lua_pushcfunction(L, matrix_getvalue); return 1;
		case matrixfield_setvalue: lua_pushcfunction(L, matrix_setvalue); return 1;
		case matrixfield_mulXYZ: lua_pushcfunction(L, matrix_mulXYZ); return 1;

		default: break;
	}

	return 0;
}

///////////////
// OPERATORS //
///////////////

static int matrix_mul(lua_State *L)
{
	matrix_t *mat1 = luaL_checkudata(L, 1, META_MATRIX);

	vector3_t *vec2 = GetVector(L, 2);
	if (vec2)
	{
		vector3_t *result = lua_newuserdata(L, sizeof(*result));
		luaL_getmetatable(L, META_VECTOR3);
		lua_setmetatable(L, -2);

		FM_MultMatrixVec3(mat1, vec2, result);
	}
	else
	{
		matrix_t *mat2 = luaL_checkudata(L, 2, META_MATRIX);
		FM_MultMatrixEx(mat1, mat2, NewMatrix(L));
	}

	return 1;
}

int LUA_MatrixLib(lua_State *L)
{
	matrixfield_fields_ref = Lua_CreateFieldTable(L, matrixfield_opt);

	luaL_newmetatable(L, META_MATRIX);
	LUA_SetCFunctionField(L, "__index", matrix_get);
	LUA_SetCFunctionField(L, "__mul", matrix_mul);
	lua_pop(L, 1);

	luaL_register(L, "Matrix", matrix);
	return 0;
}
