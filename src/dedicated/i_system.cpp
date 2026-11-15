// Emacs style mode select   -*- C++ -*-
//
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
//
// Changes by Graue <graue@oceanbase.org> are in the public domain.
//
//-----------------------------------------------------------------------------
/// \file
/// \brief SRB2 system stuff

#include <thread>

#ifdef CMAKECONFIG
#include "../config.h"
#else
#include "../config.h.in"
#endif
#include <signal.h>

#ifdef _WIN32
#define RPC_NO_WINDOWS_H
#include <windows.h>
#include "../doomtype.h"
typedef BOOL (WINAPI *p_GetDiskFreeSpaceExA)(LPCSTR, PULARGE_INTEGER, PULARGE_INTEGER, PULARGE_INTEGER);
typedef BOOL (WINAPI *p_IsProcessorFeaturePresent) (DWORD);
typedef DWORD (WINAPI *p_timeGetTime) (void);
typedef UINT (WINAPI *p_timeEndPeriod) (UINT);
typedef HANDLE (WINAPI *p_OpenFileMappingA) (DWORD, BOOL, LPCSTR);
typedef LPVOID (WINAPI *p_MapViewOfFile) (HANDLE, DWORD, DWORD, DWORD, SIZE_T);
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __GNUC__
#include <unistd.h>
#endif
#if defined (__unix__) || defined (UNIXCOMMON)
#include <sys/stat.h>
#include <fcntl.h>
#endif

#include <stdio.h>
#ifdef _WIN32
// This is for RtlGenRandom.
#define SystemFunction036 NTAPI SystemFunction036
#include <ntsecapi.h>
#undef SystemFunction036

#include <conio.h>

// This is for RtlGenRandom.
#define SystemFunction036 NTAPI SystemFunction036
#include <ntsecapi.h>
#undef SystemFunction036
#endif

#if defined (__unix__) || defined(__APPLE__) || (defined (UNIXCOMMON) && !defined (__HAIKU__))
#if defined (__linux__)
#include <sys/vfs.h>
#elif defined(__APPLE__)
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <mach/mach.h>
#else
#include <sys/param.h>
#include <sys/mount.h>
/*For meminfo*/
#include <sys/types.h>
#ifdef FREEBSD
#include <kvm.h>
#endif
#include <nlist.h>
#include <sys/sysctl.h>
#endif
#endif

#if defined (__linux__) || (defined (UNIXCOMMON) && !defined (__HAIKU__))
#ifndef NOTERMIOS
#include <termios.h>
#include <sys/ioctl.h> // ioctl
#define HAVE_TERMIOS
#endif
#endif

#if (defined (__unix__) && !defined (_MSDOS)) || (defined (UNIXCOMMON) && !defined(__APPLE__))
#include <errno.h>
#include <sys/wait.h>
#define NEWSIGNALHANDLER
#endif

#ifndef NOMUMBLE
#ifdef __linux__ // need -lrt
#include <sys/mman.h>
#ifdef MAP_FAILED
#define HAVE_SHM
#endif
#include <wchar.h>
#endif

#ifdef _WIN32
#define HAVE_MUMBLE
#define WINMUMBLE
#elif defined (HAVE_SHM)
#define HAVE_MUMBLE
#endif
#endif // NOMUMBLE

#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifdef __APPLE__
#include "macosx/mac_resources.h"
#endif

#ifndef errno
#include <errno.h>
#endif

#if defined (__unix__) || defined(__APPLE__) || defined (UNIXCOMMON)
#include <execinfo.h>
#include <time.h>
#endif

#include "dedimain.h"

const char *wadDefaultPaths[] = {
#if defined (__unix__) || defined(__APPLE__) || defined (UNIXCOMMON)
	"/usr/local/share/games/SRB2Kart-V2",
	"/usr/local/games/SRB2Kart-V2",
	"/usr/share/games/SRB2Kart-V2",
	"/usr/games/SRB2Kart-V2",
#elif defined (_WIN32)
	"c:\\games\\SRB2Kart-V2",
	"\\games\\SRB2Kart-V2",
#endif
	NULL
};

// Folders to recurse through looking for main.pk3
const char *wadSearchPaths[] = {
#if defined (__unix__) || defined(__APPLE__) || defined (UNIXCOMMON)
	"/usr/local/games",
	"/usr/games",
	"/usr/local",
#elif defined (_WIN32)
	"c:\\games",
	"\\games",
#endif
	NULL
};


/**	\brief WAD file to look for
*/
#define WADKEYWORD "main.pk3"
/**	\brief holds wad path
*/
static char returnWadPath[256];

#include "../doomdef.h"
#include "../m_misc.h"
#include "../i_time.h"
#include "../i_video.h"
#include "../i_sound.h"
#include "../i_system.h"
#include "../i_threads.h"
#include "../screen.h" //vid.WndParent
#include "../d_net.h"
#include "../g_game.h"
#include "../filesrch.h"
#include "../s_sound.h"
#include "../r_voicepreference.hpp"
#include "../core/thread_pool.h"
#include "../z_zone.h"
#include "endtxt.h"

#include "../i_gamepad.h"

#include "../m_argv.h"

#include "../m_menu.h"

// SRB2Kart
#include "../k_pwrlv.h"
#include "../r_main.h" // Frame interpolation/uncapped
#include "../r_fps.h"

#ifdef MAC_ALERT
#include "macosx/mac_alert.h"
#endif

#include "../d_main.h"

#if !defined(NOMUMBLE) && defined(HAVE_MUMBLE)
// Mumble context string
#include "../d_clisrv.h"
#include "../byteptr.h"
#endif

static std::thread::id g_main_thread_id;

dboolean consolevent = false;
dboolean framebuffer = false;

uint8_t keyboard_started = false;
static volatile sig_atomic_t g_in_exiting_signal_handler = false;

static void I_PrintSignal(int32_t signal_num, dboolean core_dumped, char *signal_msg);

#ifdef HAVE_LIBBACKTRACE
#include <backtrace.h>
// TODO - move this to some header file instead
extern struct backtrace_state *bt_state;

typedef enum {
	BTCRASH_SIGNAL,
	BTCRASH_ERRORMSG,
} bt_crash_type_t;

typedef struct bt_crash_reason_s {
	bt_crash_type_t type;

	union {
		int32_t signal;
		const char *errormsg;
	} value;
} bt_crash_reason_t;

#define BT_CRASH_REASON_SIGNAL(num) (bt_crash_reason_t){ .type = BTCRASH_SIGNAL, .value = { .signal = num } }
#define BT_CRASH_REASON_ERRORMSG(msg) (bt_crash_reason_t){ .type = BTCRASH_ERRORMSG, .value = { .errormsg = msg } }

typedef struct bt_out_buf_s {
	dboolean error;
	char *pos;
	size_t size;
} bt_out_buf_t;

static void bt_syminfo_cb(void *data, uintptr_t pc, const char *symname, uintptr_t symval, uintptr_t symsize)
{
	(void)symval;
	(void)symsize;

	bt_out_buf_t *buf = (bt_out_buf_t*)data;

	if (!symname)
		symname = "???";

	int n = snprintf(buf->pos, buf->size, "%p %s\n", (void*)pc, symname);

	if (n <= 0)
	{
		buf->size = 0;
		return;
	}

	buf->pos += n;
	buf->size -= n;
}

static int bt_simple_cb(void *data, uintptr_t pc)
{
	bt_out_buf_t *buf = (bt_out_buf_t*)data;

	backtrace_syminfo(bt_state, pc, bt_syminfo_cb, NULL, data);

	if (!buf->size) return 1;

	return 0;
}

static int bt_full_cb(void *data, uintptr_t pc, const char *filename, int lineno, const char *function)
{
	bt_out_buf_t *buf = (bt_out_buf_t*)data;

	if (!filename) filename = "???";
	if (!function) function = "???";

	int n = snprintf(buf->pos, buf->size, "%p %s\n\t%s:%d\n", (void*)pc, function, filename, lineno);

	if (n <= 0) return 1;

	buf->pos += n;
	buf->size -= n;

	if (!buf->size) return 1;

	return 0;
}

