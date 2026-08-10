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
/// \file  w_wad.c
/// \brief Handles WAD file header, directory, lump I/O

#ifdef HAVE_ZLIB
#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif

#ifndef _LFS64_LARGEFILE
#define _LFS64_LARGEFILE
#endif

#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 0
#endif

#include <zlib.h>
#endif

#ifdef __GNUC__
#include <unistd.h>
#endif

#define ZWAD

#ifdef ZWAD
#include <errno.h>
#include "lzf.h"
#endif

#include "doomdef.h"
#include "doomstat.h"
#include "doomtype.h"

#include "w_wad.h"
#include "z_zone.h"

#include "g_game.h" // G_LoadGameData
#include "filesrch.h"

#include "i_video.h" // rendermode
#include "d_netfil.h"
#include "deh_soc.h"
#include "d_clisrv.h"
#include "r_defs.h"
#include "r_data.h"
#include "r_textures.h"
#include "r_patch.h"
#include "r_picformats.h"
#include "i_time.h"
#include "i_system.h"
#include "lua_script.h"
#include "g_game.h" // G_MapNumber
#include "r_patchrotation.h"
#include "lua_hook.h"

#include "k_terrain.h"

#ifdef HWRENDER
#include "hardware/hw_main.h"
#include "hardware/hw_glob.h"
#endif

#ifdef _DEBUG
#include "console.h"
#endif

#ifndef O_BINARY
#define O_BINARY 0
#endif


typedef struct
{
	const char *name;
	size_t len;
} lumpchecklist_t;

// Must be a power of two
#define LUMPNUMCACHESIZE 2048 // should be reasonable ig
#define LUMPNUMCACHENAME 32

#define NAME      lumpnum_map
#define KEY_TY    char *
#define KEY_GET_CONST
#define VAL_TY    lumpnum_t
#define HASH_FN   vt_hash_string
#define CMPR_FN   vt_cmpr_string
#define KEY_DTOR_FN free
#include "verstable.h"

#define NAME      folder_map
#define KEY_TY    char *
#define KEY_GET_CONST
#define VAL_TY    UINT16
#define HASH_FN   vt_hash_string
#define CMPR_FN   vt_cmpr_string
#define KEY_DTOR_FN free
#include "verstable.h"

static lumpnum_map lumpnumcache;

typedef struct FolderCache {
	folder_map map;
} FolderCache;

//===========================================================================
//                                                                    GLOBALS
//===========================================================================
UINT16 numwadfiles = 0; // number of active wadfiles
wadfile_t *wadfiles[MAX_WADFILES]; // 0 to numwadfiles-1 are valid

// W_Shutdown
// Closes all of the WAD files before quitting
// If not done on a Mac then open wad files
// can prevent removable media they are on from
// being ejected
DESTRUCTOR static void W_Shutdown(void)
{
	lumpnum_map_cleanup(&lumpnumcache);

	while (numwadfiles--)
	{
		wadfile_t *wad = wadfiles[numwadfiles];

		fclose(wad->handle);
		Z_Free(wad->filename);
		while (wad->numlumps--)
		{
			Z_Free(wad->lumpinfo[wad->numlumps].longname);
			if (wad->lumpinfo[wad->numlumps].fullname != wad->lumpinfo[wad->numlumps].longname)
			{
				Z_Free(wad->lumpinfo[wad->numlumps].fullname);
			}
		}

		folder_map_cleanup(&wad->startfolders->map);
		free(wad->startfolders);
		folder_map_cleanup(&wad->endfolders->map);
		free(wad->endfolders);

		Z_Free(wad->lumpinfo);
		Z_Free(wad);
	}
}

// the lumpnum cache needs to be initialized before use
CONSTRUCTOR static void W_Startup(void)
{
	lumpnum_map_init(&lumpnumcache);
	lumpnum_map_reserve(&lumpnumcache, LUMPNUMCACHESIZE);
}

//===========================================================================
//                                                        LUMP BASED ROUTINES
//===========================================================================

// W_AddFile
// All files are optional, but at least one file must be
//  found (PWAD, if all required lumps are present).
// Files with a .wad extension are wadlink files
//  with multiple lumps.
// Other files are single lumps with the base filename
//  for the lump name.

static char filenamebuf[MAX_WADPATH];

// This #if is copied from filesrch.c, so not sure if it is 100% suitable for
// this
#if defined (_WIN32) && !defined (_XBOX)
#define WIN32_LEAN_AND_MEAN
#define RPC_NO_WINDOWS_H
#include <windows.h>

// Windows can't open utf-8 path so it must be converted to utf-16
FILE* fopen_utf8(const char* filename, const char* mode)
{
	static const int MY_PATH_MAX =  2048;
	WCHAR nameW[MY_PATH_MAX];
	memset(nameW, 0, sizeof(WCHAR)*MY_PATH_MAX);
	WCHAR modeW[16];
	memset(modeW, 0, sizeof(WCHAR)*16);
	// the following function converts the UTF-8 filename to UTF-16 (WCHAR) nameW
	int len = MultiByteToWideChar(CP_UTF8, 0, filename, -1, nameW, MY_PATH_MAX);
	if(len > 0 && MultiByteToWideChar(CP_UTF8, 0, mode, -1, modeW, 16) > 0)
	{
		// using _wfopen_s() shuts up MSVC's complaints
		// about _wfopen() being unsafe..
		FILE* ret = NULL;
		if(_wfopen_s(&ret, nameW, modeW) == 0)
			return ret;
	}
	return NULL;
}

#else

#define fopen_utf8 fopen

#endif

// W_OpenWadFile
// Helper function for opening the WAD file.
// Returns the FILE * handle for the file, or NULL if not found or could not be opened
// If "useerrors" is true then print errors in the console, else just don't bother
// "filename" may be modified to have the correct path the actual file is located in, if necessary
FILE *W_OpenWadFile(const char **filename, boolean useerrors)
{
	FILE *handle;

	if (filename == NULL)
		return NULL;

	// Officially, strncpy should not have overlapping buffers, since W_VerifyNMUSlumps is called after this, and it
	// changes filename to point at filenamebuf, it would technically be doing that. I doubt any issue will occur since
	// they point to the same location, but it's better to be safe and this is a simple change.
	if (filenamebuf != *filename)
	{
		strncpy(filenamebuf, *filename, MAX_WADPATH);
		filenamebuf[MAX_WADPATH - 1] = '\0';
		*filename = filenamebuf;
	}

	// open wad file
	if ((handle = fopen_utf8(*filename, "rb")) == NULL)
	{
		// If we failed to load the file with the path as specified by
		// the user, strip the directories and search for the file.
		nameonly(filenamebuf);

		// If findfile finds the file, the full path will be returned
		// in filenamebuf == *filename.
		if (findfile(filenamebuf, 0, true))
		{
			if ((handle = fopen_utf8(*filename, "rb")) == NULL)
			{
				if (useerrors)
					CONS_Alert(CONS_ERROR, M_GetText("Can't open %s\n"), *filename);
				return NULL;
			}
		}
		else
		{
			if (useerrors)
				CONS_Alert(CONS_ERROR, M_GetText("File %s not found.\n"), *filename);
			return NULL;
		}
	}
	return handle;
}

// Look for all DEHACKED and Lua scripts inside a PK3 archive.
static inline void W_LoadDehackedLumpsPK3(UINT16 wadnum, boolean mainfile)
{
	UINT16 posStart, posEnd;

	posStart = W_CheckNumForFullNamePK3("Init.lua", wadnum, 0);
	if (posStart != INT16_MAX)
	{
		LUA_LoadLump(wadnum, posStart, true);
	}
	else
	{
		posStart = W_CheckNumForFolderStartPK3("Lua/", wadnum, 0);
		if (posStart != INT16_MAX)
		{
			posEnd = W_CheckNumForFolderEndPK3("Lua/", wadnum, posStart);
			for (; posStart < posEnd; posStart++)
				LUA_LoadLump(wadnum, posStart, true);
		}
	}

	posStart = W_CheckNumForFolderStartPK3("SOC/", wadnum, 0);
	if (posStart != INT16_MAX)
	{
		posEnd = W_CheckNumForFolderEndPK3("SOC/", wadnum, posStart);

		for(; posStart < posEnd; posStart++)
		{
			lumpinfo_t *lump_p = &wadfiles[wadnum]->lumpinfo[posStart];
			size_t length = strlen(wadfiles[wadnum]->filename) + 1 + strlen(lump_p->fullname); // length of file name, '|', and lump name
			char *name = (char *)malloc(length + 1);

			if (!name)
				I_Error("W_LoadDehackedLumpsPK3: Out of memory!\n");

			sprintf(name, "%s|%s", wadfiles[wadnum]->filename, lump_p->fullname);
			name[length] = '\0';
			CONS_Printf(M_GetText("Loading SOC from %s\n"), name);
			DEH_LoadDehackedLumpPwad(wadnum, posStart, mainfile);
			free(name);
		}
	}
}

// search for all DEHACKED lump in all wads and load it
static inline void W_LoadDehackedLumps(UINT16 wadnum, boolean mainfile)
{
	UINT16 lump;

	// Find Lua scripts before SOCs to allow new A_Actions in SOC editing.
	{
		lumpinfo_t *lump_p = wadfiles[wadnum]->lumpinfo;
		for (lump = 0; lump < wadfiles[wadnum]->numlumps; lump++, lump_p++)
			if (memcmp(lump_p->name,"LUA_",4)==0)
				LUA_LoadLump(wadnum, lump, true);
	}

	{
		lumpinfo_t *lump_p = wadfiles[wadnum]->lumpinfo;
		for (lump = 0; lump < wadfiles[wadnum]->numlumps; lump++, lump_p++)
			if (memcmp(lump_p->name,"SOC_",4)==0) // Check for generic SOC lump
			{	// shameless copy+paste of code from LUA_LoadLump
				size_t length = strlen(wadfiles[wadnum]->filename) + 1 + strlen(lump_p->fullname); // length of file name, '|', and lump name
				char *name = (char *)malloc(length + 1);

				if (!name)
					I_Error("W_LoadDehackedLumps: Out of memory!\n");

				sprintf(name, "%s|%s", wadfiles[wadnum]->filename, lump_p->fullname);
				name[length] = '\0';

				CONS_Printf(M_GetText("Loading SOC from %s\n"), name);
				DEH_LoadDehackedLumpPwad(wadnum, lump, mainfile);
				free(name);
			}
			else if (memcmp(lump_p->name,"MAINCFG",8)==0) // Check for MAINCFG
			{
				CONS_Printf(M_GetText("Loading main config from %s\n"), wadfiles[wadnum]->filename);
				DEH_LoadDehackedLumpPwad(wadnum, lump, mainfile);
			}
			else if (memcmp(lump_p->name,"OBJCTCFG",8)==0) // Check for OBJCTCFG
			{
				CONS_Printf(M_GetText("Loading object config from %s\n"), wadfiles[wadnum]->filename);
				DEH_LoadDehackedLumpPwad(wadnum, lump, mainfile);
			}
	}
}

static inline boolean CheckCompatFilename(const char *filename)
{
	// check for a K prefix
	char *basename = (char *)strrchr(filename, *PATHSEP);
	return (toupper(basename ? basename[1] : filename[0]) == 'K');
}

static inline boolean CheckCompatExtension(const char *filename)
{
	char *basename = (char *)strrchr(filename, '.');

	if (fasticmp(basename+1, "KART"))
		return true;

	return false;
}

