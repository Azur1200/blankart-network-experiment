// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2024 by Sally "TehRealSalt" Cochenour
// Copyright (C) 2024 by Kart Krew
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  k_bot.cpp
/// \brief Bot logic & ticcmd generation code

#include <algorithm>

#include <tracy/tracy/Tracy.hpp>

#include "cxxutil.hpp"

#include "doomdef.h"
#include "d_player.h"
#include "g_game.h"
#include "r_main.h"
#include "p_local.h"
#include "k_bot.h"
#include "lua_hook.h"
#include "byteptr.h"
#include "d_net.h" // nodetoplayer
#include "k_kart.h"
#include "z_zone.h"
#include "i_system.h"
#include "p_maputl.h"
#include "d_ticcmd.h"
#include "m_random.h"
#include "r_things.h" // numskins
#include "m_perfstats.h"
#include "m_easing.h"
#include "d_clisrv.h"
#include "k_grandprix.h" // K_CanChangeRules
#include "hu_stuff.h" // HU_AddChatText
#ifdef HAVE_DISCORDRPC
#include "discord.h" // DRPC_UpdatePresence
#endif
#include "i_net.h" // doomcom
#include "blan/b_soc.h"
#include "v_video.h" // for debugging
#include "k_battle.h" // item breaker
#include "k_waypoint.h"
#include "k_items.h"

consvar_t cv_forcebots = CVAR_INIT ("kartbot_force", "Off", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL);
consvar_t cv_botcontrol = CVAR_INIT ("kartbot_control", "On", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL);
consvar_t cv_forcerival = CVAR_INIT ("kartbot_forcerival", "Off", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL);
consvar_t cv_botcanvote = CVAR_INIT ("kartbot_canvote", "Off", CV_NETVAR, CV_OnOff, NULL);
consvar_t cv_botdrifting = CVAR_INIT ("kartbot_drift", "Off", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL);

botdata_t botdata[MAXPLAYERS];

void K_DrawBotDebugger(const player_t *player)
{
	int32_t vflags = V_6WIDTHSPACE|V_ALLOWLOWERCASE|V_30TRANS;
	botdata_t *bd = &botdata[player - players];

	static const char *driftstates[] = {
		"auto", // DRIFTSTATE_AUTO
		"starting", // DRIFTSTATE_STARTING
		"active", // DRIFTSTATE_ACTIVE
		"ending", // DRIFTSTATE_ENDING
	};

	if (!cv_kartdebugbot.value || !player->bot)
		return;

	int32_t x1 = 26, x2 = 99, y = 92;

	V_DrawThinString(x1, y+0, vflags, va("predict.x: %d", bd->predict.x/FRACUNIT));
	V_DrawThinString(x2, y+0, vflags, va("predict.y: %d", bd->predict.y/FRACUNIT));
	V_DrawThinString(x1, y+8, vflags, va("predict.radius: %d", bd->predict.radius/FRACUNIT));
	V_DrawThinString(x2, y+8, vflags, va("predict.baseradius: %d", bd->predict.baseRadius/FRACUNIT));

	V_DrawThinString(x1, y+16, vflags, va("itemconfirm: %d", bd->itemconfirm));
	V_DrawThinString(x2, y+16, vflags, va("itemdelay: %d", bd->itemdelay));
	V_DrawThinString(x1, y+24, vflags, va("turnconfirm: %d", bd->turnconfirm));
	V_DrawThinString(x2, y+24, vflags, va("respawnconfirm: %d", bd->respawnconfirm));

	V_DrawThinString(x1, y+32, vflags, va("driftstate: %s", driftstates[bd->driftstate]));
	V_DrawThinString(x2, y+32, vflags|(bd->driftlockout ? V_ORANGEMAP : 0), va("driftlockout: %d", bd->driftlockout));
	V_DrawThinString(x1, y+40, vflags, va("driftturn: %d", bd->driftturn));
	V_DrawThinString(x2, y+40, vflags, va("drifttime: %d", bd->drifttime));
	V_DrawThinString(x1, y+48, vflags, va("predicterr: %.1f", ((double)(int32_t)bd->predictionerror)/ANG1));
	V_DrawThinString(x2, y+48, vflags|(bd->griplockout ? V_ORANGEMAP : 0), va("griplockout: %d", bd->griplockout));
}

/*--------------------------------------------------
	void K_SetNameForBot(uint8_t playerNum, const char *realname)

		See header file for description.
--------------------------------------------------*/
void K_SetNameForBot(uint8_t newplayernum, const char *realname)
{
	uint8_t ix = MAXPLAYERS;

	// These names are generally sourced from skins.
	I_Assert(MAXPLAYERNAME >= SKINNAMESIZE+2);

	if (netgame == true)
	{
		// Check if a player is currently using the name, case-insensitively.
		// We only do this if online, because it doesn't matter if there are multiple Eggrobo *off*line.
		// See also EnsurePlayerNameIsGood
		for (ix = 0; ix < MAXPLAYERS; ix++)
		{
			if (ix == newplayernum)
				continue;
			if (playeringame[ix] == false)
				continue;
			if (strcasecmp(realname, player_names[ix]) != 0)
				continue;

			break;
		}
	}

	if (ix == MAXPLAYERS)
	{
		// No conflict detected!
		snprintf(player_names[newplayernum], MAXPLAYERNAME+1, "%s", realname);
		return;
	}

	// Ok, now we append on the end for duplicates...
	char namebuffer[MAXPLAYERNAME+1];
	snprintf(namebuffer, sizeof(namebuffer), "%s %c", realname, 'A'+newplayernum);

	// ...and use the actual function, to handle more devious duplication.
	if (!EnsurePlayerNameIsGood(namebuffer, newplayernum))
	{
		// we can't bail from adding the bot...
		// this hopefully uncontroversial pick is all we CAN do
		snprintf(namebuffer, sizeof(namebuffer), "Bot %u", newplayernum+1);
	}

	// And finally write.
	snprintf(player_names[newplayernum], MAXPLAYERNAME+1, "%s", namebuffer);
}

/*--------------------------------------------------
	void K_SetBot(uint8_t playerNum, uint16_t skinnum, uint8_t difficulty, botStyle_e style)

		See header file for description.
--------------------------------------------------*/
void K_SetBot(uint8_t newplayernum, uint16_t skinnum, uint8_t difficulty, botStyle_e style)
{
	// Double check our skin first
	if ((skinnum >= numskins) || (skinnum == MAXSKINS))
	{
		I_Error("K_SetBot: received garbage skin number %d (acceptable range is 0 to %d)\n", skinnum, (numskins - 1));
		return;
	}

	CONS_Debug(DBG_NETPLAY, "addbot: %d\n", newplayernum);

	G_AddPlayer(newplayernum, newplayernum);

	if (newplayernum+1 > doomcom->numslots)
		doomcom->numslots = (int16_t)(newplayernum+1);

	// this will permit unlocks
	memcpy(&players[newplayernum].availabilities, R_GetSkinAvailabilities(), MAXAVAILABILITY*sizeof(uint8_t));

	playernode[newplayernum] = servernode;

	players[newplayernum].splitscreenindex = 0;
	players[newplayernum].bot = true;
	players[newplayernum].botvars.difficulty = difficulty;
	players[newplayernum].botvars.style = style;
	players[newplayernum].lives = 9;

	// Does the server want to force rivals? enforce this here.
	if (cv_forcerival.value)
	{
		players[newplayernum].botvars.rival = true;
	}

	// The bot may immediately become a spectator AT THE START of a GP.
	// For each subsequent round of GP, K_UpdateGrandPrixBots will handle this.
	players[newplayernum].spectator = grandprixinfo.gp && grandprixinfo.initalize && K_BotDefaultSpectator();

	skincolornum_t color = static_cast<skincolornum_t>(skins[skinnum]->prefcolor);
	const char *realname = skins[skinnum]->realname;
	players[newplayernum].skincolor = color;
	K_SetNameForBot(newplayernum, realname);

	SetPlayerSkinByNum(newplayernum, skinnum);

	LUA_HookPlayer(&players[newplayernum], HOOK(BotJoin));

	for (uint8_t i = 0; i < PWRLV_NUMTYPES; i++)
	{
		clientpowerlevels[newplayernum][i] = 0;
	}

	if (netgame)
	{
		HU_AddChatText(va("\x82*Bot %d has been added to the game", newplayernum+1), false);
	}

	LUA_HookInt(newplayernum, HOOK(PlayerJoin));
}

