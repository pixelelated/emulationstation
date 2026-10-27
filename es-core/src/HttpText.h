#pragma once
#ifndef ES_CORE_HTTP_TEXT_H
#define ES_CORE_HTTP_TEXT_H

#include <cstddef>
#include <string>

// HttpReq's text, as code a unit can run without curl (T21 S7, #668):
// a response header as curl hands it, and a response body as one log line.
namespace HttpText
{
	// One header line: `length` bytes with no terminator, as curl's
	// CURLOPT_HEADERFUNCTION gives it (T21 PL-07, #1195). False for the
	// status line and a blank line; a line with no colon is a name with an
	// empty value.
	bool parseHeaderLine(const char* buffer, size_t length, std::string& name, std::string& value);

	// A response body as it may reach the log (T21 PL-19, #1207): one line,
	// at most `limit` bytes before the ellipsis, through maskSecrets.
	std::string bodyForLog(const std::string& body, size_t limit = 512);
}

#endif // ES_CORE_HTTP_TEXT_H
