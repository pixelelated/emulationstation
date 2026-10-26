// The two long-job runners the transfer and scan pages sit in front of,
// compiled from their shipped sources against the doubles under fakes/:
// real processes, real process groups, real signals.
#include "doctest/doctest.h"
#include "AppWindow.h"
#include "CloudTransferJob.h"
#include "OfflineScanJob.h"
#include "Log.h"
#include "ThreadedCloudSync.h"
#include "Window.h"
#include "utils/FileSystemUtil.h"

#include <atomic>
#include <sys/stat.h>
#include <unistd.h>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{
	bool waitFor(const std::function<bool()>& done, int seconds)
	{
		const auto start = std::chrono::steady_clock::now();
		while (!done())
		{
			if (std::chrono::steady_clock::now() - start > std::chrono::seconds(seconds))
				return false;
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
		return true;
	}

	bool logged(const std::string& needle)
	{
		std::lock_guard<std::mutex> g(FakeLog::lock());
		for (auto& l : FakeLog::lines())
			if (l.find(needle) != std::string::npos)
				return true;
		return false;
	}

	void clearLog()
	{
		std::lock_guard<std::mutex> g(FakeLog::lock());
		FakeLog::lines().clear();
	}

	std::atomic<int> sPosts{ 0 };
}

// ---- Double definitions ------------------------------------------------------

namespace FakeLog
{
	std::vector<std::string>& lines() { static std::vector<std::string> l; return l; }
	std::mutex& lock() { static std::mutex m; return m; }
}

bool ThreadedCloudSync::writeStamp(const std::string&, int, const std::string&, const std::string&) { return true; }
std::string ThreadedCloudSync::whyForCode(int) { return "SOMETHING WENT WRONG"; }
// Which restamp a stopped run asks for, and when its snapshot was taken:
// readStamps' version field says whether the command had started (its
// marker file) at the moment of the snapshot.
static std::string sMarker;
static std::atomic<int> sClockRestamps{ 0 };
static std::atomic<int> sSnapshotRestamps{ 0 };
static std::string sSnapshotVersion;
void ThreadedCloudSync::restampStoppedParts(const std::string&, time_t, const std::string&) { sClockRestamps++; }
void ThreadedCloudSync::restampStoppedParts(const std::string&, const std::vector<CloudText::StampText>& before, time_t, const std::string&)
{
	sSnapshotRestamps++;
	sSnapshotVersion = before.empty() ? "" : before[0].version;
}
// The stamp's text, for a case that needs the script's own line in it (the
// T18 audit's offline-part-way case, #1152); empty for every other case.
static std::string sStampText;
std::vector<CloudText::StampText> ThreadedCloudSync::readStamps(const std::string&)
{
	struct stat st;
	const bool started = !sMarker.empty() && ::stat(sMarker.c_str(), &st) == 0;
	return { { "last-backup", started ? sStampText : "", started ? "after-the-command-started" : "before-the-command" } };
}

void Window::postToUiThread(const std::function<void()>& func, void*) { sPosts++; func(); }

