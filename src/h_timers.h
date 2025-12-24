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
	TIMER_COUNTER = 0x1,
	TIMER_BAD     = 0x2,
} timerflags_t;

extern consvar_t cv_itemtimers;
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
	UINT8 flags, boolean unsorted);
void K_DisplayItemTimers(void);
void K_getTimersDrawinfo(drawinfo_t *out);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
