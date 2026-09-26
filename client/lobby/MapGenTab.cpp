/*
 * MapGenTab.cpp, testinstall patch for the VCMIMapGen external generator
 * (VCMIMapGen queue item 25). See the header.
 *
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "MapGenTab.h"

#include "CLobbyScreen.h"
#include "SelectionTab.h"

#include "../GameEngine.h"
#include "../gui/WindowHandler.h"
#include "../widgets/Buttons.h"
#include "../widgets/ObjectLists.h"
#include "../widgets/TextControls.h"
#include "../windows/GUIClasses.h"
#include "../windows/InfoWindows.h"

#include "../../lib/CConfigHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/VCMIDirs.h"
#include "../../lib/filesystem/ResourcePath.h"
#include "../../lib/rmg/CRmgTemplate.h"
#include "../../lib/rmg/CRmgTemplateStorage.h"
#include "../../lib/texts/CGeneralTextHandler.h"

#include <fstream>
#include <random>
#include <thread>

#include <boost/algorithm/string.hpp>

#if BOOST_VERSION >= 108600
// the v1 API, as the engine's own launcher code uses it
#include <boost/process/v1/child.hpp>
#include <boost/process/v1/io.hpp>
#else
#include <boost/process/child.hpp>
#include <boost/process/io.hpp>
#endif

MapGenPage::MapGenPage(const JsonPath & file, const std::function<void(MapGenPage &)> & setup,
	const std::function<void(MapGenPage &)> & after)
	: InterfaceObjectConfigurable()
{
	OBJECT_CONSTRUCTION;
	if(setup)
		setup(*this);
	build(JsonNode(file));
	if(after)
		after(*this);
}

void MapGenPage::setCallback(const std::string & name, std::function<void(int)> callback)
{
	addCallback(name, std::move(callback));
}

std::shared_ptr<CLabel> MapGenPage::label(const std::string & name) const
{
	return widget<CLabel>(name);
}

MapGenTab::MapGenTab()
	: InterfaceObjectConfigurable()
{
	OBJECT_CONSTRUCTION;
	recActions = 0; // hidden until its lobby button opens it, like the other tabs

	const JsonNode config(JsonPath::builtin("config/widgets/mapGen/mapGenTab.json"));
	for(const auto & page : config["pages"].Vector())
		pageFiles.push_back(page.String());
	defaults = config["defaults"];

	addCallback("activateMapGenPage", [this](int index) { openPage(index); });
	addCallback("resetMapGenDefaults", [this](int) { resetToDefaults(); });
	addCallback("generateMapGenMap", [this](int) { generate(); });
	build(config);

	int first = 0;
	if(persistentStorage["mapGen"]["lastPage"].isNumber())
		first = static_cast<int>(persistentStorage["mapGen"]["lastPage"].Integer());
	if(first < 0 || first >= static_cast<int>(pageFiles.size()))
		first = 0;

	pages = std::make_shared<CTabbedInt>(std::bind(&MapGenTab::createPage, this, _1), Point(0, 0), first);
	pages->setRedrawParent(true);

	if(auto group = widget<CToggleGroup>("pageButtons"))
		group->setSelected(first);
}

std::shared_ptr<CIntObject> MapGenTab::createPage(size_t index)
{
	if(index >= pageFiles.size())
	{
		logGlobal->error("MapGen tab: no page %d", static_cast<int>(index));
		return std::make_shared<CIntObject>();
	}
	return std::make_shared<MapGenPage>(JsonPath::builtin(pageFiles[index]),
		[this](MapGenPage & page)
		{
			page.setCallback("chooseMapGenTemplate", [this](int) { chooseTemplate(); });
		},
		[this](MapGenPage & page)
		{
			if(auto name = page.label("labelTemplateName"))
			{
				const std::string chosen = templateName();
				name->setText(chosen.empty()
					? LIBRARY->generaltexth->translate("vcmi.mapGen.template.none") : chosen);
			}
		});
}

void MapGenTab::openPage(size_t index)
{
	if(!pages || index >= pageFiles.size())
		return;
	pages->setActive(index);
	CIntObject::redraw();

	Settings lastPage = persistentStorage.write["mapGen"]["lastPage"];
	lastPage->Integer() = static_cast<int>(index);
}

void MapGenTab::resetToDefaults()
{
	// every lever and map setting back to the generator's own default (the
	// Nostalgia preset), then the open page is rebuilt so it shows them
	for(const auto & entry : defaults["params"].Struct())
	{
		Settings value = persistentStorage.write["mapGen"]["params"][entry.first];
		*value.operator->() = entry.second;
	}
	for(const auto & entry : defaults["map"].Struct())
	{
		Settings value = persistentStorage.write["mapGen"]["map"][entry.first];
		*value.operator->() = entry.second;
	}
	Settings preset = persistentStorage.write["mapGen"]["preset"];
	preset->String() = "nostalgia";
	if(pages)
		pages->reset();
	CIntObject::redraw();
}

int MapGenTab::mapSetting(const std::string & key) const
{
	const JsonNode & stored = persistentStorage["mapGen"]["map"][key];
	if(stored.isNumber())
		return static_cast<int>(stored.Integer());
	return static_cast<int>(defaults["map"][key].Integer());
}

std::string MapGenTab::templateName() const
{
	const JsonNode & stored = persistentStorage["mapGen"]["map"]["template"];
	return stored.isString() ? stored.String() : std::string();
}

void MapGenTab::chooseTemplate()
{
	// the install's templates that take this map size, level count and
	// player count, the way the Random Map tab filters its own list; the
	// first entry is the generator's own free layout
	const int size = mapSetting("size");
	const int levels = mapSetting("underground") ? 2 : 1;
	const int players = mapSetting("players");
	std::vector<std::string> fits;
	for(const auto * tpl : LIBRARY->tplh->getTemplates())
		if(tpl && tpl->matchesSize(int3(size, size, levels)) && tpl->getPlayers().isInRange(players))
			fits.push_back(tpl->getName());
	std::sort(fits.begin(), fits.end());

	std::vector<std::string> names { LIBRARY->generaltexth->translate("vcmi.mapGen.template.none") };
	size_t current = 0;
	const std::string chosen = templateName();
	for(size_t i = 0; i < fits.size(); ++i)
	{
		names.push_back(fits[i]);
		if(fits[i] == chosen)
			current = i + 1;
	}
	ENGINE->windows().createAndPushWindow<CObjectListWindow>(names, nullptr,
		LIBRARY->generaltexth->translate("vcmi.mapGen.template.hover"),
		LIBRARY->generaltexth->translate("vcmi.mapGen.template.choose"),
		[this, fits](int index)
		{
			Settings entry = persistentStorage.write["mapGen"]["map"]["template"];
			entry->String() = index <= 0 || index > static_cast<int>(fits.size()) ? "" : fits[index - 1];
			if(pages)
				pages->reset();
			CIntObject::redraw();
		}, current, std::vector<std::shared_ptr<IImage>>(), true);
}

void MapGenTab::generate()
{
	if(generating && *generating)
	{
		// a generator child is already running; restate its status so the
		// ignored click still gives visible feedback
		ENGINE->statusbar()->write(LIBRARY->generaltexth->translate("vcmi.mapGen.generate.running"));
		return;
	}

	// settings.json first (its schema default names the generator on this
	// install), then persistentStorage, which survives a client whose schema
	// has no mapGen section rewriting settings.json
	std::string command = settings["mapGen"]["externalGenerator"].String();
	if(command.empty() && persistentStorage["mapGen"]["externalGenerator"].isString())
		command = persistentStorage["mapGen"]["externalGenerator"].String();
	if(command.empty())
	{
		auto window = CInfoWindow::create(LIBRARY->generaltexth->translate("vcmi.mapGen.generate.notConfigured"), PlayerColor(0), {});
		ENGINE->windows().pushWindow(window);
		return;
	}

	std::mt19937 rng(std::random_device{}());
	const int seed = static_cast<int>(rng() & 0x7fffffff);
	const int size = std::clamp(mapSetting("size"), 36, 252);
	const int players = std::clamp(mapSetting("players"), 1, 8);
	const int humans = std::clamp(mapSetting("humans"), 1, players);

	const boost::filesystem::path outDir = VCMIDirs::get().userDataPath() / "Maps" / "RandomMaps";
	const boost::filesystem::path outPath = outDir /
		("mapgen_" + std::to_string(size) + "x" + std::to_string(size)
			+ "_p" + std::to_string(players) + "_s" + std::to_string(seed) + ".vmap");

	std::vector<std::string> args {
		"--w", std::to_string(size),
		"--h", std::to_string(size),
		"--players", std::to_string(players),
		"--humans", std::to_string(humans),
		"--seed", std::to_string(seed),
		"--out", outPath.string(),
	};
	if(mapSetting("underground"))
	{
		args.push_back("--underground");
		args.push_back("1");
	}
	// Always explicit, both ways: the generator's own CLI default is "on"
	// (queue: fidelity lens, 2026-09-25 - banks/dwellings read as a real
	// shortfall with declareMods defaulting off and no way to change it from
	// this tab), so the unchecked case has to say "0" out loud rather than
	// rely on omission, or unchecked would still generate with mod content.
	args.push_back("--declaremods");
	args.push_back(mapSetting("declareMods") ? "1" : "0");
	// Where this client lives, so the generator never has to guess. It used to
	// fall back to a list of common install folders, and on the dev machine the
	// first of those was an install nothing may touch. The data path is the
	// folder VCMI itself loads config and Mods from, and userDataPath includes
	// any config/dirs.json override. No trailing separator: inside the quotes
	// the .cmd branch below adds, a final backslash would escape the closing
	// quote.
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
	if(!templateName().empty())
	{
		args.push_back("--template");
		args.push_back(templateName());
	}
	// Every lever the tab has set (persistentStorage mapGen.params.*) as
	// --bio.<lever> <value>, and the preset. A lever never touched is absent
	// and the generator uses its own default.
	const JsonNode & mapGen = persistentStorage["mapGen"];
	args.push_back("--preset");
	args.push_back(mapGen["preset"].isString() && !mapGen["preset"].String().empty()
		? mapGen["preset"].String() : std::string("nostalgia"));
	for(const auto & lever : mapGen["params"].Struct())
	{
		if(!lever.second.isNumber())
			continue;
		std::ostringstream value;
		value << lever.second.Float();
		// rivers is a generator flag of its own, not a --bio lever
		args.push_back(lever.first == "rivers" ? "--rivers" : "--bio." + lever.first);
		args.push_back(value.str());
	}

	std::error_code ec;
	boost::system::error_code dirEc;
	boost::filesystem::create_directories(outDir, dirEc);
	const boost::filesystem::path logPath = VCMIDirs::get().userLogsPath() / "extmapgen_log.txt";

	// batch scripts need the shell; anything else runs directly
	const std::string ext = boost::algorithm::to_lower_copy(boost::filesystem::path(command).extension().string());
	std::unique_ptr<boost::process::child> child;
	if(ext == ".cmd" || ext == ".bat")
	{
		// cmd /c splits `cmd /c "quoted path" args` at the first space inside
		// the quotes; the form that parses is one more quote pair around the
		// whole tail: cmd /c ""path" args", passed verbatim as one string
		std::string tail = "cmd /c \"\"" + command + "\"";
		for(const auto & a : args)
		{
			tail += ' ';
			tail += (a.find_first_of(" \"") == std::string::npos) ? a : "\"" + a + "\"";
		}
		tail += '"';
		child = std::make_unique<boost::process::child>(tail, ec,
			boost::process::std_out > logPath, boost::process::std_err > logPath);
	}
	else
		child = std::make_unique<boost::process::child>(command, args, ec,
			boost::process::std_out > logPath, boost::process::std_err > logPath);

	if(ec)
	{
		logGlobal->error("MapGen: generator failed to start: %s", ec.message());
		auto window = CInfoWindow::create(LIBRARY->generaltexth->translate("vcmi.mapGen.generate.failed"), PlayerColor(0), {});
		ENGINE->windows().pushWindow(window);
		return;
	}

	logGlobal->info("MapGen: generator running: %s -> %s", command, outPath.string());
	const std::string runningText = LIBRARY->generaltexth->translate("vcmi.mapGen.generate.running");
	ENGINE->statusbar()->write(runningText);
	generating = std::make_shared<bool>(true);
	auto flag = generating;

	std::thread([flag, child = std::move(child), outPath, logPath, runningText]() mutable
	{
		child->wait();
		const int exitCode = child->exit_code();
		const bool produced = boost::filesystem::exists(outPath);
		// A generator that refuses a combination says why on a line starting
		// "Error: " (a thrown Error as node prints it), such as water that does
		// not fit the chosen template. That line beats the bare failure text.
		std::string reason;
		if(exitCode != 0 || !produced)
		{
			std::ifstream log(logPath.string());
			std::string line;
			while(std::getline(log, line))
				if(boost::algorithm::starts_with(line, "Error: "))
					reason = boost::algorithm::trim_right_copy(line.substr(7));
		}
		ENGINE->dispatchMainThread([flag, exitCode, produced, runningText, reason]()
		{
			*flag = false;
			ENGINE->statusbar()->clearIfMatching(runningText);
			if(exitCode != 0 || !produced)
			{
				const std::string text = reason.empty()
					? LIBRARY->generaltexth->translate("vcmi.mapGen.generate.failed")
					: "{" + LIBRARY->generaltexth->translate("vcmi.mapGen.generate.hover") + "}\n\n" + reason;
				auto window = CInfoWindow::create(text, PlayerColor(0), {});
				ENGINE->windows().pushWindow(window);
				return;
			}
			auto lobbies = ENGINE->windows().findWindows<CLobbyScreen>();
			if(lobbies.empty())
				return; // left the lobby mid-generation; the file is still on disk
			auto lobby = lobbies.back();
			lobby->toggleTab(lobby->tabSel);
			lobby->tabSel->showRandom = true;
			lobby->tabSel->filter(0, true);
			lobby->tabSel->selectNewestFile();
		});
	}).detach();
}
