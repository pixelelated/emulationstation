#include "guis/GuiWifi.h"
#include "guis/WifiScanProcess.h"
#include "guis/GuiMenu.h"
#include "guis/GuiMsgBox.h"
#include "guis/GuiSettings.h"
#include "guis/GuiTextEditPopup.h"
#include "guis/GuiTextEditPopupKeyboard.h"
#include "components/TextComponent.h"
#include "components/BusyComponent.h"
#include "components/ButtonComponent.h"
#include "ApiSystem.h"
#include "GuiLoading.h"
#include "LocaleES.h"
#include "Log.h"
#include "Settings.h"
#include "SystemConf.h"
#include "ThemeData.h"
#include "Window.h"
#include "utils/StringUtil.h"

#include <algorithm>
#include <exception>
#include <utility>
#include <thread>

namespace
{
void releasePickerButtons(GuiComponent* component)
{
	// Called only after hitTest(-1, -1) has cleared every hover flag. A
	// held mouse press is then released without invoking a button action.
	if (auto button = dynamic_cast<ButtonComponent*>(component))
		button->onMouseClick(1, false, -1, -1);
	for (int i = 0; i < component->getChildCount(); ++i)
		releasePickerButtons(component->getChild(i));
}
}

// Owned by the main thread, independently of any picker. Like the save-state
// bookkeeper, keep the owner alive until the explicit shutdown/atexit join;
// a static joinable thread could otherwise be destroyed before that hook.
bool GuiWifi::sScansStopping = false;

struct GuiWifi::ScanWorker
{
	std::shared_ptr<Scan> scan;
	std::thread thread;
	bool stopping = false;
};

GuiWifi::ScanWorker* GuiWifi::scanWorker(bool create)
{
	static ScanWorker* worker = nullptr;
	if (create && worker == nullptr)
		worker = new ScanWorker();
	return worker;
}

void GuiWifi::shutdownScans()
{
	sScansStopping = true;
	auto worker = scanWorker(false);
	if (worker == nullptr || worker->stopping)
		return;
	worker->stopping = true;
	if (worker->scan)
		worker->scan->cancelled.store(true, std::memory_order_release);
	if (worker->thread.joinable())
	{
		LOG(LogInfo) << "wifi picker: stopping scan worker";
		worker->thread.join();
		LOG(LogInfo) << "wifi picker: scan worker joined";
	}
	worker->scan.reset();
}

