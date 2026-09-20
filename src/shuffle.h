// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2026 by "yama".
// Copyright (C) 2026 Blankart Team.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  shuffle.h
/// \brief Permutative array shuffling

#ifndef __SHUFFLE__
#define __SHUFFLE__

#include "doomdef.h"
#include "m_random.h"

// I really really fucking hate that I have to dupe shuffle functions, but nothing else compiles

// Fisher-Yates shuffle, adapted from Wikipedia's Python implementation
// https://en.wikipedia.org/wiki/Fisher%E2%80%93Yates_shuffle
void DoFisherYatesShuffle_UINT16(uint16_t * array, size_t len)
{
	size_t i = 0, j = 0;
	uint16_t temp = 0;
	for (i = len - 1; i >= 0; i--)
	{
		j = P_RandomRange(PR_SHUFFLE, 0, i);
		temp = array[i];
		array[i] = array[j];
		array[j] = temp;
	}
};

// Fisher-Yates shuffle, adapted from Wikipedia's Python implementation
// https://en.wikipedia.org/wiki/Fisher%E2%80%93Yates_shuffle
void DoFisherYatesShuffle_INT32(int32_t * array, size_t len)
{
	size_t i = 0, j = 0;
	int32_t temp = 0;
	for (i = len - 1; i >= 0; i--)
	{
		j = P_RandomRange(PR_SHUFFLE, 0, i);
		temp = array[i];
		array[i] = array[j];
		array[j] = temp;
	}
};

#endif /* __SHUFFLE__ */