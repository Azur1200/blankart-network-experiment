// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2012-2016 by Matthew "Kaito Sinclaire" Walsh.
// Copyright (C) 2012-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  m_cond.c
/// \brief Unlockable condition system for SRB2 version 2.1

#include "m_cond.h"
#include "doomstat.h"
#include "z_zone.h"

#include "hu_stuff.h" // CEcho
#include "v_video.h" // video flags

#include "g_game.h" // record info
#include "r_skins.h" // numskins
#include "r_draw.h" // R_GetColorByName
#include "k_kart.h"
#include "k_pwrlv.h"
#include "k_stats.h"

// Map triggers for linedef executors
// 32 triggers, one bit each
uint32_t unlocktriggers;

// The meat of this system lies in condition sets
conditionset_t conditionSets[MAXCONDITIONSETS];

// Default Emblem locations
emblem_t emblemlocations[MAXEMBLEMS] = {0};

// Default Extra Emblems
extraemblem_t extraemblems[MAXEXTRAEMBLEMS] = {0};

// Unlockables
unlockable_t unlockables[MAXUNLOCKABLES] = {0};

// Number of emblems and extra emblems
int32_t numemblems = 0;
int32_t numextraemblems = 0;

void M_AddRawCondition(uint8_t set, uint8_t id, conditiontype_t c, int32_t r, int16_t x1, int16_t x2)
{
	condition_t *cond;
	uint32_t num, wnum;

	I_Assert(set && set <= MAXCONDITIONSETS);

	wnum = conditionSets[set - 1].numconditions;
	num = ++conditionSets[set - 1].numconditions;

	conditionSets[set - 1].condition = Z_Realloc(conditionSets[set - 1].condition, sizeof(condition_t)*num, PU_STATIC, 0);

	cond = conditionSets[set - 1].condition;

	cond[wnum].id = id;
	cond[wnum].type = c;
	cond[wnum].requirement = r;
	cond[wnum].extrainfo1 = x1;
	cond[wnum].extrainfo2 = x2;
}

void M_ClearConditionSet(uint8_t set)
{
	Z_Free(conditionSets[set - 1].condition);
	conditionSets[set - 1].condition = NULL;
	conditionSets[set - 1].numconditions = 0;
	conditionSets[set - 1].achieved = false;
}

// Clear ALL secrets.
void M_ClearSecrets(void)
{
	int32_t i;

	for (size_t j = 0; j < nummaprecords; j++)
		maprecords[j]->visited = 0;

	for (i = 0; i < MAXEMBLEMS; ++i)
		emblemlocations[i].collected = false;
	for (i = 0; i < MAXEXTRAEMBLEMS; ++i)
		extraemblems[i].collected = false;
	for (i = 0; i < MAXUNLOCKABLES; ++i)
		unlockables[i].unlocked = false;
	for (i = 0; i < MAXCONDITIONSETS; ++i)
		conditionSets[i].achieved = false;

	timesBeaten = 0;

	// Re-unlock any always unlocked things
	M_SilentUpdateUnlockablesAndEmblems();
}

// ----------------------
// Condition set checking
// ----------------------
uint8_t M_CheckCondition(const condition_t *cn)
{
	switch (cn->type)
	{
		case UC_PLAYTIME: // Requires total playing time >= x
			return (kartstats.totalplaytime >= (unsigned)cn->requirement);
		case UC_MATCHESPLAYED: // Requires any level completed >= x times
			return (kartstats.matchesplayed >= (unsigned)cn->requirement);
		case UC_POWERLEVEL: // Requires power level >= x on a certain gametype
			return (vspowerlevel[cn->extrainfo1] >= (unsigned)cn->requirement);
		case UC_GAMECLEAR: // Requires game beaten >= x times
			return (timesBeaten >= (unsigned)cn->requirement);
		case UC_OVERALLTIME: // Requires overall time <= x
			return (M_GotLowEnoughTime(cn->requirement));
		case UC_MAPVISITED: // Requires map x to be visited
		case UC_MAPBEATEN: // Requires map x to be beaten
		case UC_MAPENCORE: // Requires map x to be beaten in encore
		{
			uint8_t mvtype = MV_VISITED;
			if (cn->type == UC_MAPBEATEN)
				mvtype = MV_BEATEN;
			else if (cn->type == UC_MAPENCORE)
				mvtype = MV_ENCORE;

			const char *mapname = G_BuildMapName(cn->requirement+1);
			if (mapname == NULL)
				return false;
			maprecord_t *record = G_GetMapRecord(mapname);
			return record ? (record->visited & mvtype) == mvtype : false;
		}
		case UC_MAPTIME: // Requires time on map <= x
			return (G_GetBestTime(cn->extrainfo1) <= (unsigned)cn->requirement);
		case UC_TRIGGER: // requires map trigger set
			return !!(unlocktriggers & (1 << cn->requirement));
		case UC_TOTALEMBLEMS: // Requires number of emblems >= x
			return (M_GotEnoughEmblems(cn->requirement));
		case UC_EMBLEM: // Requires emblem x to be obtained
			return emblemlocations[cn->requirement-1].collected;
		case UC_EXTRAEMBLEM: // Requires extra emblem x to be obtained
			return extraemblems[cn->requirement-1].collected;
		case UC_CONDITIONSET: // requires condition set x to already be achieved
			return M_Achieved(cn->requirement-1);
	}
	return false;
}

