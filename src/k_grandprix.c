// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2018-2020 by Sally "TehRealSalt" Cochenour
// Copyright (C) 2018-2020 by Kart Krew
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  k_grandprix.c
/// \brief Grand Prix mode game logic & bot behaviors

#include "k_grandprix.h"
#include "k_boss.h"
#include "k_specialstage.h"
#include "doomdef.h"
#include "d_player.h"
#include "g_game.h"
#include "k_bot.h"
#include "k_kart.h"
#include "m_random.h"
#include "p_local.h"
#include "r_things.h"
#include "lua_hook.h"
#include "k_battle.h"

struct grandprixinfo grandprixinfo;

/*--------------------------------------------------
	uint8_t K_BotStartingDifficulty(int8_t value)

		See header file for description.
--------------------------------------------------*/
uint8_t K_BotStartingDifficulty(int8_t value)
{
	// startingdifficulty: Easy = 3, Normal = 6, Hard = 9
	int8_t difficulty = (value + 1) * 3;

	if (difficulty > MAXBOTDIFFICULTY)
	{
		difficulty = MAXBOTDIFFICULTY;
	}
	else if (difficulty < 1)
	{
		difficulty = 1;
	}

	return difficulty;
}

/*--------------------------------------------------
	int16_t K_CalculateGPRankPoints(uint8_t position, uint8_t numplayers)

		See header file for description.
--------------------------------------------------*/
int16_t K_CalculateGPRankPoints(uint8_t position, uint8_t numplayers)
{
	int16_t points;

	if (position >= numplayers || position == 0)
	{
		// Invalid position, no points
		return 0;
	}

	points = numplayers - position;

	// somehow underflowed?
	if (points < 0)
	{
		points = 0;
	}

	LUA_HookGPRankPoints(position, numplayers, &points);

	return points;
}

/*--------------------------------------------------
	uint16_t K_BotDefaultSkin(void)

		See header file for description.
--------------------------------------------------*/
uint16_t K_BotDefaultSkin(void)
{
	const char *defaultbotskinname = "tails";
	int32_t defaultbotskin = R_SkinAvailable(defaultbotskinname);

	if (defaultbotskin == -1)
	{
		// This shouldn't happen, but just in case
		defaultbotskin = 0;
	}

	return (uint16_t)defaultbotskin;
}

/*--------------------------------------------------
	uint8_t K_GetGPPlayerCount(uint8_t humans)

		See header file for description.
--------------------------------------------------*/
uint8_t K_GetGPPlayerCount(uint8_t humans)
{
	// 1P -> 8 total
	// 2P -> 8 total
	// 3P -> 12 total
	// 4P -> 16 total

	return max(min(humans * 4, MAXGPPLAYERS), 8);
}

