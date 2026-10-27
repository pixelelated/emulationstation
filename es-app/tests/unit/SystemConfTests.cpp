// SystemConf itself -- the load, the reload, the save and the record --
// against real files in a scratch directory (the audit of the fix round).
//
// es-file-tests checks AtomicFileUtil's pieces one at a time; the defects
// the fix round's audit found were in how SystemConf strings them together:
// what a reload does when nothing could be read, which base a change is
// compared with, the mode a recovery's record gets, whether the recovery's
// write-back waits for the settings lock. So this binary compiles the
// shipped SystemConf.cpp and AtomicFileUtil.cpp, with the few things they
// call from the rest of the interface -- the log, Settings, Paths, SDL's
// clock -- defined here as doubles. A fresh SystemConf per case, through
// SystemConfTestAccess (a friend SystemConf.h names), pointed at a scratch
// system.cfg and a scratch settings lock. POSIX only, like the code.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "Log.h"
#include "Paths.h"
#include "Settings.h"
#include "SystemConf.h"
#include "utils/AtomicFileUtil.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <ctime>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// ------------------------------------------------------------ the doubles

LogLevel Log::mReportingLevel = LogDebug;
bool Log::mEnabled = true;

namespace
{
	std::mutex gLogMutex;
	std::string gLog;   // every line SystemConf logged, for a case to read
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

// SystemConf goes to Settings only when it has no system.cfg path, which no
// case here gives it.
Settings* Settings::getInstance() { return nullptr; }
bool Settings::saveFile() { return false; }
std::string Settings::getString(const std::string&) { return ""; }
bool Settings::setString(const std::string&, const std::string&) { return false; }
bool Settings::getBool(const std::string&) { return false; }
bool Settings::setBool(const std::string&, bool) { return false; }

struct SystemConfTestAccess
{
	// A new SystemConf, reading `path` as the constructor does, with the
	// settings lock at `lockPath` and a budget of `budgetMs`.
	static SystemConf* fresh(const std::string& path, const std::string& lockPath, int budgetMs, const std::string& seedPath = "")
	{
		delete SystemConf::sInstance;
		SystemConf::sSeedPath = seedPath;
		SystemConf::sInstance = nullptr;
		SystemConf::sRecovered = false;
		SystemConf::sLockPath = lockPath;
		SystemConf::sLockBudgetMs = budgetMs;
		Paths::getSystemConfFilePath() = path;
		{
			std::lock_guard<std::mutex> lock(gLogMutex);
			gLog.clear();
		}
		return SystemConf::getInstance();
	}

	// Resume at the actual publication boundary with a captured earlier read.
	static void record(SystemConf* conf, const std::string& snapshot)
	{
		conf->recordLastGood(snapshot);
	}

