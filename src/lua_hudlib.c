// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2014-2016 by John "JTE" Muniz.
// Copyright (C) 2014-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  lua_hudlib.c
/// \brief custom HUD rendering library for Lua scripting

#include "doomdef.h"
#include "r_defs.h"
#include "r_local.h"
#include "r_fps.h"
#include "st_stuff.h"
#include "y_inter.h"
#include "g_game.h"
#include "i_video.h" // rendermode
#include "p_local.h" // camera_t
#include "screen.h" // screen width/height
#include "m_random.h" // m_random
#include "m_menu.h" // M_GetMapThumbnail
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"
#include "hu_stuff.h"
#include "h_timers.h"

#include "lua_script.h"
#include "lua_libs.h"
#include "lua_hud.h"
#include "lua_hook.h"

#include "k_hud.h"
#include "h_timers.h"
#include "k_vote.h"

#define HUDONLY if (!hud_running) return luaL_error(L, "HUD rendering code should not be called outside of rendering hooks!");

dboolean hud_running = false;

dboolean hud_interpolate = false;
uint8_t hud_interptag = 0;
uint32_t hud_interpcounter = 0;
dboolean hud_interpstring = false;
dboolean hud_interplatch = false;

static uint8_t hud_enabled[(hud_MAX/8)+1];

static uint8_t camnum = 1;

static dboolean cliprectenabled;

// must match enum hud in lua_hud.h
static const char *const hud_disable_options[] = {
	"stagetitle",
	"textspectator",
	"crosshair",
	"scores",
	"time",
	"gametypeinfo",	// Bumpers / Karma / Laps depending on gametype
	"minimap",
	"item",
	"position",
	"nametags",
	"check",		// "CHECK" f-zero indicator
	"minirankings",	// Gametype rankings to the left
	"battlerankingsbumpers",	// bumper drawer for battle. Useful if you want to make a custom battle gamemode without bumpers being involved.
	"battlefullscreen",			// battlefullscreen func (WAIT, ATTACK OR PROTECT ...)
	"battlecomebacktimer",		// come back timer in battlefullscreen
	"wanted",
	"speedometer",
	"rings",
	"lives",
	"freeplay",
	"rankings",
	"lapsplits",
	"itemtimers",

	"intermissiontally",
	"intermissionmessages",
	NULL};

// you know, let's actually make sure that the table is synced.
// because fuck knows how many times this has happened at this point. :v
I_StaticAssert(sizeof(hud_disable_options) / sizeof(*hud_disable_options) == hud_MAX+1);

enum patch {
	patch_valid = 0,
	patch_width,
	patch_height,
	patch_leftoffset,
	patch_topoffset
};
static const char *const patch_opt[] = {
	"valid",
	"width",
	"height",
	"leftoffset",
	"topoffset",
	NULL};

// alignment types for v.drawString
enum align {
	align_left = 0,
	align_center,
	align_right,
	align_small,
	align_smallcenter,
	align_smallright,
	align_thin,
	align_thincenter,
	align_thinright
};
static const char *const align_opt[] = {
	"left",
	"center",
	"right",
	"small",
	"small-center",
	"small-right",
	"thin",
	"thin-center",
	"thin-right",
	NULL};

// width types for v.stringWidth
enum widtht {
	widtht_normal = 0,
	widtht_small,
	widtht_thin
};
static const char *const widtht_opt[] = {
	"normal",
	"small",
	"thin",
	NULL};

enum cameraf {
	camera_chase = 0,
	camera_aiming,
	camera_x,
	camera_y,
	camera_z,
	camera_angle,
	camera_subsector,
	camera_floorz,
	camera_ceilingz,
	camera_radius,
	camera_height,
	camera_momx,
	camera_momy,
	camera_momz,
	camera_pan,
	camera_pitch,
	camera_pnum,
	camera_freecam,
};


static const char *const camera_opt[] = {
	"chase",
	"aiming",
	"x",
	"y",
	"z",
	"angle",
	"subsector",
	"floorz",
	"ceilingz",
	"radius",
	"height",
	"momx",
	"momy",
	"momz",
	"pan",
	"pitch",
	"pnum",
	"freecam",
	NULL};

enum hudpatch {
	hudpatch_item = 0,
	hudpatch_itemmul,
	hudpatch_itemalt
};

static const char *const hud_patch_options[] = {
	"item",
	"itemmul",
	"itemalt",
	NULL};

enum hudoffsets {
	hudoffsets_item = 0,
	hudoffsets_time,
	hudoffsets_gametypeinfo,
	hudoffsets_rings,
	hudoffsets_countdown,
	hudoffsets_speedometer,
	hudoffsets_accessibility,
	hudoffsets_position,
	hudoffsets_minirankings,
	hudoffsets_startcountdown,
	hudoffsets_check,
	hudoffsets_minimap,
	hudoffsets_wanted,
	//hudoffsets_statdisplay,
};

static const char *const hud_offsets_options[] = {
	"item",
	"time",
	"gametypeinfo",
	"rings",
	"countdown",
	"speedometer",
	"accessibility",
	"position",
	"minirankings",
	"startcountdown",
	"check",
	"minimap",
	"wanted",
	//"statdisplay",
	NULL};

static int colormap_get(lua_State *L)
{
	const uint8_t *colormap = *((uint8_t **)luaL_checkudata(L, 1, META_COLORMAP));
	uint32_t i = luaL_checkinteger(L, 2);
	if (i >= 256)
		return luaL_error(L, "colormap index %d out of range (0 - %d)", i, 255);
	lua_pushinteger(L, colormap[i]);
	return 1;
}

int patch_fields_ref = LUA_NOREF;

static int patch_get(lua_State *L)
{
	patch_t *patch = *((patch_t **)luaL_checkudata(L, 1, META_PATCH));
	enum patch field = Lua_optoption(L, 2, -1, patch_fields_ref);

	// patches are invalidated when switching renderers
	if (!patch) {
		if (field == patch_valid) {
			lua_pushdboolean(L, 0);
			return 1;
		}
		return LUA_ErrInvalid(L, "patch_t");
	}

	switch (field)
	{
	case patch_valid:
		lua_pushdboolean(L, patch != NULL);
		break;
	case patch_width:
		lua_pushinteger(L, patch->width);
		break;
	case patch_height:
		lua_pushinteger(L, patch->height);
		break;
	case patch_leftoffset:
		lua_pushinteger(L, patch->leftoffset);
		break;
	case patch_topoffset:
		lua_pushinteger(L, patch->topoffset);
		break;
	}
	return 1;
}

static int patch_set(lua_State *L)
{
	return luaL_error(L, LUA_QL("patch_t") " struct cannot be edited by Lua.");
}

int camera_fields_ref = LUA_NOREF;

static int camera_get(lua_State *L)
{
	camera_t *cam = *((camera_t **)luaL_checkudata(L, 1, META_CAMERA));
	enum cameraf field = Lua_optoption(L, 2, -1, camera_fields_ref);

	// cameras should always be valid unless I'm a nutter
	I_Assert(cam != NULL);

	switch (field)
	{
	case camera_chase:
		lua_pushdboolean(L, cam->chase);
		break;
	case camera_aiming:
		lua_pushinteger(L, cam->aiming);
		break;
	case camera_x:
		lua_pushinteger(L, cam->x);
		break;
	case camera_y:
		lua_pushinteger(L, cam->y);
		break;
	case camera_z:
		lua_pushinteger(L, cam->z);
		break;
	case camera_angle:
		lua_pushinteger(L, cam->angle);
		break;
	case camera_subsector:
		LUA_PushUserdata(L, cam->subsector, META_SUBSECTOR);
		break;
	case camera_floorz:
		lua_pushinteger(L, cam->floorz);
		break;
	case camera_ceilingz:
		lua_pushinteger(L, cam->ceilingz);
		break;
	case camera_radius:
		lua_pushinteger(L, cam->radius);
		break;
	case camera_height:
		lua_pushinteger(L, cam->height);
		break;
	case camera_momx:
		lua_pushinteger(L, cam->momx);
		break;
	case camera_momy:
		lua_pushinteger(L, cam->momy);
		break;
	case camera_momz:
		lua_pushinteger(L, cam->momz);
		break;
	case camera_pan:
		lua_pushinteger(L, cam->pan);
		break;
	case camera_pitch:
		lua_pushinteger(L, cam->pitch);
		break;
	case camera_pnum:
		lua_pushinteger(L, camnum);
		break;
	case camera_freecam:
		lua_pushdboolean(L, cam->freecam);
		break;
	}
	return 1;
}

