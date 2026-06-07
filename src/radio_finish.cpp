// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2024 by $HOME
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  radio_finish.cpp
/// \brief Functions related to the Sonic Riders-esque finish ticker.

#include <math.h>
#include <string>
#include <deque>

#include "command.h"
#include "g_game.h" // for player_names
#include "r_draw.h"
#include "k_hud.h"
#include "v_video.h"
#include "p_local.h"
#include "radio_finish.h"
#include "r_fps.h" // Z_Realloc

consvar_t cv_show_riders_finish_ticker = CVAR_INIT ("ridersfinishticker", "On", CV_SAVE|CV_CALL|CV_NOINIT, CV_OnOff, KartFinishLineTicker_OnChange);

void KartFinishLineTicker_OnChange(void)
{
	// Immediately empty the queue
	if (!cv_show_riders_finish_ticker.value)
		RR_resetRidersFinishTicker();
}

struct PlayerFinishTicker {
	std::string position;
	boolean is_local_player;
	fixed_t x;
	fixed_t old_x;
};

static std::deque<PlayerFinishTicker> playerFinishTickerQueue;
static boolean drawLapFlagAtStart = false;

#define offscreen_offset() \
	(vid.scaledwidth - BASEVIDWIDTH)/ 2
#define offscreen_right_offset() \
	BASEVIDWIDTH + (offscreen_offset()) + 2

/*
* Empty the queue!
*/
void RR_resetRidersFinishTicker(void)
{
	playerFinishTickerQueue.clear();
	drawLapFlagAtStart = true;
}

void RR_addPlayerToFinshTicker(player_t *player)
{
	// https://www.mathsisfun.com/numbers/cardinal-ordinal-chart.html
	auto position_string = [](UINT8 position) -> std::string
	{
		if (position % 10 == 1 && position % 100 != 11)
			return std::to_string(position) + "st";
		if (position % 10 == 2 && position % 100 != 12)
			return std::to_string(position) + "nd";
		if (position % 10 == 3 && position % 100 != 13)
			return std::to_string(position) + "rd";

		return std::to_string(position) + "th";
	};

	/*
	* TODO:
	* Rare spectate case (just check player flags)
	*/
	playerFinishTickerQueue.push_back(
		{
			M_GetText(va("%s \x86%s", position_string(player->position).c_str(), player_names[player-players])),
			P_IsMachineLocalPlayer(player),
			(offscreen_right_offset()) * FRACUNIT, // Start just off-screen to the right,
			(offscreen_right_offset()) * FRACUNIT// Interpolation (thanks to NepDisk for bringing this up)
		}
	);
}

void RR_ridersFinishTick(void)
{
	size_t i;

	if (playerFinishTickerQueue.empty() || !cv_show_riders_finish_ticker.value) return;

	// Move over to the left
	playerFinishTickerQueue.front().old_x = playerFinishTickerQueue.front().x;
	playerFinishTickerQueue.front().x -= (2*FRACUNIT);

	auto string_width = [](std::string string) -> INT32
	{
		return V_ThinStringWidth(string.c_str(), 0);
	};

	/*
	* Once the player at the front of the queue passes
	* the middle of the screen, start drawing the next player who finished.
	*/
	for (i = 1; i < playerFinishTickerQueue.size(); ++i)
	{
		const fixed_t padding = (
			string_width(playerFinishTickerQueue[i-1].position) + 25
		) * FRACUNIT;

		if (playerFinishTickerQueue[i].x - playerFinishTickerQueue[i-1].x >= padding)
		{
			playerFinishTickerQueue[i].old_x = playerFinishTickerQueue[i].x;
			playerFinishTickerQueue[i].x -= (2*FRACUNIT);
		}
	}

	const INT32 OFFSCREEN_X = 0 - offscreen_offset() - string_width(
		playerFinishTickerQueue.front().position.c_str()
	);

	// Once the player at the front of the queue is offscreen to the left, pop them
	if (playerFinishTickerQueue.front().x <= OFFSCREEN_X*FRACUNIT)
	{
		playerFinishTickerQueue.pop_front();

		if (drawLapFlagAtStart)
			drawLapFlagAtStart = false;
	}
}

static const int FINISH_TICKER_Y = 35;

/*
* Draw the finish line ticker, like in Sonic Riders.
* You know...
*/

void RR_drawRidersFinishTicker(void)
{
	size_t i;

	if (playerFinishTickerQueue.empty() || !cv_show_riders_finish_ticker.value) return;

	for (i = 0; i < playerFinishTickerQueue.size(); ++i)
	{
		PlayerFinishTicker &player = playerFinishTickerQueue[i];

		INT32 flags = V_20TRANS;

		if (player.is_local_player)
		{
			flags = V_YELLOWMAP|V_10TRANS;
		}

		fixed_t x = R_InterpolateFixed(player.old_x, player.x);

		if (i == 0 && drawLapFlagAtStart)
		{
			V_DrawFixedPatch(
				x - (15 * FRACUNIT),
				FINISH_TICKER_Y * FRACUNIT,
				FRACUNIT,
				V_SNAPTOTOP,
				kp_splitlapflag,
				NULL
			);
		}

		V_DrawStringScaledEx(
			x,
			FINISH_TICKER_Y * FRACUNIT,
			FRACUNIT,
			FRACUNIT,
			FRACUNIT,
			FRACUNIT,
			flags|V_SNAPTOTOP|V_ALLOWLOWERCASE,
			NULL,
			TINY_FONT,
			player.position.c_str()
		);
	}
}
