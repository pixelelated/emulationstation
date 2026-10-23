#include "SaveState.h"
#include "SystemData.h"
#include "FileData.h"
#include "utils/StringUtil.h"
#include "utils/CommandLineUtil.h"
#include "ApiSystem.h"
#include "FileData.h"
#include "SaveStateRepository.h"
#include "SystemConf.h"
#include "LaunchTemplate.h"
#include "SaveStateFiles.h"
#include "Log.h"

std::string SaveState::makeStateFilename(int slot, bool fullPath, bool useImageGenerator) const
{
	std::string ret = useImageGenerator ? imageGenerator : fileGenerator;

	if (slot < 0) 
	{
		// only for the default behavior !?
		ret = Utils::String::replace(ret, "{{slot}}", ".auto");
		ret = Utils::String::replace(ret, "{{slot0}}", ".auto");
		ret = Utils::String::replace(ret, "{{slot00}}", ".auto");
		ret = Utils::String::replace(ret, "{{slot2d}}", ".auto");
	}
	else 
	{
		ret = Utils::String::replace(ret, "{{slot}}", slot == 0 ? "" : std::to_string(slot));
		ret = Utils::String::replace(ret, "{{slot0}}", std::to_string(slot));
		ret = Utils::String::replace(ret, "{{slot00}}", Utils::String::padLeft(std::to_string(slot), 2, '0'));
		ret = Utils::String::replace(ret, "{{slot2d}}", Utils::String::padLeft(std::to_string(slot), 2, '0'));	
	}

	if (fullPath)
		return Utils::FileSystem::combine(Utils::FileSystem::getParent(fileName), ret);

	return ret;
}

std::string SaveState::getScreenShot() const
{
  return screenshot;
}

// The option's value swapped in the launch command: the joined
// "--core=<v>" ROCKNIX passes (runemu.sh:23-26, fork #21) or the legacy
// "-core <v>", found only as a whole word of the command and never inside
// a quoted argument such as the ROM's path (#308 F-CS-33). The rule and its
// cases are Utils::CommandLine's (es-app/tests/unit/CommandLineTests.cpp).
static std::string _changeCommandlineArgument(const std::string& commandLine, const std::string& parameter, const std::string& value)
{
	return Utils::CommandLine::replaceOptionValue(commandLine, parameter, value);
}

std::string SaveState::setupSaveState(FileData* game, const std::string& command)
{
	if (game == nullptr)
		return command;

	std::string cmd = command;

	// Savestate has core information ? Setup correct emulator/core
	if (slot >= -1 && this->config != nullptr && !config->isActiveConfig(game) && !racommands)
	{
		cmd = _changeCommandlineArgument(cmd, "-emulator", config->emulator);
		cmd = _changeCommandlineArgument(cmd, "-core", config->core);
	}	

	bool supportsIncremental = config != nullptr ? config->incremental : game->getSourceFileData()->getSystem()->getSaveStateRepository()->supportsIncrementalSaveStates();

	bool incrementalSaveStates = SystemConf::getIncrementalSaveStates() && /*hasAutosave && */supportsIncremental;

	std::string path = Utils::FileSystem::getParent(fileName);

	if (slot == -1) // Run current AutoSave
	{
		if (racommands || fileName.empty())
			cmd = cmd + " -autosave 1 -state_slot " + std::to_string(slot);
		else
			cmd = cmd + " -autosave 1" + LaunchTemplate::stateFileOption(fileName);
	}
	else
	{
		if (slot == -2) // Run new game without AutoSave
		{
			cmd = cmd + " -autosave 0";
		}
		else if (incrementalSaveStates)
		{
			if (racommands)
			{
				cmd = cmd + " -state_slot " + std::to_string(slot); // slot

				// Run game, and activate AutoSave to load it
				if (!fileName.empty())
					cmd = cmd + " -autosave 1";
			}
			else
			  if (!fileName.empty())
			    cmd = cmd + " -state_slot " + std::to_string(slot) + LaunchTemplate::stateFileOption(fileName);
		}
		else
		{
			if (racommands)
			{
				cmd = cmd + " -state_slot " + std::to_string(slot);

				// Run game, and activate AutoSave to load it
				if (!fileName.empty())
					cmd = cmd + " -autosave 1";
			}
			else
			  if (!fileName.empty())
			    cmd = cmd + " -state_slot " + std::to_string(slot) + LaunchTemplate::stateFileOption(fileName);
		}

		if (racommands) 
		{
			// Copy to state.auto file
			auto autoFilename = makeStateFilename(-1);
			if (Utils::FileSystem::exists(autoFilename))
			{
				Utils::FileSystem::removeFile(autoFilename + ".bak");
				Utils::FileSystem::renameFile(autoFilename, autoFilename + ".bak");
			}

			// Copy to state.auto.png file
			auto autoImage = autoFilename + ".png";
			if (Utils::FileSystem::exists(autoImage))
			{
				Utils::FileSystem::removeFile(autoImage + ".bak");
				Utils::FileSystem::renameFile(autoImage, autoImage + ".bak");
			}

			mAutoImageBackup = autoImage;
			mAutoFileBackup = autoFilename;

			if (!fileName.empty())
			{
				Utils::FileSystem::copyFile(fileName, autoFilename);

				// Copy file to new slot, if the users want to reload the saved game in the slot directly from retroach
				//
				// Only when that slot is another file. Batocera's no-next-slot
				// patch (D-UI-057) changed the target from the next free slot
				// to the launched one, so this named the launched state
				// itself: removeFile deleted it and the copy from it then
				// failed -- the player's state was gone, with only the .auto
				// copy above left (#308 F-CS-14). With no free-slot target
				// left there is nothing for it to copy, and onGameEnded's
				// check below finds no mNewSlotFile.
				const std::string newSlotFile = makeStateFilename(slot);
				if (incrementalSaveStates && Utils::FileSystem::getGenericPath(newSlotFile) != Utils::FileSystem::getGenericPath(fileName))
				{
					mNewSlotFile = newSlotFile;
					Utils::FileSystem::removeFile(mNewSlotFile);
					if (Utils::FileSystem::copyFile(fileName, mNewSlotFile))
						mNewSlotCheckSum = ApiSystem::getInstance()->getMD5(fileName, false);
				}
			}
		}
	}

	return cmd;
}

