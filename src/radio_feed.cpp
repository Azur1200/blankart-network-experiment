// BLANKART
//-----------------------------------------------------------------------------
// Copyright (C) 2025 by $HOME
// Copyright (C) 2026 by Team BlanKart
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file radio_feed.cpp
/// \brief Hardcode port of killfeed Lua script from ~2018
/// \brief https://github.com/blondedradio/RadioRacers/issues/21

#include <algorithm>
#include <array>
#include <vector>
#include <deque>
#include <string>
#include <utility>

#include <iostream>
#include <memory>

#include "radio_feed.hpp"
#include "doomstat.h"
#include "i_video.h"
#include "i_time.h"
#include "d_player.h"
#include "doomtype.h"
#include "p_local.h"
#include "p_tick.h"
#include "m_easing.h"
#include "info.h"
#include "v_video.h"
#include "st_stuff.h"
#include "command.h"
#include "m_random.h"
#include "k_objects.h"
#include "k_hud.h"
#include "v_draw.hpp" // srb2:Draw

using srb2::Draw;
const BaseFeedUpdate::PatchManager BaseFeedUpdate::patches_;
static dboolean has_feed_been_initialized = false;

// Hudfeed
void RR_Hudfeed_OnChange(void)
{
	if (!cv_hudfeed_enabled.value)
		// Being turned off, clear the feed
		RR_ClearHudFeed();
}

consvar_t cv_hudfeed_enabled = CVAR_INIT ("hudfeed", "Yes", CV_SAVE|CV_NOINIT|CV_CALL, CV_YesNo, RR_Hudfeed_OnChange);
consvar_t cv_hudfeed_show_grades = CVAR_INIT ("hudfeed_show_grades", "No", CV_SAVE, CV_YesNo, RR_Hudfeed_OnChange);
static CV_PossibleValue_t hudfeed_position_cons_t[] = { {0, "Default"}, {1, "Top-Middle"}, {2, "Top-Right"}, {3, "Bottom-Middle"},{0, NULL}};
consvar_t cv_hudfeed_position = CVAR_INIT ("hudfeedposition", "Default", CV_SAVE|CV_NOINIT|CV_CALL, hudfeed_position_cons_t, RR_UpdateHudFeedConfig);

// === CLASSES ===

// 1. Player interaction (hit/explode/whip etc)
class PlayerFeedUpdate : public BaseFeedUpdate {
private:
	std::string victim {"Victim"};          // Target's name
	std::string attacker {"Attacker"};      // Source's name
	ItemConfigForFeedUpdate item {};        // The item to blame
	dboolean is_self_hit {false};            // Self-hit (how emabrassing)
	dboolean is_dplayer_attacker {false};     // Is the display player the attacker?
	int32_t attacker_colour {0};              // Attacker color (White by default)
	int32_t victim_colour {0};                // Victim color (White by default)

	// Drawer
	Draw row;
	ProcessedItemConfig c;

	void draw_attacker(dboolean reverse) {
		int attacker_name_width = FeedUpdateUtils::getNameWidth(attacker);
		if (reverse) {
			if (!is_self_hit) {
				row
				.x(-attacker_name_width)
				.scale(FEED_UPDATE_SCALE)
				.flags(attacker_colour|V_ALLOWLOWERCASE)
				.text(attacker.c_str());

				row = row.x(-(attacker_name_width + c.x_offset)).colormap(c.colormap);
			}
		} else {
			uint8_t *item_recolormap = (c.should_flash) ? c.colormap : c.item_recolormap;
			if (!is_self_hit) {
				row.scale(FEED_UPDATE_SCALE).flags(attacker_colour|V_ALLOWLOWERCASE).text(attacker.c_str());

				// Then, the item
				row = row.x(attacker_name_width + c.x_offset).colormap(item_recolormap);
			} else {
				// Begin the update with the item
				row = row.colormap(item_recolormap);
			}
		}
	}

	void draw_item(dboolean reverse) {
		int x = 0;
		if (reverse) {
			x = -c.width_scaled;
		}

		row
		.x(x)
		.y(c.y_offset)
		.scale(c.patch_scale)
		.patch(c.patch_name);

		if (c.stolen != KITEM_NONE)
		{
			patch_t *itempatch = K_GetCachedItemPatch(c.stolen, true, 1);

			row
			.x(x-5)
			.y(c.y_offset)
			.scale(0.25)
			.patch(itempatch);
		}

		if (reverse) {
			row = row.x(-(c.width_scaled + c.x_offset)).colormap(static_cast<uint8_t*>(NULL));
		} else {
			row = row.x(c.width_scaled + c.x_offset).colormap(static_cast<uint8_t*>(NULL));
		}
	}

