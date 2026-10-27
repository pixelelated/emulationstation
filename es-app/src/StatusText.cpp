#include "StatusText.h"
#include "HttpText.h"
#include "LocaleES.h"

namespace StatusText
{
	static int rank(RecoveryOutcome outcome)
	{
		switch (outcome)
		{
		case RecoveryOutcome::None: return 0;
		case RecoveryOutcome::Restored: return 1;
		case RecoveryOutcome::Missing: return 2;
		case RecoveryOutcome::RestoredInMemory: return 3;
		}
		return 0;
	}

	RecoveryOutcome worst(RecoveryOutcome a, RecoveryOutcome b)
	{
		return rank(a) >= rank(b) ? a : b;
	}

	std::string ladderSentence(RecoveryOutcome outcome)
	{
		// One sentence each, true of what happened (T21 PL-18, #1206): the
		// player was told RESTORED when the write-back had failed or had
		// waited on the lock, and when there had been no file at all.
		switch (outcome)
		{
		case RecoveryOutcome::None:
			return "";
		case RecoveryOutcome::Restored:
			return _("YOUR SETTINGS FILE WAS DAMAGED. THE LAST GOOD COPY WAS RESTORED.");
		case RecoveryOutcome::Missing:
			return _("YOUR SETTINGS FILE WAS MISSING. THE LAST GOOD COPY WAS RESTORED.");
		case RecoveryOutcome::RestoredInMemory:
			return _("YOUR SETTINGS FILE WAS DAMAGED. THE LAST GOOD COPY IS IN USE AND WILL BE SAVED NEXT TIME.");
		}
		return "";
	}

	std::string scraperTransportSentence(int status)
	{
		// A connection that never opened is offline; a server that answered
		// 501 to 504, or redirected past the limit, is not answering; a
		// certificate that did not verify is its own sentence (T21 PL-18,
		// #1206; D-NET-017). All three were YOU'RE NOT ONLINE.
		if (status == HttpText::kStatusIoError || status == HttpText::kStatusInProgress)
			return _("YOU'RE NOT ONLINE. TRY AGAIN WHEN YOU ARE.");
		if (status == HttpText::kStatusServerUnavailable)
			return _("THE SCRAPER'S SERVER ISN'T ANSWERING RIGHT NOW.");
		if (status == HttpText::kStatusCertificate)
			return _("THE SCRAPER'S SERVER COULDN'T BE VERIFIED.");
		return "";
	}
}
