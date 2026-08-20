// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2021 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  r_textures.c
/// \brief Texture generation.

#include "doomdef.h"
#include "g_game.h"
#include "i_video.h"
#include "r_local.h"
#include "r_sky.h"
#include "p_local.h"
#include "m_misc.h"
#include "r_data.h"
#include "r_textures.h"
#include "r_patch.h"
#include "r_picformats.h"
#include "w_wad.h"
#include "z_zone.h"
#include "p_setup.h" // levelflats
#include "byteptr.h"
#include "dehacked.h"
#include "k_terrain.h"

#ifdef HWRENDER
#include "hardware/hw_glob.h" // HWR_LoadMapTextures
#endif

#include <errno.h>

//
// TEXTURE_T CACHING
// When a texture is first needed, it counts the number of composite columns
//  required in the texture and allocates space for a column directory and
//  any new columns.
// The directory will simply point inside other patches if there is only one
//  patch in a given column, but any columns with multiple patches will have
//  new column_ts generated.
//

int32_t numtextures = 0; // total number of textures found,
// size of following tables

texture_t **textures = NULL;
uint32_t **texturecolumnofs; // column offset lookup table for each texture
uint8_t **texturecache; // graphics data for each generated full-size texture
uint8_t **texturebrightmapcache; // graphics data for brightmap converted for use with a specific texture

int32_t *texturewidth = NULL;
fixed_t *textureheight = NULL; // needed for texture pegging

int32_t *texturetranslation = NULL;
int32_t *texturebrightmaps = NULL;

int32_t g_texturenum_dbgline;

// Painfully simple texture id cacheing to make maps load faster. :3
static struct {
	char name[9];
	uint32_t hash;
	int32_t id;
	uint8_t type;
} *tidcache = NULL;
static int32_t tidcachelen = 0;

//
// MAPTEXTURE_T CACHING
// When a texture is first needed, it counts the number of composite columns
//  required in the texture and allocates space for a column directory and
//  any new columns.
// The directory will simply point inside other patches if there is only one
//  patch in a given column, but any columns with multiple patches will have
//  new column_ts generated.
//

//
// R_DrawColumnInCache
// Clip and draw a column from a patch into a cached post.
//
static inline void R_DrawColumnInCache(column_t *patch, uint8_t *cache, texpatch_t *originPatch, int32_t cacheheight, int32_t patchheight)
{
	int32_t count, position;
	uint8_t *source;
	int32_t topdelta, prevdelta = -1;
	int32_t originy = originPatch->originy;

	(void)patchheight; // This parameter is unused

	while (patch->topdelta != 0xff)
	{
		topdelta = patch->topdelta;
		if (topdelta <= prevdelta)
			topdelta += prevdelta;
		prevdelta = topdelta;
		source = (uint8_t *)patch + 3;
		count = patch->length;
		position = originy + topdelta;

		if (position < 0)
		{
			count += position;
			source -= position; // start further down the column
			position = 0;
		}

		if (position + count > cacheheight)
			count = cacheheight - position;

		if (count > 0)
			memcpy(cache + position, source, count);

		patch = (column_t *)((uint8_t *)patch + patch->length + 4);
	}
}

//
// R_DrawFlippedColumnInCache
// Similar to R_DrawColumnInCache; it draws the column inverted, however.
//
static inline void R_DrawFlippedColumnInCache(column_t *patch, uint8_t *cache, texpatch_t *originPatch, int32_t cacheheight, int32_t patchheight)
{
	int32_t count, position;
	uint8_t *source, *dest;
	int32_t topdelta, prevdelta = -1;
	int32_t originy = originPatch->originy;

	while (patch->topdelta != 0xff)
	{
		topdelta = patch->topdelta;
		if (topdelta <= prevdelta)
			topdelta += prevdelta;
		prevdelta = topdelta;
		topdelta = patchheight-patch->length-topdelta;
		source = (uint8_t *)patch + 2 + patch->length; // patch + 3 + (patch->length-1)
		count = patch->length;
		position = originy + topdelta;

		if (position < 0)
		{
			count += position;
			source += position; // start further UP the column
			position = 0;
		}

		if (position + count > cacheheight)
			count = cacheheight - position;

		dest = cache + position;
		if (count > 0)
		{
			for (; dest < cache + position + count; --source)
				*dest++ = *source;
		}

		patch = (column_t *)((uint8_t *)patch + patch->length + 4);
	}
}

//
// R_DrawBlendColumnInCache
// Draws a translucent column into the cache, applying a half-cooked equation to get a proper translucency value (Needs code in R_GenerateTexture()).
//
static inline void R_DrawBlendColumnInCache(column_t *patch, uint8_t *cache, texpatch_t *originPatch, int32_t cacheheight, int32_t patchheight)
{
	int32_t count, position;
	uint8_t *source, *dest;
	int32_t topdelta, prevdelta = -1;
	int32_t originy = originPatch->originy;

	(void)patchheight; // This parameter is unused

	while (patch->topdelta != 0xff)
	{
		topdelta = patch->topdelta;
		if (topdelta <= prevdelta)
			topdelta += prevdelta;
		prevdelta = topdelta;
		source = (uint8_t *)patch + 3;
		count = patch->length;
		position = originy + topdelta;

		if (position < 0)
		{
			count += position;
			source -= position; // start further down the column
			position = 0;
		}

		if (position + count > cacheheight)
			count = cacheheight - position;

		dest = cache + position;
		if (count > 0)
		{
			for (; dest < cache + position + count; source++, dest++)
				if (*source != 0xFF)
					*dest = ASTBlendPaletteIndexes(*dest, *source, originPatch->style, originPatch->alpha);
		}

		patch = (column_t *)((uint8_t *)patch + patch->length + 4);
	}
}

//
// R_DrawBlendFlippedColumnInCache
// Similar to the one above except that the column is inverted.
//
static inline void R_DrawBlendFlippedColumnInCache(column_t *patch, uint8_t *cache, texpatch_t *originPatch, int32_t cacheheight, int32_t patchheight)
{
	int32_t count, position;
	uint8_t *source, *dest;
	int32_t topdelta, prevdelta = -1;
	int32_t originy = originPatch->originy;

	while (patch->topdelta != 0xff)
	{
		topdelta = patch->topdelta;
		if (topdelta <= prevdelta)
			topdelta += prevdelta;
		prevdelta = topdelta;
		topdelta = patchheight-patch->length-topdelta;
		source = (uint8_t *)patch + 2 + patch->length; // patch + 3 + (patch->length-1)
		count = patch->length;
		position = originy + topdelta;

		if (position < 0)
		{
			count += position;
			source += position; // start further UP the column
			position = 0;
		}

		if (position + count > cacheheight)
			count = cacheheight - position;

		dest = cache + position;
		if (count > 0)
		{
			for (; dest < cache + position + count; --source, dest++)
				if (*source != 0xFF)
					*dest = ASTBlendPaletteIndexes(*dest, *source, originPatch->style, originPatch->alpha);
		}

		patch = (column_t *)((uint8_t *)patch + patch->length + 4);
	}
}

static uint8_t *R_AllocateTextureBlock(size_t blocksize, uint8_t **user)
{
	texturememory += blocksize;

	return Z_Malloc(blocksize, PU_LEVEL, user);
}

static uint8_t *R_AllocateDummyTextureBlock(size_t width, uint8_t **user)
{
	// Allocate dummy data. Keep 4-bytes aligned.
	// Column offsets will be initialized to 0, which points to the 0xff byte (empty column flag).
	size_t blocksize = 4 + (width * 4);
	uint8_t *block = R_AllocateTextureBlock(blocksize, user);

	memset(block, 0, blocksize);
	block[0] = 0xff;

	return block;
}

static dboolean R_CheckTextureLumpLength(texture_t *texture, size_t patch)
{
	uint16_t wadnum = texture->patches[patch].wad;
	uint16_t lumpnum = texture->patches[patch].lump;
	size_t lumplength = W_LumpLengthPwad(wadnum, lumpnum);

	// The header does not exist
	if (lumplength < offsetof(softwarepatch_t, columnofs))
	{
		CONS_Alert(
			CONS_ERROR,
			"%.8s: texture lump data is too small. Expected %s bytes, got %s. (%s)\n",
			texture->name,
			sizeu1(offsetof(softwarepatch_t, columnofs)),
			sizeu2(lumplength),
			wadfiles[wadnum]->lumpinfo[lumpnum].fullname
		);

		return false;
	}

	return true;
}

