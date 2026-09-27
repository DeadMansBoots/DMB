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
class CMapGenOptions;
struct StartInfo;

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
	/// mod.json "atBegin": the generator makes a game's map when the host presses Begin, with every
	/// player's town (generateForGame). Without it, its tab's Generate button makes a map to pick.
	bool atBegin = false;
};

namespace MapGenerators
{
	/// The generator a mod declares, from its installed mod.json; nothing if it declares none
	DLL_LINKAGE std::optional<MapGeneratorInfo> read(const ModDescription & mod);

	/// The generators among the game's active mods, in load order
	DLL_LINKAGE std::vector<MapGeneratorInfo> active();

	/// Makes the random map of a game that is starting, where VCMI's own generator would (the server,
	/// at Begin, CGameState::initNewGame): runs the generator of the mod `options` names with the
	/// lobby's final choices, and returns the .vmap it wrote, which stays in the user's Maps/RandomMaps.
	/// The command gets the tab's arguments (lobby/MapGenTab.h), where --players and --humans count the
	/// game's players and those a human may take, plus --factions "core:castle,random,..." (each
	/// player's town as map files name it, or random, in colour order: the game's players are the first
	/// colours), --bio.compOnly (the players only the computer takes) and --teams "red,blue;tan,green"
	/// (teams of two or more).
	/// Throws std::runtime_error with the reason when the generator is missing, not vouched for by
	/// DMB's mod catalog, or fails; the server shows it and the lobby stays open.
	DLL_LINKAGE boost::filesystem::path generateForGame(const CMapGenOptions & options, const StartInfo & start, int seed);

	/// Runs a generator's command with these arguments and waits for it, at most `timeout`, after which
	/// it is ended. Both its output streams go to `logPath`, and it opens no console window over the
	/// game. Returns its exit code; throws std::runtime_error when it cannot start or runs too long.
	DLL_LINKAGE int run(const MapGeneratorInfo & generator, const std::vector<std::string> & args,
		const boost::filesystem::path & logPath, std::chrono::seconds timeout);

	/// What a generator that failed said: its last line starting "Error: " in `logPath`, without that
	/// prefix; empty when it said nothing
	DLL_LINKAGE std::string errorLine(const boost::filesystem::path & logPath);
}

VCMI_LIB_NAMESPACE_END
