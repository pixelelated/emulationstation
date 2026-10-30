#include "ThreadedBluetooth.h"
#include "Window.h"
#include "components/AsyncNotificationComponent.h"
#include "guis/GuiMsgBox.h"
#include "ApiSystem.h"
#include "LocaleES.h"

#define ICONINDEX _U("\uF085 ")

// A scan is published under mLock before its worker starts, and its worker
// clears it under the lock before it deletes the object (#1290): a scan that
// ended before start() had stored it wrote into the freed object and left
// mInstance naming it, and every later scan was refused as already running.
ThreadedBluetooth* ThreadedBluetooth::mInstance = nullptr;
std::mutex ThreadedBluetooth::mLock;

ThreadedBluetooth::ThreadedBluetooth(Window* window)
	: mWindow(window), mHandle(nullptr)
{
	mWndNotification = mWindow->createAsyncNotificationComponent();
	mWndNotification->updateTitle(ICONINDEX + _("SCANNING BLUETOOTH"));	
	mWndNotification->updateText(_("Searching for devices..."));
}

ThreadedBluetooth::~ThreadedBluetooth()
{
	mWndNotification->close();
	mWndNotification = nullptr;
}

bool ThreadedBluetooth::isRunning()
{
	std::lock_guard<std::mutex> lock(mLock);
	return mInstance != nullptr;
}

void ThreadedBluetooth::updateNotificationComponentContent(const std::string info)
{
	if (info.empty())
		return;

	mWndNotification->updatePercent(-1);
	mWndNotification->updateText(info);
}

void ThreadedBluetooth::run()
{
	ApiSystem::getInstance()->scanNewBluetooth([this](const std::string info)
	{
		updateNotificationComponentContent(info);
	});

	{
		std::lock_guard<std::mutex> lock(mLock);
		ThreadedBluetooth::mInstance = nullptr;
	}
	delete this;
}

void ThreadedBluetooth::start(Window* window)
{
	if (isRunning())
	{
		window->pushGui(new GuiMsgBox(window, _("BLUETOOTH SCAN IS ALREADY RUNNING.")));
		return;
	}

	// start() runs on the interface thread alone, and a worker only clears.
	ThreadedBluetooth* scan = new ThreadedBluetooth(window);
	std::lock_guard<std::mutex> lock(mLock);
	ThreadedBluetooth::mInstance = scan;
	scan->mHandle = new std::thread(&ThreadedBluetooth::run, scan);
}


// Formatter

// The scan's rule (#1290).
ThreadedFormatter* ThreadedFormatter::mInstance = nullptr;
std::mutex ThreadedFormatter::mLock;

ThreadedFormatter::ThreadedFormatter(Window* window, const std::string disk, const std::string fileSystem)
	: mWindow(window), mHandle(nullptr)
{
	mDisk = disk;
	mFileSystem = fileSystem;

	mWndNotification = mWindow->createAsyncNotificationComponent();
	mWndNotification->updateTitle(ICONINDEX + _("FORMATTING DEVICE"));
	mWndNotification->updateText(_("Formatting") + " " + disk);
}

ThreadedFormatter::~ThreadedFormatter()
{
	mWndNotification->close();
	mWndNotification = nullptr;
}

bool ThreadedFormatter::isRunning()
{
	std::lock_guard<std::mutex> lock(mLock);
	return mInstance != nullptr;
}

void ThreadedFormatter::updateNotificationComponentContent(const std::string info)
{
	if (info.empty())
		return;

	mWndNotification->updatePercent(-1);
	mWndNotification->updateText(info);
}

void ThreadedFormatter::run()
{
#if WIN32
	std::this_thread::sleep_for(std::chrono::milliseconds(10000));
#endif

	int ret = ApiSystem::getInstance()->formatDisk(mDisk, mFileSystem, [this](const std::string info)
	{
		updateNotificationComponentContent(info);
	});

	if (ret == 69)
		mWindow->displayNotificationMessage(_("A REBOOT IS REQUIRED TO COMPLETE THE OPERATION"));

	{
		std::lock_guard<std::mutex> lock(mLock);
		ThreadedFormatter::mInstance = nullptr;
	}
	delete this;
}

void ThreadedFormatter::start(Window* window, const std::string disk, const std::string fileSystem)
{
	if (isRunning())
	{
		window->pushGui(new GuiMsgBox(window, _("A DRIVE IS ALREADY BEING FORMATTED.")));
		return;
	}

	ThreadedFormatter* format = new ThreadedFormatter(window, disk, fileSystem);
	std::lock_guard<std::mutex> lock(mLock);
	ThreadedFormatter::mInstance = format;
	format->mHandle = new std::thread(&ThreadedFormatter::run, format);
}