/*--------------------------------------------------
	dboolean K_AddBot(uint16_t skin, uint8_t difficulty, botStyle_e style, uint8_t *p)

		See header file for description.
--------------------------------------------------*/
dboolean K_AddBot(uint16_t skin, uint8_t difficulty, botStyle_e style, uint8_t *p)
{
	uint8_t newplayernum = *p;

	for (; newplayernum < MAXPLAYERS; newplayernum++)
	{
		if (playeringame[newplayernum] == false)
		{
			// free player slot
			break;
		}
	}

	if (newplayernum >= MAXPLAYERS)
	{
		// nothing is free
		*p = MAXPLAYERS;
		return false;
	}

	K_SetBot(newplayernum, skin, difficulty, style);
	DEBFILE(va("Everyone added bot %d\n", newplayernum));

	// use the next free slot
	*p = newplayernum+1;

	return true;
}

/*--------------------------------------------------
	void K_UpdateMatchRaceBots(void)

		See header file for description.
--------------------------------------------------*/
void K_UpdateMatchRaceBots(void)
{
	const uint16_t defaultbotskin = K_BotDefaultSkin();
	uint8_t difficulty;
	uint8_t pmax = (InADedicatedServer() ? MAXPLAYERS-1 : MAXPLAYERS);
	uint8_t numplayers = 0;
	uint8_t numbots = 0;
	uint8_t numwaiting = 0;
	int8_t wantedbots = 0;
	uint16_t usableskins = 0, skincount =  numskins;
	uint16_t grabskins[MAXSKINS+1];
	uint16_t i;

	// Init usable bot skins list
	for (i = 0; i < skincount; i++)
	{
		grabskins[usableskins++] = i;
	}
	grabskins[usableskins] = MAXSKINS;

	if (gamestate == GS_TITLESCREEN)
	{
		difficulty = 0;
	}
	else if ((gametypes[gametype]->rules & GTR_BOTS) == 0 && !cv_forcebots.value)
	{
		difficulty = 0;
	}
	else if (itembreaker && !cv_forcebots.value)
	{
		difficulty = 0;
	}
	else if (K_CanChangeRules(true) == false)
	{
		difficulty = 0;
	}
	else if (K_UsingLegacyCheckpoints() == true && !cv_forcebots.value)
	{
		if (cv_kartbot.value > 0 && cv_kartbot_cap.value > 0)
			CONS_Alert(CONS_ERROR, "This map does not use waypoints so bot functionality will not work.\nConsider adding new waypoints directly or via map patching for bot support.\n");
		difficulty = 0;
	}
	else
	{
		difficulty = cv_kartbot.value;
		pmax = std::min<uint8_t>(pmax, static_cast<uint8_t>(cv_kartbot_cap.value));
	}

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (playeringame[i])
		{
			if (!players[i].spectator)
			{
				grabskins[players[i].skin] = MAXSKINS;

				if (players[i].bot)
				{
					numbots++;

					// While we're here, we should update bot difficulty to the proper value.
					players[i].botvars.difficulty = difficulty;

					// Enforce normal style for Match Race
					players[i].botvars.style = BOT_STYLE_NORMAL;

					// Does the server want to force rivals? enforce this here.
					if (cv_forcerival.value)
					{
						players[i].botvars.rival = true;
					}
					else
					{
						players[i].botvars.rival = false;
					}
				}
				else
				{
					numplayers++;
				}
			}
			else if (players[i].pflags & PF_WANTSTOJOIN)
			{
				numwaiting++;
			}
		}
	}

	if (difficulty == 0)
	{
		// Remove bots if there are any.
		wantedbots = 0;
	}
	else
	{
		// Add bots to fill up MAXPLAYERS
		wantedbots = pmax - numplayers - numwaiting;

		if (wantedbots < 0)
		{
			wantedbots = 0;
		}
	}

	auto clear_bots = [&numbots](uint8_t max)
	{
		uint8_t i = MAXPLAYERS;
		while (numbots > max && i > 0)
		{
			i--;

			if (playeringame[i] && players[i].bot)
			{
				CL_RemovePlayer(i, KR_LEAVE);
				numbots--;
			}
		}
	};

	if (numbots < wantedbots)
	{
		// We require MORE bots!
		uint8_t newplayernum = InADedicatedServer() ? 1 : 0;

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

		while (numbots < wantedbots)
		{
			uint16_t skinnum = defaultbotskin;

			if (usableskins > 0)
			{
				uint16_t index = P_RandomKey(PR_BOTS, usableskins);
				skinnum = grabskins[index];
				if (((cv_kartbot_cap.value > 0) && (usableskins+1 >= cv_kartbot_cap.value)) || (usableskins+1 >= cv_maxplayers.value))
				{
					grabskins[index] = grabskins[--usableskins];
				}
			}

			if (!K_AddBot(skinnum, difficulty, BOT_STYLE_NORMAL, &newplayernum))
			{
				// Not enough player slots to add the bot, break the loop.
				break;
			}

			numbots++;
		}
	}
	else if (numbots > wantedbots)
	{
		clear_bots(wantedbots);
	}

	// We should have enough bots now :)

#ifdef HAVE_DISCORDRPC
	// Player count change was possible, so update presence
	DRPC_UpdatePresence();
#endif
}

/*--------------------------------------------------
	dboolean K_PlayerUsesBotMovement(const player_t *player)

		See header file for description.
--------------------------------------------------*/
dboolean K_PlayerUsesBotMovement(const player_t *player)
{
	// Lua can't override the podium sequence result, but it can
	// override the following results:
	{
		uint8_t shouldOverride = LUA_HookPlayerForceResults(const_cast<player_t*>(player),
			HOOK(PlayerUsesBotMovement));
		if (shouldOverride == 1)
			return true;
		if (shouldOverride == 2)
			return false;
	}

	if (player->bot)
		return true;

	return false;
}

/*--------------------------------------------------
	dboolean K_BotCanTakeCut(player_t *player)

		See header file for description.
--------------------------------------------------*/
dboolean K_BotCanTakeCut(const player_t *player)
{
	if (
		(K_TripwirePassConditions(player) != TRIPWIRE_NONE || K_ApplyOffroad(player) == false)
		|| player->itemtype == KITEM_SNEAKER
		|| player->itemtype == KITEM_ROCKETSNEAKER
		|| player->itemtype == KITEM_INVINCIBILITY
		)
	{
		return true;
	}

	return false;
}

/*--------------------------------------------------
	static fixed_t K_BotSpeedScaled(const player_t *player, fixed_t speed)

		What the bot "thinks" their speed is, for predictions.
		Mainly to make bots brake earlier when on friction sectors.

	Input Arguments:-
		player - The bot player to calculate speed for.
		speed - Raw speed value.

	Return:-
		The bot's speed value for calculations.
--------------------------------------------------*/
static fixed_t K_BotSpeedScaled(const player_t *player, fixed_t speed)
{
	fixed_t result = speed;

	if (!player->pogospring && P_IsObjectOnGround(player->mo) == false)
	{
		// You have no air control, so don't predict too far ahead.
		return 0;
	}

	if (player->mo->movefactor != FRACUNIT)
	{
		fixed_t moveFactor = player->mo->movefactor;

		if (moveFactor == 0)
		{
			moveFactor = 1;
		}

		// Reverse against friction. Allows for bots to
		// acknowledge they'll be moving faster on ice,
		// and to steer harder / brake earlier.
		moveFactor = FixedDiv(FRACUNIT, moveFactor);

		// The full value is way too strong, reduce it.
		// moveFactor -= (moveFactor - FRACUNIT)*3/4;

		result = FixedMul(result, moveFactor);
	}

	if (player->mo->standingslope != nullptr)
	{
		const pslope_t *slope = player->mo->standingslope;

		if (!(slope->flags & SL_NOPHYSICS) && abs(slope->zdelta) >= FRACUNIT/21)
		{
			fixed_t slopeMul = FRACUNIT;
			angle_t angle = K_MomentumAngle(player->mo) - slope->xydirection;

			if (P_MobjFlip(player->mo) * slope->zdelta < 0)
				angle ^= ANGLE_180;

			// Going uphill: 0
			// Going downhill: FRACUNIT*2
			slopeMul = FRACUNIT + FINECOSINE(angle >> ANGLETOFINESHIFT);

			// Range: 0.5 to 1.5
			result = FixedMul(result, (FRACUNIT>>1) + (slopeMul >> 1));
		}
	}

	return result;
}

