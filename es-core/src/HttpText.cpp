#include "HttpText.h"
#include "utils/StringUtil.h"

namespace HttpText
{
	bool parseHeaderLine(const char* buffer, size_t length, std::string& name, std::string& value)
	{
		// The bytes curl gave, and no more: its buffer has no terminator,
		// and reading to the next NUL took whatever followed in memory into
		// the header's value (T21 PL-07, #1195).
		std::string data = Utils::String::trim(std::string(buffer, length));
		if (data.empty() || Utils::String::startsWith(data, "HTTP/"))
			return false;
		auto splitPost = data.find(":");
		if (splitPost != std::string::npos)
		{
			name = Utils::String::trim(data.substr(0, splitPost));
			value = Utils::String::trim(data.substr(splitPost + 1));
		}
		else
		{
			name = data;
			value = "";
		}
		return true;
	}

	std::string bodyForLog(const std::string& body, size_t limit)
	{
		// One line: a server's body is anyone's text, and a line end in it
		// starts what reads as another log line (T21 PL-19, #1207). Masked
		// before it is cut, so a credential at the cut is masked whole.
		std::string line = Utils::String::replace(body, "\r\n", " / ");
		line = Utils::String::replace(line, "\r", " ");
		line = Utils::String::replace(line, "\n", " ");
		line = Utils::String::maskSecrets(Utils::String::trim(line));
		if (line.size() > limit)
			line = line.substr(0, limit) + "...";
		return line;
	}
}
