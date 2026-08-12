// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2021-2022 by "Lactozilla".
// Copyright (C) 2014-2023 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  lua_colorlib.c
/// \brief color and colormap libraries for Lua scripting

#include "doomdef.h"
#include "fastcmp.h"
#include "r_data.h"
#include "v_video.h"
#include "k_color.h"

#include "lua_script.h"
#include "lua_libs.h"

#define COLORLIB_USE_LOOKUP

#ifdef COLORLIB_USE_LOOKUP
	static colorlookup_t colormix_lut;
	#define GetNearestColor(r, g, b) GetColorLUT(&colormix_lut, r, g, b)
#else
	#define GetNearestColor(r, g, b) NearestPaletteColor(r, g, b, pMasterPalette)
#endif

////////////////
// Color library
////////////////

static int lib_colorPaletteToRgb(lua_State *L)
{
	RGBA_t color = V_GetMasterColor((uint8_t)luaL_checkinteger(L, 1));
	lua_pushinteger(L, color.s.red);
	lua_pushinteger(L, color.s.green);
	lua_pushinteger(L, color.s.blue);
	return 3;
}

static int lib_paletteFromSkincolor(lua_State *L)
{
	skincolornum_t skincolor = luaL_checkinteger(L, 1);
	uint8_t table_index = 4;

	if (!lua_isnoneornil(L, 2))
	{
		table_index = (uint8_t)luaL_checkinteger(L, 2);
	}

	uint8_t pal_index = V_GetColorFromSkinColorEx(skincolor, table_index);

	lua_pushinteger(L, pal_index);
	return 1;
}

static int lib_paletteFromSkincolorDimmed(lua_State *L)
{
	skincolornum_t skincolor = luaL_checkinteger(L, 1);
	uint8_t table_index = (uint8_t)luaL_checkinteger(L, 2);
	fixed_t brightness = luaL_checkfixed(L, 3);

	uint8_t pal_index = V_GetColorFromSkinColorWithBrightness(skincolor, table_index, brightness);

	lua_pushinteger(L, pal_index);
	return 1;
}

#define IS_HEX_CHAR(x) ((x >= '0' && x <= '9') || (x >= 'a' && x <= 'f') || (x >= 'A' && x <= 'F'))
#define ARE_HEX_CHARS(str, i) IS_HEX_CHAR(str[i]) && IS_HEX_CHAR(str[i + 1])

static uint32_t hex2int(char x)
{
	if (x >= '0' && x <= '9')
		return x - '0';
	else if (x >= 'a' && x <= 'f')
		return x - 'a' + 10;
	else if (x >= 'A' && x <= 'F')
		return x - 'A' + 10;

	return 0;
}

static uint8_t GetHTMLColorLength(const char *str)
{
	if (str[0] == '#')
		str++;
	return strlen(str) >= 8 ? 4 : 3;
}

static uint8_t ParseHTMLColor(const char *str, uint8_t *rgba, size_t numc)
{
	const char *hex = str;

	if (hex[0] == '#')
		hex++;
	else if (!IS_HEX_CHAR(hex[0]))
		return 0;

	size_t len = strlen(hex);

	if (len == 3)
	{
		// Shorthand like #09C
		for (unsigned i = 0; i < 3; i++)
		{
			if (!IS_HEX_CHAR(hex[i]))
				return 0;

			uint32_t hx = hex2int(hex[i]);
			*rgba++ = (hx * 16) + hx;
		}

		return 3;
	}
	else if (len == 6 || len == 8)
	{
		if (numc != 4)
			len = 6;

		// A triplet like #0099CC
		for (unsigned i = 0; i < len; i += 2)
		{
			if (!ARE_HEX_CHARS(hex, i))
				return false;

			*rgba++ = (hex2int(hex[i]) * 16) + hex2int(hex[i + 1]);
		}

		return len;
	}

	return 0;
}