GuiWifi::GuiWifi(Window* window, const std::string& title, const std::function<void()>& onJoined)
	: GuiComponent(window), mMenu(window, title.c_str()), mTitle(title), mOnJoined(onJoined), mSavedKnown(false), mWaitingLoad(false)
{
	setSize(Renderer::getScreenWidth(), Renderer::getScreenHeight());
	addChild(&mMenu);
	mMenu.setUpdateType(ComponentListFlags::UPDATE_WHEN_SELECTED);
	mMenu.addButton(_("REFRESH"), _("REFRESH"), [this] { onRefresh(); });
	mMenu.addButton(_("INPUT MANUALLY"), _("INPUT MANUALLY"), [this] { onManualInput(); });
	mMenu.addButton(_("BACK"), _("BACK"), [this] { if (!mWaitingLoad) delete this; });

	// Reserve the viewport once, including an in-list status band. Never
	// call updateSize/setSubTitle or rebuild buttons as scan answers arrive.
	const bool fullScreen = Renderer::ScreenSettings::fullScreenMenus();
	mMenu.setSize(fullScreen ? mSize.x() : std::min(mSize.y(), mSize.x() * 0.90f),
		fullScreen ? mSize.y() : mSize.y() * 0.75f);
	mMenu.setPosition((mSize.x() - mMenu.getSize().x()) / 2,
		fullScreen ? 0 : mSize.y() * 0.15f);

	auto theme = ThemeData::getMenuTheme();
	auto list = mMenu.getList();
	const auto position = list->getPosition();
	const auto size = list->getSize();
	// Prime this shared font before measuring its maximum glyph height.
	theme->Text.font->sizeText(_("SEARCHING WI-FI NETWORKS") + std::string(_("COULDN'T SEARCH WI-FI NETWORKS. TRY AGAIN.")) +
		_("SHOWING PREVIOUS RESULTS.") + _("COULDN'T CHECK WHICH NETWORKS ARE SAVED.") + _("NO WI-FI NETWORKS FOUND"));
	const float statusHeight = theme->Text.font->sizeText("A\nA", 1.0f).y() + mSize.y() * 0.01f;
	const float inset = ComponentList::rowInset();
	const float spinnerWidth = theme->Text.font->getLetterHeight() * 2.0f;
	mStatus = std::make_shared<TextComponent>(mWindow, "", theme->Text.font, theme->Text.color);
	mStatus->setMultiLine(TextComponent::MultiLineType::MULTILINE);
	mStatus->setLineSpacing(1.0f);
	mStatus->setVerticalAlignment(ALIGN_CENTER);
	if (EsLocale::isRTL())
		mStatus->setHorizontalAlignment(ALIGN_RIGHT);
	mStatus->setSize(size.x() - 2 * inset - spinnerWidth, statusHeight);
	mStatus->setPosition(position.x() + inset + spinnerWidth, position.y());
	mStatus->setZIndex(20);
	mMenu.addChild(mStatus.get());
	list->setPosition(position.x(), position.y() + statusHeight);
	list->setSize(size.x(), std::max(0.0f, size.y() - statusHeight));

	// No posted callback capturing this. The worker starts after the native
	// components are ready and publishes values for update() to consume.
	onRefresh(false);
}

void GuiWifi::load(const std::vector<WifiText::PickerRow>& rows)
{
	auto list = mMenu.getList();
	const bool buttonsFocused = mMenu.isCursorToButtons();
	const int previousIndex = list->getCursorIndex();
	const std::string selected = list->getSelectedUserData();
	const bool unchanged = rows.size() == mRows.size() && std::equal(rows.begin(), rows.end(), mRows.begin(),
		[](const WifiText::PickerRow& a, const WifiText::PickerRow& b)
		{
			return a.name == b.name && a.saved == b.saved && a.connected == b.connected &&
				a.savedKnown == b.savedKnown && a.profile == b.profile;
		});

	// Keep the actual list and scroll offset on identical refreshes. When
	// rows change, restore the SSID, or the nearest surviving index. The
	// native list keeps that row visible instead of jumping to the top.
	if (!unchanged)
	{
		mMenu.clear();
		mRows = rows;
		for (const auto& row : mRows)
			addRow(row);
		if (!mRows.empty())
		{
			auto match = std::find_if(mRows.begin(), mRows.end(), [&selected](const WifiText::PickerRow& row) { return row.name == selected; });
			const int index = match == mRows.end() ? std::min(previousIndex, (int)mRows.size() - 1) : (int)(match - mRows.begin());
			list->setCursorIndex(std::max(0, index), true);
		}
	}
	if (mRows.empty())
		mMenu.setCursorToButton(0);
	else if (!buttonsFocused)
		mMenu.setCursorToList();
}

// The name as NetworkManager has it -- case and spaces kept, since a
// network's name is what the player recognises it by (addEntry would
// upper-case it) -- and, on the right at the row's own weight, CONNECTED for
// the network the device is on or SAVED for one it holds a profile for: the
// shape and the word of MANAGE SAVED NETWORKS' mark, a fact beside the
// action.
void GuiWifi::addRow(const WifiText::PickerRow& network)
{
	auto theme = ThemeData::getMenuTheme();
	ComponentListRow row;

	auto name = std::make_shared<TextComponent>(mWindow, network.name, theme->Text.font, theme->Text.color);
	if (EsLocale::isRTL())
		name->setHorizontalAlignment(Alignment::ALIGN_RIGHT);
	row.addElement(name, true);

	if (network.connected || network.saved)
	{
		const std::string mark = network.connected ? _("CONNECTED") : _("SAVED");
		auto label = std::make_shared<TextComponent>(mWindow, mark, theme->Text.font, theme->Text.color, Alignment::ALIGN_RIGHT);
		label->setSize(theme->Text.font->sizeText(mark + "  ").x(), 0);
		row.addElement(label, false);
	}

	const WifiText::PickerRow chosen = network;
	row.makeAcceptInputHandler([this, chosen] { onSelect(chosen); });
	mMenu.addRow(row, false, false, network.name);
}