/*--------------------------------------------------
	void K_InitGrandPrixBots(void)

		See header file for description.
--------------------------------------------------*/
void K_InitGrandPrixBots(void)
{
	const uint16_t defaultbotskin = K_BotDefaultSkin();

	const uint8_t startingdifficulty = K_BotStartingDifficulty(grandprixinfo.gamespeed);
	uint8_t difficultylevels[MAXGPPLAYERS] = {0};

	uint8_t playercount = 8;
	uint8_t wantedbots = 0;

	uint8_t numplayers = 0;
	uint8_t competitors[MAXSPLITSCREENPLAYERS] = {0};

	uint16_t usableskins, skincount = numskins;
	uint16_t grabskins[MAXSKINS+1] = {0};

	uint16_t botskinlist[MAXGPPLAYERS] = {0};
	uint16_t botskinlistpos = 0;

	uint8_t newplayernum = 0;
	uint16_t i, j;

	memset(competitors, MAXGPPLAYERS, sizeof (competitors));
	memset(botskinlist, defaultbotskin, sizeof (botskinlist));

	// Init usable bot skins list
	for (usableskins = 0; usableskins < skincount; usableskins++)
	{
		grabskins[usableskins] = usableskins;
	}
	grabskins[usableskins] = MAXSKINS;

#if MAXGPPLAYERS != 16
	I_Error("GP bot difficulty levels need rebalanced for the new player count!\n");
#endif

	if (grandprixinfo.masterbots)
	{
		// Everyone is max difficulty!!
		memset(difficultylevels, MAXBOTDIFFICULTY, sizeof (difficultylevels));
	}
	else
	{
		// init difficulty levels list
		difficultylevels[0] = max(1, startingdifficulty);
		difficultylevels[1] = max(1, startingdifficulty-1);
		difficultylevels[2] = max(1, startingdifficulty-2);
		difficultylevels[3] = max(1, startingdifficulty-3);
		difficultylevels[4] = max(1, startingdifficulty-3);
		difficultylevels[5] = max(1, startingdifficulty-4);
		difficultylevels[6] = max(1, startingdifficulty-4);
		difficultylevels[7] = max(1, startingdifficulty-4);
		difficultylevels[8] = max(1, startingdifficulty-5);
		difficultylevels[9] = max(1, startingdifficulty-5);
		difficultylevels[10] = max(1, startingdifficulty-5);
		difficultylevels[11] = max(1, startingdifficulty-6);
		difficultylevels[12] = max(1, startingdifficulty-6);
		difficultylevels[13] = max(1, startingdifficulty-7);
		difficultylevels[14] = max(1, startingdifficulty-7);
		difficultylevels[15] = max(1, startingdifficulty-8);
	}

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (playeringame[i])
		{
			if (players[i].bot == true || i > MAXGPPLAYERS)
			{
				// Remove existing bots.
				CL_RemovePlayer(i, KR_LEAVE);
				continue;
			}

			if (numplayers < MAXSPLITSCREENPLAYERS && !players[i].spectator)
			{
				competitors[numplayers] = i;
				grabskins[players[i].skin] = MAXSKINS;
				numplayers++;
			}
			else
			{
				P_SetPlayerSpectator(i); // force spectate for all other players, if they happen to exist?
			}
		}
	}

	playercount = K_GetGPPlayerCount(numplayers);
	wantedbots = playercount - numplayers;

	// Create rival list
	if (numplayers > 0)
	{
		for (i = 0; i < SKINRIVALS; i++)
		{
			for (j = 0; j < numplayers; j++)
			{
				player_t *p = &players[competitors[j]];

				int32_t rivalnum;
				const char *rivalname = skins[p->skin]->rivals[i];
				rivalnum = R_SkinAvailable(rivalname);

				// Intentionally referenced before (currently dummied out) unlock check. Such a tease!
				if (rivalnum != -1 && grabskins[(uint16_t)rivalnum] != MAXSKINS)
				{
					botskinlist[botskinlistpos++] = (uint16_t)rivalnum;
					grabskins[(uint16_t)rivalnum] = MAXSKINS;
				}
			}
		}
	}

	// Rearrange usable bot skins list to prevent gaps for randomised selection
	for (i = 0; i < usableskins; i++)
	{
		if (!(grabskins[i] == MAXSKINS || !R_SkinUsable(-1, grabskins[i])))
		{
			continue;
		}

		while (usableskins > i && (grabskins[usableskins] == MAXSKINS || !R_SkinUsable(-1, grabskins[usableskins])))
		{
			usableskins--;
		}

		grabskins[i] = grabskins[usableskins];
		grabskins[usableskins] = MAXSKINS;
	}

	// Pad the remaining list with random skins if we need to
	if (botskinlistpos < wantedbots)
	{
		while (botskinlistpos < wantedbots)
		{
			uint16_t skinnum = defaultbotskin;

			if (usableskins > 0)
			{
				uint16_t index = P_RandomKey(PR_BOTS, usableskins);
				skinnum = grabskins[index];

				if (skinnum == MAXSKINS)
					skinnum = defaultbotskin;

				if (usableskins >= K_GetGPPlayerCount(1))
				{
					grabskins[index] = grabskins[--usableskins];
				}
			}

			botskinlist[botskinlistpos++] = skinnum;
		}
	}

	for (i = 0; i < wantedbots; i++)
	{
		uint16_t this_skin = botskinlist[i];

		if (this_skin >= numskins)
		{
			// SOMEHOW fed the bot adder an out-of-range skin
			I_Error("K_InitGrandPrixBots: received garbage skin number %d (acceptable range is 0 to %d)\n", this_skin, (numskins - 1));
			break;
		}

		if (!K_AddBot(this_skin, difficultylevels[i], BOT_STYLE_NORMAL, &newplayernum))
		{
			break;
		}
	}
}

