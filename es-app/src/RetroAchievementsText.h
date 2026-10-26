#pragma once

// RetroAchievements' requests and answers as pure code (#1171): what the
// sign-in sends and where, with nothing from the network, the window or the
// settings behind it, so es-app/tests/unit can hold the rule.

#include <cctype>
#include <functional>
#include <string>

namespace RetroAchievementsText
{
	// The sign-in's request: the URL, the body (empty for a GET), and whether a
	// redirect may leave https. The password is the caller's; encode is the
	// caller's URL encoder (HttpReq::urlEncode).
	struct SignInRequest
	{
		std::string url;
		std::string body;
		bool httpsRedirectsOnly = false;
		// The bounds the request carries (#1175): the whole request in ms, and
		// seconds without a byte. 0 leaves it to HttpReq's defaults.
		long timeoutMs = 0;
		long stallSeconds = 0;
	};

	inline SignInRequest signInRequest(const std::string& username, const std::string& password,
		const std::function<std::string(const std::string&)>& encode)
	{
		// As the vendored client signs in (rcheevos rc_api_user.c,
		// rc_api_init_login_request_hosted): a POST to dorequest.php with
		// r=login2, u and p in the URL-encoded body. A URL is logged and kept
		// by whatever relays it; a body is not. And a request carrying the
		// password never follows a redirect off https.
		SignInRequest request;
		request.url = "https://retroachievements.org/dorequest.php";
		request.body = "r=login2&u=" + encode(username) + "&p=" + encode(password);
		request.httpsRedirectsOnly = true;
		// Bounded (#1175): the pages' own 15 s in all, and 10 s without a byte,
		// so a service that accepts and then stalls ends with an answer.
		request.timeoutMs = 15000;
		request.stallSeconds = 10;
		return request;
	}

	// Whether two sign-ins name the same account: the user name, as the
	// service compares it, and the password (#1175).
	inline bool sameAccount(const std::string& userA, const std::string& passwordA,
		const std::string& userB, const std::string& passwordB)
	{
		std::string a = userA, b = userB;
		for (auto& c : a) c = (char) tolower((unsigned char) c);
		for (auto& c : b) c = (char) tolower((unsigned char) c);
		return a == b && passwordA == passwordB;
	}

	// The token a scan may carry into system.cfg when it is started before
	// the settings page has closed (#1175): the page's save at close is what
	// signs a changed account in, and until it has, the token on hand is the
	// account the page opened with.
	// The text of the pages' failure dialogs (#1178). The sentences are the
	// caller's, from the fork's catalog; these decide what goes on the screen.

	// A failure dialog: its heading (AN ERROR OCCURRED) and the sentence that
	// says what happened.
	inline std::string errorBoxText(const std::string& heading, const std::string& sentence)
	{
		// The sentence alone when there is one: it already says what happened,
		// and a heading above it says nothing more.
		return sentence.empty() ? heading : sentence;
	}

	// The sign-in's refusal: what happened, the service's own words, and what
	// to do next.
	inline std::string refusalText(const std::string& headline, const std::string& serverError, const std::string& advice)
	{
		// The service's words go to the log, never the screen.
		(void) serverError;
		return headline + "\n\n" + advice;
	}

	// A web API request that failed: the key's rejection for 401 or 403,
	// else the transport's failure.
	inline std::string apiFailureText(int httpStatus, const std::string& transportError,
		const std::string& keyRejected, const std::string& unreachable)
	{
		// curl's own words go to the log, never the screen.
		(void) transportError;
		return httpStatus == 401 || httpStatus == 403 ? keyRejected : unreachable;
	}

	// HARDCORE MODE while offline achievements are on (#1179, D-RA-052): the
	// casual-only feature holds hardcore where the ctl's enable put it, so
	// the switch is dimmed, says why, and is not writable from the page.
	inline bool hardcoreLocked(bool offlineAvailable, bool offlineOn)
	{
		return offlineAvailable && offlineOn;
	}

	inline std::string tokenForScan(const std::string& userAtOpen, const std::string& passwordAtOpen,
		const std::string& userNow, const std::string& passwordNow, const std::string& token)
	{
		return sameAccount(userAtOpen, passwordAtOpen, userNow, passwordNow) ? token : std::string();
	}
}
