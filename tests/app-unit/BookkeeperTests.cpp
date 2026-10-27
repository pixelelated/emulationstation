// The save state manager's queued deletion and the cloud transfer lock
// (audit #307 PL-068, E2's half): the shipped SaveStateBookkeeper.cpp,
// compiled against doubles for the script runner and the log, with the
// transfer lock at a path of the test's own (CLOUD_SYNC_LOCK_PATH). A
// real lock, held by a real process, as a cloud_backup run holds it.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "ApiSystem.h"
#include "Log.h"
#include "SaveStateBookkeeper.h"
#include "utils/FileSystemUtil.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace
{
	std::mutex gRunLock;
	std::vector<std::string> gRuns;

	bool fileExists(const std::string& p) { struct stat st; return ::stat(p.c_str(), &st) == 0; }

	pid_t holdLock(const std::string& path)
	{
		const pid_t child = fork();
		if (child == 0)
		{
			execl("/bin/sh", "sh", "-c", "exec 9>\"$0\"; flock 9; touch \"$0.held\"; exec sleep 60", path.c_str(), (char*) nullptr);
			_exit(127);
		}
		for (int i = 0; i < 200 && !fileExists(path + ".held"); i++)
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		return child;
	}

	void release(pid_t p, const std::string& path)
	{
		kill(p, SIGKILL);
		waitpid(p, nullptr, 0);
		std::remove((path + ".held").c_str());
	}

	int runsNamed(const std::string& part)
	{
		std::lock_guard<std::mutex> g(gRunLock);
		int n = 0;
		for (auto& r : gRuns)
			if (r.find(part) != std::string::npos)
				n++;
		return n;
	}

	bool waitFor(const std::function<bool()>& done, int ms)
	{
		const auto start = std::chrono::steady_clock::now();
		while (!done())
		{
			if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(ms))
				return false;
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
		return true;
	}

	std::string stateFile(const std::string& name)
	{
		const std::string p = std::string(CLOUD_SYNC_LOCK_PATH) + "." + name + ".state1";
		std::ofstream(p) << "state";
		return p;
	}
}

namespace FakeLog
{
	std::vector<std::string>& lines() { static std::vector<std::string> l; return l; }
	std::mutex& lock() { static std::mutex m; return m; }
}

// How long the script "runs", and what a transfer starting meanwhile would
// have found: the case below asks while the deletion's script is running.
static std::atomic<int> gRunMs{ 0 };
static std::function<void()> gDuringRun;

std::pair<std::string, int> ApiSystem::executeScriptLegacy(const std::string& command, const std::function<void(const std::string)>&)
{
	{
		std::lock_guard<std::mutex> g(gRunLock);
		gRuns.push_back(command);
	}
	if (gDuringRun)
		gDuringRun();
	if (gRunMs > 0)
		std::this_thread::sleep_for(std::chrono::milliseconds(gRunMs));
	return std::make_pair(std::string(), 0);
}

namespace Utils { namespace FileSystem {
	bool exists(const std::string& path, bool) { return path == "/usr/bin/cloud_capture" || fileExists(path); }
	std::string readAllText(const std::string&) { return ""; }
	bool removeFile(const std::string& path) { return std::remove(path.c_str()) == 0; }
}}

TEST_CASE("bookkeeper: a queued deletion waits while a transfer holds the lock")
{
	const std::string lock = CLOUD_SYNC_LOCK_PATH;
	const std::string state = stateFile("a");
	const pid_t holder = holdLock(lock);

	SaveStateBookkeeper::deleteLater(state, "");
	std::this_thread::sleep_for(std::chrono::milliseconds(1500));
	CHECK_MESSAGE(fileExists(state), "the state was deleted under a transfer");
	CHECK(runsNamed("--retire --unlink") == 0);
	CHECK(SaveStateBookkeeper::isPending(state));

	release(holder, lock);
	REQUIRE(waitFor([&state] { return !fileExists(state); }, 5000));
	CHECK(runsNamed("--retire --unlink") == 1);
	CHECK_FALSE(SaveStateBookkeeper::isPending(state));
}