	void draw_victim(dboolean reverse) {
		int victim_name_width = FeedUpdateUtils::getNameWidth(victim);
		if (reverse) {
			row.x(-(victim_name_width)).scale(FEED_UPDATE_SCALE).flags(victim_colour|V_ALLOWLOWERCASE).text(victim.c_str());
			row = row.x(-(victim_name_width + c.x_offset)).colormap(c.colormap);
		} else {
			row.scale(FEED_UPDATE_SCALE).flags(victim_colour|V_ALLOWLOWERCASE).text(victim.c_str());
			row = row.x(victim_name_width + c.x_offset).colormap(static_cast<uint8_t*>(NULL));
		}
	}

	int get_background_width() {
		int attacker_name_width = FeedUpdateUtils::getNameWidth(attacker);
		int victim_name_width = FeedUpdateUtils::getNameWidth(victim);

		int background_width = 0;

		if (!is_self_hit) {
			background_width += (attacker_name_width + c.x_offset);
		}

		background_width += (c.width_scaled + c.x_offset) + (victim_name_width - 2);
		return background_width;
	}

	// Get the width of the update up until the item graphic
	int get_width_for_centering() {
		int attacker_name_width = FeedUpdateUtils::getNameWidth(attacker);
		int center_width = 0;

		if (!is_self_hit) {
			center_width += (attacker_name_width + c.x_offset);
		}
		center_width += (c.width_scaled / 2);
		return center_width;
	}

public:
	// https://www.geeksforgeeks.org/when-do-we-use-initializer-list-in-c/
	PlayerFeedUpdate(
		const std::string &v,
		const std::string &a,
		const ItemConfigForFeedUpdate &i,
		const dboolean &self_hit,
		const dboolean &is_dplayer_attacker,
		const dboolean &about_self,
		const int32_t &attacker_colour,
		const int32_t &victim_colour
	): victim(v), attacker(a), item(i), is_self_hit(self_hit), is_dplayer_attacker(is_dplayer_attacker), attacker_colour(attacker_colour), victim_colour(victim_colour) {
		is_about_self = about_self;
	}

	void draw() override {
		if (!can_draw()) { return; }

		// Snap to the left, bottom, etc
		FeedConfig config = get_feed().get_config();
		int32_t hud_flags = config.hudflags;
		dboolean reverse = config.position == FEED_POSITION_RIGHT;
		dboolean center = config.position == FEED_POSITION_MIDDLE;

		// Check if the colours aren't an impossible value
		if (attacker_colour > V_TANMAP)
			attacker_colour = 0;
		if (victim_colour > V_TANMAP)
			victim_colour = 0;

		// If the update doesn't concern the main player
		// then draw at a higher translucency
		int32_t low_priority_trans = (!is_about_self) ? 3 : -1;

		if (is_entering() || is_exiting()) {
			hud_flags |= (clamp_translucency(low_priority_trans) << V_ALPHASHIFT);
		} else {
			if (!is_about_self)
				hud_flags |= (low_priority_trans << V_ALPHASHIFT);
		}

		// Config for drawing the patch
		c = FeedUpdateUtils::processItemConfig(item);

		if (center && !has_x_been_overwritten()) {
			// Take the total width of the update UP until the item graphic
			// subtract the x-coordinate by that width
			overwrite_x(x - (get_width_for_centering() * FRACUNIT));
		}

		// Prepare the drawer
		row = Draw(FixedToFloat(get_x()), FixedToFloat(y))
		.font(Draw::Font::kThin)
		.align(Draw::Align::kLeft)
		.flags(hud_flags);

		// Draw
		draw_background(config, static_cast<float>(get_background_width()));

		if (reverse) {
			draw_victim(reverse);
			draw_item(reverse);
			draw_attacker(reverse);
		} else {
			draw_attacker(reverse);
			draw_item(reverse);
			draw_victim(reverse);
		}
	}
};

// 2. Global event
/**
 * Shrink and SPBs are the only items that ends becoming everyone else's problem..
 * That warrants a bespoke update
 */

class GlobalFeedUpdate : public BaseFeedUpdate {
private:
	std::string responsible {"Someone"}; // Player responsible
	std::string message {""};

	// Drawer
	Draw row;
	ProcessedItemConfig c;
	ItemConfigForFeedUpdate item {};

	uint8_t* get_item_colormap() {
		return (c.rainbow) ? get_rainbow_colormap() : c.colormap;
	}

	void draw_message(dboolean reverse) {
		int message_width = FeedUpdateUtils::getNameWidth(message);

		if (reverse) {
			row.x(-message_width).scale(FEED_UPDATE_SCALE).flags(V_ALLOWLOWERCASE).text(message.c_str());
		} else {
			row.scale(FEED_UPDATE_SCALE).flags(V_ALLOWLOWERCASE).text(message.c_str());
			row = row.x(message_width + c.x_offset).colormap(c.colormap);
		}
	}