static int lib_getCamera(lua_State *L)
{
	// No cameras on dedicated
	if (dedicated)
		return 0;

	if (lua_type(L, 2) == LUA_TNUMBER)
	{
		int i = lua_tonumber(L, 2);

		if (i < 0 || i > splitscreen)
			return 0;

		LUA_PushUserdata(L, &camera[i], META_CAMERA);
		return 1;
	}

	return 0;
}

static int lib_lenCamera(lua_State *L)
{
	if (dedicated)
		lua_pushinteger(L, 0);
	else
		lua_pushinteger(L, splitscreen+1); // splitscreen == 0 -> 1 active camera, splitscreen == 1 -> 2 cameras and so on

	return 1;
}

//
// lib_draw
//

static int libd_patchExists(lua_State *L)
{
	HUDONLY
	lua_pushdboolean(L, W_LumpExists(luaL_checkstring(L, 1)));
	return 1;
}

static int libd_cachePatch(lua_State *L)
{
	HUDONLY
	const char *name = luaL_checkstring(L, 1);
	void *patch;
	if (name[0] && name[0] <= NUMFACES)
	{
		// skin.facerank has a secret message for us
		int32_t skin = R_SkinAvailableEx(name+1, false);
		if (skin != -1)
			patch = faceprefix[skin][(uint8_t)name[0]-1];
		else
			return luaL_error(L, "Skin %s not found", name+1);
	}
	else
		patch = W_CachePatchLongName(name, PU_PATCH);

#ifdef ROTSPRITE
	if (lua_isnumber(L, 2))
	{
		angle_t rollangle = luaL_checkangle(L, 2);
		int32_t rot = R_GetRollAngle(rollangle);
		if (rot) {
			patch_t *rotpatch = Patch_GetRotated(patch, rot, false);
			LUA_PushUserdata(L, rotpatch, META_PATCH);
			return 1;
		}
	}
#endif

	LUA_PushUserdata(L, patch, META_PATCH);
	return 1;
}

// v.getSpritePatch(sprite, [frame, [angle, [rollangle]]])
static int libd_getSpritePatch(lua_State *L)
{
	uint32_t i; // sprite prefix
	uint32_t frame = 0; // 'A'
	uint8_t angle = 0;
	spritedef_t *sprdef;
	spriteframe_t *sprframe;
	HUDONLY

	if (lua_isnumber(L, 1)) // sprite number given, e.g. SPR_THOK
	{
		i = lua_tonumber(L, 1);
		if (i >= NUMSPRITES)
			return 0;
	}
	else if (lua_isstring(L, 1)) // sprite prefix name given, e.g. "THOK"
	{
		const char *name = lua_tostring(L, 1);
		for (i = 0; i < NUMSPRITES; i++)
			if (fastcmp(name, sprnames[i]))
				break;
		if (i >= NUMSPRITES)
			return 0;
	}
	else
		return 0;

	if (i == SPR_PLAY) // Use getSprite2Patch instead!
		return 0;

	if (i == SPR_ITEM) // Use... uhhhhh...
		return 0;

	sprdef = &sprites[i];

	// set frame number
	frame = luaL_optinteger(L, 2, 0);
	frame &= FF_FRAMEMASK; // ignore any bits that are not the actual frame, just in case
	if (frame >= sprdef->numframes)
		return 0;
	// set angle number
	sprframe = &sprdef->spriteframes[frame];
	angle = luaL_optinteger(L, 3, 1);

	// convert WAD editor angle numbers (1-8) to internal angle numbers (0-7)
	// keep 0 the same since we'll make it default to angle 1 (which is internally 0)
	// in case somebody didn't know that angle 0 really just maps all 8/16 angles to the same patch
	if (angle != 0)
		angle--;

	if (angle >= ((sprframe->rotate & SRF_3DGE) ? 16 : 8)) // out of range?
		return 0;

#ifdef ROTSPRITE
	if (lua_isnumber(L, 4))
	{
		// rotsprite?????
		angle_t rollangle = luaL_checkangle(L, 4);
		int32_t rot = R_GetRollAngle(rollangle);

		if (rot) {
			patch_t *rotsprite = Patch_GetRotatedSprite(sprframe, frame, angle, sprframe->flip & (1<<angle), true, false, &spriteinfo[i], rot);
			LUA_PushUserdata(L, rotsprite, META_PATCH);
			lua_pushdboolean(L, false);
			lua_pushdboolean(L, true);
			return 3;
		}
	}
#endif

	// push both the patch and it's "flip" value
	LUA_PushUserdata(L, W_CachePatchNum(sprframe->lumppat[angle], PU_SPRITE), META_PATCH);
	lua_pushdboolean(L, (sprframe->flip & (1<<angle)) != 0);
	return 2;
}

// v.getSprite2Patch(skin, sprite, [super?,] [frame, [angle, [rollangle]]])
static int libd_getSprite2Patch(lua_State *L)
{
	int32_t i; // skin number
	playersprite_t j; // sprite2 prefix
	uint32_t frame = 0; // 'A'
	uint8_t angle = 0;
	spritedef_t *sprdef;
	spriteframe_t *sprframe;
	dboolean super = false; // add FF_SPR2SUPER to sprite2 if true
	HUDONLY

	// get skin first!
	if (lua_isnumber(L, 1)) // find skin by number
	{
		i = lua_tonumber(L, 1);
		if (i < 0 || i >= MAXSKINS)
			return luaL_error(L, "skin number %d out of range (0 - %d)", i, MAXSKINS-1);
		if (i >= numskins)
			return 0;
	}
	else // find skin by name
	{
		const char *name = luaL_checkstring(L, 1);
		uint32_t hash = HASH32(name, strlen(name));
		for (i = 0; i < numskins; i++)
		{
			if (skins[i]->namehash != hash)
				continue;

			if (fastcmp(skins[i]->name, name))
				break;
		}

		if (i >= numskins)
			return 0;
	}

	lua_remove(L, 1); // remove skin now

	if (lua_isnumber(L, 1)) // sprite number given, e.g. SPR2_STIL
	{
		j = lua_tonumber(L, 1);
		if (j & FF_SPR2SUPER) // e.g. SPR2_STIL|FF_SPR2SUPER
		{
			super = true;
			j &= ~FF_SPR2SUPER; // remove flag so the next check doesn't fail
		}
		if (j >= free_spr2)
			return 0;
	}
	else if (lua_isstring(L, 1)) // sprite prefix name given, e.g. "STND"
	{
		const char *name = lua_tostring(L, 1);
		for (j = 0; j < free_spr2; j++)
			if (fastcmp(name, spr2names[j]))
				break;
		// if you want super flags you'll have to use the optional dboolean following this
		if (j >= free_spr2)
			return 0;
	}
	else
		return 0;

	if (lua_isdboolean(L, 2)) // optional dboolean for superness
	{
		super = lua_todboolean(L, 2); // note: this can override FF_SPR2SUPER from sprite number
		lua_remove(L, 2); // remove
	}
	// if it's not dboolean then just assume it's the frame number

	if (super)
		j |= FF_SPR2SUPER;

	j = P_GetSkinSprite2(skins[i], j, NULL); // feed skin and current sprite2 through to change sprite2 used if necessary

	sprdef = &skins[i]->sprites[j];

	// set frame number
	frame = luaL_optinteger(L, 2, 0);
	frame &= FF_FRAMEMASK; // ignore any bits that are not the actual frame, just in case
	if (frame >= sprdef->numframes)
		return 0;
	// set angle number
	sprframe = &sprdef->spriteframes[frame];
	angle = luaL_optinteger(L, 3, 1);

	// convert WAD editor angle numbers (1-8) to internal angle numbers (0-7)
	// keep 0 the same since we'll make it default to angle 1 (which is internally 0)
	// in case somebody didn't know that angle 0 really just maps all 8/16 angles to the same patch
	if (angle != 0)
		angle--;

	if (angle >= ((sprframe->rotate & SRF_3DGE) ? 16 : 8)) // out of range?
		return 0;

	// todo: vehicles
#ifdef ROTSPRITE
	if (lua_isnumber(L, 4))
	{
		// rotsprite?????
		angle_t rollangle = luaL_checkangle(L, 4);
		int32_t rot = R_GetRollAngle(rollangle);

		if (rot) {
			patch_t *rotsprite = Patch_GetRotatedSprite(sprframe, frame, angle, sprframe->flip & (1<<angle), true, false, &skins[i]->sprinfo[j], rot);
			LUA_PushUserdata(L, rotsprite, META_PATCH);
			lua_pushdboolean(L, false);
			lua_pushdboolean(L, true);
			return 3;
		}
	}
#endif

	// push both the patch and it's "flip" value
	LUA_PushUserdata(L, W_CachePatchNum(sprframe->lumppat[angle], PU_SPRITE), META_PATCH);
	lua_pushdboolean(L, (sprframe->flip & (1<<angle)) != 0);
	return 2;
}

