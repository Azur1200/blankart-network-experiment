// DR. ROBOTNIK'S RING RACERS
//-----------------------------------------------------------------------------
// Copyright (C) 2024 by James Robert Roman
// Copyright (C) 2024 by Kart Krew
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------

#include <unordered_map>

#include "doomdef.h" // skincolornum_t
#include "doomtype.h"
#include "hu_stuff.h"
#include "i_time.h"
#include "k_hud.h"
#include "m_fixed.h"
#include "m_menu.h"
#include "r_draw.h"
#include "v_draw.hpp"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"
#include "fastcmp.h"

using srb2::Draw;
using Chain = Draw::Chain;

int Draw::TextElement::width() const
{
	return font_ ? font_width(*font_, default_font_flags(*font_) | flags_.value_or(0), string_.c_str()) / FRACUNIT : 0;
}

Draw::TextElement& Draw::TextElement::parse(std::string_view raw)
{
	static const std::unordered_map<std::string_view, char> translation = {
#define BUTTON(str, lower_bits) \
		{str,             0xB0 | lower_bits},\
		{str "_animated", 0xA0 | lower_bits},\
		{str "_pressed",  0x90 | lower_bits}

		BUTTON("up", 0x00),
		BUTTON("down", 0x01),
		BUTTON("right", 0x02),
		BUTTON("left", 0x03),

		//BUTTON("dpad", 0x04),

		BUTTON("drift", 0x07),
		BUTTON("item", 0x08),
		BUTTON("systemmenu", 0x09),

		BUTTON("accel", 0x0A),
		BUTTON("brake", 0x0B),
		BUTTON("horn", 0x0C),

		BUTTON("custom1", 0x0D),
		BUTTON("custom2", 0x0E),
		BUTTON("custom3", 0x0F),

#undef BUTTON

		{"white", 0x80},
		{"purple", 0x81},
		{"yellow", 0x82},
		{"green", 0x83},
		{"blue", 0x84},
		{"red", 0x85},
		{"gray", 0x86},
		{"orange", 0x87},
		{"sky", 0x88},
		{"lavender", 0x89},
		{"gold", 0x8A},
		{"aqua", 0x8B},
		{"magenta", 0x8C},
		{"pink", 0x8D},
		{"brown", 0x8E},
		{"tan", 0x8F},
	};

	string_.clear();
	string_.reserve(raw.size());

	using std::size_t;
	using std::string_view;

	for (;;)
	{
		size_t p = raw.find('<');

		// Copy characters until the start tag
		string_.append(raw.substr(0, p));

		if (p == raw.npos)
		{
			break; // end of string
		}

		raw.remove_prefix(p);

		// Find end tag
		p = raw.find('>');

		if (p == raw.npos)
		{
			break; // no end tag
		}

		string_view code = raw.substr(1, p - 1);

		if (auto it = translation.find(code); it != translation.end())
		{
			string_.push_back(it->second); // replace with character code
		}
		else
		{
			string_.append(raw.substr(0, p + 1)); // code not found, leave text verbatim
		}

		raw.remove_prefix(p + 1); // past end of tag
	}

	return *this;
}

void Chain::patch(patch_t* patch) const
{
	const auto _ = Clipper(*this);

	const bool stretchH = stretch_ == Stretch::kWidth || stretch_ == Stretch::kBoth;
	const bool stretchV = stretch_ == Stretch::kHeight || stretch_ == Stretch::kBoth;

	const fixed_t h = stretchH ? FloatToFixed(width_ / patch->width) : FRACUNIT;
	const fixed_t v = stretchV ? FloatToFixed(height_ / patch->height) : FRACUNIT;

	V_DrawStretchyFixedPatch(FloatToFixed(x_), FloatToFixed(y_), h * scale_, v * scale_, flags_, patch, colormap_);
}

void Chain::thumbnail(mapnum_t mapnum) const
{
	const auto _ = Clipper(*this);

	patch_t *patch;
	fixed_t scale = M_GetMapThumbnail(mapnum, &patch)/4;

	V_DrawFixedPatch(FloatToFixed(x_), FloatToFixed(y_), scale, flags_, patch, colormap_);
}

void Chain::fill(uint8_t color) const
{
	const auto _ = Clipper(*this);

	V_DrawFill(static_cast<int32_t>(x_), static_cast<int32_t>(y_), static_cast<int32_t>(width_), static_cast<int32_t>(height_), color|(flags_ & ~0xFF));
}

void Chain::string(const char* str, int32_t flags, Font font) const
{
	const auto _ = Clipper(*this);

	flags |= default_font_flags(font);

	fixed_t x = FloatToFixed(x_);
	fixed_t y = FloatToFixed(y_);

	switch (align_)
	{
	case Align::kLeft:
		break;

	case Align::kCenter:
		x -= (font_width(font, flags, str) / 2) * scale_;
		break;

	case Align::kRight:
		x -= font_width(font, flags, str) * scale_;
		break;
	}

	V_DrawStringScaledEx(x, y, FloatToFixed(scale_), FloatToFixed(scale_), FRACUNIT, FRACUNIT, flags, colormap_, font_to_fontno(font), str);
}

