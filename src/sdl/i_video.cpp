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

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_video.h>
#include <stdlib.h>
#include <errno.h>

#include <signal.h>

#ifdef HAVE_SDL
#define _MATH_DEFINES_DEFINED
#include <SDL3/SDL.h>

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

#include "../doomstat.h"
#include "../p_setup.h"
#include "../i_system.h"
#include "../v_video.h"
#include "../m_argv.h"
#include "../m_menu.h"
#include "../d_main.h"
#include "../s_sound.h"
#include "../i_sound.h"  	// midi pause/unpause
#include "../i_gamepad.h"
#include "../st_stuff.h"
#include "../hu_stuff.h"
#include "../g_game.h"
#include "../i_video.h"
#include "../console.h"
#include "../command.h"
#include "../r_main.h"
#include "../r_draw.h"
#include "../lua_hook.h"
#include "sdlmain.h"
#include "../i_system.h"
#ifdef HWRENDER
#include "../hardware/hw_main.h"
#include "../hardware/hw_gpu.h"
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

static CV_PossibleValue_t keyboardlayout_cons_t[] = {{1,"Default US"}, {2, "Native"}, {0, NULL}};
consvar_t cv_keyboardlayout = CVAR_INIT ("keyboardlayout", "Default US", CV_SAVE, keyboardlayout_cons_t, NULL);

dboolean highcolor = false;


// synchronize page flipping with screen refresh
consvar_t cv_vidwait = CVAR_INIT ("vid_wait", "On", CV_SAVE, CV_OnOff, NULL);
static consvar_t cv_stretch = CVAR_INIT ("stretch", "Off", CV_SAVE|CV_NOSHOWHELP, CV_OnOff, NULL);
static consvar_t cv_alwaysgrabmouse = CVAR_INIT ("alwaysgrabmouse", "Off", CV_SAVE, CV_OnOff, NULL);

uint8_t graphics_started = 0; // Is used in console.c and screen.c

// To disable fullscreen at startup; is set in VID_PrepareModeList
dboolean allow_fullscreen = false;
static bool disable_fullscreen = false;

// hack to avoid resolution being forced back to 320x200 on entering fullscreen
static bool just_switched;

#define USE_FULLSCREEN (disable_fullscreen||!allow_fullscreen)?static_cast<bool>(0):static_cast<bool>(cv_fullscreen.value == 1)

static bool disable_mouse = false;
#define USE_MOUSEINPUT (!disable_mouse && cv_usemouse.value && havefocus)
#define MOUSE_MENU false //(!disable_mouse && cv_usemouse.value && menustack[0] && !USE_FULLSCREEN)
#define MOUSEBUTTONS_MAX MOUSEBUTTONS

// Total mouse motion X/Y offsets
static      int32_t        mousemovex = 0, mousemovey = 0;

// SDL vars
static      uint32_t    localPalette[256];
static      bool    wrapmouseok = false;
#define HalfWarpMouse(x,y) if (wrapmouseok) SDL_WarpMouseInWindow(window, (Uint16)(x/2),(Uint16)(y/2))
static       bool    usesdlsoft = false;
static       bool    borderlesswindow = false;

SDL_Window   *window = NULL;
SDL_Renderer *renderer = NULL;
static SDL_Texture  *texture = NULL;
static bool      havefocus = true;

static uint32_t refresh_rate;

static dboolean video_init = false;

static SDL_Window *Impl_CreateWindow(void);

static void Impl_VideoSetupSurfaces(int width, int height);

static void Impl_InitOpenGL(void);

#if defined(HAVE_IMAGE)
#define USE_WINDOW_ICON
#endif

#ifdef USE_WINDOW_ICON
static void Impl_SetWindowIcon(void);
static SDL_Surface *icoSurface = NULL;
#endif

// windowed video modes from which to choose from.
static int32_t windowedModes[MAXWINMODES][2] =
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
static int32_t custom_width = 0;
static int32_t custom_height = 0;

static char vidModeName[MAXWINMODES][32];
static const char *fallback_resolution_name = "Fallback";

#define VIDEO_INIT_ERROR(str) { \
	if (!graphics_started) \
		I_Error(str, SDL_GetError()); \
	else \
		CONS_Printf(str "\n", SDL_GetError()); \
}

