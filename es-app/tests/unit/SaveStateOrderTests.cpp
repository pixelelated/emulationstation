// The save state manager's incremental order (fork #1256): std::sort needs
// a strict weak ordering. `>=` on the creation date was true for a state
// against itself and for both of two states made in the same second, which
// lets std::sort read past the end of the range; and the order of two such
// states was whatever the sort left.

#include "doctest/doctest.h"

#include "SaveStateOrder.h"

#include <algorithm>
#include <vector>

namespace
{
	struct State { long date; int slot; };
	bool cmp(const State& a, const State& b) { return SaveStateOrder::newestFirst(a.date, a.slot, b.date, b.slot); }
}

TEST_CASE("save state order: no state comes before itself")
{
	const State a{ 1789000000, 3 };
	CHECK_FALSE(cmp(a, a));
}

TEST_CASE("save state order: two states of one second are ordered one way, by the slot")
{
	const State a{ 1789000000, 2 }, b{ 1789000000, 5 };
	CHECK(cmp(a, b));
	CHECK_FALSE(cmp(b, a));
}

TEST_CASE("save state order: the newer state first")
{
	const State older{ 1789000000, 1 }, newer{ 1789000060, 9 };
	CHECK(cmp(newer, older));
	CHECK_FALSE(cmp(older, newer));
}

TEST_CASE("save state order: a sort of states with equal dates is newest first, then by slot")
{
	std::vector<State> v;
	for (int i = 0; i < 40; i++)
		v.push_back({ 1789000000 + (i % 3) * 60, 40 - i });
	std::sort(v.begin(), v.end(), cmp);
	for (size_t i = 1; i < v.size(); i++)
	{
		CHECK(v[i - 1].date >= v[i].date);
		if (v[i - 1].date == v[i].date)
			CHECK(v[i - 1].slot < v[i].slot);
	}
}
