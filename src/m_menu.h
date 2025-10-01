// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 2011-2016 by Matthew "Kaito Sinclaire" Walsh.
// Copyright (C) 1999-2025 by Sonic Team Junior.
// Copyright (C) 2025 by Team BlanKart.
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

// max size of menu stack
#define NUMMENULEVELS 8

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
boolean M_Responder(event_t *ev);

// Called by main loop, runs for demo playback. If this returns true, nullify any further user input.
boolean M_DemoResponder(event_t *ev);

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
void M_DrawTextBox(INT32 x, INT32 y, INT32 width, INT32 boxlines);

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

#define M_StartMessage(string, routine, itemtype) M_StartMessage2(string, (void (*)(void))routine, itemtype)
void M_StartMessage2(const char *string, void (*routine)(void), menumessagetype_t itemtype);

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
	IT_OVERLAY   = 1<<9, // item is drawn at screen coordinates, without scrolling

	IT_CENTER    = 1<<10, // center the text/patch
	IT_RIGHT     = 1<<11, // right-align the text/patch
	IT_SMALL     = 1<<12, // draw at half scale
	IT_STICKER   = 1<<13, // draw a sticker behind the text

#define sh 14
	ITH_NONE      = 0<<sh, // no highlight
	ITH_HIGHLIGHT = 1<<sh, // add highlightflags to text/patch
	ITH_RECOMMEND = 2<<sh, // add recommendedflags to text/patch
	ITH_WARNING   = 3<<sh, // add warningflags to text/patch
	ITH_MASK      = 0x3<<sh,
#undef sh

#define sh 16
	ITF_STANDARD  = 0<<sh, // standard font
	ITF_HEADER    = 1<<sh, // standard font, with an offset to the left
	ITF_THIN      = 2<<sh, // thin font
	ITF_THIN2     = 3<<sh, // thin font with tighter spacing
	ITF_FILL      = 4<<sh, // call drawfill using parameters in item->patch
	ITF_PATCH     = 5<<sh, // display a patch instead of text
	ITF_THUMBNAIL = 6<<sh, // display the thumbnail of the mapname in item->patch
	ITF_IMAGETYPE = ITF_PATCH,
	ITF_MASK      = 0x7<<sh,
#undef sh

	IT_STYLE     = IT_CENTER|IT_RIGHT|IT_SMALL|IT_STICKER|ITH_MASK|ITF_MASK,
} menuitemflags_t;

#define MAXSTRINGLENGTH 32

typedef INT32 (menufunc_f)(INT32);
typedef void  (menudrawer_f)(void);

//
// MENU TYPEDEFS
//
struct menuitem_t
{
	dehinfo_t info;

	char *text;    // left-side string
	char *patch;   // name of patch lump to draw, OR right-side string
	char *tooltip; // help text displayed at bottom

	consvar_t *cvar;     // console variable to modify
	menufunc_f *routine; // menu routine to call
	menutype_t submenu;  // navigate to another menu

	menuitemflags_t status; // show IT_xxx

	INT32 argument; // passed to routine/submenu, OR step size for cvars
	INT16 x, y;     // coordinates, see menuitemflags
};

struct menu_t
{
	dehinfo_t      info;
	char          *headerpic;
	INT16          numitems;           // # of menu items
	menuitem_t    *menuitems;          // menu items
	menudrawer_f  *drawroutine;        // draw routine
	INT16          x, y;               // x, y of menu
	INT16          scrollheight;       // height of scrolling area
	INT16          lastOn;             // last item user was on in menu
	INT16          cursoroffset;       // X offset of cursor
	INT16          lineheight;         // default Y offset to add for each item
	menufunc_f    *enterroutine;       // called before enter a menu
	menufunc_f    *quitroutine;        // called before quit a menu
	menufunc_f    *keyhandler;         // called before key press is processed

	// MENUPRES STUFF BELOW

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

	// Music credit shit.
	boolean muscreditshow;
	fixed_t muscredity;
	UINT16 muscreditanimtime;
	INT32 muscreditsnapflags;

	boolean enterbubble; // run all entrance line execs after common ancestor and up to child. If false, only run the child's exec
	boolean exitbubble; // run all exit line execs from child and up to before common ancestor. If false, only run the child's exec
	INT32 entertag; // line exec to run on menu enter, if titlemap
	INT32 exittag; // line exec to run on menu exit, if titlemap
	INT16 enterwipe; // wipe type to run on menu enter, -1 means default
	INT16 exitwipe; // wipe type to run on menu exit, -1 means default
};

extern menu_t menudefs[MAXMENUTYPES];
extern UINT16 nummenutypes;

void M_EnterMenu(menutype_t menu, boolean callexit, INT32 arg);
void M_ExitMenu(void);
void M_ClearMenus(boolean callexitmenufunc);
menuitem_t *M_CheckMenuItem(menutype_t type, const char *name);
menuitem_t *M_GetMenuItem(menutype_t type, const char *name);

