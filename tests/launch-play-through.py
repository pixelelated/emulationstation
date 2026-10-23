"""PLAY NOW belongs to the launch it answered (pixelelated/distribution#649,
PL-018).

While the offline achievements' send runs, a launch is asked whether to
play through it. PLAY NOW relaunches the game a frame later, and the answer
has to reach that relaunch, or the question comes back without end. It was
kept in one process-wide flag, so another launch arriving first -- the HTTP
API's, a press on another game -- took the answer and skipped its own
question, and the answered launch was asked again.

This compiles the shipped gate code out of FileData.cpp with doubles for
the jobs, the dialog and the window: FileData::launchGatesClear where the
tree has it, else the head of FileData::launchGame up to the command (the
pin's shape). With the send running it asks for launch A, answers PLAY NOW,
lets a launch B arrive before A's relaunch runs, and checks that B is asked
and that A's relaunch is not.

    python3 tests/launch-play-through.py [path/to/FileData.cpp]
"""
from pathlib import Path
import subprocess
import sys
import tempfile

source_path = (Path(sys.argv[1]) if len(sys.argv) > 1 else
               Path(__file__).resolve().parents[1] / "es-app/src/FileData.cpp")
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


by_member = "bool FileData::launchGatesClear(" in source
by_ref = "static void launchNow(Window* window, const LaunchGameRef&" in source
if by_member:
    gates = definition("bool FileData::launchGatesClear(")
    GATES = "bool launchGatesClear(Window* window, LaunchGameOptions& options);"
    CALL = "g->launchGatesClear(&window, o)"
else:
    head = "bool FileData::launchGame(Window* window, LaunchGameOptions options)\n{"
    stop = "\tFileData* gameToUpdate = getSourceFileData();"
    start = source.index(head)
    gates = source[start:source.index(stop, start)] + "\treturn true;\n}\n"
    GATES = "bool launchGame(Window* window, LaunchGameOptions options);"
    CALL = "g->launchGame(&window, o)"

LAUNCH_NOW = ("static void launchNow(Window*, const LaunchGameRef&, const LaunchGameOptions& o) { gRelaunches.push_back(o); }\n"
              "static void launchWhenGone(Window*, const LaunchGameRef&, const LaunchGameOptions&, const std::function<bool()>&, const std::function<void()>&) {}\n"
              if by_ref else
              "static void launchNow(Window*, FileData*, const LaunchGameOptions& o) { gRelaunches.push_back(o); }\n"
              "static void launchWhenGone(Window*, FileData*, const LaunchGameOptions&, const std::function<bool()>&, const std::function<void()>&) {}\n")

harness = r'''
#include <atomic>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#define _(value) std::string(value)
enum LogLevel { LogError, LogWarning, LogInfo };
struct LogLine { std::ostringstream o; template <class T> LogLine& operator<<(const T& v) { o << v; return *this; } ~LogLine() { std::cout << "  log: " << o.str() << "\n"; } };
#define LOG(level) LogLine()

struct Gui { virtual ~Gui() {} };
struct Window { std::vector<std::unique_ptr<Gui>> stack; void pushGui(Gui* g) { stack.emplace_back(g); } };
struct GuiMsgBox : Gui {
    std::string text; std::function<void()> first;
    GuiMsgBox(Window*, const std::string& t, const std::string&, const std::function<void()>& f, const std::string&, const std::function<void()>&) : text(t), first(f) {}
    GuiMsgBox(Window*, const std::string& t) : text(t) {}
};
struct SaveState {};
struct LaunchGameOptions { SaveState* saveStateInfo = nullptr; std::string saveStateFile; bool gatesCleared = false; bool playThroughSend = false; };
struct LaunchGameRef { std::string system; std::string path; };
class FileData {
public:
    ''' + GATES + r'''
    static LaunchGameRef launchRef(FileData*) { return { "snes", "/storage/roms/snes/game.sfc" }; }
};
static std::vector<LaunchGameOptions> gRelaunches;
''' + LAUNCH_NOW + r'''
static std::atomic<bool> sPlayThroughSend{ false };
static void rememberSaveState(FileData*, LaunchGameOptions&) {}
static bool captureGate(Window*, FileData*, const LaunchGameOptions&) { return true; }
struct ThreadedCloudSync {
    enum class CancelRefusal { Stopping };
    static bool isRunning() { return false; }
    static bool cancelForLaunch(CancelRefusal*, bool) { return true; }
};
struct CloudTransferJob {
    bool finished() const { return true; }
    static std::shared_ptr<CloudTransferJob> current() { return nullptr; }
    static void stopForLaunch(bool) {}
    static bool running() { return false; }
};
struct GuiCloudTransfer { static std::string stillRunningSentence(const std::shared_ptr<CloudTransferJob>&) { return ""; } };
static bool gSending = true;
struct ProxyCards {
    static bool sendRunning() { return gSending; }
    static bool topUpRunning() { return false; }
    static void stopTopUp() {}
};

''' + gates + r'''

int main()
{
    std::cout << std::unitbuf;
    int failures = 0;
    auto check = [&failures](bool ok, const std::string& what) { std::cout << (ok ? "ok   " : "FAIL ") << what << "\n"; if (!ok) failures++; };
    Window window;
    FileData a, b;
    auto gate = [&window](FileData* g, LaunchGameOptions o) { return ''' + CALL + r'''; };
    auto asked = [&window]() { return dynamic_cast<GuiMsgBox*>(window.stack.empty() ? nullptr : window.stack.back().get()); };

    // Launch A meets the send's question; the player answers PLAY NOW.
    check(!gate(&a, LaunchGameOptions()) && asked() != nullptr, "launch A is asked about the send");
    GuiMsgBox* question = asked();
    question->first();
    check(gRelaunches.size() == 1, "PLAY NOW relaunches A");

    // Launch B arrives before A's relaunch runs, and is asked its own question.
    window.stack.clear();
    const bool bWent = gate(&b, LaunchGameOptions());
    check(!bWent && asked() != nullptr, "launch B, arriving before A's relaunch, is asked about the send");

    // A's relaunch, with the options PLAY NOW gave it, goes through.
    window.stack.clear();
    const bool aWent = gate(&a, gRelaunches.back());
    check(aWent && window.stack.empty(), "A's relaunch is not asked again");

    std::cout << "launch-play-through: " << (failures ? "FAIL" : "PASS") << "\n";
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
