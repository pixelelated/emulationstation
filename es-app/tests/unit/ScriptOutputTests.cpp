// What a script printed and how it ended (T21 S10, #668, #1202): the reader
// and the exit ApiSystem's executors use, checked without a device. T20's
// reader cases (distribution ffb6b686cb, stage-b/positives/reader.cpp) are
// the shapes: a 1,500-byte record, a last line without a line end, a child
// killed by a signal.

#include "doctest/doctest.h"

#include "ScriptOutput.h"

#include <cstdio>
#include <string>
#include <vector>

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace
{
	std::vector<std::string> readAll(const std::string& bytes, size_t chunk, bool* overflowed = nullptr, size_t maxLine = ScriptOutput::kMaxLine)
	{
		std::vector<std::string> lines;
		ScriptOutput::LineReader reader([&lines](const std::string& line) { lines.push_back(line); }, maxLine);
		for (size_t at = 0; at < bytes.size(); at += chunk)
			reader.feed(bytes.data() + at, std::min(chunk, bytes.size() - at));
		reader.finish();
		if (overflowed != nullptr)
			*overflowed = reader.overflowed();
		return lines;
	}

	int statusOf(void (*child)())
	{
		const pid_t pid = fork();
		if (pid == 0)
		{
			child();
			_exit(0);
		}
		int status = 0;
		waitpid(pid, &status, 0);
		return status;
	}
}

TEST_CASE("a 1,500-byte record is one line, however the bytes arrive (T21 PL-14, #1202)")
{
	const std::string record = "{\"id\":1,\"title\":\"" + std::string(1480, 'x') + "\"}";
	const auto lines = readAll(record + "\n{\"id\":2}\n", 1024);
	REQUIRE(lines.size() == 2);
	CHECK(lines[0] == record);
	CHECK(lines[1] == "{\"id\":2}");
}

TEST_CASE("the last line is kept without a line end (T21 PL-14, #1202)")
{
	const auto lines = readAll("first\nhardcore=1", 7);
	REQUIRE(lines.size() == 2);
	CHECK(lines[1] == "hardcore=1");
}

TEST_CASE("a line past the limit is refused, not cut, and the reader says so (T21 PL-14, #1202)")
{
	bool overflowed = false;
	const auto lines = readAll("ok\n" + std::string(200, 'y') + "\nafter\n", 16, &overflowed, 100);
	CHECK(overflowed);
	REQUIRE(lines.size() == 2);
	CHECK(lines[0] == "ok");
	CHECK(lines[1] == "after");
}

TEST_CASE("exitCode: a signal is a failure, an exit is its code (T21 PL-14, #1202)")
{
	CHECK(ScriptOutput::exitCode(statusOf([] { _exit(0); })) == 0);
	CHECK(ScriptOutput::exitCode(statusOf([] { _exit(3); })) == 3);
	CHECK(ScriptOutput::exitCode(statusOf([] { raise(SIGKILL); })) == 128 + SIGKILL);
	// doctest catches SIGTERM in its own process; the child takes the default.
	CHECK(ScriptOutput::exitCode(statusOf([] { signal(SIGTERM, SIG_DFL); raise(SIGTERM); })) == 128 + SIGTERM);
	CHECK(ScriptOutput::exitCode(-1) == -1);

	// The shell itself killed under popen: the pin read it as exit 0.
	FILE* pipe = popen("echo hardcore=0; kill -KILL $$", "r");
	REQUIRE(pipe != nullptr);
	char buffer[64];
	while (fgets(buffer, sizeof(buffer), pipe)) {}
	CHECK(ScriptOutput::exitCode(pclose(pipe)) == 128 + SIGKILL);
}
