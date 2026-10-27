"""Three of the interface's sentences, as ruled (T26 S11; D-UI-112, built as proposed).

    python3 tests/player-words.py [es tree]

- PL-14 (#1228): MANUAL UPDATES' dialog in the fork's upper case, the URL as
  written, with its French.
- PL-16 (#1229): one Wi-Fi verdict. The composer (wifiSwitchNotice,
  wifiJoinedNotice) is extracted and run in English and under the French
  catalog: the switch says WI-FI IS ON. / WI-FI IS OFF., a join says the
  picker's CONNECTED. At the sites: the switch's done-callback says its state
  on success, the relink's says the join, and NETWORK SETTINGS' page close
  adds nothing on success.
- PL-28 (#1232): FINISH RESTORE PROCESS's FINISH, extracted as written and
  pressed with the marker in a folder it cannot be removed from: the ruled
  sentence, the marker left, the page left open for FINISH again.
"""
from pathlib import Path
import os
import re
import stat
import subprocess
import sys
import tempfile

es = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
source = (es / "es-app/src/guis/GuiMenu.cpp").read_text()
po = (es / "locale/lang/fr/LC_MESSAGES/emulationstation2.po").read_text()
tree = Path(__file__).resolve().parents[1]

def balanced(text, at, open_ch, close_ch):
    depth, end = 0, at
    while True:
        c = text[end]
        if text.startswith("//", end):
            end = text.index("\n", end); continue
        if c in "\"'":
            end += 1
            while text[end] != c:
                end += 2 if text[end] == "\\" else 1
        elif c == open_ch:
            depth += 1
        elif c == close_ch:
            depth -= 1
            if depth == 0:
                return end
        end += 1

def function(head):
    m = re.search(head, source, re.M)
    if not m:
        return None
    brace = source.index("{", m.start())
    return source[m.start():balanced(source, brace, "{", "}") + 1]

def french(msgid):
    m = re.search(r'^msgid "%s"\nmsgstr "((?:[^"\\]|\\.)*)"$' % re.escape(msgid.replace('"', '\\"')), po, re.M)
    return m.group(1) if m and m.group(1) else None

failures = []

# PL-14
row = source.index('_("MANUAL UPDATES"), true')
dialog = re.search(r'new GuiMsgBox\(mWindow,\s*_\("((?:[^"\\]|\\.)*)"\)', source[row:row + 600])
text = dialog.group(1) if dialog else ""
if not text:
    failures.append("MANUAL UPDATES: no dialog sentence found")
else:
    words = re.sub(r"\S*github\.com\S*", "", text)
    if words != words.upper():
        failures.append("MANUAL UPDATES' dialog is not in the house's upper case: %s" % text[:60])
    if "github.com/pixelelated/distribution/releases" not in text:
        failures.append("MANUAL UPDATES' dialog lost the URL as written")
    if not french(text):
        failures.append("MANUAL UPDATES' dialog has no French")

# PL-16, at the sites
close_at = source.index('networkApplyWifi(window, _("CONNECTING TO WI-FI"), apply, [window, newSSID](bool ok)')
close_cb = source[close_at:balanced(source, source.index("(", close_at), "(", ")")]
if "WI-FI ENABLED" in close_cb or not re.search(r"if \(!ok\)\s*window->pushGui", close_cb):
    failures.append("NETWORK SETTINGS' page close says something on success (WI-FI ENABLED)")
switch_at = source.index('networkApplyWifi(window, wifienabled ? _("CONNECTING TO WI-FI") : _("TURNING WI-FI OFF"), apply,')
switch_cb = source[switch_at:balanced(source, source.index("(", switch_at), "(", ")")]
if not re.search(r"if \(ok\)\s*wifiSwitchNotice\(window, wifienabled\)", switch_cb):
    failures.append("the Wi-Fi switch says nothing when it has turned Wi-Fi on or off")
relink_at = source.index('networkApplyWifi(window, _("CONNECTING TO WI-FI"),', source.index("void GuiMenu::openRestoreRelink("))
relink_cb = source[relink_at:balanced(source, source.index("(", relink_at), "(", ")")]
if not re.search(r"if \(ok\)\s*wifiJoinedNotice\(window, ssid\)", relink_cb):
    failures.append("the relink's WI-FI PASSWORD says nothing when it has joined")

switch_fn = function(r"^static void wifiSwitchNotice\(")
joined_fn = function(r"^static void wifiJoinedNotice\(")
# The relink's FINISH, by its captures: the setup wizard has a FINISH of its own earlier in the file.
finish_at = re.search(r'addButton\(_\("FINISH"\), _\("finish"\), \[s,[^\]]*restoreMarker', source).start()
finish_lambda = source[source.index("[", finish_at):balanced(source, source.index("{", finish_at), "{", "}") + 1]
fr = {k: french(k) for k in ("WI-FI IS ON.", "WI-FI IS OFF.", "CONNECTED",
                              "COULDN'T FINISH - THIS DEVICE COULDN'T SAVE THAT SETUP IS DONE. TRY AGAIN.")}
for k, v in fr.items():
    if v is None and (switch_fn or "COULDN'T FINISH - THIS" in k):
        failures.append("no French for %s" % k)

