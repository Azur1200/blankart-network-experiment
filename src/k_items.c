// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2018-2025 by Kart Krew.
// Copyright (C) 2025 by "Anonimus".
// Copyright (C) 2025 Blankart Team.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  k_items.c
/// \brief Kart item systems.

// SRB2kart Roulette Code - Position Based

#include "doomdef.h"
#include "doomstat.h"
#include "d_netcmd.h"
#include "d_player.h"
#include "g_game.h"
#include "info.h"
#include "m_easing.h"   // Invincibility gradienting
#include "m_fixed.h"
#include "m_random.h"
#include "p_local.h"
#include "p_mobj.h"
#include "p_setup.h"
#include "s_sound.h"
#include "typedef.h"

#include "k_battle.h"
#include "k_boss.h"
#include "k_bot.h"
#include "k_kart.h"
#include "k_waypoint.h"

#include "k_cluster.hpp"
#include "k_items.h"
#include "z_zone.h"

kartitem_t kartitems[MAXKARTITEMS] = {0};
kartresult_t kartresults[MAXKARTRESULTS] = {0};
UINT8 numkartitems = 0;
UINT8 numkartresults = 0;

static CV_PossibleValue_t kartdebugitem_cons_t[MAXKARTITEMS] = {0};
consvar_t cv_kartdebugitem = CVAR_INIT ("kartdebugitem", "NONE", CV_NETVAR|CV_CHEAT, kartdebugitem_cons_t, NULL);
static CV_PossibleValue_t kartdebugamount_cons_t[] = {{1, "MIN"}, {255, "MAX"}, {0, NULL}};
consvar_t cv_kartdebugamount = CVAR_INIT ("kartdebugamount", "1", CV_NETVAR|CV_CHEAT, kartdebugamount_cons_t, NULL);

static CV_PossibleValue_t kartitemvariant_cons_t[] = {{0, "Legacy"}, {1, "Alternative"}, {0, NULL}};

#define NUMKARTODDS 	(MAXODDS*10)

// Base multiplication to ALL item odds to simulate fractional precision.
// Reduced from 4 to 1 due to 75-count probability.
#define BASEODDSMUL 1

// Multiplication to odds for Battle item odds specifically.
#define BATTLEODDSMUL 4

// Maximum probability.
#define REALMAXPROB 75
#define MAXPROBABILITY (REALMAXPROB * BASEODDSMUL)

#define REALMAXBATTLEPROB 10
#define MAXBATTLEPROBABILITY (REALMAXBATTLEPROB * BATTLEODDSMUL)

static UINT8 oddstablelen[MAXODDSTABLES] = { MAXODDS, 2, 4 };

static void K_RegisterItem(const char *name, kartitemflags_e flags)
{
	kartitem_t *item = &kartitems[numkartitems++];

	item->name = name;
	item->flags = flags;

	kartdebugitem_cons_t[numkartitems - 1].strvalue = name;
	kartdebugitem_cons_t[numkartitems - 1].value = numkartitems - 1;
	kartdebugitem_cons_t[numkartitems].strvalue = NULL;
	kartdebugitem_cons_t[numkartitems].value = 0;
}

static kartresult_t *K_RegisterResult(const char *name, kartitemtype_e type, UINT8 amount, UINT8 flags)
{
	kartresult_t *result = &kartresults[numkartresults];
	result->flags = flags;

	// check if a result with this name already exists
	kartresult_t *prev = K_GetKartResult(name);
	if (prev != NULL)
	{
		// link to it as an alternate item
		result->cvar = prev->cvar;
		result->type = prev->type;
		result->amount = prev->amount;
		result->isalt = true;

		// check if the kartitem needs to have its alt cvar registered
		kartitem_t *item = &kartitems[result->type];
		if (item->altcvar == NULL)
		{
			char *cvname = malloc(strlen(item->name)+1 + 8);
			sprintf(cvname, "altitem_%s", item->name);
			strlwr(cvname);

			consvar_t *var = Z_Calloc(sizeof(consvar_t), PU_STATIC, &item->altcvar);
			var->name = cvname;
			var->defaultvalue = "Legacy";
			var->flags = CV_NETVAR/*|CV_CALL*/;
			var->PossibleValue = kartitemvariant_cons_t;
			var->func = NULL;
			CV_RegisterVar(var);
		}
	}
	else
	{
		result->type = type;
		result->amount = amount;
		result->isalt = false;

		consvar_t *var = Z_Calloc(sizeof(consvar_t), PU_STATIC, &result->cvar);
		var->name = name;
		var->defaultvalue = "On";
		var->flags = CV_NETVAR|CV_CHEAT;
		var->PossibleValue = CV_OnOff;
		var->func = NULL;
		CV_RegisterVar(var);
	}

	numkartresults++;
	return result;
}

kartresult_t *K_GetKartResult(const char *name)
{
	for (UINT8 i = 0; i < numkartresults; i++)
	{
		if (!strcmp(name, kartresults[i].cvar->name))
			return &kartresults[i];
	}

	return NULL;
}

// Unique odds, per-item basis

// Alt. Invincibility's odds.
static useoddsfunc_f K_AltInvinOdds;

// SPB odds (Race)
static useoddsfunc_f K_SPBRaceOdds;

