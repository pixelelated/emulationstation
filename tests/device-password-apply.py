"""The device password pages apply once, and never save an emptied entry.

Two pages edit root.password: the cloud setup wizard's SSH PASSWORD
(cloudSetupOpenPasswordPage) and FINISH RESTORE PROCESS's DEVICE PASSWORD
(openRestoreRelink). The text editor writes the accepted entry to SystemConf
at once, and GuiSettings::save() writes SystemConf to disk after the page's
save hooks run, so each hook decides what is saved. Each page's hook is
extracted as written (with the file's static helpers it calls) and run over a
page close: unchanged, emptied, changed, and a device whose stored value is
already empty. setrootpass refuses an empty password, so an emptied entry that
reaches the disk leaves root.password empty while the device keeps its
password (T26 PL-05, #1219).
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

source_path = (Path(sys.argv[1]) if len(sys.argv) > 1 else
               Path(__file__).resolve().parents[1] / "es-app/src/guis/GuiMenu.cpp")
source = source_path.read_text()

def balanced(at, open_ch, close_ch):
    """From source[at] == open_ch to its match, strings and comments skipped."""
    depth, end = 0, at
    while True:
        c = source[end]
        if source.startswith("//", end):
            end = source.index("\n", end)
        elif c in "\"'":
            end += 1
            while source[end] != c:
                end += 2 if source[end] == "\\" else 1
        elif c == open_ch:
            depth += 1
        elif c == close_ch:
            depth -= 1
            if depth == 0:
                return source[at:end + 1]
        end += 1

def hook_after(anchor):
    """The lambda passed to the first addSaveFunc after anchor."""
    call = source.index("addSaveFunc(", source.index(anchor))
    return balanced(call + len("addSaveFunc"), "(", ")")[1:-1]

def helper(name):
    m = re.search(r"^static [^\n(]*\b%s\(" % re.escape(name), source, re.M)
    if not m:
        return ""
    brace = source.index("{", m.start())
    return source[m.start():brace] + balanced(brace, "{", "}")

wizard = hook_after("static void cloudSetupOpenPasswordPage(")
relink = hook_after('addInputTextConfigRow(_("DEVICE PASSWORD"), "root.password", false);')
helpers = "\n".join(helper(n) for n in sorted(set(re.findall(r"\b([A-Za-z_]\w*)\(", wizard + relink))) if helper(n))

harness = r'''
#include <cassert>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
struct LogLine { std::ostringstream o; template <class T> LogLine& operator<<(const T& v) { o << v; return *this; } };
#define LOG(level) LogLine()
struct SystemConf {
    std::map<std::string, std::string> v, disk;
    static SystemConf* getInstance() { static SystemConf c; return &c; }
    std::string get(const std::string& k) { return v.count(k) ? v[k] : ""; }
    bool set(const std::string& k, const std::string& x) { bool c = get(k) != x; v[k] = x; return c; }
    bool saveSystemConf() { disk = v; return true; }
};
static std::vector<std::string> gCommands;
namespace Utils {
namespace Platform { int runSystemCommand(const std::string& cmd, const std::string&, void*) { gCommands.push_back(cmd); return 0; } }
namespace String { std::string shellQuote(const std::string& s) { return "'" + s + "'"; } }
}
''' + helpers + r'''
static std::function<void()> wizardHook(const std::string& current) { return ''' + wizard + r'''; }
static std::function<void()> relinkHook(const std::string& rootPass) { return ''' + relink + r'''; }
static int failures = 0;
static void check(bool ok, const std::string& page, const std::string& what)
{
    if (!ok) { std::cout << "FAIL device-password-apply: " << page << ": " << what << "\n"; failures++; }
}
// The page opened with `stored`; the editor left `entry` (or nothing); the page closes.
static void close(std::function<void()> (*make)(const std::string&), const std::string& stored, const char* entry)
{
    SystemConf& c = *SystemConf::getInstance();
    c.v.clear(); c.disk.clear(); gCommands.clear();
    c.v["root.password"] = stored; c.disk = c.v;
    auto hook = make(stored);
    if (entry) c.set("root.password", entry);
    hook();
    c.saveSystemConf();   // GuiSettings::save(), after the hooks
}
int main()
{
    struct Page { const char* name; std::function<void()> (*make)(const std::string&); } pages[] = {
        { "SSH PASSWORD (wizard)", wizardHook }, { "DEVICE PASSWORD (relink)", relinkHook } };
    int cases = 0;
    for (auto& p : pages)
    {
        SystemConf& c = *SystemConf::getInstance();
        close(p.make, "old-secret", nullptr);
        check(gCommands.empty() && c.disk["root.password"] == "old-secret", p.name, "an unchanged close ran setrootpass or changed the stored value"); cases++;
        close(p.make, "old-secret", "old-secret");
        check(gCommands.empty() && c.disk["root.password"] == "old-secret", p.name, "re-entering the same value ran setrootpass"); cases++;
        close(p.make, "old-secret", "");
        check(gCommands.empty(), p.name, "an emptied entry ran setrootpass");
        check(c.disk["root.password"] == "old-secret", p.name, "an emptied entry was saved: root.password '" + c.disk["root.password"] + "' while the device keeps its password"); cases++;
        close(p.make, "old-secret", "new-secret");
        check(gCommands.size() == 1 && gCommands[0] == "setrootpass 'new-secret'" && c.disk["root.password"] == "new-secret", p.name, "a changed entry was not applied exactly once"); cases++;
        close(p.make, "", "");
        check(gCommands.empty() && c.disk["root.password"] == "", p.name, "a device already holding an empty value changed on an empty close"); cases++;
    }
    if (failures) return 1;
    std::cout << "device-password-apply: PASS, " << cases << " closes over 2 pages\n";
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="device-password-apply-") as d:
    cpp, exe = Path(d) / "pw.cpp", Path(d) / "pw"
    cpp.write_text(harness)
    built = subprocess.run(["g++", "-std=c++14", "-O0", "-g", "-fsanitize=address,undefined", str(cpp), "-o", str(exe)], capture_output=True, text=True)
    if built.returncode:
        print("FAIL device-password-apply: the extracted hooks do not compile")
        print(built.stderr[-1500:])
        sys.exit(1)
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    print(run.stdout.strip() or run.stderr.strip()[-500:])
    sys.exit(run.returncode)
