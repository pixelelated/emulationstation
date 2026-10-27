#pragma once
#ifndef ES_CORE_UTILS_DOWNLOAD_FILE_H
#define ES_CORE_UTILS_DOWNLOAD_FILE_H

#include <cstdio>
#include <string>

// A file-mode download's start and end, as code a unit can run without curl
// (T21 PL-04, #1191): HttpReq calls these, es-fs-tests checks them.
namespace Utils
{
	namespace DownloadFile
	{
		// Where the bytes go until they are whole: a temporary of this
		// download's own beside `path`. `path` itself is not touched.
		std::string begin(const std::string& path);

		// Close the stream, and say whether everything written reached the
		// file: the flush's and the close's results.
		bool close(FILE*& file);

		// Put the whole download in place: `tmp` over `path`, only when the
		// stream closed cleanly and `tmp` is there. Otherwise `tmp` is
		// removed and `path` is left exactly as it was.
		bool finish(const std::string& tmp, const std::string& path, bool closedCleanly);
	}
}

#endif // ES_CORE_UTILS_DOWNLOAD_FILE_H