/*--------------------------------------------------
	botcontroller_t *K_GetBotController(const mobj_t *mobj)

		See header file for description.
--------------------------------------------------*/
botcontroller_t *K_GetBotController(const mobj_t *mobj)
{
	botcontroller_t *ret = nullptr;

	if (P_MobjWasRemoved(mobj) == true)
	{
		return nullptr;
	}

	if (mobj->subsector == nullptr || mobj->subsector->sector == nullptr)
	{
		return nullptr;
	}

	ret = &mobj->subsector->sector->botController;

	ffloor_t *rover = nullptr;
	for (rover = mobj->subsector->sector->ffloors; rover; rover = rover->next)
	{
		if ((rover->fofflags & FOF_EXISTS) == 0)
		{
			continue;
		}

		fixed_t topheight = P_GetFOFTopZ(mobj, mobj->subsector->sector, rover, mobj->x, mobj->y, nullptr);
		fixed_t bottomheight = P_GetFOFBottomZ(mobj, mobj->subsector->sector, rover, mobj->x, mobj->y, nullptr);

		if (mobj->z > topheight || mobj->z + mobj->height < bottomheight)
		{
			continue;
		}

		botcontroller_t *roverController = &rover->master->frontsector->botController;
		if (roverController->flags != 0)
		{
			ret = roverController;
		}
	}

	return ret;
}

/*--------------------------------------------------
	fixed_t K_BotMapModifier(void)

		See header file for description.
--------------------------------------------------*/
fixed_t K_BotMapModifier(void)
{
	// fuck it we ball
	//return 5*FRACUNIT/10;
	// ...with a bit of customization

	if ((grandprixinfo.gp == true) && (grandprixinfo.lunaticmode == true))
		return std::max(static_cast<fixed_t>(BASELUNATICSPEEDMOD), K_TrackModifierMax());

	return K_TrackModifierMax();

#if 0
	constexpr int32_t complexity_scale = 10000;
	fixed_t modifier_max = K_TrackModifierMax();

	if (K_CanChangeRules() == false)
	{
		modifier_max = FRACUNIT;
	}

	const fixed_t complexity_value = std::clamp<fixed_t>(
		FixedDiv(K_GetTrackComplexity(), complexity_scale),
		-FixedDiv(FRACUNIT, modifier_max),
		modifier_max
	);

	return FRACUNIT + complexity_value;
#endif
}

/*--------------------------------------------------
	static uint32_t K_BotRubberbandDistance(const player_t *player)

		Calculates the distance away from 1st place that the
		bot should rubberband to.

	Input Arguments:-
		player - Player to compare.

	Return:-
		Distance to add, as an integer.
--------------------------------------------------*/
static uint32_t K_BotRubberbandDistance(const player_t *player)
{
	const uint32_t spacing = FixedDiv(640 * mapobjectscale, K_GetKartGameSpeedScalar(gamespeed)) / FRACUNIT;
	const uint8_t portpriority = player - players;
	uint8_t pos = 1;
	uint8_t i;

	if (player->botvars.rival)
	{
		// The rival should always try to be the front runner for the race.
		return 0;
	}

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (i == portpriority)
		{
			continue;
		}

		if (!playeringame[i] || players[i].spectator)
		{
			continue;
		}

		if (!players[i].bot)
		{
			continue;
		}

		// First check difficulty levels, then score, then settle it with port priority!
		if (player->botvars.difficulty < players[i].botvars.difficulty)
		{
			pos += 3;
		}
		else if (player->score < players[i].score)
		{
			pos += 2;
		}
		else if (i < portpriority)
		{
			pos += 1;
		}
	}

	return (pos * spacing);
}

/*--------------------------------------------------
	fixed_t K_BotRubberband(const player_t *player)

		See header file for description.
--------------------------------------------------*/
fixed_t K_BotRubberband(const player_t *player)
{
	if (player->exiting)
	{
		// You're done, we don't need to rubberband anymore.
		return FRACUNIT;
	}

	const botcontroller_t *botController = K_GetBotController(player->mo);
	if (botController != nullptr && (botController->flags & TMBOT_NORUBBERBAND) == TMBOT_NORUBBERBAND) // Disable rubberbanding
	{
		return FRACUNIT;
	}

	fixed_t difficultyEase = ((player->botvars.difficulty - 1) * FRACUNIT) / (MAXBOTDIFFICULTY - 1);

	// Lv.   1: x0.65 avg
	// Lv. MAX: x1.05 avg
	const fixed_t rubberBase = Easing_OutSine(
		difficultyEase,
		FRACUNIT * 65 / 100,
		FRACUNIT * 105 / 100
	);

	// +/- x0.35
	fixed_t botModifier = K_BotMapModifier();

	if ((grandprixinfo.gp == true) && (grandprixinfo.lunaticmode == true))
	{
		if (player->botvars.rival == true)
			botModifier =
			    std::max(BASELUNATICRIVALSPEEDMOD,
				     K_BotMapModifier());  // The rival is faster on Lunatic mode.
	}

	const fixed_t rubberStretchiness = FixedMul(
	    FixedDiv(35 * FRACUNIT / 100, K_GetKartGameSpeedScalar(gamespeed)), botModifier);

	// Lv.   1: x0.4 min
	// Lv. MAX: x0.85 min
	constexpr fixed_t rubberSlowMin = FRACUNIT / 2;
	const fixed_t rubberSlow = std::max<fixed_t>( rubberBase - rubberStretchiness, rubberSlowMin );

	// Lv.   1: x0.9 max
	// Lv. MAX: x1.35 max
	constexpr fixed_t rubberFastMax = FRACUNIT * 3 / 2;
	const fixed_t rubberFast = std::min<fixed_t>( rubberBase + rubberStretchiness, rubberFastMax );

	fixed_t rubberband = FRACUNIT >> 1;
	player_t *firstplace = nullptr;
	size_t i = SIZE_MAX;

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator)
		{
			continue;
		}

		// Don't rubberband to ourselves...
		if (player == &players[i])
		{
			continue;
		}

#if 0
		// Only rubberband up to players.
		if (players[i].bot)
		{
			continue;
		}
#endif

		if (firstplace == nullptr || players[i].distancetofinish < firstplace->distancetofinish)
		{
			firstplace = &players[i];
		}
	}

	if (firstplace != nullptr)
	{
		const uint32_t spacing = FixedDiv(10240 * mapobjectscale, K_GetKartGameSpeedScalar(gamespeed)) / FRACUNIT;
		const uint32_t wanteddist = firstplace->distancetofinish + K_BotRubberbandDistance(player);
		const int32_t distdiff = player->distancetofinish - wanteddist;

		rubberband = FixedDiv(distdiff + spacing, spacing * 2);

		if (player->boostpower < FRACUNIT)
		{
			// Do not let bots cheese offroad as much.
			rubberband = FixedMul(rubberband, player->boostpower);
		}

		if (P_MobjWasRemoved(player->mo) == false && player->mo->movefactor < FRACUNIT)
		{
			// Do not let bots speed up on ice too much.
			rubberband = FixedMul(rubberband, player->mo->movefactor);
		}

		if (rubberband > FRACUNIT)
		{
			rubberband = FRACUNIT;
		}
		else if (rubberband < 0)
		{
			rubberband = 0;
		}
	}

	uint32_t scaled_dist = player->distancetofinish;
	if (mapobjectscale != FRACUNIT)
	{
		// Bring back to normal scale.
		scaled_dist = FixedDiv(scaled_dist, mapobjectscale);
	}

	uint32_t END_DIST = 2048 * 14;

	if (scaled_dist < END_DIST)
	{
		// At the end of tracks, start slowing down.
		rubberband = FixedMul(rubberband, FixedDiv(scaled_dist, END_DIST));
	}

	return Easing_Linear(rubberband, rubberSlow, rubberFast);
}

