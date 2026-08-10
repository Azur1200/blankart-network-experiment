// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2024 by haya3218.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  h_main.h
/// \brief HEP3 timers library.

#ifndef __H_TIMERS__
#define __H_TIMERS__

#ifdef __cplusplus
extern "C" {
#endif

#include "command.h"
#include "d_player.h"
#include "k_hud.h"
#include "lua_script.h"
#include "lua_libs.h"

typedef enum timerflags_e
{
	TIMER_COUNTER	= 1,
	TIMER_NONUMBER	= 1<<1,
	TIMER_PURPLE	= 1<<2,
	TIMER_YELLOW	= 1<<3,
	TIMER_GREEN		= 1<<4,
	TIMER_BLUE		= 1<<5,
	TIMER_RED		= 1<<6,
	TIMER_GRAY		= 1<<7,
	TIMER_ORANGE	= 1<<8,
	TIMER_SKY		= 1<<9,
	TIMER_LAVENDER	= 1<<10,
	TIMER_GOLD		= 1<<11,
	TIMER_AQUA		= 1<<12,
	TIMER_MAGENTA	= 1<<13,
	TIMER_PINK		= 1<<14,
	TIMER_BROWN		= 1<<15,
	TIMER_TAN		= 1<<16,
} timerflags_t;

extern consvar_t cv_itemtimers;
extern consvar_t cv_itemtimers_mode;
extern tic_t spbTimers[MAXPLAYERS];
UINT8 K_GetBestRank(void);
void K_UpdateSPBTimer(void);
// Adds an item timer.
void K_AddItemTimerEx(
	const char *name,
	lua_Integer timer_func_ref,
	const char **patches,
	INT32 patches_size,
	INT32 anim_duration,
	UINT32 flags, dboolean unsorted);
void K_DisplayItemTimers(void);
void K_getTimersDrawinfo(drawinfo_t *out);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