static uint8_t M_CheckConditionSet(conditionset_t *c)
{
	uint32_t i;
	uint32_t lastID = 0;
	condition_t *cn;
	uint8_t achievedSoFar = true;

	for (i = 0; i < c->numconditions; ++i)
	{
		cn = &c->condition[i];

		// If the ID is changed and all previous statements of the same ID were true
		// then this condition has been successfully achieved
		if (lastID && lastID != cn->id && achievedSoFar)
			return true;

		// Skip future conditions with the same ID if one fails, for obvious reasons
		else if (lastID && lastID == cn->id && !achievedSoFar)
			continue;

		lastID = cn->id;
		achievedSoFar = M_CheckCondition(cn);
	}

	return achievedSoFar;
}

void M_CheckUnlockConditions(void)
{
	int32_t i;
	conditionset_t *c;

	for (i = 0; i < MAXCONDITIONSETS; ++i)
	{
		c = &conditionSets[i];
		if (!c->numconditions || c->achieved)
			continue;

		c->achieved = (M_CheckConditionSet(c));
	}
}

uint8_t M_UpdateUnlockablesAndExtraEmblems(void)
{
	int32_t i;
	char cechoText[992] = "";
	uint8_t cechoLines = 0;

	M_CheckUnlockConditions();

	// Go through extra emblems
	for (i = 0; i < numextraemblems; ++i)
	{
		if (extraemblems[i].collected || !extraemblems[i].conditionset)
			continue;
		if ((extraemblems[i].collected = M_Achieved(extraemblems[i].conditionset - 1)) != false)
		{
			strcat(cechoText, va(M_GetText("Got \"%s\" medal!\\"), extraemblems[i].name));
			++cechoLines;
		}
	}

	// Fun part: if any of those unlocked we need to go through the
	// unlock conditions AGAIN just in case an emblem reward was reached
	if (cechoLines)
		M_CheckUnlockConditions();

	// Go through unlockables
	for (i = 0; i < MAXUNLOCKABLES; ++i)
	{
		if (unlockables[i].unlocked || !unlockables[i].conditionset)
			continue;
		if ((unlockables[i].unlocked = M_Achieved(unlockables[i].conditionset - 1)) != false)
		{
			if (unlockables[i].nocecho)
				continue;
			strcat(cechoText, va(M_GetText("\"%s\" unlocked!\\"), unlockables[i].name));
			++cechoLines;
		}
	}

	// Announce
	if (cechoLines)
	{
		char slashed[1024] = "";
		for (i = 0; (i < 19) && (i < 24 - cechoLines); ++i)
			slashed[i] = '\\';
		slashed[i] = 0;

		strcat(slashed, cechoText);

		HU_SetCEchoFlags(V_YELLOWMAP);
		HU_SetCEchoDuration(6);
		HU_DoCEcho(slashed);
		return true;
	}
	return false;
}