static void bt_error_cb(void *data, const char *msg, int errnum)
{
	(void)msg; // We don't need this

	bt_out_buf_t *buf = (bt_out_buf_t*)data;

	// No debug info
	if (errnum == -1)
		buf->error = true;
}

static void write_backtrace(bt_crash_reason_t reason)
{
	char sig_msg[512];
	const char *filename = va("%s" PATHSEP CRASH_LOGFILE_NAME, srb2home);
	FILE *out = fopen(filename, "a+");

	time_t rawtime;
	struct tm *timeinfo;

	const size_t BUFSIZE = 8192;
	char backtrace[BUFSIZE];

	bt_out_buf_t buf;
	buf.error = false;
	buf.pos = backtrace;
	buf.size = BUFSIZE;

	if (!out)
	{
		fprintf(stderr, "\nWARNING: Couldn't open crash log for writing! Make sure your permissions are correct. Please save the below report!\n");
		out = stderr;
	}

	// Get the current time as a string.
	time(&rawtime);
	timeinfo = localtime(&rawtime);

	fprintf(out, "------------------------\n\n");

	fprintf(out, "Program name: BlanKart %s\n", /*VERSIONSTRING*/ "Indev");

	fprintf(out, "Dedicated build\n");
	fprintf(out, "Platform: %s\n", I_GetSysName());

	if (compdate && comptime && comprevision && compbranch)
		fprintf(out, "Compiled: %s %s, commit %s, branch %s\n", compdate, comptime, comprevision, compbranch);

	if (gamestate == GS_LEVEL && gamemap)
	{
		char *title = G_BuildMapTitle(gamemap);

		if (title)
		{
			fprintf(out, "Game map: %s (%s)\n", title, G_BuildMapName(gamemap));
			Z_Free(title);
		}
		else
			fprintf(out, "Game map: %s\n", G_BuildMapName(gamemap));
	}

	fprintf(out, "Time of crash: %s\n", asctime(timeinfo));

	fprintf(out, "Caused by: ");

	switch (reason.type)
	{
		case BTCRASH_SIGNAL:
			I_PrintSignal(reason.value.signal, false, sig_msg);
			fprintf(out, "%s", sig_msg);
		break;

		case BTCRASH_ERRORMSG:
			fprintf(out, "%s", reason.value.errormsg);
		break;
	}

	fprintf(out, "\nBacktrace:\n");

	// Try to get full backtrace, it will print files and line numbers
	backtrace_full(bt_state, 2, bt_full_cb, bt_error_cb, (void*)&buf);

	if (buf.error)
	{
		// Fall back to simple backtrace, only prints function names
		backtrace_simple(bt_state, 2, bt_simple_cb, NULL, (void*)&buf);
	}

	fputs(backtrace, out);

	if (out != stderr)
	{
		fclose(out);
		fprintf(stderr, "Crash report created, find %s in your SRB2Kart directory\n", CRASH_LOGFILE_NAME);
	}
}
#endif

static void I_PrintSignal(int32_t signal_num, dboolean core_dumped, char *signal_msg)
{
	const char *sigmsg;

	switch (signal_num)
	{
#ifdef SIGINT
		case SIGINT:
			sigmsg = ("SIGINT - BlanKart was interrupted prematurely by the user.");
			break;
#endif
		case SIGILL: // illegal instruction - invalid function image
			sigmsg = ("SIGILL - BlanKart has attempted to execute an illegal instruction and needs to close.");
			break;
		case SIGFPE: // mathematical exception
			sigmsg = ("SIGFPE - BlanKart has encountered a mathematical exception and needs to close.\n\nThe most common cause of this is a division by zero in the game's code.");
			break;
		case SIGSEGV: // segment violation
			// Note: say "SIGGY SEGGY" like "CHICKEN JOCKEY"
			sigmsg = ("\"SIGGY SEGGY!\"\n\nSIGSEGV - BlanKart has attempted a read or write to a memory location that it isn't allowed to and needs to close.\n\nThis may be due to NULL pointers or garbage data left in memory.");
			break;
#ifdef SIGTERM
		case SIGTERM: // Software termination signal from terminate
			sigmsg = ("SIGTERM - BlanKart was terminated by a terminate signal.");
			break;
#endif
#ifdef SIGKILL
		case SIGKILL: // Software termination signal from kill
			sigmsg = ("SIGKILL - BlanKart was terminated by a kill signal.");
			break;
#endif
#ifdef SIGBREAK
		case SIGBREAK: // Ctrl-Break sequence
			sigmsg =("SIGBREAK - BlanKart was terminated by a Ctrl-Break sequence.")
			break;
#endif
		case SIGABRT: // abnormal termination triggered by abort call
			sigmsg = ("SIGABRT - BlanKart was terminated by an abort signal.\n\nThis may be caused by abort() calls from external modules or memory allocation errors.");
			break;
		default:
			snprintf(signal_msg, 512, "Signal number %d", signal_num);
			sigmsg = (core_dumped ? "Unknown signal" : signal_msg);
			break;
	}

	if (core_dumped)
	{
		if (sigmsg)
		{
			snprintf(signal_msg, 512, "%s (core dumped)", sigmsg);
		}
		else
		{
			strcat(signal_msg, " (core dumped)");
		}
	}
	else
	{
		snprintf(signal_msg, 512, "%s", sigmsg);
	}
}

static void I_ReportSignal(int num, int coredumped)
{
	char sigmsg[512];

	I_PrintSignal(num, coredumped, sigmsg);
	size_t len = strlen(sigmsg);
	snprintf(sigmsg + len, sizeof(sigmsg) - len, "\n\nCrash report has been saved into %s", CRASH_LOGFILE_NAME);
	I_OutputMsg("\nProcess killed by signal: %s\n\n", sigmsg);
}

dboolean I_In_Exiting_Signal_Handler(void)
{
	return g_in_exiting_signal_handler;
}

#ifndef NEWSIGNALHANDLER
static ATTRNORETURN void signal_handler(int32_t num)
{
	g_in_exiting_signal_handler = true;

	if (g_main_thread_id != std::this_thread::get_id())
	{
		// Do not attempt any sort of recovery if this signal triggers off the main thread
		signal(num, SIG_DFL);
		raise(num);
		exit(-2);
	}

	D_QuitNetGame(); // Fix server freezes
	CL_AbortDownloadResume();
#ifdef HAVE_LIBBACKTRACE
	write_backtrace(BT_CRASH_REASON_SIGNAL(num));
#endif
	I_ReportSignal(num, 0);
	signal(num, SIG_DFL);               //default signal action
	raise(num);
}
#endif

static volatile sig_atomic_t interrupted = false;

dboolean I_Interrupted(void)
{
	return interrupted;
}

static void quit_handler(int num)
{
	(void)num;
	interrupted = true;
}

#ifdef HAVE_TERMIOS
// TERMIOS console code from Quake3: thank you!
dboolean stdin_active = true;

typedef struct
{
	size_t cursor;
	char buffer[256];
} feild_t;

feild_t tty_con;

// when printing general stuff to stdout stderr (Sys_Printf)
//   we need to disable the tty console stuff
// this increments so we can recursively disable
static int32_t ttycon_hide = 0;
// some key codes that the terminal may be using
// TTimo NOTE: I'm not sure how relevant this is
static int32_t tty_erase;
static int32_t tty_eof;

static struct termios tty_tc;

// =============================================================
// tty console routines
// NOTE: if the user is editing a line when something gets printed to the early console then it won't look good
//   so we provide tty_Clear and tty_Show to be called before and after a stdout or stderr output
// =============================================================

// flush stdin, I suspect some terminals are sending a LOT of garbage
// FIXME TTimo relevant?
#if 0
static inline void tty_FlushIn(void)
{
	char key;
	while (read(STDIN_FILENO, &key, 1)!=-1);
}
#endif

