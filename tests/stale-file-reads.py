"""Files other processes write are read afresh, not from the file cache.

Utils::FileSystem remembers its answers while UseFileCache is on (the
default): exists(path) and isRegularFile(path) return a remembered miss
until the cache is reset, while exists(path, false) stats again (and
refreshes the entry). Three reads met files written while ES runs:

- cloudSelectionRead, which reads the content scan's files and the picker's
  selection (T26 PL-08, #1222; its neighbour's comment said "uncached");
- the sign-in's QR image, which qrencode writes just before the check: one
  failed attempt left every later sign-in text-only (T26 PL-07, #1221, the
  guest window's G10 with its control);
- NETWORK SETTINGS' wg0.conf (PL-08).

Each read is extracted as written -- cloudSelectionRead whole, the other two
as the condition of their `if` -- and run against a small FileSystem that
remembers a miss the way the cache does: the file absent, then written in the
same process, then read again. A read that answers from the remembered miss
FAILs. The QR's path must also not be one fixed name, which the texture cache
keys on (a page built while the previous one lives drew the previous code).
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
    depth, end = 0, at
    while True:
        c = source[end]
        if source.startswith("//", end):
            end = source.index("\n", end)
            continue
        if source.startswith("/*", end):
            end = source.index("*/", end) + 2
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
                return source[at:end + 1]
        end += 1

def function(head):
    m = re.search(head, source, re.M)
    brace = source.index("{", m.start())
    return source[m.start():brace] + balanced(brace, "{", "}")

def condition(anchor):
    """The condition of the `if` whose text starts with anchor."""
    at = source.index("if (" + anchor)
    return balanced(at + 3, "(", ")")[1:-1]

failures = []
selection = function(r"^static bool cloudSelectionRead\(")
qr_cond = condition("Utils::FileSystem::exists(qrPath")
wg_cond = condition("Utils::FileSystem::exists(wireguardConfigFile")
fixed = re.search(r'const std::string qrPath = "[^"]*";', source)
if fixed:
    failures.append("the QR's path is one fixed name (%s), which the texture cache keys on" % fixed.group(0))

harness = r'''
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <sys/stat.h>
// The file cache, as far as these reads meet it: an answer is remembered,
// a miss included, until reset; exists(path, false) stats and refreshes.
namespace Utils { namespace FileSystem {
static std::map<std::string, struct stat> seen;
static std::map<std::string, bool> present;
static bool statNow(const std::string& p) { struct stat st; bool e = ::stat(p.c_str(), &st) == 0; present[p] = e; if (e) seen[p] = st; return e; }
bool exists(const std::string& p, bool enableCache = true) {
    if (enableCache && present.count(p)) return present[p];
    return statNow(p);
}
bool isRegularFile(const std::string& p) {
    if (!present.count(p)) statNow(p);
    return present[p] && !S_ISDIR(seen[p].st_mode);
}
}}
''' + selection + r'''
static bool qrRead(const std::string& qrPath) { return ''' + qr_cond + r'''; }
static bool wgRead(const std::string& wireguardConfigFile) { return ''' + wg_cond + r'''; }
static int failed = 0;
static void check(bool ok, const char* what) { if (!ok) { std::cout << "FAIL stale-file-reads: " << what << "\n"; failed++; } }
int main(int argc, char** argv)
{
    const std::string dir = argv[1];
    std::vector<std::string> names;
    // cloudSelectionRead: absent, then written by another process.
    const std::string sel = dir + "/selection";
    check(!cloudSelectionRead(sel, names), "the selection read a file that is not there");
    std::ofstream(sel) << "alpha\nbeta\n";
    check(cloudSelectionRead(sel, names) && names.size() == 2, "the selection file, written after a miss, read as absent (the miss remembered)");
    // ... and a directory is not a selection.
    const std::string sub = dir + "/scan";
    ::mkdir(sub.c_str(), 0700);
    check(!cloudSelectionRead(sub, names), "a directory read as a selection");
    // The QR: absent (qrencode failed), then written by the next attempt.
    const std::string qr = dir + "/qr.png";
    check(!qrRead(qr), "the QR read a file that is not there");
    std::ofstream(qr) << "png";
    check(qrRead(qr), "the QR, written after a failed attempt, read as absent (the miss remembered)");
    // wg0.conf: absent, then set up.
    const std::string wg = dir + "/wg0.conf";
    check(!wgRead(wg), "wg0.conf read a file that is not there");
    std::ofstream(wg) << "[Interface]\n";
    check(wgRead(wg), "wg0.conf, written after a miss, read as absent (the miss remembered)");
    if (failed) return 1;
    std::cout << "6 reads\n";
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="stale-file-reads-") as d:
    d = Path(d)
    (d / "r.cpp").write_text(harness)
    built = subprocess.run(["g++", "-std=c++14", "-O0", "-g", "-fsanitize=address,undefined", str(d / "r.cpp"), "-o", str(d / "r")],
                           capture_output=True, text=True)
    if built.returncode:
        print("FAIL stale-file-reads: the extracted reads do not compile\n" + built.stderr[-1500:])
        sys.exit(1)
    (d / "files").mkdir()
    run = subprocess.run([str(d / "r"), str(d / "files")], capture_output=True, text=True)
    out = run.stdout.strip()
    if run.returncode:
        failures += [l[len("FAIL stale-file-reads: "):] for l in out.splitlines() if l.startswith("FAIL")] or ["rc %d: %s" % (run.returncode, run.stderr[-300:])]
if failures:
    for f in failures:
        print("FAIL stale-file-reads: " + f)
    sys.exit(1)
print("stale-file-reads: PASS, " + out)
