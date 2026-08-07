// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  deh_soc.c
/// \brief Load SOC file and change tables and text

#include "doomdef.h"
#include "d_main.h" // for srb2home
#include "g_game.h"
#include "hu_stuff.h"
#include "sounds.h"
#include "info.h"
#include "d_think.h"
#include "m_argv.h"
#include "z_zone.h"
#include "w_wad.h"
#include "y_inter.h"
#include "m_menu.h"
#include "m_misc.h"
#include "f_finale.h"
#include "st_stuff.h"
#include "i_system.h"
#include "p_setup.h"
#include "r_data.h"
#include "r_textures.h"
#include "r_draw.h"
#include "r_picformats.h"
#include "r_things.h" // R_Char2Frame
#include "r_sky.h"
#include "lua_script.h" // Reluctantly included for LUA_EvalMath
#include "d_clisrv.h"
#include "v_video.h"

#include "m_cond.h"

#include "dehacked.h"
#include "deh_soc.h"
#include "deh_lua.h" // included due to some LUA_SetLuaAction hack smh
// also used for LUA_UpdateSprName
#include "deh_tables.h"

// SRB2Kart
#include "filesrch.h" // refreshdirmenu
#include "k_follower.h"
#include "doomstat.h" // MAXMUSNAMES
#include "k_items.h"

// Loops through every constant and operation in word and performs its calculations, returning the final value.
fixed_t get_number(const char *word)
{
	return LUA_EvalMath(word);

	/*// DESPERATELY NEEDED: Order of operations support! :x
	fixed_t i = find_const(&word);
	INT32 o;
	while(*word) {
		o = operation_pad(&word);
		if (o != -1)
			i = OPERATIONS[o].v(i,find_const(&word));
		else
			break;
	}
	return i;*/
}

#define PARAMCHECK(n) do { if (!params[n]) { deh_warning("Too few parameters, need %d", n); return; }} while (0)

/* ======================================================================== */
// Load a dehacked file format
/* ======================================================================== */
/* a sample to see
                   Thing 1 (Player)       {           // MT_PLAYER
INT32 doomednum;     ID # = 3232              -1,             // doomednum
INT32 spawnstate;    Initial frame = 32       "PLAY",         // spawnstate
INT32 spawnhealth;   Hit points = 3232        100,            // spawnhealth
INT32 seestate;      First moving frame = 32  "PLAY_RUN1",    // seestate
INT32 seesound;      Alert sound = 32         sfx_None,       // seesound
INT32 reactiontime;  Reaction time = 3232     0,              // reactiontime
INT32 attacksound;   Attack sound = 32        sfx_None,       // attacksound
INT32 painstate;     Injury frame = 32        "PLAY_PAIN",    // painstate
INT32 painchance;    Pain chance = 3232       255,            // painchance
INT32 painsound;     Pain sound = 32          sfx_plpain,     // painsound
INT32 meleestate;    Close attack frame = 32  "NULL",         // meleestate
INT32 missilestate;  Far attack frame = 32    "PLAY_ATK1",    // missilestate
INT32 deathstate;    Death frame = 32         "PLAY_DIE1",    // deathstate
INT32 xdeathstate;   Exploding frame = 32     "PLAY_XDIE1",   // xdeathstate
INT32 deathsound;    Death sound = 32         sfx_pldeth,     // deathsound
INT32 speed;         Speed = 3232             0,              // speed
INT32 radius;        Width = 211812352        16*FRACUNIT,    // radius
INT32 height;        Height = 211812352       56*FRACUNIT,    // height
INT32 dispoffset;    DispOffset = 0           0,              // dispoffset
INT32 mass;          Mass = 3232              100,            // mass
INT32 damage;        Missile damage = 3232    0,              // damage
INT32 activesound;   Action sound = 32        sfx_None,       // activesound
INT32 flags;         Bits = 3232              MF_SOLID|MF_SHOOTABLE|MF_DROPOFF|MF_PICKUP|MF_NOTDMATCH,
INT32 raisestate;    Respawn frame = 32       S_NULL          // raisestate
                                         }, */

// These are for clearing all of various things
void clear_emblems(void)
{
	INT32 i;

	for (i = 0; i < MAXEMBLEMS; ++i)
	{
		Z_Free(emblemlocations[i].level);
		emblemlocations[i].level = NULL;

		Z_Free(emblemlocations[i].stringVar);
		emblemlocations[i].stringVar = NULL;
	}

	memset(&emblemlocations, 0, sizeof(emblemlocations));
	numemblems = 0;
}

void clear_unlockables(void)
{
	INT32 i;

	for (i = 0; i < MAXUNLOCKABLES; ++i)
	{
		Z_Free(unlockables[i].stringVar);
		unlockables[i].stringVar = NULL;
	}

	memset(&unlockables, 0, sizeof(unlockables));
}

void clear_conditionsets(void)
{
	UINT8 i;
	for (i = 0; i < MAXCONDITIONSETS; ++i)
		M_ClearConditionSet(i+1);
}

void clear_levels(void)
{
	// This is potentially dangerous but if we're resetting these headers,
	// we may as well try to save some memory, right?
	while (nummapheaders > 0)
	{
		nummapheaders--;

		if (!mapheaderinfo[nummapheaders])
			continue;

		// Custom map header info
		// (no need to set num to 0, we're freeing the entire header shortly)
		Z_Free(mapheaderinfo[nummapheaders]->customopts);

		P_DeleteFlickies(nummapheaders);

		Patch_Free(mapheaderinfo[nummapheaders]->thumbnailPic);
		Patch_Free(mapheaderinfo[nummapheaders]->minimapPic);

		Z_Free(mapheaderinfo[nummapheaders]->lumpname);

		Z_Free(mapheaderinfo[nummapheaders]);
		mapheaderinfo[nummapheaders] = NULL;
	}

	// Clear out the cache
	{
		cupheader_t *cup = kartcupheaders;
		UINT8 i;

		while (cup)
		{
			for (i = 0; i < CUPCACHE_MAX; i++)
			{
				cup->cachedlevels[i] = NEXTMAP_INVALID;
			}
			cup = cup->next;
		}
	}

	// Exit the current gamemap as a safeguard
	if (Playing())
		COM_BufAddText("exitgame"); // Command_ExitGame_f() but delayed
}

// TODO: use this everywhere PLEASE!
static char *parsesocline(char *s)
{
	// First remove trailing newline, if there is one
	char *tmp = strchr(s, '\n');
	if (tmp)
		*tmp = '\0';

	tmp = strchr(s, '#');
	if (tmp)
		*tmp = '\0';
	if (s == tmp)
		return tmp; // Skip comment lines, but don't break.

	// Get the part before the " = "
	tmp = strchr(s, '=');
	if (tmp)
		*(tmp-1) = '\0';
	else
		return NULL;
	strupr(s);

	// Now get the part after
	return tmp + 2;
}

// TODO: Figure out how to do undolines for this....
// TODO: Warnings for running out of freeslots
void readfreeslots(MYFILE *f)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word,*type;
	char *tmp;

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			tmp = strchr(s, '#');
			if (tmp)
				*tmp = '\0';
			if (s == tmp)
				continue; // Skip comment lines, but don't break.

			type = strtok(s, "_");
			if (type)
				strupr(type);
			else
				break;

			word = strtok(NULL, "\n");
			if (word)
				strupr(word);
			else
				break;

			INT32 index;
			if (DEH_ReadFreeslot(type, word, &index) == -2)
				deh_warning("Freeslots: unknown enum class '%s' for '%s_%s'", type, type, word);
		}
	} while (!myfeof(f)); // finish when the line is empty

	Z_Free(s);
}

void readthing(MYFILE *f, INT32 num)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word, *word2;
	char *tmp;

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			tmp = strchr(s, '#');
			if (tmp)
				*tmp = '\0';
			if (s == tmp)
				continue; // Skip comment lines, but don't break.

			word = strtok(s, " ");
			if (word)
				strupr(word);
			else
				break;

			word2 = strtok(NULL, " = ");
			if (word2)
				strupr(word2);
			else
				break;
			if (word2[strlen(word2)-1] == '\n')
				word2[strlen(word2)-1] = '\0';

			if (fastcmp(word, "MAPTHINGNUM") || fastcmp(word, "DOOMEDNUM"))
			{
				mobjinfo[num].doomednum = (INT32)atoi(word2);
			}
			else if (fastcmp(word, "SPAWNSTATE"))
			{
				mobjinfo[num].spawnstate = get_number(word2);
			}
			else if (fastcmp(word, "SPAWNHEALTH"))
			{
				mobjinfo[num].spawnhealth = (INT32)get_number(word2);
			}
			else if (fastcmp(word, "SEESTATE"))
			{
				mobjinfo[num].seestate = get_number(word2);
			}
			else if (fastcmp(word, "SEESOUND"))
			{
				mobjinfo[num].seesound = get_number(word2);
			}
			else if (fastcmp(word, "REACTIONTIME"))
			{
				mobjinfo[num].reactiontime = (INT32)get_number(word2);
			}
			else if (fastcmp(word, "ATTACKSOUND"))
			{
				mobjinfo[num].attacksound = get_number(word2);
			}
			else if (fastcmp(word, "PAINSTATE"))
			{
				mobjinfo[num].painstate = get_number(word2);
			}
			else if (fastcmp(word, "PAINCHANCE"))
			{
				mobjinfo[num].painchance = (INT32)get_number(word2);
			}
			else if (fastcmp(word, "PAINSOUND"))
			{
				mobjinfo[num].painsound = get_number(word2);
			}
			else if (fastcmp(word, "MELEESTATE"))
			{
				mobjinfo[num].meleestate = get_number(word2);
			}
			else if (fastcmp(word, "MISSILESTATE"))
			{
				mobjinfo[num].missilestate = get_number(word2);
			}
			else if (fastcmp(word, "DEATHSTATE"))
			{
				mobjinfo[num].deathstate = get_number(word2);
			}
			else if (fastcmp(word, "DEATHSOUND"))
			{
				mobjinfo[num].deathsound = get_number(word2);
			}
			else if (fastcmp(word, "XDEATHSTATE"))
			{
				mobjinfo[num].xdeathstate = get_number(word2);
			}
			else if (fastcmp(word, "SPEED"))
			{
				mobjinfo[num].speed = get_number(word2);
			}
			else if (fastcmp(word, "RADIUS"))
			{
				mobjinfo[num].radius = get_number(word2);
			}
			else if (fastcmp(word, "HEIGHT"))
			{
				mobjinfo[num].height = get_number(word2);
			}
			else if (fastcmp(word, "DISPOFFSET"))
			{
				mobjinfo[num].dispoffset = get_number(word2);
			}
			else if (fastcmp(word, "MASS"))
			{
				mobjinfo[num].mass = (INT32)get_number(word2);
			}
			else if (fastcmp(word, "DAMAGE"))
			{
				mobjinfo[num].damage = (INT32)get_number(word2);
			}
			else if (fastcmp(word, "ACTIVESOUND"))
			{
				mobjinfo[num].activesound = get_number(word2);
			}
			else if (fastcmp(word, "FLAGS"))
			{
				mobjinfo[num].flags = (INT32)get_number(word2);
			}
			else if (fastcmp(word, "RAISESTATE"))
			{
				mobjinfo[num].raisestate = get_number(word2);
			}
			else
				deh_warning("Thing %d: unknown word '%s'", num, word);
		}
	} while (!myfeof(f)); // finish when the line is empty

	if (wadfiles[f->wad]->compatmode)
		mobjinfo[num].compatmode = true;
	else
		mobjinfo[num].compatmode = false;

	Z_Free(s);
}

void readskincolor(MYFILE *f, INT32 num, boolean mainfile)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word = s;
	char *word2;
	char *tmp;

	Color_cons_t[num].value = num;

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			// First remove trailing newline, if there is one
			tmp = strchr(s, '\n');
			if (tmp)
				*tmp = '\0';

			tmp = strchr(s, '#');
			if (tmp)
				*tmp = '\0';
			if (s == tmp)
				continue; // Skip comment lines, but don't break.

			// Get the part before the " = "
			tmp = strchr(s, '=');
			if (tmp)
				*(tmp-1) = '\0';
			else
				break;
			strupr(word);

			// Now get the part after
			word2 = tmp + 2;

			if (fastcmp(word, "NAME"))
			{
				size_t namesize = sizeof(skincolors[num].name);
				char truncword[namesize];
				UINT16 dupecheck;

				deh_strlcpy(truncword, word2, namesize, va("Skincolor %d: name", num)); // truncate here to check for dupes
				dupecheck = R_GetColorByName(truncword);
				if (truncword[0] != '\0' && (fasticmp(truncword, skincolors[SKINCOLOR_NONE].name) || (dupecheck && dupecheck != num)))
				{
					size_t lastchar = strlen(truncword);
					char oldword[lastchar+1];
					char dupenum = '1';

					strlcpy(oldword, truncword, lastchar+1);
					lastchar--;
					if (lastchar == namesize-2) // exactly max length, replace last character with 0
						truncword[lastchar] = '0';
					else // append 0
					{
						strcat(truncword, "0");
						lastchar++;
					}

					while (R_GetColorByName(truncword))
					{
						truncword[lastchar] = dupenum;
						if (dupenum == '9')
							dupenum = 'A';
						else if (dupenum == 'Z') // give up :?
							break;
						else
							dupenum++;
					}

					deh_warning("Skincolor %d: name %s is a duplicate of another skincolor's name - renamed to %s", num, oldword, truncword);
				}

				strlcpy(skincolors[num].name, truncword, namesize); // already truncated
			}
			else if (fastcmp(word, "RAMP"))
			{
				UINT8 i;
				tmp = strtok(word2,",");
				for (i = 0; i < COLORRAMPSIZE; i++) {
					skincolors[num].ramp[i] = (UINT8)get_number(tmp);
					if ((tmp = strtok(NULL,",")) == NULL)
						break;
				}
				skincolor_modified[num] = true;
			}
			else if (fastcmp(word, "INVCOLOR"))
			{
				UINT16 v = (UINT16)get_number(word2);
				if (v < numskincolors)
					skincolors[num].invcolor = v;
				else
					skincolors[num].invcolor = SKINCOLOR_GREEN;
			}
			else if (fastcmp(word, "INVSHADE"))
			{
				skincolors[num].invshade = get_number(word2)%COLORRAMPSIZE;
			}
			else if (fastcmp(word, "CHATCOLOR"))
			{
				skincolors[num].chatcolor = get_number(word2);
			}
			else if (fastcmp(word, "ACCESSIBLE"))
			{
				if (mainfile || num > FIRSTSUPERCOLOR)
					skincolors[num].accessible = (boolean)(atoi(word2) || word2[0] == 'T' || word2[0] == 'Y');
			}
			else
				deh_warning("Skincolor %d: unknown word '%s'", num, word);
		}
	} while (!myfeof(f)); // finish when the line is empty

	Z_Free(s);
}

void readsprite2(MYFILE *f, INT32 num)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word, *word2;
	char *tmp;

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			tmp = strchr(s, '#');
			if (tmp)
				*tmp = '\0';
			if (s == tmp)
				continue; // Skip comment lines, but don't break.

			word = strtok(s, " ");
			if (word)
				strupr(word);
			else
				break;

			word2 = strtok(NULL, " = ");
			if (word2)
				strupr(word2);
			else
				break;
			if (word2[strlen(word2)-1] == '\n')
				word2[strlen(word2)-1] = '\0';

			if (fastcmp(word, "DEFAULT"))
				spr2defaults[num] = get_number(word2);
			else
				deh_warning("Sprite2 %s: unknown word '%s'", spr2names[num], word);
		}
	} while (!myfeof(f)); // finish when the line is empty

	Z_Free(s);
}

// copypasted from readPlayer :]
void readgametype(MYFILE *f, char *gtname)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word;
	char *word2, *word2lwr = NULL;
	char *tmp;
	INT32 i, j;

	gametype_t *newgametype = NULL;
	UINT32 newgtrules = 0;
	UINT32 newgttol = 0;
	INT32 newgtpointlimit = 0;
	INT32 newgttimelimit = 0;
	INT32 newgtcolor = V_YELLOWMAP;
	UINT8 newgtinttype = 0;
	char gtconst[MAXLINELEN];

	// Empty strings.
	gtconst[0] = '\0';

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			word = strtok(s, " ");
			if (word)
				strupr(word);
			else
				break;

			word2 = strtok(NULL, " = ");
			if (word2)
			{
				if (!word2lwr)
					word2lwr = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
				strcpy(word2lwr, word2);
				strupr(word2);
			}
			else
				break;

			if (word2[strlen(word2)-1] == '\n')
				word2[strlen(word2)-1] = '\0';
			i = atoi(word2);

			// Game type rules
			if (fastcmp(word, "RULES"))
			{
				// GTR_
				newgtrules = (UINT32)get_number(word2);
			}
			// Identifier
			else if (fastcmp(word, "IDENTIFIER"))
			{
				// GT_
				strncpy(gtconst, word2, MAXLINELEN);
			}
			// Point and time limits
			else if (fastcmp(word, "DEFAULTPOINTLIMIT"))
				newgtpointlimit = (INT32)i;
			else if (fastcmp(word, "DEFAULTTIMELIMIT"))
				newgttimelimit = (INT32)i;
			// Intermission type
			else if (fastcmp(word, "INTERMISSIONTYPE"))
			{
				// Case sensitive
				newgtinttype = (int)get_number(word2lwr);
			}
			// Type of level
			else if (fastcmp(word, "TYPEOFLEVEL"))
			{
				if (i) // it's just a number
					newgttol = (UINT32)i;
				else
				{
					UINT32 tol = 0;
					tmp = strtok(word2,",");
					do {
						for (i = 0; TYPEOFLEVEL[i].name; i++)
							if (fasticmp(tmp, TYPEOFLEVEL[i].name))
								break;
						if (!TYPEOFLEVEL[i].name)
							deh_warning("readgametype %s: unknown typeoflevel flag %s\n", gtname, tmp);
						tol |= TYPEOFLEVEL[i].flag;
					} while((tmp = strtok(NULL,",")) != NULL);
					newgttol = tol;
				}
			}
			// Menu Color
			else if (fastcmp(word, "MENUCOLOR"))
			{
				INT32 color = (INT32)get_number(word2);
				// Mask out other flags so they aren't passed into menu code.
				newgtcolor = color & V_CHARCOLORMASK; // V_
			}
			// The SOC probably provided gametype rules as words,
			// instead of using the RULES keyword.
			// Like for example "NOSPECTATORSPAWN = TRUE".
			// This is completely valid, and looks better anyway.
			else
			{
				UINT32 wordgt = 0;
				for (j = 0; GAMETYPERULE_LIST[j]; j++)
					if (fastcmp(word, GAMETYPERULE_LIST[j])) {
						wordgt |= (1<<j);
						if (i || word2[0] == 'T' || word2[0] == 'Y')
							newgtrules |= wordgt;
						break;
					}
				if (!wordgt)
					deh_warning("readgametype %s: unknown word '%s'", gtname, word);
			}
		}
	} while (!myfeof(f)); // finish when the line is empty

	// Free strings.
	Z_Free(s);
	if (word2lwr)
		Z_Free(word2lwr);

	// Ran out of gametype slots
	if (numgametypes == GT_LASTFREESLOT)
	{
		I_Error("Out of Gametype Freeslots while allocating \"%s\"\nLoad less addons to fix this.", gtname);
	}

	// Add the new gametype
	newgametype = Z_Calloc(sizeof (gametype_t), PU_STATIC, NULL);
	if (!newgametype)
	{
		I_Error("Out of memory allocating gametype \"%s\"", gtname);
	}

	if (gtconst[0] == '\0')
		strncpy(gtconst, gtname, MAXLINELEN);

	newgametype->name = Z_StrDup((const char *)gtname);
	newgametype->rules = newgtrules;
	newgametype->constant = G_PrepareGametypeConstant((const char *)gtconst);
	newgametype->tol = newgttol;
	newgametype->intermission = newgtinttype;
	newgametype->pointlimit = newgtpointlimit;
	newgametype->timelimit = newgttimelimit;
	newgametype->color = newgtcolor;

	gametypes[numgametypes++] = newgametype;

	// Update gametype_cons_t accordingly.
	G_UpdateGametypeSelections();

	CONS_Printf("Added gametype %s\n", gtname);
}

static mapheader_lighting_t *usemaplighting(mapnum_t mapnum, const char *word)
{
	if (fastncmp(word, "ENCORE", 6))
	{
		mapheaderinfo[mapnum]->use_encore_lighting = true;

		return &mapheaderinfo[mapnum]->lighting_encore;
	}
	else
	{
		return &mapheaderinfo[mapnum]->lighting;
	}
}

#define OUTOFRANGE(name, value, min, max) deh_warning("Level header %d: EFX parameter %s is out of range. (value:%d min:%d max:%d)\n", mapnum, name, value, min, max)

#define OUTOFRANGE_FLOAT(name, value, min, max) deh_warning("Level header %d: EFX parameter %s is out of range. (value:%f min:%f max:%f)\n", mapnum, name, value, min, max)

#define OUTOFRANGE_CONSTANT(name, valuename, minname, maxname) deh_warning("Level header %d: EFX parameter %s is out of range. (value:%s min:%s max:%s)\n", mapnum, name, valuename, minname, maxname)

#define FIELDSET(name, field, value, min, max) \
if (fastcmp(word, name)) \
{ \
	if (value >= min && value <= max) \
		mapheaderinfo[mapnum]->globalEFX->field = value; \
		else \
			OUTOFRANGE(name, value, min, max); \
}

#define FIELDSET_FLOAT(name, field, value, min, max) \
if (fastcmp(word, name)) \
{ \
	if (value >= min && value <= max) \
		mapheaderinfo[mapnum]->globalEFX->field = value; \
	else \
		OUTOFRANGE_FLOAT(name, value, min, max); \
}

#define FIELDSET_FVEC3(name, field, value, min, max) \
if (fastcmp(word, name)) \
{ \
	processvector(name, mapnum, value, &mapheaderinfo[mapnum]->globalEFX->field, min, max); \
}

#define FIELDSET_BOOL(name, field, value, word2) \
if (fastcmp(word, name)) \
{ \
	if (value || word2[0] == 'T' || word2[0] == 'O') \
		mapheaderinfo[mapnum]->globalEFX->field = 1; \
	else \
		mapheaderinfo[mapnum]->globalEFX->field = 0; \
}

#define FIELDSET_CONSTANT(name, minname, maxname, field, word2, min, max) \
if (fastcmp(word, name)) \
{ \
	INT32 constnum = get_number(word2); \
	if (constnum >= min && constnum <= max) \
		mapheaderinfo[mapnum]->globalEFX->field = constnum; \
	else \
		OUTOFRANGE_CONSTANT(name, word2, minname, maxname); \
}

