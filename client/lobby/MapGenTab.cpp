/*
 * MapGenTab.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "MapGenTab.h"

#include "CLobbyScreen.h"
#include "SelectionTab.h"

#include "../CServerHandler.h"
#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../gui/WindowHandler.h"
#include "../widgets/Buttons.h"
#include "../widgets/ObjectLists.h"
#include "../widgets/TextControls.h"
#include "../windows/GUIClasses.h"
#include "../windows/InfoWindows.h"

#include "../../lib/CConfigHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/VCMIDirs.h"
#include "../../lib/filesystem/Filesystem.h"
#include "../../lib/modding/AddonCode.h"
#include "../../lib/rmg/CRmgTemplate.h"
#include "../../lib/rmg/CRmgTemplateStorage.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include "../../lib/texts/MetaString.h"

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

/// One of the texts the tab itself shows: the mod's own wording ("vcmi.mapGen.<name>", from its
/// translation) when it brings one, else DMB's generic text ("vcmi.dmb.mapGenerator.<name>"). The
/// two never share a key, so the mod's wording wins whatever the mods' load order.
static std::string tabText(const std::string & name)
{
	const std::string modKey = "vcmi.mapGen." + name;
	if(LIBRARY->generaltexth->identifierExists(modKey))
		return LIBRARY->generaltexth->translate(modKey);
	return LIBRARY->generaltexth->translate("vcmi.dmb.mapGenerator." + name);
}

MapGenPage::MapGenPage(const JsonNode & layout, const std::function<void(MapGenPage &)> & setup,
	const std::function<void(MapGenPage &)> & after)
	: InterfaceObjectConfigurable()
{
	OBJECT_CONSTRUCTION;
	// A page has no background of its own: when a value label changes, the tab, which has one,
	// repaints under it. Otherwise the old text stays under the new ("KL" for L, then XL).
	setRedrawParent(true);
	// a page of many sliders: the arrow keys would move them all at once, as VCMI gives each the keyboard
	slidersTakeKeys = false;
	if(setup)
		setup(*this);
	build(layout);
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

MapGenTab::MapGenTab(const MapGeneratorInfo & info)
	: InterfaceObjectConfigurable()
	, generator(info)
{
	OBJECT_CONSTRUCTION;
	recActions = 0; // hidden until its lobby button opens it, like the other tabs

	// the mod's own filesystem, so a file of the same name in another mod never stands in
	const JsonNode config(JsonPath::builtin(generator.tab), generator.modID);
	for(const auto & page : config["pages"].Vector())
		pageFiles.push_back(page.String());
	defaults = config["defaults"];

	addCallback("activateMapGenPage", [this](int index) { openPage(index); });
	addCallback("resetMapGenDefaults", [this](int) { resetToDefaults(); });
	addCallback("generateMapGenMap", [this](int) { generate(); });
	addCallback("chooseMapGenTemplate", [this](int) { chooseTemplate(); });
	// a "pages" widget's page layouts are the mod's files too, and a page that shows the chosen
	// template by the label's name (layouts before the settings-bound label) gets it filled in
	layoutScope = generator.modID;
	onPageBuilt = [this](LayoutPage & page)
	{
		if(auto name = page.find<CLabel>("labelTemplateName"))
		{
			const std::string chosen = templateName();
			name->setText(chosen.empty() ? tabText("template.none") : chosen);
		}
	};
	build(config);

	layoutPages = widget<LayoutPages>("pages");
	if(layoutPages || pageFiles.empty())
		return;

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
	if(index >= pageFiles.size() || !CResourceHandler::get(generator.modID)->existsResource(JsonPath::builtin(pageFiles[index])))
	{
		logGlobal->error("Map generator %s: page %d is missing from the mod %s", generator.name, static_cast<int>(index), generator.modID);
		return std::make_shared<CIntObject>();
	}
	return std::make_shared<MapGenPage>(JsonNode(JsonPath::builtin(pageFiles[index]), generator.modID),
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
					? tabText("template.none") : chosen);
			}
		});
}

void MapGenTab::openPage(size_t index)
{
	if(layoutPages)
	{
		layoutPages->showPage(index);
		return;
	}
	if(!pages || index >= pageFiles.size())
		return;
	pages->setActive(index);
	CIntObject::redraw();

	Settings lastPage = persistentStorage.write["mapGen"]["lastPage"];
	lastPage->Integer() = static_cast<int>(index);
}

void MapGenTab::resetToDefaults()
{
	// every setting back to the generator's own default, from the mod's tab layout, then the
	// open page is rebuilt so it shows them
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
	Settings chosenPreset = persistentStorage.write["mapGen"]["preset"];
	chosenPreset->String() = defaults["preset"].isString() ? defaults["preset"].String() : std::string();
	if(pages)
		pages->reset();
	if(layoutPages)
		layoutPages->refresh();
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

std::string MapGenTab::preset() const
{
	const JsonNode & stored = persistentStorage["mapGen"]["preset"];
	if(stored.isString() && !stored.String().empty())
		return stored.String();
	return defaults["preset"].isString() ? defaults["preset"].String() : std::string();
}

void MapGenTab::chooseTemplate()
{
	// the install's templates that take this map size, level count and
	// player count, the way the Random Map tab filters its own list; the
	// first entry is the generator's own layout
	const int size = mapSetting("size");
	const int levels = mapSetting("underground") ? 2 : 1;
	const int players = mapSetting("players");
	std::vector<std::string> fits;
	for(const auto * tpl : LIBRARY->tplh->getTemplates())
		if(tpl && tpl->matchesSize(int3(size, size, levels)) && tpl->getPlayers().isInRange(players))
			fits.push_back(tpl->getName());
	std::sort(fits.begin(), fits.end());

	std::vector<std::string> names { tabText("template.none") };
	size_t current = 0;
	const std::string chosen = templateName();
	for(size_t i = 0; i < fits.size(); ++i)
	{
		names.push_back(fits[i]);
		if(fits[i] == chosen)
			current = i + 1;
	}
	ENGINE->windows().createAndPushWindow<CObjectListWindow>(names, nullptr,
		tabText("template.hover"),
		tabText("template.choose"),
		[this, fits](int index)
		{
			Settings entry = persistentStorage.write["mapGen"]["map"]["template"];
			entry->String() = index <= 0 || index > static_cast<int>(fits.size()) ? "" : fits[index - 1];
			if(pages)
				pages->reset();
			if(layoutPages)
				layoutPages->refresh();
			CIntObject::redraw();
		}, current, std::vector<std::shared_ptr<IImage>>(), true);
}

void MapGenTab::generate()
{
	if(generating && *generating)
	{
		// a generator child is already running; restate its status so the
		// ignored click still gives visible feedback
		ENGINE->statusbar()->write(tabText("generate.running"));
		return;
	}

	std::mt19937 rng(std::random_device{}());
	const int seed = static_cast<int>(rng() & 0x7fffffff);
	const int size = std::clamp(mapSetting("size"), 36, 252);
	int players = std::clamp(mapSetting("players"), 1, 8);
	int humans = std::clamp(mapSetting("humans"), 1, players);
	// DMB: a map with fewer human slots than the players in the lobby cannot be played there, and a
	// multiplayer lobby does not even list it, so the host saw the map made and then never offered. It
	// gets a human slot for everyone in the lobby (friends who joined, or hotseat names), and more
	// players if that takes more.
	std::string humansNote;
	const int inLobby = std::clamp(static_cast<int>(GAME->server().playerNames.size()), 1, 8);
	if(humans < inLobby)
	{
		logGlobal->info("Map generator: %d players are in the lobby, so the map gets %d human slots instead of %d", inLobby, inLobby, humans);
		MetaString note;
		note.appendRawString(tabText("generate.humans"));
		note.replaceNumber(inLobby);
		humansNote = " " + note.toString();
		humans = inLobby;
		players = std::max(players, humans);
	}

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
	// always explicit, both ways, so a generator whose own default is "on" still hears "off"
	args.push_back("--declaremods");
	args.push_back(mapSetting("declareMods") ? "1" : "0");
	// Where this client lives, so the generator never has to guess: the folder VCMI itself loads
	// config and Mods from, and the user folder (config/dirs.json included). No trailing separator:
	// inside the quotes the .cmd branch below adds, a final backslash would escape the closing quote.
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
	if(!preset().empty())
	{
		args.push_back("--preset");
		args.push_back(preset());
	}
	// every setting the tab has stored (persistentStorage mapGen.params.*) as --bio.<name> <value>;
	// one never touched is absent, and the generator uses its own default
	for(const auto & lever : persistentStorage["mapGen"]["params"].Struct())
	{
		if(!lever.second.isNumber())
			continue;
		std::ostringstream value;
		value << lever.second.Float();
		// rivers is a generator flag of its own, not a --bio setting
		args.push_back(lever.first == "rivers" ? "--rivers" : "--bio." + lever.first);
		args.push_back(value.str());
	}

	boost::system::error_code dirEc;
	boost::filesystem::create_directories(outDir, dirEc);
	const boost::filesystem::path logPath = VCMIDirs::get().userLogsPath() / "extmapgen_log.txt";

	const std::string runningText = tabText("generate.running") + humansNote;
	ENGINE->statusbar()->write(runningText);
	generating = std::make_shared<bool>(true);
	auto flag = generating;

	std::thread([flag, gen = generator, args, outPath, logPath, runningText]()
	{
		const auto failWith = [flag, runningText](const std::string & text)
		{
			ENGINE->dispatchMainThread([flag, runningText, text]()
			{
				*flag = false;
				ENGINE->statusbar()->clearIfMatching(runningText);
				ENGINE->windows().pushWindow(CInfoWindow::create(text, PlayerColor(0), {}));
			});
		};
		const std::string title = "{" + tabText("generate.hover") + "}\n\n";

		// DMB's mod catalog must vouch for the generator's code first. The check hashes the whole
		// generator folder, so it runs here, off the game's main thread.
		const std::string refused = AddonCode::trustProblem(gen.modID, gen.codeFolder);
		if(!refused.empty())
		{
			logGlobal->warn("Map generator %s (mod %s) cannot run: %s", gen.name, gen.modID, refused);
			MetaString text;
			text.appendRawString(tabText("generate.refused"));
			text.replaceRawString(gen.name);
			text.replaceRawString(refused);
			failWith(title + text.toString());
			return;
		}

		// batch scripts need the shell; anything else runs directly
		const std::string command = gen.command.string();
		const std::string ext = boost::algorithm::to_lower_copy(gen.command.extension().string());
		std::error_code ec;
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
			// one handle for both streams: two handles on the file each write from its start and
			// overwrite each other, the "Error: " line the player is shown included (MapGen's finding)
			child = std::make_unique<boost::process::child>(tail, ec,
				(boost::process::std_out & boost::process::std_err) > logPath);
		}
		else
			child = std::make_unique<boost::process::child>(command, args, ec,
				(boost::process::std_out & boost::process::std_err) > logPath);

		if(ec)
		{
			logGlobal->error("Map generator %s: failed to start %s: %s", gen.name, command, ec.message());
			failWith(tabText("generate.failed"));
			return;
		}
		logGlobal->info("Map generator %s (mod %s) running: %s -> %s", gen.name, gen.modID, command, outPath.string());

		child->wait();
		const int exitCode = child->exit_code();
		const bool produced = boost::filesystem::exists(outPath);
		if(exitCode != 0 || !produced)
		{
			// A generator that refuses a combination says why on a line starting "Error: ", such as
			// water that does not fit the chosen template. That line beats the bare failure text.
			std::string reason;
			std::ifstream log(logPath.string());
			std::string line;
			while(std::getline(log, line))
				if(boost::algorithm::starts_with(line, "Error: "))
					reason = boost::algorithm::trim_right_copy(line.substr(7));
			failWith(reason.empty() ? tabText("generate.failed") : title + reason);
			return;
		}

		ENGINE->dispatchMainThread([flag, runningText]()
		{
			*flag = false;
			ENGINE->statusbar()->clearIfMatching(runningText);
			auto lobbies = ENGINE->windows().findWindows<CLobbyScreen>();
			if(lobbies.empty())
				return; // left the lobby mid-generation; the file is still on disk
			auto lobby = lobbies.back();
			lobby->toggleTab(lobby->tabSel);
			lobby->tabSel->showRandom = true;
			// the list was read when the lobby opened, so it is read again for the new map to be in it
			lobby->tabSel->toggleMode();
			lobby->tabSel->filter(0, true);
			lobby->tabSel->selectNewestFile();
		});
	}).detach();
}