	void draw_item(dboolean reverse) {
		// Then the patch
		int x = 0;
		if (reverse) {
			x = -c.width_scaled;
		}

		row
		.x(x)
		.y(c.y_offset)
		.scale(c.patch_scale)
		.patch(c.patch_name);

		// Then the exclaimation point, because this is a big deal
		if (reverse) {
			row = row.x(-(c.width_scaled + c.x_offset)).colormap(static_cast<uint8_t*>(NULL));
		} else {
			row = row.x(c.width_scaled + c.x_offset).colormap(static_cast<uint8_t*>(NULL));
		}
	}

	void draw_exclaimation_point(dboolean reverse) {
		if (reverse) {
			int exclaimation_width = FeedUpdateUtils::getNameWidth("!");
			row.x(-(exclaimation_width)).scale(FEED_UPDATE_SCALE).flags(V_ALLOWLOWERCASE).text("!");
			row = row.x(-(exclaimation_width + c.x_offset)).colormap(c.colormap);
		} else {
			row.scale(FEED_UPDATE_SCALE).flags(V_ALLOWLOWERCASE).text("!");
		}
	}

	int get_background_width () {
		// Message
		int message_width = FeedUpdateUtils::getNameWidth(message);
		int exclaimation_width = FeedUpdateUtils::getNameWidth("!");

		return (exclaimation_width + (c.width_scaled + c.x_offset)) + (message_width + c.x_offset);
	}

public:
	// Constructor
	GlobalFeedUpdate(
		const std::string &s,
		const ItemConfigForFeedUpdate &i
	): responsible(s), item(i) {
		// It's a global event, it DOES involve you
		is_about_self = true;
		message = fmt::format("{0} used", responsible);
	}

	void draw() override {
		if (!can_draw()) return;

		FeedConfig config = get_feed().get_config();
		int32_t hud_flags = config.hudflags;
		dboolean reverse = config.position == FEED_POSITION_RIGHT;
		dboolean center = config.position == FEED_POSITION_MIDDLE;

		if (is_entering() || is_exiting()) {
			hud_flags |= (updatetrans << V_ALPHASHIFT);
		}

		// Config for drawing the patch
		c = FeedUpdateUtils::processItemConfig(item);

		if (center && !has_x_been_overwritten()) {
			// Take the total width of the update
			// subtract the x-coordinate by half of that width
			overwrite_x(x - ((get_background_width()/2) * FRACUNIT));
		}

		// Prepare drawer
		row = Draw(FixedToFloat(get_x()), FixedToFloat(y))
		.font(Draw::Font::kThin)
		.align(Draw::Align::kLeft)
		.flags(hud_flags);

		// Background
		draw_background(config, static_cast<float>(get_background_width()));

		if (reverse) {
			draw_exclaimation_point(reverse);
			draw_item(reverse);
			draw_message(reverse);
		} else {
			draw_message(reverse);
			draw_item(reverse);
			draw_exclaimation_point(reverse);
		}
	}
};

// 3. Player event
/**
 * Events that pertain to only one player but aren't the result of a player interaction.
 * Be it falling in the pit, last place explosion, grades, etc.
 */
class GlobalPlayerFeedUpdate : public BaseFeedUpdate {
private:
	std::string player {""};

	// Drawer
	Draw row;
	ProcessedItemConfig c;
	ItemConfigForFeedUpdate item {};

	uint8_t* get_item_colormap() {
		if (c.rainbow)
			return get_rainbow_colormap();
		return (c.should_flash) ? c.colormap : c.item_recolormap;
	}

	void draw_player(dboolean reverse) {
		int player_name_width = FeedUpdateUtils::getNameWidth(player);

		if (reverse) {
			row.x(-player_name_width).scale(FEED_UPDATE_SCALE).flags(V_ALLOWLOWERCASE).text(player.c_str());
			row = row.x(-(player_name_width + c.x_offset)).colormap(get_item_colormap());
		} else {
			row.scale(FEED_UPDATE_SCALE).flags(V_ALLOWLOWERCASE).text(player.c_str());
		}
	}

	void draw_patch(dboolean reverse) {
		// Then the patch
		int x = 0;
		if (reverse) {
			x = -c.width_scaled;
		} else {
			row = row.colormap(get_item_colormap());
		}

		row
		.x(x)
		.y(c.y_offset)
		.scale(c.patch_scale)
		.patch(c.patch_name);

		// If in reverse, then the player name
		if (!reverse) {
			row = row.x(c.width_scaled + c.x_offset).colormap(static_cast<uint8_t*>(NULL));
		}
	}

