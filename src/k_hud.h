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

extern consvar_t cv_newspeedometer;

extern consvar_t cv_showinput;

extern consvar_t cv_colorizedhud;
extern consvar_t cv_colorizeditembox;
extern consvar_t cv_darkitembox;
extern consvar_t cv_colorizedhudcolor;

extern consvar_t cv_newtabranking;

struct trackingResult_t
{
	fixed_t x, y;
	fixed_t scale;
	boolean onScreen;
	INT32 angle, pitch;
	fixed_t fov;
};

void K_RegisterKartHUDStuff(void);

void K_ObjectTracking(trackingResult_t *result, const vector3_t *point, boolean reverse);

typedef struct
{
	INT32 x;
	INT32 y;
	INT32 flags;
	boolean flipamount;
} drawinfo_t;

patch_t *K_getItemBoxPatch(boolean small, boolean dark);
patch_t *K_getItemMulPatch(boolean small);
patch_t *K_getItemAltPatch(boolean small, boolean multimode);
boolean K_ShowAltItemIcon(UINT8 type, boolean small);
void K_getItemBoxDrawinfo(drawinfo_t *out);
void K_getLapsDrawinfo(drawinfo_t *out);
void K_getRingsDrawinfo(drawinfo_t *out);
void K_getMinimapDrawinfo(drawinfo_t *out);
void K_getSlipstreamDrawinfo(drawinfo_t *out);
void K_getLivesnStatsDrawinfo(drawinfo_t *out);
void K_ReloadHUDColorCvar(void);
boolean K_UseColorHud(void);
UINT8 K_GetHudColor(void);
void K_LoadKartHUDGraphics(void);
void K_drawKartHUD(void);
void K_drawKartFreePlay(void);
void K_drawKartTimestamp(tic_t drawtime, INT32 TX, INT32 TY, INT16 emblemmap, UINT8 mode);
INT32 K_DrawNeoTabRankings(INT32 x, INT32 y, playersort_t *tab, INT32 scorelines, INT32 whiteplayer, INT32 hilicol, boolean split);
void K_DrawTabRankings(INT32 x, INT32 y, playersort_t *tab, INT32 scorelines, INT32 whiteplayer, INT32 hilicol);

#define MAXSERVERMODS 255
#define MAXSERVERMODNAME 14

struct servermods_t
{
	char modname[MAXSERVERMODNAME];
	UINT32 hash;
	const consvar_t *cvar;
	SINT8 active; // -1 is N/A (example: if using cvar activation), 0 is off, 1 is on.
	boolean valid;
};

typedef enum
{
	SCOREBOARDMOD_NOTUSED = -1,
	SCOREBOARDMOD_INACTIVE,
	SCOREBOARDMOD_ACTIVE,
} scoreboardmod_e;

extern servermods_t customservermods[MAXSERVERMODS];
extern UINT8 numcustomservermods;
void K_AddNewScoreboardMod(const char *name, const consvar_t *cvar, SINT8 active);
void K_SetScoreboardModStatus(const char *name, SINT8 active);

void K_DrawServerDescrption(INT32 x, INT32 y);

void K_DrawDriftGauge(void);
void K_DrawFlamometer(void);
void K_ResetAfterImageValues(void);
#ifdef __cplusplus
} // extern "C"
#endif

#endif
