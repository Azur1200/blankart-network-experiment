// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2024 by $HOME
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  radio_finish.h
/// \brief Functions related to the Sonic Riders-esque finish ticker.

#ifndef __RADIO_FINISH__
#define __RADIO_FINISH__

#ifdef __cplusplus
extern "C" {
#endif

#include "d_player.h"

void KartFinishLineTicker_OnChange(void);

void RR_resetRidersFinishTicker(void);
void RR_addPlayerToFinshTicker(player_t *player);
void RR_ridersFinishTick(void);
void RR_drawRidersFinishTicker(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __RADIO_FINISH__
