// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 2011-2016 by Matthew "Kaito Sinclaire" Walsh.
// Copyright (C) 1999-2025 by Sonic Team Junior.
// Copyright (C) 2026 by Team BlanKart.
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
#include "m_textinput.h"
#include "g_input.h"

#ifdef __cplusplus
extern "C" {
#endif

// Compatibility with old-style named NiGHTS replay files.
#define OLDNREPLAYNAME

//
// MENUS
//

extern INT32 menuKey; // keyboard key pressed for menu

#define NUMVIRTUALKEYSINROW (10+2) // 1-9, 0, and a right-side gutter of two keys' width
extern INT16 virtualKeyboard[5][NUMVIRTUALKEYSINROW];
extern INT16 shift_virtualKeyboard[5][NUMVIRTUALKEYSINROW];

typedef struct menutyping_s
{
	dboolean active;                         // Active
	dboolean menutypingclose;        // Closing
	dboolean keyboardtyping;         // If true, all keystrokes are treated as typing (ignores MBT_A etc). This is unset if you try moving the cursor on the virtual keyboard or use your controller
	SINT8 menutypingfade;           // fade in and out

	SINT8 keyboardx;
	SINT8 keyboardy;
	dboolean keyboardcapslock;
	dboolean keyboardshift;

	textinput_t *textinput;
	menuitem_t *item;
} menutyping_t;
// While typing, we'll have a fade strongly darken the screen to overlay the typing menu instead

extern menutyping_t menutyping;

#define MENUDELAYTIME 7
#define MENUMINDELAY 2


typedef enum
{
	MBT_CONFIRM = 1,
	MBT_CANCEL = 1<<1,
	MBT_HOME = 1<<2,
	MBT_INFO1 = 1<<3,
	MBT_INFO2 = 1<<4,
	MBT_INFO3 = 1<<5,
} menuButtonCode_t;

typedef struct menucmd_s
{
	SINT8 ev_dpad_ud; // Handles up and down for the actual dpad set by events
	SINT8 ev_dpad_lr; // Handles left and right for the actual dpad set by events

	// Current frame's data
	SINT8 dpad_ud; // up / down dpad
	SINT8 dpad_lr; // left / right
	UINT32 buttons; // buttons

	// Previous frame's data
	SINT8 prev_dpad_ud;
	SINT8 prev_dpad_lr;
	UINT32 buttonsHeld; // prev frame's buttons

	UINT16 delay; // menu wait
	UINT32 delayCount; // num times ya did menu wait (to make the wait shorter each time)

	dboolean wipebuffer; // Hold the last input sent.
} menucmd_t;

extern menucmd_t menucmd[MAXSPLITSCREENPLAYERS];

// max size of menu stack
#define NUMMENULEVELS 8

#define MAXMENUPANELNAME 16

#define MAXMENUITEMPARTS 4

// Menu IDs sectioned by numeric places to signify hierarchy
typedef enum
{
#define _(name, ...) MN_##name,
#include "info/menus.h"
#undef _

	MN_FIRSTFREESLOT,
	MN_LASTFREESLOT = MN_FIRSTFREESLOT + 128,
	MAXMENUTYPES,
} menutype_t;

extern menutype_t menustack[NUMMENULEVELS];
extern menutype_t restoremenustack[NUMMENULEVELS];

void M_InitMenuPresTables(void);
void M_ChangeMenuMusic(void);
void M_SetMenuCurBackground(void);
void M_SetMenuCurFadeValue(void);
void M_SetMenuCurTitlePics(void);

// Called by main loop,
// saves config file and calls I_Quit when user exits.
// Even when the menu is not displayed,
// this can resize the view and change game parameters.
// Does all the real work of the menu interaction.
dboolean M_Responder(event_t *ev);

menupanelinst_t *M_GetCurrentMenuPanelInst(void);
menupaneldef_t *M_GetCurrentMenuPanelDef(menupanelinst_t *curpi);
menuitem_t *M_GetCurrentMenuItem(menupanelinst_t *curpi);
void M_HandleMessageBox(INT32 thisMenuKey);
void M_SetMenuDelay(UINT8 i);
void M_HandleDpadEvent(event_t* ev);
void M_UpdateMenuCMD(UINT8 i, dboolean bailrequired);
dboolean M_MenuButtonPressed(UINT8 pid, UINT32 bt);
dboolean M_MenuButtonHeld(UINT8 pid, UINT32 bt);
sfxenum_t M_MenuTypingSounds(INT32 key);
void M_MenuTypingInput(INT32 key);
void M_SwitchVirtualKeyboard(dboolean gamepad);
void M_AbortVirtualKeyboard(void);
void M_OpenVirtualKeyboard(textinput_t *textinput, char *buffer, menuitem_t *item);

void M_DoItemRoutine(menuitem_t *item, INT32 thisMenuKey);

// Called by main loop, runs for demo playback. If this returns true, nullify any further user input.
dboolean M_DemoResponder(event_t *ev);

// Called by main loop, only used for menu (skull cursor) animation.
void M_Ticker(void);

// Called by main loop, draws the menus directly into the screen buffer.
void M_Drawer(void);

// Called by D_SRB2Main, loads the config file.
void M_Init(void);

// Called by intro code to force menu up upon a keypress,
// does nothing if menu is already up.
void M_StartControlPanel(void);

// Called on new server add, or other reasons
void M_SortServerList(void);

// Draws a box with a texture inside as background for messages
void M_DrawTextBox(INT32 x, INT32 y, INT32 width, INT32 boxlines, INT32 flags);

// Used in d_netcmd to restart time attack
INT32 MR_ModeAttackRetry(INT32 choice);

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

typedef union
{
	void (*routine)(INT32); // MM_YESNO
	void (*handler)(event_t *); // MM_EVENTHANDLER
} messagebox_callback;

typedef struct
{
	dboolean active;
	char *headertext;
	char *text;
	char *confirmtext;
	char *declinetext;
	messagebox_callback callback;
	menumessagetype_t messagetype;
	INT16 x, y;
	INT16 numlines;
	INT16 maxlength;
} messagebox_t;

typedef enum
{
	MA_NONE = 0,
	MA_YES,
	MA_NO
} manswer_e;

extern messagebox_t messagebox;
extern messagebox_t messageboxqueue[NUMMENULEVELS];

#define MB_ROUTINE(func) (messagebox_callback){.routine = func}
#define MB_HANDLER(func) (messagebox_callback){.handler = func}
#define MB_NONE (messagebox_callback){0}

const char *M_ButtonName(UINT8 p, gamecontrols_e gc, const char *fallback, dboolean forcekeyboard);
char *M_MessageConfirmText(dboolean gamequit, dboolean forcekeyboard);
char *M_MessageCancelText(dboolean gamequit, dboolean forcekeyboard);

void M_MessageboxClose(dboolean clearstack);

//#define M_StartMessage(string, routine, itemtype) M_StartMessage2(NULL, string, ROUTINECAST(routine), itemtype, NULL, NULL)
void M_StartMessage2(const char *headerstr, const char *str, messagebox_callback callback,
					 menumessagetype_t itemtype, const char *confirmstr, const char *declinestr);

typedef enum
{
	M_NOT_WAITING,

	M_WAITING_VERSION,
	M_WAITING_SERVERS,
}
M_waiting_mode_t;

extern M_waiting_mode_t m_waiting_mode;

typedef enum
{
	LLM_CREATESERVER,
	LLM_CUPSELECT,
	LLM_TIMEATTACK,
	LLM_ITEMBREAKER,
	LLM_BOSS,
	LLM__MAX,
} levellistmode_e;

// Called by linux_x/i_video_xshm.c
void M_QuitResponse(INT32 ch);

// Determines whether to show a level in the list (platter version does not need to be exposed)
dboolean M_CanShowLevelInList(mapnum_t mapnum);

typedef enum
{
	IT_INTERACT  = 1<<0, // item can be interacted with
	IT_HIDDEN    = 1<<1, // item is invisible and cannot be interacted with (has priority over IT_INTERACT)
	IT_GRAYEDOUT = 1<<2, // item is grayed out and non-interactible
	IT_SECRET    = 1<<3, // item text is displayed with question marks and non-interactible

	IT_ARROWS = 1<<4, // call-type items use arrow keys instead of enter
	IT_SLIDER = 1<<5, // cvar-type items display a slider

	IT_OFSX      = 1<<6, // X coordinate is relative to current position
	IT_OFSY      = 1<<7, // ditto
	IT_TEMPORARY = 1<<8, // with IT_OFS*, offset applies only to this item
} menuitemflags_t;

typedef enum
{
	ITT_STRING,     // draws the text source directly using the font in part->font
	ITT_FILL,       // parses the text source and calls drawfill
	ITT_PATCH,      // draws a patch named by the text source
	ITT_THUMBNAIL,  // draws the thumbnail of the named map
	ITT_SKINICON,   // draws the icon of the skin in the text source, with skincolor in extra
} ATTRPACK itemparttype_e;

typedef enum
{
	ITH_NONE,        // no highlight
	ITH_HIGHLIGHT,   // highlightflags
	ITH_RECOMMENDED, // recommendedflags
	ITH_WARNING,     // warningflags
} ATTRPACK itemparthighlight_e;

typedef enum
{
	ITA_LEFT,     // align to left
	ITA_CENTER,   // align to center
	ITA_RIGHT,    // align to right
	ITA_EDGE,     // align to right edge of panel
} ATTRPACK itempartalign_e;

typedef enum
{
	ITS_TEXT,    // item->text
	ITS_PATCH,   // item->patch
	ITS_EXTRA,   // item->extra
	ITS_FIXED,   // part->fixedstring
	ITS_FIXED2,  // part->fixedstring2
	ITS_DERIVED, // (parent style's source)
} ATTRPACK itempartsource_e;

typedef INT32 (menufunc_f)(INT32);
typedef void  (menudrawer_f)(menupanelinst_t *);
typedef void  (panelfunc_f)(menupanelinst_t *);

//
// MENU TYPEDEFS
//
struct menuitempart_t
{
	itemparttype_e type;
	itemparthighlight_e highlight;
	itempartalign_e align;
	itempartsource_e source, source2;

	UINT8 font;
	dboolean fontalt; // V_6WIDTHSPACE
	dboolean small;
	dboolean translucent;
	dboolean nohighlight;
	fixed_t xofs, yofs;
	char *fixedstring, *fixedstring2;
};

struct menuitemstyle_t
{
	dehinfo_t info;
	size_t parent;
	menuitempart_t parts[MAXMENUITEMPARTS];
	dboolean partenabled[MAXMENUITEMPARTS];
};

struct menuitem_t
{
	dehinfo_t info;

	char *text;
	char *patch;
	char *tooltip; // help text displayed at bottom
	char *extra;   // used by some styles

	consvar_t *cvar;     // console variable to modify
	menufunc_f *routine; // menu routine to call
	menutype_t submenu;  // navigate to another menu

	menuitemflags_t status; // show IT_xxx
	const menuitemstyle_t *style;

	INT32 argument; // passed to routine/submenu, OR step size for cvars
	fixed_t x, y;     // coordinates, see menuitemflags
};

struct menupaneldef_t
{
	dehinfo_t     info;
	UINT16         numitems;           // # of menu items
	menuitem_t   *items;              // menu items
	menudrawer_f *drawroutine;        // draw routine
	dboolean       scrollbar;          // draw scrollbar
	INT16         scrollheight;       // height of scrolling area
	INT16         cursoroffset;       // X offset of cursor
	INT16         lineheight;         // default Y offset to add for each item
	menufunc_f   *keyhandler;         // called before key press is processed
	panelfunc_f  *initializer;        // called when panel instance is setup
	panelfunc_f  *tickroutine;        // called when panel is active
	dboolean multinav;                 // navigation in two dimensions!? (disables cvar arrows)
	INT32 navitemcount;               // items per column (or row if horizontal is true)
	dboolean horizontal;               // navigate with left/right instead of up/down
};

struct menupanelinst_t
{
	char name[MAXMENUPANELNAME+1];
	size_t defnum; // index in menupanels[]
	fixed_t x, y;
	fixed_t scrollheight;
	fixed_t cursoroffset;
	UINT16 numitems;
	menuitem_t *items;
	UINT16 itemon; // currently selected item, OR last selected item for menudef panels
	INT32 vflags;
	dboolean visible;
	INT32 navitemcount;
	dboolean disabled; // can't be selected
	dboolean altscroll; // viewserver hell (list scrolling, usually paired with disabled = true)
	INT32 layer; // higher number draws on top
	dboolean cliprect; // Clips everything contained inside it
	dboolean cliprect_relative; // use the x and y position of the panel for the cliprect
	fixed_t cliprect_x, cliprect_y; // X and Y of a cliprect when not relative
	fixed_t cliprect_width, cliprect_height; // Width and Height of added cliprect
};

struct menu_t
{
	dehinfo_t      info;
	char          *headerpic;
	menufunc_f    *enterroutine;       // called before enter a menu
	menufunc_f    *quitroutine;        // called before quit a menu
	menufunc_f    *tickroutine;        // ticker routine
	size_t         numpanels;
	menupanelinst_t *panels;

	char bgname[8]; // name for background gfx lump; lays over titlemap if this is set
	SINT8 fadestrength;  // darken background when displaying this menu, strength 0-31 or -1 for undefined
	INT32 bgcolor; // fill color, overrides bg name. -1 means follow bg name rules.
	INT32 titlescrollxspeed; // background gfx scroll per menu; inherits global setting
	INT32 titlescrollyspeed; // y scroll
	dboolean bghide; // for titlemaps, hide the background.
	dboolean vhs; // Do the VHS effect.

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
	dboolean muslooping; ///< Loop the music
	dboolean musstop; ///< Don't play any music
	dboolean musignore; ///< Let the current music keep playing

	// Music credit shit.
	dboolean muscreditshow;
	fixed_t muscredity;
	UINT16 muscreditanimtime;
	INT32 muscreditsnapflags;

	dboolean enterbubble; // run all entrance line execs after common ancestor and up to child. If false, only run the child's exec
	dboolean exitbubble; // run all exit line execs from child and up to before common ancestor. If false, only run the child's exec
	INT32 entertag; // line exec to run on menu enter, if titlemap
	INT32 exittag; // line exec to run on menu exit, if titlemap
	INT16 enterwipe; // wipe type to run on menu enter, -1 means default
	INT16 exitwipe; // wipe type to run on menu exit, -1 means default
};

extern menu_t menudefs[MAXMENUTYPES];
extern UINT16 nummenutypes;

#define MAXMENUPANELS 4096
extern menupaneldef_t menupanels[MAXMENUPANELS];
extern size_t nummenupanels;

#define MAXMENUITEMSTYLES 256
extern menuitemstyle_t menuitemstyles[MAXMENUITEMSTYLES];
extern size_t nummenuitemstyles;

void M_EnterMenu(menutype_t menu, dboolean callexit, INT32 arg);
void M_ExitMenu(void);
void M_ClearMenusEx(dboolean callexitmenufunc, dboolean notitle);
#define M_ClearMenus(callexitmenufunc) M_ClearMenusEx(callexitmenufunc, false)
/*FNONULL*/ menupaneldef_t *M_AllocateMenuPanelDef(const char *name);
/*RNONULL INONULL1(1)*/ menuitemstyle_t *M_AllocateMenuItemStyle(const char *name, const menuitemstyle_t *parent);
/*ANONULL*/ menuitem_t *M_CheckMenuItemDef(const menupaneldef_t *pd, const char *name);
/*FNONULL*/ menuitem_t *M_GetMenuItemDef(const menupaneldef_t *pd, const char *name);
void M_RestoreMenuStack(dboolean backup);

panelfunc_f
MP_SinglePlayerMain, MP_Options, MP_GameOptions, MP_SetupControls, MP_CameraSetup,
MP_ScreenshotOptions, MP_AddonsOptions, MP_SetupMultiPlayer, MP_SetupMonitorToggles,
MP_PrepReplayList, MP_ReplayHutStart, MP_VideoModeList, MP_MultiplayerPause,
MP_PlayerSplitscreen, MP_SetupJoystick, MP_PrepareLevelPlatter, MP_LevelPlatterOptions,
MP_TimeAttackMenu, MP_TimeAttackPresets, MP_SetupViewServer, MP_ViewServerPlayers,
MP_ViewServerAddons, MP_PrepAddonList, MP_StatsRecords, MP_StatsRecordsBG,
MP_StatsPlaytime, MP_StatsExtra, MP_UnlockablesChecklist
;
INT32 MR_Addons(INT32 choice);
INT32 MR_QuitAddons(INT32 choice);
INT32 MR_QuitSRB2(INT32 choice);
INT32 MR_HandleLevelStats(INT32 choice);
INT32 MR_QuitReplayHut(INT32 choice);
INT32 MR_HandleReplayHutList(INT32 choice);
INT32 MR_GrandPrixTemp(INT32 choice);
INT32 MR_TimeAttack(INT32 arg);
INT32 MR_StartGrandPrix(INT32 choice);
INT32 MR_QuitTimeAttackMenu(INT32 choice);
INT32 MR_ChooseTimeAttack(INT32 choice);
INT32 MR_SetGuestReplay(INT32 arg);
INT32 MR_TimeAttackPreset(INT32 arg);
INT32 MR_ReplayTimeAttack(INT32 arg);
INT32 MR_ReplayStaff(INT32 choice);
INT32 MR_ConnectIP(INT32 choice);
INT32 MR_ConnectLastServer(INT32 choice);
INT32 MR_QuitMultiPlayerMenu(INT32 choice);
INT32 MR_StartServer(INT32 choice);
INT32 MR_ConnectMenuModChecks(INT32 choice);
INT32 MR_CancelConnect(INT32 choice);
INT32 MR_SetupControlsMenu(INT32 arg);
INT32 MR_Refresh(INT32 choice);
INT32 MR_Connect(INT32 arg);
INT32 MR_SetVideoMode(INT32 choice);
#ifdef HWRENDER
INT32 MR_OpenGLOptionsMenu(INT32 choice);
#endif
INT32 MR_HandleVideoMode(INT32 ch);
INT32 MR_PlaySound(INT32 choice);
INT32 MR_MusicTest(INT32 choice);
INT32 MR_QuitMusicTest(INT32 choice);
INT32 MR_EraseData(INT32 arg);
INT32 MR_Credits(INT32 choice);
INT32 MR_BlanCredits(INT32 choice);
INT32 MR_SecretCredits(INT32 choice);
INT32 MR_HandleAddons(INT32 choice);
INT32 MR_SelectableClearMenus(INT32 choice);
INT32 MR_GoBack(INT32 choice);
INT32 MR_ModeAttackEndGame(INT32 choice);
INT32 MR_Retry(INT32 choice);
INT32 MR_EndGame(INT32 choice);
INT32 MR_SetupMultiPlayer(INT32 choice);
INT32 MR_HandleSetupMultiPlayerMenu(INT32 choice);
INT32 MR_ConfirmSpectate(INT32 choice);
INT32 MR_ConfirmEnterGame(INT32 choice);
INT32 MR_ConfirmTeamScramble(INT32 choice);
INT32 MR_ConfirmTeamChange(INT32 choice);
INT32 MR_ConfirmSpectateChange(INT32 choice);
INT32 MR_ChangeLevel(INT32 choice);
INT32 MR_HutStartReplay(INT32 choice);
INT32 MR_PlaybackRewind(INT32 choice);
INT32 MR_PlaybackPause(INT32 choice);
INT32 MR_PlaybackFastForward(INT32 choice);
INT32 MR_PlaybackAdvance(INT32 choice);
INT32 MR_PlaybackSetViews(INT32 choice);
INT32 MR_PlaybackAdjustView(INT32 choice);
INT32 MR_PlaybackToggleFreecam(INT32 choice);
INT32 MR_PlaybackToggleDirector(INT32 choice);
INT32 MR_PlaybackQuit(INT32 choice);
INT32 MR_HandleImageDef(INT32 choice);
INT32 MR_HandleMusicTest(INT32 choice);
INT32 MR_ResetControls(INT32 choice);
INT32 MR_ChangeControl(INT32 arg);
INT32 MR_AssignJoystick(INT32 arg);
INT32 MR_HandleMonitorToggles(INT32 choice);
INT32 MR_RestartAudio(INT32 choice);
INT32 MR_QuitViewServer(INT32 choice);
INT32 MR_HandleViewServer(INT32 choice);
INT32 MR_SetLevelListMode(INT32 choice);
INT32 MR_HandleLevelPlatter(INT32 choice);
INT32 MR_LevelPlatterRandom(INT32 choice);
#ifdef HAVE_DISCORDRPC
INT32 MR_HandleDiscordRequests(INT32 choice);
#endif

void MD_DrawLevelStats(menupanelinst_t *pi);
void MD_DrawReplayHut(menupanelinst_t *pi);
void MD_DrawTimeAttackMenu(menupanelinst_t *pi);
void MD_DrawSetupMultiPlayerMenu(menupanelinst_t *pi);
void MD_DrawConnectMenu(menupanelinst_t *pi);
void MD_DrawVideoMode(menupanelinst_t *pi);
void MD_DrawAddons(menupanelinst_t *pi);
void MD_DrawReplayStartMenu(menupanelinst_t *pi);
void MD_DrawPlaybackMenu(menupanelinst_t *pi);
void MD_DrawImageDef(menupanelinst_t *pi);
void MD_DrawMusicTest(menupanelinst_t *pi);
void MD_DrawJoystick(menupanelinst_t *pi);
void MD_DrawMonitorToggles(menupanelinst_t *pi);
void MD_DrawLevelPlatterMenu(menupanelinst_t *pi);
void MD_DrawReplayHutReplayInfo(menupanelinst_t *pi);
#ifdef HAVE_DISCORDRPC
void MD_DrawDiscordRequests(menupanelinst_t *pi);
#endif

// Tickers
INT32 MR_TickStats(INT32 choice);
INT32 MR_TickReplayHutReplayInfo(INT32 choice);
INT32 MR_TickDiscordRequests(INT32 choice);
INT32 MR_TickViewServer(INT32 choice);

// Panel Tickers
void MP_TickPlayBackMenu(menupanelinst_t *pi);
void MP_TickTimeAttackMenu(menupanelinst_t *pi);
void MP_TickImageDef(menupanelinst_t *pi);
void MP_TickMultiPlayerMenu(menupanelinst_t *pi);
void MP_TickAddonsURL(menupanelinst_t *pi);
void MP_TickAddonsBackground(menupanelinst_t *pi);

void MP_TickReplayHutInfoForeground(menupanelinst_t *pi);
void MP_TickReplayHutInfoBackground(menupanelinst_t *pi);
void MP_TickReplayHutMain(menupanelinst_t *pi);
void MP_TickReplayHutDemoList(menupanelinst_t *pi);
void MP_TickReplayHutRankings(menupanelinst_t *pi);
void MP_TickReplayHutDemoTitle(menupanelinst_t *pi);

void MP_TickOptions(menupanelinst_t *pi);

// Maybe this goes here????? Who knows.
dboolean M_MouseNeeded(void);

dboolean M_AttackOnStack(void);
void M_ReturnToConnectMenu(void);

#ifdef HAVE_THREADS
extern I_mutex m_menu_mutex;
#endif

// Call upon joystick hotplug
void M_UpdateJoystickMenu(void);

// savegame struct for save game menu
typedef struct
{
	char levelname[32];
	UINT16 skinnum;
	UINT8 numemeralds;
	UINT8 numgameovers;
	INT32 lives;
	INT32 continuescore;
	INT32 gamemap;
} saveinfo_t;

extern consvar_t cv_showfocuslost;
extern consvar_t cv_newgametype, cv_nextmap, cv_nextcup, cv_chooseskin, cv_serversort, cv_levelsort;
extern consvar_t cv_dummygpdifficulty, cv_dummygpencore;
extern consvar_t cv_dummymenuplayer, cv_dummyteam, cv_dummyspectate, cv_dummyscramble;
extern consvar_t cv_dummystaff;
extern consvar_t cv_dummymultiplayer, cv_dummyip, cv_dummyname, cv_dummyfollower, cv_dummycolor;
extern consvar_t cv_dummyserverpage;

extern consvar_t cv_menucaps;
// allow menu text to be displayed in lowercase
#define MENUCAPS (!cv_menucaps.value ? V_ALLOWLOWERCASE : 0)
extern CV_PossibleValue_t gametype_cons_t[];

extern consvar_t cv_debugvirtualkeyboard;

void M_UpdateNumServerPages(void);

extern char dummystaffname[22];

extern INT16 startmap;
extern INT32 ultimate_selectable;
extern INT16 char_on, startchar;

#define MAXSAVEGAMES 31
#define NOSAVESLOT 0 //slot where Play Without Saving appears
#define MARATHONSLOT 420 // just has to be nonzero, but let's use one that'll show up as an obvious error if something goes wrong while not using our existing saves

#define BwehHehHe() S_StartSound(NULL, sfx_bewar1+M_RandomKey(4)) // Bweh heh he

// File name appender for RA gametype+mod reading.
char *M_AppendGametypeAndModName(void);

// Screenshot menu updating
void Moviemode_mode_Onchange(void);
void Screenshot_option_Onchange(void);

// Addons menu updating
void Addons_option_Onchange(void);

INT32 MR_ReplayHut(INT32 choice);
void M_SetPlaybackMenuPointer(void);

void M_RefreshPauseMenu(void);

INT32 HU_GetHighlightColor(void);

fixed_t M_GetMapThumbnail(mapnum_t mapnum, patch_t **out);

// Moviemode menu updating
void Moviemode_option_Onchange(void);

void Skinsort_option_Onchange(void);
void Skinselectstyle_option_Onchange(void);

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

#ifdef __cplusplus
} // extern "C"
#endif

#endif //__X_MENU__
