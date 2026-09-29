/*
 * CLobbyScreen.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "CSelectionBase.h"

class CBonusSelection;
class GraphicalPrimitiveCanvas;
class MapGenTab;

class CLobbyScreen final : public CSelectionBase
{
public:
	CLobbyScreen(ESelectionScreen type, bool hideScreen = false);
	~CLobbyScreen();
	void toggleTab(std::shared_ptr<CIntObject> tab) final;
	void start(bool campaign);
	void startCampaign();
	void startScenario(bool allowOnlyAI = false);
	void toggleMode(bool host);
	void toggleChat();

	void updateAfterStateChange();
	void onRemoteClientLobbyStateChanged();

	const CMapInfo * getMapInfo() final;
	const StartInfo * getStartInfo() final;

	std::shared_ptr<CBonusSelection> bonusSel;

private:
	std::shared_ptr<CButton> buttonChat;
	std::shared_ptr<GraphicalPrimitiveCanvas> blackScreen;
	// DMB: the tabs of the map generator mods (MapGenTab.h), in load order, and the map generation mode:
	// 0 for VCMI's own random map, n for the nth generator
	std::vector<std::shared_ptr<MapGenTab>> generatorTabs;
	size_t randomMode = 0;
	// DMB: the mode's golden arrows and name, at the top of the random map window, where the window's
	// own headline sits (its first row of settings is lower: config/widgets/randomMapTab.json)
	static constexpr int MODE_ARROW_LEFT = 66;
	static constexpr int MODE_ARROW_RIGHT = 362;
	// K, September 29th: "arrows and omni map gen text still sitting on the bottom of their box". Measured at
	// his screen (3440x1440, UI 200%), in this layout's pixels: the box is 22 to 47 (centre 34.5); the arrows'
	// gold glyph stood 33.5 to 41.5 and the name's capitals 35.5 to 45.5, both low, the descender of the "p"
	// below the box's edge. The arrows now stand with their glyph centred at 34.5, and the name's centre line
	// sits where its capitals centre there too (they stand 2.5 below it)
	static constexpr int MODE_ARROW_TOP = 27;
	static constexpr int MODE_NAME_MIDDLE = 32;
	static constexpr int MODE_NAME_CENTRE = 222;
	std::shared_ptr<CButton> modePrevious;
	std::shared_ptr<CButton> modeNext;
	std::shared_ptr<CLabel> modeName;

	void pressRandomMap();
	void stepRandomMode(int step);
	void showRandomMode(bool modeChanged);
	std::shared_ptr<MapGenTab> modeGenerator() const;
	bool randomWindowShown() const;
	void updateModeBar();

	bool waitingForPlayersMessageShown = false;
	bool compatibilityFilterInitialized = false;
	size_t lastRequiredHumanPlayers = 0;
	std::string lastCompatibilityNotice;

	bool isMultiplayerNetworkLobby() const;
	bool isMultiplayerHost() const;
	bool canStartLobbyGame() const;
	bool isLanOrOnlineMultiplayerHost() const;
	void updateCompatibilityNotice(size_t requiredHumanPlayers);
	void updateHostLobbyChatState();
	void updateStartButtonState();
};
