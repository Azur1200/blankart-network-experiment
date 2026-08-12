// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2012-2016 by Matthew "Kaito Sinclaire" Walsh.
// Copyright (C) 2012-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  m_cond.h
/// \brief Unlockable condition system for SRB2 version 2.1

#ifndef __M_COND_H__
#define __M_COND_H__

#include "info.h"
#include "g_mapnum.h"

#ifdef __cplusplus
extern "C" {
#endif

// --------
// Typedefs
// --------

// DEHackEd structure for each is listed after the condition type
// [required] <optional>
typedef enum
{
	UC_PLAYTIME,		// PLAYTIME [tics]
	UC_MATCHESPLAYED,	// SRB2Kart: MATCHESPLAYED [x played]
	UC_POWERLEVEL,		// SRB2Kart: POWERLEVEL [power level to reach] [gametype, "0" for race, "1" for battle]
	UC_GAMECLEAR,		// GAMECLEAR <x times>
	UC_OVERALLTIME,		// OVERALLTIME [time to beat, tics]
	UC_MAPVISITED,		// MAPVISITED [map number]
	UC_MAPBEATEN,		// MAPBEATEN [map number]
	UC_MAPENCORE,		// MAPENCORE [map number]
	UC_MAPTIME,			// MAPTIME [map number] [time to beat, tics]
	UC_TRIGGER,			// TRIGGER [trigger number]
	UC_TOTALEMBLEMS,	// TOTALEMBLEMS [number of emblems]
	UC_EMBLEM,			// EMBLEM [emblem number]
	UC_EXTRAEMBLEM,		// EXTRAEMBLEM [extra emblem number]
	UC_CONDITIONSET,	// CONDITIONSET [condition set number]
} conditiontype_t;

// Condition Set information
struct condition_t
{
	uint32_t id;           /// <- The ID of this condition.
	                     ///    In an unlock condition, all conditions with the same ID
	                     ///    must be true to fulfill the unlockable requirements.
	                     ///    Only one ID set needs to be true, however.
	conditiontype_t type;/// <- The type of condition
	int32_t requirement;   /// <- The requirement for this variable.
	int16_t extrainfo1;    /// <- Extra information for the condition when needed.
	int16_t extrainfo2;    /// <- Extra information for the condition when needed.
};
struct conditionset_t
{
	uint32_t numconditions;   /// <- number of conditions.
	condition_t *condition; /// <- All conditionals to be checked.
	uint8_t achieved;         /// <- Whether this conditional has been achieved already or not.
	                        ///    (Conditional checking is skipped if true -- it's assumed you can't relock an unlockable)
};

// Emblem information
#define ET_GLOBAL  0 // Emblem with a position in space
#define ET_MAP     1 // Beat the map
#define ET_TIME    2 // Get the time
//#define ET_DEVTIME 3 // Time, but the value is tied to a Time Trial demo, not pre-defined

// Global emblem flags
// (N/A to Kart yet)
//#define GE_OH 1

// Map emblem flags
#define ME_ENCORE 1

struct emblem_t
{
	uint8_t type;      ///< Emblem type
	int16_t tag;       ///< Tag of emblem mapthing
	char * level;     ///< Level on which this emblem can be found.
	uint8_t sprite;    ///< emblem sprite to use, 0 - 25
	uint16_t color;    ///< skincolor to use
	int32_t var;       ///< If needed, specifies information on the target amount to achieve (or target skin)
	char *stringVar; ///< String version
	char hint[110];  ///< Hint for emblem hints menu
	uint8_t collected; ///< Do you have this emblem?
};
struct extraemblem_t
{
	char name[20];          ///< Name of the goal (used in the "emblem awarded" cecho)
	char description[40];   ///< Description of goal (used in statistics)
	uint8_t conditionset;     ///< Condition set that awards this emblem.
	uint8_t showconditionset; ///< Condition set that shows this emblem.
	uint8_t sprite;           ///< emblem sprite to use, 0 - 25
	uint16_t color;           ///< skincolor to use
	uint8_t collected;        ///< Do you have this emblem?
};

// Unlockable information
struct unlockable_t
{
	char name[64];
	char objective[64];
	uint8_t conditionset;
	uint8_t showconditionset;
	int16_t type;
	int16_t variable;
	char *stringVar;
	uint8_t nocecho;
	uint8_t nochecklist;
	uint8_t unlocked;
};

#define SECRET_NONE			 0 // Does nil.  Use with levels locked by UnlockRequired
#define SECRET_HEADER		 1 // Does nothing on its own, just serves as a header for the menu

#define SECRET_SKIN			 2 // Allow this character to be selected
#define SECRET_WARP			 3 // Selectable warp
#define SECRET_LEVELSELECT	 4 // Selectable level select

#define SECRET_TIMEATTACK	 5 // Enables Time Attack on the main menu
#define SECRET_ITEMBREAKER	6 // Enables Item Breaker on the main menu
#define SECRET_SOUNDTEST	 7 // Sound Test
#define SECRET_CREDITS		 8 // Enables Credits

#define SECRET_ITEMFINDER	 9 // Enables Item Finder/Emblem Radar
#define SECRET_EMBLEMHINTS	10 // Enables Emblem Hints

#define SECRET_ENCORE		11 // Enables Encore mode cvar
#define SECRET_EXPERTSPEED	12 // Enables Expert gamespeed
#define SECRET_HELLATTACK	13 // Map Hell in record attack

#define SECRET_PANDORA		14 // Enables Pandora's Box

// If you have more secrets than these variables allow in your game,
// you seriously need to get a life.
#define MAXCONDITIONSETS 128
#define MAXEMBLEMS       512
#define MAXEXTRAEMBLEMS   16
#define MAXUNLOCKABLES    32

extern conditionset_t conditionSets[MAXCONDITIONSETS];
extern emblem_t emblemlocations[MAXEMBLEMS];
extern extraemblem_t extraemblems[MAXEXTRAEMBLEMS];
extern unlockable_t unlockables[MAXUNLOCKABLES];

extern int32_t numemblems;
extern int32_t numextraemblems;

extern uint32_t unlocktriggers;

// Condition set setup
void M_AddRawCondition(uint8_t set, uint8_t id, conditiontype_t c, int32_t r, int16_t x1, int16_t x2);

// Clearing secrets
void M_ClearConditionSet(uint8_t set);
void M_ClearSecrets(void);

// Updating conditions and unlockables
void M_CheckUnlockConditions(void);
uint8_t M_CheckCondition(const condition_t *cn);
uint8_t M_UpdateUnlockablesAndExtraEmblems(void);
void M_SilentUpdateUnlockablesAndEmblems(void);
uint8_t M_CheckLevelEmblems(void);
//uint8_t M_CompletionEmblems(void);

// Checking unlockable status
uint8_t M_AnySecretUnlocked(void);
uint8_t M_SecretUnlocked(int32_t type);
uint8_t M_MapLocked(mapnum_t mapnum);
int32_t M_CountEmblems(void);

// Emblem shit
emblem_t *M_GetLevelEmblems(mapnum_t mapnum);
skincolornum_t M_GetEmblemColor(emblem_t *em);
const char *M_GetEmblemPatch(emblem_t *em, dboolean big);
skincolornum_t M_GetExtraEmblemColor(extraemblem_t *em);
const char *M_GetExtraEmblemPatch(extraemblem_t *em, dboolean big);

// If you're looking to compare stats for unlocks or what not, use these
// They stop checking upon reaching the target number so they
// should be (theoretically?) slightly faster.
uint8_t M_GotEnoughEmblems(int32_t number);
uint8_t M_GotLowEnoughTime(int32_t tictime);

int32_t M_UnlockableSkinNum(unlockable_t *unlock);
int32_t M_EmblemSkinNum(emblem_t *emblem);

//                     vvvvvvvvvv anti-integer promotion, do not remove
#define M_Achieved(a) ((unsigned)(a) >= MAXCONDITIONSETS || conditionSets[a].achieved)

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __M_COND_H__