	int get_background_width() {
		// Message
		int player_width = FeedUpdateUtils::getNameWidth(player);
		return (c.width_scaled + c.x_offset) + (player_width);
	}

public:
	// Constructor
	GlobalPlayerFeedUpdate(
		const std::string &s,
		const ItemConfigForFeedUpdate &i,
		const dboolean &about_self
	): player(s), item(i) {
		is_about_self = about_self;
	}

	void draw() override {
		if (!can_draw()) return;

		FeedConfig config = get_feed().get_config();
		int32_t hud_flags = config.hudflags;
		dboolean reverse = config.position == FEED_POSITION_RIGHT;
		dboolean center = config.position == FEED_POSITION_MIDDLE;

		// If the update doesn't concern the main player
		// then draw at a higher translucency
		int32_t low_priority_trans = (!is_about_self) ? 5 : -1;

		if (is_entering() || is_exiting()) {
			hud_flags |= (clamp_translucency(low_priority_trans) << V_ALPHASHIFT);
		} else {
			if (!is_about_self)
				hud_flags |= (low_priority_trans << V_ALPHASHIFT);
		}

		// Config for drawing the patch
		c = FeedUpdateUtils::processItemConfig(item);

		if (center && !has_x_been_overwritten()) {
			// Take the total width of the update
			// subtract the x-coordinate by half of that width
			overwrite_x(x - ((get_background_width()/2) * FRACUNIT));
		}

		// Prepare drawer
		row = Draw(FixedToFloat(get_x()), FixedToFloat(y))
		.font(Draw::Font::kThin)
		.align(Draw::Align::kLeft)
		.flags(hud_flags);

		// Background
		draw_background(config, static_cast<float>(get_background_width()));

		if (reverse) {
			draw_player(reverse);
			draw_patch(reverse);
		} else {
			draw_patch(reverse);
			draw_player(reverse);
		}
	}
};

static FeedConfig getFeedConfig(void) {
	const FeedConfig default_config = (cv_rouletteonplayer.value == 1) ? NoItemBoxConfig : ItemBoxConfig;
	switch (cv_hudfeed_position.value) {
		case 0:
			return default_config;
		case 1:
			return TopMiddleConfig;
		case 2:
			return TimestampConfig;
		case 3:
			return BottomMiddleConfig;
		default:
			return default_config;
	}
	return default_config;
}

// Initialize the feed
static Hudfeed hudfeed(getFeedConfig());

static ItemConfigForFeedUpdate getItemConfigForFeedKillUpdate(mobj_t *mo) {
	mobjtype_t type = mo->type;
	switch (type) {
		case MT_SINK:
			return {"K_TISINK", 20, 20, {false, true}};
		default:
			break;
	}
	return {};
}

static ItemConfigForFeedUpdate getItemConfigForFeedDamageUpdate(mobj_t *mo) {
	mobjtype_t type = mo->type;

	switch (type) {
		case MT_ORBINAUT:
		case MT_ORBINAUT_SHIELD:
			return {"K_TIORBN", 22, 22};
		case MT_JAWZ:
		case MT_JAWZ_SHIELD:
			return {"K_TIJAWZ", 27, 18};
		case MT_BANANA:
		case MT_BANANA_SHIELD:
			if (mo->health > 1)
				return {"K_TIBANA", 20, 20, {false, true}};
		else
			return {"K_TIBANA", 20, 20};
		case MT_SPB:
			return {"K_TISPB", 20, 20};
		case MT_SPBEXPLOSION: {
			ItemConfigForFeedUpdate spbConfig = {"K_TISPB", 20, 20, {false, true}, {}, -2};
			if (mo->extravalue1 != 1) {
				spbConfig = {"K_TIEGGM", 24, 20, {false, true}, {}, 0};
			}
			return spbConfig;
		}
		case MT_LANDMINE:
		case MT_EGGMINE:
		case MT_EGGMINE_SHIELD:
			if (mo->health > 1)
				return {"K_TILMNE", 20, 20, {false, true}};
		else
			return {"K_TILMNE", 20, 20};
		case MT_BALLHOG:
		case MT_BALLHOGBOOM:
			return {"K_TIBHOG", 20, 20};
		case MT_SSMINE:
		case MT_SSMINE_SHIELD:
			return {"K_TIMINE", 22, 22, {false, true}};
		default:
			break;
	}

	return {};
}

const ItemConfigForFeedUpdate shrinkItemConfig = {
	"K_TISHRK",
	20,
	20,
	{{}, true, {}, {}, SKINCOLOR_ORANGE},
	{},
	0
};

