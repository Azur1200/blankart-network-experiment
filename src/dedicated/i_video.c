#include "../doomdef.h"
#include "../command.h"
#include "../i_video.h"
#include "dedimain.h"

rendermode_t rendermode = render_none;
rendermode_t chosenrendermode = render_none;

uint8_t graphics_started = 0; // Is used in console.c and screen.c

dboolean highcolor = false;

dboolean allow_fullscreen = false;

consvar_t cv_vidwait = CVAR_INIT ("vid_wait", "On", CV_SAVE, CV_OnOff, NULL);

void I_StartupGraphics(void){}
void I_ShutdownGraphics(void){}

void VID_StartupOpenGL(void){}

void I_SetPalette(RGBA_t *palette)
{
	(void)palette;
}

int32_t VID_NumModes(void)
{
	return 0;
}

int32_t VID_GetModeForSize(int32_t w, int32_t h)
{
	(void)w;
	(void)h;
	return 0;
}

void VID_PrepareModeList(void){}

int32_t VID_SetMode(int32_t modenum)
{
	(void)modenum;
	return 0;
}

dboolean VID_CheckRenderer(void)
{
	return false;
}

void VID_CheckGLLoaded(rendermode_t oldrender)
{
	(void)oldrender;
}

const char *VID_GetModeName(int32_t modenum)
{
	(void)modenum;
	return NULL;
}

uint32_t I_GetRefreshRate(void) { return 35; }

void I_UpdateNoBlit(void){}

void I_FinishUpdate(void){}

void I_UpdateNoVsync(void) {}

void I_ReadScreen(uint8_t * restrict scr, int32_t scale)
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

void I_SetTextInput(dboolean enable)
{

}

void I_RegisterSysCommands(void)
{

}

void I_StartupMouse(void)
{

}

dboolean I_UseNativeKeyboard(void)
{
	return false;
}