// do a backspace
// TTimo NOTE: it seems on some terminals just sending '\b' is not enough
//   so for now, in any case we send "\b \b" .. yeah well ..
//   (there may be a way to find out if '\b' alone would work though)
static void tty_Back(void)
{
	char key;
	ssize_t d;
	key = '\b';
	d = write(STDOUT_FILENO, &key, 1);
	key = ' ';
	d = write(STDOUT_FILENO, &key, 1);
	key = '\b';
	d = write(STDOUT_FILENO, &key, 1);
	(void)d;
}

static void tty_Clear(void)
{
	size_t i;
	if (tty_con.cursor>0)
	{
		for (i=0; i<tty_con.cursor; i++)
		{
			tty_Back();
		}
	}

}

// clear the display of the line currently edited
// bring cursor back to beginning of line
static inline void tty_Hide(void)
{
	//I_Assert(consolevent);
	if (ttycon_hide)
	{
		ttycon_hide++;
		return;
	}
	tty_Clear();
	ttycon_hide++;
}

// show the current line
// FIXME TTimo need to position the cursor if needed??
static inline void tty_Show(void)
{
	size_t i;
	ssize_t d;
	//I_Assert(consolevent);
	I_Assert(ttycon_hide>0);
	ttycon_hide--;
	if (ttycon_hide == 0 && tty_con.cursor)
	{
		for (i=0; i<tty_con.cursor; i++)
		{
			d = write(STDOUT_FILENO, tty_con.buffer+i, 1);
		}
	}
	(void)d;
}

// never exit without calling this, or your terminal will be left in a pretty bad state
static void I_ShutdownConsole(void)
{
	if (consolevent)
	{
		I_OutputMsg("Shutdown tty console\n");
		consolevent = false;
		tcsetattr (STDIN_FILENO, TCSADRAIN, &tty_tc);
	}
}

static void I_StartupConsole(void)
{
	struct termios tc;

	// TTimo
	// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=390 (404)
	// then SIGTTIN or SIGTOU is emitted, if not catched, turns into a SIGSTP
	signal(SIGTTIN, SIG_IGN);
	signal(SIGTTOU, SIG_IGN);

	consolevent = static_cast<dboolean>(!M_CheckParm("-noconsole"));
	framebuffer = static_cast<dboolean>(M_CheckParm("-framebuffer"));

	if (framebuffer)
		consolevent = false;

	if (!consolevent) return;

	if (isatty(STDIN_FILENO)!=1)
	{
		I_OutputMsg("stdin is not a tty, tty console mode failed\n");
		consolevent = false;
		return;
	}
	memset(&tty_con, 0x00, sizeof(tty_con));
	tcgetattr (0, &tty_tc);
	tty_erase = tty_tc.c_cc[VERASE];
	tty_eof = tty_tc.c_cc[VEOF];
	tc = tty_tc;
	/*
	 ECHO: don't echo input characters
	 ICANON: enable canonical mode.  This  enables  the  special
	  characters  EOF,  EOL,  EOL2, ERASE, KILL, REPRINT,
	  STATUS, and WERASE, and buffers by lines.
	 ISIG: when any of the characters  INTR,  QUIT,  SUSP,  or
	  DSUSP are received, generate the corresponding signal
	*/
	tc.c_lflag &= ~(ECHO | ICANON);
	/*
	 ISTRIP strip off bit 8
	 INPCK enable input parity checking
	 */
	tc.c_iflag &= ~(ISTRIP | INPCK);
	tc.c_cc[VMIN] = 0; //1?
	tc.c_cc[VTIME] = 0;
	tcsetattr (0, TCSADRAIN, &tc);
}

void I_GetConsoleEvents(void)
{
	// we use this when sending back commands
	event_t ev = {};
	char key = 0;
	ssize_t d;

	if (!consolevent)
		return;

	ev.type = ev_console;
	if (read(STDIN_FILENO, &key, 1) == -1 || !key)
		return;

	// we have something
	// backspace?
	// NOTE TTimo testing a lot of values .. seems it's the only way to get it to work everywhere
	if ((key == tty_erase) || (key == 127) || (key == 8))
	{
		if (tty_con.cursor > 0)
		{
			tty_con.cursor--;
			tty_con.buffer[tty_con.cursor] = '\0';
			tty_Back();
		}
		ev.data1 = KEY_BACKSPACE;
	}
	else if (key < ' ') // check if this is a control char
	{
		if (key == '\n')
		{
			tty_Clear();
			tty_con.cursor = 0;
			ev.data1 = KEY_ENTER;
		}
		else return;
	}
	else
	{
		// push regular character
		ev.data1 = tty_con.buffer[tty_con.cursor] = key;
		tty_con.cursor++;
		// print the current line (this is differential)
		d = write(STDOUT_FILENO, &key, 1);
	}
	if (ev.data1) D_PostEvent(&ev);
	//tty_FlushIn();
	(void)d;
}

#elif defined (_WIN32)
static BOOL I_ReadyConsole(HANDLE ci)
{
	DWORD gotinput;
	if (ci == INVALID_HANDLE_VALUE) return FALSE;
	if (WaitForSingleObject(ci,0) != WAIT_OBJECT_0) return FALSE;
	if (GetFileType(ci) != FILE_TYPE_CHAR) return FALSE;
	if (!GetConsoleMode(ci, &gotinput)) return FALSE;
	return (GetNumberOfConsoleInputEvents(ci, &gotinput) && gotinput);
}

static dboolean entering_con_command = false;

static void Impl_HandleKeyboardConsoleEvent(KEY_EVENT_RECORD evt, HANDLE co)
{
	event_t event;
	CONSOLE_SCREEN_BUFFER_INFO CSBI;
	DWORD t;

	memset(&event,0x00,sizeof (event));

	if (evt.bKeyDown)
	{
		event.type = ev_console;
		entering_con_command = true;
		switch (evt.wVirtualKeyCode)
		{
			case VK_ESCAPE:
			case VK_TAB:
				event.data1 = KEY_NULL;
				break;
			case VK_RETURN:
				entering_con_command = false;
				/* FALLTHRU */
			default:
				//event.data1 = MapVirtualKey(evt.wVirtualKeyCode,2); // convert in to char
				event.data1 = evt.uChar.AsciiChar;
		}
		if (co != INVALID_HANDLE_VALUE && GetFileType(co) == FILE_TYPE_CHAR && GetConsoleMode(co, &t))
		{
			if (event.data1 && event.data1 != KEY_LSHIFT && event.data1 != KEY_RSHIFT)
			{
#ifdef _UNICODE
				WriteConsole(co, &evt.uChar.UnicodeChar, 1, &t, NULL);
#else
				WriteConsole(co, &evt.uChar.AsciiChar, 1 , &t, NULL);
#endif
			}
			if (evt.wVirtualKeyCode == VK_BACK
				&& GetConsoleScreenBufferInfo(co,&CSBI))
			{
				WriteConsoleOutputCharacterA(co, " ",1, CSBI.dwCursorPosition, &t);
			}
		}
	}
	if (event.data1) D_PostEvent(&event);
}

void I_GetConsoleEvents(void)
{
	HANDLE ci = GetStdHandle(STD_INPUT_HANDLE);
	HANDLE co = GetStdHandle(STD_OUTPUT_HANDLE);
	INPUT_RECORD input;
	DWORD t;

	while (I_ReadyConsole(ci) && ReadConsoleInput(ci, &input, 1, &t) && t)
	{
		switch (input.EventType)
		{
			case KEY_EVENT:
				Impl_HandleKeyboardConsoleEvent(input.Event.KeyEvent, co);
				break;
			case MOUSE_EVENT:
			case WINDOW_BUFFER_SIZE_EVENT:
			case MENU_EVENT:
			case FOCUS_EVENT:
				break;
		}
	}
}