static int libd_draw(lua_State *L)
{
	int32_t x, y, flags;
	patch_t *patch;
	uint8_t *colormap = NULL;
	huddrawlist_h list;

	HUDONLY
	x = luaL_checkinteger(L, 1);
	y = luaL_checkinteger(L, 2);
	patch = *((patch_t **)luaL_checkudata(L, 3, META_PATCH));
	if (!patch)
		return LUA_ErrInvalid(L, "patch_t");
	flags = luaL_optinteger(L, 4, 0);
	if (!lua_isnoneornil(L, 5))
		colormap = *((uint8_t **)luaL_checkudata(L, 5, META_COLORMAP));

	flags &= ~V_PARAMMASK; // Don't let crashes happen.

	lua_getfield(L, LUA_REGISTRYINDEX, "HUD_DRAW_LIST");
	list = (huddrawlist_h)lua_touserdata(L, -1);
	lua_pop(L, 1);

	if (LUA_HUD_IsDrawListValid(list))
		LUA_HUD_AddDraw(list, x, y, patch, flags, colormap);
	else
		V_DrawFixedPatch(x<<FRACBITS, y<<FRACBITS, FRACUNIT, flags, patch, colormap);
	return 0;
}

static int libd_drawScaled(lua_State *L)
{
	fixed_t x, y, scale;
	int32_t flags;
	patch_t *patch;
	uint8_t *colormap = NULL;
	huddrawlist_h list;

	HUDONLY
	x = luaL_checkinteger(L, 1);
	y = luaL_checkinteger(L, 2);
	scale = luaL_checkinteger(L, 3);
	if (scale < 0)
		return luaL_error(L, "negative scale");
	patch = *((patch_t **)luaL_checkudata(L, 4, META_PATCH));
	if (!patch)
		return LUA_ErrInvalid(L, "patch_t");
	flags = luaL_optinteger(L, 5, 0);
	if (!lua_isnoneornil(L, 6))
		colormap = *((uint8_t **)luaL_checkudata(L, 6, META_COLORMAP));

	flags &= ~V_PARAMMASK; // Don't let crashes happen.

	lua_getfield(L, LUA_REGISTRYINDEX, "HUD_DRAW_LIST");
	list = (huddrawlist_h)lua_touserdata(L, -1);
	lua_pop(L, 1);

	if (LUA_HUD_IsDrawListValid(list))
		LUA_HUD_AddDrawScaled(list, x, y, scale, patch, flags, colormap);
	else
		V_DrawFixedPatch(x, y, scale, flags, patch, colormap);
	return 0;
}

static int libd_drawStretched(lua_State *L)
{
	fixed_t x, y, hscale, vscale;
	int32_t flags;
	patch_t *patch;
	uint8_t *colormap = NULL;
	huddrawlist_h list;

	HUDONLY
	x = luaL_checkinteger(L, 1);
	y = luaL_checkinteger(L, 2);
	hscale = luaL_checkinteger(L, 3);
	if (hscale < 0)
		return luaL_error(L, "negative horizontal scale");
	vscale = luaL_checkinteger(L, 4);
	if (vscale < 0)
		return luaL_error(L, "negative vertical scale");
	patch = *((patch_t **)luaL_checkudata(L, 5, META_PATCH));
	flags = luaL_optinteger(L, 6, 0);
	if (!lua_isnoneornil(L, 7))
		colormap = *((uint8_t **)luaL_checkudata(L, 7, META_COLORMAP));

	flags &= ~V_PARAMMASK; // Don't let crashes happen.

	lua_getfield(L, LUA_REGISTRYINDEX, "HUD_DRAW_LIST");
	list = (huddrawlist_h)lua_touserdata(L, -1);
	lua_pop(L, 1);

	if (LUA_HUD_IsDrawListValid(list))
		LUA_HUD_AddDrawStretched(list, x, y, hscale, vscale, patch, flags, colormap);
	else
		V_DrawStretchyFixedPatch(x, y, hscale, vscale, flags, patch, colormap);
	return 0;
}

static int libd_drawAffine(lua_State *L)
{
	HUDONLY
	fixed_t x = luaL_checkfixed(L, 1);
	fixed_t y = luaL_checkfixed(L, 2);
	affine_t transform = {};
	luaL_checktype(L, 3, LUA_TTABLE);
	patch_t *patch = *((patch_t **)luaL_checkudata(L, 4, META_PATCH));
	int32_t flags = luaL_optinteger(L, 5, 0);
	uint8_t *colormap = NULL;
	if (!lua_isnoneornil(L, 6))
		colormap = *((uint8_t **)luaL_checkudata(L, 6, META_COLORMAP));

	lua_pushnil(L);
	while (lua_next(L, 3))
	{
		int32_t i = 0;
		const char *k = NULL;
		if (lua_isnumber(L, -2))
			i = lua_tointeger(L, -2);
		else if (lua_isstring(L, -2))
			k = lua_tostring(L, -2);

		if (i == 1 || (k && fasticmp(k, "a")))
			transform.a = luaL_checkfixed(L, -1);
		else if (i == 2 || (k && fasticmp(k, "b")))
			transform.b = luaL_checkfixed(L, -1);
		else if (i == 3 || (k && fasticmp(k, "c")))
			transform.c = luaL_checkfixed(L, -1);
		else if (i == 4 || (k && fasticmp(k, "d")))
			transform.d = luaL_checkfixed(L, -1);
		else if (i == 5 || (k && fasticmp(k, "ox")))
			transform.ox = luaL_checkfixed(L, -1);
		else if (i == 6 || (k && fasticmp(k, "oy")))
			transform.oy = luaL_checkfixed(L, -1);

		lua_pop(L, 1);
	}
	lua_pop(L, 1);

	lua_getfield(L, LUA_REGISTRYINDEX, "HUD_DRAW_LIST");
	huddrawlist_h list = lua_touserdata(L, -1);
	(void)list;
	lua_pop(L, 1);

	//if (LUA_HUD_IsDrawListValid(list))
		//LUA_HUD_AddDrawStretched(list, x, y, hscale, vscale, patch, flags, colormap);
	//else
		V_DrawAffinePatch(x, y, &transform, flags, patch, colormap);
	return 0;
}

