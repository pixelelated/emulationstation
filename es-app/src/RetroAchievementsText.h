#pragma once

// RetroAchievements' requests and answers as pure code (#1171): what the
// sign-in sends and where, with nothing from the network, the window or the
// settings behind it, so es-app/tests/unit can hold the rule.

#include <cctype>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>

#include <rapidjson/document.h>

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

	// The service's answers, read (#1180). The sign-in's: readable when it
	// is JSON with a Success member; success with a token; refused when
	// Success is false, with the service's Error.
	struct SignInAnswer
	{
		bool readable = false;
		bool success = false;
		bool refused = false;
		std::string token;
		std::string error;
	};

	inline SignInAnswer readSignInAnswer(const std::string& json)
	{
		// Every member by its type, so an answer of another shape is refused
		// rather than asserted on (#1180); and a success needs a token.
		SignInAnswer answer;
		rapidjson::Document doc;
		doc.Parse(json.c_str());
		if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("Success") || !doc["Success"].IsBool())
			return answer;
		answer.readable = true;
		if (doc["Success"].GetBool())
		{
			if (doc.HasMember("Token") && doc["Token"].IsString() && doc["Token"].GetStringLength() > 0)
			{
				answer.success = true;
				answer.token = doc["Token"].GetString();
			}
			return answer;
		}
		answer.refused = true;
		if (doc.HasMember("Error") && doc["Error"].IsString())
			answer.error = doc["Error"].GetString();
		return answer;
	}

	// r=officialgameslist: game id to title. ok is false for an answer that
	// is not the list.
	inline std::map<int, std::string> readOfficialGames(const std::string& json, bool& ok)
	{
		std::map<int, std::string> games;
		ok = false;
		rapidjson::Document doc;
		doc.Parse(json.c_str());
		if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("Response") || !doc["Response"].IsObject())
			return games;
		const rapidjson::Value& response = doc["Response"];
		for (auto it = response.MemberBegin(); it != response.MemberEnd(); ++it)
		{
			const int id = (int) strtol(it->name.GetString(), nullptr, 10);
			if (it->value.GetType() == rapidjson::Type::kStringType)
				games[id] = it->value.GetString();
			else if (it->value.IsInt())
				games[id] = std::to_string(it->value.GetInt());
		}
		ok = true;
		return games;
	}

	// r=hashlibrary: each hash, upper case, to its game's id, for the games
	// the official list holds.
	inline std::map<std::string, std::string> readHashLibrary(const std::string& json, const std::map<int, std::string>& official, bool& ok)
	{
		std::map<std::string, std::string> hashes;
		ok = false;
		rapidjson::Document doc;
		doc.Parse(json.c_str());
		if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("MD5List") || !doc["MD5List"].IsObject())
			return hashes;
		const rapidjson::Value& list = doc["MD5List"];
		for (auto it = list.MemberBegin(); it != list.MemberEnd(); ++it)
		{
			std::string name = it->name.GetString();
			for (auto& c : name)
				c = (char) toupper((unsigned char) c);
			if (!it->value.IsInt())
				continue;
			const int id = it->value.GetInt();
			if (official.find(id) == official.cend())
				continue;
			hashes[name] = std::to_string(id);
		}
		ok = true;
		return hashes;
	}
}
