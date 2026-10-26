#pragma once
// Double for es-core/src/Settings.h: strings in a map; saveFile waits as
// long as the test says, the window in which the hasher's own threads run.
#include <string>
class Settings
{
public:
	static Settings* getInstance();
	std::string getString(const std::string& name);
	bool setString(const std::string& name, const std::string& value);
	bool saveFile();
};