// KART: draw patch on minimap from x, y coordinates on the map
static int libd_drawOnMinimap(lua_State *L)
{
	fixed_t x, y, scale;	// coordinates of the object
	patch_t *patch;	// patch we want to draw
	uint8_t *colormap = NULL;	// do we want to colormap this patch?
	dboolean centered;	// the patch is centered and doesn't need readjusting on x/y coordinates.
	huddrawlist_h list;

	// variables used to replicate k_kart's mmap drawer:
	int32_t mx, my;
	int32_t splitflags, minimaptrans;

	// base position of the minimap which also takes splits into account:
	int32_t MM_X, MM_Y;

	// variables used for actually drawing the icon:
	fixed_t amnumxpos, amnumypos;
	int32_t amxpos, amypos;

	fixed_t patchw, patchh;

	HUDONLY	// only run this function in hud hooks
	x = luaL_checkinteger(L, 1);
	y = luaL_checkinteger(L, 2);
	scale = luaL_checkinteger(L, 3);
	patch = *((patch_t **)luaL_checkudata(L, 4, META_PATCH));
	if (!lua_isnoneornil(L, 5))
		colormap = *((uint8_t **)luaL_checkudata(L, 5, META_COLORMAP));
	centered = lua_optdboolean(L, 6);

	// replicate exactly what source does for its minimap drawer; AKA hardcoded garbo.

	// first, check what position the mmap is supposed to be in (pasted from k_kart.c):
	MM_X = BASEVIDWIDTH - 50;		// 270
	MM_Y = (BASEVIDHEIGHT/2)-16; //  84
	if (r_splitscreen)
	{
		MM_Y = (BASEVIDHEIGHT/2);
		if (r_splitscreen > 1)	// 3P : bottom right
		{
			MM_X = (3*BASEVIDWIDTH/4);
			MM_Y = (3*BASEVIDHEIGHT/4);

			if (r_splitscreen > 2) // 4P: centered
			{
				MM_X = (BASEVIDWIDTH/2);
				MM_Y = (BASEVIDHEIGHT/2);
			}
		}
	}

	// splitscreen flags
	splitflags = (r_splitscreen == 3 ? 0 : V_SNAPTORIGHT);	// flags should only be 0 when it's centered (4p split)

	{
		const tic_t length = TICRATE/2;

		if (!lt_exitticker)
			return 0;
		minimaptrans = cv_kartminimap.value;
		if (lt_exitticker < length)
			minimaptrans = (((int32_t)lt_exitticker)*minimaptrans)/((int32_t)length);
		if (!minimaptrans)
			return 0;
	}


	minimaptrans = ((10-minimaptrans)<<FF_TRANSSHIFT);
	splitflags |= minimaptrans;

	if (!(r_splitscreen == 2))
	{
		splitflags &= ~minimaptrans;
		splitflags |= V_HUDTRANSHALF;
	}

	splitflags &= ~V_HUDTRANSHALF;
	splitflags |= V_HUDTRANS;

	// Draw the HUD only when playing in a level.
	// hu_stuff needs this, unlike st_stuff.
	if (gamestate != GS_LEVEL)
		return 0;

	if (stplyrnum != 0)
		return 0;

	if (minimapinfo.minimap_pic == NULL)
	{
		return 0; // no pic, just get outta here
	}

	mx = MM_X - (minimapinfo.minimap_pic->width/2);
	my = MM_Y - (minimapinfo.minimap_pic->height/2);

	// let offsets transfer to the heads, too!
	if (encoremode)
		mx += minimapinfo.minimap_pic->leftoffset;
	else
		mx -= minimapinfo.minimap_pic->leftoffset;
	my -= minimapinfo.minimap_pic->topoffset;

	// now that we have replicated this behavior, we can draw an icon from our supplied x, y coordinates by replicating k_kart.c's totally understandable uncommented code!!!

	amnumxpos = (FixedMul(x, minimapinfo.zoom) - minimapinfo.offs_x);
	amnumypos = -(FixedMul(y, minimapinfo.zoom) - minimapinfo.offs_y);

	if (encoremode)
		amnumxpos = -amnumxpos;

	// scale patch coords
	patchw = patch->width*scale /2;
	patchh = patch->height*scale /2;

	if (centered)
		patchw = patchh = 0;	// patch is supposedly already centered, don't butt in.

	amxpos = amnumxpos + ((mx + minimapinfo.minimap_pic->width/2)<<FRACBITS) - patchw;
	amypos = amnumypos + ((my + minimapinfo.minimap_pic->height/2)<<FRACBITS) - patchh;

	// and NOW we can FINALLY DRAW OUR GOD DAMN PATCH :V
	lua_getfield(L, LUA_REGISTRYINDEX, "HUD_DRAW_LIST");
	list = (huddrawlist_h)lua_touserdata(L, -1);
	lua_pop(L, 1);

	if (cv_minihead.value)
		scale /= 2;

	if (LUA_HUD_IsDrawListValid(list))
		LUA_HUD_AddDrawScaled(list, amxpos, amypos, scale, patch, splitflags, colormap);
	else
		V_DrawFixedPatch(amxpos, amypos, scale, splitflags, patch, colormap);
	return 0;
}

static int libd_drawNum(lua_State *L)
{
	int32_t x, y, flags, num;
	huddrawlist_h list;

	HUDONLY
	x = luaL_checkinteger(L, 1);
	y = luaL_checkinteger(L, 2);
	num = luaL_checkinteger(L, 3);
	flags = luaL_optinteger(L, 4, 0);
	flags &= ~V_PARAMMASK; // Don't let crashes happen.

	lua_getfield(L, LUA_REGISTRYINDEX, "HUD_DRAW_LIST");
	list = (huddrawlist_h)lua_touserdata(L, -1);
	lua_pop(L, 1);

	if (LUA_HUD_IsDrawListValid(list))
		LUA_HUD_AddDrawNum(list, x, y, num, flags);
	else
		V_DrawTallNum(x, y, flags, num);
	return 0;
}

static int libd_drawPaddedNum(lua_State *L)
{
	int32_t x, y, flags, num, digits;
	huddrawlist_h list;

	HUDONLY
	x = luaL_checkinteger(L, 1);
	y = luaL_checkinteger(L, 2);
	num = labs(luaL_checkinteger(L, 3));
	digits = luaL_optinteger(L, 4, 2);
	flags = luaL_optinteger(L, 5, 0);
	flags &= ~V_PARAMMASK; // Don't let crashes happen.

	lua_getfield(L, LUA_REGISTRYINDEX, "HUD_DRAW_LIST");
	list = (huddrawlist_h)lua_touserdata(L, -1);
	lua_pop(L, 1);

	if (LUA_HUD_IsDrawListValid(list))
		LUA_HUD_AddDrawPaddedNum(list, x, y, num, digits, flags);
	else
		V_DrawPaddedTallNum(x, y, flags, num, digits);
	return 0;
}

static int libd_drawPingNum(lua_State *L)
{
	int32_t x, y, flags, num;
	uint8_t *colormap = NULL;
	huddrawlist_h list;
	HUDONLY
	x = luaL_checkinteger(L, 1);
	y = luaL_checkinteger(L, 2);
	num = luaL_checkinteger(L, 3);
	flags = luaL_optinteger(L, 4, 0);
	flags &= ~V_PARAMMASK; // Don't let crashes happen.
	if (!lua_isnoneornil(L, 5))
		colormap = *((uint8_t **)luaL_checkudata(L, 5, META_COLORMAP));

	lua_getfield(L, LUA_REGISTRYINDEX, "HUD_DRAW_LIST");
	list = (huddrawlist_h)lua_touserdata(L, -1);
	lua_pop(L, 1);

	if (LUA_HUD_IsDrawListValid(list))
		LUA_HUD_AddDrawPingNum(list, x, y, flags, num, colormap);
	else
		V_DrawPingNum(x, y, flags, num, colormap);
	return 0;
}

static int libd_drawFill(lua_State *L)
{
	huddrawlist_h list;
	int32_t x = luaL_optinteger(L, 1, 0);
	int32_t y = luaL_optinteger(L, 2, 0);
	int32_t w = luaL_optinteger(L, 3, BASEVIDWIDTH);
	int32_t h = luaL_optinteger(L, 4, BASEVIDHEIGHT);
	int32_t c = luaL_optinteger(L, 5, 31);

	HUDONLY

	if (lua_compatmode)
		c = (c & ~0xff) | R_GetPaletteRemap(c & 0xff);

	lua_getfield(L, LUA_REGISTRYINDEX, "HUD_DRAW_LIST");
	list = (huddrawlist_h)lua_touserdata(L, -1);
	lua_pop(L, 1);

	if (LUA_HUD_IsDrawListValid(list))
		LUA_HUD_AddDrawFill(list, x, y, w, h, c);
	else
		V_DrawFill(x, y, w, h, c);
	return 0;
}

static int libd_drawFixedLine(lua_State *L)
{
	huddrawlist_h list;
	int32_t x1 = luaL_optinteger(L, 1, 0);
	int32_t y1 = luaL_optinteger(L, 2, 0);
	int32_t x2 = luaL_optinteger(L, 3, 0);
	int32_t y2 = luaL_optinteger(L, 4, 0);
	int32_t width = luaL_optinteger(L, 5, FRACUNIT);
	int32_t c = luaL_optinteger(L, 6, 31);

	HUDONLY

	if (lua_compatmode)
		c = (c & ~0xff) | R_GetPaletteRemap(c & 0xff);

	lua_getfield(L, LUA_REGISTRYINDEX, "HUD_DRAW_LIST");
	list = (huddrawlist_h)lua_touserdata(L, -1);
	lua_pop(L, 1);

	if (LUA_HUD_IsDrawListValid(list))
		LUA_HUD_AddDrawFixedLine(list, x1, y1, x2, y2, width, c);
	else
		V_DrawFixedLine(x1, y1, x2, y2, width, c);

	return 0;
}

