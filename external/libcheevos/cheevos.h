#pragma once

#include "rcheevos/include/rc_consoles.h"

bool generateHashFromFile(char hash[33], int console_id, const char* path);

// Build capability only: individual RVZ modes/inputs may still be rejected.
bool gameCubeRvzHashEnabled();
