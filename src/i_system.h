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
/// \file  i_system.h
/// \brief System specific interface stuff.

#ifndef __I_SYSTEM__
#define __I_SYSTEM__

#include "d_ticcmd.h"
#include "d_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/**	\brief max quit functions
*/
#define MAX_QUIT_FUNCS     16


/**	\brief Graphic system had started up
*/
extern uint8_t graphics_started;

/**	\brief Keyboard system is up and run
*/
extern uint8_t keyboard_started;

/** \brief Set to true when inside a signal handler that will exit the program. */
dboolean I_In_Exiting_Signal_Handler(void);

/**	\brief	The I_GetFreeMem function

	\param	total	total memory in the system

	\return	free memory in the system
*/
uint32_t I_GetFreeMem(uint32_t *total);

/**	\brief	Returns precise time value for performance measurement. The precise
            time should be a monotonically increasing counter, and will wrap.
			precise_t is internally represented as an unsigned integer and
			integer arithmetic may be used directly between values of precise_t.
  */
precise_t I_GetPreciseTime(void);

/**	\brief	Fills a buffer with random data, returns amount of data obtained.
 */
size_t I_GetRandomBytes(char *destination, size_t count);

/** \brief  Get the precision of precise_t in units per second. Invocations of
            this function for the program's duration MUST return the same value.
  */
uint64_t I_GetPrecisePrecision(void);

/** \brief  Get the current time in rendering tics, including fractions.
*/
double I_GetFrameTime(void);

/**	\brief	Sleeps for the given duration in milliseconds. Depending on the
            operating system's scheduler, the calling thread may give up its
			time slice for a longer duration. The implementation should give a
			best effort to sleep for the given duration, without spin-locking.
			Calling code should check the current precise time after sleeping
			and not assume the thread has slept for the expected duration.

	\return	void
*/
void I_Sleep(uint32_t ms);

dboolean I_CheckFrameCap(precise_t start, precise_t end);

/**	\brief Get events

	Called by D_SRB2Loop,
	called before processing each tic in a frame.
	Quick syncronous operations are performed here.
	Can call D_PostEvent.
*/
void I_GetEvent(void);

/**	\brief Asynchronous interrupt functions should maintain private queues
	that are read by the synchronous functions
	to be converted into events.
*/
void I_OsPolling(void);

// Either returns a null ticcmd,
// or calls a loadable driver to build it.
// This ticcmd will then be modified by the gameloop
// for normal input.

/**	\brief Input for the first player
*/
ticcmd_t *I_BaseTiccmd(void);

/**	\brief Input for the second player
*/
ticcmd_t *I_BaseTiccmd2(void);

/**	\brief Input for the third player
*/
ticcmd_t *I_BaseTiccmd3(void);

/**	\brief Input for the fourth player
*/
ticcmd_t *I_BaseTiccmd4(void);

/**	\brief Called by M_Responder when quit is selected, return exit code 0
*/
void I_Quit(void) FUNCNORETURN;

#ifndef NOMUMBLE
#include "p_mobj.h" // mobj_t
#include "s_sound.h" // listener_t
/** \brief to update Mumble of Player Postion
*/
void I_UpdateMumble(const mobj_t *mobj, const listener_t listener);
#endif

/**	\brief Startup the mouse
*/
void I_StartupMouse(void);

/**	\brief  setup timer irq and user timer routine.
*/
void I_StartupTimer(void);

/**	\brief sample quit function
*/
typedef void (*quitfuncptr)();

/**	\brief	add a list of functions to call at program cleanup

	\param	(*func)()	funcction to call at program cleanup

	\return	void
*/
void I_AddExitFunc(void (*func)());

/**	\brief	The I_RemoveExitFunc function

	\param	(*func)()	function to remove from the list

	\return	void
*/
void I_RemoveExitFunc(void (*func)());

/**	\brief Setup signal handler, plus stuff for trapping errors and cleanly exit.
*/
int32_t I_StartupSystem(void);

/**	\brief Shutdown systems
*/
void I_ShutdownSystem(void);

dboolean I_Interrupted(void);

/**	\brief	The I_GetDiskFreeSpace function

	\param	freespace	a int64_t pointer to hold the free space amount

	\return	void
*/
void I_GetDiskFreeSpace(int64_t *freespace);

/**	\brief find out the user's name
*/
char *I_GetUserName(void);

/**	\brief	The I_mkdir function

	\param	dirname	string of mkidr
	\param	unixright	unix right

	\return status of new folder
*/
int32_t I_mkdir(const char *dirname, int32_t unixright);

/**	\brief Change current working directory

	\param path directory path
	\return 0 on success, -1 on error
*/
int32_t I_ChDir(const char *path);

/**	\brief Get current working directory

	\param buf buffer to store path
	\param size size of buffer
	\return buf on success, NULL on error
*/
char *I_GetCwd(char *buf, size_t size);

/**	\brief Find main WAD
		\return path to main WAD
*/
const char *I_LocateWad(void);

/**	\brief First Joystick's events
*/
void I_GetJoystickEvents(uint8_t index);

int32_t I_GetControllerIDForPlayer(uint8_t pnum);

/**	\brief Checks if the mouse needs to be grabbed
*/
void I_UpdateMouseGrab(void);

char *I_GetEnv(const char *name);

int32_t I_PutEnv(char *variable);

/** \brief Put data in system clipboard
*/
int32_t I_ClipboardCopy(const char *data, size_t size);

/** \brief Retrieve data from system clipboard
*/
const char *I_ClipboardPaste(void);

void I_RegisterSysCommands(void);

/** \brief Returns the system name.
 */
const char *I_GetSysName(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
