// What the scraper reads out of a server's answer (T21 S13, #668, #1205):
// ScreenScraper's user-info thread count and a Retry-After value.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "scrapers/ScraperReplies.h"

#include <ctime>

TEST_CASE("threadCount: an answer that is not user-info is a failure, not one thread (T21 PL-17, #1205)")
{
	CHECK(ScraperReplies::threadCount("<Data><ssuser><id>qa</id><maxthreads>4</maxthreads></ssuser></Data>") == 4);
	CHECK(ScraperReplies::threadCount("<Data><ssuser><id>qa</id><maxthreads>0</maxthreads></ssuser></Data>") == 1);
	CHECK(ScraperReplies::threadCount("<Data><ssuser><id>qa</id></ssuser></Data>") == 1);
	// ScreenScraper answers a refused sign-in with 200 and a sentence.
	CHECK(ScraperReplies::threadCount("Erreur de login : Verifier vos identifiants developpeur !") == -1);
	CHECK(ScraperReplies::threadCount("") == -1);
	CHECK(ScraperReplies::threadCount("<html><body>maintenance</body></html>") == -1);
	CHECK(ScraperReplies::threadCount("<Data><jeu/></Data>") == -1);
}

TEST_CASE("retryAfterSeconds: seconds, or an HTTP-date as the wait until it, or the default; never 0 for a date (T21 PL-17, #1205)")
{
	// Sun, 06 Nov 1994 08:49:37 GMT, RFC 9110's own example.
	const time_t now = 784111777 - 120;
	CHECK(ScraperReplies::retryAfterSeconds("30", now, 5) == 30);
	CHECK(ScraperReplies::retryAfterSeconds(" 30 ", now, 5) == 30);
	CHECK(ScraperReplies::retryAfterSeconds("Sun, 06 Nov 1994 08:49:37 GMT", now, 5) == 120);
	// A date already past: wait a moment, not none.
	CHECK(ScraperReplies::retryAfterSeconds("Sun, 06 Nov 1994 08:49:37 GMT", now + 600, 5) == 1);
	CHECK(ScraperReplies::retryAfterSeconds("soon", now, 5) == 5);
	CHECK(ScraperReplies::retryAfterSeconds("", now, 5) == 5);
	CHECK(ScraperReplies::retryAfterSeconds("-3", now, 5) == 5);
}
