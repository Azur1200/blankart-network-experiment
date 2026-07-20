// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file hw_main.h
/// \brief 3D render mode functions

#ifndef __HWR_MAIN_H__
#define __HWR_MAIN_H__

#include "hw_data.h"
#include "hw_defs.h"

#include "../am_map.h"
#include "../d_player.h"
#include "../r_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

// Startup & Shutdown the hardware mode renderer
void HWR_Startup(void);
void HWR_Switch(void);
void HWR_Shutdown(void);

void HWR_drawAMline(const fline_t *fl, INT32 color);
void HWR_FadeScreenMenuBack(UINT16 color, UINT8 strength);
void HWR_DrawConsoleBack(UINT32 color, INT32 height);
void HWR_EncoreInvertScreen(void);
void HWR_DrawTutorialBack(UINT32 color, INT32 boxheight);
void HWR_RenderSkyboxView(player_t *player);
void HWR_RenderPlayerView(void);
void HWR_DrawFlatFill(INT32 x, INT32 y, INT32 w, INT32 h, lumpnum_t flatlumpnum);
void HWR_SetViewSize(void);
void HWR_SetTransformAiming(FTransform *trans, player_t *player, boolean skybox, boolean side_effect);
void HWR_RollTransform(FTransform *tr, angle_t roll);
boolean HWR_BlendMidtextureSurface(FSurfaceInfo *pSurf);
void HWR_DrawStretchyFixedPatch(patch_t *gpatch, fixed_t x, fixed_t y, fixed_t pscale, fixed_t vscale, INT32 option, const UINT8 *colormap);
void HWR_DrawCroppedPatch(patch_t *gpatch, fixed_t x, fixed_t y, fixed_t scale, INT32 option, fixed_t sx, fixed_t sy, fixed_t w, fixed_t h);
void HWR_DrawAffinePatch(patch_t *gpatch, fixed_t x, fixed_t y, const affine_t *transform, INT32 option, const UINT8 *colormap);
void HWR_MakePatch(const patch_t *patch, GLPatch_t *grPatch, GLMipmap_t *grMipmap, boolean makebitmap);
void HWR_CreatePlanePolygons(INT32 bspnum);
void HWR_CreateStaticLightmaps(INT32 bspnum);
void HWR_DrawFill(INT32 x, INT32 y, INT32 w, INT32 h, INT32 color);
void HWR_DrawFixedLine(INT32 x1, INT32 y1, INT32 x2, INT32 y2, fixed_t w, INT32 color);
void HWR_DrawFadeFill(INT32 x, INT32 y, INT32 w, INT32 h, INT32 color, UINT16 actualcolor, UINT8 strength);
void HWR_DrawConsoleFill(INT32 x, INT32 y, INT32 w, INT32 h, INT32 color, UINT32 actualcolor);	// Lat: separate flags from color since color needs to be an uint to work right.
void HWR_DrawDiag(INT32 x, INT32 y, INT32 wh, INT32 color);
void HWR_DrawPic(INT32 x,INT32 y,lumpnum_t lumpnum);
boolean HWR_IsWireframeMode(void);

UINT8 *HWR_GetScreenshot(INT32 scale);

void HWR_AddCommands(void);
void HWR_AddSessionCommands(void);
void transform(float *cx, float *cy, float *cz);
INT32 HWR_GetTextureUsed(void);
void HWR_DoPostProcessor(player_t *player);
void HWR_StartScreenWipe(void);
void HWR_EndScreenWipe(void);
void HWR_MakeIntermissionBG(void);
void HWR_DrawIntermissionBG(void);
void HWR_DoWipe(UINT8 wipenum, UINT8 scrnnum);
void HWR_DoTintedWipe(UINT8 wipenum, UINT8 scrnnum);
void HWR_RenderVhsEffect(fixed_t upbary, fixed_t downbary, UINT8 updistort, UINT8 downdistort, UINT8 barsize);
void HWR_MakeScreenFinalTexture(void);
void HWR_DrawScreenFinalTexture(int width, int height);

// This stuff is put here so models can use them
boolean HWR_UseShader(void);
boolean HWR_OverrideObjectLightLevel(mobj_t *thing, INT32 *lightlevel);
void HWR_Lighting(FSurfaceInfo *Surface, INT32 light_level, extracolormap_t *colormap, const boolean directional);
UINT8 HWR_FogBlockAlpha(INT32 light, extracolormap_t *colormap); // Let's see if this can work

UINT8 HWR_GetTranstableAlpha(INT32 transtablenum);
FBITFIELD HWR_GetBlendModeFlag(INT32 ast);
FBITFIELD HWR_SurfaceBlend(INT32 style, INT32 transtablenum, FSurfaceInfo *pSurf);
FBITFIELD HWR_TranstableToAlpha(INT32 transtablenum, FSurfaceInfo *pSurf);

boolean HWR_ShouldUsePaletteRendering(void);

extern CV_PossibleValue_t glanisotropicmode_cons_t[];

extern consvar_t cv_glshaders, cv_glallowshaders;
extern consvar_t cv_glmodels, cv_glscreentextures;

// SRB2Kart: We don't like these options.
// Interpolation should be up to who animated the model.
// Lighting makes the modeler's intended texturing look funky.
// ^ Ignoring this shit lmao.
// This should be the players choice to decide. Why must you always disregard the player's choice? - Nep
#define BAD_MODEL_OPTIONS

#ifdef BAD_MODEL_OPTIONS
extern consvar_t cv_glmodelinterpolation;
extern consvar_t cv_glmodellighting;
#endif

extern consvar_t cv_glfiltermode;
extern consvar_t cv_glanisotropicmode;
extern consvar_t cv_fovchange;

extern consvar_t cv_glshearing;
extern consvar_t cv_glspritebillboarding;
extern consvar_t cv_glskydome;

extern consvar_t cv_glsolvetjoin, cv_glpolytile, cv_glpolyshape;

extern consvar_t cv_glbatching;
extern consvar_t cv_glwireframe;
extern consvar_t cv_glpaletterendering;
extern consvar_t cv_glpalettedepth;
extern consvar_t cv_gllightdither;
extern consvar_t cv_gldebugportal;

extern consvar_t cv_glskydebug;

extern seg_t *gl_curline;
extern side_t *gl_sidedef;
extern line_t *gl_linedef;

extern sector_t *gl_frontsector;
extern sector_t *gl_backsector;

extern FTransform atransform;
extern float gl_viewsin, gl_viewcos;

extern float gl_viewx, gl_viewy, gl_viewz;
extern float gl_viewsin, gl_viewcos;

extern float gl_viewludsin, gl_viewludcos;

// Render stats
extern precise_t ps_hw_skyboxtime;
extern precise_t ps_hw_nodesorttime;
extern precise_t ps_hw_nodedrawtime;
extern precise_t ps_hw_spritesorttime;
extern precise_t ps_hw_spritedrawtime;

// Render stats for batching
extern int ps_hw_numpolys;
extern int ps_hw_numverts;
extern int ps_hw_numcalls;
extern int ps_hw_numshaders;
extern int ps_hw_numtextures;
extern int ps_hw_numpolyflags;
extern int ps_hw_numcolors;
extern precise_t ps_hw_batchsorttime;
extern precise_t ps_hw_batchdrawtime;

extern boolean gl_init;
extern boolean gl_maploaded;
extern boolean gl_sessioncommandsadded;
extern boolean gl_shadersavailable;

extern boolean gl_rendering_skybox;

#ifdef __cplusplus
} // extern "C"
#endif

#endif