/*--------------------------------------------------
	fixed_t K_UpdateRubberband(player_t *player)

		See header file for description.
--------------------------------------------------*/
fixed_t K_UpdateRubberband(player_t *player)
{
	fixed_t dest = K_BotRubberband(player);
	fixed_t ret = player->botvars.rubberband;

	// Ease into the new value.
	ret += (dest - player->botvars.rubberband) / 8;

	return ret;
}

/*--------------------------------------------------
	fixed_t K_DistanceOfLineFromPoint(fixed_t v1x, fixed_t v1y, fixed_t v2x, fixed_t v2y, fixed_t cx, fixed_t cy)

		See header file for description.
--------------------------------------------------*/
fixed_t K_DistanceOfLineFromPoint(fixed_t v1x, fixed_t v1y, fixed_t v2x, fixed_t v2y, fixed_t px, fixed_t py)
{
	// Copy+paste from P_ClosestPointOnLine :pensive:
	fixed_t startx = v1x;
	fixed_t starty = v1y;
	fixed_t dx = v2x - v1x;
	fixed_t dy = v2y - v1y;

	fixed_t cx, cy;
	fixed_t vx, vy;
	fixed_t magnitude;
	fixed_t t;

	cx = px - startx;
	cy = py - starty;

	vx = dx;
	vy = dy;

	magnitude = R_PointToDist2(v2x, v2y, startx, starty);
	vx = FixedDiv(vx, magnitude);
	vy = FixedDiv(vy, magnitude);

	t = (FixedMul(vx, cx) + FixedMul(vy, cy));

	vx = FixedMul(vx, t);
	vy = FixedMul(vy, t);

	return R_PointToDist2(px, py, startx + vx, starty + vy);
}

/*--------------------------------------------------
	static void K_GetBotWaypointRadius(waypoint_t *waypoint, fixed_t *smallestRadius, fixed_t *smallestScaled)

		Calculates a new waypoint radius size to use, making it
		thinner depending on how harsh the turn is.

	Input Arguments:-
		waypoint - Waypoint to retrieve the radius of.

	Return:-
		N/A
--------------------------------------------------*/
static void K_GetBotWaypointRadius(waypoint_t *const waypoint, fixed_t *smallestRadius, fixed_t *smallestScaled)
{
	static const fixed_t maxReduce = FRACUNIT/32;
	static const angle_t maxDelta = ANGLE_22h;

	fixed_t radius = waypoint->mobj->radius;
	fixed_t reduce = FRACUNIT;
	angle_t delta = 0;

	size_t i, j;

	for (i = 0; i < waypoint->numnextwaypoints; i++)
	{
		const waypoint_t *next = waypoint->nextwaypoints[i];
		const angle_t nextAngle = R_PointToAngle2(
			waypoint->mobj->x, waypoint->mobj->y,
			next->mobj->x, next->mobj->y
		);

		for (j = 0; j < waypoint->numprevwaypoints; j++)
		{
			const waypoint_t *prev = waypoint->prevwaypoints[j];
			const angle_t prevAngle = R_PointToAngle2(
				prev->mobj->x, prev->mobj->y,
				waypoint->mobj->x, waypoint->mobj->y
			);

			delta = std::max<angle_t>(delta, AngleDelta(nextAngle, prevAngle));
		}
	}

	if (delta > maxDelta)
	{
		delta = maxDelta;
	}

	reduce = FixedDiv(delta, maxDelta);
	reduce = FRACUNIT + FixedMul(reduce, maxReduce - FRACUNIT);

	*smallestRadius = std::min<fixed_t>(*smallestRadius, radius);
	*smallestScaled = std::min<fixed_t>(*smallestScaled, FixedMul(radius, reduce));
}

static fixed_t K_ScaleWPDistWithSlope(fixed_t disttonext, angle_t angletonext, const pslope_t *slope, int8_t flip)
{
	if (slope == nullptr)
	{
		return disttonext;
	}

	if ((slope->flags & SL_NOPHYSICS) == 0 && abs(slope->zdelta) >= FRACUNIT/21)
	{
		// Displace the prediction to go with the slope physics.
		fixed_t slopeMul = FRACUNIT;
		angle_t angle = angletonext - slope->xydirection;

		if (flip * slope->zdelta < 0)
		{
			angle ^= ANGLE_180;
		}

		// Going uphill: 0
		// Going downhill: FRACUNIT*2
		slopeMul = FRACUNIT + FINECOSINE(angle >> ANGLETOFINESHIFT);

		// Range: 0.25 to 1.75
		return FixedMul(disttonext, (FRACUNIT >> 2) + ((slopeMul * 3) >> 2));
	}

	return disttonext;
}

// Calculates a point further along the track to attempt to drive towards.
static dboolean K_CreateBotPrediction(botdata_t *bd, const player_t *player)
{
	ZoneScoped;

	const precise_t time = I_GetPreciseTime();

	const int16_t handling = K_GetKartTurnValue(player, KART_FULLTURN); // Reduce prediction based on how fast you can turn

	const tic_t futuresight = (TICRATE * KART_FULLTURN) / std::max<int16_t>(1, handling); // How far ahead into the future to try and predict
	const fixed_t speed = K_BotSpeedScaled(player, P_AproxDistance(player->mo->momx, player->mo->momy));

	const int32_t startDist = 0; //(DEFAULT_WAYPOINT_RADIUS * mapobjectscale) / FRACUNIT;
	const int32_t maxDist = (DEFAULT_WAYPOINT_RADIUS * 3 * mapobjectscale) / FRACUNIT; // This function gets very laggy when it goes far distances, and going too far isn't very helpful anyway.
	const int32_t distance = std::min<int32_t>(((speed / FRACUNIT) * static_cast<int32_t>(futuresight)) + startDist, maxDist);

	// Halves radius when encountering a wall on your way to your destination.
	fixed_t radReduce = FRACUNIT;

	fixed_t radius = INT32_MAX;
	fixed_t radiusScaled = INT32_MAX;

	int32_t distanceleft = distance;
	angle_t angletonext = ANGLE_MAX;
	int32_t disttonext = INT32_MAX;
	int32_t distscaled = INT32_MAX;
	pslope_t *nextslope = player->mo->standingslope;

	waypoint_t *wp = player->nextwaypoint;
	mobj_t *prevwpmobj = player->mo;

	const dboolean useshortcuts = K_BotCanTakeCut(player);
	const dboolean huntbackwards = false;
	dboolean pathfindsuccess = false;
	path_t pathtofinish = {0};

	size_t i;

	if (wp == nullptr || P_MobjWasRemoved(wp->mobj) == true)
	{
		// Can't do any of this if we don't have a waypoint.
		return false;
	}

	// Init defaults in case of pathfind failure
	angletonext = R_PointToAngle2(prevwpmobj->x, prevwpmobj->y, wp->mobj->x, wp->mobj->y);
	disttonext = P_AproxDistance(prevwpmobj->x - wp->mobj->x, prevwpmobj->y - wp->mobj->y);
	nextslope = wp->mobj->standingslope;
	distscaled = K_ScaleWPDistWithSlope(disttonext, angletonext, nextslope, P_MobjFlip(wp->mobj)) / FRACUNIT;

	pathfindsuccess = K_PathfindThruCircuit(
		wp, (unsigned)distanceleft,
		&pathtofinish,
		useshortcuts, huntbackwards
	);

	// Go through the waypoints until we've traveled the distance we wanted to predict ahead!
	if (pathfindsuccess == true)
	{
		for (i = 0; i < pathtofinish.numnodes; i++)
		{
			wp = (waypoint_t *)pathtofinish.array[i].nodedata;

			if (i == 0)
			{
				prevwpmobj = player->mo;
			}
			else
			{
				prevwpmobj = ((waypoint_t *)pathtofinish.array[ i - 1 ].nodedata)->mobj;
			}

			angletonext = R_PointToAngle2(prevwpmobj->x, prevwpmobj->y, wp->mobj->x, wp->mobj->y);
			disttonext = P_AproxDistance(prevwpmobj->x - wp->mobj->x, prevwpmobj->y - wp->mobj->y);
			nextslope = wp->mobj->standingslope;
			distscaled = K_ScaleWPDistWithSlope(disttonext, angletonext, nextslope, P_MobjFlip(wp->mobj)) / FRACUNIT;

			if (P_TraceBotTraversal(player->mo, wp->mobj) == false)
			{
				// If we can't get a direct path to this waypoint, reduce our prediction drastically.
				distscaled *= 4;
				radReduce = FRACUNIT >> 1;
			}

			K_GetBotWaypointRadius(wp, &radius, &radiusScaled);
			distanceleft -= distscaled;

			if (distanceleft <= 0)
			{
				// We're done!!
				break;
			}
		}

		Z_Free(pathtofinish.array);
	}

	// Set our predicted point's coordinates,
	// and use the smallest radius of all of the waypoints in the chain!
	bd->predict.x = wp->mobj->x;
	bd->predict.y = wp->mobj->y;

	bd->predict.baseRadius = radius;
	bd->predict.radius = FixedMul(radiusScaled, radReduce);

	// Set the prediction coordinates between the 2 waypoints if there's still distance left.
	if (distanceleft > 0)
	{
		// Scaled with the leftover anglemul!
		bd->predict.x += P_ReturnThrustX(nullptr, angletonext, std::min<fixed_t>(disttonext, distanceleft) * FRACUNIT);
		bd->predict.y += P_ReturnThrustY(nullptr, angletonext, std::min<fixed_t>(disttonext, distanceleft) * FRACUNIT);
	}

	ps_bots[player - players].prediction += I_GetPreciseTime() - time;
	return true;
}

