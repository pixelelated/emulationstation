#include "doctest/doctest.h"
#include "CloudFolderValidation.h"
#include <rapidjson/document.h>
#include <algorithm>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
using namespace CloudFolderValidation;
namespace {
const std::string context = R"({"schema_version":1,"config_id":"config1","paths":{"saves":"/mine/Saves","settings":"/mine/Backups","roms":"/games/roms","bios":"/games/bios","media":"/games/media"}})";
const std::string answer = R"({"schema_version":1,"run_id":"request1","config_id":"config1","complete":true,"categories":[{"category":"roms","state":"present","path":"/games/roms","detail":"files-found"},{"category":"bios","state":"empty","path":"/games/bios","detail":"no-files"}]})";
std::string replace(std::string s, const std::string& before, const std::string& after)
{
	const auto pos = s.find(before); REQUIRE(pos != std::string::npos); s.replace(pos,before.size(),after); return s;
}
Result parse(const std::string& s)
{
	auto c = parseContext(context); return parseResult(s,"request1",c,c,{"roms","bios"});
}
}
TEST_CASE("folder checks bind exact request configuration scope and paths")
{
	auto c = parseContext(context); REQUIRE(c.valid);
	auto r = parse(answer); REQUIRE(r.valid); CHECK(r.complete); CHECK(r.categories.size() == 2);
	CHECK(r.categories[0].state == "present"); CHECK(r.categories[1].state == "empty");
	CHECK_FALSE(parseResult(answer,"request2",c,c,{"roms","bios"}).valid);
	auto changed = c; changed.configId="config2";
	CHECK_FALSE(parseResult(answer,"request1",c,changed,{"roms","bios"}).valid);
	changed=c; changed.paths["roms"]="/elsewhere";
	CHECK_FALSE(parseResult(answer,"request1",c,changed,{"roms","bios"}).valid);
	CHECK_FALSE(parseResult(answer,"request1",c,c,{"roms"}).valid);
	CHECK_FALSE(parseResult(answer,"request1",c,c,{"roms","bios","media"}).valid);
	CHECK_FALSE(parseResult(answer,"request1",c,c,{"roms","bios","bios"}).valid);
	CHECK_FALSE(parseResult(answer,"request1",c,c,{"unknown"}).valid);
	CHECK_FALSE(parseResult(answer,"request1",c,c,{}).valid);
	CHECK_FALSE(parse(replace(answer,"/games/roms","/outside/roms")).valid);
	CHECK_FALSE(parse(replace(answer,"config1","config2")).valid);
}
TEST_CASE("folder checks keep missing misplaced empty and unreadable distinct")
{
	for (auto state : {"missing","empty","misplaced","present"})
	{
		auto r = parse(replace(answer,"present",state)); REQUIRE(r.valid); CHECK(r.complete);
		CHECK(r.categories[0].state == state);
	}
	auto r = parse(replace(answer,"present","unreadable")); REQUIRE(r.valid); CHECK_FALSE(r.complete);
	r = parse(replace(answer,"true","false")); REQUIRE(r.valid); CHECK_FALSE(r.complete);
	CHECK_FALSE(parse(replace(answer,"present","ready")).valid);
}
TEST_CASE("folder JSON rejects malformed missing duplicate and mistyped data")
{
	for (auto s : {"", "{}", "[]", "null", "false", "{", "truncated"}) CHECK_FALSE(parse(s).valid);
	CHECK_FALSE(parse(answer.substr(0,answer.size()-1)).valid);
	CHECK_FALSE(parse(replace(answer,"\"schema_version\":1","\"schema_version\":2")).valid);
	CHECK_FALSE(parse(replace(answer,"\"complete\":true","\"complete\":\"true\"")).valid);
	CHECK_FALSE(parse(replace(answer,"\"state\":\"present\"","\"state\":null")).valid);
	CHECK_FALSE(parse(replace(answer,"\"category\":\"bios\"","\"category\":\"roms\"")).valid);
	CHECK_FALSE(parse(replace(answer,"\"run_id\":\"request1\"","\"run_id\":\"bad\",\"run_id\":\"request1\"")).valid);
	CHECK_FALSE(parse(replace(answer,"\"detail\":\"files-found\"","\"detail\":\"a\\nline\"")).valid);
	CHECK_FALSE(parse(answer + std::string(1,'\0') + "junk").valid);
	CHECK_FALSE(parse(answer + std::string(65536,' ')).valid);
	CHECK_FALSE(parseContext(replace(context,"\"saves\":\"/mine/Saves\"","\"saves\":null")).valid);
	CHECK_FALSE(parseContext(replace(context,"\"config_id\":\"config1\"","\"config_id\":\"\"")).valid);
}

TEST_CASE("folder JSON pipe chunks preserve long records and enforce the size cap")
{
	const std::string source = replace(answer, "files-found", std::string(2500, 'a'));
	for (std::size_t chunk : {1u, 255u, 1023u, 1024u})
	{
		std::string output;
		for (std::size_t i = 0; i < source.size(); i += chunk)
			REQUIRE(appendOutput(output, source.data() + i, std::min(chunk, source.size() - i)));
		CHECK(output == source);
		CHECK(parse(output).valid);
	}
	std::string output;
	const std::string limit(65536, ' ');
	CHECK(appendOutput(output, limit.data(), limit.size()));
	CHECK_FALSE(appendOutput(output, "x", 1));
	CHECK(output.empty());
	CHECK_FALSE(appendOutput(output, limit.data(), 65537));
	CHECK(output.empty());
	CHECK_FALSE(parse(source.substr(0, 1023)).valid);
}
