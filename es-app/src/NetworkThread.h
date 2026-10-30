#pragma once

#include "Window.h"
#include <atomic>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <list>

#include "InputManager.h"
#include "ApiSystem.h"
#include "CheevosRetry.h"
#include "watchers/WatchersManager.h"

class CheckPadsBatteryLevelComponent : public IWatcher
{
public:
	CheckPadsBatteryLevelComponent();

	std::vector<PadInfo>& getPadsInfo() { return mPadsInfo; }

protected:
	bool enabled() override { return mEnabled; };
	int  updateTime() override { return 15 * 1000; } // 15 seconds

	bool check() override;

private:
	std::vector<PadInfo> mPadsInfo;
	bool mEnabled;
};

class CheckUpdatesComponent : public IWatcher
{
public:
	CheckUpdatesComponent() { mEnabled = true; }

	std::string& getLastUpdateMessage() { return mLastUpdateMessage; }

protected:
	bool enabled() override;
	int  updateTime() override { return 24 * 60 * 60 * 1000; } // 24 * 60 minutes
	bool check() override;

private:
	std::string mLastUpdateMessage;
	bool mEnabled;
};

class CheckCheevosTokenComponent : public IWatcher
{
public:
	~CheckCheevosTokenComponent();

	std::string& getLastToken() { return mLastToken; }

	// The account the last token was issued for: it is saved only while
	// that account is still the one signed in (#1288).
	std::string& getLastAccount() { return mLastAccount; }

	// The last check could not reach RetroAchievements, as against being
	// turned down by it: worth running again as soon as the network is up
	// (#175), rather than at the next scheduled check two hours on.
	bool retryWhenOnline() const { return mRetryWhenOnline; }

	// NetworkThread's view of the link, told on the watchers' thread -- the
	// one check() runs on -- whenever it changes. A link that has just come
	// up opens a new window of short retries (#175, CheevosRetry.h).
	void setOnline(bool online);

protected:
	bool enabled() override;
	// Decided by the last check(): CheevosRetry::ScheduledMs (120 minutes)
	// unless that check could not reach the server while the network was
	// up, when it is a short retry, a bounded number of times -- or, while
	// a sign-in is out, SignInPollMs, so its answer is taken on the
	// watcher's next pass.
	int  updateTime() override { return mNextDelayMs; }
	int  initialUpdateTime() override { return 100; } // 100ms
	bool check() override;

private:
	// A sign-in's answer, with the credentials it was asked with (#1288).
	struct SignIn
	{
		std::string username;
		std::string password;
		std::string tokenOrError;
		bool ok = false;
		bool refused = false;
	};

	static const int SignInPollMs = 1000;

	bool takeSignIn(SignIn& signIn);
	void startSignIn(const std::string& username, const std::string& password);
	bool applySignIn(const SignIn& signIn);
	void joinWorker();

	std::string mLastToken;
	std::string mLastAccount;
	bool mRetryWhenOnline = false;
	bool mOnline = false;
	int  mUnreachableInARow = 0;
	int  mNextDelayMs = CheevosRetry::ScheduledMs;

	// The sign-in runs here, not under WatchersManager's lock (#1283). The
	// worker leaves its answer under mSignInLock and then sets
	// mWorkerExited; it never calls into the manager, so check() may join it
	// under that lock once it has left its answer.
	std::thread       mWorker;
	std::atomic<bool> mWorkerExited { true };
	std::mutex        mSignInLock;
	bool              mSignInReady = false;
	SignIn            mSignIn;
};

class NetworkStateWatcher;

class NetworkThread : public IJoystickChangedEvent, public IWatcherNotify
{
public:
	NetworkThread(Window * window);

	// Ask the RetroAchievements token check to run now, on the watchers'
	// thread, rather than at its next scheduled time: the settings page's
	// close, which used to sign in on the interface thread itself (#308
	// 1-raoffline claude F-RA-19). Called on the interface thread, as the
	// link-up's own reset is. Nothing before the thread exists.
	static void checkCheevosTokenSoon();

public:
	void onJoystickChanged() override;

private:
	void OnWatcherChanged(IWatcher* component) override;

	CheckPadsBatteryLevelComponent					mCheckPadsBatteryLevelComponent;
	CheckUpdatesComponent							mCheckUpdatesComponent;
	CheckCheevosTokenComponent						mCheckCheevosTokenComponent;
	NetworkStateWatcher*							mNetworkStateWatcher;
	Window* mWindow;
};


