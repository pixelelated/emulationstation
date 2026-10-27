// Settings itself -- es_settings.cfg's load, its record and its save -- against
// real files in a scratch directory (T21 S8, #668, #1200).
//
// Compiles the shipped Settings.cpp, FileSystemUtil.cpp and AtomicFileUtil.cpp
// with pugixml from the recipe's source (the submodule's commit), and defines
// as doubles what they reach outside the stores: the log, Paths, Scripting's
// events, the platform's memory size, the zip CRC and DateTime's two
// constructors. A fresh Settings per case through SettingsTestAccess (a friend
// Settings.h names), its user directory and the settings lock pointed at
// scratch paths. POSIX only, like the code.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "Log.h"
#include "Paths.h"
#include "Scripting.h"
#include "Settings.h"
#include "utils/AtomicFileUtil.h"
#include "utils/Platform.h"
#include "utils/TimeUtil.h"
#include "utils/ZipFile.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

// ------------------------------------------------------------ the doubles

LogLevel Log::mReportingLevel = LogDebug;
bool Log::mEnabled = true;

namespace
{
	std::mutex gLogMutex;
	std::string gLog;
	thread_local std::ostringstream tlLine;
}

Log::Log(LogLevel level) : mLevel(level)
{
	tlLine.str("");
	tlLine.clear();
}

Log::~Log()
{
	std::lock_guard<std::mutex> lock(gLogMutex);
	gLog += tlLine.str() + "\n";
}

std::ostringstream& Log::stream()
{
	return tlLine;
}

Paths* Paths::_instance = nullptr;
Paths::Paths() {}
std::string& Paths::getHomePath()
{
	static std::string home = "/tmp";
	return home;
}
void Scripting::fireEvent(const std::string&, const std::string&, const std::string&, const std::string&) {}
unsigned long long Utils::Platform::getTotalSystemMemory() { return 2ULL * 1024 * 1024 * 1024; }
unsigned int Utils::Zip::ZipFile::computeCRC(unsigned int crc, const void*, size_t) { return crc; }
Utils::Time::DateTime::DateTime() {}
Utils::Time::DateTime::DateTime(const time_t&) {}

struct SettingsTestAccess
{
	// A new Settings, loading <dir>/es_settings.cfg as the constructor does,
	// with the settings lock at <dir>/.system.cfg.lock.
	static Settings* fresh(const std::string& dir, int budgetMs = 300)
	{
		delete Settings::sInstance;
		Settings::sInstance = nullptr;
		Settings::sRecovered = false;
		Settings::sOutcome = RecoveryOutcome::None;
		Settings::sLockPath = dir + "/.system.cfg.lock";
		Settings::sLockBudgetMs = budgetMs;
		Paths::getUserEmulationStationPath() = dir;
		return Settings::getInstance();
	}

	static bool recovered() { return Settings::sRecovered; }
};