static void I_StartupConsole(void)
{
	HANDLE ci, co;
	const int32_t ded = true;
	BOOL gotConsole = FALSE;
	if (M_CheckParm("-console") || ded)
		gotConsole = AllocConsole();
#ifdef _DEBUG
	else if (M_CheckParm("-noconsole") && !ded)
#else
	else if (!M_CheckParm("-console") && !ded)
#endif
	{
		FreeConsole();
		gotConsole = FALSE;
	}

	if (gotConsole)
	{
		SetConsoleTitleA("SRB2Kart Console");
		consolevent = true;
	}

	//Let get the real console HANDLE, because Mingw's Bash is bad!
	ci = CreateFile(TEXT("CONIN$") ,               GENERIC_READ, FILE_SHARE_READ,  NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	co = CreateFile(TEXT("CONOUT$"), GENERIC_WRITE|GENERIC_READ, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (ci != INVALID_HANDLE_VALUE)
	{
		const DWORD CM = ENABLE_LINE_INPUT|ENABLE_ECHO_INPUT|ENABLE_PROCESSED_INPUT;
		SetStdHandle(STD_INPUT_HANDLE, ci);
		if (GetFileType(ci) == FILE_TYPE_CHAR)
			SetConsoleMode(ci, CM); //default mode but no ENABLE_MOUSE_INPUT
	}
	if (co != INVALID_HANDLE_VALUE)
	{
		SetStdHandle(STD_OUTPUT_HANDLE, co);
		SetStdHandle(STD_ERROR_HANDLE, co);
	}
}
static inline void I_ShutdownConsole(void){}
#else
void I_GetConsoleEvents(void){}
static inline void I_StartupConsole(void)
{
#ifdef _DEBUG
	consolevent = !M_CheckParm("-noconsole");
#else
	consolevent = M_CheckParm("-console");
#endif

	framebuffer = M_CheckParm("-framebuffer");

	if (framebuffer)
		consolevent = false;
}
static inline void I_ShutdownConsole(void){}
#endif

//
// StartupKeyboard
//
static void I_RegisterSignals (void)
{
	g_main_thread_id = std::this_thread::get_id();

#ifdef SIGINT
	signal(SIGINT , quit_handler);
#endif
#ifdef SIGBREAK
	signal(SIGBREAK , quit_handler);
#endif
#ifdef SIGTERM
	signal(SIGTERM , quit_handler);
#endif

	// If these defines don't exist,
	// then compilation would have failed above us...
#ifndef NEWSIGNALHANDLER
	signal(SIGILL , signal_handler);
	signal(SIGSEGV , signal_handler);
	signal(SIGABRT , signal_handler);
	signal(SIGFPE , signal_handler);
#endif
}

#ifdef NEWSIGNALHANDLER
static void signal_handler_child(int32_t num)
{
#ifdef HAVE_LIBBACKTRACE
	write_backtrace(BT_CRASH_REASON_SIGNAL(num));
#endif

	signal(num, SIG_DFL);               //default signal action
	raise(num);
}

static void I_RegisterChildSignals(void)
{
	// If these defines don't exist,
	// then compilation would have failed above us...
	signal(SIGILL , signal_handler_child);
	signal(SIGSEGV , signal_handler_child);
	signal(SIGABRT , signal_handler_child);
	signal(SIGFPE , signal_handler_child);
}
#endif

//
// I_OutputMsg
//
void I_OutputMsg(const char *fmt, ...)
{
	size_t len;
	char txt[8192] = {};
	va_list  argptr;

	if (!fmt)
		return;

	va_start(argptr,fmt);
	vsnprintf(txt, 8192, fmt, argptr);
	va_end(argptr);

	len = strnlen(txt, sizeof(txt));

#ifdef LOGMESSAGES
	if (logstream)
	{
		size_t d = fwrite(txt, len, 1, logstream);
		fflush(logstream);
		(void)d;
	}
#endif

#if defined (_WIN32)
#ifdef DEBUGFILE
	if (debugfile != stderr)
#endif
	{
		HANDLE co = GetStdHandle(STD_OUTPUT_HANDLE);
		DWORD bytesWritten;

		if (co == INVALID_HANDLE_VALUE)
			return;

		if (GetFileType(co) == FILE_TYPE_CHAR && GetConsoleMode(co, &bytesWritten))
		{
			static COORD coordNextWrite = {0,0};
			LPVOID oldLines = NULL;
			INT oldLength;
			CONSOLE_SCREEN_BUFFER_INFO csbi;

			// Save the lines that we're going to obliterate.
			GetConsoleScreenBufferInfo(co, &csbi);
			oldLength = csbi.dwSize.X * (csbi.dwCursorPosition.Y - coordNextWrite.Y) + csbi.dwCursorPosition.X - coordNextWrite.X;

			if (oldLength > 0)
			{
				LPVOID blank = malloc(oldLength);
				if (!blank) return;
				memset(blank, ' ', oldLength); // Blank out.
				oldLines = malloc(oldLength*sizeof(TCHAR));
				if (!oldLines)
				{
					free(blank);
					return;
				}

				ReadConsoleOutputCharacter(co, (LPSTR)oldLines, oldLength, coordNextWrite, &bytesWritten);

				// Move to where we what to print - which is where we would've been,
				// had console input not been in the way,
				SetConsoleCursorPosition(co, coordNextWrite);

				WriteConsoleA(co, blank, oldLength, &bytesWritten, NULL);
				free(blank);

				// And back to where we want to print again.
				SetConsoleCursorPosition(co, coordNextWrite);
			}

			// Actually write the string now!
			WriteConsoleA(co, txt, (DWORD)len, &bytesWritten, NULL);

			// Next time, output where we left off.
			GetConsoleScreenBufferInfo(co, &csbi);
			coordNextWrite = csbi.dwCursorPosition;

			// Restore what was overwritten.
			if (oldLines && entering_con_command)
				WriteConsole(co, oldLines, oldLength, &bytesWritten, NULL);
			if (oldLines) free(oldLines);
		}
		else // Redirected to a file.
			WriteFile(co, txt, (DWORD)len, &bytesWritten, NULL);
	}
#else
#ifdef HAVE_TERMIOS
	if (consolevent)
	{
		tty_Hide();
	}
#endif

	if (!framebuffer)
		fprintf(stderr, "%s", txt);
#ifdef HAVE_TERMIOS
	if (consolevent)
	{
		tty_Show();
	}
#endif

	// 2004-03-03 AJR Since not all messages end in newline, some were getting displayed late.
	if (!framebuffer)
		fflush(stderr);

#endif
}

//
// I_GetKey
//
int32_t I_GetKey (void)
{
	// Warning: I_GetKey empties the event queue till next keypress
	event_t *ev;
	int32_t rc = 0;

	// return the first keypress from the event queue
	for (; eventtail != eventhead; eventtail = (eventtail+1)&(MAXEVENTS-1))
	{
		ev = &events[eventtail];
		if (ev->type == ev_keydown || ev->type == ev_console)
		{
			rc = ev->data1;
			continue;
		}
	}

	return rc;
}

#ifndef NOMUMBLE
#ifdef HAVE_MUMBLE
// Best Mumble positional audio settings:
// Minimum distance 3.0 m
// Bloom 175%
// Maximum distance 80.0 m
// Minimum volume 50%
#define DEG2RAD (0.017453292519943295769236907684883l) // TAU/360 or PI/180
#define MUMBLEUNIT (64.0f) // FRACUNITS in a Meter

static struct mumble_s {
#ifdef WINMUMBLE
	uint32_t uiVersion;
	DWORD uiTick;
#else
	uint32_t uiVersion;
	uint32_t uiTick;
#endif
	float fAvatarPosition[3];
	float fAvatarFront[3];
	float fAvatarTop[3]; // defaults to Y-is-up (only used for leaning)
	wchar_t name[256]; // game name
	float fCameraPosition[3];
	float fCameraFront[3];
	float fCameraTop[3]; // defaults to Y-is-up (only used for leaning)
	wchar_t identity[256]; // player id
	uint32_t context_len;
	unsigned char context[256]; // server/team
	wchar_t description[2048]; // game description
} *mumble = NULL;
#endif // HAVE_MUMBLE

static void I_SetupMumble(void)
{
#ifdef WINMUMBLE
	HANDLE hMap = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, L"MumbleLink");
	if (!hMap)
		return;

	mumble = static_cast<mumble_s*>(MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*mumble)));
	if (!mumble)
		CloseHandle(hMap);
