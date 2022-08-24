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
/// \file  doomdef.h
/// \brief Internally used data structures for virtually everything,
///        key definitions, lots of other stuff.

#ifndef __DOOMDEF__
#define __DOOMDEF__

#ifdef _WIN32
#define ASMCALL __cdecl
#else
#define ASMCALL
#endif

#include "version.h"
#include "doomtype.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define _USE_MATH_DEFINES // fixes M_PI errors in r_plane.c for Visual Studio
#include <math.h>

#ifdef GETTEXT
#include <libintl.h>
#endif
#include <locale.h> // locale should not be dependent on GETTEXT -- 11/01/20 Monster Iestyn

#include <ctype.h>

#include "fastcmp.h"

#ifdef __cplusplus
extern "C" {
#endif

// Special Hashing.
//#define NOFILEHASH
//#define NOVERIFYIWADS

// Uncheck this to compile debugging code
//#define RANGECHECK
//#ifndef PARANOIA
//#define PARANOIA // do some tests that never fail but maybe
// turn this on by make etc.. DEBUGMODE = 1 or use the Debug profile in the VC++ projects
//#endif
#if defined (_WIN32) || (defined (__unix__) && !defined (MSDOS)) || defined(__APPLE__) || defined (UNIXCOMMON) || defined (macintosh)
#define LOGMESSAGES // write message in log.txt
#endif

#ifdef LOGMESSAGES
extern FILE *logstream;
extern char logfilename[1024];
#endif

#define BOOTDEVNAG

#define BLANNAME "BlanKart"

/* A mod name to further distinguish versions. */
#define SRB2APPLICATION "SRB2Kart"

#define COMMITVERSION // Enable this to use add commit hash checking to client join checks.
//#define DEVELOP // Disable this for release builds to remove excessive cheat commands and enable hash checking and stuff, all in one go. :3
#ifdef DEVELOP
#define VERSIONSTRING "Development EXE"
#define VERSIONSTRING_RC "Development EXE" "\0"
// most interface strings are ignored in development mode.
// we use comprevision and compbranch instead.
// VERSIONSTRING_RC is for the resource-definition script used by windows builds
#else
#ifdef BETAVERSION
#define VERSIONSTRING "v" SRB2VERSION " " BETAVERSION
#define VERSIONSTRING_RC SRB2VERSION " " BETAVERSION "\0"
#else
#define VERSIONSTRING "v" SRB2VERSION
#define VERSIONSTRING_RC SRB2VERSION "\0"
#endif
// Hey! If you change this, add 1 to the MODVERSION below!
// Otherwise we can't force updates!
#endif

#define VERSIONSTRINGW WSTRING (VERSIONSTRING)

/* A custom URL protocol for server links. */
#define SERVER_URL_PROTOCOL "srb2kart://"

// Does this version require an added patch file?
// Comment or uncomment this as necessary.
//#define USE_PATCH_FILE

// Use .kart extension addons
#define USE_KART

// Modification options
// If you want to take advantage of the Master Server's ability to force clients to update
// to the latest version, fill these out.  Otherwise, just comment out UPDATE_ALERT and leave
// the other options the same.

// Comment out this line to completely disable update alerts (recommended for testing, but not for release)
#ifndef BETAVERSION
#define UPDATE_ALERT
#endif

// The string used in the alert that pops up in the event of an update being available.
// Please change to apply to your modification (we don't want everyone asking where your mod is on SRB2.org!).
#define UPDATE_ALERT_STRING \
"A new update is available for BlanKart.\n"\
"(or the master server you are using is reporting a bad version)\n"\
"\n"\
"You are using version: %s\n"\
"The newest version is: %s\n"\

// For future use, the codebase is the version of SRB2 that the modification is based on,
// and should not be changed unless you have merged changes between versions of SRB2
// (such as 2.0.4 to 2.0.5, etc) into your working copy.
// Will always resemble the versionstring, 205 = 2.0.5, 210 = 2.1, etc.
#define CODEBASE 220

// To version config.cfg, MAJOREXECVERSION is set equal to MODVERSION automatically.
// Increment MINOREXECVERSION whenever a config change is needed that does not correspond
// to an increment in MODVERSION. This might never happen in practice.
// If MODVERSION increases, set MINOREXECVERSION to 0.
#define MAJOREXECVERSION MODVERSION
#define MINOREXECVERSION 3
// (It would have been nice to use VERSION and SUBVERSION but those are zero'd out for DEVELOP builds)

// Macros
#define GETMAJOREXECVERSION(v) (v & 0xFFFF)
#define GETMINOREXECVERSION(v) (v >> 16)
#define GETEXECVERSION(major,minor) (major + (minor << 16))
#define EXECVERSION GETEXECVERSION(MAJOREXECVERSION, MINOREXECVERSION)

// =========================================================================

// The maximum number of players, multiplayer/networking.
// NOTE: it needs more than this to increase the number of players...

#define MAXPLAYERS 32	// 32 + host slot
#define MAXPLAYERNAME 21
#define MAXCLANTAG 11
#define MAXSPLITSCREENPLAYERS 4 // Max number of players on a single computer
#define MAXGAMEPADS (MAXSPLITSCREENPLAYERS * 2) // Number of gamepads we'll be allowing
#define PACKETLOSSCYCLES 4 // amount of cycles to do when measuring packet loss

#define MAXSKINS 4096
#define MAXFOLLOWERS UINT16_MAX
#define MAXSKINUNAVAILABLE 128
#define MAXAVAILABILITY MAXSKINUNAVAILABLE/8

#define COLORRAMPSIZE 16
#define MAXCOLORNAME 32
#define NUMCOLORFREESLOTS 32768

#define MAXGPPLAYERS 16	// max players in Grand Prix only

// Net savegame compression stuff

// Uses ZLib if LZ4 is not defined.
// if theres no ZLib then use LZF

// ZLib Options
//#define ZLIB_LEVEL 5

// LZ4 Options
#define USE_LZ4 // Use LZ4 for network savegame compression
#define LZ4_ACCEL 1

// surely nobody's gonna change the palette a second time :Clueless:
#define FADECOLOR 0 // 120
#define ENCOREFADECOLOR 209 // 122

// the highest number of playerinfo entries we can fit into a single packet
#define MAXPLAYERINFO 32

// State updates, number of tics / second.
// NOTE: used to setup the timer rate, see I_StartupTimer().
#define TICRATE 35
#define NEWTICRATERATIO 1 // try 4 for 140 fps :)
#define NEWTICRATE (TICRATE*NEWTICRATERATIO)

#define MUSICRATE 1000 // sound timing is calculated by milliseconds

#define RING_DIST 1280*FRACUNIT // how close you need to be to a ring to attract it

#define PUSHACCEL (2*FRACUNIT) // Acceleration for MF2_SLIDEPUSH items.

// Special linedef executor tag numbers! Binary map format only (UDMF has other ways of doing these things).
enum {
	LE_PINCHPHASE      =    -2, // A boss entered pinch phase (and, in most cases, is preparing their pinch phase attack!)
	LE_ALLBOSSESDEAD   =    -3, // All bosses in the map are dead (Egg capsule raise)
	LE_BOSSDEAD        =    -4, // A boss in the map died (Chaos mode boss tally)
	LE_BOSS4DROP       =    -5, // CEZ boss dropped its cage (also subtract the number of hitpoints it's lost)
	LE_BRAKVILEATACK   =    -6, // Brak's doing his LOS attack, oh noes
	LE_TURRET          = 32000, // THZ turret
	LE_BRAKPLATFORM    =  4200, // v2.0 Black Eggman destroys platform
	LE_CAPSULE2        =   682, // Egg Capsule
	LE_CAPSULE1        =   681, // Egg Capsule
	LE_CAPSULE0        =   680, // Egg Capsule
	LE_KOOPA           =   650, // Distant cousin to Gay Bowser
	LE_AXE             =   649, // MKB Axe object
	LE_PARAMWIDTH      =  -100  // If an object that calls LinedefExecute has a nonzero parameter value, this times the parameter will be subtracted. (Mostly for the purpose of coexisting bosses...)
};

#define TOSTR(x) #x
#define TOSTR2(x) TOSTR(x) // expand x first

// Name of local directory for config files and savegames
#if (((defined (__unix__) && !defined (MSDOS)) || defined (UNIXCOMMON)) && !defined (__CYGWIN__)) && !defined (__APPLE__)
#define DEFAULTDIR ".srb2kart-v2"
#else
#define DEFAULTDIR "srb2kart-v2"
#endif

#include "g_state.h"

// commonly used routines - moved here for include convenience

/**	\brief	The I_Error function

	\param	error	the error message

	\return	void


*/
void I_Error(const char *error, ...) FUNCIERROR;

/**	\brief	write a message to stderr (use before I_Quit) for when you need to quit with a msg, but need
 the return code 0 of I_Quit();

	\param	error	message string

	\return	void
*/
void I_OutputMsg(const char *error, ...) FUNCPRINTF;

// console.h
typedef enum
{
	CONS_NOTICE,
	CONS_WARNING,
	CONS_ERROR
} alerttype_t;

void CONS_Printf(const char *fmt, ...) FUNCPRINTF;
void CONS_Alert(alerttype_t level, const char *fmt, ...) FUNCDEBUG;
void CONS_Debug(uint32_t debugflags, const char *fmt, ...) FUNCDEBUG;

// For help debugging functions.
#define POTENTIALLYUNUSED CONS_Alert(CONS_WARNING, "(%s:%d) Unused code appears to be used.\n", __FILE__, __LINE__)

#include "m_swap.h"

// Things that used to be in dstrings.h
#define SAVEGAMENAME "srb2sav"
extern char savegamename[256];
extern char liveeventbackup[256];

// m_misc.h
#ifdef GETTEXT
#define M_GetText(String) gettext(String)
#else
// If no translations are to be used, make a stub
// M_GetText function that just returns the string.
#define M_GetText(x) (x)
#endif
void M_StartupLocale(void);
char *va(const char *format, ...) FUNCPRINTF;

char *M_GetToken(const char *inputString);
void M_UnGetToken(void);
uint32_t M_GetTokenPos(void);

void M_TokenizerOpen(const char *inputString, size_t inputLength);
void M_TokenizerClose(void);
const char *M_TokenizerRead(uint32_t i);
uint32_t M_TokenizerGetEndPos(void);
void M_TokenizerSetEndPos(uint32_t newPos);
dboolean M_TokenizerJustReadString(void);

char *sizeu1(size_t num);
char *sizeu2(size_t num);
char *sizeu3(size_t num);
char *sizeu4(size_t num);
char *sizeu5(size_t num);

// d_main.c
extern int    VERSION;
extern int SUBVERSION;

// 4 bytes handles 8 characters of a git object SHA. At
// around 20k commits, we only need 6 characters for a unique
// abbreviation. Maybe in another 20k commits, more than 8
// characters will be required! =P
// P.S. 8 is also what comptime generates
// #ifdef COMMITVERSION
#define GIT_SHA_ABBREV (4)
extern uint8_t comprevision_abbrev_bin[GIT_SHA_ABBREV];
// #endif

extern dboolean devparm; // development mode (-debug)

// m_cheat.c
extern uint32_t cht_debug;

typedef enum
{
	DBG_NONE			= 0x00000000,
	DBG_BASIC			= 0x00000001,
	DBG_DETAILED		= 0x00000002,
	DBG_PLAYER			= 0x00000004,
	DBG_RENDER			= 0x00000008,
	DBG_MUSIC			= 0x00000010,
	DBG_IMU				= 0x00000020,
	DBG_POLYOBJ			= 0x00000040,
	DBG_GAMELOGIC		= 0x00000080,
	DBG_NETPLAY			= 0x00000100,
	DBG_MEMORY			= 0x00000200,
	DBG_SETUP			= 0x00000400,
	DBG_LUA				= 0x00000800,
	DBG_RNG				= 0x00001000,
	DBG_DEMO			= 0x00002000,
	DBG_EFX				= 0x00004000,
} debugFlags_t;

struct debugFlagNames_s
{
	const char *str;
	debugFlags_t flag;
};

extern struct debugFlagNames_s const debug_flag_names[];

// =======================
// Misc stuff for later...
// =======================

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define ANG2RAD(angle) ((float)((angle)*M_PI)/ANGLE_180)

// Modifier key variables, accessible anywhere
extern uint8_t shiftdown, ctrldown, altdown;
extern dboolean capslock;

// WARNING: a should be unsigned but to add with 2048, it isn't!
#define AIMINGTODY(a) FixedDiv((FINETANGENT((2048+(((int32_t)a)>>ANGLETOFINESHIFT)) & FINEMASK)*160), fovtan[viewssnum])

// if we ever make our alloc stuff...
#define ZZ_Alloc(x) Z_Malloc(x, PU_STATIC, NULL)
#define ZZ_Calloc(x) Z_Calloc(x, PU_STATIC, NULL)

// i_system.c, replace getchar() once the keyboard has been appropriated
int32_t I_GetKey(void);

// Standard Hashing Functions (tm)
// see W_MakeFileHash for hashing files
#include "xxhash.h"
#define HASH64(p, n) ((uint64_t)XXH3_64bits(p, n))
#define HASH32(p, n) ((uint32_t)(XXH3_64bits(p, n) & UINT32_MAX))
#define HASHFMT "%016jx"

#ifndef __cplusplus
#ifndef min // Double-Check with WATTCP-32's cdefs.h
#define min(x, y) (((x) < (y)) ? (x) : (y))
#endif
#ifndef max // Double-Check with WATTCP-32's cdefs.h
#define max(x, y) (((x) > (y)) ? (x) : (y))
#endif
#endif

#ifndef CLAMP
#define CLAMP(x, min_val, max_val) ((x) < (min_val) ? (min_val) : ((x) > (max_val) ? (max_val) : (x)))
#endif

#ifndef u32_delta
#define u32_delta(x, y) ((x) > (y) ? (x) - (y) : (y) - (x))
#endif

// Max gamepad/joysticks that can be detected/used.
#define MAX_JOYSTICKS 4

#ifndef M_PIl
#define M_PIl 3.1415926535897932384626433832795029L
#endif

// Floating point comparison epsilons from float.h
#ifndef FLT_EPSILON
#define FLT_EPSILON 1.1920928955078125e-7f
#endif

#ifndef DBL_EPSILON
#define DBL_EPSILON 2.2204460492503131e-16l
#endif

#if defined(__GNUC__) || defined(__clang__)
#ifndef LIKELY
#define LIKELY(x)   __builtin_expect(!!(x), 1)
#endif

#ifndef UNLIKELY
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif
#else
#define LIKELY(x)       (x)
#define UNLIKELY(x)     (x)
#endif

#ifdef __cplusplus
#if defined(__GNUC__) || defined(__clang__)
#define restrict __restrict
#else
#define restrict
#endif
#endif

// the GNU cleanup attribute: plugging memory leaks since 2003!
// on scope exit, the cleanup function is called with a pointer to the declared variable,
// essentially behaving like a C++ destructor
// NOTE: you WILL have nasal troubles if the variable is not initialized
#define CLEANUP(f) __attribute__((__cleanup__(f)))

// the GNU constructor and destructor attributes
// The constructor attribute causes the function to be called automatically before execution enters main ().
// Similarly, the destructor attribute causes the function to be called automatically after main () has completed or exit () has been called.
// Functions with these attributes are useful for initializing data that will be used implicitly during the execution of the program.
#define CONSTRUCTOR __attribute__((constructor, used))
#define DESTRUCTOR __attribute__((destructor, used))

// An assert-type mechanism.
// NOTE: USE SRB2_ASSERT FOR C++ CODE INSTEAD
#ifdef PARANOIA
#define I_Assert(e) ((e) ? (void)0 : I_Error("assert failed: %s, file %s, line %d", #e, __FILE__, __LINE__))
#else
#define I_Assert(e) ((void)0)
#endif

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ == 202311L)
#define I_StaticAssert(e) static_assert(e, "Static assertion failed: " #e)
#else
#define I_StaticAssert(e) _Static_assert(e, "Static assertion failed: " #e)
#endif

