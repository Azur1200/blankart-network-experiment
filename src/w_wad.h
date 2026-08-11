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
/// \file  w_wad.h
/// \brief WAD I/O functions, wad resource definitions (some)

#ifndef __W_WAD__
#define __W_WAD__

#ifdef HWRENDER
#include "hardware/hw_data.h"
#endif

#include "doomtype.h"

#ifdef __cplusplus
extern "C" {
#endif

// a raw entry of the wad directory
// NOTE: This sits here and not in w_wad.c because p_setup.c makes use of it to load map WADs inside PK3s.
struct filelump_t
{
	UINT32 filepos; // file offset of the resource
	UINT32 size; // size of the resource
	char name[8]; // name of the resource
} ATTRPACK;

// ==============================================================
//               WAD FILE STRUCTURE DEFINITIONS
// ==============================================================

// header of a wad file
struct wadinfo_t
{
	char identification[4]; // should be "IWAD" or "PWAD"
	UINT32 numlumps; // how many resources
	UINT32 infotableofs; // the 'directory' of resources
};

// Available compression methods for lumps.
typedef enum
{
	CM_NOCOMPRESSION,
#ifdef HAVE_ZLIB
	CM_DEFLATE,
#endif
	CM_LZF,
	CM_UNSUPPORTED
} compmethod;

//  a memory entry of the wad directory
struct lumpinfo_t
{
	unsigned long position; // filelump_t filepos
	unsigned long disksize; // filelump_t size
	char name[9];           // filelump_t name[] e.g. "LongEntr"
	char *longname;         //                   e.g. "LongEntryName"
	char *fullname;         //                   e.g. "Folder/Subfolder/LongEntryName.extension"
	size_t size;            // real (uncompressed) size
	compmethod compression; // lump compression method

	size_t namelength;      // length of name
	size_t longnamelength;  // length of longname
	size_t fullnamelength;  // length of fullname

	struct {
		UINT32 name;        // hash of name
		UINT32 longname;    // hash of longname
		UINT32 fullname;    // hash of fullname
	} hash;
};

// =========================================================================
//                         'VIRTUAL' RESOURCES
// =========================================================================

struct virtlump_t {
	char name[9];
	UINT8* data;
	size_t size;
};

struct virtres_t {
	size_t numlumps;
	virtlump_t* vlumps;
};

virtres_t* vres_GetMap(lumpnum_t);
void vres_Free(virtres_t*);
virtlump_t* vres_Find(const virtres_t*, const char*);
void* vres_GetPatch(virtlump_t *vlump, INT32 tag);

// =========================================================================
//                         DYNAMIC WAD LOADING
// =========================================================================

#define MAX_WADPATH 512
#define MAX_WADFILES 255 // maximum of wad files used at the same time
// Replay code relies on it being an UINT8. There are no SINT8s handling WAD indices, though.
// Can be set all the way up to 255 but not 256,
// because an UINT8 will never be >= 256, probably breaking some conditionals.

#define lumpcache_t void *

// Resource type of the WAD. Yeah, I know this sounds dumb, but I'll leave it like this until I clean up the code further.
typedef enum restype
{
	RET_WAD,
	RET_SOC,
	RET_LUA,
	RET_PK3,
	RET_UNKNOWN,
} restype_t;

struct wadfile_t
{
	char *filename;
	restype_t type;
	lumpinfo_t *lumpinfo;
	lumpcache_t *lumpcache;
	lumpcache_t *patchcache;
	lumpcache_t *rotcache;
	struct FolderCache *startfolders;
	struct FolderCache *endfolders;
	UINT16 numlumps; // this wad's number of resources
	FILE *handle;
	UINT32 filesize; // for network
	UINT64 hash;

	dboolean important; // also network - !W_VerifyNMUSlumps
	dboolean compatmode;
};

typedef enum wadcompat
{
	WC_AUTO, // haha WC
	WC_ON,
	WC_OFF,
} wadcompat_t;

#define WADFILENUM(lumpnum) (UINT16)((lumpnum)>>16) // wad flumpnum>>16) // wad file number in upper word
#define LUMPNUM(lumpnum) (UINT16)((lumpnum)&0xFFFF) // lump number for this pwad

extern UINT16 numwadfiles;
extern wadfile_t *wadfiles[MAX_WADFILES];

// =========================================================================

// Opens a WAD file. Returns the FILE * handle for the file, or NULL if not found or could not be opened
FILE *W_OpenWadFile(const char **filename, dboolean useerrors);
// Load and add a wadfile to the active wad files, returns numbers of lumps, INT16_MAX on error
UINT16 W_InitFile(const char *filename, dboolean mainfile, dboolean startup, dboolean local, wadcompat_t compat);

dboolean W_MakeFileHash(const char *filename, dboolean openwad, UINT64 *ret);

// W_InitMultipleFiles returns 1 if all is okay, 0 otherwise,
// so that it stops with a message if a file was not found, but not if all is okay.
// W_InitMultipleFiles exits if a file was not found, but not if all is okay.
INT32 W_InitMultipleFiles(char **filenames, dboolean addons);

const char *W_CheckNameForNumPwad(UINT16 wad, UINT16 lump);
const char *W_CheckNameForNum(lumpnum_t lumpnum);

UINT16 W_FindNextEmptyInPwad(UINT16 wad, UINT16 startlump); // checks only in one pwad

UINT16 W_CheckNumForMapPwad(const char *name, UINT32 hash, UINT16 wad, UINT16 startlump);
UINT16 W_CheckNumForNamePwad(const char *name, UINT16 wad, UINT16 startlump); // checks only in one pwad
UINT16 W_CheckNumForLongNamePwad(const char *name, UINT16 wad, UINT16 startlump);

/* Find the first lump after F_START for instance. */
UINT16 W_CheckNumForMarkerStartPwad(const char *name, UINT16 wad, UINT16 startlump);

UINT16 W_CheckNumForFullNamePK3(const char *name, UINT16 wad, UINT16 startlump);
UINT16 W_CheckNumForFolderStartPK3(const char *name, UINT16 wad, UINT16 startlump);
UINT16 W_CheckNumForFolderEndPK3(const char *name, UINT16 wad, UINT16 startlump);

lumpnum_t W_CheckNumForMap(const char *name, dboolean checktofirst);
lumpnum_t W_CheckNumForName(const char *name);
lumpnum_t W_CheckNumForLongName(const char *name);
lumpnum_t W_GetNumForName(const char *name); // like W_CheckNumForName but I_Error on LUMPERROR
lumpnum_t W_GetNumForLongName(const char *name);
lumpnum_t W_CheckNumForNameInBlock(const char *name, const char *blockstart, const char *blockend);
lumpnum_t W_CheckNumForNameInFolder(const char *lump, const char *folder);
UINT8 W_LumpExists(const char *name); // Lua uses this.

size_t W_LumpLengthPwad(UINT16 wad, UINT16 lump);
size_t W_LumpLength(lumpnum_t lumpnum);

dboolean W_IsLumpWad(lumpnum_t lumpnum); // for loading maps from WADs in PK3s
dboolean W_IsLumpFolder(UINT16 wad, UINT16 lump); // for detecting folder "lumps"

#ifdef HAVE_ZLIB
void zerr(int ret); // zlib error checking
#endif

size_t W_ReadLumpHeaderPwad(UINT16 wad, UINT16 lump, void *dest, size_t size, size_t offset);
size_t W_ReadLumpHeader(lumpnum_t lump, void *dest, size_t size, size_t offest); // read all or a part of a lump
void W_ReadLumpPwad(UINT16 wad, UINT16 lump, void *dest);
void W_ReadLump(lumpnum_t lump, void *dest);

void *W_CacheLumpNumPwad(UINT16 wad, UINT16 lump, INT32 tag);
void *W_CacheLumpNum(lumpnum_t lump, INT32 tag);
void *W_CacheLumpNumForce(lumpnum_t lumpnum, INT32 tag);

dboolean W_IsLumpCached(lumpnum_t lump, void *ptr);
dboolean W_IsPatchCached(lumpnum_t lump, void *ptr);
dboolean W_NeedPaletteRemapPwad(UINT16 wad, UINT16 lump, dboolean ignorecache);
dboolean W_NeedPaletteRemap(lumpnum_t lump, dboolean ignorecache);

void *W_CacheLumpName(const char *name, INT32 tag);
void *W_CachePatchName(const char *name, INT32 tag);
void *W_CachePatchLongName(const char *name, INT32 tag);

// Returns either a Software patch, or an OpenGL patch.
// Performs any necessary conversions from PNG images.
void *W_CachePatchNumPwad(UINT16 wad, UINT16 lump, INT32 tag);
#ifdef ROTSPRITE
void *W_CachePatchNameRotated(const char *name, INT32 rotationangle, INT32 tag);
#endif
void *W_CachePatchNum(lumpnum_t lumpnum, INT32 tag);

void *W_GetCachedRotPatchPwad(UINT16 wadnum, UINT16 lumpnum);

// Returns a Software patch.
// Performs any necessary conversions from PNG images.
void *W_CacheSoftwarePatchNumPwad(UINT16 wad, UINT16 lump, INT32 tag);
void *W_CacheSoftwarePatchNum(lumpnum_t lumpnum, INT32 tag);

void W_UnlockCachedPatch(void *patch);

void W_VerifyFileHash(UINT16 wadfilenum, UINT64 matchhash);

int W_VerifyNMUSlumps(const char *filename, dboolean exit_on_error);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __W_WAD__
