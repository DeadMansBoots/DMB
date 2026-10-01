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
#include <map>
#include <numeric>
#include <sstream>

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

/// Where the load bar stands during a generator's run. The bar has 20 blocks and a generator 23 stages, and the
/// stages take very different times (on a big map two of them are most of the run), so a bar that moves one step
/// per stage stands still for most of it. This one moves with the clock instead: each stage owns a share of the
/// bar and the bar fills that share over the time the stage is expected to take. The shares are, in order of
/// preference, what the generator announces ("[weights] 1 1 2 ..." before its first stage, one per stage), then
/// what the last run on this machine measured (MapGenTimings\<mod>.json in the user folder), then equal; the bar
/// never goes back and never reaches the next stage's start before it begins.
class StageClock
{
	using Clock = std::chrono::steady_clock;

	std::vector<double> shares; // one per stage, summing to 1; empty: equal
	double expectedSeconds = 0; // the whole run on a map this size, from the last one; 0 when unknown
	int step = 0;
	int total = 0;
	Clock::time_point stageStart = Clock::now();
	std::map<int, double> finished; // seconds each finished stage took, by its step (a generator may skip a number)
	Load::Type shown = 0;

	double shareOf(int s) const
	{
		return static_cast<int>(shares.size()) == total && total > 0 ? shares[s - 1] : 1.0 / std::max(1, total);
	}

public:
	void setShares(std::vector<double> given, double expected)
	{
		const double sum = std::accumulate(given.begin(), given.end(), 0.0);
		if(sum > 0)
		{
			for(auto & s : given)
				s /= sum;
			shares = std::move(given);
			expectedSeconds = expected;
		}
	}

	bool hasShares() const
	{
		return !shares.empty();
	}

	void stageBegan(int newStep, int newTotal)
	{
		const auto now = Clock::now();
		if(step > 0 && newStep > step)
			finished[step] = std::chrono::duration<double>(now - stageStart).count();
		step = newStep;
		total = newTotal;
		stageStart = now;
	}

	Load::Type value()
	{
		if(step < 1)
			return shown;
		double before = 0;
		for(int s = 1; s < step; ++s)
			before += shareOf(s);
		const double elapsed = std::chrono::duration<double>(Clock::now() - stageStart).count();
		double expected;
		if(hasShares() && expectedSeconds > 0)
			expected = shareOf(step) * expectedSeconds;
		else if(!finished.empty())
		{
			double sum = 0;
			for(const auto & [number, seconds] : finished)
				sum += seconds;
			expected = sum / finished.size();
		}
		else
			expected = 2.0;
		expected = std::max(expected, 0.2);
		const double x = elapsed / expected;
		// on schedule it is linear; late, it slows and keeps creeping without ever reaching the next stage
		const double within = x < 0.9 ? x : 0.9 + 0.08 * (1 - std::exp(-(x - 0.9)));
		const double position = before + shareOf(step) * std::min(within, 0.98);
		const int scaled = static_cast<int>(std::numeric_limits<Load::Type>::max() * std::clamp(position, 0.0, 0.98));
		shown = std::max<Load::Type>(shown, static_cast<Load::Type>(scaled));
		return shown;
	}

	/// the seconds each stage took, the last one up to now; only a run that reached its last stage says anything
	std::vector<double> measured() const
	{
		if(step < 1 || step != total || finished.size() < 2)
			return {};
		std::vector<double> result(total, 0.0); // a step the generator never announced took no time
		for(const auto & [number, seconds] : finished)
			result[number - 1] = seconds;
		result[total - 1] = std::chrono::duration<double>(Clock::now() - stageStart).count();
		return result;
	}
};

boost::filesystem::path timingsFile(const MapGeneratorInfo & generator)
{
	return VCMIDirs::get().userDataPath() / "MapGenTimings" / (generator.modID + ".json");
}

/// the generator's shares as the last run measured them, with the map area and seconds that run took
bool readTimings(const MapGeneratorInfo & generator, std::vector<double> & shares, double & area, double & seconds)
{
	std::ifstream in(timingsFile(generator).string(), std::ios::binary);
	if(!in)
		return false;
	const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	try
	{
		const JsonNode node(reinterpret_cast<const std::byte *>(text.data()), text.size(), "timings");
		shares.clear();
		for(const auto & s : node["shares"].Vector())
			shares.push_back(s.Float());
		area = node["area"].Float();
		seconds = node["seconds"].Float();
		return !shares.empty() && area > 0 && seconds > 0;
	}
	catch(const std::exception &)
	{
		return false;
	}
}

void writeTimings(const MapGeneratorInfo & generator, const std::vector<double> & stageSeconds, double area, const std::vector<double> & before)
{
	const double sum = std::accumulate(stageSeconds.begin(), stageSeconds.end(), 0.0);
	if(sum <= 0 || area <= 0)
		return;
	JsonNode node;
	node["area"].Float() = area;
	node["seconds"].Float() = sum;
	node["shares"].Vector().clear();
	for(size_t i = 0; i < stageSeconds.size(); ++i)
	{
		double share = stageSeconds[i] / sum;
		if(before.size() == stageSeconds.size())
			share = (share + before[i]) / 2; // a long map and a short one both count
		JsonNode entry;
		entry.Float() = share;
		node["shares"].Vector().push_back(entry);
	}
	boost::system::error_code ec;
	boost::filesystem::create_directories(timingsFile(generator).parent_path(), ec);
	std::ofstream out(timingsFile(generator).string(), std::ios::binary | std::ios::trunc);
	out << node.toString();
}

