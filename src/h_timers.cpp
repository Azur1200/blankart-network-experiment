// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2025 by haya3218.
// Copyright (C) 2018-2024 by Kart Krew
// Copyright (C) 2026 by Team Blankart.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  h_timers.cpp
/// \brief C++ port of Home's item timers

#include "doomdef.h"
#include "h_timers.h"
#include "k_hud.h"
#include "r_draw.h"
#include "z_zone.h"
#include "v_video.h"
#include "k_kart.h"
#include "k_color.h"
#include "g_game.h"
#include "p_local.h"
#include "k_kart.h"
#include "g_game.h"
#include "r_main.h"
#include "d_netcmd.h"
#include "m_easing.h"
#include "k_boss.h"
#include "st_stuff.h"
#include "lua_hud.h"
#include "r_main.h"
#include "r_fps.h"
#include "s_sound.h"
#include "m_cond.h"
#include <unordered_map>
#include <vector>
#include <algorithm>

typedef enum
{
	ITM_HUD = 0,
	ITM_FOLLOW,
} itemtimermode_e;

static CV_PossibleValue_t itemtimermode_cons_t[]= {
	{ITM_HUD, "HUD"},
	{ITM_FOLLOW, "Follow"},
	{0, NULL}
};

consvar_t cv_itemtimers = CVAR_INIT ("itemtimers", "On", CV_SAVE, CV_OnOff, NULL);
consvar_t cv_itemtimers_mode = CVAR_INIT ("itemtimers_mode", "HUD", CV_SAVE, itemtimermode_cons_t, NULL);


typedef struct itimer_s
{
	const char* name;						// name of timer
	INT32 timer;							// current time
	std::vector<patch_t*> patches;	// timer graphics (small)
	INT32 anim_frames = 1;					// tic duration for each graphic
	UINT32 flags = 0;						// settings for timer
	lua_Integer timer_func_ref = LUA_NOREF; // if set, calls a function from registry to get timer value
	dboolean timer_func_error = false;       // don't print error every frame
} itimer_t;

static bool sorttimers(itimer_t a, itimer_t b)
{
	// Just sort by time please.
	return a.timer < b.timer;
}

static inline INT32 G_TicsToDeciseconds(tic_t tics)
{
	return (INT32)((tics%TICRATE) * (10.00f/TICRATE));
}

tic_t spbTimers[MAXPLAYERS] = {0};

UINT8 K_GetBestRank(void)
{
	UINT8 best = UINT8_MAX;
	INT32 i;

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].exiting || players[i].spectator)
			continue;
		best = std::min<UINT8>(best, players[i].position);
	}

	return best;
}

// SPB Timer related
void K_UpdateSPBTimer(void)
{
	if (!(cv_itemtimers.value))
	{
		// no need to attempt to account for the timers
		memset(spbTimers, 0, sizeof spbTimers);
		return;
	}
	INT32 i, bestrank = 255;
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].exiting || players[i].spectator)
			continue;
		spbTimers[i] = 0;
		bestrank = std::min<INT32>(bestrank, players[i].position);

		// check for spb
		mobj_t *mobj, *next;
		for (mobj = kitemcap; mobj; mobj = next)
		{
			next = mobj->itnext;

			// Look I don need other shit here besides the SPB
			if (mobj->type != MT_SPB)
				continue;

			// Don't bother SPBs with no player yet
			if (mobj->tracer == NULL || P_MobjWasRemoved(mobj->tracer))
				continue;
			if (mobj->tracer->player == NULL)
				continue;

			player_t *spbplayer = mobj->tracer->player;
			if (spbplayer->position > bestrank)
			{
				spbTimers[spbplayer-players] = mobj->extravalue2;
				if (players[i].position == bestrank)
					spbTimers[i] = mobj->extravalue2;
				else if (&players[i] != spbplayer)
					spbTimers[i] = 0;
			}
			else
				spbTimers[i] = 0;
		}
	}
}

static std::unordered_map<const char*, patch_t*> cachedPatches;

static patch_t *qche(const char* p)
{
	if (cachedPatches.find(p) != cachedPatches.end())
		return cachedPatches[p];
	cachedPatches[p] = (patch_t*)W_CachePatchLongName(p, PU_CACHE);
	return cachedPatches[p];
}

static std::vector<itimer_t> addTimers;
static std::vector<itimer_t> addTimers_unsorted;

