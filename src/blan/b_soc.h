// BlanKart
//-----------------------------------------------------------------------------
// Copyright (C) 2024 by NepDisk
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

boolean B_UseWallTransfer(void);
boolean B_UseTerrainDef(void);
INT32 B_TrackComplexity(void);
fixed_t K_TrackModifierMax(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