void K_InitializeItems(void)
{
	numkartitems = 0;
	K_RegisterItem("NONE", 0);
	K_RegisterItem("SNEAKER", 0);
	K_RegisterItem("ROCKETSNEAKER", 0);
	K_RegisterItem("INVINCIBILITY", KIF_ANIMATED|KIF_DARKBG);
	K_RegisterItem("BANANA", 0);
	K_RegisterItem("EGGMAN", 0);
	K_RegisterItem("ORBINAUT", 0);
	K_RegisterItem("JAWZ", 0);
	K_RegisterItem("MINE", 0);
	K_RegisterItem("BALLHOG", 0);
	K_RegisterItem("SPB", KIF_DARKBG);
	K_RegisterItem("GROW", 0);
	K_RegisterItem("SHRINK", 0);
	K_RegisterItem("THUNDERSHIELD", KIF_DARKBG);
	K_RegisterItem("HYUDORO", 0);
	K_RegisterItem("POGOSPRING", 0);
	K_RegisterItem("KITCHENSINK", 0);
	K_RegisterItem("SUPERRING", 0);
	K_RegisterItem("LANDMINE", 0);
	K_RegisterItem("BUBBLESHIELD", KIF_DARKBG);
	K_RegisterItem("FLAMESHIELD", KIF_DARKBG);

#define A(item, amount, samount) \
	kartitems[item].graphics[0].numpatches = amount; \
	kartitems[item].graphics[1].numpatches = samount; \
	Z_Malloc(sizeof(const char *) * amount, PU_STATIC, &kartitems[item].graphics[0].patchnames); \
	Z_Malloc(sizeof(const char *) * samount, PU_STATIC, &kartitems[item].graphics[1].patchnames); \
	Z_Calloc(sizeof(patch_t *) * amount, PU_STATIC, &kartitems[item].graphics[0].patches); \
	Z_Calloc(sizeof(patch_t *) * samount, PU_STATIC, &kartitems[item].graphics[1].patches)

	A(KITEM_SNEAKER, 3, 1);
	kartitems[KITEM_SNEAKER].graphics[0].patchnames[0] = "K_ITSHOE";
	kartitems[KITEM_SNEAKER].graphics[0].patchnames[1] = "K_ITSHO2";
	kartitems[KITEM_SNEAKER].graphics[0].patchnames[2] = "K_ITSHO3";
	kartitems[KITEM_SNEAKER].graphics[1].patchnames[0] = "K_ISSHOE";

	A(KITEM_INVINCIBILITY, 7, 6);
	kartitems[KITEM_INVINCIBILITY].graphics[0].patchnames[0] = "K_ITINV1";
	kartitems[KITEM_INVINCIBILITY].graphics[0].patchnames[1] = "K_ITINV2";
	kartitems[KITEM_INVINCIBILITY].graphics[0].patchnames[2] = "K_ITINV3";
	kartitems[KITEM_INVINCIBILITY].graphics[0].patchnames[3] = "K_ITINV4";
	kartitems[KITEM_INVINCIBILITY].graphics[0].patchnames[4] = "K_ITINV5";
	kartitems[KITEM_INVINCIBILITY].graphics[0].patchnames[5] = "K_ITINV6";
	kartitems[KITEM_INVINCIBILITY].graphics[0].patchnames[6] = "K_ITINV7";
	kartitems[KITEM_INVINCIBILITY].graphics[1].patchnames[0] = "K_ISINV1";
	kartitems[KITEM_INVINCIBILITY].graphics[1].patchnames[1] = "K_ISINV2";
	kartitems[KITEM_INVINCIBILITY].graphics[1].patchnames[2] = "K_ISINV3";
	kartitems[KITEM_INVINCIBILITY].graphics[1].patchnames[3] = "K_ISINV4";
	kartitems[KITEM_INVINCIBILITY].graphics[1].patchnames[4] = "K_ISINV5";
	kartitems[KITEM_INVINCIBILITY].graphics[1].patchnames[5] = "K_ISINV6";

	A(KITEM_BANANA, 4, 1);
	kartitems[KITEM_BANANA].graphics[0].patchnames[0] = "K_ITBANA";
	kartitems[KITEM_BANANA].graphics[0].patchnames[1] = "K_ITBAN2";
	kartitems[KITEM_BANANA].graphics[0].patchnames[2] = "K_ITBAN3";
	kartitems[KITEM_BANANA].graphics[0].patchnames[3] = "K_ITBAN4";
	kartitems[KITEM_BANANA].graphics[1].patchnames[0] = "K_ISBANA";

	A(KITEM_ORBINAUT, 4, 1);
	kartitems[KITEM_ORBINAUT].graphics[0].patchnames[0] = "K_ITORB1";
	kartitems[KITEM_ORBINAUT].graphics[0].patchnames[1] = "K_ITORB2";
	kartitems[KITEM_ORBINAUT].graphics[0].patchnames[2] = "K_ITORB3";
	kartitems[KITEM_ORBINAUT].graphics[0].patchnames[3] = "K_ITORB4";
	kartitems[KITEM_ORBINAUT].graphics[1].patchnames[0] = "K_ISORBN";

	A(KITEM_JAWZ, 2, 1);
	kartitems[KITEM_JAWZ].graphics[0].patchnames[0] = "K_ITJAWZ";
	kartitems[KITEM_JAWZ].graphics[0].patchnames[1] = "K_ITJAW2";
	kartitems[KITEM_JAWZ].graphics[1].patchnames[0] = "K_ISJAWZ";
#undef A

#define ONE(item, big, small) \
	kartitems[item].graphics[0].numpatches = 1; \
	kartitems[item].graphics[1].numpatches = 1; \
	Z_Malloc(sizeof(const char *), PU_STATIC, &kartitems[item].graphics[0].patchnames); \
	Z_Malloc(sizeof(const char *), PU_STATIC, &kartitems[item].graphics[1].patchnames); \
	Z_Calloc(sizeof(patch_t *), PU_STATIC, &kartitems[item].graphics[0].patches); \
	Z_Calloc(sizeof(patch_t *), PU_STATIC, &kartitems[item].graphics[1].patches); \
	kartitems[item].graphics[0].patchnames[0] = big; \
	kartitems[item].graphics[1].patchnames[0] = small

	ONE(KITEM_ROCKETSNEAKER, "K_ITRSHE", "K_ISRSHE");
	ONE(KITEM_EGGMAN, "K_ITEGGM", "K_ISEGGM");
	ONE(KITEM_MINE, "K_ITMINE", "K_ISMINE");
	ONE(KITEM_BALLHOG, "K_ITBHOG", "K_ISBHOG");
	ONE(KITEM_SPB, "K_ITSPB", "K_ISSPB");
	ONE(KITEM_GROW, "K_ITGROW", "K_ISGROW");
	ONE(KITEM_SHRINK, "K_ITSHRK", "K_ISSHRK");
	ONE(KITEM_THUNDERSHIELD, "K_ITTHNS", "K_ISTHNS");
	ONE(KITEM_HYUDORO, "K_ITHYUD", "K_ISHYUD");
	ONE(KITEM_POGOSPRING, "K_ITPOGO", "K_ISPOGO");
	ONE(KITEM_KITCHENSINK, "K_ITSINK", "K_ISSINK");
	ONE(KITEM_SUPERRING, "K_ITRING", "K_ISRING");
	ONE(KITEM_LANDMINE, "K_ITLNDM", "K_ISLNDM");
	ONE(KITEM_BUBBLESHIELD, "K_ITBUBS", "K_ISBUBS");
	ONE(KITEM_FLAMESHIELD, "K_ITFLMS", "K_ISFLMS");
#undef ONE

	numkartresults = 0;
	K_RegisterResult("sneaker", KITEM_SNEAKER, 1, 0);
	K_RegisterResult("rocketsneaker", KITEM_ROCKETSNEAKER, 1, KRF_POWERITEM);
	K_RegisterResult("invincibility", KITEM_INVINCIBILITY, 1, KRF_COOLDOWNONSTART|KRF_POWERITEM);
	K_RegisterResult("banana", KITEM_BANANA, 1, KRF_NOTNEAREND|KRF_NOTFORBOTTOM);
	K_RegisterResult("eggman", KITEM_EGGMAN, 1, KRF_COOLDOWNONSTART|KRF_POWERITEM|KRF_NOTNEAREND|KRF_NOTFORBOTTOM);
	K_RegisterResult("orbinaut", KITEM_ORBINAUT, 1, 0);
	K_RegisterResult("jawz", KITEM_JAWZ, 1, KRF_NOTFORBOTTOM|KRF_POWERITEM);
	K_RegisterResult("mine", KITEM_MINE, 1, KRF_COOLDOWNONSTART|KRF_POWERITEM);
	K_RegisterResult("ballhog", KITEM_BALLHOG, 1, KRF_NOTFORBOTTOM|KRF_POWERITEM);
	kartresult_t *spb = K_RegisterResult("spb", KITEM_SPB, 1, KRF_COOLDOWNONSTART|KRF_INDIRECTITEM|KRF_NOTNEAREND|KRF_RUNNERAUGMENT);
	K_RegisterResult("grow", KITEM_GROW, 1, KRF_COOLDOWNONSTART|KRF_POWERITEM);
	K_RegisterResult("shrink", KITEM_SHRINK, 1, KRF_COOLDOWNONSTART|KRF_INDIRECTITEM|KRF_POWERITEM|KRF_NOTNEAREND);
	K_RegisterResult("thundershield", KITEM_THUNDERSHIELD, 1, KRF_COOLDOWNONSTART|KRF_POWERITEM);
	K_RegisterResult("hyudoro", KITEM_HYUDORO, 1, KRF_COOLDOWNONSTART);
	K_RegisterResult("pogospring", KITEM_POGOSPRING, 1, 0);
	K_RegisterResult("kitchensink", KITEM_KITCHENSINK, 1, 0);
	K_RegisterResult("superring", KITEM_SUPERRING, 1, KRF_NOTNEAREND|KRF_NOTFORBOTTOM);
	K_RegisterResult("landmine", KITEM_LANDMINE, 1, 0);
	K_RegisterResult("bubbleshield", KITEM_BUBBLESHIELD, 1, KRF_NOTFORBOTTOM|KRF_POWERITEM);
	K_RegisterResult("flameshield", KITEM_FLAMESHIELD, 1, KRF_COOLDOWNONSTART|KRF_POWERITEM);

	K_RegisterResult("dualsneaker", KITEM_SNEAKER, 2, 0);
	K_RegisterResult("triplesneaker", KITEM_SNEAKER, 3, KRF_POWERITEM);
	K_RegisterResult("triplebanana", KITEM_BANANA, 3, KRF_POWERITEM|KRF_NOTFORBOTTOM|KRF_NOTNEAREND);
	K_RegisterResult("decabanana", KITEM_BANANA, 10,  KRF_POWERITEM|KRF_NOTFORBOTTOM|KRF_NOTNEAREND);
	K_RegisterResult("tripleorbinaut", KITEM_ORBINAUT, 3, KRF_NOTFORBOTTOM|KRF_POWERITEM);
	K_RegisterResult("quadorbinaut", KITEM_ORBINAUT, 4, KRF_NOTFORBOTTOM|KRF_POWERITEM);
	K_RegisterResult("dualjawz", KITEM_JAWZ, 2, KRF_NOTFORBOTTOM|KRF_POWERITEM);

	// alt items defined below (it's ass, but this will do until everything is SOCced)

	// It's a power item, yes, but we don't want mashing to lessen
	// its chances, so we lie to the game's face.
	// Start cooldowns are handled in the odds function; don't set the flag here.
	// (Also, PLEASE prevent shitty last lap bagging endings.)
	kartresult_t *altinv = K_RegisterResult("invincibility", 0, 0, KRF_NOTNEAREND);

	kartresult_t *altshrink = K_RegisterResult("shrink", 0, 0, KRF_COOLDOWNONSTART|KRF_POWERITEM|KRF_NOTNEAREND);

	kartresult_t *eggmine = K_RegisterResult("eggman", 0, 0, KRF_NOTFORBOTTOM);

#define O(item, ...) memcpy(K_GetKartResult(#item)->odds[WHICH], (UINT8[MAXODDS]){__VA_ARGS__}, MAXODDS);
#define WHICH ODDS_RACE
	//                0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
	O(sneaker,        0,  0,  0,  0,  8, 24, 60,  0,  0,  0,  0,  0,  0,  0,  0,  0) // Sneaker
	O(rocketsneaker,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 28, 35, 42, 48, 37, 37) // Rocket Sneaker
	O(invincibility,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  4, 24, 50, 75, 75) // Invincibility
	O(banana,        35, 28, 22, 10,  5,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0) // Banana
	O(eggman,        18, 15, 12,  7,  3,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0) // Eggman Monitor
	O(orbinaut,      45, 28, 24,  5,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0) // Orbinaut
	O(jawz,           0, 11, 22,  9,  7,  5,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0) // Jawz
	O(mine,           0,  0,  5,  3,  2,  2,  1,  0,  0,  0,  0,  0,  0,  0,  0,  0) // Mine
	O(ballhog,        0,  0,  0,  0,  1,  1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0) // Ballhog
	O(spb,            0,  0,  2,  3,  4,  5,  6,  9, 10, 12,  8,  5,  2,  2,  0,  0) // Self-Propelled Bomb
	O(grow,           0,  0,  0,  0,  0,  0,  0,  0,  0,  4,  9, 12,  9,  0,  0,  0) // Grow
	O(shrink,         0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  3,  7,  3,  0,  0) // Shrink
	O(thundershield, 12,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0) // Thunder Shield
	O(hyudoro,        0,  0,  0,  0,  0,  2,  2,  4,  4,  2,  0,  0,  0,  0,  0,  0) // Hyudoro
	O(pogospring,     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0) // Pogo Spring
	O(kitchensink,    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0) // Kitchen Sink
	O(superring,      7, 11, 15,  7,  3,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0) // Super Ring
	O(landmine,       8,  2,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0) // Land Mine
	O(bubbleshield,   0,  0,  0, 12, 15,  8,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0) // Bubble Shield
	O(flameshield,    0,  0,  0,  0,  0,  0,  2,  4, 12, 24, 27, 12,  7,  4,  0,  0) // Flame Shield
	O(dualsneaker,    0,  0,  0,  0,  0,  0,  8, 20, 40, 12,  0,  0,  0,  0,  0,  0) // Sneaker x2
	O(triplesneaker,  0,  0,  0,  0,  0,  0,  0,  7, 35, 45, 60, 56, 52,  4,  0,  0) // Sneaker x3
	O(triplebanana,   0,  7,  7,  7,  9,  7,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0) // Banana x3
	O(decabanana,     0,  0,  0,  1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0) // Banana x10
	O(tripleorbinaut, 0,  0,  0,  2,  5,  2,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0) // Orbinaut x3
	O(quadorbinaut,   0,  0,  0,  0,  1,  1,  1,  0,  0,  0,  0,  0,  0,  0,  0,  0) // Orbinaut x4
	O(dualjawz,       0,  0,  0,  1,  5,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0) // Jawz x2
	memcpy(altinv->odds[WHICH], (UINT8[MAXODDS])                                     // Alt. Invincibility
	                { 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0}, MAXODDS);
	memcpy(altshrink->odds[WHICH], (UINT8[MAXODDS])                                  // Alt. Shrink
	                { 0,  0,  0,  0,  2,  7,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0}, MAXODDS);
	memcpy(eggmine->odds[WHICH], (UINT8[MAXODDS])                                    // Egg Mine
	                { 8,  2,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0}, MAXODDS);

	altinv->unique_odds[WHICH] = K_AltInvinOdds; // Alternate: uses unique odds
	spb->unique_odds[WHICH] = K_SPBRaceOdds;
#undef WHICH

#define WHICH ODDS_BATTLE
	//                R  S
	O(sneaker,        2, 1) // Sneaker
	O(rocketsneaker,  0, 0) // Rocket Sneaker
	O(invincibility,  4, 1) // Invincibility
	O(banana,         0, 0) // Banana
	O(eggman,         1, 0) // Eggman Monitor
	O(orbinaut,       8, 0) // Orbinaut
	O(jawz,           8, 1) // Jawz
	O(mine,           6, 1) // Mine
	O(ballhog,        2, 1) // Ballhog
	O(spb,            0, 0) // Self-Propelled Bomb
	O(grow,           2, 1) // Grow
	O(shrink,         0, 0) // Shrink
	O(thundershield,  4, 0) // Thunder Shield
	O(hyudoro,        2, 0) // Hyudoro
	O(pogospring,     3, 0) // Pogo Spring
	O(kitchensink,    0, 0) // Kitchen Sink
	O(superring,      0, 0) // Super Ring
	O(landmine,       2, 0) // Land Mine
	O(bubbleshield,   1, 0) // Bubble Shield
	O(flameshield,    1, 0) // Flame Shield
	O(dualsneaker,    0, 0) // Sneaker x2
	O(triplesneaker,  0, 1) // Sneaker x3
	O(triplebanana,   0, 0) // Banana x3
	O(decabanana,     1, 1) // Banana x10
	O(tripleorbinaut, 2, 0) // Orbinaut x3
	O(quadorbinaut,   1, 1) // Orbinaut x4
	O(dualjawz,       5, 1) // Jawz x2
	memcpy(altinv->odds[WHICH], (UINT8[MAXODDS]) // Alt. Invincibility
	                { 0, 0}, MAXODDS);
	memcpy(altshrink->odds[WHICH], (UINT8[MAXODDS]) // Alt. Shrink
	                { 1, 0}, MAXODDS);
	memcpy(eggmine->odds[WHICH], (UINT8[MAXODDS]) // Egg Mine
	                { 2, 0}, MAXODDS);
#undef WHICH

#define WHICH ODDS_SPECIAL
	//                M  N  O  P
	O(sneaker,        1, 1, 0, 0) // Sneaker
	O(rocketsneaker,  0, 0, 0, 0) // Rocket Sneaker
	O(invincibility,  0, 0, 0, 0) // Invincibility
	O(banana,         0, 0, 0, 0) // Banana
	O(eggman,         0, 0, 0, 0) // Eggman Monitor
	O(orbinaut,       1, 1, 0, 0) // Orbinaut
	O(jawz,           1, 1, 0, 0) // Jawz
	O(mine,           0, 0, 0, 0) // Mine
	O(ballhog,        0, 0, 0, 0) // Ballhog
	O(spb,            0, 0, 0, 1) // Self-Propelled Bomb
	O(grow,           0, 0, 0, 0) // Grow
	O(shrink,         0, 0, 0, 0) // Shrink
	O(thundershield,  0, 0, 0, 0) // Thunder Shield
	O(hyudoro,        0, 0, 0, 0) // Hyudoro
	O(pogospring,     0, 0, 0, 0) // Pogo Spring
	O(kitchensink,    0, 0, 0, 0) // Kitchen Sink
	O(superring,      0, 0, 0, 0) // Super Ring
	O(landmine,       0, 0, 0, 0) // Land Mine
	O(bubbleshield,   0, 0, 0, 0) // Bubble Shield
	O(flameshield,    0, 0, 0, 0) // Flame Shield
	O(dualsneaker,    0, 1, 1, 0) // Sneaker x2
	O(triplesneaker,  0, 0, 1, 1) // Sneaker x3
	O(triplebanana,   0, 0, 0, 0) // Banana x3
	O(decabanana,     0, 0, 0, 0) // Banana x10
	O(tripleorbinaut, 0, 1, 1, 0) // Orbinaut x3
	O(quadorbinaut,   0, 0, 1, 1) // Orbinaut x4
	O(dualjawz,       0, 0, 1, 1) // Jawz x2
	memcpy(altinv->odds[WHICH], (UINT8[MAXODDS]) // Alt. Invincibility
	                { 0, 0, 0, 0}, MAXODDS);
	memcpy(altshrink->odds[WHICH], (UINT8[MAXODDS]) // Alt. Shrink
	                { 0, 0, 0, 0}, MAXODDS);
	memcpy(eggmine->odds[WHICH], (UINT8[MAXODDS]) // Egg Mine
	                { 0, 0, 0, 0}, MAXODDS);
#undef WHICH
#undef O

// Cooldown time table; contains both base (index 0) and current (index 1)
// times. Base times are in seconds, current times are in tics.
#define O(item, v) K_GetKartResult(#item)->basebgone = v;
	O(sneaker, 0) // Sneaker
	O(rocketsneaker, 0) // Rocket Sneaker
	O(invincibility, 0) // Invincibility
	O(banana, 0) // Banana
	O(eggman, 10) // Eggman Monitor
	O(orbinaut, 0) // Orbinaut
	O(jawz, 5) // Jawz
	O(mine, 0) // Mine
	O(ballhog, 10) // Ballhog
	O(spb, 20) // Self-Propelled Bomb
	O(grow, 3) // Grow
	O(shrink, 20) // Shrink
	O(thundershield, 0) // Thunder Shield
	O(hyudoro, 20) // Hyudoro
	O(pogospring, 0) // Pogo Spring
	O(kitchensink, 0) // Kitchen Sink
	O(superring, 0) // Super Ring
	O(landmine, 0) // Land Mine
	O(bubbleshield, 5) // Bubble Shield
	O(flameshield, 8) // Flame Shield
	O(dualsneaker, 0) // Sneaker x2
	O(triplesneaker, 0) // Sneaker x3
	O(triplebanana, 0) // Banana x3
	O(decabanana, 30) // Banana x10
	O(tripleorbinaut, 10) // Orbinaut x3
	O(quadorbinaut, 20) // Orbinaut x4
	O(dualjawz, 10) // Jawz x2
	altinv->basebgone = 0; // Alt. Invincibility
	altshrink->basebgone = 5; // Alt. Shrink
	eggmine->basebgone = 0; // Egg Mine
#undef O

	CV_RegisterVar(&cv_kartdebugitem);
	CV_RegisterVar(&cv_kartdebugamount);
}