static void processvector(const char* name, mapnum_t mapnum, char *word2, f_vector3_t *vec, float min, float max)
{
	char *tmp = strtok(word2,",");
	float vector[3];
	int i = 0;

	do {
		if (i >= 3)
			break;
		float value = atof(tmp);
		if (value >= min && value <= max)
			vector[i++] = value;
		else
		{
			deh_warning("Level header %d: EFX parameter %d of 3 of is out of range for %s. (value:%f min:%f max:%f)\n", mapnum, i+1, name, value, min, max);
			return;
		}
	} while ((tmp = strtok(NULL, ",")) != NULL);

	vec->x = vector[0];
	vec->y = vector[1];
	vec->z = vector[2];
}

// Handle parsing globalefx info
static void globalmapefxparameters(mapnum_t mapnum, INT32 i, char *word, char *word2)
{
	// Take off GLOBALEFX_
	word += 10;

	// this is a float.
	float j = atof(word2);

	// Check and take off effect name
	if (fastncmp(word, "EAXREVERB_", 10))
	{
		word += 10;

		FIELDSET_FLOAT("DENSITY", eaxreverb.density, j, 0.0f, 1.0f)
		else FIELDSET_FLOAT("DIFFUSION", eaxreverb.diffusion, j, 0.0f, 1.0f)
		else FIELDSET_FLOAT("GAIN", eaxreverb.gain, j, 0.0f, 1.0f)
		else FIELDSET_FLOAT("GAINHF", eaxreverb.gainhf, j, 0.0f, 1.0f)
		else FIELDSET_FLOAT("GAINLF", eaxreverb.gainlf, j, 0.0f, 1.0f)
		else FIELDSET_FLOAT("DECAY_TIME", eaxreverb.decay_time, j, 0.1f, 20.0f)
		else FIELDSET_FLOAT("DECAY_HFRATIO", eaxreverb.decay_hfratio, j, 0.1f, 2.0f)
		else FIELDSET_FLOAT("DECAY_LFRATIO", eaxreverb.decay_lfratio, j, 0.1f, 2.0f)
		else FIELDSET_FLOAT("REFLECTIONS_GAIN", eaxreverb.reflections_gain, j, 0.0f, 3.16f)
		else FIELDSET_FLOAT("REFLECTIONS_DELAY", eaxreverb.reflections_delay, j, 0.0f, 0.3f)
		else FIELDSET_FVEC3("REFLECTIONS_PAN", eaxreverb.reflections_pan, word2, -1.0f, 1.0f)
		else FIELDSET_FLOAT("LATE_REVERB_GAIN", eaxreverb.late_reverb_gain, j, 0.0f, 10.0f)
		else FIELDSET_FLOAT("LATE_REVERB_DELAY", eaxreverb.late_reverb_delay, j, 0.0f, 0.1f)
		else FIELDSET_FVEC3("LATE_REVERB_PAN", eaxreverb.late_reverb_pan, word2, -1.0f, 1.0f)
		else FIELDSET_FLOAT("ECHO_TIME", eaxreverb.echo_time, j, 0.075f, 0.25f)
		else FIELDSET_FLOAT("ECHO_DEPTH", eaxreverb.echo_depth, j, 0.0f, 1.0f)
		else FIELDSET_FLOAT("MODULATION_TIME", eaxreverb.modulation_time, j, 0.04f, 4.0f)
		else FIELDSET_FLOAT("MODULATION_DEPTH", eaxreverb.modulation_depth, j, 0.0f, 1.0f)
		else FIELDSET_FLOAT("AIR_ABSORPTION_GAINHF", eaxreverb.air_absorption_gainhf, j, 0.892f, 1.0f)
		else FIELDSET_FLOAT("HFREFERENCE", eaxreverb.hfreference, j, 1000.0f, 20000.0f)
		else FIELDSET_FLOAT("LFREFERENCE", eaxreverb.lfreference, j, 20.0f, 1000.0f)
		else FIELDSET_FLOAT("ROOM_ROLLOFF_FACTOR", eaxreverb.room_rolloff_factor, j, 0.0f, 10.0f)
		else FIELDSET_BOOL("DECAY_HFLIMIT", eaxreverb.decay_hflimit, i, word2)
	}
	else if (fastncmp(word, "REVERB_", 7))
	{
		word += 7;

		FIELDSET_FLOAT("DENSITY", reverb.density, j, 0.0f, 1.0f)
		else FIELDSET_FLOAT("DIFFUSION", reverb.diffusion, j, 0.0f, 1.0f)
		else FIELDSET_FLOAT("GAIN", reverb.gain, j, 0.0f, 1.0f)
		else FIELDSET_FLOAT("GAINHF", reverb.gainhf, j, 0.0f, 1.0f)
		else FIELDSET_FLOAT("DECAY_TIME", reverb.decay_time, j, 0.1f, 20.0f)
		else FIELDSET_FLOAT("DECAY_HFRATIO", reverb.decay_hfratio, j, 0.1f, 2.0f)
		else FIELDSET_FLOAT("REFLECTIONS_GAIN", reverb.reflections_gain, j, 0.0f, 3.16f)
		else FIELDSET_FLOAT("REFLECTIONS_DELAY", reverb.reflections_delay, j, 0.0f, 0.3f)
		else FIELDSET_FLOAT("LATE_REVERB_GAIN", reverb.late_reverb_gain, j, 0.0f, 10.0f)
		else FIELDSET_FLOAT("LATE_REVERB_DELAY", reverb.late_reverb_delay, j, 0.0f, 0.1f)
		else FIELDSET_FLOAT("AIR_ABSORPTION_GAINHF", reverb.air_absorption_gainhf, j, 0.892f, 1.0f)
		else FIELDSET_FLOAT("ROOM_ROLLOFF_FACTOR", reverb.room_rolloff_factor, j, 0.0f, 10.0f)
		else FIELDSET_BOOL("DECAY_HFLIMIT", reverb.decay_hflimit, i, word2)
	}
	else if (fastncmp(word, "CHORUS_", 7))
	{
		word += 7;

		FIELDSET_CONSTANT("WAVEFORM", "CHORUSWAVE_SINUSOID", "CHORUSWAVE_TRIANGLE", chorus.waveform, word2, CHORUSWAVE_SINUSOID, CHORUSWAVE_TRIANGLE)
		else FIELDSET("PHASE", chorus.phase, i, -180, 180)
		else FIELDSET_FLOAT("RATE", chorus.rate, j, 0.0f, 10.0f)
		else FIELDSET_FLOAT("DEPTH", chorus.depth, j, 0.0f, 1.0f)
		else FIELDSET_FLOAT("FEEDBACK", chorus.feedback, j, -1.0f, 1.0f)
		else FIELDSET_FLOAT("DELAY", chorus.delay, j, 0.0f, 0.016f)
	}
	else if (fastncmp(word, "DISTORTION_", 11))
	{
		word += 11;

		FIELDSET_FLOAT("EDGE", distortion.edge, j, 0.0f, 1.0f)
		else FIELDSET_FLOAT("GAIN", distortion.gain, j, 0.01f, 1.0f)
		else FIELDSET_FLOAT("LOWPASS_CUTOFF", distortion.lowpass_cutoff, j, 80.0f, 24000.0f)
		else FIELDSET_FLOAT("EQCENTER", distortion.eqcenter, j, 80.0f, 24000.0f)
		else FIELDSET_FLOAT("EQBANDWIDTH", distortion.eqbandwidth, j, 80.0f, 24000.0f)
	}
	else if (fastncmp(word, "ECHO_", 5))
	{
		word += 5;

		FIELDSET_FLOAT("DELAY", echo.delay, j, 0.0f, 0.207f)
		else FIELDSET_FLOAT("LRDELAY", echo.lrdelay, j, 0.0f, 0.404f)
		else FIELDSET_FLOAT("DAMPING", echo.damping, j, 0.0f, 0.99f)
		else FIELDSET_FLOAT("FEEDBACK", echo.feedback, j, 0.0f, 1.0f)
		else FIELDSET_FLOAT("SPREAD", echo.spread, j, -1.0f, 1.0f)
	}
	else if (fastncmp(word, "FLANGER_", 8))
	{
		word += 8;

		FIELDSET_CONSTANT("WAVEFORM", "FLANGERWAVE_SINUSOID", "FLANGERWAVE_TRIANGLE", flanger.waveform, word2, FLANGERWAVE_SINUSOID, FLANGERWAVE_TRIANGLE)
		else FIELDSET("PHASE", flanger.phase, i, -180, 180)
		else FIELDSET_FLOAT("RATE", flanger.rate, j, 0.0f, 10.0f)
		else FIELDSET_FLOAT("DEPTH", flanger.depth, j, 0.0f, 1.0f)
		else FIELDSET_FLOAT("FEEDBACK", flanger.feedback, j, -1.0f, 1.0f)
		else FIELDSET_FLOAT("DELAY", flanger.delay, j, 0.0f, 0.004f)
	}
	else if (fastncmp(word, "FREQSHIFT_", 10))
	{
		word += 10;

		FIELDSET_FLOAT("FREQUENCY", freqshift.frequency, j, 0.0f, 24000.0f)
		else FIELDSET_CONSTANT("LEFT_DIRECTION", "FREQSHIFTDIR_DOWN", "FREQSHIFTDIR_OFF", freqshift.left_direction, word2, FREQSHIFTDIR_DOWN, FREQSHIFTDIR_OFF)
		else FIELDSET_CONSTANT("RIGHT_DIRECTION", "FREQSHIFTDIR_DOWN", "FREQSHIFTDIR_OFF", freqshift.right_direction, word2, FREQSHIFTDIR_DOWN, FREQSHIFTDIR_OFF)
	}
	else if (fastncmp(word, "VOCALMORPH_", 11))
	{
		word += 11;

		FIELDSET("PHONEMEA", vocal_morpher.phonemea, i, 0, 29)
		else FIELDSET("PHONEMEB", vocal_morpher.phonemeb, i, 0, 29)
		else FIELDSET("PHONEMEA_COARSE_TUNING", vocal_morpher.phonemea_coarse_tuning, i, -24, 24)
		else FIELDSET("PHONEMEB_COARSE_TUNING", vocal_morpher.phonemeb_coarse_tuning, i, -24, 24)
		else FIELDSET_CONSTANT("WAVEFORM", "VOCALMORPH_SIN", "VOCALMORPH_SAW", flanger.waveform, word2, VOCALMORPH_SIN, VOCALMORPH_SAW)
		else FIELDSET_FLOAT("RATE", vocal_morpher.rate, j, 0.0f, 10.0f)
	}
	else if (fastncmp(word, "PITCHSHIFT_", 11))
	{
		word += 11;

		FIELDSET("COARSE_TUNE", pitchshift.coarse_tune, i, -12, 12)
		else FIELDSET("FINE_TUNE", pitchshift.fine_tune, i, -50, 50)
	}
	else if (fastncmp(word, "RINGMOD_", 8))
	{
		word += 8;

		FIELDSET_FLOAT("FREQUENCY", ringmod.frequency, j, 0.0f, 8000.0f)
		else FIELDSET_FLOAT("HIGHPASS_CUTOFF", ringmod.highpass_cutoff, j, 0.0f, 24000.0f)
		else FIELDSET_CONSTANT("WAVEFORM", "RINGMOD_SIN", "RINGMOD_SQUARE", ringmod.waveform, word2, RINGMOD_SIN, RINGMOD_SQUARE)
	}
	else if (fastncmp(word, "AUTOWAH_", 8))
	{
		word += 8;

		FIELDSET_FLOAT("ATTACK_TIME", autowah.attack_time, j, 0.0001f, 1.0f)
		else FIELDSET_FLOAT("RELEASE_TIME", autowah.release_time, j, 0.0001f, 1.0f)
		else FIELDSET_FLOAT("RESONANCE", autowah.resonance, j, 2.0f, 1000.0f)
		else FIELDSET_FLOAT("PEAK_GAIN", autowah.peak_gain, j, 0.00003f, 31621.0f)
	}
	else if (fastncmp(word, "COMPRESSOR_", 11))
	{
		word += 11;

		FIELDSET_BOOL("ONOFF", compressor.onoff, i, word2)
	}
	else if (fastncmp(word, "EQ_", 3))
	{
		word += 3;

		FIELDSET_FLOAT("LOW_GAIN", eq.low_gain, j, 0.126f, 7.943f)
		else FIELDSET_FLOAT("LOW_CUTOFF", eq.low_cutoff, j, 50.0f, 800.0f)
		else FIELDSET_FLOAT("MID1_GAIN", eq.mid1_gain, j, 0.126f, 7.943f)
		else FIELDSET_FLOAT("MID1_CENTER", eq.mid1_center, j, 200.0f, 3000.0f)
		else FIELDSET_FLOAT("MID1_WIDTH", eq.mid1_width, j, 0.01f, 1.0f)
		else FIELDSET_FLOAT("MID2_GAIN", eq.mid2_gain, j, 0.126f, 7.943f)
		else FIELDSET_FLOAT("MID2_CENTER", eq.mid2_center, j, 1000.0f, 8000.0f)
		else FIELDSET_FLOAT("MID2_WIDTH", eq.mid2_width, j, 0.01f, 1.0f)
		else FIELDSET_FLOAT("HIGH_GAIN", eq.high_gain, j, 0.126f, 7.943f)
		else FIELDSET_FLOAT("HIGH_CUTOFF", eq.high_cutoff, j, 4000.0f, 16000.0f)
	}
}