#elif defined (HAVE_SHM)
	int shmfd;
	char memname[256];

	snprintf(memname, 256, "/MumbleLink.%d", getuid());
	shmfd = shm_open(memname, O_RDWR, S_IRUSR | S_IWUSR);

	if(shmfd < 0)
		return;

	mumble = static_cast<mumble_s*>(mmap(NULL, sizeof(*mumble), PROT_READ | PROT_WRITE, MAP_SHARED, shmfd, 0));
	if (mumble == MAP_FAILED)
		mumble = NULL;
#endif
}

void I_UpdateMumble(const mobj_t *mobj, const listener_t listener)
{
#ifdef HAVE_MUMBLE
	double angle;
	fixed_t anglef;

	if (!mumble)
		return;

	if(mumble->uiVersion != 2) {
		wcsncpy(mumble->name, L"BlanKart " VERSIONSTRINGW, 256);
		wcsncpy(mumble->description, L"BlanKart with integrated Mumble Link support.", 2048);
		mumble->uiVersion = 2;
	}
	mumble->uiTick++;

	if (!netgame || gamestate != GS_LEVEL) { // Zero out, but never delink.
		mumble->fAvatarPosition[0] = mumble->fAvatarPosition[1] = mumble->fAvatarPosition[2] = 0.0f;
		mumble->fAvatarFront[0] = 1.0f;
		mumble->fAvatarFront[1] = mumble->fAvatarFront[2] = 0.0f;
		mumble->fCameraPosition[0] = mumble->fCameraPosition[1] = mumble->fCameraPosition[2] = 0.0f;
		mumble->fCameraFront[0] = 1.0f;
		mumble->fCameraFront[1] = mumble->fCameraFront[2] = 0.0f;
		return;
	}

	{
		uint8_t *p = mumble->context;
		WRITEMEM(p, server_context, 8);
		WRITEINT16(p, gamemap);
		mumble->context_len = (uint32_t)(p - mumble->context);
	}

	if (mobj) {
		mumble->fAvatarPosition[0] = FIXED_TO_FLOAT(mobj->x) / MUMBLEUNIT;
		mumble->fAvatarPosition[1] = FIXED_TO_FLOAT(mobj->z) / MUMBLEUNIT;
		mumble->fAvatarPosition[2] = FIXED_TO_FLOAT(mobj->y) / MUMBLEUNIT;

		anglef = AngleFixed(mobj->angle);
		angle = FIXED_TO_FLOAT(anglef)*DEG2RAD;
		mumble->fAvatarFront[0] = (float)cos(angle);
		mumble->fAvatarFront[1] = 0.0f;
		mumble->fAvatarFront[2] = (float)sin(angle);
	} else {
		mumble->fAvatarPosition[0] = mumble->fAvatarPosition[1] = mumble->fAvatarPosition[2] = 0.0f;
		mumble->fAvatarFront[0] = 1.0f;
		mumble->fAvatarFront[1] = mumble->fAvatarFront[2] = 0.0f;
	}

	mumble->fCameraPosition[0] = FIXED_TO_FLOAT(listener.x) / MUMBLEUNIT;
	mumble->fCameraPosition[1] = FIXED_TO_FLOAT(listener.z) / MUMBLEUNIT;
	mumble->fCameraPosition[2] = FIXED_TO_FLOAT(listener.y) / MUMBLEUNIT;

	anglef = AngleFixed(listener.angle);
	angle = FIXED_TO_FLOAT(anglef)*DEG2RAD;
	mumble->fCameraFront[0] = (float)cos(angle);
	mumble->fCameraFront[1] = 0.0f;
	mumble->fCameraFront[2] = (float)sin(angle);
#else
	(void)mobj;
	(void)listener;
#endif // HAVE_MUMBLE
}
#undef WINMUMBLE
#endif // NOMUMBLE

static ticcmd_t emptycmd[MAXSPLITSCREENPLAYERS];

/**	\brief empty ticcmd for player 1
*/

ticcmd_t *I_BaseTiccmd(void)
{
	return &emptycmd[0];
}

/**	\brief empty ticcmd for player 2
*/

ticcmd_t *I_BaseTiccmd2(void)
{
	return &emptycmd[1];
}

/**	\brief empty ticcmd for player 3
*/

ticcmd_t *I_BaseTiccmd3(void)
{
	return &emptycmd[2];
}

/**	\brief empty ticcmd for player 4
*/

ticcmd_t *I_BaseTiccmd4(void)
{
	return &emptycmd[3];
}

//
// I_GetTime
// returns time in 1/TICRATE second tics
//

static uint64_t timer_frequency;

precise_t I_GetPreciseTime(void)
{
#if defined (__unix__) || defined(__APPLE__) || defined (UNIXCOMMON)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (precise_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
#elif defined (_WIN32)
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (precise_t)counter.QuadPart;
#else
    return 0;
#endif
}

uint64_t I_GetPrecisePrecision(void)
{
#if defined (__unix__) || defined(__APPLE__) || defined (UNIXCOMMON)
    return 1000000000;
#elif defined (_WIN32)
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    return (uint64_t)frequency.QuadPart;
#else
    return 1000000;
#endif
}

static uint32_t frame_rate;

static double frame_frequency;
static uint64_t frame_epoch;

static void I_InitFrameTime(const uint64_t now, const uint32_t cap)
{
	frame_rate = cap;
	frame_epoch = now;

	if (frame_rate == 0)
	{
		// Shouldn't be used, but just in case...?
		frame_frequency = 1.0;
		return;
	}

	frame_frequency = timer_frequency / (double)frame_rate;
}

//
// I_StartupTimer
//
void I_StartupTimer(void){}

void I_Sleep(uint32_t ms)
{
#if defined (__unix__) || defined(__APPLE__) || defined (UNIXCOMMON)
	struct timespec ts = {
		.tv_sec = ms / 1000,
		.tv_nsec = ms % 1000 * 1000000,
	};
	int status;
	do status = clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, &ts);
	while (status == EINTR);
#elif defined (_WIN32)
	Sleep(ms);
#else
	(void)ms;
#warning No sleep function for this system!
#endif
}

void I_SleepDuration(precise_t duration)
{
#if defined(__linux__) || defined(__FreeBSD__) || defined(__HAIKU__) || defined(__OpenBSD__)
	uint64_t precision = I_GetPrecisePrecision();
	precise_t dest = I_GetPreciseTime() + duration;
#ifdef __OpenBSD__
	precise_t slack = (precision / 50); // 20 ms slack
#else
	precise_t slack = (precision / 5000); // 0.2 ms slack
#endif
	if (duration > slack)
	{
		duration -= slack;
		struct timespec ts = {
			.tv_sec = static_cast<__time_t>(duration / precision),
			.tv_nsec = static_cast<__syscall_slong_t>(duration * 1000000000 / precision % 1000000000),
		};
		int status;
#ifdef __OpenBSD__
		do status = nanosleep(&ts, &ts);
#else
		do status = clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, &ts);
#endif
		while (status == EINTR);
	}

	// busy-wait the rest
	while (((int64_t)dest - (int64_t)I_GetPreciseTime()) > 0);
#elif defined (MIN_SLEEP_DURATION_MS)
	uint64_t precision = I_GetPrecisePrecision();
	int32_t sleepvalue = cv_sleep.value;
	uint64_t delaygranularity;
	precise_t cur;
	precise_t dest;

	{
		double gran = round(((double)(precision / 1000) * sleepvalue * MIN_SLEEP_DURATION_MS));
		delaygranularity = (uint64_t)gran;
	}

	cur = I_GetPreciseTime();
	dest = cur + duration;

	// the reason this is not dest > cur is because the precise counter may wrap
	// two's complement arithmetic is our friend here, though!
	// e.g. cur 0xFFFFFFFFFFFFFFFE = -2, dest 0x0000000000000001 = 1
	// 0x0000000000000001 - 0xFFFFFFFFFFFFFFFE = 3
	while ((int64_t)(dest - cur) > 0)
	{
		// If our cv_sleep value exceeds the remaining sleep duration, use the
		// hard sleep function.
		if (sleepvalue > 0 && (dest - cur) > delaygranularity)
		{
			I_Sleep(sleepvalue);
		}

		// Otherwise, this is a spinloop.

		cur = I_GetPreciseTime();
	}
