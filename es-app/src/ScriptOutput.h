#pragma once
#ifndef ES_APP_SCRIPT_OUTPUT_H
#define ES_APP_SCRIPT_OUTPUT_H

#include <cstddef>
#include <functional>
#include <string>

// What a script the interface runs printed, and how it ended, as code a unit
// can run (T21 PL-14, #1202): ApiSystem's executors read through these.
namespace ScriptOutput
{
	// The longest line a script's line may be; a longer one is refused,
	// never cut into two.
	const size_t kMaxLine = 64 * 1024;

	// Bytes in, logical lines out: a line end ends a line, the last line is
	// kept without one, and a line past `maxLine` is not handed on --
	// overflowed() says one was refused.
	class LineReader
	{
	public:
		explicit LineReader(const std::function<void(const std::string&)>& onLine, size_t maxLine = kMaxLine);
		void feed(const char* data, size_t length);
		void finish();
		bool overflowed() const { return mOverflowed; }

	private:
		std::function<void(const std::string&)> mOnLine;
		size_t mMaxLine;
		std::string mPending;
		bool mDropping = false;
		bool mOverflowed = false;
	};

	// pclose's or system's status as an exit code: the child's own code
	// when it exited, 128 + the signal when one ended it, -1 when there is
	// no status. Only a child that exited 0 reads 0.
	int exitCode(int waitStatus);
}

#endif // ES_APP_SCRIPT_OUTPUT_H
