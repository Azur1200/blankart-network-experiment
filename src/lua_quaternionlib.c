// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2025 by LJ Sonic
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  lua_quaternionlib.c
/// \brief quaternion library for Lua scripting

#include "m_fixed.h"
#include "lua_script.h"
#include "lua_libs.h"

static vector4_t *NewQuaternion(lua_State *L)
{
	vector4_t *quat = lua_newuserdata(L, sizeof(*quat));
	luaL_getmetatable(L, META_QUATERNION);
	lua_setmetatable(L, -2);
	return quat;
}

////////////////////
// STATIC MEMBERS //
////////////////////

static int quaternion_new(lua_State *L)
{
	vector4_t *q = NewQuaternion(L);
	q->x = 0;
	q->y = 0;
	q->z = 0;
	q->a = FRACUNIT;
	return 1;
}

static int quaternion_fromAxisRotation(lua_State *L)
{
	vector3_t *axis = luaL_checkudata(L, 1, META_VECTOR3);
	fixed_t angle = luaL_checkfixed(L, 2);

	fixed_t cosangle = FINECOSINE(((angle / 2) >> ANGLETOFINESHIFT) & FINEMASK);
	fixed_t sinangle = FINESINE(((angle / 2) >> ANGLETOFINESHIFT) & FINEMASK);
	vector3_t normaxis;

	FV3_NormalizeEx(axis, &normaxis);

	FV4_Load(NewQuaternion(L),
		FixedMul(normaxis.x, sinangle),
		FixedMul(normaxis.y, sinangle),
		FixedMul(normaxis.z, sinangle),
		cosangle
	);
	return 1;
}

static luaL_Reg quaternion[] = {
	{"new", quaternion_new},
	{"fromAxisRotation", quaternion_fromAxisRotation},
	{NULL, NULL}
};

/////////////
// MEMBERS //
/////////////

static int quaternion_clone(lua_State *L)
{
	vector4_t *quat = luaL_checkudata(L, 1, META_QUATERNION);
	FV4_Copy(NewQuaternion(L), quat);
	return 1;
}

static int vector4_toMatrix(lua_State *L)
{
	vector4_t *quat = luaL_checkudata(L, 1, META_QUATERNION);

	matrix_t *mat = lua_newuserdata(L, sizeof(*mat));
	luaL_getmetatable(L, META_MATRIX);
	lua_setmetatable(L, -2);

	fixed_t x = quat->x, y = quat->y, z = quat->z, w = quat->a;

	fixed_t xx2 = 2 * FixedMul(x, x);
	fixed_t xy2 = 2 * FixedMul(x, y);
	fixed_t xz2 = 2 * FixedMul(x, z);
	fixed_t xw2 = 2 * FixedMul(x, w);
	fixed_t yy2 = 2 * FixedMul(y, y);
	fixed_t yz2 = 2 * FixedMul(y, z);
	fixed_t yw2 = 2 * FixedMul(y, w);
	fixed_t zz2 = 2 * FixedMul(z, z);
	fixed_t zw2 = 2 * FixedMul(z, w);

	FM_LoadIdentity(mat);

	mat->m[0 + 0*4] = FRACUNIT - yy2 - zz2;
	mat->m[0 + 1*4] = xy2 - zw2;
	mat->m[0 + 2*4] = xz2 + yw2;

	mat->m[1 + 0*4] = xy2 + zw2;
	mat->m[1 + 1*4] = FRACUNIT - xx2 - zz2;
	mat->m[1 + 2*4] = yz2 - xw2;

	mat->m[2 + 0*4] = xz2 - yw2;
	mat->m[2 + 1*4] = yz2 + xw2;
	mat->m[2 + 2*4] = FRACUNIT - xx2 - yy2;

	mat->m[3 + 3*4] = FRACUNIT;

	return 1;
}

// matrixfield_ fields
#define QUATERNIONFIELDS(X) \
	X(clone) \
	X(toMatrix) \
	X(x) \
	X(y) \
	X(z) \
	X(w)

enum quaternionfield_e
{
#define ENUMITEM(name) quaternionfield_##name,
	QUATERNIONFIELDS(ENUMITEM)
};

static const char *const quaternionfield_opt[] = {
#define ENUMITEMNAME(name) #name,
	QUATERNIONFIELDS(ENUMITEMNAME)
	NULL,
};
#undef ENUMITEM
#undef ENUMITEMNAME

int quaternionfield_fields_ref = LUA_NOREF;

static int quaternion_get(lua_State *L)
{
	vector4_t *quat = luaL_checkudata(L, 1, META_QUATERNION);
	enum quaternionfield_e field = Lua_optoption(L, 2, quaternionfield_clone, quaternionfield_fields_ref);

	switch(field)
	{
		case quaternionfield_clone: lua_pushcfunction(L, quaternion_clone); return 1;
		case quaternionfield_toMatrix: lua_pushcfunction(L, vector4_toMatrix); return 1;

		case quaternionfield_x: lua_pushfixed(L, quat->x); return 1;
		case quaternionfield_y: lua_pushfixed(L, quat->y); return 1;
		case quaternionfield_z: lua_pushfixed(L, quat->z); return 1;
		case quaternionfield_w: lua_pushfixed(L, quat->a); return 1;

		default: break;
	}

	return 0;
}

///////////////
// OPERATORS //
///////////////


// FV4_Mul isn't an accurate multiplication, so let's silently use LJ Sonic's multiplication
// function.
static vector4_t *QuaternionMul(vector4_t *out, vector4_t *a, vector4_t *b)
{
	fixed_t ax = a->x, ay = a->y, az = a->z, aw = a->a;
	fixed_t bx = b->x, by = b->y, bz = b->z, bw = b->a;

	FV4_NormalizeEx(out, FV4_Load(out,
		FixedMul(aw, bx) + FixedMul(ax, bw) + FixedMul(ay, bz) - FixedMul(az, by),
		FixedMul(aw, by) - FixedMul(ax, bz) + FixedMul(ay, bw) + FixedMul(az, bx),
		FixedMul(aw, bz) + FixedMul(ax, by) - FixedMul(ay, bx) + FixedMul(az, bw),
		FixedMul(aw, bw) - FixedMul(ax, bx) - FixedMul(ay, by) - FixedMul(az, bz)
	));

	return out;
}

static int quaternion_mul(lua_State *L)
{
	vector4_t *quat1 = luaL_checkudata(L, 1, META_QUATERNION);
	vector4_t *quat2 = luaL_checkudata(L, 2, META_QUATERNION);
	QuaternionMul(NewQuaternion(L), quat1, quat2);
	return 1;
}

int LUA_QuaternionLib(lua_State *L)
{
	quaternionfield_fields_ref = Lua_CreateFieldTable(L, quaternionfield_opt);

	luaL_newmetatable(L, META_QUATERNION);
	LUA_SetCFunctionField(L, "__index", quaternion_get);
	LUA_SetCFunctionField(L, "__mul", quaternion_mul);
	lua_pop(L, 1);

	luaL_register(L, "Quaternion", quaternion);
	return 0;
}
