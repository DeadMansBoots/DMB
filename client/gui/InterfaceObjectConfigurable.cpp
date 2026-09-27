/*
* InterfaceBuilder.cpp, part of VCMI engine
*
* Authors: listed in file AUTHORS in main folder
*
* License: GNU General Public License v2.0 or later
* Full text of license available in license.txt file, in main folder
*
*/

#include "StdInc.h"

#include "InterfaceObjectConfigurable.h"

#include "../CPlayerInterface.h"
#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../gui/ShortcutHandler.h"
#include "../gui/Shortcut.h"
#include "../render/Graphics.h"
#include "../render/IFont.h"
#include "../render/IRenderHandler.h"
#include "../widgets/CComponent.h"
#include "../widgets/ComboBox.h"
#include "../widgets/Buttons.h"
#include "../widgets/CTextInput.h"
#include "../widgets/GraphicalPrimitiveCanvas.h"
#include "../widgets/MiscWidgets.h"
#include "../widgets/ObjectLists.h"
#include "../widgets/Slider.h"
#include "../widgets/TextControls.h"
#include "../windows/GUIClasses.h"
#include "../windows/InfoWindows.h"

#include "../../lib/constants/StringConstants.h"
#include "../../lib/json/JsonUtils.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include "../../lib/filesystem/Filesystem.h"
#include "../../lib/filesystem/ResourcePath.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/modding/ModDescription.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/CConfigHandler.h"

#include <boost/algorithm/string.hpp>

// DMB: settings-bound widgets, so a layout a mod brings can be a whole panel of
// options. A widget config may carry "setting": "a/b/c", a slash path into settings.json.
// The widget opens on the stored value and writes every change back, so a
// panel of options needs no C++ of its own; the settings schema must declare
// the path, or VCMI erases it on the next load.
//
// "setting": "persistent:a/b/c" binds to persistentStorage
// (config/persistentStorage.json) instead. That store is loaded with no
// schema, so nothing ever erases its keys, including an official client that
// shares the same user folder and has never heard of them. Inside the path
// the store travels as a leading PERSISTENT_MARK element.
namespace
{
	const std::string PERSISTENT_MARK = "@persistent";

	std::vector<std::string> settingPath(const JsonNode & node)
	{
		std::string spec = node.String();
		std::vector<std::string> path;
		if(boost::algorithm::starts_with(spec, "persistent:"))
		{
			path.push_back(PERSISTENT_MARK);
			spec = spec.substr(std::string("persistent:").size());
		}
		std::vector<std::string> parts;
		boost::split(parts, spec, boost::is_any_of("/"));
		vstd::erase_if(parts, [](const std::string & s){ return s.empty(); });
		path.insert(path.end(), parts.begin(), parts.end());
		return path;
	}

	bool isPersistent(const std::vector<std::string> & path)
	{
		return !path.empty() && path.front() == PERSISTENT_MARK;
	}

	const JsonNode & settingValue(const std::vector<std::string> & path)
	{
		static const JsonNode nullNode;
		const size_t first = isPersistent(path) ? 1 : 0;
		if(path.size() <= first)
			return nullNode;
		const SettingsStorage & store = isPersistent(path) ? persistentStorage : settings;
		const JsonNode * node = &store[path[first]];
		for(size_t i = first + 1; i < path.size(); ++i)
			node = &(*node)[path[i]];
		return *node;
	}

	double settingNumber(const std::vector<std::string> & path, double fallback)
	{
		const JsonNode & value = settingValue(path);
		if(value.isNumber())
			return value.Float();
		if(value.getType() == JsonNode::JsonType::DATA_BOOL)
			return value.Bool() ? 1.0 : 0.0;
		return fallback;
	}

	void writeSetting(const std::vector<std::string> & path, const JsonNode & value)
	{
		const bool persistent = isPersistent(path);
		const std::vector<std::string> rest(path.begin() + (persistent ? 1 : 0), path.end());
		if(rest.empty())
			return;
		Settings entry = (persistent ? persistentStorage : settings).write(rest);
		*entry.operator->() = value;
	}

	bool sameSettingValue(const JsonNode & a, const JsonNode & b)
	{
		if(a.isNumber() && b.isNumber())
			return std::abs(a.Float() - b.Float()) < 1e-6;
		return a == b;
	}
}

InterfaceObjectConfigurable::InterfaceObjectConfigurable(const JsonNode & config, int used, Point offset):
	InterfaceObjectConfigurable(used, offset)
{
	build(config);
}

InterfaceObjectConfigurable::InterfaceObjectConfigurable(int used, Point offset):
	CIntObject(used, offset)
{
	REGISTER_BUILDER("picture", &InterfaceObjectConfigurable::buildPicture);
	REGISTER_BUILDER("image", &InterfaceObjectConfigurable::buildImage);
	REGISTER_BUILDER("texture", &InterfaceObjectConfigurable::buildTexture);
	REGISTER_BUILDER("animation", &InterfaceObjectConfigurable::buildAnimation);
	REGISTER_BUILDER("label", &InterfaceObjectConfigurable::buildLabel);
	REGISTER_BUILDER("multiLineLabel", &InterfaceObjectConfigurable::buildMultiLineLabel);
	REGISTER_BUILDER("toggleGroup", &InterfaceObjectConfigurable::buildToggleGroup);
	REGISTER_BUILDER("toggleButton", &InterfaceObjectConfigurable::buildToggleButton);
	REGISTER_BUILDER("button", &InterfaceObjectConfigurable::buildButton);
	REGISTER_BUILDER("labelGroup", &InterfaceObjectConfigurable::buildLabelGroup);
	REGISTER_BUILDER("slider", &InterfaceObjectConfigurable::buildSlider);
	REGISTER_BUILDER("layout", &InterfaceObjectConfigurable::buildLayout);
	REGISTER_BUILDER("comboBox", &InterfaceObjectConfigurable::buildComboBox);
	REGISTER_BUILDER("textInput", &InterfaceObjectConfigurable::buildTextInput);
	REGISTER_BUILDER("graphicalPrimitive", &InterfaceObjectConfigurable::buildGraphicalPrimitive);
	REGISTER_BUILDER("transparentFilledRectangle", &InterfaceObjectConfigurable::buildTransparentFilledRectangle);
	REGISTER_BUILDER("textBox", &InterfaceObjectConfigurable::buildTextBox);
	REGISTER_BUILDER("hoverHelp", &InterfaceObjectConfigurable::buildHoverHelp);
	REGISTER_BUILDER("pages", &InterfaceObjectConfigurable::buildPages);
}

