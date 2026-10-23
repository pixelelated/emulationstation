// A save state's copy, move and removal report what the file operations
// did (pixelelated/distribution#649, PL-016): es-app/src/SaveStateFiles.h.
// The operations are handed in, so each case makes one of them fail
// without a filesystem -- a named substitute for a read-only directory,
// which the guest walk gives (#782's VM line).
#include "doctest/doctest.h"
#include "SaveStateFiles.h"

#include <string>
#include <vector>

namespace
{
	struct Fake
	{
		std::vector<std::string> calls;
		std::string failing;   // the source or path whose operation fails

		SaveStateFiles::Ops ops()
		{
			SaveStateFiles::Ops o;
			o.move = [this](const std::string& a, const std::string& b) { calls.push_back("move " + a + " " + b); return a != failing; };
			o.copy = [this](const std::string& a, const std::string& b) { calls.push_back("copy " + a + " " + b); return a != failing; };
			o.remove = [this](const std::string& a) { calls.push_back("remove " + a); return a != failing; };
			return o;
		}
	};

	const std::string ST = "/s/game.state1", IM = "/s/game.state1.png", DST = "/s/game.state2", DIM = "/s/game.state2.png";
}

TEST_CASE("save state files: a copy that works reports it")
{
	Fake f;
	const auto r = SaveStateFiles::transfer(f.ops(), false, ST, IM, DST, DIM);
	CHECK(r.state);
	CHECK(r.image);
	CHECK(f.calls == std::vector<std::string>{ "copy " + ST + " " + DST, "copy " + IM + " " + DIM });
}

TEST_CASE("save state files: a copy of the state that fails reports it, and the image is not copied (PL-016)")
{
	Fake f;
	f.failing = ST;
	const auto r = SaveStateFiles::transfer(f.ops(), false, ST, IM, DST, DIM);
	CHECK_FALSE(r.state);
	CHECK(f.calls == std::vector<std::string>{ "copy " + ST + " " + DST });
}

TEST_CASE("save state files: a move whose image fails keeps the state's result and reports the image (PL-016)")
{
	Fake f;
	f.failing = IM;
	const auto r = SaveStateFiles::transfer(f.ops(), true, ST, IM, DST, DIM);
	CHECK(r.state);
	CHECK_FALSE(r.image);
	CHECK(f.calls == std::vector<std::string>{ "move " + ST + " " + DST, "move " + IM + " " + DIM });
}

TEST_CASE("save state files: a state without an image touches only the state")
{
	Fake f;
	const auto r = SaveStateFiles::transfer(f.ops(), true, ST, "", DST, DIM);
	CHECK(r.state);
	CHECK(r.image);
	CHECK(f.calls.size() == 1);
}

TEST_CASE("save state files: a removal that fails reports it (PL-016)")
{
	Fake f;
	f.failing = ST;
	const auto r = SaveStateFiles::removeFiles(f.ops(), ST, IM);
	CHECK_FALSE(r.state);
	CHECK(r.image);
	CHECK(f.calls == std::vector<std::string>{ "remove " + ST, "remove " + IM });
}
