/*
 * gamefont.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <QByteArray>
#include <QString>

/// DMB: the game's own fonts for the launcher (K, September 27th: "we need to get the font from
/// in-game heroes, and increase the font size...we want to match in-game heroes aesthetic exactly").
/// Heroes III draws its text from bitmap fonts in its data files (Data\MEDFONT.FNT and the like),
/// which Qt cannot load, so the launcher makes a TrueType font of one from the player's own files as
/// it starts: every pixel of the game's glyphs a square, so the font draws the game's pixels exactly
/// at the font's own height. DMB ships none of the game's art.
namespace GameFont
{
	struct Font
	{
		QString family;  ///< empty when the game's files hold no such font
		int height = 0;  ///< the pixel size the font draws exactly at
	};

	/// The game font `name` (MEDFONT, BIGFONT, SMALFONT...) added to the launcher's fonts under
	/// `family`, once; later calls return the same
	Font load(const std::string & name, const QString & family);

	/// The TrueType file itself, for tests: empty when the font is missing or unreadable
	QByteArray trueType(const QByteArray & fnt, const QString & family, const std::string & encoding, int & height);
}
