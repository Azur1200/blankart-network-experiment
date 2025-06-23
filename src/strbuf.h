// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2025 by Team BlanKart.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  strbuf.h
/// \brief string buffer library, for making chunks of many small strings

#ifndef __STRBUF_H__
#define __STRBUF_H__

#ifdef __cplusplus
extern "C" {
#endif

// for simplicity's sake, the header is included in the size (keeps the allocation aligned!)
struct strbuf_t
{
	UINT32 size;
	char buf[];
};

strbuf_t *strbuf_alloc(void);
UINT32 strbuf_append(strbuf_t **strbuf, const char *str);

// returns the string at the given offset
FUNCINLINE static ATTRINLINE char *strbuf_get(strbuf_t *strbuf, UINT32 ofs)
{
#ifdef __cplusplus
	return reinterpret_cast<char *>(strbuf) + ofs;
#else
	return (char *)strbuf + ofs;
#endif
}

#ifdef __cplusplus
}
#endif

#endif
