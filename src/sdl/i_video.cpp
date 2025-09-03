// Emacs style mode select   -*- C++ -*-
// BLANKART
//-----------------------------------------------------------------------------
//
// Copyright (C) 1993-1996 by id Software, Inc.
// Portions Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 2014-2020 by Sonic Team Junior.
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
/// \file i_video.cpp
/// \brief SRB2 graphics stuff for SDL

#include <stdlib.h>
#include <errno.h>

#include <signal.h>

#ifdef _MSC_VER
#pragma warning(disable : 4214 4244)
#endif

#ifdef HAVE_SDL
#define _MATH_DEFINES_DEFINED
#include <SDL.h>

#ifdef _MSC_VER
#include <windows.h>
#pragma warning(default : 4214 4244)
#endif

#ifdef HAVE_TTF
#include "i_ttf.h"
#endif

#ifdef HAVE_IMAGE
#include "SDL_image.h"
#elif defined (__unix__) || (!defined(__APPLE__) && defined (UNIXCOMMON)) // Windows & Mac don't need this, as SDL will do it for us.
#define LOAD_XPM //I want XPM!
#include "IMG_xpm.c" //Alam: I don't want to add SDL_Image.dll/so
#define HAVE_IMAGE //I have SDL_Image, sortof
#endif

#ifdef HAVE_IMAGE
#include "SDL_icon.xpm"
#endif

#include "../doomdef.h"

#ifdef _WIN32
#include "SDL_syswm.h"
#endif

#include "../doomstat.h"
#include "../p_setup.h"
#include "../i_system.h"
#include "../v_video.h"
#include "../m_argv.h"
#include "../m_menu.h"
#include "../d_main.h"
#include "../s_sound.h"
#include "../i_sound.h"  	// midi pause/unpause
#include "../i_joy.h"
#include "../st_stuff.h"
#include "../hu_stuff.h"
#include "../g_game.h"
#include "../i_video.h"
#include "../console.h"
#include "../command.h"
#include "../r_main.h"
#include "../lua_hook.h"
#include "sdlmain.h"
#include "../i_system.h"
#ifdef HWRENDER
#include "../hardware/hw_main.h"
#include "../hardware/hw_drv.h"
#include "hwsym_sdl.h"
#include "ogl_sdl.h"
#endif

#ifdef HAVE_DISCORDRPC
#include "../discord.h"
#endif

// maximum number of windowed modes (see windowedModes[][])
#define MAXWINMODES (22)

rendermode_t rendermode = render_soft;
rendermode_t chosenrendermode = render_none; // set by command line arguments

boolean highcolor = false;


// synchronize page flipping with screen refresh
consvar_t cv_vidwait = CVAR_INIT ("vid_wait", "On", CV_SAVE, CV_OnOff, NULL);
static consvar_t cv_stretch = CVAR_INIT ("stretch", "Off", CV_SAVE|CV_NOSHOWHELP, CV_OnOff, NULL);
static consvar_t cv_alwaysgrabmouse = CVAR_INIT ("alwaysgrabmouse", "Off", CV_SAVE, CV_OnOff, NULL);

UINT8 graphics_started = 0; // Is used in console.c and screen.c

// To disable fullscreen at startup; is set in VID_PrepareModeList
boolean allow_fullscreen = false;
static SDL_bool disable_fullscreen = SDL_FALSE;

#define USE_FULLSCREEN (disable_fullscreen||!allow_fullscreen)?static_cast<SDL_bool>(0):static_cast<SDL_bool>(cv_fullscreen.value == 1)

static SDL_bool disable_mouse = SDL_FALSE;
#define USE_MOUSEINPUT (!disable_mouse && cv_usemouse.value && havefocus)
#define MOUSE_MENU false //(!disable_mouse && cv_usemouse.value && menustack[0] && !USE_FULLSCREEN)
#define MOUSEBUTTONS_MAX MOUSEBUTTONS

// Total mouse motion X/Y offsets
static      INT32        mousemovex = 0, mousemovey = 0;

// SDL vars
static      UINT32    localPalette[256];
static       SDL_bool    wrapmouseok = SDL_FALSE;
#define HalfWarpMouse(x,y) if (wrapmouseok) SDL_WarpMouseInWindow(window, (Uint16)(x/2),(Uint16)(y/2))
static       SDL_bool    usesdl2soft = SDL_FALSE;
static       SDL_bool    borderlesswindow = SDL_FALSE;

Uint16      realwidth = BASEVIDWIDTH;
Uint16      realheight = BASEVIDHEIGHT;

SDL_Window   *window;
SDL_Renderer *renderer;
static SDL_Texture  *texture;
static SDL_bool      havefocus = SDL_TRUE;

static UINT32 refresh_rate;

static boolean video_init = false;

static SDL_bool Impl_CreateWindow(SDL_bool fullscreen);

static void Impl_VideoSetupSurfaces(int width, int height);

static void Impl_SetupSoftwareBuffer(void);

static void Impl_InitOpenGL(void);

#if defined(HAVE_IMAGE)
#define USE_WINDOW_ICON
#endif

#ifdef USE_WINDOW_ICON
static void Impl_SetWindowIcon(void);
static SDL_Surface *icoSurface = NULL;
#endif

// windowed video modes from which to choose from.
static INT32 windowedModes[MAXWINMODES][2] =
{
	{2560,1440}, // 1.66
	{1920,1200}, // 1.60,6.00
	{1920,1080}, // 1.66
	{1680,1050}, // 1.60,5.25
	{1600,1200}, // 1.33
	{1600, 900}, // 1.66
	{1366, 768}, // 1.66
	{1440, 900}, // 1.60,4.50
	{1280,1024}, // 1.33?
	{1280, 960}, // 1.33,4.00
	{1280, 800}, // 1.60,4.00
	{1280, 720}, // 1.66
	{1152, 864}, // 1.33,3.60
	{1024, 768}, // 1.33,3.20
	{ 960, 600}, // 1.33,3.20
	{ 800, 600}, // 1.33,2.50
	{ 735, 415}, // 1.33,2.50
	{ 640, 480}, // 1.33,2.00
	{ 640, 400}, // 1.60,2.00
	{ 500, 300}, // 1.33
	{ 320, 240}, // 1.33,1.00
	{ 320, 200}, // 1.60,1.00
};

#define CUSTOMMODENUM 9999
static INT32 custom_width = 0;
static INT32 custom_height = 0;

static char vidModeName[MAXWINMODES][32];
static const char *fallback_resolution_name = "Fallback";

#define VIDEO_INIT_ERROR(str) { \
	if (!graphics_started) \
		I_Error(str, SDL_GetError()); \
	else \
		CONS_Printf(str "\n", SDL_GetError()); \
}