#endif
}

#ifdef NEWSIGNALHANDLER
static void newsignalhandler_Warn(const char *pr)
{
	char text[128];

	snprintf(text, sizeof text,
			"Error while setting up signal reporting: %s: %s",
			pr,
			strerror(errno)
	);

	I_OutputMsg("%s\n", text);

	I_ShutdownConsole();
	exit(-1);
}

static void I_Fork(void)
{
	int child;
	int status;
	int signum;
	int c;

	child = fork();

	switch (child)
	{
		case -1:
			newsignalhandler_Warn("fork()");
			break;
		case 0:
			I_RegisterChildSignals();
			break;
		default:
			// ignore those, those are handled by child process
			// otherwise parent might exit before it
			// and the below stuff wont run and your terminal will be left in an awkward state
#ifdef SIGINT
			signal(SIGINT,   SIG_IGN);
#endif
#ifdef SIGBREAK
			signal(SIGBREAK, SIG_IGN);
#endif
#ifdef SIGTERM
			signal(SIGTERM,  SIG_IGN);
#endif
			if (logstream)
				fclose(logstream);/* the child has this */

			c = wait(&status);

#ifdef LOGMESSAGES
			/* By the way, exit closes files. */
			logstream = fopen(logfilename, "at");
#else
			logstream = 0;
#endif

			if (c == -1)
			{
				kill(child, SIGKILL);
				newsignalhandler_Warn("wait()");
			}
			else
			{
				if (WIFSIGNALED (status))
				{
					signum = WTERMSIG (status);
#ifdef WCOREDUMP
					I_ReportSignal(signum, WCOREDUMP (status));
#else
					I_ReportSignal(signum, 0);
#endif
					status = 128 + signum;
				}
				else if (WIFEXITED (status))
				{
					status = WEXITSTATUS (status);
				}

				I_ShutdownConsole();
				exit(status);
			}
	}
}
#endif/*NEWSIGNALHANDLER*/

int32_t I_StartupSystem(void)
{
	I_StartupConsole();
#ifdef NEWSIGNALHANDLER
	// This is useful when debugging. It lets GDB attach to
	// the correct process easily.
	if (!M_CheckParm("-nofork"))
		I_Fork();
#endif
#ifdef HAVE_THREADS
	I_start_threads();
	I_AddExitFunc(I_stop_threads);
	I_ThreadPoolInit();
	I_AddExitFunc(I_ThreadPoolShutdown);
#endif
	I_RegisterSignals();
#ifndef NOMUMBLE
	I_SetupMumble();
#endif
	return 0;
}

dboolean is_quitting = false;

//
// I_Quit
//
void I_Quit(void)
{
	/* prevent recursive I_Quit() */
	if (is_quitting)
		abort();

	is_quitting = true;

	if (initedvoiceprefs)
		R_SaveVoicePreferences();

	M_SaveConfig(NULL); //save game config, cvars..
	D_SaveBan(); // save the ban list

	// Make sure you lose points for ALT-F4
	if (Playing())
		K_PlayerForfeit(consoleplayer, true);

	G_SaveGameData(); // Tails 12-08-2002
	//added:16-02-98: when recording a demo, should exit using 'q' key,
	//        but sometimes we forget and use 'F10'.. so save here too.

	if (demo.recording)
		G_CheckDemoStatus();
	if (metalrecording)
		G_StopMetalRecording(false);

	D_QuitNetGame();
	CL_AbortDownloadResume();
	M_FreePlayerSetupColors();
	I_ShutdownMusic();
	I_ShutdownSound();
	// use this for 1.28 19990220 by Kin
	I_ShutdownGraphics();
	I_ShutdownSystem();
	/* if option -noendtxt is set, don't print the text */
	if (!M_CheckParm("-noendtxt") && W_CheckNumForName("ENDOOM") != LUMPERROR)
	{
		printf("\r");
		ShowEndTxt();
	}
	if (myargmalloc)
		free(myargv); // Deallocate allocated memory

	exit(0);
}

void I_WaitVBL(int32_t count)
{
	count = 1;
}

void I_BeginRead(void)
{
}

void I_EndRead(void)
{
}

//
// I_Error
//
/**	\brief phuck recursive errors
*/
static int32_t errorcount = 0;

/**	\brief recursive error detecting
*/
static dboolean shutdowning = false;

FUNCIERROR void ATTRNORETURN I_Error(const char *error, ...)
{
	va_list argptr;
	char buffer[8192];

	if (std::this_thread::get_id() != g_main_thread_id)
	{
		// Do not attempt a graceful shutdown. Errors off the main thread are unresolvable.
		exit(-2);
	}

	// recursive error detecting
	if (shutdowning)
	{
		errorcount++;
		if (errorcount == 1)
			;
		// try to shutdown each subsystem separately
		if (errorcount == 2)
			I_ShutdownMusic();
		if (errorcount == 3)
			I_ShutdownSound();
		if (errorcount == 4)
			I_ShutdownGraphics();
		if (errorcount == 5)
			;
		if (errorcount == 6)
			I_ShutdownSystem();
		if (errorcount == 7)
			;
		if (errorcount == 8)
		{
			if (initedvoiceprefs)
				R_SaveVoicePreferences();

			M_SaveConfig(NULL);
			G_SaveGameData();
		}
		if (errorcount > 20)
		{
			va_start(argptr, error);
			vsnprintf(buffer, 8192, error, argptr);
			va_end(argptr);

			exit(-1); // recursive errors detected
		}
	}
	else
	{
		// This makes crashes funnier by stimulating the funnicampus of the brain
		S_StopSounds();
		S_StartSound(NULL, sfx_s3k35);
	}

	shutdowning = true;

	// Display error message in the console before we start shutting it down
	va_start(argptr, error);
	vsnprintf(buffer, 8192, error, argptr);
	va_end(argptr);
	I_OutputMsg("\nI_Error(): %s\n", buffer);
	// ---

#ifdef HAVE_LIBBACKTRACE
	write_backtrace(BT_CRASH_REASON_ERRORMSG(buffer));
#endif

	if (initedvoiceprefs)
		R_SaveVoicePreferences();

	M_SaveConfig(NULL); // save game config, cvars..
	D_SaveBan(); // save the ban list
	G_SaveGameData(); // Tails 12-08-2002

	// Shutdown. Here might be other errors.

	if (demo.recording)
		G_CheckDemoStatus();
	if (metalrecording)
		G_StopMetalRecording(false);

	D_QuitNetGame();
	CL_AbortDownloadResume();
	M_FreePlayerSetupColors();

	I_ShutdownMusic();
	I_ShutdownGraphics();

	// We wait until now to do this so the funny sound can be heard
	I_ShutdownSound();
	// use this for 1.28 19990220 by Kin
	I_ShutdownSystem();

#if defined (PARANOIA) || defined (DEVELOP)
	*(volatile int32_t *)0 = 4; //Alam: Debug!
#endif

	exit(-1);
}

/**	\brief quit function table
*/
static quitfuncptr quit_funcs[MAX_QUIT_FUNCS]; /* initialized to all bits 0 */

//
//  Adds a function to the list that need to be called by I_SystemShutdown().
//
void I_AddExitFunc(void (*func)())
{
	int32_t c;

	for (c = 0; c < MAX_QUIT_FUNCS; c++)
	{
		if (!quit_funcs[c])
		{
			quit_funcs[c] = func;
			break;
		}
	}
}


//
//  Removes a function from the list that need to be called by
//   I_SystemShutdown().
//
void I_RemoveExitFunc(void (*func)())
{
	int32_t c;

	for (c = 0; c < MAX_QUIT_FUNCS; c++)
	{
		if (quit_funcs[c] == func)
		{
			while (c < MAX_QUIT_FUNCS-1)
			{
				quit_funcs[c] = quit_funcs[c+1];
				c++;
			}
			quit_funcs[MAX_QUIT_FUNCS-1] = NULL;
			break;
		}
	}
}

