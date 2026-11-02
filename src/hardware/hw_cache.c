// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file hw_cache.c
/// \brief load and convert graphics to the hardware format

#include "../doomdef.h"

#ifdef HWRENDER
#include "hw_main.h"
#include "hw_glob.h"
#include "hw_gpu.h"
#include "hw_batching.h"

#include "../doomstat.h"    //gamemode
#include "../i_video.h"     //rendermode
#include "../r_data.h"
#include "../r_textures.h"
#include "../w_wad.h"
#include "../z_zone.h"
#include "../v_video.h"
#include "../r_draw.h"
#include "../r_patch.h"
#include "../r_main.h"
#include "../r_bsp.h"
#include "../r_picformats.h"
#include "../p_setup.h"
#include "../p_setup.h" // levelflats
#include "../p_spec.h" // anim_t
#include "../r_sky.h"
#include "../p_local.h"

int32_t patchformat = GL_TEXFMT_AP_88; // use alpha for holes
int32_t textureformat = GL_TEXFMT_P_8; // use chromakey for hole

RGBA_t mapPalette[256] = {0}; // the palette for the currently loaded level or menu etc.

// Returns a pointer to the palette which should be used for caching textures.
RGBA_t *HWR_GetTexturePalette(void)
{
	return HWR_ShouldUsePaletteRendering() ? mapPalette : pLocalPalette;
}

static int32_t format2bpp(GLTextureFormat_t format)
{
	if (format == GL_TEXFMT_RGBA)
		return 4;
	else if (format == GL_TEXFMT_ALPHA_INTENSITY_88 || format == GL_TEXFMT_AP_88)
		return 2;
	else
		return 1;
}

// This code was originally placed directly in HWR_DrawPatchInCache.
// It is now split from it for my sanity! (and the sanity of others)
// -- Monster Iestyn (13/02/19)
static void HWR_DrawColumnInCache(const column_t *patchcol, uint8_t *block, GLMipmap_t *mipmap,
								int32_t pblockheight, int32_t blockmodulo,
								fixed_t yfracstep, fixed_t scale_y,
								texpatch_t *originPatch, int32_t patchheight,
								int32_t bpp, RGBA_t *palette)
{
	fixed_t yfrac, position, count;
	uint8_t *dest;
	const uint8_t *source;
	int32_t topdelta, prevdelta = -1;
	int32_t originy = 0;

	// for writing a pixel to dest
	RGBA_t colortemp;
	uint8_t alpha;
	uint8_t texel;
	uint16_t texelu16;

	(void)patchheight; // This parameter is unused

	if (originPatch) // originPatch can be NULL here, unlike in the software version
		originy = originPatch->originy;

	while (patchcol->topdelta != 0xff)
	{
		topdelta = patchcol->topdelta;
		if (topdelta <= prevdelta)
			topdelta += prevdelta;
		prevdelta = topdelta;
		source = (const uint8_t *)patchcol + 3;
		count  = ((patchcol->length * scale_y) + (FRACUNIT/2)) >> FRACBITS;
		position = originy + topdelta;

		yfrac = 0;
		//yfracstep = (patchcol->length << FRACBITS) / count;
		if (position < 0)
		{
			yfrac = -position<<FRACBITS;
			count += (((position * scale_y) + (FRACUNIT/2)) >> FRACBITS);
			position = 0;
		}

		position = ((position * scale_y) + (FRACUNIT/2)) >> FRACBITS;

		if (position < 0)
			position = 0;

		if (position + count >= pblockheight)
			count = pblockheight - position;

		dest = block + (position*blockmodulo);
		while (count > 0)
		{
			count--;

			texel = source[yfrac>>FRACBITS];
			alpha = 0xFF;
			// Make pixel transparent if chroma keyed
			if ((mipmap->flags & TF_CHROMAKEYED) && (texel == HWR_PATCHES_CHROMAKEY_COLORINDEX))
				alpha = 0x00;

			//Hurdler: 25/04/2000: now support colormap in hardware mode
			if (mipmap->colormap)
				texel = mipmap->colormap->data[texel];

			// hope compiler will get this switch out of the loops (dreams...)
			// gcc do it ! but vcc not ! (why don't use cygwin gcc for win32 ?)
			// Alam: SRB2 uses Mingw, HUGS
			switch (bpp)
			{
				case 2 : // uhhhhhhhh..........
						 if ((originPatch != NULL) && (originPatch->style != AST_COPY))
							 texel = ASTBlendPaletteIndexes(*(dest+1), texel, originPatch->style, originPatch->alpha);
						 texelu16 = (uint16_t)((alpha<<8) | texel);
						 memcpy(dest, &texelu16, sizeof(uint16_t));
						 break;
				case 3 : colortemp = palette[texel];
						 if ((originPatch != NULL) && (originPatch->style != AST_COPY))
						 {
							 RGBA_t rgbatexel;
							 rgbatexel.rgba = *(uint32_t *)dest;
							 colortemp.rgba = ASTBlendTexturePixel(rgbatexel, colortemp, originPatch->style, originPatch->alpha);
						 }
						 memcpy(dest, &colortemp, sizeof(RGBA_t)-sizeof(uint8_t));
						 break;
				case 4 : colortemp = palette[texel];
						 colortemp.s.alpha = alpha;
						 if ((originPatch != NULL) && (originPatch->style != AST_COPY))
						 {
							 RGBA_t rgbatexel;
							 rgbatexel.rgba = *(uint32_t *)dest;
							 colortemp.rgba = ASTBlendTexturePixel(rgbatexel, colortemp, originPatch->style, originPatch->alpha);
						 }
						 memcpy(dest, &colortemp, sizeof(RGBA_t));
						 break;
				// default is 1
				default:
						 if ((originPatch != NULL) && (originPatch->style != AST_COPY))
							 *dest = ASTBlendPaletteIndexes(*dest, texel, originPatch->style, originPatch->alpha);
						 else
							 *dest = texel;
						 break;
			}

			dest += blockmodulo;
			yfrac += yfracstep;
		}
		patchcol = (const column_t *)((const uint8_t *)patchcol + patchcol->length + 4);
	}
}

