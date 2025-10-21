// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2024 by Kart Krew.
// Copyright (C) 2020 by Sonic Team Junior.
// Copyright (C) 2000 by DooM Legacy Team.
// Copyright (C) 1996 by id Software, Inc.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  r_draw.cpp
/// \brief span / column drawer functions, for 8bpp and 16bpp
///        All drawing to the view buffer is accomplished in this file.
///        The other refresh files only know about ccordinates,
///        not the architecture of the frame buffer.
///        The frame buffer is a linear one, and we need only the base address.

#include <algorithm>

#include "doomdef.h"
#include "doomstat.h"
#include "r_local.h"
#include "st_stuff.h" // need ST_HEIGHT
#include "i_video.h"
#include "v_video.h"
#include "m_misc.h"
#include "w_wad.h"
#include "z_zone.h"
#include "console.h" // Until buffering gets finished
#include "k_color.h" // SRB2kart
#include "i_threads.h"
#include "libdivide.h" // used by NPO2 tilted span functions

#ifdef HWRENDER
#include "hardware/hw_main.h"
#endif

#include <tracy/tracy/Tracy.hpp>

// --------------------------------------------
// assembly or c drawer routines for 8bpp/16bpp
// --------------------------------------------
coldrawfunc_t *colfunc;

coldrawfunc_t *colfuncs[COLDRAWFUNC_MAX];
coldrawfunc_t *colfuncs_bm[COLDRAWFUNC_MAX];

int colfunctype;

spandrawfunc_t *spanfunc;

spandrawfunc_t *spanfuncs[SPANDRAWFUNC_MAX];
spandrawfunc_t *spanfuncs_bm[SPANDRAWFUNC_MAX];
spandrawfunc_t *spanfuncs_npo2[SPANDRAWFUNC_MAX];
spandrawfunc_t *spanfuncs_bm_npo2[SPANDRAWFUNC_MAX];
spandrawfunc_t *spanfuncs_flat[SPANDRAWFUNC_MAX];

drawcolumndata_t g_dc;
drawspandata_t g_ds;


// ==========================================================================
//                     COMMON DATA FOR 8bpp AND 16bpp
// ==========================================================================

/**	\brief view info
*/

INT32 viewwidth, viewheight, viewwindowx, viewwindowy;

UINT8 r8_flatcolor;

// =========================================================================
//                      COLUMN DRAWING CODE STUFF
// =========================================================================

// -----------------------
// translucency stuff here
// -----------------------
#define NUMTRANSTABLES 11 // how many translucency tables are used

UINT8 *transtables; // translucency tables
UINT8 *blendtables[NUMBLENDMAPS];
UINT8 *palremap, *invremap;

/**	\brief R_DrawTransColumn uses this
*/
UINT8 *dc_transmap; // one of the translucency tables

// ----------------------
// translation stuff here
// ----------------------


/**	\brief R_DrawTranslatedColumn uses this
*/
UINT8 *dc_translation;

struct r_lightlist_t *dc_lightlist = NULL;
INT32 dc_numlights = 0, dc_maxlights;

// =========================================================================
//                      SPAN DRAWING CODE STUFF
// =========================================================================

// Vectors for Software's tilted slope drawers
floatv3_t *ds_su, *ds_sv, *ds_sz;
float focallengthf[MAXSPLITSCREENPLAYERS];
float zeroheight;

// =========================================================================
//                   TRANSLATION COLORMAP CODE
// =========================================================================

#define DEFAULT_TT_CACHE_INDEX MAXSKINS
#define BOSS_TT_CACHE_INDEX (MAXSKINS + 1)
#define METALSONIC_TT_CACHE_INDEX (MAXSKINS + 2)
#define ALLWHITE_TT_CACHE_INDEX (MAXSKINS + 3)
#define RAINBOW_TT_CACHE_INDEX (MAXSKINS + 4)
#define BLINK_TT_CACHE_INDEX (MAXSKINS + 5)
#define DASHMODE_TT_CACHE_INDEX (MAXSKINS + 6)
#define HITLAG_TT_CACHE_INDEX (MAXSKINS + 7)
#define INTERMISSION_TT_CACHE_INDEX (MAXSKINS + 8)
#define TT_CACHE_SIZE (MAXSKINS + 9)