// The character that separates pathnames. Forward slash on
// most systems, but reverse solidus (\) on Windows.
#if defined (_WIN32)
	#define PATHSEP "\\"
#else
	#define PATHSEP "/"
#endif

#define PUNCTUATION "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~"

// Compile date and time and revision.
extern const char *compdate, *comptime, *comprevision, *compbranch, *complast;
extern int compuncommitted;

// Disabled code and code under testing
// None of these that are disabled in the normal build are guaranteed to work perfectly
// Compile them at your own risk!

///	Allows gravity changes in netgames, no questions asked.
//#define NETGAME_GRAVITY

///	Dumps the contents of a network save game upon consistency failure for debugging.
//#define DUMPCONSISTENCY

///	Allow loading of savegames between different versions of the game.
///	\note	XMOD port.
///	    	Most modifications should probably enable this.
//#define SAVEGAME_OTHERVERSIONS

///	Shuffle's incomplete OpenGL sorting code.
#define SHUFFLE // This has nothing to do with sorting, why was it disabled?

/// Experimental tweaks to analog mode. (Needs a lot of work before it's ready for primetime.)
//#define REDSANALOG

/// Experimental attempts at preventing MF_PAPERCOLLISION objects from getting stuck in walls.
//#define PAPER_COLLISIONCORRECTION

