/*
 * GameLettering.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "EFont.h"

class IImage;

/// DMB: the lettering Heroes III bakes into its own buttons, for words DMB draws in their place (K,
/// September 27th: DMB's art matches the game's own lettering). The letters come from the game's own
/// bitmap fonts, read from its font files at their own pixel size, so they come out the same at any
/// screen scale (the fonts VCMI draws text with are already scaled to the screen).
namespace GameLettering
{
	struct Letters
	{
		int width = 0;  ///< the text's box: the letters' advances, and the font's height
		int height = 0;
		std::vector<std::vector<bool>> pixels; ///< [row][column], true where a letter is (its shadow left out)
	};

	/// `text` in `font`, the whole box the game would draw it in, or trimmed to the letters themselves;
	/// empty when the font's file is missing or the text has no letters
	Letters letters(EFonts font, const std::string & text, bool trim);

	/// `text` carved into gold as the game carves the words of its gold buttons (RANSHOW, read pixel by
	/// pixel): black letters, a brown edge on their left, a white light on their right and underneath,
	/// transparent around. At the screen's scale, one pixel wider than the text's box on every side.
	std::shared_ptr<IImage> carved(EFonts font, const std::string & text);
}