static uint8_t GetPackedRGBA(uint32_t colors, uint8_t *rgba)
{
	if (colors > 0xFFFFFF)
	{
		rgba[0] = (colors >> 24) & 0xFF;
		rgba[1] = (colors >> 16) & 0xFF;
		rgba[2] = (colors >> 8) & 0xFF;
		rgba[3] = colors & 0xFF;
		return 4;
	}
	else
	{
		rgba[0] = (colors >> 16) & 0xFF;
		rgba[1] = (colors >> 8) & 0xFF;
		rgba[2] = colors & 0xFF;
		rgba[3] = 0xFF;
		return 3;
	}
}

static uint8_t GetArgsRGBA(lua_State *L, uint8_t index, int32_t *r, int32_t *g, int32_t *b, int32_t *a)
{
	uint8_t rgba[4] = { 0, 0, 0, 255 };
	uint8_t num = 0;

	if (lua_gettop(L) == 1 && lua_type(L, index) == LUA_TNUMBER)
	{
		num = GetPackedRGBA(luaL_checkinteger(L, 1), rgba);

		*r = rgba[0];
		*g = rgba[1];
		*b = rgba[2];
		if (a)
			*a = rgba[3];
	}
	else if (lua_type(L, index) == LUA_TSTRING)
	{
		const char *str = lua_tostring(L, index);
		uint8_t parsed = ParseHTMLColor(str, rgba, GetHTMLColorLength(str));
		if (!parsed)
			luaL_error(L, "Malformed HTML color '%s'", str);

		num = parsed == 8 ? 4 : 3;

		*r = rgba[0];
		*g = rgba[1];
		*b = rgba[2];
		if (a)
			*a = rgba[3];
	}
	else
	{
		int32_t temp;

#define CHECKINT(i) luaL_checkinteger(L, i)
#define GETCOLOR(c, i, desc) { \
			temp = CHECKINT(i); \
			if (temp < 0 || temp > 255) \
				luaL_error(L, desc " channel %d out of range (0 - 255)", temp); \
			c = temp; \
			num++; \
		}

		GETCOLOR(*r, index + 0, "red color");
		GETCOLOR(*g, index + 1, "green color");
		GETCOLOR(*b, index + 2, "blue color");
#undef CHECKINT
#define CHECKINT(i) luaL_optinteger(L, i, 255)
		if (a)
			GETCOLOR(*a, index + 3, "alpha");
#undef CHECKINT
#undef GETCOLOR

		num = 3 + (lua_type(L, index + 3) == LUA_TNUMBER);
	}

	return num;
}

static int lib_colorRgbToPalette(lua_State *L)
{
	int32_t r, g, b;
	GetArgsRGBA(L, 1, &r, &g, &b, NULL);

#ifdef COLORLIB_USE_LOOKUP
	InitColorLUT(&colormix_lut, pMasterPalette, false);
#endif

	lua_pushinteger(L, GetNearestColor(r, g, b));
	return 1;
}

#define SCALE_UINT8_TO_FIXED(val) FixedDiv(val * FRACUNIT, 255 * FRACUNIT)
#define SCALE_FIXED_TO_UINT8(val) FixedRound(FixedMul(val, 255 * FRACUNIT)) / FRACUNIT

static fixed_t hue2rgb(fixed_t p, fixed_t q, fixed_t t)
{
	if (t < 0)
		t += FRACUNIT;
	if (t > FRACUNIT)
		t -= FRACUNIT;

	fixed_t out;

	if (t < FRACUNIT / 6)
		out = p + FixedMul(FixedMul(q - p, 6 * FRACUNIT), t);
	else if (t < FRACUNIT / 2)
		out = q;
	else if (t < 2 * FRACUNIT / 3)
		out = p + FixedMul(FixedMul(q - p, 2 * FRACUNIT / 3 - t), 6 * FRACUNIT);
	else
		out = p;

	return out;
}