void InterfaceObjectConfigurable::inheritFrom(const InterfaceObjectConfigurable & owner)
{
	// Not the shortcuts: the owner answers those itself, and a page answering too would run them twice.
	// A page's own button with a "hotkey" still presses on its key.
	callbacks_int = owner.callbacks_int;
	callbacks_string = owner.callbacks_string;
	conditionals = owner.conditionals;
	variables = owner.variables;
	onPageBuilt = owner.onPageBuilt;
}

void InterfaceObjectConfigurable::registerBuilder(const std::string & type, BuilderFunction f)
{
	builders[type] = f;
}

void InterfaceObjectConfigurable::addCallback(const std::string & callbackName, std::function<void(int)> callback)
{
	callbacks_int[callbackName] = callback;
}

void InterfaceObjectConfigurable::addCallback(const std::string & callbackName, std::function<void(std::string)> callback)
{
	callbacks_string[callbackName] = callback;
}


void InterfaceObjectConfigurable::deleteWidget(const std::string & name)
{
	auto iter = widgets.find(name);
	if(iter != widgets.end())
		widgets.erase(iter);
}

void InterfaceObjectConfigurable::loadCustomBuilders(const JsonNode & config)
{
	for(auto & item : config.Struct())
	{
		std::string typeName = item.first;
		JsonNode baseConfig = item.second;

		auto const & functor = [this, baseConfig](const JsonNode & widgetConfig) -> std::shared_ptr<CIntObject>
		{
			JsonNode actualConfig = widgetConfig;
			JsonUtils::mergeCopy(actualConfig, baseConfig);

			return this->buildWidget(actualConfig);
		};
		registerBuilder(typeName, functor);
	}
}

void InterfaceObjectConfigurable::build(const JsonNode &config)
{
	OBJECT_CONSTRUCTION;

	logGlobal->debug("Building configurable interface object");
	auto * items = &config;
	
	if(config.getType() == JsonNode::JsonType::DATA_STRUCT)
	{
		if (!config["library"].isNull())
		{
			if (config["library"].isString())
			{
				const JsonNode library(JsonPath::fromJson(config["library"]));
				loadCustomBuilders(library);
			}

			if (config["library"].isVector())
			{
				for (auto const & entry : config["library"].Vector())
				{
					const JsonNode library(JsonPath::fromJson(entry));
					loadCustomBuilders(library);
				}
			}
		}

		loadCustomBuilders(config["customTypes"]);

		for(auto & item : config["variables"].Struct())
		{
			logGlobal->debug("Read variable named %s", item.first);
			variables[item.first] = item.second;
		}

		items = &config["items"];
	}
	
	for(const auto & item : items->Vector())
		addWidget(item["name"].String(), buildWidget(item));

	// load only if set
	if (!config["width"].isNull())
		pos.w = config["width"].Integer();
	if (!config["height"].isNull())
		pos.h = config["height"].Integer();

	// value labels of settings-bound sliders may be built after their slider
	refreshBoundLabels();
}

std::string InterfaceObjectConfigurable::formatBoundValue(double value, const JsonNode & format) const
{
	// named stops: the last [value, text] pair at or below the value
	if(format["valueNames"].isVector())
	{
		std::string text;
		for(const auto & stop : format["valueNames"].Vector())
			if(stop.isVector() && stop.Vector().size() >= 2 && stop.Vector()[0].Float() <= value + 1e-6)
				text = readText(stop.Vector()[1]);
		if(!text.empty())
			return text;
	}
	const double scale = format["valueDisplayScale"].isNull() ? 1.0 : format["valueDisplayScale"].Float();
	const int decimals = format["valueDecimals"].isNull() ? 2 : static_cast<int>(format["valueDecimals"].Integer());
	std::ostringstream out;
	out << std::fixed << std::setprecision(std::max(0, decimals)) << value * scale;
	if(!format["valueSuffix"].isNull())
		out << format["valueSuffix"].String();
	return out.str();
}

void InterfaceObjectConfigurable::refreshBoundLabels() const
{
	for(const auto & bound : boundLabels)
	{
		auto label = widget<CLabel>(bound.label);
		if(!label)
			continue;
		const double fallback = bound.format["valueDefault"].isNull()
			? bound.format["valueMin"].Float() : bound.format["valueDefault"].Float();
		label->setText(formatBoundValue(settingNumber(bound.path, fallback), bound.format));
	}
}

void InterfaceObjectConfigurable::addConditional(const std::string & name, bool active)
{
	conditionals[name] = active;
}

