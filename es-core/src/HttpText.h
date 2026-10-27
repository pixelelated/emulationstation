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

	// HttpReq::Status's values for the outcomes that are not HTTP codes, here
	// so pure code can name them without curl (HttpReq.h asserts they match).
	const int kStatusInProgress = 0;
	const int kStatusIoError = 3;
	const int kStatusFileStreamError = 4;
	const int kStatusServerUnavailable = 5;   // 501 to 504, or redirects exhausted
	const int kStatusCertificate = 6;         // the server's certificate did not verify
	const int kStatusSuccess = 200;

	// How a request checks the server it talks to (D-NET-017, #1154): the
	// peer verified by default, with VERIFYHOST's 2; only a request that
	// sets allowUnverifiedPeer -- the scrapers' media downloads -- skips it,
	// and a request with a client certificate keeps it whatever it sets.
	struct TlsChoice
	{
		bool verifyPeer;
		long verifyHost;
	};
	TlsChoice tlsChoice(bool allowUnverifiedPeer, bool clientCertificate);

	// The image's CA bundle (T21 G1: present on the image).
	const char* const kCaBundle = "/etc/ssl/certs/ca-certificates.crt";

	// A completed transfer's HTTP code as HttpReq::Status (T21 PL-18, #1206).
	int statusForHttpCode(long code);
	// A transfer curl ended with an error, by its CURLcode, as HttpReq::Status.
	int statusForTransferFailure(int curlCode);
}

#endif // ES_CORE_HTTP_TEXT_H