void readlevelheader(MYFILE *f, char * name)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);

	char *word;
	char *word2;
	//char *word3; // Non-uppercase version of word2

	char *tmp;
	INT32 i;

	// convert old map names to MAPxx
	INT32 exnum = atoi(name);
	if (exnum > 0 && exnum <= NUMMAPS)
	{
		if (exnum < 100)
			name = va("MAP%02d", exnum);
		else
			// one liner extended names :^)
			name = va("MAP%c%c", 'A' + (exnum - 100)/36, ((exnum - 100) % 36 < 10 ? '0' : 'A'-10) + (exnum - 100) % 36);
	}
	else if (strlen(name) == 2 && isalpha(name[0]) && isalnum(name[1]))
	{
		exnum = (name[0] - 'A')*36 + (isalpha(name[1]) ? name[1] - 'A'+10 : name[1] - '0') + 100;
		name = va("MAP%s", name);
	}

	mapnum_t num = G_MapNumber(name);

	if (num >= nummapheaders)
	{
		P_AllocMapHeader((INT16)(num = nummapheaders));
	}
	else if (f->wad >= NUMMAINWADS)
	{
		// only mark as a major mod if it replaces an already-existing mapheaderinfo
		G_SetGameModified(multiplayer, true);
	}

	// compatmode map number system:
	// 0-1034 correspond to MAP01-MAPZZ,
	// longname maps are appended starting at NUMMAPS
	if (!exnum)
	{
		exnum = ++nextexnum;
		if (exnum >= NEXTMAP_SPECIAL)
			I_Error("Ran out of compatibility map slots");
	}
	kartmap2native[exnum-1] = num+1;
	nativemap2kart[num] = exnum-1;

	if (mapheaderinfo[num]->lumpname == NULL)
	{
		mapheaderinfo[num]->lumpname = Z_StrDup(name);
		mapheaderinfo[num]->lumpnamehash = FNV1a_QuickCaseHash(mapheaderinfo[num]->lumpname, MAXMAPLUMPNAME);
	}

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			// First remove trailing newline, if there is one
			tmp = strchr(s, '\n');
			if (tmp)
				*tmp = '\0';

			tmp = strchr(s, '#');
			if (tmp)
				*tmp = '\0';
			if (s == tmp)
				continue; // Skip comment lines, but don't break.

			// Set / reset word, because some things (Lua.) move it
			word = s;

			// Get the part before the " = "
			tmp = strchr(s, '=');
			if (tmp)
				*(tmp-1) = '\0';
			else
				break;
			strupr(word);

			// Now get the part after
			word2 = tmp + 2;
			i = atoi(word2); // used for numerical settings

			// The following support mixed/lower case.

			if (fastcmp(word, "LEVELNAME"))
			{
				deh_strlcpy(mapheaderinfo[num]->lvlttl, word2,
					sizeof(mapheaderinfo[num]->lvlttl), va("Level header %d: levelname", num));
				continue;
			}

			if (fastcmp(word, "ZONETITLE"))
			{
				deh_strlcpy(mapheaderinfo[num]->zonttl, word2,
					sizeof(mapheaderinfo[num]->zonttl), va("Level header %d: zonetitle", num));
				continue;
			}

			if (fastcmp(word, "SUBTITLE"))
			{
				deh_strlcpy(mapheaderinfo[num]->subttl, word2,
					sizeof(mapheaderinfo[num]->subttl), va("Level header %d: subtitle", num));
				continue;
			}

			// Lua custom options also go above, contents may be case sensitive.
			if (fastncmp(word, "LUA.", 4))
			{
				UINT8 j;
				customoption_t *modoption;

				// Note: we actualy strlwr word here, so things are made a little easier for Lua
				strlwr(word);
				word += 4; // move past "lua."

				// ... and do a simple name sanity check; the name must start with a letter
				if (*word < 'a' || *word > 'z')
				{
					deh_warning("Level header %d: invalid custom option name \"%s\"", num, word);
					continue;
				}

				// Sanity limit of 128 params
				if (mapheaderinfo[num]->numCustomOptions == 128)
				{
					deh_warning("Level header %d: too many custom parameters", num);
					continue;
				}
				j = mapheaderinfo[num]->numCustomOptions++;

				mapheaderinfo[num]->customopts =
					Z_Realloc(mapheaderinfo[num]->customopts,
						sizeof(customoption_t) * mapheaderinfo[num]->numCustomOptions, PU_STATIC, NULL);

				// Newly allocated
				modoption = &mapheaderinfo[num]->customopts[j];

				strncpy(modoption->option, word,  31);
				modoption->option[31] = '\0';
				strncpy(modoption->value,  word2, 255);
				modoption->value[255] = '\0';
				continue;
			}

			// Now go to uppercase
			strupr(word2);

			// List of flickies that are be freed in this map
			if (fastcmp(word, "FLICKYLIST") || fastcmp(word, "ANIMALLIST"))
			{
				if (fastcmp(word2, "NONE"))
					P_DeleteFlickies(num);
				else if (fastcmp(word2, "DEMO"))
					P_SetDemoFlickies(num);
				else if (fastcmp(word2, "ALL"))
				{
					mobjtype_t tmpflickies[MAXFLICKIES];

					for (mapheaderinfo[num]->numFlickies = 0;
					((mapheaderinfo[num]->numFlickies < MAXFLICKIES) && FLICKYTYPES[mapheaderinfo[num]->numFlickies].type);
					mapheaderinfo[num]->numFlickies++)
						tmpflickies[mapheaderinfo[num]->numFlickies] = FLICKYTYPES[mapheaderinfo[num]->numFlickies].type;

					if (mapheaderinfo[num]->numFlickies) // just in case...
					{
						size_t newsize = sizeof(mobjtype_t) * mapheaderinfo[num]->numFlickies;
						mapheaderinfo[num]->flickies = Z_Realloc(mapheaderinfo[num]->flickies, newsize, PU_STATIC, NULL);
						memcpy(mapheaderinfo[num]->flickies, tmpflickies, newsize);
					}
				}
				else
				{
					mobjtype_t tmpflickies[MAXFLICKIES];
					mapheaderinfo[num]->numFlickies = 0;
					tmp = strtok(word2,",");
					// get up to the first MAXFLICKIES flickies
					do {
						if (mapheaderinfo[num]->numFlickies == MAXFLICKIES) // never going to get above that number
						{
							deh_warning("Level header %d: too many flickies\n", num);
							break;
						}

						if (fastncmp(tmp, "MT_", 3)) // support for specified mobjtypes...
						{
							i = get_mobjtype(tmp);
							if (i == NUMMOBJTYPES)
							{
								//deh_warning("Level header %d: unknown flicky mobj type %s\n", num, tmp); -- no need for this line as get_mobjtype complains too
								continue;
							}
							tmpflickies[mapheaderinfo[num]->numFlickies] = i;
						}
						else // ...or a quick, limited selection of default flickies!
						{
							for (i = 0; FLICKYTYPES[i].name; i++)
								if (fastcmp(tmp, FLICKYTYPES[i].name))
									break;

							if (!FLICKYTYPES[i].name)
							{
								deh_warning("Level header %d: unknown flicky selection %s\n", num, tmp);
								continue;
							}
							tmpflickies[mapheaderinfo[num]->numFlickies] = FLICKYTYPES[i].type;
						}
						mapheaderinfo[num]->numFlickies++;
					} while ((tmp = strtok(NULL,",")) != NULL);

					if (mapheaderinfo[num]->numFlickies)
					{
						size_t newsize = sizeof(mobjtype_t) * mapheaderinfo[num]->numFlickies;
						mapheaderinfo[num]->flickies = Z_Realloc(mapheaderinfo[num]->flickies, newsize, PU_STATIC, NULL);
						// now we add them to the list!
						memcpy(mapheaderinfo[num]->flickies, tmpflickies, newsize);
					}
					else
						deh_warning("Level header %d: no valid flicky types found\n", num);
				}
			}

			// Strings that can be truncated
			else if (fastcmp(word, "SCRIPTNAME"))
			{
				deh_strlcpy(mapheaderinfo[num]->scriptname, word2,
					sizeof(mapheaderinfo[num]->scriptname), va("Level header %d: scriptname", num));
			}
			else if (fastcmp(word, "RUNSOC"))
			{
				deh_strlcpy(mapheaderinfo[num]->runsoc, word2,
					sizeof(mapheaderinfo[num]->runsoc), va("Level header %d: runsoc", num));
			}
			else if (fastcmp(word, "ACT"))
			{
				/*if (i >= 0 && i < 20) // 0 for no act number, TTL1 through TTL19
					mapheaderinfo[num]->actnum = (UINT8)i;
				else
					deh_warning("Level header %d: invalid act number %d", num, i);*/
				deh_strlcpy(mapheaderinfo[num]->actnum, word2,
					sizeof(mapheaderinfo[num]->actnum), va("Level header %d: actnum", num));
			}
			else if (fastcmp(word, "TYPEOFLEVEL"))
			{
				if (fastcmp(word2, "NONE")) // No type of level? Sure!
					mapheaderinfo[num]->typeoflevel = 0;
				else if (i) // it's just a number
					mapheaderinfo[num]->typeoflevel = (UINT32)i;
				else
				{
					UINT32 tol = 0;
					tmp = strtok(word2,",");
					do {
						for (i = 0; TYPEOFLEVEL[i].name; i++)
							if (fastcmp(tmp, TYPEOFLEVEL[i].name))
								break;
						if (!TYPEOFLEVEL[i].name)
							deh_warning("Level header %d: unknown typeoflevel flag %s\n", num, tmp);
						tol |= TYPEOFLEVEL[i].flag;
					} while((tmp = strtok(NULL,",")) != NULL);
					mapheaderinfo[num]->typeoflevel = tol;
				}
			}
			else if (fastcmp(word, "KEYWORDS"))
			{
				deh_strlcpy(mapheaderinfo[num]->keywords, word2,
						sizeof(mapheaderinfo[num]->keywords), va("Level header %d: keywords", num));
			}
			else if (fastcmp(word, "MUSIC"))
			{
				if (fastcmp(word2, "NONE"))
				{
					mapheaderinfo[num]->musname[0][0] = 0; // becomes empty string
					mapheaderinfo[num]->musname_size = 0;
				}
				else
				{
					UINT8 j = 0; // i was declared elsewhere
					tmp = strtok(word2, ",");
					do {
						if (j >= MAXMUSNAMES)
							break;
						deh_strlcpy(mapheaderinfo[num]->musname[j], tmp,
							sizeof(mapheaderinfo[num]->musname[j]), va("Level header %d: music", num));
						j++;
					} while ((tmp = strtok(NULL,",")) != NULL);
					
					if (tmp != NULL)
						deh_warning("Level header %d: additional music slots past %d discarded", num, MAXMUSNAMES);
					mapheaderinfo[num]->musname_size = j;
				}
			}
			else if (fastcmp(word, "ENCOREMUSIC"))
			{
				if (fastcmp(word2, "NONE"))
				{
					mapheaderinfo[num]->encoremusname[0][0] = 0; // becomes empty string
					mapheaderinfo[num]->encoremusname_size = 0;
				}
				else
				{
					UINT8 j = 0; // i was declared elsewhere
					tmp = strtok(word2, ",");
					do {
						if (j >= MAXMUSNAMES)
							break;
						deh_strlcpy(mapheaderinfo[num]->encoremusname[j], tmp,
									sizeof(mapheaderinfo[num]->encoremusname[j]), va("Level header %d: encore music", num));
						j++;
					} while ((tmp = strtok(NULL,",")) != NULL);

					if (tmp != NULL)
						deh_warning("Level header %d: additional music slots past %d discarded", num, MAXMUSNAMES);
					mapheaderinfo[num]->encoremusname_size = j;
				}
			}
			else if (fastcmp(word, "MUSICSLOT"))
				deh_warning("Level header %d: MusicSlot parameter is deprecated and will be removed.\nUse \"Music\" instead.", num);
			else if (fastcmp(word, "MUSICTRACK"))
				mapheaderinfo[num]->mustrack = ((UINT16)i - 1);
			else if (fastcmp(word, "MUSICPOS"))
				mapheaderinfo[num]->muspos = (UINT32)get_number(word2);
			else if (fastcmp(word, "WEATHER"))
				mapheaderinfo[num]->weather = get_precip(word2);
			else if (fastcmp(word, "SKYTEXTURE"))
				deh_strlcpy(mapheaderinfo[num]->skytexture, word2,
					sizeof(mapheaderinfo[num]->skytexture), va("Level header %d: sky texture", num));
			else if (fastcmp(word, "SKYNUM"))
			{
				char namebuf[9];

				sprintf(namebuf, "SKY%.5s", word2);
				deh_strlcpy(mapheaderinfo[num]->skytexture, namebuf,
					sizeof(mapheaderinfo[num]->skytexture), va("Level header %d: sky texture", num));
			}
			else if (fastcmp(word, "PRECUTSCENENUM"))
				mapheaderinfo[num]->precutscenenum = (UINT8)i;
			else if (fastcmp(word, "CUTSCENENUM"))
				mapheaderinfo[num]->cutscenenum = (UINT8)i;
			else if (fastcmp(word, "PALETTE"))
				mapheaderinfo[num]->palette = (UINT16)i;
			else if (fastcmp(word, "ENCOREPAL"))
				mapheaderinfo[num]->encorepal = (UINT16)i;
			else if (fastcmp(word, "NUMLAPS"))
				mapheaderinfo[num]->numlaps = (UINT8)i;
			else if (fastcmp(word, "BASETRACKCOMPLEXITY"))
				mapheaderinfo[num]->base_track_complexity = i;
			else if (fastcmp(word, "TRACKMODIFIERMAX"))
				mapheaderinfo[num]->track_modifier_max = FloatToFixed(atof(word2));
			else if (fastcmp(word, "LAPSPERSECTION"))
				mapheaderinfo[num]->lapspersection = max((UINT8)i, 1u);
			else if (fastcmp(word, "UNLOCKABLE"))
			{
				if (i >= 0 && i <= MAXUNLOCKABLES) // 0 for no unlock required, anything else requires something
					mapheaderinfo[num]->unlockrequired = (SINT8)i - 1;
				else
					deh_warning("Level header %d: invalid unlockable number %d", num, i);
			}
			else if (fastcmp(word, "LEVELSELECT"))
				mapheaderinfo[num]->levelselect = (UINT8)i;
			else if (fastcmp(word, "SKYBOXSCALE"))
				mapheaderinfo[num]->skybox_scalex = mapheaderinfo[num]->skybox_scaley = mapheaderinfo[num]->skybox_scalez = (INT16)i;
			else if (fastcmp(word, "SKYBOXSCALEX"))
				mapheaderinfo[num]->skybox_scalex = (INT16)i;
			else if (fastcmp(word, "SKYBOXSCALEY"))
				mapheaderinfo[num]->skybox_scaley = (INT16)i;
			else if (fastcmp(word, "SKYBOXSCALEZ"))
				mapheaderinfo[num]->skybox_scalez = (INT16)i;
			else if (fastcmp(word, "LEVELFLAGS"))
				mapheaderinfo[num]->levelflags = get_number(word2);
			else if (fastcmp(word, "MENUFLAGS"))
				mapheaderinfo[num]->menuflags = get_number(word2);
			// SRB2Kart
			else if (fastcmp(word, "MOBJSCALE"))
				mapheaderinfo[num]->mobj_scale = get_number(word2);
			else if (fastcmp(word, "DEFAULTWAYPOINTRADIUS"))
				mapheaderinfo[num]->default_waypoint_radius = get_number(word2);
			else if (fastcmp(word, "LIGHTCONTRAST"))
			{
				usemaplighting(num, word)->light_contrast = (UINT8)i;
				usemaplighting(num, word)->use_custom_light = true;
			}
			else if (fastcmp(word, "SPRITEBACKLIGHT") || fastcmp(word, "ENCORESPRITEBACKLIGHT"))
			{
				usemaplighting(num, word)->sprite_backlight = (SINT8)i;
				usemaplighting(num, word)->use_custom_light = true;
			}
			else if (fastcmp(word, "LIGHTANGLE") || fastcmp(word, "ENCORELIGHTANGLE"))
			{
				mapheader_lighting_t *lighting = usemaplighting(num, word);

				if (fastcmp(word2, "EVEN"))
				{
					lighting->use_light_angle = false;
					lighting->light_angle = 0;
				}
				else
				{
					lighting->use_light_angle = true;
					lighting->light_angle = FixedAngle(FloatToFixed(atof(word2)));
				}
				usemaplighting(num, word)->use_custom_light = true;
			}
			// Individual triggers for level flags, for ease of use (and 2.0 compatibility)
			else if (fastcmp(word, "SCRIPTISFILE"))
			{
				if (i || word2[0] == 'T' || word2[0] == 'Y')
					mapheaderinfo[num]->levelflags |= LF_SCRIPTISFILE;
				else
					mapheaderinfo[num]->levelflags &= ~LF_SCRIPTISFILE;
			}
			else if (fastcmp(word, "NOZONE"))
			{
				if (i || word2[0] == 'T' || word2[0] == 'Y')
					mapheaderinfo[num]->levelflags |= LF_NOZONE;
				else
					mapheaderinfo[num]->levelflags &= ~LF_NOZONE;
			}
			else if (fastcmp(word, "SECTIONRACE"))
			{
				if (i || word2[0] == 'T' || word2[0] == 'Y')
					mapheaderinfo[num]->levelflags |= LF_SECTIONRACE;
				else
					mapheaderinfo[num]->levelflags &= ~LF_SECTIONRACE;
			}
			else if (fastcmp(word, "SUBTRACTNUM"))
			{
				if (i || word2[0] == 'T' || word2[0] == 'Y')
					mapheaderinfo[num]->levelflags |= LF_SUBTRACTNUM;
				else
					mapheaderinfo[num]->levelflags &= ~LF_SUBTRACTNUM;
			}

			// Individual triggers for menu flags
			else if (fastcmp(word, "HIDDEN"))
			{
				if (i || word2[0] == 'T' || word2[0] == 'Y')
					mapheaderinfo[num]->menuflags |= LF2_HIDEINMENU;
				else
					mapheaderinfo[num]->menuflags &= ~LF2_HIDEINMENU;
			}
			else if (fastcmp(word, "HIDEINSTATS"))
			{
				if (i || word2[0] == 'T' || word2[0] == 'Y')
					mapheaderinfo[num]->menuflags |= LF2_HIDEINSTATS;
				else
					mapheaderinfo[num]->menuflags &= ~LF2_HIDEINSTATS;
			}
			else if (fastcmp(word, "NOTIMEATTACK") || fastcmp(word, "NORECORDATTACK"))
			{ // RECORDATTACK is an accepted alias
				if (i || word2[0] == 'T' || word2[0] == 'Y')
					mapheaderinfo[num]->menuflags |= LF2_NOTIMEATTACK;
				else
					mapheaderinfo[num]->menuflags &= ~LF2_NOTIMEATTACK;
			}
			else if (fastcmp(word, "VISITNEEDED"))
			{
				if (i || word2[0] == 'T' || word2[0] == 'Y')
					mapheaderinfo[num]->menuflags |= LF2_VISITNEEDED;
				else
					mapheaderinfo[num]->menuflags &= ~LF2_VISITNEEDED;
			}
			else if (fastcmp(word, "NOVISITNEEDED"))
			{
				if (i || word2[0] == 'T' || word2[0] == 'Y')
					mapheaderinfo[num]->menuflags &= ~LF2_VISITNEEDED;
				else
					mapheaderinfo[num]->menuflags |= LF2_VISITNEEDED;
			}
			else if (fastcmp(word, "GRAVITY"))
				mapheaderinfo[num]->gravity = FLOAT_TO_FIXED(atof(word2));
			else if (fastcmp(word, "WALLTRANSFER") || fastcmp(word, "WALLRUNNING"))
			{
				if (i || word2[0] == 'T' || word2[0] == 'Y')
					mapheaderinfo[num]->use_walltransfer = true;
				else
					mapheaderinfo[num]->use_walltransfer = false;
			}
			else if (fastcmp(word, "TERRAIN") || fastcmp(word, "TERRAINDEF"))
			{
				if (i || word2[0] == 'T' || word2[0] == 'Y')
					mapheaderinfo[num]->use_terrain = true;
				else
					mapheaderinfo[num]->use_terrain = false;
			}
			// Global EFX
			else if (fastcmp(word, "GLOBALEFX"))
			{
				mapheaderinfo[num]->globalEFX->type = get_number(word2);
			}
			else if (fastncmp("GLOBALEFX_",word,10))
			{
				globalmapefxparameters(num, i, word, word2);
			}
			// Par times
			else if (fastcmp(word, "PARTIMEEASY"))
			{
				mapheaderinfo[num]->par_time[KARTSPEED_EASY] = (tic_t)i;
			}
			else if (fastcmp(word, "PARTIMENORMAL"))
			{
				mapheaderinfo[num]->par_time[KARTSPEED_NORMAL] = (tic_t)i;
			}
			else if (fastcmp(word, "PARTIMEHARD"))
			{
				mapheaderinfo[num]->par_time[KARTSPEED_HARD] = (tic_t)i;
			}
			else if (fastcmp(word, "PARTIMEEXPERT"))
			{
				mapheaderinfo[num]->par_time[KARTSPEED_EXPERT] = (tic_t)i;
			}
			 // ignored for compatibility
			else if (fastcmp(word, "NEXTLEVEL") || fastcmp(word, "TIMEATTACK") || fastcmp(word, "RECORDATTACK"))
				continue;
			else
				deh_warning("Level header %d: unknown word '%s'", num, word);
		}
	} while (!myfeof(f)); // finish when the line is empty

	Z_Free(s);
}

static void readcutscenescene(MYFILE *f, INT32 num, INT32 scenenum)
{
	char *s = Z_Calloc(MAXLINELEN, PU_STATIC, NULL);
	char *word;
	char *word2;
	INT32 i;
	UINT16 usi;
	UINT8 picid;

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			word = strtok(s, " ");
			if (word)
				strupr(word);
			else
				break;

			if (fastcmp(word, "SCENETEXT"))
			{
				char *scenetext = NULL;
				char *buffer;
				const int bufferlen = 4096;

				for (i = 0; i < MAXLINELEN; i++)
				{
					if (s[i] == '=')
					{
						scenetext = &s[i+2];
						break;
					}
				}

				if (!scenetext)
				{
					Z_Free(cutscenes[num]->scene[scenenum].text);
					cutscenes[num]->scene[scenenum].text = NULL;
					continue;
				}

				for (i = 0; i < MAXLINELEN; i++)
				{
					if (s[i] == '\0')
					{
						s[i] = '\n';
						s[i+1] = '\0';
						break;
					}
				}

				buffer = Z_Malloc(4096, PU_STATIC, NULL);
				strcpy(buffer, scenetext);

				strcat(buffer,
					myhashfgets(scenetext, bufferlen
					- strlen(buffer) - 1, f));

				// A cutscene overwriting another one...
				Z_Free(cutscenes[num]->scene[scenenum].text);

				cutscenes[num]->scene[scenenum].text = Z_StrDup(buffer);

				Z_Free(buffer);

				continue;
			}

			word2 = strtok(NULL, " = ");
			if (word2)
				strupr(word2);
			else
				break;

			if (word2[strlen(word2)-1] == '\n')
				word2[strlen(word2)-1] = '\0';
			i = atoi(word2);
			usi = (UINT16)i;


			if (fastcmp(word, "NUMBEROFPICS"))
			{
				cutscenes[num]->scene[scenenum].numpics = (UINT8)i;
			}
			else if (fastncmp(word, "PIC", 3))
			{
				picid = (UINT8)atoi(word + 3);
				if (picid > 8 || picid == 0)
				{
					deh_warning("CutSceneScene %d: unknown word '%s'", num, word);
					continue;
				}
				--picid;

				if (fastcmp(word+4, "NAME"))
				{
					strncpy(cutscenes[num]->scene[scenenum].picname[picid], word2, 8);
				}
				else if (fastcmp(word+4, "HIRES"))
				{
					cutscenes[num]->scene[scenenum].pichires[picid] = (UINT8)(i || word2[0] == 'T' || word2[0] == 'Y');
				}
				else if (fastcmp(word+4, "DURATION"))
				{
					cutscenes[num]->scene[scenenum].picduration[picid] = usi;
				}
				else if (fastcmp(word+4, "XCOORD"))
				{
					cutscenes[num]->scene[scenenum].xcoord[picid] = usi;
				}
				else if (fastcmp(word+4, "YCOORD"))
				{
					cutscenes[num]->scene[scenenum].ycoord[picid] = usi;
				}
				else
					deh_warning("CutSceneScene %d: unknown word '%s'", num, word);
			}
			else if (fastcmp(word, "MUSIC"))
			{
				strncpy(cutscenes[num]->scene[scenenum].musswitch, word2, 7);
				cutscenes[num]->scene[scenenum].musswitch[6] = 0;
			}
			else if (fastcmp(word, "MUSICTRACK"))
			{
				cutscenes[num]->scene[scenenum].musswitchflags = ((UINT16)i) & MUSIC_TRACKMASK;
			}
			else if (fastcmp(word, "MUSICPOS"))
			{
				cutscenes[num]->scene[scenenum].musswitchposition = (UINT32)get_number(word2);
			}
			else if (fastcmp(word, "MUSICLOOP"))
			{
				cutscenes[num]->scene[scenenum].musicloop = (UINT8)(i || word2[0] == 'T' || word2[0] == 'Y');
			}
			else if (fastcmp(word, "TEXTXPOS"))
			{
				cutscenes[num]->scene[scenenum].textxpos = usi;
			}
			else if (fastcmp(word, "TEXTYPOS"))
			{
				cutscenes[num]->scene[scenenum].textypos = usi;
			}
			else if (fastcmp(word, "FADEINID"))
			{
				cutscenes[num]->scene[scenenum].fadeinid = (UINT8)i;
			}
			else if (fastcmp(word, "FADEOUTID"))
			{
				cutscenes[num]->scene[scenenum].fadeoutid = (UINT8)i;
			}
			else if (fastcmp(word, "FADECOLOR"))
			{
				cutscenes[num]->scene[scenenum].fadecolor = (UINT8)i;
			}
			else
				deh_warning("CutSceneScene %d: unknown word '%s'", num, word);
		}
	} while (!myfeof(f)); // finish when the line is empty

	Z_Free(s);
}

void readcutscene(MYFILE *f, INT32 num)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word;
	char *word2;
	char *tmp;
	INT32 value;

	// Allocate memory for this cutscene if we don't yet have any
	if (!cutscenes[num])
		cutscenes[num] = Z_Calloc(sizeof (cutscene_t), PU_STATIC, NULL);

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			tmp = strchr(s, '#');
			if (tmp)
				*tmp = '\0';
			if (s == tmp)
				continue; // Skip comment lines, but don't break.

			word = strtok(s, " ");
			if (word)
				strupr(word);
			else
				break;

			word2 = strtok(NULL, " ");
			if (word2)
				value = atoi(word2);
			else
			{
				deh_warning("No value for token %s", word);
				continue;
			}

			if (fastcmp(word, "NUMSCENES"))
			{
				cutscenes[num]->numscenes = value;
			}
			else if (fastcmp(word, "SCENE"))
			{
				if (1 <= value && value <= 128)
				{
					readcutscenescene(f, num, value - 1);
				}
				else
					deh_warning("Scene number %d out of range (1 - 128)", value);

			}
			else
				deh_warning("Cutscene %d: unknown word '%s', Scene <num> expected.", num, word);
		}
	} while (!myfeof(f)); // finish when the line is empty

	Z_Free(s);
}

