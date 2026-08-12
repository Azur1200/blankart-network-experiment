// BlanKart
//-----------------------------------------------------------------------------
// Copyright (C) 2026 by NepDisk
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------

#ifndef __B_SOC__
#define __B_SOC__

#ifdef __cplusplus
extern "C" {
#endif

#include "../doomstat.h"
#include "../deh_soc.h"

dboolean B_UseWallTransfer(mobj_t *mo);
int32_t B_TrackComplexity(void);
fixed_t K_TrackModifierMax(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
