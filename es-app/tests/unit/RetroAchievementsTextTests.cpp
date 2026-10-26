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

TEST_CASE("RetroAchievements: the sign-in carries a total and a stall bound (#1175)")
{
	const auto request = RetroAchievementsText::signInRequest("t20probe", "x", encode);
	CHECK(request.timeoutMs > 0);
	CHECK(request.timeoutMs <= 30000);
	CHECK(request.stallSeconds > 0);
}

TEST_CASE("RetroAchievements: a sign-in's answer belongs to the account that asked (#1175)")
{
	CHECK(RetroAchievementsText::sameAccount("Player", "pw", "player", "pw"));
	CHECK_FALSE(RetroAchievementsText::sameAccount("player", "pw", "other", "pw"));
	CHECK_FALSE(RetroAchievementsText::sameAccount("player", "pw", "player", "pw2"));
}

TEST_CASE("RetroAchievements: a scan started after an account change carries no other account's token (#1175)")
{
	CHECK(RetroAchievementsText::tokenForScan("a", "pw", "b", "pw2", "tokenA").empty());
	CHECK(RetroAchievementsText::tokenForScan("a", "pw", "a", "pw", "tokenA") == "tokenA");
}

TEST_CASE("RetroAchievements: a failure dialog that already says what happened carries no AN ERROR OCCURRED (#1178)")
{
	CHECK(RetroAchievementsText::errorBoxText("AN ERROR OCCURRED", "THE SENTENCE.") == "THE SENTENCE.");
	CHECK(RetroAchievementsText::errorBoxText("AN ERROR OCCURRED", "") == "AN ERROR OCCURRED");
}

TEST_CASE("RetroAchievements: the sign-in's refusal never shows the service's own words (#1178)")
{
	const auto text = RetroAchievementsText::refusalText("DIDN'T ACCEPT.", "Invalid user/password combination", "CHECK, THEN TRY AGAIN.");
	CHECK(text.find("Invalid") == std::string::npos);
	CHECK(text == "DIDN'T ACCEPT.\n\nCHECK, THEN TRY AGAIN.");
}

TEST_CASE("RetroAchievements: an API failure says it in the player's words, not curl's (#1178)")
{
	const auto text = RetroAchievementsText::apiFailureText(0, "Could not resolve hostname", "KEY REJECTED.", "COULDN'T REACH.");
	CHECK(text == "COULDN'T REACH.");
	CHECK(RetroAchievementsText::apiFailureText(401, "x", "KEY REJECTED.", "COULDN'T REACH.") == "KEY REJECTED.");
}

TEST_CASE("RetroAchievements: HARDCORE MODE is locked while offline achievements are on (#1179)")
{
	CHECK(RetroAchievementsText::hardcoreLocked(true, true));
	CHECK_FALSE(RetroAchievementsText::hardcoreLocked(true, false));
	CHECK_FALSE(RetroAchievementsText::hardcoreLocked(false, true));
}