#if !(defined (__unix__) || defined(__APPLE__) || defined (UNIXCOMMON))
static void Shittycopyerror(const char *name)
{
	I_OutputMsg(
			"Error copying log file: %s: %s\n",
			name,
			strerror(errno)
	);
}

static void Shittylogcopy(void)
{
	char buf[8192];
	FILE *fp;
	size_t r;
	if (fseek(logstream, 0, SEEK_SET) == -1)
	{
		Shittycopyerror("fseek");
	}
	else if (( fp = fopen(logfilename, "wt") ))
	{
		while (( r = fread(buf, 1, sizeof buf, logstream) ))
		{
			if (fwrite(buf, 1, r, fp) < r)
			{
				Shittycopyerror("fwrite");
				break;
			}
		}
		if (ferror(logstream))
		{
			Shittycopyerror("fread");
		}
		fclose(fp);
	}
	else
	{
		Shittycopyerror(logfilename);
	}
}
#endif/*!(defined (__unix__) || defined(__APPLE__) || defined (UNIXCOMMON))*/

//
//  Closes down everything. This includes restoring the initial
//  palette and video mode, and removing whatever mouse, keyboard, and
//  timer routines have been installed.
//
//  NOTE: Shutdown user funcs are effectively called in reverse order.
//
void I_ShutdownSystem(void)
{
	int32_t c;

#ifdef NEWSIGNALHANDLER
	if (M_CheckParm("-nofork"))
#endif
		I_ShutdownConsole();

	for (c = MAX_QUIT_FUNCS-1; c >= 0; c--)
		if (quit_funcs[c])
			(*quit_funcs[c])();
#ifdef LOGMESSAGES
	if (logstream)
	{
		I_OutputMsg("I_ShutdownSystem(): end of logstream.\n");
#if !(defined (__unix__) || defined(__APPLE__) || defined (UNIXCOMMON))
		Shittylogcopy();
#endif
		fclose(logstream);
		logstream = NULL;
	}
#endif

}

void I_GetDiskFreeSpace(int64_t *freespace)
{
#if defined (__unix__) || defined(__APPLE__) || defined (UNIXCOMMON)
#if defined (SOLARIS) || defined (__HAIKU__)
	*freespace = INT32_MAX;
	return;
#else // Both Linux and BSD have this, apparently.
	struct statfs stfs;
	if (statfs(srb2home, &stfs) == -1)
	{
		*freespace = INT32_MAX;
		return;
	}
	*freespace = stfs.f_bavail * stfs.f_bsize;
#endif
#elif defined (_WIN32)
	ULARGE_INTEGER BytesAvailableCaller, TotalBytes, TotalFreeBytes;
	GetDiskFreeSpaceEx(NULL, &BytesAvailableCaller, &TotalBytes,
						&TotalFreeBytes);
	*freespace = TotalFreeBytes.QuadPart;
#else // Dummy for platform independent; 1GB should be enough
	*freespace = 1024*1024*1024;
#endif
}

char *I_GetUserName(void)
{
	static char username[MAXPLAYERNAME+1];
	char *p;
#ifdef _WIN32
	DWORD i = MAXPLAYERNAME;

	if (!GetUserNameA(username, &i))
#endif
	{
		p = I_GetEnv("USER");
		if (!p)
		{
			p = I_GetEnv("user");
			if (!p)
			{
				p = I_GetEnv("USERNAME");
				if (!p)
				{
					p = I_GetEnv("username");
					if (!p)
					{
						return NULL;
					}
				}
			}
		}
		strncpy(username, p, MAXPLAYERNAME);
	}


	if (username[0] != '\0')
		return username;
	return NULL; // dummy for platform independent version
}

int32_t I_mkdir(const char *dirname, int32_t unixright)
{
//[segabor]
#if defined (__unix__) || defined(__APPLE__) || defined (UNIXCOMMON) || defined (__CYGWIN__)
	return mkdir(dirname, unixright);
#elif defined (_WIN32)
	UNREFERENCED_PARAMETER(unixright); /// \todo should implement ntright under nt...
	return CreateDirectoryA(dirname, NULL);
#else
	(void)dirname;
	(void)unixright;
	return false;
#endif
}

int32_t I_ChDir(const char *path)
{
#ifdef _WIN32
	return (SetCurrentDirectoryA(path) ? 0 : -1);
#else
	return chdir(path);
#endif
}

char *I_GetCwd(char *buf, size_t size)
{
#ifdef _WIN32
	return (GetCurrentDirectoryA((DWORD)size, buf) ? buf : NULL);
#else
	return getcwd(buf, size);
#endif
}

char *I_GetEnv(const char *name)
{
	return getenv(name);
}

size_t I_GetRandomBytes(char *destination, size_t count)
{
#if defined (__unix__) || defined (UNIXCOMMON) || defined(__APPLE__)
	FILE *rndsource;
	size_t actual_bytes = 0;

	if (!(rndsource = fopen("/dev/urandom", "r")))
		if (!(rndsource = fopen("/dev/random", "r")))
			actual_bytes = 0;

	if (rndsource)
	{
		actual_bytes = fread(destination, 1, count, rndsource);
		fclose(rndsource);
	}

	if (actual_bytes == 0)
		I_OutputMsg("I_GetRandomBytes(): couldn't get any random bytes");

	return actual_bytes;
#elif defined (_WIN32)
	if (RtlGenRandom(destination, count))
		return count;

	I_OutputMsg("I_GetRandomBytes(): couldn't get any random bytes");
	return 0;
#else
	#warning I_GetRandomBytes is not implemented on this platform.
	return 0;
#endif
}

int32_t I_PutEnv(char *variable)
{
	return putenv(variable);
}

int32_t I_ClipboardCopy(const char *data, size_t size)
{
	return -1;
}

const char *I_ClipboardPaste(void)
{
	return NULL;
}

/**	\brief	The isWadPathOk function

	\param	path	string path to check

	\return if true, wad file found


*/
static dboolean isWadPathOk(const char *path)
{
	char *wad3path = static_cast<char*>(malloc(256));

	if (!wad3path)
		return false;

	snprintf(wad3path, 256, pandf, path, WADKEYWORD);

	if (FIL_ReadFileOK(wad3path))
	{
		free(wad3path);
		return true;
	}

	free(wad3path);
	return false;
}

static void pathonly(char *s, size_t size)
{
	size_t j;

	for (j = strnlen(s, size); j != (size_t)-1; j--)
		if ((s[j] == '\\') || (s[j] == ':') || (s[j] == '/'))
		{
			if (s[j] == ':') s[j+1] = 0;
			else s[j] = 0;
			return;
		}
}

/**	\brief	search for main.pk3 in the given path

	\param	searchDir	starting path

	\return	WAD path if not NULL


*/
static const char *searchWad(const char *searchDir)
{
	static char tempsw[256] = "";
	filestatus_t fstemp;

	snprintf(tempsw, sizeof(tempsw), "%s", WADKEYWORD);
	fstemp = filesearch(tempsw,searchDir,0,true,20);
	if (fstemp == FS_FOUND)
	{
		pathonly(tempsw, sizeof(tempsw));
		return tempsw;
	}

	return NULL;
}

#define CHECKWADPATH(ret) \
do { \
	I_OutputMsg(",%s", ret); \
	if (isWadPathOk(ret)) \
		return ret; \
} while (0)

#define SEARCHWAD(str) \
do { \
	WadPath = searchWad(str); \
	if (WadPath) \
		return WadPath; \
} while (0)

/**	\brief go through all possible paths and look for main.pk3
 *
 * \return path to main.pk3 if any
 */