static void HWR_DrawFlippedColumnInCache(const column_t *patchcol, uint8_t *block, GLMipmap_t *mipmap,
								int32_t pblockheight, int32_t blockmodulo,
								fixed_t yfracstep, fixed_t scale_y,
								texpatch_t *originPatch, int32_t patchheight,
								int32_t bpp, RGBA_t *palette)
{
	fixed_t yfrac, position, count;
	uint8_t *dest;
	const uint8_t *source;
	int32_t topdelta, prevdelta = -1;
	int32_t originy = 0;

	// for writing a pixel to dest
	RGBA_t colortemp;
	uint8_t alpha;
	uint8_t texel;
	uint16_t texelu16;

	if (originPatch) // originPatch can be NULL here, unlike in the software version
		originy = originPatch->originy;

	while (patchcol->topdelta != 0xff)
	{
		topdelta = patchcol->topdelta;
		if (topdelta <= prevdelta)
			topdelta += prevdelta;
		prevdelta = topdelta;
		topdelta = patchheight-patchcol->length-topdelta;
		source = (const uint8_t *)patchcol + 3;
		count  = ((patchcol->length * scale_y) + (FRACUNIT/2)) >> FRACBITS;
		position = originy + topdelta;

		yfrac = (patchcol->length-1) << FRACBITS;

		if (position < 0)
		{
			yfrac += position<<FRACBITS;
			count += (((position * scale_y) + (FRACUNIT/2)) >> FRACBITS);
			position = 0;
		}

		position = ((position * scale_y) + (FRACUNIT/2)) >> FRACBITS;

		if (position < 0)
			position = 0;

		if (position + count >= pblockheight)
			count = pblockheight - position;

		dest = block + (position*blockmodulo);
		while (count > 0)
		{
			count--;

			texel = source[yfrac>>FRACBITS];
			alpha = 0xFF;
			// Make pixel transparent if chroma keyed
			if ((mipmap->flags & TF_CHROMAKEYED) && (texel == HWR_PATCHES_CHROMAKEY_COLORINDEX))
				alpha = 0x00;

			//Hurdler: 25/04/2000: now support colormap in hardware mode
			if (mipmap->colormap)
				texel = mipmap->colormap->data[texel];

			// hope compiler will get this switch out of the loops (dreams...)
			// gcc do it ! but vcc not ! (why don't use cygwin gcc for win32 ?)
			// Alam: SRB2 uses Mingw, HUGS
			switch (bpp)
			{
				case 2 : // uhhhhhhhh..........
						 if ((originPatch != NULL) && (originPatch->style != AST_COPY))
							 texel = ASTBlendPaletteIndexes(*(dest+1), texel, originPatch->style, originPatch->alpha);
						 texelu16 = (uint16_t)((alpha<<8) | texel);
						 memcpy(dest, &texelu16, sizeof(uint16_t));
						 break;
				case 3 : colortemp = palette[texel];
						 if ((originPatch != NULL) && (originPatch->style != AST_COPY))
						 {
							 RGBA_t rgbatexel;
							 rgbatexel.rgba = *(uint32_t *)dest;
							 colortemp.rgba = ASTBlendTexturePixel(rgbatexel, colortemp, originPatch->style, originPatch->alpha);
						 }
						 memcpy(dest, &colortemp, sizeof(RGBA_t)-sizeof(uint8_t));
						 break;
				case 4 : colortemp = palette[texel];
						 colortemp.s.alpha = alpha;
						 if ((originPatch != NULL) && (originPatch->style != AST_COPY))
						 {
							 RGBA_t rgbatexel;
							 rgbatexel.rgba = *(uint32_t *)dest;
							 colortemp.rgba = ASTBlendTexturePixel(rgbatexel, colortemp, originPatch->style, originPatch->alpha);
						 }
						 memcpy(dest, &colortemp, sizeof(RGBA_t));
						 break;
				// default is 1
				default:
						 if ((originPatch != NULL) && (originPatch->style != AST_COPY))
							 *dest = ASTBlendPaletteIndexes(*dest, texel, originPatch->style, originPatch->alpha);
						 else
							 *dest = texel;
						 break;
			}

			dest += blockmodulo;
			yfrac -= yfracstep;
		}
		patchcol = (const column_t *)((const uint8_t *)patchcol + patchcol->length + 4);
	}
}


// Simplified patch caching function
// for use by sprites and other patches that are not part of a wall texture
// no alpha or flipping should be present since we do not want non-texture graphics to have them
// no offsets are used either
// -- Monster Iestyn (13/02/19)
static void HWR_DrawPatchInCache(GLMipmap_t *mipmap,
	int32_t pblockwidth, int32_t pblockheight,
	int32_t pwidth, int32_t pheight,
	const patch_t *realpatch)
{
	int32_t ncols;
	fixed_t xfrac, xfracstep;
	fixed_t yfracstep, scale_y;
	const column_t *patchcol;
	uint8_t *block = mipmap->data;
	int32_t bpp;
	int32_t blockmodulo;
	RGBA_t *palette;

	if (pwidth <= 0 || pheight <= 0)
		return;

	palette = HWR_GetTexturePalette();

	ncols = pwidth;

	// source advance
	xfrac = 0;
	xfracstep = FRACUNIT;
	yfracstep = FRACUNIT;
	scale_y   = FRACUNIT;

	bpp = format2bpp(mipmap->format);

	if (bpp < 1 || bpp > 4)
		I_Error("HWR_DrawPatchInCache: no drawer defined for this bpp (%d)\n",bpp);

	// NOTE: should this actually be pblockwidth*bpp?
	blockmodulo = pblockwidth*bpp;

	// Draw each column to the block cache
	for (; ncols--; block += bpp, xfrac += xfracstep)
	{
		patchcol = (const column_t *)((const uint8_t *)realpatch->columns + (realpatch->columnofs[xfrac>>FRACBITS]));

		HWR_DrawColumnInCache(patchcol, block, mipmap,
								pblockheight, blockmodulo,
								yfracstep, scale_y,
								NULL, pheight, // not that pheight is going to get used anyway...
								bpp, palette);
	}
}

// This function we use for caching patches that belong to textures
static void HWR_DrawTexturePatchInCache(GLMipmap_t *mipmap,
	int32_t pblockwidth, int32_t pblockheight,
	texture_t *texture, texpatch_t *patch,
	const softwarepatch_t *realpatch)
{
	int32_t x, x1, x2;
	int32_t col, ncols;
	fixed_t xfrac, xfracstep;
	fixed_t yfracstep, scale_y;
	const column_t *patchcol;
	uint8_t *block = mipmap->data;
	int32_t bpp;
	int32_t blockmodulo;
	int32_t width, height;
	RGBA_t *palette;

	// Column drawing function pointer.
	static void (*ColumnDrawerPointer)(const column_t *patchcol, uint8_t *block, GLMipmap_t *mipmap,
								int32_t pblockheight, int32_t blockmodulo,
								fixed_t yfracstep, fixed_t scale_y,
								texpatch_t *originPatch, int32_t patchheight,
								int32_t bpp, RGBA_t *palette);

	if (texture->width <= 0 || texture->height <= 0)
		return;

	ColumnDrawerPointer = (patch->flip & 2) ? HWR_DrawFlippedColumnInCache : HWR_DrawColumnInCache;

	x1 = patch->originx;
	width = LSBF_SHORT(realpatch->width);
	height = LSBF_SHORT(realpatch->height);
	x2 = x1 + width;

	if (x1 > texture->width || x2 < 0)
		return; // patch not located within texture's x bounds, ignore

	if (patch->originy > texture->height || (patch->originy + height) < 0)
		return; // patch not located within texture's y bounds, ignore

	palette = HWR_GetTexturePalette();

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


	col = x * pblockwidth / texture->width;
	ncols = ((x2 - x) * pblockwidth) / texture->width;

/*
	CONS_Debug(DBG_RENDER, "patch %dx%d texture %dx%d block %dx%d\n",
															width, height,
															texture->width,          texture->height,
															pblockwidth,             pblockheight);
	CONS_Debug(DBG_RENDER, "      col %d ncols %d x %d\n", col, ncols, x);
*/

	// source advance
	xfrac = 0;
	if (x1 < 0)
		xfrac = -x1<<FRACBITS;

	xfracstep = (texture->width << FRACBITS) / pblockwidth;
	yfracstep = (texture->height<< FRACBITS) / pblockheight;
	scale_y   = (pblockheight  << FRACBITS) / texture->height;

	bpp = format2bpp(mipmap->format);

	if (bpp < 1 || bpp > 4)
		I_Error("HWR_DrawTexturePatchInCache: no drawer defined for this bpp (%d)\n",bpp);

	// NOTE: should this actually be pblockwidth*bpp?
	blockmodulo = pblockwidth*bpp;

	// Draw each column to the block cache
	for (block += col*bpp; ncols--; block += bpp, xfrac += xfracstep)
	{
		if (patch->flip & 1)
			patchcol = (const column_t *)((const uint8_t *)realpatch + LSBF_LONG(realpatch->columnofs[(width-1)-(xfrac>>FRACBITS)]));
		else
			patchcol = (const column_t *)((const uint8_t *)realpatch + LSBF_LONG(realpatch->columnofs[xfrac>>FRACBITS]));

		ColumnDrawerPointer(patchcol, block, mipmap,
								pblockheight, blockmodulo,
								yfracstep, scale_y,
								patch, height,
								bpp, palette);
	}
}

