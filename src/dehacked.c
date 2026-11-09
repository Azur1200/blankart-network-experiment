// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  dehacked.c
/// \brief Load dehacked file and change tables and text

#include "doomdef.h"
#include "m_cond.h"
#include "deh_soc.h"
#include "deh_lua.h"
#include "deh_tables.h"
#include "k_hud.h"
#include "d_main.h"

dboolean gamedataadded = false;
dboolean titlechanged = false;
dboolean introchanged = false;

static int dbg_line;
static int32_t deh_num_warning = 0;

FUNCPRINTF void deh_warning(const char *first, ...)
{
	va_list argptr;
	char *buf = Z_Malloc(1000, PU_STATIC, NULL);

	va_start(argptr, first);
	vsnprintf(buf, 1000, first, argptr); // sizeof only returned 4 here. it didn't like that pointer.
	va_end(argptr);

	if (dbg_line == -1) // Not in a SOC, line number unknown.
		CONS_Alert(CONS_WARNING, "%s\n", buf);
	else
		CONS_Alert(CONS_WARNING, "Line %u: %s\n", dbg_line, buf);

	deh_num_warning++;

	Z_Free(buf);
}

void deh_strlcpy(char *dst, const char *src, size_t size, const char *warntext)
{
	size_t len = strlen(src)+1; // Used to determine if truncation has been done
	if (len > size)
		deh_warning("%s exceeds max length of %s", warntext, sizeu1(size-1));
	strlcpy(dst, src, size);
}

int freeslotusage[2][2] = {{0, 0}, {0, 0}}; // [S_, MT_][max, previous .wad's max]
void DEH_UpdateMaxFreeslots(void)
{
	freeslotusage[0][1] = freeslotusage[0][0];
	freeslotusage[1][1] = freeslotusage[1][0];
}

ATTRINLINE static FUNCINLINE unsigned char myfget_color(MYFILE *f)
{
	char c = *f->curpos++;
	if (c == '^') // oh, nevermind then.
		return '^';

	if (c >= '0' && c <= '9')
		return 0x80+(c-'0');

	c = tolower(c);

	if (c >= 'a' && c <= 'f')
		return 0x80+10+(c-'a');

	return 0x80; // Unhandled -- default to no color
}

ATTRINLINE static FUNCINLINE char myfget_hex(MYFILE *f)
{
	char c = *f->curpos++, endchr = 0;
	if (c == '\\') // oh, nevermind then.
		return '\\';

	if (c >= '0' && c <= '9')
		endchr += (c-'0') << 4;
	else if (c >= 'A' && c <= 'F')
		endchr += ((c-'A') + 10) << 4;
	else if (c >= 'a' && c <= 'f')
		endchr += ((c-'a') + 10) << 4;
	else // invalid. stop and return a question mark.
		return '?';

	c = *f->curpos++;
	if (c >= '0' && c <= '9')
		endchr += (c-'0');
	else if (c >= 'A' && c <= 'F')
		endchr += ((c-'A') + 10);
	else if (c >= 'a' && c <= 'f')
		endchr += ((c-'a') + 10);
	else // invalid. stop and return a question mark.
		return '?';

	return endchr;
}

char *myfgets(char *buf, size_t bufsize, MYFILE *f)
{
	size_t i = 0;
	if (myfeof(f))
		return NULL;
	// we need one byte for a null terminated string
	bufsize--;
	while (i < bufsize && !myfeof(f))
	{
		char c = *f->curpos++;
		if (c == '^')
			buf[i++] = myfget_color(f);
		else if (c == '\\')
			buf[i++] = myfget_hex(f);
		else if (c != '\r')
			buf[i++] = c;
		if (c == '\n')
			break;
	}
	buf[i] = '\0';

	dbg_line++;
	return buf;
}

char *myhashfgets(char *buf, size_t bufsize, MYFILE *f)
{
	size_t i = 0;
	if (myfeof(f))
		return NULL;
	// we need one byte for a null terminated string
	bufsize--;
	while (i < bufsize && !myfeof(f))
	{
		char c = *f->curpos++;
		if (c == '^')
			buf[i++] = myfget_color(f);
		else if (c == '\\')
			buf[i++] = myfget_hex(f);
		else if (c != '\r')
			buf[i++] = c;
		if (c == '\n') // Ensure debug line is right...
			dbg_line++;
		if (c == '#')
		{
			if (i > 0) // don't let i wrap past 0
				i--; // don't include hash char in string
			break;
		}
	}
	if (buf[i] != '#') // don't include hash char in string
		i++;
	buf[i] = '\0';

	return buf;
}

