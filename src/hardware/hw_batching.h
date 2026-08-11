// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file hw_batching.h
/// \brief Draw call batching and related things.

#ifndef __HWR_BATCHING_H__
#define __HWR_BATCHING_H__

#include "hw_defs.h"
#include "hw_data.h"
#include "hw_gpu.h"

#ifdef __cplusplus
extern "C" {
#endif

void HWR_StartBatching(void);
void HWR_PauseBatching(void);
void HWR_SetCurrentTexture(GLMipmap_t *texture);
void HWR_ProcessPolygon(FSurfaceInfo *pSurf, FOutVector *pOutVerts, FUINT iNumPts, FBITFIELD PolyFlags, int shader, dboolean horizonSpecial);
void HWR_PushBatchingState(void);
void HWR_PopBatchingState(void);
void HWR_RenderBatches(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
