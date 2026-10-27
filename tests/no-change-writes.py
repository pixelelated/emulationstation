"""Pages that change nothing write nothing (T26 PL-12 #1226, PL-18 #1230).

    python3 tests/no-change-writes.py [GuiMenu.cpp]

- FINISH RESTORE PROCESS > WI-FI PASSWORD: the page's finalizer is
  extracted as written and run with the key as the page opened: an unchanged
  close must start no association (networkApplyWifi), a changed one must.
- SOUND SETTINGS: the favorites switch and the battery warning threshold,
  extracted from openSoundSettings as written (from the switch's state to its
  save, from the slider to its save), are built and closed without a press,
  over stores that hold a stale ON (no favorites) and no threshold: nothing
  may be written.
- SYSTEM SETTINGS > TIMEZONE, the same way: a stored timezone and an
  unchanged close write nothing; an empty store is filled once (es_settings
  sets the device's timezone from it at every start).

The stores are doubles with the real stores' rule: a set of the value
already held changes nothing and so writes nothing; Settings::saveFile
writes only after a change.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

source = (Path(sys.argv[1]) if len(sys.argv) > 1 else
          Path(__file__).resolve().parents[1] / "es-app/src/guis/GuiMenu.cpp").read_text()

def balanced(at, open_ch, close_ch):
    depth, end = 0, at
    while True:
        c = source[end]
        if source.startswith("//", end):
            end = source.index("\n", end)
            continue
        if c in "\"'":
            end += 1
            while source[end] != c:
                end += 2 if source[end] == "\\" else 1
        elif c == open_ch:
            depth += 1
        elif c == close_ch:
            depth -= 1
            if depth == 0:
                return end
        end += 1

def through_save(start_anchor, save_anchor):
    """From start_anchor's line to the end of the addSaveFunc statement after it."""
    start = source.index(start_anchor)
    start = source.rfind("\n", 0, start) + 1
    save = source.index(save_anchor, start)
    end = balanced(source.index("(", save), "(", ")")
    return source[start:source.index(";", end) + 1]

final_at = source.index("wifi->onFinalize(", source.index("void GuiMenu::openRestoreRelink("))
op = source.index("(", final_at)
finalizer = source[op + 1:balanced(op, "(", ")")]
favorites = through_save("bool shouldUseFavorites", "s->addSaveFunc([favoriteSwitch")
battery = through_save("auto batteryWarningThreshold = ", "s->addSaveFunc([batteryWarningThreshold")
timezone = through_save("auto tzChoices = std::make_shared", "s->addSaveFunc([tzChoices")

