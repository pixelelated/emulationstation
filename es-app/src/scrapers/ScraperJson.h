#pragma once
#ifndef ES_APP_SCRAPERS_SCRAPER_JSON_H
#define ES_APP_SCRAPERS_SCRAPER_JSON_H

// The scrapers' checked reads of a response (#1260). rapidjson asserts a
// value's type before a member read, and the recipe builds with NDEBUG, so
// an unchecked HasMember on an array, or GetString on a number, reads memory
// the value does not own. Each read here checks the container, then the
// member: a body of the wrong shape gives null, never a read.

#include <rapidjson/document.h>

namespace ScraperJson
{
	// The member `name` of v, or null when v is not an object or has none.
	inline const rapidjson::Value* member(const rapidjson::Value& v, const char* name)
	{
		if (!v.IsObject())
			return nullptr;

		auto it = v.FindMember(name);
		return it == v.MemberEnd() ? nullptr : &it->value;
	}

	inline const rapidjson::Value* object(const rapidjson::Value& v, const char* name)
	{
		auto m = member(v, name);
		return m != nullptr && m->IsObject() ? m : nullptr;
	}

	inline const rapidjson::Value* array(const rapidjson::Value& v, const char* name)
	{
		auto m = member(v, name);
		return m != nullptr && m->IsArray() ? m : nullptr;
	}

	// The member's text, or null when it is absent or not a string.
	inline const char* string(const rapidjson::Value& v, const char* name)
	{
		auto m = member(v, name);
		return m != nullptr && m->IsString() ? m->GetString() : nullptr;
	}
}

#endif // ES_APP_SCRAPERS_SCRAPER_JSON_H