harness = r'''
#include <cstdio>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <sys/stat.h>
#include "WifiText.h"
static bool gFrench = false;
static std::string tr(const char* id)
{
    static const std::map<std::string, std::string> fr = {
''' + "".join('        { "%s", "%s" },\n' % (k.replace('"', '\\"'), v) for k, v in fr.items() if v) + r'''    };
    auto it = fr.find(id);
    return gFrench && it != fr.end() ? it->second : std::string(id);
}
#define _(x) tr(x)
#define _U(x) std::string(x)
enum LogLevel { LogError, LogWarning, LogInfo };
struct LogLine { std::ostringstream o; template <class T> LogLine& operator<<(const T& v) { o << v; return *this; } };
#define LOG(level) LogLine()
struct GuiMsgBox { std::string text; GuiMsgBox(void*, const std::string& t, const std::string& = "") : text(t) {} };
struct Window {
    std::vector<std::string> toasts, dialogs;
    void displayNotificationMessage(const std::string& t) { toasts.push_back(t); }
    void pushGui(GuiMsgBox* b) { dialogs.push_back(b->text); delete b; }
};
namespace Utils { namespace FileSystem { bool exists(const std::string& p, bool = true) { struct stat st; return ::stat(p.c_str(), &st) == 0; } } }
struct Page { bool closed = false; void close() { closed = true; } };
''' + (switch_fn or "") + "\n" + (joined_fn or "") + r'''
static int failed = 0;
static void check(bool ok, const std::string& what) { if (!ok) { std::cout << "FAIL player-words: " << what << "\n"; failed++; } }
int main(int argc, char** argv)
{
'''
if switch_fn and joined_fn:
    harness += r'''
    for (bool f : { false, true })
    {
        gFrench = f; Window w;
        wifiSwitchNotice(&w, true); wifiSwitchNotice(&w, false); wifiJoinedNotice(&w, "home");
        const std::string on = f ? "\xef\x81\x98 LE WI-FI EST ACTIV\xc3\x89." : "\xef\x81\x98 WI-FI IS ON.";
        const std::string off = f ? "\xef\x81\x98 LE WI-FI EST D\xc3\x89SACTIV\xc3\x89." : "\xef\x81\x98 WI-FI IS OFF.";
        check(w.toasts.size() == 3 && w.toasts[0] == on && w.toasts[1] == off, std::string("the switch's toasts") + (f ? " in French" : "") + ": " + (w.toasts.empty() ? "" : w.toasts[0]));
        check(w.toasts.size() == 3 && w.toasts[2].find("home") != std::string::npos, "the join's toast does not name the network");
        check(w.dialogs.empty(), "a Wi-Fi verdict opened a dialog");
    }
    gFrench = false;
'''
else:
    harness += '    check(false, "no Wi-Fi composer (wifiSwitchNotice, wifiJoinedNotice): the switch says nothing, a page close says WI-FI ENABLED in a dialog");\n'
harness += r'''
    // FINISH with a marker that cannot be removed.
    const std::string restoreMarker = std::string(argv[1]) + "/.restore-finish-pending";
    const bool consumeMarker = true;
    Window w; Window* window = &w; (void)window; Page page; Page* s = &page;
    auto finish = ''' + finish_lambda + r''';
    finish();
    check(Utils::FileSystem::exists(restoreMarker), "FINISH removed a marker it could not remove?");
    check(!w.dialogs.empty() && w.dialogs.back() == "COULDN'T FINISH - THIS DEVICE COULDN'T SAVE THAT SETUP IS DONE. TRY AGAIN.",
        "FINISH said nothing when the marker stayed");
    check(!page.closed, "FINISH closed the page over a marker that stayed");
    return failed ? 1 : 0;
}
'''
with tempfile.TemporaryDirectory(prefix="player-words-") as d:
    d = Path(d)
    (d / "w.cpp").write_text(harness)
    built = subprocess.run(["g++", "-std=c++14", "-O0", "-I", str(tree / "es-app/src"), "-I", str(tree / "es-core/src"), str(d / "w.cpp"),
                            str(tree / "es-app/src/WifiText.cpp"), str(tree / "es-core/src/utils/StringUtil.cpp"), "-o", str(d / "w")],
                           capture_output=True, text=True)
    if built.returncode:
        print("FAIL player-words: the extracted code does not compile\n" + built.stderr[-2000:]); sys.exit(1)
    locked = d / "locked"
    locked.mkdir()
    (locked / ".restore-finish-pending").write_text("")
    locked.chmod(stat.S_IRUSR | stat.S_IXUSR)
    try:
        run = subprocess.run([str(d / "w"), str(locked)], capture_output=True, text=True)
    finally:
        locked.chmod(stat.S_IRWXU)
    failures += [l[len("FAIL player-words: "):] for l in run.stdout.splitlines() if l.startswith("FAIL")]
    if run.returncode and not failures:
        failures.append("rc %d: %s" % (run.returncode, run.stderr[-300:]))
if failures:
    for f in failures:
        print("FAIL player-words: " + f)
    sys.exit(1)
print("player-words: PASS")
