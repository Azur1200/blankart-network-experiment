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
/// \file  st_stuff.c
/// \brief Status bar code
///        Does the face/direction indicator animatin.
///        Does palette indicators as well (red pain/berserk, bright pickup)

#include "doomdef.h"
#include "g_game.h"
#include "i_sound.h"
#include "r_local.h"
#include "p_local.h"
#include "f_finale.h"
#include "st_stuff.h"
#include "i_video.h"
#include "v_video.h"
#include "z_zone.h"
#include "hu_stuff.h"
#include "console.h"
#include "s_sound.h"
#include "i_system.h"
#include "m_menu.h"
#include "m_cheat.h"
#include "m_misc.h" // moviemode
#include "m_anigif.h" // cv_gif_downscale
#include "p_setup.h" // NiGHTS grading
#include "k_grandprix.h"	// we need to know grandprix status for titlecards
#include "k_director.h"
#include "k_boss.h"
#include "r_fps.h"
#include "g_input.h"

//random index
#include "m_random.h"

// item finder
#include "m_cond.h"

#ifdef HWRENDER
#include "hardware/hw_main.h"
#endif

#include "lua_hudlib_drawlist.h"
#include "lua_hud.h"
#include "lua_hook.h"

// SRB2Kart
#include "k_hud.h" // SRB2kart
#include "v_video.h"
#include "r_skins.h" // NUMFACES
#include "k_kart.h"

#include "r_fps.h"

// variable to stop mayonaka static from flickering
consvar_t cv_lessflicker = CVAR_INIT ("lessflicker", "Off", CV_SAVE, CV_OnOff, NULL);

consvar_t cv_stagetitle = CVAR_INIT ("maptitle", "On", CV_SAVE, CV_OnOff, NULL);

#define IMPL_RNGSEED_POSSIBLEVALUE(name) {PR_##name, #name},

static CV_PossibleValue_t rngseed_cons_t[] = {\
	{-1, "Off"},
	{PR_UNDEFINED, "LEGACY"},
	RNGSEEDS(IMPL_RNGSEED_POSSIBLEVALUE)
	{0, NULL}
};

#undef IMPL_RNGSEED_POSSIBLEVALUE

consvar_t cv_rngpeek = CVAR_INIT("rngpeek", "Off", CV_SAVE, rngseed_cons_t, NULL);

uint16_t objectsdrawn = 0;

// dumb fade thing for director toggle
tic_t directortoggletimer = 0;

//
// STATUS BAR DATA
//

patch_t *faceprefix[MAXSKINS][NUMFACES];

// ------------------------------------------
//             status bar overlay
// ------------------------------------------

// Midnight Channel:
static patch_t *hud_tv1;
static patch_t *hud_tv2;

#ifdef HAVE_DISCORDRPC
// Discord Rich Presence
static patch_t *envelope;
#endif

// current player for overlay drawing
player_t *stplyr;
uint8_t stplyrnum;

static huddrawlist_h luahuddrawlist_game[MAXSPLITSCREENPLAYERS];
static huddrawlist_h luahuddrawlist_titlecard;

//
// STATUS BAR CODE
//

dboolean ST_SameTeam(player_t *a, player_t *b)
{
	// Spectator chat.
	if (a->spectator && b->spectator)
	{
		return true;
	}

	// Team chat.
	if (G_GametypeHasTeams() == true)
	{
		// You get team messages if you're on the same team.
		return (a->ctfteam == b->ctfteam);
	}
	else
	{
		// Not that everyone's not on the same team, but team messages go to normal chat if everyone's not in the same team.
		return true;
	}
}

static dboolean st_stopped = true;

static void ST_doPaletteStuff(void)
{
	int32_t palette = 0;

	#ifdef HWRENDER
	if (rendermode == render_opengl && !HWR_ShouldUsePaletteRendering())
		return;
	#endif

	if (stplyr && stplyr->flashcount)
		palette = min(stplyr->flashpal, 13);

	if (palette != st_palette)
	{
		st_palette = palette;

		if (!splitscreen)
		{
			V_SetPalette(palette);
		}
	}
}

void ST_Ticker(dboolean run)
{
	if (st_stopped)
		return;

	if (run)
		ST_runTitleCard();

	// Do red-/gold-shifts from damage/items
	//25/08/99: Hurdler: palette changes is done for all players,
	//                   not only player1! That's why this part
	//                   of code is moved somewhere else.
	ST_doPaletteStuff();
}

// 0 is default, any others are special palettes.
int32_t st_palette = 0;
uint32_t st_translucency = 10;
fixed_t st_fadein = 0;

void ST_UnloadGraphics(void)
{
	Patch_FreeTag(PU_HUDGFX);
}

void ST_LoadGraphics(void)
{
	if (dedicated)
		return;

	// the original Doom uses 'STF' as base name for all face graphics
	// Graue 04-08-2004: face/name graphics are now indexed by skins
	//                   but load them in R_AddSkins, that gets called
	//                   first anyway
	// cache the status bar overlay icons (fullscreen mode)
	K_LoadKartHUDGraphics();

	// Midnight Channel:
	HU_UpdatePatch(&hud_tv1, "HUD_TV1");
	HU_UpdatePatch(&hud_tv2, "HUD_TV2");

#ifdef HAVE_DISCORDRPC
	// Discord Rich Presence
	HU_UpdatePatch(&envelope, "K_REQUES");
#endif
}

static dboolean ST_IconCanSpinout(uint8_t icon)
{
	return ((icon == FACE_MINIMAP)||(icon == FACE_RANK)||(icon == FACE_WANTED));
}