static int libd_getSectorColormap(lua_State *L)
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
	int lightlevel = luaL_optinteger(L, 5, 255);
	uint8_t *colormap = NULL;
	extracolormap_t *exc = NULL;

	INLEVEL
	HUDONLY

	if (has_sector && !sector)
		return LUA_ErrInvalid(L, "sector_t");

	if (sector)
		exc = P_GetColormapFromSectorAt(sector, x, y, z);
	else
		exc = P_GetSectorColormapAt(x, y, z);

	if (exc)
		colormap = exc->colormap;
	else
		colormap = colormaps;

	lightlevel = 255 - min(max(lightlevel, 0), 255);
	lightlevel >>= 3;

	LUA_PushUserdata(L, colormap + (lightlevel * 256), META_COLORMAP);
	return 1;
}

static int libd_fadeScreen(lua_State *L)
{
	uint16_t color = luaL_checkinteger(L, 1);
	uint8_t strength = luaL_checkinteger(L, 2);
	const uint8_t maxstrength = ((color & 0xFF00) ? 32 : 10);
	huddrawlist_h list;

	HUDONLY

	if (!strength)
		return 0;

	if (strength > maxstrength)
		return luaL_error(L, "%s fade strength %d out of range (0 - %d)", ((color & 0xFF00) ? "COLORMAP" : "TRANSMAP"), strength, maxstrength);

	if (lua_compatmode && !(color & 0xFF00))
		color = R_GetPaletteRemap(color);

	lua_getfield(L, LUA_REGISTRYINDEX, "HUD_DRAW_LIST");
	list = (huddrawlist_h)lua_touserdata(L, -1);
	lua_pop(L, 1);

	if (strength == maxstrength) // Allow as a shortcut for drawfill...
	{
		if (LUA_HUD_IsDrawListValid(list))
			LUA_HUD_AddDrawFill(list, 0, 0, BASEVIDWIDTH, BASEVIDHEIGHT, ((color & 0xFF00) ? 31 : color));
		else
			V_DrawFill(0, 0, BASEVIDWIDTH, BASEVIDHEIGHT, ((color & 0xFF00) ? 31 : color));
		return 0;
	}

	if (LUA_HUD_IsDrawListValid(list))
		LUA_HUD_AddFadeScreen(list, color, strength);
	else
		V_DrawFadeScreen(color, strength);
	return 0;
}

int align_fields_ref = LUA_NOREF;

static int libd_drawString(lua_State *L)
{
	huddrawlist_h list;
	fixed_t x = luaL_checkinteger(L, 1);
	fixed_t y = luaL_checkinteger(L, 2);
	const char *str = luaL_checkstring(L, 3);
	int32_t flags = luaL_optinteger(L, 4, V_ALLOWLOWERCASE);
	enum align align = Lua_optoption(L, 5, align_left, align_fields_ref);

	flags &= ~V_PARAMMASK; // Don't let crashes happen.

	HUDONLY

	lua_getfield(L, LUA_REGISTRYINDEX, "HUD_DRAW_LIST");
	list = (huddrawlist_h)lua_touserdata(L, -1);
	lua_pop(L, 1);

	// okay, sorry, this is kind of ugly
	if (LUA_HUD_IsDrawListValid(list))
		LUA_HUD_AddDrawString(list, x, y, str, flags, align);
	else
	switch(align)
	{
	// hu_font
	case align_left:
		V_DrawString(x, y, flags, str);
		break;
	case align_center:
		V_DrawCenteredString(x, y, flags, str);
		break;
	case align_right:
		V_DrawRightAlignedString(x, y, flags, str);
		break;
	// hu_font, 0.5x scale
	case align_small:
		V_DrawSmallString(x, y, flags, str);
		break;
	case align_smallcenter:
		V_DrawCenteredSmallString(x, y, flags, str);
		break;
	case align_smallright:
		V_DrawRightAlignedSmallString(x, y, flags, str);
		break;
	// tny_font
	case align_thin:
		V_DrawThinString(x, y, flags, str);
		break;
	case align_thincenter:
		V_DrawCenteredThinString(x, y, flags, str);
		break;
	case align_thinright:
		V_DrawRightAlignedThinString(x, y, flags, str);
		break;
	}
	return 0;
}

static int libd_drawTitleCardString(lua_State *L)
{

	fixed_t x = luaL_checkinteger(L, 1);
	fixed_t y = luaL_checkinteger(L, 2);
	const char *str = luaL_checkstring(L, 3);
	int32_t flags = luaL_optinteger(L, 4, V_ALLOWLOWERCASE);
	dboolean rightalign = lua_optdboolean(L, 5);
	int32_t timer = luaL_optinteger(L, 6, 0);
	int32_t threshold = luaL_optinteger(L, 7, 0);
	huddrawlist_h list;

	flags &= ~V_PARAMMASK; // Don't let crashes happen.

	HUDONLY
	lua_getfield(L, LUA_REGISTRYINDEX, "HUD_DRAW_LIST");
	list = (huddrawlist_h)lua_touserdata(L, -1);
	lua_pop(L, 1);

	if (LUA_HUD_IsDrawListValid(list))
		LUA_HUD_AddDrawTitleCardString(list, x, y, flags, str, rightalign, timer, threshold);
	else
		V_DrawTitleCardString(x, y, str, flags, rightalign, timer, threshold);
	return 0;
}

static int libd_drawKartString(lua_State *L)
{
	fixed_t x = luaL_checkinteger(L, 1);
	fixed_t y = luaL_checkinteger(L, 2);
	const char *str = luaL_checkstring(L, 3);
	int32_t flags = luaL_optinteger(L, 4, V_ALLOWLOWERCASE);
	huddrawlist_h list;

	flags &= ~V_PARAMMASK; // Don't let crashes happen.

	HUDONLY
	lua_getfield(L, LUA_REGISTRYINDEX, "HUD_DRAW_LIST");
	list = (huddrawlist_h)lua_touserdata(L, -1);
	lua_pop(L, 1);

	if (LUA_HUD_IsDrawListValid(list))
		LUA_HUD_AddDrawKartString(list, x, y, str, flags);
	else
		V_DrawKartString(x, y, flags, str);
	return 0;
}

static int libd_setClipRect(lua_State *L)
{
	fixed_t x = luaL_checkinteger(L, 1);
	fixed_t y = luaL_checkinteger(L, 2);
	fixed_t w = luaL_checkinteger(L, 3);
	fixed_t h = luaL_checkinteger(L, 4);
	int32_t flags = luaL_optinteger(L, 5, 0);
	huddrawlist_h list;

	flags &= ~V_PARAMMASK; // Don't let crashes happen.

	HUDONLY
	lua_getfield(L, LUA_REGISTRYINDEX, "HUD_DRAW_LIST");
	list = (huddrawlist_h)lua_touserdata(L, -1);
	lua_pop(L, 1);
	cliprectenabled = true;

	if (LUA_HUD_IsDrawListValid(list))
		LUA_HUD_AddSetClipRect(list, x, y, w, h, flags);
	else
		V_SetClipRect(x, y, w, h, flags);
	return 0;
}

static int libd_clearClipRect(lua_State *L)
{
	huddrawlist_h list;

	HUDONLY
	lua_getfield(L, LUA_REGISTRYINDEX, "HUD_DRAW_LIST");
	list = (huddrawlist_h)lua_touserdata(L, -1);
	lua_pop(L, 1);
	cliprectenabled = false;

	if (LUA_HUD_IsDrawListValid(list))
		LUA_HUD_AddClearClipRect(list);
	else
		V_ClearClipRect();
	return 0;
}

static int libd_titleCardStringWidth(lua_State *L)
{
	const char *str = luaL_checkstring(L, 1);
	HUDONLY

	lua_pushinteger(L, V_TitleCardStringWidth(str));
	return 1;
}

int widtht_fields_ref = LUA_NOREF;