static uint8_t *MakeBlock(GLMipmap_t *grMipmap)
{
	uint8_t *block;
	int32_t bpp, i;
	uint16_t bu16 = ((0x00 <<8) | HWR_PATCHES_CHROMAKEY_COLORINDEX);
	int32_t blocksize = (grMipmap->width * grMipmap->height);

	bpp =  format2bpp(grMipmap->format);
	block = Z_Malloc(blocksize*bpp, PU_HWRCACHE, &(grMipmap->data));

	switch (bpp)
	{
		case 1: memset(block, HWR_PATCHES_CHROMAKEY_COLORINDEX, blocksize); break;
		case 2:
				// fill background with chromakey, alpha = 0
				for (i = 0; i < blocksize; i++)
					memcpy(block+i*sizeof(uint16_t), &bu16, sizeof(uint16_t));

				break;
		case 4: memset(block, 0x00, blocksize*sizeof(uint32_t)); break;
	}

	return block;
}

//
// Create a composite texture from patches, adapt the texture size to a power of 2
// height and width for the hardware texture cache.
//
static void HWR_GenerateTexture(GLMapTexture_t *grtex, int32_t texnum, dboolean noencoremap)
{
	uint8_t *block;
	texture_t *texture;
	texpatch_t *patch;
	softwarepatch_t *realpatch;
	uint8_t *pdata;
	int32_t blockwidth, blockheight, blocksize;

	uint8_t *colormap = colormaps;

	int32_t i, idx;
	dboolean skyspecial = false; //poor hack for Legacy large skies..

	RGBA_t *palette;
	palette = HWR_GetTexturePalette();

	texture = textures[texnum];

	grtex->mipmap.flags = TF_CHROMAKEYED | TF_WRAPXY;
	grtex->mipmap.format = textureformat;

	// hack the Legacy skies..
	if (UNLIKELY(memcmp(texture->name, "SKY", 3) == 0 &&
		(texture->name[4] == 0 || texture->name[5] == 0)))
	{
		skyspecial = true;
		grtex->mipmap.flags &= ~TF_CHROMAKEYED; // don't use the chromakey for sky
		grtex->mipmap.format = GL_TEXFMT_RGBA; // that skyspecial code below assumes this format ...
	}

	grtex->mipmap.width = (uint16_t)texture->width;
	grtex->mipmap.height = (uint16_t)texture->height;
	if (skyspecial)
		grtex->mipmap.format = GL_TEXFMT_RGBA; // that skyspecial code below assumes this format ...
	else
		grtex->mipmap.format = textureformat;

	if (!noencoremap && encoremap)
		colormap += COLORMAP_REMAPOFFSET;

	grtex->mipmap.colormap = Z_Calloc(sizeof(*grtex->mipmap.colormap), PU_HWRPATCHCOLMIPMAP, NULL);
	grtex->mipmap.colormap->source = colormap;
	memcpy(grtex->mipmap.colormap->data, colormap, 256 * sizeof(uint8_t));

	blockwidth = texture->width;
	blockheight = texture->height;
	blocksize = (blockwidth * blockheight);
	block = MakeBlock(&grtex->mipmap);

	if (skyspecial) //Hurdler: not efficient, but better than holes in the sky (and it's done only at level loading)
	{
		int32_t j;
		RGBA_t col;

		col = palette[HWR_PATCHES_CHROMAKEY_COLORINDEX];
		for (idx = 0, j = 0; j < blockheight; j++)
		{
			for (i = 0; i < blockwidth; i++, idx++)
			{
				block[4*idx+0] = col.s.red;
				block[4*idx+1] = col.s.green;
				block[4*idx+2] = col.s.blue;
				block[4*idx+3] = 0xff;
			}
		}
	}

	// Composite the columns together.
	for (i = 0, patch = texture->patches; i < texture->patchcount; i++, patch++)
	{
		dboolean dealloc = true;
		dboolean doremap = W_NeedPaletteRemapPwad(patch->wad, patch->lump, false);
		size_t lumplength = W_LumpLengthPwad(patch->wad, patch->lump);
		pdata = W_CacheLumpNumPwad(patch->wad, patch->lump, PU_CACHE);
		realpatch = (softwarepatch_t *)pdata;

#ifndef NO_PNG_LUMPS
		if (Picture_IsLumpPNG((uint8_t *)realpatch, lumplength))
			realpatch = (softwarepatch_t *)Picture_PNGConvert(pdata, PICFMT_DOOMPATCH, NULL, NULL, NULL, NULL, lumplength, NULL, 0);
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

		HWR_DrawTexturePatchInCache(&grtex->mipmap, blockwidth, blockheight, texture, patch, realpatch);

		if (dealloc)
			Z_Unlock(realpatch);
	}
	//Hurdler: not efficient at all but I don't remember exactly how HWR_DrawPatchInCache works :(
	if (format2bpp(grtex->mipmap.format)==4)
	{
		for (i = 3; i < blocksize*4; i += 4) // blocksize*4 because blocksize doesn't include the bpp
		{
			if (block[i] == 0)
			{
				grtex->mipmap.flags |= TF_TRANSPARENT;
				break;
			}
		}
	}

	grtex->scaleX = 1.0f/(texture->width*FRACUNIT);
	grtex->scaleY = 1.0f/(texture->height*FRACUNIT);
}

// patch may be NULL if grMipmap has been initialised already and makebitmap is false
void HWR_MakePatch (const patch_t *patch, GLPatch_t *grPatch, GLMipmap_t *grMipmap, dboolean makebitmap)
{
	if (grMipmap->width == 0)
	{
		grMipmap->width = grMipmap->height = 1;
		while (grMipmap->width < patch->width) grMipmap->width <<= 1;
		while (grMipmap->height < patch->height) grMipmap->height <<= 1;

		// no wrap around, no chroma key
		grMipmap->flags = 0;

		// setup the texture info
		grMipmap->format = patchformat;

		grPatch->max_s = (float)patch->width / (float)grMipmap->width;
		grPatch->max_t = (float)patch->height / (float)grMipmap->height;
	}

	Z_Free(grMipmap->data);
	grMipmap->data = NULL;

	if (makebitmap)
	{
		MakeBlock(grMipmap);
		HWR_DrawPatchInCache(grMipmap,
			grMipmap->width, grMipmap->height,
			patch->width, patch->height,
			patch);
	}
}

// =================================================
//             CACHING HANDLING
// =================================================

typedef struct
{
	GLMapTexture_t normal;
	GLMapTexture_t encore;
} GLMapTextureSet_t; // idk i suck at naming things

static size_t gl_numtextures = 0; // Texture count
static GLMapTextureSet_t *gl_textures; // For all textures
static GLMapTextureSet_t *gl_flats; // For all (texture) flats, as normal flats don't need to be cached

void HWR_FreeTextureData(patch_t *patch)
{
	GLPatch_t *grPatch;

	if (!patch || !patch->hardware)
		return;

	grPatch = patch->hardware;

	if (vid.glstate == VID_GL_LIBRARY_LOADED)
		GL_DeleteTexture(grPatch->mipmap);
	if (grPatch->mipmap->data)
		Z_Free(grPatch->mipmap->data);
}

void HWR_FreeTexture(patch_t *patch)
{
	if (!patch)
		return;

	if (patch->hardware)
	{
		GLPatch_t *grPatch = patch->hardware;

		HWR_FreeTextureColormaps(patch);

		if (grPatch->mipmap)
		{
			HWR_FreeTextureData(patch);
			Z_Free(grPatch->mipmap);
		}

		Z_Free(patch->hardware);
	}

	patch->hardware = NULL;
}