static void readtextpromptpage(MYFILE *f, INT32 num, INT32 pagenum)
{
	char *s = Z_Calloc(MAXLINELEN, PU_STATIC, NULL);
	char *word;
	char *word2;
	INT32 i;
	UINT16 usi;
	UINT8 picid;

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			word = strtok(s, " ");
			if (word)
				strupr(word);
			else
				break;

			if (fastcmp(word, "PAGETEXT"))
			{
				char *pagetext = NULL;
				char *buffer;
				const int bufferlen = 4096;

				for (i = 0; i < MAXLINELEN; i++)
				{
					if (s[i] == '=')
					{
						pagetext = &s[i+2];
						break;
					}
				}

				if (!pagetext)
				{
					Z_Free(textprompts[num]->page[pagenum].text);
					textprompts[num]->page[pagenum].text = NULL;
					continue;
				}

				for (i = 0; i < MAXLINELEN; i++)
				{
					if (s[i] == '\0')
					{
						s[i] = '\n';
						s[i+1] = '\0';
						break;
					}
				}

				buffer = Z_Malloc(4096, PU_STATIC, NULL);
				strcpy(buffer, pagetext);

				// \todo trim trailing whitespace before the #
				// and also support # at the end of a PAGETEXT with no line break

				strcat(buffer,
					myhashfgets(pagetext, bufferlen
					- strlen(buffer) - 1, f));

				// A text prompt overwriting another one...
				Z_Free(textprompts[num]->page[pagenum].text);

				textprompts[num]->page[pagenum].text = Z_StrDup(buffer);

				Z_Free(buffer);

				continue;
			}

			word2 = strtok(NULL, " = ");
			if (word2)
				strupr(word2);
			else
				break;

			if (word2[strlen(word2)-1] == '\n')
				word2[strlen(word2)-1] = '\0';
			i = atoi(word2);
			usi = (UINT16)i;

			// copypasta from readcutscenescene
			if (fastcmp(word, "NUMBEROFPICS"))
			{
				textprompts[num]->page[pagenum].numpics = (UINT8)i;
			}
			else if (fastcmp(word, "PICMODE"))
			{
				UINT8 picmode = 0; // PROMPT_PIC_PERSIST
				if (usi == 1 || word2[0] == 'L') picmode = PROMPT_PIC_LOOP;
				else if (usi == 2 || word2[0] == 'D' || word2[0] == 'H') picmode = PROMPT_PIC_DESTROY;
				textprompts[num]->page[pagenum].picmode = picmode;
			}
			else if (fastcmp(word, "PICTOLOOP"))
				textprompts[num]->page[pagenum].pictoloop = (UINT8)i;
			else if (fastcmp(word, "PICTOSTART"))
				textprompts[num]->page[pagenum].pictostart = (UINT8)i;
			else if (fastcmp(word, "PICSMETAPAGE"))
			{
				if (usi && usi <= textprompts[num]->numpages)
				{
					UINT8 metapagenum = usi - 1;

					textprompts[num]->page[pagenum].numpics = textprompts[num]->page[metapagenum].numpics;
					textprompts[num]->page[pagenum].picmode = textprompts[num]->page[metapagenum].picmode;
					textprompts[num]->page[pagenum].pictoloop = textprompts[num]->page[metapagenum].pictoloop;
					textprompts[num]->page[pagenum].pictostart = textprompts[num]->page[metapagenum].pictostart;

					for (picid = 0; picid < MAX_PROMPT_PICS; picid++)
					{
						strncpy(textprompts[num]->page[pagenum].picname[picid], textprompts[num]->page[metapagenum].picname[picid], 8);
						textprompts[num]->page[pagenum].pichires[picid] = textprompts[num]->page[metapagenum].pichires[picid];
						textprompts[num]->page[pagenum].picduration[picid] = textprompts[num]->page[metapagenum].picduration[picid];
						textprompts[num]->page[pagenum].xcoord[picid] = textprompts[num]->page[metapagenum].xcoord[picid];
						textprompts[num]->page[pagenum].ycoord[picid] = textprompts[num]->page[metapagenum].ycoord[picid];
					}
				}
			}
			else if (fastncmp(word, "PIC", 3))
			{
				picid = (UINT8)atoi(word + 3);
				if (picid > MAX_PROMPT_PICS || picid == 0)
				{
					deh_warning("textpromptscene %d: unknown word '%s'", num, word);
					continue;
				}
				--picid;

				if (fastcmp(word+4, "NAME"))
				{
					strncpy(textprompts[num]->page[pagenum].picname[picid], word2, 8);
				}
				else if (fastcmp(word+4, "HIRES"))
				{
					textprompts[num]->page[pagenum].pichires[picid] = (UINT8)(i || word2[0] == 'T' || word2[0] == 'Y');
				}
				else if (fastcmp(word+4, "DURATION"))
				{
					textprompts[num]->page[pagenum].picduration[picid] = usi;
				}
				else if (fastcmp(word+4, "XCOORD"))
				{
					textprompts[num]->page[pagenum].xcoord[picid] = usi;
				}
				else if (fastcmp(word+4, "YCOORD"))
				{
					textprompts[num]->page[pagenum].ycoord[picid] = usi;
				}
				else
					deh_warning("textpromptscene %d: unknown word '%s'", num, word);
			}
			else if (fastcmp(word, "MUSIC"))
			{
				strncpy(textprompts[num]->page[pagenum].musswitch, word2, 7);
				textprompts[num]->page[pagenum].musswitch[6] = 0;
			}
			else if (fastcmp(word, "MUSICTRACK"))
			{
				textprompts[num]->page[pagenum].musswitchflags = ((UINT16)i) & MUSIC_TRACKMASK;
			}
			else if (fastcmp(word, "MUSICLOOP"))
			{
				textprompts[num]->page[pagenum].musicloop = (UINT8)(i || word2[0] == 'T' || word2[0] == 'Y');
			}
			// end copypasta from readcutscenescene
			else if (fastcmp(word, "NAME"))
			{
				if (*word2 != '\0')
				{
					INT32 j;

					// HACK: Add yellow control char now
					// so the drawing function doesn't call it repeatedly
					char name[34];
					name[0] = '\x82'; // color yellow
					name[1] = 0;
					strncat(name, word2, 33);
					name[33] = 0;

					// Replace _ with ' '
					for (j = 0; j < 32 && name[j]; j++)
					{
						if (name[j] == '_')
							name[j] = ' ';
					}

					strncpy(textprompts[num]->page[pagenum].name, name, 32);
				}
				else
					*textprompts[num]->page[pagenum].name = '\0';
			}
			else if (fastcmp(word, "ICON"))
				strncpy(textprompts[num]->page[pagenum].iconname, word2, 8);
			else if (fastcmp(word, "ICONALIGN"))
				textprompts[num]->page[pagenum].rightside = (i || word2[0] == 'R');
			else if (fastcmp(word, "ICONFLIP"))
				textprompts[num]->page[pagenum].iconflip = (i || word2[0] == 'T' || word2[0] == 'Y');
			else if (fastcmp(word, "LINES"))
				textprompts[num]->page[pagenum].lines = usi;
			else if (fastcmp(word, "BACKCOLOR"))
			{
				INT32 backcolor;
				if      (i == 0 || fastcmp(word2, "WHITE")) backcolor = 0;
				else if (i == 1 || fastcmp(word2, "GRAY") || fastcmp(word2, "GREY") ||
					fastcmp(word2, "BLACK")) backcolor = 1;
				else if (i == 2 || fastcmp(word2, "SEPIA")) backcolor = 2;
				else if (i == 3 || fastcmp(word2, "BROWN")) backcolor = 3;
				else if (i == 4 || fastcmp(word2, "PINK")) backcolor = 4;
				else if (i == 5 || fastcmp(word2, "RASPBERRY")) backcolor = 5;
				else if (i == 6 || fastcmp(word2, "RED")) backcolor = 6;
				else if (i == 7 || fastcmp(word2, "CREAMSICLE")) backcolor = 7;
				else if (i == 8 || fastcmp(word2, "ORANGE")) backcolor = 8;
				else if (i == 9 || fastcmp(word2, "GOLD")) backcolor = 9;
				else if (i == 10 || fastcmp(word2, "YELLOW")) backcolor = 10;
				else if (i == 11 || fastcmp(word2, "EMERALD")) backcolor = 11;
				else if (i == 12 || fastcmp(word2, "GREEN")) backcolor = 12;
				else if (i == 13 || fastcmp(word2, "CYAN") || fastcmp(word2, "AQUA")) backcolor = 13;
				else if (i == 14 || fastcmp(word2, "STEEL")) backcolor = 14;
				else if (i == 15 || fastcmp(word2, "PERIWINKLE")) backcolor = 15;
				else if (i == 16 || fastcmp(word2, "BLUE")) backcolor = 16;
				else if (i == 17 || fastcmp(word2, "PURPLE")) backcolor = 17;
				else if (i == 18 || fastcmp(word2, "LAVENDER")) backcolor = 18;
				else if (i >= 256 && i < 512) backcolor = i; // non-transparent palette index
				else if (i < 0) backcolor = INT32_MAX; // CONS_BACKCOLOR user-configured
				else backcolor = 1; // default gray
				textprompts[num]->page[pagenum].backcolor = backcolor;
			}
			else if (fastcmp(word, "ALIGN"))
			{
				UINT8 align = 0; // left
				if (usi == 1 || word2[0] == 'R') align = 1;
				else if (usi == 2 || word2[0] == 'C' || word2[0] == 'M') align = 2;
				textprompts[num]->page[pagenum].align = align;
			}
			else if (fastcmp(word, "VERTICALALIGN"))
			{
				UINT8 align = 0; // top
				if (usi == 1 || word2[0] == 'B') align = 1;
				else if (usi == 2 || word2[0] == 'C' || word2[0] == 'M') align = 2;
				textprompts[num]->page[pagenum].verticalalign = align;
			}
			else if (fastcmp(word, "TEXTSPEED"))
				textprompts[num]->page[pagenum].textspeed = get_number(word2);
			else if (fastcmp(word, "TEXTSFX"))
				textprompts[num]->page[pagenum].textsfx = get_number(word2);
			else if (fastcmp(word, "HIDEHUD"))
			{
				UINT8 hidehud = 0;
				if ((word2[0] == 'F' && (word2[1] == 'A' || !word2[1])) || word2[0] == 'N') hidehud = 0; // false
				else if (usi == 1 || word2[0] == 'T' || word2[0] == 'Y') hidehud = 1; // true (hide appropriate HUD elements)
				else if (usi == 2 || word2[0] == 'A' || (word2[0] == 'F' && word2[1] == 'O')) hidehud = 2; // force (hide all HUD elements)
				textprompts[num]->page[pagenum].hidehud = hidehud;
			}
			else if (fastcmp(word, "METAPAGE"))
			{
				if (usi && usi <= textprompts[num]->numpages)
				{
					UINT8 metapagenum = usi - 1;

					strncpy(textprompts[num]->page[pagenum].name, textprompts[num]->page[metapagenum].name, 32);
					strncpy(textprompts[num]->page[pagenum].iconname, textprompts[num]->page[metapagenum].iconname, 8);
					textprompts[num]->page[pagenum].rightside = textprompts[num]->page[metapagenum].rightside;
					textprompts[num]->page[pagenum].iconflip = textprompts[num]->page[metapagenum].iconflip;
					textprompts[num]->page[pagenum].lines = textprompts[num]->page[metapagenum].lines;
					textprompts[num]->page[pagenum].backcolor = textprompts[num]->page[metapagenum].backcolor;
					textprompts[num]->page[pagenum].align = textprompts[num]->page[metapagenum].align;
					textprompts[num]->page[pagenum].verticalalign = textprompts[num]->page[metapagenum].verticalalign;
					textprompts[num]->page[pagenum].textspeed = textprompts[num]->page[metapagenum].textspeed;
					textprompts[num]->page[pagenum].textsfx = textprompts[num]->page[metapagenum].textsfx;
					textprompts[num]->page[pagenum].hidehud = textprompts[num]->page[metapagenum].hidehud;

					// music: don't copy, else each page change may reset the music
				}
			}
			else if (fastcmp(word, "TAG"))
				strncpy(textprompts[num]->page[pagenum].tag, word2, 33);
			else if (fastcmp(word, "NEXTPROMPT"))
				textprompts[num]->page[pagenum].nextprompt = usi;
			else if (fastcmp(word, "NEXTPAGE"))
				textprompts[num]->page[pagenum].nextpage = usi;
			else if (fastcmp(word, "NEXTTAG"))
				strncpy(textprompts[num]->page[pagenum].nexttag, word2, 33);
			else if (fastcmp(word, "TIMETONEXT"))
				textprompts[num]->page[pagenum].timetonext = get_number(word2);
			else
				deh_warning("PromptPage %d: unknown word '%s'", num, word);
		}
	} while (!myfeof(f)); // finish when the line is empty

	Z_Free(s);
}

void readtextprompt(MYFILE *f, INT32 num)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word;
	char *word2;
	char *tmp;
	INT32 value;

	// Allocate memory for this prompt if we don't yet have any
	if (!textprompts[num])
		textprompts[num] = Z_Calloc(sizeof (textprompt_t), PU_STATIC, NULL);

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			tmp = strchr(s, '#');
			if (tmp)
				*tmp = '\0';
			if (s == tmp)
				continue; // Skip comment lines, but don't break.

			word = strtok(s, " ");
			if (word)
				strupr(word);
			else
				break;

			word2 = strtok(NULL, " ");
			if (word2)
				value = atoi(word2);
			else
			{
				deh_warning("No value for token %s", word);
				continue;
			}

			if (fastcmp(word, "NUMPAGES"))
			{
				textprompts[num]->numpages = min(max(value, 0), MAX_PAGES);
			}
			else if (fastcmp(word, "PAGE"))
			{
				if (1 <= value && value <= MAX_PAGES)
				{
					textprompts[num]->page[value - 1].backcolor = 1; // default to gray
					textprompts[num]->page[value - 1].hidehud = 1; // hide appropriate HUD elements
					readtextpromptpage(f, num, value - 1);
				}
				else
					deh_warning("Page number %d out of range (1 - %d)", value, MAX_PAGES);

			}
			else
				deh_warning("Prompt %d: unknown word '%s', Page <num> expected.", num, word);
		}
	} while (!myfeof(f)); // finish when the line is empty

	Z_Free(s);
}

// super secret menu cvars... :shushing_face:
static struct { const char *name; consvar_t *var; } HIDDENVARS[] = {
	{ "CHOOSESKIN", &cv_chooseskin },
	{ "DUMMYGPDIFFICULTY", &cv_dummygpdifficulty },
	{ "DUMMYGPENCORE", &cv_dummygpencore },
	{ "NEXTMAP", &cv_nextmap },
	{ "NEXTCUP", &cv_nextcup },
	{ "NEWGAMETYPE", &cv_newgametype },
	{ "DUMMYMENUPLAYER", &cv_dummymenuplayer },
	{ "DUMMYTEAM", &cv_dummyteam },
	{ "DUMMYSPECTATE", &cv_dummyspectate },
	{ "DUMMYSCRAMBLE", &cv_dummyscramble },
	{ "DUMMYSTAFF", &cv_dummystaff },
	{ "DUMMYMULTIPLAYER", &cv_dummymultiplayer },
	{ "DUMMYIP", &cv_dummyip },
	{ "DUMMYNAME", &cv_dummyname },
	{ "DUMMYFOLLOWER", &cv_dummyfollower },
	{ "DUMMYCOLOR", &cv_dummycolor },
	{ "DUMMYSERVERPAGE", &cv_dummyserverpage },
	{ "LEVELSORT", &cv_levelsort },
	{ NULL, NULL }
};

#define WARN(str, ...) deh_warning("MenuItem %s: " str, strbuf_get(menunames, item->info.nameofs), __VA_ARGS__)
#define WARN0(str) deh_warning("MenuItem %s: " str, strbuf_get(menunames, item->info.nameofs))
static void readmenuitems(MYFILE *f, menupaneldef_t *pd)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word = s;
	char *word2;
	char *tmp;

	menuitem_t *item = NULL;
	menuitemflags_t status = 0;

	// taking quite possibly the only opportunity i'll ever get
	// to avoid three tabs of indentation...
	do if (myfgets(s, MAXLINELEN, f))
	{
		if (s[0] == '\n')
			break;

		// First remove trailing newline, if there is one
		tmp = strchr(s, '\n');
		if (tmp)
			*tmp = '\0';

		tmp = strchr(s, '#');
		if (tmp)
			*tmp = '\0';
		if (s == tmp)
			continue; // Skip comment lines, but don't break.

		// Get the part before the " = "
		tmp = strchr(s, '=');
		if (tmp == NULL)
		{
			// new item definition
			tmp = s;
			while (isalnum(*tmp) || *tmp == '_')
				tmp++;
			if (*tmp != '\0')
			{
				deh_warning("Syntax error in menuitem name");
				break;
			}
			strupr(s);

			if (item != NULL && status != 0)
				item->status = status;
			status = 0;
			item = M_CheckMenuItemDef(pd, s);
			if (item == NULL)
			{
				Z_Realloc(pd->items, sizeof(menuitem_t)*(pd->numitems+1), PU_STATIC, &pd->items);
				item = pd->items + pd->numitems++;
				memset(item, 0, sizeof(*item));
				DEH_Link(s, &item->info, &menunames);
			}
			continue;
		}

		*(tmp-1) = '\0';

		// Remove leading indentation (an unique feature of menuitems to help readability!)
		word = s;
		while (isspace(*word))
			word++;
		strupr(word);

		if (item == NULL)
		{
			WARN("cannot use %s without defining an item", word);
			break;
		}

		// Now get the part after
		word2 = tmp + 2;
		//strupr(word2);

		if (fastcmp(word, "X"))
		{
			item->x = get_number(word2);
		}
		else if (fastcmp(word, "OFSX"))
		{
			item->x = get_number(word2);
			status |= IT_OFSX;
		}
		else if (fastcmp(word, "Y"))
		{
			item->y = get_number(word2);
		}
		else if (fastcmp(word, "OFSY"))
		{
			item->y = get_number(word2);
			status |= IT_OFSY;
		}
		else if (fastcmp(word, "TEMPOFFSET"))
		{
			status |= IT_TEMPORARY;
		}
		else if (fastcmp(word, "ARGUMENT"))
		{
			item->argument = get_number(word2);
		}
		else if (fastcmp(word, "TEXT"))
		{
			if (item->text)
				Z_Free(item->text);
			item->text = Z_StrDup(word2);
		}
		else if (fastcmp(word, "PATCH"))
		{
			if (item->patch)
				Z_Free(item->patch);
			item->patch = Z_StrDup(word2);
		}
		else if (fastcmp(word, "TOOLTIP"))
		{
			if (item->tooltip)
				Z_Free(item->tooltip);
			item->tooltip = Z_StrDup(word2);
		}
		else if (fastcmp(word, "EXTRA"))
		{
			if (item->extra)
				Z_Free(item->extra);
			item->extra = Z_StrDup(word2);
		}
		else if (fastcmp(word, "STYLE"))
		{
			strupr(word2);
			menuitemstyle_t *style = DEH_FindMenuItemStyle(word2);
			if (style == NULL)
			{
				WARN("unknown style %s", word2);
				continue;
			}
			item->style = style;
		}
		else if (fastcmp(word, "CVAR") || fastcmp(word, "SLIDER"))
		{
#ifndef HAVE_DISCORDRPC
			if (fasticmp(word2, "DISCORDRP")
				|| fasticmp(word2, "DISCORDSTREAMER")
				|| fasticmp(word2, "DISCORDASKS")
				|| fasticmp(word2, "DISCORDINVITES")
			)
			{
				continue;
			}
#endif
			consvar_t *cvar = CV_FindVar(word2);
			if (!cvar)
				for (size_t i = 0; HIDDENVARS[i].name; i++)
					if (fasticmp(word2, HIDDENVARS[i].name))
					{
						cvar = HIDDENVARS[i].var;
						break;
					}
			if (!cvar)
			{
				WARN("unable to find cvar '%s'", word2);
				continue;
			}
			status |= IT_INTERACT;
			if (word[0] == 'S')
				status |= IT_SLIDER;
			item->cvar = cvar;
		}
		else if (fastcmp(word, "SUBMENU"))
		{
#ifndef HAVE_DISCORDRPC
			{
				const char *testname = word2;
				if (fastncmp("MN_", testname, 3))
					testname += 3;
				if (fastcmp(testname, "OP_DISCORD") || fastcmp(testname, "MISC_DISCORDREQUESTS"))
				{
					continue;
				}
			}
#endif
			menutype_t mn = get_menutype(word2);
			if (mn == MAXMENUTYPES)
			{
				WARN("unknown menu '%s'", word2);
				continue;
			}
			status |= IT_INTERACT;
			item->submenu = mn;
		}
		else if (fastcmp(word, "CALL") || fastcmp(word, "ARROWS"))
		{
			menufunc_f *routine = get_menuroutine(word2);
			if (!routine)
			{
				WARN("unknown call routine '%s'", word2);
				continue;
			}
			status |= IT_INTERACT;
			if (word[0] == 'A')
				status |= IT_ARROWS;
			item->routine = routine;
		}
		else
			WARN("unknown word '%s'", word);
	}
	while (!myfeof(f)); // finish when the line is empty

	if (item != NULL && status != 0)
		item->status = status;

	Z_Free(s);
}
#undef WARN
#undef WARN0

#define WARN(str, ...) deh_warning("Menu style %s: " str, strbuf_get(menunames, style->info.nameofs), __VA_ARGS__)
#define WARN0(str) deh_warning("Menu style %s: " str, strbuf_get(menunames, style->info.nameofs))
void readmenuitemstyle(MYFILE *f, menuitemstyle_t *style)
{
	CLEANUP(Z_Pfree) char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word = s;
	char *word2;
	menuitempart_t *part = NULL;

	do if (myfgets(s, MAXLINELEN, f))
	{
		if (s[0] == '\n')
			break;

		word2 = parsesocline(s);
		if (word2 == s)
		{
			continue;
		}
		else if (word2 == NULL && !strnicmp(s, "PART ", 5))
		{
			INT32 index = atoi(s + 5);
			if (index < 0 || index >= MAXMENUITEMPARTS)
			{
				deh_warning("Part index out of range");
				break;
			}
			part = &style->parts[index];
			style->partenabled[index] = true;
			continue;
		}
		else if (word2 == NULL)
		{
			break;
		}

		// Remove leading indentation (no longer an unique feature)
		word = s;
		while (isspace(*word))
			word++;

		if (part == NULL)
		{
			WARN("cannot use %s without selecting a part", word);
			break;
		}

		if (fastcmp(word, "FIXEDSTRING"))
		{
			Z_Free(part->fixedstring);
			part->fixedstring = Z_StrDup(word2);
			continue;
		}
		else if (fastcmp(word, "FIXEDSTRING2"))
		{
			Z_Free(part->fixedstring2);
			part->fixedstring2 = Z_StrDup(word2);
			continue;
		}

		strupr(word2);

		if (fastcmp(word, "X"))
		{
			part->xofs = get_number(word2);
		}
		else if (fastcmp(word, "Y"))
		{
			part->yofs = get_number(word2);
		}
		else if (fastcmp(word, "SMALL"))
		{
			part->small = word2[0] == 'T' || word2[0] == 'Y';
		}
		else if (fastcmp(word, "TRANSLUCENT"))
		{
			part->translucent = word2[0] == 'T' || word2[0] == 'Y';
		}
		else if (fastcmp(word, "NOHIGHLIGHT"))
		{
			part->nohighlight = word2[0] == 'T' || word2[0] == 'Y';
		}
		else if (fastcmp(word, "FONT") || fastcmp(word, "FONTALT"))
		{
			if (fastcmp(word2, "HU"))
				part->font = HU_FONT;
			else if (fastcmp(word2, "TINY"))
				part->font = TINY_FONT;
			else if (fastcmp(word2, "KART"))
				part->font = KART_FONT;
			else if (fastcmp(word2, "CRED"))
				part->font = CRED_FONT;
			else if (fastcmp(word2, "GTOL"))
				part->font = GTOL_FONT;
			else if (fastcmp(word2, "GTFN"))
				part->font = GTFN_FONT;
			else
				WARN("unknown font '%s'", word2);

			part->fontalt = word[4] == 'A';
		}
		else if (fastcmp(word, "TYPE"))
		{
			if (fastcmp(word2, "STRING"))
				part->type = ITT_STRING;
			else if (fastcmp(word2, "FILL"))
				part->type = ITT_FILL;
			else if (fastcmp(word2, "PATCH"))
				part->type = ITT_PATCH;
			else if (fastcmp(word2, "THUMBNAIL"))
				part->type = ITT_THUMBNAIL;
			else if (fastcmp(word2, "SKINICON"))
				part->type = ITT_SKINICON;
			else
				WARN("unknown type '%s'", word2);
		}
		else if (fastcmp(word, "HIGHLIGHT"))
		{
			if (fastcmp(word2, "NONE"))
				part->highlight = ITH_NONE;
			else if (fastcmp(word2, "HIGHLIGHT"))
				part->highlight = ITH_HIGHLIGHT;
			else if (fastcmp(word2, "RECOMMENDED"))
				part->highlight = ITH_RECOMMENDED;
			else if (fastcmp(word2, "WARNING"))
				part->highlight = ITH_WARNING;
			else
				WARN("unknown highlight '%s'", word2);
		}
		else if (fastcmp(word, "ALIGN"))
		{
			if (fastcmp(word2, "LEFT"))
				part->align = ITA_LEFT;
			else if (fastcmp(word2, "CENTER"))
				part->align = ITA_CENTER;
			else if (fastcmp(word2, "RIGHT"))
				part->align = ITA_RIGHT;
			else if (fastcmp(word2, "EDGE"))
				part->align = ITA_EDGE;
			else
				WARN("unknown alignment '%s'", word2);
		}

		else if (fastcmp(word, "SOURCE") || fastcmp(word, "SOURCE2"))
		{
			itempartsource_e source;
			if (fastcmp(word2, "TEXT"))
				source = ITS_TEXT;
			else if (fastcmp(word2, "PATCH"))
				source = ITS_PATCH;
			else if (fastcmp(word2, "EXTRA"))
				source = ITS_EXTRA;
			else if (fastcmp(word2, "FIXED"))
				source = ITS_FIXED;
			else if (fastcmp(word2, "FIXED2"))
				source = ITS_FIXED2;
			else
			{
				WARN("unknown source '%s'", word2);
				continue;
			}
			if (word[6] == '2')
				part->source2 = source;
			else
				part->source = source;
		}
		else
			WARN("unknown word '%s'", word);
	}
	while (!myfeof(f)); // finish when the line is empty
}
#undef WARN
#undef WARN0

