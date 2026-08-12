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
/// \file  d_event.h
/// \brief Event handling

#ifndef __D_EVENT__
#define __D_EVENT__

#include "doomtype.h"
#include "g_state.h"

#ifdef __cplusplus
extern "C" {
#endif

// Input event types.
typedef enum
{
	ev_keydown,
	ev_keyup,
	ev_console,
	ev_mouse,
	ev_joystick,
	ev_accelerometer,
	ev_gyroscope,
} evtype_t;

// Event structure.
struct event_t
{
	evtype_t type;
	int32_t data1; // keys / mouse/joystick buttons
	int32_t data2; // mouse/joystick x move; key repeat
	int32_t data3; // mouse/joystick y move
	int32_t device; // which player's device it belongs to
};

//
// GLOBAL VARIABLES
//
#define MAXEVENTS 256

extern event_t events[MAXEVENTS];
extern int32_t eventhead, eventtail;

#ifdef __cplusplus
} // extern "C"
#endif

#endif
