// SPDX-License-Identifier: MIT
// Copyright (c) 2026 pixelelated contributors
#pragma once

// GameCube RVZ only; unsupported or malformed data returns false.
// No global rcheevos callback changes and no temporary extracted image.
bool generateGameCubeRvzHash(char hash[33], const char* path);
