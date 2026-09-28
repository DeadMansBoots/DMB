/*
 * MapGenerators.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "MapGenerators.h"

#include "AddonCode.h"
#include "CModHandler.h"
#include "ModDescription.h"

#include "../GameLibrary.h"
#include "../LoadProgress.h"
#include "../ScopeGuard.h"
#include "../StartInfo.h"
#include "../texts/CGeneralTextHandler.h"
#include "../VCMIDirs.h"
#include "../filesystem/Filesystem.h"
#include "../json/JsonNode.h"
#include "../rmg/CMapGenOptions.h"

#include <fstream>

#include <boost/algorithm/string.hpp>

#if BOOST_VERSION >= 108600
// the v1 API, as the engine's own launcher code uses it
#include <boost/process/v1/child.hpp>
#include <boost/process/v1/io.hpp>
#ifdef VCMI_WINDOWS
#include <boost/process/v1/windows.hpp>
#endif
#else
#include <boost/process/child.hpp>
#include <boost/process/io.hpp>
#ifdef VCMI_WINDOWS
#include <boost/process/windows.hpp>
#endif
#endif

VCMI_LIB_NAMESPACE_BEGIN

namespace
{
/// the game is a window program, so on Windows a console program it starts, cmd.exe for a .cmd
/// included, would open a console window over the game unless told not to
template<typename... Properties>
std::unique_ptr<boost::process::child> startChild(Properties &&... properties)
{
#ifdef VCMI_WINDOWS
	return std::make_unique<boost::process::child>(std::forward<Properties>(properties)..., boost::process::windows::create_no_window);
#else
	return std::make_unique<boost::process::child>(std::forward<Properties>(properties)...);
#endif
}

std::mutex phaseMutex;
MapGenerators::Phase phaseNow;

void setPhase(const MapGenerators::Phase & phase)
{
	std::scoped_lock lock(phaseMutex);
	phaseNow = phase;
}

/// The stages a generator announced in its log since `offset`, whole lines only (a line still being
/// written is read next time): the last one becomes the current phase and sets `progress`
void readPhases(const MapGeneratorInfo & generator, const boost::filesystem::path & logPath, std::streamoff & offset, Load::Progress * progress)
{
	std::ifstream log(logPath.c_str(), std::ios::binary);
	if(!log || !log.seekg(offset))
		return;
	std::string line;
	std::optional<MapGenerators::Phase> latest;
	while(std::getline(log, line))
	{
		if(log.eof())
			break;
		offset = log.tellg();
		boost::algorithm::trim_right(line);
		if(!boost::algorithm::starts_with(line, "[phase] "))
			continue;
		std::istringstream fields(line.substr(8));
		MapGenerators::Phase phase;
		char slash = 0;
		std::string id;
		if(!(fields >> phase.step >> slash >> phase.total >> id) || slash != '/' || phase.step < 1 || phase.total < phase.step)
			continue;
		const TextIdentifier textID("vcmi.mapGen.phase." + id);
		if(LIBRARY && LIBRARY->generaltexth && LIBRARY->generaltexth->identifierExists(textID))
			phase.text = LIBRARY->generaltexth->translate(textID.get());
		latest = phase;
	}
	if(!latest)
		return;
	logGlobal->debug("Map generator %s: stage %d of %d, %s", generator.name, latest->step, latest->total, latest->text);
	setPhase(*latest);
	if(progress)
		progress->set(static_cast<Load::Type>(std::numeric_limits<Load::Type>::max() * (latest->step - 1) / latest->total));
}
}

MapGenerators::Phase MapGenerators::currentPhase()
{
	std::scoped_lock lock(phaseMutex);
	return phaseNow;
}

std::optional<MapGeneratorInfo> MapGenerators::read(const ModDescription & mod)
{
	const JsonNode & section = mod.getLocalValue("mapGenerator");
	if(!section.isStruct())
		return std::nullopt;

	MapGeneratorInfo info;
	info.modID = mod.getID();
	info.name = section["name"].String();
	info.tab = section["tab"].String();
	info.atBegin = section["atBegin"].Bool();
	const std::string command = boost::filesystem::path(section["command"].String()).lexically_normal().generic_string();

	const auto folder = AddonCode::modFolder(info.modID);
	if(folder)
	{
		info.codeFolder = *folder / "generator";
		info.command = *folder / command;
	}

	bool tabFound = false;
	try
	{
		// the mod's own filesystem, so two generators' layouts never shadow each other
		tabFound = !info.tab.empty() && CResourceHandler::get(info.modID)->existsResource(JsonPath::builtin(info.tab));
	}
	catch(const std::out_of_range &)
	{
		// the mod's filesystem is not loaded: the mod is not active
	}

	if(info.name.empty() || command.empty() || info.tab.empty())
		info.problem = "its mod.json gives no name, command or tab";
	else if(!boost::starts_with(command, "generator/") || boost::contains(command, ".."))
		info.problem = "its command is not inside the mod's generator folder";
	else if(!folder)
		info.problem = "its folder was not found";
	else if(!boost::filesystem::exists(info.command))
		info.problem = "its command is missing: " + command;
	else if(!tabFound)
		info.problem = "its tab layout is missing from the mod: " + info.tab;
	return info;
}

std::vector<MapGeneratorInfo> MapGenerators::active()
{
	std::vector<MapGeneratorInfo> result;
	if(!LIBRARY || !LIBRARY->modh)
		return result;

	for(const auto & modID : LIBRARY->modh->getActiveMods())
	{
		auto info = read(LIBRARY->modh->getModInfo(modID));
		if(info)
			result.push_back(*info);
	}
	return result;
}

boost::filesystem::path MapGenerators::generateForGame(const CMapGenOptions & options, const StartInfo & start, int seed, Load::Progress * progress)
{
	std::optional<MapGeneratorInfo> generator;
	for(const auto & found : active())
		if(found.modID == options.getExternalGenerator())
			generator = found;
	if(!generator)
		throw std::runtime_error("the map generator of the mod " + options.getExternalGenerator() + " is not enabled");
	if(!generator->problem.empty())
		throw std::runtime_error(generator->name + " cannot run: " + generator->problem);
	if(!generator->atBegin)
		throw std::runtime_error(generator->name + " makes maps from its own tab, not when a game starts");
	const std::string refused = AddonCode::trustProblem(generator->modID, generator->codeFolder);
	if(!refused.empty())
		throw std::runtime_error(generator->name + " cannot run: " + refused);

	// the generator's settings as the host's lobby tab left them (MapGenTab::updateMapInfoByHost)
	const JsonNode & chosen = options.getExternalSettings();
	const auto mapSetting = [&chosen](const std::string & key) -> const JsonNode &
	{
		return chosen["map"][key];
	};

	// the game's players as the lobby left them, in colour order (playerInfos is a map by colour): the
	// first colours, red, blue, tan and on, as the lobby's slots are (its counts are never Random,
	// MapGenTab::updateMapInfoByHost), which is where a generator places its players
	int players = 0;
	int seated = 0;
	std::vector<std::string> factions;
	std::map<TeamID, std::vector<std::string>> teams;
	for(const auto & [color, player] : start.playerInfos)
	{
		players++;
		if(player.isControlledByHuman())
			seated++;
		const std::string colorName = color.toString();
		factions.push_back(player.castle == FactionID::RANDOM || !player.castle.hasValue() ? "random" : FactionID::encode(player.castle.getNum()));
		const auto & settings = options.getPlayersSettings();
		if(const auto found = settings.find(color); found != settings.end() && found->second.getTeam() != TeamID::NO_TEAM)
			teams[found->second.getTeam()].push_back(colorName);
	}
	std::string teamList;
	for(const auto & team : teams)
		if(team.second.size() > 1)
			teamList += (teamList.empty() ? "" : ";") + boost::algorithm::join(team.second, ",");
	// The players only the computer takes are the options' count: VCMI's lobby shows every slot of a
	// random map as a human's or the computer's, and its own generator picks the computer's only when
	// it runs. Never fewer human slots than the humans seated.
	const int compOnlyCount = options.getCompOnlyPlayerCount() == CMapGenOptions::RANDOM_SIZE ? 0 : options.getCompOnlyPlayerCount();
	const int humans = std::max(players - std::clamp(compOnlyCount, 0, players), seated);

	const auto outDir = VCMIDirs::get().userDataPath() / "Maps" / "RandomMaps";
	boost::filesystem::create_directories(outDir);
	const auto outPath = outDir / ("mapgen_" + std::to_string(options.getWidth()) + "x" + std::to_string(options.getHeight())
		+ "_p" + std::to_string(players) + "_s" + std::to_string(seed) + ".vmap");

	std::vector<std::string> args {
		"--w", std::to_string(options.getWidth()),
		"--h", std::to_string(options.getHeight()),
		"--players", std::to_string(players),
		"--humans", std::to_string(humans),
		"--seed", std::to_string(seed),
		"--out", outPath.string(),
		// each player's town, in colour order, and the players only the computer takes (MapGen's flags,
		// September 27th)
		"--factions", boost::algorithm::join(factions, ","),
		"--bio.compOnly", std::to_string(players - humans),
	};
	if(options.getLevels() > 1)
	{
		args.push_back("--underground");
		args.push_back("1");
	}
	if(!teamList.empty())
	{
		args.push_back("--teams");
		args.push_back(teamList);
	}
	args.push_back("--declaremods");
	args.push_back(mapSetting("declareMods").isNumber() && mapSetting("declareMods").Integer() ? "1" : "0");
	const auto noTrailingSlash = [](std::string s)
	{
		while(s.size() > 3 && (s.back() == '\\' || s.back() == '/'))
			s.pop_back();
		return s;
	};
	const auto dataPaths = VCMIDirs::get().dataPaths();
	if(!dataPaths.empty())
	{
		args.push_back("--vcmiroot");
		args.push_back(noTrailingSlash(boost::filesystem::absolute(dataPaths.front()).lexically_normal().string()));
	}
	args.push_back("--vcmiuserdir");
	args.push_back(noTrailingSlash(VCMIDirs::get().userDataPath().lexically_normal().string()));
	if(mapSetting("template").isString() && !mapSetting("template").String().empty())
	{
		args.push_back("--template");
		args.push_back(mapSetting("template").String());
	}
	if(chosen["preset"].isString() && !chosen["preset"].String().empty())
	{
		args.push_back("--preset");
		args.push_back(chosen["preset"].String());
	}
	// a setting the tab never stored is absent, and the generator uses its own default; compOnly is
	// the game's count, given above (the tab's may say Random)
	for(const auto & lever : chosen["params"].Struct())
	{
		if(!lever.second.isNumber() || lever.first == "compOnly")
			continue;
		std::ostringstream value;
		value << lever.second.Float();
		// rivers is a generator flag of its own, not a --bio setting
		args.push_back(lever.first == "rivers" ? "--rivers" : "--bio." + lever.first);
		args.push_back(value.str());
	}

	const auto logPath = VCMIDirs::get().userLogsPath() / "extmapgen_log.txt";
	logGlobal->info("Map generator %s (mod %s) makes the game's map: %s -> %s", generator->name, generator->modID, generator->command.string(), outPath.string());
	// a big map takes a minute or two; a generator still running after ten has hung
	const int exitCode = run(*generator, args, logPath, std::chrono::minutes(10), progress);
	if(exitCode != 0 || !boost::filesystem::exists(outPath))
	{
		const std::string reason = errorLine(logPath);
		throw std::runtime_error(generator->name + ": " + (reason.empty() ? "the map generator failed, see extmapgen_log.txt in the logs folder" : reason));
	}
	return outPath;
}

int MapGenerators::run(const MapGeneratorInfo & generator, const std::vector<std::string> & args,
	const boost::filesystem::path & logPath, std::chrono::seconds timeout, Load::Progress * progress)
{
	// the stages this run announces; none once it ends, however it ends
	setPhase({});
	auto clearPhase = vstd::makeScopeGuard([]() { setPhase({}); });
	std::streamoff logRead = 0;

	const std::string command = generator.command.string();
	const std::string ext = boost::algorithm::to_lower_copy(generator.command.extension().string());
	std::error_code ec;
	std::unique_ptr<boost::process::child> child;
	// one handle for both streams: two handles on the file each write from its start and overwrite each
	// other, the "Error: " line the player is shown included (MapGen's finding)
	if(ext == ".cmd" || ext == ".bat")
	{
		// batch scripts need the shell. cmd /c splits `cmd /c "quoted path" args` at the first space
		// inside the quotes; the form that parses is one more quote pair around the whole tail:
		// cmd /c ""path" args", passed verbatim as one string
		std::string tail = "cmd /c \"\"" + command + "\"";
		for(const auto & a : args)
			tail += ' ' + ((a.find_first_of(" \"") == std::string::npos) ? a : "\"" + a + "\"");
		tail += '"';
		child = startChild(tail, ec, (boost::process::std_out & boost::process::std_err) > logPath);
	}
	else
		child = startChild(command, args, ec, (boost::process::std_out & boost::process::std_err) > logPath);
	if(ec)
		throw std::runtime_error(generator.name + " could not start: " + ec.message());

	// Boost's wait_for is deprecated as unreliable, so the child is looked at five times a second
	// instead, reading the stages it announced meanwhile
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	while(child->running())
	{
		if(std::chrono::steady_clock::now() > deadline)
		{
			child->terminate();
			throw std::runtime_error(generator.name + " did not finish in " + std::to_string(timeout.count() / 60) + " minutes");
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
		readPhases(generator, logPath, logRead, progress);
	}
	child->wait();
	return child->exit_code();
}

std::string MapGenerators::errorLine(const boost::filesystem::path & logPath)
{
	// a generator that refuses a combination says why on a line starting "Error: "
	std::string reason;
	std::ifstream log(logPath.string());
	std::string line;
	while(std::getline(log, line))
		if(boost::algorithm::starts_with(line, "Error: "))
			reason = boost::algorithm::trim_right_copy(line.substr(7));
	return reason;
}

VCMI_LIB_NAMESPACE_END