static inline boolean CheckCompatSkins(UINT16 wadnum)
{
#if 0
	UINT16 skin = W_CheckNumForNamePwad("S_SKIN", wadnum, 0);
	if (skin == INT16_MAX)
		return false;

	// valid kart skins can only have one sprname for all frames
	const char *firstsprname = W_CheckNameForNumPwad(wadnum, ++skin);
	int maxframe = 0;

	while (true)
	{
		const char *sprname = W_CheckNameForNumPwad(wadnum, ++skin);
		if (!sprname)
			break;
		if (memcmp(sprname, firstsprname, 4))
			break;
		maxframe = max(maxframe, sprname[4]);
	}

	// how many frames does this first sprite name have?
	// if there's at least 19 frames, this is a valid kart skin,
	// so we need compatmode to load skins from this file!
	return (maxframe - 'A') >= 18;
#else
	(void)wadnum;
	return false;
#endif
}

// try to figure out if a PK3 file needs compatmode enabled (palette remap)
static inline boolean CheckCompatZip(UINT16 wadnum)
{
	if (CheckCompatFilename(wadfiles[wadnum]->filename) || CheckCompatSkins(wadnum))
		return true;

	// assume RR/2.2 for PK3s so basegame assets work
	return false;
}

// ditto, but for WADs
static inline boolean CheckCompatWad(UINT16 wadnum)
{
	if (CheckCompatFilename(wadfiles[wadnum]->filename) || CheckCompatSkins(wadnum)
		|| CheckCompatExtension(wadfiles[wadnum]->filename))
		return true;

	return false;
}

// adapted from the example in xxhash.h
static UINT64 hashfile(FILE *f)
{
	static XXH3_state_t *state;
	char buffer[4096];
	size_t count;

	if (!state)
		state = XXH3_createState();

	XXH3_64bits_reset(state);

	while ((count = fread(buffer, 1, sizeof(buffer), f)) != 0)
		XXH3_64bits_update(state, buffer, count);

	return XXH3_64bits_digest(state);
}

/** Computes and returns the hash for a file.
  *
  * \param filename path of file
  * \param openwad true if W_OpenWadFile should be used instead of fopen
  * \param ret pointer to UINT64 to store hash
  * \return true on success, false on error
  */
boolean W_MakeFileHash(const char *filename, boolean openwad, UINT64 *ret)
{
#ifdef NOFILEHASH
	(void)filename;
	(void)openwad;
	*ret = 0;
	return true;
#else
	FILE *fhandle = openwad ? W_OpenWadFile(&filename, true) : fopen(filename, "rb");

	if (fhandle == NULL)
	{
		CONS_Alert(CONS_WARNING, "Could not open %s for hashing\n", filename);
		return false;
	}

	CONS_Debug(DBG_SETUP, "Hashing file %s\n", filename);
	*ret = hashfile(fhandle);
	fclose(fhandle);
	return true;
#endif
}

// Invalidates the cache of lump numbers. Call this whenever a wad is added.
FUNCINLINE static ATTRINLINE void W_InvalidateLumpnumCache(void)
{
	lumpnum_map_clear(&lumpnumcache);
}

// This is unfornately duped in command.c since I want this inlined...
FUNCINLINE ATTRINLINE UINT32 W_HashLumpName(const char *name, size_t len)
{
	char *uname = (char *)alloca(len + 1);
	strlcpy(uname, name, len + 1);
	strlwr(uname);
	return HASH32(uname, len);
}

/** Detect a file type.
 * \todo Actually detect the wad/pkzip headers and whatnot, instead of just checking the extensions.
 */
static restype_t ResourceFileDetect (const char* filename)
{
	if (fasticmp(&filename[strlen(filename) - 4], ".pk3"))
		return RET_PK3;
	if (fasticmp(&filename[strlen(filename) - 4], ".soc"))
		return RET_SOC;
	if (fasticmp(&filename[strlen(filename) - 4], ".lua"))
		return RET_LUA;

	return RET_WAD;
}

/** Create a 1-lump lumpinfo_t for standalone files.
 */
static lumpinfo_t* ResGetLumpsStandalone (FILE* handle, UINT16* numlumps, const char* lumpname)
{
	lumpinfo_t* lumpinfo = (lumpinfo_t *)Z_Calloc(sizeof (*lumpinfo), PU_STATIC, NULL);
	lumpinfo->position = 0;
	fseek(handle, 0, SEEK_END);
	lumpinfo->size = ftell(handle);
	fseek(handle, 0, SEEK_SET);
	strlcpy(lumpinfo->name, lumpname, sizeof(lumpinfo->name));
	lumpinfo->namelength = strlen(lumpinfo->name);
	lumpinfo->hash.name = W_HashLumpName(lumpname, lumpinfo->namelength);

	// Allocate the lump's long name.
	lumpinfo->longname = (char *)Z_Malloc((lumpinfo->namelength + 1) * sizeof(char), PU_STATIC, NULL);
	strcpy(lumpinfo->longname, lumpname);
	lumpinfo->longname[lumpinfo->namelength] = '\0';
	lumpinfo->longnamelength = lumpinfo->namelength;
	lumpinfo->hash.longname = lumpinfo->hash.name;

	// Allocate the lump's full name.
	lumpinfo->fullname = (char *)Z_Malloc((lumpinfo->namelength + 1) * sizeof(char), PU_STATIC, NULL);
	strcpy(lumpinfo->fullname, lumpname);
	lumpinfo->fullname[lumpinfo->namelength] = '\0';
	lumpinfo->fullnamelength = lumpinfo->namelength;
	lumpinfo->hash.fullname = lumpinfo->hash.name;

	*numlumps = 1;
	return lumpinfo;
}

/** Create a lumpinfo_t array for a WAD file.
 */
static lumpinfo_t* ResGetLumpsWad (FILE* handle, UINT16* nlmp, const char* filename)
{
	UINT16 numlumps = *nlmp;
	lumpinfo_t* lumpinfo;
	size_t i;
	INT32 compressed = 0;

	wadinfo_t header;
	lumpinfo_t *lump_p;
	filelump_t *fileinfo;
	void *fileinfov;

	// read the header
	if (fread(&header, 1, sizeof header, handle) < sizeof header)
	{
		CONS_Alert(CONS_ERROR, M_GetText("Can't read wad header because %s\n"), M_FileError(handle));
		return NULL;
	}

	if (memcmp(header.identification, "ZWAD", 4) == 0)
		compressed = 1;
	else if (memcmp(header.identification, "IWAD", 4) != 0
		&& memcmp(header.identification, "PWAD", 4) != 0
		&& memcmp(header.identification, "SDLL", 4) != 0)
	{
		CONS_Alert(CONS_ERROR, M_GetText("Invalid WAD header\n"));
		return NULL;
	}

	header.numlumps = LONG(header.numlumps);
	header.infotableofs = LONG(header.infotableofs);

	// read wad file directory
	i = header.numlumps * sizeof (*fileinfo);
	fileinfov = fileinfo = (filelump_t *)malloc(i);
	if (fseek(handle, header.infotableofs, SEEK_SET) == -1
		|| fread(fileinfo, 1, i, handle) < i)
	{
		CONS_Alert(CONS_ERROR, M_GetText("Corrupt wadfile directory (%s)\n"), M_FileError(handle));
		free(fileinfov);
		return NULL;
	}

	numlumps = header.numlumps;

	// fill in lumpinfo for this wad
	lump_p = lumpinfo = (lumpinfo_t *)Z_Malloc(numlumps * sizeof (*lumpinfo), PU_STATIC, NULL);
	for (i = 0; i < numlumps; i++, lump_p++, fileinfo++)
	{
		lump_p->position = LONG(fileinfo->filepos);
		lump_p->size = lump_p->disksize = LONG(fileinfo->size);
		if (compressed) // wad is compressed, lump might be
		{
			UINT32 realsize = 0;
			if (fseek(handle, lump_p->position, SEEK_SET)
				== -1 || fread(&realsize, 1, sizeof realsize,
				handle) < sizeof realsize)
			{
				I_Error("corrupt compressed file: %s; maybe %s", /// \todo Avoid the bailout?
					filename, M_FileError(handle));
			}
			realsize = LONG(realsize);
			if (realsize != 0)
			{
				lump_p->size = realsize;
				lump_p->compression = CM_LZF;
			}
			else
			{
				lump_p->size -= 4;
				lump_p->compression = CM_NOCOMPRESSION;
			}

			lump_p->position += 4;
			lump_p->disksize -= 4;
		}
		else
			lump_p->compression = CM_NOCOMPRESSION;

		memset(lump_p->name, 0x00, 9);
		strncpy(lump_p->name, fileinfo->name, 8);

		if (WADNAMECHECK(fileinfo->name))
		{
			size_t namelen;
			const char *trimname, *dotpos;

			trimname = strrchr(filename, PATHSEP[0]);
#if (defined (_WIN32)) || (defined(__linux__))
			// For Zone Builder support, work around temporary filenames.
			// They're annoyingly randomised, BUT they follow \Temp\8\8.3...
			// AND they're always guaranteed to follow the map file, which
			// should already have a WADNAME in it for us to piggyback off.
			// WINDOWS EXAMPLE: // \Temp\gj3l7w7n\4f926789.wad
			// LINUX EXAMPLE:   // /tmp/p68uf2zt/65wav2y1.wad

			if (trimname != 0
				&& wadnamelump != LUMPERROR
				&& strlen(trimname+1) == 8+1+3)
			{
				const char *temp = trimname-1;
				// CONS_Printf("wadnumtemp: %s\n", temp);
				while (temp >= filename+5 && *temp != PATHSEP[0])
					temp--;

				if (((trimname-1) - temp) == 8
					&& temp >= filename+5
					&& (!strncmp(temp-5, PATHSEP"Temp", 5)
					|| !strncmp(temp-4, PATHSEP"tmp", 4)))
				{
					filename = wadfiles[
						((wadnamelump & ~UINT16_MAX) >> 16)
						]->filename;
					trimname = strrchr(filename, PATHSEP[0]);
					// CONS_Printf("wadnumfilename: %s\n", filename);
					// CONS_Printf("wadnumtrimname: %s\n", trimname);
				}
			}
#endif
			// Strip away file address
			if (trimname != 0)
				trimname++;
			else
				trimname = filename; // Care taken for root files.

			// First stop, not last, to permit RR_GREENHILLS.beta3.wad
			if ((dotpos = strchr(trimname, '.')) != 0)
				namelen = (dotpos + 1 - trimname);
			else
				namelen = strlen(trimname);

			// Allocate the lump's long and full name (save on memory).
			lump_p->longname = lump_p->fullname = (char*)Z_Calloc(namelen * sizeof(char), PU_STATIC, NULL);
			strncpy(lump_p->longname, trimname, namelen);
			lump_p->longname[namelen-1] = '\0';
			lump_p->hash.longname = lump_p->hash.fullname = W_HashLumpName(lump_p->longname, strlen(lump_p->longname));

			CONS_Debug(DBG_SETUP, "WADNAME handling:\n -- path %s\n -- interpreted lumpname %s\n", filename, lump_p->longname);

			// Grab the hash from the first part
			lump_p->hash.name = W_HashLumpName(lump_p->longname, 8);

			wadnamelump = i | (numwadfiles << 16);
		}
		else
		{
			lump_p->namelength = strlen(lump_p->name);
			lump_p->hash.name = W_HashLumpName(lump_p->name, lump_p->namelength);

			// Allocate the lump's long name.
			lump_p->longname = (char *)Z_Malloc(9 * sizeof(char), PU_STATIC, NULL);
			strncpy(lump_p->longname, fileinfo->name, 8);
			lump_p->longname[8] = '\0';
			lump_p->longnamelength = lump_p->namelength;
			lump_p->hash.longname = lump_p->hash.name;

			// Allocate the lump's full name.
			lump_p->fullname = (char *)Z_Malloc(9 * sizeof(char), PU_STATIC, NULL);
			strncpy(lump_p->fullname, fileinfo->name, 8);
			lump_p->fullname[8] = '\0';
			lump_p->fullnamelength = lump_p->namelength;
			lump_p->hash.fullname = lump_p->hash.name;
		}
	}
	free(fileinfov);
	*nlmp = numlumps;
	return lumpinfo;
}

