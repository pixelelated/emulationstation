#include "scrapers/ScraperReplies.h"
#include "utils/StringUtil.h"

#include <cctype>
#include <pugixml/src/pugixml.hpp>

namespace ScraperReplies
{
	int threadCount(const std::string& body)
	{
		// Validity beside the value (T21 PL-17, #1205): ScreenScraper answers
		// a refused sign-in with 200 and a sentence, which failed to parse
		// and read as one thread -- a scrape started on a refused account.
		pugi::xml_document doc;
		if (!doc.load_string(body.c_str()))
			return -1;
		pugi::xml_node user = doc.child("Data").child("ssuser");
		if (!user)
			return -1;
		const int maxthreads = user.child("maxthreads").text().as_int();
		return maxthreads > 0 ? maxthreads : 1;
	}

	long retryAfterSeconds(const std::string& value, time_t now, long fallback)
	{
		const std::string v = Utils::String::trim(value);
		if (v.empty())
			return fallback;

		// delta-seconds
		bool digits = true;
		for (char c : v)
			if (!std::isdigit((unsigned char) c))
				digits = false;
		if (digits)
			return v.size() > 9 ? fallback : std::stol(v);

		// An HTTP-date (IMF-fixdate, RFC 9110): the wait until it. It read as
		// 0, and the retries came at once (T21 PL-17, #1205). A date already
		// past waits a moment rather than none.
		struct tm when = {};
		const char* end = strptime(v.c_str(), "%a, %d %b %Y %H:%M:%S GMT", &when);
		if (end == nullptr || *end != '\0')
			return fallback;
		const time_t at = timegm(&when);
		if (at == (time_t) -1)
			return fallback;
		const long wait = (long) (at - now);
		return wait > 0 ? wait : 1;
	}
}
