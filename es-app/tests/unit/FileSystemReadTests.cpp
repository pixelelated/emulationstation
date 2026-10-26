// Utils::FileSystem::readAllText and readAllLines against real files (#1177):
// a file under three bytes, read past the UTF-8 BOM probe. The shipped
// FileSystemUtil.cpp is compiled in; the handful of things it reaches that
// a read never uses -- Paths, ZipFile's CRC, DateTime, the file cache's setting -- are
// stand-ins defined here.
#include "doctest/doctest.h"
#include "utils/FileSystemUtil.h"
#include "utils/ZipFile.h"
#include "Paths.h"
#include "Settings.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

// ---- stand-ins for what a read never reaches ----------------------------------
bool Settings::_UseFileCache = false;
Paths* Paths::_instance = nullptr;
Paths::Paths() {}
std::string& Paths::getHomePath() { static std::string home = "/tmp"; return home; }
unsigned int Utils::Zip::ZipFile::computeCRC(unsigned int crc, const void*, size_t) { return crc; }
Utils::Time::DateTime::DateTime() {}
Utils::Time::DateTime::DateTime(const time_t&) {}

namespace
{
	std::string scratchFile(const std::string& bytes)
	{
		const char* base = getenv("TMPDIR");
		std::string path = std::string(base != nullptr && *base ? base : "/tmp") + "/es-file-read-XXXXXX";
		std::vector<char> name(path.begin(), path.end());
		name.push_back('\0');
		const int fd = mkstemp(name.data());
		REQUIRE(fd >= 0);
		REQUIRE(write(fd, bytes.data(), bytes.size()) == (ssize_t) bytes.size());
		close(fd);
		return std::string(name.data());
	}
}

TEST_CASE("FileSystem: readAllText reads a file under three bytes whole (#1177)")
{
	for (const std::string& bytes : { std::string(""), std::string("7"), std::string("7\n"), std::string("12\n"), std::string("123\n") })
	{
		const std::string path = scratchFile(bytes);
		INFO("bytes: " << bytes.size());
		CHECK(Utils::FileSystem::readAllText(path) == bytes);
		unlink(path.c_str());
	}
}

TEST_CASE("FileSystem: readAllText still skips a UTF-8 byte-order mark (#1177)")
{
	const std::string path = scratchFile("\xEF\xBB\xBF" "abc\n");
	CHECK(Utils::FileSystem::readAllText(path) == "abc\n");
	unlink(path.c_str());
	const std::string bomOnly = scratchFile("\xEF\xBB\xBF");
	CHECK(Utils::FileSystem::readAllText(bomOnly) == "");
	unlink(bomOnly.c_str());
}

TEST_CASE("FileSystem: readAllLines reads a two-byte file's line (#1177)")
{
	const std::string path = scratchFile("7\n");
	const auto lines = Utils::FileSystem::readAllLines(path);
	REQUIRE(lines.size() == 1);
	CHECK(lines.front() == "7");
	unlink(path.c_str());
}