static ItemConfigForFeedUpdate getItemConfigForFeedAttackUpdate(playerattacks_t attack, kartitemtype_e stolen) {
	switch (attack) {
		case ATTACK_FLAMEDASH:
			return {"K_TIFLMS", 20, 20};
		case ATTACK_HYUDORO:
			return {"K_TIHYUD", 20, 20, {}, {}, {}, stolen};
		case ATTACK_SHRINK:
			return shrinkItemConfig;
		case ATTACK_THUNDER_SHIELD:
			return {"K_TITHNS", 20, 20};
		case ATTACK_INVINCIBILITY:
			return {"K_TIINV", 20, 20, {true, false, 6}, {}, -2};
		case ATTACK_GROW:
			return {
				"K_TIGROW",
				20,
				20,
				{
					{},
					true,
					{},
					{},
					SKINCOLOR_BLUEBERRY
				}
			};
		case ATTACK_PITFALL:
			return {"K_TIDTHP", 20, 20};
		case ATTACK_ATTRACTION:
			return {"K_TIATTR", 20, 20};
		default:
			break;
	}
	return {};
}

static ItemConfigForFeedUpdate getItemConfigForGlobalFeedUpdate(globalfeedevent_t event) {
	switch (event) {
		case EVENT_SHRINK:
			return shrinkItemConfig;
		case EVENT_SPB:
			return  {"K_TISPB", 20, 20};
		default:
			break;
	}
	return {};
}

static playerattacks_t getPlayerAttackType(mobj_t* mo) {
	player_t* p = mo->player;

	if (p->attractionattack ) {
		return ATTACK_ATTRACTION;
	}
	if (p->flamestore >= FLAMESTOREMAX-1 && p->flametimer > 0) {
		return ATTACK_FLAMEDASH;
	}
	if (p->invincibilitytimer > 0) {
		return ATTACK_INVINCIBILITY;
	}
	if(p->growshrinktimer > 0) {
		return ATTACK_GROW;
	}

	return ATTACK_NONE;
}

static dboolean isPlayerMoValid(mobj_t *mo) {
	return (
		mo != NULL
		&& !P_MobjWasRemoved(mo)
		&& mo->type == MT_PLAYER
		&& mo->player != NULL
		&& !mo->player->spectator
	);
}

static dboolean arePlayersValid(mobj_t *m1, mobj_t *m2) {
	return isPlayerMoValid(m1) && isPlayerMoValid(m2);
}

static dboolean isFeedUpdateAboutMainPlayer(player_t *p1, player_t* p2) {
	return stplyr != NULL && stplyr->spectator == false && ((p1 == stplyr) || (p2 == stplyr));
}

static dboolean isDisplayPlayerAttacker(player_t* p) {
	return stplyr != NULL && stplyr->spectator == false && ((p == stplyr));
}

static dboolean canUseHudfeed(void) {
	return !dedicated && rendermode != render_none && cv_hudfeed_enabled.value;
}

static int32_t getPlayerChatColour(player_t *p) {
	return skincolors[p->skincolor].chatcolor;
}

// Push a player interaction to the feed.
void RR_PushPlayerDamageToFeed(mobj_t *source, mobj_t *target, mobj_t *inflictor) {
	if (!canUseHudfeed()) return;

	if (!inflictor || P_MobjWasRemoved(inflictor)) return;

	// If the inflictor type is a player, check for certain conditions
	if (inflictor->type == MT_PLAYER) {
		if (isPlayerMoValid(inflictor)) {
			RR_PushPlayerInteractionToFeed(source, target, getPlayerAttackType(inflictor), KITEM_NONE);
		} else {
			return;
		}
	}

	// Special exception for MT_SPBEXPLOSION
	// Eggmark explosions use the same mobjtype, so check if the right player can be blamed
	if (inflictor->type == MT_SPBEXPLOSION) {
		if (inflictor->extravalue1 != 1) {
			if (isPlayerMoValid(inflictor->target)) {
				// True source (K_KartPlayerThink)
				source = inflictor->target;
			}
		}
	}

	// Check player mobjs
	if (!arePlayersValid(source, target)) return;

	// Verify if the item type is something the feed is configured to show
	ItemConfigForFeedUpdate itemConfig = getItemConfigForFeedDamageUpdate(inflictor);

	if (itemConfig.patch.empty()) return;

	player_t* source_plyr = source->player;
	player_t* target_plyr = target->player;

	std::string attacker = player_names[source_plyr-players];
	std::string victim = player_names[target_plyr-players];


	int32_t attacker_colour = getPlayerChatColour(source_plyr);
	int32_t victim_colour = getPlayerChatColour(target_plyr);

	// Cap off player names if they're too long
	// (maybe?)

	const dboolean self_hit = source_plyr == target_plyr;

	// Push to the feed
	hudfeed.push(
		std::make_unique<PlayerFeedUpdate>(
			victim,
			attacker,
			itemConfig,
			self_hit,
			isDisplayPlayerAttacker(source_plyr),
			isFeedUpdateAboutMainPlayer(source_plyr, target_plyr),
			attacker_colour,
			victim_colour
		)
	);
}

