#pragma once
#ifndef ES_APP_SYSTEM_FOLDERS_H
#define ES_APP_SYSTEM_FOLDERS_H

#include <ctime>
#include <functional>
#include <string>
#include <vector>

// Which sub-folders a system's scan leaves alone (pixelelated/distribution
// #649, PL-003). Upstream skips the sub-folders of the ports and windows
// systems, whose many folders slowed the start; it tested for the letters
// anywhere in the sub-folder's path, so a gb folder named Sports, or any
// path holding "ports" or "windows" in a longer name, was skipped in every
// system. Pure, so es-unit-tests holds it (SystemFoldersTests.cpp).
namespace SystemFolders
{
	inline bool isSkippedSystem(const std::string& name)
	{
		return name == "ports" || name == "windows";
	}

	// The last component of `path`, without a trailing separator.
	inline std::string lastComponent(const std::string& path)
	{
		size_t end = path.find_last_not_of('/');
		if (end == std::string::npos)
			return "";
		const size_t start = path.find_last_of('/', end);
		return path.substr(start == std::string::npos ? 0 : start + 1, end - (start == std::string::npos ? 0 : start + 1) + 1);
	}

	// Whether the scan of the system named `system`, whose folder is
	// `startPath`, skips its sub-folder `subfolder`: only the ports and
	// windows systems, known by name or by their folder's name, and only
	// for a sub-folder below that folder.
	inline bool skipsSubfolder(const std::string& system, const std::string& startPath, const std::string& subfolder)
	{
		if (!isSkippedSystem(system) && !isSkippedSystem(lastComponent(startPath)))
			return false;

		const std::string root = startPath.empty() || startPath.back() == '/' ? startPath : startPath + "/";
		return subfolder.compare(0, root.size(), root) == 0;
	}

	// The newest modification time among `folders`, the folders a scan
	// entered (PL-015). A directory's time moves only when an entry is added
	// or removed directly in it, so the rescan after a game, which read the
	// system folder's own time, missed a file added inside a sub-folder; the
	// newest of them all moves for that, and for a sub-folder made or
	// removed (its parent's time). One stat per folder; 0 for none.
	inline time_t newestTime(const std::vector<std::string>& folders, const std::function<time_t(const std::string&)>& mtime)
	{
		time_t newest = 0;
		for (const auto& f : folders)
		{
			const time_t t = mtime(f);
			if (t > newest)
				newest = t;
		}
		return newest;
	}
}

#endif // ES_APP_SYSTEM_FOLDERS_H
