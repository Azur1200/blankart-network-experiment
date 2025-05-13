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
char *strbuf_get(strbuf_t *strbuf, UINT32 ofs);

#ifdef __cplusplus
}
#endif