void RR_PushLuaPlayerMessageToFeed(player_t *source, player_t *target, LuaConfigForFeedUpdate config)
{
	if (!canUseHudfeed()) return;

	if (!source || !target) return;

	if (config.patch == nullptr) return;

	ItemAnimatedConfig anim = {0};

	if (config.animation_config.valid)
	{
		anim = {.animated = config.animation_config.animated,
			.should_flash = config.animation_config.should_flash,
			.animated_frames = config.animation_config.animated_frames,
			.tics_per_frame = config.animation_config.tics_per_frame,
			.flashing_colour = config.animation_config.flashing_colour,
		};
	}

	ItemConfigForFeedUpdate itemconfig = {.patch = config.patch,
		.width = config.width,
		.height = config.height,
		.animation_config = anim,
		.patch_scale = config.patch_scale,
		.y_offset = config.y_offset,
		.stolen_item = config.stolen_item,
		.recolour = config.recolour,
		.rainbow = config.rainbow,
	};

	std::string attacker = player_names[source-players];
	std::string victim = player_names[target-players];

	int32_t attacker_colour = getPlayerChatColour(source);
	int32_t victim_colour = getPlayerChatColour(target);

	const dboolean self_hit = source == target;

	// Push to the feed
	hudfeed.push(
		std::make_unique<PlayerFeedUpdate>(
			victim,
			attacker,
			itemconfig,
			self_hit,
			isDisplayPlayerAttacker(source),
			isFeedUpdateAboutMainPlayer(source, target),
			attacker_colour,
			victim_colour
		)
	);
}

// Like the previous one but allows for extra text like TF2
// PLAYER 1 () is DOMINATING PLAYER 2
// () PLAYER 1  fell to a clumsy, painful death
void RR_PushLuaPlayerFlavourMessageToFeed(player_t *source, dboolean after1, const char *flavour1, player_t *target, dboolean after2, const char *flavour2, LuaConfigForFeedUpdate config)
{
	if (!canUseHudfeed()) return;

	if (!source || !target) return;

	if (config.patch == nullptr) return;

	ItemAnimatedConfig anim = {0};

	if (config.animation_config.valid)
	{
		anim = {.animated = config.animation_config.animated,
			.should_flash = config.animation_config.should_flash,
			.animated_frames = config.animation_config.animated_frames,
			.tics_per_frame = config.animation_config.tics_per_frame,
			.flashing_colour = config.animation_config.flashing_colour,
		};
	}

	ItemConfigForFeedUpdate itemconfig = {.patch = config.patch,
		.width = config.width,
		.height = config.height,
		.animation_config = anim,
		.patch_scale = config.patch_scale,
		.y_offset = config.y_offset,
		.stolen_item = config.stolen_item,
		.recolour = config.recolour,
		.rainbow = config.rainbow,
	};

	uint16_t chatcolor1 = skincolors[source->skincolor].chatcolor;
	char color_prefix1[2];

	uint16_t chatcolor2 = skincolors[target->skincolor].chatcolor;
	char color_prefix2[2];

	if (chatcolor1 > V_TANMAP)
	{
		snprintf(color_prefix1, sizeof(color_prefix1), "%c", '\x80');
	}
	else
	{
		snprintf(color_prefix1, sizeof(color_prefix1), "%c", '\x80' + (chatcolor1 >> V_CHARCOLORSHIFT));
	}

	if (chatcolor2 > V_TANMAP)
	{
		snprintf(color_prefix2, sizeof(color_prefix1), "%c", '\x80');
	}
	else
	{
		snprintf(color_prefix2, sizeof(color_prefix1), "%c", '\x80' + (chatcolor2 >> V_CHARCOLORSHIFT));
	}

	std::string attacker;
	std::string victim;

	if (after1 == true)
	{
		attacker = va("%s%s\x80%s", color_prefix1, player_names[source-players], flavour1);
	}
	else
	{
		attacker = va("%s%s%s", flavour1, color_prefix1, player_names[source-players]);
	}

	if (after2 == true)
	{
		victim = va("%s%s\x80%s", color_prefix2, player_names[target-players], flavour2);
	}
	else
	{
		victim = va("%s%s%s",flavour2, color_prefix2, player_names[target-players]);
	}

	const dboolean self_hit = source == target;

	// Push to the feed
	hudfeed.push(
		std::make_unique<PlayerFeedUpdate>(
			victim,
			attacker,
			itemconfig,
			self_hit,
			isDisplayPlayerAttacker(source),
			isFeedUpdateAboutMainPlayer(source, target),
			0,
			0
		)
	);
}