static int libd_stringWidth(lua_State *L)
{
	const char *str = luaL_checkstring(L, 1);
	int32_t flags = luaL_optinteger(L, 2, V_ALLOWLOWERCASE);
	enum widtht widtht = Lua_optoption(L, 3, widtht_normal, widtht_fields_ref);

	HUDONLY
	switch(widtht)
	{
	case widtht_normal: // hu_font
		lua_pushinteger(L, V_StringWidth(str, flags));
		break;
	case widtht_small: // hu_font, 0.5x scale
		lua_pushinteger(L, V_SmallStringWidth(str, flags));
		break;
	case widtht_thin: // tny_font
		lua_pushinteger(L, V_ThinStringWidth(str, flags));
		break;
	}
	return 1;
}

static int libd_getColormap(lua_State *L)
{
	int32_t skinnum = TC_DEFAULT;
	skincolornum_t color = luaL_optinteger(L, 2, 0);
	uint8_t* colormap = NULL;
	HUDONLY
	if (lua_isnoneornil(L, 1))
		; // defaults to TC_DEFAULT
	else if (lua_type(L, 1) == LUA_TNUMBER) // skin number
	{
		skinnum = (int32_t)luaL_checkinteger(L, 1);

		if (lua_compatmode)
		{
			if (skinnum >= MAXSKINS)
				return luaL_error(L, "skin number %d is out of range (>%d)", skinnum, MAXSKINS-1);
			else if (skinnum < -6 && !(skinnum >= TC_BOSS && skinnum <= TC_DEFAULT))
				return luaL_error(L, "translation colormap index is out of range");

			switch(skinnum)
			{
				case -6:
					skinnum = TC_BLINK;
					break;
				case -5:
					skinnum = TC_RAINBOW;
					break;
				case -4:
					skinnum = TC_ALLWHITE;
					break;
				case -3:
					skinnum = TC_METALSONIC;
					break;
				case -2:
					skinnum = TC_BOSS;
					break;
				case -1:
					skinnum = TC_DEFAULT;
					break;
			}
		}
		else
		{
			if (skinnum >= MAXSKINS)
				return luaL_error(L, "skin number %d is out of range (>%d)", skinnum, MAXSKINS-1);
			else if (skinnum < 0 && skinnum > TC_DEFAULT)
				return luaL_error(L, "translation colormap index is out of range");
		}

	}
	else // skin name
	{
		const char *skinname = luaL_checkstring(L, 1);
		int32_t i = R_SkinAvailable(skinname);
		if (i != -1) // if -1, just default to TC_DEFAULT as above
			skinnum = i;
	}

	// all was successful above, now we generate the colormap at last!

	colormap = R_GetTranslationColormap(skinnum, color, GTC_CACHE);
	LUA_PushUserdata(L, colormap, META_COLORMAP); // push as META_COLORMAP userdata, specifically for patches to use!
	return 1;
}

int hud_patch_fields_ref = LUA_NOREF;

static int libd_getColorHudPatch(lua_State *L)
{
	HUDONLY
	enum hudpatch option = Lua_optoption(L, 1, -1, hud_patch_fields_ref);
	patch_t *patch;
	uint8_t *colormap = R_GetTranslationColormap(TC_DEFAULT, K_GetHudColor(), GTC_CACHE);
	dboolean small, dark, multi;

	switch (option) {
		case hudpatch_item:
			small = lua_optdboolean(L, 2);
			dark = lua_optdboolean(L, 3);
			patch = K_getItemBoxPatch(small, dark);
			if (!cv_colorizeditembox.value)
				colormap = NULL;
			break;
		case hudpatch_itemmul:
			small = lua_optdboolean(L, 2);
			patch = K_getItemMulPatch(small);
			break;
		case hudpatch_itemalt:
			small = lua_optdboolean(L, 2);
			multi = lua_optdboolean(L, 3);
			patch = K_getItemAltPatch(small, multi);
			break;
		default:
			return 0; // you shouldn't be here
	}

	LUA_PushUserdata(L, patch, META_PATCH);
	if (colormap && K_UseColorHud())
		LUA_PushUserdata(L, colormap, META_COLORMAP);
	else
		lua_pushnil(L);

	return 2;
}

static int libd_getHudColor(lua_State *L)
{
	HUDONLY
	lua_pushinteger(L, K_GetHudColor());
	return 1;
}

static int libd_useColorHud(lua_State *L)
{
	HUDONLY
	lua_pushdboolean(L, K_UseColorHud());
	return 1;
}

static int libd_getStringColormap(lua_State *L)
{
	int32_t flags = luaL_checkinteger(L, 1);
	uint8_t* colormap = NULL;
	HUDONLY
	colormap = V_GetStringColormap(flags & V_CHARCOLORMASK);
	if (colormap) {
		LUA_PushUserdata(L, colormap, META_COLORMAP); // push as META_COLORMAP userdata, specifically for patches to use!
		return 1;
	}
	return 0;
}

static int libd_getMapThumbnail(lua_State *L)
{
	mapnum_t mapnum;
	patch_t *patch = NULL;
	HUDONLY
	if (lua_type(L, 1) == LUA_TNUMBER)
		mapnum = luaL_checkinteger(L, 1) - 1;
	else
		mapnum = G_MapNumber(luaL_checkstring(L, 1));

	fixed_t scale = M_GetMapThumbnail(mapnum, &patch);
	LUA_PushUserdata(L, patch, META_PATCH);
	lua_pushfixed(L, scale);
	return 2;
}

static int libd_getMapMinimap(lua_State *L)
{
	mapnum_t mapnum;
	patch_t *patch = NULL;
	HUDONLY
	if (lua_type(L, 1) == LUA_TNUMBER)
		mapnum = luaL_checkinteger(L, 1) - 1;
	else
		mapnum = G_MapNumber(luaL_checkstring(L, 1));

	if (/*mapnum >= 0 && */mapnum < nummapheaders && mapheaderinfo[mapnum])
		patch = mapheaderinfo[mapnum]->minimapPic;
	if (!patch)
		patch = missingpat;

	LUA_PushUserdata(L, patch, META_PATCH);
	return 1;
}

static int libd_width(lua_State *L)
{
	HUDONLY
	lua_pushinteger(L, vid.width); // push screen width
	return 1;
}

static int libd_height(lua_State *L)
{
	HUDONLY
	lua_pushinteger(L, vid.height); // push screen height
	return 1;
}

static int libd_dup(lua_State *L)
{
	HUDONLY
	lua_pushinteger(L, vid.dup); // push integral scale (patch scale)
	lua_pushfixed(L, vid.fdup); // push fixed point scale (position scale)
	return 2;
}

static int libd_renderer(lua_State *L)
{
	HUDONLY
	switch (rendermode) {
		case render_opengl: lua_pushliteral(L, "opengl");   break; // OpenGL renderer
		case render_soft:   lua_pushliteral(L, "software"); break; // Software renderer
		default:            lua_pushliteral(L, "none");     break; // render_none (for dedicated), in case there's any reason this should be run
	}
	return 1;
}

// M_RANDOM
//////////////

static int libd_RandomFixed(lua_State *L)
{
	HUDONLY
	lua_pushfixed(L, M_RandomFixed());
	return 1;
}

static int libd_RandomByte(lua_State *L)
{
	HUDONLY
	lua_pushinteger(L, M_RandomByte());
	return 1;
}

static int libd_RandomKey(lua_State *L)
{
	int32_t a = (int32_t)luaL_checkinteger(L, 1);

	HUDONLY
	if (a > 65536)
		LUA_UsageWarning(L, "v.RandomKey: range > 65536 is undefined behavior");
	lua_pushinteger(L, M_RandomKey(a));
	return 1;
}

static int libd_RandomRange(lua_State *L)
{
	int32_t a = (int32_t)luaL_checkinteger(L, 1);
	int32_t b = (int32_t)luaL_checkinteger(L, 2);

	HUDONLY
	if (b < a) {
		int32_t c = a;
		a = b;
		b = c;
	}
	if ((b-a+1) > 65536)
		LUA_UsageWarning(L, "v.RandomRange: range > 65536 is undefined behavior");
	lua_pushinteger(L, M_RandomRange(a, b));
	return 1;
}

// Macros.
static int libd_SignedRandom(lua_State *L)
{
	HUDONLY
	lua_pushinteger(L, M_SignedRandom());
	return 1;
}

