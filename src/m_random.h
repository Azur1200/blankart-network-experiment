// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 2012-2016 by Matthew "Kaito Sinclaire" Walsh.
// Copyright (C) 1999-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  m_random.h
/// \brief RNG for client effects and PRNG for game actions

#ifndef __M_RANDOM__
#define __M_RANDOM__

#include "doomtype.h"
#include "m_fixed.h"

#ifdef __cplusplus
extern "C" {
#endif

// The rule for RNG classes: Don't mix up gameplay & decorative RNG.
//
// Decorative RNG is a lot less important and can be lumped together. If it's used enough, it
// might be nice to give it's own, though.
//
// However each instance of RNG being used for gameplay should be split up as much as possible.
#define RNGSEEDS(X) \
	/* Gameplay */ \
	X(ACTION) /* Used in Action (A_) functions that rely on randomization */ \
	\
	X(SRB2KART) /* SRB2Kart/SRB2 object-specific RNG that neither belongs to an action nor has a specific purpose */ \
	X(THWOMP) /* URGH! */ \
	X(AUDIENCE) /* Audience behavior and colors */ \
	\
	X(BOSSBATTLE) /* Boss behavior */ \
	X(BRAKEGGMAN) /* Brak Eggman behavior */\
	\
	X(FALLINGROCK) /* Falling rock behavior */ \
	X(FLICKY) /* Flicky spawning */ \
	X(MOVINGTARGET) /* Randomised moving targets */ \
	X(LOOKFORPLAYERS) /* Player hunting */ \
	X(EXECUTOR) /* Linedef executor */ \
	X(ACS) /* ACS scripts */ \
	\
	X(PLAYERSTARTS) /* Player starts */ \
	X(RESTAT) /* Restat system */ \
	X(BOTS) \
	X(DEADFLING) \
	\
	X(RULESCRAMBLE) \
	\
	X(MUSICSELECT) \
	\
	X(ROCK_SPAWN) /* A_RockSpawn */ \
	\
	X(ITEM_ROULETTE) \
	X(ITEM_RINGS) \
	X(ITEM_BUBBLE) \
	X(ITEM_DEBRIS) \
	X(ITEM_BOOST) /* Boost */ \
	X(ITEM_SPAWNER) /* Battle mode item spawners */ \
	X(ITEM_CAPSULE) /* MT_ITEMCAPSULE specifics */ \
	X(ITEM_ATTRACT) /* Attraction Shield specifics */ \
	X(ITEM_HYUDORO) /* Hyudoro specifics */ \
	\
	/* Decorative */ \
	X(DECORATION) \
	X(LIGHTS) \
	X(TERRAIN) \
	X(BUBBLE) \
	X(RANDOMANIM) \
	X(RANDOMSTATE) \
	\
	X(SFX) \
	X(SFX_TOUCHWATER) \
	X(SFX_ALT) \
	X(VOICES) \
	X(BOSSSCREAM) \
	\
	X(EXPLOSION) \
	X(SMOLDERING) \
	X(SPARKLE) \
	/* Lua (can be anything) */ \
	X(LUA_1) \
	X(LUA_2) \
	X(LUA_3) \
	X(LUA_4) \
	X(LUA_5) \
	X(LUA_6)

//#define DEBUGRANDOM

typedef enum
{
#define ENUMITEM(name) PR_##name,
	// Before release, cases of this RNG class should
	// be removed, only kept as the default for Lua.
	// Older replays (which had no multi-seed RNG)
	// willl fall back to this when needed.
	PR_UNDEFINED,

	PR_LEGACY = PR_UNDEFINED,

	RNGSEEDS(ENUMITEM)

	PRLASTLUACLASS = PR_LUA_6,

	// Place new ones at the end for demo compatibility.

	PRNUMCLASS // Number of RNG seed classes.
} ATTRPACK pr_class_t;

#undef ENUMITEM

extern char pr_class_name[PRNUMCLASS][33];

// M_Random functions pull random numbers of various types that aren't network synced.
// P_Random functions pulls random bytes from a PRNG that is network synced.

// RNG functions
UINT32  M_Random(void);
fixed_t M_RandomFixed(void);
UINT8   M_RandomByte(void);
UINT32  M_RandomKey(UINT32 a);
INT32   M_RandomRange(INT32 a, INT32 b);

// PRNG functions
#ifdef DEBUGRANDOM
#define P_Random(c)            P_RandomD(__FILE__, __LINE__, c)
#define P_RandomFixed(c)       P_RandomFixedD(__FILE__, __LINE__, c)
#define P_RandomByte(c)        P_RandomByteD(__FILE__, __LINE__, c)
#define P_RandomKey(c, d)      P_RandomKeyD(__FILE__, __LINE__, c, d)
#define P_RandomRange(c, d, e) P_RandomRangeD(__FILE__, __LINE__, c, d, e)
UINT32  P_RandomD(const char *rfile, INT32 rline, pr_class_t pr_class);
fixed_t P_RandomFixedD(const char *rfile, INT32 rline, pr_class_t pr_class);
UINT8   P_RandomByteD(const char *rfile, INT32 rline, pr_class_t pr_class);
INT32   P_RandomKeyD(const char *rfile, INT32 rline, pr_class_t pr_class, INT32 a);
INT32   P_RandomRangeD(const char *rfile, INT32 rline, pr_class_t pr_class, INT32 a, INT32 b);
#else
UINT32  P_Random(pr_class_t pr_class);
fixed_t P_RandomFixed(pr_class_t pr_class);
UINT8   P_RandomByte(pr_class_t pr_class);
INT32   P_RandomKey(pr_class_t pr_class, INT32 a);
INT32   P_RandomRange(pr_class_t pr_class, INT32 a, INT32 b);
#endif

// Macros for other functions
#define M_SignedRandom()  ((INT32)M_RandomByte() - 128) // [-128, 127] signed byte, originally a
#define P_SignedRandom(pr)  ((INT32)P_RandomByte(pr) - 128) // function of its own, moved to a macro

#define M_RandomChance(p) (M_RandomFixed() < p) // given fixed point probability, p, between 0 (0%)
#define P_RandomChance(pr, p) (P_RandomFixed(pr) < p) // and FRACUNIT (100%), returns true p% of the time

// Debugging
UINT32 P_RandomPeek(pr_class_t pr_class);

// Working with the seed for PRNG
#ifdef DEBUGRANDOM
#define P_GetRandSeed(pr) P_GetRandSeedD(__FILE__, __LINE__, pr)
#define P_GetInitSeed(pr) P_GetInitSeedD(__FILE__, __LINE__, pr)
#define P_SetRandSeed(pr, s) P_SetRandSeedD(__FILE__, __LINE__, pr, s)
#define P_SetRandSeedNet(pr, i, s) P_SetRandSeedD(__FILE__, __LINE__, pr, i, s)
UINT32 P_GetRandSeedD(const char *rfile, INT32 rline, pr_class_t pr_class);
UINT32 P_GetInitSeedD(const char *rfile, INT32 rline, pr_class_t pr_class);
void P_SetRandSeedD(const char *rfile, INT32 rline, pr_class_t pr_class, UINT32 seed);
void P_SetRandSeedNetD(const char *rfile, INT32 rline, pr_class_t pr_class, UINT32 init, UINT32 seed);
#else
UINT32 P_GetRandSeed(pr_class_t pr_class);
UINT32 P_GetInitSeed(pr_class_t pr_class);
void P_SetRandSeed(pr_class_t pr_class, UINT32 seed);
void P_SetRandSeedNet(pr_class_t pr_class, UINT32 init, UINT32 seed);
#endif

void P_ClearRandom(UINT32 seed);
UINT32 M_RandomizedSeed(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
