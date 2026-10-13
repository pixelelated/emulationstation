#pragma once
// SPDX-License-Identifier: MIT
// #574: narrow recovery of failed frontend RVZ hashes, not metadata cleanup.
#include <string>

namespace GameCubeRvzHashRetry
{
    inline std::string trim(const std::string& value)
    {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return {};
        return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
    }

    inline bool needed(bool readerEnabled, bool gameCubeOnly,
        const std::string& extension, const std::string& storedHash, const std::string& storedId)
    {
        if (!readerEnabled || !gameCubeOnly || extension.size() != 4 || extension[0] != '.' ||
            (extension[1] != 'r' && extension[1] != 'R') ||
            (extension[2] != 'v' && extension[2] != 'V') ||
            (extension[3] != 'z' && extension[3] != 'Z'))
            return false;

        // Only an absent ID or the existing no-ID marker is eligible. Preserve
        // positive IDs and unfamiliar nonempty metadata, even beside a bad hash.
        const std::string id = trim(storedId);
        if (!id.empty() && id != "0")
            return false;

        const std::string hash = trim(storedHash);
        if (hash.size() != 32)
            return true;
        bool nonzero = false;
        for (char c : hash)
        {
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
                return true;
            nonzero = nonzero || c != '0';
        }
        // A valid unknown hash still takes the old lookup-only path. The all-
        // zero value is getCheevosHashFromFile's historical failure sentinel.
        return !nonzero;
    }
}