/*--------------------------------------------------
	static void K_DrawPredictionDebug(botprediction_t *predict, const player_t *player)

		Draws objects to show where the viewpoint bot is trying to go.

	Input Arguments:-
		predict - The prediction to visualize.
		player - The bot player this prediction is for.

	Return:-
		None
--------------------------------------------------*/
static void K_DrawPredictionDebug(botdata_t *bd, const player_t *player)
{
	mobj_t *debugMobj = nullptr;
	angle_t sideAngle = ANGLE_MAX;
	uint8_t i = UINT8_MAX;

	I_Assert(player != nullptr);
	I_Assert(player->mo != nullptr && P_MobjWasRemoved(player->mo) == false);

	sideAngle = player->mo->angle + ANGLE_90;

	debugMobj = P_SpawnMobj(bd->predict.x, bd->predict.y, player->mo->z, MT_SPARK);
	P_SetMobjState(debugMobj, S_THOK);

	debugMobj->frame &= ~FF_TRANSMASK;
	debugMobj->frame |= FF_TRANS20|FF_FULLBRIGHT;

	debugMobj->color = SKINCOLOR_ORANGE;
	P_SetScale(debugMobj, debugMobj->destscale * 2);

	debugMobj->tics = 1;

	for (i = 0; i < 2; i++)
	{
		mobj_t *radiusMobj = nullptr;
		fixed_t radiusX = bd->predict.x, radiusY = bd->predict.y;

		if (i & 1)
		{
			radiusX -= FixedMul(bd->predict.radius, FINECOSINE(sideAngle >> ANGLETOFINESHIFT));
			radiusY -= FixedMul(bd->predict.radius, FINESINE(sideAngle >> ANGLETOFINESHIFT));
		}
		else
		{
			radiusX += FixedMul(bd->predict.radius, FINECOSINE(sideAngle >> ANGLETOFINESHIFT));
			radiusY += FixedMul(bd->predict.radius, FINESINE(sideAngle >> ANGLETOFINESHIFT));
		}

		radiusMobj = P_SpawnMobj(radiusX, radiusY, player->mo->z, MT_SPARK);
		P_SetMobjState(radiusMobj, S_THOK);

		radiusMobj->frame &= ~FF_TRANSMASK;
		radiusMobj->frame |= FF_TRANS20|FF_FULLBRIGHT;

		radiusMobj->color = SKINCOLOR_YELLOW;
		P_SetScale(debugMobj, debugMobj->destscale / 2);

		radiusMobj->tics = 1;
	}
}

// Calculates drift skill for a player based on ~~stats~~ difficulty.
static fixed_t K_BotDetermineDriftSkill(const player_t *player)
{
	return FRACUNIT/8 + (FRACUNIT * player->botvars.difficulty) / DIFFICULTBOT;
	//return ((FRACUNIT * (player->kartspeed + player->kartweight)) / 18);
}

static void K_WaypointGetDirectionVector(waypoint_t *wp1, waypoint_t *wp2, vector3_t *a_o)
{
	vector3_t v1, v2;

	v1.x = wp1->mobj->x;
	v1.y = wp1->mobj->y;
	v1.x = wp1->mobj->z;

	v2.x = wp2->mobj->x;
	v2.y = wp2->mobj->y;
	v2.x = wp2->mobj->z;

	FV3_SubEx(&v1,&v2,a_o);
	FV3_Normalize(a_o);
}

// Changes a bot's drift state.
// Resets the drift timer if the old and new state are different.
// If lockout is non-zero, apply drift lockout for this many tics.
void K_BotSetDriftState(const player_t *player, botdrift_e newstate, tic_t lockout)
{
	botdata_t *bd = &botdata[player - players];

	if (newstate != bd->driftstate)
	{
		bd->driftstate = newstate;
		bd->drifttime = 0;
	}

	if (lockout > bd->driftlockout)
		bd->driftlockout = lockout;
}

#define MINBOTDRIFT (KART_FULLTURN * 2) / 3 // 0.66

// Begins and ends "forced" drifts on a per-waypoint basis.
static void K_BotStartDrift(botdata_t *bd, const player_t* player)
{
	// Handle DRIFTING towards waypoints!
	dboolean shouldDrift;
	fixed_t botDriftSpeed;
	driftSetting_e driftsetting = DRIFT_NONE;
	fixed_t speedfactor = FixedDiv(player->speed, K_GetKartSpeed(player, false, false));

	if (cv_botdrifting.value == 0)
		return; // BOO YOU STINK

	if (speedfactor < FRACUNIT/2)
	{
		// don't bother if we're going too slow
		K_BotSetDriftState(player, DRIFTSTATE_AUTO, BOTDRIFTLOCKOUT);
		return;
	}

	if (speedfactor > 5*FRACUNIT/4)
	{
		// likewise, don't bother if we're going too fast
		K_BotSetDriftState(player, DRIFTSTATE_AUTO, BOTDRIFTLOCKOUT/2);
		return;
	}

	if (bd->driftlockout)
	{
		// things are not working out in our favor
		bd->driftlockout--;
		return;
	}

	// check for waypoints ahead of us with drift settings, based on our current speed
	path_t path = {0};
	int32_t maxdist = FixedMul(bd->driftmaxdist, speedfactor * (bd->driftstate == DRIFTSTATE_ACTIVE ? 1 : 2));

	if (maxdist >= 0 && K_PathfindThruCircuit(player->currentwaypoint, maxdist, &path, false, false))
	{
		for (size_t i = 0; i < path.numnodes; i++)
		{
			waypoint_t *wp =  static_cast<waypoint_t *>(path.array[i].nodedata);
			if (wp->driftsettings)
				driftsetting = static_cast<driftSetting_e>(wp->driftsettings);

			// don't break on DRIFT_END waypoints,
			// we could miss a drift waypoint right in front of it!
			if (driftsetting != DRIFT_NONE && driftsetting != DRIFT_END)
				break;
		}
		Z_Free(path.array);
	}

	if (driftsetting == DRIFT_NONE)
	{
		// No waypoints, nothing we can do here.
		return;
	}

	shouldDrift = false;
	botDriftSpeed = FixedMul(K_GetKartSpeed(player, false, false),
							 FixedPercentage(BOTDRIFTPERCENT));

	if (driftsetting == DRIFT_END)
	{
		if (bd->driftstate != DRIFTSTATE_AUTO)
			K_BotSetDriftState(player, DRIFTSTATE_ENDING, 0);
	}
	else if (driftsetting > DRIFT_NONE && driftsetting < DRIFT_END
	         && bd->driftstate == DRIFTSTATE_AUTO)
	{
		// Randomly decide to drift based on our skill at drifting,
		// and how fast we're moving.
		fixed_t driftpotential = M_RandomKey(MAXDRIFTSKILL);

		if ((driftpotential <= bd->driftskill) &&
			(botDriftSpeed <= player->speed))
		{
			shouldDrift = true;
		}

		if (shouldDrift)
		{
			// Start our drift based on the waypoint's drift settings.
			int8_t driftturn = 0;

			switch (driftsetting)
			{
			case DRIFT_PWRSLIDE_L:
				driftturn = -2;
				break;
			case DRIFT_LEFT:
				driftturn = -1;
				break;
			case DRIFT_PWRSLIDE_R:
				driftturn = 2;
				break;
			case DRIFT_RIGHT:
				driftturn = 1;
				break;
			default:
				break;
			}

			bd->driftturn = driftturn;
			K_BotSetDriftState(player, DRIFTSTATE_STARTING, 0);
		}
	}
}

