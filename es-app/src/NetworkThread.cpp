#include "NetworkThread.h"
#include "AppWindow.h"
#include "ApiSystem.h"
#include "Settings.h"
#include "SystemConf.h"
#include "guis/GuiMsgBox.h"
#include "LocaleES.h"
#include "Log.h"
#include <atomic>
#include <chrono>
#include <SDL.h>

#include "watchers/BatteryLevelWatcher.h"
#include "watchers/NetworkStateWatcher.h"
#include "RetroAchievements.h"
#include "OfflineAchievements.h"
#include "ProxyCards.h"
#include "SystemData.h"
#include "ThreadedHasher.h"

static CheckCheevosTokenComponent* sCheevosToken = nullptr;

// Every post from the watchers' thread goes through AppWindow's closing gate
// (#1276). Once main() has called AppWindow::closing() a post is dropped, not
// queued for a loop that will not run again, and nothing is saved after the
// window closes. Each dropped task is repeated by the next start's own
// schedule -- the pads' levels at the next check, the link-up's cards and the
// index at the next link-up, the retry's reset at the next link-up, the token
// at the next scheduled check -- so a lost token write costs one check, not a
// sign-in. Logged once.
static void droppedAfterClosing()
{
	static std::atomic<bool> logged(false);
	if (!logged.exchange(true))
		LOG(LogInfo) << "NetworkThread : the window is closing, so its posts from here on are dropped";
}

void NetworkThread::checkCheevosTokenSoon()
{
	if (sCheevosToken != nullptr)
		WatchersManager::getInstance()->ResetComponent(sCheevosToken);
}

NetworkThread::NetworkThread(Window* window) : mWindow(window)
{
	WatchersManager* mgr = WatchersManager::getInstance();

	mgr->RegisterComponent(&mCheckCheevosTokenComponent);
	sCheevosToken = &mCheckCheevosTokenComponent;
	mgr->RegisterComponent(new BatteryLevelWatcher());
	mNetworkStateWatcher = new NetworkStateWatcher();
	mgr->RegisterComponent(mNetworkStateWatcher);

	if (ApiSystem::getInstance()->isScriptingSupported(ApiSystem::UPGRADE))
		mgr->RegisterComponent(&mCheckUpdatesComponent);

	if (ApiSystem::getInstance()->isScriptingSupported(ApiSystem::PADSINFO))
		mgr->RegisterComponent(&mCheckPadsBatteryLevelComponent);

	mgr->RegisterNotify(this);

	LOG(LogDebug) << "NetworkThread : Starting";
	InputManager::joystickChanged += this;
}

CheckPadsBatteryLevelComponent::CheckPadsBatteryLevelComponent()
{
#if WIN32		
	mEnabled = false; // Windows uses SDL_JOYBATTERYUPDATED event instead
#else
	mEnabled = ApiSystem::getInstance()->isScriptingSupported(ApiSystem::PADSINFO);
#endif
}

bool CheckPadsBatteryLevelComponent::check()
{
	bool changed = false;

	auto padsInfo = ApiSystem::getInstance()->getPadsInfo();

	if (mPadsInfo.size() != padsInfo.size())
		changed = true;
	else
	{
		for (int i = 0; i < padsInfo.size(); i++)
		{
			if (padsInfo[i].status == "Charging")
				padsInfo[i].battery = -1;

			if (mPadsInfo[i].battery != padsInfo[i].battery || mPadsInfo[i].status != padsInfo[i].status || mPadsInfo[i].id != padsInfo[i].id || mPadsInfo[i].name != padsInfo[i].name || mPadsInfo[i].path != padsInfo[i].path || mPadsInfo[i].device != padsInfo[i].device)
			{
				changed = true;
				break;
			}
		}
	}

	mPadsInfo = padsInfo;
	return changed;
}

bool CheckUpdatesComponent::enabled()
{
	return mEnabled && SystemConf::getInstance()->getBool("updates.enabled") && ApiSystem::getInstance()->isScriptingSupported(ApiSystem::UPGRADE);
}

bool CheckUpdatesComponent::check()
{
	LOG(LogDebug) << "CheckUpdatesComponent : Checking for updates";

	std::vector<std::string> msgtbl;
	if (ApiSystem::getInstance()->canUpdate(msgtbl))
	{
		std::string msg = "";
		for (int i = 0; i < msgtbl.size(); i++)
		{
			if (i != 0) msg += "\n";
			msg += msgtbl[i];
		}

		LOG(LogDebug) << "CheckUpdatesComponent : Update available " << msg.c_str();
		mLastUpdateMessage = msg;
		mEnabled = false;
		return true;
	}
	else
		LOG(LogDebug) << "NetworkThread : No update found";

	return false;
}

bool CheckCheevosTokenComponent::enabled()
{
	return SystemConf::getInstance()->getBool("global.retroachievements");
};

CheckCheevosTokenComponent::~CheckCheevosTokenComponent()
{
	joinWorker();
}