namespace
{
	struct ScratchDir
	{
		std::string path;
		ScratchDir()
		{
			const char* base = getenv("TMPDIR");
			std::string tmpl = std::string(base != nullptr && *base != '\0' ? base : "/tmp") + "/es-settings-tests.XXXXXX";
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

	const std::string kRecord =
		"<?xml version=\"1.0\"?>\n<config>\n\t<string name=\"ScreenScraperUser\" value=\"qa-user\" />\n\t<string name=\"Language\" value=\"fr_FR\" />\n</config>\n";
}

// ------------------------------------------------------------ the ladder (T21 PL-12, #1200)

TEST_CASE("an empty, a cut and a non-config es_settings.cfg each load the record (T21 PL-12, #1200)")
{
	const char* lives[] = {
		"",
		"<?xml version=\"1.0\"?>\n<config>\n\t<string name=\"ScreenScraperUser\" val",
		"<?xml version=\"1.0\"?>\n<other>\n\t<string name=\"Language\" value=\"en_US\" />\n</other>\n",
	};
	for (const char* live : lives)
	{
		ScratchDir dir;
		put(dir / "es_settings.cfg", live);
		put(dir / "es_settings.cfg.backup", kRecord);
		Settings* settings = SettingsTestAccess::fresh(dir.path);
		CAPTURE(std::string(live));
		CHECK(settings->getString("ScreenScraperUser") == "qa-user");
		CHECK(settings->getString("Language") == "fr_FR");
		CHECK(SettingsTestAccess::recovered());
		CHECK(get(dir / "es_settings.cfg.backup") == kRecord);
	}
}

TEST_CASE("a record write that fails keeps the save owed, and the next save writes the record (T21 PL-12, #1200)")
{
	ScratchDir dir;
	put(dir / "es_settings.cfg", kRecord);
	put(dir / "es_settings.cfg.backup", kRecord);
	Settings* settings = SettingsTestAccess::fresh(dir.path);

	// The record's name taken by a directory: its write fails.
	REQUIRE(std::remove((dir / "es_settings.cfg.backup").c_str()) == 0);
	REQUIRE(mkdir((dir / "es_settings.cfg.backup").c_str(), 0700) == 0);
	CHECK(settings->setString("Language", "de_DE"));
	settings->saveFile();
	CHECK(get(dir / "es_settings.cfg").find("de_DE") != std::string::npos);

	// The record's name free again: the next save, with nothing new
	// changed, writes the record it owes.
	REQUIRE(rmdir((dir / "es_settings.cfg.backup").c_str()) == 0);
	CHECK(settings->saveFile());
	CHECK(get(dir / "es_settings.cfg.backup").find("de_DE") != std::string::npos);
}

TEST_CASE("a save waits for the settings lock, and keeps the change when it cannot have it (D-CLOUD-186, T21 PL-12, #1200)")
{
	ScratchDir dir;
	put(dir / "es_settings.cfg", kRecord);
	put(dir / "es_settings.cfg.backup", kRecord);
	Settings* settings = SettingsTestAccess::fresh(dir.path, 100);

	Utils::AtomicFile::PidLock script(dir / ".system.cfg.lock");
	REQUIRE(script.acquire(1000));
	CHECK(settings->setString("Language", "de_DE"));
	CHECK_FALSE(settings->saveFile());
	CHECK(get(dir / "es_settings.cfg") == kRecord);
	script.release();
	CHECK(settings->saveFile());
	CHECK(get(dir / "es_settings.cfg").find("de_DE") != std::string::npos);
}

TEST_CASE("a save on one thread while another reads and writes (run under ThreadSanitizer for the race; T21 PL-12, #1200)")
{
	ScratchDir dir;
	put(dir / "es_settings.cfg", kRecord);
	put(dir / "es_settings.cfg.backup", kRecord);
	Settings* settings = SettingsTestAccess::fresh(dir.path, 2000);

	std::atomic<bool> stop(false);
	std::thread worker([&]
	{
		for (int i = 0; i < 50 && !stop; i++)
		{
			settings->setInt("QaCounter", i);
			settings->saveFile();
		}
	});
	for (int i = 0; i < 2000; i++)
	{
		(void) settings->getString("Language");
		settings->setString("QaOther", std::to_string(i % 7));
	}
	stop = true;
	worker.join();
	CHECK(settings->getString("Language") == "fr_FR");
}

// ------------------------------------------------------------ the sweeps (T21 PL-13, #1201)

TEST_CASE("Settings' start and save sweep a gone writer's temporaries (T21 PL-13, #1201)")
{
	ScratchDir dir;
	const pid_t child = fork();
	if (child == 0)
		_exit(0);
	int status = 0;
	waitpid(child, &status, 0);
	const std::string dead = std::to_string((long) child);
	put(dir / "es_settings.cfg", kRecord);
	put(dir / "es_settings.cfg.backup", kRecord);
	put(dir / ("es_settings.cfg.tmp." + dead + ".0"), "<?xml");
	put(dir / ("es_settings.cfg.backup.tmp." + dead + ".4"), kRecord);

	Settings* settings = SettingsTestAccess::fresh(dir.path);
	CHECK(::access((dir / ("es_settings.cfg.tmp." + dead + ".0")).c_str(), F_OK) != 0);
	CHECK(::access((dir / ("es_settings.cfg.backup.tmp." + dead + ".4")).c_str(), F_OK) != 0);

	put(dir / ("es_settings.cfg.tmp." + dead + ".5"), "<?xml");
	CHECK(settings->setString("Language", "de_DE"));
	CHECK(settings->saveFile());
	CHECK(::access((dir / ("es_settings.cfg.tmp." + dead + ".5")).c_str(), F_OK) != 0);
}

// ------------------------------------------------------------ the outcome (T21 PL-18, #1206)

TEST_CASE("Settings' recovery says which one it was: damaged, missing, or in use with the write-back owed (T21 PL-18, #1206)")
{
	SUBCASE("damaged, written back")
	{
		ScratchDir dir;
		put(dir / "es_settings.cfg", "<?xml version=\"1.0\"?>\n<config><string name=");
		put(dir / "es_settings.cfg.backup", kRecord);
		SettingsTestAccess::fresh(dir.path);
		CHECK(Settings::recoveryOutcome() == RecoveryOutcome::Restored);
	}
	SUBCASE("missing, written back")
	{
		ScratchDir dir;
		put(dir / "es_settings.cfg.backup", kRecord);
		SettingsTestAccess::fresh(dir.path);
		CHECK(Settings::recoveryOutcome() == RecoveryOutcome::Missing);
		CHECK(get(dir / "es_settings.cfg") == kRecord);
	}
	SUBCASE("damaged, and the file cannot be written back: in use, owed")
	{
		ScratchDir dir;
		put(dir / "es_settings.cfg", "<?xml version=\"1.0\"?>\n<config><string name=");
		put(dir / "es_settings.cfg.backup", kRecord);
		REQUIRE(::chmod(dir.path.c_str(), 0500) == 0);
		Settings* settings = SettingsTestAccess::fresh(dir.path);
		REQUIRE(::chmod(dir.path.c_str(), 0700) == 0);
		CHECK(Settings::recoveryOutcome() == RecoveryOutcome::RestoredInMemory);
		CHECK(settings->getString("Language") == "fr_FR");
	}
}