// Called by HWR_FreePatchCache.
void HWR_FreeTextureColormaps(patch_t *patch)
{
	GLPatch_t *pat;

	// The patch must be valid, obviously
	if (!patch)
		return;

	pat = patch->hardware;
	if (!pat)
		return;

	// The mipmap must be valid, obviously
	while (pat->mipmap)
	{
		// Confusing at first, but pat->mipmap->nextcolormap
		// at the beginning of the loop is the first colormap
		// from the linked list of colormaps.
		GLMipmap_t *next = NULL;

		// No mipmap in this patch, break out of the loop.
		if (!pat->mipmap)
			break;

		// No colormap mipmaps either.
		if (!pat->mipmap->nextcolormap)
			break;

		// Set the first colormap to the one that comes after it.
		next = pat->mipmap->nextcolormap;
		pat->mipmap->nextcolormap = next->nextcolormap;

		// Free image data from memory.
		if (next->data)
			Z_Free(next->data);
		if (next->colormap)
			Z_Free(next->colormap);
		next->data = NULL;
		next->colormap = NULL;
		GL_DeleteTexture(next);

		// Free the old colormap mipmap from memory.
		free(next);
	}
}

static dboolean FreeTextureCallback(void *mem)
{
	patch_t *patch = (patch_t *)mem;
	HWR_FreeTexture(patch);
	return false;
}

static dboolean FreeColormapsCallback(void *mem)
{
	patch_t *patch = (patch_t *)mem;
	HWR_FreeTextureColormaps(patch);
	return false;
}

static void HWR_FreePatchCache(dboolean freeall)
{
	dboolean (*callback)(void *mem) = FreeTextureCallback;

	if (!freeall)
		callback = FreeColormapsCallback;

	Z_IterateTags(PU_PATCH, PU_PATCH_ROTATED, callback);
	Z_IterateTags(PU_SPRITE, PU_HUDGFX, callback);
}

// free all textures after each level
void HWR_ClearAllTextures(void)
{
	GL_ClearMipMapCache(); // free references to the textures
	HWR_FreePatchCache(true);
}

void HWR_FreeColormapCache(void)
{
	HWR_FreePatchCache(false);
}

void HWR_InitMapTextures(void)
{
	gl_textures = NULL;
	gl_flats = NULL;
}

static void FreeMapTexture(GLMapTexture_t *tex)
{
	if (!tex)
		return;

	GL_DeleteTexture(&tex->mipmap);
	Z_Free(tex->mipmap.data);
	tex->mipmap.data = NULL;
}

void HWR_FreeMapTextures(void)
{
	size_t i;

	for (i = 0; i < gl_numtextures; i++)
	{
		FreeMapTexture(&gl_textures[i].normal);
		FreeMapTexture(&gl_textures[i].encore);

		FreeMapTexture(&gl_flats[i].normal);
		FreeMapTexture(&gl_flats[i].encore);
	}

	// now the heap don't have any 'user' pointing to our
	// texturecache info, we can free it
	if (gl_textures)
		free(gl_textures);
	if (gl_flats)
		free(gl_flats);
	gl_textures = NULL;
	gl_flats = NULL;
	gl_numtextures = 0;
}

static void HWR_PrecacheLevelFlats(void)
{
	levelflat_t levelflat;
	size_t i;

	// special case for encore
	if (encoremap)
	{
		// go through all sectors to determine if it should be remapped for encore
		for (i = 0; i < numsectors; i++)
		{
			size_t j;
			sector_t *sec = &sectors[i];

			// gotta check sector floor and ceiling
			for (j = 0; j < 2; j++)
			{
				const dboolean ceiling = (j == 0);
				int32_t pic = ceiling ? sec->ceilingpic : sec->floorpic;

				levelflat = levelflats[pic];

				HWR_GetLevelFlat(&levelflat, R_NoEncore(sec, &levelflat, ceiling));
			}
		}
	}
	else
	{
		// on non encore we have it simple
		// just load every flat in the level
		for (i = 0; i < numlevelflats; i++)
		{
			levelflat = levelflats[i];

			HWR_GetLevelFlat(&levelflat, false);
		}
	}
}

static void HWR_PrecacheLevelTextures(void)
{
	char *texturepresent;
	anim_t *anim;
	size_t i, j, f;
	int32_t h;

	texturepresent = calloc(gl_numtextures, sizeof (*texturepresent));
	if (texturepresent == NULL)
		I_Error("%s: Out of memory looking up textures", "HWR_PrecacheLevel");

	// Sky texture is always present.
	// Note that F_SKY1 is the name used to indicate a sky floor/ceiling as a flat,
	// while the sky texture is stored like a wall texture, with a skynum dependent name.
	texturepresent[skytexture] = (noencoresky) ? 2 : 1;
	HWR_GetTexture(skytexture, noencoresky);

	for (i = 0; i < numlines; i++)
	{
		const line_t *line = &lines[i];
		const dboolean noencoremap = (encoremap && (line->flags & ML_TFERLINE));

		// two sides
		for (j = 0; j < 2; j++)
		{
			// Single-side linedef
			if (line->sidenum[j] == NO_INDEX)
				continue;

			const side_t *side = &sides[line->sidenum[j]];
			const int32_t sidetex[] = {side->toptexture, side->midtexture, side->bottomtexture};

			// check from top to bottom
			for (f = 0; f < 3; f++)
			{
				const int32_t texnum = sidetex[f];

				if (texnum < 0 || (unsigned)texnum >= gl_numtextures)
					continue;

				if (!(texturepresent[texnum] & 1))
				{
					texturepresent[texnum] |= 1;
					HWR_GetTexture(texnum, false);
				}

				if (noencoremap
					&& !(texturepresent[texnum] & 2))
				{
					texturepresent[texnum] |= 2;
					HWR_GetTexture(texnum, true);
				}
			}
		}
	}

	// check for animated textures
	for (anim = anims; anim < lastanim; anim++)
	{
		const char texpresent = texturepresent[anim->basepic];

		if (!texpresent)
			continue;

		for (h = 1; h < anim->numpics; h++)
		{
			const int32_t animtexnum = anim->basepic+h;

			if (texpresent & 1)
			{
				HWR_GetTexture(animtexnum, false);
			}

			if (texpresent & 2)
			{
				HWR_GetTexture(animtexnum, true);
			}
		}
	}

	free(texturepresent);
}

/*static void HWR_PrecacheLevelSprites(void)
{
	GLPatch_t *spritepatch;
	char *spritepresent;
	size_t i, j, k;
	lumpnum_t lump;

	thinker_t *th;
	mobj_t *mo;
	spriteframe_t *sf;

	spritepresent = calloc(numsprites, sizeof (*spritepresent));
	if (spritepresent == NULL) I_Error("%s: Out of memory looking up sprites", "HWR_PrecacheLevel");

	for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
	{
		if (th->function != (actionf_p1)P_MobjThinker)
			continue;

		mo = (mobj_t *)th;

		// ogl is weird
		// for some reason it does not want to preload sprites with colormaps
		// so just save us the work
		if (mo->color || mo->colorized)
			continue;

		spritepresent[mo->sprite] = 1;
	}

	for (i = 0; i < numsprites; i++)
	{
		if (!spritepresent[i])
			continue;

		for (j = 0; j < sprites[i].numframes; j++)
		{
			sf = &sprites[i].spriteframes[j];

#define cacheang(a) {\
				lump = sf->lumppat[a];\
				spritepatch = W_CachePatchNum(lump, PU_CACHE);\
				if (spritepatch != NULL)\
					HWR_GetPatch((patch_t *)spritepatch);\
			}
			// see R_InitSprites for more about lumppat,lumpid
			switch (sf->rotate)
			{
				case SRF_SINGLE:
					cacheang(0);
					break;
				case SRF_2D:
					cacheang(2);
					cacheang(6);
					break;
				default:
					k = 8;
					while (k--)
						cacheang(k);
					break;
			}
#undef cacheang
		}
	}
	free(spritepresent);
}*/

void HWR_PrecacheLevel(void)
{
	if (rendermode != render_opengl)
		return;

	// Precache flats.
	HWR_PrecacheLevelFlats();

	// prevent timeouts
	NetKeepAlive_NoTImeToWait();

	// Precache textures.
	HWR_PrecacheLevelTextures();

	// prevent timeouts
	NetKeepAlive_NoTImeToWait();

	// Precache sprites.
	//HWR_PrecacheLevelSprites();

	// prevent timeouts
	//NetKeepAlive_NoTImeToWait();
}