// made separate so that skins code can reload custom face graphics
void ST_LoadFaceGraphics(int32_t skinnum)
{
#define FACE_MAX (FACE_MINIMAP + 1)
	spritedef_t* sprdef = &skins[skinnum]->sprites[SPR2_XTRA];
	spriteframe_t* sprframe;
	uint8_t i = 0, maxer = min(sprdef->numframes, FACE_MAX);
	int16_t baseleftoffs, basetopoffs;
	dboolean alreadycentered = false;
	while (i < maxer)
	{
		// Reset "base" offsets.
		baseleftoffs = basetopoffs = 0;

		sprframe = &sprdef->spriteframes[i];
		faceprefix[skinnum][i] = W_CachePatchNum(sprframe->lumppat[0], PU_HUDGFX);

		alreadycentered = ((faceprefix[skinnum][i]->alignflags & PATCHALIGN_AUTOCENTER) ==
				   PATCHALIGN_AUTOCENTER);

		if ((ST_IconCanSpinout(i)) && (!alreadycentered))
		{
			// Auto-center the pivots of all minimap ,rank and wanted patches with
			// the ability to spinout.
			// This is a shitty, hacky solution... but it's less work for spinout
			// rotations!
			if ((!baseleftoffs) && (faceprefix[skinnum][i]->leftoffset))
			{
				baseleftoffs = faceprefix[skinnum][i]->leftoffset;
			}

			if ((!basetopoffs) && (faceprefix[skinnum][i]->topoffset))
			{
				basetopoffs = faceprefix[skinnum][i]->topoffset;
			}

			faceprefix[skinnum][i]->pivot.x =
			    (faceprefix[skinnum][i]->width / 2) + baseleftoffs;
			faceprefix[skinnum][i]->pivot.y =
			    (faceprefix[skinnum][i]->height / 2) + basetopoffs;

			// We're centered; don't do this operation again.
			faceprefix[skinnum][i]->alignflags |=
			    (PATCHALIGN_AUTOCENTER | PATCHALIGN_USEPIVOTS);
		}
		i++;
	}
	if (i < FACE_MAX)
	{
		patch_t* missing = W_CachePatchName("MISSING", PU_HUDGFX);
		while (i < FACE_MAX)
		{
			faceprefix[skinnum][i] = missing;
			i++;
		}
	}
#undef FACE_MAX
}

void ST_ReloadSkinFaceGraphics(void)
{
	int32_t i;

	for (i = 0; i < numskins; i++)
		ST_LoadFaceGraphics(i);
}

static inline void ST_InitData(void)
{
	// 'link' the statusbar display to a player, which could be
	// another player than consoleplayer, for example, when you
	// change the view in a multiplayer demo with F12.
	stplyr = &players[displayplayers[0]];

	st_palette = -1;
}

static inline void ST_Stop(void)
{
	if (st_stopped)
		return;

#ifdef HWRENDER
	if (rendermode != render_opengl)
#endif
		V_SetPalette(0);

	st_stopped = true;
}

void ST_Start(void)
{
	if (!st_stopped)
		ST_Stop();

	ST_InitData();

	if (!dedicated)
		st_stopped = false;
}

//
// Initializes the status bar, sets the defaults border patch for the window borders.
//

// used by OpenGL mode, holds lumpnum of flat used to fill space around the viewwindow
lumpnum_t st_borderpatchnum;

void ST_Init(void)
{
	if (dedicated)
		return;

	ST_LoadGraphics();

	for (int i = 0; i < MAXSPLITSCREENPLAYERS; i++)
		luahuddrawlist_game[i] = LUA_HUD_CreateDrawList();
	luahuddrawlist_titlecard = LUA_HUD_CreateDrawList();
}

// change the status bar too, when pressing F12 while viewing a demo.
void ST_changeDemoView(void)
{
	// the same routine is called at multiplayer deathmatch spawn
	// so it can be called multiple times
	ST_Start();
}

// =========================================================================
//                         STATUS BAR OVERLAY
// =========================================================================

dboolean st_overlay;

/*
static int32_t SCZ(int32_t z)
{
	return FixedInt(FixedMul(z<<FRACBITS, vid.fdup));
}
*/

/*
static int32_t SCY(int32_t y)
{
	//31/10/99: fixed by Hurdler so it _works_ also in hardware mode
	// do not scale to resolution for hardware accelerated
	// because these modes always scale by default
	y = SCZ(y); // scale to resolution
	if (splitscreen)
	{
		y >>= 1;
		if (stplyrnum != 0)
			y += vid.height / 2;
	}
	return y;
}
*/

/*
static int32_t STRINGY(int32_t y)
{
	//31/10/99: fixed by Hurdler so it _works_ also in hardware mode
	// do not scale to resolution for hardware accelerated
	// because these modes always scale by default
	if (splitscreen)
	{
		y >>= 1;
		if (stplyrnum != 0)
			y += BASEVIDHEIGHT / 2;
	}
	return y;
}
*/

/*
static int32_t SPLITFLAGS(int32_t f)
{
	// Pass this V_SNAPTO(TOP|BOTTOM) and it'll trim them to account for splitscreen! -Red
	if (splitscreen)
	{
		if (stplyrnum != 0)
			f &= ~V_SNAPTOTOP;
		else
			f &= ~V_SNAPTOBOTTOM;
	}
	return f;
}
*/

/*
static int32_t SCX(int32_t x)
{
	return FixedInt(FixedMul(x<<FRACBITS, vid.fdup));
}
*/

#if 0
static int32_t SCR(int32_t r)
{
	fixed_t y;
		//31/10/99: fixed by Hurdler so it _works_ also in hardware mode
	// do not scale to resolution for hardware accelerated
	// because these modes always scale by default
	y = FixedMul(r*FRACUNIT, vid.fdup); // scale to resolution
	if (splitscreen)
	{
		y >>= 1;
		if (stplyrnum != 0)
			y += vid.height / 2;
	}
	return FixedInt(FixedDiv(y, vid.fdup));
}
#endif

// =========================================================================
//                          INTERNAL DRAWING
// =========================================================================

// Devmode information

static void ST_pushRow(int32_t *height)
{
	*height -= 4;
}

static void ST_pushDebugString(int32_t *height, const char *string)
{
	V_DrawRightAlignedString(320, *height, V_MONOSPACE, string);
	*height -= 8;
}

static void ST_pushDebugTimeMS(int32_t *height, const char *label, uint32_t ms)
{
	ST_pushDebugString(height, va("%s%02d:%05.2f", label,
				ms / 60000, ms % 60000 / 1000.f));
}