void SaveState::onGameEnded(FileData* game)
{
	if (racommands)
	{
		// START NEW GAME (slot -2) parks the auto save as a numbered slot
		// does (setupSaveState's racommands block), so it is put back here
		// too: this returned for every slot below 0, and the auto save stayed
		// renamed to .bak for good (PL-017). Slot -1 parks nothing.
		if (slot == -1)
			return;

		if (!mNewSlotCheckSum.empty() && Utils::FileSystem::exists(mNewSlotFile))
		{
			// Check if the file in the slot has changed. If it's the same, then delete it & clear the slot
			auto fileChecksum = ApiSystem::getInstance()->getMD5(mNewSlotFile, false);
			if (fileChecksum == mNewSlotCheckSum)
				Utils::FileSystem::removeFile(mNewSlotFile);
		}

		if (!mAutoFileBackup.empty())
		{
			Utils::FileSystem::removeFile(mAutoFileBackup);
			Utils::FileSystem::renameFile(mAutoFileBackup + ".bak", mAutoFileBackup);
		}

		if (!mAutoImageBackup.empty())
		{
			Utils::FileSystem::removeFile(mAutoImageBackup);
			Utils::FileSystem::renameFile(mAutoImageBackup + ".bak", mAutoImageBackup);
		}
	}

	// No renumbering after a session (D-UI-069, maintainer 2026-09-16): a
	// slot's number never changes once written. Upstream closes the gaps a
	// deletion leaves by renaming every slot above it, which the save sync
	// reads as a deletion plus a new file -- a re-upload, a delete the
	// player never made, and on a second device a conflict out of nothing.
	// Gaps stay; the tiles show dates, and RetroArch's auto-increment saves
	// to the highest slot plus one whatever lies below.
}

// Utils::FileSystem's operations, for SaveStateFiles (PL-016).
static SaveStateFiles::Ops saveStateFileOps()
{
	SaveStateFiles::Ops ops;
	ops.move = [](const std::string& src, const std::string& dst) { return Utils::FileSystem::renameFile(src, dst); };
	ops.copy = [](const std::string& src, const std::string& dst) { return Utils::FileSystem::copyFile(src, dst); };
	ops.remove = [](const std::string& path) { return Utils::FileSystem::removeFile(path); };
	return ops;
}

bool SaveState::remove() const
{
	if (!isSlotValid())
		return false;

	const auto r = SaveStateFiles::removeFiles(saveStateFileOps(), fileName, getScreenShot());
	if (!r.state || !r.image)
		LOG(LogWarning) << "SaveState: could not remove " << (!r.state ? fileName : getScreenShot());

	return r.state && r.image;
}

// True when the state file reached its slot; the copy or move of its image
// is logged when it did not, and the tile then has no picture (PL-016: it
// was true whatever the operations returned).
bool SaveState::copyToSlot(int slot, bool move) const
{
	if (slot < 0)
		return false;

	if (!Utils::FileSystem::exists(fileName))
		return false;

	std::string destState = makeStateFilename(slot);
	std::string destStateImage = makeStateFilename(slot, true, true);

	const auto r = SaveStateFiles::transfer(saveStateFileOps(), move, fileName, getScreenShot(), destState, destStateImage);
	if (!r.state)
		LOG(LogWarning) << "SaveState: could not " << (move ? "move " : "copy ") << fileName << " to " << destState;
	else if (!r.image)
		LOG(LogWarning) << "SaveState: could not " << (move ? "move " : "copy ") << getScreenShot() << " to " << destStateImage;

	return r.state;
}

