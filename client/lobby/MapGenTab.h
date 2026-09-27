/*
 * MapGenTab.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../gui/InterfaceObjectConfigurable.h"
#include "../../lib/modding/MapGenerators.h"

class CTabbedInt;
class CLabel;

/// DMB: the tab of a map generator that a mod brings (lib/modding/MapGenerators.h), a third
/// map-selection tab beside Scenarios and Random Map, reached from its own lobby button. Everything
/// it shows comes from the mod. The mod's tab layout lists the pages and holds the defaults; each
/// page is a layout of settings-bound widgets; the texts are the mod's translations. Generate runs
/// the mod's command with those settings, once DMB's mod catalog has vouched for the mod's generator
/// folder (AddonCode::trustProblem), and selects the new map in the scenario list. The settings
/// live in persistentStorage.json (mapGen.map.*, mapGen.params.*, mapGen.preset), which no schema
/// can erase.
///
/// The command gets --w --h --players --humans --seed --out, --underground 1 on a two-level map,
/// --declaremods 1 or 0, --vcmiroot and --vcmiuserdir (the game's data and user folders),
/// --template when one is chosen, --preset, and each setting of mapGen.params as --bio.<name>
/// (rivers as --rivers). It writes a .vmap at --out. Its output goes to extmapgen_log.txt in the
/// logs folder, and a line there starting "Error: " is what the player is shown when it fails.
class MapGenPage : public InterfaceObjectConfigurable
{
public:
	/// `setup` runs before the layout is built (to add callbacks) and `after` once it is (to fill
	/// labels from settings)
	MapGenPage(const JsonNode & layout, const std::function<void(MapGenPage &)> & setup,
		const std::function<void(MapGenPage &)> & after);

	void setCallback(const std::string & name, std::function<void(int)> callback);
	std::shared_ptr<CLabel> label(const std::string & name) const;
};

class MapGenTab : public InterfaceObjectConfigurable
{
	MapGeneratorInfo generator;
	std::shared_ptr<CTabbedInt> pages;
	std::vector<std::string> pageFiles;
	JsonNode defaults;
	/// re-entry guard for the generator child, shared with its completion
	/// dispatch so it clears even if the tab is gone
	std::shared_ptr<bool> generating;

	std::shared_ptr<CIntObject> createPage(size_t index);
	void openPage(size_t index);
	void resetToDefaults();
	void chooseTemplate();
	void generate();

	int mapSetting(const std::string & key) const;
	std::string templateName() const;
	std::string preset() const;

public:
	explicit MapGenTab(const MapGeneratorInfo & generator);
};
