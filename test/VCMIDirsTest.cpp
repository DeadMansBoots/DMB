/*
 * VCMIDirsTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "../lib/VCMIDirs.h"

#ifndef VCMI_WINDOWS
#include <sys/stat.h>
#include <unistd.h>
#endif

// DMB: an earlier DMB's user folders ("DMB", "dmb") taking the current names, on scratch folders

namespace bfs = boost::filesystem;

namespace
{
class VCMIDirsRenameTest : public ::testing::Test
{
protected:
	bfs::path root;
	std::vector<std::string> steps;
	std::vector<std::string> standing;

	void SetUp() override
	{
		root = bfs::temp_directory_path() / bfs::unique_path("dmb-user-folders-%%%%-%%%%-%%%%");
		bfs::create_directories(root);
	}

	void TearDown() override
	{
		boost::system::error_code ec;
		bfs::remove_all(root, ec);
	}

	static void write(const bfs::path & file, const std::string & text = "x")
	{
		bfs::create_directories(file.parent_path());
		std::ofstream out(file.c_str(), std::ios::binary);
		out << text;
	}

	static std::string read(const bfs::path & file)
	{
		std::ifstream in(file.c_str(), std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	}

	bool rename(const std::vector<std::pair<bfs::path, bfs::path>> & folders)
	{
		return VCMIDirs::renameEarlierUserFolders(folders, steps, standing);
	}
};
}

TEST_F(VCMIDirsRenameTest, nothingEarlier)
{
	EXPECT_TRUE(rename({{root / "DMB", root / "Dead Man's Boots"}}));
	EXPECT_TRUE(steps.empty());
	EXPECT_TRUE(standing.empty());
	EXPECT_FALSE(bfs::exists(root / "Dead Man's Boots"));
}

TEST_F(VCMIDirsRenameTest, renamesDmbsFolder)
{
	write(root / "DMB" / "cache" / "downloads" / "dmbCodePins.json", "{}");
	write(root / "DMB" / "Saves" / "first.vsgm1", "save");
	write(root / "DMB" / "config" / "settings.json", "{}");

	EXPECT_TRUE(rename({{root / "DMB", root / "Dead Man's Boots"}}));
	EXPECT_FALSE(bfs::exists(root / "DMB"));
	EXPECT_EQ(read(root / "Dead Man's Boots" / "Saves" / "first.vsgm1"), "save");
	EXPECT_TRUE(bfs::exists(root / "Dead Man's Boots" / "config" / "settings.json"));
	ASSERT_EQ(steps.size(), 1u);
	EXPECT_EQ(steps[0].find("renamed "), 0u);
	EXPECT_TRUE(standing.empty());
}

TEST_F(VCMIDirsRenameTest, knowsDmbByItsLogs)
{
	write(root / "DMB" / "logs" / "VCMI_Client_log.txt", "Starting client of 'VCMI 1.7.5'\nOmniAI's folder: somewhere\n");
	EXPECT_TRUE(rename({{root / "DMB", root / "Dead Man's Boots"}}));
	EXPECT_TRUE(bfs::exists(root / "Dead Man's Boots" / "logs" / "VCMI_Client_log.txt"));

	write(root / "other" / "DMB" / "logs" / "VCMI_Launcher_log.txt", "DMB's mod catalog pins the code of 3 mods\n");
	EXPECT_TRUE(rename({{root / "other" / "DMB", root / "other" / "Dead Man's Boots"}}));
	EXPECT_FALSE(bfs::exists(root / "other" / "DMB"));
}

TEST_F(VCMIDirsRenameTest, leavesAnotherProgramsFolderAlone)
{
	// a VCMI log without DMB's lines, and settings, are not DMB's: stock VCMI and others have them too
	write(root / "DMB" / "config" / "settings.json", "{}");
	write(root / "DMB" / "logs" / "VCMI_Client_log.txt", "Starting client of 'VCMI 1.7.5'\n");

	EXPECT_TRUE(rename({{root / "DMB", root / "Dead Man's Boots"}}));
	EXPECT_TRUE(bfs::exists(root / "DMB" / "config" / "settings.json"));
	EXPECT_FALSE(bfs::exists(root / "Dead Man's Boots"));
	EXPECT_TRUE(steps.empty());
	ASSERT_EQ(standing.size(), 1u);
	EXPECT_EQ(standing[0].find("left alone"), 0u);
}

TEST_F(VCMIDirsRenameTest, keepsBothWhenTheCurrentHasSettings)
{
	write(root / "DMB" / "cache" / "downloads" / "dmbCodePins.json", "{}");
	write(root / "Dead Man's Boots" / "config" / "settings.json", "current");

	EXPECT_TRUE(rename({{root / "DMB", root / "Dead Man's Boots"}}));
	EXPECT_TRUE(bfs::exists(root / "DMB" / "cache" / "downloads" / "dmbCodePins.json"));
	EXPECT_EQ(read(root / "Dead Man's Boots" / "config" / "settings.json"), "current");
	EXPECT_TRUE(steps.empty());
	ASSERT_EQ(standing.size(), 1u);
	EXPECT_EQ(standing[0].find("both "), 0u);
}

TEST_F(VCMIDirsRenameTest, joinsGameFilesAnInstallerCopied)
{
	write(root / "DMB" / "cache" / "downloads" / "dmbCodePins.json", "{}");
	write(root / "DMB" / "Saves" / "first.vsgm1", "save");
	write(root / "DMB" / "Data" / "H3bitmap.lod", "earlier");
	write(root / "Dead Man's Boots" / "Data" / "H3bitmap.lod", "current");

	EXPECT_TRUE(rename({{root / "DMB", root / "Dead Man's Boots"}}));
	EXPECT_EQ(read(root / "Dead Man's Boots" / "Saves" / "first.vsgm1"), "save");
	EXPECT_TRUE(bfs::exists(root / "Dead Man's Boots" / "cache" / "downloads" / "dmbCodePins.json"));
	EXPECT_EQ(read(root / "Dead Man's Boots" / "Data" / "H3bitmap.lod"), "current");
	// what both had stays where it was
	EXPECT_EQ(read(root / "DMB" / "Data" / "H3bitmap.lod"), "earlier");
	EXPECT_FALSE(bfs::exists(root / "DMB" / "Saves"));
}

TEST_F(VCMIDirsRenameTest, renamesEveryFolderWhenOneHasTheTrace)
{
	// Linux: the pins are in the cache folder, the saves in the data folder, the settings in the config folder
	write(root / "cache" / "dmb" / "downloads" / "dmbCodePins.json", "{}");
	write(root / "share" / "dmb" / "Saves" / "first.vsgm1", "save");
	write(root / "config" / "dmb" / "settings.json", "{}");

	EXPECT_TRUE(rename({
		{root / "share" / "dmb", root / "share" / "dead-mans-boots"},
		{root / "cache" / "dmb", root / "cache" / "dead-mans-boots"},
		{root / "config" / "dmb", root / "config" / "dead-mans-boots"},
	}));
	EXPECT_EQ(read(root / "share" / "dead-mans-boots" / "Saves" / "first.vsgm1"), "save");
	EXPECT_TRUE(bfs::exists(root / "cache" / "dead-mans-boots" / "downloads" / "dmbCodePins.json"));
	EXPECT_TRUE(bfs::exists(root / "config" / "dead-mans-boots" / "settings.json"));
	EXPECT_FALSE(bfs::exists(root / "share" / "dmb"));
	EXPECT_EQ(steps.size(), 3u);
}

TEST_F(VCMIDirsRenameTest, failedRenameUndoesTheOthers)
{
	write(root / "a" / "dmb" / "downloads" / "dmbCodePins.json", "{}");
	write(root / "b" / "dmb" / "Saves" / "first.vsgm1", "save");

#ifdef VCMI_WINDOWS
	// a file open in a folder keeps Windows from renaming the folder
	std::ofstream held((root / "b" / "dmb" / "Saves" / "first.vsgm1").c_str(), std::ios::app);
	ASSERT_TRUE(held.is_open());
#else
	if(geteuid() == 0)
		GTEST_SKIP() << "root renames in a read-only folder";
	// a folder its owner cannot write keeps anything in it from being renamed
	ASSERT_EQ(chmod((root / "b").c_str(), 0555), 0);
#endif

	const bool usesCurrentNames = rename({
		{root / "a" / "dmb", root / "a" / "dead-mans-boots"},
		{root / "b" / "dmb", root / "b" / "dead-mans-boots"},
	});

#ifdef VCMI_WINDOWS
	held.close();
#else
	chmod((root / "b").c_str(), 0755);
#endif

	EXPECT_FALSE(usesCurrentNames);
	EXPECT_TRUE(bfs::exists(root / "a" / "dmb" / "downloads" / "dmbCodePins.json"));
	EXPECT_FALSE(bfs::exists(root / "a" / "dead-mans-boots"));
	EXPECT_EQ(read(root / "b" / "dmb" / "Saves" / "first.vsgm1"), "save");
	EXPECT_FALSE(bfs::exists(root / "b" / "dead-mans-boots"));
	ASSERT_FALSE(steps.empty());
	EXPECT_EQ(steps.back(), "this run uses the earlier name; the next start tries again");
}
