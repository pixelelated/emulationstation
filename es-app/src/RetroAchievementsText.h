#pragma once

// RetroAchievements' requests and answers as pure code (#1171): what the
// sign-in sends and where, with nothing from the network, the window or the
// settings behind it, so es-app/tests/unit can hold the rule.

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
		return request;
	}
}
