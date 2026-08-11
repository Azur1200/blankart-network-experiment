// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2026 by Team BlanKart.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  strbuf.h
/// \brief string buffer library, for making chunks of many small strings

#ifndef __BK_STRBUF__
#define __BK_STRBUF__

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
UINT32 strbuf_write(strbuf_t **strbuf, const UINT8 *src, size_t len);

// returns the string at the given offset
// NOTE: use only with strbuf_append/strbuf_write, the offsets are relative to the address of the struct!
FUNCINLINE static ATTRINLINE char *strbuf_get(strbuf_t *strbuf, UINT32 ofs)
{
#ifdef __cplusplus
	return reinterpret_cast<char *>(strbuf) + ofs;
#else
	return (char *)strbuf + ofs;
#endif
}

// returns the byte at the index within the buffer
FUNCINLINE static ATTRINLINE char strbuf_byte(strbuf_t *strbuf, UINT32 ofs)
{
	return strbuf->buf[ofs];
}

// checks wheter two strbufs have the same contents (i.e. memcmp, but no length param)
FUNCINLINE static ATTRINLINE dboolean strbuf_cmp(strbuf_t *buf1, strbuf_t *buf2)
{
	return buf1->size == buf2->size && !memcmp(buf1->buf, buf2->buf, buf1->size);
}

// returns the length of the buffer
FUNCINLINE static ATTRINLINE UINT32 strbuf_len(strbuf_t *strbuf)
{
	return strbuf->size - sizeof(UINT32);
}

#ifdef __cplusplus
}
#endif

#endif // __BK_STRBUF__