static int libd_RandomChance(lua_State *L)
{
	fixed_t p = luaL_checkfixed(L, 1);
	HUDONLY
	lua_pushdboolean(L, M_RandomChance(p));
	return 1;
}

// 30/10/18 Lat': Get st_translucency's value for HUD rendering as a normal V_xxTRANS int
// Could as well be thrown in global vars for ease of access but I guess it makes sense for it to be a HUD fn
static int libd_getlocaltransflag(lua_State *L)
{
	HUDONLY
	lua_pushinteger(L, (10-st_translucency)*V_10TRANS);
	return 1;
}

enum huddrawinfo {
	huddrawinfo_item = 0,
	huddrawinfo_gametypeinfo,
	huddrawinfo_rings,
	huddrawinfo_minimap,
	huddrawinfo_timers,
	huddrawinfo_drafting,
	huddrawinfo_lives,
};

static const char *const hud_drawinfo_options[] = {
	"item",
	"gametypeinfo",
	"rings",
	"minimap",
	"timers",
	"drafting",
	"lives",
	NULL};

int hud_drawinfo_fields_ref = LUA_NOREF;

static int libd_getDrawInfo(lua_State *L)
{
	HUDONLY
	enum huddrawinfo option = Lua_optoption(L, 1, -1, hud_drawinfo_fields_ref);
	drawinfo_t info = {0};
	rouletteinfo_t rinfo = {0};

	// Initialize the extra values that might not get used.
	info.hudScale = 0;
	info.hudScaleFloat = 0.0f;

	rinfo.crop.x = 0;
	rinfo.crop.y = 0;
	rinfo.flags = 0;
	rinfo.offset = 0;
	rinfo.spacing = 0;
	rinfo.intSpacing = 0;

	switch(option) {
		case huddrawinfo_item:          K_getItemBoxDrawinfo(&info, &rinfo);break;
		case huddrawinfo_gametypeinfo:  K_getLapsDrawinfo(&info);			break;
		case huddrawinfo_rings:         K_getRingsDrawinfo(&info);			break;
		case huddrawinfo_minimap:       K_getMinimapDrawinfo(&info);		break;
		case huddrawinfo_timers:        K_getSlipstreamDrawinfo(&info);		break;
		case huddrawinfo_drafting:      K_getTimersDrawinfo(&info);			break;
		case huddrawinfo_lives:      	K_getLivesnStatsDrawinfo(&info);	break;
		default:
			return 0; // unreachable
	}

	// There's no easy solution to pushing temporary metadata to Lua,
	// so to avoid memory leaks, I'm making a massive table with all the data. Sue me.

	lua_createtable(L, 0, 5);

	lua_pushinteger(L, info.x);
	lua_setfield(L, -2, "x");

	lua_pushinteger(L, info.y);
	lua_setfield(L, -2, "y");

	lua_pushinteger(L, info.flags);
	lua_setfield(L, -2, "flags");

	lua_pushinteger(L, info.flipamount);
	lua_setfield(L, -2, "flipamount");

	lua_pushinteger(L, FLOAT_TO_FIXED(info.hudScaleFloat)); // Can't use floats in BLUA
	lua_setfield(L, -2, "hudscale");

	lua_createtable(L, 0, 5);

		lua_createtable(L, 0, 2);
			lua_pushinteger(L, rinfo.crop.x);
			lua_setfield(L, -2, "x");

			lua_pushinteger(L, rinfo.crop.y);
			lua_setfield(L, -2, "y");
		lua_setfield(L, -2, "crop");

		lua_pushinteger(L, rinfo.offset);
		lua_setfield(L, -2, "offset");

		lua_pushinteger(L, rinfo.spacing);
		lua_setfield(L, -2, "spacing");

		lua_pushinteger(L, rinfo.intSpacing);
		lua_setfield(L, -2, "intspacing");

		lua_pushinteger(L, rinfo.flags);
		lua_setfield(L, -2, "flags");
	lua_setfield(L, -2, "roulette");

	return 1;
}

// Get cv_translucenthud's value for HUD rendering as a normal V_xxTRANS int
static int libd_getusertransflag(lua_State *L)
{
	HUDONLY
	lua_pushinteger(L, (10-cv_translucenthud.value)*V_10TRANS);	// A bit weird that it's called "translucenthud" yet 10 is fully opaque :V
	return 1;
}

// Return the time elapsed for the previous frame, in tics.
static int libd_getDeltaTime(lua_State *L)
{
	HUDONLY
	lua_pushfixed(L, renderdeltatics);
	return 1;
}

static int libd_interpolate(lua_State *L)
{
	HUDONLY

	// do type checking even if interpolation is disabled
	dboolean newinterpolate;
	uint8_t newtag;

	if (lua_isnumber(L, 1))
	{
		newinterpolate = true;
		newtag = luaL_checkinteger(L, 1);
	}
	else
	{
		newinterpolate = luaL_checkdboolean(L, 1);
		newtag = 0;
	}

	hud_interpolate = newinterpolate;
	hud_interptag = newtag;

	return 0;
}

static int libd_interpLatch(lua_State *L)
{
	HUDONLY
	hud_interpstring = hud_interplatch = luaL_checkdboolean(L, 1);
	return 0;
}

static luaL_Reg lib_draw[] = {
	// cache
	{"patchExists", libd_patchExists},
	{"cachePatch", libd_cachePatch},
	{"cachePatchRotated", libd_cachePatch}, // Backcompat for v1 extension rotated patch caching.
	{"getSpritePatch", libd_getSpritePatch},
	{"getSprite2Patch", libd_getSprite2Patch},
	{"getColormap", libd_getColormap},
	{"getStringColormap", libd_getStringColormap},
	{"getSectorColormap", libd_getSectorColormap},
	{"getMapThumbnail", libd_getMapThumbnail},
	{"getMapMinimap", libd_getMapMinimap},
	// drawing
	{"draw", libd_draw},
	{"drawScaled", libd_drawScaled},
	{"drawStretched", libd_drawStretched},
	{"drawAffine", libd_drawAffine},
	{"drawNum", libd_drawNum},
	{"drawPaddedNum", libd_drawPaddedNum},
	{"drawPingNum", libd_drawPingNum},
	{"drawFill", libd_drawFill},
	{"drawLine", libd_drawFixedLine},
	{"fadeScreen", libd_fadeScreen},
	{"drawString", libd_drawString},
	{"drawTitleCardString", libd_drawTitleCardString},
	{"drawKartString", libd_drawKartString},
	{"setClipRect", libd_setClipRect},
	{"clearClipRect", libd_clearClipRect},
	// misc
	{"stringWidth", libd_stringWidth},
	{"titleCardStringWidth", libd_titleCardStringWidth},
	// m_random
	{"RandomFixed",libd_RandomFixed},
	{"RandomByte",libd_RandomByte},
	{"RandomKey",libd_RandomKey},
	{"RandomRange",libd_RandomRange},
	{"SignedRandom",libd_SignedRandom}, // MACRO
	{"RandomChance",libd_RandomChance}, // MACRO
	// properties
	{"width", libd_width},
	{"height", libd_height},
	{"dup", libd_dup},
	{"dupx", libd_dup},
	{"dupy", libd_dup},
	{"renderer", libd_renderer},
	{"localTransFlag", libd_getlocaltransflag},
	{"drawOnMinimap", libd_drawOnMinimap},
	{"getColorHudPatch", libd_getColorHudPatch},
	{"getDrawInfo", libd_getDrawInfo},
	{"getHudColor", libd_getHudColor},
	{"useColorHud", libd_useColorHud},
	{"userTransFlag", libd_getusertransflag},
	{"getDeltaTime", libd_getDeltaTime},
	// interpolation
	{"interpolate", libd_interpolate},
	{"interpLatch", libd_interpLatch},
	{NULL, NULL}
};

static int lib_draw_ref;

//
// lib_hud
//

int hud_disable_fields_ref = LUA_NOREF;

// enable vanilla HUD element
static int lib_hudenable(lua_State *L)
{
	enum hud option = Lua_optoption(L, 1, -1, hud_disable_fields_ref);
	hud_enabled[option/8] |= 1<<(option%8);
	return 0;
}

