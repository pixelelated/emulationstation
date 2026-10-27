#pragma once
//
// BiosText - the BIOS CHECK page's words that are pure string code
// (es-code-traps.md, Pure text has a home; fork #1245). The page passes the
// catalog's templates, each already in the form for its count (ngettext),
// and this fills and joins them, so a language agrees its words with the
// number and orders them its own way: the fragments OF and MISSING were
// joined in English order, and MANQUANT never agreed.

#include <initializer_list>
#include <string>

namespace BiosText
{
	// `text` with each %i replaced by the next of `values`.
	inline std::string fill(const std::string& text, std::initializer_list<int> values)
	{
		std::string out;
		auto value = values.begin();
		for (size_t i = 0; i < text.size(); i++)
		{
			if (text[i] == '%' && i + 1 < text.size() && text[i + 1] == 'i' && value != values.end())
			{
				out += std::to_string(*value++);
				i++;
			}
			else
				out += text[i];
		}
		return out;
	}

	// ALL PRESENT (N) when nothing is missing or of an unknown version; else
	// the missing count of the total, then the unknown count, ", " between.
	inline std::string summary(int missing, int untested, int present,
		const std::string& missingOfTotal, const std::string& notKnown, const std::string& allPresent)
	{
		const int total = missing + untested + present;
		if (missing == 0 && untested == 0)
			return fill(allPresent, { total });

		std::string s;
		if (missing > 0)
			s = fill(missingOfTotal, { missing, total });
		if (untested > 0)
			s += (s.empty() ? "" : ", ") + fill(notKnown, { untested });
		return s;
	}
}
