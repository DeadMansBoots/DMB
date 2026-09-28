/*
 * VCMIDirs.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

VCMI_LIB_NAMESPACE_BEGIN

class DLL_LINKAGE IVCMIDirs
{
public:
	// Path to user-specific data directory
	virtual boost::filesystem::path userDataPath() const = 0;

	// Path to "cache" directory, can be used for any non-essential files
	virtual boost::filesystem::path userCachePath() const = 0;

	// Path to writeable directory with user configs
	virtual boost::filesystem::path userConfigPath() const = 0;

	// Path to writeable directory to store log files
	virtual boost::filesystem::path userLogsPath() const;

	// Path to saved games
	virtual boost::filesystem::path userSavePath() const;

	// Path to "extracted" directory, used to temporarily hold extracted Original H3 files
	virtual boost::filesystem::path userExtractedPath() const;

	// Paths to global system-wide data directories. First items have higher priority
	virtual std::vector<boost::filesystem::path> dataPaths() const = 0;

	// Full path to client executable, including name (e.g. /usr/bin/vcmiclient)
	virtual boost::filesystem::path clientPath() const = 0;

	// Full path to editor executable, including name (e.g. /usr/bin/vcmieditor)
	virtual boost::filesystem::path mapEditorPath() const = 0;

	// Full path to server executable, including name (e.g. /usr/bin/vcmiserver)
	virtual boost::filesystem::path serverPath() const = 0;

	// Path where vcmi libraries can be found (in AI and Scripting subdirectories)
	virtual boost::filesystem::path libraryPath() const = 0;

	// absolute path to passed library (needed due to android libs being placed in single dir, not respecting original lib dirs;
	// by default just concats libraryPath, given folder and libraryName
	virtual boost::filesystem::path fullLibraryPath(const std::string & desiredFolder,
													const std::string & baseLibName) const;

	// Path where vcmi binaries can be found
	virtual boost::filesystem::path binaryPath() const = 0;

	// Returns system-specific name for dynamic libraries ( StupidAI => "libStupidAI.so" or "StupidAI.dll")
	virtual std::string libraryName(const std::string & basename) const = 0;
	// virtual std::string libraryName(const char* basename) const = 0; ?
	// virtual std::string libraryName(std::string&& basename) const = 0;?

	virtual std::string genHelpString() const;

	// DMB: stock VCMI's counterpart of one of DMB's default user folders (DMB's folder name swapped
	// for "vcmi"), so a player's existing VCMI setup can be copied across. Empty for a folder that
	// config/dirs.json sets, since that has no stock counterpart.
	boost::filesystem::path stockVcmiPath(const boost::filesystem::path & path) const;

	// Creates not existed, but required directories.
	// Updates directories what change name/path between versions.
	// Function called automatically.
	virtual void init();

	// DMB: what this start did to an earlier DMB's user folders, for the launcher to tell the player:
	// renamed them to the current name, or could not, and then this run keeps the earlier name. Empty
	// when there was nothing to move, and then the player hears nothing (K, September 27th).
	const std::vector<std::string> & earlierFolderSteps() const { return renameSteps; }
	bool keptEarlierFolders() const { return keptEarlierNames; }

protected:
	std::vector<std::string> renameSteps;
	bool keptEarlierNames = false;

	// DMB: the name of the user folders this run uses: the current one ("Dead Man's Boots", Linux
	// "dead-mans-boots"), or the earlier one when renaming the earlier folders failed this start
	const char * userDirName() const;
	const char * userDirNameXdg() const;

	// DMB: the user folders named after DMB where this platform puts them by default. None where the
	// platform names the folders itself (Android, iOS) or where config/dirs.json moves them.
	virtual std::vector<boost::filesystem::path> namedUserFolders() const;

	// DMB: an earlier DMB's user folders, named "DMB" (Linux "dmb"), take the current name before
	// anything reads them, when they hold DMB's own traces; a folder of that name that DMB did not make
	// is left alone. A rename that fails leaves this run on the earlier name, and the next start tries
	// again. The steps go to rename_log.txt in the logs folder.
	void renameEarlierFolders();
};

namespace VCMIDirs
{
	extern DLL_LINKAGE const IVCMIDirs & get();

	/// DMB: renames each earlier user folder (first) to its current name (second) when DMB made them,
	/// all or nothing, as IVCMIDirs::init() does on every start. False when this run has to keep the
	/// earlier names. What it did goes to `steps`, and what every start finds again (a folder left
	/// alone) to `standing`. Its own function so tests can run it on scratch folders.
	DLL_LINKAGE bool renameEarlierUserFolders(const std::vector<std::pair<boost::filesystem::path, boost::filesystem::path>> & folders,
		std::vector<std::string> & steps, std::vector<std::string> & standing);
}

VCMI_LIB_NAMESPACE_END
