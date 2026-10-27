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

	int statusForHttpCode(long code)
	{
		if (code >= 200 && code <= 299)
			return kStatusSuccess;
		if (code >= 400 && code <= 500)
			return (int) code;
		// A server that answered and could not serve: not the player's
		// connection, which this read as before T21 PL-18 (#1206).
		if (code >= 501 && code <= 504)
			return kStatusServerUnavailable;
		return kStatusIoError;
	}

	int statusForTransferFailure(int curlCode)
	{
		switch (curlCode)
		{
		case 47:   // CURLE_TOO_MANY_REDIRECTS
			return kStatusServerUnavailable;
		case 60:   // CURLE_PEER_FAILED_VERIFICATION
		case 77:   // CURLE_SSL_CACERT_BADFILE
		case 83:   // CURLE_SSL_ISSUER_ERROR
		case 90:   // CURLE_SSL_PINNEDPUBKEYNOTMATCH
		case 91:   // CURLE_SSL_INVALIDCERTSTATUS
			return kStatusCertificate;
		}
		return kStatusIoError;
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