// A press: the network the device is on joins again with the key
// NetworkManager holds -- it answers at once, or comes back if it had
// dropped (#308 F-WF-05: this used to close on the list's snapshot); a
// saved one is offered CONNECT / FORGET / CANCEL (#318, D-UI-118); any
// unsaved open network connects without a key; protected or unknown ones ask
// for their key. One whose profile could not be asked about
// is asked about again first (F-WF-03/06). WifiText::pressAction decides.
void GuiWifi::onSelect(const WifiText::PickerRow& row)
{
	if (mWaitingLoad || mScan)
		return;

	if (row.savedKnown && WifiText::profilesForSsid(row.name, mSaved).size() > 1 && row.profile.empty())
	{
		chooseProfile(row.name);
		return;
	}
	act(WifiText::pressAction(row), row.name, WifiText::joinName(row));
}

// Several profiles can name one SSID. The player chooses a profile before
// CONNECT or FORGET; neither operation guesses which stored key they meant.
void GuiWifi::chooseProfile(const std::string& name)
{
	auto page = new GuiSettings(mWindow, _("SAVED NETWORKS"));
	for (const auto& profile : WifiText::profilesForSsid(name, mSaved))
		page->addEntry(profile.name, true, [this, name, profile, page]
		{
			// Closing page destroys this row's callback and its captures.
			// Keep the values on the stack before opening the next dialog.
			GuiWifi* picker = this;
			const std::string networkName = name;
			const std::string profileName = profile.name;
			page->close();
			picker->act(WifiText::PressAction::ChooseSaved, networkName, profileName);
		});
	mWindow->pushGui(page);
}

// A join goes by the profile's name, which need not be the SSID the row
// shows (#308 claude F-WF-12, gpt F-WF-03): the connected row joined by its
// SSID, which is no profile's name for a renamed one, and the player read
// COULDN'T CONNECT, with forget-and-rejoin advice, about the network they
// were on.
void GuiWifi::act(WifiText::PressAction action, const std::string& name, const std::string& joinAs)
{
	switch (action)
	{
	case WifiText::PressAction::Join:
		join(name, joinAs);
		break;
	case WifiText::PressAction::ChooseSaved:
	{
		// A saved network the device is not on: CONNECT with the key
		// NetworkManager holds, or FORGET it -- the second choice is the one
		// ROCKNIX's own saved-Wi-Fi work offers, behind the manage page's
		// confirmation and reader (GuiMenu::forgetWifiNetworkWithConfirmation),
		// which rebuilds this list once the profile is gone. CONNECT first;
		// CANCEL last, so B lands on it (es-ui-style-guide: back never lands
		// on the destructive choice). The profile is forgotten by the name
		// NetworkManager has for it (joinAs), which is the name the manage
		// page shows.
		const std::string text = Utils::String::format(_("%s IS SAVED.").c_str(), name.c_str())
			+ "\n\n" + _("CONNECT WITH ITS SAVED KEY, OR FORGET IT?");
		mWindow->pushGui(new GuiMsgBox(mWindow, text,
			_("CONNECT"), [this, name, joinAs] { join(name, joinAs); },
			_("FORGET"), [this, joinAs] { GuiMenu::forgetWifiNetworkWithConfirmation(mWindow, joinAs, false, [this] { onRefresh(false); }); },
			_("CANCEL"), nullptr));
		break;
	}
	case WifiText::PressAction::AskKey:
		checkSecurityAndConnect(name);
		break;
	case WifiText::PressAction::CheckAgain:
		// Never taken for a network with no profile: the key path rebuilds a
		// profile of that name from what is typed.
		mWindow->pushGui(new GuiMsgBox(mWindow,
			_("COULDN'T CHECK WHICH NETWORKS ARE SAVED.") + std::string("\n\n") + _("CHECK AGAIN NOW?"),
			_("YES"), [this] { onRefresh(false); },
			_("NO"), nullptr));
		break;
	}
}