void HWR_LoadMapTextures(size_t pnumtextures)
{
	// we must free it since numtextures may have changed
	HWR_FreeMapTextures();

	gl_numtextures = pnumtextures;
	gl_textures = calloc(gl_numtextures, sizeof(*gl_textures)); // *2 - 1 for encore-remapped texture and another for noencore texture (unused when not in encore), see: GLMapTextureSet_t
	gl_flats = calloc(gl_numtextures, sizeof(*gl_flats));

	if (gl_textures == NULL || gl_flats == NULL)
		I_Error("HWR_LoadMapTextures: ran out of memory for OpenGL textures");
}

// --------------------------------------------------------------------------
// Make sure texture is downloaded and set it as the source
// --------------------------------------------------------------------------
GLMapTexture_t *HWR_GetTexture(int32_t tex, dboolean noencoremap)
{
	GLMapTexture_t *grtex;

	if (tex < 0 || (unsigned)tex >= gl_numtextures)
	{
#ifdef PARANOIA
		I_Error("HWR_GetTexture: Invalid texture ID %d", tex);
#else
		tex = 0;
#endif
	}

	grtex = (!noencoremap && encoremap) ? &gl_textures[tex].encore : &gl_textures[tex].normal;

	// Generate texture if missing from the cache
	if (!grtex->mipmap.data && !grtex->mipmap.downloaded)
		HWR_GenerateTexture(grtex, tex, noencoremap || !R_TextureCanRemap(tex));

	// If hardware does not have the texture, then call pfnSetTexture to upload it
	if (!grtex->mipmap.downloaded)
		GL_SetTexture(&grtex->mipmap);
	HWR_SetCurrentTexture(&grtex->mipmap);

	// The system-memory data can be purged now.
	Z_ChangeTag(grtex->mipmap.data, PU_HWRCACHE_UNLOCKED);

	int32_t brightmapnum = R_GetTextureBrightmap(tex);

	if (brightmapnum)
	{
		GLMapTexture_t *grtexbright;

		// Every texture in memory, stored in the
		// hardware renderer's bit depth format. Wow!
		grtexbright = &gl_textures[brightmapnum].normal; // brightmaps do not need extra encore textures as they never remap

		// Generate texture if missing from the cache
		if (!grtexbright->mipmap.data && !grtexbright->mipmap.downloaded)
			HWR_GenerateTexture(grtexbright, brightmapnum, true);

		grtexbright->mipmap.flags |= TF_BRIGHTMAP;

		// If hardware does not have the texture, then call pfnSetTexture to upload it
		if (!grtexbright->mipmap.downloaded)
			GL_SetTexture(&grtexbright->mipmap);
		HWR_SetCurrentTexture(&grtexbright->mipmap);

		// The system-memory data can be purged now.
		Z_ChangeTag(grtexbright->mipmap.data, PU_HWRCACHE_UNLOCKED);
	}

	return grtex;
}

static void HWR_CacheFlat(GLMipmap_t *grMipmap, lumpnum_t flatlumpnum)
{
	uint8_t *flat;
	size_t steppy;
	size_t size, pflatsize;
	dboolean doremap;


	// setup the texture info
	grMipmap->format = GL_TEXFMT_P_8;
	grMipmap->flags = TF_WRAPXY|TF_CHROMAKEYED;

	size = W_LumpLength(flatlumpnum);

	switch (size)
	{
		case 4194304: // 2048x2048 lump
			pflatsize = 2048;
			break;
		case 1048576: // 1024x1024 lump
			pflatsize = 1024;
			break;
		case 262144:// 512x512 lump
			pflatsize = 512;
			break;
		case 65536: // 256x256 lump
			pflatsize = 256;
			break;
		case 16384: // 128x128 lump
			pflatsize = 128;
			break;
		case 1024: // 32x32 lump
			pflatsize = 32;
			break;
		default: // 64x64 lump
			pflatsize = 64;
			break;
	}

	grMipmap->width  = (uint16_t)pflatsize;
	grMipmap->height = (uint16_t)pflatsize;

	doremap = W_NeedPaletteRemap(flatlumpnum, true);

	// the flat raw data needn't be converted with palettized textures
	W_ReadLump(flatlumpnum, Z_Malloc(W_LumpLength(flatlumpnum),
		PU_HWRCACHE, &grMipmap->data));

	if (doremap)
		R_DoPaletteRemapFlat(grMipmap->data, size);

	flat = grMipmap->data;
	for (steppy = 0; steppy < size; steppy++)
		if (flat[steppy] != HWR_PATCHES_CHROMAKEY_COLORINDEX)
			flat[steppy] = grMipmap->colormap->source[flat[steppy]];
}

// Download a Doom 'flat' to the hardware cache and make it ready for use
void HWR_GetRawFlat(lumpnum_t flatlumpnum, dboolean noencoremap)
{
	GLMipmap_t *grmip;
	patch_t *patch;

	uint8_t *colormap = colormaps;

	if (flatlumpnum == LUMPERROR)
		return;

	patch = HWR_GetCachedGLPatch(flatlumpnum);
	grmip = ((GLPatch_t *)Patch_AllocateHardwarePatch(patch))->mipmap;

	if (!grmip->colormap)
	{
		if (!noencoremap && encoremap)
			colormap += COLORMAP_REMAPOFFSET;

		grmip->colormap = Z_Calloc(sizeof(*grmip->colormap), PU_HWRPATCHCOLMIPMAP, NULL);
		grmip->colormap->source = colormap;
		memcpy(grmip->colormap->data, colormap, 256 * sizeof(uint8_t));
	}

	if (!grmip->downloaded && !grmip->data)
	{
		HWR_CacheFlat(grmip, flatlumpnum);
	}

	// If hardware does not have the texture, then call pfnSetTexture to upload it
	if (!grmip->downloaded)
		GL_SetTexture(grmip);
	HWR_SetCurrentTexture(grmip);

	// The system-memory data can be purged now.
	Z_ChangeTag(grmip->data, PU_HWRCACHE_UNLOCKED);
}

void HWR_GetLevelFlat(levelflat_t *levelflat, dboolean noencoremap)
{
	if (levelflat->type == LEVELFLAT_NONE)
	{
		HWR_SetCurrentTexture(NULL);
		return;
	}

	int32_t texturenum = R_GetTextureNum(levelflat->texture_id);
	int32_t brightmapnum = R_GetTextureBrightmap(texturenum);

	if (texturenum <= 0)
	{
		HWR_SetCurrentTexture(NULL);
		return;
	}

	GLMapTexture_t *grtex = (!noencoremap && encoremap) ? &gl_flats[texturenum].encore : &gl_flats[texturenum].normal;
	GLMipmap_t *grMipmap = &grtex->mipmap;

	if (!grMipmap->data && !grMipmap->downloaded)
	{
		uint8_t *flat;
		uint8_t *colormap = colormaps;

		grMipmap->format = GL_TEXFMT_P_8;
		grMipmap->flags = TF_WRAPXY|TF_CHROMAKEYED;

		grMipmap->width  = (uint16_t)textures[texturenum]->width;
		grMipmap->height = (uint16_t)textures[texturenum]->height;

		size_t size = grMipmap->width * grMipmap->height;
		flat = Picture_TextureToFlat(texturenum);
		Z_ChangeTag(flat, PU_HWRCACHE);
		Z_SetUser(flat, &grMipmap->data);

		if (!noencoremap && encoremap)
			colormap += COLORMAP_REMAPOFFSET;

		grtex->mipmap.colormap = Z_Calloc(sizeof(*grtex->mipmap.colormap), PU_HWRPATCHCOLMIPMAP, NULL);
		grtex->mipmap.colormap->source = colormap;
		memcpy(grtex->mipmap.colormap->data, colormap, 256);

		for (size_t steppy = 0; steppy < size; steppy++)
			if (flat[steppy] != HWR_PATCHES_CHROMAKEY_COLORINDEX)
				flat[steppy] = grtex->mipmap.colormap->source[flat[steppy]];
	}

	if (!grMipmap->downloaded)
		GL_SetTexture(&grtex->mipmap);

	HWR_SetCurrentTexture(&grtex->mipmap);

	Z_ChangeTag(grMipmap->data, PU_HWRCACHE_UNLOCKED);

	if (brightmapnum)
	{
		grtex = &gl_flats[brightmapnum].normal; // brightmaps do not need extra encore textures as they never remap
		grMipmap = &grtex->mipmap;

		if (!grMipmap->data && !grMipmap->downloaded)
		{
			grMipmap->format = GL_TEXFMT_P_8;
			grMipmap->flags = TF_WRAPXY|TF_CHROMAKEYED;

			grMipmap->width  = (uint16_t)textures[brightmapnum]->width;
			grMipmap->height = (uint16_t)textures[brightmapnum]->height;

			size_t size = grMipmap->width * grMipmap->height;
			memcpy(Z_Malloc(size, PU_HWRCACHE, &grMipmap->data), R_GetFlatForTexture(brightmapnum), size);
		}

		grtex->mipmap.flags |= TF_BRIGHTMAP;

		if (!grMipmap->downloaded)
			GL_SetTexture(&grtex->mipmap);

		HWR_SetCurrentTexture(&grtex->mipmap);

		Z_ChangeTag(grMipmap->data, PU_HWRCACHE_UNLOCKED);
	}
}

