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
#include "../../lib/Rect.h"

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

	struct ButtonWords
	{
		std::shared_ptr<IImage> image; ///< at the screen's scale; transparent where the gold shows through
		Rect letters; ///< the letters' black in the image: the words' width, and the lettering's height
	};

	/// `text` in capitals in the lettering of the game's gold buttons (K, September 27th: "its a bold, and
	/// specific font (that we should match)"), 22 pixels tall as on the game's 40 pixel bars. No bitmap
	/// font of the game's has it: each letter is lifted from a button whose word has it (BEGIN, BACK,
	/// LOAD, NEXT, SAVE, EXIT, RESTART) as its black and white over the bar's gold, so it lands on any
	/// gold bar as the game drew it. F, H, M, P, U, W and Y are built from those letters' strokes. No
	/// image when the text needs anything else (J, Q, Z, digits, marks), or when the game's files or the
	/// player's language are not English: those buttons carry other words.
	ButtonWords buttonWords(const std::string & text);
}
