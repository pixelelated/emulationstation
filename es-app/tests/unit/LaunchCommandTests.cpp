// What a finished launch command says it launched (#308 8-es-menus-and-core
// claude F-ES-20): es-app/src/LaunchCommand.h, pure and, until these cases,
// untested. The capture manifest records the system, emulator and core read
// here, and the boot game's are read from its stored command alone.
// `runemu` below is runemu.sh's reading of the same command: the arguments
// as the shell hands them over, the platform the first -P word after the ROM
// (#308) and --core= / --emulator= whole arguments, the last of each winning
// (distribution fe3e832e4a, #730). Where the two readings part company a case
// says so rather than hiding it.
#include "doctest/doctest.h"
#include "LaunchCommand.h"
#include "utils/CommandLineUtil.h"

#include <string>
#include <vector>

namespace
{
	// One word as the shell passes it on: single quotes keep every
	// character; inside double quotes a backslash is dropped only before a
	// dollar sign, a backquote, a double quote or a backslash; outside
	// quotes it is dropped before any character; the quotes themselves go.
	std::string shellWord(const std::string& word)
	{
		std::string out;
		char quote = 0;
		for (size_t i = 0; i < word.size(); i++)
		{
			const char c = word[i];
			if (quote == '\'')
			{
				if (c == '\'')
					quote = 0;
				else
					out += c;
			}
			else if (quote == '"')
			{
				if (c == '"')
					quote = 0;
				else if (c == '\\' && i + 1 < word.size() && std::string("$`\"\\").find(word[i + 1]) != std::string::npos)
					out += word[++i];
				else
					out += c;
			}
			else if (c == '\\' && i + 1 < word.size())
				out += word[++i];
			else if (c == '\'' || c == '"')
				quote = c;
			else
				out += c;
		}
		return out;
	}

	// runemu.sh's arguments: the command's words after the executable,
	// unquoted. The first is the ROM ($1); the loops read "${@:2}".
	std::vector<std::string> runemuArgs(const std::string& command)
	{
		std::vector<std::string> args;
		size_t i = 0;
		bool executable = true;
		while (i < command.size())
		{
			if (command[i] == ' ' || command[i] == '\t')
			{
				i++;
				continue;
			}
			const size_t end = Utils::CommandLine::wordEnd(command, i);
			if (!executable)
				args.push_back(shellWord(command.substr(i, end - i)));
			executable = false;
			i = end;
		}
		return args;
	}

	// PLATFORM: the first argument after the ROM matching -P?*, then stop.
	// CORE and EMULATOR: every argument after the ROM matching --core=* or
	// --emulator=*, the last winning.
	std::string runemu(const std::string& command, const std::string& prefix)
	{
		const std::vector<std::string> args = runemuArgs(command);
		std::string value;
		for (size_t i = 1; i < args.size(); i++)
		{
			if (args[i].compare(0, prefix.size(), prefix) != 0)
				continue;
			if (prefix == "-P")
			{
				if (args[i].size() > prefix.size())
					return args[i].substr(prefix.size());
				continue;
			}
			value = args[i].substr(prefix.size());
		}
		return value;
	}

	// The controllers blob as InputManager::configureEmulators writes it on
	// ROCKNIX: index and GUID per player, no names (Batocera's -p1name, which
	// a name like "Anbernic-Pad" would turn into a second -P, is not ours).
	const std::string ROCKNIX =
		"/usr/bin/runemu.sh /storage/roms/snes/Super\\ Mario\\ World.sfc -Psnes --core=snes9x --emulator=retroarch "
		"--controllers=\" -p1index 0 -p1guid 19000000010000000100000000010000 \"";
}

TEST_CASE("launch command: the shape ES builds")
{
	CHECK(launchToken(ROCKNIX, "-P") == "snes");
	CHECK(launchArgument(ROCKNIX, "--core", "fallback") == "snes9x");
	CHECK(launchArgument(ROCKNIX, "--emulator", "fallback") == "retroarch");
	CHECK(runemu(ROCKNIX, "-P") == "snes");
	CHECK(runemu(ROCKNIX, "--core=") == "snes9x");
}

TEST_CASE("launch command: a ROM whose name holds -P, before the real one")
{
	const std::string cmd = "/usr/bin/runemu.sh /storage/roms/arcade/Super-Pang.zip -Parcade --core=fbneo --emulator=retroarch";
	CHECK(launchToken(cmd, "-P") == "arcade");
	CHECK(runemu(cmd, "-P") == "arcade");
}

TEST_CASE("launch command: a per-system command with no --core= takes the fallback")
{
	const std::string tools = "/usr/bin/run /storage/roms/tools/Start\\ Kodi.sh";
	CHECK(launchArgument(tools, "--core", "kodi-core") == "kodi-core");
	CHECK(launchArgument(tools, "--emulator", "") == "");
	CHECK(launchToken(tools, "-P") == "");
}

TEST_CASE("launch command: a command that begins with the prefix")
{
	CHECK(launchToken("-Pgb --core=gambatte", "-P") == "gb");
}

TEST_CASE("launch command: a netplay nick holding -P after the system (where runemu.sh parts company)")
{
	const std::string cmd = ROCKNIX + " --connect 192.168.1.20 --port 55435 --nick 'My-Player'";
	// Both read the system the command was built with: the nick is one
	// argument that does not begin with -P. Until fe3e832e4a's sibling #308
	// runemu.sh read ${ARGUMENTS##*-P}, which matched inside the nick.
	CHECK(launchToken(cmd, "-P") == "snes");
	CHECK(runemu(cmd, "-P") == "snes");
}

