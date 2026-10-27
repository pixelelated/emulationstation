"""Without cloud_scan the transfer pages say so and stop (T26 PL-29, #1233).

    python3 tests/cloud-scan-absent.py [GuiMenu.cpp]

cloudOpenTransfer (BACK UP / RESTORE FROM THE CLOUD) and cloudScanContent
(the content pages) run cloud_scan first. With it absent, the first fell
back to the transfer options, whose first act is to refuse a check that was
not stamped -- so the player read THE CLOUD CHECK IS NO LONGER CURRENT,
about a check that never ran -- and the second said that sentence directly.

Both functions are extracted as written, with cloudCheckChanged and (where
the tree has it) cloudCheckToolMissing; the scan page, the stores and the
options page are doubles. The options double keeps the real page's first
statement, its stamp guard, and the source is checked to still open with it.
Absent: each says the missing-tool sentence and opens nothing. Present: the
transfer runs cloud_scan, and a completed check reaches the options.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

source = (Path(sys.argv[1]) if len(sys.argv) > 1 else
          Path(__file__).resolve().parents[1] / "es-app/src/guis/GuiMenu.cpp").read_text()

def function(head):
    m = next((m for m in re.finditer(head, source, re.M)
              if source.index("{", m.start()) < source.index(";", m.start())), None)
    if not m:
        return None
    end = source.index("{", m.start()) + 1
    depth = 1
    while depth:
        c = source[end]
        if source.startswith("//", end):
            end = source.index("\n", end); continue
        if c in "\"'":
            end += 1
            while source[end] != c:
                end += 2 if source[end] == "\\" else 1
        elif c in "{}":
            depth += 1 if c == "{" else -1
        end += 1
    return source[m.start():end]

failures = []
options = function(r"^static void cloudOpenTransferOptions\(")
if not re.match(r'[^{]*\{\s*if \(!cloudScanStamped\("done"\)\)\s*\{\s*cloudCheckChanged\(window\);\s*return;\s*\}', options):
    failures.append("cloudOpenTransferOptions no longer opens with its stamp guard: this test's double of it is wrong")
missing = function(r"^static void cloudCheckToolMissing\(")
harness = r'''
#include <functional>
#include <iostream>
#include <string>
#include <vector>
#define _(x) std::string(x)
struct GuiMsgBox { std::string text; GuiMsgBox(void*, const std::string& t, const std::string& = "", std::function<void()> = nullptr,
    const std::string& = "", std::nullptr_t = nullptr) : text(t) {} };
struct GuiCloudTransfer {
    std::string command; std::function<void()> cont;
    GuiCloudTransfer(void*, const std::string& c, const std::string&, int) : command(c) {}
    void setAutoContinue(const std::function<void()>& f) { cont = f; }
};
struct Window {
    std::vector<std::string> dialogs; GuiCloudTransfer* page = nullptr;
    void pushGui(GuiMsgBox* b) { dialogs.push_back(b->text); delete b; }
    void pushGui(GuiCloudTransfer* p) { page = p; }
};
static bool gScan = false, gStamped = false;
static int gOptions = 0;
namespace Utils {
    namespace FileSystem { bool exists(const std::string& p, bool = true) { return p == "/usr/bin/cloud_scan" ? gScan : true; } }
    namespace String { std::string shellQuote(const std::string& s) { return "'" + s + "'"; } }
}
std::string cloudShellQuote(const std::string& s) { return Utils::String::shellQuote(s); }
struct GuiMenu { static void openCloudAddRemote(Window*) {} };
static std::string sCloudScanRun, sCloudContentScanRun, sCloudContentScanMode;
static std::string cloudNewCheckId() { return "run1"; }
static std::string cloudContentMode(bool, bool) { return ""; }
static bool cloudScanStamped(const std::string&) { return gStamped; }
''' + function(r"^static void cloudCheckChanged\(") + "\n" + (missing or "") + r'''
// The options page: its real first statement, then a count.
static void cloudOpenTransferOptions(Window* window, bool)
{
    if (!cloudScanStamped("done")) { cloudCheckChanged(window); return; }
    gOptions++;
}
''' + function(r"^static void cloudScanContent\(") + "\n" + function(r"^static void cloudOpenTransfer\(") + r'''
static int failed = 0;
static void check(bool ok, const std::string& what) { if (!ok) { std::cout << "FAIL cloud-scan-absent: " << what << "\n"; failed++; } }
static const std::string MISSING = "THIS DEVICE CAN'T CHECK YOUR CLOUD. SOMETHING IT NEEDS IS MISSING FROM THIS BUILD.";
int main()
{
    { gScan = false; gStamped = false; gOptions = 0; Window w; cloudOpenTransfer(&w, true);
      check(w.dialogs.size() == 1 && w.dialogs[0] == MISSING && !w.page && gOptions == 0,
          "BACK UP TO THE CLOUD without cloud_scan said \"" + (w.dialogs.empty() ? std::string("nothing") : w.dialogs[0]) + "\""); }
    { gScan = false; Window w; bool then = false; cloudScanContent(&w, true, true, false, [&then] { then = true; });
      check(w.dialogs.size() == 1 && w.dialogs[0] == MISSING && !w.page && !then,
          "the content check without cloud_scan said \"" + (w.dialogs.empty() ? std::string("nothing") : w.dialogs[0]) + "\""); }
    { gScan = true; gStamped = false; gOptions = 0; Window w; cloudOpenTransfer(&w, false);
      check(w.page && w.page->command.find("/usr/bin/cloud_scan --run-id") == 0, "with cloud_scan the transfer did not run it");
      gStamped = true; if (w.page && w.page->cont) w.page->cont();
      check(gOptions == 1 && w.dialogs.empty(), "a completed check did not reach the transfer options"); delete w.page; }
    if (failed) return 1;
    std::cout << "3 cases\n";
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="cloud-scan-absent-") as d:
    d = Path(d)
    (d / "a.cpp").write_text(harness)
    built = subprocess.run(["g++", "-std=c++14", "-O0", "-g", "-fsanitize=address,undefined", str(d / "a.cpp"), "-o", str(d / "a")],
                           capture_output=True, text=True)
    if built.returncode:
        print("FAIL cloud-scan-absent: the extracted pages do not compile\n" + built.stderr[-2000:]); sys.exit(1)
    run = subprocess.run([str(d / "a")], capture_output=True, text=True)
    failures += [l[len("FAIL cloud-scan-absent: "):] for l in run.stdout.splitlines() if l.startswith("FAIL")]
    if run.returncode and not failures:
        failures.append("rc %d: %s" % (run.returncode, run.stderr[-300:]))
if failures:
    for f in failures:
        print("FAIL cloud-scan-absent: " + f)
    sys.exit(1)
print("cloud-scan-absent: PASS, " + run.stdout.strip())
