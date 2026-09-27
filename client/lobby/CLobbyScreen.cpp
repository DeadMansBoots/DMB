/*
 * CLobbyScreen.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "CLobbyScreen.h"

#include "CBonusSelection.h"
#include "TurnOptionsTab.h"
#include "ExtraOptionsTab.h"
#include "OptionsTab.h"
#include "RandomMapTab.h"
#include "SelectionTab.h"
#include "BattleOnlyModeTab.h"
#include "MapGenTab.h"

#include "../CServerHandler.h"
#include "../GameEngine.h"
#include "../GameChatHandler.h"
#include "../GameInstance.h"
#include "../gui/Shortcut.h"
#include "../widgets/Buttons.h"
#include "../widgets/GraphicalPrimitiveCanvas.h"
#include "../windows/InfoWindows.h"
#include "../render/Colors.h"
#include "../globalLobby/GlobalLobbyClient.h"

#include "../../lib/CConfigHandler.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include "../../lib/campaign/CampaignHandler.h"
#include "../../lib/mapping/CMapInfo.h"
#include "../../lib/networkPacks/PacksForLobby.h"
#include "../../lib/rmg/CMapGenOptions.h"
#include "../../lib/modding/MapGenerators.h"
#include "../../lib/GameLibrary.h"

CLobbyScreen::CLobbyScreen(ESelectionScreen screenType, bool hideScreen)
	: CSelectionBase(screenType), bonusSel(nullptr)
{
	OBJECT_CONSTRUCTION;
	tabSel = std::make_shared<SelectionTab>(screenType);
	curTab = tabSel;

	auto initLobby = [&]()
	{
		tabSel->callOnSelect = [this](std::shared_ptr<CMapInfo> mapInfo)
		{
			GAME->server().setMapInfo(mapInfo, nullptr);
			if(curTab != tabBattleOnlyMode)
				updateStartButtonState();
		};

		buttonSelect = std::make_shared<CButton>(Point(411, 80), AnimationPath::builtin("GSPBUTT.DEF"), LIBRARY->generaltexth->zelp[45], 0, EShortcut::LOBBY_SELECT_SCENARIO);
		buttonSelect->addCallback([this]()
		{
			toggleTab(tabSel);
			if (getMapInfo() && getMapInfo()->isRandomMap)
				GAME->server().setMapInfo(tabSel->getSelectedMapInfo());
		});

		buttonOptions = std::make_shared<CButton>(Point(411, 510), AnimationPath::builtin("GSPBUTT.DEF"), LIBRARY->generaltexth->zelp[46], std::bind(&CLobbyScreen::toggleTab, this, tabOpt), EShortcut::LOBBY_ADDITIONAL_OPTIONS);
		if(settings["general"]["enableUiEnhancements"].Bool())
		{
			if(screenType == ESelectionScreen::newGame)
				buttonBattleMode = std::make_shared<CButton>(Point(619, 80), AnimationPath::builtin("GSPButton2Arrow"), CButton::tooltip("", LIBRARY->generaltexth->translate("vcmi.lobby.battleOnlyMode.help")), [this](){
					updateAfterStateChange(); // creates tabBattleOnlyMode -> cannot created by init of object because GAME->server().isGuest() isn't valid at that point
					toggleTab(tabBattleOnlyMode);
				}, EShortcut::LOBBY_BATTLE_MODE);
			buttonExtraOptions = std::make_shared<CButton>(Point(619, 510), AnimationPath::builtin("GSPButton2Arrow"), LIBRARY->generaltexth->zelp[46], std::bind(&CLobbyScreen::toggleTab, this, tabExtraOptions), EShortcut::LOBBY_EXTRA_OPTIONS);
		}
	};

	if(screenType != ESelectionScreen::campaignList && isMultiplayerNetworkLobby())
	{
		buttonChat = std::make_shared<CButton>(Point(619, 105), AnimationPath::builtin("GSPBUT2.DEF"), LIBRARY->generaltexth->zelp[48], std::bind(&CLobbyScreen::toggleChat, this), EShortcut::LOBBY_TOGGLE_CHAT);
		buttonChat->setTextOverlay(card->showChat ? LIBRARY->generaltexth->allTexts[531] : LIBRARY->generaltexth->allTexts[532], FONT_SMALL, Colors::WHITE);
	}

	switch(screenType)
	{
	case ESelectionScreen::newGame:
	{
		tabOpt = std::make_shared<OptionsTab>();
		tabTurnOptions = std::make_shared<TurnOptionsTab>();
		tabExtraOptions = std::make_shared<ExtraOptionsTab>();
		tabRand = std::make_shared<RandomMapTab>();
		tabRand->mapInfoChanged += std::bind(&IServerAPI::setMapInfo, &GAME->server(), _1, _2);

		// DMB: a map generator mod (lib/modding/MapGenerators.h) is a second way to make the random map,
		// beside VCMI's own; with several, the last in load order, as later mods win in VCMI. There is one
		// Random Map button (K, September 27th: "the golden arrows should pick map generation mode"): the
		// first press opens the random map settings in the last mode used, and each press after that moves
		// to the next mode, "VCMI Random" or the generator's name, which the button shows. Only the host
		// can use it. A generator that makes the map at Begin (atBegin) gets its tab's settings as the
		// lobby's random map; the players who join need neither the generator nor its mod, since the
		// server sends every player the whole game, the map included (LobbyStartGame).
		std::optional<MapGeneratorInfo> generator;
		for(const auto & found : MapGenerators::active())
		{
			if(!found.problem.empty())
				logGlobal->warn("Map generator %s (mod %s) is not offered: %s", found.name, found.modID, found.problem);
			else
			{
				if(generator)
					logGlobal->info("Map generator %s (mod %s) takes the place of %s (mod %s)", found.name, found.modID, generator->name, generator->modID);
				generator = found;
			}
		}
		if(generator)
		{
			tabMapGen = std::make_shared<MapGenTab>(*generator);
			randomByGenerator = persistentStorage["dmb"]["randomMapMode"].String() == generator->modID;
		}

		buttonRMG = std::make_shared<CButton>(Point(411, 105), AnimationPath::builtin("GSPBUTT.DEF"),
			generator ? CButton::tooltipLocalized("vcmi.dmb.randomMode") : LIBRARY->generaltexth->zelp[47], 0, EShortcut::LOBBY_RANDOM_MAP);
		buttonRMG->addCallback([this]() { pressRandomMap(); });

		card->iconDifficulty->addCallback(std::bind(&IServerAPI::setDifficulty, &GAME->server(), _1));

		buttonStart = std::make_shared<CButton>(Point(411, 535), AnimationPath::builtin("SCNRBEG.DEF"), LIBRARY->generaltexth->zelp[103], std::bind(&CLobbyScreen::start, this, false), EShortcut::LOBBY_BEGIN_STANDARD_GAME);
		initLobby();
		break;
	}
	case ESelectionScreen::loadGame:
	{
		tabOpt = std::make_shared<OptionsTab>();
		tabTurnOptions = std::make_shared<TurnOptionsTab>();
		tabExtraOptions = std::make_shared<ExtraOptionsTab>();
		buttonStart = std::make_shared<CButton>(Point(411, 535), AnimationPath::builtin("SCNRLOD.DEF"), LIBRARY->generaltexth->zelp[103], std::bind(&CLobbyScreen::start, this, false), EShortcut::LOBBY_LOAD_GAME);
		initLobby();
		break;
	}
	case ESelectionScreen::campaignList:
		tabSel->callOnSelect = std::bind(&IServerAPI::setMapInfo, &GAME->server(), _1, nullptr);
		buttonStart = std::make_shared<CButton>(Point(411, 535), AnimationPath::builtin("SCNRLOD.DEF"), CButton::tooltip(), std::bind(&CLobbyScreen::start, this, true), EShortcut::LOBBY_BEGIN_CAMPAIGN);
		break;
	}

	buttonStart->block(true); // to be unblocked after map list is ready

	buttonBack = std::make_shared<CButton>(Point(581, 535), AnimationPath::builtin("SCNRBACK.DEF"), LIBRARY->generaltexth->zelp[105], [&]()
	{
		bool wasInLobbyRoom = GAME->server().inLobbyRoom();
		GAME->server().sendClientDisconnecting();
		close();

		if (wasInLobbyRoom)
			GAME->server().getGlobalLobby().activateInterface();
	}, EShortcut::GLOBAL_CANCEL);

	if(hideScreen) // workaround to avoid confusing players by custom campaign list displaying for a few ms -> instead of this draw a black screen while "loading"
	{
		blackScreen = std::make_shared<GraphicalPrimitiveCanvas>(Rect(Point(0, 0), pos.dimensions()));
		blackScreen->addBox(Point(0, 0), pos.dimensions(), Colors::BLACK);
	}

	onRemoteClientLobbyStateChanged();
}

CLobbyScreen::~CLobbyScreen()
{
	// TODO: For now we always destroy whole lobby when leaving bonus selection screen
	if(GAME->server().getState() == EClientState::LOBBY_CAMPAIGN)
		GAME->server().sendClientDisconnecting();
}

bool CLobbyScreen::isMultiplayerNetworkLobby() const
{
	return GAME->server().loadMode == ELoadMode::MULTI && !GAME->server().hotseatMode;
}

bool CLobbyScreen::isMultiplayerHost() const
{
	return isMultiplayerNetworkLobby() && GAME->server().isHost();
}

bool CLobbyScreen::canStartLobbyGame() const
{
	if(GAME->server().isGuest() || GAME->server().mi == nullptr)
		return false;

	if(isMultiplayerHost() && !GAME->server().hasRemoteClientInLobby())
		return false;

	return true;
}

bool CLobbyScreen::isLanOrOnlineMultiplayerHost() const
{
	return buttonChat && isMultiplayerHost() && (GAME->server().serverMode == EServerMode::LOCAL || GAME->server().serverMode == EServerMode::LOBBY_HOST);
}

void CLobbyScreen::updateCompatibilityNotice(size_t requiredHumanPlayers)
{
	const size_t hiddenIncompatibleMaps = tabSel->getHiddenIncompatibleMapsCount();
	if(hiddenIncompatibleMaps && screenType == ESelectionScreen::newGame && GAME->server().loadMode == ELoadMode::MULTI)
	{
		MetaString warningText;
		warningText.appendTextID("vcmi.lobby.system.hidingIncompatibleMaps");
		warningText.replaceNumber(requiredHumanPlayers);
		const std::string warningTextFormatted = warningText.toString();

		if(lastCompatibilityNotice != warningTextFormatted)
		{
			logGlobal->info("%s", warningTextFormatted);
			if(!GAME->server().hotseatMode)
				GAME->server().getGameChat().onNewLobbyMessageReceived("System", warningTextFormatted);
			lastCompatibilityNotice = warningTextFormatted;
		}
	}
	else
	{
		lastCompatibilityNotice.clear();
	}
}

void CLobbyScreen::updateHostLobbyChatState()
{
	if(!isLanOrOnlineMultiplayerHost())
		return;

	buttonChat->setTextOverlay(card->showChat ? LIBRARY->generaltexth->allTexts[531] : LIBRARY->generaltexth->allTexts[532], FONT_SMALL, Colors::WHITE);

}

void CLobbyScreen::updateStartButtonState()
{
	buttonStart->block(!canStartLobbyGame());
}

void CLobbyScreen::onRemoteClientLobbyStateChanged()
{
	if(!isLanOrOnlineMultiplayerHost())
	{
		updateHostLobbyChatState();
		return;
	}

	const bool hasRemoteClient = GAME->server().hasRemoteClientInLobby();

	if(hasRemoteClient)
	{
		waitingForPlayersMessageShown = false;
	}
	else if(!waitingForPlayersMessageShown)
	{
		// Show this message exactly once per "everyone disconnected" event,
		// regardless of how many state refreshes the lobby screen performs.
		GAME->server().getGameChat().onNewLobbyMessageReceived("System", LIBRARY->generaltexth->translate("vcmi.lobby.system.waitingForPlayers"));
		waitingForPlayersMessageShown = true;
	}

	updateHostLobbyChatState();
}

void CLobbyScreen::pressRandomMap()
{
	if(!tabMapGen)
	{
		// VCMI's own behaviour while no generator mod is enabled
		toggleTab(tabRand);
		if (getMapInfo() && !getMapInfo()->isRandomMap)
			tabRand->updateMapInfoByHost();
		return;
	}
	// the first press opens the random map settings in the last mode, the next ones move to the next mode
	const bool nextMode = curTab == tabRand || curTab == tabMapGen;
	if(nextMode)
	{
		randomByGenerator = !randomByGenerator;
		Settings mode = persistentStorage.write["dmb"]["randomMapMode"];
		mode->String() = randomByGenerator ? tabMapGen->getGenerator().modID : std::string();
	}
	showRandomMode(nextMode);
}

void CLobbyScreen::showRandomMode(bool modeChanged)
{
	if(randomByGenerator)
	{
		if(curTab != tabMapGen)
			toggleTab(tabMapGen);
		// a generator that makes the map at Begin is the lobby's random map; one that makes maps to pick
		// leaves the lobby's map as it is until its Generate button makes one
		tabMapGen->updateMapInfoByHost();
	}
	else
	{
		if(curTab != tabRand)
			toggleTab(tabRand);
		// on a change of mode even when the lobby's map is already random, as the server may not have
		// answered the last press yet
		const auto & options = GAME->server().si->mapGenOptions;
		if (modeChanged || !getMapInfo() || !getMapInfo()->isRandomMap || (options && !options->getExternalGenerator().empty()))
			tabRand->updateMapInfoByHost();
	}
	buttonRMG->setTextOverlay("  " + randomModeLabel(), FONT_SMALL, GAME->server().isHost() ? Colors::WHITE : Colors::ORANGE);
}

std::string CLobbyScreen::randomModeLabel() const
{
	// a guest's own mode says nothing of the host's, whose random map the guest's card shows
	if(!tabMapGen || GAME->server().isGuest())
		return LIBRARY->generaltexth->allTexts[740];
	return randomByGenerator ? tabMapGen->getGenerator().name : LIBRARY->generaltexth->translate("vcmi.dmb.randomMode.vcmi");
}

void CLobbyScreen::toggleTab(std::shared_ptr<CIntObject> tab)
{
	if(tab == curTab)
		GAME->server().sendGuiAction(LobbyGuiAction::NO_TAB);
	else if(tab == tabOpt)
		GAME->server().sendGuiAction(LobbyGuiAction::OPEN_OPTIONS);
	else if(tab == tabSel)
		GAME->server().sendGuiAction(LobbyGuiAction::OPEN_SCENARIO_LIST);
	else if(tab == tabRand)
		GAME->server().sendGuiAction(LobbyGuiAction::OPEN_RANDOM_MAP_OPTIONS);
	else if(tab == tabTurnOptions)
		GAME->server().sendGuiAction(LobbyGuiAction::OPEN_TURN_OPTIONS);
	else if(tab == tabExtraOptions)
		GAME->server().sendGuiAction(LobbyGuiAction::OPEN_EXTRA_OPTIONS);
	else if(tab == tabBattleOnlyMode)
		GAME->server().sendGuiAction(LobbyGuiAction::BATTLE_MODE);

	if(tab == tabBattleOnlyMode)
	{
		tabBattleOnlyMode->setStartButtonEnabled();
		card->clearSelection();
	}
	else
	{
		updateStartButtonState();
		card->changeSelection();
	}

	CSelectionBase::toggleTab(tab);
}

void CLobbyScreen::start(bool campaign)
{
	if(curTab == tabBattleOnlyMode)
		tabBattleOnlyMode->startBattle();
	else if(campaign)
		startCampaign();
	else
		startScenario(false);
}

void CLobbyScreen::startCampaign()
{
	if(!GAME->server().mi)
		return;

	try {
		auto ourCampaign = CampaignHandler::getCampaign(GAME->server().mi->fileURI);
		GAME->server().setCampaignState(ourCampaign);
	}
	catch (const std::runtime_error &e)
	{
		// handle possible exception on map loading. For example campaign that contains map in unsupported format
		// for example, wog campaigns or hota campaigns without hota map support mod
		MetaString message;
		message.appendTextID("vcmi.client.errors.invalidMap");
		message.replaceRawString(e.what());

		CInfoWindow::showInfoDialog(message.toString(), {});
	}
}

void CLobbyScreen::startScenario(bool allowOnlyAI)
{
	if (tabRand && GAME->server().si->mapGenOptions && GAME->server().si->mapGenOptions->getExternalGenerator().empty())
	{
		// Save RMG settings at game start (DMB: VCMI's own; a generator mod's tab keeps its own settings)
		tabRand->saveOptions(*GAME->server().si->mapGenOptions);
	}

	// Save chosen difficulty
	Settings lastDifficulty = settings.write["general"]["lastDifficulty"];
	lastDifficulty->Integer() = getCurrentDifficulty();

	if (GAME->server().validateGameStart(allowOnlyAI))
	{
		GAME->server().sendStartGame(allowOnlyAI);
		buttonStart->block(true);
	}
}

void CLobbyScreen::toggleMode(bool host)
{
	tabSel->toggleMode();
	if(screenType == ESelectionScreen::campaignList)
	{
		buttonStart->block(!host);
		return;
	}

	auto buttonColor = host ? Colors::WHITE : Colors::ORANGE;
	buttonSelect->setTextOverlay("  " + LIBRARY->generaltexth->allTexts[500], FONT_SMALL, buttonColor);
	buttonOptions->setTextOverlay(LIBRARY->generaltexth->allTexts[501], FONT_SMALL, buttonColor);

	if (buttonBattleMode)
		buttonBattleMode->setTextOverlay(LIBRARY->generaltexth->translate("vcmi.lobby.battleOnlyMode"), FONT_SMALL, buttonColor);

	if (buttonExtraOptions)
		buttonExtraOptions->setTextOverlay(LIBRARY->generaltexth->translate("vcmi.optionsTab.extraOptions.hover"), FONT_SMALL, buttonColor);

	if(buttonRMG)
	{
		buttonRMG->setTextOverlay("  " + randomModeLabel(), FONT_SMALL, buttonColor);
		buttonRMG->block(!host);
	}
	buttonSelect->block(!host);
	buttonOptions->block(!host);

	if (buttonBattleMode)
		buttonBattleMode->block(!host);

	if (buttonExtraOptions)
		buttonExtraOptions->block(!host);

	if(GAME->server().mi)
	{
		tabOpt->recreate();
		tabTurnOptions->recreate();
		tabExtraOptions->recreate();
	}

	updateStartButtonState();
}

void CLobbyScreen::toggleChat()
{
	if(!buttonChat)
		return;

	card->toggleChat();
	if(card->showChat)
		buttonChat->setTextOverlay(LIBRARY->generaltexth->allTexts[531], FONT_SMALL, Colors::WHITE);
	else
		buttonChat->setTextOverlay(LIBRARY->generaltexth->allTexts[532], FONT_SMALL, Colors::WHITE);
}

void CLobbyScreen::updateAfterStateChange()
{
	OBJECT_CONSTRUCTION;
	onRemoteClientLobbyStateChanged();
	const bool shouldFilterByPlayerCount = screenType == ESelectionScreen::newGame && GAME->server().loadMode == ELoadMode::MULTI;
	const size_t requiredHumanPlayers = shouldFilterByPlayerCount ? std::max<size_t>(2, GAME->server().playerNames.size()) : 0;
	if(!compatibilityFilterInitialized || (shouldFilterByPlayerCount && requiredHumanPlayers != lastRequiredHumanPlayers))
	{
		tabSel->rememberCurrentSelection();
		tabSel->filter(-1, requiredHumanPlayers);
		tabSel->restoreLastSelection();
		updateCompatibilityNotice(requiredHumanPlayers);
		compatibilityFilterInitialized = true;
		lastRequiredHumanPlayers = requiredHumanPlayers;
	}

	if(!tabBattleOnlyMode)
	{
		tabBattleOnlyMode = std::make_shared<BattleOnlyModeTab>();
		tabBattleOnlyMode->setEnabled(false);

		if(GAME->server().battleMode)
			toggleTab(tabBattleOnlyMode);
	}

	if(GAME->server().isHost() && screenType == ESelectionScreen::newGame)
	{
		bool isMultiplayer = GAME->server().loadMode == ELoadMode::MULTI;
		ExtraOptionsInfo info = SEL->getStartInfo()->extraOptionsInfo;
		info.cheatsAllowed = isMultiplayer ? persistentStorage["startExtraOptions"]["multiPlayer"]["cheatsAllowed"].Bool() : !persistentStorage["startExtraOptions"]["singlePlayer"]["cheatsNotAllowed"].Bool();
		info.unlimitedReplay = persistentStorage["startExtraOptions"][isMultiplayer ? "multiPlayer" : "singlePlayer"]["unlimitedReplay"].Bool();
		if(info.cheatsAllowed != GAME->server().si->extraOptionsInfo.cheatsAllowed || info.unlimitedReplay != GAME->server().si->extraOptionsInfo.unlimitedReplay)
			GAME->server().setExtraOptionsInfo(info);
	}

	if(GAME->server().mi)
	{
		if (tabOpt)
			tabOpt->recreate();
		if (tabTurnOptions)
			tabTurnOptions->recreate();
		if (tabExtraOptions)
			tabExtraOptions->recreate();
	}

	if(curTab != tabBattleOnlyMode)
	{
		updateStartButtonState();
		card->changeSelection();
	}

	if (card->iconDifficulty)
	{
		if (screenType == ESelectionScreen::loadGame)
		{
			// When loading the game, only one button in the difficulty toggle group should be enabled, so here disable all other buttons first, then make selection
			card->iconDifficulty->setSelectedOnly(GAME->server().si->difficulty);
		}
		else
		{
			card->iconDifficulty->setSelected(GAME->server().si->difficulty);
		}
	}
	
	if(curTab && curTab == tabRand && GAME->server().si->mapGenOptions)
		tabRand->setMapGenOptions(GAME->server().si->mapGenOptions);
}

const StartInfo * CLobbyScreen::getStartInfo()
{
	return GAME->server().si.get();
}

const CMapInfo * CLobbyScreen::getMapInfo()
{
	return GAME->server().mi.get();
}
