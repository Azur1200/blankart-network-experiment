// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2025 by haya3218.
// Copyright (C) 2018-2024 by Kart Krew
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  h_timers.cpp
/// \brief C++ port of Home's item timers

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
#include "fastcmp.h"
#include "m_cond.h"
#include <unordered_map>
#include <vector>
#include <algorithm>

consvar_t cv_itemtimers = CVAR_INIT ("itemtimers", "On", CV_SAVE, CV_OnOff, NULL);

typedef struct itimer_s 
{
	const char* name;						// name of timer
	INT32 timer;							// current time
	//std::vector<patch_t*> patches;			// timer graphics (big/freeplay)
	std::vector<patch_t*> patches_small;	// timer graphics (small)
	INT32 anim_frames;						// tic duration for each graphic
	//INT32 xoffs = 0, yoffs = 0;				// offsets for freeplay mode
	INT32 xoffs_small = 0, yoffs_small = 0;	// offsets for normal mode
	boolean counter;						// not a timer, show a counter instead
	boolean badtimer;						// timer is colored red
	INT32 *timer_p;							// if defined, uses a pointer for the timer instead
} itimer_t;

static bool sorttimers(itimer_t a, itimer_t b)
{
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
	INT32 *timer, 
	/*char **patches,*/ char **patches_small,
	/*INT32 patches_size,*/ INT32 patches_small_size,
	INT32 anim_duration,
	INT32 xoffs, INT32 yoffs, 
	INT32 xoffs_small, INT32 yoffs_small, 
	boolean counter, boolean unsorted)
{
	itimer_t t; // woo yeah baby

	t.name = name;
	t.timer_p = timer;
	INT32 i;

	//for (i = 0; i < patches_size; i++)
		//t.patches.push_back(qche(patches[i]));
	for (i = 0; i < patches_small_size; i++)
		t.patches_small.push_back(qche(patches_small[i]));

	t.anim_frames = anim_duration;
	//t.xoffs = xoffs; t.yoffs = yoffs;
	t.xoffs_small = xoffs_small; t.yoffs_small = yoffs_small;
	t.counter = counter;

	// yeah
	if (unsorted)
		addTimers_unsorted.push_back(t);
	else
		addTimers.push_back(t);
}

void K_getTimersDrawinfo(drawinfo_t *out)
{
	INT32 fx, fy, flags = V_HUDTRANS|V_SNAPTOBOTTOM|V_SPLITSCREEN;

	fx = 160 + cv_timers_xoffset.value;
	fy = 170 + cv_timers_yoffset.value;

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

	if (r_splitscreen > 0)
	{
		flags &= ~V_HUDTRANS;
		flags |= V_30TRANS;
	}

	out->x = fx;
	out->y = fy;
	out->flags = flags;
}