/** Optimized pattern search in a file.
 */
static boolean ResFindSignature (FILE* handle, char endPat[], UINT32 startpos)
{
	char *s;
	int c;

	fseek(handle, startpos, SEEK_SET);
	s = endPat;
	while((c = fgetc(handle)) != EOF)
	{
		if (*s != c && s > endPat) // No match?
			s = endPat; // We "reset" the counter by sending the s pointer back to the start of the array.
		if (*s == c)
		{
			s++;
			if (*s == 0x00) // The array pointer has reached the key char which marks the end. It means we have matched the signature.
			{
				return true;
			}
		}
	}
	return false;
}

typedef struct zend_s
{
	char signature[4];
	UINT16 diskpos;
	UINT16 cdirdisk;
	UINT16 diskentries;
	UINT16 entries;
	UINT32 cdirsize;
	UINT32 cdiroffset;
	UINT16 commentlen;
} ATTRPACK zend_t;

typedef struct zentry_s
{
	char signature[4];
	UINT16 version;
	UINT16 versionneeded;
	UINT16 flags;
	UINT16 compression;
	UINT16 modtime;
	UINT16 moddate;
	UINT32 CRC32;
	UINT32 compsize;
	UINT32 size;
	UINT16 namelen;
	UINT16 xtralen;
	UINT16 commlen;
	UINT16 diskstart;
	UINT16 attrint;
	UINT32 attrext;
	UINT32 offset;
} ATTRPACK zentry_t;

typedef struct zlentry_s
{
	char signature[4];
	UINT16 versionneeded;
	UINT16 flags;
	UINT16 compression;
	UINT16 modtime;
	UINT16 moddate;
	UINT32 CRC32;
	UINT32 compsize;
	UINT32 size;
	UINT16 namelen;
	UINT16 xtralen;
} ATTRPACK zlentry_t;

/** Create a lumpinfo_t array for a PKZip file.
 */
static lumpinfo_t* ResGetLumpsZip (FILE* handle, UINT16* nlmp)
{
    zend_t zend;
    zentry_t zentry;
    zlentry_t zlentry;

	UINT16 numlumps = *nlmp;
	lumpinfo_t* lumpinfo;
	lumpinfo_t *lump_p;
	size_t i;

	char pat_central[] = {0x50, 0x4b, 0x01, 0x02, 0x00};
	char pat_end[] = {0x50, 0x4b, 0x05, 0x06, 0x00};

	// Look for central directory end signature near end of file.
	// Contains entry number (number of lumps), and central directory start offset.
	fseek(handle, 0, SEEK_END);
	if (!ResFindSignature(handle, pat_end, (long)max(0, ftell(handle) - (22 + 65536))))
	{
		CONS_Alert(CONS_ERROR, "Missing central directory\n");
		return NULL;
	}

	fseek(handle, -4, SEEK_CUR);
	if (fread(&zend, 1, sizeof zend, handle) < sizeof zend)
	{
		CONS_Alert(CONS_ERROR, "Corrupt central directory (%s)\n", M_FileError(handle));
		return NULL;
	}
	numlumps = zend.entries;

	lump_p = lumpinfo = (lumpinfo_t *)Z_Malloc(numlumps * sizeof (*lumpinfo), PU_STATIC, NULL);

	fseek(handle, zend.cdiroffset, SEEK_SET);
	for (i = 0; i < numlumps; i++, lump_p++)
	{
		char* fullname;
		char* trimname;
		char* dotpos;

		if (fread(&zentry, 1, sizeof(zentry_t), handle) < sizeof(zentry_t))
		{
			CONS_Alert(CONS_ERROR, "Failed to read central directory (%s)\n", M_FileError(handle));
			Z_Free(lumpinfo);
			return NULL;
		}
		if (memcmp(zentry.signature, pat_central, 4))
		{
			CONS_Alert(CONS_ERROR, "Central directory is corrupt\n");
			Z_Free(lumpinfo);
			return NULL;
		}

		lump_p->position = zentry.offset; // NOT ACCURATE YET: we still need to read the local entry to find our true position
		lump_p->disksize = zentry.compsize;
		lump_p->size = zentry.size;

		fullname = (char *)malloc(zentry.namelen + 1);

		if (fullname == NULL)
			I_Error("ResGetLumpsZip: Out of memory!\n");

		if (fgets(fullname, zentry.namelen + 1, handle) != fullname)
		{
			CONS_Alert(CONS_ERROR, "Unable to read lumpname (%s)\n", M_FileError(handle));
			Z_Free(lumpinfo);
			free(fullname);
			return NULL;
		}

		// Strip away file address and extension for the 8char name.
		if ((trimname = strrchr(fullname, '/')) != 0)
			trimname++;
		else
			trimname = fullname; // Care taken for root files.

		if ((dotpos = strrchr(trimname, '.')) == 0)
			dotpos = fullname + strlen(fullname); // Watch for files without extension.

		memset(lump_p->name, '\0', 9); // Making sure they're initialized to 0. Is it necessary?
		strncpy(lump_p->name, trimname, (size_t)min(8, dotpos - trimname));
		lump_p->namelength = strlen(lump_p->name);
		lump_p->hash.name = W_HashLumpName(lump_p->name, lump_p->namelength);

		lump_p->longname = (char *)Z_Calloc(dotpos - trimname + 1, PU_STATIC, NULL);
		strlcpy(lump_p->longname, trimname, dotpos - trimname + 1);
		lump_p->longnamelength = strlen(lump_p->longname);
		lump_p->hash.longname = W_HashLumpName(lump_p->longname, lump_p->longnamelength);

		lump_p->fullname = (char *)Z_Calloc(zentry.namelen + 1, PU_STATIC, NULL);
		strncpy(lump_p->fullname, fullname, zentry.namelen);
		lump_p->fullnamelength = zentry.namelen;
		lump_p->hash.fullname = W_HashLumpName(lump_p->fullname, lump_p->fullnamelength);

		switch(zentry.compression)
		{
		case 0:
			lump_p->compression = CM_NOCOMPRESSION;
			break;
#ifdef HAVE_ZLIB
		case 8:
			lump_p->compression = CM_DEFLATE;
			break;
#endif
		case 14:
			lump_p->compression = CM_LZF;
			break;
		default:
			CONS_Alert(CONS_WARNING, "%s: Unsupported compression method\n", fullname);
			lump_p->compression = CM_UNSUPPORTED;
			break;
		}

		free(fullname);

		// skip and ignore comments/extra fields
		if (fseek(handle, zentry.xtralen + zentry.commlen, SEEK_CUR) != 0)
		{
			CONS_Alert(CONS_ERROR, "Central directory is corrupt\n");
			Z_Free(lumpinfo);
			return NULL;
		}
	}

	// Adjust lump position values properly
	for (i = 0, lump_p = lumpinfo; i < numlumps; i++, lump_p++)
	{
		// skip and ignore comments/extra fields
		if ((fseek(handle, lump_p->position, SEEK_SET) != 0) || (fread(&zlentry, 1, sizeof(zlentry_t), handle) < sizeof(zlentry_t)))
		{
			CONS_Alert(CONS_ERROR, "Local headers for lump %s are corrupt\n", lump_p->fullname);
			Z_Free(lumpinfo);
			return NULL;
		}

		lump_p->position += sizeof(zlentry_t) + zlentry.namelen + zlentry.xtralen;
	}

	*nlmp = numlumps;
	return lumpinfo;
}

static UINT16 W_InitFileError (const char *filename, boolean exitworthy)
{
	if (exitworthy)
	{
#ifdef _DEBUG
		CONS_Error(va("%s was not found or not valid.\nCheck the log for more details.\n", filename));
#else
		I_Error("%s was not found or not valid.\nCheck the log for more details.\n", filename);
#endif
	}
	else
		CONS_Printf(M_GetText("Errors occurred while loading %s; not added.\n"), filename);
	return INT16_MAX;
}

//  Allocate a wadfile, setup the lumpinfo (directory) and
//  lumpcache, add the wadfile to the current active wadfiles
//
//  now returns index into wadfiles[], you can get wadfile_t *
//  with:
//       wadfiles[<return value>]
//
//  return -1 in case of problem
//
// Can now load dehacked files (.soc)
//
UINT16 W_InitFile(const char *filename, boolean mainfile, boolean startup, boolean local, wadcompat_t compat)
{
	FILE *handle;
	lumpinfo_t *lumpinfo = NULL;
	wadfile_t *wadfile;
	restype_t type;
	UINT16 numlumps = 0;
	UINT64 filehash = 0;
	int important;

	if (!(refreshdirmenu & REFRESHDIR_ADDFILE))
		refreshdirmenu = REFRESHDIR_NORMAL|REFRESHDIR_ADDFILE; // clean out cons_alerts that happened earlier

	if (refreshdirname)
		Z_Free(refreshdirname);
	if (dirmenu)
	{
		refreshdirname = Z_StrDup(filename);
		nameonly(refreshdirname);
	}
	else
		refreshdirname = NULL;

	CONS_Printf("Loading %s\n", filename);
	//
	// check if limit of active wadfiles
	//
	if (numwadfiles >= MAX_WADFILES)
	{
		CONS_Alert(CONS_ERROR, M_GetText("Maximum wad files reached\n"));
		refreshdirmenu |= REFRESHDIR_MAX;
		return W_InitFileError(filename, startup);
	}

	// open wad file
	if ((handle = W_OpenWadFile(&filename, true)) == NULL)
		return W_InitFileError(filename, startup);

	if (local)
		important = false;
	else
	{
		important = W_VerifyNMUSlumps(filename, startup);

		if (important == -1)
		{
			fclose(handle);
			return INT16_MAX;
		}

		important = !important;
	}

#ifndef NOFILEHASH
	//
	// w-waiiiit!
	// Let's not add a wad file if the hash matches
	// the hash of an already added WAD file!
	//
	if (!W_MakeFileHash(filename, false, &filehash))
		filehash = 0;

	for (size_t i = 0; i < numwadfiles; i++)
	{
		if (wadfiles[i]->hash == filehash)
		{
			CONS_Alert(CONS_ERROR, M_GetText("%s is already loaded\n"), filename);
			if (handle)
				fclose(handle);
			return W_InitFileError(filename, false);
		}
	}
#endif

	switch(type = ResourceFileDetect(filename))
	{
	case RET_SOC:
		lumpinfo = ResGetLumpsStandalone(handle, &numlumps, "OBJCTCFG");
		break;
	case RET_LUA:
		lumpinfo = ResGetLumpsStandalone(handle, &numlumps, "LUA_INIT");
		break;
	case RET_PK3:
		lumpinfo = ResGetLumpsZip(handle, &numlumps);
		break;
	case RET_WAD:
		lumpinfo = ResGetLumpsWad(handle, &numlumps, filename);
		break;
	default:
		CONS_Alert(CONS_ERROR, "Unsupported file format\n");
	}

	if (lumpinfo == NULL)
	{
		fclose(handle);
		return W_InitFileError(filename, startup);
	}

	if (important && !mainfile)
	{
		//G_SetGameModified(true);
		modifiedgame = true; // avoid savemoddata being set to false
	}

	//
	// link wad file to search files
	//
	wadfile = (wadfile_t *)Z_Malloc(sizeof (*wadfile), PU_STATIC, NULL);
	wadfile->filename = Z_StrDup(filename);
	wadfile->type = type;
	wadfile->handle = handle;
	wadfile->numlumps = (UINT16)numlumps;
	wadfile->lumpinfo = lumpinfo;
	wadfile->important = important;
	fseek(handle, 0, SEEK_END);
	wadfile->filesize = (unsigned)ftell(handle);
	wadfile->type = type;
	wadfile->startfolders = (FolderCache *)malloc(sizeof(FolderCache));
	folder_map_init(&wadfile->startfolders->map);
	wadfile->endfolders = (FolderCache *)malloc(sizeof(FolderCache));
	folder_map_init(&wadfile->endfolders->map);

	// already generated, just copy it over
	wadfile->hash = filehash;

	//
	// set up caching
	//
	Z_Calloc(numlumps * sizeof (*wadfile->lumpcache), PU_STATIC, &wadfile->lumpcache);
	Z_Calloc(numlumps * sizeof (*wadfile->patchcache), PU_STATIC, &wadfile->patchcache);

#ifdef ROTSPRITE
	Z_Calloc(numlumps * sizeof (*wadfile->rotcache), PU_STATIC, &wadfile->rotcache);
#endif

	//
	// add the wadfile
	//
	CONS_Printf(M_GetText("Added file %s (%u lumps)\n"), filename, numlumps);
	wadfiles[numwadfiles] = wadfile;
	numwadfiles++; // must come BEFORE W_LoadDehackedLumps, so any addfile called by COM_BufInsertText called by Lua doesn't overwrite what we just loaded

#ifdef HWRENDER
	// Read shaders from file
	if (rendermode == render_opengl && (vid.glstate == VID_GL_LIBRARY_LOADED))
		HWR_LoadCustomShadersFromFile(numwadfiles - 1, (type == RET_PK3), false);
#endif // HWRENDER

	// check if compatmode is needed
	if (compat != WC_AUTO)
		wadfile->compatmode = compat == WC_ON ? true : false;
	else switch (wadfile->type)
	{
	case RET_WAD:
		wadfile->compatmode = CheckCompatWad(numwadfiles - 1);
		break;
	case RET_PK3:
		wadfile->compatmode = CheckCompatZip(numwadfiles - 1);
		break;
	default:
		wadfile->compatmode = CheckCompatFilename(filename);
		break;
	}

	// TODO: HACK ALERT - Load Lua & SOC stuff right here. I feel like this should be out of this place, but... Let's stick with this for now.
	switch (wadfile->type)
	{
	case RET_WAD:
		W_LoadDehackedLumps(numwadfiles - 1, mainfile);
		break;
	case RET_PK3:
		W_LoadDehackedLumpsPK3(numwadfiles - 1, mainfile);
		break;
	case RET_SOC:
		CONS_Printf(M_GetText("Loading SOC from %s\n"), wadfile->filename);
		DEH_LoadDehackedLumpPwad(numwadfiles - 1, 0, mainfile);
		break;
	case RET_LUA:
		LUA_LoadLump(numwadfiles - 1, 0, true);
		break;
	default:
		break;
	}

	if (refreshdirmenu & REFRESHDIR_GAMEDATA)
		G_LoadGameData();
	DEH_UpdateMaxFreeslots();

	lua_lumploading++;
	LUA_HookVoid(HOOK(AddonLoaded));
	lua_lumploading--;

	W_InvalidateLumpnumCache();
	return wadfile->numlumps;
}