// INPUT MANUALLY: a hidden network's name, taken as a row would be -- a
// name NetworkManager holds a profile for joins, the one the device is on
// needs nothing, any other is asked for its key. Not trimmed: a name may
// begin or end with a space, and it is matched as NetworkManager has it.
void GuiWifi::onManualInput()
{
	if (mWaitingLoad || mScan)
		return;

	auto onName = [this](const std::string& name)
	{
		if (name.empty())
			return;
		const auto profile = WifiText::preferredProfile(name, mSaved);
		if (mSavedKnown && WifiText::profilesForSsid(name, mSaved).size() > 1 && profile.empty())
		{
			chooseProfile(name);
			return;
		}
		act(WifiText::manualAction(name, mCurrent, mSaved, mSavedKnown), name,
			profile.empty() ? WifiText::manualJoinName(name, mCurrent, mCurrentProfile) : profile);
	};

	if (Settings::getInstance()->getBool("UseOSK"))
		mWindow->pushGui(new GuiTextEditPopupKeyboard(mWindow, mTitle, "", onName, false));
	else
		mWindow->pushGui(new GuiTextEditPopup(mWindow, mTitle, "", onName, false));
}

// A saved network: its profile, with the key NetworkManager holds, behind
// the spinner -- wifictl join waits up to 90 s for the association, as
// connect does, and the screen would otherwise freeze for it. The script
// also moves the settings wifi.ssid and wifi.key onto the joined network,
// so the paths that connect from the settings (the restore wizard, the
// ENABLE WI-FI switch) still name the network the player is on; SystemConf
// is re-read so the interface sees the same file.
void GuiWifi::join(const std::string& name, const std::string& profile)
{
	Window* window = mWindow;
	mWaitingLoad = true;
	LOG(LogInfo) << "wifi picker: joining the saved network " << name << " (profile " << profile << ")";
	window->pushGui(new GuiLoading<WifiText::JoinAnswer>(window, _("CONNECTING TO WI-FI"),
		[profile](IGuiLoadingHandler*) { return ApiSystem::getInstance()->joinWifiNetwork(profile); },
		[this, window, name, profile](WifiText::JoinAnswer answer)
		{
			mWaitingLoad = false;
			const int code = answer.code;
			if (!answer.joined())
			{
				LOG(LogWarning) << "wifi picker: could not join the saved network " << name << " (profile " << profile << ", wifictl join exited " << code << ")";
				// The advice by what happened (WifiText::joinFailure, #308
				// F-WF-03/06): NetworkManager not answering is not a key that
				// changed, and forgetting the network on that advice would
				// lose a profile that was fine.
				const std::string advice = WifiText::joinFailure(code) == WifiText::JoinFailure::ServiceNotAnswering
					? _("THE WI-FI SERVICE DIDN'T ANSWER. TRY AGAIN IN A MOMENT.")
					: _("IF ITS KEY HAS CHANGED, FORGET IT UNDER MANAGE SAVED NETWORKS AND JOIN IT AGAIN WITH THE NEW KEY.");
				window->pushGui(new GuiMsgBox(window,
					Utils::String::format(_("COULDN'T CONNECT TO %s.").c_str(), name.c_str()) + "\n\n" + advice, _("OK")));
				return;
			}
			// Re-read for what the join wrote (wifi.ssid, wifi.key), keeping
			// any change this interface has not saved yet -- one the settings
			// lock refused -- unless the join wrote that key (G-E1-03).
			SystemConf::getInstance()->loadSystemConf(true);
			joined(name);
		}));
}

