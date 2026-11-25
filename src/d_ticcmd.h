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
/// \file  d_ticcmd.h
/// \brief Button/action code definitions, ticcmd_t

#ifndef __D_TICCMD__
#define __D_TICCMD__

#include "m_fixed.h"
#include "doomtype.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAXPREDICTTICS 12

// Button/action code definitions.
typedef enum
{
	BT_ACCELERATE	  = 1,		// Accelerate
	BT_DRIFT		  = 1<<2,	// Drift (direction is cmd->turning)
	BT_BRAKE		  = 1<<3,	// Brake
	BT_ATTACK		  = 1<<4,	// Use Item
	BT_FORWARD		  = 1<<5,	// Aim Item Forward
	BT_BACKWARD		  = 1<<6,	// Aim Item Backward
	BT_LOOKBACK		  = 1<<7,	// Look Backward
	BT_HORN           = 1<<8,   // Honk your (follower's) horn

	// free: 1<<9 to 1<<12

	// Lua garbage
	BT_CUSTOM1		= 1<<13,
	BT_CUSTOM2		= 1<<14,
	BT_CUSTOM3		= 1<<15,
} buttoncode_t;

// The data sampled per tick (single player)
// and transmitted to other peers (multiplayer).
// Mainly movements/button commands per game tick,
// plus a checksum for internal state consistency.

// ticcmd turning bits
#define TICCMD_REDUCE 16

// ticcmd latency mask
#define TICCMD_LATENCYMASK 0xFF

// ticcmd flags
#define TICCMD_RECEIVED		(0x01) /* Actual tic recieved from client */
#define TICCMD_TYPING		(0x02) /* chat window or console open */
#define TICCMD_KEYSTROKE	(0x04) /* chat character input */
#define TICCMD_USINGTILT	(0x08) /* player is using tilt control instead of the stick/buttons to turn */
#define TICCMD_EXCESSTILT	(0x10) /* player is tilting gamepad farther than the max range */
#define TICCMD_PREDTURN		(0x20) /* angle was computed from predicted local kart state (latency 0 turn clamp) */

// 16 bytes long now!
struct ticcmd_t
{
	int8_t forwardmove; // -MAXPLMOVE to MAXPLMOVE (50) (has anticheat)
	int8_t sidemove; // -MAXPLMOVE to MAXPLMOVE (50) (has anticheat)
	int16_t turning; // "Steering Wheel" turn speed when driving (has anticheat)
	int16_t angle; // Predicted angle, use me if you can!
	int16_t throwdir; // Forwards/Backwards item use direction (has anticheat)
	int16_t aiming; // vertical aiming, see G_BuildTicCmd
	uint16_t buttons;
	uint8_t latency; // Netgames: how many tics ago was this ticcmd generated from this player's end?
	uint8_t flags;
} ATTRPACK;

#ifdef __cplusplus
} // extern "C"
#endif

#endif