void K_SetupItemOdds(void)
{
	// Update the item's variant based on the cvar's value.
	for (kartitemtype_e i = 0; i < numkartitems; i++)
	{
		kartitem_t *item = &kartitems[i];
		if (item->altcvar != NULL && item->altenabled != !!item->altcvar->value)
		{
			CONS_Printf("KITEM_%s: updating variant to %s\n", item->name, item->altcvar->string);
			item->altenabled = !!item->altcvar->value;
		}
	}
}

boolean K_ItemResultEnabled(const kartresult_t *result)
{
	if (result == NULL)
		return false;

	if (result->type == KITEM_SUPERRING && !K_RingsActive())
		return false;

	return result->cvar->value == 1;
}

boolean K_IsKartItemAlternate(kartitemtype_e itemtype)
{
	return itemtype >= 0 && itemtype < numkartitems && kartitems[itemtype].altenabled;
}

/**	\brief	Are the odds in legacy distancing mode?

	\return	true if kartforcelegacyodds is enabled, or the map uses legacy WPs
*/
boolean K_LegacyOddsMode(void)
{
	return (K_UsingLegacyCheckpoints() || (cv_kartforcelegacyodds.value));
}

static boolean K_InStartCooldown(void)
{
	return (leveltime < (30*TICRATE)+starttime);
}

