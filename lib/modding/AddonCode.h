/*
 * AddonCode.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

VCMI_LIB_NAMESPACE_BEGIN

/// DMB: the code an addon mod brings (an AI plugin's "ai" folder, a map generator's "generator" folder)
/// runs only when DMB's mod catalog vouches for it (K, September 26th). A catalog entry pins its mod's
/// code with "codeSha256", one hash or a list of them; the launcher saves the pins of the default
/// catalog (the one DMB ships with), and the game checks the folder against them before it runs
/// anything from it. The setting mods.allowUnlistedCode lets developers run code they built themselves.
namespace AddonCode
{
	/// The addon API level this DMB offers mods; mod.json's "dmb": { "api": N } asks for at least N.
	/// 1: AI plugins, map generators and the catalog's pins (DMB 0.1.0-rc.1 and rc.2).
	/// 2: the "pages" layout widget, mod.json "tabPages", settings-bound labels, and this check.
	constexpr int API_LEVEL = 2;

	/// A mod's own folder on disk, absolute, found in any Mods folder; a submod's is inside its parent's
	DLL_LINKAGE std::optional<boost::filesystem::path> modFolder(const std::string & modID);

	/// SHA-256 of a file as 64 lowercase hex digits; empty when the file cannot be read
	DLL_LINKAGE std::string fileHash(const boost::filesystem::path & file);

	/// The hash a catalog pin names: each file under the folder as a line "<its fileHash>  <its path>\n",
	/// the path relative to the folder with "/" separators, the lines sorted by path, and then the SHA-256
	/// of those lines together. They are the lines sha256sum prints. Empty when the folder cannot be read.
	DLL_LINKAGE std::string folderHash(const boost::filesystem::path & folder);

	/// Where the launcher keeps the default catalog's pins: <user cache>/downloads/dmbCodePins.json
	DLL_LINKAGE boost::filesystem::path pinsFile();

	/// Why the code in the folder, from the mod modID, may not run; empty when it may
	DLL_LINKAGE std::string trustProblem(const std::string & modID, const boost::filesystem::path & folder);
}

VCMI_LIB_NAMESPACE_END