static void ST_drawMusicDebug(int32_t *height)
{
	char mname[7];
	uint16_t mflags; // unused
	dboolean looping;
	uint8_t i = 0;

	const musicdef_t *def;
	musictype_t format;

	if (!S_MusicInfo(mname, &mflags, &looping))
	{
		ST_pushDebugString(height, "Song: <NOTHING>");
		return;
	}

	def = S_FindMusicDef(mname, &i);
	format = S_MusicType();

	ST_pushDebugTimeMS(height, " Elapsed: ", S_GetMusicPosition());
	ST_pushDebugTimeMS(height, looping
			? "  Loop B: "
			: "Duration: ", S_GetMusicLength());

	if (looping)
	{
		ST_pushDebugTimeMS(height, "  Loop A: ", S_GetMusicLoopPoint());
	}

	if (def)
	{
		ST_pushDebugString(height, va("  Volume: %4d/100", def->volume));
	}

	if (format)
	{
		musictype_t mustype = S_MusicType();
		ST_pushDebugString(height, va("  Format: %s", S_MusicTypeName(mustype)));
	}

	ST_pushDebugString(height, va("    Song: %8s", mname));
}

static void ST_drawImuDebug(int32_t *height)
{
	int32_t pnum = stplyrnum;
	vector3_t accel, gyro, gyrocalibrated, grav, shake;
	if (demo.playback || (!P_IsMachineLocalPlayer(stplyr)))
	{
		ST_pushDebugString(height, va("Only for local player!!"));
		return;
	}

	accel = G_PlayerInputSensor(pnum, ACCELEROMETER);
	gyro = G_PlayerInputSensor(pnum, GYROSCOPE);
	gyrocalibrated = G_GetGamepadCalibratedGyro(pnum);
	grav = G_GetGamepadGravity(pnum);
	shake = G_GetGamepadShake(pnum);

	ST_pushDebugString(height, va(" Grav :%4.2f,%4.2f,%4.2f", FixedToFloat(grav.x), FixedToFloat(grav.y), FixedToFloat(grav.z)));
	ST_pushDebugString(height, va("Shake :%4.2f,%4.2f,%4.2f", FixedToFloat(shake.x), FixedToFloat(shake.y), FixedToFloat(shake.z)));
	ST_pushDebugString(height, va("Cal. G:%4.2f,%4.2f,%4.2f", FixedToFloat(gyrocalibrated.x), FixedToFloat(gyrocalibrated.y), FixedToFloat(gyrocalibrated.z)));
	ST_pushDebugString(height, va(" Gyro :%4.2f,%4.2f,%4.2f", FixedToFloat(gyro.x), FixedToFloat(gyro.y), FixedToFloat(gyro.z)));
	ST_pushDebugString(height, va("Accel :%4.2f,%4.2f,%4.2f", FixedToFloat(accel.x), FixedToFloat(accel.y), FixedToFloat(accel.z)));
}

static void ST_drawRenderDebug(int32_t *height)
{
	const struct RenderStats *i = &g_renderstats;

	ST_pushDebugString(height, va("     Visplanes: %4s", sizeu1(i->visplanes)));
	ST_pushDebugString(height, va("      Drawsegs: %4s", sizeu1(i->drawsegs)));

	ST_pushRow(height);

	ST_pushDebugString(height, va("Skybox Portals: %4s", sizeu1(i->skybox_portals)));
}

static void ST_drawDemoDebug(int32_t *height)
{
	if (!demo.recording && !demo.playback)
		return;

	size_t needle = demo.buffer->p - demo.buffer->buffer;
	size_t size = demo.buffer->size;
	double percent = (double)needle / size * 100.0;
	double avg = (double)needle / leveltime;

	ST_pushDebugString(height, va("%s/%s bytes", sizeu1(needle), sizeu2(size)));
	ST_pushDebugString(height, va(
			"%.2f/%.2f MB %5.2f%%",
			needle / (1024.0 * 1024.0),
			size / (1024.0 * 1024.0),
			percent
	));
	ST_pushDebugString(height, va(
			"%.2f KB/s (ETA %.2f minutes)",
			avg * TICRATE / 1024.0,
			(size - needle) / (avg * TICRATE * 60.0)
	));
	ST_pushDebugString(height, va("Demo (%s)", demo.recording ? "recording" : "playback"));
}

static int32_t ST_FixedToPercent(fixed_t value)
{
	return (value >> 14) + (value >> 11) + (value >> 10);
}

static void ST_drawPlayerDebug(void)
{
	fixed_t boostpower = stplyr->boostpower;
	fixed_t speedboost = stplyr->speedboost;
	fixed_t accelboost = stplyr->accelboost;
	fixed_t handleboost = stplyr->handleboost;
	fixed_t totalspeed = (FixedDiv(stplyr->speed, FixedMul(K_GetKartSpeed(stplyr, false, false), K_PlayerBaseFriction(stplyr, ORIG_FRICTION)))*100)>>FRACBITS; // Stolen from speedometer lol
	int32_t flags =  V_SNAPTOLEFT|V_6WIDTHSPACE|V_ALLOWLOWERCASE;

	V_DrawThinString(9, 30, flags, va("RRID: %s", GetPrettyRRID(stplyr->public_key, true)));
	V_DrawThinString(9, 40, flags, va("Name: %s", player_names[stplyr-players]));

	V_DrawThinString(9, 60, flags, va("Laps: %d", stplyr->laps));
	V_DrawThinString(9, 70, flags, va("Rings: %d", stplyr->rings));

	V_DrawThinString(9, 90, flags, va("TtlSpdPrc: %d%%", totalspeed));
	V_DrawThinString(65, 90, flags, va("SpdBstPrc: %d%%", ST_FixedToPercent(speedboost+boostpower)));
	V_DrawThinString(9, 100, flags, va("SpdBst: %d (%d%%)", speedboost, ST_FixedToPercent(speedboost)));
	V_DrawThinString(9, 110, flags, va("AclBst: %d (%d%%)", accelboost, ST_FixedToPercent(accelboost)));
	V_DrawThinString(9, 120, flags, va("HdlBst: %d (%d%%)", handleboost, ST_FixedToPercent(handleboost)));
	V_DrawThinString(9, 130, flags, va("NumBst: %d", stplyr->numboosts));
}