// --------------------+
// HWR_LoadPatchMipmap : Generates a patch into a mipmap, usually the mipmap inside the patch itself
// --------------------+
static void HWR_LoadPatchMipmap(patch_t *patch, GLMipmap_t *grMipmap)
{
	GLPatch_t *grPatch = patch->hardware;
	if (!grMipmap->downloaded && !grMipmap->data)
		HWR_MakePatch(patch, grPatch, grMipmap, true);

	// If hardware does not have the texture, then call pfnSetTexture to upload it
	if (!grMipmap->downloaded)
		GL_SetTexture(grMipmap);
	HWR_SetCurrentTexture(grMipmap);

	// The system-memory data can be purged now.
	Z_ChangeTag(grMipmap->data, PU_HWRCACHE_UNLOCKED);
}

// ----------------------+
// HWR_UpdatePatchMipmap : Updates a mipmap.
// ----------------------+
static void HWR_UpdatePatchMipmap(patch_t *patch, GLMipmap_t *grMipmap)
{
	GLPatch_t *grPatch = patch->hardware;
	HWR_MakePatch(patch, grPatch, grMipmap, true);

	// If hardware does not have the texture, then call pfnSetTexture to upload it
	// If it does have the texture, then call pfnUpdateTexture to update it
	if (!grMipmap->downloaded)
		GL_SetTexture(grMipmap);
	else
		GL_UpdateTexture(grMipmap);
	HWR_SetCurrentTexture(grMipmap);

	// The system-memory data can be purged now.
	Z_ChangeTag(grMipmap->data, PU_HWRCACHE_UNLOCKED);
}

// -----------------+
// HWR_GetPatch     : Downloads a patch to the hardware cache and make it ready for use
// -----------------+
void HWR_GetPatch(patch_t *patch)
{
	if (!patch->hardware)
		Patch_CreateGL(patch);
	HWR_LoadPatchMipmap(patch, ((GLPatch_t *)patch->hardware)->mipmap);
}

// -------------------+
// HWR_GetMappedPatch : Same as HWR_GetPatch for sprite color
// -------------------+
void HWR_GetMappedPatch(patch_t *patch, const uint8_t *colormap)
{
	GLPatch_t *grPatch;
	GLMipmap_t *grMipmap, *newMipmap;

	if (!patch->hardware)
		Patch_CreateGL(patch);
	grPatch = patch->hardware;

	// Blatant hack for encore colormapping aside...
	if (colormap == colormaps || colormap == NULL || colormap == (const uint8_t*)(COLORMAP_REMAPOFFSET))
	{
		// Load the default (green) color in hardware cache
		HWR_GetPatch(patch);
		return;
	}

	// search for the mipmap
	// skip the first (no colormap translated)
	for (grMipmap = grPatch->mipmap; LIKELY(grMipmap->nextcolormap); )
	{
		grMipmap = grMipmap->nextcolormap;
		if (UNLIKELY(grMipmap->colormap && grMipmap->colormap->source == colormap))
		{
			if (memcmp(grMipmap->colormap->data, colormap, 256 * sizeof(uint8_t)))
			{
				memcpy(grMipmap->colormap->data, colormap, 256 * sizeof(uint8_t));
				HWR_UpdatePatchMipmap(patch, grMipmap);
			}
			else
				HWR_LoadPatchMipmap(patch, grMipmap);
			return;
		}
	}
	// not found, create it!
	// If we are here, the sprite with the current colormap is not already in hardware memory

	//BP: WARNING: don't free it manually without clearing the cache of harware renderer
	//              (it have a liste of mipmap)
	//    this malloc is cleared in HWR_FreeColormapCache
	//    (...) unfortunately z_malloc fragment alot the memory :(so malloc is better
	newMipmap = calloc(1, sizeof (*newMipmap));
	if (newMipmap == NULL)
		I_Error("%s: Out of memory", "HWR_GetMappedPatch");
	grMipmap->nextcolormap = newMipmap;

	newMipmap->colormap = Z_Calloc(sizeof(*newMipmap->colormap), PU_HWRPATCHCOLMIPMAP, NULL);
	newMipmap->colormap->source = colormap;
	memcpy(newMipmap->colormap->data, colormap, 256 * sizeof(uint8_t));

	HWR_LoadPatchMipmap(patch, newMipmap);
}

void HWR_UnlockCachedPatch(GLPatch_t *gpatch)
{
	if (!gpatch)
		return;

	Z_ChangeTag(gpatch->mipmap->data, PU_HWRCACHE_UNLOCKED);
}

static const int32_t picmode2GR[] =
{
	GL_TEXFMT_P_8,                // PALETTE
	0,                            // INTENSITY          (unsupported yet)
	GL_TEXFMT_ALPHA_INTENSITY_88, // INTENSITY_ALPHA    (corona use this)
	0,                            // RGB24              (unsupported yet)
	GL_TEXFMT_RGBA,               // RGBA32             (opengl only)
};

static void HWR_DrawPicInCache(uint8_t *block, int32_t pblockwidth, int32_t pblockheight,
	int32_t blockmodulo, pic_t *pic, int32_t bpp)
{
	int32_t i,j;
	fixed_t posx, posy, stepx, stepy;
	uint8_t *dest, *src, texel;
	uint16_t texelu16;
	int32_t picbpp;
	RGBA_t col;
	RGBA_t *palette = HWR_GetTexturePalette();

	stepy = ((int32_t)LSBF_SHORT(pic->height)<<FRACBITS)/pblockheight;
	stepx = ((int32_t)LSBF_SHORT(pic->width)<<FRACBITS)/pblockwidth;
	picbpp = format2bpp(picmode2GR[pic->mode]);
	posy = 0;
	for (j = 0; j < pblockheight; j++)
	{
		posx = 0;
		dest = &block[j*blockmodulo];
		src = &pic->data[(posy>>FRACBITS)*LSBF_SHORT(pic->width)*picbpp];
		for (i = 0; i < pblockwidth;i++)
		{
			switch (pic->mode)
			{ // source bpp
				case PALETTE :
					texel = src[(posx+FRACUNIT/2)>>FRACBITS];
					switch (bpp)
					{ // destination bpp
						case 1 :
							*dest++ = texel; break;
						case 2 :
							texelu16 = (uint16_t)(texel | 0xff00);
							memcpy(dest, &texelu16, sizeof(uint16_t));
							dest += sizeof(uint16_t);
							break;
						case 3 :
							col = palette[texel];
							memcpy(dest, &col, sizeof(RGBA_t)-sizeof(uint8_t));
							dest += sizeof(RGBA_t)-sizeof(uint8_t);
							break;
						case 4 :
							memcpy(dest, &palette[texel], sizeof(RGBA_t));
							dest += sizeof(RGBA_t);
							break;
					}
					break;
				case INTENSITY :
					*dest++ = src[(posx+FRACUNIT/2)>>FRACBITS];
					break;
				case INTENSITY_ALPHA : // assume dest bpp = 2
					memcpy(dest, src + ((posx+FRACUNIT/2)>>FRACBITS)*sizeof(uint16_t), sizeof(uint16_t));
					dest += sizeof(uint16_t);
					break;
				case RGB24 :
					break;  // not supported yet
				case RGBA32 : // assume dest bpp = 4
					dest += sizeof(uint32_t);
					memcpy(dest, src + ((posx+FRACUNIT/2)>>FRACBITS)*sizeof(uint32_t), sizeof(uint32_t));
					break;
			}
			posx += stepx;
		}
		posy += stepy;
	}
}