// Magic number distance for use with item roulette tiers
#define ACTIVEDISTVAR (K_LegacyOddsMode() ? DISTVAR_LEGACY : DISTVAR)
#define ACTIVESPBDIST (K_LegacyOddsMode() ? SPBDISTVAR_LEGACY : SPBDISTVAR)

#define SPBSTARTDIST (ACTIVESPBDIST) // Distance when SPB can start appearing
#define SPBFORCEDIST (7 * ACTIVESPBDIST / 2) // Distance when SPB is forced onto 2nd place (3.5x SPBDISTVAR)

#define ENDDIST (12*ACTIVEDISTVAR) // Distance when the game stops giving you bananas

static boolean K_RaceForceSPB(SINT8 playerpos, UINT32 pdis)
{
	return ((gametyperules & GTR_CIRCUIT) && playerpos == 2 && pdis > (UINT32)SPBFORCEDIST);
}

// 1/21/2025: I hate tiptoeing around the integer limit.
// This is at a smaller scale.
static UINT32 K_Dist2D(INT32 x1, INT32 y1, INT32 x2, INT32 y2)
{
	// d = √((x2 - x1)² + (y2 - y1)²)
	INT32 xdiff, ydiff;
	INT64 xprod, yprod;

	xdiff = (x2 - x1);
	ydiff = (y2 - y1);

	xprod = ((INT64)xdiff * (INT64)xdiff);
	yprod = ((INT64)ydiff * (INT64)ydiff);

	return (UINT32)(IntSqrt64(xprod + yprod));
}

// Basic integer distancing, to quote myself:
// "Even if you did 256 units for 1 fracunit in distancing, it'd be a better result than trying to
// deal with overflows on a system that's already being pushed to the limit by needing 65536 units
// for precision. No seriously, I don't think anyone's losing sleep over "hmmmmm, 0.0000152 or
// 0.0039?????" when most 2D game engines only give a fuck about MAYBE 0.001"
/*static UINT32 K_IntDistance(fixed_t curx,
			 fixed_t cury,
			 fixed_t curz,
			 fixed_t destx,
			 fixed_t desty,
			 fixed_t destz)
{
	return K_Dist2D(0,
		      curz / FRACUNIT,
		      K_Dist2D(curx / FRACUNIT, cury / FRACUNIT, destx / FRACUNIT, desty / FRACUNIT),
		      destz / FRACUNIT);
}*/

// This one uses map scaling instead, use in case of loss of depth on mobjscaled maps.
static UINT32 K_IntDistanceForMap(fixed_t curx,
			       fixed_t cury,
			       fixed_t curz,
			       fixed_t destx,
			       fixed_t desty,
			       fixed_t destz)
{
	return K_Dist2D(0,
		      curz / mapobjectscale,
		      K_Dist2D(curx / mapobjectscale,
			     cury / mapobjectscale,
			     destx / mapobjectscale,
			     desty / mapobjectscale),
		      destz / mapobjectscale);
}

/**	\brief	Prevent overpowered rolls while under the effects of Alt. Shrink.

	\param	item		the item to check

	\return	Don't double for this item?
*/
static boolean K_DontDoubleMyItems(const kartresult_t *result)
{
	return result->type == KITEM_BALLHOG || result->type == KITEM_SPB
	    || result->type == KITEM_INVINCIBILITY || result->type == KITEM_GROW
	    || result->type == KITEM_BUBBLESHIELD || result->type == KITEM_FLAMESHIELD
	    || result->type == KITEM_ROCKETSNEAKER || result->type == KITEM_SHRINK
	    || result->type == KITEM_HYUDORO
	    || (result->type == KITEM_BANANA && result->amount >= 4)
	    || (result->type == KITEM_ORBINAUT && result->amount >= 3)
	    || (result->type == KITEM_SNEAKER && result->amount >= 3)
	    || (result->type == KITEM_JAWZ && result->amount >= 2)
	    || result->type >= KITEM_FIRSTFREESLOT; // TODO: excludes custom items for now
}

static void K_AwardPlayerItem(player_t *player, kartresult_t *result)
{
	K_BotResetItemConfirm(player, true);

	if (result == NULL)
	{
		player->itemtype = KITEM_SAD;
		player->itemamount = 1;
		return;
	}

	if (result->type == KITEM_SPB || result->type == KITEM_SHRINK) // Indirect items
	{
		indirectitemcooldown = 20*TICRATE;
	}
	else if (result->basebgone > 0) // Item cooldowns
	{
		result->bgone = result->basebgone * TICRATE;
	}

	player->itemtype = result->type;
	UINT8 itemamount = result->amount;

	if (K_IsAltShrunk(player) && !K_DontDoubleMyItems(result))
	{
		itemamount *= 2;
	}

	player->itemamount = itemamount;
}

static fixed_t K_ItemOddsScale(UINT8 numPlayers, boolean spbrush)
{
	// CEP: due to how baseplayer works, 17P+ lobbies will STILL have the disastrous odds of 0.22 prior, if not WORSE
	// let's try adding another condition

	const UINT8 basePlayer = 16; // The player count we design most of the game around.
	const UINT8 vanillaMax = 17; // CEP: Maximum players in "vanilla" (non-30P) clients.
	const UINT8 extPlayer = 24; // CEP: Cap for 17P+ so that odds don't get too muddled.

	UINT8 playerCount = (spbrush ? 2 : numPlayers);
	fixed_t playerScaling = 0;

	// Then, it multiplies it further if the player count isn't equal to basePlayer.
	// This is done to make low player count races more interesting and high player count rates more fair.
	// (If you're in SPB mode and in 2nd place, it acts like it's a 1v1, so the catch-up game is not weakened.)
	if (playerCount < basePlayer)
	{
		// Less than basePlayer: increase odds significantly.
		// 2P: x1.4
		playerScaling = (basePlayer - playerCount) * (FRACUNIT / 10);
	}
	else if (playerCount > basePlayer)
	{
		// More than basePlayer: reduce odds slightly.

		// CEP: 17P+ adjustments
		if (playerCount < vanillaMax)
		{
			// Less than vanillaMax: Use standard calculations.
			// 16P: x0.6
			playerScaling = (basePlayer - playerCount) * (FRACUNIT / 20);

		}
		else if (playerCount > vanillaMax)
		{
			// More than vanillaMax: Increase odds to fit with the increased playercount
			// 24P: x0.6
			// 30P: x0.45
			playerScaling = (basePlayer - min(extPlayer, playerCount)) * (FRACUNIT / 40); // adding a cap here to be sure
		}
	}

	return playerScaling;
}

UINT32 K_ScaleItemDistance(UINT32 distance, UINT8 numPlayers, boolean spbrush)
{
	if (mapobjectscale != FRACUNIT)
	{
		// Bring back to normal scale.
		distance = FixedDiv(distance * FRACUNIT, mapobjectscale) / FRACUNIT;
	}

	if (franticitems == true)
	{
		// Frantic items pretends everyone's farther apart, for crazier items.
		distance = (15 * distance) / 14;
	}

	if (numPlayers > 0)
	{
		// Items get crazier with the fewer players that you have.
		distance = FixedMul(
			distance * FRACUNIT,
			FRACUNIT + (K_ItemOddsScale(numPlayers, spbrush) / 2)
		) / FRACUNIT;
	}

	return distance;
}

#define INVODDS 30

// Prevent integer overflows; don't let this go past 16383
#define INVINDESPERATION 4
#define MAXINVODDS ((MAXPROBABILITY * 2) * INVINDESPERATION)

// Odds value for Alt. Invin. to force itself on trailing players.
#define INVFORCEODDS (MAXINVODDS / 2)

#define FRAC_3h (3 * FRACUNIT / 4)