// disable vanilla HUD element
static int lib_huddisable(lua_State *L)
{
	enum hud option = Lua_optoption(L, 1, -1, hud_disable_fields_ref);
	hud_enabled[option/8] &= ~(1<<(option%8));
	return 0;
}

// 30/10/18: Lat': How come this wasn't here before?
static int lib_hudenabled(lua_State *L)
{
	enum hud option = Lua_optoption(L, 1, -1, hud_disable_fields_ref);
	if (hud_enabled[option/8] & (1<<(option%8)))
		lua_pushdboolean(L, true);
	else
		lua_pushdboolean(L, false);

	return 1;
}

static int lib_hudsetvotebackground(lua_State *L)
{	
	if (dedicated)
		return 0;

	// Reset to default
	memset(VoteScreen.luaPrefix, 0, sizeof(VoteScreen.luaPrefix));
	VoteScreen.timePerAnimFrame = 2;

	if (!lua_isnoneornil(L, 1))
	{
		const char *prefix = luaL_checkstring(L, 1);

		if (strlen(prefix) != 4)
		{
			return luaL_argerror(L, 1, "prefix should 4 characters wide");
		}

		strncpy(VoteScreen.luaPrefix, prefix, 4);
		strupr(VoteScreen.luaPrefix);
	}

	if (!lua_isnoneornil(L, 2))
	{
		int32_t time_per_frame = luaL_checkinteger(L, 2);

		if (time_per_frame <= 0)
			return luaL_argerror(L, 2, "time per frame should be 1 or more");

		VoteScreen.timePerAnimFrame = time_per_frame;
	}

	// Update background if we're already on vote screen
	if (gamestate == GS_VOTING)
		Y_VoteScreenCheck();

	return 0;
}

int hud_offsets_fields_ref = LUA_NOREF;

static int lib_hudgetoffsets(lua_State *L)
{
	enum hudoffsets option = Lua_optoption(L, 1, -1, hud_offsets_fields_ref);
	int32_t ofx, ofy;

#define OFS(it) ofx = cv_##it##_xoffset.value; ofy = cv_##it##_yoffset.value; break;
#define OFY(it) ofx = 0; ofy = cv_##it##_yoffset.value; break;
	switch(option) {
		case hudoffsets_item:           OFS(item)
		case hudoffsets_time:           OFS(time)
		case hudoffsets_gametypeinfo:   OFS(laps)
		case hudoffsets_rings:          OFS(rings)
		case hudoffsets_countdown:      OFS(dnft)
		case hudoffsets_speedometer:    OFS(speed)
		case hudoffsets_position:       OFS(posi)
		case hudoffsets_minirankings:   OFS(face)
		case hudoffsets_startcountdown: OFS(stcd)
		case hudoffsets_check:          OFY(chek)
		case hudoffsets_minimap:        OFS(mini)
		case hudoffsets_wanted:         OFS(want)
		//case hudoffsets_statdisplay:    OFS(stat)
		default:
			return 0; // unreachable
	}
#undef OFS
#undef OFY

	lua_pushinteger(L, ofx);
	lua_pushinteger(L, ofy);
	return 2;
}

static int lib_hudaddtimer(lua_State *L)
{
	const char *name = luaL_checkstring(L, 1);

	luaL_checktype(L, 2, LUA_TFUNCTION); // We get to it later

	luaL_checktype(L, 3, LUA_TTABLE);

	int32_t patches_size = lua_objlen(L, 3);
	CLEANUP(pfree) const char **patches = calloc(patches_size, sizeof(const char*));

	if (patches == NULL)
		I_Error("lib_hudaddtimer: Out of memory!\n");

	for (int32_t i = 0; i < patches_size; ++i)
	{
		lua_rawgeti(L, 3, i+1);
		patches[i] = lua_tostring(L, -1);
		lua_pop(L, 1);
	}

	int32_t anim_duration = luaL_optinteger(L, 4, 1);

	uint32_t flags = luaL_optinteger(L, 5, 0);

	dboolean unsorted = lua_todboolean(L, 6);

	lua_Integer timer_func_ref;

	// Store function into registry
	lua_getfield(L, LUA_REGISTRYINDEX, LREG_TIMERS);
	lua_pushvalue(L, 2);
	timer_func_ref = luaL_ref(L, -2);

	K_AddItemTimerEx(name, timer_func_ref, patches, patches_size, anim_duration, flags, unsorted);

	return 0;
}

// add a HUD element for rendering
extern int lib_hudadd(lua_State *L);

static luaL_Reg lib_hud[] = {
	{"enable", lib_hudenable},
	{"disable", lib_huddisable},
	{"enabled", lib_hudenabled},
	{"add", lib_hudadd},
	{"setVoteBackground", lib_hudsetvotebackground},
	{"getOffsets", lib_hudgetoffsets},
	{"addTimer", lib_hudaddtimer},
	{NULL, NULL}
};

//
//
//

int LUA_HudLib(lua_State *L)
{
	memset(hud_enabled, 0xff, (hud_MAX/8)+1);

	patch_fields_ref = Lua_CreateFieldTable(L, patch_opt);
	camera_fields_ref = Lua_CreateFieldTable(L, camera_opt);
	align_fields_ref = Lua_CreateFieldTable(L, align_opt);
	widtht_fields_ref = Lua_CreateFieldTable(L, widtht_opt);
	hud_patch_fields_ref = Lua_CreateFieldTable(L, hud_patch_options);
	hud_drawinfo_fields_ref = Lua_CreateFieldTable(L, hud_drawinfo_options);
	hud_disable_fields_ref = Lua_CreateFieldTable(L, hud_disable_options);
	hud_offsets_fields_ref = Lua_CreateFieldTable(L, hud_offsets_options);

	lua_newtable(L);
	luaL_register(L, NULL, lib_draw);
	lib_draw_ref = luaL_ref(L, LUA_REGISTRYINDEX);

	lua_newtable(L);
	lua_setfield(L, LUA_REGISTRYINDEX, LREG_TIMERS);

	luaL_newmetatable(L, META_COLORMAP);
		lua_pushcfunction(L, colormap_get);
		lua_setfield(L, -2, "__index");
	lua_pop(L,1);

	luaL_newmetatable(L, META_PATCH);
		lua_pushcfunction(L, patch_get);
		lua_setfield(L, -2, "__index");

		lua_pushcfunction(L, patch_set);
		lua_setfield(L, -2, "__newindex");
	lua_pop(L,1);

	luaL_newmetatable(L, META_CAMERA);
		lua_pushcfunction(L, camera_get);
		lua_setfield(L, -2, "__index");
	lua_pop(L,1);

	lua_newuserdata(L, 0);
		lua_createtable(L, 0, 2);
			lua_pushcfunction(L, lib_getCamera);
			lua_setfield(L, -2, "__index");

			lua_pushcfunction(L, lib_lenCamera);
			lua_setfield(L, -2, "__len");
		lua_setmetatable(L, -2);
	lua_setglobal(L, "cameras");

	luaL_register(L, "hud", lib_hud);
	return 0;
}

dboolean LUA_HudEnabled(enum hud option)
{
	if (!gL || hud_enabled[option/8] & (1<<(option%8)))
		return true;
	return false;
}

void LUA_SetHudHook(int hook, huddrawlist_h list)
{
	lua_getref(gL, lib_draw_ref);

	lua_pushlightuserdata(gL, list);
	lua_setfield(gL, LUA_REGISTRYINDEX, "HUD_DRAW_LIST");

	cliprectenabled = false;

	switch (hook)
	{
		case HUD_HOOK(game):
			camnum = R_GetViewNumber();

			LUA_PushUserdata(gL, stplyr, META_PLAYER);
			LUA_PushUserdata(gL, &camera[camnum], META_CAMERA);

			camnum++; // for compatibility
			break;

		case HUD_HOOK(titlecard):
			LUA_PushUserdata(gL, stplyr, META_PLAYER);
			lua_pushinteger(gL, lt_ticker);
			lua_pushinteger(gL, (lt_endtime + TICRATE));
			break;
	}
}

void LUA_ClearClipRect(void)
{
	// V_ClearClipRect is not enough: we need to insert a clearClipRect
	// into the drawlist to protect both hardcode and other HUD hooks.
	// thankfully the below function takes zero Lua arguments :^)
	if (cliprectenabled)
		libd_clearClipRect(gL);
	//cliprectenabled = false;
}