// -----------------+
// HWR_GetPic       : Download a Doom pic (raw row encoded with no 'holes')
// Returns          :
// -----------------+
patch_t *HWR_GetPic(lumpnum_t lumpnum)
{
	patch_t *patch = HWR_GetCachedGLPatch(lumpnum);
	GLPatch_t *grPatch = (GLPatch_t *)(patch->hardware);

	if (!grPatch->mipmap->downloaded && !grPatch->mipmap->data)
	{
		pic_t *pic;
		uint8_t *block;
		size_t len;

		pic = W_CacheLumpNum(lumpnum, PU_CACHE);
		patch->width = LSBF_SHORT(pic->width);
		patch->height = LSBF_SHORT(pic->height);
		len = W_LumpLength(lumpnum) - sizeof (pic_t);

		grPatch->mipmap->width = (uint16_t)patch->width;
		grPatch->mipmap->height = (uint16_t)patch->height;

		if (pic->mode == PALETTE)
			grPatch->mipmap->format = textureformat; // can be set by driver
		else
			grPatch->mipmap->format = picmode2GR[pic->mode];

		Z_Free(grPatch->mipmap->data);

		// allocate block
		block = MakeBlock(grPatch->mipmap);

		if (patch->width  == LSBF_SHORT(pic->width) &&
			patch->height == LSBF_SHORT(pic->height) &&
			format2bpp(grPatch->mipmap->format) == format2bpp(picmode2GR[pic->mode]))
		{
			// no conversion needed
			memcpy(grPatch->mipmap->data, pic->data,len);
		}
		else
			HWR_DrawPicInCache(block, LSBF_SHORT(pic->width), LSBF_SHORT(pic->height),
			                   LSBF_SHORT(pic->width)*format2bpp(grPatch->mipmap->format),
			                   pic,
			                   format2bpp(grPatch->mipmap->format));

		Z_Unlock(pic);
		Z_ChangeTag(block, PU_HWRCACHE_UNLOCKED);

		grPatch->mipmap->flags = 0;
		grPatch->max_s = grPatch->max_t = 1.0f;
	}
	GL_SetTexture(grPatch->mipmap);
	//CONS_Debug(DBG_RENDER, "picloaded at %x as texture %d\n",grPatch->mipmap->data, grPatch->mipmap->downloaded);

	return patch;
}

patch_t *HWR_GetCachedGLPatchPwad(uint16_t wadnum, uint16_t lumpnum)
{
	lumpcache_t *lumpcache = wadfiles[wadnum]->patchcache;
	if (!lumpcache[lumpnum])
	{
		void *ptr = Z_Calloc(sizeof(patch_t), PU_PATCH, &lumpcache[lumpnum]);
		Patch_Create(NULL, 0, ptr);
		Patch_AllocateHardwarePatch(ptr);
	}
	return (patch_t *)(lumpcache[lumpnum]);
}

patch_t *HWR_GetCachedGLPatch(lumpnum_t lumpnum)
{
	return HWR_GetCachedGLPatchPwad(WADFILENUM(lumpnum),LUMPNUM(lumpnum));
}

// Need to do this because they aren't powers of 2
static void HWR_DrawFadeMaskInCache(GLMipmap_t *mipmap, int32_t pblockwidth, int32_t pblockheight,
	lumpnum_t fademasklumpnum, uint16_t fmwidth, uint16_t fmheight)
{
	int32_t i,j;
	fixed_t posx, posy, stepx, stepy;
	uint8_t *block = mipmap->data; // places the data directly into here
	uint8_t *flat;
	uint8_t *dest, *src, texel;
	RGBA_t col;
	RGBA_t *palette = HWR_GetTexturePalette();

	// Place the flats data into flat
	W_ReadLump(fademasklumpnum, Z_Malloc(W_LumpLength(fademasklumpnum),
		PU_HWRCACHE, &flat));

	stepy = ((int32_t)fmheight<<FRACBITS)/pblockheight;
	stepx = ((int32_t)fmwidth<<FRACBITS)/pblockwidth;
	posy = 0;
	for (j = 0; j < pblockheight; j++)
	{
		posx = 0;
		dest = &block[j*(mipmap->width)]; // 1bpp
		src = &flat[(posy>>FRACBITS)*LSBF_SHORT(fmwidth)];
		for (i = 0; i < pblockwidth;i++)
		{
			// fademask bpp is always 1, and is used just for alpha
			texel = src[(posx)>>FRACBITS];
			col = palette[texel];
			*dest = col.s.red; // take the red level of the colour and use it for alpha, as fademasks do

			dest++;
			posx += stepx;
		}
		posy += stepy;
	}

	Z_Free(flat);
}

static void HWR_CacheFadeMask(GLMipmap_t *grMipmap, lumpnum_t fademasklumpnum)
{
	size_t size;
	uint16_t fmheight = 0, fmwidth = 0;

	// setup the texture info
	grMipmap->format = GL_TEXFMT_ALPHA_8; // put the correct alpha levels straight in so I don't need to convert it later
	grMipmap->flags = 0;

	size = W_LumpLength(fademasklumpnum);

	switch (size)
	{
		// None of these are powers of 2, so I'll need to do what is done for textures and make them powers of 2 before they can be used
		case 256000: // 640x400
			fmwidth = 640;
			fmheight = 400;
			break;
		case 64000: // 320x200
			fmwidth = 320;
			fmheight = 200;
			break;
		case 16000: // 160x100
			fmwidth = 160;
			fmheight = 100;
			break;
		case 4000: // 80x50 (minimum)
			fmwidth = 80;
			fmheight = 50;
			break;
		default: // Bad lump
			CONS_Alert(CONS_WARNING, "Fade mask lump of incorrect size, ignored\n"); // I should avoid this by checking the lumpnum in HWR_RunWipe
			fmwidth = 0;
			fmheight = 0;
			return;
	}

	// Thankfully, this will still work for this scenario
	grMipmap->width  = fmwidth;
	grMipmap->height = fmheight;

	MakeBlock(grMipmap);

	HWR_DrawFadeMaskInCache(grMipmap, fmwidth, fmheight, fademasklumpnum, fmwidth, fmheight);

	// I DO need to convert this because it isn't power of 2 and we need the alpha
}


void HWR_GetFadeMask(lumpnum_t fademasklumpnum)
{
	patch_t *patch = HWR_GetCachedGLPatch(fademasklumpnum);
	GLMipmap_t *grmip = ((GLPatch_t *)Patch_AllocateHardwarePatch(patch))->mipmap;
	if (!grmip->downloaded && !grmip->data)
		HWR_CacheFadeMask(grmip, fademasklumpnum);

	GL_SetTexture(grmip);

	// The system-memory data can be purged now.
	Z_ChangeTag(grmip->data, PU_HWRCACHE_UNLOCKED);
}

// =================================================
//             PALETTE HANDLING
// =================================================

