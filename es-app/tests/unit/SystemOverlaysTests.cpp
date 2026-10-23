// The es_systems_*.cfg overlays (pixelelated/distribution#649, PL-004):
// es-app/src/SystemOverlays.h. Which files are read and in what order, and
// that a file which cannot be read is named and passed over rather than
// ending the read. The XML merge itself stays in SystemData.
#include "doctest/doctest.h"
#include "SystemOverlays.h"

#include <map>
#include <string>
#include <vector>

using SystemOverlays::Read;

namespace
{
	const std::string USER = "/storage/.config/emulationstation", IMAGE = "/usr/share/emulationstation";
}

TEST_CASE("system overlays: the image's first, then the player's, each in name order (PL-004)")
{
	const std::vector<std::string> user = { USER + "/es_systems_zz.cfg", USER + "/es_systems_aa.cfg", USER + "/notes.txt", USER + "/es_settings.cfg" };
	const std::vector<std::string> image = { IMAGE + "/es_systems_b.cfg", IMAGE + "/es_systems_a.cfg" };
	CHECK(SystemOverlays::order(USER, user, IMAGE, image) == std::vector<std::string>{
		IMAGE + "/es_systems_a.cfg", IMAGE + "/es_systems_b.cfg", USER + "/es_systems_aa.cfg", USER + "/es_systems_zz.cfg" });
}

TEST_CASE("system overlays: one folder for both is read once")
{
	const std::vector<std::string> files = { USER + "/es_systems_b.cfg", USER + "/es_systems_a.cfg" };
	CHECK(SystemOverlays::order(USER, files, USER, files) == std::vector<std::string>{ USER + "/es_systems_a.cfg", USER + "/es_systems_b.cfg" });
}

TEST_CASE("system overlays: a file that cannot be read is named, and the next is still read (PL-004)")
{
	const std::vector<std::string> files = { "/a/es_systems_aa-broken.cfg", "/a/es_systems_bb-empty.cfg", "/a/es_systems_zz-rename.cfg" };
	const std::map<std::string, Read> reads = { { files[0], Read::Unparsable }, { files[1], Read::NoSystemList }, { files[2], Read::Ok } };
	std::vector<std::string> applied;
	const auto refused = SystemOverlays::readEach(files, [&](const std::string& f)
	{
		const Read r = reads.at(f);
		if (r == Read::Ok)
			applied.push_back(f);
		return r;
	});
	CHECK(applied == std::vector<std::string>{ files[2] });
	REQUIRE(refused.size() == 2);
	CHECK(refused[0].first == files[0]);
	CHECK(refused[0].second == Read::Unparsable);
	CHECK(refused[1].first == files[1]);
	CHECK(refused[1].second == Read::NoSystemList);
}
