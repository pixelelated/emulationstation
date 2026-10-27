#pragma once
//
// SaveStateOrder - the save state manager's order for an emulator that
// keeps incremental states: newest first (GuiSaveState::loadGrid's sort).
// Pure, so es-app/tests/unit can hold it to std::sort's contract: a strict
// weak ordering, false for a state against itself (fork #1256).

namespace SaveStateOrder
{
	template <class Date>
	inline bool newestFirst(const Date& date1, int slot1, const Date& date2, int slot2)
	{
		// `>=` alone was true for a state against itself, and for both of two
		// states made in the same second (#1256): the slot breaks the tie.
		if (date1 > date2)
			return true;
		if (date2 > date1)
			return false;
		return slot1 < slot2;
	}
}
