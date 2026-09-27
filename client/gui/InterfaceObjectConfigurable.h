/*
* InterfaceBuilder.h, part of VCMI engine
*
* Authors: listed in file AUTHORS in main folder
*
* License: GNU General Public License v2.0 or later
* Full text of license available in license.txt file, in main folder
*
*/

#pragma once

#include "CIntObject.h"
#include "TextAlignment.h"
#include "../render/EFont.h"

#include "../../lib/json/JsonNode.h"

class CPicture;
class CLabel;
class CMultiLineLabel;
class CToggleGroup;
class CToggleButton;
class CButton;
class CLabelGroup;
class CSlider;
class CAnimImage;
class CShowableAnim;
class CFilledTexture;
class ComboBox;
class CTextInput;
class TransparentFilledRectangle;
class CTextBox;
class LRClickableAreaWText;
class CTabbedInt;
class LayoutPage;
class LayoutPages;

#define REGISTER_BUILDER(type, method) registerBuilder(type, std::bind(method, this, std::placeholders::_1))

class InterfaceObjectConfigurable: public CIntObject
{
	friend class LayoutPage;
	friend class LayoutPages;

public:
	InterfaceObjectConfigurable(int used=0, Point offset=Point());
	InterfaceObjectConfigurable(const JsonNode & config, int used=0, Point offset=Point());

protected:
	/// DMB: the mod whose files the layout's own file paths resolve in (a "pages" widget's page layouts);
	/// empty for the game's own layouts. An owner that loads a mod's layout sets it before build().
	std::string layoutScope;

	/// DMB: runs on every page a "pages" widget of this layout builds, once the page is built (to fill in
	/// what only code knows). Set it before build().
	std::function<void(LayoutPage &)> onPageBuilt;

	/// DMB: whether this layout's horizontal sliders take the arrow keys, as VCMI's do; a slider's own
	/// "keyboard" field overrides it. Pages set it off: their arrows turn the page. Set it before build().
	bool slidersTakeKeys = true;

	/// DMB: runs after a settings-bound widget of this layout (or of its pages) writes its setting, with
	/// the widget's "setting" as written in the layout. Set it before build().
	std::function<void(const std::string &)> onSettingChanged;

	/// DMB: a "pages" widget's page gets its owner's callbacks, conditionals and variables
	void inheritFrom(const InterfaceObjectConfigurable & owner);

	/// DMB: lets mods add pages (mod.json "tabPages") to a stock screen, which has no pages widget of its
	/// own. Only when an enabled mod targets `id`: everything built so far except the widgets named in
	/// `frame` becomes page 1, and a pages widget, `pagesConfig` giving its title and arrows, pages
	/// through it and the mods' pages. Call it after build().
	void acceptTabPages(const std::string & id, const std::set<std::string> & frame, const JsonNode & pagesConfig);

	/// DMB: where a screen that took pages (acceptTabPages) builds content it makes later, such as rows
	/// rebuilt on every change, so they stay on its own page; the screen itself when it took none
	CIntObject * stockPageOrSelf();

	/// Set blocked status for all buttons associated with provided shortcut
	void setShortcutBlocked(EShortcut shortcut, bool isBlocked);

	/// Registers provided callback to be called whenever specified shortcut is triggered
	void addShortcut(EShortcut shortcut, std::function<void()> callback);

	void keyPressed(EShortcut key) override;

	using BuilderFunction = std::function<std::shared_ptr<CIntObject>(const JsonNode &)>;
	void registerBuilder(const std::string &, BuilderFunction);

	void loadCustomBuilders(const JsonNode & config);
	
	//must be called after adding callbacks
	void build(const JsonNode & config);

	void addConditional(const std::string & name, bool active);

	void addWidget(const std::string & name, std::shared_ptr<CIntObject> widget);
	
	void addCallback(const std::string & callbackName, std::function<void(int)> callback);
	void addCallback(const std::string & callbackName, std::function<void(std::string)> callback);
	JsonNode variables;
	
