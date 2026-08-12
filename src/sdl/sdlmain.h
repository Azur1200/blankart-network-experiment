// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Copyright (C) 2006-2020 by Sonic Team Junior.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//-----------------------------------------------------------------------------
/// \file
/// \brief System specific interface stuff.

#ifndef __sdlmain__
#define __sdlmain__

extern bool consolevent;
extern bool framebuffer;

#include "../m_fixed.h"
#include "../doomdef.h"

#ifdef __cplusplus
extern "C" {
#endif

// SDL3 stub macro
#define SDL3STUB() CONS_Printf("SDL3: stubbed: %s:%d\n", __func__, __LINE__)


// So m_menu knows whether to store cv_usecontroller value or string
#define JOYSTICK_HOTPLUG

/**	\brief	The JoyInfo_s struct

  info about joystick
*/
typedef struct SDLJoyInfo_s
{
	/// Gamepad handle
	SDL_Gamepad *dev;
	/// Joystick handle
	//SDL_Joystick *joydev;
	/// Controller index
	SDL_JoystickID id;
	/// name of controller
	//const char *name;
	/// scale of axises
	int32_t scale;

	// Rumble Support
	dboolean hasrumble;
	// Mono LED Support
	dboolean hasmonoled;
	// RGB LED Support
	dboolean hasrgbled;
	// Player LED Support
	dboolean hasplayerled;

	// Accelerometer Support
	dboolean hasaccelerometer;
	// Gyroscope Support
	dboolean hasgyro;

	// you can pry my nintendo button layout out of my cold dead hands, SDL3
	dboolean flipbuttons;
	// >swapping X and B
	// ?????????????????????????????
	dboolean flipgc;
} controllerinfo_t;

/**	\brief SDL info about controllers
*/
extern controllerinfo_t controllerInfo[MAXSPLITSCREENPLAYERS];

void I_ShutdownControllerSystem(void);

// So we can call this from i_video event loop
void I_ShutdownController(uint8_t index);

// Cheat to get the device index for a game controller handle
int32_t I_GetControllerID(SDL_Gamepad *dev);

void I_UpdateControllerList(void);


void I_GetConsoleEvents(void);

void SDLforceUngrabMouse(void);

// Needed for some WIN32 functions
extern SDL_Window *window;

#ifdef __cplusplus
} // extern "C"
#endif

#endif
