"""A start-up flag that takes a value, given last, is refused rather than
read past the arguments (pixelelated/distribution#649, PL-011).

--videoduration, --video, --vsync and --max-vram read argv[i + 1] without
the check their neighbours make, so as the last argument each read the
array's terminating null: strcmp or atoi on it ends the process. And
--max-vram did not skip its value, which was then read as a flag. This
compiles the shipped parseArgs out of main.cpp with doubles for the
settings and the paths, and runs each case in a child process, with argv
null-terminated as the kernel hands it over: the child must not crash,
parseArgs must refuse the line, and it must say so as a refusal rather than
as the help text (main exits 0 for the help and 1 for a refusal).

    python3 tests/main-parse-args.py [path/to/main.cpp]
"""
from pathlib import Path
import subprocess
import sys
import tempfile

source_path = (Path(sys.argv[1]) if len(sys.argv) > 1 else
               Path(__file__).resolve().parents[1] / "es-app/src/main.cpp")
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


parse = definition("bool parseArgs(int argc, char* argv[])")
# The help text and a refusal are told apart since PL-011; at the pin there
# is nothing to tell them apart by, and the check below fails there.
has_help_flag = "gArgsHelpShown" in source

harness = r'''
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

struct Settings {
    static Settings* getInstance() { static Settings s; return &s; }
    void setBool(const std::string&, bool) {}
    void setInt(const std::string&, int) {}
    void setString(const std::string&, const std::string&) {}
    static void setShowHiddenFiles(bool) {}
};
struct Paths { static void setExePath(const std::string&) {} static void setHomePath(const std::string&) {} };
#define PROGRAM_VERSION_STRING "test"
#define PROGRAM_BUILT_STRING "test"
static int gPlayVideoDuration = 0;
static std::string gPlayVideo;
static bool enable_startup_game = true;
''' + ("static bool gArgsHelpShown = false;\n" if has_help_flag else "") + parse + r'''

// 0 parsed, 1 refused, 2 refused as the help text, 3 or more a signal.
static int run(std::vector<const char*> args)
{
    const pid_t child = fork();
    if (child == 0)
    {
        std::vector<char*> argv;
        for (auto* a : args) argv.push_back(const_cast<char*>(a));
        argv.push_back(nullptr);
        const bool ok = parseArgs((int) args.size(), argv.data());
''' + ("        _exit(ok ? 0 : (gArgsHelpShown ? 2 : 1));\n" if has_help_flag else "        _exit(ok ? 0 : 2);\n") + r'''
    }
    int status = 0;
    waitpid(child, &status, 0);
    if (WIFSIGNALED(status)) return 100 + WTERMSIG(status);
    return WEXITSTATUS(status);
}

int main()
{
    std::cout << std::unitbuf;
    int failures = 0;
    auto check = [&failures](bool ok, const std::string& what) { std::cout << (ok ? "ok   " : "FAIL ") << what << "\n"; if (!ok) failures++; };

    for (const char* flag : { "--videoduration", "--video", "--vsync", "--max-vram" })
    {
        const int r = run({ "emulationstation", flag });
        check(r < 100, std::string(flag) + " last: the process does not crash (" + std::to_string(r) + ")");
        check(r == 1, std::string(flag) + " last: the line is refused, not taken as the help text");
    }
    check(run({ "emulationstation", "--max-vram", "256", "--windowed" }) == 0, "--max-vram's value is skipped, not read as a flag");
    check(run({ "emulationstation", "--help" }) == 2, "--help is the help text");
    check(run({ "emulationstation", "--windowed" }) == 0, "a line without a valued flag parses");

    std::cout << "main-parse-args: " << (failures ? "FAIL" : "PASS") << "\n";
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