	template<class T>
	const std::shared_ptr<T> widget(const std::string & name) const
	{
		auto iter = widgets.find(name);
		if(iter == widgets.end())
			return nullptr;
		return std::dynamic_pointer_cast<T>(iter->second);
	}
	
	void deleteWidget(const std::string & name);
		
	//basic serializers
	Point readPosition(const JsonNode &) const;
	Rect readRect(const JsonNode &) const;
	ETextAlignment readTextAlignment(const JsonNode &) const;
	ColorRGBA readColor(const JsonNode &) const;
	EFonts readFont(const JsonNode &) const;
	std::string readText(const JsonNode &) const;
	std::pair<std::string, std::string> readHintText(const JsonNode &) const;
	EShortcut readHotkey(const JsonNode &) const;
	PlayerColor readPlayerColor(const JsonNode &) const;
	
	void loadToggleButtonCallback(std::shared_ptr<CToggleButton> button, const JsonNode & config) const;
	void loadButtonCallback(std::shared_ptr<CButton> button, const JsonNode & config) const;
	void loadButtonHotkey(std::shared_ptr<CButton> button, const JsonNode & config) const;
	void loadButtonBorderColor(std::shared_ptr<CButton> button, const JsonNode & config) const;

	//basic widgets
	std::shared_ptr<CPicture> buildPicture(const JsonNode &) const;
	std::shared_ptr<CLabel> buildLabel(const JsonNode &) const;
	std::shared_ptr<CMultiLineLabel> buildMultiLineLabel(const JsonNode &) const;
	std::shared_ptr<CToggleGroup> buildToggleGroup(const JsonNode &) const;
	std::shared_ptr<CToggleButton> buildToggleButton(const JsonNode &) const;
	std::shared_ptr<CButton> buildButton(const JsonNode &) const;
	std::shared_ptr<CLabelGroup> buildLabelGroup(const JsonNode &) const;
	std::shared_ptr<CSlider> buildSlider(const JsonNode &) const;
	std::shared_ptr<CAnimImage> buildImage(const JsonNode &) const;
	std::shared_ptr<CShowableAnim> buildAnimation(const JsonNode &) const;
	std::shared_ptr<CFilledTexture> buildTexture(const JsonNode &) const;
	std::shared_ptr<CIntObject> buildLayout(const JsonNode &);
	std::shared_ptr<ComboBox> buildComboBox(const JsonNode &);
	std::shared_ptr<CTextInput> buildTextInput(const JsonNode &) const;
	std::shared_ptr<TransparentFilledRectangle> buildTransparentFilledRectangle(const JsonNode & config) const;
	std::shared_ptr<CIntObject> buildGraphicalPrimitive(const JsonNode & config) const;
	std::shared_ptr<CTextBox> buildTextBox(const JsonNode & config) const;
	/// An invisible hoverable rect: hover text on the status bar, help text on
	/// right-click, nothing drawn. What the base game actually uses instead of
	/// a visible help icon (client/lobby/RandomMapTab.cpp has none).
	std::shared_ptr<LRClickableAreaWText> buildHoverHelp(const JsonNode & config) const;
	/// DMB: several layouts shown one at a time, see LayoutPages
	std::shared_ptr<CIntObject> buildPages(const JsonNode & config);

	//composite widgets
	std::shared_ptr<CIntObject> buildWidget(JsonNode config) const;
	
	/// DMB: settings-bound widgets, for layouts a mod brings: a
	/// toggleGroup, toggleButton or slider whose config names a "setting"
	/// (a slash path into settings.json) opens on the stored value and
	/// writes every change back, so a panel of options needs no C++ of its
	/// own. A slider may name a "valueLabel" widget that shows its value.
	void refreshBoundLabels() const;

private:
	struct ShortcutState
	{
		std::function<void()> callback;
		mutable std::vector<std::shared_ptr<CButton>> assignedButtons;
		bool blocked = false;
	};