#define WARN(str, ...) deh_warning("Menu panel %s: " str, DEH_MenuPanelName(pd), __VA_ARGS__)
#define WARN0(str) deh_warning("Menu panel %s: " str, DEH_MenuPanelName(pd))
void readmenupanel(MYFILE *f, menupaneldef_t *pd)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word = s;
	char *word2;
	char *tmp;

	do if (myfgets(s, MAXLINELEN, f))
	{
		if (s[0] == '\n')
			break;

		// First remove trailing newline, if there is one
		tmp = strchr(s, '\n');
		if (tmp)
			*tmp = '\0';

		tmp = strchr(s, '#');
		if (tmp)
			*tmp = '\0';
		if (s == tmp)
			continue; // Skip comment lines, but don't break.

		// Get the part before the " = "
		tmp = strchr(s, '=');
		if (tmp)
		{
			*(tmp-1) = '\0';
		}
		else if (fasticmp(s, "ITEMS"))
		{
			readmenuitems(f, pd);
			break;
		}
		else
		{
			break;
		}
		strupr(word);

		// Now get the part after
		word2 = (tmp += 2);
		strupr(word2);

		if (fastcmp(word, "INITIALIZER"))
		{
			panelfunc_f *initializer = get_panelfunc(word2);
			if (initializer == NULL)
			{
				WARN("unknown panel func '%s'", word2);
				continue;
			}
			pd->initializer = initializer;
		}
		else if (fastcmp(word, "DRAWROUTINE"))
		{
			menudrawer_f *drawer = get_menudrawer(word2);
			if (drawer == NULL)
			{
				WARN("unknown draw routine '%s'", word2);
				continue;
			}
			pd->drawroutine = drawer;
		}
		else if (fastcmp(word, "TICKROUTINE"))
		{
			panelfunc_f *tickroutine = get_panelfunc(word2);
			if (tickroutine == NULL)
			{
				WARN("unknown panel tick func '%s'", word2);
				continue;
			}
			pd->tickroutine = tickroutine;
		}
		else if (fastcmp(word, "SCROLLBAR"))
		{
			pd->scrollbar = (boolean)(word2[0] == 'T' || word2[0] == 'Y' || get_number(word2));
		}
		else if (fastcmp(word, "SCROLLHEIGHT"))
		{
			pd->scrollheight = get_number(word2);
		}
		else if (fastcmp(word, "CURSOROFFSET"))
		{
			pd->cursoroffset = get_number(word2);
		}
		else if (fastcmp(word, "LINEHEIGHT"))
		{
			pd->lineheight = get_number(word2);
		}
		else if (fastcmp(word, "KEYHANDLER"))
		{
			menufunc_f *routine = get_menuroutine(word2);
			if (!routine)
			{
				WARN("unknown key handler '%s'", word2);
				continue;
			}
			pd->keyhandler = routine;
		}
		else if (fastcmp(word, "MULTINAV"))
		{
			pd->multinav = word2[0] == 'T' || word2[0] == 'Y';
		}
		else if (fastcmp(word, "NAVITEMCOUNT"))
		{
			pd->navitemcount = get_number(word2);
		}
		else if (fastcmp(word, "HORIZONTAL"))
		{
			pd->horizontal = word2[0] == 'T' || word2[0] == 'Y';
		}
		else
			WARN("unknown word '%s'", word);
	} while (!myfeof(f)); // finish when the line is empty

	Z_Free(s);
}
#undef WARN
#undef WARN0

#define WARN(str, ...) deh_warning("Menu %s: " str, DEH_MenutypeName(num), __VA_ARGS__)
#define WARN0(str) deh_warning("Menu %s: " str, DEH_MenutypeName(num))
static void readmenupanelinstances(MYFILE *f, INT32 num)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word = s;
	char *word2;
	char *tmp;

	menu_t *menudef = &menudefs[num];
	menupanelinst_t *pi = NULL;

	do if (myfgets(s, MAXLINELEN, f))
	{
		if (s[0] == '\n')
			break;

		// First remove trailing newline, if there is one
		tmp = strchr(s, '\n');
		if (tmp)
			*tmp = '\0';

		tmp = strchr(s, '#');
		if (tmp)
			*tmp = '\0';
		if (s == tmp)
			continue; // Skip comment lines, but don't break.

		// Get the part before the " = "
		tmp = strchr(s, '=');
		if (tmp == NULL)
		{
			// new panel definition
			char *srcname = NULL;
			tmp = s;
			while (isalnum(*tmp) || *tmp == '_')
				tmp++;
			if (fastncmp(tmp, " : ", 3))
			{
				*tmp = '\0';
				tmp += 3;
				srcname = tmp;
				while (isalnum(*tmp) || *tmp == '_')
					tmp++;
			}
			if (srcname == NULL)
			{
				WARN0("no source panel specified");
				break;
			}
			if (*tmp != '\0')
			{
				WARN("invalid character '%c' in panel instance name", *tmp);
				break;
			}
			if (strlen(s) > MAXMENUPANELNAME)
			{
				WARN("panel instance name '%s' is longer than %d characters", s, MAXMENUPANELNAME);
				break;
			}
			for (size_t i = 0; i < menudef->numpanels; i++)
			{
				if (fastcmp(menudef->panels[i].name, s))
				{
					WARN("panel instance name '%s' already exists", s);
					break;
				}
			}

			menupaneldef_t *pd = DEH_FindMenuPanel(srcname);
			if (pd == NULL)
			{
				WARN("couldn't find panel named '%s'", srcname);
				break;
			}
			Z_Realloc(menudef->panels, sizeof(*menudef->panels)*(menudef->numpanels+1), PU_STATIC, &menudef->panels);
			pi = menudef->panels + menudef->numpanels++;
			memset(pi, 0, sizeof(*pi));
			strlcpy(pi->name, s, sizeof(pi->name));
			pi->defnum = pd - menupanels;
			continue;
		}

		*(tmp-1) = '\0';

		// Remove leading indentation (an unique feature of menuitems to help readability!)
		word = s;
		while (isspace(*word))
			word++;
		strupr(word);

		if (pi == NULL)
		{
			WARN("cannot use %s without defining a panel", word);
			break;
		}

		// Now get the part after
		word2 = (tmp += 2);
		strupr(word2);

		if (fastcmp(word, "X"))
		{
			pi->x = get_number(word2);
		}
		else if (fastcmp(word, "Y"))
		{
			pi->y = get_number(word2);
		}
		else if (fastcmp(word, "FLAGS"))
		{
			UINT32 allow = V_SNAPTOTOP|V_SNAPTOBOTTOM|V_SNAPTOLEFT|V_SNAPTORIGHT
			              |V_ALLOWLOWERCASE // overrides menucaps
			              |V_ALPHAMASK; // woah, transparency!
			pi->vflags = get_number(word2) & allow;
		}
		else if (fastcmp(word, "DISABLED"))
		{
			pi->disabled = word2[0] == 'T' || word2[0] == 'Y';
		}
		else if (fastcmp(word, "LAYER"))
		{
			pi->layer = get_number(word2);
		}
		else if (fastcmp(word, "CLIPRECT"))
		{
			pi->cliprect = word2[0] == 'T' || word2[0] == 'Y';
		}
		else if (fastcmp(word, "CLIPRECT_RELATIVE"))
		{
			pi->cliprect_relative = word2[0] == 'T' || word2[0] == 'Y';
		}
		else if (fastcmp(word, "CLIPRECT_X"))
		{
			pi->cliprect_x = get_number(word2);
		}
		else if (fastcmp(word, "CLIPRECT_Y"))
		{
			pi->cliprect_y = get_number(word2);
		}
		else if (fastcmp(word, "CLIPRECT_WIDTH"))
		{
			pi->cliprect_width = get_number(word2);
		}
		else if (fastcmp(word, "CLIPRECT_HEIGHT"))
		{
			pi->cliprect_height = get_number(word2);
		}
		else
			WARN("unknown word '%s'", word);
	} while (!myfeof(f)); // finish when the line is empty

	Z_Free(s);
}

void readmenu(MYFILE *f, INT32 num)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word = s;
	char *word2;
	char *tmp;
	INT32 value;

	menu_t *menudef = &menudefs[num];

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			// First remove trailing newline, if there is one
			tmp = strchr(s, '\n');
			if (tmp)
				*tmp = '\0';

			tmp = strchr(s, '#');
			if (tmp)
				*tmp = '\0';
			if (s == tmp)
				continue; // Skip comment lines, but don't break.

			// Get the part before the " = "
			tmp = strchr(s, '=');
			if (tmp)
			{
				*(tmp-1) = '\0';
			}
			else if (fasticmp(s, "PANELS"))
			{
				readmenupanelinstances(f, num);
				break;
			}
			else
			{
				break;
			}
			strupr(word);

			// Now get the part after
			word2 = (tmp += 2);
			strupr(word2);

			value = atoi(word2); // used for numerical settings

			if (fastcmp(word, "BACKGROUNDNAME"))
			{
				strncpy(menudefs[num].bgname, word2, 8);
				titlechanged = true;
			}
			else if (fastcmp(word, "HIDEBACKGROUND"))
			{
				menudefs[num].bghide = (boolean)(value || word2[0] == 'T' || word2[0] == 'Y');
				titlechanged = true;
			}
			else if (fastcmp(word, "BACKGROUNDCOLOR"))
			{
				menudefs[num].bgcolor = get_number(word2);
				titlechanged = true;
			}
			else if (fastcmp(word, "HIDETITLEPICS") || fastcmp(word, "HIDEPICS") || fastcmp(word, "TITLEPICSHIDE"))
			{
				// true by default, except MM_MAIN
				menudefs[num].hidetitlepics = (boolean)(value || word2[0] == 'T' || word2[0] == 'Y');
				titlechanged = true;
			}
			else if (fastcmp(word, "TITLEPICSMODE"))
			{
				if (fastcmp(word2, "USER"))
					menudefs[num].ttmode = TTMODE_USER;
				else if (fastcmp(word2, "HIDE") || fastcmp(word2, "HIDDEN") || fastcmp(word2, "NONE"))
				{
					menudefs[num].ttmode = TTMODE_USER;
					menudefs[num].ttname[0] = 0;
					menudefs[num].hidetitlepics = true;
				}
				else if (fastcmp(word2, "KART"))
					menudefs[num].ttmode = TTMODE_KART;
				titlechanged = true;
			}
			else if (fastcmp(word, "TITLEPICSSCALE"))
			{
				// Don't handle Alacroix special case here; see Maincfg section.
				menudefs[num].ttscale = max(1, min(8, (UINT8)get_number(word2)));
				titlechanged = true;
			}
			else if (fastcmp(word, "TITLEPICSNAME"))
			{
				strncpy(menudefs[num].ttname, word2, 9);
				titlechanged = true;
			}
			else if (fastcmp(word, "TITLEPICSX"))
			{
				menudefs[num].ttx = (INT16)get_number(word2);
				titlechanged = true;
			}
			else if (fastcmp(word, "TITLEPICSY"))
			{
				menudefs[num].tty = (INT16)get_number(word2);
				titlechanged = true;
			}
			else if (fastcmp(word, "TITLEPICSLOOP"))
			{
				menudefs[num].ttloop = (INT16)get_number(word2);
				titlechanged = true;
			}
			else if (fastcmp(word, "TITLEPICSTICS"))
			{
				menudefs[num].tttics = (UINT16)get_number(word2);
				titlechanged = true;
			}
			else if (fastcmp(word, "TITLESCROLLSPEED") || fastcmp(word, "TITLESCROLLXSPEED")
				|| fastcmp(word, "SCROLLSPEED") || fastcmp(word, "SCROLLXSPEED"))
			{
				menudefs[num].titlescrollxspeed = get_number(word2);
				titlechanged = true;
			}
			else if (fastcmp(word, "TITLESCROLLYSPEED") || fastcmp(word, "SCROLLYSPEED"))
			{
				menudefs[num].titlescrollyspeed = get_number(word2);
				titlechanged = true;
			}
			else if (fastcmp(word, "VHS"))
			{
				menudefs[num].vhs = (boolean)(value || word2[0] == 'T' || word2[0] == 'Y');
				titlechanged = true;
			}
			else if (fastcmp(word, "MUSIC"))
			{
				strncpy(menudefs[num].musname, word2, 7);
				menudefs[num].musname[6] = 0;
				titlechanged = true;
			}
			else if (fastcmp(word, "MUSICTRACK"))
			{
				menudefs[num].mustrack = ((UINT16)value - 1);
				titlechanged = true;
			}
			else if (fastcmp(word, "MUSICLOOP"))
			{
				// true by default except MM_MAIN
				menudefs[num].muslooping = (value || word2[0] == 'T' || word2[0] == 'Y');
				titlechanged = true;
			}
			else if (fastcmp(word, "NOMUSIC"))
			{
				menudefs[num].musstop = (value || word2[0] == 'T' || word2[0] == 'Y');
				titlechanged = true;
			}
			else if (fastcmp(word, "IGNOREMUSIC"))
			{
				menudefs[num].musignore = (value || word2[0] == 'T' || word2[0] == 'Y');
				titlechanged = true;
			}
			else if (fastcmp(word, "MUSICCREDITSHOW"))
			{
				menudefs[num].muscreditshow = (value || word2[0] == 'T' || word2[0] == 'Y');
			}
			else if (fastcmp(word, "MUSICCREDITYOFFSET"))
			{
				menudefs[num].muscredity = get_number(word2);
			}
			else if (fastcmp(word, "MUSICCREDITANIMTIME") || fastcmp(word, "MUSICCREDITANIMATIONTIME"))
			{
				menudefs[num].muscreditanimtime = (UINT16)get_number(word2);
			}
			else if (fastcmp(word, "MUSICCREDITSNAPFLAGS"))
			{
				menudefs[num].muscreditsnapflags = (INT32)get_number(word2);
			}
			else if (fastcmp(word, "FADESTRENGTH"))
			{
				// one-based, <= 0 means use default value. 1-32
				menudefs[num].fadestrength = (get_number(word2) - 1) % 32;
				titlechanged = true;
			}
			else if (fastcmp(word, "NOENTERBUBBLE"))
			{
				menudefs[num].enterbubble = !(value || word2[0] == 'T' || word2[0] == 'Y');
				titlechanged = true;
			}
			else if (fastcmp(word, "NOEXITBUBBLE"))
			{
				menudefs[num].exitbubble = !(value || word2[0] == 'T' || word2[0] == 'Y');
				titlechanged = true;
			}
			else if (fastcmp(word, "ENTERTAG"))
			{
				menudefs[num].entertag = get_number(word2);
				titlechanged = true;
			}
			else if (fastcmp(word, "EXITTAG"))
			{
				menudefs[num].exittag = get_number(word2);
				titlechanged = true;
			}
			else if (fastcmp(word, "ENTERWIPE"))
			{
				menudefs[num].enterwipe = get_number(word2);
				titlechanged = true;
			}
			else if (fastcmp(word, "EXITWIPE"))
			{
				menudefs[num].exitwipe = get_number(word2);
				titlechanged = true;
			}
			else if (fastcmp(word, "HEADERPIC"))
			{
				if (menudef->headerpic)
					Z_Free(menudef->headerpic);
				menudef->headerpic = Z_StrDup(word2);
			}
			else if (fastcmp(word, "ENTERROUTINE"))
			{
				menufunc_f *routine = get_menuroutine(word2);
				if (!routine)
				{
					WARN("unknown enter routine '%s'", word2);
					continue;
				}
				menudef->enterroutine = routine;
			}
			else if (fastcmp(word, "QUITROUTINE"))
			{
				menufunc_f *routine = get_menuroutine(word2);
				if (!routine)
				{
					WARN("unknown quit routine '%s'", word2);
					continue;
				}
				menudef->quitroutine = routine;
			}
			else if (fastcmp(word, "TICKROUTINE"))
			{
				menufunc_f *routine = get_menuroutine(word2);
				if (!routine)
				{
					WARN("unknown tick routine '%s'", word2);
					continue;
				}
				menudef->tickroutine = routine;
			}
			else
				WARN("unknown word '%s'", word);
		}
	} while (!myfeof(f)); // finish when the line is empty

	Z_Free(s);
}
#undef WARN
#undef WARN0

void readframe(MYFILE *f, INT32 num)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word1;
	char *word2 = NULL;
	char *tmp;

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			tmp = strchr(s, '#');
			if (tmp)
				*tmp = '\0';
			if (s == tmp)
				continue; // Skip comment lines, but don't break.

			word1 = strtok(s, " ");
			if (word1)
				strupr(word1);
			else
				break;

			word2 = strtok(NULL, " = ");
			if (word2)
				strupr(word2);
			else
				break;
			if (word2[strlen(word2)-1] == '\n')
				word2[strlen(word2)-1] = '\0';

			if (fastcmp(word1, "SPRITENUMBER") || fastcmp(word1, "SPRITENAME"))
			{
				states[num].sprite = get_sprite(word2);
			}
			else if (fastcmp(word1, "SPRITESUBNUMBER") || fastcmp(word1, "SPRITEFRAME"))
			{
				states[num].frame = (INT32)get_number(word2); // So the FF_ flags get calculated
			}
			else if (fastcmp(word1, "DURATION"))
			{
				states[num].tics = (INT32)get_number(word2); // So TICRATE can be used
			}
			else if (fastcmp(word1, "NEXT"))
			{
				statenum_t state = get_state(word2);
				states[num].nextstate = state == NUMSTATES ? S_NULL : state;
			}
			else if (fastcmp(word1, "VAR1"))
			{
				states[num].var1 = (INT32)get_number(word2);
			}
			else if (fastcmp(word1, "VAR2"))
			{
				states[num].var2 = (INT32)get_number(word2);
			}
			else if (fastcmp(word1, "ACTION"))
			{
				size_t z;
				boolean found = false;
				char actiontocompare[32];

				memset(actiontocompare, 0x00, sizeof(actiontocompare));
				strlcpy(actiontocompare, word2, sizeof (actiontocompare));
				strupr(actiontocompare);

				for (z = 0; z < 32; z++)
				{
					if (actiontocompare[z] == '\n' || actiontocompare[z] == '\r')
					{
						actiontocompare[z] = '\0';
						break;
					}
				}

				for (z = 0; actionpointers[z].name; z++)
				{
					if (actionpointers[z].action == states[num].action)
						break;
				}

				z = 0;
				found = LUA_SetLuaAction(&states[num], actiontocompare);
				if (!found)
				{
					while (actionpointers[z].name)
					{
						if (fastcmp(actiontocompare, actionpointers[z].name))
						{
							states[num].action = actionpointers[z].action;
							found = true;
							break;
						}
						z++;
					}
				}

				if (!found)
					deh_warning("Unknown action %s", actiontocompare);
			}
			else
				deh_warning("Frame %d: unknown word '%s'", num, word1);
		}
	} while (!myfeof(f));

	Z_Free(s);
}

void readsound(MYFILE *f, INT32 num)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word;
	char *word2;
	char *tmp;
	INT32 value;

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			// First remove trailing newline, if there is one
			tmp = strchr(s, '\n');
			if (tmp)
				*tmp = '\0';

			tmp = strchr(s, '#');
			if (tmp)
				*tmp = '\0';
			if (s == tmp)
				continue; // Skip comment lines, but don't break.

			// Set / reset word
			word = s;

			// Get the part before the " = "
			tmp = strchr(s, '=');
			if (tmp)
				*(tmp-1) = '\0';
			else
				break;
			strupr(word);

			// Now get the part after
			word2 = tmp + 2;
			value = atoi(word2); // used for numerical settings

			if (fastcmp(word, "SINGULAR"))
			{
				S_sfx[num].singularity = value;
			}
			else if (fastcmp(word, "PRIORITY"))
			{
				S_sfx[num].priority = value;
			}
			else if (fastcmp(word, "FLAGS"))
			{
				S_sfx[num].flags = value;
			}
			else if (fastcmp(word, "VOLUME"))
			{
				S_sfx[num].volume = value;
			}
			else if (fastcmp(word, "SKINSOUND"))
			{
				// ignore this for the sake of not needing an asset update
				// this was only exposed in blan anyway
				//S_sfx[num].skinsound = get_number(word2);
			}
			else if (fastcmp(word, "CAPTION") || fastcmp(word, "DESCRIPTION"))
			{
				deh_strlcpy(S_sfx[num].caption, word2,
					sizeof(S_sfx[num].caption), va("Sound effect %d: caption", num));
			}
			else
				deh_warning("Sound %d : unknown word '%s'",num,word);
		}
	} while (!myfeof(f));

	Z_Free(s);
}

/** Checks if a game data file name for a mod is good.
 * "Good" means that it contains only alphanumerics, _, and -;
 * ends in ".dat"; has at least one character before the ".dat";
 * and is not "gamedata.dat" (tested case-insensitively).
 *
 * Assumption: that gamedata.dat is the only .dat file that will
 * ever be treated specially by the game.
 *
 * Note: Check for the tail ".dat" case-insensitively since at
 * present, we get passed the filename in all uppercase.
 *
 * \param s Filename string to check.
 * \return True if the filename is good.
 * \sa readmaincfg()
 * \author Graue <graue@oceanbase.org>
 */
/*static boolean GoodDataFileName(const char *s)
{
	const char *p;
	const char *tail = ".dat";

	for (p = s; *p != '\0'; p++)
		if (!isalnum(*p) && *p != '_' && *p != '-' && *p != '.')
			return false;

	p = s + strlen(s) - strlen(tail);
	if (p <= s) return false; // too short
	if (!fasticmp(p, tail)) return false; // doesn't end in .dat
	if (fasticmp(s, "gamedata.dat")) return false;

	return true;
}*/