void R_DoPaletteRemapPatch(softwarepatch_t *patch, size_t size)
{
	if (!Picture_CheckIfDoomPatch(patch, size))
		return;

	// thanks to my very own ingenious patch-compression technology,
	// i now have to make sure i don't accidentally remap a column twice. cool!
	// yes, it assumes all columns are sequential in the patch,
	// but it's efficient so who cares
	uint8_t *last = 0;

	for (int16_t i = 0; i < patch->width; i++)
	{
		uint8_t *p = (uint8_t *)patch + patch->columnofs[i];
		if (p <= last)
			continue;
		else
			last = p;
		while (*p++ != 0xff) // topdelta
		{
			int length = *p++;
			for (length += 2; length--; p++)
				*p = R_GetPaletteRemap(*p);
		}
	}
}

void R_DoPaletteRemapFlat(uint8_t *data, size_t size)
{
	for (size_t i = 0; i < size; i++, data++)
		*data = R_GetPaletteRemap(*data);
}

//
// R_GenerateTexture
//
// Allocate space for full size texture, either single patch or 'composite'
// Build the full textures from patches.
// The texture caching system is a little more hungry of memory, but has
// been simplified for the sake of highcolor (lol), dynamic ligthing, & speed.
//
// This is not optimised, but it's supposed to be executed only once
// per level, when enough memory is available.
//
uint8_t *R_GenerateTexture(size_t texnum)
{
	uint8_t *block;
	uint8_t *blocktex;
	texture_t *texture;
	texpatch_t *patch;
	softwarepatch_t *realpatch;
	uint8_t *pdata;
	int x, x1, x2, i, width, height;
	size_t blocksize;
	column_t *patchcol;
	uint8_t *colofs;

	uint16_t wadnum;
	lumpnum_t lumpnum;
	size_t lumplength;
	dboolean doremap;

	I_Assert(texnum <= (size_t)numtextures);
	texture = textures[texnum];
	I_Assert(texture != NULL);

	if (texture->type == TEXTURETYPE_FLAT)
		goto multipatch;

	// single-patch textures can have holes in them and may be used on
	// 2sided lines so they need to be kept in 'packed' format
	// BUT this is wrong for skies and walls with over 255 pixels,
	// so check if there's holes and if not strip the posts.
	if (texture->patchcount == 1)
	{
		dboolean holey = false;
		patch = texture->patches;

		wadnum = patch->wad;
		lumpnum = patch->lump;
		lumplength = W_LumpLengthPwad(wadnum, lumpnum);

		// The header does not exist
		if (R_CheckTextureLumpLength(texture, 0) == false)
		{
			block = R_AllocateDummyTextureBlock(texture->width, &texturecache[texnum]);
			texturecolumnofs[texnum] = (uint32_t*)&block[4];
			textures[texnum]->holes = true;
			return block;
		}

		doremap = W_NeedPaletteRemapPwad(wadnum, lumpnum, false);
		pdata = W_CacheLumpNumPwad(wadnum, lumpnum, PU_LEVEL);
		realpatch = (softwarepatch_t *)pdata;

#ifndef NO_PNG_LUMPS
		// TODO: Is it worth converting those?
		if (Picture_IsLumpPNG(pdata, lumplength))
			goto multipatch;
#endif

		if (doremap)
			R_DoPaletteRemapPatch(realpatch, lumplength);

		// Check the patch for holes.
		if (texture->width > LSBF_SHORT(realpatch->width) || texture->height > LSBF_SHORT(realpatch->height))
			holey = true;
		colofs = (uint8_t *)realpatch->columnofs;
		for (x = 0; x < texture->width && !holey; x++)
		{
			column_t *col = (column_t *)((uint8_t *)realpatch + LSBF_LONG(*(uint32_t *)&colofs[x<<2]));
			int32_t topdelta, prevdelta = -1, y = 0;
			while (col->topdelta != 0xff)
			{
				topdelta = col->topdelta;
				if (topdelta <= prevdelta)
					topdelta += prevdelta;
				prevdelta = topdelta;
				if (topdelta > y)
					break;
				y = topdelta + col->length + 1;
				col = (column_t *)((uint8_t *)col + col->length + 4);
			}
			if (y < texture->height)
				holey = true; // this texture is HOLEy! D:
		}

		// If the patch uses transparency, we have to save it this way.
		if (holey)
		{
			texture->holes = true;
			texture->flip = patch->flip;
			blocksize = lumplength;
			block = Z_Calloc(blocksize, PU_LEVEL, // will change tag at end of this function
				&texturecache[texnum]);
			memcpy(block, realpatch, blocksize);
			texturememory += blocksize;

			// use the patch's column lookup
			colofs = (block + 8);
			texturecolumnofs[texnum] = (uint32_t *)colofs;
			blocktex = block;
			if (patch->flip & 1) // flip the patch horizontally
			{
				uint8_t *realcolofs = (uint8_t *)realpatch->columnofs;
				for (x = 0; x < texture->width; x++)
					*(uint32_t *)&colofs[x<<2] = realcolofs[( texture->width-1-x )<<2]; // swap with the offset of the other side of the texture
			}
			// we can't as easily flip the patch vertically sadly though,
			//  we have wait until the texture itself is drawn to do that
			for (x = 0; x < texture->width; x++)
				*(uint32_t *)&colofs[x<<2] = LSBF_LONG(LSBF_LONG(*(uint32_t *)&colofs[x<<2]) + 3);
			goto done;
		}

		// Otherwise, do multipatch format.
	}

	// multi-patch textures (or 'composite')
	multipatch:
	texture->holes = false;
	texture->flip = 0;
	blocksize = (texture->width * 4) + (texture->width * texture->height);
	texturememory += blocksize;
	block = Z_Malloc(blocksize+1, PU_LEVEL, &texturecache[texnum]);

	memset(block, TRANSPARENTPIXEL, blocksize+1); // Transparency hack

	// columns lookup table
	colofs = block;
	texturecolumnofs[texnum] = (uint32_t *)colofs;

	// texture data after the lookup table
	blocktex = block + (texture->width*4);

	for (x = 0; x < texture->width; ++x)
	{
		// generate column ofset lookup
		*(uint32_t *)&colofs[x<<2] = LSBF_LONG((x * texture->height) + (texture->width*4));
	}

	// Composite the columns together.
	for (i = 0, patch = texture->patches; i < texture->patchcount; i++, patch++)
	{
		dboolean dealloc = true;
		static void (*ColumnDrawerPointer)(column_t *, uint8_t *, texpatch_t *, int32_t, int32_t); // Column drawing function pointer.
		if (patch->style != AST_COPY)
			ColumnDrawerPointer = (patch->flip & 2) ? R_DrawBlendFlippedColumnInCache : R_DrawBlendColumnInCache;
		else
			ColumnDrawerPointer = (patch->flip & 2) ? R_DrawFlippedColumnInCache : R_DrawColumnInCache;

		wadnum = patch->wad;
		lumpnum = patch->lump;
		doremap = W_NeedPaletteRemapPwad(wadnum, lumpnum, false);
		pdata = W_CacheLumpNumPwad(wadnum, lumpnum, PU_LEVEL);
		lumplength = W_LumpLengthPwad(wadnum, lumpnum);
		realpatch = (softwarepatch_t *)pdata;
		dealloc = true;

#ifndef NO_PNG_LUMPS
		if (Picture_IsLumpPNG((uint8_t *)realpatch, lumplength))
			realpatch = (softwarepatch_t *)Picture_PNGConvert((uint8_t *)realpatch, PICFMT_DOOMPATCH, NULL, NULL, NULL, NULL, lumplength, NULL, 0);
		else
#endif
		if (texture->type == TEXTURETYPE_FLAT)
		{
			if (doremap)
				R_DoPaletteRemapFlat(pdata, lumplength);
			realpatch = (softwarepatch_t *)Picture_Convert(PICFMT_FLAT, pdata, PICFMT_DOOMPATCH, 0, NULL, texture->width, texture->height, 0, 0, 0);
		}
		else
		{
			dealloc = false;
			if (doremap)
				R_DoPaletteRemapPatch(realpatch, lumplength);
		}

		x1 = patch->originx;
		width = LSBF_SHORT(realpatch->width);
		height = LSBF_SHORT(realpatch->height);
		x2 = x1 + width;

		if (x1 > texture->width || x2 < 0)
		{
			if (dealloc)
				Z_Free(realpatch);
			continue; // patch not located within texture's x bounds, ignore
		}

		if (patch->originy > texture->height || (patch->originy + height) < 0)
		{
			if (dealloc)
				Z_Free(realpatch);
			continue; // patch not located within texture's y bounds, ignore
		}

		// patch is actually inside the texture!
		// now check if texture is partly off-screen and adjust accordingly

		// left edge
		if (x1 < 0)
			x = 0;
		else
			x = x1;

		// right edge
		if (x2 > texture->width)
			x2 = texture->width;

		for (; x < x2; x++)
		{
			if (patch->flip & 1)
				patchcol = (column_t *)((uint8_t *)realpatch + LSBF_LONG(realpatch->columnofs[(x1+width-1)-x]));
			else
				patchcol = (column_t *)((uint8_t *)realpatch + LSBF_LONG(realpatch->columnofs[x-x1]));

			ColumnDrawerPointer(patchcol, block + LSBF_LONG(*(uint32_t *)&colofs[x<<2]), patch, texture->height, height);
		}

		if (dealloc)
			Z_Free(realpatch);
	}

done:
	return blocktex;
}

