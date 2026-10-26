// RetroAchievements' requests and answers as pure code (#1171).
#include "doctest/doctest.h"
#include "RetroAchievementsText.h"

#include <cctype>
#include <cstdio>
#include <string>

namespace
{
	std::string encode(const std::string& s)
	{
		std::string out;
		for (char c : s)
		{
			if (isalnum((unsigned char) c) || c == '-' || c == '_' || c == '.' || c == '~')
				out += c;
			else
			{
				char hex[4];
				snprintf(hex, sizeof(hex), "%%%02X", (unsigned char) c);
				out += hex;
			}
		}
		return out;
	}
}

TEST_CASE("RetroAchievements: the sign-in carries the password in its body, never in its URL (#1171)")
{
	// The vendored client's own login (rcheevos rc_api_user.c,
	// rc_api_init_login_request_hosted): POST to dorequest.php, the
	// parameters r=login2, u and p in the body, URL-encoded.
	const auto request = RetroAchievementsText::signInRequest("t20probe", "p&ss word", encode);
	CHECK(request.url == "https://retroachievements.org/dorequest.php");
	CHECK(request.url.find("p=") == std::string::npos);
	CHECK(request.url.find("u=") == std::string::npos);
	CHECK(request.body == "r=login2&u=t20probe&p=p%26ss%20word");
}

TEST_CASE("RetroAchievements: a request carrying a credential is never redirected to http (#1171)")
{
	const auto request = RetroAchievementsText::signInRequest("t20probe", "secretish", encode);
	CHECK(request.httpsRedirectsOnly);
}
