// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2025 by $HOME
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file radio_feed.hpp

#ifndef __RADIO_CFEED__
#define __RADIO_CFEED__

#include "p_mobj.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * HUD feed
 */
typedef enum
{
    ATTACK_NONE = -1,
    ATTACK_FLAMEDASH,
    ATTACK_INVINCIBILITY,
    ATTACK_HYUDORO,
    ATTACK_THUNDER_SHIELD,
    ATTACK_GROW,
    ATTACK_SHRINK,
    ATTACK_PITFALL,
    ATTACK_ATTRACTION
} playerattacks_t;

typedef enum
{
    EVENT_SPB = 0,
    EVENT_SHRINK,
    EVENT_GRADE
} globalfeedevent_t;

void RR_PushPlayerDamageToFeed(mobj_t *source, mobj_t *target, mobj_t *inflictor);
void RR_PushPlayerDeathToFeed(mobj_t *source, mobj_t *target, mobj_t *inflictor);
void RR_PushPlayerInteractionToFeed(mobj_t *source, mobj_t *target, playerattacks_t attack, kartitemtype_e stolen);
void RR_PushGlobalEventToFeed(player_t* player, globalfeedevent_t event);
void RR_PushGlobalGradeEventToFeed(player_t* player, ranktier_e rank);
void RR_DrawHudFeed(void);
void RR_TickHudFeed(void);
void RR_ClearHudFeed(void);
void RR_UpdateHudFeedConfig(void);
void RR_FeedCom(void);

extern consvar_t cv_hudfeed_enabled;
extern consvar_t cv_hudfeed_show_faults;
extern consvar_t cv_hudfeed_show_grades;
extern consvar_t cv_hudfeed_show_snipes;
extern consvar_t cv_hudfeed_position;

#ifdef __cplusplus
} // extern "C"
#endif

#endif
