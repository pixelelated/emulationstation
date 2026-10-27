// The LED settings' text (T21 S11, #668, #1203): led.colour and the
// brightness base, read without a device. The pin threw out of the
// settings page's build on a word, and read 255,0,165 as white.

#include "doctest/doctest.h"

#include "LedText.h"

namespace
{
	const LedText::Rgb kDefault = { 255, 0, 165 };

	bool same(const LedText::Rgb& a, int r, int g, int b)
	{
		return a.r == r && a.g == g && a.b == b;
	}
}

TEST_CASE("parseColour: three numbers in range, or the default, and never a throw (T21 PL-15, #1203)")
{
	CHECK(same(LedText::parseColour("255 0 165", kDefault), 255, 0, 165));
	CHECK(same(LedText::parseColour("0 128 255", kDefault), 0, 128, 255));
	CHECK(same(LedText::parseColour("255,0,165", kDefault), 255, 0, 165));
	CHECK_NOTHROW(LedText::parseColour("blue", kDefault));
	CHECK(same(LedText::parseColour("blue", kDefault), 255, 0, 165));
	CHECK(same(LedText::parseColour("", kDefault), 255, 0, 165));
	CHECK(same(LedText::parseColour("255 0", kDefault), 255, 0, 165));
	CHECK(same(LedText::parseColour("300 0 0", kDefault), 255, 0, 165));
	CHECK(same(LedText::parseColour("-1 0 0", kDefault), 255, 0, 165));
	CHECK(same(LedText::parseColour("255 0 165 9", kDefault), 255, 0, 165));
	CHECK(same(LedText::parseColour("12x 0 0", kDefault), 255, 0, 165));
}

TEST_CASE("parseBaseColour: R G B or RRGGBB, or the default, and never a throw (T21 PL-15, #1203)")
{
	const LedText::Rgb white = { 255, 255, 255 };
	CHECK(same(LedText::parseBaseColour("ff00a5", white), 255, 0, 165));
	CHECK(same(LedText::parseBaseColour("255 0 165", white), 255, 0, 165));
	CHECK_NOTHROW(LedText::parseBaseColour("zzzzzz", white));
	CHECK(same(LedText::parseBaseColour("zzzzzz", white), 255, 255, 255));
	CHECK(same(LedText::parseBaseColour("ff00", white), 255, 255, 255));
}
