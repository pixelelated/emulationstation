// The scrapers' checked reads of a response (#1260): a member is read only
// from an object, a string only from a string, so a body of the wrong shape
// is a parse failure, never a read of memory the value does not own (the
// recipe builds with NDEBUG, so rapidjson's own checks are compiled out).

#include "doctest/doctest.h"

#include "scrapers/ScraperJson.h"

#include <string>

static rapidjson::Document parse(const char* json)
{
	rapidjson::Document doc;
	doc.Parse(json);
	return doc;
}

TEST_CASE("member: only an object has members")
{
	auto obj = parse("{\"a\":1}");
	CHECK(ScraperJson::member(obj, "a") != nullptr);
	CHECK(ScraperJson::member(obj, "b") == nullptr);
	for (const char* wrong : { "[]", "[1,2]", "\"x\"", "5", "null", "true" })
	{
		auto doc = parse(wrong);
		CHECK(ScraperJson::member(doc, "a") == nullptr);
	}
}

TEST_CASE("object, array and string read the member's type before it")
{
	auto doc = parse("{\"o\":{},\"a\":[1],\"s\":\"text\",\"n\":5}");
	CHECK(ScraperJson::object(doc, "o") != nullptr);
	CHECK(ScraperJson::object(doc, "a") == nullptr);
	CHECK(ScraperJson::array(doc, "a") != nullptr);
	CHECK(ScraperJson::array(doc, "s") == nullptr);
	CHECK(std::string(ScraperJson::string(doc, "s")) == "text");
	CHECK(ScraperJson::string(doc, "n") == nullptr);
	CHECK(ScraperJson::string(doc, "absent") == nullptr);
}

TEST_CASE("a token body of the wrong shape gives no token")
{
	for (const char* body : { "[]", "{\"access_token\":5}", "\"access_token\"", "{\"token\":[]}" })
	{
		auto doc = parse(body);
		CHECK(ScraperJson::string(doc, "access_token") == nullptr);
		CHECK(ScraperJson::string(doc, "token") == nullptr);
	}
	auto ok = parse("{\"access_token\":\"t\"}");
	CHECK(std::string(ScraperJson::string(ok, "access_token")) == "t");
}

TEST_CASE("nested reads stop at the first container of the wrong shape")
{
	auto doc = parse("{\"include\":{\"boxart\":\"x\"}}");
	auto include = ScraperJson::object(doc, "include");
	REQUIRE(include != nullptr);
	CHECK(ScraperJson::object(*include, "boxart") == nullptr);
	auto data = parse("{\"data\":[]}");
	CHECK(ScraperJson::object(data, "data") == nullptr);
}