uint8_t *R_GetFlatForTexture(size_t texnum)
{
	if (texnum >= (unsigned)numtextures)
		return NULL;

	texture_t *texture = textures[texnum];
	if (texture->flat != NULL)
		return texture->flat;

	// Special case: Textures that are flats don't need to be converted FROM a texture INTO a flat.
	if (texture->type == TEXTURETYPE_FLAT)
	{
		texpatch_t *patch = &texture->patches[0];
		uint16_t wadnum = patch->wad;
		lumpnum_t lumpnum = patch->lump;
		dboolean doremap = W_NeedPaletteRemapPwad(wadnum, lumpnum, false);
		uint8_t *pdata = W_CacheLumpNumPwad(wadnum, lumpnum, PU_CACHE);
		size_t lumplength = W_LumpLengthPwad(wadnum, lumpnum);

#ifndef NO_PNG_LUMPS
		if (Picture_IsLumpPNG(pdata, lumplength))
			texture->flat = Picture_PNGConvert(pdata, PICFMT_FLAT, NULL, NULL, NULL, NULL, lumplength, NULL, 0);
		else
#endif
		{
			texture->flat = Z_Malloc(lumplength, PU_STATIC, NULL);
			memcpy(texture->flat, pdata, lumplength);
		}

		if (doremap)
			R_DoPaletteRemapFlat(texture->flat, lumplength);

		Z_SetUser(texture->flat, &texture->flat);

		Z_Free(pdata);
	}
	else
		texture->flat = (uint8_t *)Picture_TextureToFlat(texnum);

	flatmemory += texture->width * texture->height;

	return texture->flat;
}

// This function writes a column to p, using the posts from
// tcol, but the pixel values from bcol. If bcol is larger
// than tcol, the pixels are cropped. If bcol is smaller than
// tcol, the empty space is filled with TRANSPARENTPIXEL.
static void R_ConvertBrightmapColumn(uint8_t *p, const column_t *tcol, const column_t *bcol)
{
	/*

	___t1
	|
	|     ___b1
	|     |
	|     |
	|__t2 |
	|     |__b2
	|
	|     ___b3
	|__t3 |
		  |
	___t4 |
	|     |__b4
	|__t5

	___t6
	|__t7

	*/

	// copy post header
	memcpy(&p[-3], tcol, 3);

	int32_t ttop = tcol->topdelta;
	int32_t btop = bcol->topdelta;

	int32_t y = ttop;

	while (tcol->topdelta != 0xff && bcol->topdelta != 0xff)
	{
		int32_t tbot = ttop + tcol->length;
		int32_t bbot = btop + bcol->length;

		int32_t n;

		// t1 to b1
		// b2 to b3
		// --------
		// The brightmap column starts below the
		// texture column, so pad it with black
		// pixels.

		n = max(0, min(btop, tbot) - y);
		memset(&p[y - ttop], TRANSPARENTPIXEL, n);
		y += n;

		// b1 to t2
		// t2 to b2
		// b3 to t3
		// t4 to b4
		// --------
		// Copy parts of the brightmap column which
		// line up with the texture column.

		n = max(0, min(bbot, tbot) - y);
		memcpy(&p[y - ttop], (const uint8_t*)bcol + 3 + (y - btop), n);
		y += n;

		if (y == tbot)
		{
			p += 4 + tcol->length;
			tcol = (const column_t*)((const uint8_t*)tcol + 4 + tcol->length);

			memcpy(&p[-3], tcol, 3); // copy post header

			// Tall patches add the topdelta if it is less
			// than the running topdelta.
			ttop = tcol->topdelta <= ttop ? ttop + tcol->topdelta : tcol->topdelta;
			y = ttop;
		}

		if (y >= bbot)
		{
			bcol = (const column_t*)((const uint8_t*)bcol + 4 + bcol->length);
			btop = bcol->topdelta <= btop ? btop + bcol->topdelta : bcol->topdelta; // tall patches
		}
	}

	if (tcol->topdelta != 0xff)
	{
		// b4 to t5
		// t6 to t7
		// --------
		// The texture column continues past the end
		// of the brightmap column, so pad it with
		// black pixels.

		y -= ttop;
		memset(&p[y], TRANSPARENTPIXEL, tcol->length - y);

		while
		(
			(
				p += 4 + tcol->length,
				tcol = (const column_t*)((const uint8_t*)tcol + 4 + tcol->length)
			)->topdelta != 0xff
		)
		{
			memcpy(&p[-3], tcol, 3); // copy post header
			memset(p, TRANSPARENTPIXEL, tcol->length);
		}
	}

	p[-3] = 0xff;
}

struct rawcheckcolumn_state
{
	softwarepatch_t *patch;
	size_t data_size;
	dboolean error;
	const char *name;
};

static void R_InitRawCheckColumn(
		struct rawcheckcolumn_state *state,
		softwarepatch_t *patch,
		size_t size,
		const char *name
)
{
	state->patch = patch;
	state->data_size = size;
	state->error = (patch == NULL);
	state->name = name;
}

static void R_CheckRawColumn_Error(struct rawcheckcolumn_state *state, const char *error)
{
	if (state->error)
	{
		return;
	}

	CONS_Alert(CONS_WARNING, "%.8s: %s\n", state->name, error);

	state->error = true;
}

static column_t *R_CheckRawColumn(struct rawcheckcolumn_state *state, int32_t x)
{
	static column_t empty = {0xff, 0};

	if (state->error)
	{
		return &empty;
	}

	if (x < LSBF_SHORT(state->patch->width))
	{
		size_t ofs = LSBF_LONG(state->patch->columnofs[x]);

		if (ofs < state->data_size)
		{
			return (column_t*)((uint8_t*)state->patch + ofs);
		}
		else
		{
			R_CheckRawColumn_Error(state, "Patch column offsets go out of bounds."
					" Make sure the lump is in Doom Graphics format and not a Flat or PNG or anything else.");
		}
	}

	return &empty;
}

// Remember, this function must generate a texture that
// matches the layout of texnum. It must have the same width
// and same columns. Only the pixels that overlap are copied
// from the brightmap texture.
uint8_t *R_GenerateTextureBrightmap(size_t texnum)
{
	texture_t *texture = textures[texnum];
	texture_t *bright = textures[R_GetTextureBrightmap(texnum)];

	if (R_TextureHasBrightmap(texnum) && bright->patchcount > 1)
	{
		CONS_Alert(
				CONS_WARNING,
				"%.8s: BRIGHTMAP should not be a composite texture. Only using the first patch.\n",
				bright->name
		);
	}

	if (R_CheckTextureLumpLength(texture, 0) == false)
	{
		return R_AllocateDummyTextureBlock(texture->width, &texturebrightmapcache[texnum]);
	}

	R_CheckTextureCache(texnum);

	softwarepatch_t *bmap = NULL;
	struct rawcheckcolumn_state rchk;

	if (R_TextureHasBrightmap(texnum) && R_CheckTextureLumpLength(bright, 0))
	{
		int32_t wad = bright->patches[0].wad;
		int32_t lump = bright->patches[0].lump;

		bmap = W_CacheLumpNumPwad(wad, lump, PU_STATIC);
		R_InitRawCheckColumn(&rchk, bmap, W_LumpLengthPwad(wad, lump), bright->name);
	}
	else
	{
		R_InitRawCheckColumn(&rchk, NULL, 0, bright->name);
	}

	uint8_t *block;

	if (texture->holes)
	{
		block = R_AllocateTextureBlock(
				W_LumpLengthPwad(texture->patches[0].wad, texture->patches[0].lump),
				&texturebrightmapcache[texnum]
		);

		int32_t x;

		for (x = 0; x < texture->width; ++x)
		{
			const column_t *tcol = (column_t*)(R_GetColumn(texnum, x) - 3);
			const column_t *bcol = R_CheckRawColumn(&rchk, x);

			R_ConvertBrightmapColumn(block + LSBF_LONG(texturecolumnofs[texnum][x]), tcol, bcol);
		}
	}
	else
	{
		// Allocate the same size as composite textures.
		size_t blocksize = (texture->width * 4) + (texture->width * texture->height) + 1;

		block = R_AllocateTextureBlock(blocksize, &texturebrightmapcache[texnum]);
		memset(block, TRANSPARENTPIXEL, blocksize); // Transparency hack

		texpatch_t origin = {0};
		int32_t x;

		for (x = 0; x < texture->width; ++x)
		{
			R_DrawColumnInCache(
					R_CheckRawColumn(&rchk, x),
					block + LSBF_LONG(texturecolumnofs[texnum][x]),
					&origin,
					texture->height,
					LSBF_SHORT(bmap->height)
			);
		}
	}

	Z_Free(bmap);

	return block;
}