void InterfaceObjectConfigurable::addWidget(const std::string & namePreferred, std::shared_ptr<CIntObject> widget)
{
	static const std::string unnamedObjectPrefix = "__widget_";

	std::string nameActual;

	if (widgets.count(namePreferred) == 0)
		nameActual = namePreferred;
	else
		logGlobal->error("Duplicated widget name: '%s'", namePreferred);

	if (nameActual.empty())
		nameActual = unnamedObjectPrefix + std::to_string(unnamedObjectId++);

	logGlobal->debug("Building widget with name %s", nameActual);
	widgets[nameActual] = widget;
}

std::string InterfaceObjectConfigurable::readText(const JsonNode & config) const
{
	if(config.isNull())
		return "";
	
	std::string s = config.String();
	if(s.empty())
		return s;
	logGlobal->debug("Reading text from translations by key: %s", s);
	return LIBRARY->generaltexth->translate(s);
}

Point InterfaceObjectConfigurable::readPosition(const JsonNode & config) const
{
	Point p;
	logGlobal->debug("Reading point");
	p.x = config["x"].Integer();
	p.y = config["y"].Integer();
	return p;
}

Rect InterfaceObjectConfigurable::readRect(const JsonNode & config) const
{
	Rect p;
	logGlobal->debug("Reading rect");
	p.x = config["x"].Integer();
	p.y = config["y"].Integer();
	p.w = config["w"].Integer();
	p.h = config["h"].Integer();
	return p;
}

ETextAlignment InterfaceObjectConfigurable::readTextAlignment(const JsonNode & config) const
{
	logGlobal->debug("Reading text alignment");
	if(!config.isNull())
	{
		if(config.String() == "center")
			return ETextAlignment::CENTER;
		if(config.String() == "left")
			return ETextAlignment::TOPLEFT;
		if(config.String() == "right")
			return ETextAlignment::BOTTOMRIGHT;
	}
	logGlobal->debug("Unknown text alignment attribute");
	return ETextAlignment::CENTER;
}

ColorRGBA InterfaceObjectConfigurable::readColor(const JsonNode & config) const
{
	logGlobal->debug("Reading color");
	if(!config.isNull())
	{
		if(config.isString())
		{
			if(config.String() == "yellow")
				return Colors::YELLOW;
			if(config.String() == "white")
				return Colors::WHITE;
			if(config.String() == "gold")
				return Colors::METALLIC_GOLD;
			if(config.String() == "green")
				return Colors::GREEN;
			if(config.String() == "orange")
				return Colors::ORANGE;
			if(config.String() == "bright-yellow")
				return Colors::BRIGHT_YELLOW;
		}
		if(config.isVector())
		{
			const auto & asVector = config.Vector();
			if(asVector.size() == 4)
				return ColorRGBA(asVector[0].Integer(), asVector[1].Integer(), asVector[2].Integer(), asVector[3].Integer());
			if(asVector.size() == 3)
				return ColorRGBA(asVector[0].Integer(), asVector[1].Integer(), asVector[2].Integer());
		}
	}
	logGlobal->debug("Unknown color attribute");
	return Colors::DEFAULT_KEY_COLOR;

}

PlayerColor InterfaceObjectConfigurable::readPlayerColor(const JsonNode & config) const
{
	logGlobal->debug("Reading PlayerColor");
	if(!config.isNull() && config.isString())
		return PlayerColor(PlayerColor::decode(config.String()));
	
	logGlobal->debug("Unknown PlayerColor attribute");
	return PlayerColor::CANNOT_DETERMINE;
}

EFonts InterfaceObjectConfigurable::readFont(const JsonNode & config) const
{
	logGlobal->debug("Reading font");
	if(!config.isNull())
	{
		if(config.String() == "big")
			return EFonts::FONT_BIG;
		if(config.String() == "medium")
			return EFonts::FONT_MEDIUM;
		if(config.String() == "small")
			return EFonts::FONT_SMALL;
		if(config.String() == "tiny")
			return EFonts::FONT_TINY;
		if(config.String() == "calisto")
			return EFonts::FONT_CALLI;
	}
	logGlobal->debug("Unknown font attribute");
	return EFonts::FONT_MEDIUM;
}

std::pair<std::string, std::string> InterfaceObjectConfigurable::readHintText(const JsonNode & config) const
{
	logGlobal->debug("Reading hint text");
	std::pair<std::string, std::string> result;
	if(!config.isNull())
	{
		if(config.getType() == JsonNode::JsonType::DATA_STRUCT)
		{
			result.first = readText(config["hover"]);
			result.second = readText(config["help"]);
			return result;
		}
		if(config.getType() == JsonNode::JsonType::DATA_STRING)
		{
			logGlobal->debug("Reading hint text (help) from generaltext handler:%sd", config.String());
			result.first  = LIBRARY->generaltexth->translate( config.String(), "hover");
			result.second = LIBRARY->generaltexth->translate( config.String(), "help");
		}
	}
	return result;
}

EShortcut InterfaceObjectConfigurable::readHotkey(const JsonNode & config) const
{
	logGlobal->debug("Reading hotkey");

	if(config.getType() != JsonNode::JsonType::DATA_STRING)
	{
		logGlobal->error("Invalid hotket format in interface configuration! Expected string!", config.String());
		return EShortcut::NONE;
	}

	EShortcut result = ENGINE->shortcuts().findShortcut(config.String());
	if (result == EShortcut::NONE)
		logGlobal->error("Invalid hotkey '%s' in interface configuration!", config.String());
	return result;
}

