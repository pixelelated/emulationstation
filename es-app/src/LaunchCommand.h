#pragma once
#ifndef ES_APP_LAUNCH_COMMAND_H
#define ES_APP_LAUNCH_COMMAND_H

#include <functional>
#include <string>
#include <vector>

#include "utils/CommandLineUtil.h"

// Readers for a finished launch command (fork #21 R5). The save manifest
// records exactly what the emulator was handed, so these never re-resolve
// anything from SystemConf. Shared by FileData::getlaunchCommand (the
// ordinary launch) and launchStartupGame in main.cpp (the boot game, whose
// command is stored and replayed before any system is loaded, so the
// stored string is the only source).
//
// A token is a whole shell word of the command -- the words
// Utils::CommandLine::replaceOptionValue reads when a save state swaps the
// core, so the two agree -- and the last such word wins, as it does there.
// It was the last occurrence of the letters anywhere in the line, and a
// netplay nick is the player's own text, one quoted word: 'Bob -Pro'
// recorded the system as ro (#308 8-es-menus-and-core claude F-ES-20).
// The prefix is matched on the word as written, so a quoted word is never
// an option; the value after it is read as the shell hands it on, without
// its quotes (pixelelated/distribution#649 PL-021: a template's
// --core='x' recorded the core as 'x'). runemu.sh reads each option as a
// whole argument too (#308, distribution fe3e832e4a / #730); where the two
// still part company is in es-app/tests/unit/LaunchCommandTests.cpp.

// One shell word's text as the shell passes it on: single quotes keep
// every character; inside double quotes a backslash is dropped only before
// a dollar sign, a backquote, a double quote or a backslash; outside quotes
// before any character; the quotes themselves go.
inline std::string launchWordText(const std::string& word)
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

// Whether the command carries a word that begins with `prefix`, and the
// value of the last one, to the end of that word and without its quotes.
// A word that is the prefix alone ("--core=") is present with an empty
// value.
inline bool findLaunchToken(const std::string& command, const std::string& prefix, std::string& value)
{
	bool found = false;
	size_t i = 0;
	while (i < command.size())
	{
		if (command[i] == ' ' || command[i] == '\t')
		{
			i++;
			continue;
		}
		const size_t end = Utils::CommandLine::wordEnd(command, i);
		if (command.compare(i, prefix.size(), prefix) == 0)
		{
			value = launchWordText(command.substr(i + prefix.size(), end - i - prefix.size()));
			found = true;
		}
		i = end;
	}
	return found;
}

// The value of the last word that begins with `prefix` ("-P" ->
// the system name); "" when the command carries none.
inline std::string launchToken(const std::string& command, const std::string& prefix)
{
	std::string value;
	return findLaunchToken(command, prefix, value) ? value : std::string();
}

// The value of a "--key=" word: a ROM path or a --controllers= blob holding
// the literal is part of another word and cannot win. An empty value the
// command states ("--core=") is that value (PL-021: it took the fallback).
// `fallback` covers a per-system customCommandLine with no such token
// (config/emulators/tools.conf is "%RUNCOMMAND%").
inline std::string launchArgument(const std::string& command, const std::string& key, const std::string& fallback)
{
	std::string value;
	return findLaunchToken(command, key + "=", value) ? value : fallback;
}

// The text a launcher left in /tmp/launch_error.log, as the launch error
// box shows it (pixelelated/distribution#649, PL-024): without its trailing
// line breaks and spaces, and at most `max` bytes, ending in "..." when cut.
// A cut lands before a UTF-8 character, never inside one. The box showed
// the file as it was written, whatever its length.
inline std::string launchErrorText(const std::string& text, size_t max)
{
	size_t end = text.size();
	while (end > 0 && (text[end - 1] == '\n' || text[end - 1] == '\r' || text[end - 1] == ' ' || text[end - 1] == '\t'))
		end--;
	if (end <= max)
		return text.substr(0, end);

	size_t cut = max;
	while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
		cut--;
	return text.substr(0, cut) + "...";
}

// The boot game's stored command, brought up to date from the player's
// settings (pixelelated/distribution#649 PL-006, the coordinator's option
// (a)). It was stored when the game was chosen and replayed as it was, so
// a core or emulator set since, or a state file since deleted, was not
// seen. No system is loaded at boot, so only the player's explicit choices
// are read: the game's own (`<system>["<file>"].core`/`.emulator`, the key
// FileData::getConfigurationName makes), then the system's
// (`<system>.core`/`.emulator`), "auto" meaning none. What this cannot see,
// without the systems table: a table default that moved, and an override
// naming a core its emulator lacks, which FileData::getCore would drop.
// `changed` says what was refreshed, for the log.
struct BootCommand
{
	std::string command;
	std::vector<std::string> changed;
};

inline BootCommand refreshBootCommand(const std::string& stored, const std::string& gamePath,
	const std::function<std::string(const std::string&)>& setting, const std::function<bool(const std::string&)>& fileExists)
{
	BootCommand r;
	r.command = stored;

	const std::string system = launchToken(stored, "-P");
	if (!system.empty())
	{
		std::string file = gamePath.substr(gamePath.find_last_of('/') == std::string::npos ? 0 : gamePath.find_last_of('/') + 1);
		std::string clean;
		for (char c : file)
			if (c != '=' && c != '#')
				clean += c;
		const std::string game = system + "[\"" + clean + "\"]";

		auto chosen = [&setting](const std::string& gameKey, const std::string& systemKey)
		{
			std::string v = setting(gameKey);
			if (v.empty() || v == "auto")
				v = setting(systemKey);
			return v == "auto" ? std::string() : v;
		};
		for (const char* option : { "emulator", "core" })
		{
			const std::string now = chosen(game + "." + option, system + "." + option);
			std::string was;
			if (now.empty() || !findLaunchToken(r.command, std::string("--") + option + "=", was) || was == now)
				continue;
			r.command = Utils::CommandLine::replaceOptionValue(r.command, std::string("-") + option, now);
			r.changed.push_back(std::string(option) + " " + was + " -> " + now);
		}
	}

	// A stored -state_file whose file is gone is dropped with its value.
	size_t i = 0;
	while (i < r.command.size())
	{
		if (r.command[i] == ' ' || r.command[i] == '\t')
		{
			i++;
			continue;
		}
		const size_t end = Utils::CommandLine::wordEnd(r.command, i);
		if (r.command.compare(i, end - i, "-state_file") == 0 && end - i == 11)
		{
			size_t v = end;
			while (v < r.command.size() && (r.command[v] == ' ' || r.command[v] == '\t'))
				v++;
			const size_t vEnd = Utils::CommandLine::wordEnd(r.command, v);
			const std::string path = launchWordText(r.command.substr(v, vEnd - v));
			if (!path.empty() && !fileExists(path))
			{
				size_t from = i;
				while (from > 0 && (r.command[from - 1] == ' ' || r.command[from - 1] == '\t'))
					from--;
				r.command = r.command.substr(0, from) + r.command.substr(vEnd);
				r.changed.push_back("state file " + path + " is gone");
				i = from;
				continue;
			}
			i = vEnd;
			continue;
		}
		i = end;
	}
	return r;
}

#endif // ES_APP_LAUNCH_COMMAND_H