//
// R_GetTextureNum
//
// Returns the actual texture id that we should use.
// This can either be texnum, the current frame for texnum's anim (if animated),
// or 0 if not valid.
//
int32_t R_GetTextureNum(int32_t texnum)
{
	if (texnum < 0 || texnum >= numtextures)
		return 0;
	return texturetranslation[texnum];
}

//
// R_GetTextureBrightmap
//
// Returns the actual texture id that we should use.
// This can either be the texture's brightmap,
// or 0 if not valid.
//
int32_t R_GetTextureBrightmap(int32_t texnum)
{
	if (texnum < 0 || texnum >= numtextures)
		return 0;
	return texturebrightmaps[texnum];
}

dboolean R_TextureHasBrightmap(int32_t texnum)
{
	return R_GetTextureBrightmap(texnum) != 0;
}

dboolean R_TextureCanRemap(int32_t texnum)
{
	const terrain_t *t = K_GetTerrainForTextureNum(texnum);
	return !t || t->flags & TRF_REMAP;
}

//
// R_CheckTextureCache
//
// Use this if you need to make sure the texture is cached before R_GetColumn calls
// e.g.: midtextures and FOF walls
//
void R_CheckTextureCache(int32_t tex)
{
	if (!texturecache[tex])
		R_GenerateTexture(tex);
}

static inline int32_t wrap_column(fixed_t tex, int32_t col)
{
	int32_t width = texturewidth[tex];

	if (width & (width - 1))
		col = (uint32_t)col % width;
	else
		col &= (width - 1);

	return col;
}

//
// R_GetColumn
//
uint8_t *R_GetColumn(fixed_t tex, int32_t col)
{
	return texturecache[tex] + LSBF_LONG(texturecolumnofs[tex][wrap_column(tex, col)]);
}

//
// R_GetBrightmapColumn
//
uint8_t *R_GetBrightmapColumn(fixed_t tex, int32_t col)
{
	if (!texturebrightmapcache[tex])
		R_GenerateTextureBrightmap(tex);

	return texturebrightmapcache[tex] + LSBF_LONG(texturecolumnofs[tex][wrap_column(tex, col)]);
}

int32_t R_GetTextureNumForFlat(levelflat_t *levelflat)
{
	return texturetranslation[levelflat->texture_id];
}

void *R_GetFlat(levelflat_t *levelflat)
{
	if (levelflat->type == LEVELFLAT_NONE)
		return NULL;

	return R_GetFlatForTexture(R_GetTextureNumForFlat(levelflat));
}

//
// R_CheckPowersOfTwo
//
// Sets ds_powersoftwo true if the flat's dimensions are powers of two, and returns that.
//
dboolean R_CheckPowersOfTwo(drawspandata_t* ds)
{
	dboolean wpow2 = (!(ds->flatwidth & (ds->flatwidth - 1)));
	dboolean hpow2 = (!(ds->flatheight & (ds->flatheight - 1)));

	// Initially, the flat isn't powers-of-two-sized.
	ds->powersoftwo = false;

	// But if the width and height are powers of two,
	// and are EQUAL, then it's okay :]
	if ((ds->flatwidth == ds->flatheight) && (wpow2 && hpow2))
		ds->powersoftwo = true;

	// Just return ds_powersoftwo.
	return ds->powersoftwo;
}

//
// R_FlatDimensionsFromLumpSize
//
// Returns the flat's square size from its lump length.
//
size_t R_FlatDimensionsFromLumpSize(size_t size)
{
	switch (size)
	{
		case 4194304: // 2048x2048 lump
			return 2048;

		case 1048576: // 1024x1024 lump
			return 1024;

		case 262144:// 512x512 lump
			return 512;

		case 65536: // 256x256 lump
			return 256;

		case 16384: // 128x128 lump
			return 128;

		case 1024: // 32x32 lump
			return 32;

		case 256: // 16x16 lump
			return 16;

		case 64: // 8x8 lump
			return 8;

		default: // 64x64 lump
			return 64;
	}
}

//
// R_CheckFlatLength
//
// Determine the flat's dimensions from its lump length.
//
void R_CheckFlatLength(drawspandata_t* ds, size_t size)
{
	switch (size)
	{
		case 4194304: // 2048x2048 lump
			ds->nflatmask = 0x3FF800;
			ds->nflatxshift = 21;
			ds->nflatyshift = 10;
			ds->nflatshiftup = 5;
			ds->flatwidth = ds->flatheight = 2048;
			break;
		case 1048576: // 1024x1024 lump
			ds->nflatmask = 0xFFC00;
			ds->nflatxshift = 22;
			ds->nflatyshift = 12;
			ds->nflatshiftup = 6;
			ds->flatwidth = ds->flatheight = 1024;
			break;
		case 262144:// 512x512 lump
			ds->nflatmask = 0x3FE00;
			ds->nflatxshift = 23;
			ds->nflatyshift = 14;
			ds->nflatshiftup = 7;
			ds->flatwidth = ds->flatheight = 512;
			break;
		case 65536: // 256x256 lump
			ds->nflatmask = 0xFF00;
			ds->nflatxshift = 24;
			ds->nflatyshift = 16;
			ds->nflatshiftup = 8;
			ds->flatwidth = ds->flatheight = 256;
			break;
		case 16384: // 128x128 lump
			ds->nflatmask = 0x3F80;
			ds->nflatxshift = 25;
			ds->nflatyshift = 18;
			ds->nflatshiftup = 9;
			ds->flatwidth = ds->flatheight = 128;
			break;
		case 1024: // 32x32 lump
			ds->nflatmask = 0x3E0;
			ds->nflatxshift = 27;
			ds->nflatyshift = 22;
			ds->nflatshiftup = 11;
			ds->flatwidth = ds->flatheight = 32;
			break;
		case 256: // 16x16 lump
			ds->nflatmask = 0xF0;
			ds->nflatxshift = 28;
			ds->nflatyshift = 24;
			ds->nflatshiftup = 12;
			ds->flatwidth = ds->flatheight = 16;
			break;
		case 64: // 8x8 lump
			ds->nflatmask = 0x38;
			ds->nflatxshift = 29;
			ds->nflatyshift = 26;
			ds->nflatshiftup = 13;
			ds->flatwidth = ds->flatheight = 8;
			break;
		default: // 64x64 lump
			ds->nflatmask = 0xFC0;
			ds->nflatxshift = 26;
			ds->nflatyshift = 20;
			ds->nflatshiftup = 10;
			ds->flatwidth = ds->flatheight = 64;
			break;
	}
}

//
// Empty the texture cache (used for load wad at runtime)
//
void R_FlushTextureCache(void)
{
	int32_t i;

	if (numtextures)
		for (i = 0; i < numtextures; i++)
		{
			Z_Free(textures[i]->flat);
			Z_Free(texturecache[i]);
		}
}

// Need these prototypes for later; defining them here instead of r_textures.h so they're "private"
int R_CountTexturesInTEXTURESLump(uint16_t wadNum, uint16_t lumpNum);
void R_ParseTEXTURESLump(uint16_t wadNum, uint16_t lumpNum, int32_t *index);