std::string Utils::FileSystem::getFileName(const std::string& path)
{
	const size_t slash = path.find_last_of('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

// ---- Cases -------------------------------------------------------------------

// #308 5-cloud-sync-and-saves gpt F-CS-24 and claude F-CS-26: a stop that
// arrives before the run's ">>> pid" line used to set the flag and signal
// nothing, and nothing signalled it later -- the command ran to its end and
// the page then called it stopped. The stop is kept and sent when the pid
// arrives.
static void stopBeforeThePid(const std::string& who, const std::function<bool()>& stop)
{
	// The case is the seam: the stop finds no pid yet. The race is the
	// test's to win, and on a loaded runner it can lose -- so a run where
	// the pid came first is stopped, let go and tried again, up to five
	// times (audit of the fixes, E2 claude G-E2-09); five losses fail,
	// since a run that never reproduced the seam proves nothing.
	for (int attempt = 1; attempt <= 5; attempt++)
	{
		clearLog();
		const auto started = std::chrono::steady_clock::now();
		auto job = CloudTransferJob::start("sleep 4", "TEST", 1, 0);
		REQUIRE(job != nullptr);
		REQUIRE(stop());
		const bool seam = logged("group 0");
		REQUIRE(waitFor([&] { return job->finished(); }, 10));
		const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
		CloudTransferJob::dismiss(job);
		if (!seam)
		{
			MESSAGE(who << ": attempt " << attempt << " -- the pid came before the stop; trying again");
			continue;
		}
		INFO(who << ": the run took " << ms << " ms (attempt " << attempt << ")");
		CHECK(ms < 2000);
		return;
	}
	FAIL(who << ": five runs, and the stop never came before the pid; the case did not happen");
}

TEST_CASE("transfer job: a cancel before the run says its pid reaches the run when it does")
{
	stopBeforeThePid("stopByPlayer", [] { return CloudTransferJob::stopByPlayer(); });
}

TEST_CASE("transfer job: a stop for a game before the run says its pid reaches the run when it does")
{
	stopBeforeThePid("stopForLaunch", [] { return CloudTransferJob::stopForLaunch(false); });
}

// The other half of gpt F-CS-24: a stop that finds the command's work done
// -- here a command that ignores SIGTERM and ends 0, the same as one whose
// last line was already written -- is not a stopped run. The command's own
// 0 says it completed, and the page words it COMPLETED.
TEST_CASE("transfer job: a run that completed is not called stopped because a stop came")
{
	clearLog();
	auto job = CloudTransferJob::start("trap '' TERM; sleep 1", "TEST", 1, 0);
	REQUIRE(job != nullptr);
	// Long enough for the pid line, so the stop is sent and not kept.
	std::this_thread::sleep_for(std::chrono::milliseconds(300));
	REQUIRE(CloudTransferJob::stopByPlayer());
	REQUIRE_FALSE(logged("group 0"));
	REQUIRE(waitFor([&] { return job->finished(); }, 10));
	CHECK_FALSE(job->stoppedByPlayer());
	CloudTransferJob::dismiss(job);
}

// Audit of the fixes, E2 gpt G-E2-05: a stop that looked, found the run
// not finished, and was descheduled while the run finished on its own
// marked a completed run stopped -- after the run's end had cleared the
// flags, so the page said SKIPPED - YOU CANCELLED IT over a run that
// completed. The pause is the seam's, made wide on purpose.
TEST_CASE("transfer job: a stop that looked before the run ended does not mark a completed run stopped")
{
	clearLog();
	auto job = CloudTransferJob::start("sleep 0.3", "TEST", 1, 0);
	REQUIRE(job != nullptr);
	std::this_thread::sleep_for(std::chrono::milliseconds(100));   // the pid line is in
	CloudTransferJob::testPauseInStop = [] { std::this_thread::sleep_for(std::chrono::milliseconds(1000)); };
	CloudTransferJob::stopByPlayer();
	CloudTransferJob::testPauseInStop = nullptr;
	REQUIRE(waitFor([&] { return job->finished(); }, 10));
	CHECK_FALSE(job->stoppedByPlayer());
	CloudTransferJob::dismiss(job);
}

// The T18 seats' R-11 and R-41 (#1150): the run's own end -- pclose has
// reaped the command -- comes before the run takes its lock, and a stop in
// that window used to mark a run that failed on its own as stopped, since
// the end cleared a stop only over 0 or 9. The page then said SKIPPED -
// YOU CANCELLED IT over a failure. The seam holds the window open.
TEST_CASE("transfer job: a stop after the run failed on its own keeps the failure (#1150)")
{
	clearLog();
	CloudTransferJob::testPauseAfterExit = [] { CloudTransferJob::stopByPlayer(); };
	auto job = CloudTransferJob::start("exit 5", "TEST", 1, 0);
	REQUIRE(job != nullptr);
	REQUIRE(waitFor([&] { return job->finished(); }, 10));
	CloudTransferJob::testPauseAfterExit = nullptr;
	CHECK_FALSE(job->stoppedByPlayer());
	CloudTransferJob::dismiss(job);
}

// The T18 seats' A-37 and R-1 (#1151): a part that says why before its
// first unit had the why filed under the previous part's unit, since a tier
// line cleared the whys but not the unit's label, and line 4 named an item
// that had finished.
TEST_CASE("transfer job: a part's early why is its own, not the previous part's unit (#1151)")
{
	clearLog();
	auto job = CloudTransferJob::start(
		"echo '>>> unit nes|1|1'; echo '>>> tier ROMS AND BIOS|0';"
		" echo '>>> why YOUR CLOUD STOPPED ANSWERING'; echo '>>> tier SAVES|5'; exit 5", "TEST", 2, 0);
	REQUIRE(job != nullptr);
	REQUIRE(waitFor([&] { return job->finished(); }, 10));
	const auto failed = job->failedItems();
	CloudTransferJob::dismiss(job);
	REQUIRE(failed.size() == 1);
	CHECK(failed[0].label == "SAVES");
	CHECK(failed[0].why == "YOUR CLOUD STOPPED ANSWERING");
}

// The T18 seats' R-42 (#1152): the content scripts exit 69 when the network
// goes after files have moved, and say so in their stamp alone ("69 gaps
// YOU WENT OFFLINE PART-WAY THROUGH"); the page read it as COULDN'T FINISH
// with an empty line 4, where the card reads the stamp's why.
TEST_CASE("transfer job: a run the network cut part-way says so on line 4 (#1152)")
{
	clearLog();
	sMarker = "/tmp/t18-jobs-offline-" + std::to_string((long) ::getpid());
	std::remove(sMarker.c_str());
	sStampText = "1789000000 69 gaps YOU WENT OFFLINE PART-WAY THROUGH";
	auto job = CloudTransferJob::start("echo '>>> unit gb|1|2'; touch '" + sMarker + "'; exit 69", "TEST", 1, 0);
	REQUIRE(job != nullptr);
	REQUIRE(waitFor([&] { return job->finished(); }, 10));
	const auto failed = job->failedItems();
	CloudTransferJob::dismiss(job);
	std::remove(sMarker.c_str());
	sMarker.clear();
	sStampText.clear();
	REQUIRE(failed.size() == 1);
	CHECK(failed[0].label == "GB");
	CHECK(failed[0].why == "YOU WENT OFFLINE PART-WAY THROUGH");
}

// The same on the page's composed run, where each part ends on its tier
// line (GuiMenu's transfer command): the one part that ended 69 without a
// why is the part the stamp speaks for. Two such parts and the stamp cannot
// say which, so both keep the code's phrase (#1152).
TEST_CASE("transfer job: a composed run's part the network cut part-way says so on line 4 (#1152)")
{
	for (const bool twoParts : { false, true })
	{
		clearLog();
		sMarker = "/tmp/t18-jobs-offline-tier-" + std::to_string((long) ::getpid());
		std::remove(sMarker.c_str());
		sStampText = "1789000000 69 gaps YOU WENT OFFLINE PART-WAY THROUGH";
		std::string cmd = "echo '>>> unit gb|1|1'; touch '" + sMarker + "'; echo '>>> tier ROMS AND BIOS|69';";
		if (twoParts)
			cmd += " echo '>>> tier GAME CONTENT|69';";
		auto job = CloudTransferJob::start(cmd + " exit 69", "TEST", twoParts ? 2 : 1, 0);
		REQUIRE(job != nullptr);
		REQUIRE(waitFor([&] { return job->finished(); }, 10));
		const auto failed = job->failedItems();
		CloudTransferJob::dismiss(job);
		std::remove(sMarker.c_str());
		sMarker.clear();
		sStampText.clear();
		INFO("two parts: " << twoParts);
		REQUIRE(failed.size() == (twoParts ? 2u : 1u));
		CHECK(failed[0].label == "ROMS AND BIOS");
		CHECK(failed[0].why == std::string(twoParts ? "SOMETHING WENT WRONG" : "YOU WENT OFFLINE PART-WAY THROUGH"));
	}
}

// Orchestrator finding G-E2-O2 (E1's G-E1-04, on the page): a stop
// restamped the stamps its run wrote by the clock -- a stop stamp an
// earlier run left within the second read as this run's. The card takes a
// snapshot when it starts and restamps only the files written since
// (ThreadedCloudSync::readStamps, 54d5699b2); the page does the same, with
// its snapshot taken before its command runs.
TEST_CASE("transfer job: a stop restamps against the stamps as they were before the command")
{
	clearLog();
	sMarker = "/tmp/e2-jobs-marker-" + std::to_string((long) ::getpid());
	std::remove(sMarker.c_str());
	sClockRestamps = 0; sSnapshotRestamps = 0; sSnapshotVersion.clear();
	auto job = CloudTransferJob::start("touch '" + sMarker + "'; sleep 3", "TEST", 1, 0);
	REQUIRE(job != nullptr);
	std::this_thread::sleep_for(std::chrono::milliseconds(300));
	REQUIRE(CloudTransferJob::stopByPlayer());
	REQUIRE(waitFor([&] { return job->finished(); }, 10));
	CloudTransferJob::dismiss(job);
	std::remove(sMarker.c_str());
	CHECK(sClockRestamps.load() == 0);
	CHECK(sSnapshotRestamps.load() == 1);
	CHECK(sSnapshotVersion == "before-the-command");
	sMarker.clear();
}

// Last in this file, and so last in the run: main() letting the window go
// cannot be undone in a process, and doctest runs a file's cases in order.
//
// The scan's worker posts its refresh to the window when the run's state
// changes and once when it ends (#308 8-es-menus-and-core claude F-ES-26's
// rule, carried to the long jobs): the thread is detached and a scan runs
// for minutes, so it can end after main() has torn the window down. Its
// post goes through AppWindow, which drops it once the window is closing.
TEST_CASE("scan job: nothing is posted to a window main() has let go")
{
	Window window;
	auto runOnce = [&window]
	{
		auto job = OfflineScanJob::start(&window, "sleep 0.3");
		REQUIRE(job != nullptr);
		job->setOnChanged([] {});
		REQUIRE(waitFor([&] { return job->state().finished; }, 10));
		// The end's post is made after finished is set.
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	};

	// The control: with the window open, the run's end is posted.
	const int before = sPosts;
	runOnce();
	REQUIRE(sPosts.load() > before);

	AppWindow::closing();   // main(), before the window goes
	const int atClose = sPosts;
	runOnce();
	CHECK(sPosts.load() == atClose);
}
