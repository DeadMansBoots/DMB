/*
 * modstatecontroller.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "modstatecontroller.h"

#include "modstatemodel.h"

#include "../../lib/VCMIDirs.h"
#include "../../lib/CConfigHandler.h"
#include "../../lib/filesystem/Filesystem.h"
#include "../../lib/filesystem/CZipLoader.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/modding/IdentifierStorage.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/texts/CGeneralTextHandler.h"

#include "../vcmiqt/jsonutils.h"
#include "../vcmiqt/launcherdirs.h"

#include <future>

namespace
{
void findContentArchives(const QDir & currentDir, QVector<QString> & archives)
{
	const QFileInfoList entries = currentDir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
	for(const QFileInfo & entry : entries)
	{
		if(entry.isDir())
		{
			findContentArchives(QDir(entry.absoluteFilePath()), archives);
			continue;
		}

		if(entry.fileName().compare("content.zip", Qt::CaseInsensitive) == 0)
			archives.push_back(entry.absoluteFilePath());
	}
}

bool extractContentArchives(ModStateController * controller, const QString & modName, const QString & modPath)
{
	QVector<QString> archives;
	findContentArchives(QDir(modPath), archives);

	for(qint64 archiveIndex = 0; archiveIndex < archives.size(); ++archiveIndex)
	{
		controller->contentExtractionProgress(modName, archiveIndex + 1, archives.size());
		qApp->processEvents();

		const QString archivePath = archives[archiveIndex];
		const QFileInfo archiveInfo(archivePath);
		const QString contentDirPath = archiveInfo.dir().filePath("content");

		QDir contentDir(contentDirPath);
		if(!contentDir.exists() && !archiveInfo.dir().mkpath("content"))
		{
			logGlobal->error("Failed to create content directory for '%s'", archivePath.toStdString().c_str());
			return false;
		}

		auto futureExtract = std::async(std::launch::async, [archivePath, contentDirPath]()
		{
			ZipArchive archive(qstringToPath(archivePath));
			for(const auto & file : archive.listFiles())
			{
				if(!archive.extract(qstringToPath(contentDirPath), file))
				{
					logGlobal->error("Failed to extract '%s' from '%s'", file, archivePath.toStdString().c_str());
					return false;
				}
			}
			return true;
		});

		while(futureExtract.wait_for(std::chrono::milliseconds(10)) != std::future_status::ready)
		{
			qApp->processEvents();
		}

		if(!futureExtract.get())
			return false;

		if(!QFile::remove(archivePath))
		{
			logGlobal->error("Failed to remove extracted archive '%s'", archivePath.toStdString().c_str());
			return false;
		}
	}

	controller->contentExtractionProgress(modName, archives.size(), archives.size());
	qApp->processEvents();
	return true;
}

bool extractNestedModArchives(ModStateController * controller, const QString & modName, const QString & modPath)
{
	try
	{
		return extractContentArchives(controller, modName, modPath);
	}
	catch(const std::runtime_error & e)
	{
		logGlobal->error("Failed to extract nested mod archive. Reason: %s", e.what());
		return false;
	}
}

QString detectModArchive(QString path, QString modName, std::vector<std::string> & filesToExtract)
{
	try {
		ZipArchive archive(qstringToPath(path));
		filesToExtract = archive.listFiles();
	}
	catch (const std::runtime_error & e)
	{
		logGlobal->error("Failed to open zip archive. Reason: %s", e.what());
		return "";
	}

	QString modDirName;

	for(int folderLevel : {0, 1}) //search in subfolder if there is no mod.json in the root
	{
		for(const auto & file : filesToExtract)
		{
			QString filename = QString::fromUtf8(file.c_str());
			modDirName = filename.section('/', 0, folderLevel);
			
			if(filename == modDirName + "/mod.json")
			{
				return modDirName;
			}
		}
	}

	logGlobal->error("Failed to detect mod path in archive!");
	logGlobal->debug("List of file in archive:");
	for(const auto & file : filesToExtract)
		logGlobal->debug("%s", file.c_str());
	
	return "";
}
}


ModStateController::ModStateController(std::shared_ptr<ModStateModel> modList)
	: modList(modList)
{
	// DMB: what an uninstall renamed away but could not delete then, a file being in use (doUninstallMod)
	const QFileInfoList leftovers = QDir(CLauncherDirs::modsPath()).entryInfoList({"*.removing-*"}, QDir::Dirs | QDir::NoDotAndDotDot);
	for(const QFileInfo & leftover : leftovers)
		if(isInUserModsFolder(leftover.absoluteFilePath()) && !QDir(leftover.absoluteFilePath()).removeRecursively())
			logGlobal->warn("Could not delete %s yet, what an earlier uninstall left", leftover.absoluteFilePath().toStdString());
}

ModStateController::~ModStateController() = default;

void ModStateController::setRepositoryData(const JsonNode & repomap)
{
	modList->setRepositoryData(repomap);
}

bool ModStateController::addError(QString modname, QString message)
{
	recentErrors.push_back(QString("%1: %2").arg(modname).arg(message));
	return false;
}

QStringList ModStateController::getErrors()
{
	QStringList ret = recentErrors;
	recentErrors.clear();
	return ret;
}

bool ModStateController::installMod(QString modname, QString archivePath)
{
	return canInstallMod(modname) && doInstallMod(modname, archivePath);
}

bool ModStateController::uninstallMod(QString modname)
{
	return canUninstallMod(modname) && doUninstallMod(modname);
}

bool ModStateController::enableMods(QStringList modlist)
{
	for (const auto & modname : modlist)
		if (!canEnableMod(modname))
			return false;

	modList->doEnableMods(modlist);
	return true;
}

bool ModStateController::disableMod(QString modname)
{
	if (!canDisableMod(modname))
		return false;
	modList->doDisableMod(modname);
	return true;
}

bool ModStateController::canInstallMod(QString modname)
{
	if (!modList->isModExists(modname))
		return true; // for installation of unknown mods, e.g. via "Install from file" option

	auto mod = modList->getMod(modname);

	if(mod.isSubmod())
		return addError(modname, tr("Can not install submod"));

	if(mod.isInstalled())
		return addError(modname, tr("Mod is already installed"));
	return true;
}

bool ModStateController::canUninstallMod(QString modname)
{
	auto mod = modList->getMod(modname);

	if(mod.isSubmod())
		return addError(modname, tr("Can not uninstall submod"));

	if(!mod.isInstalled())
		return addError(modname, tr("Mod is not installed"));

	return true;
}

bool ModStateController::canEnableMod(QString modname)
{
	if (!modList->isModExists(modname))
		return false;

	auto mod = modList->getMod(modname);

	if(modList->isModEnabled(modname))
		return addError(modname, tr("Mod is already enabled"));

	if(!mod.isInstalled())
		return addError(modname, tr("Mod must be installed first"));

	//check for compatibility
	if(!mod.isCompatible())
		return addError(modname, tr("Mod is not compatible, please update VCMI and check the latest mod revisions"));

	if (mod.isTranslation() && CGeneralTextHandler::getPreferredLanguage() != mod.getBaseLanguage().toStdString())
		return addError(modname, tr("Can not enable translation mod for a different language!"));

	for(const auto & modEntry : mod.getDependencies())
	{
		if(!modList->isModExists(modEntry)) // required mod is not available
			return addError(modname, tr("Required mod %1 is missing").arg(modEntry));
	}

	return true;
}

bool ModStateController::canDisableMod(QString modname)
{
	auto mod = modList->getMod(modname);

	if(!modList->isModEnabled(modname))
		return addError(modname, tr("Mod is already disabled"));

	if(!mod.isInstalled())
		return addError(modname, tr("Mod must be installed first"));

	return true;
}

bool ModStateController::doInstallMod(QString modname, QString archivePath)
{
	const auto destDir = CLauncherDirs::modsPath() + QChar{'/'};

	if(!QFile(archivePath).exists())
		return addError(modname, tr("Mod archive is missing"));

	std::vector<std::string> filesToExtract;
	QString modDirName = ::detectModArchive(archivePath, modname, filesToExtract);
	if(!modDirName.size())
		return addError(modname, tr("Mod archive is invalid or corrupted"));
	
	std::atomic<int> filesCounter = 0;

	auto futureExtract = std::async(std::launch::async, [&archivePath, &destDir, &filesCounter, &filesToExtract]()
	{
		const auto destDirFsPath = qstringToPath(destDir);
		ZipArchive archive(qstringToPath(archivePath));
		for(const auto & file : filesToExtract)
		{
			if (!archive.extract(destDirFsPath, file))
				return false;
			++filesCounter;
		}
		return true;
	});
	
	while(futureExtract.wait_for(std::chrono::milliseconds(10)) != std::future_status::ready)
	{
		extractionProgress(filesCounter, filesToExtract.size());
		qApp->processEvents();
	}
	
	if(!futureExtract.get())
	{
		removeModDir(destDir + modDirName);
		return addError(modname, tr("Failed to extract mod data"));
	}

	//rename folder and fix the path
	QDir extractedDir(destDir + modDirName);
	auto rc = QFile::rename(destDir + modDirName, destDir + modname);
	if (rc)
		extractedDir.setPath(destDir + modname);

	// Remove .github folder from installed mod
	QDir githubDir(extractedDir.filePath(".github"));
	if (githubDir.exists())
		githubDir.removeRecursively();
	
	//there are possible excessive files - remove them
	QString upperLevel = modDirName.section('/', 0, 0);
	if(upperLevel != modDirName)
		removeModDir(destDir + upperLevel);

	if(settings["launcher"]["fullModExtraction"].Bool() && !extractNestedModArchives(this, modname, extractedDir.path()))
		return addError(modname, tr("Failed to extract mod data"));

	return true;
}

bool ModStateController::doUninstallMod(QString modname)
{
	ResourcePath resID(std::string("Mods/") + modname.toStdString(), EResType::DIRECTORY);
	// Get location of the mod, in case-insensitive way
	const auto location = CResourceHandler::get()->getResourceName(resID);
	if(!location)
		return addError(modname, tr("Mod data was not found"));
	QString modDir = pathToQString(*location);

	// DMB: the folder was there when the launcher last looked, so the player deleted it by hand, and what they
	// asked for is done; the reload after this drops the mod from the list
	if(!QDir(modDir).exists())
	{
		logGlobal->info("Mod '%s' was already removed from %s", modname.toStdString(), modDir.toStdString());
		return true;
	}

	QDir modFullDir(modDir);
	if(!isInUserModsFolder(modDir))
		return addError(modname, tr("Mod is located in a protected directory, please remove it manually:\n") + modFullDir.absolutePath());

	// DMB: all or nothing. Deleting in place stops at a file another program has open and leaves a broken
	// mod behind. So the folder first leaves the mod list in one step, a rename to a name VCMI never reads
	// as a mod (it has a dot); Windows refuses that rename while a file inside is open, and then nothing
	// is deleted and the player is told why. What cannot be deleted after the rename goes at the next start.
	const QString doomed = QDir::cleanPath(modFullDir.absolutePath()) + ".removing-" + QString::number(QDateTime::currentMSecsSinceEpoch());
	if(!QDir().rename(modFullDir.absolutePath(), doomed))
		return addError(modname, tr("Some of this mod's files are in use, perhaps by the game or another program. Close it, then uninstall again:\n") + modFullDir.absolutePath());
	if(!QDir(doomed).removeRecursively())
		logGlobal->warn("Mod '%s': some files in %s could not be deleted; the launcher deletes them at its next start", modname.toStdString(), doomed.toStdString());

	return true;
}

bool ModStateController::isInUserModsFolder(QString path)
{
	// issues 2673 and 2680 its why you do not recursively remove without sanity check
	// DMB: VCMI checked folder names here (a Mods folder inside one named vcmi). DMB's user folder has its own
	// name, so every uninstall was refused and players were told to delete mods by hand. Now a folder is
	// removed only when its parent is the user's Mods folder, the one the launcher installs into, compared as
	// the same folder on disk, so letter case and links do not matter.
	boost::system::error_code error;
	const auto parent = qstringToPath(QDir::cleanPath(QDir(path).absolutePath())).parent_path();
	return boost::filesystem::equivalent(parent, qstringToPath(CLauncherDirs::modsPath()), error) && !error;
}

bool ModStateController::removeModDir(QString path)
{
	return isInUserModsFolder(path) && QDir(path).removeRecursively();
}