void reademblemdata(MYFILE *f, INT32 num)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word = s;
	char *word2;
	char *tmp;
	INT32 value;

	memset(&emblemlocations[num-1], 0, sizeof(emblem_t));

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			// First remove trailing newline, if there is one
			tmp = strchr(s, '\n');
			if (tmp)
				*tmp = '\0';

			tmp = strchr(s, '#');
			if (tmp)
				*tmp = '\0';
			if (s == tmp)
				continue; // Skip comment lines, but don't break.

			// Get the part before the " = "
			tmp = strchr(s, '=');
			if (tmp)
				*(tmp-1) = '\0';
			else
				break;
			strupr(word);

			// Now get the part after
			word2 = tmp + 2;
			value = atoi(word2); // used for numerical settings

			// Up here to allow lowercase in hints
			if (fastcmp(word, "HINT"))
			{
				while ((tmp = strchr(word2, '\\')))
					*tmp = '\n';
				deh_strlcpy(emblemlocations[num-1].hint, word2, sizeof (emblemlocations[num-1].hint), va("Emblem %d: hint", num));
				continue;
			}
			strupr(word2);

			if (fastcmp(word, "TYPE"))
			{
				if (fastcmp(word2, "GLOBAL"))
					emblemlocations[num-1].type = ET_GLOBAL;
				else if (fastcmp(word2, "MAP"))
					emblemlocations[num-1].type = ET_MAP;
				else if (fastcmp(word2, "TIME"))
					emblemlocations[num-1].type = ET_TIME;
				else
					emblemlocations[num-1].type = (UINT8)value;
			}
			else if (fastcmp(word, "TAG"))
				emblemlocations[num-1].tag = (INT16)value;
			else if (fastcmp(word, "MAPNAME"))
			{
				emblemlocations[num-1].level = Z_StrDup(word2);
			}
			else if (fastcmp(word, "SPRITE"))
			{
				if (word2[0] >= 'A' && word2[0] <= 'Z')
					value = word2[0];
				else
					value += 'A'-1;

				if (value < 'A' || value > 'Z')
					deh_warning("Emblem %d: sprite must be from A - Z (1 - 26)", num);
				else
					emblemlocations[num-1].sprite = (UINT8)value;
			}
			else if (fastcmp(word, "COLOR"))
				emblemlocations[num-1].color = get_number(word2);
			else if (fastcmp(word, "VAR"))
			{
				Z_Free(emblemlocations[num-1].stringVar);
				emblemlocations[num-1].stringVar = Z_StrDup(word2);

				emblemlocations[num-1].var = get_number(word2);
			}
			else
				deh_warning("Emblem %d: unknown word '%s'", num, word);
		}
	} while (!myfeof(f));

	// Default sprite and color definitions for lazy people like me
	if (!emblemlocations[num-1].sprite) switch (emblemlocations[num-1].type)
	{
		case ET_TIME:
			emblemlocations[num-1].sprite = 'B'; break;
		default:
			emblemlocations[num-1].sprite = 'A'; break;
	}
	if (!emblemlocations[num-1].color) switch (emblemlocations[num-1].type)
	{
		case ET_TIME: //case ET_NTIME:
			emblemlocations[num-1].color = SKINCOLOR_GREY; break;
		default:
			emblemlocations[num-1].color = SKINCOLOR_GOLD; break;
	}

	Z_Free(s);
}

void readextraemblemdata(MYFILE *f, INT32 num)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word = s;
	char *word2;
	char *tmp;
	INT32 value;

	memset(&extraemblems[num-1], 0, sizeof(extraemblem_t));

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			// First remove trailing newline, if there is one
			tmp = strchr(s, '\n');
			if (tmp)
				*tmp = '\0';

			tmp = strchr(s, '#');
			if (tmp)
				*tmp = '\0';
			if (s == tmp)
				continue; // Skip comment lines, but don't break.

			// Get the part before the " = "
			tmp = strchr(s, '=');
			if (tmp)
				*(tmp-1) = '\0';
			else
				break;
			strupr(word);

			// Now get the part after
			word2 = tmp + 2;

			value = atoi(word2); // used for numerical settings

			if (fastcmp(word, "NAME"))
				deh_strlcpy(extraemblems[num-1].name, word2,
					sizeof (extraemblems[num-1].name), va("Extra emblem %d: name", num));
			else if (fastcmp(word, "OBJECTIVE"))
				deh_strlcpy(extraemblems[num-1].description, word2,
					sizeof (extraemblems[num-1].description), va("Extra emblem %d: objective", num));
			else if (fastcmp(word, "CONDITIONSET"))
				extraemblems[num-1].conditionset = (UINT8)value;
			else if (fastcmp(word, "SHOWCONDITIONSET"))
				extraemblems[num-1].showconditionset = (UINT8)value;
			else
			{
				strupr(word2);
				if (fastcmp(word, "SPRITE"))
				{
					if (word2[0] >= 'A' && word2[0] <= 'Z')
						value = word2[0];
					else
						value += 'A'-1;

					if (value < 'A' || value > 'Z')
						deh_warning("Emblem %d: sprite must be from A - Z (1 - 26)", num);
					else
						extraemblems[num-1].sprite = (UINT8)value;
				}
				else if (fastcmp(word, "COLOR"))
					extraemblems[num-1].color = get_number(word2);
				else
					deh_warning("Extra emblem %d: unknown word '%s'", num, word);
			}
		}
	} while (!myfeof(f));

	if (!extraemblems[num-1].sprite)
		extraemblems[num-1].sprite = 'C';
	if (!extraemblems[num-1].color)
		extraemblems[num-1].color = SKINCOLOR_RED;

	Z_Free(s);
}

void readunlockable(MYFILE *f, INT32 num)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word = s;
	char *word2;
	char *tmp;
	INT32 i;

	memset(&unlockables[num], 0, sizeof(unlockable_t));
	unlockables[num].objective[0] = '/';

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			// First remove trailing newline, if there is one
			tmp = strchr(s, '\n');
			if (tmp)
				*tmp = '\0';

			tmp = strchr(s, '#');
			if (tmp)
				*tmp = '\0';
			if (s == tmp)
				continue; // Skip comment lines, but don't break.

			// Get the part before the " = "
			tmp = strchr(s, '=');
			if (tmp)
				*(tmp-1) = '\0';
			else
				break;
			strupr(word);

			// Now get the part after
			word2 = tmp + 2;

			i = atoi(word2); // used for numerical settings

			if (fastcmp(word, "NAME"))
				deh_strlcpy(unlockables[num].name, word2,
					sizeof (unlockables[num].name), va("Unlockable %d: name", num));
			else if (fastcmp(word, "OBJECTIVE"))
				deh_strlcpy(unlockables[num].objective, word2,
					sizeof (unlockables[num].objective), va("Unlockable %d: objective", num));
			else
			{
				strupr(word2);

				if (fastcmp(word, "CONDITIONSET"))
					unlockables[num].conditionset = (UINT8)i;
				else if (fastcmp(word, "SHOWCONDITIONSET"))
					unlockables[num].showconditionset = (UINT8)i;
				else if (fastcmp(word, "NOCECHO"))
					unlockables[num].nocecho = (UINT8)(i || word2[0] == 'T' || word2[0] == 'Y');
				else if (fastcmp(word, "NOCHECKLIST"))
					unlockables[num].nochecklist = (UINT8)(i || word2[0] == 'T' || word2[0] == 'Y');
				else if (fastcmp(word, "TYPE"))
				{
					if (fastcmp(word2, "NONE"))
						unlockables[num].type = SECRET_NONE;
					else if (fastcmp(word2, "HEADER"))
						unlockables[num].type = SECRET_HEADER;
					else if (fastcmp(word2, "SKIN"))
						unlockables[num].type = SECRET_SKIN;
					else if (fastcmp(word2, "WARP"))
						unlockables[num].type = SECRET_WARP;
					else if (fastcmp(word2, "LEVELSELECT"))
						unlockables[num].type = SECRET_LEVELSELECT;
					else if (fastcmp(word2, "TIMEATTACK"))
						unlockables[num].type = SECRET_TIMEATTACK;
					else if (fastcmp(word2, "ITEMBREAKER"))
						unlockables[num].type = SECRET_ITEMBREAKER;
					else if (fastcmp(word2, "SOUNDTEST"))
						unlockables[num].type = SECRET_SOUNDTEST;
					else if (fastcmp(word2, "CREDITS"))
						unlockables[num].type = SECRET_CREDITS;
					else if (fastcmp(word2, "ITEMFINDER"))
						unlockables[num].type = SECRET_ITEMFINDER;
					else if (fastcmp(word2, "EMBLEMHINTS"))
						unlockables[num].type = SECRET_EMBLEMHINTS;
					else if (fastcmp(word2, "ENCORE"))
						unlockables[num].type = SECRET_ENCORE;
					else if (fastcmp(word2, "EXPERTSPEED"))
						unlockables[num].type = SECRET_EXPERTSPEED;
					else if (fastcmp(word2, "HELLATTACK"))
						unlockables[num].type = SECRET_HELLATTACK;
					else if (fastcmp(word2, "PANDORA"))
						unlockables[num].type = SECRET_PANDORA;
					else
						unlockables[num].type = (INT16)i;
				}
				else if (fastcmp(word, "VAR"))
				{
					Z_Free(unlockables[num].stringVar);
					unlockables[num].stringVar = Z_StrDup(word2);
					unlockables[num].variable = G_MapNumber(word2);
				}
				else
					deh_warning("Unlockable %d: unknown word '%s'", num+1, word);
			}
		}
	} while (!myfeof(f)); // finish when the line is empty

	Z_Free(s);
}

static void readcondition(UINT8 set, UINT32 id, char *word2)
{
	INT32 i;
	char *params[4]; // condition, requirement, extra info, extra info
	char *spos;

	conditiontype_t ty;
	INT32 re;
	INT16 x1 = 0, x2 = 0;

	INT32 offset = 0;

	spos = strtok(word2, " ");

	for (i = 0; i < 4; ++i)
	{
		if (spos != NULL)
		{
			params[i] = spos;
			spos = strtok(NULL, " ");
		}
		else
			params[i] = NULL;
	}

	if (!params[0])
	{
		deh_warning("condition line is empty for condition ID %d", id);
		return;
	}

	if				(fastcmp(params[0], "PLAYTIME")
	|| (++offset && fastcmp(params[0], "MATCHESPLAYED")))
	{
		PARAMCHECK(1);
		ty = UC_PLAYTIME + offset;
		re = atoi(params[1]);
	}
	else if (fastcmp(params[0], "POWERLEVEL"))
	{
		PARAMCHECK(2);
		ty = UC_POWERLEVEL;
		re = atoi(params[1]);
		x1 = atoi(params[2]);

		if (x1 < 0 || x1 >= PWRLV_NUMTYPES)
		{
			deh_warning("Power level type %d out of range (0 - %d) for condition ID %d", x1, PWRLV_NUMTYPES-1, id);
			return;
		}
	}
	else if (fastcmp(params[0], "GAMECLEAR"))
	{
		ty = UC_GAMECLEAR;
		re = (params[1]) ? atoi(params[1]) : 1;
	}
	else if (fastcmp(params[0], "OVERALLTIME"))
	{
		PARAMCHECK(1);
		ty = UC_OVERALLTIME;
		re = atoi(params[1]);
	}
	else if ((offset=0) || fastcmp(params[0], "MAPVISITED")
	||        (++offset && fastcmp(params[0], "MAPBEATEN")))
	{
		PARAMCHECK(1);
		ty = UC_MAPVISITED + offset;
		re = G_MapNumber(params[1]);

		if (re >= nummapheaders)
		{
			deh_warning("Invalid level %s for condition ID %d", params[1], id);
			return;
		}
	}
	else if (fastcmp(params[0], "MAPTIME"))
	{
		PARAMCHECK(2);
		ty = UC_MAPTIME;
		re = atoi(params[2]);
		x1 = G_MapNumber(params[1]);

		if (x1 >= nummapheaders)
		{
			deh_warning("Invalid level %s for condition ID %d", params[1], id);
			return;
		}
	}
	else if (fastcmp(params[0], "TRIGGER"))
	{
		PARAMCHECK(1);
		ty = UC_TRIGGER;
		re = atoi(params[1]);

		// constrained by 32 bits
		if (re < 0 || re > 31)
		{
			deh_warning("Trigger ID %d out of range (0 - 31) for condition ID %d", re, id);
			return;
		}
	}
	else if (fastcmp(params[0], "TOTALEMBLEMS"))
	{
		PARAMCHECK(1);
		ty = UC_TOTALEMBLEMS;
		re = atoi(params[1]);
	}
	else if (fastcmp(params[0], "EMBLEM"))
	{
		PARAMCHECK(1);
		ty = UC_EMBLEM;
		re = atoi(params[1]);

		if (re <= 0 || re > MAXEMBLEMS)
		{
			deh_warning("Emblem %d out of range (1 - %d) for condition ID %d", re, MAXEMBLEMS, id);
			return;
		}
	}
	else if (fastcmp(params[0], "EXTRAEMBLEM"))
	{
		PARAMCHECK(1);
		ty = UC_EXTRAEMBLEM;
		re = atoi(params[1]);

		if (re <= 0 || re > MAXEXTRAEMBLEMS)
		{
			deh_warning("Extra emblem %d out of range (1 - %d) for condition ID %d", re, MAXEXTRAEMBLEMS, id);
			return;
		}
	}
	else if (fastcmp(params[0], "CONDITIONSET"))
	{
		PARAMCHECK(1);
		ty = UC_CONDITIONSET;
		re = atoi(params[1]);

		if (re <= 0 || re > MAXCONDITIONSETS)
		{
			deh_warning("Condition set %d out of range (1 - %d) for condition ID %d", re, MAXCONDITIONSETS, id);
			return;
		}
	}
	else
	{
		deh_warning("Invalid condition name %s for condition ID %d", params[0], id);
		return;
	}

	M_AddRawCondition(set, (UINT8)id, ty, re, x1, x2);
}

void readconditionset(MYFILE *f, UINT8 setnum)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word = s;
	char *word2;
	char *tmp;
	UINT8 id;
	UINT8 previd = 0;

	M_ClearConditionSet(setnum);

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			// First remove trailing newline, if there is one
			tmp = strchr(s, '\n');
			if (tmp)
				*tmp = '\0';

			tmp = strchr(s, '#');
			if (tmp)
				*tmp = '\0';
			if (s == tmp)
				continue; // Skip comment lines, but don't break.

			// Get the part before the " = "
			tmp = strchr(s, '=');
			if (tmp)
				*(tmp-1) = '\0';
			else
				break;
			strupr(word);

			// Now get the part after
			word2 = tmp + 2;
			strupr(word2);

			if (fastncmp(word, "CONDITION", 9))
			{
				id = (UINT8)atoi(word + 9);
				if (id == 0)
				{
					deh_warning("Condition set %d: unknown word '%s'", setnum, word);
					continue;
				}
				else if (previd > id)
				{
					// out of order conditions can cause problems, so enforce proper order
					deh_warning("Condition set %d: conditions are out of order, ignoring this line", setnum);
					continue;
				}
				previd = id;

				readcondition(setnum, id, word2);
			}
			else
				deh_warning("Condition set %d: unknown word '%s'", setnum, word);
		}
	} while (!myfeof(f)); // finish when the line is empty

	Z_Free(s);
}

void readmaincfg(MYFILE *f)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word = s;
	char *word2;
	char *tmp;
	INT32 value;

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			// First remove trailing newline, if there is one
			tmp = strchr(s, '\n');
			if (tmp)
				*tmp = '\0';

			tmp = strchr(s, '#');
			if (tmp)
				*tmp = '\0';
			if (s == tmp)
				continue; // Skip comment lines, but don't break.

			// Get the part before the " = "
			tmp = strchr(s, '=');
			if (tmp)
				*(tmp-1) = '\0';
			else
				break;
			strupr(word);

			// Now get the part after
			word2 = tmp + 2;
			strupr(word2);

			value = atoi(word2); // used for numerical settings

			if (fastcmp(word, "EXECCFG"))
			{
				if (strchr(word2, '.'))
					COM_BufAddText(va("exec \"%s\" -immediate\n", word2));
				else
				{
					lumpnum_t lumpnum;
					char newname[9];

					strncpy(newname, word2, 8);

					newname[8] = '\0';

					lumpnum = W_CheckNumForName(newname);

					if (lumpnum == LUMPERROR || W_LumpLength(lumpnum) == 0)
						CONS_Debug(DBG_SETUP, "SOC Error: script lump %s not found/not valid.\n", newname);
					else
						COM_BufInsertText(W_CacheLumpNum(lumpnum, PU_CACHE));
				}
			}
			else if (fastcmp(word, "REDTEAM"))
			{
				skincolor_redteam = (UINT16)get_number(word2);
			}
			else if (fastcmp(word, "BLUETEAM"))
			{
				skincolor_blueteam = (UINT16)get_number(word2);
			}
			else if (fastcmp(word, "REDRING"))
			{
				skincolor_redring = (UINT16)get_number(word2);
			}
			else if (fastcmp(word, "BLUERING"))
			{
				skincolor_bluering = (UINT16)get_number(word2);
			}
			else if (fastcmp(word, "INVULNTICS"))
			{
				invulntics = (UINT16)get_number(word2);
			}
			else if (fastcmp(word, "SNEAKERTICS"))
			{
				sneakertics = (UINT16)get_number(word2);
			}
			else if (fastcmp(word, "FLASHINGTICS"))
			{
				flashingtics = (UINT16)get_number(word2);
			}
			else if (fastcmp(word, "TAILSFLYTICS"))
			{
				tailsflytics = (UINT16)get_number(word2);
			}
			else if (fastcmp(word, "UNDERWATERTICS"))
			{
				underwatertics = (UINT16)get_number(word2);
			}
			else if (fastcmp(word, "SPACETIMETICS"))
			{
				spacetimetics = (UINT16)get_number(word2);
			}
			else if (fastcmp(word, "EXTRALIFETICS"))
			{
				extralifetics = (UINT16)get_number(word2);
			}
			else if (fastcmp(word, "NIGHTSLINKTICS"))
			{
				nightslinktics = (UINT16)get_number(word2);
			}
			else if (fastcmp(word, "GAMEOVERTICS"))
			{
				gameovertics = get_number(word2);
			}
			else if (fastcmp(word, "AMMOREMOVALTICS"))
			{
				ammoremovaltics = get_number(word2);
			}
			// BlanKart: Easy ways to set these for balance purposes
			else if (fastcmp(word, "HYUDOROTIME"))
			{
				hyudorotime = get_number(word2);
			}
			else if (fastcmp(word, "STEALTIME"))
			{
				stealtime = get_number(word2);
			}
			else if (fastcmp(word, "WATERPANELTIME"))
			{
				waterpaneltime = get_number(word2);
			}
			else if (fastcmp(word, "SNEAKERTIME"))
			{
				sneakertime = get_number(word2);
			}
			else if (fastcmp(word, "BUBBLETIME"))
			{
				bubbletime = get_number(word2);
			}
			else if (fastcmp(word, "FLAMETIME"))
			{
				flametime = get_number(word2);
			}
			else if (fastcmp(word, "COMEBACKTIME"))
			{
				comebacktime = get_number(word2);
			}
			else if (fastcmp(word, "GREASETICS"))
			{
				greasetics = get_number(word2);
			}
			else if (fastcmp(word, "WIPEOUTSLOWTIME"))
			{
				wipeoutslowtime = get_number(word2);
			}
			else if (fastcmp(word, "WANTEDREDUCE"))
			{
				wantedreduce = get_number(word2);
			}
			else if (fastcmp(word, "WANTEDFREQUENCY"))
			{
				wantedfrequency = get_number(word2);
			}
			else if (fastcmp(word, "INTROTOPLAY"))
			{
				introtoplay = (UINT8)get_number(word2);
				// range check, you morons.
				if (introtoplay > 128)
					introtoplay = 128;
				introchanged = true;
			}
			else if (fastcmp(word, "CREDITSCUTSCENE"))
			{
				creditscutscene = (UINT8)get_number(word2);
				// range check, you morons.
				if (creditscutscene > 128)
					creditscutscene = 128;
			}
			else if (fastcmp(word, "LOOPTITLE"))
			{
				looptitle = (value || word2[0] == 'T' || word2[0] == 'Y');
				titlechanged = true;
			}
			else if (fastcmp(word, "TITLEMAP"))
			{
				titlemap = Z_StrDup(word2);
				titlechanged = true;
			}
			else if (fastcmp(word, "HIDETITLEPICS") || fastcmp(word, "TITLEPICSHIDE"))
			{
				hidetitlepics = (boolean)(value || word2[0] == 'T' || word2[0] == 'Y');
				titlechanged = true;
			}
			else if (fastcmp(word, "TITLEPICSMODE"))
			{
				if (fastcmp(word2, "USER"))
					ttmode = TTMODE_USER;
				else if (fastcmp(word2, "HIDE") || fastcmp(word2, "HIDDEN") || fastcmp(word2, "NONE"))
				{
					ttmode = TTMODE_USER;
					ttname[0] = 0;
					hidetitlepics = true;
				}
				else if (fastcmp(word2, "KART"))
					ttmode = TTMODE_KART;
				titlechanged = true;
			}
			else if (fastcmp(word, "TITLEPICSNAME"))
			{
				strncpy(ttname, word2, 9);
				titlechanged = true;
			}
			else if (fastcmp(word, "TITLEPICSX"))
			{
				ttx = (INT16)get_number(word2);
				titlechanged = true;
			}
			else if (fastcmp(word, "TITLEPICSY"))
			{
				tty = (INT16)get_number(word2);
				titlechanged = true;
			}
			else if (fastcmp(word, "TITLEPICSLOOP"))
			{
				ttloop = (INT16)get_number(word2);
				titlechanged = true;
			}
			else if (fastcmp(word, "TITLEPICSTICS"))
			{
				tttics = (UINT16)get_number(word2);
				titlechanged = true;
			}
			else if (fastcmp(word, "TITLESCROLLSPEED") || fastcmp(word, "TITLESCROLLXSPEED"))
			{
				titlescrollxspeed = get_number(word2);
				titlechanged = true;
			}
			else if (fastcmp(word, "TITLESCROLLYSPEED"))
			{
				titlescrollyspeed = get_number(word2);
				titlechanged = true;
			}
			else if (fastcmp(word, "NUMDEMOS"))
			{
				numDemos = (UINT8)get_number(word2);
				titlechanged = true;
			}
			else if (fastcmp(word, "DEMODELAYTIME"))
			{
				demoDelayTime = get_number(word2);
				titlechanged = true;
			}
			else if (fastcmp(word, "DEMOIDLETIME"))
			{
				demoIdleTime = get_number(word2);
				titlechanged = true;
			}
			else if (fastcmp(word, "USE1UPSOUND"))
			{
				use1upSound = (UINT8)(value || word2[0] == 'T' || word2[0] == 'Y');
			}
			else if (fastcmp(word, "MAXXTRALIFE"))
			{
				maxXtraLife = (UINT8)get_number(word2);
			}

			else if (fastcmp(word, "GAMEDATA"))
			{
				// just ignore it but dont throw a warning
				/*size_t filenamelen;

				// Check the data filename so that mods
				// can't write arbitrary files.
				if (!GoodDataFileName(word2))
					I_Error("Maincfg: bad data file name '%s'\n", word2);

				G_SaveGameData();
				strlcpy(gamedatafilename, word2, sizeof (gamedatafilename));
				strlwr(gamedatafilename);
				savemoddata = true;
				majormods = false;

				// Also save a time attack folder
				filenamelen = strlen(gamedatafilename)-4;  // Strip off the extension
				filenamelen = min(filenamelen, sizeof (timeattackfolder));
				strncpy(timeattackfolder, gamedatafilename, filenamelen);
				timeattackfolder[min(filenamelen, sizeof (timeattackfolder) - 1)] = '\0';

				strcpy(savegamename, timeattackfolder);
				strlcat(savegamename, "%u.ssg", sizeof(savegamename));
				// can't use sprintf since there is %u in savegamename
				strcatbf(savegamename, srb2home, PATHSEP);

				strcpy(liveeventbackup, va("live%s.bkp", timeattackfolder));
				strcatbf(liveeventbackup, srb2home, PATHSEP);

				refreshdirmenu |= REFRESHDIR_GAMEDATA;
				gamedataadded = true;
				titlechanged = true;*/
			}
			else if (fastcmp(word, "RESETDATA"))
			{
				P_ResetData(value);
				titlechanged = true;
			}
			else if (fastcmp(word, "CUSTOMVERSION"))
			{
				strlcpy(customversionstring, word2, sizeof (customversionstring));
				//titlechanged = true;
			}
			else if (fastcmp(word, "BOOTMAP"))
			{
				bootmap = Z_StrDup(word2);
				//titlechanged = true;
			}
			else if (fastcmp(word, "TUTORIALMAP"))
			{
				tutorialmap = Z_StrDup(word2);
			}
			else
				deh_warning("Maincfg: unknown word '%s'", word);
		}
	} while (!myfeof(f));

	Z_Free(s);
}