/** Tries to load a series of files.
  * All files are wads unless they have an extension of ".soc" or ".lua".
  *
  * Each file is optional, but at least one file must be found or an error will
  * result. Lump names can appear multiple times. The name searcher looks
  * backwards, so a later file overrides all earlier ones.
  *
  * \param filenames A null-terminated list of files to use.
  */
INT32 W_InitMultipleFiles(char **filenames, boolean addons)
{
	INT32 rc = 1;
	INT32 overallrc = 1;

	// will be realloced as lumps are added
	for (; *filenames; filenames++)
	{
		if (addons && !W_VerifyNMUSlumps(*filenames, !addons))
			G_SetGameModified(true, false);

		//CONS_Debug(DBG_SETUP, "Loading %s\n", *filenames);
		rc = W_InitFile(*filenames, !addons, true, false, WC_AUTO);
		if (rc == INT16_MAX)
			CONS_Printf(M_GetText("Errors occurred while loading %s; not added.\n"), *filenames);
		overallrc &= (rc != INT16_MAX) ? 1 : 0;
	}

	if (!numwadfiles)
		I_Error("W_InitMultipleFiles: no files found");

	return overallrc;
}

/** Make sure a lump number is valid.
  * Compiles away to nothing if PARANOIA is not defined.
  */
static boolean TestValidLump(UINT16 wad, UINT16 lump)
{
	I_Assert(wad < MAX_WADFILES);
	if (!wadfiles[wad]) // make sure the wad file exists
		return false;

	I_Assert(lump < wadfiles[wad]->numlumps);
	if (lump >= wadfiles[wad]->numlumps) // make sure the lump exists
		return false;

	return true;
}


const char *W_CheckNameForNumPwad(UINT16 wad, UINT16 lump)
{
	if (lump >= wadfiles[wad]->numlumps || !TestValidLump(wad, 0))
		return NULL;

	return wadfiles[wad]->lumpinfo[lump].name;
}

const char *W_CheckNameForNum(lumpnum_t lumpnum)
{
	return W_CheckNameForNumPwad(WADFILENUM(lumpnum),LUMPNUM(lumpnum));
}

static const char *W_CheckFullNameForNumPwad(UINT16 wad, UINT16 lump)
{
	if (lump >= wadfiles[wad]->numlumps || !TestValidLump(wad, 0))
		return NULL;

	return wadfiles[wad]->lumpinfo[lump].fullname;
}

static const char *W_CheckFullNameForNum(lumpnum_t lumpnum)
{
	return W_CheckFullNameForNumPwad(WADFILENUM(lumpnum),LUMPNUM(lumpnum));
}

//
// wadid is a wad number
// (Used for sprites loading)
//
// 'startlump' is the lump number to start the search
//
UINT16 W_FindNextEmptyInPwad(UINT16 wad, UINT16 startlump)
{
	UINT16 i;

	if (!TestValidLump(wad,0))
		return INT16_MAX;

	//
	// scan forward
	// start at 'startlump', useful parameter when there are multiple
	//                       resources with the same name
	//
	if (startlump < wadfiles[wad]->numlumps)
	{
		lumpinfo_t *lump_p = wadfiles[wad]->lumpinfo + startlump;
		for (i = startlump; i < wadfiles[wad]->numlumps; i++, lump_p++)
			if (!lump_p->size)
				return i;
	}

	// not found.
	return INT16_MAX;
}

// Get a map marker for WADs, and a standalone WAD file lump inside PK3s.
UINT16 W_CheckNumForMapPwad(const char *name, UINT32 hash, UINT16 wad, UINT16 startlump)
{
	UINT16 i, end;
	boolean compatload = false;

	if (wadfiles[wad]->compatmode)
	{
		// Compatibility mode: "blind idiot" trace to the first map header/WAD
		compatload = true;
	}

	if (wadfiles[wad]->type == RET_WAD)
	{
		for (i = startlump; i < wadfiles[wad]->numlumps; i++)
		{
			// Not the hash?
			if ((wadfiles[wad]->lumpinfo + i)->hash.longname != hash)
				continue;

			// Not the name? (always use longname, even in wads, to accomodate WADNAME)
			if (strcasecmp(name, (wadfiles[wad]->lumpinfo + i)->longname))
				continue;

			// Not a header?
			if (W_LumpLength(i | (wad << 16)) > 0)
				continue;

			return i;
		}
	}
	else if (wadfiles[wad]->type == RET_PK3)
	{
		i = W_CheckNumForFolderStartPK3("maps/", wad, startlump);

		if (i != INT16_MAX)
		{
			end = W_CheckNumForFolderEndPK3("maps/", wad, i);

			// Now look for the specified map.
			for (; i < end; i++)
			{
				// Not the hash?
				if ((wadfiles[wad]->lumpinfo + i)->hash.longname != hash)
					continue;

				// Not the name?
				if (strcasecmp(name, (wadfiles[wad]->lumpinfo + i)->longname))
					continue;

#if 0
				// Not a .wad?
				if (!W_IsLumpWad(i | (wad << 16)))
					continue;
#endif

				return i;
			}
		}
		else if (compatload)
		{
			for (i = startlump; i < wadfiles[wad]->numlumps; i++)
			{
				// Not the hash?
				if ((wadfiles[wad]->lumpinfo + i)->hash.longname != hash)
					continue;

				// Not the name? (always use longname, even in wads, to accomodate WADNAME)
				if (strcasecmp(name, (wadfiles[wad]->lumpinfo + i)->longname))
					continue;

				if (W_LumpLength(i | (wad << 16)) > 0)
				{
					// Not a header? Maybe it's a WAD file...

					if (!W_IsLumpWad(i | (wad << 16)))
					{
						// ...no dice?
						continue;
					}
				}

				// Found a map!
				return i;
			}
		}
	}

	return INT16_MAX;
}

//
// Same as the original, but checks in one pwad only.
// wadid is a wad number
// (Used for sprites loading)
//
// 'startlump' is the lump number to start the search
//
UINT16 W_CheckNumForNamePwad(const char *name, UINT16 wad, UINT16 startlump)
{
	UINT16 i;
	UINT32 hash;
	size_t namelen;

	if (!TestValidLump(wad, 0))
		return INT16_MAX;

	// Name = SHORTNAME. Truncate/move the string to something that has a max of 8 characters.
	// Will this cause lumpname/lumphash collisions? Of course it will! But we need it for
	// SRB2Kart compatibility, so...
	namelen = min(strlen(name), 8);

	hash = W_HashLumpName(name, namelen); // Not a mistake, legacy system for short lumpnames

	//
	// scan forward
	// start at 'startlump', useful parameter when there are multiple
	//                       resources with the same name
	//
	if (startlump < wadfiles[wad]->numlumps)
	{
		lumpinfo_t *lump_p = wadfiles[wad]->lumpinfo + startlump;
		for (i = startlump; i < wadfiles[wad]->numlumps; i++, lump_p++)
		{
			if (lump_p->namelength != namelen)
				continue;
			if (lump_p->hash.name != hash)
				continue;
			if (strncasecmp(lump_p->name, name, namelen))
				continue;
			return i;
		}
	}

	// not found.
	return INT16_MAX;
}

//
// Like W_CheckNumForNamePwad, but can find entries with long names
//
// Should be the only version, but that's not possible until we fix
// all the instances of non null-terminated strings in the codebase...
//
UINT16 W_CheckNumForLongNamePwad(const char *name, UINT16 wad, UINT16 startlump)
{
	UINT16 i;
	UINT32 hash;
	size_t namelen;

	if (!TestValidLump(wad, 0))
		return INT16_MAX;

	namelen = strlen(name);
	hash = W_HashLumpName(name, namelen); // Not a mistake, legacy system for short lumpnames

	//
	// scan forward
	// start at 'startlump', useful parameter when there are multiple
	//                       resources with the same name
	//
	if (startlump < wadfiles[wad]->numlumps)
	{
		lumpinfo_t *lump_p = wadfiles[wad]->lumpinfo + startlump;
		for (i = startlump; i < wadfiles[wad]->numlumps; i++, lump_p++)
		{
			if (lump_p->longnamelength != namelen)
				continue;
			if (lump_p->hash.longname != hash)
				continue;
			if (strcasecmp(lump_p->longname, name))
				continue;
			return i;
		}
	}

	// not found.
	return INT16_MAX;
}

