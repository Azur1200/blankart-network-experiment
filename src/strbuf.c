// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2025 by Team BlanKart.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  strbuf.c
/// \brief string buffer library, for making chunks of many small strings

#include "doomdef.h"
#include "strbuf.h"
#include "z_zone.h"

#define STRBUF_BLOCKSIZE 16384 // 16K pages are the future, son!

// allocates and returns a new string buffer
// free with Z_Free
strbuf_t *strbuf_alloc(void)
{
	strbuf_t *buf = Z_Malloc(STRBUF_BLOCKSIZE, PU_STATIC, NULL);
	if (!buf)
		I_Error("Failed to allocate string buffer");
	buf->size = sizeof(UINT32);
	return buf;
}

// appends a string to the buffer
// returns an offset to the newly appended string
UINT32 strbuf_append(strbuf_t **strbuf, const char *str)
{
	strbuf_t *buf = *strbuf;
	size_t addbytes = strlen(str) + 1;
	UINT32 oldsize = buf->size;

	buf->size += addbytes;
	if ((oldsize % STRBUF_BLOCKSIZE) + addbytes >= STRBUF_BLOCKSIZE) {
		UINT32 newsize = buf->size + STRBUF_BLOCKSIZE - (buf->size % STRBUF_BLOCKSIZE);
		if (newsize <= oldsize)
			I_Error("String buffer size wrapped around!?");
		buf = *strbuf = Z_Realloc(buf, newsize, PU_STATIC, NULL);
		if (!buf)
			I_Error("Failed to allocate string buffer");
	}

	strcpy(buf->buf - sizeof(UINT32) + oldsize, str);
	return oldsize;
}

// returns the string at the given offset
char *strbuf_get(strbuf_t *strbuf, UINT32 ofs)
{
	return strbuf->buf - sizeof(UINT32) + ofs;
}
