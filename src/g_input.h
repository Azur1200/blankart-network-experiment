// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  g_input.h
/// \brief handle mouse/keyboard/joystick inputs,
///        maps inputs to game controls (forward, spin, jump...)

#ifndef __G_INPUT__
#define __G_INPUT__

#include "d_event.h"
#include "keys.h"
#include "command.h"

#ifdef __cplusplus
extern "C" {
#endif

// number of total 'button' inputs, include keyboard keys, plus virtual
// keys (mousebuttons and joybuttons becomes keys)
#define NUMKEYS 256

#define MOUSEBUTTONS 8

#define JOYBUTTONS   21 // 21 buttons, to match SDL_GameControllerButton
#define JOYANALOGS   4 // 4 analog stick axes (2 sticks * 2 axes)
#define JOYTRIGGERS  2 // 2 trigger axes, positive only
#define JOYAXISES   (JOYANALOGS + JOYTRIGGERS)
#define JOYAXISKEYS ((2 * JOYANALOGS) + JOYTRIGGERS)

#define MAXINPUTMAPPING 4

//
// mouse and joystick buttons are handled as 'virtual' keys
//
typedef enum
{
	KEY_JOY1 = NUMKEYS,
	KEY_HAT1 = KEY_JOY1 + 11, // macro for SDL_CONTROLLER_BUTTON_DPAD_UP
	KEY_AXIS1 = KEY_JOY1 + JOYBUTTONS,
	JOYINPUTEND = KEY_AXIS1 + JOYAXISKEYS,

	KEY_MOUSE1 = JOYINPUTEND,
	KEY_MOUSEMOVE = KEY_MOUSE1 + MOUSEBUTTONS,
	KEY_MOUSEWHEELUP = KEY_MOUSEMOVE + 4,
	KEY_MOUSEWHEELDOWN = KEY_MOUSEWHEELUP + 1,
	MOUSEINPUTEND = KEY_MOUSEWHEELDOWN + 1,

	NUMINPUTS = MOUSEINPUTEND,
} key_input_e;

typedef enum
{
	gc_null = 0, // a key/button mapped to gc_null has no effect
	gc_aimforward,
	gc_aimbackward,
	gc_turnleft,
	gc_turnright,
	gc_accelerate,
	gc_drift,
	gc_brake,
	gc_fire,
	gc_lookback,
	gc_camreset,
	gc_camtoggle,
	gc_spectate,
	gc_lookup,
	gc_lookdown,
	gc_centerview,
	gc_talkkey,
	gc_teamkey,
	gc_scores,
	gc_console,
	gc_pause,
	gc_systemmenu,
	gc_screenshot,
	gc_recordgif,
	gc_viewpoint,
	gc_custom1, // Lua scriptable
	gc_custom2, // Lua scriptable
	gc_custom3, // Lua scriptable
	num_gamecontrols
} gamecontrols_e;

// mouse values are used once
extern consvar_t cv_mousesens, cv_mouseysens;
extern consvar_t cv_mousesens2, cv_mouseysens2;
extern consvar_t cv_controlperkey, cv_turnsmooth;

// current state of the keys: JOYAXISRANGE or 0 when boolean.
// Or anything inbetween for analog values
#define MAXDEVICES (MAXGAMEPADS + 1) // Gamepads + keyboard & mouse
extern INT32 gamekeydown[MAXDEVICES][NUMINPUTS];
extern boolean deviceResponding[MAXDEVICES];

// several key codes (or virtual key) per game control
extern INT32 gamecontrol[MAXSPLITSCREENPLAYERS][num_gamecontrols][MAXINPUTMAPPING];
extern INT32 gamecontroldefault[num_gamecontrols][MAXINPUTMAPPING]; // default control storage

/*
#define num_gcl_accelerate 1
#define num_gcl_brake 1
#define num_gcl_drift 1
#define num_gcl_movement 6
#define num_gcl_item 3
#define num_gcl_full 10

extern const INT32 gcl_accelerate[num_gcl_accelerate];
extern const INT32 gcl_brake[num_gcl_brake];
extern const INT32 gcl_drift[num_gcl_drift];
extern const INT32 gcl_movement[num_gcl_movement];
extern const INT32 gcl_item[num_gcl_item];
extern const INT32 gcl_full[num_gcl_full];
*/

// peace to my little coder fingers!
// check a gamecontrol being active or not

INT32 G_GetDevicePlayer(INT32 deviceID);

INT32 G_AxisToKey(event_t *ev);

// remaps the input event to a game control.
void G_MapEventsToControls(event_t *ev);

// returns the name of a key
const char *G_KeynumToString(INT32 keynum);
INT32 G_KeyStringtoNum(const char *keystr);

boolean G_KeyBindIsNecessary(INT32 gc);
boolean G_KeyIsAvailable(INT32 key, INT32 deviceID);

extern boolean inputcheckdigital;

// detach any keys associated to the given game control
void G_ClearControlKeys(INT32 (*setupcontrols)[MAXINPUTMAPPING], INT32 control);
void G_ClearAllControlKeys(void);
void Command_Setcontrol_f(void);
void Command_Setcontrol2_f(void);
void Command_Setcontrol3_f(void);
void Command_Setcontrol4_f(void);
void G_DefineDefaultControls(void);
INT32 G_GetControlScheme(INT32 (*fromcontrols)[MAXINPUTMAPPING], const INT32 *gclist, INT32 gclen);
void G_CopyControls(INT32 (*setupcontrols)[MAXINPUTMAPPING], INT32 (*fromcontrols)[MAXINPUTMAPPING], const INT32 *gclist, INT32 gclen);
void G_SaveKeySetting(FILE *f, INT32 (*fromcontrolsa)[MAXINPUTMAPPING], INT32 (*fromcontrolsb)[MAXINPUTMAPPING], INT32 (*fromcontrolsc)[MAXINPUTMAPPING], INT32 (*fromcontrolsd)[MAXINPUTMAPPING]);
INT32 G_CheckDoubleUsage(INT32 keynum, INT32 playernum, boolean modify);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
