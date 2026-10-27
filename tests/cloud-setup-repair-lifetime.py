"""The REPAIR A CONNECTION picker's lifetime, with real deletion and AddressSanitizer.

openCloudSetup, cloudSetupShowSshStep, cloudSetupPresent and
cloudSetupDisplayName are extracted unchanged from the source under test;
only the screen, the stores and the scripts are small doubles. With two
connections the picker opens; the network then drops, so the SSH step
refuses (no IP) without presenting; the picker's next press must not touch a
page that was closed (T26 PL-01, #1215). The network then returns and the
press must replace the picker with the wizard's first step.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

source_path = (Path(sys.argv[1]) if len(sys.argv) > 1 else
               Path(__file__).resolve().parents[1] / "es-app/src/guis/GuiMenu.cpp")
source = source_path.read_text()

def definition(head):
    start = source.index(head)
    while source.index(';', start) < source.index('{', start):
        start = source.index(head, start + 1)
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

harness = r'''
#include <algorithm>
#include <cassert>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <set>
#include <string>
#include <utility>
#include <vector>
#define _(value) std::string(value)
#define _U(value) std::string(value)
enum LogLevel { LogError, LogWarning, LogInfo };
struct LogLine { std::ostringstream o; template <class T> LogLine& operator<<(const T& v) { o << v; return *this; } };
#define LOG(level) LogLine()
namespace Utils { namespace Platform {
int runSystemCommand(const std::string&, const std::string&, void*) { return 0; }
}}
namespace Utils { namespace String {
std::string trim(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out; std::string cur;
    for (char c : s) { if (c == sep) { out.push_back(cur); cur.clear(); } else cur += c; }
    out.push_back(cur); return out;
}
}}
namespace Renderer {
int getScreenWidth() { return 640; }
int getScreenHeight() { return 480; }
struct ScreenSettings { static bool fullScreenMenus() { return false; } };
}
struct SystemConf {
    std::map<std::string, std::string> v;
    static SystemConf* getInstance() { static SystemConf c; return &c; }
    std::string get(const std::string& k) { return v[k]; }
    void set(const std::string& k, const std::string& x) { v[k] = x; }
    void saveSystemConf() {}
};
struct GuiComponent { virtual ~GuiComponent() {} };
struct Window {
    std::vector<GuiComponent*> stack;
    void pushGui(GuiComponent* gui) { stack.push_back(gui); }
    void remove(GuiComponent* gui) { stack.erase(std::remove(stack.begin(), stack.end(), gui), stack.end()); }
};
static int gRefusals = 0;
struct GuiMsgBox : GuiComponent {
    Window* window;
    GuiMsgBox(Window* w, const std::string&, const std::string& = "", std::function<void()> = nullptr,
              const std::string& = "", std::function<void()> = nullptr) : window(w) { gRefusals++; }
    ~GuiMsgBox() { window->remove(this); }
};
struct Menu { void setSize(float, float) {} void setPosition(float, float) {} void clearButtons() {} };
struct GuiSettings : GuiComponent {
    Window* window;
    std::string title;
    Menu menu;
    std::vector<std::pair<std::string, std::function<void()>>> entries;
    GuiSettings(Window* w, const std::string& t) : window(w), title(t) {}
    ~GuiSettings() { window->remove(this); }
    void setSubTitle(const std::string&) {}
    void addGroup(const std::string&) {}
    Menu& getMenu() { return menu; }
    void addEntry(const std::string& label, bool, const std::function<void()>& f) { entries.emplace_back(label, f); }
    std::function<void()> finalize;
    void onFinalize(const std::function<void()>& f) { finalize = f; }
    void close() { if (finalize) finalize(); delete this; }
};
enum class CloudSetupMode { FirstRemote, AddRemote, RepairRemote };
static std::string cloudSetupTitle(CloudSetupMode) { return "REPAIR A CONNECTION"; }
// The network, as cloud_setup --info reports it.
static bool gOnline = true;
static std::map<std::string, std::string> cloudSetupInfo()
{
    std::map<std::string, std::string> info;
    info["IP"] = gOnline ? "192.0.2.10" : "";
    info["REMOTES"] = "alpha: beta:";
    info["SAVES_REMOTE"] = "/pixelelated/Saves";
    info["PASSWORD_SET"] = "1";
    info["SSH_UP"] = "active";
    return info;
}
static void cloudSetupAddInfoRow(GuiSettings*, Window*, const std::string&, bool = false) {}
static void cloudSetupAddFact(GuiSettings*, Window*, const std::string&, const std::string&, const std::function<void()>& = nullptr) {}
static void cloudSetupSetButtons(GuiSettings*, const std::function<void()>&) {}
static void cloudSetupOpenPasswordPage(Window*, const std::string&, const std::function<void()>&) {}
static void cloudSetupShowConnectStep(Window*, CloudSetupMode, const std::string&, const std::string&, GuiSettings*) {}
static void cloudSetupRunCheck(Window*, const std::string&, const std::function<void(int, const std::string&)>&) {}
static void cloudOpenFolderSettings(Window*, const std::function<void()>&) {}
static std::set<GuiSettings*> sOAuthSessionPages;
'''
harness += '\n' + definition('static std::string cloudSetupDisplayName(') + '\n'
harness += '\n' + definition('static void cloudSetupPresent(') + '\n'
harness += '\n' + definition('static void cloudSetupShowSshStep(') + '\n'
harness += '\nstruct GuiMenu { static void openCloudSetup(Window* window); };\n'
harness += '\n' + definition('void GuiMenu::openCloudSetup(') + '\n'
harness += r'''
static GuiSettings* top(Window& w) { return dynamic_cast<GuiSettings*>(w.stack.back()); }
static void press(GuiSettings* page, const std::string& label)
{
    auto row = std::find_if(page->entries.begin(), page->entries.end(),
        [&label](const auto& e) { return e.first.find(label) != std::string::npos; });
    assert(row != page->entries.end());
    // The press's own copy, as ES's event dispatch holds it.
    const auto action = row->second;
    action();
}
int main()
{
    Window window;
    auto hub = new GuiSettings(&window, "CLOUD");
    window.pushGui(hub);
    GuiMenu::openCloudSetup(&window);
    GuiSettings* setup = top(window);
    assert(setup->title == "CLOUD STORAGE SETUP");
    press(setup, "REPAIR A CONNECTION");
    GuiSettings* picker = top(window);
    assert(picker->title == "WHICH CONNECTION?");
    // The network drops before the press: the step refuses, the picker stays.
    gOnline = false;
    press(picker, "alpha");
    assert(gRefusals == 1);
    delete window.stack.back();               // the refusal dismissed
    assert(top(window) == picker);
    press(picker, "alpha");                   // the second press after the refusal
    assert(gRefusals == 2);
    delete window.stack.back();
    // The network returns: the press replaces the picker with the wizard.
    gOnline = true;
    press(picker, "alpha");
    GuiSettings* step = top(window);
    assert(step != picker);
    assert(std::find(window.stack.begin(), window.stack.end(), (GuiComponent*) picker) == window.stack.end());
    step->close();
    std::cout << "PASS repair picker: " << window.stack.size() << " page(s) left, " << gRefusals << " refusals\n";
    while (!window.stack.empty()) delete window.stack.back();
    return 0;
}
'''

scratch = tempfile.TemporaryDirectory(prefix='cloud-setup-repair-lifetime-')
folder = Path(scratch.name)
(folder / 'repair.cpp').write_text(harness)
subprocess.run(['g++', '-std=c++14', '-O1', '-g', '-fsanitize=address',
                '-fno-omit-frame-pointer', str(folder / 'repair.cpp'),
                '-o', str(folder / 'repair')], check=True)
env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0:halt_on_error=1')
run = subprocess.run([str(folder / 'repair')], capture_output=True, text=True, env=env)
if run.returncode:
    print('FAIL repair picker: ' + next((l for l in run.stderr.splitlines() if 'ERROR: AddressSanitizer' in l), 'rc %d' % run.returncode))
    print('\n'.join(l for l in run.stderr.splitlines() if l.lstrip().startswith('#') and ('openCloudSetup' in l or 'cloudSetup' in l or 'main' in l))[:1500])
    sys.exit(1)
print(run.stdout.strip())