// Used when loading gamedata to make sure all unlocks are synched with conditions
void M_SilentUpdateUnlockablesAndEmblems(void)
{
	int32_t i;
	dboolean checkAgain = false;

	// Just in case they aren't to sync
	M_CheckUnlockConditions();
	M_CheckLevelEmblems();

	// Go through extra emblems
	for (i = 0; i < numextraemblems; ++i)
	{
		if (extraemblems[i].collected || !extraemblems[i].conditionset)
			continue;
		if ((extraemblems[i].collected = M_Achieved(extraemblems[i].conditionset - 1)) != false)
			checkAgain = true;
	}

	// check again if extra emblems unlocked, blah blah, etc
	if (checkAgain)
		M_CheckUnlockConditions();

	// Go through unlockables
	for (i = 0; i < MAXUNLOCKABLES; ++i)
	{
		if (unlockables[i].unlocked || !unlockables[i].conditionset)
			continue;
		unlockables[i].unlocked = M_Achieved(unlockables[i].conditionset - 1);
	}
}

// Emblem unlocking shit
uint8_t M_CheckLevelEmblems(void)
{
	int32_t i;
	int32_t valToReach;
	int16_t levelnum;
	uint8_t res;
	uint8_t somethingUnlocked = 0;

	if (!G_EmblemsEnabled())
		return false;

	// Update Score, Time, Rings emblems
	for (i = 0; i < numemblems; ++i)
	{
		int32_t checkLevel;

		if (emblemlocations[i].type < ET_TIME || emblemlocations[i].collected)
			continue;

		checkLevel = G_MapNumber(emblemlocations[i].level);

		if (checkLevel >= nummapheaders || !mapheaderinfo[checkLevel])
			continue;

		levelnum = checkLevel;
		valToReach = emblemlocations[i].var;

		switch (emblemlocations[i].type)
		{
			case ET_TIME: // Requires time on map <= x
				res = (G_GetBestTime(levelnum) <= (unsigned)valToReach);
				break;
			default: // unreachable but shuts the compiler up.
				continue;
		}

		emblemlocations[i].collected = res;
		if (res)
			++somethingUnlocked;
	}
	return somethingUnlocked;
}

// Unseal when we add more emblems
/*uint8_t M_CompletionEmblems(void) // Bah! Duplication sucks, but it's for a separate print when awarding emblems and it's sorta different enough.
{
	int32_t i;
	int32_t embtype;
	int16_t levelnum;
	uint8_t res;
	uint8_t somethingUnlocked = 0;
	uint8_t flags;

	for (i = 0; i < numemblems; ++i)
	{
		int32_t checkLevel;

		if (emblemlocations[i].type < ET_TIME || emblemlocations[i].collected)
			continue;

		checkLevel = G_MapNumber(emblemlocations[i].level);

		if (checkLevel >= nummapheaders || !mapheaderinfo[checkLevel])
			continue;

		levelnum = checkLevel;
		embtype = emblemlocations[i].var;
		flags = MV_BEATEN;

		if (embtype & ME_ENCORE)
			flags |= MV_ENCORE;

		res = ((mapheaderinfo[levelnum]->mapvisited & flags) == flags);

		emblemlocations[i].collected = res;
		if (res)
			++somethingUnlocked;
	}
	return somethingUnlocked;
}*/

// -------------------
// Quick unlock checks
// -------------------
uint8_t M_AnySecretUnlocked(void)
{
	int32_t i;

#ifdef DEVELOP
	if (1)
		return true;	
#endif	
	
	for (i = 0; i < MAXUNLOCKABLES; ++i)
	{
		if (!unlockables[i].nocecho && unlockables[i].unlocked)
			return true;
	}
	return false;
}

uint8_t M_SecretUnlocked(int32_t type)
{
	int32_t i;

#if 1
	if (dedicated)
		return true;
#endif

#ifdef DEVELOP
#define CHADYES true
#else
#define CHADYES false
#endif

	for (i = 0; i < MAXUNLOCKABLES; ++i)
	{
		if (unlockables[i].type == type && unlockables[i].unlocked != CHADYES)
			return !CHADYES;
	}
	return CHADYES;

#undef CHADYES
}

uint8_t M_MapLocked(mapnum_t mapnum)
{

#ifdef DEVELOP
	if (1)
		return false;
#endif
	if (!mapnum || mapnum > nummapheaders)
		return false;
	if (!mapheaderinfo[mapnum-1] || mapheaderinfo[mapnum-1]->unlockrequired < 0)
		return false;
	if (!unlockables[mapheaderinfo[mapnum-1]->unlockrequired].unlocked)
		return true;
	return false;
}

int32_t M_CountEmblems(void)
{
	int32_t found = 0, i;
	for (i = 0; i < numemblems; ++i)
	{
		if (emblemlocations[i].collected)
			found++;
	}
	for (i = 0; i < numextraemblems; ++i)
	{
		if (extraemblems[i].collected)
			found++;
	}
	return found;
}

