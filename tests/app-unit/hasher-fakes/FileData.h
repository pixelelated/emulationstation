#pragma once
// Double for es-app/src/FileData.h, for hasher-tests: a game's metadata in
// a map, its system, and the two checks the hasher calls.
#include <map>
#include <string>
#include <vector>
#include "utils/FileSystemUtil.h"
class SystemData;
enum FileType { GAME = 1, FOLDER = 2 };
enum class MetaDataId { CheevosHash, CheevosId, Crc32 };
class FileData
{
public:
	FileData(SystemData* system, const std::string& path) : mSystem(system), mPath(path) {}
	std::string getMetadata(MetaDataId id) { auto it = mMeta.find(id); return it == mMeta.end() ? std::string() : it->second; }
	void setMetadata(MetaDataId id, const std::string& value) { mMeta[id] = value; }
	const std::string& getPath() const { return mPath; }
	std::string getName() const { return mPath; }
	std::string getSystemName() const;
	SystemData* getSystem() const { return mSystem; }
	void checkCrc32(bool force);
	void checkCheevosHash(bool force);
private:
	SystemData* mSystem;
	std::string mPath;
	std::map<MetaDataId, std::string> mMeta;
};
class FolderData
{
public:
	std::vector<FileData*> getFilesRecursive(unsigned int typeMask) const { (void) typeMask; return mFiles; }
	std::vector<FileData*> mFiles;
};
