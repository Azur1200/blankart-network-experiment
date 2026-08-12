// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  i_gamepad.h
/// \brief share gamepad information with game control code

#ifndef __I_GAMEPAD_H__
#define __I_GAMEPAD_H__

#include "g_input.h"

#ifdef __cplusplus
extern "C" {
#endif

/*!
  \brief	-JOYAXISRANGE to +JOYAXISRANGE for each axis

	(1024-1) so we can do a right shift instead of division
	(doesnt matter anyway, just give enough precision)
	a gamepad will return -1, 0, or 1 in the event data
	an analog type joystick will return a value
	from -JOYAXISRANGE to +JOYAXISRANGE for each axis
*/

#define JOYAXISRANGE 1023

// detect a bug if we increase JOYBUTTONS above DIJOYSTATE's number of buttons
#if (JOYBUTTONS > 64)
"JOYBUTTONS is greater than int64_t bits can hold"
#endif

/**	\brief	The struct JoyType_s

 share some joystick information (maybe 2 for splitscreen), to the game input code,
 actually, we need to know if it is a gamepad or analog controls
*/

struct JoyType_t
{
	/*! this joystick is a gamepad, read: digital axes
	if FALSE, interpret the joystick event data as JOYAXISRANGE (see above) */
	int32_t bGamepadStyle;

};
/**	\brief Joystick info
	for palyer[sic] 1-4's joystick/gamepad
*/

extern JoyType_t Joystick[MAXSPLITSCREENPLAYERS];

/**	\brief to set up the first joystick scale
*/
void I_ControllerScale(void);

/**	\brief to set up the second joystick scale
*/
void I_ControllerScale2(void);

/**	\brief to set up the third joystick scale
*/
void I_ControllerScale3(void);

/**	\brief to set up the fourth joystick scale
*/
void I_ControllerScale4(void);

// Called by D_SRB2Main.

/**	\brief to startup a joystick
*/
void I_InitController(uint8_t index);

/**	\brief to startup the first joystick
*/
void I_InitController1(void);

/**	\brief to startup the second joystick
*/
void I_InitController2(void);

/**	\brief to startup the third joystick
*/
void I_InitController3(void);

/**	\brief to startup the fourth joystick
*/
void I_InitController4(void);

/**	\brief return the number of joystick on the system
*/
int32_t I_NumControllers(void);

/**	\brief	The *I_GetControllerName function

	\param	joyid	which joystick

	\return	joystick name
*/
const char *I_GetControllerName(int32_t joyid);

int32_t I_GetControllerSlotfromID(int32_t id);

dboolean I_ControllerSupportsRumble(int32_t playernum);
dboolean I_ControllerSupportsIndicatorMono(int32_t playernum);
dboolean I_ControllerSupportsIndicatorColor(int32_t playernum);
dboolean I_ControllerSupportsIndicatorPlayer(int32_t playernum);

dboolean I_ControllerSupportsSensorAccelerometer(int32_t playernum);
dboolean I_ControllerSupportsSensorGyro(int32_t playernum);

void I_ControllerRumble(int32_t playernum, uint16_t low_strength, uint16_t high_strength, uint32_t duration);
void I_SetControllerIndicatorColor(int32_t playernum, uint8_t red, uint8_t green, uint8_t blue);
void I_SetControllerIndicatorMonochrome(int32_t playernum, uint8_t brightness);
void I_SetControllerPlayerNumber(int32_t playernum, int32_t number);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __I_JOY_H__