	struct BoundLabel
	{
		std::string label;              ///< widget name of the CLabel to update
		std::vector<std::string> path;  ///< settings path the value lives at
		JsonNode format;                ///< the slider's config, for formatting
	};
	mutable std::vector<BoundLabel> boundLabels;
	std::string formatBoundValue(double value, const JsonNode & format) const;
	
	int unnamedObjectId = 0;
	std::map<std::string, BuilderFunction> builders;
	std::map<std::string, std::shared_ptr<CIntObject>> widgets;
	std::map<std::string, std::function<void(int)>> callbacks_int;
	std::map<std::string, std::function<void(std::string)>> callbacks_string;
	std::map<std::string, bool> conditionals;
	std::map<EShortcut, ShortcutState> shortcuts;
};

/// DMB: one page of a "pages" widget: a layout with its owner's callbacks, conditionals, variables
/// and shortcuts, so its buttons reach the code of the tab it belongs to
class LayoutPage : public InterfaceObjectConfigurable
{
public:
	LayoutPage(const InterfaceObjectConfigurable & owner, const JsonNode & layout, const std::string & scope);

	template<class T>
	std::shared_ptr<T> find(const std::string & name) const
	{
		return widget<T>(name);
	}
};

/// DMB: a layout widget of type "pages": several layouts shown one at a time, with the shown page's
/// title between a previous and a next arrow, the way Heroes III's Random Map Setup pages. Any layout
/// may hold one. Mods add pages to it by its "id" (mod.json "tabPages"), after its own, in load order.
/// The arrows step and wrap, MOVE_LEFT and MOVE_RIGHT press them, and they hide when there is one
/// page. docs/modders/DMB_UI_Modding.md is the contract.
///
///   { "type": "pages", "name": "pages", "id": "myTab",
///     "pages": [ { "layout": "config/widgets/myTab/first.json", "title": "myMod.page.first" } ],
///     "position": { "x": 0, "y": 0 },            where the pages' own layouts sit in the owner
///     "title": { "font": "big", "color": "yellow", "alignment": "center", "position": { ... } },
///     "previous": { "image": "SCNRBLF", "position": { ... } },
///     "next": { "image": "SCNRBRT", "position": { ... } },
///     "remember": "persistent:myMod/lastPage" }  the shown page, kept between visits
class LayoutPages : public CIntObject
{
	struct Page
	{
		JsonNode layout;   ///< the page entry's layout path; null for an adopted first page
		JsonNode title;    ///< a text key, or text
		std::string scope; ///< the mod whose files the layout path resolves in
		std::string from;  ///< the mod that added it, for the log
	};

	InterfaceObjectConfigurable & owner;
	std::string id;
	std::vector<Page> pages;
	std::vector<std::string> remember;
	Point pagePosition;
	std::shared_ptr<CTabbedInt> shown;
	std::shared_ptr<CLabel> title;
	std::shared_ptr<CButton> previous;
	std::shared_ptr<CButton> next;
	/// a stock screen's own content, its first page (InterfaceObjectConfigurable::acceptTabPages)
	std::shared_ptr<CIntObject> adopted;

	std::shared_ptr<CIntObject> createPage(size_t index);
	std::string pageTitle(size_t index) const;
	void updateAround();

public:
	/// `adopted`, when given, is shown as the first page, titled `adoptedTitle`
	LayoutPages(InterfaceObjectConfigurable & owner, const JsonNode & config,
		std::shared_ptr<CIntObject> adopted = nullptr, const JsonNode & adoptedTitle = JsonNode());

	size_t count() const;
	size_t current() const;
	/// shows page `index` (wrapping), and remembers it
	void showPage(size_t index);
	/// the next page (+1) or the previous (-1), wrapping
	void step(int direction);
	/// builds the shown page again, after a setting it shows changed elsewhere
	void refresh();
	/// the page shown now; null when there is none
	std::shared_ptr<LayoutPage> shownPage() const;
};