UINT16
W_CheckNumForMarkerStartPwad (const char *name, UINT16 wad, UINT16 startlump)
{
	UINT16 marker;
	marker = W_CheckNumForNamePwad(name, wad, startlump);
	if (marker != INT16_MAX)
		marker++; // Do not count the first marker
	return marker;
}

// Look for the first lump from a folder.
UINT16 W_CheckNumForFolderStartPK3(const char *name, UINT16 wad, UINT16 startlump)
{
	UINT16 i;
	size_t namelen;
	lumpinfo_t *lump_p = wadfiles[wad]->lumpinfo + startlump;

	folder_map_itr it = folder_map_get(&wadfiles[wad]->startfolders->map, name);
	if (!folder_map_is_end(it))
		return it.data->val;

	namelen = strlen(name);

	for (i = startlump; i < wadfiles[wad]->numlumps; i++, lump_p++)
	{
		if (strnicmp(name, lump_p->fullname, namelen) == 0)
		{
			/* SLADE is special and puts a single directory entry. Skip that. */
			if (lump_p->fullnamelength == namelen)
				i++;

			folder_map_insert(&wadfiles[wad]->startfolders->map, strdup(name), i);
			return i;
		}
	}

	folder_map_insert(&wadfiles[wad]->startfolders->map, strdup(name), INT16_MAX);
	return INT16_MAX;
}

// In a PK3 type of resource file, it looks for the next lumpinfo entry that doesn't share the specified pathfile.
// Useful for finding folder ends.
// Returns the position of the lumpinfo entry.
UINT16 W_CheckNumForFolderEndPK3(const char *name, UINT16 wad, UINT16 startlump)
{
	UINT16 i;
	size_t namelen;
	lumpinfo_t *lump_p = wadfiles[wad]->lumpinfo + startlump;

	if (name == NULL)
		return INT16_MAX;

	folder_map_itr it = folder_map_get(&wadfiles[wad]->endfolders->map, name);
	if (!folder_map_is_end(it))
		return it.data->val;

	namelen = strlen(name);

	for (i = startlump; i < wadfiles[wad]->numlumps; i++, lump_p++)
	{
		if (strnicmp(name, lump_p->fullname, namelen))
			break;
	}

	folder_map_insert(&wadfiles[wad]->endfolders->map, strdup(name), i);
	return i;
}

// In a PK3 type of resource file, it looks for an entry with the specified full name.
// Returns lump position in PK3's lumpinfo, or INT16_MAX if not found.
UINT16 W_CheckNumForFullNamePK3(const char *name, UINT16 wad, UINT16 startlump)
{
	INT32 i;
	lumpinfo_t *lump_p = wadfiles[wad]->lumpinfo + startlump;
	for (i = startlump; i < wadfiles[wad]->numlumps; i++, lump_p++)
	{
		if (!strnicmp(name, lump_p->fullname, strlen(name)))
		{
			return i;
		}
	}
	// Not found at all?
	return INT16_MAX;
}

FUNCINLINE static ATTRINLINE lumpnum_t CheckLumpInCache(const char *name)
{
	lumpnum_map_itr it = lumpnum_map_get(&lumpnumcache, name);
	if (!lumpnum_map_is_end(it))
		return it.data->val;

	return LUMPERROR;
}

FUNCINLINE static ATTRINLINE void AddLumpToCache(lumpnum_t lumpnum, const char *name)
{
	lumpnum_map_insert(&lumpnumcache, strdup(name), lumpnum);
}

//
// W_CheckNumForName
// Returns LUMPERROR if name not found.
//
lumpnum_t W_CheckNumForName(const char *name)
{
	INT32 i;
	lumpnum_t check = INT16_MAX;

	if (name == NULL)
		return LUMPERROR;

	if (!*name) // some doofus gave us an empty string?
		return LUMPERROR;

	// Check the lumpnumcache first.
	lumpnum_t cachenum = CheckLumpInCache(name);
	if (cachenum != LUMPERROR)
		return cachenum;

	// scan wad files backwards so patch lump files take precedence
	for (i = numwadfiles - 1; i >= 0; i--)
	{
		check = W_CheckNumForNamePwad(name, (UINT16)i, 0);
		if (check != INT16_MAX)
			break; //found it
	}

	if (check == INT16_MAX)
	{
		return LUMPERROR;
	}
	else
	{
		// Update the cache.
		lumpnum_t lumpnum = (i << 16) | check;
		AddLumpToCache(lumpnum, name);

		return lumpnum;
	}
}

//
// Like W_CheckNumForName, but can find entries with long names
//
// Should be the only version, but that's not possible until we fix
// all the instances of non null-terminated strings in the codebase...
//
lumpnum_t W_CheckNumForLongName(const char *name)
{
	INT32 i;
	lumpnum_t check = INT16_MAX;

	if (name == NULL)
		return LUMPERROR;

	if (!*name) // some doofus gave us an empty string?
		return LUMPERROR;

	// Check the lumpnumcache first. Loop backwards so that we check
	// most recent entries first
	lumpnum_t cachenum = CheckLumpInCache(name);
	if (cachenum != LUMPERROR)
		return cachenum;

	// scan wad files backwards so patch lump files take precedence
	for (i = numwadfiles - 1; i >= 0; i--)
	{
		check = W_CheckNumForLongNamePwad(name,(UINT16)i,0);
		if (check != INT16_MAX)
			break; //found it
	}

	if (check == INT16_MAX) return LUMPERROR;
	else
	{
		if (strlen(name) < 32)
		{
			// Update the cache.
			lumpnum_t lumpnum = (i << 16) | check;
			AddLumpToCache(lumpnum, name);

			return lumpnum;
		}

		return (i << 16) | check;
	}
}

// Look for valid map data through all added files in descendant order.
// Get a map marker for WADs, and a standalone WAD file lump inside PK3s.
lumpnum_t W_CheckNumForMap(const char *name, boolean checktofirst)
{
	lumpnum_t check = INT16_MAX;
	UINT32 uhash;
	INT32 i;
	UINT16 firstfile = (checktofirst || (partadd_earliestfile == UINT16_MAX)) ? 0 : partadd_earliestfile;

	// Check the lumpnumcache first. Loop backwards so that we check
	// most recent entries first
	lumpnum_t cachenum = CheckLumpInCache(name);
	if (cachenum != LUMPERROR)
		return cachenum;

	uhash = W_HashLumpName(name, strlen(name)); // Not a mistake, legacy system for short lumpnames

	for (i = numwadfiles - 1; i >= firstfile; i--)
	{
		check = W_CheckNumForMapPwad(name, uhash, (UINT16)i, 0);

		if (check != INT16_MAX)
			break; // found it
	}

	if (check == INT16_MAX)
	{
		return LUMPERROR;
	}
	else
	{
		if (strlen(name) < LUMPNUMCACHENAME)
		{
			// Update the cache.
			lumpnum_t lumpnum = (i << 16) | check;
			AddLumpToCache(lumpnum, name);

			return lumpnum;
		}

		return (i << 16) | check;
	}
}

//
// W_GetNumForName
//
// Calls W_CheckNumForName, but bombs out if not found.
//
lumpnum_t W_GetNumForName(const char *name)
{
	lumpnum_t i;

	i = W_CheckNumForName(name);

	if (i == LUMPERROR)
		I_Error("W_GetNumForName: %s not found!\n", name);

	return i;
}

//
// Like W_GetNumForName, but can find entries with long names
//
// Should be the only version, but that's not possible until we fix
// all the instances of non null-terminated strings in the codebase...
//
lumpnum_t W_GetNumForLongName(const char *name)
{
	lumpnum_t i;

	i = W_CheckNumForLongName(name);

	if (i == LUMPERROR)
		I_Error("W_GetNumForLongName: %s not found!\n", name);

	return i;
}

//
// W_CheckNumForNameInBlock
// Checks only in blocks from blockstart lump to blockend lump
//
lumpnum_t W_CheckNumForNameInBlock(const char *name, const char *blockstart, const char *blockend)
{
	INT32 i;
	lumpnum_t bsid, beid;
	lumpnum_t check = INT16_MAX;

	// scan wad files backwards so patch lump files take precedence
	for (i = numwadfiles - 1; i >= 0; i--)
	{
		if (wadfiles[i]->type == RET_WAD)
		{
			bsid = W_CheckNumForNamePwad(blockstart, (UINT16)i, 0);
			if (bsid == INT16_MAX)
				continue; // Start block doesn't exist?
			beid = W_CheckNumForNamePwad(blockend, (UINT16)i, 0);
			if (beid == INT16_MAX)
				continue; // End block doesn't exist?

			check = W_CheckNumForNamePwad(name, (UINT16)i, bsid);
			if (check < beid)
				return (i<<16)+check; // found it, in our constraints
		}
	}
	return LUMPERROR;
}

//
// W_CheckNumForNameInFolder
// Checks only in PK3s in the specified folder
//
lumpnum_t W_CheckNumForNameInFolder(const char *lump, const char *folder)
{
	INT32 i;
	lumpnum_t fsid, feid;
	lumpnum_t check = INT16_MAX;

	// scan wad files backwards so patch lump files take precedence
	for (i = numwadfiles - 1; i >= 0; i--)
	{
		if (wadfiles[i]->type == RET_PK3)
		{
			fsid = W_CheckNumForFolderStartPK3(folder, (UINT16)i, 0);
			if (fsid == INT16_MAX)
			{
				continue; // Start doesn't exist?
			}

			feid = W_CheckNumForFolderEndPK3(folder, (UINT16)i, fsid);
			if (feid == INT16_MAX)
			{
				continue; // End doesn't exist?
			}

			check = W_CheckNumForLongNamePwad(lump, (UINT16)i, fsid);
			if (check < feid)
			{
				return (i<<16) | check; // found it, in our constraints
			}
		}
	}

	return LUMPERROR;
}

// Used by Lua. Case sensitive lump checking, quickly...
UINT8 W_LumpExists(const char *name)
{
	INT32 i, j;
	UINT32 hash;
	size_t namelen;

	// Check the lumpnumcache first.
	lumpnum_t cachenum = CheckLumpInCache(name);
	if (cachenum != LUMPERROR)
		return true;

	namelen = strlen(name);
	hash = W_HashLumpName(name, namelen);

	for (i = numwadfiles - 1; i >= 0; i--)
	{
		lumpinfo_t *lump_p = wadfiles[i]->lumpinfo;
		for (j = 0; j < wadfiles[i]->numlumps; ++j, ++lump_p)
		{
			if (lump_p->longnamelength == namelen
			 && lump_p->hash.longname == hash
			 && fastcmp(lump_p->longname, name))
				return true;
		}
	}
	return false;
}

size_t W_LumpLengthPwad(UINT16 wad, UINT16 lump)
{
	if (!TestValidLump(wad, lump))
		return 0;
	return wadfiles[wad]->lumpinfo[lump].size;
}

/** Returns the buffer size needed to load the given lump.
  *
  * \param lump Lump number to look at.
  * \return Buffer size needed, in bytes.
  */
size_t W_LumpLength(lumpnum_t lumpnum)
{
	return W_LumpLengthPwad(WADFILENUM(lumpnum),LUMPNUM(lumpnum));
}

//
// W_IsLumpWad
// Is the lump a WAD? (presumably in a PK3)
//
boolean W_IsLumpWad(lumpnum_t lumpnum)
{
	if (wadfiles[WADFILENUM(lumpnum)]->type == RET_PK3)
	{
		const char *lumpfullName = (wadfiles[WADFILENUM(lumpnum)]->lumpinfo + LUMPNUM(lumpnum))->fullname;

		if (strlen(lumpfullName) < 4)
			return false; // can't possibly be a WAD can it?
		return !strnicmp(lumpfullName + strlen(lumpfullName) - 4, ".wad", 4);
	}

	return false; // WADs should never be inside non-PK3s as far as SRB2 is concerned
}

