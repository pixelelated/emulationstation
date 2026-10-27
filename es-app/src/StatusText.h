#pragma once
#ifndef ES_APP_STATUS_TEXT_H
#define ES_APP_STATUS_TEXT_H

#include "RecoveryOutcome.h"

#include <string>

// The words for two outcomes, as code a unit can check (T21 PL-18, #1206):
// what a settings recovery did, and why the scraper's server gave nothing.
namespace StatusText
{
	// The one the player is told about when both stores recovered: the one
	// with more left to do.
	RecoveryOutcome worst(RecoveryOutcome a, RecoveryOutcome b);

	// The sentence for a recovery; empty for None.
	std::string ladderSentence(RecoveryOutcome outcome);

	// The sentence for a request that got no usable answer -- no connection,
	// a server not answering, a certificate that did not verify; empty for
	// any other status, which the caller's own table words.
	std::string scraperTransportSentence(int status);
}

#endif // ES_APP_STATUS_TEXT_H
