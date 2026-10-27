"""The cloud pages' command readers keep every byte, fail closed, and parse once.

    python3 tests/command-readers.py [GuiMenu.cpp] [Platform.cpp]

T26 PL-11 (#1225):
- Utils::Platform::GetShOutput (es-core) dropped the last byte of every
  fgets chunk -- the newline of a short line, a real character of each
  255-byte piece of a long one and of a last line with no newline -- and read
  from popen's result without testing it. Its contract, the lines joined
  without their newlines, is what its callers rely on and is kept.
- A cloud_remote change's "OK=<name>" was parsed two ways (anywhere in the
  joined text; at its start), and the name could run on into the next line.
- The relink page's CHECK CONNECTION read an empty `echo $?` as 0: YOUR
  CLOUD IS ANSWERING.

GetShOutput is extracted from Platform.cpp and run against real commands, a
failed popen stood in by a hook. The OK= parse and the status reader are
extracted from GuiMenu.cpp and run over their cases; where the tree has
neither (the pin), its two parse expressions and its atoi are named.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
guimenu = (Path(sys.argv[1]) if len(sys.argv) > 1 else root / "es-app/src/guis/GuiMenu.cpp").read_text()
platform = (Path(sys.argv[2]) if len(sys.argv) > 2 else root / "es-core/src/utils/Platform.cpp").read_text()

def function(text, head):
    m = next((m for m in re.finditer(head, text, re.M)
              if text.index("{", m.start()) < text.index(";", m.start())), None)
    if not m:
        return None
    end = text.index("{", m.start()) + 1
    depth = 1
    while depth:
        c = text[end]
        if text.startswith("//", end):
            end = text.index("\n", end)
            continue
        if c in "\"'":
            end += 1
            while text[end] != c:
                end += 2 if text[end] == "\\" else 1
        elif c in "{}":
            depth += 1 if c == "{" else -1
        end += 1
    return text[m.start():end]

def struct(text, name):
    m = re.search(r"^struct %s\n\{" % name, text, re.M)
    return function(text, r"^struct %s\n" % name) + ";" if m else None

failures = []
reader = function(platform, r"^\s*std::string GetShOutput\(const std::string& mStr\)")
okname = function(guimenu, r"^static std::string cloudRemoteOkName\(")
status = function(guimenu, r"^static int cloudExitStatus\(")
result = struct(guimenu, "CloudRemoteResult")

harness = r'''
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include "utils/StringUtil.h"
// popen, with one command that fails to start.
static FILE* hookPopen(const char* command, const char* mode)
{
    return std::string(command) == "FAIL-POPEN" ? nullptr : ::popen(command, mode);
}
#define popen hookPopen
''' + reader + "\n#undef popen\n"
if okname and status and result:
    harness += result + "\n" + okname + "\n" + status + "\n"
harness += r'''
static int failed = 0;
static void check(bool ok, const std::string& what) { if (!ok) { std::cout << "FAIL command-readers: " << what << "\n"; failed++; } }
int main()
{
    const std::string longLine(300, 'x');
    const std::string got = GetShOutput("printf '%s\\n' " + longLine);
    check(got == longLine, "GetShOutput gave a 300-byte line back as " + std::to_string(got.size()) + " bytes");
    check(GetShOutput("printf CONNECTED") == "CONNECTED", "GetShOutput lost the last byte of a line with no newline: " + GetShOutput("printf CONNECTED"));
    check(GetShOutput("printf 'a\\nb\\n'") == "ab", "GetShOutput no longer joins lines without their newlines (its callers' contract)");
    std::cout.flush();
    const std::string none = GetShOutput("FAIL-POPEN");
    check(none.empty(), "GetShOutput read something from a failed popen");
'''
if okname and status and result:
    harness += r'''
    auto r = [](int rc, std::vector<std::string> lines) { CloudRemoteResult x; x.rc = rc; x.lines = lines; return x; };
    check(cloudRemoteOkName(r(0, { "Note: something", "OK=alpha" })) == "alpha", "OK= after another line");
    check(cloudRemoteOkName(r(0, { "OK=alpha", "warning" })) == "alpha", "OK= followed by a line ran on into it");
    check(cloudRemoteOkName(r(1, { "OK=alpha" })).empty(), "OK= with a failing exit read as success");
    check(cloudRemoteOkName(r(0, { "XOK=alpha", "the OK=beta" })).empty(), "OK= inside a line read as success");
    check(cloudRemoteOkName(r(-1, {})).empty(), "a run that could not start read as success");
    check(cloudExitStatus("") == -1, "an empty status read as " + std::to_string(cloudExitStatus("")));
    check(cloudExitStatus("0") == 0 && cloudExitStatus("2") == 2, "a status read wrong");
    check(cloudExitStatus("RC=0") == -1 && cloudExitStatus("0 1") == -1, "a garbled status read as a number");
'''
harness += "    return failed ? 1 : 0;\n}\n"

if not (okname and status and result):
    if re.search(r'out\.find\("OK="\)', guimenu) and re.search(r'startsWith\(result, "OK="\)', guimenu):
        failures.append('two parses of one "OK=" line: anywhere in the joined text (create) and at its start (select, remove)')
    if re.search(r'cloud_setup --check >/dev/null 2>&1; echo \$\?"\)\);\s*int rc = atoi\(out\.c_str\(\)\);', guimenu):
        failures.append("CHECK CONNECTION reads its status with atoi: an empty one is 0, YOUR CLOUD IS ANSWERING")

with tempfile.TemporaryDirectory(prefix="command-readers-") as d:
    d = Path(d)
    (d / "r.cpp").write_text(harness)
    built = subprocess.run(["g++", "-std=c++14", "-O0", "-g", "-I", str(root / "es-core/src"), str(d / "r.cpp"),
                            str(root / "es-core/src/utils/StringUtil.cpp"), "-o", str(d / "r")], capture_output=True, text=True)
    if built.returncode:
        print("FAIL command-readers: the extracted readers do not compile\n" + built.stderr[-1500:])
        sys.exit(1)
    run = subprocess.run([str(d / "r")], capture_output=True, text=True, timeout=60)
    failures += [l[len("FAIL command-readers: "):] for l in run.stdout.splitlines() if l.startswith("FAIL")]
    if run.returncode < 0:
        failures.append("the reader crashed (signal %d) after the failed popen: it read from a null stream" % -run.returncode)
    elif run.returncode and not run.stdout.strip():
        failures.append("rc %d" % run.returncode)
if failures:
    for f in failures:
        print("FAIL command-readers: " + f)
    sys.exit(1)
print("command-readers: PASS")
