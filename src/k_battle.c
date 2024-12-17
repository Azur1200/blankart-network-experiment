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
boolean itembreaker = false;

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

boolean K_IsPlayerWanted(player_t *player)
{
	UINT8 i = 0, nump = 0, numfirst = 0;
	for (; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator)
			continue;
		nump++;
		if (players[i].position > 1)
			continue;
		numfirst++;
	}
	return ((numfirst < nump) && !player->spectator && (player->position == 1));
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
	boolean nobumpers = false;

	if (!(gametyperules & GTR_BUMPERS))
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

	if (bossinfo.boss)
	{
		if (nobumpers)
		{
			for (i = 0; i < MAXPLAYERS; i++)
			{
				players[i].pflags |= PF_NOCONTEST;
				P_DoPlayerExit(&players[i]);
			}
		}
		return;
	}
	else if (numingame <= 1)
	{
		if (!itembreaker)
		{
			// Reset map to turn on battle capsules
			D_MapChange(gamemap, gametype, encoremode, true, 0, false, false);
		}
		else
		{
			if (nobumpers)
			{
				for (i = 0; i < MAXPLAYERS; i++)
				{
					players[i].pflags |= PF_NOCONTEST;
					P_DoPlayerExit(&players[i]);
				}
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

	for (i = 0; i < MAXPLAYERS; i++) // and it can't be merged with this loop because it needs to be all updated before exiting... multi-loops suck...
	{
		if (!playeringame[i])
			continue;
		if (players[i].spectator)
			continue;
		P_DoPlayerExit(&players[i]);
	}
}

void K_CheckEmeralds(player_t *player)
{
	UINT8 i;

	if (!ALLCHAOSEMERALDS(player->emeralds))
	{
		return;
	}

	player->roundscore++; // lol

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator)
		{
			continue;
		}

		if (&players[i] == player)
		{
			continue;
		}

		players[i].bumper = 0;
	}

	K_CheckBumpers();
}

mobj_t *K_SpawnChaosEmerald(fixed_t x, fixed_t y, fixed_t z, angle_t angle, SINT8 flip, UINT32 emeraldType)
{
	boolean validEmerald = true;
	mobj_t *emerald = P_SpawnMobj(x, y, z, MT_EMERALD);
	mobj_t *overlay;

	P_Thrust(emerald,
		FixedAngle(P_RandomFixed() * 180) + angle,
		24 * mapobjectscale);

	emerald->momz = flip * 24 * mapobjectscale;
	if (emerald->eflags & MFE_UNDERWATER)
		emerald->momz = (117 * emerald->momz) / 200;

	emerald->threshold = 10;

	switch (emeraldType)
	{
		case EMERALD_CHAOS1:
			emerald->color = SKINCOLOR_PERIDOT;
			break;
		case EMERALD_CHAOS2:
			emerald->color = SKINCOLOR_RED;
			break;
		case EMERALD_CHAOS3:
			emerald->color = SKINCOLOR_BLUE;
			break;
		case EMERALD_CHAOS4:
			emerald->color = SKINCOLOR_YELLOW;
			break;
		case EMERALD_CHAOS5:
			emerald->color = SKINCOLOR_PURPLE;
			break;
		case EMERALD_CHAOS6:
			emerald->color = SKINCOLOR_CYAN;
			break;
		case EMERALD_CHAOS7:
			emerald->color = SKINCOLOR_WHITE;
			break;
		default:
			CONS_Printf("Invalid emerald type %d\n", emeraldType);
			validEmerald = false;
			break;
	}

	if (validEmerald == true)
	{
		emerald->extravalue1 = emeraldType;
	}

	overlay = P_SpawnMobjFromMobj(emerald, 0, 0, 0, MT_OVERLAY);
	P_SetTarget(&overlay->target, emerald);
	P_SetMobjState(overlay, S_CHAOSEMERALD_UNDER);
	overlay->color = emerald->color;

	return emerald;
}

