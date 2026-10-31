#include "WatchersManager.h"
#include "Log.h"
#include <algorithm>
#include <chrono>
#include <vector>

std::mutex											WatchersManager::mNotificationLock;
std::list<IWatcherNotify*>							WatchersManager::mNotification;
std::mutex											WatchersManager::mWatchersLock;
std::list<WatchersManager::WatcherInfo*>			WatchersManager::mWatchers;
std::condition_variable								WatchersManager::mProbeDone;

WatchersManager* WatchersManager::mInstance = nullptr;

WatchersManager* WatchersManager::getInstance()
{
	if (mInstance == nullptr)
		mInstance = new WatchersManager();

	return mInstance;
}

void WatchersManager::stop()
{
	if (mInstance != nullptr)
	{
		delete mInstance;
		mInstance = nullptr;
	}
}

void WatchersManager::pause()
{
	if (mInstance == nullptr)
		return;

	{
		std::unique_lock<std::mutex> lock(mInstance->mThreadLock);
		mInstance->mPaused = true;
	}

	mInstance->mEvent.notify_all();
}

void WatchersManager::resume()
{
	if (mInstance == nullptr)
		return;

	{
		std::unique_lock<std::mutex> lock(mInstance->mThreadLock);
		mInstance->mPaused = false;
	}

	mInstance->mEvent.notify_all();
}

WatchersManager::WatchersManager()
{
	LOG(LogDebug) << "WatchersManager : Starting";

	mRunning = true;
	mPaused = false;
	mThread = new std::thread(&WatchersManager::run, this);
}

void WatchersManager::RegisterComponent(IWatcher* instance)
{
	std::unique_lock<std::mutex> lock(mWatchersLock);

	WatcherInfo* info = new WatcherInfo();
	info->component = instance;
	info->nextCheckTime =
		std::chrono::steady_clock::now() + std::chrono::milliseconds(instance->initialUpdateTime());

	mWatchers.push_back(info);
}

void WatchersManager::UnregisterComponent(IWatcher* instance)
{
	std::unique_lock<std::mutex> lock(mWatchersLock);

	// A probe of this watcher in flight keeps its record and the watcher
	// alive until it ends: wait for that probe alone, then look again, since
	// the list may have changed while the lock was released (#1283).
	for (;;)
	{
		auto it = std::find_if(mWatchers.begin(), mWatchers.end(), [instance](WatcherInfo* info) { return info->component == instance; });
		if (it == mWatchers.end())
			return;

		WatcherInfo* info = *it;
		if (info->probing)
		{
			mProbeDone.wait(lock);
			continue;
		}

		mWatchers.erase(it);
		delete info;
		return;
	}
}

void WatchersManager::ResetComponent(IWatcher* instance)
{
	std::unique_lock<std::mutex> lock(mWatchersLock);

	for (auto comp : mWatchers)
	{
		if (comp->component == instance)
		{
			comp->nextCheckTime = std::chrono::steady_clock::now();
			comp->resetPending = comp->probing; // kept when the probe ends (#1283)
			return;
		}
	}
}

void WatchersManager::RegisterNotify(IWatcherNotify* instance)
{
	std::unique_lock<std::mutex> lock(mNotificationLock);
	mNotification.push_back(instance);
}

void WatchersManager::UnregisterNotify(IWatcherNotify* instance)
{
	std::unique_lock<std::mutex> lock(mNotificationLock);

	for (auto it = mNotification.cbegin(); it != mNotification.cend(); it++)
	{
		if ((*it) == instance)
		{
			mNotification.erase(it);
			return;
		}
	}
}

void WatchersManager::NotifyComponentChanged(IWatcher* component)
{
	std::unique_lock<std::mutex> lock(mNotificationLock);

	for (IWatcherNotify* n : mNotification)
		n->OnWatcherChanged(component);
}

WatchersManager::~WatchersManager()
{
	if (mThread == nullptr)
		return;

	LOG(LogDebug) << "WatchersManager : Exit";

	// Stop the thread first: a probe runs outside the list lock, so the
	// records are freed only once no pass holds one (#1283).
	{
		std::unique_lock<std::mutex> lock(mThreadLock);
		mRunning = false;
	}
	mEvent.notify_all();

	mThread->join();
	delete mThread;
	mThread = nullptr;

	// Remove locks
	{
		std::unique_lock<std::mutex> lock(mWatchersLock);

		for (auto comp : mWatchers)
			delete comp;

		mWatchers.clear();
	}
	mProbeDone.notify_all();
}

void WatchersManager::run()
{
	while (mRunning)
	{
		// ThreadLock
		{
			std::unique_lock<std::mutex> lock(mThreadLock);

			if (mPaused)
				mEvent.wait(lock, [this]() { return !mPaused || !mRunning; });
			else
				mEvent.wait_for(lock, std::chrono::seconds(1));

			if (!mRunning)
				return;

			if (mPaused)
				continue;
		}

		// The due records are taken under the list lock and marked in flight;
		// each probe and its notification run outside it, so Register, Reset,
		// Get and every other watcher's Unregister return at once, and the
		// next check time is written back under the lock (#1283).
		std::vector<WatcherInfo*> due;
		auto now = std::chrono::steady_clock::now();
		{
			std::unique_lock<std::mutex> lock(mWatchersLock);

			for (auto item : mWatchers)
			{
				if (!item->component->enabled())
					continue;

				if (now < item->nextCheckTime)
					continue;

				item->probing = true;
				due.push_back(item);
			}
		}

		for (auto item : due)
		{
			if (mRunning && item->component->check())
				NotifyComponentChanged(item->component);

			std::unique_lock<std::mutex> lock(mWatchersLock);
			item->nextCheckTime = item->resetPending ? std::chrono::steady_clock::now() : now + std::chrono::milliseconds(item->component->updateTime());
			item->resetPending = false;
			item->probing = false;
			mProbeDone.notify_all();
		}

		if (!mRunning)
			return;
	}
}