/*--------------------------------------------------
	static int16_t K_RivalScore(player_t *bot)

		Creates a "rival score" for a bot, used to determine which bot is the
		most deserving of the rival status.

	Input Arguments:-
		bot - Player to check.

	Return:-
		"Rival score" value.
--------------------------------------------------*/
static int16_t K_RivalScore(player_t *bot)
{
	const uint16_t difficulty = bot->botvars.difficulty;
	const uint16_t score = bot->score;
	int8_t roundnum = 1, roundsleft = 1;
	uint16_t lowestscore = UINT16_MAX;
	uint8_t lowestdifficulty = MAXBOTDIFFICULTY;
	uint8_t i;

	if (grandprixinfo.cup != NULL && roundqueue.size > 0)
	{
		roundnum = roundqueue.roundnum;
		roundsleft = grandprixinfo.cup->numlevels - roundnum;
	}

	for (i = 0; i < MAXGPPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator)
		{
			continue;
		}

		if (players[i].score < lowestscore)
		{
			lowestscore = players[i].score;
		}

		if (players[i].bot == true && players[i].botvars.difficulty < lowestdifficulty)
		{
			lowestdifficulty = players[i].botvars.difficulty;
		}
	}

	// In the early game, difficulty is more important.
	// This will try to influence the higher difficulty bots to get rival more often & get even more points.
	// However, when we're running low on matches left, we need to focus more on raw score!

	return ((difficulty - lowestdifficulty) * roundsleft) + ((score - lowestscore) * roundnum);
}

/*--------------------------------------------------
	void K_UpdateGrandPrixBots(void)

		See header file for description.
--------------------------------------------------*/
void K_UpdateGrandPrixBots(void)
{
	player_t *oldrival = NULL;
	player_t *newrival = NULL;
	uint16_t newrivalscore = 0;
	uint8_t i;

	for (i = 0; i < MAXGPPLAYERS; i++)
	{
		if (!playeringame[i] || !players[i].bot)
		{
			continue;
		}

		players[i].spectator = K_BotDefaultSpectator();
	}

	if (grandprixinfo.wonround == false)
	{
		return;
	}

	// Find the rival.
	for (i = 0; i < MAXGPPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator || !players[i].bot)
		{
			continue;
		}

		if (players[i].botvars.diffincrease)
		{
			players[i].botvars.difficulty += players[i].botvars.diffincrease;

			if (players[i].botvars.difficulty > MAXBOTDIFFICULTY)
			{
				players[i].botvars.difficulty = MAXBOTDIFFICULTY;
			}

			players[i].botvars.diffincrease = 0;
		}

		if (players[i].botvars.rival)
		{
			if (oldrival == NULL)
			{
				// Record the old rival to compare with our calculated new rival
				oldrival = &players[i];
			}
			else
			{
				// Somehow 2 rivals were set?!
				// Let's quietly fix our mess...
				players[i].botvars.rival = false;
			}
		}
	}

	// Find the bot with the best average of score & difficulty.
	for (i = 0; i < MAXGPPLAYERS; i++)
	{
		uint16_t ns = 0;

		if (!playeringame[i] || players[i].spectator || !players[i].bot)
		{
			continue;
		}

		ns = K_RivalScore(&players[i]);

		if (ns > newrivalscore)
		{
			newrival = &players[i];
			newrivalscore = ns;
		}
	}

	// Even if there's a new rival, we want to make sure that they're a better fit than the current one.
	if (oldrival != newrival)
	{
		if (oldrival != NULL)
		{
			uint16_t os = K_RivalScore(oldrival);

			if (newrivalscore < os + 5)
			{
				// This rival's only *slightly* better, no need to fire the old one.
				// Our current rival's just fine, thank you very much.
				return;
			}

			// Hand over your badge.
			oldrival->botvars.rival = false;
		}

		// Set our new rival!
		newrival->botvars.rival = true;
	}

	if (K_UsingLegacyCheckpoints() && !K_BotDefaultSpectator())
	{
		CONS_Alert(CONS_ERROR, "This map does not use waypoints so bot functionality will not work.\nConsider adding new waypoints directly or via map patching for bot support.\n");
	}
}