/// The stages a generator announced in its log since `offset`, whole lines only (a line still being
/// written is read next time): the last one becomes the current phase and starts its stage in `clock`; a
/// "[weights]" line gives the stages' shares of the run
void readPhases(const MapGeneratorInfo & generator, const boost::filesystem::path & logPath, std::streamoff & offset, StageClock & clock, double expectedSeconds)
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
		if(boost::algorithm::starts_with(line, "[weights] "))
		{
			std::istringstream numbers(boost::algorithm::replace_all_copy(line.substr(10), ",", " "));
			std::vector<double> given;
			for(double w; numbers >> w;)
				given.push_back(std::max(0.0, w));
			clock.setShares(std::move(given), expectedSeconds);
			continue;
		}
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
	clock.stageBegan(latest->step, latest->total);
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
	for(const auto & argument : section["arguments"].Vector())
		info.arguments.insert(argument.String());
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
	std::vector<std::string> humanColors;
	std::map<TeamID, std::vector<std::string>> teams;
	for(const auto & [color, player] : start.playerInfos)
	{
		players++;
		const std::string colorName = color.toString();
		if(player.isControlledByHuman())
		{
			seated++;
			humanColors.push_back(colorName);
		}
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
	int playersGiven = players;
	int humansGiven = humans;
	int compOnlyGiven = players - humans;
	const bool takesColours = generator->arguments.count("humanColors") != 0;
	if(takesColours)
	{
		// the lobby left a count on Random unrolled (MapGenTab::lobbyOptions): the generator rolls it, and
		// the seated humans keep the colours they took (--humanColors); -1 is Random
		const int optionsHumans = options.getHumanOrCpuPlayerCount();
		const int optionsCompOnly = options.getCompOnlyPlayerCount();
		humansGiven = optionsHumans == CMapGenOptions::RANDOM_SIZE ? -1 : std::max(optionsHumans, seated);
		compOnlyGiven = optionsCompOnly == CMapGenOptions::RANDOM_SIZE ? -1 : optionsCompOnly;
		playersGiven = humansGiven < 0 || compOnlyGiven < 0 ? -1 : humansGiven + compOnlyGiven;
	}

	const auto outDir = VCMIDirs::get().userDataPath() / "Maps" / "RandomMaps";
	boost::filesystem::create_directories(outDir);
	const auto outPath = outDir / ("mapgen_" + std::to_string(options.getWidth()) + "x" + std::to_string(options.getHeight())
		+ "_p" + (playersGiven < 0 ? std::string("R") : std::to_string(playersGiven)) + "_s" + std::to_string(seed) + ".vmap");

	std::vector<std::string> args {
		"--w", std::to_string(options.getWidth()),
		"--h", std::to_string(options.getHeight()),
		"--players", std::to_string(playersGiven),
		"--humans", std::to_string(humansGiven),
		"--seed", std::to_string(seed),
		"--out", outPath.string(),
		// each player's town, in colour order, and the players only the computer takes (MapGen's flags,
		// September 27th). With --humanColors the lobby's slots are every colour from red, so each entry
		// is that colour's town.
		"--factions", boost::algorithm::join(factions, ","),
		"--bio.compOnly", std::to_string(compOnlyGiven),
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
	// the colours the seated humans took in the lobby, for a generator that takes them: the player count
	// stays a surprise until Begin, as in VCMI, so the humans are not always the first colours (K)
	if(takesColours && !humanColors.empty())
	{
		args.push_back("--humanColors");
		args.push_back(boost::algorithm::join(humanColors, ","));
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

	// the bar's clock: the last run's shares of the time, scaled to this map's area from --w and --h
	StageClock clock;
	double area = 0;
	for(size_t i = 0; i + 1 < args.size(); ++i)
	{
		if(args[i] == "--w")
			area = std::atof(args[i + 1].c_str());
		if(args[i] == "--h" && area > 0)
			area *= std::atof(args[i + 1].c_str());
	}
	std::vector<double> lastShares;
	double lastArea = 0;
	double lastSeconds = 0;
	double expectedSeconds = 0;
	if(readTimings(generator, lastShares, lastArea, lastSeconds) && area > 0)
	{
		expectedSeconds = lastSeconds * area / lastArea;
		clock.setShares(lastShares, expectedSeconds);
	}

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
		readPhases(generator, logPath, logRead, clock, expectedSeconds);
		if(progress)
			progress->set(clock.value());
	}
	child->wait();
	const int exitCode = child->exit_code();
	// what this run measured, for the next one's bar (a run that failed or skipped a stage says nothing)
	if(exitCode == 0)
	{
		readPhases(generator, logPath, logRead, clock, expectedSeconds);
		const auto seconds = clock.measured();
		if(!seconds.empty())
			writeTimings(generator, seconds, area, lastShares.size() == seconds.size() ? lastShares : std::vector<double>());
	}
	return exitCode;
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
