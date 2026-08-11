// BLANKART
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
#include "info.h"

#ifdef __cplusplus
extern "C" {
#endif

// number of total 'button' inputs, include keyboard keys, plus virtual
// keys (mousebuttons and joybuttons becomes keys)
#define NUMKEYS 256

#define MOUSEBUTTONS 8

#define JOYBUTTONS	26 // 26 buttons, to match SDL_GameControllerButton
#define JOY_DPAD_UP 11

#define JOYANALOGS		4 // 4 analog stick axes (2 sticks * 2 axes)
#define JOYTRIGGERS		2 // 2 trigger axes, positive only
#define JOYIMUAXISES	6 // 3 accelerometer, 3 gyroscope axes (x, y, z)
#define JOYAXISES		(JOYANALOGS + JOYTRIGGERS + JOYIMUAXISES)
#define JOYAXISKEYS		(2 * JOYANALOGS) + JOYTRIGGERS
#define JOYSTATEKEYS	JOYAXISKEYS + JOYIMUAXISES

#define MAXINPUTMAPPING 4

//
// mouse and joystick buttons are handled as 'virtual' keys
//
typedef enum
{
	KEY_JOY1 = NUMKEYS,
	KEY_HAT1 = KEY_JOY1 + JOY_DPAD_UP,
	KEY_AXIS1 = KEY_JOY1 + JOYBUTTONS,
	KEY_ACCELEROMETER1 = KEY_AXIS1 + JOYAXISKEYS,
	KEY_GYROSCOPE1 = KEY_ACCELEROMETER1 + 3,
	JOYINPUTEND = KEY_AXIS1 + JOYSTATEKEYS,

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
	gc_respawn,
	gc_director,
	gc_horncode,
	gc_freelook,
	gc_flickstick_left, // ASTROS BULLSHIT OH YEAH
	gc_flickstick_right, // ASTROS BULLSHIT OH YEAH
	gc_flickstick_forward, // ASTROS BULLSHIT OH YEAH
	gc_flickstick_back, // ASTROS BULLSHIT OH YEAH
	num_gamecontrols
} gamecontrols_e;

typedef enum
{
	DEADZONE_X,
	DEADZONE_Y,
	DEADZONE_BUTTON,
	DEADZONE_2D,
	DEADZONE_NONE,
} analogdeadzone_e;

typedef enum
{
	ACCELEROMETER,
	GYROSCOPE,
} motionsensortype_e;

// mouse values are used once
extern consvar_t cv_mousesens, cv_mouseysens;
extern consvar_t cv_mousesens2, cv_mouseysens2;
extern consvar_t cv_controlperkey;
extern consvar_t cv_litesteer[MAXSPLITSCREENPLAYERS];
extern consvar_t cv_turnsmooth[MAXSPLITSCREENPLAYERS];
extern consvar_t cv_controllerrumble[MAXSPLITSCREENPLAYERS];
extern consvar_t cv_rumblestrength[MAXSPLITSCREENPLAYERS];
extern consvar_t cv_controllerled[MAXSPLITSCREENPLAYERS];

typedef enum
{
	TILTCONTROL_OFF = 0,
	TILTCONTROL_STEER = 1,
	TILTCONTROL_CAME = 2,
	TILTCONTROL_ON = 3
} tiltcontrol_e;
extern consvar_t cv_tiltcontrol[MAXSPLITSCREENPLAYERS];

// current state of the keys: JOYAXISRANGE or 0 when dboolean.
// Or anything inbetween for analog values
#define MAXDEVICES (MAXGAMEPADS + 1) // Gamepads + keyboard & mouse
// the above is horrid since device tracks the gamepad id for whatever godawful reason
// the gamepad instance id may be alot larger than (MAXGAMEPADS + 1)/"9"

typedef INT32 inputpoll_t[MAXDEVICES][NUMINPUTS];
extern inputpoll_t gamekeydown, gamekeydown_msec;

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

#define INVALID_DEVICE -1
#define KEYBOARD_MOUSE_DEVICE 0

skincolornum_t G_GetSkinColorForController(INT32 playernum);
void G_SetPlayerControllerIndicatorColor(INT32 playernum, UINT16 color);
void G_ControllerLEDTick(void);
void G_ResetControllerLED(void);
void G_ResetAllControllerRumbles(void);
void G_PlayerControllerRumble(INT32 playernum, UINT16 low_strength, UINT16 high_strength, UINT32 duration);
void G_ControllerRumbleTick(void);

INT32 G_GetDevicePlayer(INT32 deviceID);

INT32 G_AxisToKey(event_t *ev);

// remaps the input event to a game control.
void G_MapEventsToControls(event_t *ev);

// returns the name of a key
const char *G_KeynumToString(INT32 keynum);
INT32 G_KeyStringtoNum(const char *keystr);

dboolean G_KeyBindIsNecessary(INT32 gc);
dboolean G_KeyIsAvailable(INT32 key, INT32 deviceID, dboolean digital);

// detach any keys associated to the given game control
void G_ClearControlKeys(INT32 (*setupcontrols)[MAXINPUTMAPPING], INT32 control);
void G_ClearAllControlKeys(void);
void Command_Setcontrol_f(void);
void Command_Setcontrol2_f(void);
void Command_Setcontrol3_f(void);
void Command_Setcontrol4_f(void);
INT32 G_GetControlScheme(INT32 (*fromcontrols)[MAXINPUTMAPPING], const INT32 *gclist, INT32 gclen);
void G_ResetControls(UINT8 p);
void G_SaveKeySetting(FILE *f, INT32 (*fromcontrolsa)[MAXINPUTMAPPING], INT32 (*fromcontrolsb)[MAXINPUTMAPPING], INT32 (*fromcontrolsc)[MAXINPUTMAPPING], INT32 (*fromcontrolsd)[MAXINPUTMAPPING]);
INT32 G_CheckDoubleUsage(INT32 keynum, INT32 playernum, dboolean modify);

#define MAXGAMEPADTILT (55*FRACUNIT/100)
#define ACCELEROMETERGRAVITY (642688) // 9.80665
dboolean G_GetGamepadCanUseTilt(INT32 p);
void G_ResetGyroCalibration(INT32 p);
void G_UpdateGamepadAutoCalibration(INT32 p, vector3_t accel, vector3_t gyro, dboolean allowautocalibration);
void G_UpdateGamepadGyro(INT32 p, vector3_t gyro);
void G_UpdateGamepadGravity(INT32 p, vector3_t gyro, vector3_t accel);
fixed_t G_GetGamepadShakinessFactor(INT32 p);
vector3_t G_GetGamepadShake(INT32 p);
vector3_t G_GetGamepadGravity(INT32 p);
vector3_t G_GetCalibratedGyroOffset(INT32 p);
vector3_t G_GetGamepadCalibratedGyro(INT32 p);
vector3_t G_PlayerInputSensor(UINT8 p, motionsensortype_e sensor);
INT32 G_GetGamepadTilt(INT32 p);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