static int lib_colorHslToRgb(lua_State *L)
{
	fixed_t h, s, l;

#define GETHSL(c, i, desc) \
	c = luaL_checkinteger(L, i); \
	if (c < 0 || c > 255) \
		luaL_error(L, desc " %d out of range (0 - 255)", c)

	GETHSL(h, 1, "hue");
	GETHSL(s, 2, "saturation");
	GETHSL(l, 3, "value");
#undef GETHSL

	if (!s)
	{
		lua_pushinteger(L, l);
		lua_pushinteger(L, l);
		lua_pushinteger(L, l);
	}
	else
	{
		h = SCALE_UINT8_TO_FIXED(h);
		s = SCALE_UINT8_TO_FIXED(s);
		l = SCALE_UINT8_TO_FIXED(l);

		fixed_t q, p;

		if (l < FRACUNIT/2)
			q = FixedMul(l, FRACUNIT + s);
		else
			q = l + s - FixedMul(l, s);

		p = l * 2 - q;

		lua_pushinteger(L, SCALE_FIXED_TO_UINT8(hue2rgb(p, q, h + FRACUNIT/3)));
		lua_pushinteger(L, SCALE_FIXED_TO_UINT8(hue2rgb(p, q, h)));
		lua_pushinteger(L, SCALE_FIXED_TO_UINT8(hue2rgb(p, q, h - FRACUNIT/3)));
	}

	return 3;
}

static int lib_colorRgbToHsl(lua_State *L)
{
	int32_t ir, ig, ib;
	GetArgsRGBA(L, 1, &ir, &ig, &ib, NULL);

	fixed_t r = SCALE_UINT8_TO_FIXED(ir);
	fixed_t g = SCALE_UINT8_TO_FIXED(ig);
	fixed_t b = SCALE_UINT8_TO_FIXED(ib);

	fixed_t cmin = min(min(r, g), b);
	fixed_t cmax = max(max(r, g), b);

	fixed_t h, s, l = (cmax + cmin) / 2;
	fixed_t delta = cmax - cmin;

	if (!delta)
		h = s = 0;
	else
	{
		if (l > FRACUNIT / 2)
			s = FixedDiv(delta, (FRACUNIT * 2) - cmax - cmin);
		else
			s = FixedDiv(delta, cmax + cmin);

		if (r > g && r > b)
		{
			h = FixedDiv(g - b, delta);

			if (g < b)
				h += FRACUNIT * 6;
		}
		else
		{
			h = FixedDiv(r - g, delta);

			if (g > b)
				h += FRACUNIT * 2;
			else
				h += FRACUNIT * 4;
		}

		h = FixedDiv(h, FRACUNIT * 6);
	}

	lua_pushinteger(L, SCALE_FIXED_TO_UINT8(h));
	lua_pushinteger(L, SCALE_FIXED_TO_UINT8(s));
	lua_pushinteger(L, SCALE_FIXED_TO_UINT8(l));

	return 3;
}

static int lib_colorHexToRgb(lua_State *L)
{
	uint8_t rgba[4] = { 0, 0, 0, 255 };

	const char *str = luaL_checkstring(L, 1);
	uint8_t parsed = ParseHTMLColor(str, rgba, 4), num = 3;
	if (!parsed)
		luaL_error(L, "Malformed HTML color '%s'", str);
	else if (parsed == 8)
		num++;

	lua_pushinteger(L, rgba[0]);
	lua_pushinteger(L, rgba[1]);
	lua_pushinteger(L, rgba[2]);
	if (num == 4)
		lua_pushinteger(L, rgba[3]);

	return num;
}

static int lib_colorRgbToHex(lua_State *L)
{
	int32_t r, g, b, a;
	uint8_t num = GetArgsRGBA(L, 1, &r, &g, &b, &a);

	char buffer[10];
	if (num >= 4)
		snprintf(buffer, sizeof buffer, "#%02X%02X%02X%02X", r, g, b, a);
	else
		snprintf(buffer, sizeof buffer, "#%02X%02X%02X", r, g, b);

	lua_pushstring(L, buffer);
	return 1;
}