//
// W_IsLumpFolder
// Is the lump a folder? (in a PK3 obviously)
//
boolean W_IsLumpFolder(UINT16 wad, UINT16 lump)
{
	if (wadfiles[wad]->type == RET_PK3)
	{
		const char *name = wadfiles[wad]->lumpinfo[lump].fullname;

		return (name[strlen(name)-1] == '/'); // folders end in '/'
	}

	return false; // non-PK3s don't have folders
}

#ifdef HAVE_ZLIB
/* report a zlib or i/o error */
void zerr(int ret)
{
    CONS_Printf("zpipe: ");
    switch (ret) {
    case Z_ERRNO:
        if (ferror(stdin))
            CONS_Printf("error reading stdin\n");
        if (ferror(stdout))
            CONS_Printf("error writing stdout\n");
        break;
    case Z_STREAM_ERROR:
        CONS_Printf("invalid compression level\n");
        break;
    case Z_DATA_ERROR:
        CONS_Printf("invalid or incomplete deflate data\n");
        break;
    case Z_MEM_ERROR:
        CONS_Printf("out of memory\n");
        break;
    case Z_VERSION_ERROR:
        CONS_Printf("zlib version mismatch!\n");
    }
}
#endif

// returns true if the given lump is not cached, AND comes from a compatmode file (AND isn't special-cased)
boolean W_NeedPaletteRemapPwad(UINT16 wad, UINT16 lump, boolean ignorecache)
{
	if (!wadfiles[wad]->lumpcache[lump] || ignorecache)
	{
		// ugh, palette textures...
		// never do remapping on them; PaletteTextureHack handles that
		unsigned num;
		const char *name = (wadfiles[wad]->lumpinfo + lump)->longname;
		if (sscanf(name, "~%03u", &num) && num < 256)
			return false;
		else
			return wadfiles[wad]->compatmode;
	}
	return false;
}

boolean W_NeedPaletteRemap(lumpnum_t lump, boolean ignorecache)
{
	return W_NeedPaletteRemapPwad(WADFILENUM(lump), LUMPNUM(lump), ignorecache);
}

/** Reads bytes from the head of a lump.
  * Note: If the lump is compressed, the whole thing has to be read anyway.
  *
  * \param wad Wad number to read from.
  * \param lump Lump number to read from.
  * \param dest Buffer in memory to serve as destination.
  * \param size Number of bytes to read.
  * \param offest Number of bytes to offset.
  * \return Number of bytes read (should equal size).
  * \sa W_ReadLump, W_RawReadLumpHeader
  */
size_t W_ReadLumpHeaderPwad(UINT16 wad, UINT16 lump, void *dest, size_t size, size_t offset)
{
	size_t lumpsize;
	lumpinfo_t *l;
	FILE *handle;

	if (!TestValidLump(wad,lump))
		return 0;

	lumpsize = wadfiles[wad]->lumpinfo[lump].size;
	// empty resource (usually markers like S_START, F_END ..)
	if (!lumpsize || lumpsize<offset)
		return 0;

	// zero size means read all the lump
	if (!size || size+offset > lumpsize)
		size = lumpsize - offset;

	// Let's get the raw lump data.
	// We setup the desired file handle to read the lump data.
	l = wadfiles[wad]->lumpinfo + lump;
	handle = wadfiles[wad]->handle;
	fseek(handle, (long)(l->position + offset), SEEK_SET);

	// But let's not copy it yet. We support different compression formats on lumps, so we need to take that into account.
	switch(wadfiles[wad]->lumpinfo[lump].compression)
	{
	case CM_NOCOMPRESSION:		// If it's uncompressed, we directly write the data into our destination, and return the bytes read.
#ifdef NO_PNG_LUMPS
		{
			size_t bytesread = fread(dest, 1, size, handle);
			if (Picture_IsLumpPNG((UINT8 *)dest, bytesread))
				Picture_ThrowPNGError(l->fullname, wadfiles[wad]->filename);
			return bytesread;
		}
#else
		return fread(dest, 1, size, handle);
#endif
	case CM_LZF:		// Is it LZF compressed? Used by ZWADs.
		{
#ifdef ZWAD
			char *rawData; // The lump's raw data.
			char *decData; // Lump's decompressed real data.
			size_t retval; // Helper var, lzf_decompress returns 0 when an error occurs.

			rawData = (char *)Z_Malloc(l->disksize, PU_STATIC, NULL);
			decData = (char *)Z_Malloc(l->size, PU_STATIC, NULL);

			if (fread(rawData, 1, l->disksize, handle) < l->disksize)
				I_Error("wad %d, lump %d: cannot read compressed data", wad, lump);
			retval = lzf_decompress(rawData, l->disksize, decData, l->size);
#ifndef AVOID_ERRNO
			if (retval == 0) // If this was returned, check if errno was set
			{
				// errno is a global var set by the lzf functions when something goes wrong.
				if (errno == E2BIG)
					I_Error("wad %d, lump %d: compressed data too big (bigger than %s)", wad, lump, sizeu1(l->size));
				else if (errno == EINVAL)
					I_Error("wad %d, lump %d: invalid compressed data", wad, lump);
			}
			// Otherwise, fall back on below error (if zero was actually the correct size then ???)
#endif
			if (retval != l->size)
			{
				I_Error("wad %d, lump %d: decompressed to wrong number of bytes (expected %s, got %s)", wad, lump, sizeu1(l->size), sizeu2(retval));
			}

			if (!decData) // Did we get no data at all?
				return 0;
			memcpy(dest, decData + offset, size);
			Z_Free(rawData);
			Z_Free(decData);
#ifdef NO_PNG_LUMPS
			if (Picture_IsLumpPNG((UINT8 *)dest, size))
				Picture_ThrowPNGError(l->fullname, wadfiles[wad]->filename);
#endif
			return size;
#else
			//I_Error("ZWAD files not supported on this platform.");
			return 0;
#endif

		}
#ifdef HAVE_ZLIB
	case CM_DEFLATE: // Is it compressed via DEFLATE? Very common in ZIPs/PK3s, also what most doom-related editors support.
		{
			UINT8 *rawData; // The lump's raw data.
			UINT8 *decData; // Lump's decompressed real data.

			int zErr; // Helper var.
			z_stream strm;
			unsigned long rawSize = l->disksize;
			unsigned long decSize = size;

			rawData = (UINT8 *)Z_Malloc(rawSize, PU_STATIC, NULL);
			decData = (UINT8 *)dest;

			if (fread(rawData, 1, rawSize, handle) < rawSize)
				I_Error("wad %d, lump %d: cannot read compressed data", wad, lump);

			strm.zalloc = Z_NULL;
			strm.zfree = Z_NULL;
			strm.opaque = Z_NULL;

			strm.total_in = strm.avail_in = rawSize;
			strm.total_out = strm.avail_out = decSize;

			strm.next_in = rawData;
			strm.next_out = decData;

			zErr = inflateInit2(&strm, -15);
			if (zErr == Z_OK)
			{
				zErr = inflate(&strm, Z_SYNC_FLUSH);
				if (zErr != Z_OK && zErr != Z_STREAM_END)
				{
					size = 0;
					zerr(zErr);
				}
				(void)inflateEnd(&strm);
			}
			else
			{
				size = 0;
				zerr(zErr);
			}

			Z_Free(rawData);

#ifdef NO_PNG_LUMPS
			if (Picture_IsLumpPNG((UINT8 *)dest, size))
				Picture_ThrowPNGError(l->fullname, wadfiles[wad]->filename);
#endif
			return size;
		}
#endif
	default:
		I_Error("wad %d, lump %d: unsupported compression type!", wad, lump);
	}
	return 0;
}

size_t W_ReadLumpHeader(lumpnum_t lumpnum, void *dest, size_t size, size_t offset)
{
	return W_ReadLumpHeaderPwad(WADFILENUM(lumpnum), LUMPNUM(lumpnum), dest, size, offset);
}

/** Reads a lump into memory.
  *
  * \param lump Lump number to read from.
  * \param dest Buffer in memory to serve as destination. Size must be >=
  *             W_LumpLength().
  * \sa W_ReadLumpHeader
  */
void W_ReadLump(lumpnum_t lumpnum, void *dest)
{
	W_ReadLumpHeaderPwad(WADFILENUM(lumpnum),LUMPNUM(lumpnum),dest,0,0);
}

void W_ReadLumpPwad(UINT16 wad, UINT16 lump, void *dest)
{
	W_ReadLumpHeaderPwad(wad, lump, dest, 0, 0);
}

// ==========================================================================
// W_CacheLumpNum
// ==========================================================================
void *W_CacheLumpNumPwad(UINT16 wad, UINT16 lump, INT32 tag)
{
	lumpcache_t *lumpcache;

	if (!TestValidLump(wad,lump))
		return NULL;

	lumpcache = wadfiles[wad]->lumpcache;
	if (!lumpcache[lump])
	{
		void *ptr = Z_Malloc(W_LumpLengthPwad(wad, lump), tag, &lumpcache[lump]);
		W_ReadLumpHeaderPwad(wad, lump, ptr, 0, 0);  // read the lump in full
	}
	else
		Z_ChangeTag(lumpcache[lump], tag);

	return lumpcache[lump];
}

void *W_CacheLumpNum(lumpnum_t lumpnum, INT32 tag)
{
	return W_CacheLumpNumPwad(WADFILENUM(lumpnum),LUMPNUM(lumpnum),tag);
}

//
// W_CacheLumpNumForce
//
// Forces the lump to be loaded, even if it already is!
//
void *W_CacheLumpNumForce(lumpnum_t lumpnum, INT32 tag)
{
	UINT16 wad, lump;
	void *ptr;

	wad = WADFILENUM(lumpnum);
	lump = LUMPNUM(lumpnum);

	if (!TestValidLump(wad,lump))
		return NULL;

	ptr = Z_Malloc(W_LumpLengthPwad(wad, lump), tag, NULL);
	W_ReadLumpHeaderPwad(wad, lump, ptr, 0, 0);  // read the lump in full

	return ptr;
}

//
// W_IsLumpCached
//
// If a lump is already cached return true, otherwise
// return false.
//
// no outside code uses the PWAD form, for now
static inline boolean W_IsLumpCachedPWAD(UINT16 wad, UINT16 lump, void *ptr)
{
	void *lcache;

	if (!TestValidLump(wad, lump))
		return false;

	lcache = wadfiles[wad]->lumpcache[lump];

	if (ptr)
	{
		if (ptr == lcache)
			return true;
	}
	else if (lcache)
		return true;

	return false;
}

boolean W_IsLumpCached(lumpnum_t lumpnum, void *ptr)
{
	return W_IsLumpCachedPWAD(WADFILENUM(lumpnum),LUMPNUM(lumpnum), ptr);
}

//
// W_IsPatchCached
//
// If a patch is already cached return true, otherwise
// return false.
//
// no outside code uses the PWAD form, for now
static inline boolean W_IsPatchCachedPWAD(UINT16 wad, UINT16 lump, void *ptr)
{
	void *lcache;

	if (!TestValidLump(wad, lump))
		return false;

	lcache = wadfiles[wad]->patchcache[lump];

	if (ptr)
	{
		if (ptr == lcache)
			return true;
	}
	else if (lcache)
		return true;

	return false;
}

boolean W_IsPatchCached(lumpnum_t lumpnum, void *ptr)
{
	return W_IsPatchCachedPWAD(WADFILENUM(lumpnum),LUMPNUM(lumpnum), ptr);
}

