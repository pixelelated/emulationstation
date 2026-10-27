#pragma once

#ifndef ES_APP_SCRAPERS_IGDBDB_SCRAPER_H
#define ES_APP_SCRAPERS_IGDBDB_SCRAPER_H

#include "scrapers/Scraper.h"
#include "utils/TimeUtil.h"

namespace pugi
{
	class xml_document;
}

class IGDBScraper : public Scraper
{
public:
	void generateRequests(const ScraperSearchParams& params,
		std::queue<std::unique_ptr<ScraperRequest>>& requests,
		std::vector<ScraperSearchResult>& results) override;

	bool isSupportedPlatform(SystemData* system) override;

	const std::set<ScraperMediaSource>& getSupportedMedias() override;

private:
	bool ensureToken();

	std::string mToken;
	Utils::Time::DateTime mTokenDate;
	// The last token request reached nothing (no network, a refused or
	// stalled connection): not a rejected sign-in (#1259).
	bool mTokenUnreachable = false;
};

class IGDBRequest : public ScraperHttpRequest
{
public:
	// ctor for a GetGameList request
	IGDBRequest(std::queue<std::unique_ptr<ScraperRequest>>& requestsWrite, std::vector<ScraperSearchResult>& resultsWrite, const std::string& url, HttpReqOptions* options, bool isArcade, bool isManualScrape)
		: ScraperHttpRequest(resultsWrite, url, options), mRequestQueue(&requestsWrite)
	{
		mIsManualScrape = isManualScrape;
		mIsArcade = isArcade;
	}

	// ctor for a GetGame request
	IGDBRequest(std::vector<ScraperSearchResult>& resultsWrite, const std::string& url, HttpReqOptions* options, bool isArcade, bool isManualScrape)
		: ScraperHttpRequest(resultsWrite, url, options), mRequestQueue(nullptr)
	{
		mIsManualScrape = isManualScrape;
		mIsArcade = isArcade;
	}

	virtual bool retryOn249() { return !mIsManualScrape; }

protected:
	bool process(const std::string& response, std::vector<ScraperSearchResult>& results) override;
	bool isGameRequest() { return !mRequestQueue; }

	bool mIsManualScrape;
	bool mIsArcade;
	std::queue<std::unique_ptr<ScraperRequest>>* mRequestQueue;
};

#endif // ES_APP_SCRAPERS_IGDBDB_SCRAPER_H
