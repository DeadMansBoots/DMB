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
#include "RandomMapTab.h"
#include "SelectionTab.h"

#include "../CServerHandler.h"
#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../gui/Shortcut.h"
#include "../gui/WindowHandler.h"
#include "../render/Colors.h"
#include "../widgets/Buttons.h"
#include "../widgets/CTextInput.h"
#include "../widgets/GraphicalPrimitiveCanvas.h"
#include "../widgets/Images.h"
#include "../widgets/ObjectLists.h"
#include "../widgets/TextControls.h"
#include "../windows/GUIClasses.h"
#include "../windows/InfoWindows.h"

#include "../../lib/CConfigHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/VCMIDirs.h"
#include "../../lib/filesystem/Filesystem.h"
#include "../../lib/modding/AddonCode.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/modding/ModDescription.h"
#include "../../lib/rmg/CMapGenOptions.h"
#include "../../lib/rmg/CRmgTemplate.h"
#include "../../lib/rmg/CRmgTemplateStorage.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include "../../lib/texts/MetaString.h"

#include <fstream>
#include <random>
#include <thread>

#include <boost/algorithm/string.hpp>

namespace
{
/// asks for the name to save the tab's settings under, as VCMI's custom size window asks for a size
class PresetNameWindow : public CWindowObject
{
	std::shared_ptr<FilledTexturePlayerColored> background;
	std::shared_ptr<CLabel> title;
	std::shared_ptr<TransparentFilledRectangle> field;
	std::shared_ptr<CTextInput> name;
	std::shared_ptr<CButton> buttonOk;
	std::shared_ptr<CButton> buttonCancel;

public:
	PresetNameWindow(const std::string & heading, std::function<void(const std::string &)> onOk)
		: CWindowObject(BORDERED)
	{
		OBJECT_CONSTRUCTION;
		pos.w = 300;
		pos.h = 130;
		updateShadow();
		center();

		background = std::make_shared<FilledTexturePlayerColored>(Rect(0, 0, pos.w, pos.h));
		background->setPlayerColor(PlayerColor(1));
		title = std::make_shared<CLabel>(150, 20, FONT_BIG, ETextAlignment::CENTER, Colors::YELLOW, heading);
		field = std::make_shared<TransparentFilledRectangle>(Rect(20, 45, 260, 22), ColorRGBA(0, 0, 0, 128), ColorRGBA(64, 64, 64, 64), 1);
		name = std::make_shared<CTextInput>(Rect(24, 47, 252, 18), FONT_SMALL, ETextAlignment::CENTERLEFT, true);
		buttonOk = std::make_shared<CButton>(Point(70, 85), AnimationPath::builtin("MuBchck"), CButton::tooltip(), [this, onOk]()
		{
			const std::string typed = name->getText();
			close();
			onOk(typed);
		}, EShortcut::GLOBAL_ACCEPT);
		buttonCancel = std::make_shared<CButton>(Point(160, 85), AnimationPath::builtin("MuBcanc"), CButton::tooltip(), [this]() { close(); }, EShortcut::GLOBAL_CANCEL);
	}
};
}

/// the template a generator picks for itself from those that fit (OmniMapGen reads "random" so)
static const std::string RANDOM_TEMPLATE = "random";

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