INT32 MR_SinglePlayerMenu(INT32 choice);
INT32 MR_Options(INT32 choice);
INT32 MR_Addons(INT32 choice);
INT32 MR_QuitAddons(INT32 choice);
INT32 MR_QuitSRB2(INT32 choice);
INT32 MR_Statistics(INT32 choice);
INT32 MR_HandleLevelStats(INT32 choice);
INT32 MR_QuitReplayHut(INT32 choice);
INT32 MR_HandleReplayHutList(INT32 choice);
INT32 MR_GrandPrixTemp(INT32 choice);
INT32 MR_MainMenu(INT32 choice);
INT32 MR_MainMenuExit(INT32 choice);
INT32 MR_TimeAttack(INT32 arg);
INT32 MR_StartGrandPrix(INT32 choice);
INT32 MR_QuitTimeAttackMenu(INT32 choice);
INT32 MR_ChooseTimeAttack(INT32 choice);
INT32 MR_SetGuestReplay(INT32 arg);
INT32 MR_TimeAttackPreset(INT32 arg);
INT32 MR_ReplayTimeAttack(INT32 arg);
INT32 MR_ReplayStaff(INT32 choice);
INT32 MR_ConnectIP(INT32 choice);
INT32 MR_QuitMultiPlayerMenu(INT32 choice);
INT32 MR_StartServerMenu(INT32 choice);
INT32 MR_StartServer(INT32 choice);
INT32 MR_ConnectMenuModChecks(INT32 choice);
INT32 MR_CancelConnect(INT32 choice);
INT32 MR_SetupControlsMenu(INT32 arg);
INT32 MR_Refresh(INT32 choice);
INT32 MR_Connect(INT32 arg);
INT32 MR_VideoModeMenu(INT32 choice);
#ifdef HWRENDER
INT32 MR_OpenGLOptionsMenu(INT32 choice);
#endif
INT32 MR_HandleVideoMode(INT32 ch);
INT32 MR_PlaySound(INT32 choice);
INT32 MR_MusicTest(INT32 choice);
INT32 MR_QuitMusicTest(INT32 choice);
INT32 MR_ScreenshotOptions(INT32 choice);
INT32 MR_AddonsOptions(INT32 choice);
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
INT32 MR_MapChange(INT32 choice);
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
INT32 MR_PlaybackQuit(INT32 choice);
INT32 MR_HandleImageDef(INT32 choice);
INT32 MR_HandleMusicTest(INT32 choice);
INT32 MR_ResetControls(INT32 choice);
INT32 MR_ChangeControl(INT32 arg);
INT32 MR_AssignJoystick(INT32 arg);
INT32 MR_HandleMonitorToggles(INT32 choice);
INT32 MR_RestartAudio(INT32 choice);
INT32 MR_EnterViewServer(INT32 choice);
INT32 MR_QuitViewServer(INT32 choice);
INT32 MR_HandleViewServer(INT32 choice);
INT32 MR_CameraSetup(INT32 arg);
#ifdef HAVE_DISCORDRPC
INT32 MR_HandleDiscordRequests(INT32 choice);
#endif

void MD_DrawGenericMenu(void);
void MD_DrawChecklist(void);
void MD_DrawLevelStats(void);
void MD_DrawReplayHut(void);
void MD_DrawTimeAttackMenu(void);
void MD_DrawMPMainMenu(void);
void MD_DrawSetupMultiPlayerMenu(void);
void MD_DrawCssStatBacker(void);
void MD_DrawCssStatBars(void);
void MD_DrawCssColourBar(void);
void MD_DrawCssCharacter(void);
void MD_DrawBarCssSelector(void);
void MD_DrawGridCssSelector(void);
void MD_DrawConnectMenu(void);
void MD_DrawVideoMode(void);
void MD_DrawAddons(void);
void MD_DrawReplayStartMenu(void);
void MD_DrawPlaybackMenu(void);
void MD_DrawImageDef(void);
void MD_DrawMusicTest(void);
void MD_DrawJoystick(void);
void MD_DrawMonitorToggles(void);
void MD_DrawViewServer(void);
#ifdef HAVE_DISCORDRPC
void MD_DrawDiscordRequests(void);
#endif

INT32 MapGridSelectToSkin(INT32 column, INT32 row);

// Maybe this goes here????? Who knows.
boolean M_MouseNeeded(void);

boolean M_AttackOnStack(void);

#ifdef HAVE_THREADS
extern I_mutex m_menu_mutex;
#endif

// Call upon joystick hotplug
INT32 MR_SetupJoystickMenu(INT32 arg);

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
	UINT16 skinnum;
	UINT8 numemeralds;
	UINT8 numgameovers;
	INT32 lives;
	INT32 continuescore;
	INT32 gamemap;
} saveinfo_t;

extern consvar_t cv_showfocuslost;
extern consvar_t cv_newgametype, cv_nextmap, cv_chooseskin, cv_serversort;
extern consvar_t cv_dummygpdifficulty, cv_dummygpencore, cv_dummygpcup;
extern consvar_t cv_dummymenuplayer, cv_dummyteam, cv_dummyspectate, cv_dummyscramble;
extern consvar_t cv_dummyattackingrings, cv_dummyattackingstacking,  cv_dummyattackingchaining;
extern consvar_t cv_dummyattackingslipdash, cv_dummyattackingpurpledrift, cv_dummyattackingslopeboost;
extern consvar_t cv_dummyattackingairdrop, cv_dummyattackingbumpspark;
extern consvar_t cv_dummystaff;
extern consvar_t cv_dummymultiplayer, cv_dummyip, cv_dummyname, cv_dummyfollower, cv_dummycolor;
extern consvar_t cv_dummyserverpage;

extern consvar_t cv_menucaps;
// allow menu text to be displayed in lowercase
#define MENUCAPS (!cv_menucaps.value ? V_ALLOWLOWERCASE : 0)
extern CV_PossibleValue_t gametype_cons_t[];

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

// Level select updating
void Nextmap_OnChange(void);

// Screenshot menu updating
void Moviemode_mode_Onchange(void);
void Screenshot_option_Onchange(void);

// Addons menu updating
void Addons_option_Onchange(void);

INT32 MR_ReplayHut(INT32 choice);
void M_SetPlaybackMenuPointer(void);

void M_RefreshPauseMenu(void);

INT32 HU_GetHighlightColor(void);

fixed_t M_GetMapThumbnail(INT16 mapnum, patch_t **out);

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

extern boolean menu_text_input;

#ifdef __cplusplus
} // extern "C"
#endif

#endif //__X_MENU__