static void ST_drawDebugInfo(void)
{
	int32_t height = 192;

	if (!stplyr->mo)
		return;

	if (cht_debug & DBG_BASIC)
	{
		const fixed_t d = AngleFixed(stplyr->mo->angle);
		V_DrawRightAlignedString(320, 168, V_MONOSPACE, va("X: %6d", stplyr->mo->x>>FRACBITS));
		V_DrawRightAlignedString(320, 176, V_MONOSPACE, va("Y: %6d", stplyr->mo->y>>FRACBITS));
		V_DrawRightAlignedString(320, 184, V_MONOSPACE, va("Z: %6d", stplyr->mo->z>>FRACBITS));
		V_DrawRightAlignedString(320, 192, V_MONOSPACE, va("A: %6d", FixedInt(d)));

		height = 152;
	}

	if (cht_debug & DBG_DETAILED)
	{
		//V_DrawRightAlignedString(320, height - 104, V_MONOSPACE, va("SHIELD: %5x", stplyr->powers[pw_shield]));
		V_DrawRightAlignedString(320, height - 96,  V_MONOSPACE, va("SCALE: %5d%%", (stplyr->mo->scale*100)/FRACUNIT));
		//V_DrawRightAlignedString(320, height - 88,  V_MONOSPACE, va("DASH: %3d/%3d", stplyr->dashspeed>>FRACBITS, FixedMul(stplyr->maxdash,stplyr->mo->scale)>>FRACBITS));
		//V_DrawRightAlignedString(320, height - 80,  V_MONOSPACE, va("AIR: %4d, %3d", stplyr->powers[pw_underwater], stplyr->powers[pw_spacetime]));

		// Flags
		//V_DrawRightAlignedString(304-64, height - 72, V_MONOSPACE, "Flags:");
		//V_DrawString(304-60,             height - 72, (stplyr->jumping) ? V_GREENMAP : V_REDMAP, "JM");
		//V_DrawString(304-40,             height - 72, (stplyr->pflags & PF_JUMPED) ? V_GREENMAP : V_REDMAP, "JD");
		//V_DrawString(304-20,             height - 72, (stplyr->pflags & PF_SPINNING) ? V_GREENMAP : V_REDMAP, "SP");
		//V_DrawString(304,                height - 72, (stplyr->pflags & PF_STARTDASH) ? V_GREENMAP : V_REDMAP, "ST");

		V_DrawRightAlignedString(320, height - 64, V_MONOSPACE, va("CEILZ: %6d", stplyr->mo->ceilingz>>FRACBITS));
		V_DrawRightAlignedString(320, height - 56, V_MONOSPACE, va("FLOORZ: %6d", stplyr->mo->floorz>>FRACBITS));

		V_DrawRightAlignedString(320, height - 48, V_MONOSPACE, va("CNVX: %6d", stplyr->cmomx>>FRACBITS));
		V_DrawRightAlignedString(320, height - 40, V_MONOSPACE, va("CNVY: %6d", stplyr->cmomy>>FRACBITS));
		V_DrawRightAlignedString(320, height - 32, V_MONOSPACE, va("PLTZ: %6d", stplyr->mo->pmomz>>FRACBITS));

		V_DrawRightAlignedString(320, height - 24, V_MONOSPACE, va("MOMX: %6d", stplyr->rmomx>>FRACBITS));
		V_DrawRightAlignedString(320, height - 16, V_MONOSPACE, va("MOMY: %6d", stplyr->rmomy>>FRACBITS));
		V_DrawRightAlignedString(320, height - 8,  V_MONOSPACE, va("MOMZ: %6d", stplyr->mo->momz>>FRACBITS));
		V_DrawRightAlignedString(320, height,      V_MONOSPACE, va("SPEED: %6d", stplyr->speed>>FRACBITS));

		height -= 120;
	}

	if (cht_debug & DBG_PLAYER)
	{
		ST_drawPlayerDebug();
	}

	// TODO: this only accounts for the undefined class,
	// which should be phased out as much as possible anyway.
	// Figure out some other way to display all of the RNG classes.
	// yama: Maybe a cvar will do?
	int32_t rngdebug = cv_rngpeek.value;

	// The actual seed we're debugging.
	pr_class_t debug_class = (pr_class_t)(rngdebug);

	if (rngdebug >= 0) // randomizer testing
	{
		fixed_t peekres = P_RandomPeek(debug_class);

		V_DrawRightAlignedString(320, height - 24, V_MONOSPACE|V_ALLOWLOWERCASE, va("Class: %s", cv_rngpeek.string));
		V_DrawRightAlignedString(320, height - 16, V_MONOSPACE|V_ALLOWLOWERCASE, va("Init : %08X", P_GetInitSeed(debug_class)));
		V_DrawRightAlignedString(320, height - 8,  V_MONOSPACE|V_ALLOWLOWERCASE, va("Seed : %08X", P_GetRandSeed(debug_class)));
		V_DrawRightAlignedString(320, height,      V_MONOSPACE, va("==   : %08x", peekres));

		height -= 40;
	}

	if (cht_debug & DBG_IMU)
	{
		ST_drawImuDebug(&height);
	}

	if (cht_debug & DBG_MUSIC)
	{
		ST_drawMusicDebug(&height);
	}

	if (cht_debug & DBG_RENDER)
	{
		ST_drawRenderDebug(&height);
	}

	if (cht_debug & DBG_DEMO)
	{
		ST_drawDemoDebug(&height);
	}

	if (cht_debug & DBG_MEMORY)
		V_DrawRightAlignedString(320, height,     V_MONOSPACE, va("Heap used: %7sKB", sizeu1(Z_TagsUsage(0, NUM_PURGETAGS-1)>>10)));
}

tic_t lt_ticker = 0, lt_lasttic = 0;
tic_t lt_exitticker = 0, lt_endtime = 0;


//
// Load the graphics for the title card.
// Don't let LJ see this
//
static void ST_cacheLevelTitle(void)
{
	//uint8_t i;
	//char buf[9];

}

//
// Start the title card.
//
void ST_startTitleCard(void)
{
	// cache every HUD patch used
	ST_cacheLevelTitle(); // Nothing

	// initialize HUD variables
	lt_ticker = lt_exitticker = lt_lasttic = 0;
	lt_endtime = 4*TICRATE;	// + (10*NEWTICRATERATIO);
}

//
// What happens before drawing the title card.
// Which is just setting the HUD translucency.
//
void ST_preDrawTitleCard(void)
{
	if (!G_IsTitleCardAvailable())
		return;

	if (lt_ticker >= (lt_endtime + TICRATE))
		return;

	// Kart: nothing
}