// Determines inputs for standard track driving.
static int32_t K_HandleBotTrack(botdata_t *bd, const player_t *player, angle_t destangle)
{
	ZoneScoped;

	// Handle steering towards waypoints!
	int32_t turnamt = 0;
	int8_t turnsign = 0;
	angle_t moveangle;
	int32_t anglediff, anglediff2;
	fixed_t speedfactor = FixedDiv(player->speed, K_GetKartSpeed(player, false, false));
	dboolean onground = P_IsObjectOnGround(player->mo);

	moveangle = player->mo->angle;
	anglediff = AngleDeltaSigned(moveangle, destangle);

	bd->predictionerror = std::min(destangle - moveangle, moveangle - destangle);

	// line up for an incoming drift
	if (bd->driftstate == DRIFTSTATE_STARTING)
	{
		anglediff += FixedMul(ANG10-ANG2, speedfactor) * bd->driftturn;
	}

	if (anglediff < 0)
	{
		turnsign = 1;
	}
	else
	{
		turnsign = -1;
	}

	anglediff2 = anglediff;
	anglediff = abs(anglediff);
	turnamt = KART_FULLTURN * turnsign;

	if (anglediff > ANGLE_67h)
	{
		// Wrong way!
		if (onground)
		{
			bd->acceldown = false;
			bd->brakedown = true;
		}
	}
	else
	{
		const fixed_t playerwidth = (player->mo->radius * 2);
		fixed_t realrad = bd->predict.radius*3/4; // Remove a "safe" distance away from the edges of the road
		fixed_t rad = realrad;
		fixed_t dirdist = K_DistanceOfLineFromPoint(
			player->mo->x, player->mo->y,
			player->mo->x + FINECOSINE(moveangle >> ANGLETOFINESHIFT), player->mo->y + FINESINE(moveangle >> ANGLETOFINESHIFT),
			bd->predict.x, bd->predict.y
		);

		if (realrad < playerwidth)
		{
			realrad = playerwidth;
		}

		// Become more precise based on how hard you need to turn
		// This makes predictions into turns a little nicer
		// Facing 90 degrees away from the predicted point gives you 0 radius
		rad = FixedMul(rad,
			FixedDiv(std::max<angle_t>(0, ANGLE_90 - anglediff), ANGLE_90)
		);

		// Become more precise the slower you're moving
		// Also helps with turns
		// Full speed uses full radius
		rad = FixedMul(rad,
			FixedDiv(K_BotSpeedScaled(player, player->speed), K_GetKartSpeed(player, false, false))
		);

		// Cap the radius to reasonable bounds
		if (rad > realrad)
		{
			rad = realrad;
		}
		else if (rad < playerwidth)
		{
			rad = playerwidth;
		}

		// Full speed ahead!
		bd->acceldown = true;
		bd->brakedown = false;

		// Additional grip for turns.
		if (player->mo && onground && (player->speed > K_GetKartSpeed(player, false, false)/2) && !bd->griplockout)//35*FRACUNIT
		{
			const angle_t MAXERROR = ANGLE_45;
			const angle_t MIDERROR = ANGLE_22h;

			if (bd->predictionerror > MAXERROR && bd->driftstate == DRIFTSTATE_AUTO)
			{
				bd->acceldown = false;
				bd->brakedown = true;
			}
			else if (bd->predictionerror > MIDERROR && bd->driftstate == DRIFTSTATE_AUTO)
			{
				bd->brakedown = true;
			}
		}

		if (bd->griplockout > 0)
			bd->griplockout--;

		if (dirdist <= rad
		 && bd->driftstate != DRIFTSTATE_STARTING) // steer towards waypoints when starting drift
		{
			// Going the right way, don't turn at all.
			turnamt = 0;
		}

		// 0.5 on Easy, 1.0 on Normal, 1.5 on Hard.
		//int32_t mindriftamt = FixedMul(MINBOTDRIFT * (cv_kartspeed.value + 1), 2 * FRACUNIT);

		// Start or continue a drift.
		if (bd->driftstate == DRIFTSTATE_ACTIVE || bd->driftstate == DRIFTSTATE_ENDING)
		{
			bd->driftdown = true;

			fixed_t angofs = K_GetKartSpeedFromStat(5 - (player->kartspeed - 5), false, player->kartmagnet.factor) * -bd->driftturn;

			// adjust for speed
			angofs = FixedMul(angofs, speedfactor - (2-gamespeed)*FRACUNIT/4);

			fixed_t driftpower = angofs - FixedDiv(anglediff2, ANG1);

			// arbitrary divider on the final driftpower
			driftpower /= bd->driftpowerdiv;

			// brakedrift if we're steering too hard
			if (abs(driftpower) >= FRACUNIT)
				bd->brakedown = true;

			// get the raw turn value and "invert" it (higher weight needs harder steering!)
			int16_t turnvalue = abs(K_GetKartTurnValue(player, KART_FULLTURN * (bd->driftturn < 0 ? 1 : -1)));
			turnvalue = 541 - (turnvalue - 541); // weight 5 = 541

			turnamt = std::clamp(FixedMul(driftpower, turnvalue), -KART_FULLTURN, KART_FULLTURN);

			bd->griplockout = 6;
		}
		/*
		else if ((turnamt) && (bd->driftstate == DRIFTSTATE_AUTO) &&
			 (turnpower > FixedPercentage(DRIFTSTARTPCT)))
		{
			// TODO: Figure out a drift prediction system.
		}
		*/
	}

	return turnamt;
}

#undef MINBOTDRIFT

static void K_IncrementBotRespawn(const player_t *player, uint8_t *respawn, const uint8_t respawnmax)
{
	const fixed_t requireDist = (12*player->mo->scale) / FRACUNIT;
	int32_t progress = player->distancetofinishprev - player->distancetofinish;
	dboolean exceptions = (
		(leveltime < starttime)
		|| player->flashing != 0
		|| player->spinouttimer != 0
		|| player->crash_state != 0
		|| player->airtime > 3*TICRATE/2
		|| (player->justbumped > 0 && player->justbumped < bumptime-1)
	);

	if (!exceptions && (progress < requireDist))
	{
		if (*respawn < respawnmax)
		{
			// Making no progress, start counting against you.
			*respawn = *respawn + 1;
			if (progress < -requireDist && *respawn < respawnmax)
			{
				// Making NEGATIVE progress? Start counting even harder.
				*respawn = *respawn + 1;
			}
		}
	}
	else if (*respawn > 0)
	{
		// Playing normally.
		*respawn = *respawn - 1;
	}
}

