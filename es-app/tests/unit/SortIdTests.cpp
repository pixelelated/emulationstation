// A system's stored sort id, bounded before it picks a sort
// (pixelelated/distribution#649, PL-005): es-app/src/SortId.h. The id comes
// from es_settings.cfg, and a value equal to the number of sort types
// passed the old check and ended the interface at std::vector::at.
#include "doctest/doctest.h"
#include "SortId.h"

TEST_CASE("sort id: a valid id is kept")
{
	CHECK(SortId::bounded(0, 28) == 0);
	CHECK(SortId::bounded(27, 28) == 27);
}

TEST_CASE("sort id: an id at or past the number of sort types is the default (PL-005)")
{
	CHECK(SortId::bounded(28, 28) == 0);
	CHECK(SortId::bounded(29, 28) == 0);
	CHECK(SortId::bounded(-1, 28) == 0);
	CHECK(SortId::bounded(3, 0) == 0);
}
