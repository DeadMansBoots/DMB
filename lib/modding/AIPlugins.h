/*
 * AIPlugins.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

VCMI_LIB_NAMESPACE_BEGIN

class ModDescription;

/// DMB: an AI plugin is a mod that carries an AI library. Its mod.json declares it:
///     "aiPlugin" : { "library" : "OmniAI", "name" : "OmniAI", "kinds" : [ "adventure" ], "builtFor" : "0.1.0" }
/// and the library sits in the mod's own "ai" folder (ai/OmniAI.dll, ai/libOmniAI.so, ai/libOmniAI.dylib),
/// exporting VCMI's usual GetAiName plus GetNewAI and/or GetNewBattleAI. A C++ AI shares classes with
/// the engine and has no stable binary interface, so a plugin loads only in the DMB version it was
/// built for, and only when DMB's mod catalog pins its "ai" folder (AddonCode.h).
struct DLL_LINKAGE AIPluginInfo
{
	std::string modID;
	/// what the AI menus show and the settings store
	std::string name;
	/// the library's base name, "OmniAI"
	std::string library;
	bool adventure = false;
	bool battle = false;
	std::string builtFor;
	/// the library, absolute; set when the mod's folder was found
	boost::filesystem::path path;
	/// empty when the plugin can load, else why it cannot
	std::string problem;
};

namespace AIPlugins
{
	/// The plugin a mod declares, from its installed mod.json; nothing if it declares none
	DLL_LINKAGE std::optional<AIPluginInfo> read(const ModDescription & mod);

	/// The plugins among the game's active mods, in load order
	DLL_LINKAGE std::vector<AIPluginInfo> active();
}

VCMI_LIB_NAMESPACE_END
