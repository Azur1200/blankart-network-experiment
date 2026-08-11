// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  r_main.h
/// \brief Rendering variables, consvars, defines

#ifndef __R_MAIN__
#define __R_MAIN__

#include "d_player.h"
#include "r_data.h"
#include "r_textures.h"

#include "m_perfstats.h"

#ifdef __cplusplus
extern "C" {
#endif

//
// POV related.
//
extern fixed_t viewcos, viewsin;
extern INT32 viewheight;
extern INT32 centerx, centery;

extern fixed_t centerxfrac;
extern fixed_t centeryfrac;
extern fixed_t projection[MAXSPLITSCREENPLAYERS];
extern fixed_t projectiony[MAXSPLITSCREENPLAYERS];
extern fixed_t fovtan[MAXSPLITSCREENPLAYERS];

extern size_t validcount, linecount, loopcount, framecount;

// Evaluated delta tics for this frame (how many tics since the last frame)
extern fixed_t renderdeltatics;
// The current render is a new logical tic
extern dboolean renderisnewtic;

//
// Lighting LUT.
// Used for z-depth cuing per column/row,
//  and other lighting effects (sector ambient, flash).
//

// Lighting constants.
// Now with 32 levels.
// LIGHTLEVELS is now defined in r_state.h
#define LIGHTSEGSHIFT 3

#define MAXLIGHTSCALE 48
#define LIGHTSCALESHIFT 12
#define MAXLIGHTZ 128
#define LIGHTZSHIFT 20

#define LIGHTRESOLUTIONFIX (640*fovtan[viewssnum]/vid.width)

extern lighttable_t *scalelight[LIGHTLEVELS][MAXLIGHTSCALE];
extern lighttable_t *scalelightfixed[MAXLIGHTSCALE];
extern lighttable_t *zlight[LIGHTLEVELS][MAXLIGHTZ];

// Number of diminishing brightness levels.
// There a 0-31, i.e. 32 LUT in the COLORMAP lump.
#define NUMCOLORMAPS 32

// Utility functions.
#define R_PointOnSide(x,y,node) (mapnamespace != MNS_SRB2KART) ? R_PointOnSideUDMF(x,y,node) : R_PointOnSideKart(x,y,node) // maybe a function pointer would be more appropiate?
INT32 R_PointOnSideUDMF(fixed_t x, fixed_t y, const node_t *node);
INT32 R_PointOnSideKart(fixed_t x, fixed_t y, const node_t *node);

// This is not as accurate
// SHOULD NOT BE USED FOR ANYTHING GAMEPLAY RELATED!!
FUNCINLINE static ATTRINLINE INT32 R_PointOnSideFast(fixed_t x, fixed_t y, const node_t *node)
{
	// use cross product to determine side quickly
	return ((INT64)y - node->y) * node->dx - ((INT64)x - node->x) * node->dy > 0;
}

INT32 R_PointOnSegSide(fixed_t x, fixed_t y, const seg_t *line);
#define R_PointToAngle(x, y) R_PointToAngle2(viewx, viewy, x, y)
#define R_PointToAngleFloat(x, y) (R_PointToAngleFloat2(viewx, viewy, x, y))
angle_t R_PointToAnglePlayer(player_t *player, fixed_t x, fixed_t y);
angle_t R_PointToAngle64(INT64 x, INT64 y);
angle_t R_PointToAngle2(fixed_t px2, fixed_t py2, fixed_t px1, fixed_t py1);
angle_t R_PointToAngleFloat2(fixed_t x1, fixed_t y1, fixed_t x2, fixed_t y2);
angle_t R_PointToAngle2Precise(fixed_t pviewx, fixed_t pviewy, fixed_t x, fixed_t y);
fixed_t R_PointToDist2Precise(fixed_t px2, fixed_t py2, fixed_t px1, fixed_t py1);
#define R_PointToDist(x, y) R_PointToDist2(viewx, viewy, x, y)
#define R_PointToDist2(px2, py2, px1, py1) FixedHypot((px1) - (px2), (py1) - (py2))

fixed_t R_ScaleFromGlobalAngle(angle_t visangle);
subsector_t *R_PointInSubsector(fixed_t x, fixed_t y);
subsector_t *R_PointInSubsectorOrNull(fixed_t x, fixed_t y);

// uses R_PointOnSideFast
// SHOULD NOT BE USED FOR ANYTHING GAMEPLAY RELATED!!
FUNCINLINE static ATTRINLINE subsector_t *R_PointInSubsectorFast(fixed_t x, fixed_t y)
{
	size_t nodenum = numnodes-1;

	while (!(nodenum & NF_SUBSECTOR))
		nodenum = nodes[nodenum].children[R_PointOnSideFast(x, y, nodes+nodenum)];

	return &subsectors[nodenum & ~NF_SUBSECTOR];
}

dboolean R_DoCulling(line_t *cullheight, line_t *viewcullheight, fixed_t vz, fixed_t bottomh, fixed_t toph);

void R_GetRenderBlockMapDimensions(fixed_t drawdist, INT32 *xl, INT32 *xh, INT32 *yl, INT32 *yh);

// Render stats

extern ps_metric_t ps_prevframetime;// time when previous frame was rendered
extern ps_metric_t ps_rendercalltime;
extern ps_metric_t ps_otherrendertime;
extern ps_metric_t ps_uitime;
extern ps_metric_t ps_swaptime;

extern ps_metric_t ps_skyboxtime;
extern ps_metric_t ps_bsptime;

extern ps_metric_t ps_sw_spritecliptime;
extern ps_metric_t ps_sw_portaltime;
extern ps_metric_t ps_sw_planetime;
extern ps_metric_t ps_sw_maskedtime;

extern ps_metric_t ps_numbspcalls;
extern ps_metric_t ps_numsprites;
extern ps_metric_t ps_numdrawnodes;
extern ps_metric_t ps_numpolyobjects;

struct RenderStats
{
	size_t visplanes;
	size_t drawsegs;
	size_t skybox_portals;
};

extern struct RenderStats g_renderstats;

//
// REFRESH - the actual rendering functions.
//

extern consvar_t cv_precachetextures;
extern consvar_t cv_showhud, cv_translucenthud;
extern consvar_t cv_homremoval;
extern consvar_t cv_chasecam[MAXSPLITSCREENPLAYERS];
extern consvar_t cv_flipcam[MAXSPLITSCREENPLAYERS];

extern consvar_t cv_shadow;
extern consvar_t cv_ffloorclip;
extern consvar_t cv_drawdist, cv_drawdist_precip;
extern consvar_t cv_playerfade;
extern consvar_t cv_fov[MAXSPLITSCREENPLAYERS];
extern consvar_t cv_skybox;
extern consvar_t cv_debugfinishline;
extern consvar_t cv_secbright;

extern consvar_t cv_sloperoll;
extern consvar_t cv_sliptidetilt;
extern consvar_t cv_nulldriftefx, cv_nulldrifttilt;

extern consvar_t cv_fakerollangle;
extern consvar_t cv_affineprescale, cv_affinemosaic;

// debugging

typedef enum {
	SW_HI_PLANES,
	SW_HI_FOFPLANES,
	SW_HI_FOFSIDES,
	SW_HI_MIDTEXTURES,
	SW_HI_WALLS,
	SW_HI_THINGS,
	SW_HI_SKY,

	NUM_SW_HI
} debugrender_highlight_t;

extern UINT32 debugrender_highlight;

void R_CheckDebugHighlight(debugrender_highlight_t type);
INT32 R_AdjustLightLevel(INT32 light);
UINT8 R_DebugLineColor(const line_t *ld);

void Command_Debugrender_highlight(void);

extern consvar_t
	cv_debugrender_contrast,
	cv_debugrender_spriteclip,
	cv_debugrender_portal;

// Called by startup code.
void R_Init(void);

void R_CheckViewMorph(int split);
void R_ApplyViewMorph(int split);
angle_t R_ViewRollAngle(const player_t *player, UINT8 viewnum);

// just sets setsizeneeded true
extern dboolean setsizeneeded;
void R_SetViewSize(void);

// do it (sometimes explicitly called)
void R_ExecuteSetViewSize(void);

void R_SetupFrame(int split, dboolean skybox);
void R_SkyboxFrame(int split);

dboolean R_ViewpointHasChasecam(player_t *player);
dboolean R_IsViewpointThirdPerson(player_t *player, dboolean skybox);

// Called by D_Display.
void R_RenderPlayerView(void);

// add commands related to engine, at game startup
void R_RegisterEngineStuff(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