static SDL_bool Impl_RenderContextCreate(void)
{
	if (rendermode != render_opengl)
	{
		int flags = 0; // Use this to set SDL_RENDERER_* flags now

		if (usesdl2soft)
			flags |= SDL_RENDERER_SOFTWARE;
		else if (cv_vidwait.value)
		{
#if SDL_VERSION_ATLEAST(2, 0, 18)
			// If SDL is new enough, we can turn off vsync later.
			flags |= SDL_RENDERER_PRESENTVSYNC;
#else
			// However, if it isn't, we should just silently turn vid_wait off
			// This is because the renderer will be created before the config
			// is read and vid_wait is set from the user's preferences, and thus
			// vid_wait will have no effect.
			CV_StealthSetValue(&cv_vidwait, 0);
#endif
		}

		SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");

		if (!renderer)
			renderer = SDL_CreateRenderer(window, -1, flags);

		if (renderer == NULL)
		{
			VIDEO_INIT_ERROR("Couldn't create rendering context: %s");
			return SDL_FALSE;
		}
	}

#ifdef HWRENDER
	if (rendermode == render_opengl && vid.glstate != VID_GL_LIBRARY_ERROR)
	{
		if (sdlglcontext == NULL)
		{
			sdlglcontext = SDL_GL_CreateContext(window);

			if (sdlglcontext == NULL)
			{
				VIDEO_INIT_ERROR("Couldn't create OpenGL context: %s");
				return SDL_FALSE;
			}
		}
	}
#endif

	return SDL_TRUE;
}

static SDL_bool Impl_RenderContextReset(void)
{
	if (renderer)
	{
		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindowSurface(window); // workaround for a bug in sdl
		texture = NULL; // Destroying a renderer also destroys all of its textures
	}
	renderer = NULL;

	if (Impl_RenderContextCreate() == SDL_FALSE)
		return SDL_FALSE;

#ifdef HWRENDER
	if (rendermode == render_opengl)
	{
		SDL_GL_MakeCurrent(window, sdlglcontext);
		SDL_GL_SetSwapInterval(cv_vidwait.value ? 1 : 0);

		OglSdlSurface(realwidth, realheight);
		HWR_Startup();
	}
	else
#endif
	{
		SDL_RenderClear(renderer);
		SDL_RenderSetLogicalSize(renderer, realwidth, realheight);
		Impl_VideoSetupSurfaces(realwidth, realheight);
	}

	return SDL_TRUE;
}

static void Impl_SetSoftwareVsync(int vsync)
{
	static int oldvsync = 0;
#if SDL_VERSION_ATLEAST(2,0,18)
	if (oldvsync != vsync)
	{
		SDL_RenderSetVSync(renderer, vsync);
	}
	oldvsync = vsync;
#endif
}

static void Impl_VideoSetupSurfaces(int width, int height)
{
	int sw_texture_format = SDL_PIXELFORMAT_ABGR8888;

	if (texture == NULL)
		texture = SDL_CreateTexture(renderer, sw_texture_format, SDL_TEXTUREACCESS_STREAMING, width, height);
}

static void Impl_SetupSoftwareBuffer(void)
{
	// Set up game's software render buffer
	size_t size;

	vid.rowbytes = vid.width * vid.bpp;
	vid.direct = NULL;

	free(vid.buffer);

	size = vid.rowbytes*vid.height * NUMSCREENS;
	vid.buffer = static_cast<UINT8*>(malloc(size));

	if (vid.buffer)
	{
		// Clear the buffer
		// HACK: Wasn't sure where else to put this.
		memset(vid.buffer, 31, size);
	}
	else
		I_Error("%s", M_GetText("Not enough memory for video buffer\n"));
}

static SDL_Rect src_rect = { 0, 0, 0, 0 };

static SDL_bool SDLSetMode(INT32 width, INT32 height, SDL_bool fullscreen, SDL_bool reposition)
{
	static SDL_bool wasfullscreen = SDL_FALSE;
	int fullscreen_type = SDL_WINDOW_FULLSCREEN_DESKTOP;

	src_rect.w = realwidth = width;
	src_rect.h = realheight = height;

	if (window)
	{
		if (fullscreen)
		{
			wasfullscreen = SDL_TRUE;
			SDL_SetWindowFullscreen(window, fullscreen_type);
			I_SetBorderlessWindow();
		}
		else // windowed mode
		{
			if (wasfullscreen)
			{
				wasfullscreen = SDL_FALSE;
				SDL_SetWindowFullscreen(window, 0);
			}
			// Reposition window only in windowed mode

			SDL_SetWindowSize(window, width, height);

			if (reposition)
			{
				// Reposition window only in windowed mode
				SDL_SetWindowPosition(window,
					SDL_WINDOWPOS_CENTERED_DISPLAY(SDL_GetWindowDisplayIndex(window)),
					SDL_WINDOWPOS_CENTERED_DISPLAY(SDL_GetWindowDisplayIndex(window))
				);
			}
		}
	}
	else
	{
		if (Impl_CreateWindow(fullscreen) == SDL_FALSE)
			return SDL_FALSE;

		wasfullscreen = fullscreen;
		SDL_SetWindowSize(window, width, height);
		if (fullscreen)
			SDL_SetWindowFullscreen(window, fullscreen_type);
	}

	if (Impl_RenderContextReset() == SDL_FALSE)
		I_Error("Couldn't create or reset rendering context");

	if (vid.buffer)
	{
		free(vid.buffer);
		vid.buffer = NULL;
	}

	return SDL_TRUE;
}

static UINT32 VID_GetRefreshRate(void)
{
	int index = SDL_GetWindowDisplayIndex(window);
	SDL_DisplayMode m;

	if (SDL_WasInit(SDL_INIT_VIDEO) == 0)
	{
		// Video not init yet.
		return 0;
	}

	if (SDL_GetDesktopDisplayMode(index, &m) != 0)
	{
		// Error has occurred.
		return 0;
	}

	return m.refresh_rate;
}

