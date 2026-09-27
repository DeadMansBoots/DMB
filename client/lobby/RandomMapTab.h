/*
 * RandomMapTab.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "CSelectionBase.h"

#include "../../lib/FunctionList.h"
#include "../../lib/GameConstants.h"
#include "../../lib/rmg/CRmgTemplate.h"
#include "../gui/InterfaceObjectConfigurable.h"

#include <optional>

VCMI_LIB_NAMESPACE_BEGIN

class CMapGenOptions;
class MetaString;

VCMI_LIB_NAMESPACE_END

class CToggleButton;
class CLabel;
class CLabelGroup;
class CSlider;
class CPicture;
class CTextInput;
class TransparentFilledRectangle;

class RandomMapTab : public InterfaceObjectConfigurable
{
public:
	RandomMapTab();

	void updateMapInfoByHost();
	/// The lobby's entry for a random map the options describe, named as given (DMB: a map generator
	/// mod's tab makes its own, MapGenTab)
	static std::shared_ptr<CMapInfo> createRandomMapInfo(const CMapGenOptions & options, const MetaString & mapName, const MetaString & mapDescription);
	void setMapGenOptions(std::shared_ptr<CMapGenOptions> opts);
	void setTemplate(const CRmgTemplate *);

	void saveOptions(const CMapGenOptions & options);
	void loadOptions();
	CMapGenOptions & obtainMapGenOptions() {return *mapGenOptions;}
	bool isCustomMapSizeMode() const { return customMapSizeMode; }
	size_t getCustomMapSizeIconFrame() const;

	CFunctionList<void(std::shared_ptr<CMapInfo>, std::shared_ptr<CMapGenOptions>)> mapInfoChanged;

private:
	void deactivateButtonsFrom(CToggleGroup & group, const std::set<int> & allowed);
	std::vector<int> getStandardMapSizes() const;
	std::optional<int> getMapSizeForButtonId(int btnId) const;
	bool isCustomSizeButtonId(int btnId) const;
	void onToggleMapSize(int btnId);

	std::shared_ptr<CMapInfo> mapInfo;
	std::shared_ptr<CMapGenOptions> mapGenOptions;
	
	//options allowed - need to store as impact each other
	std::set<int> playerCountAllowed;
	std::set<int> playerTeamsAllowed;
	std::set<int> compCountAllowed;
	std::set<int> compTeamsAllowed;
	bool customMapSizeMode = false;

	int templateIndex;
};

/// DMB: a team for each player, in colour order, as the team grid's OK hands them on
using TeamChoice = std::function<void(const std::vector<TeamID> &)>;

class TeamAlignmentsWidget: public InterfaceObjectConfigurable
{
public:
	/// DMB: the grid for the players `options` has; OK hands the teams to `apply` (RandomMapTab sets them
	/// on its options, a map generator mod's tab stores them). `options` is read here only.
	TeamAlignmentsWidget(const CMapGenOptions & options, TeamChoice apply);

private:
	void checkTeamCount();

	TeamChoice apply;

	std::shared_ptr<CFilledTexture> background;
	std::shared_ptr<CLabelGroup> labels;
	std::shared_ptr<CButton> buttonOk;
	std::shared_ptr<CButton> buttonCancel;
	std::vector<std::shared_ptr<CToggleGroup>> players;
	std::vector<std::shared_ptr<CIntObject>> placeholders;
};

class TeamAlignments: public CWindowObject
{
	std::shared_ptr<TeamAlignmentsWidget> widget;
public:
	TeamAlignments(RandomMapTab & randomMapTab);
	TeamAlignments(const CMapGenOptions & options, TeamChoice apply);
};

class SetSizeWindow: public CWindowObject
{
	std::shared_ptr<FilledTexturePlayerColored> background;
	std::vector<std::shared_ptr<CLabel>> titles;
	std::shared_ptr<CButton> buttonOk;
	std::shared_ptr<CButton> buttonCancel;
	std::vector<std::shared_ptr<CLabel>> sizeLabels;

	std::vector<std::shared_ptr<CTextInput>> numInputs;
	std::vector<std::shared_ptr<TransparentFilledRectangle>> rectangles;
public:
	SetSizeWindow(int3 initSize, const CRmgTemplate * mapTemplate, std::function<void(int3)> cb);
};