TEST_CASE("bookkeeper: with no transfer, a deletion goes at once")
{
	const std::string state = stateFile("b");
	const auto before = runsNamed("--retire --unlink");
	SaveStateBookkeeper::deleteLater(state, "");
	REQUIRE(waitFor([&state] { return !fileExists(state); }, 2000));
	CHECK(runsNamed("--retire --unlink") == before + 1);
}

// A lock the bookkeeper cannot ask is not a free one (engineering-practices.md,
// guards fail closed): a deletion waits on a lock file it cannot open, as it
// waits on a held one, and goes once the file can be asked again.
//
// Unopenable for anyone, root included -- a device runs as root, and root
// reads a mode-0 file, so the case used to fail outright there (audit of
// the fixes, E2 claude G-E2-09): the lock path is a link to itself, which
// open() answers with ELOOP whoever asks.
TEST_CASE("bookkeeper: a lock file that cannot be opened is taken as held")
{
	const std::string lock = CLOUD_SYNC_LOCK_PATH;
	std::remove(lock.c_str());
	REQUIRE(::symlink(lock.c_str(), lock.c_str()) == 0);
	const int fd = ::open(lock.c_str(), O_RDONLY);
	REQUIRE_MESSAGE(fd < 0, "the looped link opened; the case cannot happen");
	const std::string state = stateFile("d");

	SaveStateBookkeeper::deleteLater(state, "");
	std::this_thread::sleep_for(std::chrono::milliseconds(1500));
	CHECK_MESSAGE(fileExists(state), "the state was deleted past a lock nobody could ask");

	REQUIRE(std::remove(lock.c_str()) == 0);
	{ std::ofstream touch(lock); }
	REQUIRE(waitFor([&state] { return !fileExists(state); }, 5000));
}

// gpt's coverage note on PL-068 (audit of the fixes, E2): the bookkeeper
// asked whether the lock was held and then deleted, so a transfer that
// started between the answer and the deletion ran beside it. The lock is
// now held for the deletion: a transfer that starts meanwhile finds it
// taken -- the scripts give a busy lock a second, longer than a deletion.
TEST_CASE("bookkeeper: a deletion holds the transfer lock while it runs")
{
	const std::string lock = CLOUD_SYNC_LOCK_PATH;
	const std::string state = stateFile("e");
	std::atomic<int> freeDuring{ -1 };
	std::atomic<int> ownDuring{ -1 };
	gDuringRun = [&lock, &freeDuring, &ownDuring]
	{
		// A transfer starting now: flock -n on the lock, from another process.
		const int rc = std::system(("flock -n '" + lock + "' true").c_str());
		freeDuring = WIFEXITED(rc) && WEXITSTATUS(rc) == 0 ? 1 : 0;
		ownDuring = SaveStateBookkeeper::holdsTransferLock() ? 1 : 0;
	};
	SaveStateBookkeeper::deleteLater(state, "");
	REQUIRE(waitFor([&state] { return !fileExists(state); }, 5000));
	gDuringRun = nullptr;
	CHECK_MESSAGE(freeDuring.load() == 0, "a transfer could have taken the lock while the deletion ran");
	CHECK(ownDuring.load() == 1);                          // the manager can tell it is ours
	CHECK_FALSE(SaveStateBookkeeper::holdsTransferLock());
	// And it is let go after.
	const int after = std::system(("flock -n '" + lock + "' true").c_str());
	CHECK((WIFEXITED(after) && WEXITSTATUS(after) == 0));
}

// COPY TO FREE SLOT on the worker (#1242): the copy waits for a transfer as a
// deletion does, is checked by its size, leaves nothing at the slot when it
// fails, is recorded only once made, and its outcome is read once.
namespace
{
	std::string bytes(size_t n)
	{
		std::string b(n, '\0');
		for (size_t i = 0; i < n; i++)
			b[i] = (char) (i * 131 % 251);   // NULs and high bytes: a state is not text
		return b;
	}