std::shared_ptr<CPicture> InterfaceObjectConfigurable::buildPicture(const JsonNode & config) const
{
	logGlobal->debug("Building widget CPicture");
	auto image = ImagePath::fromJson(config["image"]);
	auto position = readPosition(config["position"]);
	auto pic = std::make_shared<CPicture>(image, position.x, position.y);

	if ( config["playerColored"].Bool() && GAME->interface())
		pic->setPlayerColor(GAME->interface()->playerID);
	return pic;
}

std::shared_ptr<CLabel> InterfaceObjectConfigurable::buildLabel(const JsonNode & config) const
{
	logGlobal->debug("Building widget CLabel");
	auto font = readFont(config["font"]);
	auto alignment = readTextAlignment(config["alignment"]);
	auto color = readColor(config["color"]);
	auto text = readText(config["text"]);
	// DMB: a label bound to a setting shows the stored text, or its "emptyText" while there is none
	if(config["setting"].isString())
	{
		const JsonNode & stored = settingValue(settingPath(config["setting"]));
		text = stored.isString() && !stored.String().empty() ? stored.String() : readText(config["emptyText"]);
	}
	auto position = readPosition(config["position"]);
	auto maxWidth = config["maxWidth"].Integer();
	return std::make_shared<CLabel>(position.x, position.y, font, alignment, color, text, maxWidth);
}

std::shared_ptr<CMultiLineLabel> InterfaceObjectConfigurable::buildMultiLineLabel(const JsonNode & config) const
{	
	logGlobal->debug("Building widget CMultiLineLabel");
	auto font = readFont(config["font"]);
	auto alignment = readTextAlignment(config["alignment"]);
	auto color = readColor(config["color"]);
	auto text = readText(config["text"]);
	Rect rect = readRect(config["rect"]);
	const auto & fontPtr = ENGINE->renderHandler().loadFont(font);
	if(!config["adoptHeight"].isNull() && config["adoptHeight"].Bool())
		rect.h = fontPtr->getLineHeight() * 2;
	return std::make_shared<CMultiLineLabel>(rect, font, alignment, color, text);
}


std::shared_ptr<CToggleGroup> InterfaceObjectConfigurable::buildToggleGroup(const JsonNode & config) const
{
	logGlobal->debug("Building widget CToggleGroup");
	auto position = readPosition(config["position"]);
	auto group = std::make_shared<CToggleGroup>(0);
	group->pos += position;
	if(!config["items"].isNull())
	{
		OBJECT_CONSTRUCTION_TARGETED(group.get());
		int itemIdx = -1;
		for(const auto & item : config["items"].Vector())
		{
			itemIdx = item["index"].isNull() ? itemIdx + 1 : item["index"].Integer();
			auto newToggle = std::dynamic_pointer_cast<CToggleButton>(buildWidget(item));
			group->addToggle(itemIdx, newToggle);
		}
	}
	if(!config["setting"].isNull())
	{
		// Settings-bound group: a toggle's value is values[index] when
		// "values" is given, else its index. Opens on the stored value
		// (or "selected" when nothing matches) before the writer is added,
		// so opening the panel writes nothing.
		const auto path = settingPath(config["setting"]);
		const JsonNode values = config["values"];
		auto valueOf = [values](int index) -> JsonNode
		{
			if(values.isVector() && index >= 0 && index < static_cast<int>(values.Vector().size()))
				return values.Vector()[index];
			return JsonNode(static_cast<int32_t>(index));
		};
		const JsonNode & stored = settingValue(path);
		bool matched = false;
		for(const auto & entry : group->buttons)
		{
			if(!stored.isNull() && sameSettingValue(stored, valueOf(entry.first)))
			{
				group->setSelected(entry.first);
				matched = true;
				break;
			}
		}
		if(!matched && !config["selected"].isNull())
			group->setSelected(config["selected"].Integer());
		group->addCallback([path, valueOf](int index)
		{
			writeSetting(path, valueOf(index));
		});
		if(!config["callback"].isNull())
			group->addCallback(callbacks_int.at(config["callback"].String()));
		return group;
	}
	if(!config["selected"].isNull())
		group->setSelected(config["selected"].Integer());
	if(!config["callback"].isNull())
		group->addCallback(callbacks_int.at(config["callback"].String()));
	return group;
}

std::shared_ptr<CToggleButton> InterfaceObjectConfigurable::buildToggleButton(const JsonNode & config) const
{
	logGlobal->debug("Building widget CToggleButton");
	auto position = readPosition(config["position"]);
	auto image = AnimationPath::fromJson(config["image"]);
	auto help = readHintText(config["help"]);
	auto button = std::make_shared<CToggleButton>(position, image, help);
	if(!config["items"].isNull())
	{
		for(const auto & item : config["items"].Vector())
		{
			button->setOverlay(buildWidget(item));
		}
	}
	if(!config["selected"].isNull())
		button->setSelected(config["selected"].Bool());
	if(!config["imageOrder"].isNull())
	{
		auto imgOrder = config["imageOrder"].Vector();
		assert(imgOrder.size() >= 4);
		button->setImageOrder(imgOrder[0].Integer(), imgOrder[1].Integer(), imgOrder[2].Integer(), imgOrder[3].Integer());
	}
	if(!config["setting"].isNull())
	{
		// Settings-bound checkbox: opens silently on the stored value, then
		// writes 1 for on and 0 for off
		const auto path = settingPath(config["setting"]);
		const double fallback = (!config["selected"].isNull() && config["selected"].Bool()) ? 1 : 0;
		button->setSelectedSilent(settingNumber(path, fallback) != 0);
		button->addCallback([path](bool on)
		{
			writeSetting(path, JsonNode(static_cast<int32_t>(on ? 1 : 0)));
		});
	}
	loadToggleButtonCallback(button, config["callback"]);
	loadButtonHotkey(button, config["hotkey"]);
	return button;
}