/*--------------------------------------------------
	static int32_t K_HandleBotReverse(const player_t *player, ticcmd_t *cmd, botprediction_t *predict)

		Determines inputs for reversing.

	Input Arguments:-
		player - Player to generate the ticcmd for.
		cmd - The player's ticcmd to modify.
		predict - Pointer to the bot's prediction.

	Return:-
		New value for turn amount.
--------------------------------------------------*/
/*static int32_t K_HandleBotReverse(const player_t *player, ticcmd_t *cmd, botprediction_t *predict, angle_t destangle)
{
	ZoneScoped;

	// Handle steering towards waypoints!
	int32_t turnamt = 0;
	int8_t turnsign = 0;
	angle_t moveangle, angle;
	int16_t anglediff, momdiff;

	if (predict != nullptr)
	{
		// TODO: Should we reverse through bot controllers?
		return K_HandleBotTrack(player, cmd, predict, destangle);
	}

	if (player->nextwaypoint == nullptr
		|| player->nextwaypoint->mobj == nullptr
		|| P_MobjWasRemoved(player->nextwaypoint->mobj))
	{
		// No data available...
		return 0;
	}

	if ((player->nextwaypoint->prevwaypoints != nullptr)
		&& (player->nextwaypoint->numprevwaypoints > 0U))
	{
		size_t i;
		for (i = 0U; i < player->nextwaypoint->numprevwaypoints; i++)
		{
			if (!K_GetWaypointIsEnabled(player->nextwaypoint->prevwaypoints[i]))
			{
				continue;
			}

			destangle = R_PointToAngle2(
				player->nextwaypoint->prevwaypoints[i]->mobj->x, player->nextwaypoint->prevwaypoints[i]->mobj->y,
				player->nextwaypoint->mobj->x, player->nextwaypoint->mobj->y
			);

			break;
		}
	}

	// Calculate turn direction first.
	moveangle = player->mo->angle;
	angle = (moveangle - destangle);

	if (angle < ANGLE_180)
	{
		turnsign = -1; // Turn right
		anglediff = AngleFixed(angle)>>FRACBITS;
	}
	else
	{
		turnsign = 1; // Turn left
		anglediff = 360-(AngleFixed(angle)>>FRACBITS);
	}

	anglediff = abs(anglediff);
	turnamt = KART_FULLTURN * turnsign;

	// Now calculate momentum
	momdiff = 180;
	if (player->speed > player->mo->scale)
	{
		momdiff = 0;
		moveangle = K_MomentumAngle(player->mo);
		angle = (moveangle - destangle);

		if (angle < ANGLE_180)
		{
			momdiff = AngleFixed(angle)>>FRACBITS;
		}
		else
		{
			momdiff = 360-(AngleFixed(angle)>>FRACBITS);
		}

		momdiff = abs(momdiff);
	}

	if (anglediff > 90 || momdiff < 90)
	{
		// We're not facing the track,
		// or we're going too fast.
		// Let's E-Brake.
		cmd->forwardmove = 0;
		cmd->buttons |= BT_ACCELERATE|BT_BRAKE;
	}
	else
	{
		fixed_t slopeMul = FRACUNIT;

		if (player->mo->standingslope != nullptr)
		{
			const pslope_t *slope = player->mo->standingslope;

			if (!(slope->flags & SL_NOPHYSICS) && abs(slope->zdelta) >= FRACUNIT/21)
			{
				angle_t sangle = player->mo->angle - slope->xydirection;

				if (P_MobjFlip(player->mo) * slope->zdelta < 0)
					sangle ^= ANGLE_180;

				slopeMul = FRACUNIT - FINECOSINE(sangle >> ANGLETOFINESHIFT);
			}
		}

#define STEEP_SLOPE (FRACUNIT*11/10)
		if (slopeMul > STEEP_SLOPE)
		{
			// Slope is too steep to reverse -- EBrake.
			cmd->forwardmove = 0;
			cmd->buttons |= BT_ACCELERATE|BT_BRAKE;
		}
		else
		{
			cmd->forwardmove = -MAXPLMOVE;
			cmd->buttons |= BT_BRAKE; //|BT_LOOKBACK
		}
#undef STEEP_SLOPE

		if (anglediff < 10)
		{
			turnamt = 0;
		}
	}

	return turnamt;
}*/