static INT32 Impl_SDL_Scancode_To_Keycode(SDL_Scancode code)
{
	if (code >= SDL_SCANCODE_A && code <= SDL_SCANCODE_Z)
	{
		// get lowercase ASCII
		return code - SDL_SCANCODE_A + 'a';
	}
	if (code >= SDL_SCANCODE_1 && code <= SDL_SCANCODE_9)
	{
		return code - SDL_SCANCODE_1 + '1';
	}
	else if (code == SDL_SCANCODE_0)
	{
		return '0';
	}
	if (code >= SDL_SCANCODE_F1 && code <= SDL_SCANCODE_F10)
	{
		return KEY_F1 + (code - SDL_SCANCODE_F1);
	}
	switch (code)
	{
		// F11 and F12 are separated from the rest of the function keys
		case SDL_SCANCODE_F11: return KEY_F11;
		case SDL_SCANCODE_F12: return KEY_F12;

		case SDL_SCANCODE_KP_0: return KEY_KEYPAD0;
		case SDL_SCANCODE_KP_1: return KEY_KEYPAD1;
		case SDL_SCANCODE_KP_2: return KEY_KEYPAD2;
		case SDL_SCANCODE_KP_3: return KEY_KEYPAD3;
		case SDL_SCANCODE_KP_4: return KEY_KEYPAD4;
		case SDL_SCANCODE_KP_5: return KEY_KEYPAD5;
		case SDL_SCANCODE_KP_6: return KEY_KEYPAD6;
		case SDL_SCANCODE_KP_7: return KEY_KEYPAD7;
		case SDL_SCANCODE_KP_8: return KEY_KEYPAD8;
		case SDL_SCANCODE_KP_9: return KEY_KEYPAD9;

		case SDL_SCANCODE_RETURN:         return KEY_ENTER;
		case SDL_SCANCODE_ESCAPE:         return KEY_ESCAPE;
		case SDL_SCANCODE_BACKSPACE:      return KEY_BACKSPACE;
		case SDL_SCANCODE_TAB:            return KEY_TAB;
		case SDL_SCANCODE_SPACE:          return KEY_SPACE;
		case SDL_SCANCODE_MINUS:          return KEY_MINUS;
		case SDL_SCANCODE_EQUALS:         return KEY_EQUALS;
		case SDL_SCANCODE_LEFTBRACKET:    return '[';
		case SDL_SCANCODE_RIGHTBRACKET:   return ']';
		case SDL_SCANCODE_BACKSLASH:      return '\\';
		case SDL_SCANCODE_NONUSHASH:      return '#';
		case SDL_SCANCODE_SEMICOLON:      return ';';
		case SDL_SCANCODE_APOSTROPHE:     return '\'';
		case SDL_SCANCODE_GRAVE:          return '`';
		case SDL_SCANCODE_COMMA:          return ',';
		case SDL_SCANCODE_PERIOD:         return '.';
		case SDL_SCANCODE_SLASH:          return '/';
		case SDL_SCANCODE_CAPSLOCK:       return KEY_CAPSLOCK;
		case SDL_SCANCODE_PRINTSCREEN:    return 0; // undefined?
		case SDL_SCANCODE_SCROLLLOCK:     return KEY_SCROLLLOCK;
		case SDL_SCANCODE_PAUSE:          return KEY_PAUSE;
		case SDL_SCANCODE_INSERT:         return KEY_INS;
		case SDL_SCANCODE_HOME:           return KEY_HOME;
		case SDL_SCANCODE_PAGEUP:         return KEY_PGUP;
		case SDL_SCANCODE_DELETE:         return KEY_DEL;
		case SDL_SCANCODE_END:            return KEY_END;
		case SDL_SCANCODE_PAGEDOWN:       return KEY_PGDN;
		case SDL_SCANCODE_RIGHT:          return KEY_RIGHTARROW;
		case SDL_SCANCODE_LEFT:           return KEY_LEFTARROW;
		case SDL_SCANCODE_DOWN:           return KEY_DOWNARROW;
		case SDL_SCANCODE_UP:             return KEY_UPARROW;
		case SDL_SCANCODE_NUMLOCKCLEAR:   return KEY_NUMLOCK;
		case SDL_SCANCODE_KP_DIVIDE:      return KEY_KPADSLASH;
		case SDL_SCANCODE_KP_MULTIPLY:    return '*'; // undefined?
		case SDL_SCANCODE_KP_MINUS:       return KEY_MINUSPAD;
		case SDL_SCANCODE_KP_PLUS:        return KEY_PLUSPAD;
		case SDL_SCANCODE_KP_ENTER:       return KEY_ENTER;
		case SDL_SCANCODE_KP_PERIOD:      return KEY_KPADDEL;
		case SDL_SCANCODE_NONUSBACKSLASH: return '\\';

		case SDL_SCANCODE_LSHIFT: return KEY_LSHIFT;
		case SDL_SCANCODE_RSHIFT: return KEY_RSHIFT;
		case SDL_SCANCODE_LCTRL:  return KEY_LCTRL;
		case SDL_SCANCODE_RCTRL:  return KEY_RCTRL;
		case SDL_SCANCODE_LALT:   return KEY_LALT;
		case SDL_SCANCODE_RALT:   return KEY_RALT;
		case SDL_SCANCODE_LGUI:   return KEY_LEFTWIN;
		case SDL_SCANCODE_RGUI:   return KEY_RIGHTWIN;

		default:                  break;
	}
	return 0;
}

static boolean IgnoreMouse(void)
{
	if (cv_alwaysgrabmouse.value)
		return false;
	if (menustack[0])
		return !M_MouseNeeded();
	if (paused || con_destlines || chat_on)
		return true;
	if (gamestate != GS_LEVEL && gamestate != GS_INTERMISSION &&
			gamestate != GS_CUTSCENE)
		return true;
	return false;
}

static void SDLdoGrabMouse(void)
{
	SDL_ShowCursor(SDL_DISABLE);
	SDL_SetWindowGrab(window, SDL_TRUE);
	if (SDL_SetRelativeMouseMode(SDL_TRUE) == 0) // already warps mouse if successful
		wrapmouseok = SDL_TRUE; // TODO: is wrapmouseok or HalfWarpMouse needed anymore?
}

static void SDLdoUngrabMouse(void)
{
	SDL_ShowCursor(SDL_ENABLE);
	SDL_SetWindowGrab(window, SDL_FALSE);
	wrapmouseok = SDL_FALSE;
	SDL_SetRelativeMouseMode(SDL_FALSE);
}

void SDLforceUngrabMouse(void)
{
	if (SDL_WasInit(SDL_INIT_VIDEO)==SDL_INIT_VIDEO && window != NULL)
		SDLdoUngrabMouse();
}

void I_UpdateMouseGrab(void)
{
	if (SDL_WasInit(SDL_INIT_VIDEO) == SDL_INIT_VIDEO && window != NULL
	&& SDL_GetMouseFocus() == window && SDL_GetKeyboardFocus() == window
	&& USE_MOUSEINPUT && !IgnoreMouse())
		SDLdoGrabMouse();
}

static void VID_Command_NumModes_f (void)
{
	CONS_Printf(M_GetText("%d video mode(s) available(s)\n"), VID_NumModes());
}

static void VID_Command_ModeList_f(void)
{
	for (INT32 i = 0; i < MAXWINMODES; i++)
		CONS_Printf("%2d: %dx%d\n", i, windowedModes[i][0], windowedModes[i][1]);
}

static void VID_Command_Mode_f (void)
{
	INT32 modenum;

	if (COM_Argc()!= 2)
	{
		CONS_Printf(M_GetText("vid_mode <modenum> : set video mode, current video mode %i\n"), vid.modenum);
		return;
	}

	modenum = atoi(COM_Argv(1));

	if (modenum >= VID_NumModes())
		CONS_Printf(M_GetText("Video mode not present\n"));
	else
	{
		setmodeneeded = modenum + 1; // request vid mode change
	}
}

static void Impl_SetFocused(boolean focused)
{
	window_notinfocus = !focused;

	if (window_notinfocus)
	{
		if (! cv_playmusicifunfocused.value)
			I_SetMusicVolume(0);
		if (! cv_playsoundifunfocused.value)
			S_DisableSound();

		memset(gamekeydown, 0, NUMKEYS); // TODO this is a scary memset
	}
	else if (!paused)
	{
		S_InitMusicVolume();
		S_EnableSound();
	}
}

static inline void SDLJoyRemap(event_t *event)
{
	(void)event;
}

static INT32 SDLJoyAxis(const Sint16 axis, UINT8 pid)
{
	// -32768 to 32767
	INT32 raxis = axis / 32;

	if (Joystick[pid].bGamepadStyle)
	{
		// gamepad control type, on or off, live or die
		if (raxis < -(JOYAXISRANGE/2))
			raxis = -1;
		else if (raxis > (JOYAXISRANGE/2))
			raxis = 1;
		else
			raxis = 0;
	}
	else
	{
		raxis = (abs(JoyInfo[pid].scale) > 1) ? ((raxis / JoyInfo[pid].scale) * JoyInfo[pid].scale) : raxis;

#ifdef SDL_JDEADZONE
		if (-SDL_JDEADZONE <= raxis && raxis <= SDL_JDEADZONE)
			raxis = 0;
#endif
	}

	return raxis;
}

