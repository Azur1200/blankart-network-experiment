// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2020 by Jaime "Lactozilla" Passos.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  r_patchrotation.h
/// \brief Patch rotation.

#ifndef __R_PATCHROTATION__
#define __R_PATCHROTATION__

#include "r_patch.h"
#include "r_picformats.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ROTSPRITE
rotsprite_t *RotatedPatch_Create(int32_t numangles);
void RotatedPatch_DoRotation(rotsprite_t *rotsprite, patch_t *patch, int32_t angle, int32_t xpivot, int32_t ypivot, dboolean flip);

extern fixed_t rollcosang[ROTANGLES];
extern fixed_t rollsinang[ROTANGLES];

#ifdef __cplusplus
} // extern "C"
#endif

#endif

#endif // __R_PATCHROTATION__
