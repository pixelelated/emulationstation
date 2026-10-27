// The words for a settings recovery and for a scraper request with no
// usable answer (T21 S12, #668, #1206).

#include "doctest/doctest.h"

#include "HttpText.h"
#include "StatusText.h"

TEST_CASE("ladderSentence: three outcomes, three sentences, each true of its outcome (T21 PL-18, #1206)")
{
	CHECK(StatusText::ladderSentence(RecoveryOutcome::None) == "");
	CHECK(StatusText::ladderSentence(RecoveryOutcome::Restored) == "YOUR SETTINGS FILE WAS DAMAGED. THE LAST GOOD COPY WAS RESTORED.");
	CHECK(StatusText::ladderSentence(RecoveryOutcome::RestoredInMemory) == "YOUR SETTINGS FILE WAS DAMAGED. THE LAST GOOD COPY IS IN USE AND WILL BE SAVED NEXT TIME.");
	CHECK(StatusText::ladderSentence(RecoveryOutcome::Missing) == "YOUR SETTINGS FILE WAS MISSING. THE LAST GOOD COPY WAS RESTORED.");
}

TEST_CASE("worst: the outcome with more left to do is the one told (T21 PL-18, #1206)")
{
	CHECK(StatusText::worst(RecoveryOutcome::None, RecoveryOutcome::None) == RecoveryOutcome::None);
	CHECK(StatusText::worst(RecoveryOutcome::None, RecoveryOutcome::Restored) == RecoveryOutcome::Restored);
	CHECK(StatusText::worst(RecoveryOutcome::Restored, RecoveryOutcome::Missing) == RecoveryOutcome::Missing);
	CHECK(StatusText::worst(RecoveryOutcome::Missing, RecoveryOutcome::RestoredInMemory) == RecoveryOutcome::RestoredInMemory);
	CHECK(StatusText::worst(RecoveryOutcome::RestoredInMemory, RecoveryOutcome::Restored) == RecoveryOutcome::RestoredInMemory);
}

TEST_CASE("scraperTransportSentence: offline, not answering and not verified are three sentences (T21 PL-18, #1206)")
{
	CHECK(StatusText::scraperTransportSentence(HttpText::kStatusIoError) == "YOU'RE NOT ONLINE. TRY AGAIN WHEN YOU ARE.");
	CHECK(StatusText::scraperTransportSentence(HttpText::kStatusServerUnavailable) == "THE SCRAPER'S SERVER ISN'T ANSWERING RIGHT NOW.");
	CHECK(StatusText::scraperTransportSentence(HttpText::kStatusCertificate) == "THE SCRAPER'S SERVER COULDN'T BE VERIFIED.");
	CHECK(StatusText::scraperTransportSentence(403) == "");
	CHECK(StatusText::scraperTransportSentence(HttpText::kStatusSuccess) == "");
}