static void Impl_HandleWindowEvent(SDL_WindowEvent evt)
{
	static SDL_bool firsttimeonmouse = SDL_TRUE;
	static SDL_bool mousefocus = SDL_TRUE;
	static SDL_bool kbfocus = SDL_TRUE;

	switch (evt.event)
	{
		case SDL_WINDOWEVENT_ENTER:
			mousefocus = SDL_TRUE;
			break;
		case SDL_WINDOWEVENT_LEAVE:
			mousefocus = SDL_FALSE;
			break;
		case SDL_WINDOWEVENT_FOCUS_GAINED:
			kbfocus = SDL_TRUE;
			mousefocus = SDL_TRUE;
			break;
		case SDL_WINDOWEVENT_FOCUS_LOST:
			kbfocus = SDL_FALSE;
			mousefocus = SDL_FALSE;
			break;
		case SDL_WINDOWEVENT_MAXIMIZED:
			break;
		case SDL_WINDOWEVENT_SIZE_CHANGED:
			break;
	}

	if (mousefocus && kbfocus)
	{
		// Tell game we got focus back, resume music if necessary
		Impl_SetFocused(true);

		if (!firsttimeonmouse && cv_usemouse.value)
			I_StartupMouse();

		if (USE_MOUSEINPUT && !IgnoreMouse())
			SDLdoGrabMouse();
	}
	else if (!mousefocus && !kbfocus)
	{
		// Tell game we lost focus, pause music
		Impl_SetFocused(false);

		if (!disable_mouse)
			SDLforceUngrabMouse();

		if (MOUSE_MENU)
			SDLdoUngrabMouse();
	}
}

static void Impl_HandleKeyboardEvent(SDL_KeyboardEvent evt, Uint32 type)
{
	event_t event;

	event.device = 0;

	if (type == SDL_KEYUP)
	{
		event.type = ev_keyup;
	}
	else if (type == SDL_KEYDOWN)
	{
		event.type = ev_keydown;
	}
	else
	{
		return;
	}
	event.data1 = Impl_SDL_Scancode_To_Keycode(evt.keysym.scancode);

	if (event.data1) D_PostEvent(&event);
}

static void Impl_HandleMouseMotionEvent(SDL_MouseMotionEvent evt)
{
	static boolean firstmove = true;

	if (USE_MOUSEINPUT)
	{
		if ((SDL_GetMouseFocus() != window && SDL_GetKeyboardFocus() != window) || (IgnoreMouse() && !firstmove))
		{
			SDLdoUngrabMouse();
			firstmove = false;
			return;
		}

		// If using relative mouse mode, don't post an event_t just now,
		// add on the offsets so we can make an overall event later.
		if (SDL_GetRelativeMouseMode())
		{
			if (SDL_GetMouseFocus() == window && SDL_GetKeyboardFocus() == window)
			{
				mousemovex +=  evt.xrel;
				mousemovey += -evt.yrel;
				SDL_SetWindowGrab(window, SDL_TRUE);
			}
			firstmove = false;
			return;
		}

		// If the event is from warping the pointer to middle
		// of the screen then ignore it.
		if ((evt.x == realwidth/2) && (evt.y == realheight/2))
		{
			firstmove = false;
			return;
		}

		// Don't send an event_t if not in relative mouse mode anymore,
		// just grab and set relative mode
		// this fixes the stupid camera jerk on mouse entering bug
		// -- Monster Iestyn
		if (SDL_GetMouseFocus() == window && SDL_GetKeyboardFocus() == window)
		{
			SDLdoGrabMouse();
		}
	}

	firstmove = false;
}

static void Impl_HandleMouseButtonEvent(SDL_MouseButtonEvent evt, Uint32 type)
{
	event_t event;

	SDL_memset(&event, 0, sizeof(event_t));

	// Ignore the event if the mouse is not actually focused on the window.
	// This can happen if you used the mouse to restore keyboard focus;
	// this apparently makes a mouse button down event but not a mouse button up event,
	// resulting in whatever key was pressed down getting "stuck" if we don't ignore it.
	// -- Monster Iestyn (28/05/18)
	if (SDL_GetMouseFocus() != window || IgnoreMouse())
		return;

	/// \todo inputEvent.button.which
	if (USE_MOUSEINPUT)
	{
		event.device = 0;

		if (type == SDL_MOUSEBUTTONUP)
		{
			event.type = ev_keyup;
		}
		else if (type == SDL_MOUSEBUTTONDOWN)
		{
			event.type = ev_keydown;
		}
		else return;
		if (evt.button == SDL_BUTTON_MIDDLE)
			event.data1 = KEY_MOUSE1+2;
		else if (evt.button == SDL_BUTTON_RIGHT)
			event.data1 = KEY_MOUSE1+1;
		else if (evt.button == SDL_BUTTON_LEFT)
			event.data1 = KEY_MOUSE1;
		else if (evt.button == SDL_BUTTON_X1)
			event.data1 = KEY_MOUSE1+3;
		else if (evt.button == SDL_BUTTON_X2)
			event.data1 = KEY_MOUSE1+4;
		if (event.type == ev_keyup || event.type == ev_keydown)
		{
			D_PostEvent(&event);
		}
	}
}

static void Impl_HandleMouseWheelEvent(SDL_MouseWheelEvent evt)
{
	event_t event;

	SDL_memset(&event, 0, sizeof(event_t));

	event.device = 0;

	if (evt.y > 0)
	{
		event.data1 = KEY_MOUSEWHEELUP;
		event.type = ev_keydown;
	}
	if (evt.y < 0)
	{
		event.data1 = KEY_MOUSEWHEELDOWN;
		event.type = ev_keydown;
	}
	if (evt.y == 0)
	{
		event.data1 = 0;
		event.type = ev_keyup;
	}
	if (event.type == ev_keyup || event.type == ev_keydown)
	{
		D_PostEvent(&event);
	}
}

static void Impl_HandleControllerAxisEvent(SDL_ControllerAxisEvent evt)
{
	event_t event;

	event.type = ev_joystick;

	event.device = 1 + evt.which;
	if (event.device == INT32_MAX)
	{
		return;
	}

	event.data1 = event.data2 = event.data3 = INT32_MAX;

	//axis
	switch (evt.axis)
	{
		case SDL_CONTROLLER_AXIS_LEFTX:
			event.data1 = 0;
			break;
		case SDL_CONTROLLER_AXIS_LEFTY:
			event.data1 = 1;
			break;
		case SDL_CONTROLLER_AXIS_RIGHTX:
			event.data1 = 2;
			break;
		case SDL_CONTROLLER_AXIS_RIGHTY:
			event.data1 = 3;
			break;
		case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
			event.data1 = 4;
			break;
		case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
			event.data1 = 5;
			break;
		default:
			return;
	}

	//value
	event.data2 = SDLJoyAxis(evt.value, evt.which);

	D_PostEvent(&event);
}

static void Impl_HandleControllerButtonEvent(SDL_ControllerButtonEvent evt, Uint32 type)
{
	event_t event;

	event.device = 1 + evt.which;

	event.data1 = event.data2 = event.data3 = INT32_MAX;

	if (event.device == INT32_MAX)
	{
		return;
	}

	if (type == SDL_CONTROLLERBUTTONUP)
	{
		event.type = ev_keyup;
	}
	else if (type == SDL_CONTROLLERBUTTONDOWN)
	{
		event.type = ev_keydown;
	}
	else
	{
		return;
	}

	if (evt.button < JOYBUTTONS)
	{
		event.data1 = KEY_JOY1 + evt.button;
	}
	else
	{
		return;
	}

	SDLJoyRemap(&event);

	if (event.type != ev_console)
	{
		D_PostEvent(&event);
	}
}