static int32_t
Rloadflats (int32_t i, int32_t w)
{
	uint16_t j;
	uint16_t texstart, texend;
	texture_t *texture;
	texpatch_t *patch;
	uint8_t header[PNG_HEADER_SIZE];

	if (wadfiles[w]->type == RET_PK3)
	{
		texstart = W_CheckNumForFolderStartPK3("flats/", (uint16_t)w, 0);
		texend = W_CheckNumForFolderEndPK3("flats/", (uint16_t)w, texstart);
	}
	else
	{
		texstart = W_CheckNumForMarkerStartPwad("F_START", (uint16_t)w, 0);
		texend = W_CheckNumForNamePwad("F_END", (uint16_t)w, texstart);
	}

	if (!( texstart == INT16_MAX || texend == INT16_MAX ))
	{
		// Work through each lump between the markers in the WAD.
		for (j = 0; j < (texend - texstart); j++)
		{
			uint16_t wadnum = (uint16_t)w;
			lumpnum_t lumpnum = texstart + j;

			if (wadfiles[w]->type == RET_PK3)
			{
				if (W_IsLumpFolder(wadnum, lumpnum)) // Check if lump is a folder
					continue; // If it is then SKIP IT
			}

			size_t lumplength = W_LumpLengthPwad(wadnum, lumpnum);
			size_t flatsize = R_FlatDimensionsFromLumpSize(lumplength);

			//CONS_Printf("\n\"%s\" is a flat, dimensions %d x %d",W_CheckNameForNumPwad((uint16_t)w,texstart+j),flatsize,flatsize);
			texture = textures[i] = Z_Calloc(sizeof(texture_t) + sizeof(texpatch_t), PU_STATIC, NULL);

			// Set texture properties.
			memcpy(texture->name, W_CheckNameForNumPwad(wadnum, lumpnum), sizeof(texture->name));
			texture->hash = FNV1a_QuickCaseHash(texture->name, 8);

#ifndef NO_PNG_LUMPS
			W_ReadLumpHeaderPwad(wadnum, lumpnum, header, sizeof header, 0);

			if (Picture_IsLumpPNG(header, lumplength))
			{
				uint8_t *flatlump = W_CacheLumpNumPwad(wadnum, lumpnum, PU_CACHE);
				int32_t width, height;
				Picture_PNGDimensions((uint8_t *)flatlump, &width, &height, NULL, NULL, lumplength);
				texture->width = (int16_t)width;
				texture->height = (int16_t)height;
				Z_Free(flatlump);
			}
			else
#endif
				texture->width = texture->height = flatsize;

			texture->type = TEXTURETYPE_FLAT;
			texture->patchcount = 1;
			texture->holes = false;
			texture->flip = 0;
			texture->terrainID = K_GetTerrainIDForTextureName(texture->name);

			// Allocate information for the texture's patches.
			patch = &texture->patches[0];

			patch->originx = patch->originy = 0;
			patch->wad = (uint16_t)w;
			patch->lump = texstart + j;
			patch->flip = 0;

			texturewidth[i] = texture->width;
			textureheight[i] = texture->height << FRACBITS;
			i++;
		}
	}

	return i;
}

#define TX_START "TX_START"
#define TX_END "TX_END"

static int32_t
Rloadtextures (int32_t i, int32_t w)
{
	uint16_t j;
	uint16_t texstart, texend, texturesLumpPos;
	texture_t *texture;
	texpatch_t *patch;
	softwarepatch_t patchlump;

	// Get the lump numbers for the markers in the WAD, if they exist.
	if (wadfiles[w]->type == RET_PK3)
	{
		texstart = W_CheckNumForFolderStartPK3("textures/", (uint16_t)w, 0);
		texend = W_CheckNumForFolderEndPK3("textures/", (uint16_t)w, texstart);
		texturesLumpPos = W_CheckNumForNamePwad("TEXTURES", (uint16_t)w, 0);
		while (texturesLumpPos != INT16_MAX)
		{
			R_ParseTEXTURESLump(w, texturesLumpPos, &i);
			texturesLumpPos = W_CheckNumForNamePwad("TEXTURES", (uint16_t)w, texturesLumpPos + 1);
		}
	}
	else
	{
		texstart = W_CheckNumForMarkerStartPwad(TX_START, (uint16_t)w, 0);
		texend = W_CheckNumForNamePwad(TX_END, (uint16_t)w, 0);
		texturesLumpPos = W_CheckNumForNamePwad("TEXTURES", (uint16_t)w, 0);
		if (texturesLumpPos != INT16_MAX)
			R_ParseTEXTURESLump(w, texturesLumpPos, &i);
	}

	if (!( texstart == INT16_MAX || texend == INT16_MAX ))
	{
		// Work through each lump between the markers in the WAD.
		for (j = 0; j < (texend - texstart); j++)
		{
			uint16_t wadnum = (uint16_t)w;
			lumpnum_t lumpnum = texstart + j;
#ifndef NO_PNG_LUMPS
			size_t lumplength;
#endif

			if (wadfiles[w]->type == RET_PK3)
			{
				if (W_IsLumpFolder(wadnum, lumpnum)) // Check if lump is a folder
					continue; // If it is then SKIP IT
			}

			W_ReadLumpHeaderPwad(wadnum, lumpnum, &patchlump, PNG_HEADER_SIZE, 0);
#ifndef NO_PNG_LUMPS
			lumplength = W_LumpLengthPwad(wadnum, lumpnum);
#endif

			//CONS_Printf("\n\"%s\" is a single patch, dimensions %d x %d",W_CheckNameForNumPwad((uint16_t)w,texstart+j),patchlump->width, patchlump->height);
			texture = textures[i] = Z_Calloc(sizeof(texture_t) + sizeof(texpatch_t), PU_STATIC, NULL);

			// Set texture properties.
			memcpy(texture->name, W_CheckNameForNumPwad(wadnum, lumpnum), sizeof(texture->name));
			texture->hash = FNV1a_QuickCaseHash(texture->name, 8);

#ifndef NO_PNG_LUMPS
			if (Picture_IsLumpPNG((uint8_t *)&patchlump, lumplength))
			{
				uint8_t *png = W_CacheLumpNumPwad(wadnum, lumpnum, PU_CACHE);
				int32_t width, height;
				Picture_PNGDimensions(png, &width, &height, NULL, NULL, lumplength);
				texture->width = (int16_t)width;
				texture->height = (int16_t)height;
				Z_Free(png);
			}
			else
#endif
			{
				texture->width = LSBF_SHORT(patchlump.width);
				texture->height = LSBF_SHORT(patchlump.height);
			}

			texture->type = TEXTURETYPE_TEXTURE;
			texture->patchcount = 1;
			texture->holes = false;
			texture->flip = 0;
			texture->terrainID = K_GetTerrainIDForTextureName(texture->name);

			// Allocate information for the texture's patches.
			patch = &texture->patches[0];

			patch->originx = patch->originy = 0;
			patch->wad = (uint16_t)w;
			patch->lump = texstart + j;
			patch->flip = 0;

			texturewidth[i] = texture->width;
			textureheight[i] = texture->height << FRACBITS;
			i++;
		}
	}

	return i;
}

static int32_t
count_range
(		const char * marker_start,
		const char * marker_end,
		const char * folder,
		uint16_t wadnum)
{
	uint16_t j;
	uint16_t texstart, texend;
	int32_t count = 0;

	// Count flats
	if (wadfiles[wadnum]->type == RET_PK3)
	{
		texstart = W_CheckNumForFolderStartPK3(folder, wadnum, 0);
		texend = W_CheckNumForFolderEndPK3(folder, wadnum, texstart);
	}
	else
	{
		texstart = W_CheckNumForMarkerStartPwad(marker_start, wadnum, 0);
		texend = W_CheckNumForNamePwad(marker_end, wadnum, texstart);
	}

	if (texstart != INT16_MAX && texend != INT16_MAX)
	{
		// PK3s have subfolders, so we can't just make a simple sum
		if (wadfiles[wadnum]->type == RET_PK3)
		{
			for (j = texstart; j < texend; j++)
			{
				if (!W_IsLumpFolder(wadnum, j)) // Check if lump is a folder; if not, then count it
					count++;
			}
		}
		else // Add all the textures between markers
		{
			count += (texend - texstart);
		}
	}

	return count;
}