void HWR_SetPalette(RGBA_t *palette)
{
	if (HWR_ShouldUsePaletteRendering())
	{
		// set the palette for palette postprocessing

		if (cv_glpalettedepth.value == 16)
		{
			// crush to 16-bit rgb565, like software currently does in the standard configuration
			// Note: Software's screenshots have the 24-bit palette, but the screen gets
			// the 16-bit version! For making comparison screenshots either use an external screenshot
			// tool or set the palette depth to 24 bits.
			RGBA_t crushed_palette[256];
			int i;

			for (i = 0; i < 256; i++)
			{
				float fred = (float)(palette[i].s.red >> 3);
				float fgreen = (float)(palette[i].s.green >> 2);
				float fblue = (float)(palette[i].s.blue >> 3);
				crushed_palette[i].s.red = (uint8_t)(fred / 31.0f * 255.0f);
				crushed_palette[i].s.green = (uint8_t)(fgreen / 63.0f * 255.0f);
				crushed_palette[i].s.blue = (uint8_t)(fblue / 31.0f * 255.0f);
				crushed_palette[i].s.alpha = 255;
			}

			NetKeepAlive_NoTImeToWait();

			GL_SetScreenPalette(crushed_palette);
		}
		else
		{
			GL_SetScreenPalette(palette);
		}

		// this part is responsible for keeping track of the palette OUTSIDE of a level.
		if (!(gamestate == GS_LEVEL || (gamestate == GS_TITLESCREEN && titlemapinaction)))
			HWR_SetMapPalette();
	}
	else
	{
		// set the palette for the textures
		GL_SetPalette(palette);
		// reset mapPalette so next call to HWR_SetMapPalette will update everything correctly
		memset(mapPalette, 0, sizeof(mapPalette));
		// hardware driver will flush there own cache if cache is non paletized
		// now flush data texture cache so 32 bit texture are recomputed
		if (patchformat == GL_TEXFMT_RGBA || textureformat == GL_TEXFMT_RGBA)
		{
			Z_FreeTag(PU_HWRCACHE);
			Z_FreeTag(PU_HWRCACHE_UNLOCKED);
		}
	}
}

static void HWR_SetPaletteLookup(RGBA_t *palette)
{
	int r, g, b;
	uint8_t *lut = Z_Malloc(
		HWR_PALETTE_LUT_SIZE*HWR_PALETTE_LUT_SIZE*HWR_PALETTE_LUT_SIZE*sizeof(uint8_t),
																	PU_STATIC, NULL);
#define STEP_SIZE (256/HWR_PALETTE_LUT_SIZE)
	for (b = 0; b < HWR_PALETTE_LUT_SIZE; b++)
	{
		for (g = 0; g < HWR_PALETTE_LUT_SIZE; g++)
		{
			for (r = 0; r < HWR_PALETTE_LUT_SIZE; r++)
			{
				lut[b*HWR_PALETTE_LUT_SIZE*HWR_PALETTE_LUT_SIZE+g*HWR_PALETTE_LUT_SIZE+r] =
				NearestPaletteColor(r*STEP_SIZE, g*STEP_SIZE, b*STEP_SIZE, palette);
			}
		}
	}
#undef STEP_SIZE
	NetKeepAlive_NoTImeToWait();
	GL_SetPaletteLookup(lut);
	Z_Free(lut);
}

// Updates mapPalette to reflect the loaded level or other game state.
// Textures are flushed if needed.
// Call this function only in palette rendering mode.
void HWR_SetMapPalette(void)
{
	RGBA_t RGBA_converted[256];
	RGBA_t *palette;
	int i;

	if (!(gamestate == GS_LEVEL || (gamestate == GS_TITLESCREEN && titlemapinaction)))
	{
		// outside of a level, pMasterPalette should have PLAYPAL ready for us
		palette = pMasterPalette;
	}
	else
	{
		// in a level pMasterPalette might have a flash palette, but we
		// want the map's original palette.
		lumpnum_t lumpnum = W_GetNumForName(GetPalette());
		size_t palsize = W_LumpLength(lumpnum);
		uint8_t *RGB_data;

		if (palsize < 768) // 256 * 3
			I_Error("HWR_SetMapPalette: A programmer assumed palette lumps are at least 768 bytes long, but apparently this was a wrong assumption!\n");

		RGB_data = W_CacheLumpNum(lumpnum, PU_CACHE);
		// we got the RGB palette now, but we need it in RGBA format.
		for (i = 0; i < 256; i++)
		{
			RGBA_converted[i].s.red = *(RGB_data++);
			RGBA_converted[i].s.green = *(RGB_data++);
			RGBA_converted[i].s.blue = *(RGB_data++);
			RGBA_converted[i].s.alpha = 255;
		}

		NetKeepAlive_NoTImeToWait();

		// remap the palette from 2.1 to 2.2 indices in compatmode
		RGBA_t *palcopy = NULL;

		if (wadfiles[WADFILENUM(lumpnum)]->compatmode)
		{
			palcopy = malloc(sizeof(*palcopy)*256);
			memcpy(palcopy, RGBA_converted, sizeof(*palcopy)*256);
		}

		if (palcopy)
		{
			for (i = 0; i < 256; i++)
			{
				RGBA_converted[i].rgba = palcopy[R_InvPaletteRemap(i)].rgba;
			}

			NetKeepAlive_NoTImeToWait();

			free(palcopy);
		}

		palette = RGBA_converted;
	}

	// check if the palette has changed from the previous one
	if (memcmp(mapPalette, palette, sizeof(mapPalette)))
	{
		memcpy(mapPalette, palette, sizeof(mapPalette));

		// in palette rendering mode, this means that all rgba textures now have wrong colors
		// and the lookup table is outdated
		HWR_SetPaletteLookup(mapPalette);
		GL_SetPalette(mapPalette);

		if (patchformat == GL_TEXFMT_RGBA || textureformat == GL_TEXFMT_RGBA)
		{
			Z_FreeTag(PU_HWRCACHE);
			Z_FreeTag(PU_HWRCACHE_UNLOCKED);
		}
	}
}

// Creates a hardware lighttable from the supplied lighttable.
// Returns the id of the hw lighttable, usable in FSurfaceInfo.
uint32_t HWR_CreateLightTable(uint8_t *lighttable)
{
	uint32_t i, id;
	RGBA_t *palette = HWR_GetTexturePalette();
	RGBA_t *hw_lighttable = Z_Malloc(256 * 32 * sizeof(RGBA_t), PU_STATIC, NULL);

	// To make the palette index -> RGBA mapping easier for the shader,
	// the hardware lighttable is composed of RGBA colors instead of palette indices.
	for (i = 0; i < 256 * 32; i++)
		hw_lighttable[i] = palette[lighttable[i]];

	id = GL_CreateLightTable(hw_lighttable);
	Z_Free(hw_lighttable);
	return id;
}

// get hwr lighttable id for colormap, create it if it doesn't already exist
uint32_t HWR_GetLightTableID(extracolormap_t *colormap)
{
	dboolean default_colormap = false;
	if (!colormap)
	{
		colormap = R_GetDefaultColormap(); // a place to store the hw lighttable id
		// alternatively could just store the id in a global variable if there are issues
		default_colormap = true;
	}

	// create hw lighttable if there isn't one
	if (!colormap->gl_lighttable_id)
	{
		uint8_t *colormap_pointer;

		if (default_colormap)
			colormap_pointer = colormaps; // don't actually use the data from the "default colormap"
		else
			colormap_pointer = colormap->colormap;
		colormap->gl_lighttable_id = HWR_CreateLightTable(colormap_pointer);
	}

	return colormap->gl_lighttable_id;
}

// Note: all hardware lighttable ids assigned before this
// call become invalid and must not be used.
void HWR_ClearLightTables(void)
{
	if (vid.glstate == VID_GL_LIBRARY_LOADED)
		GL_ClearLightTables();
}

#endif //HWRENDER