static void Impl_HandleVideoEvent(SDL_Event *evt)
{
	switch (evt->type)
	{
		case SDL_WINDOWEVENT:
			Impl_HandleWindowEvent(evt->window);
			break;
		default:
			break;
	}
}

void I_GetEvent(void)
{
	SDL_Event evt;
	char* dropped_filedir;

	UINT8 i;

	mousemovex = mousemovey = 0;

	while (SDL_PollEvent(&evt))
	{
		switch (evt.type)
		{
			default:
				Impl_HandleVideoEvent(&evt);
				break;
			// TODO: Move input code out of this file, desperately
			case SDL_KEYUP:
			case SDL_KEYDOWN:
				Impl_HandleKeyboardEvent(evt.key, evt.type);
				break;
			case SDL_MOUSEMOTION:
				//if (!mouseMotionOnce)
				Impl_HandleMouseMotionEvent(evt.motion);
				//mouseMotionOnce = 1;
				break;
			case SDL_MOUSEBUTTONUP:
			case SDL_MOUSEBUTTONDOWN:
				Impl_HandleMouseButtonEvent(evt.button, evt.type);
				break;
			case SDL_MOUSEWHEEL:
				Impl_HandleMouseWheelEvent(evt.wheel);
				break;
			case SDL_CONTROLLERAXISMOTION:
				Impl_HandleControllerAxisEvent(evt.caxis);
				break;
			case SDL_CONTROLLERBUTTONUP:
			case SDL_CONTROLLERBUTTONDOWN:
				Impl_HandleControllerButtonEvent(evt.cbutton, evt.type);
				break;


			////////////////////////////////////////////////////////////

			case SDL_CONTROLLERDEVICEADDED:
				{
					// OH BOY are you in for a good time! #abominationstation

					SDL_GameController *newcontroller = SDL_GameControllerOpen(evt.cdevice.which);

					CONS_Debug(DBG_GAMELOGIC, "Controller device index %d added\n", evt.cdevice.which + 1);

					////////////////////////////////////////////////////////////
					// Because SDL's device index is unstable, we're going to cheat here a bit:
					// For the first joystick setting that is NOT active:
					//
					// 1. Set cv_usejoystickX.value to the new device index (this does not change what is written to config.cfg)
					//
					// 2. Set OTHERS' cv_usejoystickX.value to THEIR new device index, because it likely changed
					//    * If device doesn't exist, switch cv_usejoystick back to default value (.string)
					//      * BUT: If that default index is being occupied, use ANOTHER cv_usejoystick's default value!
					////////////////////////////////////////////////////////////

					for (i = 0; i < MAXSPLITSCREENPLAYERS; i++)
					{
						if (newcontroller && (!JoyInfo[i].dev || !SDL_GameControllerGetAttached(JoyInfo[i].dev)))
						{
							UINT8 j;

							for (j = 0; j < MAXSPLITSCREENPLAYERS; j++)
							{
								if (i == j)
									continue;

								if (JoyInfo[j].dev == newcontroller)
									break;
							}

							if (j == MAXSPLITSCREENPLAYERS)
							{
								// ensures we aren't overriding a currently active device
								cv_usejoystick[i].value = evt.cdevice.which + 1;
								I_UpdateJoystickDeviceIndices(0);
							}
						}
					}

					////////////////////////////////////////////////////////////
					// Was cv_usejoystick disabled in settings?
					////////////////////////////////////////////////////////////

					for (i = 0; i < MAXSPLITSCREENPLAYERS; i++)
					{
						if (!strcmp(cv_usejoystick[i].string, "0") || !cv_usejoystick[i].value)
							cv_usejoystick[i].value = 0;
						else if (atoi(cv_usejoystick[i].string) <= I_NumJoys() // don't mess if we intentionally set higher than NumJoys
								 && cv_usejoystick[i].value) // update the cvar ONLY if a device exists
							CV_SetValue(&cv_usejoystick[i], cv_usejoystick[i].value);
					}

					////////////////////////////////////////////////////////////
					// Update all joysticks' init states
					// This is a little wasteful since cv_usejoystick already calls this, but
					// we need to do this in case CV_SetValue did nothing because the string was already same.
					// if the device is already active, this should do nothing, effectively.
					////////////////////////////////////////////////////////////

					for (i = 0; i < MAXSPLITSCREENPLAYERS; i++)
						I_InitJoystick(i);

					////////////////////////////////////////////////////////////

					for (i = 0; i < MAXSPLITSCREENPLAYERS; i++)
						CONS_Debug(DBG_GAMELOGIC, "Joystick%d device index: %d\n", i+1, JoyInfo[i].oldjoy);

					// update the menu
					if (menustack[0] == MN_OP_JOYSTICKSET)
						MR_SetupJoystickMenu(0);

					for (i = 0; i < MAXSPLITSCREENPLAYERS; i++)
					{
						if (JoyInfo[i].dev == newcontroller)
							break;
					}

					if (i == MAXSPLITSCREENPLAYERS)
						I_StoreExJoystick(newcontroller);
				}
				break;

			////////////////////////////////////////////////////////////

			case SDL_CONTROLLERDEVICEREMOVED:
				for (i = 0; i < MAXSPLITSCREENPLAYERS; i++)
				{
					if (JoyInfo[i].dev && !SDL_GameControllerGetAttached(JoyInfo[i].dev))
					{
						CONS_Debug(DBG_GAMELOGIC, "Joystick%d removed, device index: %d\n", i+1, JoyInfo[i].oldjoy);
						I_ShutdownJoystick(i);
					}
				}

				////////////////////////////////////////////////////////////
				// Update the device indexes, because they likely changed
				// * If device doesn't exist, switch cv_usejoystick back to default value (.string)
				//   * BUT: If that default index is being occupied, use ANOTHER cv_usejoystick's default value!
				////////////////////////////////////////////////////////////

				for (i = 0; i < MAXSPLITSCREENPLAYERS; i++)
				{
					I_UpdateJoystickDeviceIndex(i);
				}

				////////////////////////////////////////////////////////////
				// Was cv_usejoystick disabled in settings?
				////////////////////////////////////////////////////////////

				for (i = 0; i < MAXSPLITSCREENPLAYERS; i++)
				{
					if (!strcmp(cv_usejoystick[i].string, "0"))
					{
						cv_usejoystick[i].value = 0;
					}
					else if (atoi(cv_usejoystick[i].string) <= I_NumJoys() // don't mess if we intentionally set higher than NumJoys
						&& cv_usejoystick[i].value) // update the cvar ONLY if a device exists
					{
						CV_SetValue(&cv_usejoystick[i], cv_usejoystick[i].value);
					}
				}

				////////////////////////////////////////////////////////////

				for (i = 0; i < MAXSPLITSCREENPLAYERS; i++)
					CONS_Debug(DBG_GAMELOGIC, "Joystick%d device index: %d\n", i+1, JoyInfo[i].oldjoy);

				// update the menu
				if (menustack[0] == MN_OP_JOYSTICKSET)
					MR_SetupJoystickMenu(0);
				break;
			case SDL_DROPFILE:
				dropped_filedir = evt.drop.file;
				P_AddWadFile(dropped_filedir, WC_AUTO);
				SDL_free(dropped_filedir);    // Free dropped_filedir memory
				break;
			case SDL_QUIT:
				LUA_HookBool(true, HOOK(GameQuit));
				I_Quit();
				break;
		}
	}

	// Send all relative mouse movement as one single mouse event.
	if (mousemovex || mousemovey)
	{
		event_t event;
		int wwidth, wheight;
		SDL_GetWindowSize(window, &wwidth, &wheight);
		//SDL_memset(&event, 0, sizeof(event_t));
		event.type = ev_mouse;
		event.device = 0; // TODO device
		event.data1 = 0;
		event.data2 = (INT32)lround(mousemovex * ((float)wwidth / (float)realwidth));
		event.data3 = (INT32)lround(mousemovey * ((float)wheight / (float)realheight));
		D_PostEvent(&event);
	}

	// In order to make wheels act like buttons, we have to set their state to Up.
	// This is because wheel messages don't have an up/down state.
	for (i = 0; i < MAXSPLITSCREENPLAYERS; i++)
	{
		gamekeydown[i][KEY_MOUSEWHEELDOWN] = gamekeydown[i][KEY_MOUSEWHEELUP] = 0;
	}
}

