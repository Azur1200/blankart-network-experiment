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
/// \file  r_segs.h
/// \brief Refresh module, drawing LineSegs from BSP

#ifndef __R_SEGS__
#define __R_SEGS__

#ifdef __cplusplus
extern "C" {
#endif

transnum_t R_GetLinedefTransTable(fixed_t alpha);
void R_RenderMaskedSegRange(drawseg_t *ds, int32_t x1, int32_t x2);
void R_RenderThickSideRange(drawseg_t *ds, int32_t x1, int32_t x2, ffloor_t *pffloor);
void R_StoreWallRange(int32_t start, int32_t stop);
void R_ClearSegTables(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