void CheckCheevosTokenComponent::joinWorker()
{
	if (mWorker.joinable())
		mWorker.join();
}

bool CheckCheevosTokenComponent::check()
{
	// The regular schedule unless this check decides otherwise below, so a
	// check that returns early -- the switch off, no account -- never
	// inherits a short retry from the one before it.
	mNextDelayMs = CheevosRetry::ScheduledMs;

	// check() runs under WatchersManager's lock, which the settings page's
	// close waits on through ResetComponent (checkCheevosTokenSoon), so the
	// sign-in is never made here (#1283): a check takes the answer a worker
	// has left, or starts one, and returns at once.
	SignIn signIn;
	if (takeSignIn(signIn))
	{
		// The page may have turned achievements off and cleared the token,
		// or changed the account, while the sign-in was out (#1288). Its
		// answer is applied only to the credentials it was asked with;
		// otherwise it is dropped and the current ones are asked below.
		if (enabled() &&
			signIn.username == SystemConf::getInstance()->get("global.retroachievements.username") &&
			signIn.password == SystemConf::getInstance()->get("global.retroachievements.password"))
			return applySignIn(signIn);

		LOG(LogInfo) << "[CheckCheevosTokenComponent] The account or the switch changed during the sign-in, so its answer was dropped.";
	}
	else if (mWorker.joinable())
	{
		mNextDelayMs = SignInPollMs;
		return false;
	}

	if (!enabled())
		return false;

	std::string cheevosUsername = SystemConf::getInstance()->get("global.retroachievements.username");
	std::string cheevosPassword = SystemConf::getInstance()->get("global.retroachievements.password");
	
	if (cheevosUsername.empty() || cheevosPassword.empty())
		return false;

	startSignIn(cheevosUsername, cheevosPassword);
	mNextDelayMs = SignInPollMs;
	return false;
}

bool CheckCheevosTokenComponent::takeSignIn(SignIn& signIn)
{
	// Exited is read first: the worker leaves its answer and then sets it,
	// so a worker seen to have exited has left its answer, and joining it
	// does not wait.
	bool exited = mWorkerExited;
	bool ready = false;
	{
		std::lock_guard<std::mutex> lock(mSignInLock);
		if (mSignInReady)
		{
			signIn = mSignIn;
			mSignInReady = false;
			ready = true;
		}
	}

	if (exited)
		joinWorker();

	return ready;
}

void CheckCheevosTokenComponent::startSignIn(const std::string& username, const std::string& password)
{
	// NetworkThread is never deleted (main.cpp), so this component's
	// destructor does not run at exit, and a sign-in still out then would
	// run on while the process's statics go, HttpReq's among them. This
	// static is made after those and goes before them: it joins the worker.
	static struct JoinAtExit
	{
		CheckCheevosTokenComponent* component;
		~JoinAtExit() { component->joinWorker(); }
	} joinAtExit { this };

	// Called only with no worker or one past its sign-in.
	joinWorker();

	mWorkerExited = false;
	mWorker = std::thread([this, username, password]()
	{
		SignIn signIn;
		signIn.username = username;
		signIn.password = password;
		signIn.ok = RetroAchievements::testAccount(username, password, signIn.tokenOrError, &signIn.refused);

		{
			std::lock_guard<std::mutex> lock(mSignInLock);
			mSignIn = signIn;
			mSignInReady = true;
		}

		mWorkerExited = true;
	});
}

bool CheckCheevosTokenComponent::applySignIn(const SignIn& signIn)
{
	if (signIn.ok)
	{
		mRetryWhenOnline = false;
		mUnreachableInARow = 0;
		if (signIn.tokenOrError == SystemConf::getInstance()->get("global.retroachievements.token"))
		{
			LOG(LogInfo) << "[CheckCheevosTokenComponent] Cheevos token is unchanged.";
		}
		else
		{
			LOG(LogInfo) << "[CheckCheevosTokenComponent] Generated a new cheevos token, saving.";
			mLastToken = signIn.tokenOrError;
			mLastAccount = signIn.username;
			return true;
		}
	}
	else
	{
		// A refusal is the account's to fix and no network changes it; any
		// other failure is the first check of a boot running before the
		// link was up (#175), and NetworkThread asks for it again when
		// the network arrives.
		mRetryWhenOnline = !signIn.refused;
		mUnreachableInARow = signIn.refused ? 0 : mUnreachableInARow + 1;
		LOG(LogError) << "[CheckCheevosTokenComponent] Failed to generate a new cheevos token: " << signIn.tokenOrError;

		// That network-up check can itself run before the resolver answers
		// (RC-3 on #175: "Could not resolve hostname" five seconds after
		// the address), and the watcher only speaks when the link changes.
		// So while the link is up, try again shortly, a bounded number of
		// times; WatchersManager reads updateTime() right after this.
		mNextDelayMs = CheevosRetry::nextDelayMs(mRetryWhenOnline, mOnline, mUnreachableInARow);
		if (mNextDelayMs != CheevosRetry::ScheduledMs)
		{
			LOG(LogWarning) << "[CheckCheevosTokenComponent] The network is up, so trying again in " << (mNextDelayMs / 1000) << " s (" << mUnreachableInARow << " of " << CheevosRetry::Attempts << ")";
		}
	}

	return false;
}