void I_StartupMouse(void)
{
	static SDL_bool firsttimeonmouse = SDL_TRUE;

	if (disable_mouse)
		return;

	if (!firsttimeonmouse)
	{
		HalfWarpMouse(realwidth, realheight); // warp to center
	}
	else
		firsttimeonmouse = SDL_FALSE;
	if (cv_usemouse.value && !IgnoreMouse())
		SDLdoGrabMouse();
	else
		SDLdoUngrabMouse();
}

//
// I_OsPolling
//
void I_OsPolling(void)
{
	SDL_Keymod mod;

	if (consolevent)
		I_GetConsoleEvents();

	if (SDL_WasInit(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) == (SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER))
		SDL_GameControllerUpdate();

	I_GetEvent();

	mod = SDL_GetModState();
	/* Handle here so that our state is always synched with the system. */
	shiftdown = ctrldown = altdown = 0;
	capslock = false;
	if (mod & KMOD_LSHIFT) shiftdown |= 1;
	if (mod & KMOD_RSHIFT) shiftdown |= 2;
	if (mod & KMOD_LCTRL)   ctrldown |= 1;
	if (mod & KMOD_RCTRL)   ctrldown |= 2;
	if (mod & KMOD_LALT)     altdown |= 1;
	if (mod & KMOD_RALT)     altdown |= 2;
	if (mod & KMOD_CAPS) capslock = true;
}

//
// I_UpdateNoBlit
//
void I_UpdateNoBlit(void)
{
}

// I_SkipFrame
//
// Returns true if it thinks we can afford to skip this frame
// from PrBoom's src/SDL/i_video.c
static inline boolean I_SkipFrame(void)
{
#if 1
	// While I fixed the FPS counter bugging out with this,
	// I actually really like being able to pause and
	// use perfstats to measure rendering performance
	// without game logic changes.
	return false;
#else
	static boolean skip = false;

	skip = !skip;

	switch (gamestate)
	{
		case GS_LEVEL:
			if (!paused)
				return false;
			/* FALLTHRU */
		case GS_WAITINGPLAYERS:
			return skip; // Skip odd frames
		default:
			return false;
	}
#endif
}

//
// I_FinishUpdate
//
void I_FinishUpdate(void)
{
	int player;

	if (rendermode == render_none)
		return; //Alam: No software or OpenGl surface

	SCR_CalculateFPS();

	if (st_overlay)
	{
		if (cv_ticrate.value)
			SCR_DisplayTicRate();

		if (cv_showping.value && netgame &&
				( consoleplayer != serverplayer || ! server_lagless ))
		{
			if (server_lagless)
			{
				if (consoleplayer != serverplayer)
					SCR_DisplayLocalPing();
			}
			else
			{
				for (
						player = 1;
						player < MAXPLAYERS;
						player++
				){
					if (D_IsPlayerHumanAndGaming(player))
					{
						SCR_DisplayLocalPing();
						break;
					}
				}
			}
		}
		if (cv_mindelay.value && consoleplayer == serverplayer && Playing())
			SCR_DisplayLocalPing();
	}

	if (marathonmode)
		SCR_DisplayMarathonInfo();

	// draw captions if enabled
	if (cv_closedcaptioning.value)
		SCR_ClosedCaptions();

#ifdef HAVE_DISCORDRPC
	if (discordRequestList != NULL)
		ST_AskToJoinEnvelope();
#endif

	if (rendermode == render_soft && screens[0])
	{
		void *pixels;
		int pitch;
		SDL_LockTexture(texture, NULL, &pixels, &pitch);
		int step = pitch / 4 - vid.width;
		UINT32 *restrict dst = (UINT32*)pixels;
		UINT8 *restrict src = screens[0];
		UINT32 *restrict palette = localPalette;
		for (int32_t y = 0; y < vid.height; y++)
		{
			UINT8 *restrict end = src + vid.width;
			do *dst++ = palette[*src++];
			while (src < end);
			dst += step;
		}
		SDL_UnlockTexture(texture);

		SDL_RenderClear(renderer);
		SDL_RenderCopy(renderer, texture, &src_rect, NULL);
		SDL_RenderPresent(renderer);
		Impl_SetSoftwareVsync(cv_vidwait.value);
	}
#ifdef HWRENDER
	else if (rendermode == render_opengl)
		OglSdlFinishUpdate(cv_vidwait.value);
#endif
}

//
// I_UpdateNoVsync
//
void I_UpdateNoVsync(void)
{
	INT32 real_vidwait = cv_vidwait.value;
	cv_vidwait.value = 0;
	I_FinishUpdate();
	cv_vidwait.value = real_vidwait;
}

//
// I_ReadScreen
//
void I_ReadScreen(UINT8 *scr)
{
	if (rendermode != render_soft)
		I_Error ("I_ReadScreen: called while in non-software mode");
	else
		VID_BlitLinearScreen(screens[0], scr,
			vid.width*vid.bpp, vid.height,
			vid.rowbytes, vid.rowbytes);
}

//
// I_SetPalette
//
void I_SetPalette(RGBA_t *palette)
{
	size_t i;
	for (i=0; i<256; i++)
	{
		localPalette[i] = 0xff000000;
		localPalette[i] |= palette[i].s.red << 0;
		localPalette[i] |= palette[i].s.green << 8;
		localPalette[i] |= palette[i].s.blue << 16;
	}
}

// return number of fullscreen + X11 modes
INT32 VID_NumModes(void)
{
	return MAXWINMODES;
}

const char *VID_GetModeName(INT32 modeNum)
{

	if (modeNum == -1)
		return fallback_resolution_name;
	else if (modeNum > MAXWINMODES)
		return NULL;

	snprintf(&vidModeName[modeNum][0], 32, "%dx%d", windowedModes[modeNum][0], windowedModes[modeNum][1]);
	return &vidModeName[modeNum][0];
}