//
// Run the title card.
// Called from ST_Ticker.
//
void ST_runTitleCard(void)
{
	dboolean run = !(paused || P_AutoPause());
	//dboolean gp = (marathonmode || (grandprixinfo.gp && grandprixinfo.roundnum));

	if (!G_IsTitleCardAvailable())
		return;

	if (lt_ticker >= (lt_endtime + TICRATE))
		return;

	if (run || (lt_ticker < PRELEVELTIME))
	{
		// tick
		lt_ticker++;

		// used for hud slidein
		if (lt_ticker >= lt_endtime)
			lt_exitticker++;
	}
}

//
// Draw the title card itself.
//

void ST_drawTitleCard(void)
{
	char *lvlttl = mapheaderinfo[gamemap-1]->lvlttl;
	char *subttl = mapheaderinfo[gamemap-1]->subttl;
	char *zonttl = mapheaderinfo[gamemap-1]->zonttl; // SRB2kart
	char *actnum = mapheaderinfo[gamemap-1]->actnum;
	int32_t lvlttlxpos;
	int32_t ttlnumxpos;
	int32_t zonexpos;
	int32_t dupcalc = (vid.scaledwidth);
	uint8_t gtc = G_GetGametypeColor(gametype);
	int32_t sub = 0;
	int32_t bary = (r_splitscreen)
		? BASEVIDHEIGHT/2
		: 163;
	int32_t lvlw;

	if (!cv_stagetitle.value)
		return;

	if (!LUA_HudEnabled(hud_stagetitle))
		goto luahook;

	if (timeinmap > 113 || timeinmap < 6)
		goto luahook;

	lvlw = V_LevelNameWidth(lvlttl);

	if (actnum[0])
		lvlttlxpos = ((BASEVIDWIDTH/2) - (lvlw/2)) - V_LevelNameWidth(actnum);
	else
		lvlttlxpos = ((BASEVIDWIDTH/2) - (lvlw/2));

	zonexpos = ttlnumxpos = lvlttlxpos + lvlw;
	if (!(mapheaderinfo[gamemap-1]->levelflags & LF_NOZONE))
	{
		if (zonttl[0])
			zonexpos -= V_LevelNameWidth(zonttl); // SRB2kart
		else
			zonexpos -= V_LevelNameWidth(M_GetText("Zone"));
	}

	if (lvlttlxpos < 0)
		lvlttlxpos = 0;

	if (timeinmap > 105)
	{
		int32_t count = (113 - (int32_t)(timeinmap));

		// uh... "fill in the bits" of sub, or something... no idea what I came up with
		// VERY janky, but doesn't require m_easing
		int32_t frac = (FixedMul(R_GetTimeFrac(RTF_LEVEL), FixedDiv(dupcalc, BASEVIDWIDTH)) >> (count + 4))/13; // these two magic numbers seem to do the trick

		sub = dupcalc;
		while (count-- > 0)
			sub >>= 1;
		sub = -sub  - frac;
	}

	{
		dupcalc = (dupcalc - BASEVIDWIDTH)>>1;
		V_DrawFill(sub - dupcalc, bary+9, ttlnumxpos+dupcalc + 1, 2, 31|V_SNAPTOBOTTOM);
		V_DrawDiag(sub + ttlnumxpos + 1, bary, 11, 31|V_SNAPTOBOTTOM);
		V_DrawFill(sub - dupcalc, bary, ttlnumxpos+dupcalc, 10, gtc|V_SNAPTOBOTTOM);
		V_DrawDiag(sub + ttlnumxpos, bary, 10, gtc|V_SNAPTOBOTTOM);

		if (subttl[0])
			V_DrawRightAlignedString(sub + zonexpos - 8, bary+1, V_ALLOWLOWERCASE|V_SNAPTOBOTTOM, subttl);
	}

	ttlnumxpos += sub;
	lvlttlxpos += sub;
	zonexpos += sub;

	V_DrawLevelTitle(lvlttlxpos, bary-18, V_SNAPTOBOTTOM, lvlttl);

	if (strlen(zonttl) > 0)
		V_DrawLevelTitle(zonexpos, bary+6, V_SNAPTOBOTTOM, zonttl);
	else if (!(mapheaderinfo[gamemap-1]->levelflags & LF_NOZONE))
		V_DrawLevelTitle(zonexpos, bary+6, V_SNAPTOBOTTOM, M_GetText("Zone"));

	if (actnum[0])
		V_DrawLevelTitle(ttlnumxpos+12, bary+6, V_SNAPTOBOTTOM, actnum);

	// TODO: a bit more elaborate?
	if (roundqueue.position > 0 && roundqueue.position <= roundqueue.size)
		V_DrawCenteredString(BASEVIDWIDTH/2 + sub, bary+24, V_SNAPTOBOTTOM|V_YELLOWMAP, va("ROUND %d", roundqueue.roundnum));

luahook:
	if (renderisnewtic)
	{
		LUA_HUD_ClearDrawList(luahuddrawlist_titlecard);
		LUA_HookHUD(luahuddrawlist_titlecard, HUD_HOOK(titlecard));
	}
	LUA_HUD_DrawList(luahuddrawlist_titlecard);
}


//
// Drawer for G_PreLevelTitleCard.
//
void ST_preLevelTitleCardDrawer(void)
{
	V_DrawFill(0, 0, BASEVIDWIDTH, BASEVIDHEIGHT, levelfadecol);

	ST_drawTitleCard();
	I_OsPolling();
	I_UpdateNoBlit();
}

// returns the actual button name for any gamecontrol
// if unbound is set, it returns "Unbound" as a string should there be no button bound to a gamecontrol
// if gamectrl is set, it adds an extra "-" for cases where theres also a hardcoded button like accelerate
static const char *ST_GetButtonName(int32_t control, const char *inputtext, dboolean unbound, dboolean gamectrl)
{
	static char buttname[32] = "";
	const char *butt1 = (gamecontrol[0][control][0] != 0 ? G_KeynumToString(gamecontrol[0][control][0]) : NULL);
	const char *butt2 = (gamecontrol[0][control][1] != 0 ? G_KeynumToString(gamecontrol[0][control][1]) : NULL); // alternative bind

	if (butt1 == NULL && butt2 == NULL) // not bound to a button
		snprintf(buttname, 32, (unbound ? "%s - %s" : (gamectrl ? "-%s - %s" : "%s - %s")), (unbound ? "Unbound" : ""), inputtext);
	else if (butt1 != NULL && butt2 != NULL) // bound to two buttons
		snprintf(buttname, 32, "%s/%s - %s", butt1, butt2, inputtext);
	else // bound to only one
		snprintf(buttname, 32, (gamectrl ? "-%s - %s" : "%s - %s"), (butt1 != NULL ? butt1 : butt2 != NULL ? butt2 : ""), inputtext);

	return buttname;
}