void RR_PushLuaMessageToFeed(const char *source, int32_t source_color, const char *target, int32_t target_color, dboolean displayplayer, LuaConfigForFeedUpdate config)
{
	if (!canUseHudfeed()) return;

	if (config.patch == nullptr) return;

	const dboolean self_hit = fastcmp(source, target);

	ItemAnimatedConfig anim = {0};

	if (config.animation_config.valid)
	{
		anim = {.animated = config.animation_config.animated,
			.should_flash = config.animation_config.should_flash,
			.animated_frames = config.animation_config.animated_frames,
			.tics_per_frame = config.animation_config.tics_per_frame,
			.flashing_colour = config.animation_config.flashing_colour,
		};
	}

	ItemConfigForFeedUpdate itemconfig = {.patch = config.patch,
		.width = config.width,
		.height = config.height,
		.animation_config = anim,
		.patch_scale = config.patch_scale,
		.y_offset = config.y_offset,
		.stolen_item = config.stolen_item,
		.recolour = config.recolour,
		.rainbow = config.rainbow,
	};

	std::string source_string = source;
	std::string target_string = target;

	// Push to the feed
	hudfeed.push(
		std::make_unique<PlayerFeedUpdate>(
			target_string,
			source_string,
			itemconfig,
			self_hit,
			displayplayer,
			displayplayer,
			source_color,
			target_color
		)
	);
}

dboolean RR_PushPlayerDeathToFeed(mobj_t *source, mobj_t *target, mobj_t *inflictor) {
	if (!canUseHudfeed()) return false;

	if (!inflictor || P_MobjWasRemoved(inflictor)) return false;
	if (!arePlayersValid(source, target)) return false;

	ItemConfigForFeedUpdate itemConfig = getItemConfigForFeedKillUpdate(inflictor);

	if (itemConfig.patch.empty()) return false;

	player_t* source_plyr = source->player;
	player_t* target_plyr = target->player;

	std::string attacker = player_names[source_plyr-players];
	std::string victim = player_names[target_plyr-players];

	int32_t attacker_colour = getPlayerChatColour(source_plyr);
	int32_t victim_colour = getPlayerChatColour(target_plyr);

	const dboolean self_hit = source_plyr == target_plyr;

	// Push to the feed
	hudfeed.push(
		std::make_unique<PlayerFeedUpdate>(
			victim,
			attacker,
			itemConfig,
			self_hit,
			isDisplayPlayerAttacker(source_plyr),
			isFeedUpdateAboutMainPlayer(source_plyr, target_plyr),
			attacker_colour,
			victim_colour
		)
	);

	return true;
}

void RR_PushPlayerInteractionToFeed(mobj_t *source, mobj_t *target, playerattacks_t attack, kartitemtype_e stolen) {
	if (!canUseHudfeed()) return;

	if (attack == ATTACK_NONE) return;
	if (!arePlayersValid(source, target)) return;

	// Verify if the attack is something the feed is configured to show
	ItemConfigForFeedUpdate itemConfig = getItemConfigForFeedAttackUpdate(attack, stolen);

	if (itemConfig.patch.empty()) return;

	player_t* source_plyr = source->player;
	player_t* target_plyr = target->player;

	std::string attacker = player_names[source_plyr-players];
	std::string victim = player_names[target_plyr-players];

	int32_t attacker_colour = getPlayerChatColour(source_plyr);
	int32_t victim_colour = getPlayerChatColour(target_plyr);

	// Cap off player names if they're too long
	// (maybe?)

	const dboolean self_hit = source_plyr == target_plyr;

	// Push to the feed
	hudfeed.push(
		std::make_unique<PlayerFeedUpdate>(
			victim,
			attacker,
			itemConfig,
			self_hit,
			isDisplayPlayerAttacker(source_plyr),
			isFeedUpdateAboutMainPlayer(source_plyr, target_plyr),
			attacker_colour,
			victim_colour
		)
	);
}

void RR_PushGlobalEventToFeed(player_t* player, globalfeedevent_t event) {
	if (!canUseHudfeed()) return;

	ItemConfigForFeedUpdate itemConfig = getItemConfigForGlobalFeedUpdate(event);
	if (itemConfig.patch.empty()) return;

	uint16_t chatcolor = skincolors[player->skincolor].chatcolor;
	char color_prefix[2];

	if (chatcolor > V_TANMAP)
	{
		snprintf(color_prefix, sizeof(color_prefix), "%c", '\x80');
	}
	else
	{
		snprintf(color_prefix, sizeof(color_prefix), "%c", '\x80' + (chatcolor >> V_CHARCOLORSHIFT));
	}

	const std::string player_name = va("%s%s\x80", color_prefix, player_names[player-players]);

	// Push to the feed
	hudfeed.push(std::make_unique<GlobalFeedUpdate>(player_name, itemConfig));
}

