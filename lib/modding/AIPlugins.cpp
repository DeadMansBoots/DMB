/*
 * AIPlugins.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "AIPlugins.h"

#include "CModHandler.h"
#include "ModDescription.h"

#include "../GameLibrary.h"
#include "../VCMIDirs.h"
#include "../filesystem/Filesystem.h"
#include "../json/JsonNode.h"

VCMI_LIB_NAMESPACE_BEGIN

// The same folder ModManager and CModHandler use for a mod: "MODS/<ID>", a submod under its parent
static std::string modDirectory(const std::string & modID)
{
	std::string result = modID;
	boost::to_upper(result);
	boost::algorithm::replace_all(result, ".", "/MODS/");
	return "MODS/" + result;
}

std::optional<AIPluginInfo> AIPlugins::read(const ModDescription & mod)
{
	const JsonNode & section = mod.getLocalValue("aiPlugin");
	if(!section.isStruct())
		return std::nullopt;

	AIPluginInfo info;
	info.modID = mod.getID();
	info.name = section["name"].String();
	info.library = section["library"].String();
	info.builtFor = section["builtFor"].String();
	for(const auto & kind : section["kinds"].Vector())
	{
		info.adventure = info.adventure || kind.String() == "adventure";
		info.battle = info.battle || kind.String() == "battle";
	}

	// "initial" is the filesystem that indexes every Mods folder, the game's and the player's; the
	// library is no resource any filesystem lists, so it is found on disk beside the mod's own folder
	const ResourcePath directory(modDirectory(info.modID), EResType::DIRECTORY);
	const auto folder = CResourceHandler::get("initial")->getResourceName(directory);
	if(folder && !info.library.empty())
		info.path = boost::filesystem::absolute(*folder) / "ai" / VCMIDirs::get().libraryName(info.library);

#ifdef STATIC_AI
	info.problem = "this build of the game cannot load AI libraries";
#else
	if(info.name.empty() || info.library.empty())
		info.problem = "its mod.json gives no name or no library";
	else if(!info.adventure && !info.battle)
		info.problem = "its mod.json names no kind (adventure or battle)";
	else if(info.builtFor != DMB_VERSION_STRING)
		info.problem = "it was built for DMB " + (info.builtFor.empty() ? std::string("(none named)") : info.builtFor) + ", and this is DMB " DMB_VERSION_STRING;
	else if(!folder)
		info.problem = "its folder was not found";
	else if(!boost::filesystem::exists(info.path))
		info.problem = "its library is missing: ai/" + VCMIDirs::get().libraryName(info.library);
#endif
	return info;
}

std::vector<AIPluginInfo> AIPlugins::active()
{
	std::vector<AIPluginInfo> result;
	if(!LIBRARY || !LIBRARY->modh)
		return result;

	for(const auto & modID : LIBRARY->modh->getActiveMods())
	{
		auto info = read(LIBRARY->modh->getModInfo(modID));
		if(info)
			result.push_back(*info);
	}
	return result;
}

VCMI_LIB_NAMESPACE_END
