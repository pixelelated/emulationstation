#pragma once
#ifndef ES_APP_LED_TEXT_H
#define ES_APP_LED_TEXT_H

#include <string>

// The LED settings' text as values, without a device (T21 PL-15, #1203):
// led.colour and the brightness base, read by ApiSystem.
namespace LedText
{
	struct Rgb
	{
		int r, g, b;
	};

	// "R G B" -- spaces, or commas a hand-written setting may carry --
	// three whole numbers from 0 to 255. Anything else is `fallback`, and
	// nothing throws.
	Rgb parseColour(const std::string& value, Rgb fallback);

	// The brightness base: "R G B" as parseColour reads it, or "RRGGBB" in
	// hex. Anything else is `fallback`.
	Rgb parseBaseColour(const std::string& value, Rgb fallback);
}

#endif // ES_APP_LED_TEXT_H
