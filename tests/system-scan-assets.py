"""The folder scan makes no FileData it does not keep (pixelelated/
distribution#649, PL-036).

On an arcade system, populateFolder made a FileData for every file with a
valid extension and only then asked whether it was an arcade asset (a BIOS
or a device file): an asset was neither attached nor freed, one leak per
asset per scan -- at every start and every UPDATE GAMELISTS. This compiles
the shipped populateFolder out of SystemData.cpp with doubles for the file
system, the settings and FileData (which counts the objects alive), scans a
fixture folder holding a game and a BIOS file beside it, frees the tree, and
checks that no FileData is left alive, that the game is listed and that the
asset is not.

    python3 tests/system-scan-assets.py [path/to/SystemData.cpp]
"""
from pathlib import Path
import subprocess
import sys
import tempfile

source_path = (Path(sys.argv[1]) if len(sys.argv) > 1 else
               Path(__file__).resolve().parents[1] / "es-app/src/SystemData.cpp")
source = source_path.read_text()


def definition(signature):
    """The body of the first definition (not declaration) of `signature`."""
    if signature not in source:
        raise SystemExit(f"{signature} not found in {source_path}")
    start = source.index(signature)
    while source.index(';', start) < source.index('{', start):
        start = source.index(signature, start + 1)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        if source[end] == '{':
            depth += 1
        elif source[end] == '}':
            depth -= 1
        end += 1
    return source[start:end]


scan = definition("void SystemData::populateFolder(")
# SystemFolders.h is pure; the tree's copy is used when the scan calls it.
folders_h = source_path.parent / "SystemFolders.h"
include_folders = ('#include "' + str(folders_h) + '"\n') if "SystemFolders::" in scan else ""

harness = r'''
#include <algorithm>
#include <cctype>
#include <iostream>
#include <list>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
''' + include_folders + r'''
enum LogLevel { LogError, LogWarning, LogInfo };
struct LogLine { std::ostringstream o; template <class T> LogLine& operator<<(const T& v) { o << v; return *this; } ~LogLine() { std::cout << "  log: " << o.str() << "\n"; } };
#define LOG(level) LogLine()

// The fixture: a folder of an arcade system holding a game and a BIOS file.
static std::map<std::string, std::vector<std::pair<std::string, bool>>> gTree = {
    { "/storage/roms/neogeo", { { "/storage/roms/neogeo/mslug.zip", false }, { "/storage/roms/neogeo/neogeo.zip", false } } },
};
static std::set<std::string> gBios = { "neogeo" };

namespace Utils {
namespace String {
    inline std::string toLower(std::string s) { for (auto& c : s) c = (char) std::tolower((unsigned char) c); return s; }
    inline bool startsWith(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }
}
namespace FileSystem {
    struct FileInfo { std::string path; bool hidden = false; bool directory = false; };
    typedef std::list<FileInfo> fileList;
    inline bool isDirectory(const std::string& p) { return gTree.count(p) > 0; }
    inline fileList getDirectoryFiles(const std::string& p) {
        fileList out; for (auto& e : gTree[p]) { FileInfo f; f.path = e.first; f.directory = e.second; out.push_back(f); } return out; }
    inline std::list<std::string> getDirContent(const std::string&, bool = false, bool = false) { return {}; }
    inline std::string getFileName(const std::string& p) { auto i = p.find_last_of('/'); return i == std::string::npos ? p : p.substr(i + 1); }
    inline std::string getExtension(const std::string& p) { auto n = getFileName(p); auto i = n.find_last_of('.'); return i == std::string::npos ? "" : n.substr(i); }
    inline std::string getStem(const std::string& p) { auto n = getFileName(p); auto i = n.find_last_of('.'); return i == std::string::npos ? n : n.substr(0, i); }
}
}

struct Settings {
    static bool ShowHiddenFiles() { return false; }
    static bool PreloadMedias() { return false; }
    static bool HiddenSystemsShowGames() { return true; }
    static Settings* getInstance() { static Settings s; return &s; }
    std::string getString(const std::string&) { return ""; }
};

enum FileType { GAME = 1, FOLDER = 2 };
class SystemData;
static int gLive = 0;   // FileData objects alive
class FileData {
public:
    FileData(FileType t, const std::string& p, SystemData*) : mType(t), mPath(p) { gLive++; }
    virtual ~FileData() { gLive--; }
    const std::string& getPath() const { return mPath; }
    FileType getType() const { return mType; }
    // The pin asks the object; the fix asks before there is one.
    bool isArcadeAsset() { return gBios.count(Utils::FileSystem::getStem(mPath)) > 0; }
    static bool isArcadeAssetPath(SystemData*, const std::string& p) { return gBios.count(Utils::FileSystem::getStem(p)) > 0; }
private:
    FileType mType; std::string mPath;
};
class FolderData : public FileData {
public:
    FolderData(const std::string& p, SystemData* s) : FileData(FOLDER, p, s) {}
    ~FolderData() { for (auto* c : mChildren) delete c; }
    void addChild(FileData* f) { mChildren.push_back(f); }
    const std::vector<FileData*>& getChildren() const { return mChildren; }
private:
    std::vector<FileData*> mChildren;
};

struct SystemEnvironmentData {
    std::string mStartPath = "/storage/roms/neogeo";
    bool isValidExtension(const std::string& e) const { return e == ".zip"; }
};
struct SystemMetadata { std::string name = "neogeo"; };
class SystemData {
public:
    SystemEnvironmentData* mEnvData;
    SystemMetadata mMetadata;
    bool mHidden = false;
    std::vector<std::string>* mCollectFolders = nullptr;
    std::string getName() const { return mMetadata.name; }
    void populateFolder(FolderData* folder, std::unordered_map<std::string, FileData*>& fileMap);
};

''' + scan + r'''

int main()
{
    std::cout << std::unitbuf;
    int failures = 0;
    auto check = [&failures](bool ok, const std::string& what) { std::cout << (ok ? "ok   " : "FAIL ") << what << "\n"; if (!ok) failures++; };

    SystemEnvironmentData env;
    SystemData system;
    system.mEnvData = &env;
    FolderData* root = new FolderData(env.mStartPath, &system);
    std::unordered_map<std::string, FileData*> fileMap;
    fileMap[env.mStartPath] = root;
    system.populateFolder(root, fileMap);

    check(fileMap.count("/storage/roms/neogeo/mslug.zip") == 1, "the game is listed");
    check(fileMap.count("/storage/roms/neogeo/neogeo.zip") == 0, "the BIOS file is not listed");
    delete root;
    check(gLive == 0, "no FileData is left alive once the scanned tree is freed (" + std::to_string(gLive) + " left)");

    std::cout << "system-scan-assets: " << (failures ? "FAIL" : "PASS") << "\n";
    return failures ? 1 : 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    cpp = tmp / "harness.cpp"
    exe = tmp / "harness"
    cpp.write_text(harness)
    subprocess.run(["g++", "-std=c++17", "-o", str(exe), str(cpp)], check=True)
    sys.exit(subprocess.run([str(exe)]).returncode)
