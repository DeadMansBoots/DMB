/*
 * MapGenerators.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "MapGenerators.h"

#include "AddonCode.h"
#include "CModHandler.h"
#include "ModDescription.h"

#include "../GameLibrary.h"
#include "../filesystem/Filesystem.h"
#include "../json/JsonNode.h"

VCMI_LIB_NAMESPACE_BEGIN

std::optional<MapGeneratorInfo> MapGenerators::read(const ModDescription & mod)
{
	const JsonNode & section = mod.getLocalValue("mapGenerator");
	if(!section.isStruct())
		return std::nullopt;

	MapGeneratorInfo info;
	info.modID = mod.getID();
	info.name = section["name"].String();
	info.tab = section["tab"].String();
	const std::string command = boost::filesystem::path(section["command"].String()).lexically_normal().generic_string();

	const auto folder = AddonCode::modFolder(info.modID);
	if(folder)
	{
		info.codeFolder = *folder / "generator";
		info.command = *folder / command;
	}

	bool tabFound = false;
	try
	{
		// the mod's own filesystem, so two generators' layouts never shadow each other
		tabFound = !info.tab.empty() && CResourceHandler::get(info.modID)->existsResource(JsonPath::builtin(info.tab));
	}
	catch(const std::out_of_range &)
	{
		// the mod's filesystem is not loaded: the mod is not active
	}

	if(info.name.empty() || command.empty() || info.tab.empty())
		info.problem = "its mod.json gives no name, command or tab";
	else if(!boost::starts_with(command, "generator/") || boost::contains(command, ".."))
		info.problem = "its command is not inside the mod's generator folder";
	else if(!folder)
		info.problem = "its folder was not found";
	else if(!boost::filesystem::exists(info.command))
		info.problem = "its command is missing: " + command;
	else if(!tabFound)
		info.problem = "its tab layout is missing from the mod: " + info.tab;
	return info;
}

std::vector<MapGeneratorInfo> MapGenerators::active()
{
	std::vector<MapGeneratorInfo> result;
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