void K_AddItemTimerEx(
	const char *name,
	lua_Integer timer_func_ref,
	const char **patches,
	INT32 patches_size,
	INT32 anim_duration,
	UINT32 flags, dboolean unsorted)
{
	itimer_t t; // woo yeah baby

	t.name = name;
	t.timer_func_ref = timer_func_ref;
	INT32 i;

	for (i = 0; i < patches_size; i++)
		t.patches.push_back(qche(patches[i]));

	t.anim_frames = anim_duration;
	t.flags = flags;

	// yeah
	if (unsorted)
		addTimers_unsorted.push_back(t);
	else
		addTimers.push_back(t);
}

static trackingResult_t K_getTimersPositionForTrackingPlayer(void)
{
	trackingResult_t result = {0};

	// No player object? Not bothering.
	const dboolean doesPlayerHaveMo = !((stplyr->mo == NULL || P_MobjWasRemoved(stplyr->mo)));
	if (!doesPlayerHaveMo)
		return result;

	vector3_t v = {
		R_InterpolateFixed(stplyr->mo->old_x, stplyr->mo->x) + stplyr->mo->sprxoff,
		R_InterpolateFixed(stplyr->mo->old_y, stplyr->mo->y) + stplyr->mo->spryoff,
		R_InterpolateFixed(stplyr->mo->old_z, stplyr->mo->z) + stplyr->mo->sprzoff + (stplyr->mo->height >> 1),
	};

	K_ObjectTracking(&result, &v, false);

	return result;
}

void K_getTimersDrawinfo(drawinfo_t *out)
{
	INT32 fx, fy, flags = V_HUDTRANS|V_SNAPTOBOTTOM|V_SPLITSCREEN;

	fx = 160 + cv_timers_xoffset.value;
	fy = 170 + cv_timers_yoffset.value;

	if (cv_itemtimers_mode.value == ITM_HUD)
	{
		if (r_splitscreen == 1)
		{
			flags &= ~V_SNAPTOBOTTOM;
			fx = 160;
			fy = 75;
		}
		else if (r_splitscreen > 1)
		{
			flags &= ~V_SNAPTOBOTTOM;
			fy = 75;
			fx = 80;
		}
	}
	else
	{
		trackingResult_t result = K_getTimersPositionForTrackingPlayer();

		if(result.x != 0 && result.y != 0)
		{
			INT32 base_x = 15;
			INT32 base_y = 18;
			INT32 baseUpsideDown_y = 18;

			const dboolean isupsidedown = (stplyr->mo->eflags & MFE_VERTICALFLIP);

			fx = (result.x / FRACUNIT) + base_x;
			fy = (result.y / FRACUNIT) - (isupsidedown ? baseUpsideDown_y : base_y);
		}
	}

	if (r_splitscreen > 0)
	{
		flags &= ~V_HUDTRANS;
		flags |= V_HUDTRANSHALF;
	}

	out->x = fx+stplyr->karthud[khud_itemtimer_xoff];
	out->y = fy+stplyr->karthud[khud_itemtimer_yoff];
	out->flags = flags;
}

static INT32 getLuaTimer(itimer_t *itimer)
{
	lua_pushcfunction(gL, LUA_GetErrorMessage);

	lua_Integer errorh = lua_gettop(gL);

	lua_getfield(gL, LUA_REGISTRYINDEX, LREG_TIMERS);
	lua_rawgeti(gL, -1, itimer->timer_func_ref);

	I_Assert(lua_isfunction(gL, -1));

	LUA_PushUserdata(gL, stplyr, META_PLAYER);

	if (lua_pcall(gL, 1, 1, errorh) != 0)
	{
		if (!itimer->timer_func_error)
		{
			itimer->timer_func_error = true;
			CONS_Alert(CONS_WARNING, "%s\n", lua_tostring(gL, -1));
		}

		lua_pop(gL, 3); // Pop error message, timers table and error handler
		return 0;
	}

	// TODO - print error if returned value isn't integer?
	if (lua_isnumber(gL, -1))
		itimer->timer = lua_tointeger(gL, -1);

	lua_pop(gL, 3); // Pop function result, timers table and error handler

	return itimer->timer;
}