namespace
{
// Keep up to date with G_SupportedButtonDrawing in g_input.c
patch_t** get_button_patch(gamecontrols_e type)
{
	int32_t keynum = G_KeynumFromGamecontrol(type);

	if (keynum == 0)
		return kp_buttons_keyboardback;

	switch (keynum)
	{
		case KEY_JOY1+0:
			return kp_buttons_a;
		case KEY_JOY1+1:
				return kp_buttons_b;
		case KEY_JOY1+2:
			return kp_buttons_x;
		case KEY_JOY1+3:
			return kp_buttons_y;
		case KEY_JOY1+6:
			return kp_buttons_start;
		case KEY_JOY1+7:
			return kp_buttons_lstickclick;
		case KEY_JOY1+8:
			return kp_buttons_rstickclick;
		case KEY_JOY1+9:
			return kp_buttons_lbumper;
		case KEY_JOY1+10:
			return kp_buttons_rbumper;
		case KEY_JOY1+11:
			return kp_buttons_dpadup;
		case KEY_JOY1+12:
			return kp_buttons_dpaddown;
		case KEY_JOY1+13:
			return kp_buttons_dpadleft;
		case KEY_JOY1+14:
			return kp_buttons_dpadright;
		case KEY_JOY1+16:
			return kp_buttons_paddle1;
		case KEY_JOY1+17:
			return kp_buttons_paddle2;
		case KEY_JOY1+18:
			return kp_buttons_paddle3;
		case KEY_JOY1+19:
			return kp_buttons_paddle4;
		case KEY_JOY1+20:
			return kp_buttons_touchpad;
		case KEY_AXIS1+0:
			return kp_buttons_lstickleft;
		case KEY_AXIS1+1:
			return kp_buttons_lstickright;
		case KEY_AXIS1+2:
			return kp_buttons_lstickup;
		case KEY_AXIS1+3:
			return kp_buttons_lstickdown;
		case KEY_AXIS1+4:
			return kp_buttons_rstickleft;
		case KEY_AXIS1+5:
			return kp_buttons_rstickright;
		case KEY_AXIS1+6:
			return kp_buttons_rstickup;
		case KEY_AXIS1+7:
			return kp_buttons_rstickdown;
		case KEY_AXIS1+8:
			return kp_buttons_ltrigger;
		case KEY_AXIS1+9:
			return kp_buttons_rtrigger;
		default:
				return kp_buttons_keyboardback;
			break;
	}

	return nullptr;
};

}; // namespace

void Chain::button_(gamecontrols_e type, std::optional<bool> press) const
{
	const auto _ = Clipper(*this);

	if (press)
	{
		K_drawButton(FloatToFixed(x_), FloatToFixed(y_), flags_, get_button_patch(type), *press, type);
	}
	else
	{
		K_drawButtonAnim(x_, y_, flags_, get_button_patch(type), I_GetTime(), type);
	}
}

void Chain::sticker(patch_t* end_graphic, uint8_t color) const
{
	const auto _ = Clipper(*this);

	int32_t x = x_;
	int32_t y = y_;
	int32_t width = width_;
	int32_t flags = flags_ | V_FLIP;

	auto fill = [&](int x, int width) { V_DrawFill(x, y, width, LSBF_SHORT(end_graphic->height), color | (flags_ & ~0xFF)); };

	if (align_ == Align::kRight)
	{
		width = -(width);
		flags ^= V_FLIP;
		fill(x + width, -(width));
	}
	else
	{
		fill(x, width);
	}

	V_DrawScaledPatch(x + width, y, flags, end_graphic);

	if (align_ == Align::kCenter)
	{
		V_DrawScaledPatch(x, y, flags ^ V_FLIP, end_graphic);
	}
}

Chain::Clipper::Clipper(const Chain& chain)
{
	V_SaveClipRect(&save_);

	if (chain.clip_)
	{
		V_SetClipRect(
			FloatToFixed(chain.clipx1_),
			FloatToFixed(chain.clipy1_),
			FloatToFixed(chain.clipx2_ - chain.clipx1_),
			FloatToFixed(chain.clipy2_ - chain.clipy1_),
			chain.flags_
		);
	}
}

Chain::Clipper::~Clipper()
{
	V_RestoreClipRect(&save_);
}

patch_t* Draw::cache_patch(const char* name)
{
	return static_cast<patch_t*>(W_CachePatchName(name, PU_CACHE));
}

int Draw::font_to_fontno(Font font)
{
	switch (font)
	{
	case Font::kThin:
		return TINY_FONT;

	case Font::kConsole:
		return HU_FONT;

	case Font::kFreeplay:
		return KART_FONT;

	case Font::kLevelTitle:
		return LT_FONT;

	case Font::kCredits:
		return CRED_FONT;

	case Font::kGTOL:
		return GTOL_FONT;

	case Font::kGTFN:
		return GTFN_FONT;

	case Font::kTallNum:
		return TALLNUM_FONT;

	case Font::kNightsNum:
		return NIGHTSNUM_FONT;

	case Font::kPingNum:
		return PINGNUM_FONT;

	case Font::kMiniRanking:
		return OPPRNK_FONT;
	}

	return TINY_FONT;
};

int32_t Draw::default_font_flags(Font font)
{
	int32_t flags = 0;

	(void)font;

	return flags;
};

fixed_t Draw::font_width(Font font, int32_t flags, const char* string)
{
	return V_StringScaledWidth(FRACUNIT, FRACUNIT, FRACUNIT, flags, font_to_fontno(font), string);
}
