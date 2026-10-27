// Utils::FileSystem::renameFile and a file-mode download's start and end
// (T21 S4, #668, #1191), against real files in a scratch directory.
//
// FileSystemUtil.cpp is linked with the few things it reaches outside the
// file system defined here as doubles: Paths' home, Settings' file-cache
// switch, the zip CRC and DateTime's two constructors. HttpReq itself needs curl, which this host lacks;
// its file-mode start and end are Utils::DownloadFile, checked here.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "Paths.h"
#include "Settings.h"
#include "utils/DownloadFile.h"
#include "utils/FileSystemUtil.h"
#include "utils/TimeUtil.h"
#include "utils/ZipFile.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

// ------------------------------------------------------------ the doubles

Paths* Paths::_instance = nullptr;
Paths::Paths() {}
std::string& Paths::getHomePath()
{
	static std::string home = "/tmp";
	return home;
}
bool Settings::_UseFileCache = false;
unsigned int Utils::Zip::ZipFile::computeCRC(unsigned int crc, const void*, size_t) { return crc; }
// FileSystemUtil stamps a file's times through these two; nothing here
// reads a time.
Utils::Time::DateTime::DateTime() {}
Utils::Time::DateTime::DateTime(const time_t&) {}

namespace
{
	struct ScratchDir
	{
		std::string path;
		ScratchDir()
		{
			const char* base = getenv("TMPDIR");
			std::string tmpl = std::string(base != nullptr && *base != '\0' ? base : "/tmp") + "/es-fs-tests.XXXXXX";
			std::vector<char> buf(tmpl.begin(), tmpl.end());
			buf.push_back('\0');
			REQUIRE(mkdtemp(buf.data()) != nullptr);
			path = buf.data();
		}
		~ScratchDir()
		{
			const std::string cmd = "rm -rf '" + path + "'";
			if (std::system(cmd.c_str()) != 0)
				fprintf(stderr, "could not remove %s\n", path.c_str());
		}
		std::string operator/(const std::string& name) const { return path + "/" + name; }
	};

	void put(const std::string& path, const std::string& text)
	{
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out << text;
	}

	std::string get(const std::string& path)
	{
		std::ifstream in(path, std::ios::binary);
		return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	}

	bool there(const std::string& path)
	{
		return access(path.c_str(), F_OK) == 0;
	}

	// The download's bytes, written to its temporary as HttpReq's
	// write_content does.
	void writeTo(const std::string& tmp, const std::string& text)
	{
		FILE* file = fopen(tmp.c_str(), "wb");
		REQUIRE(file != nullptr);
		fwrite(text.data(), 1, text.size(), file);
		CHECK(Utils::DownloadFile::close(file));
	}
}

TEST_CASE("renameFile of a source that is not there fails and leaves the destination (T21 PL-04, #1191)")
{
	ScratchDir dir;
	put(dir / "old.png", "OLD");
	CHECK_FALSE(Utils::FileSystem::renameFile(dir / "absent.tmp", dir / "old.png"));
	CHECK(get(dir / "old.png") == "OLD");
}

TEST_CASE("renameFile replaces an existing destination with the source")
{
	ScratchDir dir;
	put(dir / "old.png", "OLD");
	put(dir / "new.tmp", "NEW");
	CHECK(Utils::FileSystem::renameFile(dir / "new.tmp", dir / "old.png"));
	CHECK(get(dir / "old.png") == "NEW");
	CHECK_FALSE(there(dir / "new.tmp"));
}

TEST_CASE("a download that fails after its start leaves the old file as it was (T21 PL-04, #1191)")
{
	ScratchDir dir;
	const std::string path = dir / "game-image.png";
	put(path, "OLD ART");
	const std::string tmp = Utils::DownloadFile::begin(path);
	writeTo(tmp, "PARTI");
	// The transfer fails: no finish; HttpReq's destructor removes its
	// temporary.
	Utils::FileSystem::removeFile(tmp);
	CHECK(get(path) == "OLD ART");
}

TEST_CASE("a stream that did not close cleanly is not put in place (T21 PL-04, #1191)")
{
	ScratchDir dir;
	const std::string path = dir / "game-image.png";
	put(path, "OLD ART");
	const std::string tmp = Utils::DownloadFile::begin(path);
	writeTo(tmp, "PARTI");
	CHECK_FALSE(Utils::DownloadFile::finish(tmp, path, false));
	CHECK(get(path) == "OLD ART");
	CHECK_FALSE(there(tmp));
}

TEST_CASE("a finish whose temporary is gone fails and leaves the old file (T21 PL-04, #1191)")
{
	ScratchDir dir;
	const std::string path = dir / "game-image.png";
	put(path, "OLD ART");
	const std::string tmp = Utils::DownloadFile::begin(path);
	writeTo(tmp, "NEW ART");
	Utils::FileSystem::removeFile(tmp);
	CHECK_FALSE(Utils::DownloadFile::finish(tmp, path, true));
	CHECK(get(path) == "OLD ART");
}

TEST_CASE("a whole download replaces the old file, and its temporary is gone")
{
	ScratchDir dir;
	const std::string path = dir / "game-image.png";
	put(path, "OLD ART");
	const std::string tmp = Utils::DownloadFile::begin(path);
	writeTo(tmp, "NEW ART");
	CHECK(Utils::DownloadFile::finish(tmp, path, true));
	CHECK(get(path) == "NEW ART");
	CHECK_FALSE(there(tmp));
}

TEST_CASE("two downloads of one path write to two temporaries (T21 PL-04, #1191)")
{
	ScratchDir dir;
	const std::string path = dir / "game-image.png";
	const std::string first = Utils::DownloadFile::begin(path);
	const std::string second = Utils::DownloadFile::begin(path);
	CHECK(first != second);
}

TEST_CASE("a write that never reached the disk is a failed close (T21 PL-04, #1191)")
{
	FILE* full = fopen("/dev/full", "wb");
	REQUIRE(full != nullptr);
	fwrite("X", 1, 1, full);
	CHECK_FALSE(Utils::DownloadFile::close(full));
	CHECK(full == nullptr);
}