static bool Impl_RenderContextCreate(void)
{
	SDL_GL_ResetAttributes();
	SDL_SetHint("SDL_HINT_OPENGL_FORCE_SRGB_FRAMEBUFFER", "0");

	if (rendermode != render_opengl)
	{
		if (!renderer)
		{
			SDL_PropertiesID props = SDL_CreateProperties();
			if (props == 0)
			{
				VIDEO_INIT_ERROR("Couldn't create rendering properties: %s");
				return false;
			}
			if (usesdlsoft)
				SDL_SetStringProperty(props, SDL_PROP_RENDERER_CREATE_NAME_STRING, "SDL_SOFTWARE_RENDERER");
#ifdef _WIN32
			else
				SDL_SetStringProperty(props, SDL_PROP_RENDERER_CREATE_NAME_STRING, "opengl");
#endif
			SDL_SetNumberProperty(props, SDL_PROP_RENDERER_CREATE_PRESENT_VSYNC_NUMBER, cv_vidwait.value);
			SDL_SetPointerProperty(props, SDL_PROP_RENDERER_CREATE_WINDOW_POINTER, window);
			renderer = SDL_CreateRendererWithProperties(props);
		}

		if (renderer == NULL)
		{
			VIDEO_INIT_ERROR("Couldn't create rendering context: %s");
			return false;
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
				return false;
			}
		}
	}
#endif

	return true;
}

static bool Impl_RenderContextReset(void)
{
	if (renderer)
	{
		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindowSurface(window); // workaround for a bug in sdl
		texture = NULL; // Destroying a renderer also destroys all of its textures
	}
	renderer = NULL;

	if (Impl_RenderContextCreate() == false)
		return false;

#ifdef HWRENDER
	if (rendermode == render_opengl)
	{
		SDL_GL_MakeCurrent(window, sdlglcontext);
		SDL_GL_SetSwapInterval(cv_vidwait.value);

		OglSdlSurface(vid.width, vid.height);
		HWR_Startup();
	}
	else
#endif
	{
		SDL_RenderClear(renderer);
		SDL_SetRenderLogicalPresentation(renderer, vid.width, vid.height, SDL_LOGICAL_PRESENTATION_LETTERBOX);
		Impl_VideoSetupSurfaces(vid.width, vid.height);
	}

	return true;
}

static void Impl_SetSoftwareVsync(int vsync)
{
	static int oldvsync = 0;
	if (oldvsync != vsync)
	{
		SDL_SetRenderVSync(renderer, vsync);
	}
	oldvsync = vsync;
}

static void Impl_VideoSetupSurfaces(int width, int height)
{
	if (texture == NULL)
		texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, width, height);
	SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
}

static SDL_FRect src_rect = { 0, 0, 0, 0 };

static bool SDLSetMode(int32_t width, int32_t height, bool fullscreen, bool reposition)
{
	static bool wasfullscreen = false;
	int fullscreen_type = SDL_WINDOW_FULLSCREEN;
	just_switched = true;

	src_rect.w = vid.width = width;
	src_rect.h = vid.height = height;

	if (window)
	{
		if (fullscreen)
		{
			wasfullscreen = true;
			SDL_SetWindowFullscreen(window, fullscreen_type);
		}
		else // windowed mode
		{
			if (wasfullscreen)
			{
				wasfullscreen = false;
				SDL_SetWindowFullscreen(window, 0);
				I_SetBorderlessWindow();
			}

			SDL_SetWindowSize(window, width, height);

			if (reposition)
			{
				// Reposition window only in windowed mode
				SDL_SetWindowPosition(window,
					SDL_WINDOWPOS_CENTERED_DISPLAY(SDL_GetDisplayForWindow(window)),
					SDL_WINDOWPOS_CENTERED_DISPLAY(SDL_GetDisplayForWindow(window))
				);
			}
		}
	}
	else
	{
		window = Impl_CreateWindow();
		if (window == NULL)
			return false;

		wasfullscreen = fullscreen;
		if (fullscreen)
			SDL_SetWindowFullscreen(window, fullscreen_type);
	}

	if (Impl_RenderContextReset() == false)
		I_Error("Couldn't create or reset rendering context");

	return true;
}

static uint32_t VID_GetRefreshRate(void)
{
	int index = SDL_GetDisplayForWindow(window);
	const SDL_DisplayMode *m;

	if (SDL_WasInit(SDL_INIT_VIDEO) == 0)
	{
		// Video not init yet.
		return 0;
	}

	m = SDL_GetDesktopDisplayMode(index);
	if (m == NULL)
	{
		// Error has occurred.
		return 0;
	}

	return m->refresh_rate;
}

static int32_t Impl_SDL_Scancode_To_Keycode(SDL_Scancode code)
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

static dboolean IgnoreMouse(void)
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
	if (disable_mouse)
		return;

	SDL_HideCursor();
	SDL_SetWindowMouseGrab(window, true);
	if (SDL_SetWindowRelativeMouseMode(window, true)) // already warps mouse if successful
		wrapmouseok = true; // TODO: is wrapmouseok or HalfWarpMouse needed anymore?
}