std::shared_ptr<CButton> InterfaceObjectConfigurable::buildButton(const JsonNode & config) const
{
	logGlobal->debug("Building widget CButton");
	auto position = readPosition(config["position"]);
	auto image = AnimationPath::fromJson(config["image"]);
	auto help = readHintText(config["help"]);
	auto button = std::make_shared<CButton>(position, image, help);
	if(!config["items"].isNull())
	{
		for(const auto & item : config["items"].Vector())
		{
			button->setOverlay(buildWidget(item));
		}
	}
	if(!config["imageOrder"].isNull())
	{
		auto imgOrder = config["imageOrder"].Vector();
		assert(imgOrder.size() >= 4);
		button->setImageOrder(imgOrder[0].Integer(), imgOrder[1].Integer(), imgOrder[2].Integer(), imgOrder[3].Integer());
	}

	loadButtonBorderColor(button, config["borderColor"]);
	loadButtonCallback(button, config["callback"]);
	loadButtonHotkey(button, config["hotkey"]);
	return button;
}

void InterfaceObjectConfigurable::loadButtonBorderColor(std::shared_ptr<CButton> button, const JsonNode & config) const
{
	if (config.isNull())
		return;

	auto color = readColor(config);
	button->setBorderColor(color);
}

void InterfaceObjectConfigurable::loadToggleButtonCallback(std::shared_ptr<CToggleButton> button, const JsonNode & config) const
{
	if(config.isNull())
		return;

	std::string callbackName = config.String();

	if (callbacks_int.count(callbackName) > 0)
		button->addCallback(callbacks_int.at(callbackName));
	else
		logGlobal->error("Invalid callback '%s' in widget", callbackName );
}

void InterfaceObjectConfigurable::loadButtonCallback(std::shared_ptr<CButton> button, const JsonNode & config) const
{
	if(config.isNull())
		return;

	std::string callbackName = config.String();

	if (callbacks_int.count(callbackName) > 0)
		button->addCallback(std::bind(callbacks_int.at(callbackName), 0));
	else
		logGlobal->error("Invalid callback '%s' in widget", callbackName );
}

void InterfaceObjectConfigurable::loadButtonHotkey(std::shared_ptr<CButton> button, const JsonNode & config) const
{
	if(config.isNull())
		return;

	if(config.getType() != JsonNode::JsonType::DATA_STRING)
	{
		logGlobal->error("Invalid shortcut format - string expected!");
		return;
	}

	button->assignedKey = readHotkey(config);

	auto target = shortcuts.find(button->assignedKey);
	if (target == shortcuts.end())
		return;

	button->addCallback(target->second.callback);
	target->second.assignedButtons.push_back(button);
}

std::shared_ptr<CLabelGroup> InterfaceObjectConfigurable::buildLabelGroup(const JsonNode & config) const
{
	logGlobal->debug("Building widget CLabelGroup");
	auto font = readFont(config["font"]);
	auto alignment = readTextAlignment(config["alignment"]);
	auto color = readColor(config["color"]);
	auto group = std::make_shared<CLabelGroup>(font, alignment, color);
	if(!config["items"].isNull())
	{
		for(const auto & item : config["items"].Vector())
		{
			auto position = readPosition(item["position"]);
			auto text = readText(item["text"]);
			group->add(position.x, position.y, text);
		}
	}
	return group;
}

std::shared_ptr<CSlider> InterfaceObjectConfigurable::buildSlider(const JsonNode & config) const
{
	logGlobal->debug("Building widget CSlider");
	auto position = readPosition(config["position"]);
	int length = config["size"].Integer();
	auto style = config["style"].String() == "brown" ? CSlider::BROWN : CSlider::BLUE;
	auto value = config["selected"].Integer();
	bool horizontal = config["orientation"].String() == "horizontal";
	auto orientation = horizontal ? Orientation::HORIZONTAL : Orientation::VERTICAL;

	std::shared_ptr<CSlider> result;

	if(!config["setting"].isNull())
	{
		// Settings-bound slider: position p stands for valueMin + p *
		// valueStep, p in 0..itemsTotal. Opens on the stored value (or
		// valueDefault), writes every move, and keeps its "valueLabel"
		// widget showing the value.
		const auto path = settingPath(config["setting"]);
		const double vmin = config["valueMin"].Float();
		const double vstep = config["valueStep"].isNull() ? 1.0 : config["valueStep"].Float();
		const int total = std::max<int>(1, static_cast<int>(config["itemsTotal"].Integer()));
		const double fallback = config["valueDefault"].isNull() ? vmin : config["valueDefault"].Float();
		const int start = std::clamp(static_cast<int>(std::lround((settingNumber(path, fallback) - vmin) / vstep)), 0, total);
		const std::string labelName = config["valueLabel"].isNull() ? std::string() : config["valueLabel"].String();
		const JsonNode format = config;
		auto moved = [this, path, vmin, vstep, labelName, format](int p)
		{
			const double v = std::round((vmin + p * vstep) * 1e6) / 1e6;
			writeSetting(path, JsonNode(v));
			if(!labelName.empty())
				if(auto label = widget<CLabel>(labelName))
					label->setText(formatBoundValue(v, format));
		};
		result = std::make_shared<CSlider>(position, length, moved, 0, total, start, orientation, style);
		if(!labelName.empty())
			boundLabels.push_back({labelName, path, format});
	}
	else if (config["items"].isNull())
	{
		auto itemsVisible = config["itemsVisible"].Integer();
		auto itemsTotal = config["itemsTotal"].Integer();

		result = std::make_shared<CSlider>(position, length, callbacks_int.at(config["callback"].String()), itemsVisible, itemsTotal, value, orientation, style);
	}
	else
	{
		auto items = config["items"].convertTo<std::vector<int>>();
		result = std::make_shared<SliderNonlinear>(position, length, callbacks_int.at(config["callback"].String()), items, value, orientation, style);
	}


	if(!config["scrollBounds"].isNull())
	{
		Rect bounds = readRect(config["scrollBounds"]);
		result->setScrollBounds(bounds);
	}
	
	if(!config["panningStep"].isNull())
		result->setPanningStep(config["panningStep"].Integer());

	return result;
}