//
// Draw the status bar overlay, customisable: the user chooses which
// kind of information to overlay
//
static void ST_overlayDrawer(void)
{
	const uint8_t viewnum = R_GetViewNumber();

	// hu_showscores = auto hide score/time/rings when tab rankings are shown
	if (!(hu_showscores && (netgame || multiplayer)))
	{
		K_drawKartHUD();

		if (renderisnewtic)
		{
			LUA_HUD_ClearDrawList(luahuddrawlist_game[stplyrnum]);
			LUA_HookHUD(luahuddrawlist_game[stplyrnum], HUD_HOOK(game));
		}
		LUA_HUD_DrawList(luahuddrawlist_game[stplyrnum]);
	}

	if (!hu_showscores) // hide the following if TAB is held
	{
		// TODO: splitscreen support!
		if (!splitscreen && !P_IsLocalPlayer(stplyr) && K_DirectorIsAvailable())
		{
			char directortext[20] = {0};

			snprintf(directortext, 20, "Director: %s", cv_director.value ? "On" : "Off");

			if ((!demo.playback && directortoggletimer < 13*TICRATE) || (demo.playback && directortoggletimer < 4*TICRATE))
			{
				if (renderisnewtic)
					directortoggletimer++;

				if (directortoggletimer < 4*TICRATE)
					V_DrawString(1, BASEVIDHEIGHT-8-1, V_SNAPTOLEFT|V_SNAPTOBOTTOM|V_HUDTRANSHALF|V_ALLOWLOWERCASE, directortext);
				else if (cv_translucenthud.value != 0)
					V_DrawString(1, BASEVIDHEIGHT-8-1, V_SNAPTOLEFT|V_SNAPTOBOTTOM|V_80TRANS|V_ALLOWLOWERCASE, directortext); // idk if V_80TRANS is good?
			}
		}
		else
		{
			directortoggletimer = 0;
		}

		if (cv_showviewpointtext.value)
		{
			if (!demo.title && !P_IsLocalPlayer(stplyr) && !camera[viewnum].freecam)
			{
				if (!r_splitscreen)
				{
					V_DrawCenteredString((BASEVIDWIDTH/2), BASEVIDHEIGHT-40, V_HUDTRANSHALF|V_SNAPTOBOTTOM, M_GetText("VIEWPOINT:"));
					V_DrawCenteredString((BASEVIDWIDTH/2), BASEVIDHEIGHT-32, V_HUDTRANSHALF|V_ALLOWLOWERCASE|V_SNAPTOBOTTOM, player_names[stplyr-players]);
				}
				else if (r_splitscreen == 1)
				{
					char name[MAXPLAYERNAME+12];

					int32_t y = (stplyrnum == 0) ? 4 : BASEVIDHEIGHT/2-12;
					snprintf(name, sizeof(name), "VIEWPOINT: %s", player_names[stplyr-players]);
					V_DrawRightAlignedThinString(BASEVIDWIDTH-40, y, V_HUDTRANSHALF|V_ALLOWLOWERCASE|V_SNAPTOTOP|V_SNAPTOBOTTOM|V_SNAPTORIGHT|V_SPLITSCREEN, name);
				}
				else if (r_splitscreen)
				{
					V_DrawCenteredThinString((vid.scaledwidth)/4, BASEVIDHEIGHT/2 - 12, V_HUDTRANSHALF|V_ALLOWLOWERCASE|V_SNAPTOBOTTOM|V_SNAPTOLEFT|V_SPLITSCREEN, player_names[stplyr-players]);
				}
			}
		}
	}

	if (!hu_showscores && netgame && !mapreset)
	{
		if (stplyr->spectator && LUA_HudEnabled(hud_textspectator))
		{
			const char *itemtxt = M_GetText("Item - Join Game");

			if (stplyr->flashing)
				itemtxt = M_GetText("Item - . . .");
			else if (stplyr->pflags & PF_WANTSTOJOIN)
				itemtxt = M_GetText("Item - Cancel Join");
			else if (G_GametypeHasTeams())
				itemtxt = M_GetText("Item - Join Team");

			if (cv_ingamecap.value)
			{
				uint8_t numingame = 0;
				uint8_t i;

				for (i = 0; i < MAXPLAYERS; i++)
					if (playeringame[i] && !players[i].spectator)
						numingame++;

				itemtxt = va("%s (%s: %d)", itemtxt, M_GetText("Slots left"), max(0, cv_ingamecap.value - numingame));
			}

			// SRB2kart: changed positions & text
			if (r_splitscreen)
			{
				V_DrawThinString(2, (BASEVIDHEIGHT/2)-20, V_YELLOWMAP|V_HUDTRANSHALF|V_SPLITSCREEN|V_SNAPTOLEFT|V_SNAPTOBOTTOM, M_GetText("- SPECTATING -"));
				V_DrawThinString(2, (BASEVIDHEIGHT/2)-10, V_HUDTRANSHALF|V_SPLITSCREEN|V_SNAPTOLEFT|V_SNAPTOBOTTOM, itemtxt);
			}
			else
			{
				V_DrawString(2, BASEVIDHEIGHT-50, V_SNAPTOBOTTOM|V_SNAPTOLEFT|V_HUDTRANSHALF|V_YELLOWMAP, M_GetText("- SPECTATING -"));
				V_DrawString(2, BASEVIDHEIGHT-40, V_SNAPTOBOTTOM|V_SNAPTOLEFT|V_HUDTRANSHALF, itemtxt);
				V_DrawString(2, BASEVIDHEIGHT-30, V_SNAPTOBOTTOM|V_SNAPTOLEFT|V_HUDTRANSHALF, va("Accelerate %s", ST_GetButtonName(gc_accelerate, "Float", false, true)));
				V_DrawString(2, BASEVIDHEIGHT-20, V_SNAPTOBOTTOM|V_SNAPTOLEFT|V_HUDTRANSHALF, va("Brake %s", ST_GetButtonName(gc_brake, "Sink", false, true)));
				V_DrawString(2, BASEVIDHEIGHT-10, V_SNAPTOBOTTOM|V_SNAPTOLEFT|V_HUDTRANSHALF, ST_GetButtonName(gc_director, "Toggle Director", true, false));
			}
		}
	}
}