void CheckCheevosTokenComponent::setOnline(bool online)
{
	// A link that has just come up opens a new window: the failures before
	// it were the old link's, or no link's at all.
	if (online && !mOnline)
		mUnreachableInARow = 0;

	mOnline = online;
}


void NetworkThread::onJoystickChanged()
{
	WatchersManager::getInstance()->ResetComponent(&mCheckPadsBatteryLevelComponent);
}

void NetworkThread::OnWatcherChanged(IWatcher* component)
{
	if (component == &mCheckUpdatesComponent)
	{
		mWindow->displayNotificationMessage(_U("\uF019  ") + _("UPDATE AVAILABLE") + std::string(": ") + mCheckUpdatesComponent.getLastUpdateMessage());
		return;
	}

	if (component == &mCheckPadsBatteryLevelComponent)
	{
		auto pads = mCheckPadsBatteryLevelComponent.getPadsInfo();

		if (!AppWindow::post(mWindow, [pads]() { for (auto pad : pads) InputManager::getInstance()->updateBatteryLevel(pad.id, pad.device, pad.path, pad.battery); }))
			droppedAfterClosing();
		return;
	}

	if (component == mNetworkStateWatcher)
	{
		// The first sign-in of a boot can run before the network is up and
		// fail (#175); the next scheduled one is two hours off, and the
		// launch scripts read the token it would have written. So when the
		// network arrives, ask for that check now. Posted to the interface
		// thread: this runs on the watchers' thread, under the lock that
		// ResetComponent takes. The component hears about the link first,
		// on this same thread, so the check that follows knows it is worth
		// a short retry if the resolver is not answering yet.
		bool online = mNetworkStateWatcher->isConnected();
		mCheckCheevosTokenComponent.setOnline(online);

		// The device has come online (fork #292, #293, #305; D-UI-095,
		// D-RA-030, D-UI-109): the exit sync that was skipped for no network
		// runs first, with its card; then the RetroAchievements batch -- the
		// awards the proxy held while offline go up with a card that follows
		// the queue, and the recently played games' achievement data is
		// cached so they earn offline too (the ctl's top-up, fork #179, with
		// a card once it has work). With no saves owed the batch runs at
		// once. The ctl decides whether the toggle is on, an account is
		// signed in and RetroAchievements answers, and bounds how often it
		// runs. Posted to the interface thread: this runs on the watchers'
		// thread.
		if (online)
		{
			Window* window = mWindow;
			if (!AppWindow::post(mWindow, [window]() { ProxyCards::linkReturned(window); }))
				droppedAfterClosing();
		}

		// The startup index asks RetroAchievements for its hash library as the
		// interface starts, and a handheld's Wi-Fi is often still associating
		// then: the library does not come, and the run ends with nothing
		// indexed. When the link arrives, run it again; once the library has
		// come this session there is nothing to repeat (fork #183). D-RA-013's
		// promise -- a game added later is cached when the device is next
		// connected -- rests on the index knowing the game. On the interface
		// thread, as every hasher start is; the netplay index is left alone.
		if (online && Settings::CheevosCheckIndexesAtStart() && !ThreadedHasher::cheevosLibraryCameThisSession())
		{
			Window* window = mWindow;
			if (!AppWindow::post(mWindow, [window]() { SystemData::startIndexesAtStart(window, true); }))
				droppedAfterClosing();
		}

		if (online && mCheckCheevosTokenComponent.retryWhenOnline())
		{
			CheckCheevosTokenComponent* cheevos = &mCheckCheevosTokenComponent;
			if (!AppWindow::post(mWindow, [cheevos]() { WatchersManager::getInstance()->ResetComponent(cheevos); }))
				droppedAfterClosing();
		}
		return;
	}

	if (component == &mCheckCheevosTokenComponent)
	{
		auto token = mCheckCheevosTokenComponent.getLastToken();
		auto account = mCheckCheevosTokenComponent.getLastAccount();

		// Saved only while achievements are on and the account is the one the
		// token was issued for: the page may have turned the switch off,
		// clearing the token, or changed the account since the check (#1288).
		if (!AppWindow::post(mWindow, [token, account]()
			{
				if (!SystemConf::getInstance()->getBool("global.retroachievements") ||
					SystemConf::getInstance()->get("global.retroachievements.username") != account)
				{
					LOG(LogInfo) << "[CheckCheevosTokenComponent] The account or the switch changed before the token was saved, so it was dropped.";
					return;
				}

				SystemConf::getInstance()->set("global.retroachievements.token", token);
				SystemConf::getInstance()->saveSystemConf();
			}))
			droppedAfterClosing();

		return;
	}
}