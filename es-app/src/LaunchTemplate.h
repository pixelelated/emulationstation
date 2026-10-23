#pragma once
#ifndef ES_APP_LAUNCH_TEMPLATE_H
#define ES_APP_LAUNCH_TEMPLATE_H

#include <string>
#include <utility>
#include <vector>

#include "utils/CommandLineUtil.h"
#include "utils/StringUtil.h"

// Building a launch command from its template (pixelelated/distribution#649,
// PL-001 and PL-007). LaunchCommand.h reads a finished command; this file
// builds one. Pure, so es-unit-tests holds it (LaunchTemplateTests.cpp).
// The rule for every value built from a name or a path: it reaches the
// shell as one argument, quoted by Utils::String::shellQuote, the way the
// capture command quotes its paths (FileData::launchGame).
namespace LaunchTemplate
{
	// The save-state option SaveState::setupSaveState appends. It was the
	// path in double quotes, inside which the shell still expands, so a ROM
	// whose name held shell characters handed the launcher another path.
	inline std::string stateFileOption(const std::string& file)
	{
		return " -state_file " + Utils::String::shellQuote(file);
	}

	// Every placeholder of `values` (its name with the percent signs, as
	// "%ROM%") replaced in one pass from the left. The text a value inserts
	// is never scanned again, so a scraped name or a file name that spells a
	// placeholder stays as it is; it was one replace per placeholder over
	// the whole line, each rewriting what the ones before had inserted. A
	// placeholder not given is left as it is.
	inline std::string expand(const std::string& command, const std::vector<std::pair<std::string, std::string>>& values)
	{
		std::string out;
		size_t i = 0;
		while (i < command.size())
		{
			bool matched = false;
			if (command[i] == '%')
			{
				for (const auto& v : values)
				{
					if (!v.first.empty() && command.compare(i, v.first.size(), v.first) == 0)
					{
						out += v.second;
						i += v.first.size();
						matched = true;
						break;
					}
				}
			}
			if (!matched)
				out += command[i++];
		}
		return out;
	}

	// Whether `command` already carries the option `name` ("-emulator",
	// "-core") as a whole word, in either form the launchers take: separate
	// ("-core snes9x") or joined ("--core=snes9x", ROCKNIX's). It looked for
	// the letters anywhere, so an executable or a path holding them hid the
	// option a forced netplay core needs.
	inline bool hasOption(const std::string& command, const std::string& name)
	{
		const std::string joined = "-" + name + "=";
		size_t i = 0;
		while (i < command.size())
		{
			if (command[i] == ' ' || command[i] == '\t')
			{
				i++;
				continue;
			}
			const size_t end = Utils::CommandLine::wordEnd(command, i);
			if ((end - i == name.size() && command.compare(i, name.size(), name) == 0)
				|| command.compare(i, joined.size(), joined) == 0)
				return true;
			i = end;
		}
		return false;
	}
}

#endif // ES_APP_LAUNCH_TEMPLATE_H
