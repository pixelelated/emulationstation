#pragma once
#ifndef ES_APP_SCRAPERS_SCRAPER_REPLIES_H
#define ES_APP_SCRAPERS_SCRAPER_REPLIES_H

#include <ctime>
#include <string>

// What the scraper reads out of a server's answer, as code a unit can check
// (T21 PL-17, #1205): validity beside the value, so an answer that is not
// one is a failure, never a number.
namespace ScraperReplies
{
	// ScreenScraper's user-info answer: its maxthreads, at least 1, or -1
	// when the answer is not one (not XML, no <Data><ssuser>).
	int threadCount(const std::string& body);

	// A Retry-After value -- delta-seconds or an HTTP-date -- as seconds to
	// wait from `now`; `fallback` for a value that is neither.
	long retryAfterSeconds(const std::string& value, time_t now, long fallback);
}

#endif // ES_APP_SCRAPERS_SCRAPER_REPLIES_H
