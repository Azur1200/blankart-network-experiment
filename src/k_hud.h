// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2018-2020 by Kart Krew
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  k_hud.h
/// \brief HUD drawing functions exclusive to Kart

#ifndef __K_HUD__
#define __K_HUD__

#include "doomtype.h"
#include "doomstat.h"
#include "hu_stuff.h"
#include "k_items.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RINGANIM_NUMFRAMES 10
#define RINGANIM_DELAYMAX 5

// Hud offset cvars
#define IMPL_HUD_OFFSET_X(name)\
extern consvar_t cv_##name##_xoffset;

#define IMPL_HUD_OFFSET_Y(name)\
extern consvar_t cv_##name##_yoffset;

#define IMPL_HUD_OFFSET(name)\
IMPL_HUD_OFFSET_X(name)\
IMPL_HUD_OFFSET_Y(name)

IMPL_HUD_OFFSET(item); // Item box
IMPL_HUD_OFFSET(time); // Time
IMPL_HUD_OFFSET(laps); // Number of laps
IMPL_HUD_OFFSET(rings); // Number of rings
IMPL_HUD_OFFSET(dnft); // Countdown (did not finish timer)
IMPL_HUD_OFFSET(speed); // Speedometer
IMPL_HUD_OFFSET(acce); // Accessibility
IMPL_HUD_OFFSET(timers); // Drafting
IMPL_HUD_OFFSET(draft); // Drafting
IMPL_HUD_OFFSET(posi); // Position in race
IMPL_HUD_OFFSET(face); // Mini rankings
IMPL_HUD_OFFSET(stcd); // Starting countdown
IMPL_HUD_OFFSET_Y(chek); // Check gfx
IMPL_HUD_OFFSET(mini); // Minimap
IMPL_HUD_OFFSET(want); // Wanted
//IMPL_HUD_OFFSET(stat); // Stats

#undef IMPL_HUD_OFFSET
#undef IMPL_HUD_OFFSET_X
#undef IMPL_HUD_OFFSET_Y

extern consvar_t cv_seenames;
extern consvar_t cv_seenamerestat;
extern consvar_t cv_seeownname;

extern consvar_t cv_newspeedometer;

extern consvar_t cv_showinput;

extern consvar_t cv_colorizedhud;
extern consvar_t cv_colorizeditembox;
extern consvar_t cv_darkitembox;
extern consvar_t cv_colorizedhudcolor;

extern consvar_t cv_newtabranking;

extern consvar_t cv_kartdebughudtracker;

extern consvar_t cv_highresportrait;

extern patch_t *kp_facenum[MAXPLAYERS+1];
extern patch_t *kp_itemboxminimap;
extern patch_t *kp_minimapdot;

struct trackingResult_t
{
	fixed_t x, y;
	fixed_t scale;
	dboolean onScreen;
	int32_t angle, pitch;
	fixed_t fov;
};

void K_RegisterKartHUDStuff(void);

void K_ObjectTracking(trackingResult_t *result, const vector3_t *point, dboolean reverse);

typedef struct
{
	int32_t x;
	int32_t y;
	int32_t flags;
	dboolean flipamount;
	fixed_t hudScale;
	float hudScaleFloat;
} drawinfo_t;

#define ROULETTE_SPACING (34)
#define ROULETTE_SPACING_FIXED (ROULETTE_SPACING << FRACBITS)

typedef enum
{
	RINFO_DRAWONPLAYER = (1<<0),
	RINFO_USECROP = (1<<1),
	/* free: 2 through 31 */
} rinfoflags_t;

typedef struct
{
	fixed_t offset;
	fixed_t spacing;
	fixed_t intSpacing;
	vector2_t crop;
	rinfoflags_t flags;
} rouletteinfo_t;

patch_t *K_getItemBoxPatch(dboolean small, dboolean dark);
patch_t *K_getItemMulPatch(dboolean small);
patch_t *K_getItemAltPatch(dboolean small, dboolean multimode);
dboolean K_ShowAltItemIcon(kartitemtype_e type, dboolean small);
void K_getItemBoxDrawinfo(drawinfo_t *out, rouletteinfo_t *rinfo);
void K_getLapsDrawinfo(drawinfo_t *out);
void K_getRingsDrawinfo(drawinfo_t *out);
void K_getMinimapDrawinfo(drawinfo_t *out);
void K_getSlipstreamDrawinfo(drawinfo_t *out);
void K_getLivesnStatsDrawinfo(drawinfo_t *out);
void K_ReloadHUDColorCvar(void);
dboolean K_UseColorHud(void);
skincolornum_t K_GetHudColor(void);
void K_LoadKartHUDGraphics(void);
void K_drawKartHUD(void);
void K_drawKartFreePlay(void);
void K_drawKartTimestamp(tic_t drawtime, int32_t TX, int32_t TY, mapnum_t emblemmap, uint8_t mode);
int32_t K_DrawNeoTabRankings(int32_t x, int32_t y, playersort_t *tab, int32_t scorelines, int32_t whiteplayer, int32_t hilicol, dboolean split);
void K_DrawTabRankings(int32_t x, int32_t y, playersort_t *tab, int32_t scorelines, int32_t whiteplayer, int32_t hilicol);

#define MAXSERVERMODS 255
#define MAXSERVERMODNAME 14

struct servermods_t
{
	char modname[MAXSERVERMODNAME];
	uint32_t hash;
	const consvar_t *cvar;
	int8_t active; // -1 is N/A (example: if using cvar activation), 0 is off, 1 is on.
	dboolean valid;
};

typedef enum
{
	SCOREBOARDMOD_NOTUSED = -1,
	SCOREBOARDMOD_INACTIVE,
	SCOREBOARDMOD_ACTIVE,
} scoreboardmod_e;

extern servermods_t customservermods[MAXSERVERMODS];
extern uint8_t numcustomservermods;
void K_AddNewScoreboardMod(const char *name, const consvar_t *cvar, int8_t active);
void K_SetScoreboardModStatus(const char *name, int8_t active);

void K_DrawServerDescrption(int32_t x, int32_t y);

void K_DrawDriftGauge(void);
void K_DrawFlamometer(void);
void K_ResetAfterImageValues(void);

typedef enum
{
	PLAYERTAG_NONE,
	PLAYERTAG_LOCAL,
	PLAYERTAG_CPU,
	PLAYERTAG_RIVAL,
	PLAYERTAG_NAME,
}
playertagtype_t;

playertagtype_t K_WhichPlayerTag(player_t *p);
void K_DrawPlayerTag(fixed_t x, fixed_t y, player_t *p, playertagtype_t type, dboolean foreground);
void K_drawTargetHUD(const vector3_t *origin, player_t *player);

extern patch_t *kp_itemtarget_arrow[2][2];
extern patch_t *kp_itemtarget_icon[2];
extern patch_t *kp_itemtarget_far[2][2];
extern patch_t *kp_itemtarget_far_text[2];
extern patch_t *kp_itemtarget_near[2][8];

dboolean K_UseHighResPortraits(void);

extern patch_t *kp_splitlapflag;
extern consvar_t cv_show_riders_finish_ticker;

#ifdef __cplusplus
} // extern "C"
#endif

#endif