// MayonakaStatic: draw Midnight Channel's TV-like borders
static void ST_MayonakaStatic(void)
{
	int32_t flag;
	if (cv_lessflicker.value)
		flag = V_70TRANS;
	else
		flag = (leveltime%2) ? V_90TRANS : V_70TRANS;

	V_DrawFixedPatch(0, 0, FRACUNIT, V_SNAPTOTOP|V_SNAPTOLEFT|flag, hud_tv1, NULL);
	V_DrawFixedPatch(320<<FRACBITS, 0, FRACUNIT, V_SNAPTOTOP|V_SNAPTORIGHT|V_FLIP|flag, hud_tv1, NULL);
	V_DrawFixedPatch(0, 142<<FRACBITS, FRACUNIT, V_SNAPTOBOTTOM|V_SNAPTOLEFT|flag, hud_tv2, NULL);
	V_DrawFixedPatch(320<<FRACBITS, 142<<FRACBITS, FRACUNIT, V_SNAPTOBOTTOM|V_SNAPTORIGHT|V_FLIP|flag, hud_tv2, NULL);
}

#ifdef HAVE_DISCORDRPC
void ST_AskToJoinEnvelope(void)
{
	const tic_t freq = TICRATE/2;

	if (menustack[0])
		return;

	if ((leveltime % freq) < freq/2)
		return;

	V_DrawFixedPatch(296*FRACUNIT, 2*FRACUNIT, FRACUNIT, V_SNAPTOTOP|V_SNAPTORIGHT, envelope, NULL);
	// maybe draw number of requests with V_DrawPingNum ?
}
#endif

static fixed_t ST_CalculateFadeIn(player_t *player)
{
	(void)player;

	const tic_t length = TICRATE/4;

	tic_t timer = lt_exitticker;

	if (bossinfo.boss == true || G_IsTitleCardAvailable() == false)
	{
		if (timeinmap <= 16)
			timer = 0;
		else
			timer = timeinmap-16;
	}

	if (timer < length)
	{
		fixed_t f = timer * FRACUNIT;

		// Not interpolated at very beginning
		if (timer > 0)
		{
			f += R_GetTimeFrac(RTF_LEVEL);
		}

		return f / length;
	}

	// Not interpolated at very end
	return FRACUNIT;
}

static int32_t ST_ServerSplash_OpacityFlag(int32_t opacity)
{
	if (opacity >= NUMTRANSMAPS)
	{
		return 0;
	}

	opacity = max(opacity, 1);
	return (NUMTRANSMAPS - opacity) << V_ALPHASHIFT;
}

#define SPLASH_LEN ((FRACUNIT * TICRATE) * 3)
#define SPLASH_WAIT ((FRACUNIT * TICRATE) / 2)

void ST_DrawServerSplash(dboolean timelimited)
{
	static fixed_t splashTime = -SPLASH_WAIT;
	static char prevContext[8] = {0};

	if (memcmp(prevContext, server_context, 8) != 0)
	{
		// Context changed, we want to draw it again
		splashTime = -SPLASH_WAIT;
		memcpy(prevContext, server_context, 8);
	}

	if (timelimited && lt_ticker < lt_endtime)
	{
		// Level title is running rn
		return;
	}

	if (timelimited
		&& splashTime >= SPLASH_LEN)
	{
		// We finished drawing it
		return;
	}

	splashTime += renderdeltatics;
	if (splashTime <= 0)
	{
		// We're waiting a tiny bit to draw it
		return;
	}

	const int32_t splashTic = splashTime >> FRACBITS;
	int32_t opacity = NUMTRANSMAPS;

	if (timelimited)
	{
		if (splashTic < NUMTRANSMAPS)
		{
			opacity = splashTic;
		}
		else if (timelimited
			&& splashTic > (SPLASH_LEN >> FRACBITS) - NUMTRANSMAPS)
		{
			opacity = (SPLASH_LEN >> FRACBITS) - splashTic;
		}
	}

	int32_t opacityFlag = ST_ServerSplash_OpacityFlag(opacity);

	patch_t *gridPatch = W_CachePatchName("MOTDBG", PU_CACHE);

	if (gridPatch && gridPatch->width)
	{
		fixed_t gridX = -(splashTime / 3) % (gridPatch->width * FRACUNIT);
		fixed_t gridY = (gridPatch->height) * FRACUNIT;
		int32_t gridOpacity = ST_ServerSplash_OpacityFlag(opacity / 2);
		fixed_t maxX = (vid.width * FRACUNIT) / vid.dup;

		while (gridX < maxX)
		{
			V_DrawFixedPatch(
				gridX, gridY,
				FRACUNIT,
				(V_SNAPTOLEFT|V_SNAPTOTOP) | V_SUBTRACT | V_VFLIP | gridOpacity,
				gridPatch,
				NULL
			);

			gridX += (gridPatch->width * FRACUNIT);
		}
	}

	// We're a bit crunched atm to do this but hopefully in the future
	// the icon can be made a bmp file on the hard drive that the server
	// sends on client join instead.
	patch_t *iconPatch = W_CachePatchName("MOTDICON", PU_CACHE);
	fixed_t iconX = (BASEVIDWIDTH - 16 - iconPatch->width) * FRACUNIT;
	fixed_t iconY = (8) * FRACUNIT;
	V_DrawFixedPatch(
		iconX, iconY,
		FRACUNIT,
		(V_SNAPTORIGHT|V_SNAPTOTOP) | opacityFlag,
		iconPatch,
		NULL
	);

	fixed_t textX = (BASEVIDWIDTH - 16 - 36);
	fixed_t textY = (24 - 8);

	V_DrawRightAlignedString(
		textX, textY,
		(V_SNAPTORIGHT|V_SNAPTOTOP|V_ALLOWLOWERCASE) | opacityFlag,
		connectedservername
	);

	textY += 10;

	if (connectedservercontact[0] != 0)
	{
		V_DrawRightAlignedThinString(
			textX, textY,
			(V_SNAPTORIGHT|V_SNAPTOTOP|V_ALLOWLOWERCASE) | opacityFlag,
			va("Contact: %c%s", '\x80' + cv_shoutcolor.value, connectedservercontact)
		);
	}

	textX -= 260;
	textY += 10;

	if (connectedservermotd[0] != 0)
	{
		V_DrawSmallString(
			textX, textY,
			(V_SNAPTORIGHT|V_SNAPTOTOP|V_ALLOWLOWERCASE) | opacityFlag,
			va("%s", connectedservermotd)
		);
	}
}