static INT32 K_KartGetInvincibilityOdds(UINT32 dist)
{
	UINT32 invindist = INVINDIST/2;

	if (dist < invindist)
		return 0;

	INT32 finodds = 0;
	fixed_t fac = (min(32000, (fixed_t)dist) * FRACUNIT) / (INVINDIST);

	if (fac > FRACUNIT)
	{
		// Desperation! Climb exponentially until Invincibility is practically guaranteed.
		fac = (((min(32000, (fixed_t)dist) * FRACUNIT) / (INVINDIST)) - FRACUNIT) >> 1;
		finodds = Easing_InCubic(fac, INVODDS, MAXINVODDS);
	}
	else
	{
		if (fac <= FRAC_3h)
		{
			// Invincibility is practically useless at lower distances.
			// At below 75%, remove it from the item pool.
			return 0;
		}
		// Basic linear climb to "reasonable" odds.
		finodds = FixedMul(INVODDS, fac);
	}

	return min(MAXINVODDS, finodds);
}

#undef FRAC_3h

// Assigns general cooldowns to shield items.
void K_KartHandleShieldCooldown(kartitemtype_e item)
{
	INT32 i, j;

	UINT8 pingame = 0, pexiting = 0;

	INT32 shieldtype = KSHIELD_NONE;
	shieldtype = K_GetShieldFromItem(item);

	if (shieldtype == KSHIELD_NONE)
	{
		// Not a shield!
		return;
	}

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator)
			continue;

		if (!(gametyperules & GTR_BUMPERS) || players[i].bumper)
			pingame++;

		if (players[i].exiting)
			pexiting++;

		if (((shieldtype == K_GetShieldFromItem(players[i].itemtype))
		    || (shieldtype == K_GetShieldFromPlayer(&players[i]))))
		{
			for (j = 0; j < numkartresults; j++)
			{
				kartresult_t *result = &kartresults[j];

				// If this shield has a cooldown, force-apply the cooldown preeemptively for
				// the entire time the shield is being held by a player.
				if (result->type == item && result->basebgone > 0)
				{
					result->bgone = result->basebgone * TICRATE;
					break;
				}
			}
		}
	}
}

// Unique odds functions, for REAL this time

// Alt. Invin. odds
static INT32 K_AltInvinOdds(INT32 odds, const kartroulette_t *roulette, kartresult_t *result)
{
	odds = K_KartGetInvincibilityOdds(roulette->clusterDist);

	// Special case: if you're SERIOUSLY far behind before the cooldown finishes, ignore it and start forcing,
	if (odds >= INVFORCEODDS)
	{
		result->forceme = 3; // Take priority over SPBs
	}
	else if (K_InStartCooldown())
	{
		odds = 0;
	}

	odds *= BASEODDSMUL;

	return odds;
}

// SPB odds
static INT32 K_SPBRaceOdds(INT32 odds, const kartroulette_t *roulette, kartresult_t *result)
{
	if (!K_LegacyOddsMode() && roulette->firstDist < (UINT32)ENDDIST) // No SPB near the end of the race
	{
		odds = 0;
	}
	else if (K_LegacyOddsMode() && roulette->pexiting > 0)
	{
		odds = 0;
	}

	// No forced SPB in 1v1s, it has to be randomly rolled
	if (roulette->pingame <= 2)
	{
		result->forceme = 0;
	}
	else if (K_RaceForceSPB(roulette->playerpos, roulette->pdis)
		&& spbplace == -1 && !indirectitemcooldown)
	{
		// Force SPB onto 2nd if they get too far behind.
		result->forceme = 2;
	}

	return odds;
}

static INT32 GetItemOdds(kartroulette_t *roulette, kartresult_t *result)
{
	INT32 newodds;
	INT32 i;

	SINT8 first = -1, second = -1;
	UINT32 secondToFirst = UINT32_MAX;

	UINT8 flags = result->flags;

	if (result->cvar->value == 0 && !modeattacking)
		return 0;

	if (kartitems[result->type].altenabled != result->isalt)
		return 0; // wrong alt item result

	/*
	if (roulette->bot)
	{
		// TODO: Item use on bots should all be passed-in functions.
		// Instead of manually inserting these, it should return 0
		// for any items without an item use function supplied

		switch (item)
		{
			case KITEM_SNEAKER:
				break;
			default:
				return 0;
		}
	}
	*/

	INT32 oddsmul = BASEODDSMUL;

	// Item type used for actual odds retrieval and cooldown assignments.
	UINT8 oddstable;

	if (gametyperules & GTR_BATTLEODDS)
		oddstable = ODDS_BATTLE;
	else if (gametyperules & GTR_RACEODDS)
		oddstable = ODDS_RACE;
	else
		oddstable = ODDS_SPECIAL;

	I_Assert(roulette->pos < oddstablelen[oddstable]); // DO NOT allow positions past the bounds of the table

	if (gametyperules & GTR_BATTLEODDS)
		oddsmul = BATTLEODDSMUL;

	// Reset forceme
	result->forceme = 0;

	// TODO: braaap (make a separate table for the current level!)
	newodds = result->odds[oddstable][roulette->pos];

	// Blow up the odds with a multiplier.
	newodds *= oddsmul;

	INT32 shieldtype = K_GetShieldFromItem(result->type);

	roulette->pexiting = 0;
	roulette->pingame = 0;
	roulette->firstDist = UINT32_MAX;

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator)
			continue;

		if (!(gametyperules & GTR_BUMPERS) || players[i].bumper)
			roulette->pingame++;

		if (players[i].exiting)
			roulette->pexiting++;

		if (shieldtype != KSHIELD_NONE && ((shieldtype == K_GetShieldFromItem(players[i].itemtype))
			|| (shieldtype == K_GetShieldFromPlayer(&players[i]))))
		{
			// Don't allow more than one of each shield type at a time
			return 0;
		}

		if (players[i].mo && gametype == GT_RACE)
		{
			if (players[i].position == 1 && first == -1)
				first = i;
			if (players[i].position == 2 && second == -1)
				second = i;
		}
	}

	if (first != -1 && second != -1) // calculate 2nd's distance from 1st, for SPB
	{

		if (!K_LegacyOddsMode())
		{
			roulette->firstDist = players[first].distancetofinish;

			if (mapobjectscale != FRACUNIT)
			{
				roulette->firstDist = FixedDiv(roulette->firstDist * FRACUNIT, mapobjectscale) / FRACUNIT;
			}

			secondToFirst = K_ScaleItemDistance(
				players[second].distancetofinish - players[first].distancetofinish,
				roulette->pingame, roulette->spbrush
			);
		}
		else
		{
			secondToFirst = P_AproxDistance(P_AproxDistance(
				players[first].mo->x/4 - players[second].mo->x/4,
				players[first].mo->y/4 - players[second].mo->y/4),
				players[first].mo->z/4 - players[second].mo->z/4);

			// Scale it to prevent overflow issues.
			secondToFirst = (secondToFirst / FRACUNIT)*2;

			secondToFirst = K_ScaleItemDistance(
				secondToFirst,
				roulette->pingame, roulette->spbrush
			);
		}
	}

	if (result->unique_odds[oddstable])
	{
		// This item has unique odds!

		//CONS_Printf("Unique odds found. Assigning uoddsfunc...");
		useoddsfunc_f *uoddsfunc = result->unique_odds[oddstable];

		//CONS_Printf("Running...");
		newodds = uoddsfunc(newodds, roulette, result);
		//CONS_Printf("OK!\n");
	}
	else
	{
		switch (result->type)
		{
			// Alt. Invin. odds are handled in its odds function
			// So are SPB odds
			case KITEM_LANDMINE:
				// au revoir
				newodds = 0;
				break;
			case KITEM_SHRINK:
				if (!K_IsKartItemAlternate(KITEM_SHRINK))
				{
					flags |= KRF_INDIRECTITEM;

					if (roulette->pingame-1 <= roulette->pexiting)
						newodds = 0;
				}
				else
				{
					if (roulette->rival)
					{
						// Rival bot or already shrunk. DON'T roll another.
						newodds = 0;
					}
				}
				break;
			case KITEM_THUNDERSHIELD:
				if (spbplace != -1)
					newodds = 0;
				break;
			default:
				break;
		}
	}

	// In very small matches, remove the stupid bottom half item limiter
	if (roulette->pingame < 6)
	{
		flags &= ~KRF_NOTFORBOTTOM;
	}

	if (newodds == 0)
	{
		// Nothing else we want to do with odds matters at this point :p
		return newodds;
	}

	if (flags & KRF_RUNNERAUGMENT)
	{
		// These odds get stronger as 1st's frontrun increases.
		const INT32 distFromStart = max(secondToFirst - SPBSTARTDIST, 0);
		const INT32 distRange = SPBFORCEDIST - SPBSTARTDIST;
		const INT32 mulMax = 24;

		INT32 multiplier = (distFromStart * mulMax) / distRange;

		if (multiplier < 0)
			multiplier = 0;
		if (multiplier > mulMax)
			multiplier = mulMax;

		newodds *= multiplier;
	}

	if (result->bgone > 0)
	{
		// (Replaces hyubgone) This item is on cooldown; don't let it get rolled.
		newodds = 0;
	}
	else if (flags & KRF_INDIRECTITEM && indirectitemcooldown > 0)
	{
		// Too many items that act indirectly in a match can feel kind of bad.
		newodds = 0;
	}
	else if (flags & KRF_COOLDOWNONSTART && K_InStartCooldown())
	{
		// This item should not appear at the beginning of a race. (Usually really powerful crowd-breaking items)
		newodds = 0;
	}
	else if (!K_LegacyOddsMode() && flags & KRF_NOTNEAREND && roulette->ourDist < (UINT32)ENDDIST)
	{
		// This item should not appear at the end of a race. (Usually trap items that lose their effectiveness)
		newodds = 0;
	}
	else if (flags & KRF_NOTFORBOTTOM && roulette->inBottom && leveltime >= (30*TICRATE)+starttime)
	{
		// This item should not appear for losing players. (Usually items that feel less effective at these positions)
		newodds = 0;
	}
	else if (flags & KRF_POWERITEM)
	{
		// This item is a "power item". This activates "frantic item" toggle related functionality.
		fixed_t fracOdds = newodds * FRACUNIT;

		if (franticitems == true)
		{
			// First, power items multiply their odds by 2 if frantic items are on; easy-peasy.
			fracOdds *= 2;
		}

		if (roulette->rival == true)
		{
			// The Rival bot gets frantic-like items, also :p
			fracOdds *= 2;
		}

		fracOdds = FixedMul(fracOdds, FRACUNIT + K_ItemOddsScale(roulette->pingame, roulette->spbrush));

		if (roulette->mashed > 0)
		{
			// Lastly, it *divides* it based on your mashed value, so that power items are less likely when you mash.
			fracOdds = FixedDiv(fracOdds, FRACUNIT + roulette->mashed);
		}

		newodds = fracOdds / FRACUNIT;
	}

	return newodds;
}

