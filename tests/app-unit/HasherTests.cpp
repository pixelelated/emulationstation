// The shipped es-app/src/ThreadedHasher.cpp against the doubles under
// hasher-fakes/ (#1170): the hasher's lifetime -- its workers, its
// completion, the publication of the running instance and stop() -- run
// under AddressSanitizer, as on a device. The cases are the schedules a
// short queue makes: every worker ending before start() has finished with
// the hasher, a stop while a run is in flight and after it, and two runs in
// a row.
#include "doctest/doctest.h"

#include "ThreadedHasher.h"
#include "FileData.h"
#include "Gamelist.h"
#include "OfflineAchievements.h"
#include "ProxyCards.h"
#include "RetroAchievements.h"
#include "Settings.h"
#include "SystemData.h"
#include "Window.h"
#include "guis/GuiMsgBox.h"
#include "components/AsyncNotificationComponent.h"
#include "Log.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>

// ---- the doubles' bodies ----------------------------------------------------

std::vector<SystemData*> SystemData::sSystemVector;
std::string FileData::getSystemName() const { return mSystem != nullptr ? mSystem->getName() : std::string(); }

namespace
{
	std::map<std::string, std::string> gLibrary;
	std::map<std::string, std::string> gSettings;
	std::atomic<int> gSaveFileMs{ 0 };
	std::atomic<int> gRecoverySaves{ 0 };
	std::atomic<int> gTopUps{ 0 };
	std::atomic<int> gToasts{ 0 };

	// A gate checkCheevosHash waits at, so a run can be held in flight.
	std::mutex gGateLock;
	std::condition_variable gGateCv;
	bool gGateOpen = true;
	std::atomic<int> gInHash{ 0 };

	void closeGate() { std::lock_guard<std::mutex> l(gGateLock); gGateOpen = false; }
	void openGate() { { std::lock_guard<std::mutex> l(gGateLock); gGateOpen = true; } gGateCv.notify_all(); }

	bool waitUntil(const std::function<bool()>& done, int ms)
	{
		const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
		while (!done())
		{
			if (std::chrono::steady_clock::now() > until)
				return false;
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		return true;
	}
}

void FileData::checkCrc32(bool) {}
void FileData::checkCheevosHash(bool)
{
	gInHash++;
	std::unique_lock<std::mutex> l(gGateLock);
	gGateCv.wait(l, [] { return gGateOpen; });
}

void saveToGamelistRecovery(FileData*) { gRecoverySaves++; }
std::map<std::string, std::string> RetroAchievements::getCheevosHashes() { return gLibrary; }
void OfflineAchievements::topUpAfterIndex(Window*) { gTopUps++; }
void ProxyCards::indexRanOffline(Window*, int) {}

Settings* Settings::getInstance() { static Settings s; return &s; }
std::string Settings::getString(const std::string& name) { return gSettings[name]; }
bool Settings::setString(const std::string& name, const std::string& value) { gSettings[name] = value; return true; }
bool Settings::saveFile()
{
	std::this_thread::sleep_for(std::chrono::milliseconds(gSaveFileMs.load()));
	return true;
}

AsyncNotificationComponent* Window::createAsyncNotificationComponent(bool) { return new AsyncNotificationComponent(); }
void Window::displayNotificationMessage(const std::string&, int) { gToasts++; }
void Window::pushGui(GuiComponent* gui) { delete gui; }

void AsyncNotificationComponent::updateTitle(const std::string) {}
void AsyncNotificationComponent::updateText(const std::string, const std::string) {}
void AsyncNotificationComponent::updateText(const std::string, const std::vector<std::string>&) {}
void AsyncNotificationComponent::updateText(const std::vector<std::string>&, const std::vector<std::string>&) {}
void AsyncNotificationComponent::updatePercent(int) {}
void AsyncNotificationComponent::close() { delete this; }

namespace FakeLog
{
	std::vector<std::string>& lines() { static std::vector<std::string> l; return l; }
	std::mutex& lock() { static std::mutex m; return m; }
}

// ---- the cases ----------------------------------------------------------------

namespace
{
	// One system holding `games` games, each with the given hash and id.
	struct Library
	{
		SystemData system;
		std::vector<FileData*> files;
		Library(int games, const std::string& hash, const std::string& id)
		{
			for (int i = 0; i < games; i++)
			{
				FileData* f = new FileData(&system, "game" + std::to_string(i) + ".gb");
				if (!hash.empty())
					f->setMetadata(MetaDataId::CheevosHash, hash);
				if (!id.empty())
					f->setMetadata(MetaDataId::CheevosId, id);
				files.push_back(f);
			}
			system.mRoot.mFiles = files;
			SystemData::sSystemVector = { &system };
		}
		~Library()
		{
			SystemData::sSystemVector.clear();
			for (auto f : files)
				delete f;
		}
	};

	void reset()
	{
		gLibrary.clear();
		gSettings.clear();
		gSaveFileMs = 0;
		gRecoverySaves = 0;
		gTopUps = 0;
		gToasts = 0;
		gInHash = 0;
		openGate();
	}
}

TEST_CASE("hasher: a short queue whose workers end before start() is done with the hasher")
{
	// The silent lookups-alone run at startup (#1170): one game with a hash
	// the library knows and no id. start() writes the day's stamp through
	// Settings::saveFile, which waits here long enough for every worker to
	// finish -- the schedule a device makes when the stamp's write is slow.
	reset();
	Library lib(1, "ABCDEF", "");
	gLibrary["ABCDEF"] = "42";
	gSaveFileMs = 300;
	Window window;

	ThreadedHasher::start(&window, ThreadedHasher::HASH_CHEEVOS_MD5, false, true);
	CHECK(waitUntil([] { return !ThreadedHasher::isRunning(); }, 5000));

	CHECK(lib.files[0]->getMetadata(MetaDataId::CheevosId) == "42");
	CHECK(gRecoverySaves.load() == 1);
	CHECK(gToasts.load() == 1);
	CHECK(gSettings["CheevosLookupOnlyLast"] != "");
}

TEST_CASE("hasher: stop() during a run and after it touches no freed hasher")
{
	reset();
	Library lib(3, "", "");
	gLibrary["SOMETHING"] = "1";
	Window window;

	closeGate();
	ThreadedHasher::start(&window, ThreadedHasher::HASH_CHEEVOS_MD5, false, true);
	REQUIRE(waitUntil([] { return gInHash.load() > 0; }, 5000));
	CHECK(ThreadedHasher::isRunning());

	ThreadedHasher::stop();
	openGate();
	CHECK(waitUntil([] { return !ThreadedHasher::isRunning(); }, 5000));

	// After the run: nothing to stop, and nothing freed is touched.
	ThreadedHasher::stop();
	CHECK_FALSE(ThreadedHasher::isRunning());
	// A run the player stopped does not follow the index with a top-up.
	CHECK(gTopUps.load() == 0);
}

TEST_CASE("hasher: two runs in a row leave no worker or thread object behind")
{
	// LeakSanitizer, at the program's exit, reads what each run left.
	for (int run = 0; run < 2; run++)
	{
		reset();
		Library lib(2, "", "");
		gLibrary["SOMETHING"] = "1";
		Window window;
		ThreadedHasher::start(&window, ThreadedHasher::HASH_CHEEVOS_MD5, false, true);
		CHECK(waitUntil([] { return !ThreadedHasher::isRunning(); }, 5000));
	}
}