void ST_Drawer(void)
{
	dboolean stagetitle = false; // Decide whether to draw the stage title or not

	// Doom's status bar only updated if necessary.
	// However, ours updates every frame regardless, so the "refresh" param was removed
	//(void)refresh;

	// force a set of the palette by using doPaletteStuff()
	if (vid.recalc)
		st_palette = -1;

	fixed_t localfadein[MAXSPLITSCREENPLAYERS];

	// HUD fading for anything not tied to a single player,
	// i.e. the minimap. Since individual splitscreen
	// players' HUDs may fade away before other's, use the
	// the last one remaining.
	{
		fixed_t maxFade = 0;
		uint8_t i;

		for (i = 0; i <= r_splitscreen; i++)
		{
			localfadein[i] = ST_CalculateFadeIn(&players[displayplayers[i]]);

			if (localfadein[i] > maxFade)
			{
				maxFade = localfadein[i];
			}
		}

		if (maxFade == FRACUNIT)
			st_translucency = cv_translucenthud.value;
		else
			st_translucency = FixedMul(cv_translucenthud.value, maxFade);

		st_translucency = min(max((st_translucency), 0), 10);
	}

	// Check for a valid level title
	// If the HUD is enabled
	// And, if Lua is running, if the HUD library has the stage title enabled
	if ((stagetitle = (G_IsTitleCardAvailable() && *mapheaderinfo[gamemap-1]->lvlttl != '\0' && !(hu_showscores && (netgame || multiplayer)))))
		ST_preDrawTitleCard();

	if (st_overlay)
	{
		uint8_t i;
		// No deadview!
		for (i = 0; i <= r_splitscreen; i++)
		{
			stplyr = &players[displayplayers[i]];
			st_fadein = localfadein[i];
			stplyrnum = i;
			R_SetViewContext(VIEWCONTEXT_PLAYER1 + i);
			R_InterpolateView(R_GetTimeFrac(RTF_CAMERA)); // to assist with object tracking
			ST_overlayDrawer();
		}

		// draw Midnight Channel's overlay ontop
		if (mapheaderinfo[gamemap-1]->typeoflevel & TOL_TV)	// Very specific Midnight Channel stuff.
			ST_MayonakaStatic();
	}

	// Draw a white fade on level opening
	if (timeinmap < 15)
	{
		if (timeinmap <= 5)
			V_DrawFill(0,0,BASEVIDWIDTH,BASEVIDHEIGHT,FADECOLOR); // Pure white on first few frames, to hide SRB2's awful level load artifacts
		else
			V_DrawFadeScreen(FADECOLOR, 15-timeinmap); // Then gradually fade out from there
	}

	if (stagetitle)
		ST_drawTitleCard();

	// Replay manual-save stuff
	if (demo.recording && multiplayer && demo.savebutton && demo.savebutton + 3*TICRATE < leveltime)
	{
		int32_t gtc = HU_GetHighlightColor();
		switch (demo.savemode)
		{
		case DSM_NOTSAVING:
		{
			char replaytext[60] = {0};
			int32_t flags = 0;
			const char *item1 = gamecontrol[0][gc_lookback][0] != 0 ? G_KeynumToString(gamecontrol[0][gc_lookback][0]) : NULL;
			const char *item2 = gamecontrol[0][gc_lookback][1] != 0 ? G_KeynumToString(gamecontrol[0][gc_lookback][1]) : NULL;
			const char *item3 = gamecontrol[0][gc_lookback][2] != 0 ? G_KeynumToString(gamecontrol[0][gc_lookback][2]) : NULL;
			const char *item4 = gamecontrol[0][gc_lookback][3] != 0 ? G_KeynumToString(gamecontrol[0][gc_lookback][3]) : NULL;

			if (item1 && item2 && item3 && item4)
			{
				snprintf(replaytext, 60, "%s/%s/%s/%s: Save Replay", item1,item2,item3,item4);
				flags = V_6WIDTHSPACE;
			}
			else if (item1 && item2 && item3)
			{
				snprintf(replaytext, 60, "%s/%s/%s: Save Replay", item1,item2,item3);
				flags = V_6WIDTHSPACE;
			}
			else if (item1 && item2)
			{
				snprintf(replaytext, 60, "%s/%s: Save Replay", item1,item2);
				flags = V_6WIDTHSPACE;
			}
			else
			{
				snprintf(replaytext, 60, "%s: Save Replay", item1 ? item1 : "Look Backwards");
			}

			V_DrawRightAlignedThinString(BASEVIDWIDTH - 2, 2, V_SNAPTOTOP|V_SNAPTORIGHT|V_ALLOWLOWERCASE|gtc|flags, replaytext);
			break;
		}

		case DSM_WILLAUTOSAVE:
		{
			char replaytext[100] = {0};
			int32_t flags = 0;
			const char *button = M_ButtonName(0, gc_lookback, "Look Backwards", false);

			snprintf(replaytext, 100, "Replay will be saved. (%s: Change title)", button);

			V_DrawRightAlignedThinString(BASEVIDWIDTH - 2, 2, V_HUDTRANS|V_SNAPTOTOP|V_SNAPTORIGHT|V_ALLOWLOWERCASE|gtc|flags, replaytext);
			break;
		}

		case DSM_WILLSAVE:
			V_DrawRightAlignedThinString(BASEVIDWIDTH - 2, 2, V_HUDTRANS|V_SNAPTOTOP|V_SNAPTORIGHT|V_ALLOWLOWERCASE|gtc, "Replay will be saved.");
			break;

		default: // Don't render anything
			break;
		}
	}

	ST_drawDebugInfo();
}