#ifdef DEVELOP
// Easily make it so that overtime works offline
#define TESTOVERTIMEINFREEPLAY
#define COMMITVERSION
#endif

/// FINALLY some real clipping that doesn't make walls dissappear AND speeds the game up
/// (that was the original comment from SRB2CB, sadly it is a lie and actually slows game down)
/// on the bright side it fixes some weird issues with translucent walls
/// \note	SRB2CB port.
///      	SRB2CB itself ported this from PrBoom+
#define NEWCLIP

/// OpenGL shaders
#define GL_SHADERS


/// Handle touching sector specials in P_PlayerAfterThink instead of P_PlayerThink.
/// \note   Required for proper collision with moving sloped surfaces that have sector specials on them.
#define SECTORSPECIALSAFTERTHINK

/// Sprite rotation
#define ROTSPRITE
#define ROTANGLES 72 // Needs to be a divisor of 360 (45, 60, 90, 120...)
#define ROTANGDIFF (360 / ROTANGLES)

/// PNG support
#ifndef HAVE_PNG
#define NO_PNG_LUMPS
#endif

// Volume scale is 0-100 in new mixer. 100 is treated as -0dB or 100% gain. No more weirdness to work around SDL_mixer
// problems

#define MAX_VOLUME 100

// Crypto/RRID primitives
#define PUBKEYLENGTH 32 // Enforced by Monocypher EdDSA
#define PRIVKEYLENGTH 64 // Enforced by Monocypher EdDSA
#define SIGNATURELENGTH 64 // Enforced by Monocypher EdDSA
#define CHALLENGELENGTH 64 // Servers verify client identity by giving them messages to sign. How long are these messages?

#ifdef HAVE_CURL
#define MASTERSERVER
#else
#undef UPDATE_ALERT
#endif

#ifdef HAVE_CURL
#define MASTERSERVER
#else
#undef UPDATE_ALERT
#endif

// p_sight.c
#define TRAVERSE_MAX 8

// m_menu.h
#define MAXSTRINGLENGTH 32

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __DOOMDEF__