void K_KartGetItemOdds(kartroulette_t *roulette, INT32 outodds[static MAXKARTRESULTS])
{
	for (UINT8 i = 0; i < numkartresults; i++)
	{
		outodds[i] = GetItemOdds(roulette, &kartresults[i]);
	}
};

//{ SRB2kart Roulette Code - Distance Based, yes waypoints

UINT8 K_FindUseodds(const player_t *player, fixed_t mashed, UINT32 pdis, UINT8 bestbumper, boolean spbrush)
{
	fixed_t oddsfac = max(FRACUNIT, (MAXODDS * FRACUNIT) / 8);
	INT32 oddsdiv = ((MAXODDS - 1) * 2);

	UINT8 i, j;
	UINT8 useodds = 0;
	UINT8 disttable[oddsdiv];
	UINT8 distlen = 0;
	boolean oddsvalid[MAXODDS];
	boolean rivalodds = false;

	// Unused now, oops :V
	(void)bestbumper;

	if ((player->bot && player->botvars.rival) || K_IsAltShrunk(player))
	{
		// Rival bots and players using Alt. Shrink get crazier items.
		rivalodds = true;
	}

	INT32 itemodds[MAXKARTRESULTS];
	kartroulette_t roulette = {
		.pdis = pdis,
		.playerpos = player->position,
		//.pos = i,
		.ourDist = player->distancetofinish,
		.clusterDist = player->distancefromcluster,
		.mashed = mashed,
		.spbrush = spbrush,
		.bot = player->bot,
		.rival = rivalodds,
		.inBottom = K_IsPlayerLosing(player),
	};

	for (i = 0; i < MAXODDS; i++)
	{
		boolean available = false;

		if ((gametyperules & GTR_BATTLEODDS) && i > 1)
		{
			oddsvalid[i] = false;
			break;
		}

		roulette.pos = i;
		K_KartGetItemOdds(&roulette, itemodds);

		for (j = 0; j < numkartresults; j++)
		{
			if (itemodds[j] > 0)
			{
				available = true;
				break;
			}
		}

		oddsvalid[i] = available;
	}

#define SETUPDISTTABLE(odds, num) \
	if (oddsvalid[odds]) \
		for (i = num; i; --i) \
        { \
			disttable[distlen++] = odds; \
			distlen = min(oddsdiv - 1, distlen); \
        }

	if (gametyperules & GTR_BATTLEODDS) // Battle Mode
	{
		if (player->roulettetype == KROULETTETYPE_KARMA && oddsvalid[1] == true)
		{
			// 1 is the extreme odds of player-controlled "Karma" items
			useodds = 1;
		}
		else
		{
			useodds = 0;

			if (oddsvalid[0] == false && oddsvalid[1] == true)
			{
				// try to use karma odds as a fallback
				useodds = 1;
			}
		}
	}
	else if (gametyperules & GTR_RACEODDS)
	{

		INT32 tablediv = FixedMul(2, oddsfac);
		INT32 jj;

		// "Why j instead of i"?
		// Using i causes an infinite loop due to SETUPDISTTABLE. Yep.
		for (j = 0; j < MAXODDS; j++)
		{
			if (j == (MAXODDS - 1))
			{
				// Attempt to replicate vanilla behavior; Useodds 8 is set up like this.
				SETUPDISTTABLE(j,1);
			}
			else
			{
				jj = max(1, ((j - 3) / tablediv) + 1);

				if (jj < 1)
				{
					jj = 1;
				}

				//CONS_Printf("SETUPDISTTABLE(%d, %d)\n", j, jj);

				SETUPDISTTABLE(j,jj);
			}
		}

		/*SETUPDISTTABLE(0,1);
		SETUPDISTTABLE(1,1);
		SETUPDISTTABLE(2,1);
		SETUPDISTTABLE(3,2);
		SETUPDISTTABLE(4,2);
		SETUPDISTTABLE(5,3);
		SETUPDISTTABLE(6,3);
		SETUPDISTTABLE(7,1);*/

		const INT32 usedistvar = FixedDiv(ACTIVEDISTVAR, oddsfac);

		if (pdis == 0)
			useodds = disttable[0];
		else if (pdis > (UINT32)ACTIVEDISTVAR * ((12 * distlen) / oddsdiv))
			useodds = disttable[distlen-1];
		else
		{
			for (i = 1; i < (oddsdiv - 1); i++)
			{
				INT32 distcalc = min(distlen-1, (i * distlen) / oddsdiv);

				if (pdis <= (UINT32)usedistvar * distcalc)
				{
					useodds = disttable[distcalc];
					break;
				}
			}
		}
	}

#undef SETUPDISTTABLE

	return min(MAXODDS - 1, useodds);
}

INT32 K_GetRollingRouletteItem(player_t *player)
{
	static UINT8 translation[MAXKARTRESULTS];
	static UINT16 roulette_size;

	static INT16 odds_cached = -1;

	// Race odds have more columns than Battle
	const UINT8 EMPTYODDS[MAXODDS] = {0}; // or not

	if (odds_cached != gametype)
	{
		UINT8 oddstable = 0;
		kartitemtype_e seen[MAXKARTITEMS];
		size_t numseen = 0, j;

		roulette_size = 0;

		if (gametyperules & GTR_BATTLEODDS)
			oddstable = ODDS_BATTLE;
		else
			oddstable = ODDS_RACE;

		for (UINT8 i = 0; i < numkartresults; i++)
		{
			kartresult_t *result = &kartresults[i];

			for (j = 0; j < numseen; j++)
				if (seen[j] == result->type)
					break;

			if (j == numseen && memcmp(result->odds[oddstable], EMPTYODDS, sizeof(EMPTYODDS)))
				translation[roulette_size++] = seen[numseen++] = result->type;
		}

		roulette_size *= 3;
		odds_cached = gametype;
	}

	return translation[(player->itemroulette % roulette_size) / 3];
}

// Legacy odds are fickle and finicky, so we exaggerate distances
// to simulate parity with pathfind odds.
#define LEGACYODDSEXAGGERATE (2*FRACUNIT/3)

