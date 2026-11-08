// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2014-2016 by John "JTE" Muniz.
// Copyright (C) 2014-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  lua_hud.h
/// \brief HUD enable/disable flags for Lua scripting

#ifndef __LUA_HUD_H__
#define __LUA_HUD_H__

#include "lua_hudlib_drawlist.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HUDFIELDS(X) \
	X(stagetitle) \
	X(textspectator) \
	X(crosshair) \
	X(scores) \
	X(time) \
	X(gametypeinfo) /* Bumpers / Karma / Laps depending on gametype */ \
	X(minimap) \
	X(item) \
	X(position) \
	X(nametags) \
	X(check)	/* "CHECK" f-zero indicator */ \
	X(minirankings) /* Gametype rankings to the left */ \
	X(battlerankingsbumpers) /* bumper drawer for battle. Useful if you want to make a custom battle gamemode without bumpers being involved. */ \
	X(battlefullscreen) /* battlefullscreen func (WAIT, ATTACK OR PROTECT ...) */ \
	X(battlecomebacktimer) /* come back timer in battlefullscreen */ \
	X(wanted) \
	X(speedometer) \
	X(rings) \
	X(lives) \
	X(freeplay) \
	X(rankings) \
	X(lapsplits) \
	X(itemtimers) \
	X(intermissiontally) \
	X(intermissionmessages)

enum hud {
#define ENUMITEM(name) hud_##name,
	HUDFIELDS(ENUMITEM)
	hud_MAX
};
#undef ENUMITEM

extern dboolean hud_running;

extern dboolean hud_interpolate; // interpolation enabled
extern uint8_t hud_interptag; // current tag
extern uint32_t hud_interpcounter; // number of HUD hooks ran
extern dboolean hud_interpstring; // string mode
extern dboolean hud_interplatch; // string mode was toggled

dboolean LUA_HudEnabled(enum hud option);

void LUA_SetHudHook(int hook, huddrawlist_h list);

void LUA_ClearClipRect(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __LUA_HUD_H__
