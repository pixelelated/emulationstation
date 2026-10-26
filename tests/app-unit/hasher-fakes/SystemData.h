#pragma once
// Double for es-app/src/SystemData.h, for hasher-tests: one plain game
// system whose games the test sets.
#include <set>
#include <string>
#include <vector>
#include "FileData.h"
#include "PlatformId.h"
class SystemData
{
public:
	static std::vector<SystemData*> sSystemVector;
	bool isGroupSystem() const { return false; }
	bool isCollection() const { return false; }
	bool isNetplaySupported() const { return false; }
	bool isCheevosSupported() const { return true; }
	bool isGameSystem() const { return true; }
	bool isGroupChildSystem() const { return false; }
	bool isHidden() const { return false; }
	bool isVisible() const { return true; }
	std::string getName() const { return "gb"; }
	FolderData* getRootFolder() { return &mRoot; }
	const std::set<PlatformIds::PlatformId>& getPlatformIds() const { return mPlatforms; }
	FolderData mRoot;
	std::set<PlatformIds::PlatformId> mPlatforms{ PlatformIds::GAME_BOY };
};