/**	\brief	Gets the distance between a player and a position by chaining all other players
 * 			between them and the target position.

	\param	player	player object
	\param  startPos first position to trace to the player from

	\return	(UINT32) pdis
*/
UINT32 K_GetCongaLineDistance(const player_t *player, UINT8 startPos)
{
	SINT8 sortedPlayers[MAXPLAYERS];
	UINT8 sortLength = 0;

	UINT32 pdis = 0;
	INT32 i;

	memset(sortedPlayers, -1, sizeof(sortedPlayers));

	if (player->mo != NULL && P_MobjWasRemoved(player->mo) == false)
	{
		// Sort all of the players ahead of you.
		// Then tally up their distances in a conga line.

		// This will create a much more consistent item
		// distance algorithm than the "spider web" thing
		// that it was doing before.

		// Add yourself to the list.
		// You'll always be the end of the list,
		// so we can also calculate the length here.
		sortedPlayers[ player->position - 1 ] = player - players;
		sortLength = player->position;

		// Will only need to do this if there's goint to be
		// more than yourself in the list.
		if (sortLength > 1)
		{
			SINT8 firstIndex = -1;
			SINT8 secondIndex = -1;
			INT32 startFrom = INT32_MAX;

			// Add all of the other players.
			for (i = 0; i < MAXPLAYERS; i++)
			{
				INT32 pos = INT32_MAX;

				if (!playeringame[i] || players[i].spectator)
				{
					continue;
				}

				if (players[i].mo == NULL || P_MobjWasRemoved(players[i].mo) == true)
				{
					continue;
				}

				pos = players[i].position;

				if ((pos <= 0) || (pos > MAXPLAYERS) || (pos < startPos))
				{
					// Invalid position.
					continue;
				}

				if (pos >= player->position)
				{
					// Tied / behind us.
					// Also handles ourselves, obviously.
					continue;
				}

				// Ties are done with port priority, if there are any.
				if (sortedPlayers[ pos - 1 ] == -1)
				{
					sortedPlayers[ pos - 1 ] = i;
				}
			}

			// The chance of this list having gaps is improbable,
			// but not impossible. So we need to spend some extra time
			// to prevent the gaps from mattering.
			for (i = 0; i < sortLength-1; i++)
			{
				if (sortedPlayers[i] >= 0 && sortedPlayers[i] < MAXPLAYERS)
				{
					// First valid index in the list found.
					firstIndex = sortedPlayers[i];

					// Start the next loop after this player.
					startFrom = i + 1;
					break;
				}
			}

			if (firstIndex >= 0 && firstIndex < MAXPLAYERS
				&& startFrom < sortLength)
			{
				// First index is valid, so we can
				// start comparing the players.

				player_t *firstPlayer = NULL;
				player_t *secondPlayer = NULL;

				for (i = startFrom; i < sortLength; i++)
				{
					if (sortedPlayers[i] >= 0 && sortedPlayers[i] < MAXPLAYERS)
					{
						secondIndex = sortedPlayers[i];

						firstPlayer = &players[firstIndex];
						secondPlayer = &players[secondIndex];

						// Add the distance to the player behind you.
						// At a (relative to map) integer scale using basic distancing
						// arithmetic; more accurate and less concern for overflows.
						// TODO: Overflow-safe addition (caps at UINT32_MAX).
						pdis += K_IntDistanceForMap(
								secondPlayer->mo->x,
								secondPlayer->mo->y,
								secondPlayer->mo->z,
								firstPlayer->mo->x,
								firstPlayer->mo->y,
								firstPlayer->mo->z);

						// Advance to next index.
						firstIndex = secondIndex;
					}
				}
			}
		}
	}

	return pdis;
}

UINT32 K_CalculateInitalPDIS(const player_t *player, UINT8 pingame)
{
	UINT8 i;
	UINT32 pdis = 0;

	(void)pingame;

	if (!K_LegacyOddsMode())
	{
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (playeringame[i] && !players[i].spectator
				&& players[i].position == 1)
			{
				// This player is first! Yay!

				if (player->distancetofinish <= players[i].distancetofinish)
				{
					// Guess you're in first / tied for first?
					pdis = 0;
				}
				else
				{
					// Subtract 1st's distance from your distance, to get your distance from 1st!
					pdis = player->distancetofinish - players[i].distancetofinish;
				}
				break;
			}
		}
	}
	else
	{
		pdis = K_GetCongaLineDistance(player, 1);
	}

	// Exaggerate odds; don't you love the legacy system? :Trollic:
	pdis = FixedMul(pdis, LEGACYODDSEXAGGERATE);

	return pdis;
}

#undef LEGACYODDSEXAGGERATE

UINT32 K_CalculatePDIS(const player_t *player, UINT8 numPlayers, boolean *spbrush)
{
	UINT32 pdis = 0;

	pdis = K_CalculateInitalPDIS(player, numPlayers);

	if (spbplace != -1 && player->position == spbplace+1)
	{
		// SPB Rush Mode: It's 2nd place's job to catch-up items and make 1st place's job hell
		pdis = (3 * pdis) / 2;
		*spbrush = true;
	}

	pdis = K_ScaleItemDistance(pdis, numPlayers, *spbrush);

	if (player->bot && player->botvars.rival)
	{
		// Rival has better odds :)
		pdis = (15 * pdis) / 14;
	}

	// Can almost barely overflow this calc, fudge to prevent this.
	if (pdis > 30000)
		pdis = 30000;

	return pdis;
}

static boolean K_BattleForceSPB(player_t *player)
{
	boolean battlecond = ((gametyperules & GTR_WANTED) && (gametyperules & GTR_WANTEDSPB) && (mostwanted != -1) && (!K_IsPlayerMostWanted(player)));

	return battlecond;
}

