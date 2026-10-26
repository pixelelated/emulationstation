#pragma once
// Double for es-app/src/RetroAchievements.h: the hash library the test sets.
#include <map>
#include <string>
class RetroAchievements
{
public:
	static std::map<std::string, std::string> getCheevosHashes();
};