static int lib_colorPackRgb(lua_State *L)
{
	int32_t r, g, b;

	GetArgsRGBA(L, 1, &r, &g, &b, NULL);

	uint32_t packed = ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);

	lua_pushinteger(L, packed);
	return 1;
}

static int lib_colorPackRgba(lua_State *L)
{
	int32_t r, g, b, a;

	GetArgsRGBA(L, 1, &r, &g, &b, &a);

	uint32_t packed = ((r & 0xFF) << 24) | ((g & 0xFF) << 16) | ((b & 0xFF) << 8) | (a & 0xFF);

	lua_pushinteger(L, packed);
	return 1;
}

static int lib_colorUnpackRgb(lua_State *L)
{
	uint8_t rgba[4];

	uint8_t num = GetPackedRGBA(lua_tointeger(L, 1), rgba);

	for (uint8_t i = 0; i < num; i++)
	{
		lua_pushinteger(L, rgba[i]);
	}

	return num;
}

static luaL_Reg color_lib[] = {
	{"paletteToRgb", lib_colorPaletteToRgb},
	{"rgbToPalette", lib_colorRgbToPalette},
	{"hslToRgb", lib_colorHslToRgb},
	{"rgbToHsl", lib_colorRgbToHsl},
	{"hexToRgb", lib_colorHexToRgb},
	{"rgbToHex", lib_colorRgbToHex},
	{"packRgb", lib_colorPackRgb},
	{"packRgba", lib_colorPackRgba},
	{"unpackRgb", lib_colorUnpackRgb},
	{"paletteFromSkincolor", lib_paletteFromSkincolor},
	{"paletteFromSkincolorDimmed",lib_paletteFromSkincolorDimmed},
	{NULL, NULL}
};

/////////////////////////
// extracolormap userdata
/////////////////////////

enum extracolormap_e {
	extracolormap_red = 0,
	extracolormap_green,
	extracolormap_blue,
	extracolormap_alpha,
	extracolormap_color,
	extracolormap_fade_red,
	extracolormap_fade_green,
	extracolormap_fade_blue,
	extracolormap_fade_alpha,
	extracolormap_fade_color,
	extracolormap_fade_start,
	extracolormap_fade_end,
	extracolormap_colormap
};

static const char *const extracolormap_opt[] = {
	"red",
	"green",
	"blue",
	"alpha",
	"color",
	"fade_red",
	"fade_green",
	"fade_blue",
	"fade_alpha",
	"fade_color",
	"fade_start",
	"fade_end",
	"colormap",
	NULL};

#define ALPHA_SCALE_FACTOR 102 // (255 / 25) * 10
#define SCALE_ALPHA_UP(alpha) (((alpha) * ALPHA_SCALE_FACTOR) / 10)
#define SCALE_ALPHA_DOWN(alpha) (((alpha) * 10) / ALPHA_SCALE_FACTOR)

int extracolormap_fields_ref = LUA_NOREF;