/*--------------------------------------------------
	static uint8_t K_BotExpectedStanding(player_t *bot)

		Predicts what placement a bot was expected to be in.
		Used for determining if a bot's difficulty should raise.

	Input Arguments:-
		bot - Player to check.

	Return:-
		Position number the bot was expected to be in.
--------------------------------------------------*/
static uint8_t K_BotExpectedStanding(player_t *bot)
{
	uint8_t pos = 1;
	uint8_t i;

	for (i = 0; i < MAXGPPLAYERS; i++)
	{
		if (i == (bot - players))
		{
			continue;
		}

		if (playeringame[i] && !players[i].spectator)
		{
			if (players[i].bot)
			{
				if (players[i].botvars.difficulty > bot->botvars.difficulty)
				{
					pos++;
				}
				else if (players[i].score > bot->score)
				{
					pos++;
				}
			}
			else
			{
				// Human player, always increment
				pos++;
			}
		}
	}

	return pos;
}

/*--------------------------------------------------
	void K_IncreaseBotDifficulty(player_t *bot)

		See header file for description.
--------------------------------------------------*/
void K_IncreaseBotDifficulty(player_t *bot)
{
	uint8_t expectedstanding;
	int16_t standingdiff;

	if (bot->botvars.difficulty >= MAXBOTDIFFICULTY)
	{
		// Already at max difficulty, don't need to increase
		return;
	}

	// Increment bot difficulty based on what position you were meant to come in!
	expectedstanding = K_BotExpectedStanding(bot);
	standingdiff = expectedstanding - bot->position;

	if (standingdiff >= -2)
	{
		uint8_t increase;

		if (standingdiff > 5)
		{
			increase = 3;
		}
		else if (standingdiff > 2)
		{
			increase = 2;
		}
		else
		{
			increase = 1;
		}

		bot->botvars.diffincrease = increase;
	}
	else
	{
		bot->botvars.diffincrease = 0;
	}
}

/*--------------------------------------------------
	void K_RetireBots(void)

		See header file for description.
--------------------------------------------------*/
void K_RetireBots(void)
{
	const uint16_t defaultbotskin = K_BotDefaultSkin();
	int8_t newDifficulty;

	uint16_t usableskins = 0, skincount = numskins;
	uint16_t grabskins[MAXSKINS+1];

	uint16_t i;

	if (grandprixinfo.gp == true
		&& (grandprixinfo.eventmode != GPEVENT_NONE
		|| (grandprixinfo.cup != NULL
		&& roundqueue.size > 0
		&& roundqueue.roundnum >= grandprixinfo.cup->numlevels)))
	{
		// No replacement.
		return;
	}

	// Init usable bot skins list
	for (i = 0; i < skincount; i++)
	{
		grabskins[usableskins++] = i;
	}
	grabskins[usableskins] = MAXSKINS;

	// Rearrange usable bot skins list to prevent gaps for randomised selection
	for (i = 0; i < usableskins; i++)
	{
		if (!(grabskins[i] == MAXSKINS || !R_SkinUsable(-1, grabskins[i])))
		{
			continue;
		}

		while (usableskins > i && (grabskins[usableskins] == MAXSKINS || !R_SkinUsable(-1, grabskins[usableskins])))
		{
			usableskins--;
		}

		grabskins[i] = grabskins[usableskins];
		grabskins[usableskins] = MAXSKINS;
	}

	if (grandprixinfo.gp) // Sure, let's let this happen all the time :)
	{
		if (grandprixinfo.masterbots == true)
		{
			newDifficulty = MAXBOTDIFFICULTY;
		}
		else
		{
			const uint8_t startingdifficulty = K_BotStartingDifficulty(grandprixinfo.gamespeed);
			newDifficulty = startingdifficulty - 4;
			if (roundqueue.size > 0)
			{
				newDifficulty += roundqueue.roundnum;
			}
		}
	}
	else
	{
		newDifficulty = cv_kartbot.value;
	}

	if (newDifficulty > MAXBOTDIFFICULTY)
	{
		newDifficulty = MAXBOTDIFFICULTY;
	}
	else if (newDifficulty < 1)
	{
		newDifficulty = 1;
	}

	for (i = 0; i < MAXPLAYERS; i++)
	{
		player_t *bot = NULL;

		if (!playeringame[i] || !players[i].bot || players[i].spectator)
		{
			continue;
		}

		bot = &players[i];

		if (bot->pflags & PF_NOCONTEST)
		{
			// HACK!!!!! two days to end of cleanup period :)
			// we do this so that any bot that's been removed doesn't count for K_SetNameForBot conflicts
			player_names[i][0] = '0';
		}
	}

	for (i = 0; i < MAXPLAYERS; i++)
	{
		player_t *bot = NULL;

		if (!playeringame[i] || !players[i].bot || players[i].spectator)
		{
			continue;
		}

		bot = &players[i];

		if (bot->pflags & PF_NOCONTEST)
		{
			uint16_t skinnum = defaultbotskin;

			if (usableskins > 0)
			{
				uint16_t index = P_RandomKey(PR_BOTS, numskins);
				skinnum = grabskins[index];
				if (usableskins+1 >= K_GetGPPlayerCount(1))
				{
					grabskins[index] = grabskins[--usableskins];
				}
			}

			bot->botvars.difficulty = newDifficulty;
			bot->botvars.diffincrease = 0;

			SetPlayerSkinByNum(i, skinnum);
			bot->skincolor = skins[skinnum]->prefcolor;
			K_SetNameForBot(i, skins[skinnum]->realname);

			bot->score = 0;
			LUA_HookPlayer(bot, HOOK(BotJoin));
			bot->pflags &= ~PF_NOCONTEST;
		}
	}
}

