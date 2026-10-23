// Which sub-folders a system's scan leaves alone (pixelelated/distribution
// #649, PL-003): es-app/src/SystemFolders.h. The scan skipped a sub-folder
// whose path held the letters of "ports" or "windows" anywhere, so a gb
// folder named Sports was never read. The rule is the two systems'.
#include "doctest/doctest.h"
#include "SystemFolders.h"

TEST_CASE("system folders: the ports and windows systems skip their sub-folders")
{
	CHECK(SystemFolders::skipsSubfolder("ports", "/storage/roms/ports", "/storage/roms/ports/Doom"));
	CHECK(SystemFolders::skipsSubfolder("windows", "/storage/roms/windows", "/storage/roms/windows/Game"));
	// A system whose folder is named so, whatever the system is called.
	CHECK(SystemFolders::skipsSubfolder("ports2", "/storage/roms/ports", "/storage/roms/ports/Doom"));
}

TEST_CASE("system folders: the letters inside a longer name do not skip a folder (PL-003)")
{
	CHECK_FALSE(SystemFolders::skipsSubfolder("gb", "/storage/roms/gb", "/storage/roms/gb/Sports"));
	CHECK_FALSE(SystemFolders::skipsSubfolder("pc", "/storage/roms/pc", "/storage/roms/pc/windows95"));
	CHECK_FALSE(SystemFolders::skipsSubfolder("gb", "/storage/roms/gb", "/storage/roms/gb/Airports/Deep"));
	// A sub-folder named ports in another system is that system's folder.
	CHECK_FALSE(SystemFolders::skipsSubfolder("gb", "/storage/roms/gb", "/storage/roms/gb/ports"));
	// A start path that holds the letters in a longer name.
	CHECK_FALSE(SystemFolders::skipsSubfolder("sports", "/storage/roms/sports", "/storage/roms/sports/Game"));
}