// Any other network: its key first -- empty for an open network -- then
// the connection wifictl connect makes, which leaves a profile behind for
// next time. On success the settings take the network and its key, as they
// did when the row edited them: the network configured last, for the paths
// that connect from the settings.
void GuiWifi::checkSecurityAndConnect(const std::string& name)
{
	mWaitingLoad = true;
	mWindow->pushGui(new GuiLoading<std::string>(mWindow, _("CHECKING WI-FI NETWORK"),
		[name](IGuiLoadingHandler*) { return ApiSystem::getInstance()->getWifiSecurity(name); },
		[this, name](const std::string& security)
		{
			mWaitingLoad = false;
			if (security == "open")
				connect(name, "", true);
			else
				askKeyAndConnect(name);
		}));
}

void GuiWifi::askKeyAndConnect(const std::string& name)
{
	auto onKey = [this, name](const std::string& key) { connect(name, key); };

	if (Settings::getInstance()->getBool("UseOSK"))
		mWindow->pushGui(new GuiTextEditPopupKeyboard(mWindow, _("WI-FI KEY"), "", onKey, false));
	else
		mWindow->pushGui(new GuiTextEditPopup(mWindow, _("WI-FI KEY"), "", onKey, false));
}

void GuiWifi::connect(const std::string& name, const std::string& key, bool openNetwork)
{
	Window* window = mWindow;
	mWaitingLoad = true;
	LOG(LogInfo) << "wifi picker: connecting to " << name;
#if !WIN32
	const std::string country = SystemConf::getInstance()->get("wifi.country");
	auto connectNow = [name, key, country, openNetwork]() { return openNetwork ? ApiSystem::getInstance()->connectOpenWifi(name) : ApiSystem::getInstance()->enableWifi(name, key, country); };
#else
	auto connectNow = [name, key, openNetwork]() { return openNetwork ? ApiSystem::getInstance()->connectOpenWifi(name) : ApiSystem::getInstance()->enableWifi(name, key); };
#endif
	// The connect's exit is not the answer: wifictl exits 0 when the device
	// has no Wi-Fi at all. The page asks wifictl current which network the
	// device is on, and says CONNECTED only for the one asked for (T21
	// PL-06, #1194).
	auto apply = [connectNow, name](IGuiLoadingHandler*)
	{
		const bool connected = connectNow();
		std::string current;
		const bool answered = connected && ApiSystem::getInstance()->getCurrentWifiSsid(current);
		return WifiText::connectOutcome(connected, answered, current, name);
	};
	window->pushGui(new GuiLoading<WifiText::ConnectOutcome>(window, _("CONNECTING TO WI-FI"), apply,
		[this, window, name, key, openNetwork](WifiText::ConnectOutcome outcome)
		{
			mWaitingLoad = false;
			if (outcome == WifiText::ConnectOutcome::NotAvailable)
			{
				LOG(LogWarning) << "wifi picker: connect to " << name << " exited 0 with no network joined";
				window->pushGui(new GuiMsgBox(window, _("WI-FI ISN'T AVAILABLE ON THIS DEVICE RIGHT NOW."), _("OK")));
				return;
			}
			if (outcome != WifiText::ConnectOutcome::Joined)
			{
				LOG(LogWarning) << "wifi picker: could not connect to " << name;
				window->pushGui(new GuiMsgBox(window,
					Utils::String::format(_("COULDN'T CONNECT TO %s.").c_str(), name.c_str()) + "\n\n" + (openNetwork ? _("REFRESH THE NETWORK LIST AND TRY AGAIN.") : _("CHECK THE KEY AND TRY AGAIN.")), _("OK")));
				return;
			}
			SystemConf::getInstance()->set("wifi.ssid", name);
			SystemConf::getInstance()->set("wifi.key", key);
			SystemConf::getInstance()->saveSystemConf();
			joined(name);
		}));
}

