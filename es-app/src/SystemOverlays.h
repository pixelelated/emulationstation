#pragma once
#ifndef ES_APP_SYSTEM_OVERLAYS_H
#define ES_APP_SYSTEM_OVERLAYS_H

#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

// The es_systems_*.cfg overlays SystemData::loadAdditionnalConfig merges
// into the systems table (pixelelated/distribution#649, PL-004): which
// files, in what order, and what happens to one that cannot be read. Pure,
// so es-unit-tests holds it (SystemOverlaysTests.cpp); the XML merge stays
// in SystemData.
namespace SystemOverlays
{
	// How reading one overlay went: merged, not XML, or XML without the
	// <systemList> it must carry.
	enum class Read { Ok, Unparsable, NoSystemList };

	inline std::string fileName(const std::string& path)
	{
		const size_t slash = path.find_last_of('/');
		return slash == std::string::npos ? path : path.substr(slash + 1);
	}

	inline bool isOverlay(const std::string& path)
	{
		const std::string name = fileName(path);
		return name.compare(0, 11, "es_systems_") == 0 && name.size() > 4 && name.compare(name.size() - 4, 4, ".cfg") == 0;
	}

	// The overlays to merge, in order: the image's folder first and the
	// player's last, so a player's overlay has the last word; each folder in
	// name order, so two overlays touching one system merge the same way on
	// every device. It was the player's folder first and each folder in the
	// order the file system listed it, so the image's overlays won and the
	// order was the directory's. One folder serving as both is read once.
	inline std::vector<std::string> order(const std::string& userDir, const std::vector<std::string>& user, const std::string& imageDir, const std::vector<std::string>& image)
	{
		auto sorted = [](const std::vector<std::string>& entries)
		{
			std::vector<std::string> out;
			for (const auto& f : entries)
				if (isOverlay(f))
					out.push_back(f);
			std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return fileName(a) < fileName(b); });
			return out;
		};

		std::vector<std::string> out = sorted(image);
		if (userDir != imageDir)
			for (const auto& f : sorted(user))
				out.push_back(f);
		return out;
	}

	// Reads each file in turn with `read`, which merges it and says how it
	// went. A file that cannot be read is returned in the refused list, with
	// why, and the next file is still read: one broken overlay ended the
	// read, and every overlay after it was lost without a word.
	inline std::vector<std::pair<std::string, Read>> readEach(const std::vector<std::string>& files, const std::function<Read(const std::string&)>& read)
	{
		std::vector<std::pair<std::string, Read>> refused;
		for (const auto& f : files)
		{
			const Read r = read(f);
			if (r != Read::Ok)
				refused.push_back({ f, r });
		}
		return refused;
	}
}

#endif // ES_APP_SYSTEM_OVERLAYS_H