//
// SRB2KART
//

static void invalidateacrosscups(UINT16 map)
{
	cupheader_t *cup = kartcupheaders;
	UINT8 i;

	if (map >= nummapheaders)
		return;

	while (cup)
	{
		for (i = 0; i < CUPCACHE_MAX; i++)
		{
			if (cup->cachedlevels[i] != map)
				continue;
			cup->cachedlevels[i] = NEXTMAP_INVALID;
		}
		cup = cup->next;
	}

	mapheaderinfo[map]->cup = NULL;
}

static char *MapNameOrRemoval(char *name)
{
	if (name[0] == '\0'
		|| (name[0] == '/' && name[1] == '\0'))
		return NULL;

	return Z_StrDup(name);
}

void readwipes(MYFILE *f)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word = s;
	char *pword;
	char *word2;
	char *tmp;
	INT32 value;
	INT32 wipeoffset;

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			// First remove trailing newline, if there is one
			tmp = strchr(s, '\n');
			if (tmp)
				*tmp = '\0';

			tmp = strchr(s, '#');
			if (tmp)
				*tmp = '\0';
			if (s == tmp)
				continue; // Skip comment lines, but don't break.

			// Get the part before the " = "
			tmp = strchr(s, '=');
			if (tmp)
				*(tmp-1) = '\0';
			else
				break;
			strupr(word);

			// Now get the part after
			word2 = tmp + 2;
			value = atoi(word2); // used for numerical settings

			if (value < -1 || value > 99)
			{
				deh_warning("Wipes: bad value '%s'", word2);
				continue;
			}
			else if (value == -1)
				value = UINT8_MAX;

			// error catching
			wipeoffset = -1;

			if (fastncmp(word, "LEVEL_", 6))
			{
				pword = word + 6;
				if (fastcmp(pword, "TOBLACK"))
					wipeoffset = wipe_level_toblack;
				else if (fastcmp(pword, "FINAL"))
					wipeoffset = wipe_level_final;
			}
			else if (fastncmp(word, "INTERMISSION_", 13))
			{
				pword = word + 13;
				if (fastcmp(pword, "TOBLACK"))
					wipeoffset = wipe_intermission_toblack;
				else if (fastcmp(pword, "FINAL"))
					wipeoffset = wipe_intermission_final;
			}
			else if (fastncmp(word, "VOTING_", 7))
			{
				pword = word + 7;
				if (fastcmp(pword, "TOBLACK"))
					wipeoffset = wipe_voting_toblack;
				else if (fastcmp(pword, "FINAL"))
					wipeoffset = wipe_voting_final;
			}
			else if (fastncmp(word, "TITLESCREEN_", 12))
			{
				pword = word + 12;
				if (fastcmp(pword, "TOBLACK"))
					wipeoffset = wipe_titlescreen_toblack;
				else if (fastcmp(pword, "FINAL"))
					wipeoffset = wipe_titlescreen_final;
			}
			else if (fastncmp(word, "CREDITS_", 8))
			{
				pword = word + 8;
				if (fastcmp(pword, "TOBLACK"))
					wipeoffset = wipe_credits_toblack;
				else if (fastcmp(pword, "FINAL"))
					wipeoffset = wipe_credits_final;
				else if (fastcmp(pword, "INTERMEDIATE"))
					wipeoffset = wipe_credits_intermediate;
			}
			else if (fastncmp(word, "EVALUATION_", 11))
			{
				pword = word + 11;
				if (fastcmp(pword, "TOBLACK"))
					wipeoffset = wipe_evaluation_toblack;
				else if (fastcmp(pword, "FINAL"))
					wipeoffset = wipe_evaluation_final;
			}
			else if (fastncmp(word, "ENCORE_", 7))
			{
				pword = word + 7;
				if (fastcmp(pword, "TOINVERT"))
					wipeoffset = wipe_encore_toinvert;
				else if (fastcmp(pword, "TOWHITE"))
					wipeoffset = wipe_encore_towhite;
			}

			if (wipeoffset < 0)
			{
				deh_warning("Wipes: unknown word '%s'", word);
				continue;
			}

			if (value == UINT8_MAX
			 && (wipeoffset <= wipe_level_toblack || wipeoffset >= wipe_encore_toinvert))
			{
				 // Cannot disable non-toblack wipes
				 // (or the level toblack wipe, or the special encore wipe)
				deh_warning("Wipes: can't disable wipe of type '%s'", word);
				continue;
			}

			wipedefs[wipeoffset] = (UINT8)value;
		}
	} while (!myfeof(f));

	Z_Free(s);
}

//
// SRB2KART
//

void readcupheader(MYFILE *f, cupheader_t *cup)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word;
	char *word2;
	char *tmp;
	INT32 i;

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			// First remove trailing newline, if there is one
			tmp = strchr(s, '\n');
			if (tmp)
				*tmp = '\0';

			tmp = strchr(s, '#');
			if (tmp)
				*tmp = '\0';
			if (s == tmp)
				continue; // Skip comment lines, but don't break.

			// Set / reset word, because some things (Lua.) move it
			word = s;

			// Get the part before the " = "
			tmp = strchr(s, '=');
			if (tmp)
				*(tmp-1) = '\0';
			else
				break;
			strupr(word);

			// Now get the part after
			word2 = tmp + 2;
			i = atoi(word2); // used for numerical settings
			strupr(word2);

			if (fastcmp(word, "MONITOR"))
			{
				if (i > 0 && i < 10)
					cup->monitor = i;
				else if (!word2[0] || word2[1] != '\0' || word2[0] == '0')
					deh_warning("%s Cup: Invalid monitor type \"%s\" (should be 1-9 or A-Z)\n", cup->name, word2);
				else
					cup->monitor = (word2[0] - 'A') + 10;
			}
			else if (fastcmp(word, "ICON"))
			{
				deh_strlcpy(cup->icon, word2,
					sizeof(cup->icon), va("%s Cup: icon", cup->name));
			}
			else if (fastcmp(word, "LEVELLIST"))
			{
				while (cup->numlevels > 0)
				{
					cup->numlevels--;
					Z_Free(cup->levellist[cup->numlevels]);
					cup->levellist[cup->numlevels] = NULL;
					if (cup->cachedlevels[cup->numlevels] == NEXTMAP_INVALID)
						continue;
					invalidateacrosscups(cup->cachedlevels[cup->numlevels]);
				}

				tmp = strtok(word2,",");
				do {
					if (cup->numlevels >= MAXLEVELLIST)
					{
						deh_warning("%s Cup: reached max levellist (%d)\n", cup->name, MAXLEVELLIST);
						break;
					}

					cup->levellist[cup->numlevels] = Z_StrDup(tmp);
					cup->cachedlevels[cup->numlevels] = NEXTMAP_INVALID;
					cup->numlevels++;
				} while((tmp = strtok(NULL,",")) != NULL);
			}
			else if (fastcmp(word, "BONUSGAME"))
			{
				while (cup->numbonus > 0)
				{
					cup->numbonus--;
					Z_Free(cup->levellist[CUPCACHE_BONUS + cup->numbonus]);
					cup->levellist[CUPCACHE_BONUS + cup->numbonus] = NULL;
					if (cup->cachedlevels[CUPCACHE_BONUS + cup->numbonus] == NEXTMAP_INVALID)
						continue;
					invalidateacrosscups(cup->cachedlevels[CUPCACHE_BONUS + cup->numbonus]);
				}

				tmp = strtok(word2,",");
				do {
					if (cup->numbonus >= MAXBONUSLIST)
					{
						deh_warning("%s Cup: reached max bonus list (%d)\n", cup->name, MAXBONUSLIST);
						break;
					}

					cup->levellist[CUPCACHE_BONUS + cup->numbonus] = MapNameOrRemoval(tmp);
					cup->cachedlevels[CUPCACHE_BONUS + cup->numbonus] = NEXTMAP_INVALID;

					if (cup->levellist[CUPCACHE_BONUS + cup->numbonus] == NULL)
						break;

					cup->numbonus++;
				} while((tmp = strtok(NULL,",")) != NULL);
			}
			else if (fastcmp(word, "SPECIALSTAGE"))
			{
				Z_Free(cup->levellist[CUPCACHE_SPECIAL]);
				cup->levellist[CUPCACHE_SPECIAL] = Z_StrDup(word2);
				cup->cachedlevels[CUPCACHE_SPECIAL] = NEXTMAP_INVALID;
			}
			else if (fastcmp(word, "EMERALDNUM"))
			{
				if (i >= 0 && i <= 14)
					cup->emeraldnum = (UINT8)i;
				else
					deh_warning("%s Cup: invalid emerald number %d", cup->name, i);
			}
			else if (fastcmp(word, "UNLOCKABLE"))
			{
				if (i >= 0 && i <= MAXUNLOCKABLES) // 0 for no unlock required, anything else requires something
					cup->unlockrequired = (SINT8)i - 1;
				else
					deh_warning("%s Cup: invalid unlockable number %d", cup->name, i);
			}
			else
				deh_warning("%s Cup: unknown word '%s'", cup->name, word);
		}
	} while (!myfeof(f)); // finish when the line is empty

	Z_Free(s);
}

void readfollower(MYFILE *f)
{
	char *s;
	char *word, *word2, dname[SKINNAMESIZE+1];
	char *tmp;
	char testname[SKINNAMESIZE+1];

	boolean nameset;
	INT32 fallbackstate = 0;
	INT32 res;
	INT32 i;

	if (numfollowers > MAXFOLLOWERS)
	{
		deh_warning("Error: Too many followers, cannot add anymore.\n");
		return;
	}

	s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);

	// Ready the default variables for followers. We will overwrite them as we go! We won't set the name or states RIGHT HERE as this is handled down instead.
	followers[numfollowers].mode = FOLLOWERMODE_FLOAT;
	followers[numfollowers].scale = FRACUNIT;
	followers[numfollowers].bubblescale = 0; // No bubble by default
	followers[numfollowers].atangle = FixedAngle(230 * FRACUNIT);
	followers[numfollowers].dist = 32*FRACUNIT; // changed from 16 to 32 to better account for ogl models
	followers[numfollowers].height = 16*FRACUNIT;
	followers[numfollowers].zoffs = 32*FRACUNIT;
	followers[numfollowers].horzlag = 3*FRACUNIT;
	followers[numfollowers].vertlag = 6*FRACUNIT;
	followers[numfollowers].anglelag = 8*FRACUNIT;
	followers[numfollowers].bobspeed = TICRATE*2;
	followers[numfollowers].bobamp = 4*FRACUNIT;
	followers[numfollowers].hitconfirmtime = TICRATE;
	followers[numfollowers].horntime = TICRATE;
	followers[numfollowers].defaultcolor = FOLLOWERCOLOR_MATCH;
	followers[numfollowers].hornsound = sfx_horn00;
	strcpy(followers[numfollowers].icon, "MISSING");

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			tmp = strchr(s, '#');
			if (tmp)
				*tmp = '\0';
			if (s == tmp)
				continue; // Skip comment lines, but don't break.

			word = strtok(s, " ");
			if (word)
				strupr(word);
			else
				break;

			word2 = strtok(NULL, " = ");

			if (!word2)
				break;

			if (word2[strlen(word2)-1] == '\n')
				word2[strlen(word2)-1] = '\0';

			if (fastcmp(word, "NAME"))
			{
				strlcpy(followers[numfollowers].name, word2, SKINNAMESIZE+1);
				nameset = true;
			}
			else if (fastcmp(word, "ICON"))
			{
				strlcpy(followers[numfollowers].icon, word2, 8+1);
				nameset = true;
			}
			else if (fastcmp(word, "MODE"))
			{
				if (word2)
					strupr(word2);

				if (fastcmp(word2, "FLOAT") || fastcmp(word2, "DEFAULT"))
					followers[numfollowers].mode = FOLLOWERMODE_FLOAT;
				else if (fastcmp(word2, "GROUND"))
					followers[numfollowers].mode = FOLLOWERMODE_GROUND;
				else
					deh_warning("Follower %d: unknown follower mode '%s'", numfollowers, word2);
			}
			else if (fastcmp(word, "DEFAULTCOLOR"))
			{
				if (word2)
					strupr(word2);

				if (fastcmp(word2, "MATCH"))
					followers[numfollowers].defaultcolor = FOLLOWERCOLOR_MATCH;
				else if (fastcmp(word2, "OPPOSITE"))
					followers[numfollowers].defaultcolor = FOLLOWERCOLOR_OPPOSITE;
				else
				{
					skincolornum_t color = get_skincolor(word2);
					followers[numfollowers].defaultcolor = color == MAXSKINCOLORS ? SKINCOLOR_GREEN : color;
				}
			}
			else if (fastcmp(word, "HORNSOUND"))
			{
				followers[numfollowers].hornsound = get_number(word2);
			}
			else if (fastcmp(word, "SCALE"))
			{
				followers[numfollowers].scale = (fixed_t)get_number(word2);
			}
			else if (fastcmp(word, "BUBBLESCALE"))
			{
				followers[numfollowers].bubblescale = (fixed_t)get_number(word2);
			}
			else if (fastcmp(word, "ATANGLE"))
			{
				followers[numfollowers].atangle = (angle_t)(get_number(word2) * ANG1);
			}
			else if (fastcmp(word, "HORZLAG"))
			{
				followers[numfollowers].horzlag = (fixed_t)get_number(word2);
			}
			else if (fastcmp(word, "VERTLAG"))
			{
				followers[numfollowers].vertlag = (fixed_t)get_number(word2);
			}
			else if (fastcmp(word, "ANGLELAG"))
			{
				followers[numfollowers].anglelag = (fixed_t)get_number(word2);
			}
			else if (fastcmp(word, "BOBSPEED"))
			{
				followers[numfollowers].bobspeed = (tic_t)get_number(word2);
			}
			else if (fastcmp(word, "BOBAMP"))
			{
				followers[numfollowers].bobamp = (fixed_t)get_number(word2);
			}
			else if (fastcmp(word, "ZOFFSET") || (fastcmp(word, "ZOFFS")))
			{
				followers[numfollowers].zoffs = (fixed_t)get_number(word2);
			}
			else if (fastcmp(word, "DISTANCE") || (fastcmp(word, "DIST")))
			{
				followers[numfollowers].dist = (fixed_t)get_number(word2);
			}
			else if (fastcmp(word, "HEIGHT"))
			{
				followers[numfollowers].height = (fixed_t)get_number(word2);
			}
			else if (fastcmp(word, "IDLESTATE"))
			{
				if (word2)
					strupr(word2);
				followers[numfollowers].idlestate = get_number(word2);
				fallbackstate = followers[numfollowers].idlestate;
			}
			else if (fastcmp(word, "FOLLOWSTATE"))
			{
				if (word2)
					strupr(word2);
				followers[numfollowers].followstate = get_number(word2);
			}
			else if (fastcmp(word, "HURTSTATE"))
			{
				if (word2)
					strupr(word2);
				followers[numfollowers].hurtstate = get_number(word2);
			}
			else if (fastcmp(word, "LOSESTATE"))
			{
				if (word2)
					strupr(word2);
				followers[numfollowers].losestate = get_number(word2);
			}
			else if (fastcmp(word, "WINSTATE"))
			{
				if (word2)
					strupr(word2);
				followers[numfollowers].winstate = get_number(word2);
			}
			else if (fastcmp(word, "HITSTATE") || (fastcmp(word, "HITCONFIRMSTATE")))
			{
				if (word2)
					strupr(word2);
				followers[numfollowers].hitconfirmstate = get_number(word2);
			}
			else if (fastcmp(word, "HITTIME") || (fastcmp(word, "HITCONFIRMTIME")))
			{
				followers[numfollowers].hitconfirmtime = (tic_t)get_number(word2);
			}
			else if (fastcmp(word, "HORNSTATE"))
			{
				if (word2)
					strupr(word2);
				followers[numfollowers].hornstate = get_number(word2);
			}
			else if (fastcmp(word, "HORNTIME"))
			{
				followers[numfollowers].horntime = (tic_t)get_number(word2);
			}
			else
			{
				deh_warning("Follower %d: unknown word '%s'", numfollowers, word);
			}
		}
	} while (!myfeof(f)); // finish when the line is empty

	if (!nameset)
	{
		// well this is problematic.
		strlcpy(followers[numfollowers].name, va("Follower%d", numfollowers), SKINNAMESIZE+1); // this is lazy, so what
	}

	// set skin name (this is just the follower's name in lowercases):
	// but before we do, let's... actually check if another follower isn't doing the same shit...

	strlcpy(testname, followers[numfollowers].name, SKINNAMESIZE+1);

	// lower testname for skin checks...
	strlwr(testname);
	res = K_FollowerAvailable(testname);
	if (res > -1)	// yikes, someone else has stolen our name already
	{
		INT32 startlen = strlen(testname);
		char cpy[2];
		//deh_warning("There was already a follower with the same name. (%s)", testname);	This warning probably isn't necessary anymore?
		sprintf(cpy, "%d", numfollowers);
		memcpy(&testname[startlen], cpy, 2);
		// in that case, we'll be very lazy and copy numfollowers to the end of our skin name.
	}

	strlcpy(followers[numfollowers].skinname, testname, SKINNAMESIZE+1);
	strlcpy(dname, followers[numfollowers].skinname, SKINNAMESIZE+1);	// display name, just used for printing succesful stuff or errors later down the line.

	// now that the skin name is ready, post process the actual name to turn the underscores into spaces!
	for (i = 0; followers[numfollowers].name[i]; i++)
	{
		if (followers[numfollowers].name[i] == '_')
			followers[numfollowers].name[i] = ' ';
	}

	// fallbacks for variables
	// Print a warning if the variable is on a weird value and set it back to the minimum available if that's the case.

	if (followers[numfollowers].mode < FOLLOWERMODE_FLOAT || followers[numfollowers].mode >= FOLLOWERMODE__MAX)
	{
		followers[numfollowers].mode = FOLLOWERMODE_FLOAT;
		deh_warning("Follower '%s': Value for 'mode' should be between %d and %d.", dname, FOLLOWERMODE_FLOAT, FOLLOWERMODE__MAX-1);
	}

#define FALLBACK(field, field2, threshold, set) \
if ((signed)followers[numfollowers].field < threshold) \
{ \
	followers[numfollowers].field = set; \
	deh_warning("Follower '%s': Value for '%s' is too low! Minimum should be %d. Value was overwritten to %d.", dname, field2, threshold, set); \
} \

	FALLBACK(dist, "DIST", 0, 0);
	FALLBACK(height, "HEIGHT", 1, 1);
	FALLBACK(zoffs, "ZOFFS", 0, 0);
	FALLBACK(horzlag, "HORZLAG", FRACUNIT, FRACUNIT);
	FALLBACK(vertlag, "VERTLAG", FRACUNIT, FRACUNIT);
	FALLBACK(anglelag, "ANGLELAG", FRACUNIT, FRACUNIT);
	FALLBACK(bobamp, "BOBAMP", 0, 0);
	FALLBACK(bobspeed, "BOBSPEED", 0, 0);
	FALLBACK(hitconfirmtime, "HITCONFIRMTIME", 1, 1);
	FALLBACK(horntime, "HORNTIME", 1, 1);
	FALLBACK(scale, "SCALE", 1, 1);				// No null/negative scale
	FALLBACK(bubblescale, "BUBBLESCALE", 0, 0);	// No negative scale

#undef FALLBACK

	// Special case for color I suppose
	if (followers[numfollowers].defaultcolor > (unsigned)(numskincolors-1)
	 && followers[numfollowers].defaultcolor != FOLLOWERCOLOR_MATCH
	 && followers[numfollowers].defaultcolor != FOLLOWERCOLOR_OPPOSITE)
	{
		followers[numfollowers].defaultcolor = FOLLOWERCOLOR_MATCH;
		deh_warning("Follower \'%s\': Value for 'color' should be between 1 and %d.\n", dname, numskincolors-1);
	}

	// also check if we forgot states. If we did, we will set any missing state to the follower's idlestate.
	// Print a warning in case we don't have a fallback and set the state to S_INVISIBLE (rather than S_NULL) if unavailable.

#define NOSTATE(field, field2) \
if (!followers[numfollowers].field) \
{ \
	followers[numfollowers].field = fallbackstate ? fallbackstate : S_INVISIBLE; \
	if (!fallbackstate) \
		deh_warning("Follower '%s' is missing state definition for '%s', no idlestate fallback was found", dname, field2); \
} \

	NOSTATE(idlestate, "IDLESTATE");
	NOSTATE(followstate, "FOLLOWSTATE");
	NOSTATE(hurtstate, "HURTSTATE");
	NOSTATE(losestate, "LOSESTATE");
	NOSTATE(winstate, "WINSTATE");
	NOSTATE(hitconfirmstate, "HITCONFIRMSTATE");
	NOSTATE(hornstate, "HORNSTATE");
#undef NOSTATE

	CONS_Printf("Added follower '%s'\n", dname);
	numfollowers++; // add 1 follower
	Z_Free(s);
}

