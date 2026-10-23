"""A launch deferred behind a gate carries its save state by file
(#308 8-es-menus-and-core claude F-ES-11).

STOP IT AND PLAY, PLAY NOW and the capture's wait relaunch seconds later
with a copy of the launch's options. A save state chosen in the manager is
one of its repository's objects, and the repository's refresh -- the
manager's jobs call it as they land -- deletes every one of them. This
compiles the shipped launchNow out of FileData.cpp with doubles for the
repository, the view controller and the window; chooses a state; lets the
repository refresh (the chosen object deleted, an equal one made); runs the
deferred launch; and checks which object the launch was handed.

The game itself is carried the same way since PL-008
(pixelelated/distribution#649): a game-list reload during the wait frees
every FileData, so a launch that held the game's pointer ran on a freed
one. The last case frees the game and makes an equal one, as a reload
does, and checks which object the deferred launch found; then removes it
and checks that nothing is launched. The harness follows the source's
shape -- launchNow by pointer (the pin) or by LaunchGameRef -- so it
fails on the pin and passes on the fix.

    python3 tests/launch-deferred-state.py [path/to/FileData.cpp]
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

source_path = (Path(sys.argv[1]) if len(sys.argv) > 1 else
               Path(__file__).resolve().parents[1] / "es-app/src/FileData.cpp")
source = source_path.read_text()


def definition(signature, required=True):
    """The body of the first definition (not declaration) of `signature`."""
    if signature not in source:
        if required:
            raise SystemExit(f"{signature} not found in {source_path}")
        return ""
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


# The options' fields as FileData.h declares them: the harness mirrors the
# ones launchNow can read, plus saveStateFile when the tree has it.
has_file_field = re.search(r'\bstd::string\s+saveStateFile\s*;', (source_path.parent / "FileData.h").read_text()) is not None
# The game by reference (PL-008), or by pointer as at the pin.
by_ref = "static void launchNow(Window* window, const LaunchGameRef&" in source
LAUNCH_NOW = "launchNow(w, FileData::launchRef(g), o)" if by_ref else "launchNow(w, g, o)"
REF_TAKEN = "        const LaunchGameRef ref = FileData::launchRef(before);\n" if by_ref else ""
CALL_1 = "        launchNow(&window, ref, o);\n" if by_ref else "        launchNow(&window, before, o);\n"
CALL_2 = "        launchNow(&window, ref, o);\n" if by_ref else "        launchNow(&window, after, o);\n"

harness = r'''
#include <algorithm>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
enum LogLevel { LogError, LogWarning, LogInfo };
struct LogLine { std::ostringstream o; template <class T> LogLine& operator<<(const T& v) { o << v; return *this; } ~LogLine() { std::cout << "  log: " << o.str() << "\n"; } };
#define LOG(level) LogLine()

struct SaveState { std::string fileName; };
struct LaunchGameOptions {
    SaveState* saveStateInfo = nullptr;
''' + ("    std::string saveStateFile;\n" if has_file_field else "") + r'''
};
struct FileData;
struct SaveStateRepository {
    std::vector<std::unique_ptr<SaveState>> states;
    static bool isEnabled(FileData*) { return true; }
    std::vector<SaveState*> getSaveStates(FileData*, std::nullptr_t = nullptr) {
        std::vector<SaveState*> v; for (auto& s : states) v.push_back(s.get()); return v; }
    void refresh() {   // every object deleted, the same files read again
        std::vector<std::string> files; for (auto& s : states) files.push_back(s->fileName);
        states.clear();
        for (auto& f : files) states.emplace_back(new SaveState{ f }); }
    static SaveState* getEmptySaveState() { static SaveState e{ "" }; return &e; }
    SaveState* getDefaultAutoSaveSaveState() { static SaveState a{ "auto" }; return &a; }
    SaveState* getDefaultNewGameSaveState() { static SaveState n{ "new" }; return &n; }
};
struct SystemData { SaveStateRepository repo; std::string name = "snes"; SaveStateRepository* getSaveStateRepository() { return &repo; } };
struct LaunchGameRef { std::string system; std::string path; };
struct FileData;
static std::vector<FileData*> gGames;   // the games the systems hold now
struct FileData {
    SystemData* system; std::string path = "/storage/roms/snes/game.sfc";
    FileData* getSourceFileData() { return this; } SystemData* getSystem() { return system; }
    static LaunchGameRef launchRef(FileData* g) { return { g->system->name, g->path }; }
    static FileData* findLaunchRef(const LaunchGameRef& r) {
        for (auto* g : gGames) if (g->system->name == r.system && g->path == r.path) return g;
        return nullptr; }
};
struct ViewController {
    SaveState* launchedWith = reinterpret_cast<SaveState*>(1);
    FileData* launchedGame = nullptr;
    int launches = 0;
    static ViewController* get() { static ViewController v; return &v; }
    void launch(FileData* g, const LaunchGameOptions& o) { launchedWith = o.saveStateInfo; launchedGame = g; launches++; }
};
struct Window {
    std::vector<std::function<void()>> posted;
    void postToUiThread(const std::function<void()>& f) { posted.push_back(f); }
};
''' + definition('static void rememberSaveState(', required=False) + '\n' + definition('static void findSaveStateAgain(', required=False) + '\n' + definition('static void launchNow(') + '\n#define LAUNCH_NOW(w, g, o) ' + LAUNCH_NOW + r'''
int main()
{
    std::cout << std::unitbuf;
    SystemData system;
    system.repo.states.emplace_back(new SaveState{ "/storage/savestates/snes/game.state1" });
    system.repo.states.emplace_back(new SaveState{ "/storage/savestates/snes/game.state2" });
    FileData game{ &system };
    gGames.push_back(&game);
    Window window;
    int failures = 0;

    LaunchGameOptions options;
    options.saveStateInfo = system.repo.states[1].get();   // the player picks slot 2
''' + ("    rememberSaveState(&game, options);   // launchGame's entry\n" if "static void rememberSaveState(" in source else "") + r'''
    // The gate asks; while the player answers, a manager job lands.
    system.repo.refresh();
    LAUNCH_NOW(&window, &game, options);
    for (auto& f : window.posted) f();
    SaveState* launched = ViewController::get()->launchedWith;
    if (launched == system.repo.states[1].get())
        std::cout << "ok   the deferred launch was handed the state the repository holds now\n";
    else { std::cout << "FAIL the deferred launch was handed an object the refresh deleted\n"; failures++; }

    // A state that is gone by then is not handed over at all.
    ViewController::get()->launchedWith = reinterpret_cast<SaveState*>(1);
    window.posted.clear();
    LaunchGameOptions again;
    again.saveStateInfo = system.repo.states[0].get();
''' + ("    rememberSaveState(&game, again);\n" if "static void rememberSaveState(" in source else "") + r'''
    system.repo.states.erase(system.repo.states.begin());   // slot 1 deleted by a job
    LAUNCH_NOW(&window, &game, again);
    for (auto& f : window.posted) f();
    if (ViewController::get()->launchedWith == nullptr)
        std::cout << "ok   a state deleted meanwhile is not handed over\n";
    else { std::cout << "FAIL a state deleted meanwhile was handed over\n"; failures++; }

    // The shared states are never deleted and go as they are.
    ViewController::get()->launchedWith = reinterpret_cast<SaveState*>(1);
    window.posted.clear();
    LaunchGameOptions fresh;
    fresh.saveStateInfo = system.repo.getDefaultNewGameSaveState();
''' + ("    rememberSaveState(&game, fresh);\n" if "static void rememberSaveState(" in source else "") + r'''
    system.repo.refresh();
    LAUNCH_NOW(&window, &game, fresh);
    for (auto& f : window.posted) f();
    if (ViewController::get()->launchedWith == system.repo.getDefaultNewGameSaveState())
        std::cout << "ok   the new-game state goes as it is\n";
    else { std::cout << "FAIL the new-game state was not handed over\n"; failures++; }

    // PL-008: the game itself freed and made again during the wait, as a
    // game-list reload does. The reference is taken when the gate asks.
    {
        FileData* before = new FileData{ &system };
        gGames = { before };
        LaunchGameOptions o;
''' + REF_TAKEN + r'''
        // The reload: the old object goes (its memory kept, so the check
        // compares the pointer without reading through it), an equal one comes.
        FileData* after = new FileData{ &system };
        gGames = { after };
        window.posted.clear();
        ViewController::get()->launches = 0;
''' + CALL_1 + r'''
        for (auto& f : window.posted) f();
        if (ViewController::get()->launches == 1 && ViewController::get()->launchedGame == after)
            std::cout << "ok   the deferred launch found the game again after a reload\n";
        else { std::cout << "FAIL the deferred launch was handed the game the reload freed\n"; failures++; }

        // Gone altogether by the time the launch runs: nothing is launched.
        gGames.clear();
        window.posted.clear();
        ViewController::get()->launches = 0;
''' + CALL_2 + r'''
        for (auto& f : window.posted) f();
        if (ViewController::get()->launches == 0)
            std::cout << "ok   a game gone during the wait is not launched\n";
        else { std::cout << "FAIL a game gone during the wait was launched\n"; failures++; }
        delete before;
        delete after;
    }

    std::cout << "launch-deferred-state: " << (failures ? "FAIL" : "PASS") << "\n";
    return failures ? 1 : 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    cpp = tmp / "harness.cpp"
    exe = tmp / "harness"
    cpp.write_text(harness)
    # Not under AddressSanitizer: the unfixed launchNow hands over a freed
    # pointer, and the check compares it rather than reading through it.
    subprocess.run(["g++", "-std=c++17", "-o", str(exe), str(cpp)], check=True)
    sys.exit(subprocess.run([str(exe)]).returncode)