	std::string readFile(const std::string& p)
	{
		std::ifstream f(p, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	}

	int outcomeWithin(unsigned ticket, int ms)
	{
		int o = -1;
		waitFor([&] { o = SaveStateBookkeeper::copyOutcome(ticket); return o != -1; }, ms);
		return o;
	}
}

TEST_CASE("bookkeeper: a copy waits for a transfer, then is made whole and posted once")
{
	const std::string lock = CLOUD_SYNC_LOCK_PATH;
	const std::string src = lock + ".copy-a.state1", shot = src + ".png";
	const std::string dst = lock + ".copy-a.state2", dstShot = dst + ".png";
	const std::string data = bytes(256 * 1024);
	std::ofstream(src, std::ios::binary) << data;
	std::ofstream(shot, std::ios::binary) << bytes(3000);
	const int adoptsBefore = runsNamed("--adopt");
	const pid_t holder = holdLock(lock);
	const unsigned ticket = SaveStateBookkeeper::copyLater(src, shot, dst, dstShot, "nes", "/r/Bobl.nes");
	REQUIRE(ticket != 0);
	std::this_thread::sleep_for(std::chrono::milliseconds(800));
	CHECK(SaveStateBookkeeper::copyOutcome(ticket) == -1);           // still waiting
	CHECK_MESSAGE(!fileExists(dst), "the copy was written while a transfer held the lock");
	release(holder, lock);
	CHECK(outcomeWithin(ticket, 5000) == 1);
	CHECK(SaveStateBookkeeper::copyOutcome(ticket) == -1);           // given once
	CHECK(readFile(dst) == data);
	CHECK(readFile(dstShot) == bytes(3000));
	CHECK(waitFor([&] { return runsNamed("--adopt") == adoptsBefore + 1; }, 5000));
	CHECK_FALSE(SaveStateBookkeeper::holdsTransferLock());
	for (auto& f : { src, shot, dst, dstShot })
		std::remove(f.c_str());
}

TEST_CASE("bookkeeper: a copy that cannot be made leaves the slot empty and is not recorded")
{
	const std::string lock = CLOUD_SYNC_LOCK_PATH;
	const std::string src = lock + ".copy-b.state1";
	const std::string ro = lock + ".copy-b.ro";
	std::ofstream(src, std::ios::binary) << bytes(4096);
	::mkdir(ro.c_str(), 0755);
	::chmod(ro.c_str(), 0555);
	const std::string dst = ro + "/b.state2";
	const int adoptsBefore = runsNamed("--adopt");
	const unsigned ticket = SaveStateBookkeeper::copyLater(src, "", dst, "", "nes", "/r/Bobl.nes");
	CHECK(outcomeWithin(ticket, 5000) == 0);
	CHECK_FALSE(fileExists(dst));
	// A missing source is not made either.
	const unsigned gone = SaveStateBookkeeper::copyLater(lock + ".copy-b.absent", "", lock + ".copy-b.state3", "", "nes", "/r/Bobl.nes");
	CHECK(outcomeWithin(gone, 5000) == 0);
	CHECK_FALSE(fileExists(lock + ".copy-b.state3"));
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	CHECK(runsNamed("--adopt") == adoptsBefore);
	::chmod(ro.c_str(), 0755);
	::rmdir(ro.c_str());
	std::remove(src.c_str());
}

TEST_CASE("bookkeeper: a copy never writes over a slot that is no longer free")
{
	const std::string lock = CLOUD_SYNC_LOCK_PATH;
	const std::string src = lock + ".copy-c.state1", dst = lock + ".copy-c.state2";
	std::ofstream(src, std::ios::binary) << bytes(4096);
	std::ofstream(dst, std::ios::binary) << "taken";
	const unsigned ticket = SaveStateBookkeeper::copyLater(src, "", dst, "", "nes", "/r/Bobl.nes");
	CHECK(outcomeWithin(ticket, 5000) == 0);
	CHECK(readFile(dst) == "taken");
	std::remove(src.c_str());
	std::remove(dst.c_str());
}

TEST_CASE("bookkeeper: exit does not wait out a transfer, and deletes nothing under it")
{
	const std::string lock = CLOUD_SYNC_LOCK_PATH;
	const std::string state = stateFile("c");
	const pid_t holder = holdLock(lock);
	SaveStateBookkeeper::deleteLater(state, "");
	std::this_thread::sleep_for(std::chrono::milliseconds(300));
	const auto start = std::chrono::steady_clock::now();
	SaveStateBookkeeper::shutdown();
	const auto took = std::chrono::steady_clock::now() - start;
	CHECK(took < std::chrono::seconds(10));
	CHECK_MESSAGE(fileExists(state), "the state was deleted under a transfer at exit");
	release(holder, lock);
	std::remove(state.c_str());
}
