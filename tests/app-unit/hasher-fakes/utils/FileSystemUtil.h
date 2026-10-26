#pragma once
// Double for es-core/src/utils/FileSystemUtil.h: the one call the hasher makes.
#include <string>
namespace Utils { namespace FileSystem {
	inline std::string getExtension(const std::string& path) { auto d = path.rfind('.'); return d == std::string::npos ? std::string() : path.substr(d); }
}}