static UINT8 **translationtablecache[TT_CACHE_SIZE] = {NULL};
UINT8 skincolor_modified[MAXSKINCOLORS];

static INT32 SkinToCacheIndex(INT32 skinnum)
{
	switch (skinnum)
	{
		case TC_DEFAULT:    return DEFAULT_TT_CACHE_INDEX;
		case TC_BOSS:       return BOSS_TT_CACHE_INDEX;
		case TC_METALSONIC: return METALSONIC_TT_CACHE_INDEX;
		case TC_ALLWHITE:   return ALLWHITE_TT_CACHE_INDEX;
		case TC_RAINBOW:    return RAINBOW_TT_CACHE_INDEX;
		case TC_BLINK:      return BLINK_TT_CACHE_INDEX;
		case TC_DASHMODE:   return DASHMODE_TT_CACHE_INDEX;
		     default:       break;
	}

	return skinnum;
}

static INT32 CacheIndexToSkin(INT32 ttc)
{
	switch (ttc)
	{
		case DEFAULT_TT_CACHE_INDEX:    return TC_DEFAULT;
		case BOSS_TT_CACHE_INDEX:       return TC_BOSS;
		case METALSONIC_TT_CACHE_INDEX: return TC_METALSONIC;
		case ALLWHITE_TT_CACHE_INDEX:   return TC_ALLWHITE;
		case RAINBOW_TT_CACHE_INDEX:    return TC_RAINBOW;
		case BLINK_TT_CACHE_INDEX:      return TC_BLINK;
		case DASHMODE_TT_CACHE_INDEX:   return TC_DASHMODE;
		     default:                   break;
	}

	return ttc;
}

CV_PossibleValue_t Color_cons_t[MAXSKINCOLORS+1];

#define TRANSTAB_AMTMUL10 (255.0f / 10.0f)

struct GenerateBlendTables_State
{
	RGBA_t *masterPalette;
	RGBA_t *gammaCorrectedPalette;
};

static void R_GenerateBlendTables_Core(struct GenerateBlendTables_State *state);
static void R_GenerateTranslucencyTable(UINT8 *table, RGBA_t* sourcepal, int style, UINT8 blendamt);

static void R_AllocateBlendTables(void)
{
	INT32 i;

	for (i = 0; i < NUMBLENDMAPS; i++)
	{
		if (i == blendtab_modulate)
			continue;
		blendtables[i] = static_cast<UINT8 *>(Z_MallocAlign((NUMTRANSTABLES + 1) * 0x10000, PU_STATIC, NULL, 16));
	}

	// Modulation blending only requires a single table
	blendtables[blendtab_modulate] = static_cast<UINT8 *>(Z_MallocAlign(0x10000, PU_STATIC, NULL, 16));
}

#ifdef HAVE_THREADS
static void R_GenerateBlendTables_Thread(void *userdata)
{
	struct GenerateBlendTables_State *state = static_cast<struct GenerateBlendTables_State *>(userdata);

	R_GenerateBlendTables_Core(state);

	free(state->masterPalette);
	free(state->gammaCorrectedPalette);
	free(state);
}
#endif

/** \brief Initializes the translucency tables used by the Software renderer.
*/
void R_InitTranslucencyTables(void)
{
	// Load here the transparency lookup tables 'TINTTAB'
	// NOTE: the TINTTAB resource MUST BE aligned on 64k for the asm
	// optimised code (in other words, transtables pointer low word is 0)
	transtables = static_cast<UINT8 *>(Z_MallocAlign(NUMTRANSTABLES*0x10000, PU_STATIC, NULL, 16));

	W_ReadLump(W_GetNumForName("TRANS10"), transtables);
	W_ReadLump(W_GetNumForName("TRANS20"), transtables+0x10000);
	W_ReadLump(W_GetNumForName("TRANS30"), transtables+0x20000);
	W_ReadLump(W_GetNumForName("TRANS40"), transtables+0x30000);
	W_ReadLump(W_GetNumForName("TRANS50"), transtables+0x40000);
	W_ReadLump(W_GetNumForName("TRANS60"), transtables+0x50000);
	W_ReadLump(W_GetNumForName("TRANS70"), transtables+0x60000);
	W_ReadLump(W_GetNumForName("TRANS80"), transtables+0x70000);
	W_ReadLump(W_GetNumForName("TRANS90"), transtables+0x80000);

	R_AllocateBlendTables();
	R_GenerateBlendTables();
}

