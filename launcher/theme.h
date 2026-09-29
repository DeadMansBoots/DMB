/*
 * theme.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <string>

class QString;
class QIcon;
class QToolButton;

/// DMB: the launcher's look (K, September 26th), the setting launcher.theme:
///  - "leather": the leather Heroes III fills its windows with, read from the player's own game files
///    (DIBOXBCK; DMB ships no game art), gold buttons with black text and gold lettering, the game's
///    own look. Before any game files are imported there is no leather to read, and "dark" is used.
///  - "dark": black and dark grey with the same gold accents.
///  - "system": the platform's own look, as stock VCMI's launcher has it.
/// Applied after the game's filesystem is loaded and before the main window builds its widgets, and again
/// whenever the Settings page's Launcher Look changes; every window and dialog of the launcher follows.
namespace LauncherTheme
{
	void apply();

	/// the least a button can be and still carve `text` in gold-button lettering; 0 if there is
	/// nothing to carve it with yet (the language is not English, a letter is missing, or nothing has
	/// been harvested). For a row of buttons laid out to one shared width before any look is chosen
	/// (CSettingsView's category bar): reserve this now, rather than fight a layout already computed
	/// to Qt's own narrower default once carving turns on.
	int minimumCarvedButtonWidth(const QString & text);

	/// `button`'s icon replaced with a crop of the game's own main-menu button art (SPRITES/<defName>,
	/// its lit frame), for a sidebar icon that matches Heroes' own identity instead of a generic Qt
	/// stock icon (Gemini's launcher feedback, K, September 28th: the puzzle-piece/wrench/question-mark
	/// icons clash with the fantasy look). Left alone (keeping whatever icon MainWindow's own .ui gave
	/// it) when there is nothing to read yet: DMB ships no game art of its own, so a player who has not
	/// imported their files sees the same stock icon as before, same fallback leather/dark already use.
	void applySidebarIcon(QToolButton * button, const std::string & defName, int cropX, int cropY, int cropW, int cropH);
}
