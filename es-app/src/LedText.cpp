#include "LedText.h"

#include <cctype>
#include <string>
#include <vector>

namespace LedText
{
	// Whole decimal numbers 0..255 separated by spaces or commas, exactly
	// three of them; false for anything else (T21 PL-15, #1203). std::stoi
	// threw on a word out of the settings page's build, and read
	// "255,0,165" as 255 three times.
	static bool threeNumbers(const std::string& value, Rgb& out)
	{
		std::vector<int> numbers;
		size_t i = 0;
		while (i < value.size())
		{
			while (i < value.size() && (value[i] == ' ' || value[i] == ',' || value[i] == '\t' || value[i] == '\r' || value[i] == '\n'))
				i++;
			if (i >= value.size())
				break;
			size_t start = i;
			while (i < value.size() && std::isdigit((unsigned char) value[i]))
				i++;
			if (i == start || i - start > 3)
				return false;
			if (i < value.size() && value[i] != ' ' && value[i] != ',' && value[i] != '\t' && value[i] != '\r' && value[i] != '\n')
				return false;
			const int n = std::stoi(value.substr(start, i - start));
			if (n > 255)
				return false;
			numbers.push_back(n);
		}
		if (numbers.size() != 3)
			return false;
		out = { numbers[0], numbers[1], numbers[2] };
		return true;
	}

	static int hexDigit(char c)
	{
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'a' && c <= 'f') return c - 'a' + 10;
		if (c >= 'A' && c <= 'F') return c - 'A' + 10;
		return -1;
	}

	Rgb parseColour(const std::string& value, Rgb fallback)
	{
		Rgb out;
		return threeNumbers(value, out) ? out : fallback;
	}

	Rgb parseBaseColour(const std::string& value, Rgb fallback)
	{
		Rgb out;
		if (threeNumbers(value, out))
			return out;
		if (value.size() == 6)
		{
			int digits[6];
			for (int i = 0; i < 6; i++)
				if ((digits[i] = hexDigit(value[i])) < 0)
					return fallback;
			return { digits[0] * 16 + digits[1], digits[2] * 16 + digits[3], digits[4] * 16 + digits[5] };
		}
		return fallback;
	}
}