void K_DisplayItemTimers(void)
{
	if (!cv_itemtimers.value) return;
	// nooope
	if (!LUA_HudEnabled(hud_itemtimers)) return;
	if (stplyr->mo == NULL || P_MobjWasRemoved(stplyr->mo)) return;
	// I love and hate you C++
	std::vector<itimer_t> timers =
	{
		{ // sneaker
			"shoe",
			stplyr->sneakertimer,
			{qche("K_TISHOE")},
			1,
		},
		{ // invinc
			"invincible",
			stplyr->invincibilitytimer,
			{qche("K_TIINV1"), qche("K_TIINV2"), qche("K_TIINV3"), qche("K_TIINV4"), qche("K_TIINV5"), qche("K_TIINV6")},
			3,
		},
		{ // s-monitor
			"s-monitor",
			stplyr->smonitortimer,
			{qche("K_TISMR1")/*, qche("K_TIINV2"), qche("K_TIINV3"), qche("K_TIINV4"), qche("K_TIINV5"), qche("K_TIINV6")*/},
			3,
			// Hide the timer until it's necessary
			static_cast<uint32_t>(((stplyr->smonitortimer >= SMONITORTIME)) ? TIMER_NONUMBER : 0),
		},
		{ // grow
			"grow",
			std::max<INT16>(0, stplyr->growshrinktimer),
			{qche("K_TIGROW")},
			1,
		},
		{ // rocket sneakers
			"rocketsneakers",
			stplyr->rocketsneakertimer,
			{qche("K_TIRSHE")},
			1,
		},
		{ // hyudoro
			"hyudoro",
			stplyr->hyudorotimer,
			{qche("K_TIHYUD")},
			1,
		},
		{ // drift boost
			"driftsparkboost",
			stplyr->driftboost,
			{qche("K_TISRK1"), qche("K_TISRK2")},
			2,
		},
		{ // start boost
			"startboost",
			stplyr->startboost,
			{qche("K_TISTB")},
			1,
		},
		{ // ring boost
			"ringboost",
			stplyr->ringboost,
			{qche("K_TIRING")},
			1,
		},
		{ // bubbleshield
			"bubbleshield",
			stplyr->bubbleboost,
			{qche("K_TIBUBS")},
			1,
		},
		{ // flameshield
			"flameshield",
			stplyr->flametimer,
			{qche("K_TIFLMS")},
			1,
		},
		{ // attractionshield
			"attractionshield",
			static_cast<int32_t>(stplyr->attractionattack),
			{qche("K_TIATTR")},
			1,
		},
	};

	// insert sortable timers
	timers.insert(timers.end(), addTimers.begin(), addTimers.end());

	// Sort them based on time
	std::sort(timers.begin(), timers.end(), sorttimers);

	// Spinout and shrink are always last
	timers.push_back(
		{ // spinout
			"spinout",
			std::max<UINT16>(stplyr->spinouttimer, stplyr->wipeoutslow),
			{qche("K_TISPN1"), qche("K_TISPN2"), qche("K_TISPN3"), qche("K_TISPN4")},
			3,
			TIMER_RED,
		}
	);
	timers.push_back(
		{ // shrink
			"shrink",
			std::max<INT16>(0, -stplyr->growshrinktimer),
			{qche("K_TISHRK")},
			1,
			static_cast<uint32_t>((K_IsAltShrunk(stplyr) ? 0 : TIMER_RED)),
		}
	);
	timers.push_back(
		{ // spb
			"spb",
			(INT32)spbTimers[stplyr-players],
			{qche("K_TISPB")},
			1,
			static_cast<uint32_t>((stplyr->position == K_GetBestRank()) ? TIMER_RED : 0),
		}
	);
	// Same with boost stacks
	timers.push_back(
		{ // boosts
			"boosts",
			K_StackingActive() ? stplyr->numboosts : 0,
			{qche("K_TISRK1"), qche("K_TISRK2")},
			2,
			TIMER_GREEN|TIMER_COUNTER,
		}
	);
	timers.push_back(
		{ // item cooldown
			"itemusecooldown",
			(INT32)stplyr->itemusecooldown,
			{qche("SERVLOCK")},
			1,
		}
	);

	// insert unsortable timers
	timers.insert(timers.end(), addTimers_unsorted.begin(), addTimers_unsorted.end());

	if (!modeattacking) // "HOME" style, with icons at the bottom
	{
		INT32 itemx = 0, itemy = 0;
		INT32 flags = 0;
		INT32 step = 25;
		INT32 offs = 0;

		drawinfo_t info;
		K_getTimersDrawinfo(&info);
		itemx = info.x;
		itemy = info.y;
		flags = info.flags;

		if (cv_itemtimers_mode.value == ITM_FOLLOW)
			step = 10;

		offs = -step;

		for (itimer_t& t : timers)
		{
			INT32 timer = t.timer;
			if (t.timer_func_ref != LUA_NOREF) timer = getLuaTimer(&t);
			if (stplyr->deadtimer) continue;
			if (timer > 0)
				offs += step;
		}

		if (cv_itemtimers_mode.value == ITM_HUD)
			itemx -= offs/2;

		// draw relevant timers
		for (itimer_t& t : timers)
		{
			if (t.timer <= 0) continue;
			if (stplyr->deadtimer) continue;
			INT32 timer = t.timer;
			INT32 seconds = timer / TICRATE, _centiseconds = G_TicsToCentiseconds(timer);

			const char* str = va("%d.%02d", seconds, _centiseconds);
			if (seconds > 9) // exceeds 9 seconds?
				str = va("%d.%01d", seconds, G_TicsToDeciseconds(timer));
			if (seconds > 99) // exceeds 100 seconds?
				str = va("%d", seconds);
			INT32 animsize = t.patches.size();
			INT32 patchnum = (leveltime % (t.anim_frames * animsize) / t.anim_frames);
			UINT8 *cmap = NULL, *textcmap = NULL;
			INT32 font = TINY_FONT;

			if (fastcmp(t.name, "driftsparkboost"))
			{
				if (timer > 85) // rainbow
					cmap = R_GetTranslationColormap(TC_DEFAULT, (skincolornum_t)K_RainbowColor(leveltime), GTC_CACHE);
				else if (timer > 50 && K_PurpleDriftActive()) // purple
					cmap = R_GetTranslationColormap(TC_DEFAULT, SKINCOLOR_PURPLE, GTC_CACHE);
				else if (timer > 25) // ketchup
					cmap = R_GetTranslationColormap(TC_DEFAULT, SKINCOLOR_KETCHUP, GTC_CACHE);
				else // blu
					cmap = R_GetTranslationColormap(TC_DEFAULT, SKINCOLOR_SAPPHIRE, GTC_CACHE);
				textcmap = cmap;
			}

			if (t.flags & TIMER_COUNTER) // don't show up as time
			{
				str = va("%d", timer);
				font = HU_FONT;
			}
			INT32 width = V_StringScaledWidth(FRACUNIT, FRACUNIT, FRACUNIT, flags|V_MONOSPACE, font, str) >> FRACBITS;

#define COLORTIMER(color) \
	if (t.flags & TIMER_##color) \
		flags |= V_##color##MAP\

			// Color timers!
			COLORTIMER(RED);
			COLORTIMER(PURPLE);
			COLORTIMER(YELLOW);
			COLORTIMER(GREEN);
			COLORTIMER(BLUE);
			COLORTIMER(RED);
			COLORTIMER(GRAY);
			COLORTIMER(ORANGE);
			COLORTIMER(SKY);
			COLORTIMER(LAVENDER);
			COLORTIMER(GOLD);
			COLORTIMER(AQUA);
			COLORTIMER(MAGENTA);
			COLORTIMER(PINK);
			COLORTIMER(BROWN);
			COLORTIMER(TAN);

			patch_t *item = t.patches[patchnum];


			if (cv_itemtimers_mode.value == ITM_HUD)
			{
				V_DrawFixedPatch((itemx - (item->width/2))<<FRACBITS, (itemy - 2)<<FRACBITS, FRACUNIT, flags, t.patches[patchnum], cmap);

				if (!(t.flags & TIMER_NONUMBER))
				{
					V_DrawStringScaledEx(
						(itemx - (width/2) - item->leftoffset) << FRACBITS,
						(itemy + 12) << FRACBITS,
						FRACUNIT,
						FRACUNIT,
						FRACUNIT,
						FRACUNIT,
						flags|V_MONOSPACE,
						textcmap,
						font,
						str
					);
				}
			}
			else
			{
				V_DrawFixedPatch((itemx - (item->width/2))<<FRACBITS, (itemy)<<FRACBITS, FRACUNIT/2, flags, t.patches[patchnum], cmap);

				if (!(t.flags & TIMER_NONUMBER))
				{
					V_DrawStringScaledEx(
						(itemx) << FRACBITS,
						(t.flags & TIMER_COUNTER ? itemy + 2 : itemy) << FRACBITS,
						FRACUNIT,
						FRACUNIT,
						FRACUNIT,
						FRACUNIT,
						flags|V_MONOSPACE,
						textcmap,
						font,
						str
					);
				}

			}


			if (cv_itemtimers_mode.value == ITM_HUD)
				itemx += step;
			else
				itemy += step;
		}
	}
}