	static bool recovered() { return SystemConf::sRecovered; }
};

namespace
{
	struct ScratchDir
	{
		std::string path;
		ScratchDir()
		{
			const char* base = getenv("TMPDIR");
			std::string tmpl = std::string(base != nullptr && *base != '\0' ? base : "/tmp") + "/es-conf-tests.XXXXXX";
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

	int modeOf(const std::string& path)
	{
		struct stat st;
		return ::stat(path.c_str(), &st) == 0 ? (int) (st.st_mode & 07777) : -1;
	}

	std::string logged()
	{
		std::lock_guard<std::mutex> lock(gLogMutex);
		return gLog;
	}
}

using Utils::AtomicFile::PidLock;

// ------------------------------------------------------------ the harness itself

TEST_CASE("the harness: a load, a change and a save, as the interface makes them")
{
	ScratchDir dir;
	const std::string path = dir / "system.cfg";
	put(path, "system.hostname=A\naudio.volume=70\n");

	SystemConf* conf = SystemConfTestAccess::fresh(path, dir / ".system.cfg.lock", 500);
	CHECK(conf->get("audio.volume") == "70");
	CHECK(conf->set("audio.volume", "40"));
	CHECK(conf->saveSystemConf());
	CHECK(get(path) == "system.hostname=A\naudio.volume=40\n");
	// A save onto a whole file is the record.
	CHECK(get(path + ".backup") == "system.hostname=A\naudio.volume=40\n");
	CHECK_FALSE(SystemConfTestAccess::recovered());
}

// ------------------------------------------------------------ G2-E-core-06

TEST_CASE("a reload that reads nothing keeps every change still waiting to be saved (audit of the fix round, gpt G2-E-core-06)")
{
	// The reload cleared the pending changes, read nothing, and then
	// compared them with the empty reading: every change to a key the file
	// held looked like somebody had removed the key since, and was dropped
	// -- from the pending set and from memory -- before the false came back.
	// Nothing read is no evidence about any key.
	ScratchDir dir;
	const std::string path = dir / "system.cfg";
	const std::string lockPath = dir / ".system.cfg.lock";
	put(path, "system.hostname=A\naudio.volume=70\n");
	SystemConf* conf = SystemConfTestAccess::fresh(path, lockPath, 300);
	REQUIRE(conf->get("audio.volume") == "70");

	// The player's change; the save is refused for the lock (PL-024) and kept.
	CHECK(conf->set("audio.volume", "40"));
	{
		PidLock other(lockPath);
		REQUIRE(other.acquire(1000));
		CHECK_FALSE(conf->saveSystemConf());
	}

	// The file and its record gone for the moment: the reload reads nothing.
	REQUIRE(std::rename(path.c_str(), (dir / "aside").c_str()) == 0);
	REQUIRE(std::remove((path + ".backup").c_str()) == 0);
	CHECK_FALSE(conf->loadSystemConf(true));
	CHECK(conf->get("audio.volume") == "40");

	// The file back: the next save makes the change.
	REQUIRE(std::rename((dir / "aside").c_str(), path.c_str()) == 0);
	CHECK(conf->saveSystemConf());
	CHECK(get(path) == "system.hostname=A\naudio.volume=40\n");
}

// ------------------------------------------------------------ G2-E-core-07

TEST_CASE("a change is compared with the file it was made against, not with memory (audit of the fix round, gpt G2-E-core-07, claude G2-E-core-09)")
{
	// set() took a change's base from confMap, which also holds keys the
	// file no longer has -- a key another writer removed since the last
	// load, a key the last save wrote away (a `default` is a removed
	// line). A reload compared that base with the file, found the key
	// missing where the base said it was there, and dropped the change as
	// removed by somebody since, when nobody had touched the file.
	ScratchDir dir;
	const std::string path = dir / "system.cfg";
	const std::string lockPath = dir / ".system.cfg.lock";

	SUBCASE("a key another writer removed before the change was made")
	{
		put(path, "system.hostname=A\ntone=old\nkeep=1\n");
		SystemConf* conf = SystemConfTestAccess::fresh(path, lockPath, 300);
		put(path, "system.hostname=A\nkeep=1\n");   // a script removed tone
		REQUIRE(conf->loadSystemConf(true));

		CHECK(conf->set("tone", "new"));
		{
			PidLock other(lockPath);
			REQUIRE(other.acquire(1000));
			CHECK_FALSE(conf->saveSystemConf());
		}
		REQUIRE(conf->loadSystemConf(true));   // the file as it was when the change was made
		CHECK(conf->get("tone") == "new");
		CHECK(conf->saveSystemConf());
		CHECK(get(path) == "system.hostname=A\nkeep=1\ntone=new\n");
	}
	SUBCASE("a key the last save wrote away")
	{
		put(path, "system.hostname=A\na=1\n");
		SystemConf* conf = SystemConfTestAccess::fresh(path, lockPath, 300);
		// `default` removes the line; an empty value is written as a= since
		// T21 PL-01 (#1185), so the removal is asked for by its word
		// (D-WORKFLOW-211: the fixture completed, no assertion changed).
		CHECK(conf->set("a", "default"));
		REQUIRE(conf->saveSystemConf());
		REQUIRE(get(path) == "system.hostname=A\n");

		CHECK(conf->set("a", "3"));
		{
			PidLock other(lockPath);
			REQUIRE(other.acquire(1000));
			CHECK_FALSE(conf->saveSystemConf());
		}
		REQUIRE(conf->loadSystemConf(true));
		CHECK(conf->get("a") == "3");
		CHECK(conf->saveSystemConf());
		CHECK(get(path) == "system.hostname=A\na=3\n");
	}
	SUBCASE("a key somebody else really did change is still theirs")
	{
		put(path, "system.hostname=A\nwifi.ssid=Home\n");
		SystemConf* conf = SystemConfTestAccess::fresh(path, lockPath, 300);
		CHECK(conf->set("wifi.ssid", "Cafe"));
		{
			PidLock other(lockPath);
			REQUIRE(other.acquire(1000));
			CHECK_FALSE(conf->saveSystemConf());
		}
		put(path, "system.hostname=A\nwifi.ssid=Library\n");   // wifictl join wrote it
		REQUIRE(conf->loadSystemConf(true));
		CHECK(conf->get("wifi.ssid") == "Library");
	}
}

// ------------------------------------------------------------ G2-E-core-05

TEST_CASE("a recovery's record is no less private than the copy it came from (audit of the fix round, gpt G2-E-core-05, claude G2-E-core-04 b)")
{
	// chooseConfig works out the mode every copy on disk shares (G-E1-05),
	// and the recovery wrote the live file with it -- but the record took
	// its mode from the live file alone, or 0644 where there was none.
	ScratchDir dir;
	const mode_t before = umask(022);
	const std::string path = dir / "system.cfg";
	const std::string whole = "system.hostname=A\nwifi.key=secret\n";

	SUBCASE("a private temporary, and a live file that cannot be written back")
	{
		// system.cfg a directory: nothing can be renamed over it, so the
		// write-back fails and the record is all the recovery leaves.
		REQUIRE(mkdir(path.c_str(), 0755) == 0);
		put(path + ".tmp", whole);
		REQUIRE(chmod((path + ".tmp").c_str(), 0600) == 0);
		SystemConf* conf = SystemConfTestAccess::fresh(path, dir / ".system.cfg.lock", 300);
		CHECK(conf->get("system.hostname") == "A");
		CHECK(get(path + ".backup") == whole);
		CHECK(modeOf(path + ".backup") == 0600);
	}
	SUBCASE("a private live file recovered from a record made 0644 by an earlier build")
	{
		put(path, "# nothing usable\n");
		REQUIRE(chmod(path.c_str(), 0600) == 0);
		put(path + ".backup", whole);
		REQUIRE(chmod((path + ".backup").c_str(), 0644) == 0);
		SystemConf* conf = SystemConfTestAccess::fresh(path, dir / ".system.cfg.lock", 300);
		CHECK(conf->get("system.hostname") == "A");
		CHECK(get(path) == whole);
		CHECK(modeOf(path) == 0600);
		CHECK(get(path + ".backup") == whole);
		CHECK(modeOf(path + ".backup") == 0600);
	}
	umask(before);
}

// ------------------------------------------------------------ G3-E-06

TEST_CASE("a reload onto a damaged file keeps the pending change (the audit of the fixes, claude G3-E-06)")
{
	// A change pending; the live file then overwritten with junk, the record
	// unusable too and no whole temporary: the reload parsed the junk, came
	// back true, and the comparison dropped the change as removed by somebody.
	ScratchDir dir;
	const std::string path = dir / "system.cfg";
	put(path, "system.hostname=A\naudio.volume=70\n");
	SystemConf* conf = SystemConfTestAccess::fresh(path, dir / ".system.cfg.lock", 300);
	REQUIRE(conf->get("audio.volume") == "70");
	CHECK(conf->set("audio.volume", "40"));
	put(path + ".backup", "# nothing usable\n");
	put(path, "# nothing usable here\n");
	CHECK_FALSE(conf->loadSystemConf(true));
	CHECK(conf->get("audio.volume") == "40");
	put(path, "system.hostname=A\naudio.volume=70\n");
	CHECK(conf->saveSystemConf());
	CHECK(get(path) == "system.hostname=A\naudio.volume=40\n");
}

// ------------------------------------------------------------ G2-E-core-03

TEST_CASE("a save onto an emptied system.cfg keeps the record's keys (audit of the fix round, gpt G2-E-core-03)")
{
	// The whole record held every key; the live file was emptied after the
	// load; the player changed one setting. The save wrote that one key
	// alone, called the empty base whole, and recorded it: every other key
	// gone from the file and from the record.
	ScratchDir dir;
	const std::string path = dir / "system.cfg";
	const std::string whole = "system.hostname=A\nwifi.ssid=Home\naudio.volume=70\n";
	put(path, whole);
	SystemConf* conf = SystemConfTestAccess::fresh(path, dir / ".system.cfg.lock", 300);
	REQUIRE(get(path + ".backup") == whole);

	put(path, "");
	CHECK(conf->set("audio.volume", "40"));
	CHECK(conf->saveSystemConf());
	const std::string saved = "system.hostname=A\nwifi.ssid=Home\naudio.volume=40\n";
	CHECK(get(path) == saved);
	CHECK(get(path + ".backup") == saved);
}

// ------------------------------------------------------------ G2-E-core-02

TEST_CASE("a recovery is written back under the settings lock, and only while it is still the recovery (audit of the fix round, gpt G2-E-core-02, claude G2-E-core-07)")
{
	// The load chose the record (or the temporary) and wrote it over the
	// live file without the lock every other writer of system.cfg takes --
	// the save, the shell's set_setting. A write the shell landed between
	// the choice and the write-back was lost under it.
	ScratchDir dir;
	const std::string path = dir / "system.cfg";
	const std::string lockPath = dir / ".system.cfg.lock";
	const std::string damaged = "# nothing usable here\n";
	const std::string record = "system.hostname=A\nwifi.ssid=Home\naudio.volume=70\n";
	put(path, damaged);
	put(path + ".backup", record);

	SUBCASE("a holder keeps the lock past the budget: nothing is written, and the next save writes the recovery")
	{
		SystemConf* conf = nullptr;
		{
			PidLock other(lockPath);
			REQUIRE(other.acquire(1000));
			conf = SystemConfTestAccess::fresh(path, lockPath, 300);
			CHECK(get(path) == damaged);   // not written while somebody else held the lock
		}
		CHECK(conf->get("wifi.ssid") == "Home");   // read, all the same
		CHECK(SystemConfTestAccess::recovered());

		CHECK(conf->set("audio.volume", "40"));
		CHECK(conf->saveSystemConf());
		CHECK(get(path) == "system.hostname=A\nwifi.ssid=Home\naudio.volume=40\n");
	}
	SUBCASE("the shell writes a whole file while the load waits: that file is the one read, and it stands")
	{
		const std::string theirs = "system.hostname=A\nwifi.ssid=Library\naudio.volume=70\n";
		PidLock other(lockPath);
		REQUIRE(other.acquire(1000));
		std::thread shell([&]()
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(300));
			put(path + ".sh", theirs);
			std::rename((path + ".sh").c_str(), path.c_str());
			other.release();
		});
		SystemConf* conf = SystemConfTestAccess::fresh(path, lockPath, 3000);
		shell.join();
		CHECK(get(path) == theirs);
		CHECK(conf->get("wifi.ssid") == "Library");
	}
	SUBCASE("a free lock: the recovery is written back, as before")
	{
		SystemConf* conf = SystemConfTestAccess::fresh(path, lockPath, 300);
		CHECK(get(path) == record);
		CHECK(conf->get("wifi.ssid") == "Home");
		CHECK(SystemConfTestAccess::recovered());
	}
}

// ------------------------------------------------------------ PL-018

TEST_CASE("a record cut short is neither loaded nor recorded, and the defaults answer (audit of the fix round PL-018)")
{
	// chooseConfig's half is es-file-tests'; this is what SystemConf does
	// with it. The cut record used to be read -- its hostname answered --
	// and written back as the live file.
	ScratchDir dir;
	const std::string path = dir / "system.cfg";
	const std::string cut = "system.hostname=A\nwifi.ssid=Home\nwifi.key=sec";
	put(path + ".backup", cut);

	SUBCASE("no live file and no temporary")
	{
		SystemConf* conf = SystemConfTestAccess::fresh(path, dir / ".system.cfg.lock", 300);
		CHECK(conf->get("system.hostname") == "BATOCERA");   // the default, not the cut record's A
		CHECK(conf->get("wifi.ssid") == "");
		CHECK_FALSE(SystemConfTestAccess::recovered());
		CHECK(access(path.c_str(), F_OK) != 0);               // nothing written back
		CHECK(get(path + ".backup") == cut);
	}
	SUBCASE("an unusable live file, and a change saved onto it")
	{
		put(path, std::string("\0\0\0", 3));
		SystemConf* conf = SystemConfTestAccess::fresh(path, dir / ".system.cfg.lock", 300);
		CHECK(conf->get("system.hostname") == "BATOCERA");
		CHECK_FALSE(SystemConfTestAccess::recovered());
		CHECK(conf->set("audio.volume", "40"));
		CHECK(conf->saveSystemConf());
		// Merged onto what was there -- no whole copy to merge onto -- and
		// not whole, so the cut record stays as it was.
		CHECK(get(path + ".backup") == cut);
	}
	SUBCASE("a whole temporary beside it: the temporary recovers")
	{
		const std::string whole = "system.hostname=B\nwifi.ssid=Home\n";
		put(path + ".tmp", whole);
		SystemConf* conf = SystemConfTestAccess::fresh(path, dir / ".system.cfg.lock", 300);
		CHECK(conf->get("system.hostname") == "B");
		CHECK(SystemConfTestAccess::recovered());
		CHECK(get(path) == whole);
		CHECK(get(path + ".backup") == whole);
	}
}

TEST_CASE("a script's newer good state published between the read and the record is not overwritten")
{
	ScratchDir dir;
	const std::string path = dir / "system.cfg";
	const std::string lockPath = dir / ".system.cfg.lock";
	const std::string earlier = "system.hostname=A\naudio.volume=70\n";
	const std::string newer = "system.hostname=A\naudio.volume=40\n";
	put(path, earlier);
	SystemConf* conf = SystemConfTestAccess::fresh(path, lockPath, 20);
	// The loader/save retains this snapshot while a script gets its turn.
	const std::string snapshot = get(path);
	{
		PidLock script(lockPath);
		REQUIRE(script.acquire(1000));
		REQUIRE(Utils::AtomicFile::writeText(path, newer, 0600));
		REQUIRE(Utils::AtomicFile::writeText(path + ".backup", newer, 0600));
	}
	SystemConfTestAccess::record(conf, snapshot);
	CHECK(get(path) == newer);
	CHECK(get(path + ".backup") == newer);
	CHECK(modeOf(path + ".backup") == 0600);
}

TEST_CASE("LockBusy recovery reads its temporary without publishing over the holder's record")
{
	ScratchDir dir;
	const std::string path = dir / "system.cfg";
	const std::string lockPath = dir / ".system.cfg.lock";
	const std::string unfinished = "system.hostname=A\naudio.volume=70\n";
	const std::string recorded = "system.hostname=A\naudio.volume=40\n";
	put(path, "system.hostname=A\naudio.vol");
	put(path + ".tmp", unfinished);
	put(path + ".backup", recorded);
	PidLock script(lockPath);
	REQUIRE(script.acquire(1000));
	SystemConf* conf = SystemConfTestAccess::fresh(path, lockPath, 20);
	CHECK(conf->get("audio.volume") == "70");
	CHECK(SystemConfTestAccess::recovered());
	CHECK(get(path) == "system.hostname=A\naudio.vol");
	CHECK(get(path + ".backup") == recorded);
	CHECK(get(path + ".tmp") == unfinished);
}

// ------------------------------------------------------------ T21 S1 (#668)
//
// PL-01 (#1185): a key set to an empty value is written as `key=`, never
// removed -- system.hostname= is the line chksysconfig's verify requires,
// and its removal made the next boot reseed the image's defaults over the
// file and the record. PL-10 (#1198): an empty value read back is present
// and empty, and a reload forgets keys the file no longer has. PL-11
// (#1199): a comment line is left alone, a value with a line end is
// refused, the default comparison is on the key, and `default` is never
// written as a literal.

TEST_CASE("an empty value is saved as key=, in the file and the record (T21 PL-01, #1185)")
{
	ScratchDir dir;
	const std::string path = dir / "system.cfg";
	put(path, "system.hostname=qa\nqa.marker=player\n");
	SystemConf* conf = SystemConfTestAccess::fresh(path, dir / ".system.cfg.lock", 300);
	CHECK(conf->set("system.hostname", ""));
	CHECK(conf->saveSystemConf());
	CHECK(get(path) == "system.hostname=\nqa.marker=player\n");
	CHECK(get(path + ".backup") == "system.hostname=\nqa.marker=player\n");

	// The next start reads the key as present and empty, not as the table's
	// BATOCERA.
	conf = SystemConfTestAccess::fresh(path, dir / ".system.cfg.lock", 300);
	CHECK(conf->get("system.hostname") == "");
	CHECK(conf->get("qa.marker") == "player");
}

TEST_CASE("the hostname set to default or auto keeps its line, empty (T21 PL-01, #1185)")
{
	for (const char* word : { "default", "auto" })
	{
		ScratchDir dir;
		const std::string path = dir / "system.cfg";
		put(path, "system.hostname=qa\nqa.marker=player\n");
		SystemConf* conf = SystemConfTestAccess::fresh(path, dir / ".system.cfg.lock", 300);
		CHECK(conf->set("system.hostname", word));
		CHECK(conf->saveSystemConf());
		CHECK(get(path) == "system.hostname=\nqa.marker=player\n");
	}
}

TEST_CASE("a reload forgets a key the file no longer holds, and reads key= as empty (T21 PL-10, #1198)")
{
	ScratchDir dir;
	const std::string path = dir / "system.cfg";
	put(path, "system.hostname=qa\nwifi.ssid=Home\nwifi.key=secret-one\nqa.gone=1\n");
	SystemConf* conf = SystemConfTestAccess::fresh(path, dir / ".system.cfg.lock", 300);
	REQUIRE(conf->get("wifi.key") == "secret-one");

	// A script joined an open network (wifi.key= written on purpose) and
	// removed another key.
	put(path, "system.hostname=qa\nwifi.ssid=Open\nwifi.key=\n");
	CHECK(conf->loadSystemConf(true));
	CHECK(conf->get("wifi.ssid") == "Open");
	CHECK(conf->get("wifi.key") == "");
	CHECK(conf->get("qa.gone") == "");
}

TEST_CASE("a commented key is left alone and the live key rewritten once (T21 PL-11, #1199)")
{
	ScratchDir dir;
	const std::string path = dir / "system.cfg";
	put(path, "#k=1\nk=2\nz=9\n");
	SystemConf* conf = SystemConfTestAccess::fresh(path, dir / ".system.cfg.lock", 300);
	CHECK(conf->set("k", "3"));
	CHECK(conf->saveSystemConf());
	CHECK(get(path) == "#k=1\nk=3\nz=9\n");
}

TEST_CASE("a new key set to default writes no literal; a key at its table default is removed (T21 PL-11, #1199)")
{
	ScratchDir dir;
	const std::string path = dir / "system.cfg";
	put(path, "system.hostname=qa\nglobal.retroachievements.hardcore=1\n");
	SystemConf* conf = SystemConfTestAccess::fresh(path, dir / ".system.cfg.lock", 300);
	CHECK(conf->set("qa.new", "default"));
	CHECK(conf->set("global.retroachievements.hardcore", "0"));
	CHECK(conf->saveSystemConf());
	CHECK(get(path) == "system.hostname=qa\n");

	// The hostname at the table's own BATOCERA is a name the player gave:
	// kept, never removed (the line is chksysconfig's sentinel).
	CHECK(conf->set("system.hostname", "BATOCERA"));
	CHECK(conf->saveSystemConf());
	CHECK(get(path) == "system.hostname=BATOCERA\n");
}

TEST_CASE("a value with a line end is refused, and nothing is written for it (T21 PL-11, #1199)")
{
	ScratchDir dir;
	const std::string path = dir / "system.cfg";
	put(path, "system.hostname=qa\n");
	SystemConf* conf = SystemConfTestAccess::fresh(path, dir / ".system.cfg.lock", 300);
	CHECK_FALSE(conf->set("qa.k", "x\nroot.password=y"));
	CHECK_FALSE(conf->set("qa.k", "x\r"));
	CHECK_FALSE(conf->saveSystemConf());
	CHECK(get(path) == "system.hostname=qa\n");
}

TEST_CASE("a byte-order mark before the first key does not hide it (T21 PL-11, #1199)")
{
	ScratchDir dir;
	const std::string path = dir / "system.cfg";
	put(path, "\xEF\xBB\xBFk=1\nz=2\n");
	SystemConf* conf = SystemConfTestAccess::fresh(path, dir / ".system.cfg.lock", 300);
	CHECK(conf->get("k") == "1");
	CHECK(conf->set("k", "5"));
	CHECK(conf->saveSystemConf());
	CHECK(get(path) == "k=5\nz=2\n");
}

TEST_CASE("a save does not record a file that lost a key the record holds and this save did not remove (T21 PL-01, #1185)")
{
	ScratchDir dir;
	const std::string path = dir / "system.cfg";
	const std::string whole = "a.key=1\nb.key=2\n";
	put(path, whole);
	SystemConf* conf = SystemConfTestAccess::fresh(path, dir / ".system.cfg.lock", 300);
	REQUIRE(get(path + ".backup") == whole);

	// Something else rewrote the live file without a.key; the player
	// changes c.key.
	put(path, "b.key=2\n");
	CHECK(conf->set("c.key", "3"));
	CHECK(conf->saveSystemConf());
	CHECK(get(path) == "b.key=2\nc.key=3\n");
	CHECK(get(path + ".backup") == whole);
}

// ------------------------------------------------------------ T21 S2 (#668)
//
// PL-09 (#1197): a live file cut part way through a line, the start of its
// whole record, is a recovery whatever its mtime -- it was merged onto and
// saved whole, and the next load recorded it over the record, the keys after
// the cut gone. PL-03 (#1189), the recorder's half: a live file that is the
// image's seed, byte for byte, beside a record with other keys is the seed
// copied over the player's file; the record is chosen and nothing is
// recorded over it.

namespace
{
	void touchAt(const std::string& path, time_t when)
	{
		struct timespec times[2];
		times[0].tv_sec = when; times[0].tv_nsec = 0;
		times[1].tv_sec = when; times[1].tv_nsec = 0;
		REQUIRE(utimensat(AT_FDCWD, path.c_str(), times, 0) == 0);
	}
}

TEST_CASE("a cut live file newer than its record is still a recovery, and the record keeps its keys (T21 PL-09, #1197)")
{
	ScratchDir dir;
	const std::string path = dir / "system.cfg";
	const std::string whole = "system.hostname=qa\nqa.marker=player\nqa.tail=kept\n";
	put(path + ".backup", whole);
	put(path, "system.hostname=qa\nqa.mark");
	const time_t now = time(nullptr);
	touchAt(path + ".backup", now - 100);
	touchAt(path, now + 100);

	SystemConf* conf = SystemConfTestAccess::fresh(path, dir / ".system.cfg.lock", 300);
	CHECK(conf->set("qa.new", "4"));
	CHECK(conf->saveSystemConf());
	CHECK(conf->loadSystemConf());
	CHECK(conf->get("qa.marker") == "player");
	CHECK(conf->get("qa.tail") == "kept");
	CHECK(get(path + ".backup").find("qa.tail=kept\n") != std::string::npos);
	CHECK(get(path).find("qa.tail=kept\n") != std::string::npos);
}

TEST_CASE("a live file equal to the image's seed beside a differing record loads the record and records nothing over it (T21 PL-03, #1189)")
{
	ScratchDir dir;
	const std::string path = dir / "system.cfg";
	const std::string seed = "system.hostname=\nqa.seed=1\n";
	const std::string record = "system.hostname=qa\nqa.marker=player\n";
	put(dir / "seed.cfg", seed);
	put(path, seed);
	put(path + ".backup", record);

	SystemConf* conf = SystemConfTestAccess::fresh(path, dir / ".system.cfg.lock", 300, dir / "seed.cfg");
	CHECK(conf->get("qa.marker") == "player");
	CHECK(get(path + ".backup") == record);
	CHECK(get(path) == record);
}
