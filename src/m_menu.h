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

#define MAXREPLAYQUERY 37

// Compatibility with old-style named NiGHTS replay files.
#define OLDNREPLAYNAME

//
// MENUS
//

extern int32_t menuKey; // keyboard key pressed for menu

#define NUMVIRTUALKEYSINROW (10+2) // 1-9, 0, and a right-side gutter of two keys' width
extern int16_t virtualKeyboard[5][NUMVIRTUALKEYSINROW];
extern int16_t shift_virtualKeyboard[5][NUMVIRTUALKEYSINROW];

typedef struct menutyping_s
{
	dboolean active;                         // Active
	dboolean menutypingclose;        // Closing
	dboolean keyboardtyping;         // If true, all keystrokes are treated as typing (ignores MBT_A etc). This is unset if you try moving the cursor on the virtual keyboard or use your controller
	int8_t menutypingfade;           // fade in and out

	int8_t keyboardx;
	int8_t keyboardy;
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
	int8_t ev_dpad_ud; // Handles up and down for the actual dpad set by events
	int8_t ev_dpad_lr; // Handles left and right for the actual dpad set by events

	// Current frame's data
	int8_t dpad_ud; // up / down dpad
	int8_t dpad_lr; // left / right
	uint32_t buttons; // buttons

	// Previous frame's data
	int8_t prev_dpad_ud;
	int8_t prev_dpad_lr;
	uint32_t buttonsHeld; // prev frame's buttons

	uint16_t delay; // menu wait
	uint32_t delayCount; // num times ya did menu wait (to make the wait shorter each time)

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
void M_HandleMessageBox(int32_t thisMenuKey);
void M_SetMenuDelay(uint8_t i);
void M_HandleDpadEvent(event_t* ev);
void M_UpdateMenuCMD(uint8_t i, dboolean bailrequired);
dboolean M_MenuButtonPressed(uint8_t pid, uint32_t bt);
dboolean M_MenuButtonHeld(uint8_t pid, uint32_t bt);
sfxenum_t M_MenuTypingSounds(int32_t key);
void M_MenuTypingInput(int32_t key);
void M_SwitchVirtualKeyboard(dboolean gamepad);
void M_AbortVirtualKeyboard(void);
void M_OpenVirtualKeyboard(textinput_t *textinput, char *buffer, menuitem_t *item);

void M_DoItemRoutine(menuitem_t *item, int32_t thisMenuKey);

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
void M_DrawTextBox(int32_t x, int32_t y, int32_t width, int32_t boxlines, int32_t flags);

// Used in d_netcmd to restart time attack
int32_t MR_ModeAttackRetry(int32_t choice);

// the function to show a message box typing with the string inside
// string must be static (not in the stack)
// routine is a function taking a int32_t in parameter

typedef enum
{
	MM_NOTHING = 0, // is just displayed until the user do someting
	MM_YESNO,       // routine is called with only 'y' or 'n' in param
	MM_EVENTHANDLER // the same of above but without 'y' or 'n' restriction
	// and routine is void routine(event_t *) (ex: set control)
} menumessagetype_t;

typedef union
{
	void (*routine)(int32_t); // MM_YESNO
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
	int16_t x, y;
	int16_t numlines;
	int16_t maxlength;
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

const char *M_ButtonName(uint8_t p, gamecontrols_e gc, const char *fallback, dboolean forcekeyboard);
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
void M_QuitResponse(int32_t ch);

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

typedef int32_t (menufunc_f)(int32_t);
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

	uint8_t font;
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

	int32_t argument; // passed to routine/submenu, OR step size for cvars
	fixed_t x, y;     // coordinates, see menuitemflags
};

struct menupaneldef_t
{
	dehinfo_t     info;
	uint16_t         numitems;           // # of menu items
	menuitem_t   *items;              // menu items
	menudrawer_f *drawroutine;        // draw routine
	dboolean       scrollbar;          // draw scrollbar
	int16_t         scrollheight;       // height of scrolling area
	int16_t         cursoroffset;       // X offset of cursor
	int16_t         lineheight;         // default Y offset to add for each item
	menufunc_f   *keyhandler;         // called before key press is processed
	panelfunc_f  *initializer;        // called when panel instance is setup
	panelfunc_f  *tickroutine;        // called when panel is active
	dboolean multinav;                 // navigation in two dimensions!? (disables cvar arrows)
	int32_t navitemcount;               // items per column (or row if horizontal is true)
	dboolean horizontal;               // navigate with left/right instead of up/down
};

struct menupanelinst_t
{
	char name[MAXMENUPANELNAME+1];
	size_t defnum; // index in menupanels[]
	fixed_t x, y;
	fixed_t scrollheight;
	fixed_t cursoroffset;
	uint16_t numitems;
	menuitem_t *items;
	uint16_t itemon; // currently selected item, OR last selected item for menudef panels
	int32_t vflags;
	dboolean visible;
	int32_t navitemcount;
	dboolean disabled; // can't be selected
	dboolean altscroll; // viewserver hell (list scrolling, usually paired with disabled = true)
	int32_t layer; // higher number draws on top
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
	int8_t fadestrength;  // darken background when displaying this menu, strength 0-31 or -1 for undefined
	int32_t bgcolor; // fill color, overrides bg name. -1 means follow bg name rules.
	int32_t titlescrollxspeed; // background gfx scroll per menu; inherits global setting
	int32_t titlescrollyspeed; // y scroll
	dboolean bghide; // for titlemaps, hide the background.
	dboolean vhs; // Do the VHS effect.

	int8_t hidetitlepics; // hide title gfx per menu; -1 means undefined, inherits global setting
	ttmode_enum ttmode; // title wing animation mode; default TTMODE_KART
	uint8_t ttscale; // scale of title wing gfx (FRACUNIT / ttscale); -1 means undefined, inherits global setting
	char ttname[9]; // lump name of title wing gfx. If name length is <= 6, engine will attempt to load numbered frames (TTNAMExx)
	int16_t ttx; // X position of title wing
	int16_t tty; // Y position of title wing
	int16_t ttloop; // # frame to loop; -1 means dont loop
	uint16_t tttics; // # of tics per frame

	char musname[7]; ///< Music track to play. "" for no music.
	uint16_t mustrack; ///< Subsong to play. Only really relevant for music modules and specific formats supported by GME. 0 to ignore.
	dboolean muslooping; ///< Loop the music
	dboolean musstop; ///< Don't play any music
	dboolean musignore; ///< Let the current music keep playing

	// Music credit shit.
	dboolean muscreditshow;
	fixed_t muscredity;
	uint16_t muscreditanimtime;
	int32_t muscreditsnapflags;

	dboolean enterbubble; // run all entrance line execs after common ancestor and up to child. If false, only run the child's exec
	dboolean exitbubble; // run all exit line execs from child and up to before common ancestor. If false, only run the child's exec
	int32_t entertag; // line exec to run on menu enter, if titlemap
	int32_t exittag; // line exec to run on menu exit, if titlemap
	int16_t enterwipe; // wipe type to run on menu enter, -1 means default
	int16_t exitwipe; // wipe type to run on menu exit, -1 means default
};

extern menu_t menudefs[MAXMENUTYPES];
extern uint16_t nummenutypes;

#define MAXMENUPANELS 4096
extern menupaneldef_t menupanels[MAXMENUPANELS];
extern size_t nummenupanels;

#define MAXMENUITEMSTYLES 256
extern menuitemstyle_t menuitemstyles[MAXMENUITEMSTYLES];
extern size_t nummenuitemstyles;

void M_EnterMenu(menutype_t menu, dboolean callexit, int32_t arg);
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
int32_t MR_Addons(int32_t choice);
int32_t MR_QuitAddons(int32_t choice);
int32_t MR_QuitSRB2(int32_t choice);
int32_t MR_HandleLevelStats(int32_t choice);
int32_t MR_QuitReplayHut(int32_t choice);
int32_t MR_HandleReplayHutList(int32_t choice);
int32_t MR_GrandPrixTemp(int32_t choice);
int32_t MR_TimeAttack(int32_t arg);
int32_t MR_StartGrandPrix(int32_t choice);
int32_t MR_QuitTimeAttackMenu(int32_t choice);
int32_t MR_ChooseTimeAttack(int32_t choice);
int32_t MR_SetGuestReplay(int32_t arg);
int32_t MR_TimeAttackPreset(int32_t arg);
int32_t MR_ReplayTimeAttack(int32_t arg);
int32_t MR_ReplayStaff(int32_t choice);
int32_t MR_ConnectIP(int32_t choice);
int32_t MR_ConnectLastServer(int32_t choice);
int32_t MR_QuitMultiPlayerMenu(int32_t choice);
int32_t MR_StartServer(int32_t choice);
int32_t MR_ConnectMenuModChecks(int32_t choice);
int32_t MR_CancelConnect(int32_t choice);
int32_t MR_SetupControlsMenu(int32_t arg);
int32_t MR_Refresh(int32_t choice);
int32_t MR_Connect(int32_t arg);
int32_t MR_SetVideoMode(int32_t choice);
#ifdef HWRENDER
int32_t MR_OpenGLOptionsMenu(int32_t choice);
#endif
int32_t MR_HandleVideoMode(int32_t ch);
int32_t MR_PlaySound(int32_t choice);
int32_t MR_MusicTest(int32_t choice);
int32_t MR_QuitMusicTest(int32_t choice);
int32_t MR_EraseData(int32_t arg);
int32_t MR_Credits(int32_t choice);
int32_t MR_BlanCredits(int32_t choice);
int32_t MR_SecretCredits(int32_t choice);
int32_t MR_HandleAddons(int32_t choice);
int32_t MR_SelectableClearMenus(int32_t choice);
int32_t MR_GoBack(int32_t choice);
int32_t MR_ModeAttackEndGame(int32_t choice);
int32_t MR_Retry(int32_t choice);
int32_t MR_EndGame(int32_t choice);
int32_t MR_SetupMultiPlayer(int32_t choice);
int32_t MR_HandleSetupMultiPlayerMenu(int32_t choice);
int32_t MR_ConfirmSpectate(int32_t choice);
int32_t MR_ConfirmEnterGame(int32_t choice);
int32_t MR_ConfirmTeamScramble(int32_t choice);
int32_t MR_ConfirmTeamChange(int32_t choice);
int32_t MR_ConfirmSpectateChange(int32_t choice);
int32_t MR_ChangeLevel(int32_t choice);
int32_t MR_HutStartReplay(int32_t choice);
int32_t MR_PlaybackRewind(int32_t choice);
int32_t MR_PlaybackPause(int32_t choice);
int32_t MR_PlaybackFastForward(int32_t choice);
int32_t MR_PlaybackAdvance(int32_t choice);
int32_t MR_PlaybackSetViews(int32_t choice);
int32_t MR_PlaybackAdjustView(int32_t choice);
int32_t MR_PlaybackToggleFreecam(int32_t choice);
int32_t MR_PlaybackToggleDirector(int32_t choice);
int32_t MR_PlaybackQuit(int32_t choice);
int32_t MR_HandleImageDef(int32_t choice);
int32_t MR_HandleMusicTest(int32_t choice);
int32_t MR_ResetControls(int32_t choice);
int32_t MR_ChangeControl(int32_t arg);
int32_t MR_AssignJoystick(int32_t arg);
int32_t MR_HandleMonitorToggles(int32_t choice);
int32_t MR_RestartAudio(int32_t choice);
int32_t MR_QuitViewServer(int32_t choice);
int32_t MR_HandleViewServer(int32_t choice);
int32_t MR_SetLevelListMode(int32_t choice);
int32_t MR_HandleLevelPlatter(int32_t choice);
int32_t MR_LevelPlatterRandom(int32_t choice);
#ifdef HAVE_DISCORDRPC
int32_t MR_HandleDiscordRequests(int32_t choice);
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
int32_t MR_TickStats(int32_t choice);
int32_t MR_TickReplayHutReplayInfo(int32_t choice);
int32_t MR_TickDiscordRequests(int32_t choice);
int32_t MR_TickViewServer(int32_t choice);

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
	uint16_t skinnum;
	uint8_t numemeralds;
	uint8_t numgameovers;
	int32_t lives;
	int32_t continuescore;
	int32_t gamemap;
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

extern int16_t startmap;
extern int32_t ultimate_selectable;
extern int16_t char_on, startchar;

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

int32_t MR_ReplayHut(int32_t choice);
void M_SetPlaybackMenuPointer(void);

void M_RefreshPauseMenu(void);

int32_t HU_GetHighlightColor(void);

fixed_t M_GetMapThumbnail(mapnum_t mapnum, patch_t **out);

// Moviemode menu updating
void Moviemode_option_Onchange(void);

void Skinsort_option_Onchange(void);
void Skinselectstyle_option_Onchange(void);

// Player Setup menu colors linked list
struct menucolor_t {
	menucolor_t *next;
	menucolor_t *prev;
	uint16_t color;
};

extern menucolor_t *menucolorhead, *menucolortail;

void M_AddMenuColor(uint16_t color);
void M_MoveColorBefore(uint16_t color, uint16_t targ);
void M_MoveColorAfter(uint16_t color, uint16_t targ);
uint16_t M_GetColorBefore(uint16_t color);
uint16_t M_GetColorAfter(uint16_t color);
void M_InitPlayerSetupColors(void);
void M_FreePlayerSetupColors(void);

extern consvar_t cv_dummyreplaysearch;

#ifdef __cplusplus
} // extern "C"
#endif

#endif //__X_MENU__
