#pragma once
#ifndef ES_APP_TEXT_ROW_EDIT_H
#define ES_APP_TEXT_ROW_EDIT_H

#include <string>

// What a text row's OK does, as pure code (T21 S1, #668, #1185): kept apart
// from GuiSettings so a unit can check it without a window.
namespace TextRowEdit
{
	// Whether the typed text is written. `openedBlank` is a popup that
	// opened empty because the stored value is hidden (a password row under
	// system.security.enabled); everywhere else an empty entry is a real
	// answer -- the device's own name, a signed-out account.
	bool writes(bool openedBlank, const std::string& typed);
}

#endif // ES_APP_TEXT_ROW_EDIT_H
