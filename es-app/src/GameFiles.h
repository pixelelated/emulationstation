#pragma once
#ifndef ES_APP_GAME_FILES_H
#define ES_APP_GAME_FILES_H

#include <string>
#include <vector>

// What DELETE GAME may remove (pixelelated/distribution#649, PL-002). A
// game's playlist, cue sheet or gdi lists the files it is made of, and its
// metadata names its art; FileData::deleteGameFiles removes each only when
// it lies inside the game's system folder. A list is the player's text, or
// a scraper's, and it named an absolute path or one climbing out with
// "..", which the delete removed wherever it was. Pure, so es-unit-tests
// holds it (GameFilesTests.cpp); the caller resolves links first.
namespace GameFiles
{
	// `path` with "." and ".." folded and repeated separators dropped, as
	// text; "" when it is not absolute. ".." at the root stays at the root.
	inline std::string normalise(const std::string& path)
	{
		if (path.empty() || path[0] != '/')
			return "";

		std::vector<std::string> parts;
		size_t i = 0;
		while (i < path.size())
		{
			size_t j = path.find('/', i);
			if (j == std::string::npos)
				j = path.size();

			const std::string part = path.substr(i, j - i);
			if (part == "..")
			{
				if (!parts.empty())
					parts.pop_back();
			}
			else if (!part.empty() && part != ".")
				parts.push_back(part);

			i = j + 1;
		}

		std::string out;
		for (const auto& part : parts)
			out += "/" + part;
		return out.empty() ? "/" : out;
	}

	// Whether `path` lies below `folder`: the folder itself is not inside
	// it, and neither is a sibling whose name begins with the folder's.
	inline bool isInside(const std::string& folder, const std::string& path)
	{
		const std::string f = normalise(folder);
		const std::string p = normalise(path);
		if (f.empty() || p.empty() || p == f)
			return false;

		const std::string prefix = f == "/" ? f : f + "/";
		return p.compare(0, prefix.size(), prefix) == 0;
	}

	// A cue sheet's line without a UTF-8 byte-order mark and the spaces and
	// tabs before its keyword, so an indented FILE line or the first line of
	// a file saved with a mark is still read.
	inline std::string cueLine(const std::string& line)
	{
		size_t i = 0;
		if (line.compare(0, 3, "\xEF\xBB\xBF") == 0)
			i = 3;
		while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
			i++;
		return line.substr(i);
	}
}

#endif // ES_APP_GAME_FILES_H