void RR_PushLuaGlobalEventToFeed(player_t* player, LuaConfigForFeedUpdate config)
{
	if (!canUseHudfeed()) return;

	if (config.patch == nullptr) return;

	ItemAnimatedConfig anim = {0};

	if (config.animation_config.valid)
	{
		anim = {.animated = config.animation_config.animated,
			.should_flash = config.animation_config.should_flash,
			.animated_frames = config.animation_config.animated_frames,
			.tics_per_frame = config.animation_config.tics_per_frame,
			.flashing_colour = config.animation_config.flashing_colour,
		};
	}

	ItemConfigForFeedUpdate itemconfig = {.patch = config.patch,
		.width = config.width,
		.height = config.height,
		.animation_config = anim,
		.patch_scale = config.patch_scale,
		.y_offset = config.y_offset,
		.stolen_item = config.stolen_item,
		.recolour = config.recolour,
		.rainbow = config.rainbow,
	};

	uint16_t chatcolor = skincolors[player->skincolor].chatcolor;
	char color_prefix[2];

	if (chatcolor > V_TANMAP)
	{
		snprintf(color_prefix, sizeof(color_prefix), "%c", '\x80');
	}
	else
	{
		snprintf(color_prefix, sizeof(color_prefix), "%c", '\x80' + (chatcolor >> V_CHARCOLORSHIFT));
	}

	const std::string player_name = va("%s%s\x80", color_prefix, player_names[player-players]);

	// Push to the feed
	hudfeed.push(std::make_unique<GlobalFeedUpdate>(player_name, itemconfig));
}


static const char *ranktierpatch_name(ranktier_e rank)
{
	switch (rank)
	{
		case RTIER_E:
			return "BKRNS01";
		case RTIER_D:
			return "BKRNS02";
		case RTIER_C:
			return "BKRNS03";
		case RTIER_B:
			return "BKRNS04";
		case RTIER_A:
			return "BKRNS05";
		case RTIER_ONESTAR:
			return "BKRNS06";
		case RTIER_TWOSTAR:
			return "BKRNS07";
		case  RTIER_THREESTAR:
			return "BKRNS08";
		case  RTIER_P:
			return "BKRNS09";
		default:
			return nullptr;
	}
}

void RR_PushGlobalGradeEventToFeed(player_t* player, ranktier_e rank) {
	if (!canUseHudfeed()) return;
	if (!cv_hudfeed_show_grades.value) return;

	const char *ranktier = ranktierpatch_name(rank);

	if (ranktier == nullptr)
		return;

	ItemConfigForFeedUpdate gradeConfig = {
		ranktier,
		16,
		8,
		{},
		1.0f,
		{},
		KITEM_NONE,
		SKINCOLOR_YELLOW,
		(rank == RTIER_P),
	};

	uint16_t chatcolor = skincolors[player->skincolor].chatcolor;
	char color_prefix[2];

	if (chatcolor > V_TANMAP)
	{
		snprintf(color_prefix, sizeof(color_prefix), "%c", '\x80');
	}
	else
	{
		snprintf(color_prefix, sizeof(color_prefix), "%c", '\x80' + (chatcolor >> V_CHARCOLORSHIFT));
	}

	const std::string player_name = va("%s%s\x80", color_prefix, player_names[player-players]);

	// Push to the feed
	hudfeed.push(std::make_unique<GlobalPlayerFeedUpdate>(
		player_name,
		gradeConfig,
		stplyr == player
	));
}

// Draw the HUD feed
void RR_DrawHudFeed(void) {
	if (!has_feed_been_initialized) {
		RR_UpdateHudFeedConfig();
		has_feed_been_initialized = true;
		return;
	}
	if (!canUseHudfeed()) return;
	if (r_splitscreen > 0) return; // Never supporting splitscreen

	hudfeed.animate_feed();
}

// Update the HUD feed's timers
void RR_TickHudFeed(void) {
	if (!has_feed_been_initialized) return;
	if (!canUseHudfeed()) return;

	hudfeed.tick_feed();
}

// Wipe the feed whenever the player can't see it (i.e. during intermissions)
void RR_ClearHudFeed(void) {
	hudfeed.clear();
}

// Update the feed config on cvar change
void RR_UpdateHudFeedConfig(void) {
	hudfeed.clear();
	hudfeed.update_config(getFeedConfig());
}

static void dummyupdate(void) {
	if (!netgame && stplyr != NULL)
		RR_PushGlobalGradeEventToFeed(stplyr, RTIER_P);
}

void RR_FeedCom(void) {
	COM_AddCommand("ff", dummyupdate);
}
