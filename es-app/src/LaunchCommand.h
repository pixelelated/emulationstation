#pragma once
#ifndef ES_APP_LAUNCH_COMMAND_H
#define ES_APP_LAUNCH_COMMAND_H

#include <string>

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

#endif // ES_APP_LAUNCH_COMMAND_H
