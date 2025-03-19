// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 2011-2016 by Matthew "Kaito Sinclaire" Walsh.
// Copyright (C) 1999-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  m_menu.h
/// \brief Menu widget stuff, selection and such

#ifndef __X_MENU__
#define __X_MENU__

#include "doomstat.h" // for NUMGAMETYPES
#include "d_event.h"
#include "command.h"
#include "f_finale.h" // for ttmode_enum
#include "i_threads.h"
#include "mserv.h"
#include "r_skins.h" // for SKINNAMESIZE

#ifdef __cplusplus
extern "C" {
#endif

// Compatibility with old-style named NiGHTS replay files.
#define OLDNREPLAYNAME

//
// MENUS
//

// If menu hierarchies go deeper, change this up to 5.
// Zero-based, inclusive.
#define NUMMENULEVELS 3
#define MENUBITS 6

// Menu IDs sectioned by numeric places to signify hierarchy
/**
 * IF YOU MODIFY THIS, MODIFY MENUTYPES_LIST[] IN dehacked.c TO MATCH.
 */
typedef enum
{
	MN_NONE,

	MN_MAIN,

	// Single Player
	MN_SP_MAIN,

	MN_SP_LOAD,
	MN_SP_PLAYER,

	MN_SP_LEVELSELECT,
	MN_SP_LEVELSTATS,

	MN_SP_TIMEATTACK,
	MN_SP_TIMEATTACK_LEVELSELECT,
	MN_SP_GUESTREPLAY,
	MN_SP_REPLAY,
	MN_SP_GHOST,

	MN_SP_NIGHTSATTACK,
	MN_SP_NIGHTS_LEVELSELECT,
	MN_SP_NIGHTS_GUESTREPLAY,
	MN_SP_NIGHTS_REPLAY,
	MN_SP_NIGHTS_GHOST,

	MN_SP_MARATHON,

	// Multiplayer
	MN_MP_MAIN,
	MN_MP_SPLITSCREEN, // SplitServer
	MN_MP_SERVER,
	MN_MP_CONNECT,
	MN_MP_ROOM,
	MN_MP_PLAYERSETUP, // MP_PlayerSetupDef shared with SPLITSCREEN if #defined NONET
	MN_MP_SERVER_OPTIONS,

	// Options
	MN_OP_MAIN,

	MN_OP_P1CONTROLS,
	MN_OP_CHANGECONTROLS, // OP_ChangeControlsDef shared with P2
	MN_OP_P1MOUSE,
	MN_OP_P1JOYSTICK,
	MN_OP_JOYSTICKSET, // OP_JoystickSetDef shared with P2
	MN_OP_P1CAMERA,

	MN_OP_P2CONTROLS,
	MN_OP_P2MOUSE,
	MN_OP_P2JOYSTICK,
	MN_OP_P2CAMERA,

	MN_OP_PLAYSTYLE,

	MN_OP_VIDEO,
	MN_OP_VIDEOMODE,
	MN_OP_COLOR,
	MN_OP_OPENGL,
	MN_OP_OPENGL_LIGHTING,

	MN_OP_SOUND,

	MN_OP_SERVER,
	MN_OP_MONITORTOGGLE,

	MN_OP_DATA,
	MN_OP_ADDONS,
	MN_OP_SCREENSHOTS,
	MN_OP_ERASEDATA,

	// Extras
	MN_SR_MAIN,
	MN_SR_PANDORA,
	MN_SR_LEVELSELECT,
	MN_SR_UNLOCKCHECKLIST,
	MN_SR_EMBLEMHINT,
	MN_SR_PLAYER,
	MN_SR_SOUNDTEST,

	// Addons (Part of MISC, but let's make it our own)
	MN_AD_MAIN,

	// MISC
	// MN_MESSAGE,
	MN_SPAUSE,

	MN_MPAUSE,
	MN_SCRAMBLETEAM,
	MN_CHANGETEAM,
	MN_CHANGELEVEL,

	MN_MAPAUSE,
	MN_HELP,

	// SRB2Kart
	MN_OP_HUD,
	MN_OP_CHAT,
	MN_OP_GAME,
	MN_OP_BLANKARTGAME,
	MN_OP_ADVANCEDSERVER,
	MN_OP_CAMERA,
	MN_OP_P3CAMERA,
	MN_OP_P4CAMERA,
	MN_MISC_REPLAYOPTIONS,
	MN_MP_OFFLINESERVER,
	MN_SP_GRANDPRIX,
	MN_MISC_REPLAYHUT,
	MN_MISC_DISCORDREQUESTS,
	MN_CHANGESPECTATE,
	MN_MISC_REPLAYSTART,
	MN_PLAYBACK,
	MN_OP_CONTROLSETUP,

	MN_SPECIAL,

	MN_FIRSTFREESLOT,
	MN_LASTFREESLOT,
	NUMMENUTYPES,
} menutype_t; // up to 63
#define NUMMENUFREESLOTS (NUMMENUTYPES - MN_FIRSTFREESLOT)
#define MTREE2(a,b) (a | (b<<MENUBITS))
#define MTREE3(a,b,c) MTREE2(a, MTREE2(b,c))
#define MTREE4(a,b,c,d) MTREE2(a, MTREE3(b,c,d))

extern menu_t *menunum2menudef[NUMMENUTYPES];

typedef struct
{
	char bgname[8]; // name for background gfx lump; lays over titlemap if this is set
	SINT8 fadestrength;  // darken background when displaying this menu, strength 0-31 or -1 for undefined
	INT32 bgcolor; // fill color, overrides bg name. -1 means follow bg name rules.
	INT32 titlescrollxspeed; // background gfx scroll per menu; inherits global setting
	INT32 titlescrollyspeed; // y scroll
	boolean bghide; // for titlemaps, hide the background.

	SINT8 hidetitlepics; // hide title gfx per menu; -1 means undefined, inherits global setting
	ttmode_enum ttmode; // title wing animation mode; default TTMODE_KART
	UINT8 ttscale; // scale of title wing gfx (FRACUNIT / ttscale); -1 means undefined, inherits global setting
	char ttname[9]; // lump name of title wing gfx. If name length is <= 6, engine will attempt to load numbered frames (TTNAMExx)
	INT16 ttx; // X position of title wing
	INT16 tty; // Y position of title wing
	INT16 ttloop; // # frame to loop; -1 means dont loop
	UINT16 tttics; // # of tics per frame

	char musname[7]; ///< Music track to play. "" for no music.
	UINT16 mustrack; ///< Subsong to play. Only really relevant for music modules and specific formats supported by GME. 0 to ignore.
	boolean muslooping; ///< Loop the music
	boolean musstop; ///< Don't play any music
	boolean musignore; ///< Let the current music keep playing

	boolean enterbubble; // run all entrance line execs after common ancestor and up to child. If false, only run the child's exec
	boolean exitbubble; // run all exit line execs from child and up to before common ancestor. If false, only run the child's exec
	INT32 entertag; // line exec to run on menu enter, if titlemap
	INT32 exittag; // line exec to run on menu exit, if titlemap
	INT16 enterwipe; // wipe type to run on menu enter, -1 means default
	INT16 exitwipe; // wipe type to run on menu exit, -1 means default
} menupres_t;

extern menupres_t menupres[NUMMENUTYPES];
extern UINT32 prevMenuId;
extern UINT32 activeMenuId;

void M_InitMenuPresTables(void);
//UINT8 M_GetYoungestChildMenu(void);
//void M_ChangeMenuMusic(const char *defaultmusname, boolean defaultmuslooping);
//void M_SetMenuCurBackground(const char *defaultname);
//void M_SetMenuCurFadeValue(UINT8 defaultvalue);
//void M_SetMenuCurTitlePics(void);

// Called by main loop,
// saves config file and calls I_Quit when user exits.
// Even when the menu is not displayed,
// this can resize the view and change game parameters.
// Does all the real work of the menu interaction.
boolean M_Responder(event_t *ev);

// Called by main loop, runs for demo playback. If this returns true, nullify any further user input.
boolean M_DemoResponder(event_t *ev);

// Called by main loop, only used for menu (skull cursor) animation.
void M_Ticker(void);

// Called by main loop, draws the menus directly into the screen buffer.
void M_Drawer(void);

// Called by D_SRB2Main, loads the config file.
void M_Init(void);

// Called by D_SRB2Main also, sets up the playermenu and description tables.
void M_InitCharacterTables(void);

// Called by intro code to force menu up upon a keypress,
// does nothing if menu is already up.
void M_StartControlPanel(void);

// Called upon end of a mode attack run
void M_EndModeAttackRun(void);

// Called on new server add, or other reasons
void M_SortServerList(void);

// Draws a box with a texture inside as background for messages
void M_DrawTextBox(INT32 x, INT32 y, INT32 width, INT32 boxlines);

// Used in d_netcmd to restart time attack
void M_ModeAttackRetry(INT32 choice);

// the function to show a message box typing with the string inside
// string must be static (not in the stack)
// routine is a function taking a INT32 in parameter
typedef enum
{
	MM_NOTHING = 0, // is just displayed until the user do someting
	MM_YESNO,       // routine is called with only 'y' or 'n' in param
	MM_EVENTHANDLER // the same of above but without 'y' or 'n' restriction
	                // and routine is void routine(event_t *) (ex: set control)
} menumessagetype_t;
void M_StartMessage(const char *string, void *routine, menumessagetype_t itemtype);

typedef enum
{
	M_NOT_WAITING,

	M_WAITING_VERSION,
	M_WAITING_SERVERS,
}
M_waiting_mode_t;

extern M_waiting_mode_t m_waiting_mode;

// Called by linux_x/i_video_xshm.c
void M_QuitResponse(INT32 ch);

// Determines whether to show a level in the list (platter version does not need to be exposed)
boolean M_CanShowLevelInList(INT32 mapnum, INT32 gt);

// flags for items in the menu
// menu handle (what we do when key is pressed
#define IT_TYPE             15     // (1+2+4+8)
#define IT_CALL              0     // call the function
#define IT_SPACE             1     // no handling
#define IT_ARROWS            2     // call function with 0 for left arrow and 1 for right arrow in param
#define IT_KEYHANDLER        4     // call with the key in param
#define IT_SUBMENU           6     // go to sub menu
#define IT_CVAR              8     // handle as a cvar
#define IT_PAIR             11     // no handling, define both sides of text
#define IT_MSGHANDLER       12     // same as key but with event and sometime can handle y/n key (special for message)

#define IT_DISPLAY   (48+64+128)    // 16+32+64+128
#define IT_NOTHING            0     // space
#define IT_PATCH             16     // a patch or a string with big font
#define IT_STRING            32     // little string (spaced with 10)
#define IT_WHITESTRING       48     // little string in white
#define IT_DYBIGSPACE        64     // same as noting
#define IT_DYLITLSPACE   (16+64)    // little space
#define IT_STRING2       (32+64)    // a simple string
#define IT_GRAYPATCH     (16+32+64) // grayed patch or big font string
#define IT_BIGSLIDER        128     // volume sound use this
#define IT_TRANSTEXT     (16+128)   // Transparent text
#define IT_TRANSTEXT2    (32+128)   // used for control names
#define IT_HEADERTEXT    (48+128)   // Non-selectable header option, displays in yellow offset to the left a little
#define IT_QUESTIONMARKS (64+128)   // Displays as question marks, used for secrets
#define IT_CENTER           256     // if IT_PATCH, center it on screen

//consvar specific
#define IT_CVARTYPE   (512+1024+2048)
#define IT_CV_NORMAL         0
#define IT_CV_SLIDER       512
#define IT_CV_STRING      1024
#define IT_CV_NOPRINT     1536
#define IT_CV_NOMOD       2048
#define IT_CV_INVISSLIDER 2560
#define IT_CV_INTEGERSTEP 4096      // if IT_CV_NORMAL and cvar is CV_FLOAT, modify it by 1 instead of 0.0625
#define IT_CV_FLOATSLIDER 4608      // IT_CV_SLIDER, value modified by 0.0625 instead of 1 (for CV_FLOAT cvars)

//call/submenu specific
// There used to be a lot more here but ...
// A lot of them became redundant with the advent of the Pause menu, so they were removed
#define IT_CALLTYPE   (512+1024)
#define IT_CALL_NORMAL          0
#define IT_CALL_NOTMODIFIED   512

// in INT16 for some common use
#define IT_BIGSPACE    (IT_SPACE  +IT_DYBIGSPACE)
#define IT_LITLSPACE   (IT_SPACE  +IT_DYLITLSPACE)
#define IT_CONTROL     (IT_STRING2+IT_CALL)
#define IT_CVARMAX     (IT_CVAR   +IT_CV_NOMOD)
#define IT_DISABLED    (IT_SPACE  +IT_GRAYPATCH)
#define IT_GRAYEDOUT   (IT_SPACE  +IT_TRANSTEXT)
#define IT_GRAYEDOUT2  (IT_SPACE  +IT_TRANSTEXT2)
#define IT_HEADER      (IT_SPACE  +IT_HEADERTEXT)
#define IT_SECRET      (IT_SPACE  +IT_QUESTIONMARKS)

#define MAXSTRINGLENGTH 32

typedef union
{
	menu_t *submenu;      // IT_SUBMENU
	consvar_t *cvar;             // IT_CVAR
	void (*routine)(INT32 choice); // IT_CALL, IT_KEYHANDLER, IT_ARROWS
	void (*eventhandler)(event_t *ev); // MM_EVENTHANDLER
} itemaction_t;

//
// MENU TYPEDEFS
//
struct menuitem_t
{
	// show IT_xxx
	UINT16 status;

	const char *patch;
	const char *text; // used when FONTBxx lump is found

	itemaction_t itemaction;

	// hotkey in menu or y of the item
	UINT16 alphaKey;
};

struct menu_t
{
	UINT32         menuid;             // ID to encode menu type and hierarchy
	const char    *headerpic;
	INT16          numitems;           // # of menu items
	menu_t *prevMenu;           // previous menu
	menuitem_t    *menuitems;          // menu items
	void         (*drawroutine)(void); // draw routine
	INT16          x, y;               // x, y of menu
	INT16          lastOn;             // last item user was on in menu
	void         (*quitroutine)(INT32 choice); // called before quit a menu
};

void M_SetupNextMenu(menu_t *menudef);
void M_ClearMenus(boolean callexitmenufunc);

void M_SinglePlayerMenu(INT32 choice);
void M_Options(INT32 choice);
void M_Addons(INT32 choice);
void M_QuitSRB2(INT32 choice);
void M_Statistics(INT32 choice);
void M_HandleLevelStats(INT32 choice);
void M_QuitReplayHut(INT32 choice);
void M_HandleReplayHutList(INT32 choice);
void M_GrandPrixTemp(INT32 choice);
void M_TimeAttack(INT32 choice);
void M_ItemBreaker(INT32 choice);
void M_StartGrandPrix(INT32 choice);
void M_QuitTimeAttackMenu(INT32 choice);
void M_ChooseTimeAttack(INT32 choice);
void M_SetGuestReplay(INT32 choice);
void M_ReplayTimeAttack(INT32 choice);
void M_HandleStaffReplay(INT32 choice);
void M_SetupMultiHandler(INT32 choice);
void M_HandleSetupMultiPlayer(INT32 choice);
void M_QuitMultiPlayerMenu(INT32 choice);
void M_StartServerMenu(INT32 choice);
void M_StartOfflineServerMenu(INT32 choice);
void M_StartServer(INT32 choice);
void M_ConnectMenuModChecks(INT32 choice);
void M_HandleConnectIP(INT32 choice);
void M_CancelConnect(INT32 choice);
void M_Setup1PControlsMenu(INT32 choice);
void M_Setup2PControlsMenu(INT32 choice);
void M_Setup3PControlsMenu(INT32 choice);
void M_Setup4PControlsMenu(INT32 choice);
void M_VideoModeMenu(INT32 choice);
#ifdef HWRENDER
void M_OpenGLOptionsMenu(INT32 choice);
#endif
void M_HandleVideoMode(INT32 ch);
void M_HandleSoundTest(INT32 choice);
void M_MusicTest(INT32 choice);
void M_ScreenshotOptions(INT32 choice);
void M_AddonsOptions(INT32 choice);
void M_EraseData(INT32 choice);
void M_Manual(INT32 choice);
void M_Credits(INT32 choice);
void M_BlanCredits(INT32 choice);
void M_HandleAddons(INT32 choice);
void M_SelectableClearMenus(INT32 choice);
void M_ModeAttackEndGame(INT32 choice);
void M_PandorasBox(INT32 choice);
void M_EmblemHints(INT32 choice);
void M_Retry(INT32 choice);
void M_EndGame(INT32 choice);
void M_MapChange(INT32 choice);
void M_SetupMultiPlayer(INT32 choice);
void M_SetupMultiPlayer2(INT32 choice);
void M_SetupMultiPlayer3(INT32 choice);
void M_SetupMultiPlayer4(INT32 choice);
void M_ConfirmSpectate(INT32 choice);
void M_ConfirmEnterGame(INT32 choice);
void M_ExitPandorasBox(INT32 choice);
void M_GetAllEmeralds(INT32 choice);
void M_DestroyRobots(INT32 choice);
ATTRNORETURN void FUNCNORETURN M_UltimateCheat(INT32 choice);
void M_ConfirmTeamScramble(INT32 choice);
void M_ConfirmTeamChange(INT32 choice);
void M_ConfirmSpectateChange(INT32 choice);
void M_ChangeLevel(INT32 choice);
void M_HutStartReplay(INT32 choice);
void M_PlaybackRewind(INT32 choice);
void M_PlaybackPause(INT32 choice);
void M_PlaybackFastForward(INT32 choice);
void M_PlaybackAdvance(INT32 choice);
void M_PlaybackSetViews(INT32 choice);
void M_PlaybackAdjustView(INT32 choice);
void M_PlaybackToggleFreecam(INT32 choice);
void M_PlaybackQuit(INT32 choice);
void M_HandleImageDef(INT32 choice);
void M_HandleMusicTest(INT32 choice);
void M_Setup1PJoystickMenu(INT32 choice);
void M_Setup2PJoystickMenu(INT32 choice);
void M_Setup3PJoystickMenu(INT32 choice);
void M_Setup4PJoystickMenu(INT32 choice);
void M_ResetControls(INT32 choice);
void M_ChangeControl(INT32 choice);
void M_AssignJoystick(INT32 choice);
void M_HandleMonitorToggles(INT32 choice);

void M_DrawGenericMenu(void);
void M_DrawCenteredMenu(void);
void M_DrawPauseMenu(void);
void M_DrawChecklist(void);
void M_DrawLevelStats(void);
void M_DrawReplayHut(void);
void M_DrawTimeAttackMenu(void);
void M_DrawMPMainMenu(void);
void M_DrawSetupMultiPlayerMenu(void);
void M_DrawServerMenu(void);
void M_DrawVideoMenu(void);
void M_DrawVideoMode(void);
void M_DrawSkyRoom(void);
void M_DrawHUDOptions(void);
void M_DrawAddons(void);
void M_DrawEmblemHints(void);
void M_DrawReplayStartMenu(void);
void M_DrawPlaybackMenu(void);
void M_DrawImageDef(void);
void M_DrawMusicTest(void);
void M_DrawControl(void);
void M_DrawJoystick(void);
void M_DrawMonitorToggles(void);

// Maybe this goes here????? Who knows.
boolean M_MouseNeeded(void);

#ifdef HAVE_THREADS
extern I_mutex m_menu_mutex;
#endif

extern menu_t *currentMenu;

extern menu_t MainDef;
extern menu_t SP_LoadDef;

// Call upon joystick hotplug
void M_SetupJoystickMenu(INT32 choice);
extern menu_t OP_JoystickSetDef;

// Stuff for customizing the player select screen
typedef struct
{
	boolean used;
	char notes[441];
	char picname[8];
	char skinname[SKINNAMESIZE*2+2]; // skin&skin\0
	patch_t *charpic;
	UINT8 prev;
	UINT8 next;

	// new character select
	char displayname[SKINNAMESIZE+1];
	SINT8 skinnum[2];
	UINT16 oppositecolor;
	char nametag[8];
	patch_t *namepic;
	UINT16 tagtextcolor;
	UINT16 tagoutlinecolor;
} description_t;

// level select platter
typedef struct
{
	char header[22+5]; // mapheader_t lvltttl max length + " ZONE"
	INT32 maplist[3];
	char mapnames[3][17+1];
	boolean mapavailable[4]; // mapavailable[3] == wide or not
} levelselectrow_t;

typedef struct
{
	UINT8 numrows;
	levelselectrow_t *rows;
} levelselect_t;
// experimental level select end

// descriptions for gametype select screen
/*typedef struct
{
	UINT8 col[2];
	char notes[441];
} gtdesc_t;
extern gtdesc_t gametypedesc[NUMGAMETYPES];*/

// mode descriptions for video mode menu
struct modedesc_t
{
	INT32 modenum; // video mode number in the vidmodes list
	const char *desc;  // XXXxYYY
	UINT8 goodratio; // aspect correct if 1
};

// savegame struct for save game menu
typedef struct
{
	char levelname[32];
	UINT8 skinnum;
	UINT8 numemeralds;
	UINT8 numgameovers;
	INT32 lives;
	INT32 continuescore;
	INT32 gamemap;
} saveinfo_t;

extern description_t description[MAXSKINS];

extern consvar_t cv_showfocuslost;
extern consvar_t cv_newgametype, cv_nextmap, cv_chooseskin, cv_serversort;
extern consvar_t cv_dummygpdifficulty, cv_dummygpencore, cv_dummygpcup;
extern consvar_t cv_dummyrings, cv_dummylives;
extern consvar_t cv_dummymenuplayer, cv_dummyteam, cv_dummyspectate, cv_dummyscramble;
extern CV_PossibleValue_t gametype_cons_t[];

extern char dummystaffname[22];

extern INT16 startmap;
extern INT32 ultimate_selectable;
extern INT16 char_on, startchar;

#define MAXSAVEGAMES 31
#define NOSAVESLOT 0 //slot where Play Without Saving appears
#define MARATHONSLOT 420 // just has to be nonzero, but let's use one that'll show up as an obvious error if something goes wrong while not using our existing saves

#define BwehHehHe() S_StartSound(NULL, sfx_bewar1+M_RandomKey(4)) // Bweh heh he

//void M_TutorialSaveControlResponse(INT32 ch);

void M_ForceSaveSlotSelected(INT32 sslot);

void M_CheatActivationResponder(INT32 ch);

void M_ModeAttackRetry(INT32 choice);

// Level select updating
void Nextmap_OnChange(void);

// Screenshot menu updating
void Moviemode_mode_Onchange(void);
void Screenshot_option_Onchange(void);

// Addons menu updating
void Addons_option_Onchange(void);

void M_ReplayHut(INT32 choice);
void M_SetPlaybackMenuPointer(void);

void M_RefreshPauseMenu(void);

INT32 HU_GetHighlightColor(void);

fixed_t M_GetMapThumbnail(INT16 mapnum, patch_t **out);

// Moviemode menu updating
void Moviemode_option_Onchange(void);

// Player Setup menu colors linked list
struct menucolor_t {
	menucolor_t *next;
	menucolor_t *prev;
	UINT16 color;
};

extern menucolor_t *menucolorhead, *menucolortail;

void M_AddMenuColor(UINT16 color);
void M_MoveColorBefore(UINT16 color, UINT16 targ);
void M_MoveColorAfter(UINT16 color, UINT16 targ);
UINT16 M_GetColorBefore(UINT16 color);
UINT16 M_GetColorAfter(UINT16 color);
void M_InitPlayerSetupColors(void);
void M_FreePlayerSetupColors(void);

// These defines make it a little easier to make menus
#define DEFAULTMENUSTYLE(id, header, source, prev, x, y)\
{\
	id,\
	header,\
	sizeof(source)/sizeof(menuitem_t),\
	prev,\
	source,\
	M_DrawGenericMenu,\
	x, y,\
	0,\
	NULL\
}

#define DEFAULTSCROLLMENUSTYLE(id, header, source, prev, x, y)\
{\
	id,\
	header,\
	sizeof(source)/sizeof(menuitem_t),\
	prev,\
	source,\
	M_DrawGenericScrollMenu,\
	x, y,\
	0,\
	NULL\
}

#define PAUSEMENUSTYLE(source, x, y)\
{\
	MN_SPECIAL,\
	NULL,\
	sizeof(source)/sizeof(menuitem_t),\
	NULL,\
	source,\
	M_DrawPauseMenu,\
	x, y,\
	0,\
	NULL\
}

#define CENTERMENUSTYLE(id, header, source, prev, y)\
{\
	id,\
	header,\
	sizeof(source)/sizeof(menuitem_t),\
	prev,\
	source,\
	M_DrawCenteredMenu,\
	BASEVIDWIDTH/2, y,\
	0,\
	NULL\
}

#define MAPICONMENUSTYLE(header, source, prev)\
{\
	MN_NONE,\
	header,\
	sizeof (source)/sizeof (menuitem_t),\
	prev,\
	source,\
	M_DrawServerMenu,\
	24,40,\
	0,\
	NULL\
}

#define IMAGEDEF(source)\
{\
	MN_SPECIAL,\
	NULL,\
	sizeof (source)/sizeof (menuitem_t),\
	NULL,\
	source,\
	M_DrawImageDef,\
	0, 0,\
	0,\
	NULL\
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif //__X_MENU__