void R_GenerateBlendTables(void)
{
#ifdef HAVE_THREADS
	// Allocate copies for the worker thread since the originals can be freed in the main thread.
	struct GenerateBlendTables_State *state = static_cast<struct GenerateBlendTables_State *>(malloc(sizeof *state));
	size_t palsize = 256 * sizeof(RGBA_t);

	state->masterPalette = static_cast<RGBA_t *>(memcpy(malloc(palsize), pMasterPalette, palsize));
	state->gammaCorrectedPalette = static_cast<RGBA_t *>(memcpy(malloc(palsize), pGammaCorrectedPalette, palsize));

	I_spawn_thread("blend-tables",
			R_GenerateBlendTables_Thread, state);
#else
	struct GenerateBlendTables_State state = {pMasterPalette, pGammaCorrectedPalette};

	R_GenerateBlendTables_Core(&state);
#endif
}

static void R_GenerateBlendTables_Core(struct GenerateBlendTables_State *state)
{
	INT32 i;

	for (i = 0; i <= 9; i++)
	{
		const size_t offs = (0x10000 * i);
		const UINT8 alpha = (TRANSTAB_AMTMUL10 * ((float)(10-i)));

		R_GenerateTranslucencyTable(blendtables[blendtab_add] + offs, state->gammaCorrectedPalette, AST_ADD, alpha);
		R_GenerateTranslucencyTable(blendtables[blendtab_subtract] + offs, state->masterPalette, AST_SUBTRACT, alpha); // intentionally uses pMasterPalette
		R_GenerateTranslucencyTable(blendtables[blendtab_reversesubtract] + offs, state->gammaCorrectedPalette, AST_REVERSESUBTRACT, alpha);
	}

	R_GenerateTranslucencyTable(blendtables[blendtab_modulate], state->gammaCorrectedPalette, AST_MODULATE, 0);
}

void R_GenerateTranslucencyTable(UINT8 *table, RGBA_t* sourcepal, int style, UINT8 blendamt)
{
	INT16 bg, fg;
	RGBA_t backrgba, frontrgba, result;

	if (table == NULL)
		I_Error("R_GenerateTranslucencyTable: input table was NULL!");

	for (bg = 0; bg <= 0xFF; bg++)
	{
		backrgba = sourcepal[bg];
		for (fg = 0; fg <= 0xFF; fg++)
		{
			frontrgba = sourcepal[fg];

			result.rgba = ASTBlendPixel(backrgba, frontrgba, style, blendamt);
			table[((fg * 0x100) + bg)] = NearestPaletteColor(result.s.red, result.s.green, result.s.blue, sourcepal);
		}
	}
}

#define ClipTransLevel(trans) std::clamp<INT32>(trans, 0, NUMTRANSMAPS-2)

UINT8 *R_GetTranslucencyTable(INT32 alphalevel)
{
	return transtables + (ClipTransLevel(alphalevel-1) << FF_TRANSSHIFT);
}

UINT8 *R_GetBlendTable(int style, INT32 alphalevel)
{
	size_t offs = (ClipTransLevel(alphalevel) << FF_TRANSSHIFT);

	// Lactozilla: Returns the equivalent to AST_TRANSLUCENT
	// if no alpha style matches any of the blend tables.
	switch (style)
	{
		case AST_ADD:
			return blendtables[blendtab_add] + offs;
		case AST_SUBTRACT:
			return blendtables[blendtab_subtract] + offs;
		case AST_REVERSESUBTRACT:
			return blendtables[blendtab_reversesubtract] + offs;
		case AST_MODULATE:
			return blendtables[blendtab_modulate];
		default:
			break;
	}

	// Return a normal translucency table
	if (--alphalevel < 0)
		return NULL;
	return transtables + (ClipTransLevel(alphalevel) << FF_TRANSSHIFT);
}

void R_InitPaletteRemap(void)
{
	palremap = static_cast<UINT8 *>(Z_Malloc(256, PU_STATIC, NULL));
	invremap = static_cast<UINT8 *>(Z_Malloc(256, PU_STATIC, NULL));
	W_ReadLump(W_GetNumForName("PALREMAP"), palremap);
	W_ReadLump(W_GetNumForName("INVREMAP"), invremap);
}

