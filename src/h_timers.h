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

extern consvar_t cv_itemtimers;
extern tic_t spbTimers[MAXPLAYERS];
UINT8 K_GetBestRank(void);
void K_UpdateSPBTimer(void);
// Adds an item timer.
void K_AddItemTimerEx(
	const char *name,
	INT32 *timer,
	char **patches, char **patches_small,
	INT32 patches_size, INT32 patches_small_size,
	INT32 anim_duration,
	INT32 xoffs, INT32 yoffs,
	INT32 xoffs_small, INT32 yoffs_small,
	boolean counter, boolean unsorted);
void K_DisplayItemTimers(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