std::shared_ptr<CAnimImage> InterfaceObjectConfigurable::buildImage(const JsonNode & config) const
{
	logGlobal->debug("Building widget CAnimImage");
	auto position = readPosition(config["position"]);
	auto image = AnimationPath::fromJson(config["image"]);
	int group = config["group"].isNull() ? 0 : config["group"].Integer();
	int frame = config["frame"].isNull() ? 0 : config["frame"].Integer();
	return std::make_shared<CAnimImage>(image, frame, group, position.x, position.y);
}

std::shared_ptr<CFilledTexture> InterfaceObjectConfigurable::buildTexture(const JsonNode & config) const
{
	logGlobal->debug("Building widget CFilledTexture");
	auto rect = readRect(config["rect"]);
	auto playerColor = readPlayerColor(config["color"]);
	if(playerColor.isValidPlayer())
	{
		auto result = std::make_shared<FilledTexturePlayerColored>(rect);
		result->setPlayerColor(playerColor);
		return result;
	}
	else
	{
		auto image = ImagePath::fromJson(config["image"]);
		return std::make_shared<CFilledTexture>(image, rect);
	}
}

std::shared_ptr<ComboBox> InterfaceObjectConfigurable::buildComboBox(const JsonNode & config)
{
	logGlobal->debug("Building widget ComboBox");
	auto position = readPosition(config["position"]);
	auto dropDownPosition = readPosition(config["dropDownPosition"]);
	auto image = AnimationPath::fromJson(config["image"]);
	auto help = readHintText(config["help"]);
	auto result = std::make_shared<ComboBox>(position, image, help, config["dropDown"], dropDownPosition);

	if(!config["items"].isNull())
	{
		for(const auto & item : config["items"].Vector())
		{
			result->setOverlay(buildWidget(item));
		}
	}
	if(!config["imageOrder"].isNull())
	{
		auto imgOrder = config["imageOrder"].Vector();
		assert(imgOrder.size() >= 4);
		result->setImageOrder(imgOrder[0].Integer(), imgOrder[1].Integer(), imgOrder[2].Integer(), imgOrder[3].Integer());
	}

	loadButtonBorderColor(result, config["borderColor"]);
	loadButtonHotkey(result, config["hotkey"]);
	return result;
}

std::shared_ptr<CTextInput> InterfaceObjectConfigurable::buildTextInput(const JsonNode & config) const
{
	logGlobal->debug("Building widget CTextInput");
	auto rect = readRect(config["rect"]);
	auto offset = readPosition(config["backgroundOffset"]);
	auto bgName = ImagePath::fromJson(config["background"]);
	auto result = std::make_shared<CTextInput>(rect, offset, bgName);
	if(!config["alignment"].isNull())
		result->setAlignment(readTextAlignment(config["alignment"]));
	if(!config["font"].isNull())
		result->setFont(readFont(config["font"]));
	if(!config["color"].isNull())
		result->setColor(readColor(config["color"]));
	if(!config["text"].isNull() && config["text"].isString())
		result->setText(config["text"].String()); //for input field raw string is taken
	if(!config["callback"].isNull())
		result->setCallback(callbacks_string.at(config["callback"].String()));
	return result;
}

/// Small helper class that provides ownership for shared_ptr's of child elements
class InterfaceLayoutWidget : public CIntObject
{
public:
	std::vector<std::shared_ptr<CIntObject>> ownedChildren;
	InterfaceLayoutWidget();
};

InterfaceLayoutWidget::InterfaceLayoutWidget()
	:CIntObject() 
{
	setRedrawParent(true);
}

std::shared_ptr<CIntObject> InterfaceObjectConfigurable::buildLayout(const JsonNode & config)
{
	logGlobal->debug("Building widget Layout");
	bool vertical = config["vertical"].Bool();
	bool horizontal = config["horizontal"].Bool();
	bool dynamic = config["dynamic"].Bool();
	int distance = config["distance"].Integer();
	std::string customType = config["customType"].String();
	auto position = readPosition(config["position"]);

	auto result = std::make_shared<InterfaceLayoutWidget>();
	result->moveBy(position);
	Point layoutPosition;

	for(auto item : config["items"].Vector())
	{
		if (item["type"].String().empty())
			item["type"].String() = customType;

		if (!item["created"].isNull())
		{
			std::string name = item["created"].String();

			if (conditionals.count(name) != 0)
			{
				if (!conditionals.at(name))
					continue;
			}
			else
			{
				logMod->warn("Unknown condition %s in widget!", name);
			}
		}

		auto widget = buildWidget(item);

		addWidget(item["name"].String(), widget);
		result->ownedChildren.push_back(widget);
		result->addChild(widget.get(), false);

		widget->moveBy(position + layoutPosition);

		if (dynamic && vertical)
			layoutPosition.y += widget->pos.h;
		if (dynamic && horizontal)
			layoutPosition.x += widget->pos.w;

		if (vertical)
			layoutPosition.y += distance;
		if (horizontal)
			layoutPosition.x += distance;
	}

	return result;
}