static int extracolormap_get(lua_State *L)
{
	extracolormap_t *exc = *((extracolormap_t **)luaL_checkudata(L, 1, META_EXTRACOLORMAP));
	enum extracolormap_e field = Lua_optoption(L, 2, -1, extracolormap_fields_ref);

	if (!exc)
		return LUA_ErrInvalid(L, "extracolormap_t");

	switch (field)
	{
	case extracolormap_red:
		lua_pushinteger(L, R_GetRgbaR(exc->rgba));
		break;
	case extracolormap_green:
		lua_pushinteger(L, R_GetRgbaG(exc->rgba));
		break;
	case extracolormap_blue:
		lua_pushinteger(L, R_GetRgbaB(exc->rgba));
		break;
	case extracolormap_alpha:
		lua_pushinteger(L, SCALE_ALPHA_UP(R_GetRgbaA(exc->rgba)));
		break;
	case extracolormap_color:
		lua_pushinteger(L, R_GetRgbaR(exc->rgba));
		lua_pushinteger(L, R_GetRgbaG(exc->rgba));
		lua_pushinteger(L, R_GetRgbaB(exc->rgba));
		lua_pushinteger(L, SCALE_ALPHA_UP(R_GetRgbaA(exc->rgba)));
		return 4;
	case extracolormap_fade_red:
		lua_pushinteger(L, R_GetRgbaR(exc->fadergba));
		break;
	case extracolormap_fade_green:
		lua_pushinteger(L, R_GetRgbaG(exc->fadergba));
		break;
	case extracolormap_fade_blue:
		lua_pushinteger(L, R_GetRgbaB(exc->fadergba));
		break;
	case extracolormap_fade_alpha:
		lua_pushinteger(L, SCALE_ALPHA_UP(R_GetRgbaA(exc->fadergba)));
		break;
	case extracolormap_fade_color:
		lua_pushinteger(L, R_GetRgbaR(exc->fadergba));
		lua_pushinteger(L, R_GetRgbaG(exc->fadergba));
		lua_pushinteger(L, R_GetRgbaB(exc->fadergba));
		lua_pushinteger(L, SCALE_ALPHA_UP(R_GetRgbaA(exc->fadergba)));
		return 4;
	case extracolormap_fade_start:
		lua_pushinteger(L, exc->fadestart);
		break;
	case extracolormap_fade_end:
		lua_pushinteger(L, exc->fadeend);
		break;
	case extracolormap_colormap:
		LUA_PushUserdata(L, exc->colormap, META_LIGHTTABLE);
		break;
	}
	return 1;
}

static void GetExtraColormapRGBA(lua_State *L, uint8_t *rgba, int arg)
{
	rgba[3] = SCALE_ALPHA_UP(rgba[3]);

	if (lua_type(L, arg) == LUA_TSTRING)
	{
		const char *str = lua_tostring(L, arg);
		uint8_t parsed = ParseHTMLColor(str, rgba, 4);
		if (!parsed)
			luaL_error(L, "Malformed HTML color '%s'", str);
	}
	else
	{
		GetPackedRGBA(lua_tointeger(L, arg), rgba);
	}

	rgba[3] = SCALE_ALPHA_DOWN(rgba[3]);
}