// updates server-sided bot logic
void K_BotTicker(const player_t *player)
{
	angle_t destangle = 0;
	int32_t turnamt = 0;

	botdata_t *bd = &botdata[player - players];
	bd->itemwasdown = bd->itemdown;
	bd->acceldown = bd->brakedown = bd->driftdown = bd->itemdown = false;
	bd->dolookback = false;
	bd->itemthrow = 0;
	dboolean onground = P_IsObjectOnGround(player->mo);

	if (!(gametypes[gametype]->rules & GTR_BOTS) // No bot behaviors
		|| K_GetNumWaypoints() == 0 // No waypoints
		|| leveltime <= introtime // During intro camera
		|| player->playerstate == PST_DEAD // Dead, respawning.
		|| player->mo->scale <= 1 // Post-finish "death" animation
		|| player->spectator) // spectating
	{
		// No need to do anything else.
		return;
	}

	if (player->exiting && player->nextwaypoint == K_GetFinishLineWaypoint() && ((mapheaderinfo[gamemap - 1]->levelflags & LF_SECTIONRACE) == LF_SECTIONRACE))
	{
		// Sprint map finish, don't give Sal's children migraines trying to pathfind out
		return;
	}

	// Defanging bots for testing.
	if (!cv_botcontrol.value)
		return;

	// Actual gameplay behaviors below this block!
	const botcontroller_t *botController = K_GetBotController(player->mo);

	if (botController != nullptr && (botController->flags & TMBOT_NOCONTROL) == TMBOT_NOCONTROL)
	{
		// Disable bot controls entirely.
		return;
	}

	if (player->exiting)
	{
		//Bot finish
		// TODO: Make bots spin around like a player would based on random chance
		return;
	}

	if (onground)
	{
		// Is a bot not making any progress? Kill it and respawn at next waypoint.
		K_IncrementBotRespawn(player, &bd->respawnconfirm, BOTRESPAWNCONFIRM);

		if (bd->respawnconfirm > BOTRESPAWNCONFIRM - TICRATE)
		{
			// We want to respawn. Simply hold brake and stop here!
			bd->acceldown = false;
			K_BotSetDriftState(player, DRIFTSTATE_AUTO, TICRATE);
			if (player->speed > 0)
				bd->brakedown = true;

			if (bd->respawnconfirm >= BOTRESPAWNCONFIRM || player->speed < 10*FRACUNIT)
			{
				// WHAT ARE YOU DOING??? RACE ALREADY!
				// ...
				// FINE, I'LL SEND A RESPAWN COMMAND ON YOUR BEHALF!
				char buf[1], *cp = buf;
				WRITEUINT8(cp, player - players);
				SendNetXCmdForPlayer(consoleplayer, XD_RESPAWN, buf, sizeof(buf));
			}
			return;
		}
	}

	destangle = player->mo->angle;

	// it's time to start predicting
	memset(&bd->predict, 0, sizeof(bd->predict));

	dboolean forcedDir = false;
	if (botController != nullptr && (botController->flags & TMBOT_FORCEDIR) == TMBOT_FORCEDIR)
	{
		const fixed_t dist = DEFAULT_WAYPOINT_RADIUS * player->mo->scale;

		// Overwritten prediction
		bd->predict = {
			.x = player->mo->x + FixedMul(dist, FINECOSINE(botController->forceAngle >> ANGLETOFINESHIFT)),
			.y = player->mo->y + FixedMul(dist, FINESINE(botController->forceAngle >> ANGLETOFINESHIFT)),
			.radius = (DEFAULT_WAYPOINT_RADIUS / 4) * mapobjectscale,
		};

		forcedDir = true;
	}

	if (!onground)
	{
		if (player->airdroptime == 0
			&& K_AirDropActive()
			&& !P_PlayerInPain(player)
			&& !player->loop.radius
			&& !(player->mo->tracer && player->mo->tracer->type == MT_TUBEWAYPOINT)
			&& !player->respawn)
		{
			if (botController != nullptr && (botController->flags & TMBOT_AIRDROP) == TMBOT_AIRDROP)
			{
				// Air Drop!
				bd->brakedown = true;
				return;
			}
		}

		//if (!player->pogospring && !player->respawn)
			//return; // Don't allow bots to turn in the air.
	}

	if (forcedDir == true)
	{
		destangle = R_PointToAngle2(player->mo->x, player->mo->y, bd->predict.x, bd->predict.y);
		turnamt = K_HandleBotTrack(bd, player, destangle);
	}
	else if (leveltime <= starttime)
	{
		uint8_t timing = M_RandomRange(0, 5);
		uint8_t finaltiming = (MAXBOTDIFFICULTY/2)-(player->botvars.difficulty/2)+timing;
		if (player->botvars.difficulty > 4)
		{
			if (leveltime >= starttime-TICRATE-TICRATE/7+finaltiming)
				bd->acceldown = true;
		}
	}
	else
	{
		// Create a prediction.
		if (K_CreateBotPrediction(bd, player))
		{
			K_NudgePredictionTowardsObjects(bd, player);
			destangle = R_PointToAngle2(player->mo->x, player->mo->y, bd->predict.x, bd->predict.y);
			turnamt = K_HandleBotTrack(bd, player, destangle);
		}
	}

	if (turnamt > KART_FULLTURN)
	{
		turnamt = KART_FULLTURN;
	}
	else if (turnamt < -KART_FULLTURN)
	{
		turnamt = -KART_FULLTURN;
	}

	bd->turnamt = turnamt;

	if (player->exiting == 0)
	{
		// TODO: Allowing projectile items like orbinaut while e-braking would be nice, maybe just pass in the spindash variable?
		precise_t t = I_GetPreciseTime();
		K_BotItemUsage(bd, player);
		ps_bots[player - players].item = I_GetPreciseTime() - t;
	}

	// Update turning quicker if we're moving at high speeds.
	uint8_t turndelta = (player->speed > (7 * K_GetKartSpeed(player, false, false) / 4)) ? 2 : 1;

	if (turnamt > 0)
	{
		// Count up
		if (bd->turnconfirm < BOTTURNCONFIRM)
		{
			bd->turnconfirm += turndelta;
		}
	}
	else if (turnamt < 0)
	{
		// Count down
		if (bd->turnconfirm > -BOTTURNCONFIRM)
		{
			bd->turnconfirm -= turndelta;
		}
	}
	else
	{
		// Back to neutral
		if (bd->turnconfirm < 0)
		{
			bd->turnconfirm += turndelta;
		}
		else if (bd->turnconfirm > 0)
		{
			bd->turnconfirm -= turndelta;
		}
	}

	if (!bd->acceldown && bd->brakedown)
	{
		// stop drifting if we're reversing
		K_BotSetDriftState(player, DRIFTSTATE_AUTO, BOTDRIFTLOCKOUT);
	}
	else
	{
		// Figure out if we need to drift.
		// Drift-ending waypoints will kill the drift timer,
		// so no need to worry about doing that ourselves.
		K_BotStartDrift(bd, player);

		int32_t limit = bd->driftstatedelay;
		int32_t dtime = ++bd->drifttime;

		// the faster we are going, the sooner we need to drift
		fixed_t speedfactor = FixedDiv(player->speed, K_GetKartSpeed(player, false, false));

		switch (bd->driftstate)
		{
		case DRIFTSTATE_STARTING:
			limit = std::max(0, limit - FixedMul(TICRATE/5, speedfactor));
			if (dtime > limit)
			{
				K_BotSetDriftState(player, DRIFTSTATE_ACTIVE, 0);
			}
			break;

		case DRIFTSTATE_ENDING:
			limit = std::max(0, limit - FixedMul(TICRATE/5, speedfactor));
			if (dtime > limit)
			{
				K_BotSetDriftState(player, DRIFTSTATE_AUTO, 0);
			}
			break;

		default:
			break;
		}
	}

	if (cv_kartdebugbot.value != 0 && player - players == displayplayers[0] && !(paused || P_AutoPause()))
	{
		K_DrawPredictionDebug(bd, player);
	}
}

/*--------------------------------------------------
	void K_BuildBotTiccmd(player_t *player, ticcmd_t *cmd)

		See header file for description.
--------------------------------------------------*/
void K_BuildBotTiccmd(player_t *player, ticcmd_t *cmd)
{
	ZoneScoped;

	botdata_t *bd = &botdata[player - players];

	// Remove any existing controls
	memset(cmd, 0, sizeof(ticcmd_t));

	if (player->mo == nullptr
		|| player->spectator == true
		|| G_GamestateUsesLevel() == false)
	{
		// Not in the level.
		return;
	}

	cmd->angle = player->mo->angle >> TICCMD_REDUCE;

	// Complete override of all ticcmd functionality.
	// May add more hooks to individual pieces of bot ticcmd,
	// but this should always be here so anyone can roll
	// their own :)
	if (LUA_HookTiccmd(player, cmd, HOOK(BotTiccmd)) == true)
		return;

	if (player->botvars.style == BOT_STYLE_STAY)
	{
		// Hey, this one's pretty easy :P
		return;
	}

	// BOT_STYLE_NORMAL is the only other style, so...
	int8_t maxacceldown = (!!bd->acceldown * MAXPLMOVE);
	int8_t maxbrakedown = !!bd->brakedown * MAXPLMOVE/2;

	cmd->forwardmove = maxacceldown - maxbrakedown;
	if (bd->itemthrow != 0)
	{
		cmd->throwdir = bd->itemthrow * KART_FULLTURN;
		cmd->buttons |= bd->itemthrow > 0 ? BT_FORWARD : BT_BACKWARD;
	}

	if (bd->acceldown)
		cmd->buttons |= BT_ACCELERATE;
	if (bd->brakedown)
		cmd->buttons |= BT_BRAKE;
	if (bd->driftdown)
		cmd->buttons |= BT_DRIFT;
	if (bd->itemdown)
		cmd->buttons |= BT_ATTACK;

	if (bd->dolookback)
		cmd->buttons |= BT_LOOKBACK;

	if (abs(bd->turnconfirm) >= BOTTURNCONFIRM)
	{
		// You're commiting to your turn, you're allowed!
		cmd->turning = bd->turnamt;
		if (P_CanPlayerTurn(player, cmd))
			cmd->angle += K_GetKartTurnValue(player, bd->turnamt);
	}
}

/*--------------------------------------------------
	void K_UpdateBotGameplayVars(player_t *player);

		See header file for description.
--------------------------------------------------*/
void K_UpdateBotGameplayVars(player_t *player)
{
	if (gamestate != GS_LEVEL || !player->mo)
	{
		// Not in the level.
		return;
	}

	player->botvars.rubberband = K_UpdateRubberband(player);

	K_UpdateBotGameplayVarsItemUsage(player);
}

// resets some botdata stuff after respawning
void K_BotReborn(const player_t *player)
{
	botdata_t *bd = &botdata[player - players];
	memset(bd, 0, sizeof(*bd));

	bd->driftstate = DRIFTSTATE_AUTO;
	bd->driftskill = FixedMul(MAXDRIFTSKILL, K_BotDetermineDriftSkill(player));

	// drift parameters, just fixed values for now
	bd->driftmaxdist = 300;
	bd->driftpowerdiv = 40;
	bd->driftstatedelay = TICRATE/5;
}

void K_BotResetItemConfirm(const player_t *player, dboolean setdelay)
{
	botdata_t *bd = &botdata[player - players];
	bd->itemconfirm = 0;
	if (setdelay)
		bd->itemdelay = TICRATE;
}

botdata_t *K_GetBotData(uint8_t num)
{
	if (num < 0 || num >= MAXPLAYERS)
		return NULL;

	return &botdata[num];
}
