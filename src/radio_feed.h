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

// Basically the same as the ones in the radio_feed.hpp but in C so its usable by Lua.
typedef struct
{
	dboolean valid; // Check for Lua handler if this was used or not
	dboolean animated; // Animated?
	dboolean should_flash; // Flashing patch?
	int animated_frames; // Number of frames to animate
	int tics_per_frame; // Tics per frame (how long each frame should animate for)
	skincolornum_t flashing_colour; // The colour to flash
} LuaAnimatedConfig;

typedef struct
{
	char* patch; // Graphic name
	int width; // Patch width
	int height; // Patch height
	LuaAnimatedConfig animation_config; // Animation-related variables
	float patch_scale; // Graphic scale
	int y_offset; // Shrink, Eggmark, etc
	kartitemtype_e stolen_item; // Hyudoro Steal Patch
	skincolornum_t recolour; // The colour to recolour the item too (usually droptarget)
	dboolean rainbow; // Rainbow colormap
} LuaConfigForFeedUpdate;

void RR_PushPlayerDamageToFeed(mobj_t *source, mobj_t *target, mobj_t *inflictor);
dboolean RR_PushPlayerDeathToFeed(mobj_t *source, mobj_t *target, mobj_t *inflictor);
void RR_PushPlayerInteractionToFeed(mobj_t *source, mobj_t *target, playerattacks_t attack, kartitemtype_e stolen);
void RR_PushGlobalEventToFeed(player_t* player, globalfeedevent_t event);
void RR_PushGlobalGradeEventToFeed(player_t* player, ranktier_e rank);
void RR_PushLuaPlayerMessageToFeed(player_t *source, player_t *target, LuaConfigForFeedUpdate config);
void RR_PushLuaPlayerFlavourMessageToFeed(player_t *source, dboolean after1, const char *flavour1, player_t *target, dboolean after2, const char *flavour2, LuaConfigForFeedUpdate config);
void RR_PushLuaMessageToFeed(const char *source, int32_t source_color, const char *target, int32_t target_color, dboolean displayplayer, LuaConfigForFeedUpdate config);
void RR_PushLuaGlobalEventToFeed(player_t* player, LuaConfigForFeedUpdate config);
void RR_DrawHudFeed(void);
void RR_TickHudFeed(void);
void RR_ClearHudFeed(void);
void RR_UpdateHudFeedConfig(void);
void RR_FeedCom(void);

extern consvar_t cv_hudfeed_enabled;
extern consvar_t cv_hudfeed_show_grades;
extern consvar_t cv_hudfeed_position;

#ifdef __cplusplus
} // extern "C"
#endif

#endif
