// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2026 by Team BlanKart.
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
	return strbuf_write(strbuf, (const UINT8 *)str, strlen(str)+1);
}

// writes a raw block of bytes to the buffer
// returns an offset to the newly appended bytes
UINT32 strbuf_write(strbuf_t **strbuf, const UINT8 *src, size_t len)
{
	strbuf_t *buf = *strbuf;
	UINT32 oldsize = buf->size;

	buf->size += len;
	if ((oldsize % STRBUF_BLOCKSIZE) + len >= STRBUF_BLOCKSIZE) {
		UINT32 newsize = buf->size + STRBUF_BLOCKSIZE - (buf->size % STRBUF_BLOCKSIZE);
		if (newsize <= oldsize)
			I_Error("String buffer size wrapped around!?");
		buf = *strbuf = Z_Realloc(buf, newsize, PU_STATIC, NULL);
		if (!buf)
			I_Error("Failed to allocate string buffer");
	}

	memcpy(buf->buf - sizeof(UINT32) + oldsize, src, len);
	return oldsize;
}
