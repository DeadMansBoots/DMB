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
}
