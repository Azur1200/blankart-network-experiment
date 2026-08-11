/// \file  k_battle.c
/// \brief SRB2Kart Battle Mode specific code

#include "k_battle.h"
#include "k_boss.h"
#include "k_kart.h"
#include "doomtype.h"
#include "doomdata.h"
#include "g_game.h"
#include "p_mobj.h"
#include "p_local.h"
#include "p_setup.h"
#include "p_slopes.h" // P_GetZAt
#include "r_main.h"
#include "r_defs.h" // MAXFFLOORS
#include "info.h"
#include "s_sound.h"
#include "m_random.h"
#include "r_sky.h" // skyflatnum
#include "k_grandprix.h" // K_CanChangeRules
#include "p_spec.h"

// Item Breaker mode enabled for this map?
dboolean itembreaker = false;

// box respawning in battle mode
INT32 nummapboxes = 0;
INT32 numgotboxes = 0;

// Item Breaker counters
UINT8 numtargets = 0; // Itemboxes busted

INT32 K_StartingBumperCount(void)
{
	if (itembreaker)
		return 1; // always 1 hit in Item Breaker

	return cv_kartbumpers.value;
}

dboolean K_IsPlayerWanted(player_t *player)
{
	UINT8 i;

	if (!(gametypes[gametype]->rules & GTR_WANTED))
		return false;

	for (i = 0; i < 4; i++)
	{
		if (battlewanted[i] == -1)
			break;
		if (player == &players[battlewanted[i]])
			return true;
	}
	return false;
}

dboolean K_IsPlayerMostWanted(player_t *player)
{
	if (mostwanted == -1)
		return false;

	if (!(gametypes[gametype]->rules & GTR_WANTEDSPB))
		return false;

	if (player == &players[mostwanted])
		return true;

	return false;
}

void K_CalculateBattleWanted(void)
{
	UINT8 numingame = 0, numwanted = 0;
	SINT8 camppos[MAXPLAYERS]; // who is the biggest camper
	UINT8 ties = 0, nextcamppos = 0;
	UINT8 i, j;

	mostwanted = -1;

	if (!(gametypes[gametype]->rules & GTR_WANTED))
	{
		memset(battlewanted, -1, sizeof (battlewanted));
		return;
	}

	wantedcalcdelay = wantedfrequency;
	memset(camppos, -1, sizeof (camppos)); // initialize

	for (i = 0; i < MAXPLAYERS; i++)
	{
		UINT8 position = 1;

		if (!playeringame[i] || players[i].spectator) // Not playing
			continue;

		if (players[i].exiting) // We're done, don't calculate.
			return;

		if (players[i].bumper <= 0) // Not alive, so don't do anything else
			continue;

		numingame++;

		for (j = 0; j < MAXPLAYERS; j++)
		{
			if (!playeringame[j] || players[j].spectator)
				continue;

			if (players[j].bumper <= 0)
				continue;

			if (j == i)
				continue;

			if (K_NumEmeralds(&players[j]) > K_NumEmeralds(&players[i]))
			{
				position++;
			}
			else if (players[j].bumper > players[i].bumper)
			{
				position++;
			}
			else if (players[j].roundscore > players[i].roundscore)
			{
				position++;
			}
			else if (players[j].wanted > players[i].wanted)
			{
				position++;
			}
		}

		position--; // Make zero based

		while (camppos[position] != -1) // Port priority!
			position++;

		camppos[position] = i;
	}

	if (numingame <= 2) // In 1v1s then there's no need for WANTED.
		numwanted = 0;
	else
		numwanted = min(4, 1 + ((numingame-2) / 4));

	for (i = 0; i < 4; i++)
	{
		if (i+1 > numwanted) // Not enough players for this slot to be wanted!
		{
			battlewanted[i] = -1;
		}
		else
		{
			// Do not add *any* more people if there's too many times that are tied with others.
			// This could theoretically happen very easily if people don't hit each other for a while after the start of a match.
			// (I will be sincerely impressed if more than 2 people tie after people start hitting each other though)

			if (camppos[nextcamppos] == -1 // Out of entries
				|| ties >= (numwanted-i)) // Already counted ties
			{
				battlewanted[i] = -1;
				continue;
			}

			if (ties < (numwanted-i))
			{
				ties = 0; // Reset
				for (j = 0; j < 2; j++)
				{
					if (camppos[nextcamppos+(j+1)] == -1) // Nothing beyond, cancel
						break;
					if (players[camppos[nextcamppos]].wanted == players[camppos[nextcamppos+(j+1)]].wanted)
						ties++;
				}
			}

			if (ties < (numwanted-i)) // Is it still low enough after counting?
			{
				battlewanted[i] = camppos[nextcamppos];

				if (!nextcamppos)
					mostwanted = battlewanted[i];

				nextcamppos++;
			}
			else
				battlewanted[i] = -1;
		}
	}
}