static const char *locateWad(void)
{
	const char *envstr;
	const char *WadPath;
	int i;

	I_OutputMsg("SRB2WADDIR");
	// does SRB2WADDIR exist?
	if (((envstr = I_GetEnv("SRB2WADDIR")) != NULL) && isWadPathOk(envstr))
		return envstr;

#ifndef NOCWD
	// examine current dir
	snprintf(returnWadPath, sizeof(returnWadPath), "%s", ".");
	I_OutputMsg(",%s", returnWadPath);
	if (isWadPathOk(returnWadPath))
		return NULL;
#endif

#ifdef __APPLE__
	OSX_GetResourcesPath(returnWadPath);
	CHECKWADPATH(returnWadPath);
#endif

	// examine default dirs
	for (i = 0; wadDefaultPaths[i]; i++)
	{
		snprintf(returnWadPath, sizeof(returnWadPath), "%s", wadDefaultPaths[i]);
		CHECKWADPATH(returnWadPath);
	}

#ifndef NOHOME
	// find in $HOME
	I_OutputMsg(",HOME/" DEFAULTDIR);
	if ((envstr = I_GetEnv("HOME")) != NULL)
	{
		char *tmp = static_cast<char*>(malloc(strlen(envstr) + sizeof(PATHSEP) + sizeof(DEFAULTDIR)));

		if (tmp == NULL)
			I_Error("locateWad: Out of memory!\n");

		strcpy(tmp, envstr);
		strcat(tmp, PATHSEP);
		strcat(tmp, DEFAULTDIR);
		CHECKWADPATH(tmp);
		free(tmp);
	}
	#endif

	// search paths
	for (i = 0; wadSearchPaths[i]; i++)
	{
		I_OutputMsg(", in:%s", wadSearchPaths[i]);
		SEARCHWAD(wadSearchPaths[i]);
	}

	// if nothing was found
	return NULL;
}

const char *I_LocateWad(void)
{
	const char *waddir;

	I_OutputMsg("Looking for WADs in: ");
	waddir = locateWad();
	I_OutputMsg("\n");

	if (waddir)
	{
		// change to the directory where we found main.pk3
#if defined (_WIN32)
		waddir = _fullpath(NULL, waddir, MAX_PATH);
		SetCurrentDirectoryA(waddir);
#else
		waddir = realpath(waddir, NULL);
		if (waddir == NULL || chdir(waddir) == -1)
			I_OutputMsg("Couldn't change working directory\n");
#endif
	}
	return waddir;
}

#ifdef __linux__
#define MEMINFO_FILE "/proc/meminfo"
#define MEMTOTAL "MemTotal:"
#define MEMAVAILABLE "MemAvailable:"
#define MEMFREE "MemFree:"
#define CACHED "Cached:"
#define BUFFERS "Buffers:"
#define SHMEM "Shmem:"

/* Parse the contents of /proc/meminfo (in buf), return value of "name"
 * (example: MemTotal) */
static long get_entry(const char* name, const char* buf)
{
	long val;
	const char* hit = strstr(buf, name);
	if (hit == NULL) {
		return -1;
	}

	errno = 0;
	val = strtol(hit + strlen(name), NULL, 10);
	if (errno != 0) {
		CONS_Alert(CONS_ERROR, M_GetText("get_entry: strtol() failed: %s\n"), strerror(errno));
		return -1;
	}
	return val;
}
#endif

// quick fix for compil
uint32_t I_GetFreeMem(uint32_t *total)
{
#ifdef FREEBSD
	u_int v_free_count, v_page_size, v_page_count;
	size_t size = sizeof(v_free_count);
	sysctlbyname("vm.stats.vm.v_free_count", &v_free_count, &size, NULL, 0);
	size = sizeof(v_page_size);
	sysctlbyname("vm.stats.vm.v_page_size", &v_page_size, &size, NULL, 0);
	size = sizeof(v_page_count);
	sysctlbyname("vm.stats.vm.v_page_count", &v_page_count, &size, NULL, 0);

	if (total)
		*total = v_page_count * v_page_size;
	return v_free_count * v_page_size;
#elif defined (SOLARIS)
	/* Just guess */
	if (total)
		*total = 32 << 20;
	return 32 << 20;
#elif defined (_WIN32)
	MEMORYSTATUSEX info;

	info.dwLength = sizeof (MEMORYSTATUSEX);
	GlobalMemoryStatusEx( &info );
	if (total)
		*total = (size_t)info.ullTotalPhys;
	return (size_t)info.ullAvailPhys;
#elif defined (__linux__)
	/* Linux */
	char buf[1024];
	char *memTag;
	size_t freeKBytes;
	size_t totalKBytes;
	int32_t n;
	int32_t meminfo_fd = -1;
	long Cached;
	long MemFree;
	long Buffers;
	long Shmem;
	long MemAvailable = -1;

	meminfo_fd = open(MEMINFO_FILE, O_RDONLY);
	n = read(meminfo_fd, buf, 1023);
	close(meminfo_fd);

	if (n < 0)
	{
		// Error
		if (total)
			*total = 0;
		return 0;
	}

	buf[n] = '\0';
	if ((memTag = strstr(buf, MEMTOTAL)) == NULL)
	{
		// Error
		if (total)
			*total = 0;
		return 0;
	}

	memTag += sizeof (MEMTOTAL);
	totalKBytes = strtoul(memTag, NULL, 10);

	if ((memTag = strstr(buf, MEMAVAILABLE)) == NULL)
	{
		Cached = get_entry(CACHED, buf);
		MemFree = get_entry(MEMFREE, buf);
		Buffers = get_entry(BUFFERS, buf);
		Shmem = get_entry(SHMEM, buf);
		MemAvailable = Cached + MemFree + Buffers - Shmem;

		if (MemAvailable == -1)
		{
			// Error
			if (total)
				*total = 0;
			return 0;
		}
		freeKBytes = MemAvailable;
	}
	else
	{
		memTag += sizeof (MEMAVAILABLE);
		freeKBytes = strtoul(memTag, NULL, 10);
	}

	if (total)
		*total = totalKBytes << 10;
	return freeKBytes << 10;
#elif defined(__APPLE__)
	/* macOS */
	mach_port_t host = mach_host_self();
	kern_return_t kr;
	mach_msg_type_number_t count;
	vm_size_t v_page_size;
	struct vm_statistics64 vm_stats;
	uint64_t total_mem, free_mem;
	size_t size;

	size = sizeof(total_mem);
	if (sysctlbyname("hw.memsize", &total_mem, &size, NULL, 0) < 0)
		total_mem = 0;

	kr = host_page_size(host, &v_page_size);
	if (kr != KERN_SUCCESS)
		v_page_size = 4096;

	count = HOST_VM_INFO64_COUNT;
	kr = host_statistics64(host, HOST_VM_INFO64, (host_info64_t)&vm_stats, &count);
	if (kr == KERN_SUCCESS)
		free_mem = (uint64_t)(vm_stats.free_count + vm_stats.inactive_count) * v_page_size;
	else
		free_mem = 0;

	if (total)
		*total = (size_t)total_mem;
	return (size_t)free_mem;
#else
	// Guess 48 MB.
	if (total)
	{
		*total = 48<<20;
	}
	return 48<<20;
#endif
}

const char *I_GetSysName(void)
{
	// reference: https://sourceforge.net/p/predef/wiki/OperatingSystems/
#if defined(_WIN32) || defined(__CYGWIN__)
	return "Windows";
#elif defined(__APPLE__)
	return "Mac OS";
#elif defined(__linux__)
	return "Linux";
#elif defined(__FreeBSD__)
	return "FreeBSD";
#elif defined(__OpenBSD__)
	return "OpenBSD";
#elif defined(__NetBSD__)
	return "NetBSD";
#elif defined(__DragonFly__)
	return "DragonFly BSD";
#elif defined(__gnu_hurd__)
	return "GNU Hurd"; // for anyone mental enough to set up an SRB2 server on GNU Hurd
#elif defined(__hpux)
	return "HP-UX";
#elif defined(EPLAN9)
	return "Plan 9";
#elif defined(__HAIKU__)
	return "Haiku";
#elif defined(__BEOS__)
	return "BeOS";
#elif defined(__minix)
	return "Minix";
#elif defined(__sun)
#if defined(__SVR4) || defined(__srv4__)
	return "Solaris"; // this would be so cursed...
#else
	return "SunOS";
#endif
#elif defined(_AIX)
	return "AIX";
#elif defined(__SYLLABLE__)
	return "SyllableOS"; // RIP SyllableOS, i still miss you ;-;
#else
	return "Unknown";
#endif
}
