#include "utils/DownloadFile.h"
#include "utils/FileSystemUtil.h"

#include <atomic>

#if defined(_WIN32)
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

namespace Utils
{
	namespace DownloadFile
	{
		std::string begin(const std::string& path)
		{
			// A temporary of this download's own, the atomic writer's shape
			// (<path>.tmp.<pid>.<n>): one shared <path>.tmp let two requests
			// for one path write and rename the same file. And `path` stays
			// where it is until the whole download replaces it -- it used to
			// be removed here, before a byte arrived (T21 PL-04, #1191).
			static std::atomic<unsigned long> counter(0);
			return path + ".tmp." + std::to_string((long) getpid()) + "." + std::to_string(++counter);
		}

		bool close(FILE*& file)
		{
			if (file == nullptr)
				return true;
			const bool flushed = fflush(file) == 0;
			const bool closed = fclose(file) == 0;
			file = nullptr;
			return flushed && closed;
		}

		bool finish(const std::string& tmp, const std::string& path, bool closedCleanly)
		{
			if (!closedCleanly || !Utils::FileSystem::exists(tmp, false))
			{
				Utils::FileSystem::removeFile(tmp);
				return false;
			}

			if (Utils::FileSystem::renameFile(tmp, path))
				return true;
#if defined(_WIN32)
			// Windows can refuse a rename just after the stream closed; the
			// copy is the fallback HttpReq always had there.
			if (Utils::FileSystem::copyFile(tmp, path))
			{
				Utils::FileSystem::removeFile(tmp);
				return true;
			}
#endif
			Utils::FileSystem::removeFile(tmp);
			return false;
		}
	}
}
