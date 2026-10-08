#pragma once

#include <map>
#include <string>
#include <vector>

// A folder check describes layout, never file integrity or game compatibility.
// The identity and paths must still be those selected when the check started.
namespace CloudFolderValidation
{
struct Context
{
	bool valid = false;
	std::string configId;
	std::map<std::string, std::string> paths;
};
struct Category
{
	std::string category, state, path, detail;
};
struct Result
{
	bool valid = false;
	bool complete = false;
	std::vector<Category> categories;
};
Context parseContext(const std::string& json);
Result parseResult(const std::string& json, const std::string& runId,
	const Context& started, const Context& current, const std::vector<std::string>& selected);
}