// --------------------------------------
// Quick functions for calculating things
// --------------------------------------

// Theoretically faster than using M_CountEmblems()
// Stops when it reaches the target number of emblems.
uint8_t M_GotEnoughEmblems(int32_t number)
{
	int32_t i, gottenemblems = 0;
	for (i = 0; i < numemblems; ++i)
	{
		if (emblemlocations[i].collected)
			if (++gottenemblems >= number) return true;
	}
	for (i = 0; i < numextraemblems; ++i)
	{
		if (extraemblems[i].collected)
			if (++gottenemblems >= number) return true;
	}
	return false;
}

uint8_t M_GotLowEnoughTime(int32_t tictime)
{
	(void)tictime;
	I_Error("fixme M_GotLowEnoughTime");
	/*
	int32_t curtics = 0;
	int32_t i;

	for (i = 0; i < nummapheaders; ++i)
	{
		recordpresetnum_e preset = G_RecordPresetIndex();
		if (!mapheaderinfo[i] || (mapheaderinfo[i]->menuflags & LF2_NOTIMEATTACK))
			continue;

		if (!mapheaderinfo[i]->mainrecord[preset] || !mapheaderinfo[i]->mainrecord[preset]->time)
			return false;
		else if ((curtics += mapheaderinfo[i]->mainrecord[preset]->time) > tictime)
			return false;
	}
	return true;
	*/
}

// Gets the skin number for a SECRET_SKIN unlockable.
int32_t M_UnlockableSkinNum(unlockable_t *unlock)
{
	if (unlock->type != SECRET_SKIN)
	{
		// This isn't a skin unlockable...
		return -1;
	}

	if (unlock->stringVar && strcmp(unlock->stringVar, ""))
	{
		// Get the skin from the string.
		int32_t skinnum = R_SkinAvailable(unlock->stringVar);
		if (skinnum != -1)
		{
			return skinnum;
		}
	}

	if (unlock->variable >= 0 && unlock->variable < numskins)
	{
		// Use the number directly.
		return unlock->variable;
	}

	// Invalid skin unlockable.
	return -1;
}

// ----------------
// Misc Emblem shit
// ----------------

// Returns pointer to an emblem if an emblem exists for that level.
// Pass -1 mapnum to continue from last emblem.
// NULL if not found.
// note that this goes in reverse!!
emblem_t *M_GetLevelEmblems(mapnum_t mapnum)
{
	static int32_t map = -1;
	static int32_t i = -1;

	if (mapnum != NEXTMAP_INVALID)
	{
		map = mapnum;
		i = numemblems;
	}

	while (--i >= 0)
	{
		int32_t checkLevel = G_MapNumber(emblemlocations[i].level);

		if (checkLevel >= nummapheaders || !mapheaderinfo[checkLevel])
			continue;

		if (checkLevel == map)
			return &emblemlocations[i];
	}
	return NULL;
}

skincolornum_t M_GetEmblemColor(emblem_t *em)
{
	if (!em || em->color >= numskincolors)
		return SKINCOLOR_NONE;
	return em->color;
}

const char *M_GetEmblemPatch(emblem_t *em, dboolean big)
{
	static char pnamebuf[7];

	if (!big)
		strcpy(pnamebuf, "GOTITn");
	else
		strcpy(pnamebuf, "EMBMn0");

	I_Assert(em->sprite >= 'A' && em->sprite <= 'Z');

	if (!big)
		pnamebuf[5] = em->sprite;
	else
		pnamebuf[4] = em->sprite;

	return pnamebuf;
}

skincolornum_t M_GetExtraEmblemColor(extraemblem_t *em)
{
	if (!em || em->color >= numskincolors)
		return SKINCOLOR_NONE;
	return em->color;
}

const char *M_GetExtraEmblemPatch(extraemblem_t *em, dboolean big)
{
	static char pnamebuf[7];

	if (!big)
		strcpy(pnamebuf, "GOTITn");
	else
		strcpy(pnamebuf, "EMBMn0");

	I_Assert(em->sprite >= 'A' && em->sprite <= 'Z');

	if (!big)
		pnamebuf[5] = em->sprite;
	else
		pnamebuf[4] = em->sprite;

	return pnamebuf;
}
