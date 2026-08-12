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
/// \file  i_video.h
/// \brief System specific interface stuff.

#ifndef __I_VIDEO__
#define __I_VIDEO__

#include "doomtype.h"

#ifdef __cplusplus
extern "C" {
#endif

dboolean I_UseNativeKeyboard(void);
void I_SetTextInput(dboolean enable);

typedef enum
{
	/// Software
	render_soft = 1,

	/// OpenGL
	render_opengl = 2,

	/// Dedicated
	render_none = 3  // for dedicated server
} rendermode_t;

/**	\brief current render mode
*/
extern rendermode_t rendermode;

/**	\brief render mode set by command line arguments
*/
extern rendermode_t chosenrendermode;

/**	\brief use highcolor modes if true
*/
extern dboolean highcolor;

/**	\brief setup video mode
*/
void I_StartupGraphics(void);

/**	\brief shutdown video mode
*/
void I_ShutdownGraphics(void);

/**	\brief	The I_SetPalette function

	\param	palette	Takes full 8 bit values

	\return	void
*/
void I_SetPalette(RGBA_t *palette);

/**	\brief return the number of video modes
*/
int32_t VID_NumModes(void);

/**	\brief	The VID_GetModeForSize function

	\param	w	width
	\param	h	height

	\return	vidmode closest to w : h
*/
int32_t VID_GetModeForSize(int32_t w, int32_t h);


/**	\brief	The VID_SetMode function

	Set the video mode right now,
	the video mode change is delayed until the start of the next refresh
	by setting the setmodeneeded to a value >0
	setup a video mode, this is to be called from the menu


	\param	modenum	video mode to set to

	\return	current video mode
*/
int32_t VID_SetMode(int32_t modenum);

/**	\brief Checks the render state
	\return	true if the renderer changed
*/
dboolean VID_CheckRenderer(void);

/**	\brief Load OpenGL mode
*/
void VID_StartupOpenGL(void);

/**	\brief Checks if OpenGL loaded
*/
void VID_CheckGLLoaded(rendermode_t oldrender);

/**	\brief	The VID_GetModeName function

	\param	modenum	video mode number

	\return	name of video mode
*/
const char *VID_GetModeName(int32_t modenum);

void VID_PrepareModeList(void);

/**	\brief can video system do fullscreen
*/
extern dboolean allow_fullscreen;

/**	\brief Update video system without updating frame
*/
void I_UpdateNoBlit(void);

/**	\brief Update video system with updating frame
*/
void I_FinishUpdate(void);

/**	\brief I_FinishUpdate(), but vsync disabled
*/
void I_UpdateNoVsync(void);

/**	\brief	Wait for vertical retrace or pause a bit.

	\param	count	max wait

	\return	void
*/
void I_WaitVBL(int32_t count);

/**	\brief	The I_ReadScreen function

	\param	scr	buffer to copy screen to

	\return	void
*/
void I_ReadScreen(uint8_t * restrict scr, int32_t scale);

/**	\brief Start disk icon
*/
void I_BeginRead(void);

/**	\brief Stop disk icon
*/
void I_EndRead(void);

uint32_t I_GetRefreshRate(void);

extern consvar_t cv_keyboardlayout;

void I_SetBorderlessWindow(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
