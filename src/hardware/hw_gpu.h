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
void GL_FinishUpdate(int32_t waitvbl);
void GL_Draw2DLine(F2DCoord *v1, F2DCoord *v2, RGBA_t Color);
void GL_DrawPolygon(FSurfaceInfo *pSurf, FOutVector *pOutVerts, FUINT iNumPts, FBITFIELD PolyFlags);
void GL_DrawIndexedTriangles(FSurfaceInfo *pSurf, FOutVector *pOutVerts, FUINT iNumPts, FBITFIELD PolyFlags, uint32_t *IndexArray);
void GL_RenderSkyDome(gl_sky_t *sky);
void GL_SetBlend(FBITFIELD PolyFlags);
void GL_ClearBuffer(FBOOLEAN ColorMask, FBOOLEAN DepthMask, FBOOLEAN StencilMask, FRGBAFloat *ClearColor);
void GL_SetTexture(GLMipmap_t *pTexInfo);
void GL_UpdateTexture(GLMipmap_t *pTexInfo);
void GL_DeleteTexture(GLMipmap_t *pTexInfo);
void GL_ReadScreenTexture(int tex, uint8_t *restrict dest, int32_t scale);
void GL_GClipRect(int32_t minx, int32_t miny, int32_t maxx, int32_t maxy, float nearclip);
void GL_ClearMipMapCache(void);

void GL_SetSpecialState(hwdspecialstate_t IdState, int32_t Value);
void GL_SetStencilMode(hwdstencilmode_t mode, int32_t ref);

// Hurdler: added for new development
void GL_DrawModel(model_t *model, int32_t frameIndex, float duration, float tics, int32_t nextFrameIndex, FTransform *pos, float hscale, float vscale, uint8_t flipped, uint8_t hflipped, FSurfaceInfo *Surface);
void GL_CreateModelVBOs(model_t *model);
void GL_SetTransform(FTransform *ptransform);
int32_t GL_GetTextureUsed(void);

void GL_FlushScreenTextures(void);
void GL_DoScreenWipe(int wipeStart, int wipeEnd);
void GL_DrawScreenTexture(int tex, FSurfaceInfo *surf, FBITFIELD polyflags);
void GL_MakeScreenTexture(int tex);
void GL_RenderVhsEffect(fixed_t upbary, fixed_t downbary, uint8_t updistort, uint8_t downdistort, uint8_t barsize);
void GL_DrawScreenFinalTexture(int tex, int width, int height);

#define SCREENVERTS 10
void GL_PostImgRedraw(float points[SCREENVERTS][SCREENVERTS][2]);

dboolean GL_InitShaders(void);
void GL_LoadShader(int slot, char *code, hwdshaderstage_t stage);
dboolean GL_CompileShader(int slot);
void GL_SetShader(int slot);
void GL_UnSetShader(void);

void GL_SetShaderInfo(hwdshaderinfo_t info, int32_t value);

void GL_SetPaletteLookup(uint8_t *lut);
uint32_t GL_CreateLightTable(RGBA_t *hw_lighttable);
void GL_ClearLightTables(void);
void GL_SetScreenPalette(RGBA_t *palette);

#ifdef __cplusplus
} // extern "C"
#endif

#endif //__HWR_GPU_H__