void K_KartItemRoulette(player_t *player, ticcmd_t *cmd)
{
	INT32 i;
	UINT8 pingame = 0;
	UINT8 roulettestop;
	UINT32 pdis = 0;
	UINT8 useodds = 0;
	INT32 spawnchance[MAXKARTRESULTS];
	INT64 totalspawnchance = 0; // 75-scale numbers are going to get BIG. This is for paranoia's sake.
	UINT8 bestbumper = 0;
	fixed_t mashed = 0;
	boolean dontforcespb = false;
	boolean spbrush = false;

	// This makes the roulette cycle through items - if this is 0, you shouldn't be here.
	if (!player->itemroulette)
		return;
	player->itemroulette++;

	// Gotta check how many players are active at this moment.
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator)
			continue;
		pingame++;
		if (players[i].exiting)
			dontforcespb = true;
		if (players[i].bumper > bestbumper)
			bestbumper = players[i].bumper;
	}

	// No forced SPB in 1v1s, it has to be randomly rolled
	if (pingame <= 2)
		dontforcespb = true;

	// This makes the roulette produce the random noises.
	if ((player->itemroulette % 3) == 1 && P_IsDisplayPlayer(player))
	{
#define PLAYROULETTESND S_StartSound(NULL, sfx_itrol1 + ((player->itemroulette / 3) % 8))
		for (i = 0; i <= r_splitscreen; i++)
		{
			if (player == &players[displayplayers[i]] && players[displayplayers[i]].itemroulette)
				PLAYROULETTESND;
		}
#undef PLAYROULETTESND
	}

	roulettestop = TICRATE + (3*(pingame - player->position));

	// If the roulette finishes or the player presses BT_ATTACK, stop the roulette and calculate the item.
	// I'm returning via the exact opposite, however, to forgo having another bracket embed. Same result either way, I think.
	// Finally, if you get past this check, now you can actually start calculating what item you get.
	if ((cmd->buttons & BT_ATTACK) && (player->itemroulette >= roulettestop)
		&& (K_RingsActive() || (modeattacking == ATTACKING_NONE))
		&& !(player->itemflags & (IF_ITEMOUT|IF_EGGMANOUT|IF_USERINGS)))
	{
		// Mashing reduces your chances for the good items
		mashed = FixedDiv((player->itemroulette)*FRACUNIT, ((TICRATE*3)+roulettestop)*FRACUNIT) - FRACUNIT;
	}
	else if (!(player->itemroulette >= (TICRATE*3)))
		return;


	pdis = K_CalculatePDIS(player, pingame, &spbrush);

	// SPECIAL CASE No. 1:
	// Fake Eggman items
	if (player->roulettetype == KROULETTETYPE_EGGMAN)
	{
		player->eggmanexplode = 4*TICRATE;
		player->itemroulette = KROULETTE_DISABLED;
		player->roulettetype = KROULETTETYPE_NORMAL;
		if (P_IsDisplayPlayer(player))
			S_StartSound(NULL, sfx_itrole);
		return;
	}

	// SPECIAL CASE No. 2:
	// Give a debug item instead if specified
	if (cv_kartdebugitem.value != 0 && !modeattacking)
	{
		player->itemtype = cv_kartdebugitem.value;
		player->itemamount = cv_kartdebugamount.value;
		player->itemblink = TICRATE;
		player->itemblinkmode = KITEMBLINKMODE_KARMA;
		player->itemroulette = KROULETTE_DISABLED;
		player->roulettetype = KROULETTETYPE_NORMAL;
		if (P_IsDisplayPlayer(player))
			S_StartSound(NULL, sfx_dbgsal);
		return;
	}

	// SPECIAL CASE No. 3:
	// This Gametype never specified an odds type. Roll something random please!
	if (!(gametyperules & GTR_RACEODDS) && !(gametyperules & GTR_BATTLEODDS))
	{
		UINT8 itemroll = P_RandomRange(0, numkartresults - 1);

		K_AwardPlayerItem(player, &kartresults[itemroll]);
		player->itemblink = TICRATE;
		player->itemblinkmode = KITEMBLINKMODE_NORMAL;
		player->itemroulette = KROULETTE_DISABLED;
		player->roulettetype = KROULETTETYPE_NORMAL;
		if (P_IsDisplayPlayer(player))
			S_StartSound(NULL, sfx_itrolf);
		return;
	}

	// SPECIAL CASE No. 4:
	// Record Attack / alone mashing behavior
	if ((modeattacking || pingame == 1)
		&& ((gametyperules & GTR_RACEODDS)
		|| ((gametyperules & GTR_BATTLEODDS) && (itembreaker || bossinfo.boss))))
	{
		if ((gametyperules & GTR_RACEODDS))
		{
			if (mashed && (K_ItemResultEnabled(K_GetKartResult("superring")) || (modeattacking && K_RingsActive()))) // ANY mashed value? You get rings.
			{
				K_AwardPlayerItem(player, K_GetKartResult("superring"));
				player->itemblinkmode = KITEMBLINKMODE_MASHED;
				if (P_IsDisplayPlayer(player))
					S_StartSound(NULL, sfx_itrolm);
			}
			else
			{
				if (modeattacking || K_ItemResultEnabled(K_GetKartResult("sneaker"))) // Waited patiently? You get a sneaker!
					K_AwardPlayerItem(player, K_GetKartResult("sneaker"));
				else  // Default to sad if nothing's enabled...
					K_AwardPlayerItem(player, NULL);
				player->itemblinkmode = KITEMBLINKMODE_NORMAL;
				if (P_IsDisplayPlayer(player))
					S_StartSound(NULL, sfx_itrolf);
			}
		}
		else if (gametyperules & GTR_BATTLEODDS)
		{
			if (mashed && (bossinfo.boss || K_ItemResultEnabled(K_GetKartResult("banana"))) && !itembreaker) // ANY mashed value? You get a banana.
			{
				K_AwardPlayerItem(player, K_GetKartResult("banana"));
				player->itemblinkmode = KITEMBLINKMODE_MASHED;
				if (P_IsDisplayPlayer(player))
					S_StartSound(NULL, sfx_itrolm);
			}
			else if (bossinfo.boss)
			{
				K_AwardPlayerItem(player, K_GetKartResult("orbinaut"));
				player->itemblinkmode = KITEMBLINKMODE_NORMAL;
				if (P_IsDisplayPlayer(player))
					S_StartSound(NULL, sfx_itrolf);
			}
			else if (itembreaker)
			{
				K_AwardPlayerItem(player, K_GetKartResult("sneaker"));
				player->itemblinkmode = KITEMBLINKMODE_MASHED;
				if (P_IsDisplayPlayer(player))
					S_StartSound(NULL, sfx_itrolm);
			}
		}

		player->itemblink = TICRATE;
		player->itemroulette = KROULETTE_DISABLED;
		player->roulettetype = KROULETTETYPE_NORMAL;
		return;
	}

	// SPECIAL CASE No. 5:
	// Being in ring debt occasionally forces Super Ring on you if you mashed
	if (K_ItemResultEnabled(K_GetKartResult("superring")) && mashed && player->rings < 0)
	{
		INT32 debtamount = min(abs(player->ringmin), abs(player->rings));
		if (P_RandomChance((debtamount*FRACUNIT)/abs(player->ringmin)))
		{
			K_AwardPlayerItem(player, K_GetKartResult("superring"));
			player->itemblink = TICRATE;
			player->itemblinkmode = KITEMBLINKMODE_MASHED;
			player->itemroulette = KROULETTE_DISABLED;
			player->roulettetype = KROULETTETYPE_NORMAL;
			if (P_IsDisplayPlayer(player))
				S_StartSound(NULL, sfx_itrolm);
			return;
		}
	}

	// SPECIAL CASE No. 6:
	// Item forcing; the item with the highest "forceme" priority is the one given.
	{
		kartresult_t *forceresult;
		UINT8 bestforce = 0;

		for (i = 0; i < numkartresults; i++)
		{
			if ((kartresults[i].forceme) && (kartresults[i].forceme > bestforce))
			{
				bestforce = kartresults[i].forceme;
				forceresult = &kartresults[i];
			}
		}

		if ((bestforce) && (forceresult))
		{
			K_AwardPlayerItem(player, forceresult);
			player->itemblink = TICRATE;
			player->itemblinkmode = KITEMBLINKMODE_KARMA;
			player->itemroulette = KROULETTE_DISABLED;
			player->roulettetype = KROULETTETYPE_NORMAL;
			if (P_IsDisplayPlayer(player))
				S_StartSound(NULL, sfx_itrolk);
			return;
		}
	}

	// SPECIAL CASE No. 7:
	// In battle, an SPB is forced onto players to target the "most wanted" player
	if (K_BattleForceSPB(player)
		&& spbplace == -1 && !indirectitemcooldown && !dontforcespb
		&& K_ItemResultEnabled(K_GetKartResult("spb")))
	{
		K_AwardPlayerItem(player, K_GetKartResult("spb"));
		player->itemblink = TICRATE;
		player->itemblinkmode = KITEMBLINKMODE_KARMA;
		player->itemroulette = KROULETTE_DISABLED;
		player->roulettetype = KROULETTETYPE_NORMAL;
		if (P_IsDisplayPlayer(player))
			S_StartSound(NULL, sfx_itrolk);
		return;
	}

	// NOW that we're done with all of those specialized cases, we can move onto the REAL item roulette tables.
	// Initializes existing spawnchance values
	memset(spawnchance, 0, sizeof(spawnchance));

	// Split into another function for a debug function below
	useodds = K_FindUseodds(player, mashed, pdis, bestbumper, spbrush);

	kartroulette_t roulette = {
		.pdis = pdis,
		.playerpos = player->position,
		.pos = useodds,
		.ourDist = player->distancetofinish,
		.clusterDist = player->distancefromcluster,
		.mashed = mashed,
		.spbrush = spbrush,
		.bot = player->bot,
		.rival = player->bot && player->botvars.rival,
		.inBottom = K_IsPlayerLosing(player),
	};

	K_KartGetItemOdds(&roulette, spawnchance);

	for (i = 0; i < numkartresults; i++)
	{
		totalspawnchance += spawnchance[i];
		spawnchance[i] = totalspawnchance;
	}

	// Award the player whatever power is rolled
	if (totalspawnchance > 0)
	{
		totalspawnchance = P_RandomKey(totalspawnchance);
		for (i = 0; i < numkartresults && spawnchance[i] <= totalspawnchance; i++);

		K_AwardPlayerItem(player, &kartresults[i]);
	}
	else
	{
		K_AwardPlayerItem(player, NULL);
	}

	if (P_IsDisplayPlayer(player))
		S_StartSound(NULL, ((player->roulettetype == KROULETTETYPE_KARMA) ? sfx_itrolk : (mashed ? sfx_itrolm : sfx_itrolf)));

	player->itemblink = TICRATE;
	player->itemblinkmode = ((player->roulettetype == KROULETTETYPE_KARMA) ? KITEMBLINKMODE_KARMA : (mashed ? KITEMBLINKMODE_MASHED :  KITEMBLINKMODE_NORMAL));

	player->itemroulette = KROULETTE_DISABLED; // Since we're done, clear the roulette number
	player->roulettetype = KROULETTETYPE_NORMAL; // This too
}

/**	\brief	Returns true or false if the player is shrunk.

	\param	player		(player_t) Player to test shrunken status for

	\return	Is this player shrunk?
*/
boolean K_IsShrunk(const player_t *player)
{
    return (player->growshrinktimer < 0);
}

INT16 K_GetShrinkTime(player_t *player)
{
    return (player->growshrinktimer * -1);
}

fixed_t K_AccomodateShrinkScaling(fixed_t x)
{
    return FixedDiv(x, SHRINK_SCALE);
}

/**	\brief	Depending on the Shrink type set, this returns true or false if the player is shrunk.

	\param	player		(player_t) Player to test shrunken status for
	\param	legacy		(boolean) Legacy shrink?

	\return	void
*/
boolean K_IsShrunkMode(const player_t *player, boolean legacy)
{
    boolean shrunk = K_IsShrunk(player);
    boolean legacytype = (!K_IsKartItemAlternate(KITEM_SHRINK));

    if (legacy)
    {
        return ((shrunk) && (legacytype));
    }

    return ((shrunk) && (!legacytype));
}

#define ALTSHRINK_EPSILON (320 * FRACUNIT)
#define NEIGHBOR_IFRAMES (TICRATE / 2)
#define BASE_IFRAMES (2 * TICRATE)

void K_AltShrinkIFrames(player_t *player)
{
	vector3_t tempclusterpoint;

	// Find neighbors
    INT32 N = K_CountNeighboringPlayers(player, ALTSHRINK_EPSILON, &tempclusterpoint);

	// For every neighbor, add some iframes for a clean breakaway.
	player->flashing = (UINT16)min((INT32)(UINT16_MAX), BASE_IFRAMES + (NEIGHBOR_IFRAMES * N));
}

#define PITY_SHRINKINCREASE_BASE (3)
#define PITY_SHRINKINCREASE (TICRATE / 2)

void K_AltShrinkPityIncrease(player_t *player)
{
    // Increase your shrink timer by a little bit for every player you run into.
    INT32 shrinktime = (UINT32)(K_GetShrinkTime(player));

    fixed_t dimin = FRACUNIT;

    dimin = max(0, 5 - player->altshrinktimeshit) * FRACUNIT / 5;

    shrinktime = min(INT16_MAX, shrinktime + PITY_SHRINKINCREASE_BASE + FixedMul(PITY_SHRINKINCREASE, dimin));

    player->growshrinktimer = ((INT16)shrinktime) * -1;
}
