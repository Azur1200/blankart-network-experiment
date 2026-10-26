// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2024 by Kart Krew.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  k_hud.c
/// \brief HUD message drawing functions

#include <algorithm>
#include <array>
#include <vector>
#include <deque>

#include "v_draw.hpp"

#include "k_hud.h"

#include "k_hud_message.h"

#include "g_party.h"

#include "k_bot.h"

#include "cxxutil.hpp"

typedef enum
{
	MM_IN,
	MM_HOLD,
	MM_OUT,
} messagemode_t;

typedef struct
{
	std::string text;
	sfxenum_t sound;
	dboolean background;
} message_t;

struct messagestate_t
{
	std::deque<message_t> messages;
	std::string objective = "";
	tic_t timer = 0;
	dboolean persist = false;
	messagemode_t mode = MM_IN;
	const tic_t speedyswitch = 2*TICRATE;
	const tic_t lazyswitch = 4*TICRATE;

	void add(message_t msg)
	{
		messages.push_back(msg);
	}

	void clear()
	{
		messages.clear();
		switch_mode(MM_IN);
	}

	void switch_mode(messagemode_t nextmode)
	{
		mode = nextmode;
		timer = 0;
	}

	void tick()
	{
		if (messages.size() == 0)
		{
			if (!objective.empty())
				restore();
			else
				return;
		}

		if (exitcountdown)
			return;

		if (timer == 0 && mode == MM_IN)
			S_StartSound(NULL, sfx_s3k47);

		timer++;

		switch (mode)
		{
			case MM_IN:
				if (timer > messages[0].text.length())
					switch_mode(MM_HOLD);
				break;
			case MM_HOLD:
				if (messages.size() > 1 && timer > speedyswitch) // Waiting message, switch to it right away!
					next();
				else if (timer > lazyswitch && !persist) // If there's no pending message, we can chill for a bit.
					switch_mode(MM_OUT);
				break;
			case MM_OUT:
				if (timer > messages[0].text.length())
					next();
				break;
		}
	}

	void restore()
	{
		switch_mode(MM_IN);
		persist = true;
		messages.clear();
		messages.push_front({.text = objective});
	}

	void next()
	{
		switch_mode(MM_IN);
		persist = false;
		if (messages.size() > 0)
			messages.pop_front();
	}

};

static std::vector<messagestate_t> messagestates{MAXSPLITSCREENPLAYERS};

void K_AddMessage(const char *msg, dboolean interrupt, dboolean persist, dboolean hidebackground)
{
	for (auto &state : messagestates)
	{
		if (interrupt)
			state.clear();

		if (persist)
			state.objective = msg;
		else
			state.add({.text = msg, .background = !hidebackground});
	}
}

void K_ClearPersistentMessages()
{
	for (auto &state : messagestates)
	{
		state.objective = "";
		state.clear();
	}
}

// Return value can be used for "paired" splitscreen messages, true = was displayed
void K_AddMessageForPlayer(player_t *player, const char *msg, dboolean interrupt, dboolean persist, dboolean hidebackground)
{
	if (!player)
		return;

	if (player && !P_IsDisplayPlayer(player))
		return;

	if (player && K_PlayerUsesBotMovement(player))
		return;

	messagestate_t *state = &messagestates[G_PartyPosition(player - players)];

	if (interrupt)
		state->clear();

	if (persist)
		state->objective = msg;
	else
		state->add({.text = msg, .background = !hidebackground});
}

void K_ClearPersistentMessageForPlayer(player_t *player)
{
	if (!player)
		return;

	if (player && !P_IsDisplayPlayer(player))
		return;

	messagestate_t *state = &messagestates[G_PartyPosition(player - players)];
	state->objective = "";
}

void K_AddCEchoMessage(char* msg)
{
	// Convert \\ into /n
	for (int32_t i = 0; msg[i] != '\0'; ++i)
		if (msg[i] == '\\')
			msg[i] = '\n';

	K_AddMessage(msg, true, false, true);
}

void K_TickMessages(void)
{
	for (auto &state : messagestates)
	{
		state.tick();
	}
}

void K_DrawMessageFeed(void)
{
	int i;

	if (exitcountdown)
		return;

	for (i = 0; i <= r_splitscreen; i++)
	{
		messagestate_t state = messagestates[i];

		if (state.messages.size() == 0)
			continue;

		std::string msg = state.messages[0].text;
		dboolean background = state.messages[0].background;

		size_t sublen = state.timer;
		size_t messagelength = srb2::messagelength(msg);
		if (state.mode == MM_IN)
			sublen = state.timer;
		else if (state.mode == MM_HOLD)
			sublen = messagelength;
		else if (state.mode == MM_OUT)
			sublen = state.timer > messagelength ? 0 : messagelength - state.timer;

		std::string submsg = srb2::messagesubstring(msg, 0, sublen);

		using srb2::Draw;

		Draw::TextElement text(submsg);

		text.font(Draw::Font::kThin);
		text.flags(V_ALLOWLOWERCASE|V_6WIDTHSPACE|V_USETEXTTAG);

		uint8_t x = 160;
		uint8_t y = 10;
		int8_t shift = 0;
		if (r_splitscreen >= 2)
		{
			text.font(Draw::Font::kThin);
			shift = -2;

			x = BASEVIDWIDTH/4;
			y = 5;

			if (i % 2)
				x += BASEVIDWIDTH/2;

			if (i >= 2)
				y += BASEVIDHEIGHT / 2;
		}
		else if (r_splitscreen >= 1)
		{
			y = 5;

			if (i >= 1)
				y += BASEVIDHEIGHT / 2;
		}
		uint16_t sw = text.width();

		if (background)
			K_DrawSticker(x - sw/2, y, sw, 0, true);
		Draw(x, y+shift).align(Draw::Align::kCenter).flags(V_ALLOWLOWERCASE|V_6WIDTHSPACE|V_USETEXTTAG).text(text);
	}
}
