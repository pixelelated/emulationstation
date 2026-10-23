#pragma once
#ifndef ES_APP_SORT_ID_H
#define ES_APP_SORT_ID_H

#include <cstddef>

// A system's stored sort id, bounded before it picks one of the sort types
// (pixelelated/distribution#649, PL-005). The id is read from
// es_settings.cfg, which a build with fewer sort types than the one before
// leaves holding a value past the end; the check was `id > count`, so an id
// equal to the count reached std::vector::at and ended the interface.
// Pure, so es-unit-tests holds it (SortIdTests.cpp).
namespace SortId
{
	// `id` when it names one of `count` sort types, else 0, the default.
	inline unsigned int bounded(long long id, size_t count)
	{
		return id >= 0 && (unsigned long long) id < count ? (unsigned int) id : 0;
	}
}

#endif // ES_APP_SORT_ID_H