static void SDLdoUngrabMouse(void)
{
	if (disable_mouse)
		return;

	SDL_ShowCursor();
	SDL_SetWindowMouseGrab(window, false);
	wrapmouseok = false;
	SDL_SetWindowRelativeMouseMode(window, false);
}

void SDLforceUngrabMouse(void)
{
	if (disable_mouse)
		return;

	if (SDL_WasInit(SDL_INIT_VIDEO)==SDL_INIT_VIDEO && window != NULL)
		SDLdoUngrabMouse();
}

void I_UpdateMouseGrab(void)
{
	if (disable_mouse)
		return;

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
	for (int32_t i = 0; i < MAXWINMODES; i++)
		CONS_Printf("%2d: %dx%d\n", i, windowedModes[i][0], windowedModes[i][1]);
}

static void VID_Command_Mode_f (void)
{
	int32_t modenum;

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

static void Impl_SetFocused(dboolean focused)
{
	window_notinfocus = !focused;

	if (window_notinfocus)
	{
		if (! cv_playmusicifunfocused.value)
			I_SetMusicVolume(0);
		if (! cv_playsoundifunfocused.value)
			S_StopSounds();

		memset(gamekeydown, 0, NUMKEYS); // TODO this is a scary memset
	}
	else
	{
		S_InitMusicVolume();
	}
}

static int32_t SDLJoyAxis(const Sint16 axis, uint8_t pid)
{
	// -32768 to 32767
	int32_t raxis = axis / 32;

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
		raxis = (abs(controllerInfo[pid].scale) > 1) ? ((raxis / controllerInfo[pid].scale) * controllerInfo[pid].scale) : raxis;
	}

	return raxis;
}