harness = r'''
#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#define _(x) std::string(x)
enum LogLevel { LogError, LogWarning, LogInfo };
struct LogLine { std::ostringstream o; template <class T> LogLine& operator<<(const T& v) { o << v; return *this; } };
#define LOG(level) LogLine()
static int gWrites = 0, gAssociations = 0;
struct SystemConf {
    std::map<std::string, std::string> v; bool dirty = false;
    static SystemConf* getInstance() { static SystemConf c; return &c; }
    std::string get(const std::string& k) { return v.count(k) ? v[k] : ""; }
    bool getBool(const std::string& k) { return get(k) == "1"; }
    bool set(const std::string& k, const std::string& x) { if (get(k) == x && v.count(k)) return false; v[k] = x; dirty = true; gWrites++; return true; }
    bool saveSystemConf() { dirty = false; return true; }
};
struct Settings {
    std::map<std::string, bool> b; bool dirty = false;
    static Settings* getInstance() { static Settings s; return &s; }
    bool getBool(const std::string& k) { return b.count(k) && b[k]; }
    bool setBool(const std::string& k, bool x) { if (getBool(k) == x) return false; b[k] = x; dirty = true; return true; }
    bool saveFile() { if (!dirty) return false; dirty = false; gWrites++; return true; }
};
struct Window { void pushGui(void*) {} };
struct GuiMsgBox { GuiMsgBox(Window*, const std::string&) {} };
struct ApiSystem { static ApiSystem* getInstance() { static ApiSystem a; return &a; }
    bool enableWifi(const std::string&, const std::string&, const std::string&) { return true; } };
static void networkApplyWifi(Window*, const std::string&, const std::function<bool()>&, const std::function<void(bool)>&) { gAssociations++; }
namespace Utils { namespace String { std::string format(const char* f, const char*) { return f; } }
    namespace Platform {
        std::string GetShOutput(const std::string& cmd) { return cmd.find("timezones") != std::string::npos ? "Europe/Paris,America/New_York" : "Europe/Paris"; }
        int runSystemCommand(const std::string&, const std::string&, void*) { return 0; } } }
struct AudioManager { static AudioManager* getInstance() { static AudioManager a; return &a; } void playRandomMusic(bool) {} };
struct Page {
    std::vector<std::function<void()>> saves;
    void close() { for (auto& f : saves) f(); }
    void addSaveFunc(const std::function<void()>& f) { saves.push_back(f); }
    template <class... A> void addWithDescription(A...) {}
    template <class... A> void addWithLabel(A...) {}
};
struct SwitchComponent { bool on = false; SwitchComponent(Window*) {} void setState(bool s) { on = s; } bool getState() { return on; } };
struct SliderComponent { float val = 0; SliderComponent(Window*, float, float, float, const std::string&) {} void setValue(float x) { val = x; } float getValue() { return val; } };
template <class T> struct OptionListComponent {
    std::vector<std::pair<T, bool>> items; int initial = -1;
    OptionListComponent(Window*, const std::string&, bool) {}
    void add(const std::string&, const T& v, bool sel) { items.push_back({v, sel}); if (sel && initial < 0) initial = items.size() - 1; }
    int selected() { for (size_t i = 0; i < items.size(); i++) if (items[i].second) return i; return -1; }
    bool changed() { return selected() != initial; }
    T getSelected() { return items[selected() < 0 ? 0 : selected()].first; }
};
static void reset() { SystemConf::getInstance()->v.clear(); Settings::getInstance()->b.clear(); gWrites = 0; gAssociations = 0; }
static int failed = 0;
static void check(bool ok, const std::string& what) { if (!ok) { std::cout << "FAIL no-change-writes: " << what << "\n"; failed++; } }
static void relink(const std::string& entered)
{
    Window w; Window* window = &w; Page page; Page* s = &page; int reopened = 0;
    std::function<void()> reopen = [&reopened] { reopened++; };
    auto& conf = *SystemConf::getInstance();
    conf.v["wifi.ssid"] = "home"; conf.v["wifi.key"] = "secret"; conf.v["wifi.enabled"] = "1";
    const std::string keyAtEntry = conf.get("wifi.key");
    (void)keyAtEntry;
    auto finalize = ''' + finalizer + r''';
    conf.v["wifi.key"] = entered;   // what the editor left
    finalize();
}
static void sound()
{
    Window w; Window* mWindow = &w; Page page; Page* s = &page;
    auto favoriteSwitch = std::make_shared<SwitchComponent>(mWindow);
    bool hasFavorites = false;
''' + favorites + "\n" + battery + r'''
    page.close();
}
static void systemSettings()
{
    Window w; Window* mWindow = &w; Page page; Page* s = &page;
''' + timezone + r'''
    page.close();
}
int main()
{
    reset(); relink("secret");
    check(gAssociations == 0, "WI-FI PASSWORD: an unchanged close started an association");
    reset(); relink("new-secret");
    check(gAssociations == 1, "WI-FI PASSWORD: a changed key started no association");
    reset(); Settings::getInstance()->b["audio.useFavoriteMusic"] = true;
    sound();
    check(gWrites == 0, "SOUND SETTINGS: a visit that changed nothing wrote " + std::to_string(gWrites) + " time(s)");
    reset(); SystemConf::getInstance()->v["system.timezone"] = "Europe/Paris";
    systemSettings();
    check(gWrites == 0, "TIMEZONE: an unchanged close over a stored timezone wrote " + std::to_string(gWrites) + " time(s)");
    reset(); systemSettings();
    check(gWrites == 1 && SystemConf::getInstance()->get("system.timezone") == "Europe/Paris", "TIMEZONE: an empty store was not filled once");
    if (failed) return 1;
    std::cout << "5 cases\n";
    return 0;
}
'''
failures = []
with tempfile.TemporaryDirectory(prefix="no-change-writes-") as d:
    d = Path(d)
    (d / "w.cpp").write_text(harness)
    built = subprocess.run(["g++", "-std=c++14", "-O0", "-g", "-fsanitize=address,undefined", str(d / "w.cpp"), "-o", str(d / "w")],
                           capture_output=True, text=True)
    if built.returncode:
        print("FAIL no-change-writes: the extracted pages do not compile\n" + built.stderr[-2000:])
        sys.exit(1)
    run = subprocess.run([str(d / "w")], capture_output=True, text=True)
    failures = [l[len("FAIL no-change-writes: "):] for l in run.stdout.splitlines() if l.startswith("FAIL")]
    if run.returncode and not failures:
        failures.append("rc %d: %s" % (run.returncode, run.stderr[-400:]))
if failures:
    for f in failures:
        print("FAIL no-change-writes: " + f)
    sys.exit(1)
print("no-change-writes: PASS, " + run.stdout.strip())
