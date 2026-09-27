/*
 * MapGenerators.h, part of VCMI engine
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

/// DMB: a map generator is a mod that carries a program making random maps, and the layout of the
/// lobby tab that drives it (K, September 26th: MapGen is an addon, installed like any other mod).
/// Its mod.json declares it:
///     "mapGenerator" : { "name" : "MapGen", "command" : "generator/generate.cmd", "tab" : "config/widgets/mapGen/mapGenTab.json" }
/// Everything that runs lives in the mod's "generator" folder, the command included; DMB's mod
/// catalog pins that folder (AddonCode.h). The tab's layout is a resource of the mod's own Content.
/// The lobby offers the tab only while such a mod is enabled.
struct DLL_LINKAGE MapGeneratorInfo
{
	std::string modID;
	/// what the lobby's button says
	std::string name;
	/// the program, absolute; set when the mod's folder was found
	boost::filesystem::path command;
	/// the mod's "generator" folder, absolute: the code the catalog's pin covers
	boost::filesystem::path codeFolder;
	/// the tab's layout, a resource path in the mod's Content
	std::string tab;
	/// empty when the generator can be offered, else why not. The catalog is checked before each
	/// run instead (AddonCode::trustProblem), because hashing a generator takes a moment
	std::string problem;
};

namespace MapGenerators
{
	/// The generator a mod declares, from its installed mod.json; nothing if it declares none
	DLL_LINKAGE std::optional<MapGeneratorInfo> read(const ModDescription & mod);

	/// The generators among the game's active mods, in load order
	DLL_LINKAGE std::vector<MapGeneratorInfo> active();
}

VCMI_LIB_NAMESPACE_END