// The picker's job is done: the toast says so over whatever the page
// returns to, and the page that opened the picker rebuilds to read the
// connection back. The callback is copied out first -- it belongs to that
// page, and this component is gone before it runs.
void GuiWifi::joined(const std::string& name)
{
	Window* window = mWindow;
	auto onJoined = mOnJoined;
	// <glyph> <subject> : <outcome>, one space after the glyph, as every
	// toast (#308 F-WF-08); CONNECTED is the row's word for the same fact.
	window->displayNotificationMessage(_U("\uF058 ") + WifiText::joinedNotice(name, _("CONNECTED")));
	delete this;
	if (onJoined != nullptr)
		onJoined();
}

bool GuiWifi::input(InputConfig* config, Input input)
{
	if (input.value != 0 && config->isMappedTo(BUTTON_BACK, input))
	{
		if (!mWaitingLoad)
			delete this;
		return true;
	}

	// A press during a scan cannot join an old row, open a key editor or
	// queue another scan. BACK remains available without joining the worker.
	if (mScan || mWaitingLoad)
		return true;
	return GuiComponent::input(config, input);
}

bool GuiWifi::hitTest(int x, int y, Transform4x4f& parentTransform, std::vector<GuiComponent*>* result)
{
	if (!mScan)
		return GuiComponent::hitTest(x, y, parentTransform, result);

	// Mouse/touch bypass input(). During scanning only the native BACK
	// button may capture a press, so a release after completion cannot act
	// on a refreshed row or start the action of a previously pressed button.
	std::vector<GuiComponent*> hits;
	const bool hit = GuiComponent::hitTest(x, y, parentTransform, &hits);
	if (result)
		for (auto component : hits)
			if (auto button = dynamic_cast<ButtonComponent*>(component))
				if (button->getText() == _("BACK"))
					result->push_back(button);
	return hit;
}

std::vector<HelpPrompt> GuiWifi::getHelpPrompts()
{
	std::vector<HelpPrompt> prompts;
	if (!mScan && !mWaitingLoad)
		prompts = mMenu.getHelpPrompts();
	prompts.push_back(HelpPrompt(BUTTON_BACK, _("BACK")));
	return prompts;
}

void GuiWifi::update(int deltaTime)
{
	GuiComponent::update(deltaTime);
	if (!mScan || !mScan->ready.load(std::memory_order_acquire))
		return;

	const Answer answer = mScan->answer;
	mScan.reset();
	mBusy.reset();
	mWindow->releaseMouseCapture();

	// A failed range query keeps prior network names. Saved/current metadata
	// is freshly read even then; unknown metadata keeps the existing guarded
	// CheckAgain route rather than interpreting an unknown profile as absent.
	std::vector<std::string> inRange = answer.inRange;
	const bool retainedResults = !answer.inRangeKnown && !mRows.empty();
	if (!answer.inRangeKnown)
		for (const auto& row : mRows)
			inRange.push_back(row.name);
	const auto rows = WifiText::pickerRows(inRange, answer.saved, answer.current,
		answer.savedKnown, answer.currentKnown);
	mSaved = answer.saved;
	mSavedKnown = answer.savedKnown;
	mCurrent.clear();
	mCurrentProfile.clear();
	for (const auto& row : rows)
		if (row.connected)
		{
			mCurrent = row.name;
			mCurrentProfile = row.profile;
		}
	load(rows);

	std::string status;
	if (!answer.inRangeKnown)
	{
		status = _("COULDN'T SEARCH WI-FI NETWORKS. TRY AGAIN.");
		if (retainedResults)
			status += "\n" + std::string(_("SHOWING PREVIOUS RESULTS."));
	}
	else if (rows.empty())
		status = _("NO WI-FI NETWORKS FOUND");
	else if (!mSavedKnown)
		status = _("COULDN'T CHECK WHICH NETWORKS ARE SAVED.");
	mStatus->setText(status);
	LOG(LogInfo) << "wifi picker: scan " << (answer.inRangeKnown ? "completed" : "failed")
		<< "; rows=" << rows.size() << "; retained=" << retainedResults;
	updateHelpPrompts();
}

