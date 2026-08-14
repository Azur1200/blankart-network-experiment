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

#include "../m_perfstats.h"

#ifdef __cplusplus
extern "C" {
#endif

// Startup & Shutdown the hardware mode renderer
void HWR_Startup(void);
void HWR_Switch(void);
void HWR_Shutdown(void);

void HWR_drawAMline(const fline_t *fl, int32_t color);
void HWR_FadeScreenMenuBack(uint16_t color, uint8_t strength);
void HWR_DrawConsoleBack(uint32_t color, int32_t height);
void HWR_EncoreInvertScreen(void);
void HWR_DrawTutorialBack(uint32_t color, int32_t boxheight);
void HWR_RenderSkyboxView(player_t *player);
void HWR_RenderPlayerView(void);
void HWR_DrawFlatFill(int32_t x, int32_t y, int32_t w, int32_t h, lumpnum_t flatlumpnum);
void HWR_SetViewSize(void);
void HWR_SetTransformAiming(FTransform *trans, player_t *player, dboolean skybox, dboolean side_effect);
void HWR_RollTransform(FTransform *tr, angle_t roll);
dboolean HWR_BlendMidtextureSurface(FSurfaceInfo *pSurf);
void HWR_DrawStretchyFixedPatch(patch_t *gpatch, fixed_t x, fixed_t y, fixed_t pscale, fixed_t vscale, int32_t option, const uint8_t *colormap);
void HWR_DrawCroppedPatch(patch_t *gpatch, fixed_t x, fixed_t y, fixed_t scale, int32_t option, fixed_t sx, fixed_t sy, fixed_t w, fixed_t h);
void HWR_DrawAffinePatch(patch_t *gpatch, fixed_t x, fixed_t y, const affine_t *transform, int32_t option, const uint8_t *colormap);
void HWR_MakePatch(const patch_t *patch, GLPatch_t *grPatch, GLMipmap_t *grMipmap, dboolean makebitmap);
void HWR_CreatePlanePolygons(int32_t bspnum);
void HWR_CreateStaticLightmaps(int32_t bspnum);
void HWR_DrawFill(int32_t x, int32_t y, int32_t w, int32_t h, int32_t color);
void HWR_DrawFixedLine(int32_t x1, int32_t y1, int32_t x2, int32_t y2, fixed_t w, int32_t color);
void HWR_DrawFadeFill(int32_t x, int32_t y, int32_t w, int32_t h, int32_t color, uint16_t actualcolor, uint8_t strength);
void HWR_DrawConsoleFill(int32_t x, int32_t y, int32_t w, int32_t h, int32_t color, uint32_t actualcolor);	// Lat: separate flags from color since color needs to be an uint to work right.
void HWR_DrawDiag(int32_t x, int32_t y, int32_t wh, int32_t color);
void HWR_DrawPic(int32_t x,int32_t y,lumpnum_t lumpnum);
dboolean HWR_IsWireframeMode(void);

uint8_t *HWR_GetScreenshot(int32_t scale);

void HWR_AddCommands(void);
void HWR_AddSessionCommands(void);
void transform(float *cx, float *cy, float *cz);
int32_t HWR_GetTextureUsed(void);
void HWR_DoPostProcessor(player_t *player);
void HWR_StartScreenWipe(void);
void HWR_EndScreenWipe(void);
void HWR_MakeIntermissionBG(void);
void HWR_DrawIntermissionBG(void);
void HWR_DoWipe(uint8_t wipenum, uint8_t scrnnum);
void HWR_DoTintedWipe(uint8_t wipenum, uint8_t scrnnum);
void HWR_RenderVhsEffect(fixed_t upbary, fixed_t downbary, uint8_t updistort, uint8_t downdistort, uint8_t barsize);
void HWR_MakeScreenFinalTexture(void);
void HWR_DrawScreenFinalTexture(int width, int height);

// This stuff is put here so models can use them
dboolean HWR_UseShader(void);
dboolean HWR_OverrideObjectLightLevel(mobj_t *thing, int32_t *lightlevel);
void HWR_Lighting(FSurfaceInfo *Surface, int32_t light_level, extracolormap_t *colormap, const dboolean directional);
uint8_t HWR_FogBlockAlpha(int32_t light, extracolormap_t *colormap); // Let's see if this can work

uint8_t HWR_GetTranstableAlpha(int32_t transtablenum);
FBITFIELD HWR_GetBlendModeFlag(int32_t ast);
FBITFIELD HWR_SurfaceBlend(int32_t style, int32_t transtablenum, FSurfaceInfo *pSurf);
FBITFIELD HWR_TranstableToAlpha(int32_t transtablenum, FSurfaceInfo *pSurf);

dboolean HWR_ShouldUsePaletteRendering(void);

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
extern consvar_t cv_glsolvetjoin;
extern consvar_t cv_glshearing;
extern consvar_t cv_glspritebillboarding;
extern consvar_t cv_glskydome;

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
extern ps_metric_t ps_hw_nodesorttime;
extern ps_metric_t ps_hw_nodedrawtime;
extern ps_metric_t ps_hw_spritesorttime;
extern ps_metric_t ps_hw_spritedrawtime;

// Render stats for batching
extern ps_metric_t ps_hw_numpolys;
extern ps_metric_t ps_hw_numverts;
extern ps_metric_t ps_hw_numcalls;
extern ps_metric_t ps_hw_numshaders;
extern ps_metric_t ps_hw_numtextures;
extern ps_metric_t ps_hw_numpolyflags;
extern ps_metric_t ps_hw_numcolors;
extern ps_metric_t ps_hw_batchsorttime;
extern ps_metric_t ps_hw_batchdrawtime;

extern dboolean gl_init;
extern dboolean gl_maploaded;
extern dboolean gl_sessioncommandsadded;
extern dboolean gl_shadersavailable;

extern dboolean gl_rendering_skybox;

#ifdef __cplusplus
} // extern "C"
#endif

#endif
