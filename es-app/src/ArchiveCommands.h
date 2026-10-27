#pragma once
//
// ArchiveCommands -- the shell commands the scraper and the manual viewer
// build around a file the player named, and the readers of their output
// (fork #668, T21: bug #1187 for the commands, #1205 for the readers).
//
// A path reaches 7zr, pdfinfo and pdftoppm inside single quotes, through
// Utils::String::shellQuote, never inside double quotes: double quotes leave
// $, backticks and backslashes live to the shell that ApiSystem's executors
// run (see the note at ApiSystem::executeEnumerationScript). Pure: strings in,
// strings out, so es-unit-tests proves the quoting and the readers without a
// process, and ApiSystem keeps only the call.
//
#include <cctype>
#include <string>
#include <vector>

#include "utils/StringUtil.h"

namespace ArchiveCommands
{
	// `<7z> x -so '<path>' | md5sum`: the one content file's digest, on one line.
	inline std::string md5Pipeline(const std::string& sevenZip, const std::string& path)
	{
		return sevenZip + " x -so " + Utils::String::shellQuote(path) + " | md5sum";
	}

	// `<7z> l -slt '<path>'`: the technical listing, one `CRC = ` line per entry.
	inline std::string listCommand(const std::string& sevenZip, const std::string& path)
	{
		return sevenZip + " l -slt " + Utils::String::shellQuote(path);
	}

	// `<7z> x '<path>' -y -o'<destination>'`: 7z reads -o glued to its value.
	inline std::string extractCommand(const std::string& sevenZip, const std::string& path, const std::string& destination)
	{
		return sevenZip + " x " + Utils::String::shellQuote(path) + " -y -o" + Utils::String::shellQuote(destination);
	}

	// `pdfinfo '<path>'`
	inline std::string pdfInfoCommand(const std::string& path)
	{
		return "pdfinfo " + Utils::String::shellQuote(path);
	}

	// `pdftoppm -jpeg -r <quality> -cropbox<page range> '<path>' '<output prefix>'`;
	// pageRange is "" or " -f N -l M", as extractPdfImages builds it.
	inline std::string pdfToPpmCommand(const std::string& quality, const std::string& pageRange, const std::string& path, const std::string& outputPrefix)
	{
		return "pdftoppm -jpeg -r " + quality + " -cropbox" + pageRange + " " + Utils::String::shellQuote(path) + " " + Utils::String::shellQuote(outputPrefix);
	}

	// md5sum over empty input: what a failed extraction hands the pipeline.
	// At the pin this digest was stored as the game's hash (#1205).
	inline const char* const EMPTY_INPUT_DIGEST = "d41d8cd98f00b204e9800998ecf8427e";

	// The digest the pipeline printed, or "" when there is none to trust: not
	// exactly one line, a line too short or not hexadecimal, or the digest of
	// empty input. "" tells the caller to hash another way or store nothing.
	inline std::string md5FromPipelineLines(const std::vector<std::string>& lines)
	{
		if (lines.size() != 1 || lines.front().length() < 32)
			return "";

		std::string md5 = lines.front().substr(0, 32);
		for (char c : md5)
			if (!std::isxdigit(static_cast<unsigned char>(c)))
				return "";

		if (md5 == EMPTY_INPUT_DIGEST)
			return "";

		return md5;
	}

	// The CRC from `7zr l -slt`'s lines, or "" when the listing carries none:
	// an empty `CRC = ` entry is not a CRC, and a line shorter than the file's
	// name never matches it (the pin subtracted the sizes unsigned, #1205).
	inline std::string crcFromListingLines(const std::vector<std::string>& lines, const std::string& fileName)
	{
		for (const std::string& all : lines)
		{
			size_t idx = all.find("CRC = ");
			if (idx != std::string::npos)
			{
				std::string crc = all.substr(idx + 6);
				if (!crc.empty())
					return crc;
			}
			else if (!fileName.empty() && fileName.size() <= all.size()
				&& all.find(fileName) == all.size() - fileName.size()
				&& all.length() > 9 && all[9] == ' ')
			{
				return all.substr(0, 8);
			}
		}

		return "";
	}
}