INT32 VID_GetModeForSize(INT32 w, INT32 h)
{
	int i;
	for (i = 0; i < MAXWINMODES; i++)
	{
		if (windowedModes[i][0] == w && windowedModes[i][1] == h)
		{
			return i;
		}
	}
	// did not find mode from list, make custom resolution if the values somewhat make sense
	// opengl mode does not mind about max resolution defined in screen.h
	// if not using opengl, check against the maximum as well
	if ((w >= BASEVIDWIDTH && h >= BASEVIDHEIGHT) &&
		(rendermode != render_none || (w <= MAXVIDWIDTH && h <= MAXVIDHEIGHT)))
	{
		custom_width = w;
		custom_height = h;
		return CUSTOMMODENUM;
	}
	return -1;
}

void VID_PrepareModeList(void)
{
	// Under SDL2, we just use the windowed modes list, and scale in windowed fullscreen.
	allow_fullscreen = true;
}

void VID_CheckGLLoaded(rendermode_t oldrender)
{
	(void)oldrender;
#ifdef HWRENDER
	if (vid.glstate == VID_GL_LIBRARY_ERROR) // Well, it didn't work the first time anyway.
	{
		rendermode = oldrender;
		if (chosenrendermode == render_opengl) // fallback to software
			rendermode = render_soft;
		if (setrenderneeded)
		{
			CV_StealthSetValue(&cv_renderer, oldrender);
			setrenderneeded = 0;
		}
	}
#endif
}

boolean VID_CheckRenderer(void)
{
	boolean rendererchanged = false;

#ifdef HWRENDER
	rendermode_t oldrenderer = rendermode;
#endif

	refresh_rate = VID_GetRefreshRate();

	if (dedicated)
		return 0;

	if (setrenderneeded)
	{
		rendermode = static_cast<rendermode_t>(setrenderneeded);
		rendererchanged = true;

#ifdef HWRENDER
		if (rendermode == render_opengl)
		{
			VID_CheckGLLoaded(oldrenderer);

			// Initialize OpenGL before calling SDLSetMode, because it calls OglSdlSurface.
			if (vid.glstate == VID_GL_LIBRARY_NOTLOADED)
				Impl_InitOpenGL();
			else if (vid.glstate == VID_GL_LIBRARY_ERROR)
				rendererchanged = false;
		}
#endif

		setrenderneeded = 0;
	}

	SDL_bool center = setmodeneeded ? SDL_TRUE : SDL_FALSE;

	if (SDLSetMode(vid.width, vid.height, USE_FULLSCREEN, center) == SDL_FALSE)
	{
		if (!graphics_started)
		{
			// Guess I'll die
			I_Error("Couldn't initialize video");
		}
		else
		{
			CONS_Printf("Couldn't initialize video\n");
			return false;
		}
	}

	if (rendererchanged)
		vid.recalc = true;

	if (rendermode == render_soft)
	{
		Impl_SetupSoftwareBuffer();
		SCR_SetDrawFuncs();
	}
#ifdef HWRENDER
	else if (rendermode == render_opengl && rendererchanged)
	{
		HWR_Switch();
		V_SetPalette(0);
	}
#endif

	return rendererchanged;
}

INT32 VID_SetMode(INT32 modeNum)
{
	SDLdoUngrabMouse();

	vid.recalc = true;
	vid.bpp = 1;

	if (modeNum >= 0 && modeNum < MAXWINMODES)
	{
		vid.width = windowedModes[modeNum][0];
		vid.height = windowedModes[modeNum][1];
		vid.modenum = modeNum;
	}
	else if (modeNum == CUSTOMMODENUM && custom_width && custom_height)
	{
		// at this point these values are assumed to be okay
		vid.width = custom_width;
		vid.height = custom_height;
		vid.modenum = modeNum;
	}
	else
	{
		// just set the desktop resolution as a fallback
		SDL_DisplayMode mode;
		SDL_GetWindowDisplayMode(window, &mode);
		if (mode.w >= 2048)
		{
			vid.width = 1920;
			vid.height = 1200;
		}
		else
		{
			vid.width = mode.w;
			vid.height = mode.h;
		}
		vid.modenum = -1;
	}

	VID_CheckRenderer();

	return SDL_TRUE;
}

static SDL_bool Impl_CreateWindow(SDL_bool fullscreen)
{
	int flags = 0;

	if (window != NULL)
		return SDL_TRUE;

	if (fullscreen)
		flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

	if (borderlesswindow)
		flags |= SDL_WINDOW_BORDERLESS;

#ifdef HWRENDER
	flags |= SDL_WINDOW_OPENGL;
#endif

	// Create a window
	window = SDL_CreateWindow("SRB2Kart " VERSIONSTRING, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, realwidth, realheight, flags);

	if (window == NULL)
	{
		VIDEO_INIT_ERROR("Couldn't create window: %s");
		return SDL_FALSE;
	}

#ifdef USE_WINDOW_ICON
	Impl_SetWindowIcon();
#endif

	return SDL_TRUE;

}

#ifdef USE_WINDOW_ICON
static void Impl_SetWindowIcon(void)
{
	if (window && icoSurface)
		SDL_SetWindowIcon(window, icoSurface);
}
#endif

static void Impl_InitVideoSubSystem(void)
{
	if (video_init)
		return;

	if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0)
	{
		CONS_Printf(M_GetText("Couldn't initialize SDL's Video System: %s\n"), SDL_GetError());
		return;
	}

	video_init = true;
}

void I_RegisterSysCommands(void)
{
	if (dedicated || graphics_started)
		return;

	COM_AddCommand ("vid_nummodes", VID_Command_NumModes_f);
	COM_AddCommand ("vid_modelist", VID_Command_ModeList_f);
	COM_AddCommand ("vid_mode", VID_Command_Mode_f);
	CV_RegisterVar (&cv_vidwait);
	CV_RegisterVar (&cv_stretch);
	CV_RegisterVar (&cv_alwaysgrabmouse);
}