UINT8 R_GetPaletteRemap(UINT8 color)
{
	return palremap[color];
}

UINT8 R_InvPaletteRemap(UINT8 color)
{
	return invremap[color];
}

/**	\brief	Retrieves a translation colormap from the cache.

	\param	skinnum	number of skin, TC_DEFAULT or TC_BOSS
	\param	color	translation color
	\param	flags	set GTC_CACHE to use the cache

	\return	Colormap. If not cached, caller should Z_Free.
*/
UINT8* R_GetTranslationColormap(INT32 skinnum, skincolornum_t color, UINT8 flags)
{
	UINT8* ret;
	INT32 skintableindex = SkinToCacheIndex(skinnum); // Adjust if we want the default colormap
	INT32 i;

	if (flags & GTC_CACHE)
	{
		// Allocate table for skin if necessary
		if (!translationtablecache[skintableindex])
			translationtablecache[skintableindex] = static_cast<UINT8 **>(Z_Calloc(MAXSKINCOLORS * sizeof(UINT8**), PU_STATIC, NULL));

		// Get colormap
		ret = translationtablecache[skintableindex][color];

		// Rebuild the cache if necessary
		if (skincolor_modified[color])
		{
			for (i = 0; i < (INT32)(sizeof(translationtablecache) / sizeof(translationtablecache[0])); i++)
				if (translationtablecache[i] && translationtablecache[i][color])
					K_GenerateKartColormap(translationtablecache[i][color], CacheIndexToSkin(i), color);
			skincolor_modified[color] = false;
		}
	}
	else ret = NULL;

	// Generate the colormap if necessary
	if (!ret)
	{
		ret = static_cast<UINT8 *>(Z_MallocAlign(NUM_PALETTE_ENTRIES, (flags & GTC_CACHE) ? PU_LEVEL : PU_STATIC, NULL, 8));
		K_GenerateKartColormap(ret, skinnum, color); //R_GenerateTranslationColormap(ret, skinnum, color); // SRB2kart

		// Cache the colormap if desired
		if (flags & GTC_CACHE)
			translationtablecache[skintableindex][color] = ret;
	}

	return ret;
}

/**	\brief	Flushes cache of translation colormaps.

	Flushes cache of translation colormaps, but doesn't actually free the
	colormaps themselves. These are freed when PU_LEVEL blocks are purged,
	at or before which point, this function should be called.

	\return	void
*/
void R_FlushTranslationColormapCache(void)
{
	INT32 i;

	for (i = 0; i < (INT32)(sizeof(translationtablecache) / sizeof(translationtablecache[0])); i++)
		if (translationtablecache[i])
			memset(translationtablecache[i], 0, MAXSKINCOLORS * sizeof(UINT8**));
}

UINT16 R_GetColorByName(const char *name)
{
	UINT16 color = (UINT16)atoi(name);
	if (color > 0 && color < numskincolors)
		return color;
	for (color = 1; color < numskincolors; color++)
		if (!stricmp(skincolors[color].name, name))
			return color;
	return SKINCOLOR_NONE;
}

UINT16 R_GetSuperColorByName(const char *name)
{
	UINT16 i, color = SKINCOLOR_NONE;
	char *realname = static_cast<char *>(Z_Malloc(MAXCOLORNAME+1, PU_STATIC, NULL));
	snprintf(realname, MAXCOLORNAME+1, "Super %s 1", name);
	for (i = 1; i < numskincolors; i++)
		if (!stricmp(skincolors[i].name, realname)) {
			color = i;
			break;
		}
	Z_Free(realname);
	return color;
}

// ==========================================================================
//               COMMON DRAWER FOR 8 AND 16 BIT COLOR MODES
// ==========================================================================

// in a perfect world, all routines would be compatible for either mode,
// and optimised enough
//
// in reality, the few routines that can work for either mode, are
// put here

enum columncontext_e columncontext = COLUMNCONTEXT_DIRECT;

