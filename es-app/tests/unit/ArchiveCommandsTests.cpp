// ArchiveCommands: the scraper's and the manual viewer's shell commands around a
// file the player named, and the readers of their output (fork #668, T21 S3;
// bugs #1187 and #1205). The pin built these commands with the path inside
// double quotes; a stub program on PATH shows what the child actually receives.
#include "doctest/doctest.h"

#include "ArchiveCommands.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
	// A directory with a stub `pdfinfo` that prints its first argument, byte for
	// byte, so the test can read what a command hands the program.
	std::string makeStubDir()
	{
		char tmpl[] = "/tmp/es-archive-commands-XXXXXX";
		char* dir = mkdtemp(tmpl);
		REQUIRE(dir != nullptr);
		std::string stub = std::string(dir) + "/pdfinfo";
		FILE* f = fopen(stub.c_str(), "w");
		REQUIRE(f != nullptr);
		fputs("#!/bin/sh\nprintf '%s' \"$1\"\n", f);
		fclose(f);
		chmod(stub.c_str(), 0755);
		return dir;
	}

	void removeStubDir(const std::string& dir)
	{
		unlink((dir + "/pdfinfo").c_str());
		rmdir(dir.c_str());
	}

	// Runs the command through `sh -c`, as ApiSystem::executeEnumerationScript's
	// popen does, with the stub first on PATH; returns the child's output.
	std::string receivedBy(const std::string& stubDir, const std::string& command)
	{
		std::string full = "PATH=" + Utils::String::shellQuote(stubDir) + ":$PATH " + command;
		FILE* pipe = popen(full.c_str(), "r");
		if (pipe == nullptr)
			return "<popen failed>";
		std::string out;
		char buf[1024];
		size_t n;
		while ((n = fread(buf, 1, sizeof buf, pipe)) > 0)
			out.append(buf, n);
		pclose(pipe);
		return out;
	}

	// The pin's reading of the md5 pipeline's lines (ApiSystem.cpp:1530-1531 at
	// 16dbac5211): any single line of 32 characters or more is the hash.
	std::string pinMd5Reading(const std::vector<std::string>& ret)
	{
		if (ret.size() == 1 && ret.cbegin()->length() >= 32)
			return ret.cbegin()->substr(0, 32);
		return "";
	}
}

TEST_CASE("a path reaches the program as one argument, byte for byte, where the pin's double quotes let the shell read it")
{
	std::string stubDir = makeStubDir();

	// A name with a command substitution: benign, and it shows which form ran it.
	const std::string substituted = "a $(echo X) b.pdf";
	CHECK(receivedBy(stubDir, ArchiveCommands::pdfInfoCommand(substituted)) == substituted);
	std::string pinForm = "pdfinfo \"" + substituted + "\"";
	std::string pinReceived = receivedBy(stubDir, pinForm);
	CHECK(pinReceived != substituted);
	CHECK(pinReceived == "a X b.pdf");

	// Every character the shell would otherwise read, in one name.
	const std::string everything = "tricky \"name\" `echo Y` back\\slash 'quote' $HOME end.pdf";
	CHECK(receivedBy(stubDir, ArchiveCommands::pdfInfoCommand(everything)) == everything);
	CHECK(receivedBy(stubDir, "pdfinfo \"" + everything + "\"") != everything);

	removeStubDir(stubDir);
}

TEST_CASE("the five builders quote the path and never wrap it in double quotes")
{
	const std::string name = "/storage/roms/snes/it's a \"game\" $(x).7z";
	const std::string q = Utils::String::shellQuote(name);
	CHECK(ArchiveCommands::md5Pipeline("7zr", name) == "7zr x -so " + q + " | md5sum");
	CHECK(ArchiveCommands::listCommand("7zr", name) == "7zr l -slt " + q);
	CHECK(ArchiveCommands::extractCommand("7zr", name, "/tmp/out dir") == "7zr x " + q + " -y -o" + Utils::String::shellQuote("/tmp/out dir"));
	CHECK(ArchiveCommands::pdfInfoCommand(name) == "pdfinfo " + q);
	CHECK(ArchiveCommands::pdfToPpmCommand("96", " -f 1 -l 1", name, "/tmp/o/p") == "pdftoppm -jpeg -r 96 -cropbox -f 1 -l 1 " + q + " " + Utils::String::shellQuote("/tmp/o/p"));
	for (const std::string& cmd : { ArchiveCommands::md5Pipeline("7zr", name), ArchiveCommands::listCommand("7zr", name), ArchiveCommands::extractCommand("7zr", name, "/d"), ArchiveCommands::pdfInfoCommand(name), ArchiveCommands::pdfToPpmCommand("96", "", name, "/p") })
		CHECK(cmd.find("\"" + name + "\"") == std::string::npos);
}

TEST_CASE("a failed extraction's digest of empty input is no hash")
{
	const std::vector<std::string> emptyInput = { std::string(ArchiveCommands::EMPTY_INPUT_DIGEST) + "  -" };
	// The pin stored it as the game's MD5.
	CHECK(pinMd5Reading(emptyInput) == ArchiveCommands::EMPTY_INPUT_DIGEST);
	// The fix stores nothing from it.
	CHECK(ArchiveCommands::md5FromPipelineLines(emptyInput) == "");
	CHECK(ArchiveCommands::md5FromPipelineLines({ "0123456789abcdef0123456789abcdef  -" }) == "0123456789abcdef0123456789abcdef");
	CHECK(ArchiveCommands::md5FromPipelineLines({}) == "");
	CHECK(ArchiveCommands::md5FromPipelineLines({ "too short" }) == "");
	CHECK(ArchiveCommands::md5FromPipelineLines({ "zz23456789abcdef0123456789abcdef  -" }) == "");
	CHECK(ArchiveCommands::md5FromPipelineLines({ "0123456789abcdef0123456789abcdef  -", "a second line" }) == "");
}

TEST_CASE("the listing reader returns no CRC for an empty entry and never matches a line shorter than the name")
{
	CHECK(ArchiveCommands::crcFromListingLines({ "Path = game.sfc", "CRC = " }, "game.sfc") == "");
	CHECK(ArchiveCommands::crcFromListingLines({ "CRC = ", "CRC = 1A2B3C4D" }, "game.sfc") == "1A2B3C4D");
	CHECK(ArchiveCommands::crcFromListingLines({ "ab" }, "a-much-longer-name.bin") == "");
	CHECK(ArchiveCommands::crcFromListingLines({ "1A2B3C4D  2026-10-27 game.sfc" }, "game.sfc") == "1A2B3C4D");
	CHECK(ArchiveCommands::crcFromListingLines({}, "game.sfc") == "");
}