void readweather(MYFILE *f, INT32 num)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word;
	char *word2;
	char *tmp;

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			// First remove trailing newline, if there is one
			tmp = strchr(s, '\n');
			if (tmp)
				*tmp = '\0';

			tmp = strchr(s, '#');
			if (tmp)
				*tmp = '\0';
			if (s == tmp)
				continue; // Skip comment lines, but don't break.

			// Set / reset word
			word = s;

			// Get the part before the " = "
			tmp = strchr(s, '=');
			if (tmp)
				*(tmp-1) = '\0';
			else
				break;
			strupr(word);

			// Now get the part after
			word2 = tmp + 2;

			if (fastcmp(word, "TYPE"))
			{
				mobjtype_t mt = get_mobjtype(word2);
				precipprops[num].type = mt == NUMMOBJTYPES ? MT_NULL : mt;
			}
			else if (fastcmp(word, "EFFECTS"))
			{
				precipprops[num].effects = get_number(word2);
			}
			else
				deh_warning("Weather %d : unknown word '%s'", num, word);
		}
	} while (!myfeof(f));

	Z_Free(s);
}

#define WARN(str, ...) deh_warning("KartItem %s: " str, strbuf_get(kartitemnames, item->info.nameofs), __VA_ARGS__)
#define WARN0(str) deh_warning("KartItem %s: " str, strbuf_get(kartitemnames, item->info.nameofs))
void readkartitem(MYFILE *f, kartitem_t *item)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word = s;
	char *word2;
	char *tmp;
	INT32 i;

	do if (myfgets(s, MAXLINELEN, f))
	{
		if (s[0] == '\n')
			break;

		// First remove trailing newline, if there is one
		tmp = strchr(s, '\n');
		if (tmp)
			*tmp = '\0';

		tmp = strchr(s, '#');
		if (tmp)
			*tmp = '\0';
		if (s == tmp)
			continue; // Skip comment lines, but don't break.

		// Get the part before the " = "
		tmp = strchr(s, '=');
		if (tmp)
			*(tmp-1) = '\0';
		else
			break;
		strupr(word);

		// Now get the part after
		word2 = (tmp += 2);
		strupr(word2);

		if (fastcmp(word, "BIGPATCHES") || fastcmp(word, "SMALLPATCHES") ||
		    fastcmp(word, "BIGALTPATCHES") || fastcmp(word, "SMALLALTPATCHES"))
		{
			UINT8 patch_idx = (word[0] == 'B') ? 0 : 1;

			// Alt. patches are stored in the same array, just in their own slots.
			patch_idx += (word[3 + (patch_idx << 1)] == 'A') ? 2 : 0;

			kartitemgraphics_t* graphics = &item->graphics[patch_idx];

			if (graphics == &item->graphics[0] && item->spritedef.numframes > 0)
			{
				Z_Free(item->spritedef.spriteframes);
				memset(&item->spritedef, 0, sizeof(item->spritedef));
			}
			for (i = 0; i < graphics->numpatches; i++)
			{
				Z_Free(graphics->patchnames[i]);
				Patch_Free(graphics->patches[i]);
				graphics->patches[i] = NULL;
			}

			tmp = strtok(word2, ",");
			for (graphics->numpatches = 0; graphics->numpatches < MAXITEMPATCHES;)
			{
				graphics->patchnames[graphics->numpatches++] = Z_StrDup(tmp);

				if ((tmp = strtok(NULL, ",")) == NULL)
					break;
			}
		}
		else if (fastcmp(word, "FLAGS") || fastcmp(word, "FLAGSALT"))
		{
			item->flags[word[5] == 'A' ? 1 : 0] = get_number(word2);
		}
		else if (fastcmp(word, "EQUIP") || fastcmp(word, "EQUIPALT"))
		{
			item->equipstyle[word[5] == 'A' ? 1 : 0] = get_number(word2);
		}
		else
			WARN("unknown word '%s'", word);
	}
	while (!myfeof(f)); // finish when the line is empty

	Z_Free(s);
}
#undef WARN
#undef WARN0

#define WARN(str, ...) deh_warning("KartResult %s: " str, result->cvar->name, __VA_ARGS__)
#define WARN0(str) deh_warning("KartResult %s: " str, result->cvar->name)
static CV_PossibleValue_t kartrunneraugment_cons_t[] = {{0, "MIN"}, {30000, "MAX"}, {0, NULL}};

void readkartresult(MYFILE *f, kartresult_t *result)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word = s;
	char *word2;
	char *tmp;

	do if (myfgets(s, MAXLINELEN, f))
	{
		if (s[0] == '\n')
			break;

		// First remove trailing newline, if there is one
		tmp = strchr(s, '\n');
		if (tmp)
			*tmp = '\0';

		tmp = strchr(s, '#');
		if (tmp)
			*tmp = '\0';
		if (s == tmp)
			continue; // Skip comment lines, but don't break.

		// Get the part before the " = "
		tmp = strchr(s, '=');
		if (tmp)
			*(tmp-1) = '\0';
		else
			break;
		strupr(word);

		// Now get the part after
		word2 = tmp + 2;
		//strupr(word2);

		if (fastcmp(word, "TYPE"))
		{
			if (result->isalt)
			{
				WARN0("can't set type for alternate result");
				continue;
			}
			result->type = get_kartitem(word2);
		}
		else if (fastcmp(word, "AMOUNT"))
		{
			if (result->isalt)
			{
				WARN0("can't set amount for alternate result");
				continue;
			}
			result->amount = get_number(word2);
		}
		else if (fastcmp(word, "ISALT"))
		{
			result->isalt = (boolean)(atoi(word2) || word2[0] == 'T' || word2[0] == 'Y');
		}
		else if (fastcmp(word, "FLAGS"))
		{
			result->flags = get_number(word2);
		}
		else if (fastncmp(word, "RUNNERDISTANCE", 14))
		{
			const UINT8 idx = (fastcmp(word+14, "LEGACY") ? 1 : 0);

			result->baserunneraug[idx] = get_number(word2);

			if (result->augcvar[idx] == NULL)
			{
				char *cvname;

				UINT16 distprefixlen = strlen(result->isalt ? "runnerdistalt" : "runnerdist");

				if (idx)
				{
					cvname = malloc(strlen(result->cvar->name) + 9 + distprefixlen);
					sprintf(cvname, "%s_%s_legacy", result->isalt ? "runnerdistalt" : "runnerdist", result->cvar->name);
					strlwr(cvname);
				}
				else
				{
					cvname = malloc(strlen(result->cvar->name) + 2 + distprefixlen);
					sprintf(cvname, "%s_%s", result->isalt ? "runnerdistalt" : "runnerdist", result->cvar->name);
					strlwr(cvname);
				}

				consvar_t *var = Z_Calloc(sizeof(consvar_t), PU_STATIC, &result->augcvar[idx]);
				var->name = cvname;

				char *defval = malloc(5+1); // UINT16_MAX + NUL

				if (defval == NULL)
					I_Error("readkartresult: Out of memory!\n");

				snprintf(defval, 6, "%u", result->baserunneraug[idx]);
				var->defaultvalue = defval;

				var->flags = CV_NETVAR|CV_CHEAT;
				var->PossibleValue = kartrunneraugment_cons_t;
				var->func = NULL;
				CV_RegisterVar(var);
			}
			else
			{
				char *defval; // uh oh! const cast!
				memcpy(&defval, &result->augcvar[idx]->defaultvalue, sizeof(defval));
				snprintf(defval, 6, "%u", result->baserunneraug[idx]);
				CV_StealthSet(result->augcvar[idx], result->augcvar[idx]->defaultvalue);
			}
		}
		else if (fastncmp(word, "ODDS", 4))
		{
			UINT8 oddstable;
			if (fastcmp(word+4, "RACE"))
				oddstable = ODDS_RACE;
			else if (fastcmp(word+4, "BATTLE"))
				oddstable = ODDS_BATTLE;
			else if (fastcmp(word+4, "SPECIAL"))
				oddstable = ODDS_SPECIAL;
			else
			{
				WARN("unknown word '%s'", word);
				continue;
			}

			UINT8 *odds = result->odds[oddstable];
			memset(odds, 0, sizeof(*odds)*MAXODDS);
			tmp = strtok(word2,",");
			for (UINT8 i = 0; i <= MAXODDS; i++)
			{
				if (i == oddstablemax[oddstable])
				{
					WARN("odds table %s is limited to %d odds", word+4, oddstablemax[oddstable]);
					continue;
				}
				odds[i] = atoi(tmp);
				if ((tmp = strtok(NULL,",")) == NULL)
					break;
			}
		}
		else if (fastncmp(word, "UNIQUEODDS", 10))
		{
			UINT8 oddstable;
			if (fastcmp(word+10, "RACE"))
				oddstable = ODDS_RACE;
			else if (fastcmp(word+10, "BATTLE"))
				oddstable = ODDS_BATTLE;
			else if (fastcmp(word+10, "SPECIAL"))
				oddstable = ODDS_SPECIAL;

			else
			{
				WARN("unknown word '%s'", word);
				continue;
			}

			result->unique_odds[oddstable] = get_useoddsfunc(word2);
		}
		else if (fastcmp(word, "COOLDOWNTIME"))
		{
			result->basecooldown = atoi(word2) * TICRATE;
		}
		else if (fastcmp(word, "DISPLAYNAME"))
		{
			result->displayname = Z_StrDup(word2);
		}
		else
			WARN("unknown word '%s'", word);
	}
	while (!myfeof(f)); // finish when the line is empty

	Z_Free(s);
}
#undef WARN
#undef WARN0

//
//
//

mobjtype_t get_mobjtype(const char *word)
{ // Returns the value of MT_ enumerations
	mobjtype_t i;
	if (*word >= '0' && *word <= '9')
		return atoi(word);
	if (fastncmp("MT_",word,3))
		word += 3; // take off the MT_
	i = DEH_FindMobjtype(word);
	if (i == NUMMOBJTYPES)
		deh_warning("Couldn't find mobjtype named 'MT_%s'", word);
	return i;
}

statenum_t get_state(const char *word)
{ // Returns the value of S_ enumerations
	statenum_t i;
	if (*word >= '0' && *word <= '9')
		return atoi(word);
	if (fastncmp("S_",word,2))
		word += 2; // take off the S_
	i = DEH_FindState(word);
	if (i == NUMSTATES)
		deh_warning("Couldn't find state named 'S_%s'", word);
	return i;
}

skincolornum_t get_skincolor(const char *word)
{ // Returns the value of SKINCOLOR_ enumerations
	skincolornum_t i;
	if (*word >= '0' && *word <= '9')
		return atoi(word);
	if (fastncmp("SKINCOLOR_",word,10))
		word += 10; // take off the SKINCOLOR_
	i = DEH_FindSkincolor(word);
	if (i == MAXSKINCOLORS)
		deh_warning("Couldn't find skincolor named 'SKINCOLOR_%s'", word);
	return i;
}

spritenum_t get_sprite(const char *word)
{ // Returns the value of SPR_ enumerations
	spritenum_t i;
	if (*word >= '0' && *word <= '9')
		return atoi(word);
	if (fastncmp("SPR_",word,4))
		word += 4; // take off the SPR_
	for (i = 0; i < NUMSPRITES; i++)
		if (memcmp(word,sprnames[i],4)==0)
			return i;
	deh_warning("Couldn't find sprite named 'SPR_%s'",word);
	return SPR_NULL;
}

playersprite_t get_sprite2(const char *word)
{ // Returns the value of SPR2_ enumerations
	playersprite_t i;
	if (*word >= '0' && *word <= '9')
		return atoi(word);
	if (fastncmp("SPR2_",word,5))
		word += 5; // take off the SPR2_
	for (i = 0; i < NUMPLAYERSPRITES; i++)
		if (!spr2names[i][4] && memcmp(word,spr2names[i],4)==0)
			return i;
	deh_warning("Couldn't find sprite named 'SPR2_%s'",word);
	return SPR2_STIN;
}

sfxenum_t get_sfx(const char *word)
{ // Returns the value of SFX_ enumerations
	sfxenum_t i;
	if (*word >= '0' && *word <= '9')
		return atoi(word);
	if (fastncmp("SFX_",word,4))
		word += 4; // take off the SFX_
	else if (fastncmp("DS",word,2))
		word += 2; // take off the DS
	for (i = 0; i < NUMSFX; i++)
		if (S_sfx[i].name && fasticmp(word, S_sfx[i].name))
			return i;
	deh_warning("Couldn't find sfx named 'SFX_%s'",word);
	return NUMSFX;
}

menutype_t get_menutype(const char *word)
{ // Returns the value of MN_ enumerations
	menutype_t i;
	if (*word >= '0' && *word <= '9')
		return atoi(word);
	if (fastncmp("MN_",word,3))
		word += 3; // take off the MN_
	i = DEH_FindMenutype(word);
	if (i == MAXMENUTYPES)
		deh_warning("Couldn't find menutype named 'MN_%s'", word);
	return i;
}

menufunc_f *get_menuroutine(const char *word)
{ // Returns the value of MR_ enumerations
	size_t i;
	if (fastncmp("MR_",word,3))
		word += 3; // take off the MR_
	for (i = 0; MENU_ROUTINES[i].name; i++)
		if (fasticmp(word, MENU_ROUTINES[i].name))
			return MENU_ROUTINES[i].routine;
	deh_warning("Couldn't find menu routine named 'MR_%s'",word);
	return NULL;
}

menudrawer_f *get_menudrawer(const char *word)
{ // Returns the value of MD_ enumerations
	size_t i;
	if (fastncmp("MD_",word,3))
		word += 3; // take off the MD_
	for (i = 0; MENU_DRAWERS[i].name; i++)
		if (fasticmp(word, MENU_DRAWERS[i].name))
			return MENU_DRAWERS[i].drawer;
	deh_warning("Couldn't find menu drawer named 'MD_%s'",word);
	return NULL;
}

panelfunc_f *get_panelfunc(const char *word)
{ // Returns the value of MP_ enumerations
	size_t i;
	if (fastncmp("MP_",word,3))
		word += 3; // take off the MP_
	for (i = 0; PANEL_FUNCS[i].name; i++)
		if (fasticmp(word, PANEL_FUNCS[i].name))
			return PANEL_FUNCS[i].func;
	deh_warning("Couldn't find panel func named 'MP_%s'",word);
	return NULL;
}

/*static INT16 get_gametype(const char *word)
{ // Returns the value of GT_ enumerations
	INT16 i;
	if (*word >= '0' && *word <= '9')
		return atoi(word);
	if (fastncmp("GT_",word,3))
		word += 3; // take off the GT_
	for (i = 0; i < MAXGAMETYPES; i++)
		if (fastcmp(word, Gametype_ConstantNames[i]+3))
			return i;
	deh_warning("Couldn't find gametype named 'GT_%s'",word);
	return GT_COOP;
}*/

preciptype_t get_precip(const char *word)
{
	// Returns the value of PRECIP_ enumerations
	if (*word >= '0' && *word <= '9')
		return atoi(word);
	if (fastncmp("PRECIP_",word,7))
	{
		fixed_t eval = LUA_EvalMath(word);

		if (eval)
			return eval;

		if (fasticmp("PRECIP_NONE",word))
			return PRECIP_NONE;
	}
	deh_warning("Couldn't find weather type named 'PRECIP_%s'",word);
	return PRECIP_NONE;
}

kartitemtype_e get_kartitem(const char *word)
{ // Returns the value of KITEM_ enumerations
	kartitemtype_e i;
	if (fastncmp("KITEM_",word,6))
		word += 6; // take off the KITEM_
	i = DEH_FindKartItem(word);
	if (i == MAXKARTITEMS)
		deh_warning("Couldn't find kartitem named 'KITEM_%s'", word);
	return i;
}

useoddsfunc_f *get_useoddsfunc(const char *word)
{ // Returns the value of KO_ enumerations
	size_t i;
	if (fastncmp("KO_",word,3))
		word += 3; // take off the KO_
	for (i = 0; USEODDS_FUNCS[i].name; i++)
		if (fasticmp(word, USEODDS_FUNCS[i].name))
			return USEODDS_FUNCS[i].func;
	deh_warning("Couldn't find odds function named 'KO_%s'",word);
	return NULL;
}

/// \todo Make ANY of this completely over-the-top math craziness obey the order of operations.
static fixed_t op_mul(fixed_t a, fixed_t b) { return a*b; }
static fixed_t op_div(fixed_t a, fixed_t b) { return a/b; }
static fixed_t op_add(fixed_t a, fixed_t b) { return a+b; }
static fixed_t op_sub(fixed_t a, fixed_t b) { return a-b; }
static fixed_t op_or(fixed_t a, fixed_t b) { return a|b; }
static fixed_t op_and(fixed_t a, fixed_t b) { return a&b; }
static fixed_t op_lshift(fixed_t a, fixed_t b) { return a<<b; }
static fixed_t op_rshift(fixed_t a, fixed_t b) { return a>>b; }

struct {
	const char c;
	fixed_t (*v)(fixed_t,fixed_t);
} OPERATIONS[] = {
	{'*',op_mul},
	{'/',op_div},
	{'+',op_add},
	{'-',op_sub},
	{'|',op_or},
	{'&',op_and},
	{'<',op_lshift},
	{'>',op_rshift},
	{0,NULL}
};

// Returns the full word, cut at the first symbol or whitespace
/*static char *read_word(const char *line)
{
	// Part 1: You got the start of the word, now find the end.
  const char *p;
	INT32 i;
	for (p = line+1; *p; p++) {
		if (*p == ' ' || *p == '\t')
			break;
		for (i = 0; OPERATIONS[i].c; i++)
			if (*p == OPERATIONS[i].c) {
				i = -1;
				break;
			}
		if (i == -1)
			break;
	}

	// Part 2: Make a copy of the word and return it.
	{
		size_t len = (p-line);
		char *word = malloc(len+1);
		memcpy(word,line,len);
		word[len] = '\0';
		return word;
	}
}

static INT32 operation_pad(const char **word)
{ // Brings word the next operation and returns the operation number.
	INT32 i;
	for (; **word; (*word)++) {
		if (**word == ' ' || **word == '\t')
			continue;
		for (i = 0; OPERATIONS[i].c; i++)
			if (**word == OPERATIONS[i].c)
			{
				if ((**word == '<' && *(*word+1) == '<') || (**word == '>' && *(*word+1) == '>')) (*word)++; // These operations are two characters long.
				else if (**word == '<' || **word == '>') continue; // ... do not accept one character long.
				(*word)++;
				return i;
			}
		deh_warning("Unknown operation '%c'",**word);
		return -1;
	}
	return -1;
}

static void const_warning(const char *type, const char *word)
{
	deh_warning("Couldn't find %s named '%s'",type,word);
}

static fixed_t find_const(const char **rword)
{ // Finds the value of constants and returns it, bringing word to the next operation.
	INT32 i;
	fixed_t r;
	char *word = read_word(*rword);
	*rword += strlen(word);
	if ((*word >= '0' && *word <= '9') || *word == '-') { // Parse a number
		r = atoi(word);
		free(word);
		return r;
	}
	if (!*(word+1) && // Turn a single A-z symbol into numbers, like sprite frames.
	 ((*word >= 'A' && *word <= 'Z') || (*word >= 'a' && *word <= 'z'))) {
		r = R_Char2Frame(*word);
		free(word);
		return r;
	}
	if (fastncmp("MF_", word, 3)) {
		char *p = word+3;
		for (i = 0; MOBJFLAG_LIST[i]; i++)
			if (fastcmp(p, MOBJFLAG_LIST[i])) {
				free(word);
				return (1<<i);
			}

		// Not found error
		const_warning("mobj flag",word);
		free(word);
		return 0;
	}
	else if (fastncmp("MF2_", word, 4)) {
		char *p = word+4;
		for (i = 0; MOBJFLAG2_LIST[i]; i++)
			if (fastcmp(p, MOBJFLAG2_LIST[i])) {
				free(word);
				return (1<<i);
			}

		// Not found error
		const_warning("mobj flag2",word);
		free(word);
		return 0;
	}
	else if (fastncmp("MFE_", word, 4)) {
		char *p = word+4;
		for (i = 0; MOBJEFLAG_LIST[i]; i++)
			if (fastcmp(p, MOBJEFLAG_LIST[i])) {
				free(word);
				return (1<<i);
			}

		// Not found error
		const_warning("mobj eflag",word);
		free(word);
		return 0;
	}
	else if (fastncmp("PF_", word, 3)) {
		char *p = word+3;
		for (i = 0; PLAYERFLAG_LIST[i]; i++)
			if (fastcmp(p, PLAYERFLAG_LIST[i])) {
				free(word);
				return (1<<i);
			}
		if (fastcmp(p, "FULLSTASIS"))
			return PF_FULLSTASIS;

		// Not found error
		const_warning("player flag",word);
		free(word);
		return 0;
	}
	else if (fastncmp("S_",word,2)) {
		r = get_state(word);
		free(word);
		return r;
	}
	else if (fastncmp("SKINCOLOR_",word,10)) {
		r = get_skincolor(word);
		free(word);
		return r;
	}
	else if (fastncmp("MT_",word,3)) {
		r = get_mobjtype(word);
		free(word);
		return r;
	}
	else if (fastncmp("SPR_",word,4)) {
		r = get_sprite(word);
		free(word);
		return r;
	}
	else if (fastncmp("SFX_",word,4) || fastncmp("DS",word,2)) {
		r = get_sfx(word);
		free(word);
		return r;
	}
	else if (fastncmp("PW_",word,3)) {
		r = get_power(word);
		free(word);
		return r;
	}
	else if (fastncmp("MN_",word,3)) {
		r = get_menutype(word);
		free(word);
		return r;
	}
	else if (fastncmp("GT_",word,3)) {
		r = get_gametype(word);
		free(word);
		return r;
	}
	else if (fastncmp("GTR_", word, 4)) {
		char *p = word+4;
		for (i = 0; GAMETYPERULE_LIST[i]; i++)
			if (fastcmp(p, GAMETYPERULE_LIST[i])) {
				free(word);
				return (1<<i);
			}

		// Not found error
		const_warning("game type rule",word);
		free(word);
		return 0;
	}
	else if (fastncmp("TOL_", word, 4)) {
		char *p = word+4;
		for (i = 0; TYPEOFLEVEL[i].name; i++)
			if (fastcmp(p, TYPEOFLEVEL[i].name)) {
				free(word);
				return TYPEOFLEVEL[i].flag;
			}

		// Not found error
		const_warning("typeoflevel",word);
		free(word);
		return 0;
	}
	for (i = 0; INT_CONST[i].n; i++)
		if (fastcmp(word,INT_CONST[i].n)) {
			free(word);
			return INT_CONST[i].v;
		}

	if fastcmp(word,"MAXSKINCOLORS")
	{
		free(word);
		return MAXSKINCOLORS;
	}

	// Not found error.
	const_warning("constant",word);
	free(word);
	return 0;
}*/