static void Impl_HandleWindowEvent(SDL_WindowEvent evt)
{
	static bool firsttimeonmouse = true;
	static bool mousefocus = true;
	static bool kbfocus = true;

	switch (evt.type)
	{
		case SDL_EVENT_WINDOW_MOUSE_ENTER:
			mousefocus = true;
			break;
		case SDL_EVENT_WINDOW_MOUSE_LEAVE:
			mousefocus = false;
			break;
		case SDL_EVENT_WINDOW_FOCUS_GAINED:
			kbfocus = true;
			mousefocus = true;
			break;
		case SDL_EVENT_WINDOW_FOCUS_LOST:
			kbfocus = false;
			mousefocus = false;
			break;
		case SDL_EVENT_WINDOW_MAXIMIZED:
		case SDL_EVENT_WINDOW_RESIZED:
		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
			just_switched = false;
			break;
		default:
			return;
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

static int32_t Impl_SDL_Keysym_To_Keycode(SDL_KeyboardEvent evt)
{
	SDL_Keycode keycode = evt.key;
	SDL_Scancode scancode = evt.scancode;

	if (keycode >= SDLK_A && keycode <= SDLK_Z)
	{
		// get lowercase ASCII
		return keycode;
	}
	if (keycode >= SDLK_F1 && keycode <= SDLK_F10)
	{
		return KEY_F1 + (keycode - SDLK_F1);
	}

	switch (scancode)
	{
		case SDL_SCANCODE_APOSTROPHE:    return KEY_FR_U_GRAVE;
		case SDL_SCANCODE_LEFTBRACKET:   return '^';
		default:               break;
	}

	switch (keycode)
	{
		// F11 and F12 are separated from the rest of the function keys
		case SDLK_F11: return KEY_F11;
		case SDLK_F12: return KEY_F12;

		case SDLK_RETURN:         return KEY_ENTER;
		case SDLK_ESCAPE:         return KEY_ESCAPE;
		case SDLK_BACKSPACE:      return KEY_BACKSPACE;
		case SDLK_TAB:            return KEY_TAB;
		case SDLK_SPACE:          return KEY_SPACE;
		case SDLK_EQUALS:         return KEY_EQUALS;
		case SDLK_SEMICOLON:      return ';';
		case SDLK_COMMA:          return ',';
		case SDLK_COLON:          return ':';
		case SDLK_EXCLAIM:        return '!';
		case SDLK_DOLLAR:         return '$';
		case SDLK_ASTERISK:       return '*';
		case SDLK_PERCENT:        return '%';
		case SDLK_0:              return KEY_FR_A_GRAVE;
		case SDLK_1:
		case SDLK_AMPERSAND:      return '&';
		case SDLK_2:              return KEY_FR_E_AIGUE;
		case SDLK_3:
		case SDLK_DBLAPOSTROPHE:  return '"';
		case SDLK_4:		      return '\'';
		case SDLK_5:
		case SDLK_LEFTPAREN:      return '(';
		case SDLK_6:
		case SDLK_MINUS:          return KEY_MINUS;
		case SDLK_7:              return KEY_FR_E_GRAVE;
		case SDLK_8:
		case SDLK_UNDERSCORE:     return '_';
		case SDLK_9:              return KEY_FR_C_CEDILLE;
		case SDLK_RIGHTPAREN:     return ')';
		case SDLK_SLASH:          return '/';
		case SDLK_LESS:           return '<';


		case SDLK_KP_0: return KEY_KEYPAD0;
		case SDLK_KP_1: return KEY_KEYPAD1;
		case SDLK_KP_2: return KEY_KEYPAD2;
		case SDLK_KP_3: return KEY_KEYPAD3;
		case SDLK_KP_4: return KEY_KEYPAD4;
		case SDLK_KP_5: return KEY_KEYPAD5;
		case SDLK_KP_6: return KEY_KEYPAD6;
		case SDLK_KP_7: return KEY_KEYPAD7;
		case SDLK_KP_8: return KEY_KEYPAD8;
		case SDLK_KP_9: return KEY_KEYPAD9;

		default:                  break;
	}

	return Impl_SDL_Scancode_To_Keycode(scancode);
}

static dboolean native_input_active = false;

// used to supress the games shift/alt handling
dboolean I_UseNativeKeyboard(void)
{
	return (cv_keyboardlayout.value == 2 && native_input_active);
}

void I_SetTextInput(dboolean enable)
{
	if (!window)
		return;

	if (enable && !native_input_active)
	{
		SDL_StartTextInput(window);
		native_input_active = true;
	}
	else if (!enable && native_input_active)
	{
		SDL_StopTextInput(window);
		native_input_active = false;
	}
}

// Get the equivalent ASCII (Unicode?) character for a keypress.
static int32_t GetTypedChar(SDL_KeyboardEvent evt)
{
	SDL_Event next_event;
	SDL_Keycode keycode = evt.key;
	SDL_Scancode scancode = evt.scancode;

	// only use this this if on chat or console or the current menu wants inputs from us (except if its the control setup menu ig)
	if (native_input_active)
	{
		// Special cases, where we always return a fixed value.
		switch (keycode)
		{
			case SDLK_BACKSPACE: return KEY_BACKSPACE;
			case SDLK_RETURN:    return KEY_ENTER;
			default:
				break;
		}

		// Special case for console key
		if (scancode == SDL_SCANCODE_GRAVE)
			return '`';

		if (SDL_PeepEvents(&next_event, 1, SDL_PEEKEVENT, SDL_EVENT_FIRST, SDL_EVENT_LAST) == 1 && next_event.type == SDL_EVENT_TEXT_INPUT)
		{
			if (next_event.text.text[1] == '\0') // limit to ASCII
			{
				return next_event.text.text[0];
			}
		}
	}

	// otherwise fallback to scancodes
	return Impl_SDL_Scancode_To_Keycode(scancode);
}

static void Impl_HandleKeyboardEvent(SDL_KeyboardEvent evt, Uint32 type)
{
	event_t event;

	event.device = 0;

	if (type == SDL_EVENT_KEY_UP)
	{
		event.type = ev_keyup;
	}
	else if (type == SDL_EVENT_KEY_DOWN)
	{
		event.type = ev_keydown;
	}
	else
	{
		return;
	}

	switch (cv_keyboardlayout.value)
	{
		case 2: // "native"
			event.data1 = GetTypedChar(evt);
			break;
		default:
			event.data1 = Impl_SDL_Scancode_To_Keycode(evt.scancode);
			break;
	}

	event.data2 = evt.repeat;

	if (event.data1) D_PostEvent(&event);
}

static void Impl_HandleMouseMotionEvent(SDL_MouseMotionEvent evt)
{
	static dboolean firstmove = true;

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
		if (SDL_GetWindowRelativeMouseMode(window))
		{
			if (SDL_GetMouseFocus() == window && SDL_GetKeyboardFocus() == window)
			{
				mousemovex +=  evt.xrel;
				mousemovey += -evt.yrel;
				SDL_SetWindowMouseGrab(window, true);
			}
			firstmove = false;
			return;
		}

		// If the event is from warping the pointer to middle
		// of the screen then ignore it.
		if (((int)evt.x == vid.width/2) && ((int)evt.y == vid.height/2))
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

		if (type == SDL_EVENT_MOUSE_BUTTON_UP)
		{
			event.type = ev_keyup;
		}
		else if (type == SDL_EVENT_MOUSE_BUTTON_DOWN)
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

	event.device = 0;

	int y = evt.y;
	if (y > 0)
	{
		event.data1 = KEY_MOUSEWHEELUP;
		event.type = ev_keydown;
	}
	else if (y < 0)
	{
		event.data1 = KEY_MOUSEWHEELDOWN;
		event.type = ev_keydown;
	}
	else if (y == 0)
	{
		event.data1 = 0;
		event.type = ev_keyup;
	}
	if (event.type == ev_keyup || event.type == ev_keydown)
	{
		D_PostEvent(&event);
	}
}

static void Impl_HandleControllerAxisEvent(SDL_GamepadAxisEvent evt)
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
		case SDL_GAMEPAD_AXIS_LEFTX:
			event.data1 = 0;
			break;
		case SDL_GAMEPAD_AXIS_LEFTY:
			event.data1 = 1;
			break;
		case SDL_GAMEPAD_AXIS_RIGHTX:
			event.data1 = 2;
			break;
		case SDL_GAMEPAD_AXIS_RIGHTY:
			event.data1 = 3;
			break;
		case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
			event.data1 = 4;
			break;
		case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
			event.data1 = 5;
			break;
		default:
			return;
	}

	//value
	event.data2 = SDLJoyAxis(evt.value, G_GetDevicePlayer(event.device));

	D_PostEvent(&event);
}