mobj_t *K_SpawnSphereBox(fixed_t x, fixed_t y, fixed_t z, angle_t angle, SINT8 flip, UINT8 amount)
{
	mobj_t *drop = P_SpawnMobj(x, y, z, MT_SPHEREBOX);

	drop->angle = angle;
	P_Thrust(drop,
		FixedAngle(P_RandomFixed() * 180) + angle,
		P_RandomRange(4, 12) * mapobjectscale);

	drop->momz = flip * 12 * mapobjectscale;
	if (drop->eflags & MFE_UNDERWATER)
		drop->momz = (117 * drop->momz) / 200;

	drop->flags &= ~(MF_NOGRAVITY|MF_NOCLIPHEIGHT);

	drop->extravalue1 = amount;

	return drop;
}

void K_DropEmeraldsFromPlayer(player_t *player, UINT32 emeraldType)
{
	UINT8 i;
	SINT8 flip = P_MobjFlip(player->mo);

	for (i = 0; i < 14; i++)
	{
		UINT32 emeraldFlag = (1 << i);

		if ((player->emeralds & emeraldFlag) && (emeraldFlag & emeraldType))
		{
			mobj_t *emerald = K_SpawnChaosEmerald(player->mo->x, player->mo->y, player->mo->z, player->mo->angle - ANGLE_90, flip, emeraldFlag);
			P_SetTarget(&emerald->target, player->mo);

			player->emeralds &= ~emeraldFlag;
		}
	}
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

	const boolean canmakeemeralds = true; //(!(itembreaker || bossinfo.boss));

	UINT32 emeraldsSpawned = 0;
	UINT32 firstUnspawnedEmerald = 0;

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

		emeraldsSpawned |= players[i].emeralds;

		if ((players[i].exiting > 0 || (players[i].pflags & PF_ELIMINATED))
			|| ((gametyperules & GTR_BUMPERS) && players[i].bumper <= 0))
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
				if (th->function.acp1 == (actionf_p1)P_RemoveThinkerDelayed)
					continue;

				mo = (mobj_t *)th;

				if (mo->type == MT_EMERALD)
				{
					emeraldsSpawned |= mo->extravalue1;
				}

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

			if (canmakeemeralds)
			{
				for (i = 0; i < 7; i++)
				{
					UINT32 emeraldFlag = (1 << i);

					if (!(emeraldsSpawned & emeraldFlag))
					{
						firstUnspawnedEmerald = emeraldFlag;
						starti = -1;
						break;
					}
				}
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
					key = P_RandomKey(spotCount);
				}

				r = spotMap[key];

				//CONS_Printf("[%d %d %d] ", i, key, r);

				flip = P_MobjFlip(spotList[r]);

				// When -1, we're spawning a Chaos Emerald.
				if (i == -1)
				{
					drop = K_SpawnChaosEmerald(
						spotList[r]->x, spotList[r]->y, spotList[r]->z + (128 * mapobjectscale * flip),
						FixedAngle(P_RandomRange(0, 359) * FRACUNIT), flip,
						firstUnspawnedEmerald
					);
				}
				else
				{
					if (gametyperules & GTR_SPHERES)
					{
						drop = K_SpawnSphereBox(
							spotList[r]->x, spotList[r]->y, spotList[r]->z + (128 * mapobjectscale * flip),
								FixedAngle(P_RandomRange(0, 359) * FRACUNIT), flip,
								10
						);
						K_FlipFromObject(drop, spotList[r]);
					}

					drop = K_CreatePaperItem(
						spotList[r]->x, spotList[r]->y, spotList[r]->z + (128 * mapobjectscale * flip),
						FixedAngle(P_RandomRange(0, 359) * FRACUNIT), flip,
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

void K_BattleInit(void)
{
	size_t i;

	if ((gametyperules & GTR_ITEMBREAKER) && !itembreaker && !bossinfo.boss)
	{
		if (modeattacking != ATTACKING_ITEMBREAK)
		{
			UINT8 n = 0;

			for (i = 0; i < MAXPLAYERS; i++)
			{
				if (!playeringame[i] || players[i].spectator)
					continue;
				n++;
			}

			if (n > 1)
				goto afteritembreaker;
		}

		itembreaker = true;
	}
afteritembreaker:

	if (gametyperules & GTR_BUMPERS)
	{
		INT32 maxbumpers = K_StartingBumperCount();

		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i] || players[i].spectator)
				continue;
			players[i].bumper = maxbumpers;
			K_SpawnPlayerBattleBumpers(players+i);
		}
	}
}