void K_SpawnBattlePoints(player_t *source, player_t *victim, UINT8 amount)
{
	statenum_t st;
	mobj_t *pt;

	if (!source || !source->mo)
		return;

	if (amount == 1)
		st = S_BATTLEPOINT1A;
	else if (amount == 2)
		st = S_BATTLEPOINT2A;
	else if (amount == 3)
		st = S_BATTLEPOINT3A;
	else
		return; // NO STATE!

	pt = P_SpawnMobj(source->mo->x, source->mo->y, source->mo->z, MT_BATTLEPOINT);
	P_SetTarget(&pt->target, source->mo);
	P_SetMobjState(pt, st);
	if (victim && victim->skincolor)
		pt->color = victim->skincolor;
	else
		pt->color = source->skincolor;

	if (encoremode)
		pt->renderflags ^= RF_HORIZONTALFLIP;
}

void K_CheckBumpers(void)
{
	UINT8 i;
	UINT8 numingame = 0;
	SINT8 winnernum = -1;
	UINT32 winnerscoreadd = 0, maxroundscore = 0;
	dboolean nobumpers = false;

	if (!(gametypes[gametype]->rules & GTR_BUMPERS))
		return;

	if (gameaction == ga_completed)
		return;

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator) // not even in-game
			continue;

		if (players[i].exiting) // we're already exiting! stop!
			return;

		numingame++;
		winnerscoreadd += players[i].roundscore;

		if (players[i].roundscore > maxroundscore)
		{
			maxroundscore = players[i].roundscore;
		}

		if (players[i].bumper <= 0) // if you don't have any bumpers, you're probably not a winner
		{
			nobumpers = true;
			continue;
		}
		else if (winnernum != -1) // TWO winners? that's dumb :V
			return;

		winnernum = i;
		winnerscoreadd -= players[i].roundscore;
	}

	if (K_CanChangeRules(true) == false)
	{
		if (nobumpers)
		{
			P_DoAllPlayersExit(PF_NOCONTEST, false);
		}
		return;
	}
	else if (numingame <= 1)
	{
		if (!itembreaker && cv_kartitembreaker.value)
		{
			// Reset map to turn on battle capsules
			if (server)
				D_MapChange(gamemap, gametype, encoremode, true, 0, false, false);
		}
		else
		{
			if (nobumpers)
			{
				P_DoAllPlayersExit(PF_NOCONTEST, false);
			}
		}

		return;
	}

	if (winnernum > -1 && playeringame[winnernum])
	{
		if ((players[winnernum].roundscore+winnerscoreadd) == maxroundscore)
			winnerscoreadd++; // break ties if luigi wins by doing nothing
		players[winnernum].roundscore += winnerscoreadd;
		CONS_Printf(M_GetText("%s recieved %d point%s for winning!\n"), player_names[winnernum], winnerscoreadd, (winnerscoreadd == 1 ? "" : "s"));
	}

	for (i = 0; i < MAXPLAYERS; i++) // This can't go in the earlier loop because winning adds points
		K_KartUpdatePosition(&players[i]);

	P_DoAllPlayersExit(0, false);
}

UINT8 K_NumEmeralds(player_t *player)
{
	UINT8 i;
	UINT8 num = 0;

	for (i = 0; i < 14; i++)
	{
		UINT32 emeraldFlag = (1 << i);

		if (player->emeralds & emeraldFlag)
		{
			num++;
		}
	}

	return num;
}

void K_RunPaperItemSpawners(void)
{
	tic_t interval = 8*TICRATE;

	thinker_t *th;
	mobj_t *mo;

	UINT8 pcount = 0;
	INT16 i;

	if (itembreaker || bossinfo.boss)
	{
		// Gametype uses paper items, but this specific expression doesn't
		return;
	}

	if (leveltime < starttime)
	{
		// Round hasn't started yet!
		return;
	}

	if (((leveltime - starttime) % interval) != 0)
	{
		return;
	}

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator)
		{
			continue;
		}

		if ((players[i].exiting > 0 || (players[i].pflags & PF_ELIMINATED))
			|| ((gametypes[gametype]->rules & GTR_BUMPERS) && players[i].bumper <= 0))
		{
			continue;
		}

		pcount++;
	}

	{
		if (pcount > 0)
		{
#define MAXITEM 64
			mobj_t *spotList[MAXITEM];
			UINT8 spotMap[MAXITEM];
			UINT8 spotCount = 0, spotBackup = 0;

			INT16 starti = 0;

			for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
			{
				if (th->function == (actionf_p1)P_RemoveThinkerDelayed)
					continue;

				mo = (mobj_t *)th;

				if (mo->type != MT_PAPERITEMSPOT)
					continue;

				if (spotCount >= MAXITEM)
					continue;

				spotList[spotCount] = mo;
				spotMap[spotCount] = spotCount;
				spotCount++;
			}

			if (spotCount <= 0)
			{
				return;
			}

			//CONS_Printf("leveltime = %d ", leveltime);

			spotBackup = spotCount;
			for (i = starti; i < pcount; i++)
			{
				UINT8 r = 0, key = 0;
				mobj_t *drop = NULL;
				SINT8 flip = 1;

				if (spotCount == 0)
				{
					// all are accessible again
					spotCount = spotBackup;
				}

				if (spotCount == 1)
				{
					key = 0;
				}
				else
				{
					key = P_RandomKey(PR_ITEM_SPAWNER, spotCount);
				}

				r = spotMap[key];

				//CONS_Printf("[%d %d %d] ", i, key, r);

				flip = P_MobjFlip(spotList[r]);

				{

					drop = K_CreatePaperItem(
						spotList[r]->x, spotList[r]->y, spotList[r]->z + (128 * mapobjectscale * flip),
						FixedAngle(P_RandomRange(PR_ITEM_SPAWNER, 0, 359) * FRACUNIT), flip,
						0, 0
					);
				}

				K_FlipFromObject(drop, spotList[r]);

				spotCount--;
				if (key != spotCount)
				{
					// So the core theory of what's going on is that we keep every
					// available option at the front of the array, so we don't have
					// to skip over any gaps or do recursion to avoid doubles.
					// But because spotCount can be reset in the case of a low
					// quanitity of item spawnpoints in a map, we still need every
					// entry in the array, even outside of the "visible" range.
					// A series of swaps allows us to adhere to both constraints.
					// -toast 22/03/22 (semipalindromic!)
					spotMap[key] = spotMap[spotCount];
					spotMap[spotCount] = r; // was set to spotMap[key] previously
				}
			}
			//CONS_Printf("\n");
		}
	}
}