static int32_t R_CountTextures(uint16_t wadnum)
{
	uint16_t texturesLumpPos;
	int32_t count = 0;

	// Load patches and textures.

	// Get the number of textures to check.
	// NOTE: Make SURE the system does not process
	// the markers.
	// This system will allocate memory for all duplicate/patched textures even if it never uses them,
	// but the alternative is to spend a ton of time checking and re-checking all previous entries just to skip any potentially patched textures.

	count += count_range("F_START", "F_END", "flats/", wadnum);

	// Count the textures from TEXTURES lumps
	texturesLumpPos = W_CheckNumForNamePwad("TEXTURES", wadnum, 0);

	while (texturesLumpPos != INT16_MAX)
	{
		count += R_CountTexturesInTEXTURESLump(wadnum, texturesLumpPos);
		texturesLumpPos = W_CheckNumForNamePwad("TEXTURES", wadnum, texturesLumpPos + 1);
	}

	// Count single-patch textures
	count += count_range(TX_START, TX_END, "textures/", wadnum);

	return count;
}

static void R_AllocateTextures(int32_t add)
{
	const int32_t newtextures = (numtextures + add);
	const size_t newsize = newtextures * sizeof (void*);
	//const size_t oldsize = numtextures * sizeof (void*);

	int32_t i;

	// Allocate memory and initialize to 0 for all the textures we are initialising.
	Z_Realloc(textures, newsize, PU_STATIC, &textures);

	// Allocate texture column offset table.
	Z_Realloc(texturecolumnofs, newsize, PU_STATIC, &texturecolumnofs);
	// Allocate texture referencing cache.
	Z_Realloc(texturecache, newsize, PU_STATIC, &texturecache);
	Z_Realloc(texturebrightmapcache, newsize, PU_STATIC, &texturebrightmapcache);
	// Allocate texture width table.
	Z_Realloc(texturewidth, newsize, PU_STATIC, &texturewidth);
	// Allocate texture height table.
	Z_Realloc(textureheight, newsize, PU_STATIC, &textureheight);
	// Create translation table for global animation.
	Z_Realloc(texturetranslation, (newtextures + 1) * sizeof(*texturetranslation), PU_STATIC, &texturetranslation);
	// Create brightmap texture table.
	Z_Realloc(texturebrightmaps, (newtextures + 1) * sizeof(*texturebrightmaps), PU_STATIC, &texturebrightmaps);

	for (i = 0; i < numtextures; ++i)
	{
		// R_FlushTextureCache relies on the user for
		// Z_Free, texturecache has been reallocated so the
		// user is now garbage memory.
		Z_SetUser(texturecache[i], (void**)&texturecache[i]);
		Z_SetUser(texturebrightmapcache[i], (void**)&texturebrightmapcache[i]);
	}

	while (i < newtextures)
	{
		texturetranslation[i] = i;
		texturebrightmaps[i] = 0;
		i++;
	}
}

void R_UpdateTextureBrightmap(int32_t tx, int32_t bm)
{
	I_Assert(tx > 0 && tx < numtextures);
	I_Assert(bm >= 0 && bm < numtextures);

	texturebrightmaps[tx] = bm;
}

static int32_t R_DefineTextures(int32_t i, uint16_t w)
{
	i = Rloadtextures(i, w);
	return Rloadflats(i, w);
}

static void R_FinishLoadingTextures(int32_t add)
{
	numtextures += add;

#ifdef HWRENDER
	if (rendermode == render_opengl)
		HWR_LoadMapTextures(numtextures);
#endif

	g_texturenum_dbgline = R_CheckTextureNumForName("DBGLINE", TEXTURETYPE_TEXTURE);
}

//
// R_LoadTextures
// Initializes the texture list with the textures from the world map.
//
void R_LoadTextures(void)
{
	int32_t i, w;
	int32_t newtextures = 0;

	for (w = 0; w < numwadfiles; w++)
	{
		newtextures += R_CountTextures((uint16_t)w);
	}

	// If no textures found by this point, bomb out
	if (!newtextures)
		I_Error("No textures detected in any WADs!\n");

	R_AllocateTextures(newtextures);

	for (i = 0, w = 0; w < numwadfiles; w++)
	{
		i = R_DefineTextures(i, w);
	}

	R_FinishLoadingTextures(newtextures);
}

void R_LoadTexturesPwad(uint16_t wadnum)
{
	int32_t newtextures = R_CountTextures(wadnum);

	R_AllocateTextures(newtextures);
	R_DefineTextures(numtextures, wadnum);
	R_FinishLoadingTextures(newtextures);
}

static texpatch_t *R_ParsePatch(dboolean actuallyLoadPatch)
{
	char *texturesToken;
	size_t texturesTokenLength;
	char *endPos;
	char *patchName = NULL;
	int16_t patchXPos;
	int16_t patchYPos;
	uint8_t flip = 0;
	uint8_t alpha = 255;
	patchalphastyle_t style = AST_COPY;
	texpatch_t *resultPatch = NULL;
	lumpnum_t patchLumpNum;

	// Patch identifier
	texturesToken = M_GetToken(NULL);
	if (texturesToken == NULL)
	{
		I_Error("Error parsing TEXTURES lump: Unexpected end of file where patch name should be");
	}
	texturesTokenLength = strlen(texturesToken);
	if (texturesTokenLength>8)
	{
		I_Error("Error parsing TEXTURES lump: Patch name \"%s\" exceeds 8 characters",texturesToken);
	}
	else
	{
		if (patchName != NULL)
		{
			Z_Free(patchName);
		}
		patchName = (char *)Z_Malloc((texturesTokenLength+1)*sizeof(char),PU_STATIC,NULL);
		memcpy(patchName,texturesToken,texturesTokenLength*sizeof(char));
		patchName[texturesTokenLength] = '\0';
	}

	// Comma 1
	Z_Free(texturesToken);
	texturesToken = M_GetToken(NULL);
	if (texturesToken == NULL)
	{
		I_Error("Error parsing TEXTURES lump: Unexpected end of file where comma after \"%s\"'s patch name should be",patchName);
	}
	if (!fastcmp(texturesToken,","))
	{
		I_Error("Error parsing TEXTURES lump: Expected \",\" after %s's patch name, got \"%s\"",patchName,texturesToken);
	}

	// XPos
	Z_Free(texturesToken);
	texturesToken = M_GetToken(NULL);
	if (texturesToken == NULL)
	{
		I_Error("Error parsing TEXTURES lump: Unexpected end of file where patch \"%s\"'s x coordinate should be",patchName);
	}
	endPos = NULL;
#ifndef AVOID_ERRNO
	errno = 0;
#endif
	patchXPos = strtol(texturesToken,&endPos,10);
	(void)patchXPos; //unused for now
	if (endPos == texturesToken // Empty string
		|| *endPos != '\0' // Not end of string
#ifndef AVOID_ERRNO
		|| errno == ERANGE // Number out-of-range
#endif
		)
	{
		I_Error("Error parsing TEXTURES lump: Expected an integer for patch \"%s\"'s x coordinate, got \"%s\"",patchName,texturesToken);
	}

	// Comma 2
	Z_Free(texturesToken);
	texturesToken = M_GetToken(NULL);
	if (texturesToken == NULL)
	{
		I_Error("Error parsing TEXTURES lump: Unexpected end of file where comma after patch \"%s\"'s x coordinate should be",patchName);
	}
	if (!fastcmp(texturesToken,","))
	{
		I_Error("Error parsing TEXTURES lump: Expected \",\" after patch \"%s\"'s x coordinate, got \"%s\"",patchName,texturesToken);
	}

	// YPos
	Z_Free(texturesToken);
	texturesToken = M_GetToken(NULL);
	if (texturesToken == NULL)
	{
		I_Error("Error parsing TEXTURES lump: Unexpected end of file where patch \"%s\"'s y coordinate should be",patchName);
	}
	endPos = NULL;
#ifndef AVOID_ERRNO
	errno = 0;
#endif
	patchYPos = strtol(texturesToken,&endPos,10);
	(void)patchYPos; //unused for now
	if (endPos == texturesToken // Empty string
		|| *endPos != '\0' // Not end of string
#ifndef AVOID_ERRNO
		|| errno == ERANGE // Number out-of-range
#endif
		)
	{
		I_Error("Error parsing TEXTURES lump: Expected an integer for patch \"%s\"'s y coordinate, got \"%s\"",patchName,texturesToken);
	}
	Z_Free(texturesToken);

	// Patch parameters block (OPTIONAL)
	// added by Monster Iestyn (22/10/16)

	// Left Curly Brace
	texturesToken = M_GetToken(NULL);
	if (texturesToken == NULL)
		; // move on and ignore, R_ParseTextures will deal with this
	else
	{
		if (fastcmp(texturesToken,"{"))
		{
			Z_Free(texturesToken);
			texturesToken = M_GetToken(NULL);
			if (texturesToken == NULL)
			{
				I_Error("Error parsing TEXTURES lump: Unexpected end of file where patch \"%s\"'s parameters should be",patchName);
			}
			while (!fastcmp(texturesToken,"}"))
			{
				if (fasticmp(texturesToken, "ALPHA"))
				{
					Z_Free(texturesToken);
					texturesToken = M_GetToken(NULL);
					alpha = 255*strtof(texturesToken, NULL);
				}
				else if (fasticmp(texturesToken, "STYLE"))
				{
					Z_Free(texturesToken);
					texturesToken = M_GetToken(NULL);
					if (fasticmp(texturesToken, "TRANSLUCENT"))
						style = AST_TRANSLUCENT;
					else if (fasticmp(texturesToken, "ADD"))
						style = AST_ADD;
					else if (fasticmp(texturesToken, "SUBTRACT"))
						style = AST_SUBTRACT;
					else if (fasticmp(texturesToken, "REVERSESUBTRACT"))
						style = AST_REVERSESUBTRACT;
					else if (fasticmp(texturesToken, "MODULATE"))
						style = AST_MODULATE;
				}
				else if (fasticmp(texturesToken, "FLIPX"))
					flip |= 1;
				else if (fasticmp(texturesToken, "FLIPY"))
					flip |= 2;
				Z_Free(texturesToken);

				texturesToken = M_GetToken(NULL);
				if (texturesToken == NULL)
				{
					I_Error("Error parsing TEXTURES lump: Unexpected end of file where patch \"%s\"'s parameters or right curly brace should be",patchName);
				}
			}
		}
		else
		{
			 // this is not what we wanted...
			 // undo last read so R_ParseTextures can re-get the token for its own purposes
			M_UnGetToken();
		}
		Z_Free(texturesToken);
	}

	if (actuallyLoadPatch == true)
	{
		// Check lump exists
		patchLumpNum = W_GetNumForName(patchName);
		// If so, allocate memory for texpatch_t and fill 'er up
		resultPatch = (texpatch_t *)Z_Malloc(sizeof(texpatch_t),PU_STATIC,NULL);
		resultPatch->originx = patchXPos;
		resultPatch->originy = patchYPos;
		resultPatch->lump = patchLumpNum & 65535;
		resultPatch->wad = patchLumpNum>>16;
		resultPatch->flip = flip;
		resultPatch->alpha = alpha;
		resultPatch->style = style;
		// Clean up a little after ourselves
		Z_Free(patchName);
		// Then return it
		return resultPatch;
	}
	else
	{
		Z_Free(patchName);
		return NULL;
	}
}