std::shared_ptr<CShowableAnim> InterfaceObjectConfigurable::buildAnimation(const JsonNode & config) const
{
	logGlobal->debug("Building widget CShowableAnim");
	auto position = readPosition(config["position"]);
	auto image = AnimationPath::fromJson(config["image"]);
	ui8 flags = 0;
	if(!config["repeat"].Bool())
		flags |= CShowableAnim::EFlags::PLAY_ONCE;
	
	int group = config["group"].isNull() ? 0 : config["group"].Integer();
	auto anim = std::make_shared<CShowableAnim>(position.x, position.y, image, flags, 4, group);
	if(!config["alpha"].isNull())
		anim->setAlpha(config["alpha"].Integer());
	if(!config["callback"].isNull())
		anim->callback = std::bind(callbacks_int.at(config["callback"].String()), 0);
	if(!config["frames"].isNull())
	{
		auto b = config["frames"]["start"].Integer();
		auto e = config["frames"]["end"].Integer();
		anim->set(group, b, e);
	}
	return anim;
}

std::shared_ptr<CIntObject> InterfaceObjectConfigurable::buildGraphicalPrimitive(const JsonNode & config) const
{
	logGlobal->debug("Building widget GraphicalPrimitiveCanvas");

	auto rect = readRect(config["rect"]);
	auto widget = std::make_shared<GraphicalPrimitiveCanvas>(rect);

	for (auto const & entry : config["primitives"].Vector())
	{
		auto color = readColor(entry["color"]);
		auto typeString = entry["type"].String();
		auto pointA = readPosition(entry["a"]);
		auto pointB = readPosition(entry["b"]);

		if (typeString == "line")
			widget->addLine(pointA, pointB, color);
		if (typeString == "filledBox")
			widget->addBox(pointA, pointB, color);
		if (typeString == "rectangle")
			widget->addRectangle(pointA, pointB, color);
	}

	return widget;
}

std::shared_ptr<TransparentFilledRectangle> InterfaceObjectConfigurable::buildTransparentFilledRectangle(const JsonNode & config) const
{
	logGlobal->debug("Building widget TransparentFilledRectangle");

	auto rect = readRect(config["rect"]);
	auto color = readColor(config["color"]);
	if(!config["colorLine"].isNull())
	{
		auto colorLine = readColor(config["colorLine"]);
		return std::make_shared<TransparentFilledRectangle>(rect, color, colorLine);
	}
	return std::make_shared<TransparentFilledRectangle>(rect, color);
}

std::shared_ptr<CTextBox> InterfaceObjectConfigurable::buildTextBox(const JsonNode & config) const
{
	logGlobal->debug("Building widget CTextBox");

	auto rect = readRect(config["rect"]);
	auto font = readFont(config["font"]);
	auto alignment = readTextAlignment(config["alignment"]);
	auto color = readColor(config["color"]);
	auto text = readText(config["text"]);
	auto blueTheme = config["blueTheme"].Bool();

	return std::make_shared<CTextBox>(text, rect, blueTheme ? 1 : 0, font, alignment, color);
}

std::shared_ptr<LRClickableAreaWText> InterfaceObjectConfigurable::buildHoverHelp(const JsonNode & config) const
{
	logGlobal->debug("Building widget LRClickableAreaWText");

	auto rect = readRect(config["rect"]);
	auto hint = readHintText(config["help"]);

	return std::make_shared<LRClickableAreaWText>(rect, hint.first, hint.second);
}

std::shared_ptr<CIntObject> InterfaceObjectConfigurable::buildPages(const JsonNode & config)
{
	logGlobal->debug("Building widget LayoutPages");
	return std::make_shared<LayoutPages>(*this, config);
}

std::shared_ptr<CIntObject> InterfaceObjectConfigurable::buildWidget(JsonNode config) const
{
	assert(!config.isNull());
	logGlobal->debug("Building widget from config");
	//overrides from variables
	for(auto & item : config["overrides"].Struct())
	{
		logGlobal->debug("Config attribute %s was overridden by variable %s", item.first, item.second.String());
		config[item.first] = variables[item.second.String()];
	}
	
	auto type = config["type"].String();
	auto buildIterator = builders.find(type);
	if(buildIterator != builders.end())
		return (buildIterator->second)(config);

	logGlobal->error("Builder with type %s is not registered", type);
	return nullptr;
}

void InterfaceObjectConfigurable::setShortcutBlocked(EShortcut shortcut, bool isBlocked)
{
	auto target = shortcuts.find(shortcut);
	if (target == shortcuts.end())
		return;

	target->second.blocked = isBlocked;

	for	(auto & entry : widgets)
	{
		auto button = std::dynamic_pointer_cast<CButton>(entry.second);

		if (button && button->assignedKey == shortcut)
			button->block(isBlocked);
	}
}

void InterfaceObjectConfigurable::addShortcut(EShortcut shortcut, std::function<void()> callback)
{
	assert(shortcuts.count(shortcut) == 0);
	shortcuts[shortcut].callback = callback;
}