void I_StartupGraphics(void)
{
	if (dedicated)
	{
		rendermode = render_none;
		return;
	}
	if (graphics_started)
		return;

	disable_mouse = static_cast<SDL_bool>(M_CheckParm("-nomouse"));
	disable_fullscreen = M_CheckParm("-win") ? SDL_TRUE : SDL_FALSE;

	keyboard_started = true;

	// If it wasn't already initialized
	if (!video_init)
		Impl_InitVideoSubSystem();

	const char *vd = SDL_GetCurrentVideoDriver();
	if (vd)
	{
		const char *vd = SDL_GetCurrentVideoDriver();
		//CONS_Printf(M_GetText("Starting up with video driver: %s\n"), vd);
		if (
			strncasecmp(vd, "gcvideo", 8) == 0 ||
			strncasecmp(vd, "fbcon", 6) == 0 ||
			strncasecmp(vd, "wii", 4) == 0 ||
			strncasecmp(vd, "psl1ght", 8) == 0
		)
			framebuffer = SDL_TRUE;
	}

	rendermode = render_soft;

	// Renderer choices
	// Takes priority over the config.
	if (M_CheckParm("-renderer"))
	{
		INT32 i = 0;
		CV_PossibleValue_t *renderer_list = cv_renderer_t;
		const char *modeparm = M_GetNextParm();
		while (renderer_list[i].strvalue)
		{
			if (!stricmp(modeparm, renderer_list[i].strvalue))
			{
				chosenrendermode = static_cast<rendermode_t>(renderer_list[i].value);
				break;
			}
			i++;
		}
	}

	// Choose Software renderer
	else if (M_CheckParm("-software"))
		chosenrendermode = render_soft;

#ifdef HWRENDER
	// Choose OpenGL renderer
	else if (M_CheckParm("-opengl"))
		chosenrendermode = render_opengl;

	// Don't startup OpenGL
	if (M_CheckParm("-nogl"))
	{
		vid.glstate = VID_GL_LIBRARY_ERROR;
		if (chosenrendermode == render_opengl)
			chosenrendermode = render_none;
	}
#endif

	if (chosenrendermode != render_none)
		rendermode = chosenrendermode;

	usesdl2soft = M_CheckParm("-softblit") ? SDL_TRUE : SDL_FALSE;
	borderlesswindow = M_CheckParm("-borderless") ? SDL_TRUE : SDL_FALSE;

#ifdef HWRENDER
	if (rendermode == render_opengl)
		Impl_InitOpenGL();
#endif

	// Window icon
#ifdef USE_WINDOW_ICON
	icoSurface = IMG_ReadXPMFromArray(SDL_icon_xpm);
#endif

	// Fury: we do window initialization after GL setup to allow
	// SDL_GL_LoadLibrary to work well on Windows
	vid.recalc = true;
	vid.direct = NULL;
	vid.bpp = 1;
	vid.WndParent = NULL;

	// Create window
	// Default size for startup
	vid.width = BASEVIDWIDTH;
	vid.height = BASEVIDHEIGHT;

	VID_SetMode(VID_GetModeForSize(vid.width, vid.height));

#ifdef HAVE_TTF
	I_ShutdownTTF();
#endif

	SDLdoUngrabMouse();

	SDL_RaiseWindow(window);

	graphics_started = true;
}

static void Impl_InitOpenGL(void)
{
#ifdef HWRENDER
	if (vid.glstate == VID_GL_LIBRARY_LOADED)
		return;

	CONS_Printf("VID_StartupOpenGL()...\n");
	*(void**)&HWD.pfnInit             = hwSym("Init",NULL);
	*(void**)&HWD.pfnFinishUpdate     = NULL;
	*(void**)&HWD.pfnDraw2DLine       = hwSym("Draw2DLine",NULL);
	*(void**)&HWD.pfnDrawPolygon      = hwSym("DrawPolygon",NULL);
	*(void**)&HWD.pfnDrawIndexedTriangles = hwSym("DrawIndexedTriangles",NULL);
	*(void**)&HWD.pfnRenderSkyDome    = hwSym("RenderSkyDome",NULL);
	*(void**)&HWD.pfnSetBlend         = hwSym("SetBlend",NULL);
	*(void**)&HWD.pfnClearBuffer      = hwSym("ClearBuffer",NULL);
	*(void**)&HWD.pfnSetTexture       = hwSym("SetTexture",NULL);
	*(void**)&HWD.pfnUpdateTexture    = hwSym("UpdateTexture",NULL);
	*(void**)&HWD.pfnDeleteTexture    = hwSym("DeleteTexture",NULL);
	*(void**)&HWD.pfnReadRect         = hwSym("ReadRect",NULL);
	*(void**)&HWD.pfnGClipRect        = hwSym("GClipRect",NULL);
	*(void**)&HWD.pfnClearMipMapCache = hwSym("ClearMipMapCache",NULL);
	*(void**)&HWD.pfnSetSpecialState  = hwSym("SetSpecialState",NULL);
	*(void**)&HWD.pfnSetPalette       = hwSym("SetPalette",NULL);
	*(void**)&HWD.pfnGetTextureUsed   = hwSym("GetTextureUsed",NULL);
	*(void**)&HWD.pfnDrawModel        = hwSym("DrawModel",NULL);
	*(void**)&HWD.pfnCreateModelVBOs  = hwSym("CreateModelVBOs",NULL);
	*(void**)&HWD.pfnSetTransform     = hwSym("SetTransform",NULL);
	*(void**)&HWD.pfnPostImgRedraw    = hwSym("PostImgRedraw",NULL);
	*(void**)&HWD.pfnFlushScreenTextures=hwSym("FlushScreenTextures",NULL);
	*(void**)&HWD.pfnStartScreenWipe  = hwSym("StartScreenWipe",NULL);
	*(void**)&HWD.pfnEndScreenWipe    = hwSym("EndScreenWipe",NULL);
	*(void**)&HWD.pfnDoScreenWipe     = hwSym("DoScreenWipe",NULL);
	*(void**)&HWD.pfnDrawIntermissionBG=hwSym("DrawIntermissionBG",NULL);
	*(void**)&HWD.pfnMakeIntermissionBG=hwSym("MakeIntermissionBG",NULL);
	*(void**)&HWD.pfnMakeScreenTexture= hwSym("MakeScreenTexture",NULL);
	*(void**)&HWD.pfnRenderVhsEffect  = hwSym("RenderVhsEffect",NULL);
	*(void**)&HWD.pfnMakeScreenFinalTexture=hwSym("MakeScreenFinalTexture",NULL);
	*(void**)&HWD.pfnDrawScreenFinalTexture=hwSym("DrawScreenFinalTexture",NULL);

	*(void**)&HWD.pfnCompileShaders   = hwSym("CompileShaders",NULL);
	*(void**)&HWD.pfnCleanShaders     = hwSym("CleanShaders",NULL);
	*(void**)&HWD.pfnSetShader        = hwSym("SetShader",NULL);
	*(void**)&HWD.pfnUnSetShader      = hwSym("UnSetShader",NULL);

	*(void**)&HWD.pfnSetShaderInfo    = hwSym("SetShaderInfo",NULL);
	*(void**)&HWD.pfnLoadCustomShader = hwSym("LoadCustomShader",NULL);

	if (HWD.pfnInit())
		vid.glstate = VID_GL_LIBRARY_LOADED;
	else
	{
		vid.glstate = VID_GL_LIBRARY_ERROR;

		CV_StealthSet(&cv_renderer, "Software");
		rendermode = render_soft;

		if (setrenderneeded)
			setrenderneeded = 0;
	}
#endif
}

void I_ShutdownGraphics(void)
{
#ifdef USE_WINDOW_ICON
	if (icoSurface)
		SDL_FreeSurface(icoSurface);
	icoSurface = NULL;
#endif

	free(vid.buffer);
	vid.buffer = NULL;

	rendermode = render_none;

	I_OutputMsg("I_ShutdownGraphics(): ");

	// was graphics initialized anyway?
	if (!graphics_started)
	{
		I_OutputMsg("graphics never started\n");
		return;
	}
	graphics_started = false;
	I_OutputMsg("shut down\n");

#ifdef HWRENDER
	if (sdlglcontext)
	{
		SDL_GL_DeleteContext(sdlglcontext);
	}
#endif
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
	framebuffer = SDL_FALSE;
}

UINT32 I_GetRefreshRate(void)
{
	// Moved to VID_GetRefreshRate.
	// Precalculating it like that won't work as
	// well for windowed mode since you can drag
	// the window around, but very slow PCs might have
	// trouble querying mode over and over again.
	return refresh_rate;
}

void I_SetBorderlessWindow(void)
{
	SDL_bool borderless = (cv_fullscreen.value == 2) ? SDL_FALSE : SDL_TRUE;
	SDL_SetWindowBordered(window, borderless);
}
#endif