void K_SpawnPlayerBattleBumpers(player_t *p)
{
	if (!p->mo || p->bumper <= 0)
		return;

	{
		INT32 i;
		angle_t diff = FixedAngle(360*FRACUNIT/p->bumper);
		angle_t newangle = p->mo->angle;
		mobj_t *bump;

		for (i = 0; i < p->bumper; i++)
		{
			bump = P_SpawnMobjFromMobj(p->mo,
				P_ReturnThrustX(p->mo, newangle + ANGLE_180, 64*FRACUNIT),
				P_ReturnThrustY(p->mo, newangle + ANGLE_180, 64*FRACUNIT),
				0, MT_BATTLEBUMPER);
			bump->threshold = i;
			P_SetTarget(&bump->target, p->mo);
			bump->angle = newangle;
			bump->color = p->mo->color;
			if (p->mo->renderflags & RF_DONTDRAW)
				bump->renderflags |= RF_DONTDRAW;
			else
				bump->renderflags &= ~RF_DONTDRAW;
			newangle += diff;
		}
	}
}

void K_BattleInit(dboolean singleplayercontext)
{
	if ((gametypes[gametype]->rules & GTR_ITEMBREAKER) && singleplayercontext && !itembreaker && !bossinfo.boss)
	{
		if (!(K_CanChangeRules(true) && !cv_kartitembreaker.value))
			itembreaker = true;
		
		for (int i = 0; i < MAXPLAYERS; i++)
		{
			if (players[i].bot == true)
				P_SetPlayerSpectator(i);
		}
	}

	if (gametypes[gametype]->rules & GTR_BUMPERS)
	{
		INT32 maxbumpers = K_StartingBumperCount();
		UINT8 i;

		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i] || players[i].spectator)
				continue;
			players[i].bumper = maxbumpers;
			K_SpawnPlayerBattleBumpers(players+i);
		}
	}
}

// Handle respawning the battle boxes.
void K_RespawnBattleBoxes(void)
{
	if (!(gametypes[gametype]->rules & GTR_BATTLEBOXES))
		return;

	if (itembreaker)
		return;

	if (numgotboxes < (4*nummapboxes/5))
		return;

	thinker_t *th;

	for (th = thlist[THINK_MOBJ].next; th != &thlist[THINK_MOBJ]; th = th->next)
	{
		mobj_t *box;
		mobj_t *newmobj;

		if (th->function == (actionf_p1)P_RemoveThinkerDelayed)
			continue;

		box = (mobj_t *)th;

		if (box->type != MT_RANDOMITEM || box->threshold != 68 || box->fuse) // only popped items
			continue;

		// Respawn from mapthing if you have one!
		if (box->spawnpoint)
		{
			P_SpawnMapThing(box->spawnpoint);
			newmobj = box->spawnpoint->mobj; // this is set to the new mobj in P_SpawnMapThing
			P_SpawnMobj(box->spawnpoint->mobj->x, box->spawnpoint->mobj->y, box->spawnpoint->mobj->z, MT_EXPLODE); // poof into existance
		}
		else
		{
			newmobj = P_SpawnMobj(box->x, box->y, box->z, box->type);
			P_SpawnMobj(newmobj->x, newmobj->y, newmobj->z, MT_EXPLODE); // poof into existance
		}

		// Transfer flags2 (strongbox, objectflip)
		newmobj->flags2 = box->flags2;
		P_RemoveMobj(box); // make sure they disappear
		numgotboxes--; // you've restored a box, remove it from the count
	}

	if (numgotboxes < 0)
		numgotboxes = 0;
}