static void Impl_HandleControllerButtonEvent(SDL_GamepadButtonEvent evt, Uint32 type)
{
	event_t event;

	event.device = 1 + evt.which;

	event.data1 = event.data2 = event.data3 = INT32_MAX;

	if (event.device == INT32_MAX)
	{
		return;
	}

	if (type == SDL_EVENT_GAMEPAD_BUTTON_UP)
	{
		event.type = ev_keyup;
	}
	else if (type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
	{
		event.type = ev_keydown;
	}
	else
	{
		return;
	}

	uint8_t button = evt.button;
	if (button >= JOYBUTTONS)
		return;

	for (int32_t i = 0; i < MAXSPLITSCREENPLAYERS; i++)
	{
		if (controllerInfo[i].dev == NULL || controllerInfo[i].id != evt.which)
			continue;

		if (controllerInfo[i].flipbuttons) switch (evt.button)
		{
			case SDL_GAMEPAD_BUTTON_SOUTH: button = SDL_GAMEPAD_BUTTON_EAST; break;
			case SDL_GAMEPAD_BUTTON_EAST: button = SDL_GAMEPAD_BUTTON_SOUTH; break;
			case SDL_GAMEPAD_BUTTON_WEST: button = SDL_GAMEPAD_BUTTON_NORTH; break;
			case SDL_GAMEPAD_BUTTON_NORTH: button = SDL_GAMEPAD_BUTTON_WEST; break;
		}
		else if (controllerInfo[i].flipgc) switch (evt.button)
		{
			case SDL_GAMEPAD_BUTTON_EAST: button = SDL_GAMEPAD_BUTTON_WEST; break;
			case SDL_GAMEPAD_BUTTON_WEST: button = SDL_GAMEPAD_BUTTON_EAST; break;
		}
		break;
	}

	event.data1 = KEY_JOY1 + button;
	event.data2 = 0;

	if (event.type != ev_console)
	{
		D_PostEvent(&event);
	}
}

static void Impl_HandleControllerSensorEvent(SDL_GamepadSensorEvent evt)
{
	event_t event;

	event.device = 1 + evt.which;

	event.data1 = event.data2 = event.data3 = INT32_MAX;

	if (event.device == INT32_MAX)
	{
		return;
	}

	switch (evt.sensor)
	{
		// data[0]: x acceleration in m/s
		// data[1]: y acceleration in m/s
		// data[2]: z acceleration in m/s
		// we convert to gs before passing it to the event
		case SDL_SENSOR_ACCEL:
			// CONS_Debug(DBG_IMU, "Got Accelerometer event %4.2f %4.2f %4.2f\n", evt.data[0]/SDL_STANDARD_GRAVITY, evt.data[1]/SDL_STANDARD_GRAVITY, evt.data[2]/SDL_STANDARD_GRAVITY);
			event.type = ev_accelerometer;
			event.data1 = FLOAT_TO_FIXED(evt.data[0] / SDL_STANDARD_GRAVITY);
			event.data2 = FLOAT_TO_FIXED(evt.data[1] / SDL_STANDARD_GRAVITY);
			event.data3 = FLOAT_TO_FIXED(evt.data[2] / SDL_STANDARD_GRAVITY);
			D_PostEvent(&event);
			return;
			
		// data[0]: delta pitch in rad/s
		// data[1]: delta yaw in rad/s
		// data[2]: delta roll in rad/s
		// we convert to degrees per second before passing it to the event
		case SDL_SENSOR_GYRO:
#define RAD2DEG -57.295779513f
			// CONS_Debug(DBG_IMU, "Got Gyro event %4.2f %4.2f %4.2f\n", RAD2DEG * evt.data[0], RAD2DEG * evt.data[1], RAD2DEG * evt.data[2]);
			event.type = ev_gyroscope;
			event.data1 = FLOAT_TO_FIXED(RAD2DEG * evt.data[0]);
			event.data2 = FLOAT_TO_FIXED(RAD2DEG * evt.data[1]);
			event.data3 = FLOAT_TO_FIXED(RAD2DEG * evt.data[2]);
#undef RAD2DEG
			D_PostEvent(&event);
			return;

		default:
			return;
	}
}

static void Impl_HandleControllerAddEvent(SDL_Event evt)
{
	uint8_t i;
	SDL_JoystickID joystick_id = evt.gdevice.which;

	if (joystick_id == 0)
	{
		CONS_Debug(DBG_GAMELOGIC, "Invalid Controller: %s\n", SDL_GetError());
		return;
	}

	for (i = 0; i < MAXSPLITSCREENPLAYERS; i++)
	{
		I_InitController(i);
		G_SetPlayerControllerIndicatorColor(i, SKINCOLOR_NONE); // gotta update the controller led again on reconnect
	}

	I_UpdateControllerList();

	// update the menu
	M_UpdateJoystickMenu();

	CONS_Debug(DBG_GAMELOGIC, "Controller device index %d added\n", joystick_id);
}

static void Impl_HandleControllerRemoveEvent(SDL_Event evt)
{
	uint8_t i;
	SDL_JoystickID joystick_id = evt.gdevice.which;

	if (joystick_id == 0)
	{
		CONS_Debug(DBG_GAMELOGIC, "Invalid Controller: %s\n", SDL_GetError());
		return;
	}

	for (i = 0; i < MAXSPLITSCREENPLAYERS; i++)
	{
		if (controllerInfo[i].dev && (controllerInfo[i].id == joystick_id) && !SDL_GamepadConnected(controllerInfo[i].dev))
		{
			CONS_Debug(DBG_GAMELOGIC, "Controller for Player %d removed, device index: %d\n", i+1, joystick_id);
			I_ShutdownController(i);
		}
	}

	I_UpdateControllerList();

	// update the menu
	M_UpdateJoystickMenu();
}

void I_GetEvent(void)
{
	SDL_Event evt;
	const char* dropped_filedir;

	uint8_t i;

	mousemovex = mousemovey = 0;

	while (SDL_PollEvent(&evt))
	{
		switch (evt.type)
		{
			// TODO: Move input code out of this file, desperately
			case SDL_EVENT_KEY_UP:
			case SDL_EVENT_KEY_DOWN:
				Impl_HandleKeyboardEvent(evt.key, evt.type);
				break;
			case SDL_EVENT_MOUSE_MOTION:
				//if (!mouseMotionOnce)
				Impl_HandleMouseMotionEvent(evt.motion);
				//mouseMotionOnce = 1;
				break;
			case SDL_EVENT_MOUSE_BUTTON_UP:
			case SDL_EVENT_MOUSE_BUTTON_DOWN:
				Impl_HandleMouseButtonEvent(evt.button, evt.type);
				break;
			case SDL_EVENT_MOUSE_WHEEL:
				Impl_HandleMouseWheelEvent(evt.wheel);
				break;
			case SDL_EVENT_GAMEPAD_AXIS_MOTION:
				Impl_HandleControllerAxisEvent(evt.gaxis);
				break;
			case SDL_EVENT_GAMEPAD_BUTTON_UP:
			case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
				Impl_HandleControllerButtonEvent(evt.gbutton, evt.type);
				break;
			case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
				Impl_HandleControllerSensorEvent(evt.gsensor);
				break;
			case SDL_EVENT_GAMEPAD_ADDED:
				Impl_HandleControllerAddEvent(evt);
				break;
			case SDL_EVENT_GAMEPAD_REMOVED:
				Impl_HandleControllerRemoveEvent(evt);
				break;
			case SDL_EVENT_DROP_FILE:
				dropped_filedir = evt.drop.data;
				COM_BufInsertText(va("addfile \"%s\"", dropped_filedir));
				break;
			case SDL_EVENT_QUIT:
				LUA_HookBool(true, HOOK(GameQuit));
				I_Quit();
				break;
			default:
				Impl_HandleWindowEvent(evt.window);
				break;
		}
	}

	// Send all relative mouse movement as one single mouse event.
	if (mousemovex || mousemovey)
	{
		event_t event;
		int wwidth, wheight;
		SDL_GetWindowSize(window, &wwidth, &wheight);
		event.type = ev_mouse;
		event.device = 0; // TODO device
		event.data1 = 0;
		event.data2 = (int32_t)lround(mousemovex * ((float)wwidth / (float)vid.width));
		event.data3 = (int32_t)lround(mousemovey * ((float)wheight / (float)vid.height));
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
	static bool firsttimeonmouse = true;

	disable_mouse = static_cast<bool>(M_CheckParm("-nomouse"));

	if (disable_mouse)
	{
		CONS_Printf("Mouse Input Disabled.\n");
		return;
	}

	if (!firsttimeonmouse)
	{
		HalfWarpMouse(vid.width, vid.height); // warp to center
	}
	else
		firsttimeonmouse = false;
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

	if (SDL_WasInit(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD) == (SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD))
		SDL_UpdateGamepads();

	I_GetEvent();

	mod = SDL_GetModState();
	/* Handle here so that our state is always synched with the system. */
	shiftdown = ctrldown = altdown = 0;
	capslock = false;
	if (mod & SDL_KMOD_LSHIFT) shiftdown |= 1;
	if (mod & SDL_KMOD_RSHIFT) shiftdown |= 2;
	if (mod & SDL_KMOD_LCTRL)   ctrldown |= 1;
	if (mod & SDL_KMOD_RCTRL)   ctrldown |= 2;
	if (mod & SDL_KMOD_LALT)     altdown |= 1;
	if (mod & SDL_KMOD_RALT)     altdown |= 2;
	if (mod & SDL_KMOD_CAPS) capslock = true;
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
static inline dboolean I_SkipFrame(void)
{
#if 1
	// While I fixed the FPS counter bugging out with this,
	// I actually really like being able to pause and
	// use perfstats to measure rendering performance
	// without game logic changes.
	return false;
#else
	static dboolean skip = false;

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
				( consoleplayer != serverplayer || (server_lagless == 0 || server_lagless == -1) ))
		{
			if (server_lagless == 1)
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

	if (rendermode == render_soft && vid.screens[0])
	{
		void *pixels;
		int pitch;
		SDL_LockTexture(texture, NULL, &pixels, &pitch);
		int step = pitch / 4 - vid.width;
		uint32_t *restrict dst = (uint32_t*)pixels;
		uint8_t *restrict src = vid.screens[0];
		uint32_t *restrict palette = localPalette;
		for (int32_t y = 0; y < vid.height; y++)
		{
			uint8_t *restrict end = src + vid.width;
			do *dst++ = palette[*src++];
			while (src < end);
			dst += step;
		}
		SDL_UnlockTexture(texture);

		SDL_RenderClear(renderer);
		SDL_RenderTexture(renderer, texture, &src_rect, NULL);
		SDL_RenderPresent(renderer);
		Impl_SetSoftwareVsync(cv_vidwait.value);
	}
#ifdef HWRENDER
	else if (rendermode == render_opengl)
	{
		// Final postprocess step of palette rendering, after everything else has been drawn.
		if (HWR_ShouldUsePaletteRendering())
		{
			GL_MakeScreenTexture(HWD_SCREENTEXTURE_GENERIC2);
			GL_SetShader(HWR_GetShaderFromTarget(SHADER_PALETTE_POSTPROCESS));
			GL_DrawScreenTexture(HWD_SCREENTEXTURE_GENERIC2, NULL, 0);
			GL_UnSetShader();
		}
		OglSdlFinishUpdate(cv_vidwait.value);
	}
#endif
}

//
// I_UpdateNoVsync
//
void I_UpdateNoVsync(void)
{
	int32_t real_vidwait = cv_vidwait.value;
	cv_vidwait.value = 0;
	I_FinishUpdate();
	cv_vidwait.value = real_vidwait;
}

//
// I_ReadScreen
//
void I_ReadScreen(uint8_t * restrict scr, int32_t scale)
{
	if (rendermode != render_soft)
		I_Error ("I_ReadScreen: called while in non-software mode");
	else if (scale == 1)
		VID_BlitLinearScreen(vid.screens[0], scr,
			vid.width, vid.height,
			vid.rowbytes, vid.rowbytes);
	else
	{
		uint8_t * restrict source = vid.screens[0];
		uintptr_t w = vid.width/scale*scale, h = vid.height/scale*scale;

		// size_t saves a lea + movsxd over int32_t. mind your types!
		// uintptr_t is even better since it's guaranteed to be the size of a pointer
		for (uintptr_t y = 0; y < h; y += scale)
			for (uintptr_t x = 0; x < w; x += scale)
				*scr++ = source[y*vid.width + x];
	}
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
int32_t VID_NumModes(void)
{
	return MAXWINMODES;
}

const char *VID_GetModeName(int32_t modeNum)
{

	if (modeNum == -1)
		return fallback_resolution_name;
	else if (modeNum > MAXWINMODES)
		return NULL;

	snprintf(&vidModeName[modeNum][0], 32, "%dx%d", windowedModes[modeNum][0], windowedModes[modeNum][1]);
	return &vidModeName[modeNum][0];
}

int32_t VID_GetModeForSize(int32_t w, int32_t h)
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
	// Under SDL, we just use the windowed modes list, and scale in windowed fullscreen.
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

dboolean VID_CheckRenderer(void)
{
	dboolean rendererchanged = false;

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

	bool center = setmodeneeded ? true : false;

	if (SDLSetMode(vid.width, vid.height, USE_FULLSCREEN, center) == false)
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
		vid.rowbytes = vid.width;
		SCR_SetDrawFuncs(COLUMNCONTEXT_DIRECT);
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

int32_t VID_SetMode(int32_t modeNum)
{
	SDLdoUngrabMouse();

	vid.recalc = true;

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
		const SDL_DisplayMode *mode;
		mode = SDL_GetWindowFullscreenMode(window);
		if (mode->w >= 2048)
		{
			vid.width = 1920;
			vid.height = 1200;
		}
		else
		{
			vid.width = mode->w;
			vid.height = mode->h;
		}
		vid.modenum = -1;
	}

	VID_CheckRenderer();

	return true;
}

static SDL_Window *Impl_CreateWindow(void)
{
	int flags = 0;

	if (window != NULL)
		return window;

	if (borderlesswindow)
		flags |= SDL_WINDOW_BORDERLESS;

#ifdef HWRENDER
	flags |= SDL_WINDOW_OPENGL;

	if (M_CheckParm("-msaa") && M_IsNextParm())
	{
		unsigned int value;
		const char* str = M_GetNextParm();
		if (sscanf(str, "%u", &value))
		{
			SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
			SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, value);
		}
	}

	// Without a 24-bit depth buffer many visuals are ruined by z-fighting.
	// Some GPU drivers may give us a 16-bit depth buffer since the
	// default value for SDL_GL_DEPTH_SIZE is 16.
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 4);
#endif

	SDL_Window *win = NULL;
	// Create a window
#ifdef COMMITVERSION
	{
		char versionstring[256];
		snprintf(versionstring, sizeof(versionstring), "BlanKart Indev %s", comprevision);

		win = SDL_CreateWindow(versionstring, vid.width, vid.height, flags);
	}
#else
	win = SDL_CreateWindow("BlanKart " VERSIONSTRING, vid.width, vid.height, flags);
#endif

	if (win == NULL)
	{
		VIDEO_INIT_ERROR("Couldn't create window: %s");
		return NULL;
	}

	SDL_SetWindowMinimumSize(win, BASEVIDWIDTH, BASEVIDHEIGHT);
	SDL_SetWindowMaximumSize(win, MAXVIDWIDTH, MAXVIDHEIGHT);

#ifdef USE_WINDOW_ICON
	Impl_SetWindowIcon();
#endif

	return win;
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
#if defined (__linux__)
	SDL_SetHint("SDL_VIDEO_WAYLAND_SCALE_TO_DISPLAY", "1");
#endif
	if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
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

	COM_AddCommand("vid_nummodes", VID_Command_NumModes_f);
	COM_AddCommand("vid_modelist", VID_Command_ModeList_f);
	COM_AddCommand("vid_mode", VID_Command_Mode_f);
	CV_RegisterVar(&cv_vidwait);
	CV_RegisterVar(&cv_stretch);
	CV_RegisterVar(&cv_alwaysgrabmouse);
	CV_RegisterVar(&cv_keyboardlayout);
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

	disable_mouse = static_cast<bool>(M_CheckParm("-nomouse"));
	disable_fullscreen = M_CheckParm("-win") ? true : false;

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
			framebuffer = true;
	}

	rendermode = render_soft;

	// Renderer choices
	// Takes priority over the config.
	if (M_CheckParm("-renderer"))
	{
		int32_t i = 0;
		CV_PossibleValue_t *renderer_list = cv_renderer_t;
		const char *modeparm = M_GetNextParm();
		while (renderer_list[i].strvalue)
		{
			if (fasticmp(modeparm, renderer_list[i].strvalue))
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

	usesdlsoft = M_CheckParm("-softblit") ? true : false;
	borderlesswindow = M_CheckParm("-borderless") ? true : false;

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

	// Create window
	// Default size for startup
	vid.width = BASEVIDWIDTH;
	vid.height = BASEVIDHEIGHT;

	VID_SetMode(VID_GetModeForSize(vid.width, vid.height));

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

	if (GL_Init())
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
		SDL_DestroySurface(icoSurface);
	icoSurface = NULL;
#endif

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
		SDL_GL_DestroyContext(sdlglcontext);
	}
#endif
	if (SDL_WasInit(SDL_INIT_VIDEO) == SDL_INIT_VIDEO)
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
	framebuffer = false;
}

uint32_t I_GetRefreshRate(void)
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
	bool bordered = cv_fullscreen.value != 2;
	SDL_SetWindowBordered(window, bordered);
}
#endif


