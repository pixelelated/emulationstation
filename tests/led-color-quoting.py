"""The LED colour reaches ledcontrol as one literal argument (T26 PL-13, #1227).

    python3 tests/led-color-quoting.py [GuiMenu.cpp] [StringUtil.cpp]

SYSTEM SETTINGS > LED COLOR runs `/usr/bin/sh -lc ...ledcontrol <colour>`
through runSystemCommand, itself a shell: two shells read the colour, which
is kept from system.cfg and selectable as stored. The command the callback
builds is extracted as written and built for hostile colours; each command
is run the way system() runs it (sh -c), inside bubblewrap with a clear
environment, with /usr/bin/ledcontrol's path pointed at a stub that records
its arguments, and `-l` dropped (the host's login files are not the
device's). Each colour must arrive as one argument, byte for byte, and run
nothing.
"""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
source = (Path(sys.argv[1]) if len(sys.argv) > 1 else root / "es-app/src/guis/GuiMenu.cpp").read_text()
util = (Path(sys.argv[2]) if len(sys.argv) > 2 else root / "es-core/src/utils/StringUtil.cpp").read_text()

at = source.index("setSelectedChangedCallback([](const std::string& newColor)")
call = source.index("runSystemCommand(", at)
depth, end = 0, call + len("runSystemCommand")
while True:
    c = source[end]
    if c == '"':
        end += 1
        while source[end] != '"':
            end += 2 if source[end] == "\\" else 1
    elif c == "(":
        depth += 1
    elif c == ")":
        depth -= 1
        if depth == 0:
            break
    end += 1
args = source[call + len("runSystemCommand("):end]
command = args[:args.rindex(', "", nullptr')]
start = util.index("std::string shellQuote(const std::string& _string)")
quote = "namespace Utils { namespace String {\n" + util[start:util.index("} // shellQuote", start)] + "}\n} }\n"

colours = ["red", "deep blue", "$(touch CANARY)", "`touch CANARY`", "a\"b", "it's", "a;touch CANARY", "x\\$y"]
harness = "#include <iostream>\n#include <string>\n" + quote + r'''
static std::string ledCommand(const std::string& newColor) { return ''' + command + r'''; }
int main(int argc, char** argv) { std::cout << ledCommand(argv[1]); return 0; }
'''
failures = []
if not shutil.which("bwrap"):
    print("FAIL led-color-quoting: no bwrap to run the commands in"); sys.exit(2)
with tempfile.TemporaryDirectory(prefix="led-color-quoting-") as d:
    d = Path(d)
    (d / "l.cpp").write_text(harness)
    built = subprocess.run(["g++", "-std=c++14", "-O0", str(d / "l.cpp"), "-o", str(d / "l")], capture_output=True, text=True)
    if built.returncode:
        print("FAIL led-color-quoting: the extracted command does not compile\n" + built.stderr[-1500:]); sys.exit(1)
    stub = d / "ledcontrol"
    stub.write_text('#!/bin/sh\nfor a in "$@"; do printf "%s\\0" "$a"; done > "$(dirname "$0")/args"\n')
    stub.chmod(0o755)
    for colour in colours:
        cmd = subprocess.run([str(d / "l"), colour], capture_output=True, text=True).stdout
        cmd = cmd.replace("/usr/bin/ledcontrol", str(stub)).replace("/usr/bin/sh -lc", "sh -c", 1)
        for f in ("args", "CANARY"):
            (d / f).unlink(missing_ok=True)
        subprocess.run(["bwrap", "--ro-bind", "/", "/", "--bind", str(d), str(d), "--dev", "/dev", "--chdir", str(d),
                        "--clearenv", "--setenv", "PATH", "/usr/bin:/bin", "sh", "-c", cmd], capture_output=True)
        got = (d / "args").read_bytes().split(b"\0")[:-1] if (d / "args").exists() else None
        if (d / "CANARY").exists():
            failures.append("%r ran a command: %s" % (colour, cmd))
        elif got != [colour.encode()]:
            failures.append("%r arrived as %r: %s" % (colour, got, cmd))
if failures:
    for f in failures:
        print("FAIL led-color-quoting: " + f)
    sys.exit(1)
print("led-color-quoting: PASS, %d colours each one literal argument" % len(colours))
