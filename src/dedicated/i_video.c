#include "../doomdef.h"
#include "../command.h"
#include "../i_video.h"
#include "dedimain.h"

rendermode_t rendermode = render_none;
rendermode_t chosenrendermode = render_none;

UINT8 graphics_started = 0; // Is used in console.c and screen.c

boolean highcolor = false;

boolean allow_fullscreen = false;

consvar_t cv_vidwait = CVAR_INIT ("vid_wait", "On", CV_SAVE, CV_OnOff, NULL);

void I_StartupGraphics(void){}
void I_ShutdownGraphics(void){}

void VID_StartupOpenGL(void){}

void I_SetPalette(RGBA_t *palette)
{
	(void)palette;
}

INT32 VID_NumModes(void)
{
	return 0;
}

INT32 VID_GetModeForSize(INT32 w, INT32 h)
{
	(void)w;
	(void)h;
	return 0;
}

void VID_PrepareModeList(void){}

INT32 VID_SetMode(INT32 modenum)
{
	(void)modenum;
	return 0;
}

boolean VID_CheckRenderer(void)
{
	return false;
}

void VID_CheckGLLoaded(rendermode_t oldrender)
{
	(void)oldrender;
}

const char *VID_GetModeName(INT32 modenum)
{
	(void)modenum;
	return NULL;
}

UINT32 I_GetRefreshRate(void) { return 35; }

void I_UpdateNoBlit(void){}

void I_FinishUpdate(void){}

void I_UpdateNoVsync(void) {}

void I_ReadScreen(UINT8 * restrict scr, INT32 scale)
{
	(void)scr;
	(void)scale;
}

void I_OsPolling(void)
{
	if (consolevent)
		I_GetConsoleEvents();
}

void I_SetBorderlessWindow(void)
{

}

void I_UpdateMouseGrab(void)
{

}

void I_SetTextInput(boolean enable)
{

}

void I_RegisterSysCommands(void)
{

}

void I_StartupMouse(void)
{

}

boolean I_UseNativeKeyboard(void)
{
	return false;
}