static int extracolormap_set(lua_State *L)
{
	extracolormap_t *exc = *((extracolormap_t **)luaL_checkudata(L, 1, META_EXTRACOLORMAP));
	enum extracolormap_e field = Lua_optoption(L, 2, -1, extracolormap_fields_ref);

	if (!exc)
		return LUA_ErrInvalid(L, "extracolormap_t");

	uint8_t r = R_GetRgbaR(exc->rgba);
	uint8_t g = R_GetRgbaG(exc->rgba);
	uint8_t b = R_GetRgbaB(exc->rgba);
	uint8_t a = R_GetRgbaA(exc->rgba);

	uint8_t fr = R_GetRgbaR(exc->fadergba);
	uint8_t fg = R_GetRgbaG(exc->fadergba);
	uint8_t fb = R_GetRgbaB(exc->fadergba);
	uint8_t fa = R_GetRgbaA(exc->fadergba);

	uint8_t rgba[4];

	int32_t old_rgba = exc->rgba, old_fade_rgba = exc->fadergba; // It's not unsigned?
	uint8_t old_fade_start = exc->fadestart, old_fade_end = exc->fadeend;

#define val luaL_checkinteger(L, 3)

	switch(field)
	{
	case extracolormap_red:
		exc->rgba = R_PutRgbaRGBA(val, g, b, a);
		break;
	case extracolormap_green:
		exc->rgba = R_PutRgbaRGBA(r, val, b, a);
		break;
	case extracolormap_blue:
		exc->rgba = R_PutRgbaRGBA(r, g, val, a);
		break;
	case extracolormap_alpha:
		exc->rgba = R_PutRgbaRGBA(r, g, b, SCALE_ALPHA_DOWN(val));
		break;
	case extracolormap_color:
		rgba[0] = r;
		rgba[1] = g;
		rgba[2] = b;
		rgba[3] = a;
		GetExtraColormapRGBA(L, rgba, 3);
		exc->rgba = R_PutRgbaRGBA(rgba[0], rgba[1], rgba[2], rgba[3]);
		break;
	case extracolormap_fade_red:
		exc->fadergba = R_PutRgbaRGBA(val, fg, fb, fa);
		break;
	case extracolormap_fade_green:
		exc->fadergba = R_PutRgbaRGBA(fr, val, fb, fa);
		break;
	case extracolormap_fade_blue:
		exc->fadergba = R_PutRgbaRGBA(fr, fg, val, fa);
		break;
	case extracolormap_fade_alpha:
		exc->fadergba = R_PutRgbaRGBA(fr, fg, fb, SCALE_ALPHA_DOWN(val));
		break;
	case extracolormap_fade_color:
		rgba[0] = fr;
		rgba[1] = fg;
		rgba[2] = fb;
		rgba[3] = fa;
		GetExtraColormapRGBA(L, rgba, 3);
		exc->fadergba = R_PutRgbaRGBA(rgba[0], rgba[1], rgba[2], rgba[3]);
		break;
	case extracolormap_fade_start:
		if (val > 31)
			return luaL_error(L, "fade start %d out of range (0 - 31)", val);
		exc->fadestart = val;
		break;
	case extracolormap_fade_end:
		if (val > 31)
			return luaL_error(L, "fade end %d out of range (0 - 31)", val);
		exc->fadeend = val;
		break;
	case extracolormap_colormap:
		return luaL_error(L, LUA_QL("extracolormap_t") " field " LUA_QS " should not be set directly.", extracolormap_opt[field]);
	}

#undef val

	if (exc->rgba != old_rgba
		|| exc->fadergba != old_fade_rgba
		|| exc->fadestart != old_fade_start
		|| exc->fadeend != old_fade_end)
	R_GenerateLightTable(exc, true);

	return 0;
}

static int lighttable_get(lua_State *L)
{
	void **userdata;

	lighttable_t *table = *((lighttable_t **)luaL_checkudata(L, 1, META_LIGHTTABLE));
	uint32_t row = luaL_checkinteger(L, 2);
	if (row < 1 || row > 34)
		return luaL_error(L, "lighttable row %d out of range (1 - %d)", row, 34);

	userdata = lua_newuserdata(L, sizeof(void *));
	*userdata = &table[256 * (row - 1)];
	luaL_getmetatable(L, META_COLORMAP);
	lua_setmetatable(L, -2);

	return 1;
}

static int lighttable_len(lua_State *L)
{
	lua_pushinteger(L, NUM_PALETTE_ENTRIES);
	return 1;
}

int LUA_ColorLib(lua_State *L)
{
	extracolormap_fields_ref = Lua_CreateFieldTable(L, extracolormap_opt);

	luaL_newmetatable(L, META_EXTRACOLORMAP);
		lua_pushcfunction(L, extracolormap_get);
		lua_setfield(L, -2, "__index");

		lua_pushcfunction(L, extracolormap_set);
		lua_setfield(L, -2, "__newindex");
	lua_pop(L, 1);

	luaL_newmetatable(L, META_LIGHTTABLE);
		lua_pushcfunction(L, lighttable_get);
		lua_setfield(L, -2, "__index");

		lua_pushcfunction(L, lighttable_len);
		lua_setfield(L, -2, "__len");
	lua_pop(L, 1);

	luaL_register(L, "color", color_lib);

	return 0;
}