// ==========================================================================
// W_CacheLumpName
// ==========================================================================
void *W_CacheLumpName(const char *name, INT32 tag)
{
	return W_CacheLumpNum(W_GetNumForName(name), tag);
}

// ==========================================================================
//                                         CACHING OF GRAPHIC PATCH RESOURCES
// ==========================================================================

// Graphic 'patches' are loaded, and if necessary, converted into the format
// the most useful for the current rendermode. For software renderer, the
// graphic patches are kept as is. For the hardware renderer, graphic patches
// are 'unpacked', and are kept into the cache in that unpacked format, and
// the heap memory cache then acts as a 'level 2' cache just after the
// graphics card memory.

//
// Cache a patch into heap memory, convert the patch format as necessary
//

static void *MakePatch(void *lumpdata, size_t size, INT32 tag, void *cache, boolean remap)
{
	void *ptr, *dest;
	size_t len = size;

	ptr = lumpdata;

#ifndef NO_PNG_LUMPS
	if (Picture_IsLumpPNG((UINT8 *)lumpdata, len))
		ptr = Picture_PNGConvert((UINT8 *)lumpdata, PICFMT_DOOMPATCH, NULL, NULL, NULL, NULL, len, &len, (pictureflags_t)0);
#endif

	if (remap)
		R_DoPaletteRemapPatch((softwarepatch_t *)ptr, len);

	dest = Z_Calloc(sizeof(patch_t), tag, cache);

	Patch_Create((softwarepatch_t *)ptr, len, dest);

	return dest;
}

void *W_CacheSoftwarePatchNumPwad(UINT16 wad, UINT16 lump, INT32 tag)
{
	lumpcache_t *lumpcache = NULL;

	if (!TestValidLump(wad, lump))
		return NULL;

	lumpcache = wadfiles[wad]->patchcache;

	if (!lumpcache[lump])
	{
		size_t len = W_LumpLengthPwad(wad, lump);
		void *lumpdata = Z_Malloc(len, PU_STATIC, NULL);

		// read the lump in full
		W_ReadLumpHeaderPwad(wad, lump, lumpdata, 0, 0);

		if (!Picture_IsLumpPNG((UINT8 *)lumpdata, len) && !Picture_CheckIfDoomPatch((softwarepatch_t *)lumpdata, len))
			return NULL;

		MakePatch(lumpdata, len, tag, &lumpcache[lump], wadfiles[wad]->compatmode);
		Z_Free(lumpdata);
	}
	else
		Z_ChangeTag(lumpcache[lump], tag);

	return lumpcache[lump];
}

void *W_CacheSoftwarePatchNum(lumpnum_t lumpnum, INT32 tag)
{
	return W_CacheSoftwarePatchNumPwad(WADFILENUM(lumpnum),LUMPNUM(lumpnum),tag);
}

void *W_CachePatchNumPwad(UINT16 wad, UINT16 lump, INT32 tag)
{
	patch_t *patch;

	if (!TestValidLump(wad, lump))
		return NULL;

	patch = (patch_t *)W_CacheSoftwarePatchNumPwad(wad, lump, tag);

	if (!patch)
		return NULL;

#ifdef HWRENDER
	// Software-only compile cache the data without conversion
	if (rendermode == render_soft || rendermode == render_none)
#endif
		return (void *)patch;

#ifdef HWRENDER
	Patch_CreateGL(patch);
	return (void *)patch;
#endif
}

void *W_CachePatchNum(lumpnum_t lumpnum, INT32 tag)
{
	return W_CachePatchNumPwad(WADFILENUM(lumpnum),LUMPNUM(lumpnum),tag);
}

#ifdef ROTSPRITE
// Caches a rotsprite for patch rotation.
void *W_GetCachedRotPatchPwad(UINT16 wadnum, UINT16 lumpnum)
{
	lumpcache_t *rotcache = wadfiles[wadnum]->rotcache;

	if (!rotcache[lumpnum])
	{
		rotsprite_t *rspr = RotatedPatch_Create(ROTANGLES);
		Z_SetUser(rspr, (void **)(&rotcache[lumpnum]));
	}

	return (void *)(rotcache[lumpnum]);
}
#endif // ROTSPRITE

void W_UnlockCachedPatch(void *patch)
{
	if (!patch)
		return;

	// The hardware code does its own memory management, as its patches
	// have different lifetimes from software's.
#ifdef HWRENDER
	if (rendermode == render_opengl)
		HWR_UnlockCachedPatch((GLPatch_t *)((patch_t *)patch)->hardware);
	else
#endif
		Z_Unlock(patch);
}

void *W_CachePatchName(const char *name, INT32 tag)
{
	lumpnum_t num;

	num = W_CheckNumForName(name);

	if (num == LUMPERROR)
		return missingpat;
	return W_CachePatchNum(num, tag);
}

#ifdef ROTSPRITE
// Caches a patch, and if needed, rotates said patch.
void *W_CachePatchNameRotated(const char *name, INT32 rotationangle, INT32 tag)
{
	lumpnum_t num;
	patch_t *ptr;
	rotsprite_t *rspr;
	INT32 idx = rotationangle;

	num = W_CheckNumForName(name);

	if (num == LUMPERROR)
		num = W_GetNumForName("MISSING");

	// No rotation? No need to do any of this nonsense.
	if (rotationangle < 1 || rotationangle >= ROTANGLES)
		return W_CachePatchNum(num, tag);

	rspr = (rotsprite_t *)W_GetCachedRotPatchPwad(WADFILENUM(num),LUMPNUM(num));

	if (rspr == NULL)
		return missingpat;

	if (rspr->patches[idx] == NULL)
	{
		INT32 xpivot = 0, ypivot = 0;

		ptr = (patch_t *)W_CachePatchNum(num, PU_PATCH);
		if (ptr == NULL)
			return missingpat;

		// >y pivot centered
		// >x pivot not centered
		// Why?
		xpivot = ptr->width / 2;
		ypivot = ptr->height / 2;

		RotatedPatch_DoRotation(rspr, ptr, rotationangle, xpivot, ypivot, false);
	}

	return rspr->patches[idx];
}
#endif

void *W_CachePatchLongName(const char *name, INT32 tag)
{
	lumpnum_t num;

	num = W_CheckNumForLongName(name);

	if (num == LUMPERROR)
		return missingpat;
	return W_CachePatchNum(num, tag);
}

/** Verifies a file's hash is as it should be.
  * For releases, used as cheat prevention -- if the hash doesn't match, a
  * fatal error is thrown. In debug mode, a hash mismatch only triggers a
  * warning.
  *
  * \param wadfilenum Number of the loaded wad file to check.
  * \param matchhash  The hash value this wad should have.
  * \author Graue <graue@oceanbase.org>
  */
void W_VerifyFileHash(UINT16 wadfilenum, UINT64 matchhash)
{
#if defined(NOVERIFYIWADS) || defined(NOFILEHASH)
	(void)wadfilenum;
	(void)matchhash;
#else
	I_Assert(wadfilenum < numwadfiles);

	if (matchhash != wadfiles[wadfilenum]->hash)
	{
#ifdef _DEBUG
		CONS_Printf
#else
		I_Error
#endif
			(M_GetText("File is old, is corrupt or has been modified: %s (found hash: " HASHFMT ", wanted: " HASHFMT ")\n"),
			 wadfiles[wadfilenum]->filename, wadfiles[wadfilenum]->hash, matchhash);
	}
#endif
}

// Verify versions for different archive
// formats. checklist assumed to be valid.

static int
W_VerifyName (const char *name, lumpchecklist_t *checklist, boolean status)
{
	size_t j;
	for (j = 0; checklist[j].len && checklist[j].name; ++j)
	{
		if (( strncasecmp(name, checklist[j].name,
						checklist[j].len) != false ) == status)
		{
			return true;
		}
	}
	return false;
}

static int
W_VerifyWAD (FILE *fp, lumpchecklist_t *checklist, boolean status)
{
	size_t i;

	// assume wad file
	wadinfo_t header;
	filelump_t lumpinfo;

	// read the header
	if (fread(&header, 1, sizeof header, fp) == sizeof header
			&& header.numlumps < INT16_MAX
			&& strncmp(header.identification, "ZWAD", 4)
			&& strncmp(header.identification, "IWAD", 4)
			&& strncmp(header.identification, "PWAD", 4)
			&& strncmp(header.identification, "SDLL", 4))
	{
		return true;
	}

	header.numlumps = LONG(header.numlumps);
	header.infotableofs = LONG(header.infotableofs);

	// let seek to the lumpinfo list
	if (fseek(fp, header.infotableofs, SEEK_SET) == -1)
		return true;

	for (i = 0; i < header.numlumps; i++)
	{
		// fill in lumpinfo for this wad file directory
		if (fread(&lumpinfo, sizeof (lumpinfo), 1 , fp) != 1)
			return true;

		lumpinfo.filepos = LONG(lumpinfo.filepos);
		lumpinfo.size = LONG(lumpinfo.size);

		if (lumpinfo.size == 0)
			continue;

		if (! W_VerifyName(lumpinfo.name, checklist, status))
			return false;
	}

	return true;
}

// List of blacklisted folders to use when checking the PK3
static lumpchecklist_t folderblacklist[] =
{
	{"Lua/", 4},
	{"SOC/", 4},
	{"Sprites/",  8},
	{"Textures/", 9},
	{"Patches/", 8},
	{"Flats/", 6},
	{"Fades/", 6},
	{NULL, 0},
};

static int
W_VerifyPK3 (FILE *fp, lumpchecklist_t *checklist, boolean status)
{
	int verified = true;

    zend_t zend;
    zentry_t zentry;
    zlentry_t zlentry;

	long file_size;/* size of zip file */
	long data_size;/* size of data inside zip file */

	long old_position;

	UINT16 numlumps;
	size_t i;

	char pat_central[] = {0x50, 0x4b, 0x01, 0x02, 0x00};
	char pat_end[] = {0x50, 0x4b, 0x05, 0x06, 0x00};

	char lumpname[9];

	// Haha the ResGetLumpsZip function doesn't
	// check for file errors, so neither will I.

	// Central directory bullshit

	fseek(fp, 0, SEEK_END);
	file_size = ftell(fp);

	if (!ResFindSignature(fp, pat_end, (long)max(0, ftell(fp) - (22 + 65536))))
		return true;

	fseek(fp, -4, SEEK_CUR);
	if (fread(&zend, 1, sizeof zend, fp) < sizeof zend)
		return true;

	data_size = sizeof zend;

	numlumps = zend.entries;

	fseek(fp, zend.cdiroffset, SEEK_SET);
	for (i = 0; i < numlumps; i++)
	{
		char* fullname;
		char* trimname;
		char* dotpos;

		if (fread(&zentry, 1, sizeof(zentry_t), fp) < sizeof(zentry_t))
			return true;
		if (memcmp(zentry.signature, pat_central, 4))
			return true;

		if (verified == true)
		{
			fullname = (char *)malloc(zentry.namelen + 1);

			if (fullname == NULL)
				I_Error("W_VerifyPK3: No more free memory\n");

			if (fgets(fullname, zentry.namelen + 1, fp) != fullname)
				return true;

			// Strip away file address and extension for the 8char name.
			if ((trimname = strrchr(fullname, '/')) != 0)
				trimname++;
			else
				trimname = fullname; // Care taken for root files.

			if (*trimname) // Ignore directories, well kinda
			{
				if ((dotpos = strrchr(trimname, '.')) == 0)
					dotpos = fullname + strlen(fullname); // Watch for files without extension.

				memset(lumpname, '\0', 9); // Making sure they're initialized to 0. Is it necessary?
				strncpy(lumpname, trimname,(size_t)min(8, dotpos - trimname));

				if (! W_VerifyName(lumpname, checklist, status))
					verified = false;

				// Check for directories next, if it's blacklisted it will return false
				else if (W_VerifyName(fullname, folderblacklist, status))
					verified = false;
			}

			free(fullname);

			// skip and ignore comments/extra fields
			if (fseek(fp, zentry.xtralen + zentry.commlen, SEEK_CUR) != 0)
				return true;
		}
		else
		{
			if (fseek(fp, zentry.namelen + zentry.xtralen + zentry.commlen, SEEK_CUR) != 0)
				return true;
		}

		data_size +=
			sizeof zentry + zentry.namelen + zentry.xtralen + zentry.commlen;

		old_position = ftell(fp);

		if (fseek(fp, zentry.offset, SEEK_SET) != 0)
			return true;

		if (fread(&zlentry, 1, sizeof(zlentry_t), fp) < sizeof (zlentry_t))
			return true;

		data_size +=
			sizeof zlentry + zlentry.namelen + zlentry.xtralen + zlentry.compsize;

		fseek(fp, old_position, SEEK_SET);
	}

	if (data_size < file_size)
	{
		const char * error = "ZIP file has holes (%ld extra bytes)\n";
		CONS_Alert(CONS_ERROR, error, (file_size - data_size));
		return -1;
	}
	else if (data_size > file_size)
	{
		const char * error = "Reported size of ZIP file contents exceeds file size (%ld extra bytes)\n";
		CONS_Alert(CONS_ERROR, error, (data_size - file_size));
		return -1;
	}
	else
	{
		return verified;
	}
}