void MapGenPage::setOnSettingChanged(std::function<void(const std::string &)> callback)
{
	onSettingChanged = std::move(callback);
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
	addCallback("generateMapGenMap", [this](int)
	{
		if(!generator.atBegin)
			generate();
		else // the map is made when the game starts; a layout's leftover Generate button only says so
			ENGINE->statusbar()->write(tabText("generate.atBegin"));
	});
	addCallback("chooseMapGenTemplate", [this](int) { chooseTemplate(); });
	addCallback("chooseMapGenTeams", [this](int) { chooseTeams(); });
	addCallback("chooseMapGenCustomSize", [this](int) { chooseCustomSize(); });
	addCallback("saveMapGenPreset", [this](int) { savePreset(); });
	addCallback("loadMapGenPreset", [this](int) { loadPreset(); });
	// every setting the generator gets goes to the server with the lobby's random map, so a change puts
	// that together again (at Begin it would be too late: a new map resets the towns picked)
	onSettingChanged = [this](const std::string & setting)
	{
		if(setting == "persistent:mapGen/map/size")
			clearCustomSize(); // a standard size picked after a custom one replaces it
		if(boost::algorithm::starts_with(setting, "persistent:mapGen/"))
			updateMapInfoByHost();
	};
	// a "pages" widget's page layouts are the mod's files too, and a page that shows the chosen
	// template by the label's name (layouts before the settings-bound label) gets it filled in
	layoutScope = generator.modID;
	onPageBuilt = [this](LayoutPage & page)
	{
		if(auto name = page.find<CLabel>("labelTemplateName"))
			name->setText(templateLabel());
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
			page.setCallback("chooseMapGenTeams", [this](int) { chooseTeams(); });
			page.setCallback("chooseMapGenCustomSize", [this](int) { chooseCustomSize(); });
			page.setCallback("saveMapGenPreset", [this](int) { savePreset(); });
			page.setCallback("loadMapGenPreset", [this](int) { loadPreset(); });
			page.setOnSettingChanged(onSettingChanged);
		},
		[this](MapGenPage & page)
		{
			if(auto name = page.label("labelTemplateName"))
				name->setText(templateLabel());
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
	{
		Settings chosenPreset = persistentStorage.write["mapGen"]["preset"];
		chosenPreset->String() = defaults["preset"].isString() ? defaults["preset"].String() : std::string();
	}
	// a custom size the layout's defaults do not name goes too
	const JsonNode & layoutDefaults = defaults;
	if(!layoutDefaults["map"]["width"].isNumber() || !layoutDefaults["map"]["height"].isNumber())
		clearCustomSize();
	refreshPages();
	updateMapInfoByHost();
}

void MapGenTab::refreshPages()
{
	if(pages)
		pages->reset();
	if(layoutPages)
		layoutPages->refresh();
	CIntObject::redraw();
}

const MapGeneratorInfo & MapGenTab::getGenerator() const
{
	return generator;
}

void MapGenTab::updateMapInfoByHost()
{
	if(!generator.atBegin || GAME->server().isGuest())
		return;

	const auto options = lobbyOptions();
	MetaString name;
	name.appendRawString(generator.name);
	MetaString description;
	description.appendRawString(LIBRARY->modh->getModInfo(generator.modID).getLocalizedDescription().String());
	GAME->server().setMapInfo(RandomMapTab::createRandomMapInfo(*options, name, description), options);
}

std::shared_ptr<CMapGenOptions> MapGenTab::lobbyOptions() const
{
	const JsonNode & stored = persistentStorage["mapGen"];
	// read through a const reference, so a key the layout lacks is not added to it as null
	const JsonNode & layoutDefaults = defaults;
	const auto number = [&stored, &layoutDefaults](const std::string & section, const std::string & key, int fallback)
	{
		const JsonNode & value = stored[section][key].isNumber() ? stored[section][key] : layoutDefaults[section][key];
		return value.isNumber() ? static_cast<int>(value.Integer()) : fallback;
	};

	auto options = std::make_shared<CMapGenOptions>();
	const int size = std::clamp(number("map", "size", 108), 36, 252);
	const int width = number("map", "width", 0);
	const int height = number("map", "height", 0);
	const bool custom = width > 0 && height > 0;
	options->setWidth(custom ? width : size);
	options->setHeight(custom ? height : size);
	options->setLevels(number("map", "underground", 0) ? 2 : 1);
	// the lobby's slots: map/humans a human or the computer may take, at least one for everyone in the
	// lobby (friends who joined, or hotseat names), as the Generate button's maps have; params/compOnly
	// only the computer, or, in a layout without it, the rest of map/players
	int humans = number("map", "humans", 1);
	const int inLobby = std::clamp<int>(static_cast<int>(GAME->server().playerNames.size()), 1, PlayerColor::PLAYER_LIMIT_I);
	if(humans >= 0 && humans < inLobby)
		humans = inLobby;
	const bool hasCompOnly = stored["params"]["compOnly"].isNumber() || layoutDefaults["params"]["compOnly"].isNumber();
	const int compOnly = hasCompOnly ? number("params", "compOnly", 0) : std::max(0, number("map", "players", 0) - std::max(humans, 1));
	// A count left on Random is rolled now, not at Begin as VCMI's own random map does: the lobby then
	// has exactly the game's players, on the first colours, where a generator places its players (a
	// player on a later colour would have no place on the map). The lobby shows what was rolled.
	std::mt19937 roll(std::random_device{}());
	const auto rolled = [&roll](int low, int high)
	{
		return high <= low ? low : std::uniform_int_distribution<int>(low, high)(roll);
	};
	const int humanSlots = humans < 0 ? rolled(inLobby, PlayerColor::PLAYER_LIMIT_I) : std::clamp<int>(humans, 1, PlayerColor::PLAYER_LIMIT_I);
	options->setHumanOrCpuPlayerCount(humanSlots);
	const int room = PlayerColor::PLAYER_LIMIT_I - humanSlots;
	options->setCompOnlyPlayerCount(compOnly < 0 ? rolled(humanSlots >= 2 ? 0 : 1, room) : std::clamp(compOnly, 0, room));
	const JsonNode & teams = stored["map"]["teams"];
	if(teams.isVector())
	{
		const auto players = options->getPlayersSettings();
		for(const auto & player : players)
		{
			const size_t index = player.first.getNum();
			if(index < teams.Vector().size() && teams.Vector()[index].isNumber() && teams.Vector()[index].Integer() >= 0)
				options->setPlayerTeam(player.first, TeamID(static_cast<int>(teams.Vector()[index].Integer())));
		}
	}
	// the settings the generator gets, which go to the server with the options: the map's (stored over
	// the layout's defaults), the params only as stored (the generator keeps its own default for the
	// rest), and the preset
	JsonNode chosen;
	chosen["map"] = layoutDefaults["map"];
	for(const auto & entry : stored["map"].Struct())
		chosen["map"][entry.first] = entry.second;
	chosen["params"] = stored["params"];
	chosen["preset"].String() = preset();
	options->setExternalGenerator(generator.modID, chosen);
	return options;
}

void MapGenTab::chooseTeams()
{
	// VCMI's own team grid, whose layout base VCMI lacks: the VCMI Extras mod's extended lobby brings it
	if(!CResourceHandler::get()->existsResource(JsonPath::builtin("config/widgets/randomMapTeamsWidget.json")))
	{
		logGlobal->warn("Map generator %s: the team grid's layout (config/widgets/randomMapTeamsWidget.json) is missing", generator.name);
		ENGINE->statusbar()->write(tabText("teams.missing"));
		return;
	}
	// the grid is for the players of the lobby's random map while it is this generator's (the counts as
	// rolled), else of the tab's settings; the teams are stored a player each, in colour order
	auto options = GAME->server().si->mapGenOptions;
	if(!options || options->getExternalGenerator() != generator.modID)
		options = lobbyOptions();
	ENGINE->windows().createAndPushWindow<TeamAlignments>(*options, [this](const std::vector<TeamID> & teams)
	{
		{
			Settings stored = persistentStorage.write["mapGen"]["map"]["teams"];
			stored->Vector().clear();
			for(const auto & team : teams)
			{
				JsonNode number;
				number.Integer() = team.getNum();
				stored->Vector().push_back(number);
			}
		}
		refreshPages();
		updateMapInfoByHost();
	});
}

void MapGenTab::chooseCustomSize()
{
	// VCMI's own custom size window, with no template's limits: the generator fits any template to any size
	const int width = mapSetting("width");
	const int height = mapSetting("height");
	const int size = std::clamp(mapSetting("size"), 36, 252);
	const bool custom = width > 0 && height > 0;
	const int3 current(custom ? width : size, custom ? height : size, mapSetting("underground") ? 2 : 1);
	ENGINE->windows().createAndPushWindow<SetSizeWindow>(current, nullptr, [this](int3 chosen)
	{
		{
			Settings width = persistentStorage.write["mapGen"]["map"]["width"];
			width->Integer() = chosen.x;
			Settings height = persistentStorage.write["mapGen"]["map"]["height"];
			height->Integer() = chosen.y;
			// a generator makes one or two levels
			Settings underground = persistentStorage.write["mapGen"]["map"]["underground"];
			underground->Integer() = chosen.z > 1 ? 1 : 0;
			// no standard size is picked now, so the size row shows none, and picking one there replaces this
			Settings size = persistentStorage.write["mapGen"]["map"]["size"];
			size->Integer() = 0;
		}
		refreshPages();
		updateMapInfoByHost();
	});
}

boost::filesystem::path MapGenTab::presetFolder() const
{
	return VCMIDirs::get().userDataPath() / "MapGenPresets" / generator.modID;
}

void MapGenTab::savePreset()
{
	ENGINE->windows().createAndPushWindow<PresetNameWindow>(tabText("presets.saveTitle"), [this](const std::string & typed)
	{
		// the file takes the name typed, with the characters Windows refuses in a file name as _
		std::string name = boost::algorithm::trim_copy(typed);
		for(auto & c : name)
			if(std::string("\\/:*?\"<>|").find(c) != std::string::npos || static_cast<unsigned char>(c) < 32)
				c = '_';
		if(name.empty())
			return;

		// everything the tab stores for the generator; the page shown is not a setting
		const JsonNode & stored = persistentStorage["mapGen"];
		JsonNode preset;
		preset["map"] = stored["map"];
		preset["params"] = stored["params"];
		preset["preset"] = stored["preset"];
		boost::system::error_code ec;
		boost::filesystem::create_directories(presetFolder(), ec);
		const auto path = presetFolder() / (name + ".json");
		std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
		out << preset.toString();
		out.close();

		MetaString text;
		text.appendRawString(tabText(out.fail() ? "presets.failed" : "presets.saved"));
		text.replaceRawString(name);
		ENGINE->statusbar()->write(text.toString());
		logGlobal->info("Map generator %s: settings %s to %s", generator.name, out.fail() ? "not saved" : "saved", path.string());
	});
}

void MapGenTab::loadPreset()
{
	std::vector<std::string> names;
	boost::system::error_code ec;
	for(boost::filesystem::directory_iterator it(presetFolder(), ec), end; !ec && it != end; it.increment(ec))
		if(boost::algorithm::iequals(it->path().extension().string(), ".json"))
			names.push_back(it->path().stem().string());
	std::sort(names.begin(), names.end());
	if(names.empty())
	{
		ENGINE->statusbar()->write(tabText("presets.none"));
		return;
	}
	ENGINE->windows().createAndPushWindow<CObjectListWindow>(names, nullptr, tabText("presets.loadTitle"), tabText("presets.loadHelp"),
		[this, names](int index)
		{
			if(index < 0 || index >= static_cast<int>(names.size()))
				return;
			const auto path = presetFolder() / (names[index] + ".json");
			std::ifstream in(path.c_str(), std::ios::binary);
			const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			const JsonNode preset(reinterpret_cast<const std::byte *>(text.data()), text.size(), path.string());
			// the saved settings in place of the tab's, as they were saved
			{
				Settings map = persistentStorage.write["mapGen"]["map"];
				*map.operator->() = preset["map"].isStruct() ? preset["map"] : JsonNode();
				Settings params = persistentStorage.write["mapGen"]["params"];
				*params.operator->() = preset["params"].isStruct() ? preset["params"] : JsonNode();
				Settings chosenPreset = persistentStorage.write["mapGen"]["preset"];
				chosenPreset->String() = preset["preset"].isString() ? preset["preset"].String() : std::string();
			}
			refreshPages();
			updateMapInfoByHost();

			MetaString loaded;
			loaded.appendRawString(tabText("presets.loaded"));
			loaded.replaceRawString(names[index]);
			ENGINE->statusbar()->write(loaded.toString());
			logGlobal->info("Map generator %s: settings loaded from %s", generator.name, path.string());
		}, 0, std::vector<std::shared_ptr<IImage>>(), true);
}

void MapGenTab::clearCustomSize()
{
	Settings width = persistentStorage.write["mapGen"]["map"]["width"];
	width->Integer() = 0;
	Settings height = persistentStorage.write["mapGen"]["map"]["height"];
	height->Integer() = 0;
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

std::string MapGenTab::templateLabel() const
{
	const std::string chosen = templateName();
	if(chosen.empty())
		return tabText("template.none");
	return chosen == RANDOM_TEMPLATE ? tabText("template.random") : chosen;
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
	// every template of the install, whatever the map's size, levels and players: the generator fits
	// the template to the map instead of refusing it (MapGen, September 27th, for K's "never refuse").
	// The first entry is the generator's own layout.
	std::vector<std::string> fits;
	for(const auto * tpl : LIBRARY->tplh->getTemplates())
		if(tpl)
			fits.push_back(tpl->getName());
	std::sort(fits.begin(), fits.end());
	fits.erase(std::unique(fits.begin(), fits.end()), fits.end());

	// then Random, which the generator rolls from the templates that fit (MapGen's "random"), then each
	std::vector<std::string> names { tabText("template.none"), tabText("template.random") };
	const std::string chosen = templateName();
	size_t current = chosen == RANDOM_TEMPLATE ? 1 : 0;
	for(size_t i = 0; i < fits.size(); ++i)
	{
		names.push_back(fits[i]);
		if(fits[i] == chosen)
			current = i + 2;
	}
	ENGINE->windows().createAndPushWindow<CObjectListWindow>(names, nullptr,
		tabText("template.hover"),
		tabText("template.choose"),
		[this, fits](int index)
		{
			{
				Settings entry = persistentStorage.write["mapGen"]["map"]["template"];
				if(index == 1)
					entry->String() = RANDOM_TEMPLATE;
				else
					entry->String() = index <= 1 || index > static_cast<int>(fits.size()) + 1 ? "" : fits[index - 2];
			}
			refreshPages();
			updateMapInfoByHost();
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

		logGlobal->info("Map generator %s (mod %s) running: %s -> %s", gen.name, gen.modID, gen.command.string(), outPath.string());
		int exitCode = -1;
		try
		{
			exitCode = MapGenerators::run(gen, args, logPath, std::chrono::minutes(10));
		}
		catch(const std::exception & e)
		{
			logGlobal->error("Map generator %s: %s", gen.name, e.what());
			failWith(tabText("generate.failed"));
			return;
		}
		if(exitCode != 0 || !boost::filesystem::exists(outPath))
		{
			// A generator that refuses a combination says why on a line starting "Error: ", such as
			// water that does not fit the chosen template. That line beats the bare failure text.
			const std::string reason = MapGenerators::errorLine(logPath);
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