enum ColumnFlushType
{
	FLUSH_NONE				= 0x0000,
	FLUSH_OPAQUE			= 0x0001,
	FLUSH_TRANS				= 0x0002,
	FLUSH_COLORMAP			= 0x0004,
	FLUSH_COLORMAP_TRANS	= 0x0008,
};

typedef struct drawcolumndata_temp_s
{
	INT32    x;
	INT32    yl[8], yh[8];

	// e6y: resolution limitation is removed
	UINT8 *buf;

	INT32    startx;
	ColumnFlushType    type;
	INT32   commontop, commonbot;
	UINT8 *transmap;
	// SoM 7-28-04: Fix the fuzz problem.
	UINT8 *translation;
} drawcolumndata_temp_t;

drawcolumndata_temp_t temp_dc = {};

//
// Error functions that will abort if R_FlushColumns tries to flush
// columns without a column type.
//
FUNCNORETURN static ATTRNORETURN void R_FlushWholeError(void)
{
	I_Error("R_FlushWholeColumns called without being initialized.\n");
}

FUNCNORETURN static ATTRNORETURN void R_FlushHTError(void)
{
	I_Error("R_FlushHTColumns called without being initialized.\n");
}

FUNCNORETURN static ATTRNORETURN void R_QuadFlushError(void)
{
	I_Error("R_FlushQuadColumn called without being initialized.\n");
}

static void (*R_FlushWholeColumns)(void) = R_FlushWholeError;
static void (*R_FlushHTColumns)(void) = R_FlushHTError;
static void (*R_FlushQuadColumn)(void) = R_QuadFlushError;

static void R_FlushColumns(void)
{
	if (temp_dc.x != 8 || temp_dc.commontop >= temp_dc.commonbot)
		R_FlushWholeColumns();
	else
	{
		R_FlushHTColumns();
		R_FlushQuadColumn();
	}

	temp_dc.x = 0;
}

//
// R_ResetColumnBuffer
//
// haleyjd 09/13/04: new function to call from main rendering loop
// which gets rid of the unnecessary reset of various variables during
// column drawing.
//
void R_ResetColumnBuffer(void)
{
	// haleyjd 10/06/05: this must not be done if x == 0!
	if (temp_dc.x)
	{
		R_FlushColumns();
	}

	temp_dc.type = FLUSH_NONE;
	R_FlushWholeColumns = R_FlushWholeError;
	R_FlushHTColumns = R_FlushHTError;
	R_FlushQuadColumn = R_QuadFlushError;
}

/**	\brief	The R_InitViewBuffer function

	Creates lookup tables for getting the framebuffer address
	of a pixel to draw.

	\return	void


*/

void R_InitViewBuffer(void)
{
	viewwindowx = 0;
	viewwindowy = 0;
	INT32 bufsize = (vid.width * 8) * sizeof(*temp_dc.buf);

	if (temp_dc.buf)
	{
#if defined(__SSE__)
		aligned_free(temp_dc.buf);
#else
		Z_Free(temp_dc.buf);
#endif
	}

	memset(&temp_dc, 0, sizeof(temp_dc));

#if defined(__SSE__)
	while (bufsize & 15)
		bufsize++;
	temp_dc.buf = static_cast<UINT8*>(aligned_alloc(16, bufsize));
#else
	temp_dc.buf = static_cast<UINT8*>(Z_Malloc(bufsize, PU_STATIC, NULL));
#endif

	memset(temp_dc.buf, 0, bufsize);
}

/**	\brief	The R_VideoErase function
 *
 *
 *
 *
 *
 *
 * Copy a screen buffer.
 *
 *
 *
 *
 *
 * \param	ofs	offest from buffer
 *
 *
 * \param	count	bytes to erase
 *
 *
 *
 *
 *
 * \return	void
 *
 *
 *
 *
 *
 *
 *
 *
 */


void R_VideoErase(size_t ofs, INT32 count)
{
	// LFB copy.
	// This might not be a good idea if memcpy
	//  is not optimal, e.g. byte by byte on
	//  a 32bit CPU, as GNU GCC/Linux libc did
	//  at one point.
	M_Memcpy(vid.screens[0] + ofs, vid.screens[1] + ofs, count);
}

// ==========================================================================
//                   INCLUDE MAIN DRAWERS CODE HERE
// ==========================================================================

#include "r_draw_column.cpp"
#include "r_draw_span.cpp"
