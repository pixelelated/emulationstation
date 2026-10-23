// What DELETE GAME may remove (pixelelated/distribution#649, PL-002):
// es-app/src/GameFiles.h. A playlist, a cue sheet or a gdi lists the files
// a game is made of, and the game's metadata names its art; each is removed
// only when it lies inside the game's system folder. These cases build the
// paths as strings, so the rule is checked without a filesystem.
#include "doctest/doctest.h"
#include "GameFiles.h"

#include <string>

namespace
{
	const std::string FOLDER = "/storage/roms/psx";
}

TEST_CASE("delete scope: a file in the system folder or below it is inside")
{
	CHECK(GameFiles::isInside(FOLDER, "/storage/roms/psx/Game (Disc 1).bin"));
	CHECK(GameFiles::isInside(FOLDER, "/storage/roms/psx/images/Game-image.png"));
	CHECK(GameFiles::isInside(FOLDER + "/", "/storage/roms/psx/sub/./Game.bin"));
}

TEST_CASE("delete scope: a path that leaves the folder is outside (PL-002)")
{
	// An absolute path elsewhere, and a relative one climbing out.
	CHECK_FALSE(GameFiles::isInside(FOLDER, "/storage/outside.bin"));
	CHECK_FALSE(GameFiles::isInside(FOLDER, "/storage/roms/psx/../outside.bin"));
	CHECK_FALSE(GameFiles::isInside(FOLDER, "/storage/roms/psx/sub/../../outside.bin"));
	// A sibling folder whose name begins with the folder's.
	CHECK_FALSE(GameFiles::isInside(FOLDER, "/storage/roms/psx2/Game.bin"));
	// The folder itself, and a path that is not absolute.
	CHECK_FALSE(GameFiles::isInside(FOLDER, "/storage/roms/psx"));
	CHECK_FALSE(GameFiles::isInside(FOLDER, "/storage/roms/psx/sub/.."));
	CHECK_FALSE(GameFiles::isInside(FOLDER, "Game.bin"));
	CHECK_FALSE(GameFiles::isInside("", "/storage/roms/psx/Game.bin"));
}

TEST_CASE("delete scope: a cue sheet's FILE line after a byte-order mark or indentation (PL-002)")
{
	const std::string bom = "\xEF\xBB\xBF";
	CHECK(GameFiles::cueLine(bom + "FILE \"Game.bin\" BINARY") == "FILE \"Game.bin\" BINARY");
	CHECK(GameFiles::cueLine("  \tFILE \"Game.bin\" BINARY") == "FILE \"Game.bin\" BINARY");
	CHECK(GameFiles::cueLine("FILE \"Game.bin\" BINARY") == "FILE \"Game.bin\" BINARY");
	CHECK(GameFiles::cueLine("  TRACK 01 MODE2/2352") == "TRACK 01 MODE2/2352");
}
