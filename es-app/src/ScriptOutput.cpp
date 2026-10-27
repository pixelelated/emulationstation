#include "ScriptOutput.h"

#if !defined(WIN32)
#include <sys/wait.h>
#endif

namespace ScriptOutput
{
	LineReader::LineReader(const std::function<void(const std::string&)>& onLine, size_t maxLine)
		: mOnLine(onLine), mMaxLine(maxLine)
	{
	}

	void LineReader::feed(const char* data, size_t length)
	{
		for (size_t i = 0; i < length; i++)
		{
			const char c = data[i];
			if (c == '\n')
			{
				// The executors read 1,023 bytes at a time and called each
				// read a line: a long record came back in pieces (T21 PL-14).
				if (!mDropping)
					mOnLine(mPending);
				mPending.clear();
				mDropping = false;
				continue;
			}
			if (mDropping)
				continue;
			mPending += c;
			if (mPending.size() > mMaxLine)
			{
				// Refused whole: a cut record reads as two short ones.
				mOverflowed = true;
				mDropping = true;
				mPending.clear();
			}
		}
	}

	void LineReader::finish()
	{
		// The last line, which a script need not end with a line end.
		if (!mDropping && !mPending.empty())
			mOnLine(mPending);
		mPending.clear();
		mDropping = false;
	}

	int exitCode(int waitStatus)
	{
#if defined(WIN32)
		return waitStatus;
#else
		if (waitStatus == -1)
			return -1;
		if (WIFEXITED(waitStatus))
			return WEXITSTATUS(waitStatus);
		// A child a signal ended did not succeed: WEXITSTATUS alone read its
		// status's exit bits, which are 0 (T21 PL-14, #1202).
		if (WIFSIGNALED(waitStatus))
			return 128 + WTERMSIG(waitStatus);
		return -1;
#endif
	}
}
