// The BIOS CHECK page's summary (fork #1245): es-app/src/BiosText.h, pure.
// The page hands it the catalog's template for each count (ngettext); these
// hand it the English and the French forms as the catalog holds them.

#include "doctest/doctest.h"

#include "BiosText.h"

TEST_CASE("bios text: each %i takes the next value, and a template without one keeps its words")
{
	CHECK(BiosText::fill("%i OF %i MISSING", { 1, 7 }) == "1 OF 7 MISSING");
	CHECK(BiosText::fill("NOTHING TO COUNT", { 3 }) == "NOTHING TO COUNT");
	CHECK(BiosText::fill("%i AND %i", { 2 }) == "2 AND %i");
}

TEST_CASE("bios text: nothing missing or unknown reads ALL PRESENT with the total")
{
	CHECK(BiosText::summary(0, 0, 3, "%i OF %i MISSING", "%i NOT A KNOWN VERSION", "ALL PRESENT (%i)") == "ALL PRESENT (3)");
}

TEST_CASE("bios text: the missing count of the total, then the unknown count, in the template's own order")
{
	CHECK(BiosText::summary(1, 2, 4, "%i OF %i MISSING", "%i NOT KNOWN VERSIONS", "ALL PRESENT (%i)") == "1 OF 7 MISSING, 2 NOT KNOWN VERSIONS");
	CHECK(BiosText::summary(7, 0, 0, "%i SUR %i MANQUANTS", "%i DE VERSION INCONNUE", "TOUS PR\xC3\x89SENTS (%i)") == "7 SUR 7 MANQUANTS");
	CHECK(BiosText::summary(0, 2, 1, "%i SUR %i MANQUANTS", "%i DE VERSION INCONNUE", "TOUS PR\xC3\x89SENTS (%i)") == "2 DE VERSION INCONNUE");
}