// The rest of the finding's list: a netplay client's nick and a savestate's
// rewrite. A nick is the player's own text, single-quoted as one word
// (fork #275), and reading " -P" or " --core=" inside it recorded a system
// or a core the command was never built with -- the reader took the last
// occurrence of the letters anywhere in the line.
TEST_CASE("launch command: a netplay nick holding the prefixes is one word, not a token")
{
	const std::string cmd = ROCKNIX + " --connect '192.168.1.20' --port 55435 --nick 'Bob -Pro --core=x --emulator=y'";
	CHECK(launchToken(cmd, "-P") == "snes");
	CHECK(launchArgument(cmd, "--core", "fallback") == "snes9x");
	CHECK(launchArgument(cmd, "--emulator", "fallback") == "retroarch");
	CHECK(runemu(cmd, "-P") == "snes");
	CHECK(runemu(cmd, "--core=") == "snes9x");
	CHECK(runemu(cmd, "--emulator=") == "retroarch");
}

TEST_CASE("launch command: a ROM whose escaped name holds the prefixes")
{
	const std::string cmd = "/usr/bin/runemu.sh /storage/roms/snes/Hack\\ -Pbad\\ --core=bad.sfc -Psnes --core=snes9x --emulator=retroarch";
	CHECK(launchToken(cmd, "-P") == "snes");
	CHECK(launchArgument(cmd, "--core", "fallback") == "snes9x");
	CHECK(runemu(cmd, "-P") == "snes");
	CHECK(runemu(cmd, "--core=") == "snes9x");
}

TEST_CASE("launch command: a save state made with another core reads the core it was rewritten to")
{
	// SaveState::setupSaveState: the option swapped in place, the slot after.
	std::string cmd = Utils::CommandLine::replaceOptionValue(ROCKNIX, "-core", "snes9x2010");
	cmd = Utils::CommandLine::replaceOptionValue(cmd, "-emulator", "retroarch");
	cmd += " -autosave 1 -state_slot 3";
	CHECK(launchArgument(cmd, "--core", "fallback") == "snes9x2010");
	CHECK(launchArgument(cmd, "--emulator", "fallback") == "retroarch");
	CHECK(launchToken(cmd, "-P") == "snes");
}

// PL-021 (pixelelated/distribution#649, #787): a custom template may quote
// an option's value. The shell hands the launcher the value without its
// quotes, and the manifest records that, not the quoted text.
TEST_CASE("launch command: a quoted value is read without its quotes (PL-021)")
{
	const std::string single = "/usr/bin/runemu.sh /storage/roms/gb/game.gb -Pgb --core='gambatte' --emulator=\"retroarch\"";
	CHECK(launchArgument(single, "--core", "fallback") == "gambatte");
	CHECK(launchArgument(single, "--emulator", "fallback") == "retroarch");
	CHECK(runemu(single, "--core=") == "gambatte");
	CHECK(runemu(single, "--emulator=") == "retroarch");

	const std::string spaced = "/usr/bin/runemu.sh /storage/roms/gb/game.gb -P'gb' --core='my core'";
	CHECK(launchToken(spaced, "-P") == "gb");
	CHECK(launchArgument(spaced, "--core", "fallback") == "my core");
	CHECK(runemu(spaced, "-P") == "gb");
	CHECK(runemu(spaced, "--core=") == "my core");
}

TEST_CASE("launch command: an explicitly empty value is present, not absent (PL-021)")
{
	const std::string bare = "/usr/bin/runemu.sh /storage/roms/gb/game.gb -Pgb --core= --emulator=retroarch";
	CHECK(launchArgument(bare, "--core", "fallback") == "");
	CHECK(runemu(bare, "--core=") == "");

	const std::string quoted = "/usr/bin/runemu.sh /storage/roms/gb/game.gb -Pgb --core='' --emulator=retroarch";
	CHECK(launchArgument(quoted, "--core", "fallback") == "");
	CHECK(runemu(quoted, "--core=") == "");

	// Absent still takes the fallback.
	const std::string none = "/usr/bin/runemu.sh /storage/roms/gb/game.gb -Pgb --emulator=retroarch";
	CHECK(launchArgument(none, "--core", "fallback") == "fallback");
}

// Where the two readings part company, at fe3e832e4a: runemu.sh reads every
// whole argument, so a netplay nick that is itself an option (one quoted
// word, fork #275) is read as that option, and it comes after the real one.
// The header matches the option's name on the word as ES wrote it, so a
// quoted word is never an option and the manifest keeps the core ES built
// the command with. The launcher's half is a lead for T1 (#649).
TEST_CASE("launch command: a nick that is a whole option word (where runemu.sh parts company)")
{
	const std::string cmd = ROCKNIX + " --connect '192.168.1.20' --port 55435 --nick '--core=x'";
	CHECK(launchArgument(cmd, "--core", "fallback") == "snes9x");
	CHECK(runemu(cmd, "--core=") == "x");
}

// PL-024 (pixelelated/distribution#649): the launch error box shows the
// text a launcher wrote to /tmp/launch_error.log, as it was written. It is
// bounded, without its trailing line breaks, and never cut inside a UTF-8
// character.
TEST_CASE("launch error: the launcher's text is bounded for the box (PL-024)")
{
	CHECK(launchErrorText("CORE IS MISSING\n\n", 300) == "CORE IS MISSING");
	const std::string longText(1000, 'A');
	const std::string bounded = launchErrorText(longText, 300);
	CHECK(bounded.size() <= 303);
	CHECK(bounded.compare(bounded.size() - 3, 3, "...") == 0);
	// A two-byte character across the bound is dropped whole, not split.
	const std::string accented = std::string(299, 'A') + "\xC3\xA9" + "B";
	const std::string cut = launchErrorText(accented, 300);
	CHECK(cut == std::string(299, 'A') + "...");
	CHECK(launchErrorText("", 300) == "");
}
