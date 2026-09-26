/*
 * MapGenTab.h, testinstall patch for the VCMIMapGen external generator
 * (VCMIMapGen queue item 25).
 *
 * The MapGen tab: a third top-level map-selection tab beside Scenarios and
 * Random Map, reached from its own lobby button. It holds every setting of
 * the external generator on pages (map, zones, borders, treasure, monsters,
 * underground, scenery, water), each page layout JSON of settings-bound
 * widgets (config/widgets/mapGen/, produced from the generator's own lever
 * list by VCMIMapGen tools/gen_vcmi_ui.js), and a Generate button that runs
 * the generator with those settings and selects the new map in the scenario
 * list. Its state lives in persistentStorage.json (mapGen.map.*,
 * mapGen.params.*), which no schema can erase. The stock Random Map tab is
 * left exactly as shipped.
 *
 * License: GNU General Public License v2.0 or later
 */
#pragma once

#include "../gui/InterfaceObjectConfigurable.h"

class CTabbedInt;
class CLabel;

/// One page of the tab. `setup` runs before the layout is built (to add
/// callbacks) and `after` once it is (to fill labels from settings).
class MapGenPage : public InterfaceObjectConfigurable
{
public:
	MapGenPage(const JsonPath & file, const std::function<void(MapGenPage &)> & setup,
		const std::function<void(MapGenPage &)> & after);

	void setCallback(const std::string & name, std::function<void(int)> callback);
	std::shared_ptr<CLabel> label(const std::string & name) const;
};

class MapGenTab : public InterfaceObjectConfigurable
{
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

public:
	MapGenTab();
};