// Used when you do something invalid like read a bad item number
// to prevent extra unnecessary errors
static void ignorelines(MYFILE *f)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;
		}
	} while (!myfeof(f));
	Z_Free(s);
}

static void ignoremenulines(MYFILE *f)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
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

			ignorelines(f);
		}
	} while (!myfeof(f));
	Z_Free(s);
}

void DEH_Link(const char *name, dehinfo_t *info, strbuf_t **buf)
{
	info->namehash = HASH32(name, strlen(name));
	info->nameofs = strbuf_append(buf, name);
}

int32_t DEH_ReadFreeslot(const char *type, const char *word, int32_t *out)
{
	size_t i;

	// TODO: Check for existing freeslot mobjs/states/etc. and make errors.
	// TODO: Name too long (truncated) warnings.
	if (fastcmp(type, "SFX"))
	{
		CONS_Printf("Sound sfx_%s allocated.\n", word);
		i = S_AddSoundFx(word, false);
		if (i != sfx_None)
		{
			*out = i;
			return 0;
		}
		CONS_Alert(CONS_WARNING, "Ran out of free SFX slots!\n");
		return -1;
	}
	else if (fastcmp(type, "SPR"))
	{
		for (i = SPR_FIRSTFREESLOT; i <= SPR_LASTFREESLOT; i++)
		{
			if (used_spr[(i-SPR_FIRSTFREESLOT)/8] & (1<<(i%8)))
				continue; // Already allocated, next.
			// Found a free slot!
			CONS_Printf("Sprite SPR_%s allocated.\n",word);
			strncpy(sprnames[i],word,4);
			used_spr[(i-SPR_FIRSTFREESLOT)/8] |= 1<<(i%8); // Okay, this sprite slot has been named now.
			// Lua needs to update the value in _G if it exists
			LUA_UpdateSprName(word, i);
			*out = i;
			return 0;
		}
		CONS_Alert(CONS_WARNING, "Ran out of free sprite slots!\n");
		return -1;
	}
	else if (fastcmp(type, "S"))
	{
		*out = DEH_FindState(word);
		if (*out != NUMSTATES)
			return 0; // already allocated

		if (numstates == NUMSTATES) {
			CONS_Alert(CONS_WARNING, "Ran out of free State slots!\n");
			return -1;
		}
		CONS_Printf("State S_%s allocated.\n",word);
		DEH_Link(word, &states[numstates].info, &statenames);
		*out = numstates++;
		freeslotusage[0][0]++;
		return 0;
	}
	else if (fastcmp(type, "MT"))
	{
		*out = DEH_FindMobjtype(word);
		if (*out != NUMMOBJTYPES)
			return 0; // already allocated

		if (nummobjtypes == NUMMOBJTYPES) {
			CONS_Alert(CONS_WARNING, "Ran out of free MobjType slots!\n");
			return -1;
		}
		CONS_Printf("MobjType MT_%s allocated.\n",word);
		DEH_Link(word, &mobjinfo[nummobjtypes].info, &mobjnames);
		*out = nummobjtypes++;
		freeslotusage[1][0]++;
		return 0;
	}
	else if (fastcmp(type, "SKINCOLOR"))
	{
		*out = DEH_FindSkincolor(word);
		if (*out != MAXSKINCOLORS)
		{
			if (*out < FIRSTSUPERCOLOR)
				CONS_Alert(CONS_WARNING, "Attempted to freeslot over standard color SKINCOLOR_%s, ignoring. This might be a color conflict!\n", DEH_SkincolorName(*out ));
			return 0; // already allocated
		}

		if (numskincolors == MAXSKINCOLORS) {
			CONS_Alert(CONS_WARNING, "Ran out of free skincolor slots!\n");
			return -1;
		}
		CONS_Printf("Skincolor SKINCOLOR_%s allocated.\n",word);
		DEH_Link(word, &skincolors[numskincolors].info, &skincolornames);
		M_AddMenuColor(numskincolors++);
		K_ReloadHUDColorCvar();
		*out = numskincolors;
		return 0;
	}
	else if (fastcmp(type, "SPR2"))
	{
		// Search if we already have an SPR2 by that name...
		for (i = SPR2_FIRSTFREESLOT; i < free_spr2; i++)
			if (memcmp(spr2names[i],word,4) == 0)
				break;
		// We don't, so allocate a new one.
		if (i >= free_spr2) {
			if (free_spr2 < NUMPLAYERSPRITES)
			{
				CONS_Printf("Sprite SPR2_%s allocated.\n",word);
				strncpy(spr2names[free_spr2],word,4);
				spr2defaults[free_spr2] = 0;
				*out = free_spr2;
				spr2names[free_spr2++][4] = 0;
				return 0;
			} else {
				CONS_Alert(CONS_WARNING, "Ran out of free SPR2 slots!\n");
				return -1;
			}
		}
	}
	else if (fastcmp(type, "TOL"))
	{
		// Search if we already have a typeoflevel by that name...
		for (i = 0; TYPEOFLEVEL[i].name; i++)
			if (fastcmp(word, TYPEOFLEVEL[i].name))
				break;

		// We don't, so allocate a new one.
		if (TYPEOFLEVEL[i].name == NULL) {
			if (lastcustomtol == (uint32_t)MAXTOL) // Unless you have way too many, since they're flags.
			{
				CONS_Alert(CONS_WARNING, "Ran out of free typeoflevel slots!\n");
				return -1;
			} else {
				CONS_Printf("TypeOfLevel TOL_%s allocated.\n",word);
				G_AddTOL(lastcustomtol, word);
				*out = lastcustomtol;
				lastcustomtol <<= 1;
				return 0;
			}
		}
	}
	else if (fastcmp(type, "PRECIP"))
	{
		// Search if we already have a PRECIP by that name...
		for (i = PRECIP_FIRSTFREESLOT; i < precip_freeslot; i++)
			if (fastcmp(word, precipprops[i].name))
				break;

		// We don't, so allocate a new one.
		if (i >= precip_freeslot) {
			if (precip_freeslot < MAXPRECIP)
			{
				CONS_Printf("Weather PRECIP_%s allocated.\n",word);
				precipprops[i].name = Z_StrDup(word);
				*out = precip_freeslot;
				precip_freeslot++;
				return 0;
			} else {
				CONS_Alert(CONS_WARNING, "Ran out of free PRECIP slots!\n");
				return -1;
			}
		}
	}
	else if (fastcmp(type, "MN"))
	{
		*out = DEH_FindMenutype(word);
		if (*out != MAXMENUTYPES)
			return 0; // already allocated

		if (nummenutypes == MAXMENUTYPES) {
			CONS_Alert(CONS_WARNING, "Ran out of free menu slots!\n");
			return -1;
		}
		CONS_Printf("Menu MN_%s allocated.\n",word);
		DEH_Link(word, &menudefs[nummenutypes].info, &menunames);
		*out = nummenutypes++;
		return 0;
	}
	else if (fastcmp(type, "KITEM"))
	{
		*out = DEH_FindKartItem(word);
		if (*out != MAXKARTITEMS)
			return 0; // already allocated

		if (numkartitems == MAXKARTITEMS) {
			CONS_Alert(CONS_WARNING, "Ran out of free kartitem slots!\n");
			return -1;
		}
		CONS_Printf("Kartitem KITEM_%s allocated.\n",word);
		DEH_Link(word, &kartitems[numkartitems].info, &kartitemnames);
		K_RegisterItem(numkartitems);
		*out = numkartitems++;
		return 0;
	}

	return -2;
}

