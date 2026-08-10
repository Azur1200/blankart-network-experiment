// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2020 by Jaime "Lactozilla" Passos.
// Copyright (C) 2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file hw_gpu.h
/// \brief GPU low-level interface API

#ifndef __HWR_GPU_H__
#define __HWR_GPU_H__

#include "../screen.h"

#include "hw_data.h"
#include "hw_defs.h"
#include "hw_md2.h"

#ifdef __cplusplus
extern "C" {
#endif

dboolean GL_Init(void);
void GL_SetPalette(RGBA_t *palette);
void GL_FinishUpdate(INT32 waitvbl);
void GL_Draw2DLine(F2DCoord *v1, F2DCoord *v2, RGBA_t Color);
void GL_DrawPolygon(FSurfaceInfo *pSurf, FOutVector *pOutVerts, FUINT iNumPts, FBITFIELD PolyFlags);
void GL_DrawIndexedTriangles(FSurfaceInfo *pSurf, FOutVector *pOutVerts, FUINT iNumPts, FBITFIELD PolyFlags, UINT32 *IndexArray);
void GL_RenderSkyDome(gl_sky_t *sky);
void GL_SetBlend(FBITFIELD PolyFlags);
void GL_ClearBuffer(FBOOLEAN ColorMask, FBOOLEAN DepthMask, FBOOLEAN StencilMask, FRGBAFloat *ClearColor);
void GL_SetTexture(GLMipmap_t *pTexInfo);
void GL_UpdateTexture(GLMipmap_t *pTexInfo);
void GL_DeleteTexture(GLMipmap_t *pTexInfo);
void GL_ReadScreenTexture(int tex, UINT8 *restrict dest, INT32 scale);
void GL_GClipRect(INT32 minx, INT32 miny, INT32 maxx, INT32 maxy, float nearclip);
void GL_ClearMipMapCache(void);

void GL_SetSpecialState(hwdspecialstate_t IdState, INT32 Value);
void GL_SetStencilMode(hwdstencilmode_t mode, INT32 ref);

// Hurdler: added for new development
void GL_DrawModel(model_t *model, INT32 frameIndex, float duration, float tics, INT32 nextFrameIndex, FTransform *pos, float hscale, float vscale, UINT8 flipped, UINT8 hflipped, FSurfaceInfo *Surface);
void GL_CreateModelVBOs(model_t *model);
void GL_SetTransform(FTransform *ptransform);
INT32 GL_GetTextureUsed(void);

void GL_FlushScreenTextures(void);
void GL_DoScreenWipe(int wipeStart, int wipeEnd);
void GL_DrawScreenTexture(int tex, FSurfaceInfo *surf, FBITFIELD polyflags);
void GL_MakeScreenTexture(int tex);
void GL_RenderVhsEffect(fixed_t upbary, fixed_t downbary, UINT8 updistort, UINT8 downdistort, UINT8 barsize);
void GL_DrawScreenFinalTexture(int tex, int width, int height);

#define SCREENVERTS 10
void GL_PostImgRedraw(float points[SCREENVERTS][SCREENVERTS][2]);

dboolean GL_InitShaders(void);
void GL_LoadShader(int slot, char *code, hwdshaderstage_t stage);
dboolean GL_CompileShader(int slot);
void GL_SetShader(int slot);
void GL_UnSetShader(void);

void GL_SetShaderInfo(hwdshaderinfo_t info, INT32 value);

void GL_SetPaletteLookup(UINT8 *lut);
UINT32 GL_CreateLightTable(RGBA_t *hw_lighttable);
void GL_UpdateLightTable(UINT32 id, RGBA_t *hw_lighttable);
void GL_ClearLightTables(void);
void GL_SetScreenPalette(RGBA_t *palette);

#ifdef __cplusplus
} // extern "C"
#endif

#endif //__HWR_GPU_H__
