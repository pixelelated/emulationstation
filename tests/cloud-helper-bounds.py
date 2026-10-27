"""The cloud pages' helper calls are bounded and leave the calling thread free.

    python3 tests/cloud-helper-bounds.py [GuiMenu.cpp] [Platform.cpp]

The cloud pages read cloud_remote's lists, cloud_setup --connected and
cloud_remote active, and run cloud_oauth cancel (T26 PL-06, #1220). A stub
helper that sleeps before answering stands in for a slow one.

With GuiMenu.cpp's cloudHelperLines (the argv executor under its 5 s
deadline), extracted unchanged with the executor it calls: a 3 s stub run on
a worker answers in about 3 s while the calling thread keeps ticking; a 6 s
stub reads as a failure at the deadline with no lines; and no listed command
is left to a shell runner (GetShOutput, GetShOutputLines, runSystemCommand).

Without it (the pin), the same stubs go through the pin's GetShOutput, the
runner those calls used, on the calling thread: the 3 s stub holds that
thread for 3 s, and the 6 s stub for 6 s -- no bound -- which is the FAIL.
"""
from pathlib import Path
import os
import re
import stat
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
source_path = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "es-app/src/guis/GuiMenu.cpp"
platform_path = Path(sys.argv[2]) if len(sys.argv) > 2 else root / "es-core/src/utils/Platform.cpp"
source = source_path.read_text()

def function(text, head):
    m = re.search(head, text, re.M)
    if not m:
        return None
    end = text.index("{", m.start()) + 1
    depth = 1
    while depth:
        depth += {"{": 1, "}": -1}.get(text[end], 0)
        end += 1
    return text[m.start():end]

helper = function(source, r"^static std::vector<std::string> cloudHelperLines\(")
failures = []

if helper:
    # No listed command reaches a shell runner any more.
    listed = re.compile(r"cloud_remote\b[^\"]*\b(list|active|providers|fields|choices|subproviders)\b|cloud_setup\b[^\"]*--connected|cloud_oauth\b[^\"]*\bcancel\b")
    for m in re.finditer(r"\b(GetShOutputLines|GetShOutput|runSystemCommand|executeScriptLegacy)\(\s*\"([^\"]*)\"", source):
        if listed.search(m.group(2)):
            failures.append("line %d still runs %r through %s" % (source.count("\n", 0, m.start()) + 1, m.group(2), m.group(1)))
    # A new sign-in starts only after the old session's cancel has finished:
    # cloudOAuthStart launches serve from cancel's continuation.
    start = function(source, r"^static void cloudOAuthStart\(")
    cont = start.find("cloudOAuthCancelThen(") if start else -1
    if cont < 0:
        failures.append("cloudOAuthStart does not cancel through cloudOAuthCancelThen")
    else:
        depth, end = 0, start.index("(", cont)
        while True:
            depth += {"(": 1, ")": -1}.get(start[end], 0)
            if depth == 0:
                break
            end += 1
        if "cloud_oauth serve" not in start[cont:end] or "cloud_oauth serve" in start[:cont]:
            failures.append("cloudOAuthStart launches serve outside its cancel's continuation")
    deadline = re.search(r"CLOUD_HELPER_DEADLINE\((\d+)\)", source)
    call = "cloudHelperLines({ stub, seconds }, &ok)"
    prelude = '#include "%s"\n' % (root / "es-app/src/guis/WifiScanProcess.h") + r'''
#include <sstream>
struct LogLine { std::ostringstream o; template <class T> LogLine& operator<<(const T& v) { o << v; return *this; } };
#define LOG(level) LogLine()
namespace Utils { namespace String {
std::string trim(const std::string& s) { auto a = s.find_first_not_of(" \t\r\n"); return a == std::string::npos ? "" : s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1); }
}}
''' + "static const std::chrono::seconds CLOUD_HELPER_DEADLINE(%s);\n" % (deadline.group(1) if deadline else "0") + helper
    mode = "the argv executor (cloudHelperLines)"
else:
    runner = function(platform_path.read_text(), r"^\s*std::string GetShOutput\(const std::string& mStr\)")
    if runner is None:
        sys.exit("FAIL cloud-helper-bounds: neither cloudHelperLines nor the pin's GetShOutput found")
    prelude = "#include <cstdio>\n#include <string>\n" + runner.strip()
    call = "lines(GetShOutput(stub + \" \" + seconds))"
    mode = "the pin's GetShOutput"

harness = r'''
#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
''' + prelude + r'''
static std::vector<std::string> lines(const std::string& s) { return s.empty() ? std::vector<std::string>() : std::vector<std::string>{ s }; }
int main(int argc, char** argv)
{
    const std::string stub = argv[1];
    const bool onWorker = std::string(argv[2]) == "worker";
    for (const char* seconds : { "3", "6" })
    {
        std::atomic<bool> done(false);
        bool ok = true;
        std::vector<std::string> got;
        long ticks = 0;
        const auto start = std::chrono::steady_clock::now();
        if (onWorker)
        {
            std::thread worker([&] { got = ''' + call + r'''; done = true; });
            while (!done) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); ticks++; }
            worker.join();
        }
        else
            got = ''' + call + r''';
        const double held = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::cout << seconds << " " << held << " " << ticks << " " << got.size() << " " << ok << "\n";
    }
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="cloud-helper-bounds-") as d:
    d = Path(d)
    stub = d / "cloud_stub"
    stub.write_text("#!/bin/sh\nsleep \"$1\"\necho STATE=active\necho alpha\n")
    stub.chmod(stub.stat().st_mode | stat.S_IXUSR)
    (d / "b.cpp").write_text(harness)
    built = subprocess.run(["g++", "-std=c++17", "-O0", "-pthread", str(d / "b.cpp"), "-o", str(d / "b")], capture_output=True, text=True)
    if built.returncode:
        print("FAIL cloud-helper-bounds: the extracted code does not compile\n" + built.stderr[-1500:])
        sys.exit(1)
    run = subprocess.run([str(d / "b"), str(stub), "worker" if helper else "caller"], capture_output=True, text=True, timeout=60)
    rows = {r.split()[0]: r.split()[1:] for r in run.stdout.split("\n") if r.strip()}

def row(s):
    held, ticks, count, ok = rows[s]
    return float(held), int(ticks), int(count), ok == "1"

held3, ticks3, n3, ok3 = row("3")
held6, ticks6, n6, ok6 = row("6")
print("%s: 3 s stub answered in %.1f s with %d line(s), %d calling-thread ticks; 6 s stub ended at %.1f s with %d line(s)" % (mode, held3, n3, ticks3, held6, n6))
if helper:
    if not (2.8 <= held3 < 4.5 and n3 == 2 and ok3 and ticks3 >= 200):
        failures.append("the 3 s stub did not answer on a worker while the calling thread ran (%.1f s, %d lines, %d ticks)" % (held3, n3, ticks3))
    if not (4.8 <= held6 < 5.8 and n6 == 0 and not ok6):
        failures.append("the 6 s stub did not read as a failure at the 5 s deadline (%.1f s, %d lines)" % (held6, n6))
else:
    failures.append("the calling thread was held for the whole of each stub: %.1f s and %.1f s, no bound" % (held3, held6))
if failures:
    for f in failures:
        print("FAIL cloud-helper-bounds: " + f)
    sys.exit(1)
print("cloud-helper-bounds: PASS")