static texture_t *R_ParseTexture(dboolean actuallyLoadTexture)
{
	char *texturesToken;
	size_t texturesTokenLength;
	char *endPos;
	int32_t newTextureWidth;
	int32_t newTextureHeight;
	texture_t *resultTexture = NULL;
	texpatch_t *newPatch;
	char newTextureName[9]; // no longer dynamically allocated

	// Texture name
	texturesToken = M_GetToken(NULL);
	if (texturesToken == NULL)
	{
		I_Error("Error parsing TEXTURES lump: Unexpected end of file where texture name should be");
	}
	texturesTokenLength = strlen(texturesToken);
	if (texturesTokenLength>8)
	{
		I_Error("Error parsing TEXTURES lump: Texture name \"%s\" exceeds 8 characters",texturesToken);
	}
	else
	{
		memset(&newTextureName, 0, 9);
		memcpy(newTextureName, texturesToken, texturesTokenLength);
		// ^^ we've confirmed that the token is <= 8 characters so it will never overflow a 9 byte char buffer
		strupr(newTextureName); // Just do this now so we don't have to worry about it
	}
	Z_Free(texturesToken);

	// Comma 1
	texturesToken = M_GetToken(NULL);
	if (texturesToken == NULL)
	{
		I_Error("Error parsing TEXTURES lump: Unexpected end of file where comma after texture \"%s\"'s name should be",newTextureName);
	}
	else if (!fastcmp(texturesToken,","))
	{
		I_Error("Error parsing TEXTURES lump: Expected \",\" after texture \"%s\"'s name, got \"%s\"",newTextureName,texturesToken);
	}
	Z_Free(texturesToken);

	// Width
	texturesToken = M_GetToken(NULL);
	if (texturesToken == NULL)
	{
		I_Error("Error parsing TEXTURES lump: Unexpected end of file where texture \"%s\"'s width should be",newTextureName);
	}
	endPos = NULL;
#ifndef AVOID_ERRNO
	errno = 0;
#endif
	newTextureWidth = strtol(texturesToken,&endPos,10);
	if (endPos == texturesToken // Empty string
		|| *endPos != '\0' // Not end of string
#ifndef AVOID_ERRNO
		|| errno == ERANGE // Number out-of-range
#endif
		|| newTextureWidth < 0) // Number is not positive
	{
		I_Error("Error parsing TEXTURES lump: Expected a positive integer for texture \"%s\"'s width, got \"%s\"",newTextureName,texturesToken);
	}
	Z_Free(texturesToken);

	// Comma 2
	texturesToken = M_GetToken(NULL);
	if (texturesToken == NULL)
	{
		I_Error("Error parsing TEXTURES lump: Unexpected end of file where comma after texture \"%s\"'s width should be",newTextureName);
	}
	if (!fastcmp(texturesToken,","))
	{
		I_Error("Error parsing TEXTURES lump: Expected \",\" after texture \"%s\"'s width, got \"%s\"",newTextureName,texturesToken);
	}
	Z_Free(texturesToken);

	// Height
	texturesToken = M_GetToken(NULL);
	if (texturesToken == NULL)
	{
		I_Error("Error parsing TEXTURES lump: Unexpected end of file where texture \"%s\"'s height should be",newTextureName);
	}
	endPos = NULL;
#ifndef AVOID_ERRNO
	errno = 0;
#endif
	newTextureHeight = strtol(texturesToken,&endPos,10);
	if (endPos == texturesToken // Empty string
		|| *endPos != '\0' // Not end of string
#ifndef AVOID_ERRNO
		|| errno == ERANGE // Number out-of-range
#endif
		|| newTextureHeight < 0) // Number is not positive
	{
		I_Error("Error parsing TEXTURES lump: Expected a positive integer for texture \"%s\"'s height, got \"%s\"",newTextureName,texturesToken);
	}
	Z_Free(texturesToken);

	// Left Curly Brace
	texturesToken = M_GetToken(NULL);
	if (texturesToken == NULL)
	{
		I_Error("Error parsing TEXTURES lump: Unexpected end of file where open curly brace for texture \"%s\" should be",newTextureName);
	}
	if (fastcmp(texturesToken,"{"))
	{
		if (actuallyLoadTexture)
		{
			// Allocate memory for a zero-patch texture. Obviously, we'll be adding patches momentarily.
			resultTexture = (texture_t *)Z_Calloc(sizeof(texture_t),PU_STATIC,NULL);
			memcpy(resultTexture->name, newTextureName, 8);
			resultTexture->hash = FNV1a_QuickCaseHash(newTextureName, 8);
			resultTexture->width = newTextureWidth;
			resultTexture->height = newTextureHeight;
			resultTexture->type = TEXTURETYPE_TEXTURE;
			resultTexture->terrainID = K_GetTerrainIDForTextureName(newTextureName);
		}
		Z_Free(texturesToken);
		texturesToken = M_GetToken(NULL);
		if (texturesToken == NULL)
		{
			I_Error("Error parsing TEXTURES lump: Unexpected end of file where patch definition for texture \"%s\" should be",newTextureName);
		}
		while (!fastcmp(texturesToken,"}"))
		{
			if (fasticmp(texturesToken, "PATCH"))
			{
				Z_Free(texturesToken);
				if (resultTexture)
				{
					// Get that new patch
					newPatch = R_ParsePatch(true);
					// Make room for the new patch
					resultTexture = Z_Realloc(resultTexture, sizeof(texture_t) + (resultTexture->patchcount+1)*sizeof(texpatch_t), PU_STATIC, NULL);
					// Populate the uninitialized values in the new patch entry of our array
					memcpy(&resultTexture->patches[resultTexture->patchcount], newPatch, sizeof(texpatch_t));
					// Account for the new number of patches in the texture
					resultTexture->patchcount++;
					// Then free up the memory assigned to R_ParsePatch, as it's unneeded now
					Z_Free(newPatch);
				}
				else
				{
					R_ParsePatch(false);
				}
			}
			else
			{
				I_Error("Error parsing TEXTURES lump: Expected \"PATCH\" in texture \"%s\", got \"%s\"",newTextureName,texturesToken);
			}

			texturesToken = M_GetToken(NULL);
			if (texturesToken == NULL)
			{
				I_Error("Error parsing TEXTURES lump: Unexpected end of file where patch declaration or right curly brace for texture \"%s\" should be",newTextureName);
			}
		}
		if (resultTexture && resultTexture->patchcount == 0)
		{
			I_Error("Error parsing TEXTURES lump: Texture \"%s\" must have at least one patch",newTextureName);
		}
	}
	else
	{
		I_Error("Error parsing TEXTURES lump: Expected \"{\" for texture \"%s\", got \"%s\"",newTextureName,texturesToken);
	}
	Z_Free(texturesToken);

	if (actuallyLoadTexture) return resultTexture;
	else return NULL;
}