/*--------------------------------------------------
	void K_FakeBotResults(player_t *bot)

		See header file for description.
--------------------------------------------------*/
void K_FakeBotResults(player_t *bot)
{
	const uint32_t distfactor = FixedMul(32 * mapobjectscale, K_GetKartGameSpeedScalar(gamespeed)) / FRACUNIT;
	uint32_t worstdist = 0;
	tic_t besttime = UINT32_MAX;
	uint8_t numplayers = 0;
	uint8_t i;

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (playeringame[i] && !players[i].spectator)
		{
			numplayers++;

			if (players[i].exiting && players[i].realtime < besttime)
			{
				besttime = players[i].realtime;
			}

			if (players[i].distancetofinish > worstdist)
			{
				worstdist = players[i].distancetofinish;
			}
		}
	}

	if (besttime == UINT32_MAX // No one finished, so you don't finish either.
	|| bot->distancetofinish >= worstdist) // Last place, you aren't going to finish.
	{
		bot->pflags |= PF_NOCONTEST;
		return;
	}

	// hey, you "won"
	bot->exiting = 2;
	bot->realtime += (bot->distancetofinish / distfactor);
	bot->distancetofinish = 0;
	K_IncreaseBotDifficulty(bot);
}

/*--------------------------------------------------
	void K_PlayerLoseLife(player_t *player)

		See header file for description.
--------------------------------------------------*/
void K_PlayerLoseLife(player_t *player)
{
	if (!G_GametypeUsesLives())
	{
		return;
	}

	if (player->spectator || player->exiting || player->bot || (player->pflags & PF_LOSTLIFE))
	{
		return;
	}

	player->lives--;
	player->pflags |= PF_LOSTLIFE;

#if 0
	if (player->lives <= 0)
	{
		if (P_IsLocalPlayer(player))
		{
			S_StopMusic();
			S_ChangeMusicInternal("gmover", false);
		}
	}
#endif
}

/*--------------------------------------------------
	dboolean K_CanChangeRules(dboolean allowdemos)

		See header file for description.
--------------------------------------------------*/
dboolean K_CanChangeRules(dboolean allowdemos)
{
	if (grandprixinfo.gp == true)
	{
		// Don't cheat the rules of the GP!
		return false;
	}

	if (bossinfo.boss == true)
	{
		// Don't cheat the boss!
		return false;
	}

	if (specialStage.active == true)
	{
		// Don't cheat special stages!
		return false;
	}

	if (marathonmode)
	{
		// Don't cheat the endurance challenge!
		return false;
	}

	if (modeattacking != ATTACKING_NONE)
	{
		// Don't cheat the rules of Time Trials!
		return false;
	}

	if (!allowdemos && demo.playback)
	{
		// We've already got our important settings!
		return false;
	}

	return true;
}

/*--------------------------------------------------
	dboolean K_BotDefaultSpectator(player_t *player);

		See header file for description.
--------------------------------------------------*/
dboolean K_BotDefaultSpectator(void)
{
	if (cv_forcebots.value)
	{
		return false;
	}

	if (!(gametypes[gametype]->rules & GTR_BOTS))
	{
		// This gametype does not support bots.
		return true;
	}

	if (grandprixinfo.eventmode != GPEVENT_NONE)
	{
		// This is a special round of GP, so bots must spectate.
		return true;
	}

	if (K_UsingLegacyCheckpoints())
	{
		return true;
	}

	return false;
}
