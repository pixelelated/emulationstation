#include "TextRowEdit.h"

namespace TextRowEdit
{
	bool writes(bool openedBlank, const std::string& typed)
	{
		// OK with nothing typed in a popup that hid the stored value is no
		// change; every other answer, an empty one included, is written.
		return !(openedBlank && typed.empty());
	}
}