// Parses the TEXTURES lump... but just to count the number of textures.
int R_CountTexturesInTEXTURESLump(uint16_t wadNum, uint16_t lumpNum)
{
	char *texturesLump;
	size_t texturesLumpLength;
	char *texturesText;
	uint32_t numTexturesInLump = 0;
	char *texturesToken;

	// Since lumps AREN'T \0-terminated like I'd assumed they should be, I'll
	// need to make a space of memory where I can ensure that it will terminate
	// correctly. Start by loading the relevant data from the WAD.
	texturesLump = (char *)W_CacheLumpNumPwad(wadNum, lumpNum, PU_STATIC);
	// If that didn't exist, we have nothing to do here.
	if (texturesLump == NULL) return 0;
	// If we're still here, then it DOES exist; figure out how long it is, and allot memory accordingly.
	texturesLumpLength = W_LumpLengthPwad(wadNum, lumpNum);
	texturesText = (char *)Z_Malloc((texturesLumpLength+1)*sizeof(char),PU_STATIC,NULL);
	// Now move the contents of the lump into this new location.
	memmove(texturesText,texturesLump,texturesLumpLength);
	// Make damn well sure the last character in our new memory location is \0.
	texturesText[texturesLumpLength] = '\0';
	// Finally, free up the memory from the first data load, because we really
	// don't need it.
	Z_Free(texturesLump);

	texturesToken = M_GetToken(texturesText);
	while (texturesToken != NULL)
	{
		if (fasticmp(texturesToken, "WALLTEXTURE") || fasticmp(texturesToken, "TEXTURE"))
		{
			numTexturesInLump++;
			Z_Free(texturesToken);
			R_ParseTexture(false);
		}
		else
		{
			I_Error("Error parsing TEXTURES lump: Expected \"WALLTEXTURE\" or \"TEXTURE\", got \"%s\"",texturesToken);
		}
		texturesToken = M_GetToken(NULL);
	}
	Z_Free(texturesToken);
	Z_Free((void *)texturesText);

	return numTexturesInLump;
}

// Parses the TEXTURES lump... for real, this time.
void R_ParseTEXTURESLump(uint16_t wadNum, uint16_t lumpNum, int32_t *texindex)
{
	char *texturesLump;
	size_t texturesLumpLength;
	char *texturesText;
	char *texturesToken;
	texture_t *newTexture;

	I_Assert(texindex != NULL);

	// Since lumps AREN'T \0-terminated like I'd assumed they should be, I'll
	// need to make a space of memory where I can ensure that it will terminate
	// correctly. Start by loading the relevant data from the WAD.
	texturesLump = (char *)W_CacheLumpNumPwad(wadNum, lumpNum, PU_STATIC);
	// If that didn't exist, we have nothing to do here.
	if (texturesLump == NULL) return;
	// If we're still here, then it DOES exist; figure out how long it is, and allot memory accordingly.
	texturesLumpLength = W_LumpLengthPwad(wadNum, lumpNum);
	texturesText = (char *)Z_Malloc((texturesLumpLength+1)*sizeof(char),PU_STATIC,NULL);
	// Now move the contents of the lump into this new location.
	memmove(texturesText,texturesLump,texturesLumpLength);
	// Make damn well sure the last character in our new memory location is \0.
	texturesText[texturesLumpLength] = '\0';
	// Finally, free up the memory from the first data load, because we really
	// don't need it.
	Z_Free(texturesLump);

	texturesToken = M_GetToken(texturesText);
	while (texturesToken != NULL)
	{
		if (fasticmp(texturesToken, "WALLTEXTURE") || fasticmp(texturesToken, "TEXTURE"))
		{
			Z_Free(texturesToken);
			// Get the new texture
			newTexture = R_ParseTexture(true);
			// Store the new texture
			textures[*texindex] = newTexture;
			texturewidth[*texindex] = newTexture->width;
			textureheight[*texindex] = newTexture->height << FRACBITS;
			// Increment i back in R_LoadTextures()
			(*texindex)++;
		}
		else
		{
			I_Error("Error parsing TEXTURES lump: Expected \"WALLTEXTURE\" or \"TEXTURE\", got \"%s\"",texturesToken);
		}
		texturesToken = M_GetToken(NULL);
	}
	Z_Free(texturesToken);
	Z_Free((void *)texturesText);
}

static void PaletteTextureHack(const char **name)
{
	static char uberhack[5];
	if (mapnamespace == MNS_SRB2KART)
	{
		unsigned num;

		char tmp[9];
		strncpy(tmp, *name, 8);
		tmp[8] = '\0';

		if (sscanf(tmp, "~%03u", &num) && num < 256)
		{
			snprintf(uberhack, 5, "~%03u", R_GetPaletteRemap(num));
			*name = uberhack;
		}
	}
}

void R_ClearTextureNumCache(dboolean btell)
{
	Z_Free(tidcache);
	tidcache = NULL;
	if (btell)
		CONS_Debug(DBG_SETUP, "Fun Fact: There are %d textures used in this map.\n", tidcachelen);
	tidcachelen = 0;
}

static void AddTextureToCache(const char *name, uint32_t hash, int32_t id, uint8_t type)
{
	tidcachelen++;
	Z_Realloc(tidcache, tidcachelen * sizeof(*tidcache), PU_STATIC, &tidcache);
	strncpy(tidcache[tidcachelen-1].name, name, 8);
	tidcache[tidcachelen-1].name[8] = '\0';
#ifndef ZDEBUG
	CONS_Debug(DBG_SETUP, "texture #%s: %s\n", sizeu1(tidcachelen), tidcache[tidcachelen-1].name);
#endif
	tidcache[tidcachelen-1].hash = hash;
	tidcache[tidcachelen-1].id = id;
	tidcache[tidcachelen-1].type = type;
}

//
// R_CheckTextureNumForName
//
// Check whether texture is available. Filter out NoTexture indicator.
//
int32_t R_CheckTextureNumForName(const char *name, uint8_t type)
{
	int32_t i;
	uint32_t hash;

	// "NoTexture" marker.
	if (name[0] == '-')
		return 0;

	PaletteTextureHack(&name);

	hash = FNV1a_QuickCaseHash(name, 8);

	for (i = 0; i < tidcachelen; i++)
		if (tidcache[i].type == type && tidcache[i].hash == hash && !strncasecmp(tidcache[i].name, name, 8))
			return tidcache[i].id;

	for (i = numtextures - 1; i >= 0; i--)
		if (textures[i]->type == type && textures[i]->hash == hash && !strncasecmp(textures[i]->name, name, 8))
		{
			AddTextureToCache(name, hash, i, type);
			return i;
		}

	return -1;
}

//
// R_TextureNumForName
//
// Calls R_CheckTextureNumForName. Returns REDWALL if not found.
//
int32_t R_TextureNumForNameEx(const char *name, dboolean lua)
{
	int32_t i = R_CheckTextureNumForName(name, TEXTURETYPE_TEXTURE);

	// Didn't find it, so look for a flat
	if (i == -1)
	{
		i = R_CheckTextureNumForName(name, TEXTURETYPE_FLAT);
	}

	// Still didn't find it, so return REDWALL
	if (i == -1)
	{
		static int32_t redwall = -2;
		CONS_Debug(DBG_SETUP, "WARNING: R_TextureNumForName: %.8s not found\n", name);
		if (redwall == -2)
		{
			redwall = R_CheckTextureNumForName(MISSING_TEXTURE, TEXTURETYPE_TEXTURE);
			if (redwall <= 256 && !lua)
				// see P_SetBinaryFOFAlpha
				I_Error(MISSING_TEXTURE " resolved to ID <= 256. Shit's fucked man");
		}
		if (redwall != -1)
			return redwall;
		return 1;
	}

	return i;
}
