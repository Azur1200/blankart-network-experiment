// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2026 by Team BlanKart.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  d_player.h
/// \brief Map number type

#ifndef __MAPNUM__
#define __MAPNUM__

#include "doomdef.h"

#ifdef __cplusplus
extern "C" {
#endif

// see also G_MapNumber
typedef enum
{
    NEXTMAP_RESERVED = UINT16_MAX, // so nextmap+1 doesn't roll over -- remove when gamemap is made 0-indexed
    NEXTMAP_TITLE = UINT16_MAX-1,
    NEXTMAP_EVALUATION = UINT16_MAX-2,
    NEXTMAP_CREDITS = UINT16_MAX-3,
    NEXTMAP_CEREMONY = UINT16_MAX-4,
    NEXTMAP_VOTING = UINT16_MAX-5,
    NEXTMAP_INVALID = UINT16_MAX-6, // Always last
    NEXTMAP_SPECIAL = NEXTMAP_INVALID
} ATTRPACK mapnum_t;

#ifdef __cplusplus
} // extern "C"
#endif

#endif