// Note: This never opens lumps themselves and therefore doesn't have to
// deal with compressed lumps.
static int W_VerifyFile(const char *filename, lumpchecklist_t *checklist,
	boolean status)
{
	FILE *handle;
	int goodfile = false;

	if (!checklist)
		I_Error("No checklist for %s\n", filename);
	// open wad file
	if ((handle = W_OpenWadFile(&filename, false)) == NULL)
		return -1;

	if (fasticmp(&filename[strlen(filename) - 4], ".pk3"))
		goodfile = W_VerifyPK3(handle, checklist, status);
	else
	{
		// detect wad file by the absence of the other supported extensions
		if (!fasticmp(&filename[strlen(filename) - 4], ".soc")
		&& !fasticmp(&filename[strlen(filename) - 4], ".lua"))
		{
			goodfile = W_VerifyWAD(handle, checklist, status);
		}
	}
	fclose(handle);
	return goodfile;
}


/** Checks a wad for lumps other than music and sound.
  * Used during game load to verify music.dta is a good file and during a
  * netgame join (on the server side) to see if a wad is important enough to
  * be sent.
  *
  * \param filename Filename of the wad to check.
  * \param exit_on_error Whether to exit upon file error.
  * \return 1 if file contains only music/sound lumps, 0 if it contains other
  *         stuff (maps, sprites, dehacked lumps, and so on). -1 if there no
  *         file exists with that filename
  * \author Alam Arias
  */
int W_VerifyNMUSlumps(const char *filename, boolean exit_on_error)
{
	lumpchecklist_t NMUSlist[] =
	{
		{"D_", 2}, // MIDI music
		{"O_", 2}, // Digital music
		{"DS", 2}, // Sound effects

		{"ENDOOM", 6}, // ENDOOM text lump
		{"PLAYPAL", 7}, // Palette
		{"COLORMAP", 8}, // Colormap
		{"PAL", 3}, // Palette changes
		{"CLM", 3}, // Colormap changes
		{"TRANS", 5}, // Translucency map
		{"FADE", 4}, // Fades

		{"CONSBACK", 8}, // Console Background graphic

		{"SAVE", 4}, // Save Select graphics here and below
		{"BLACXLVL", 8},
		{"GAMEDONE", 8},
		{"CONT", 4}, // Continue icons on saves (probably not used anymore)
		{"STNONEX", 7}, // "X" graphic
		{"ULTIMATE", 8}, // Ultimate no-save

		{"CRFNT", 5}, // Sonic 1 font changes
		{"NTFNT", 5}, // Character Select font changes
		{"NTFNO", 5}, // Character Select font (outline)
		{"LTFNT", 5}, // Level title font changes
		{"TTL", 3}, // Act number changes
		{"STCFN", 5}, // Console font changes
		{"TNYFN", 5}, // Tiny console font changes

		{"STLIVE", 6}, // Life graphics, background and the "X" that shows under skin's HUDNAME
		{"CROSHAI", 7}, // First person crosshairs
		{"INTERSC", 7}, // Default intermission backgrounds (co-op)
		{"STT", 3}, // Acceptable HUD changes (Score Time Rings)
		{"YB_", 3}, // Intermission graphics, goes with the above
		{"M_", 2}, // As does menu stuff
		{"MKFNT", 5}, // Kart font changes
		{"K_", 2}, // Kart graphic changes
		{"MUSICDEF", 8}, // Kart song definitions
		{"SP_", 3}, // Speedometer changes do not count either.
		{"SC_", 3}, // Colored speedometer stuff too.
		{"SPRTINFO", 8}, // Sprite info
		{"MMAPDOT", 7},  // Minimap Dot
		{"MMAPHDLT", 8}, // Minimap Headlight
		{"JOYBCK", 6}, // Joystick graphic
		{"JOYKNB", 6}, // ^
		{"JOYSHD", 6}, // ^

#ifdef HWRENDER
		{"SHADERS", 7},
		{"SH_", 3},
#endif
		{NULL, 0},
	};

	int status = W_VerifyFile(filename, NMUSlist, false);

	if (status == -1)
		W_InitFileError(filename, exit_on_error);

	return status;
}

/** \brief Generates a virtual resource used for level data loading.
 *
 * \param lumpnum_t reference
 * \return Virtual resource
 *
 */
virtres_t* vres_GetMap(lumpnum_t lumpnum)
{
	UINT32 i;
	virtres_t* vres = NULL;
	virtlump_t* vlumps = NULL;
	size_t numlumps = 0;

	if (lastloadedmaplumpnum == lumpnum && curmapvirt != NULL)
	{
		// Avoid duplicating all our hard work.
		return curmapvirt;
	}

	if (W_IsLumpWad(lumpnum))
	{
		UINT32 realentry;
		size_t *vsizecache;

		// Remember that we're assuming that the WAD will have a specific set of lumps in a specific order.
		UINT8 *wadData = (UINT8 *)W_CacheLumpNum(lumpnum, PU_LEVEL);
		if (wadData == NULL)
			I_Error("vres_GetMap: Invalid WadData!\n");

		filelump_t *fileinfo = (filelump_t *)(wadData + ((wadinfo_t *)wadData)->infotableofs);

		i = ((wadinfo_t *)wadData)->numlumps;
		vsizecache = (size_t *)Z_Malloc(sizeof(size_t)*i, PU_LEVEL, NULL);

		for (realentry = 0; realentry < i; realentry++)
		{
			vsizecache[realentry] = (size_t)(((filelump_t *)(fileinfo + realentry))->size);

			if (!vsizecache[realentry])
				continue;

			numlumps++;
		}

		vlumps = (virtlump_t *)Z_Malloc(sizeof(virtlump_t)*numlumps, PU_LEVEL, NULL);

		if (vlumps == NULL)
			I_Error("vres_GetMap: Out of memory!\n");

		// Build the lumps, skipping over empty entries.
		for (i = 0, realentry = 0; i < numlumps; realentry++)
		{
			if (vsizecache[realentry] == 0)
				continue;
			vlumps[i].size = vsizecache[realentry];
			// Play it safe with the name in this case.
			memcpy(vlumps[i].name, (fileinfo + realentry)->name, 8);
			vlumps[i].name[8] = '\0';
			vlumps[i].data = (UINT8 *)Z_Malloc(vlumps[i].size, PU_LEVEL, NULL); // This is memory inefficient, sorry about that.
			memcpy(vlumps[i].data, wadData + (fileinfo + realentry)->filepos, vlumps[i].size);
			i++;
		}

		Z_Free(vsizecache);
		Z_Free(wadData);
	}
	else
	{
		// Count number of lumps until the end of resource OR up until next 0-length lump.
		lumpnum_t lumppos = lumpnum + 1;
		lumpnum_t lastlump = wadfiles[WADFILENUM(lumpnum)]->numlumps;
		// or the end of directory, for PK3 files
		if (wadfiles[WADFILENUM(lumpnum)]->type == RET_PK3)
		{
			const char *fullname = W_CheckFullNameForNum(lumpnum);
			const char *dirend = strrchr(fullname, '/');
			char *dirname = (char *)malloc(dirend - fullname + 2);
			strlcpy(dirname, fullname, dirend - fullname + 2);
			lastlump = W_CheckNumForFolderEndPK3(dirname, WADFILENUM(lumpnum), lumpnum);
			free(dirname);
		}
		for (i = LUMPNUM(lumppos); i < lastlump; i++, lumppos++, numlumps++)
		{
			if (W_LumpLength(lumppos) > 0)
				continue;

			break;
		}
		numlumps++;

		vlumps = (virtlump_t *)Z_Malloc(sizeof(virtlump_t)*numlumps, PU_LEVEL, NULL);
		if (vlumps == NULL)
			I_Error("vres_GetMap: Out of memory!\n");

		for (i = 0; i < numlumps; i++, lumpnum++)
		{
			vlumps[i].size = W_LumpLength(lumpnum);
			memcpy(vlumps[i].name, W_CheckNameForNum(lumpnum), 8);
			vlumps[i].name[8] = '\0';
			vlumps[i].data = (UINT8 *)W_CacheLumpNum(lumpnum, PU_LEVEL);
		}
	}
	vres = (virtres_t *)Z_Malloc(sizeof(virtres_t), PU_LEVEL, NULL);
	vres->vlumps = vlumps;
	vres->numlumps = numlumps;

	return vres;
}

/** \brief Frees zone memory for a given virtual resource.
 *
 * \param Virtual resource
 */
void vres_Free(virtres_t* vres)
{
	if (vres == curmapvirt)
	{
		// No-sell multiple references.
		return;
	}

	while (vres->numlumps--)
	{
		Z_Free(vres->vlumps[vres->numlumps].data);
	}
	Z_Free(vres->vlumps);
	Z_Free(vres);
}

/** (Debug) Prints lumps from a virtual resource into console.
 */
/*
static void vres_Diag(const virtres_t* vres)
{
	UINT32 i;
	for (i = 0; i < vres->numlumps; i++)
		CONS_Printf("%s\n", vres->vlumps[i].name);
}
*/

/** \brief Finds a lump in a given virtual resource.
 *
 * \param Virtual resource
 * \param Lump name to look for
 * \return Virtual lump if found, NULL otherwise
 *
 */
virtlump_t* vres_Find(const virtres_t* vres, const char* name)
{
	UINT32 i;
	for (i = 0; i < vres->numlumps; i++)
		if (fastcmp(name, vres->vlumps[i].name))
			return &vres->vlumps[i];
	return NULL;
}

/** \brief Gets patch from given virtual lump
 *
 * \param Virtual lump
 * \return Patch data
 *
 */
void *vres_GetPatch(virtlump_t *vlump, INT32 tag)
{
	patch_t *patch;

	if (!vlump)
		return NULL;

	patch = (patch_t *)MakePatch(vlump->data, vlump->size, tag, NULL, false);

#ifdef HWRENDER
	// Software-only compile cache the data without conversion
	if (rendermode == render_soft || rendermode == render_none)
#endif
		return (void *)patch;

#ifdef HWRENDER
	Patch_CreateGL(patch);
	return (void *)patch;
#endif
}
