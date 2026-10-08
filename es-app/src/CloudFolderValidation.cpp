#include "CloudFolderValidation.h"
#include <rapidjson/document.h>
#include <set>

namespace
{
const std::set<std::string> categories = {"saves", "settings", "roms", "bios", "media"};
const std::set<std::string> states = {"present", "missing", "empty", "misplaced", "unreadable"};
bool object(const rapidjson::Value& value)
{
	if (!value.IsObject()) return false;
	std::set<std::string> names;
	for (auto it = value.MemberBegin(); it != value.MemberEnd(); ++it)
		if (!names.insert(std::string(it->name.GetString(), it->name.GetStringLength())).second)
			return false;
	return true;
}
bool string(const rapidjson::Value& value, const char* name, std::string& out)
{
	if (!value.HasMember(name) || !value[name].IsString()) return false;
	out.assign(value[name].GetString(), value[name].GetStringLength());
	// Paths and IDs go to single facts and commands, never terminal control text.
	for (unsigned char c : out)
		if (c < 32 || c == 127) return false;
	return true;
}
bool document(const std::string& json, rapidjson::Document& doc)
{
	if (json.empty() || json.size() > 65536 || json.find('\0') != std::string::npos) return false;
	doc.Parse(json.c_str());
	return !doc.HasParseError() && object(doc) && doc.HasMember("schema_version")
		&& doc["schema_version"].IsInt() && doc["schema_version"].GetInt() == 1;
}
}

bool CloudFolderValidation::appendOutput(std::string& json, const char* data, std::size_t size)
{
	if (size > 65536 || json.size() > 65536 - size)
	{
		json.clear();
		return false;
	}
	json.append(data, size);
	return true;
}

CloudFolderValidation::Context CloudFolderValidation::parseContext(const std::string& json)
{
	Context c;
	rapidjson::Document doc;
	if (!document(json, doc) || !string(doc, "config_id", c.configId) || c.configId.empty()
		|| !doc.HasMember("paths") || !object(doc["paths"])) return {};
	for (const auto& key : categories)
	{
		std::string path;
		if (!string(doc["paths"], key.c_str(), path) || path.empty()) return {};
		c.paths[key] = path;
	}
	c.valid = true;
	return c;
}

CloudFolderValidation::Result CloudFolderValidation::parseResult(const std::string& json,
	const std::string& runId, const Context& started, const Context& current,
	const std::vector<std::string>& selected)
{
	Result result;
	if (runId.empty() || !started.valid || !current.valid || started.configId != current.configId
		|| started.paths != current.paths || selected.empty()) return result;
	std::set<std::string> expected;
	for (const auto& key : selected)
		if (!categories.count(key) || !expected.insert(key).second) return result;
	rapidjson::Document doc;
	std::string gotRun, gotConfig;
	if (!document(json, doc) || !string(doc, "run_id", gotRun) || gotRun != runId
		|| !string(doc, "config_id", gotConfig) || gotConfig != started.configId
		|| !doc.HasMember("complete") || !doc["complete"].IsBool()
		|| !doc.HasMember("categories") || !doc["categories"].IsArray()) return result;
	std::set<std::string> seen;
	for (const auto& row : doc["categories"].GetArray())
	{
		Category c;
		if (!object(row) || !string(row, "category", c.category) || !expected.count(c.category)
			|| !seen.insert(c.category).second || !string(row, "state", c.state) || !states.count(c.state)
			|| !string(row, "path", c.path) || c.path != started.paths.at(c.category)
			|| !string(row, "detail", c.detail)) return {};
		result.categories.push_back(c);
	}
	if (seen != expected) return {};
	result.complete = doc["complete"].GetBool();
	// An unreadable category is never a completed check, regardless of the flag.
	for (const auto& c : result.categories)
		if (c.state == "unreadable") result.complete = false;
	result.valid = true;
	return result;
}
