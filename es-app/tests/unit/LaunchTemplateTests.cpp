// Building a launch command (pixelelated/distribution#649, PL-001 and
// PL-007): es-app/src/LaunchTemplate.h. Each case builds a command and
// splits it into the arguments the shell would hand the launcher, then
// asserts that a value inserted from a name or a path is one argument,
// equal to the input.
#include "doctest/doctest.h"
#include "LaunchTemplate.h"
#include "utils/CommandLineUtil.h"

#include <string>
#include <vector>

namespace
{
	// One shell word as the shell passes it on: single quotes keep every
	// character, double quotes and a bare word drop a backslash before the
	// next character, and the quotes themselves go. No expansion is done
	// here; a case checks the word that reaches the launcher, character for
	// character.
	std::string unquote(const std::string& word)
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
				continue;
			}
			if (c == '\\' && i + 1 < word.size())
			{
				out += word[++i];
				continue;
			}
			if (quote == '"')
			{
				if (c == '"')
					quote = 0;
				else
					out += c;
				continue;
			}
			if (c == '\'' || c == '"')
				quote = c;
			else
				out += c;
		}
		return out;
	}

	std::vector<std::string> words(const std::string& line)
	{
		std::vector<std::string> out;
		size_t i = 0;
		while (i < line.size())
		{
			if (line[i] == ' ' || line[i] == '\t')
			{
				i++;
				continue;
			}
			const size_t end = Utils::CommandLine::wordEnd(line, i);
			out.push_back(unquote(line.substr(i, end - i)));
			i = end;
		}
		return out;
	}

	// The argument after `flag`, or "<none>".
	std::string after(const std::vector<std::string>& args, const std::string& flag)
	{
		for (size_t i = 0; i + 1 < args.size(); i++)
			if (args[i] == flag)
				return args[i + 1];
		return "<none>";
	}

	// A name holding a space, a single quote, a double quote, a dollar sign
	// and a backslash, the characters a shell treats specially inside or
	// around double quotes.
	const std::string SPACE = " ", SQ = "'", DQ = "\"", DOLLAR = "$", BS = "\\";
	const std::string NAME = "Test" + SPACE + "Game" + SQ + "s" + SPACE + DQ + "Cut" + DQ + SPACE + DOLLAR + "HOME" + SPACE + BS + "x";
	const std::string BASE = "/usr/bin/runemu.sh /storage/roms/gb/game.gb -Pgb --core=gambatte --emulator=retroarch";
}

TEST_CASE("state file: one argument, equal to the file's path (PL-001)")
{
	const std::string file = "/storage/roms/savestates/gb/" + NAME + ".state.auto";
	const std::vector<std::string> args = words(BASE + " -autosave 1" + LaunchTemplate::stateFileOption(file));
	CHECK(after(args, "-state_file") == file);
	CHECK(after(args, "-autosave") == "1");
}

TEST_CASE("state file: a plain path is unchanged")
{
	const std::string file = "/storage/roms/savestates/snes/Super Mario World.state1";
	CHECK(after(words(BASE + LaunchTemplate::stateFileOption(file)), "-state_file") == file);
}

TEST_CASE("template: a placeholder's quoted value is one argument, equal to the input (PL-007)")
{
	const std::string cmd = LaunchTemplate::expand("/usr/bin/tool --name %GAMENAME% --base %BASENAME% --end",
		{ { "%GAMENAME%", Utils::String::shellQuote(NAME) }, { "%BASENAME%", Utils::String::shellQuote(NAME + "2") } });
	const std::vector<std::string> args = words(cmd);
	CHECK(after(args, "--name") == NAME);
	CHECK(after(args, "--base") == NAME + "2");
	CHECK(args.back() == "--end");
}

TEST_CASE("template: text a value inserts is not expanded again (PL-007)")
{
	// A game whose scraped name spells another placeholder keeps its name.
	const std::string cmd = LaunchTemplate::expand("/usr/bin/tool %GAMENAME% %CORE%",
		{ { "%GAMENAME%", "Road %CORE% Rash" }, { "%CORE%", "gambatte" } });
	CHECK(cmd == "/usr/bin/tool Road %CORE% Rash gambatte");
}

TEST_CASE("template: a placeholder it is not given is left as it is")
{
	CHECK(LaunchTemplate::expand("%ROM% %NETPLAY%", { { "%ROM%", "x" } }) == "x %NETPLAY%");
	CHECK(LaunchTemplate::expand("100% done", { { "%ROM%", "x" } }) == "100% done");
}

TEST_CASE("template: the forced options are found as whole words, in either form (PL-007)")
{
	CHECK(LaunchTemplate::hasOption(BASE, "-emulator"));
	CHECK(LaunchTemplate::hasOption(BASE, "-core"));
	CHECK(LaunchTemplate::hasOption("/usr/bin/sa -core snes9x %ROM%", "-core"));
	// A path or an argument holding the letters is not the option.
	CHECK_FALSE(LaunchTemplate::hasOption("/usr/bin/my-emulator-wrapper %ROM%", "-emulator"));
	CHECK_FALSE(LaunchTemplate::hasOption("/usr/bin/tool --hardcore %ROM%", "-core"));
}

// PL-010 (pixelelated/distribution#649, #776; the seam's contract is
// D-WORKFLOW-192): on ROCKNIX the netplay part of the line names the mode
// and the relay session in RetroArch's own words, and the launcher reads
// the host, the port and the password from settings.
namespace
{
	bool has(const std::vector<std::string>& args, const std::string& word)
	{
		for (const auto& a : args)
			if (a == word)
				return true;
		return false;
	}
}

TEST_CASE("netplay: a client join is the connect, the port and the nick, each one word")
{
	const auto args = words(LaunchTemplate::netplayArguments(false, false, "192.168.1.20", 55435, "", NAME, ""));
	CHECK(after(args, "--connect") == "192.168.1.20");
	CHECK(after(args, "--port") == "55435");
	CHECK(after(args, "--nick") == NAME);
	CHECK_FALSE(has(args, "--netplaymode"));
}

TEST_CASE("netplay: a spectator join says so beside the connect (PL-010)")
{
	const auto args = words(LaunchTemplate::netplayArguments(false, true, "192.168.1.20", 55435, "", "Bob", ""));
	CHECK(after(args, "--netplaymode") == "spectator");
	CHECK(after(args, "--connect") == "192.168.1.20");
}

TEST_CASE("netplay: a lobby entry's relay session is one word (PL-010)")
{
	const auto args = words(LaunchTemplate::netplayArguments(false, false, "10.0.0.1", 55435, "", "Bob", "abc123"));
	CHECK(has(args, "--mitm-session=abc123"));
	const auto none = words(LaunchTemplate::netplayArguments(false, false, "10.0.0.1", 55435, "", "Bob", ""));
	for (const auto& a : none)
		CHECK(a.compare(0, 15, "--mitm-session=") != 0);
}

TEST_CASE("netplay: a host with no port set takes the shipped default (PL-010)")
{
	CHECK(after(words(LaunchTemplate::netplayArguments(true, false, "", 0, "", "Bob", "")), "--port") == "55435");
	CHECK(after(words(LaunchTemplate::netplayArguments(true, false, "", 0, "55500", "Bob", "")), "--port") == "55500");
	CHECK(has(words(LaunchTemplate::netplayArguments(true, false, "", 0, "", "Bob", "")), "--host"));
}