void K_DisplayItemTimers(void)
{
	if (!cv_itemtimers.value) return;
	// nooope
	if (stplyr->mo == NULL || P_MobjWasRemoved(stplyr->mo)) return;
	// I love and hate you C++
	std::vector<itimer_t> timers = 
	{
		{ // sneaker
			"shoe",
			stplyr->sneakertimer,
			//{qche("K_ISSHOE")},
			{qche("K_TISHOE")},
			1, /*-15, -15,*/ -4, -2,
		},
		{ // invinc
			"invincible",
			stplyr->invincibilitytimer,
			//{qche("K_ISINV1"), qche("K_ISINV2"), qche("K_ISINV3"), qche("K_ISINV4"), qche("K_ISINV5"), qche("K_ISINV6")},
			{qche("K_TIINV1"), qche("K_TIINV2"), qche("K_TIINV3"), qche("K_TIINV4"), qche("K_TIINV5"), qche("K_TIINV6")},
			3, /*-15, -15,*/ -4, -2,
		},
		{ // grow
			"grow",
			std::max<INT16>(0, stplyr->growshrinktimer),
			//{qche("K_ISGROW")},
			{qche("K_TIGROW")},
			1, /*-15, -15,*/ -4, -2,
		},
		{ // rocket sneakers
			"rocketsneakers",
			stplyr->rocketsneakertimer,
			//{qche("K_ISRSHE")},
			{qche("K_TIRSHE")},
			1, /*-15, -15,*/ -4, -2,
		},
		{ // hyudoro
			"hyudoro",
			stplyr->hyudorotimer,
			//{qche("K_ISHYUD")},
			{qche("K_TIHYUD")},
			1, /*-15, -15,*/ -4, -2,
		},
		{ // drift boost
			"driftsparkboost",
			stplyr->driftboost,
			//{qche("K_DRSP1"), qche("K_DRSP2")},
			{qche("K_TISRK1"), qche("K_TISRK2")},
			2, /*-15, -15,*/ -4, -2,
		},
		{ // start boost
			"startboost",
			stplyr->startboost,
			//{qche("K_ISSTB")},
			{qche("K_TISTB")},
			1, /*-15, -15,*/ -4, -2,
		},
		{ // ring boost
			"ringboost",
			stplyr->ringboost,
			//{qche("K_ISRING")},
			{qche("K_TIRING")},
			1, /*-15, -15,*/ -4, -2,
		},
		{ // flameshield
			"flameshield",
			stplyr->flametimer,
			//{qche("K_ISFLMS")},
			{qche("K_TIFLMS")},
			1, /*-15, -15,*/ -4, -2,
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
			//{qche("K_DIZZ1"), qche("K_DIZZ2"), qche("K_DIZZ3"), qche("K_DIZZ4")},
			{qche("K_TISPN1"), qche("K_TISPN2"), qche("K_TISPN3"), qche("K_TISPN4")},
			3, /*-15, -15,*/ -4, -2,
			false,
			true,
		}
	);
	timers.push_back(
		{ // shrink
			"shrink",
			std::max<INT16>(0, -stplyr->growshrinktimer),
			//{qche("K_ISSHRK")},
			{qche("K_TISHRK")},
			1,
			//-15,
			//-15,
			-4,
			-2,
			false,
			true,
		}
	);
	timers.push_back(
		{ // spb
			"spb",
			(INT32)spbTimers[stplyr-players],
			//{qche("K_ISSPB")},
			{qche("K_TISPB")},
			1,
			//-15,
			//-15,
			-4,
			-2,
			false,
			(stplyr->position == K_GetBestRank())
		}
	);
	// Same with boost stacks
	timers.push_back(
		{ // boosts
			"boosts",
			K_StackingActive() ? stplyr->numboosts : 0,
			//{qche("K_DRSP1"), qche("K_DRSP2")},
			{qche("K_TISRK1"), qche("K_TISRK2")},
			2, /*-15, -15,*/ -3, -2,
			true,
		}
	);

	/*timers.push_back(
		{ // dead
			"spr_realisticexplosion",
			stplyr->deadtimer ? std::max<INT32>(0, TICRATE - stplyr->deadtimer) : 0,
			//{qche("IT_DED1"), qche("IT_DED2"), qche("IT_DED3"), qche("IT_DED4"), qche("IT_DED5"), qche("IT_DED6"), qche("IT_DED7"), qche("IT_DED8")},
			{qche("IT_DEAD1"), qche("IT_DEAD2"), qche("IT_DEAD3"), qche("IT_DEAD4"), qche("IT_DEAD5"), qche("IT_DEAD6"), qche("IT_DEAD7"), qche("IT_DEAD8")},
			1,
			//0,
			//-4,
			0,
			0,
			false,
			true,
		}
	);*/

	// insert unsortable timers
	timers.insert(timers.end(), addTimers_unsorted.begin(), addTimers_unsorted.end());

	if (!modeattacking) // "HOME" style, with icons at the bottom
	{
		INT32 itemx = 0, itemy = 0;
		INT32 flags = 0;
		INT32 stepx = 25;
		INT32 viewnum = R_GetViewNumber();

		drawinfo_t info;
		K_getTimersDrawinfo(&info);
		itemx = info.x;
		itemy = info.y;
		flags = info.flags;

		// Move it up to account for viewpoint text
		if (cv_showviewpointtext.value && !demo.title && !P_IsLocalPlayer(stplyr) && !camera[viewnum].freecam && !r_splitscreen)
			itemy -= 12;

		// flags |= V_HUDTRANS;

		INT32 offs = -stepx;
		for (itimer_t& t : timers)
		{
			INT32 timer = t.timer;
			if (t.timer_p) timer = *t.timer_p;
			if (stplyr->deadtimer && !fastcmp(t.name, "spr_realisticexplosion")) continue;
			if (timer > 0)
				offs += stepx;
		}

		itemx -= offs/2;
		// draw relevant timers
		for (itimer_t& t : timers)
		{
			if (t.timer <= 0) continue;
			if (stplyr->deadtimer && !fastcmp(t.name, "spr_realisticexplosion")) continue;
			INT32 timer = t.timer;
			if (t.timer_p) timer = *t.timer_p;
			INT32 seconds = timer / TICRATE, _centiseconds = G_TicsToCentiseconds(timer);

			const char* str = va("%d.%02d", seconds, _centiseconds);
			if (seconds > 9) // exceeds 9 seconds?
				str = va("%d.%01d", seconds, G_TicsToDeciseconds(timer));
			if (seconds > 99) // exceeds 100 seconds?
				str = va("%d", seconds);
			INT32 animsize = t.patches_small.size();
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

			if (fastcmp(t.name, "boosts"))
			{
				flags |= V_GREENMAP;
			}

			if (t.counter) // don't show up as time
			{
				str = va("%d", timer);
				font = HU_FONT;
			}
			INT32 width = V_StringScaledWidth(FRACUNIT, FRACUNIT, FRACUNIT, flags|V_MONOSPACE, font, str) >> FRACBITS;

			// very bad!
			if (t.badtimer)
			{
				flags |= V_REDMAP;
				//textcmap = R_GetTranslationColormap(TC_RAINBOW, SKINCOLOR_CRIMSON, GTC_CACHE);
			}

			patch_t *item = t.patches_small[patchnum];
			V_DrawFixedPatch((itemx - (item->width/2))<<FRACBITS, (itemy + t.yoffs_small)<<FRACBITS, FRACUNIT, flags, t.patches_small[patchnum], cmap);
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

			itemx += stepx;
		}
	}
	/*else // FREEPLAY - Move these to the left side, where the rankings usually are
	{
		if (r_splitscreen) // How?
			return;

		INT32 fx = 9, fy = 92, step = 20;
		INT32 flags = V_SNAPTOLEFT|V_SLIDEIN;

		// center it
		INT32 offs = -step;
		for (itimer_t& t : timers)
		{
			INT32 timer = t.timer;
			if (t.timer_p) timer = *t.timer_p;
			if (stplyr->deadtimer && !fastcmp(t.name, "spr_realisticexplosion")) continue;
			if (timer > 0)
				offs += step;
		}
		fy -= offs/2;

		// draw relevant timers
		for (itimer_t& t : timers)
		{
			if (t.timer <= 0) continue;
			if (stplyr->deadtimer && !fastcmp(t.name, "spr_realisticexplosion")) continue;
			INT32 timer = t.timer;
			if (t.timer_p) timer = *t.timer_p;
			INT32 seconds = timer / TICRATE, _centiseconds = G_TicsToCentiseconds(timer);
			INT32 patchnum = (leveltime % (t.anim_frames * t.patches.size()) / t.anim_frames);
			UINT8 *cmap = NULL, *textcmap = NULL;

			const char* str = va("%d.%02d", seconds, _centiseconds);
			if (seconds > 9) // exceeds 9 seconds?
				str = va("%d.%01d", seconds, G_TicsToDeciseconds(timer));
			if (seconds > 99) // exceeds 100 seconds?
				str = va("%d", seconds);
			boolean bost = false;
			if (fastcmp(t.name, "boosts")) // don't show up as time
			{
				str = va("%d", timer);
				bost = true;
			}
			INT32 width = V_StringScaledWidth(FRACUNIT, FRACUNIT, FRACUNIT, flags|V_MONOSPACE, OPPRNK_FONT, str) >> FRACBITS;

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

			// very bad!
			if (t.badtimer)
				textcmap = R_GetTranslationColormap(TC_RAINBOW, SKINCOLOR_CRIMSON, GTC_CACHE);

			patch_t *item = t.patches[patchnum];
			V_DrawFixedPatch((fx+t.xoffs)<<FRACBITS, (fy+t.yoffs)<<FRACBITS, FRACUNIT, flags, item, cmap);

			V_DrawStringScaledEx(
				(fx + (item->width/2) - (width/2) - item->leftoffset + t.xoffs) << FRACBITS,
				(fy+5)  << FRACBITS,
				FRACUNIT,
				FRACUNIT,
				FRACUNIT,
				FRACUNIT,
				flags|V_MONOSPACE,
				textcmap,
				TINY_FONT,
				str
			);


			fy += step;
		}
	}
	*/
}
