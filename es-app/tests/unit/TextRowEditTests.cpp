// A text row's OK, decided without a window (T21 S1, #668, #1185).
//
// An empty entry is a real answer wherever clearing means something: an
// empty HOSTNAME is the device's own name, an empty sign-in row signs out.
// The one exception is a popup that opened blank because the stored value
// is hidden (a password row with system.security.enabled): OK with nothing
// typed there changes nothing.

#include "doctest/doctest.h"

#include "TextRowEdit.h"

TEST_CASE("TextRowEdit::writes: the four cases of (opened blank, typed text)")
{
	CHECK(TextRowEdit::writes(false, ""));          // a shown value, cleared: written
	CHECK(TextRowEdit::writes(false, "RG35XX"));    // a shown value, edited: written
	CHECK_FALSE(TextRowEdit::writes(true, ""));     // a hidden value, OK with nothing typed: no change
	CHECK(TextRowEdit::writes(true, "new"));        // a hidden value, replaced: written
}