void GuiWifi::onRefresh(bool rescan)
{
	if (mScan || mWaitingLoad || sScansStopping)
		return;

	// The process owns the one worker; picker Back never waits for it.
	// Reopening attaches to a pending scan. A later explicit refresh joins
	// the completed worker and starts a new job instead of reusing its answer.
	auto worker = scanWorker(true);
	if (worker->stopping)
		return;
	mScan = worker->scan;
	if (mScan && mScan->ready.load(std::memory_order_acquire))
	{
		if (worker->thread.joinable())
			worker->thread.join();
		worker->scan.reset();
		mScan.reset();
	}
	const bool reused = mScan != nullptr;
	if (!mScan)
	{
		mScan = std::make_shared<Scan>();
		worker->scan = mScan;
		auto scan = mScan;
		try
		{
			worker->thread = std::thread([scan, rescan]
			{
				try
				{
					Answer answer;
					const auto range = WifiScanProcess::read({"wifictl", rescan ? "scanlist" : "list"}, std::chrono::seconds(rescan ? 45 : 30), &scan->cancelled);
					answer.inRangeKnown = range.code == 0;
					answer.inRange = range.lines;

					// Match ApiSystem's saved/current protocol, but reject signal
					// and read failures too. No singleton or GUI is worker-owned.
					if (!scan->cancelled.load(std::memory_order_acquire))
					{
						const auto saved = WifiScanProcess::read({"wifictl", "saved", "--ssid"}, std::chrono::seconds(15), &scan->cancelled);
						answer.savedKnown = saved.code == 0;
						if (answer.savedKnown)
							for (const auto& line : saved.lines)
							{
								WifiText::SavedNetwork network;
								if (!WifiText::parseSavedDetailsLine(line, network))
								{
									answer.savedKnown = false;
									break;
								}
								answer.saved.push_back(network);
							}
						if (!answer.savedKnown)
							answer.saved.clear();
					}

					if (!scan->cancelled.load(std::memory_order_acquire))
					{
						const auto current = WifiScanProcess::read({"wifictl", "current"}, std::chrono::seconds(10), &scan->cancelled);
						answer.current = WifiText::parseCurrent(current.lines);
						answer.currentKnown = (current.code == 0 && !answer.current.empty()) || current.code == 1;
						if (current.code != 0)
							answer.current.clear();
					}
					scan->answer = std::move(answer);
				}
				catch (...)
				{
					// The default answer is unknown, not a successful empty scan.
					LOG(LogWarning) << "wifi picker: scan worker failed";
				}
				LOG(LogInfo) << "wifi picker: scan worker finished";
				scan->ready.store(true, std::memory_order_release);
			});
		}
		catch (const std::exception&)
		{
			LOG(LogWarning) << "wifi picker: could not start scan worker";
			mScan->ready.store(true, std::memory_order_release);
		}
	}

	if (mMenu.getList()->getScrollingVelocity() != 0)
		mMenu.getList()->stopScrolling();
	auto transform = Transform4x4f::Identity();
	mMenu.hitTest(-1, -1, transform);
	releasePickerButtons(&mMenu);
	mWindow->releaseMouseCapture();
	mStatus->setText(_("SEARCHING WI-FI NETWORKS"));
	const float spinnerWidth = ThemeData::getMenuTheme()->Text.font->getLetterHeight() * 2.0f;
	mBusy = std::make_shared<BusyComponent>(mWindow, "");
	mBusy->setBackgroundVisible(false);
	mBusy->setSize(spinnerWidth, mStatus->getSize().y());
	mBusy->setPosition(mStatus->getPosition().x() - spinnerWidth, mStatus->getPosition().y());
	mBusy->setZIndex(20);
	mMenu.addChild(mBusy.get());
	LOG(LogInfo) << "wifi picker: " << (reused ? "attached to pending scan" : "scan started");
	updateHelpPrompts();
}