void InterfaceObjectConfigurable::keyPressed(EShortcut key)
{
	auto target = shortcuts.find(key);
	if (target == shortcuts.end())
		return;

	for (auto const & button :target->second.assignedButtons)
		if (button->isActive())
			return; // will be handled by button instance

	if (target->second.blocked)
		return;

	target->second.callback();
}

LayoutPage::LayoutPage(const InterfaceObjectConfigurable & owner, const JsonNode & layout, const std::string & scope)
	: InterfaceObjectConfigurable()
{
	OBJECT_CONSTRUCTION;
	// A page has no background of its own: when a value label changes, the owner, which has one,
	// repaints under it. Otherwise the old text stays under the new.
	setRedrawParent(true);
	inheritFrom(owner);
	layoutScope = scope;
	build(layout);
	if(onPageBuilt)
		onPageBuilt(*this);
}

LayoutPages::LayoutPages(InterfaceObjectConfigurable & owner, const JsonNode & config)
	: owner(owner)
	, id(config["id"].String())
{
	OBJECT_CONSTRUCTION;
	setRedrawParent(true);
	pos.w = owner.pos.w;
	pos.h = owner.pos.h;
	pagePosition = owner.readPosition(config["position"]);

	const std::string ownScope = owner.layoutScope;
	for(const auto & entry : config["pages"].Vector())
		pages.push_back({entry["layout"], entry["title"], ownScope, ownScope.empty() ? "the game" : ownScope});

	// the pages enabled mods add to this one by its id (mod.json "tabPages"), after its own, in load order
	if(!id.empty())
	{
		for(const auto & modID : LIBRARY->modh->getActiveMods())
		{
			for(const auto & entry : LIBRARY->modh->getModInfo(modID).getLocalValue("tabPages").Vector())
			{
				if(entry["target"].String() != id)
					continue;
				pages.push_back({entry["layout"], entry["title"], modID, modID});
				logMod->info("Mod %s adds the page %s to the pages %s", modID, entry["layout"].String(), id);
			}
		}
	}

	if(!config["title"].isNull())
		title = owner.buildLabel(config["title"]);

	const auto arrow = [this](const JsonNode & spec, int direction, EShortcut key) -> std::shared_ptr<CButton>
	{
		if(spec.isNull())
			return nullptr;
		return std::make_shared<CButton>(this->owner.readPosition(spec["position"]), AnimationPath::fromJson(spec["image"]),
			CButton::tooltip(), [this, direction]() { step(direction); }, key);
	};
	previous = arrow(config["previous"], -1, EShortcut::MOVE_LEFT);
	next = arrow(config["next"], 1, EShortcut::MOVE_RIGHT);

	size_t first = 0;
	if(config["remember"].isString())
	{
		remember = settingPath(config["remember"]);
		const double stored = settingNumber(remember, 0);
		if(stored >= 0 && stored < static_cast<double>(pages.size()))
			first = static_cast<size_t>(stored);
	}

	shown = std::make_shared<CTabbedInt>(std::bind(&LayoutPages::createPage, this, std::placeholders::_1), pagePosition, first);
	shown->setRedrawParent(true);
	updateAround();
}

std::shared_ptr<CIntObject> LayoutPages::createPage(size_t index)
{
	if(index >= pages.size())
		return std::make_shared<CIntObject>();

	const Page & page = pages[index];
	const JsonPath path = JsonPath::builtin(page.layout.String());
	try
	{
		const auto * files = page.scope.empty() ? CResourceHandler::get() : CResourceHandler::get(page.scope);
		if(files->existsResource(path))
			return std::make_shared<LayoutPage>(owner, page.scope.empty() ? JsonNode(path) : JsonNode(path, page.scope), page.scope);
	}
	catch(const std::out_of_range &)
	{
		// the mod's files are not loaded: the mod is not active
	}
	logMod->error("Pages %s: the layout %s from %s is missing", id, page.layout.String(), page.from);
	return std::make_shared<CIntObject>();
}

std::string LayoutPages::pageTitle(size_t index) const
{
	return index < pages.size() ? owner.readText(pages[index].title) : std::string();
}

void LayoutPages::updateAround()
{
	const size_t index = current();
	if(title)
		title->setText(pageTitle(index));

	for(const auto & [button, direction] : { std::make_pair(previous, -1), std::make_pair(next, 1) })
	{
		if(!button)
			continue;
		if(pages.size() < 2)
		{
			button->disable(); // nothing to page to
			continue;
		}
		const size_t target = (index + pages.size() + direction) % pages.size();
		button->setHelp(CButton::tooltip(pageTitle(target)));
	}
}

size_t LayoutPages::count() const
{
	return pages.size();
}

size_t LayoutPages::current() const
{
	return shown ? shown->getActive() : 0;
}

void LayoutPages::showPage(size_t index)
{
	if(pages.empty())
		return;
	index %= pages.size();
	shown->setActive(index);
	updateAround();
	if(!remember.empty())
		writeSetting(remember, JsonNode(static_cast<int32_t>(index)));
	logGlobal->info("Pages %s: page %d of %d, %s", id, static_cast<int>(index) + 1, static_cast<int>(pages.size()), pageTitle(index));
	owner.redraw();
}

void LayoutPages::step(int direction)
{
	if(!pages.empty())
		showPage((current() + pages.size() + direction) % pages.size());
}

void LayoutPages::refresh()
{
	if(shown)
		shown->reset();
	owner.redraw();
}

std::shared_ptr<LayoutPage> LayoutPages::shownPage() const
{
	return shown ? std::dynamic_pointer_cast<LayoutPage>(shown->getItem()) : nullptr;
}
