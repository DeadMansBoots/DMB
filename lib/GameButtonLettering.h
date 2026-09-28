/*
 * GameButtonLettering.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "Color.h"

VCMI_LIB_NAMESPACE_BEGIN

/// DMB: the letters of the game's own gold buttons (K, September 27th: "its a bold, and specific font
/// (that we should match)"), harvested from the player's own copies of them. Reading the buttons
/// themselves into pixels is the caller's own job (the game client and the Qt launcher each read the
/// player's files a different way); everything from there, matching letters across buttons and building
/// the few they lack, is shared, so a fix to the harvest reaches every user of it at once. See
/// client/render/GameLettering.h for the game's own words on gold bars, and the launcher's theme.cpp for
/// its buttons.
namespace GameButtonLettering
{
	/// a button's first frame, pixel by pixel, at the game's own size
	struct Picture
	{
		int w = 0;
		int h = 0;
		std::vector<ColorRGBA> pixels;
		const ColorRGBA & at(int x, int y) const { return pixels[y * w + x]; }
	};

	/// how much of a pixel is black and how much white, over the bar's gold
	struct Share
	{
		double black = 0;
		double white = 0;
	};

	using Cell = std::pair<int, int>; ///< column, row
	using Pixels = std::map<Cell, Share>;

	struct Glyph
	{
		Pixels pixels; ///< columns from its black's left edge, rows from the lettering's top row
		int width = 0; ///< of its black
	};

	struct Lettering
	{
		int band = 0; ///< the letters' height, from the top of their black to its foot
		std::map<char, Glyph> glyphs;
	};

	/// a button of the game's with words in its gold buttons' lettering, the words, and the letters
	/// taken from it
	struct WordedButton
	{
		std::string name;
		std::string words;
		std::string taken; ///< empty: every letter it has that no button before it gave
	};

	/// the scenario screen's and the game setup's buttons, one cut of the lettering, 22 rows tall. The
	/// campaign screen's is a little narrower, so only RESTART's R, which no other button has, is taken
	/// from it. RANSHOW's is a condensed cut, its letters seven tenths as wide, and none are taken.
	DLL_LINKAGE extern const std::vector<WordedButton> WORDED_BUTTONS;

	/// the lettering, matched across `WORDED_BUTTONS` and the letters it lacks built from the ones it
	/// has: empty when `read` cannot give every button (its own resources missing, or the language is
	/// not English, whose buttons carry other words), or when a letter found again on a later button is
	/// not the one an earlier button already gave (its own words misread, most likely)
	DLL_LINKAGE Lettering harvest(const std::function<Picture(const std::string & name)> & read);
}

VCMI_LIB_NAMESPACE_END