static void DEH_LoadDehackedFile(MYFILE *f, dboolean mainfile)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char textline[MAXLINELEN];
	char *word;
	char *word2;
	char *word3;
	int32_t i;

	deh_num_warning = 0;

	gamedataadded = titlechanged = introchanged = false;

	// it doesn't test the version of SRB2 and version of dehacked file
	dbg_line = -1; // start at -1 so the first line is 0.
	while (!myfeof(f))
	{
		myfgets(s, MAXLINELEN, f);
		memcpy(textline, s, MAXLINELEN);
		if (s[0] == '\n' || s[0] == '#')
			continue;

		if (NULL != (word = strtok(s, " "))) {
			strupr(word);
			if (word[strlen(word)-1] == '\n')
				word[strlen(word)-1] = '\0';
		}
		if (word)
		{
			if (fastcmp(word, "FREESLOT"))
			{
				// This is not a major mod.
				readfreeslots(f);
				continue;
			}
			else if (fastcmp(word, "MAINCFG"))
			{
				G_SetGameModified(multiplayer, true);
				readmaincfg(f);
				continue;
			}
			else if (fastcmp(word, "WIPES"))
			{
				// This is not a major mod.
				readwipes(f);
				continue;
			}
			//
			// SRB2KART
			//
			else if (fastcmp(word, "FOLLOWER"))
			{
				// This is not a major mod.
				readfollower(f);
				continue;
			}

			word2 = strtok(NULL, " ");
			if (word2) {
				strupr(word2);
				if (word2[strlen(word2) - 1] == '\n')
					word2[strlen(word2) - 1] = '\0';
				i = atoi(word2);
			}
			else
				i = 0;

			word3 = strtok(NULL, " : ");
			if (word3) {
				strupr(word3);
				if (word3[strlen(word3) - 1] == '\n')
					word3[strlen(word3) - 1] = '\0';
			}

			if (fastcmp(word, "EMBLEM"))
			{
				if (!mainfile && !gamedataadded)
				{
					deh_warning("You must define a custom gamedata to use \"%s\"", word);
					ignorelines(f);
				}
				else
				{
					if (!word2)
						i = numemblems + 1;

					if (i > 0 && i <= MAXEMBLEMS)
					{
						if (numemblems < i)
							numemblems = i;
						reademblemdata(f, i);
					}
					else
					{
						deh_warning("Emblem number %d out of range (1 - %d)", i, MAXEMBLEMS);
						ignorelines(f);
					}
				}
				continue;
			}
			else if (fastcmp(word, "EXTRAEMBLEM"))
			{
				if (!mainfile && !gamedataadded)
				{
					deh_warning("You must define a custom gamedata to use \"%s\"", word);
					ignorelines(f);
				}
				else
				{
					if (!word2)
						i = numextraemblems + 1;

					if (i > 0 && i <= MAXEXTRAEMBLEMS)
					{
						if (numextraemblems < i)
							numextraemblems = i;
						readextraemblemdata(f, i);
					}
					else
					{
						deh_warning("Extra emblem number %d out of range (1 - %d)", i, MAXEXTRAEMBLEMS);
						ignorelines(f);
					}
				}
				continue;
			}
			if (word2)
			{
				if (fastcmp(word, "THING") || fastcmp(word, "MOBJ") || fastcmp(word, "OBJECT"))
				{
					if (i == 0 && word2[0] != '0') // If word2 isn't a number
						i = get_mobjtype(word2); // find a thing by name
					if (i < NUMMOBJTYPES && i >= 0)
					{
						if (!mainfile && i < (MT_FIRSTFREESLOT+freeslotusage[1][1]))
						{
							G_SetGameModified(multiplayer, true); // Only a major mod if editing stuff that isn't your own!
						}

						readthing(f, i);
					}
					else
					{
						deh_warning("Thing %d out of range (0 - %d)", i, NUMMOBJTYPES-1);
						ignorelines(f);
					}
				}
				else if (fastcmp(word, "SKINCOLOR") || fastcmp(word, "COLOR"))
				{
					if (i == 0 && word2[0] != '0') // If word2 isn't a number
						i = get_skincolor(word2); // find a skincolor by name
					if (i < numskincolors && i >= 0)
						readskincolor(f, i, mainfile);
					else
					{
						deh_warning("Skincolor %d out of range (0 - %d)", i, numskincolors-1);
						ignorelines(f);
					}
				}
				else if (fastcmp(word, "SPRITE2"))
				{
					if (i == 0 && word2[0] != '0') // If word2 isn't a number
						i = get_sprite2(word2); // find a sprite by name
					if (i < (int32_t)free_spr2 && i >= (int32_t)SPR2_FIRSTFREESLOT)
						readsprite2(f, i);
					else
					{
						deh_warning("Sprite2 number %d out of range (%d - %d)", i, SPR2_FIRSTFREESLOT, free_spr2-1);
						ignorelines(f);
					}
				}
				else if (fastcmp(word, "LEVEL"))
				{
					size_t len = strlen(word2);
					if (len <= MAXMAPLUMPNAME-1)
					{
						readlevelheader(f, word2);
					}
					else
					{
						deh_warning("Map header's lumpname %s is too long (%s characters VS %d max)", word2, sizeu1(len), (MAXMAPLUMPNAME-1));
						ignorelines(f);
					}
				}
				else if (fastcmp(word, "GAMETYPE"))
				{
					// Get the gametype name from textline
					// instead of word2, so that gametype names
					// aren't allcaps
					int32_t c;
					for (c = 0; c < MAXLINELEN; c++)
					{
						if (textline[c] == '\0')
							break;
						if (textline[c] == ' ')
						{
							char *gtname = (textline+c+1);
							if (gtname)
							{
								// remove funny characters
								int32_t j;
								for (j = 0; j < (MAXLINELEN - c); j++)
								{
									if (gtname[j] == '\0')
										break;
									if (gtname[j] < 32)
										gtname[j] = '\0';
								}
								readgametype(f, gtname);
							}
							break;
						}
					}
				}
				else if (fastcmp(word, "CUTSCENE"))
				{
					if (i > 0 && i < 129)
						readcutscene(f, i - 1);
					else
					{
						deh_warning("Cutscene number %d out of range (1 - 128)", i);
						ignorelines(f);
					}
				}
				else if (fastcmp(word, "PROMPT"))
				{
					if (i > 0 && i < MAX_PROMPTS)
						readtextprompt(f, i - 1);
					else
					{
						deh_warning("Prompt number %d out of range (1 - %d)", i, MAX_PROMPTS);
						ignorelines(f);
					}
				}
				else if (fastcmp(word, "FRAME") || fastcmp(word, "STATE"))
				{
					if (i == 0 && word2[0] != '0') // If word2 isn't a number
						i = get_state(word2); // find a state by name
					if (i < NUMSTATES && i >= 0)
					{
						if (!mainfile && i < (S_FIRSTFREESLOT+freeslotusage[0][1]))
						{
							G_SetGameModified(multiplayer, true); // Only a major mod if editing stuff that isn't your own!
						}

						readframe(f, i);
					}
					else
					{
						deh_warning("Frame %d out of range (0 - %d)", i, NUMSTATES-1);
						ignorelines(f);
					}
				}
				else if (fastcmp(word, "SOUND"))
				{
					if (i == 0 && word2[0] != '0') // If word2 isn't a number
						i = get_sfx(word2); // find a sound by name
					if (i < NUMSFX && i >= 0)
						readsound(f, i);
					else
					{
						deh_warning("Sound %d out of range (0 - %d)", i, NUMSFX-1);
						ignorelines(f);
					}
				}
				else if (fastcmp(word, "MENU"))
				{
					if (dedicated)
					{
						ignoremenulines(f);
						continue; // dedis don't need menus, silly!
					}

#ifndef HAVE_DISCORDRPC
					{
						const char *testname = word2;
						if (fastncmp("MN_", testname, 3))
							testname += 3;
						if (fastcmp(testname, "OP_DISCORD") || fastcmp(testname, "MISC_DISCORDREQUESTS"))
						{
							ignoremenulines(f);
							continue;
						}
					}
#endif
					if (i == 0 && word2[0] != '0') // If word2 isn't a number
						i = get_menutype(word2); // find a huditem by name
					if (i >= 1 && i < nummenutypes)
						readmenu(f, i);
					else
					{
						// zero-based, but let's start at 1
						deh_warning("Menu number %d out of range (1 - %d)", i, nummenutypes-1);
						ignorelines(f);
					}
				}
				else if (fastcmp(word, "MENUPANEL"))
				{
					if (dedicated)
					{
						ignoremenulines(f);
						continue; // dedis don't need menus, silly!
					}

					menupaneldef_t *pd = DEH_FindMenuPanel(word2);
					if (pd == NULL)
						pd = M_AllocateMenuPanelDef(word2);
					readmenupanel(f, pd);
				}
				else if (fastcmp(word, "MENUITEMSTYLE"))
				{
					if (dedicated)
					{
						ignoremenulines(f);
						continue; // dedis don't need menus, silly!
					}

					menuitemstyle_t *style = DEH_FindMenuItemStyle(word2);
					if (style == NULL)
						style = M_AllocateMenuItemStyle(word2, word3 != NULL ? DEH_FindMenuItemStyle(word3) : NULL);
					readmenuitemstyle(f, style);
				}
				else if (fastcmp(word, "UNLOCKABLE"))
				{
					if (!mainfile && !gamedataadded)
					{
						deh_warning("You must define a custom gamedata to use \"%s\"", word);
						ignorelines(f);
					}
					else if (i > 0 && i <= MAXUNLOCKABLES)
						readunlockable(f, i - 1);
					else
					{
						deh_warning("Unlockable number %d out of range (1 - %d)", i, MAXUNLOCKABLES);
						ignorelines(f);
					}
				}
				else if (fastcmp(word, "CONDITIONSET"))
				{
					if (!mainfile && !gamedataadded)
					{
						deh_warning("You must define a custom gamedata to use \"%s\"", word);
						ignorelines(f);
					}
					else if (i > 0 && i <= MAXCONDITIONSETS)
						readconditionset(f, (uint8_t)i);
					else
					{
						deh_warning("Condition set number %d out of range (1 - %d)", i, MAXCONDITIONSETS);
						ignorelines(f);
					}
				}
				//
				// SRB2KART
				//
				else if (fastcmp(word, "CUP"))
				{
					size_t len = strlen(word2);
					if (len <= MAXCUPNAME-1)
					{
						cupheader_t *cup = kartcupheaders;
						cupheader_t *prev = NULL;
						uint32_t hash = FNV1a_QuickCaseHash(word2, MAXCUPNAME);

						while (cup)
						{
							if (!mainfile && hash == cup->namehash && fastcmp(cup->name, word2))
							{
								// Only a major mod if editing stuff that isn't your own!
								G_SetGameModified(multiplayer, true);
								break;
							}

							prev = cup;
							cup = cup->next;
						}

						// Nothing found, add to the end.
						if (!cup)
						{
							cup = Z_Calloc(sizeof (cupheader_t), PU_STATIC, NULL);
							cup->id = numkartcupheaders;
							cup->monitor = 1;
							deh_strlcpy(cup->name, word2,
								sizeof(cup->name), va("Cup header %s: name", word2));
							cup->namehash = hash;
							cup->wadnum = numwadfiles - 1;
							if (prev != NULL)
								prev->next = cup;
							if (kartcupheaders == NULL)
								kartcupheaders = cup;
							numkartcupheaders++;
							CONS_Printf("Added cup %d ('%s')\n", cup->id, cup->name);
						}

						readcupheader(f, cup);

					}
					else
					{
						deh_warning("Cup header's name %s is too long (%s characters VS %d max)", word2, sizeu1(len), (MAXCUPNAME-1));
						ignorelines(f);
					}
				}
				else if (fastcmp(word, "KARTITEM"))
				{
					if (i == 0 && word2[0] != '0') // If word2 isn't a number
						i = get_kartitem(word2); // find a kartitem by name
					if (i >= (mainfile ? 0 : 1) && i < numkartitems)
						readkartitem(f, &kartitems[i]);
					else
					{
						// zero-based, but let's start at 1
						deh_warning("KartItem number %d out of range (1 - %d)", i, numkartitems-1);
						ignorelines(f);
					}
				}
				else if (fastncmp(word, "KARTRESULT", 10))
				{
					dboolean alternate = fastcmp(word, "KARTRESULTALT");
					kartresult_t *result = K_GetKartResultAlt(word2, alternate);
					if (result == NULL)
						result = K_RegisterResult(word2, alternate);
					readkartresult(f, result);
				}
				else if (fastcmp(word, "WEATHER") || fastcmp(word, "PRECIP") || fastcmp(word, "PRECIPITATION"))
				{
					if (i == 0 && word2[0] != '0') // If word2 isn't a number
						i = get_precip(word2); // find a weather type by name
					if (i < MAXPRECIP && i > 0)
						readweather(f, i);
					else
					{
						deh_warning("Weather number %d out of range (1 - %d)", i, MAXPRECIP-1);
						ignorelines(f);
					}
				}
				else if (fastcmp(word, "SRB2KART"))
				{
					if (isdigit(word2[0]))
					{
						i = atoi(word2);
						if (i != PATCHVERSION)
						{
							deh_warning(
									"Patch is for SRB2Kart version %d, "
									"only version %d is supported",
									i,
									PATCHVERSION
							);
						}
					}
					else
					{
						deh_warning(
								"SRB2Kart version definition has incorrect format, "
								"use \"SRB2KART %d\"",
								PATCHVERSION
						);
					}
				}
				else if (fastcmp(word, "RINGRACERS"))
				{
					deh_warning("Patch is only compatible with Ring Racers.");
				}
				else if (fastcmp(word, "SRB2"))
				{
					deh_warning("Patch is only compatible with base SRB2.");
				}
				// Clear all data in certain locations (mostly for unlocks)
				// Unless you REALLY want to piss people off,
				// define a custom gamedata /before/ doing this!!
				// (then again, modifiedgame will prevent game data saving anyway)
				else if (fastcmp(word, "CLEAR"))
				{
					dboolean clearall = (fastcmp(word2, "ALL"));

					if (!mainfile && !gamedataadded)
					{
						deh_warning("You must define a custom gamedata to use \"%s\"", word);
						continue;
					}

					if (clearall || fastcmp(word2, "UNLOCKABLES"))
					{
						clear_unlockables();
					}

					if (clearall || fastcmp(word2, "EMBLEMS"))
					{
						clear_emblems();
					}

					if (clearall || fastcmp(word2, "EXTRAEMBLEMS"))
					{
						memset(&extraemblems, 0, sizeof(extraemblems));
						numextraemblems = 0;
					}

					if (clearall || fastcmp(word2, "CONDITIONSETS"))
						clear_conditionsets();

					if (clearall || fastcmp(word2, "LEVELS"))
						clear_levels();
				}
				else
					deh_warning("Unknown word: %s", word);
			}
			else
				deh_warning("missing argument for '%s'", word);
		}
		else
			deh_warning("No word in this line: %s", s);
	} // end while

	if (gamedataadded)
		G_LoadGameData();

	if (gamestate == GS_MENU || gamestate == GS_TITLESCREEN)
	{
		if (introchanged)
		{
			M_ClearMenus(true);
			I_UpdateMouseGrab();
			COM_BufAddText("playintro");
		}
		else if (titlechanged)
		{
			M_ClearMenus(true);
			I_UpdateMouseGrab();
			//COM_BufAddText("exitgame"); // Command_ExitGame_f() but delayed
			D_StartTitle();
		}
	}

	dbg_line = -1;
	if (deh_num_warning)
	{
		CONS_Printf(M_GetText("%d warning%s in the SOC lump\n"), deh_num_warning, deh_num_warning == 1 ? "" : "s");
		if (devparm || (numwadfiles > 1 && numwadfiles < NUMMAINWADS)) {
			I_Error("%s%s",va(M_GetText("%d warning%s in the SOC lump\n"), deh_num_warning, deh_num_warning == 1 ? "" : "s"), M_GetText("See log.txt for details.\n"));
			//while (!I_GetKey())
				//I_OsPolling();
		}
	}

	Z_Free(s);
}

// read dehacked lump in a wad (there is special trick for for deh
// file that are converted to wad in w_wad.c)
void DEH_LoadDehackedLumpPwad(uint16_t wad, uint16_t lump, dboolean mainfile)
{
	MYFILE f;
	f.wad = wad;
	f.size = W_LumpLengthPwad(wad, lump);
	f.data = Z_Malloc(f.size + 1, PU_STATIC, NULL);
	W_ReadLumpPwad(wad, lump, f.data);
	f.curpos = f.data;
	f.data[f.size] = 0;
	DEH_LoadDehackedFile(&f, mainfile);
	Z_Free(f.data);
}

void DEH_LoadDehackedLump(lumpnum_t lumpnum)
{
	DEH_LoadDehackedLumpPwad(WADFILENUM(lumpnum),LUMPNUM(lumpnum), false);
}
