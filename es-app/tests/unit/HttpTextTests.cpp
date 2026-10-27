// HttpReq's text without curl (T21 S7, #668): a response header as curl
// hands it, and a response body as one log line.

#include "doctest/doctest.h"

#include "HttpText.h"

#include <string>

TEST_CASE("parseHeaderLine reads only the bytes curl gave it (T21 PL-07, #1195)")
{
	// curl's buffer is size * nitems bytes with no terminator; what follows
	// in memory is not the header's. Here the next bytes are a whole other
	// line, as a reused buffer can hold.
	const std::string memory = "Content-Type: image/png\r\nX-Next: not-this\r\n";
	const size_t length = std::string("Content-Type: image/png\r\n").size();
	std::string name, value;
	REQUIRE(HttpText::parseHeaderLine(memory.data(), length, name, value));
	CHECK(name == "Content-Type");
	CHECK(value == "image/png");
}

TEST_CASE("parseHeaderLine: the status line and a blank line are not headers; a value keeps its colons")
{
	std::string name, value;
	const std::string status = "HTTP/2 200\r\n";
	CHECK_FALSE(HttpText::parseHeaderLine(status.data(), status.size(), name, value));
	const std::string blank = "\r\n";
	CHECK_FALSE(HttpText::parseHeaderLine(blank.data(), blank.size(), name, value));
	const std::string location = "Location: https://example.invalid:8443/a\r\n";
	REQUIRE(HttpText::parseHeaderLine(location.data(), location.size(), name, value));
	CHECK(name == "Location");
	CHECK(value == "https://example.invalid:8443/a");
	const std::string bare = "X-Flag\r\n";
	REQUIRE(HttpText::parseHeaderLine(bare.data(), bare.size(), name, value));
	CHECK(name == "X-Flag");
	CHECK(value == "");
}

TEST_CASE("bodyForLog: a 10 kB body reaches the log as one bounded, masked line (T21 PL-19, #1207)")
{
	std::string body = "error\r\nuser=qa&password=hunter2-qa\r\n";
	while (body.size() < 10 * 1024)
		body += "lorem ipsum dolor sit amet\n";
	const std::string line = HttpText::bodyForLog(body, 512);
	CHECK(line.size() <= 512 + 3);
	CHECK(line.find('\n') == std::string::npos);
	CHECK(line.find('\r') == std::string::npos);
	CHECK(line.find("hunter2-qa") == std::string::npos);
	CHECK(line.find("error") == 0);
	// A short body is kept whole, on one line.
	CHECK(HttpText::bodyForLog("not found\n", 512) == "not found");
}
