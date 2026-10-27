"""The theme-set file is written whole or not at all (T26 PL-25, #1231; Backlog #1186).

    python3 tests/theme-set-writer.py [--killer <killer.so>] [GuiMenu.cpp] [es tree]

GuiMenu::saveSubsetSettings writes <user ES>/themesettings/<theme>.cfg. Its
write is extracted as written (from `if (fileData.empty())` to the end of the
statement), with the writers it calls compiled from es-core as they are:
Utils::FileSystem::writeAllText (fopen "wb", fwrite, fclose, in place) and
Utils::AtomicFile::writeText (a temporary, fsync, rename). The fixture
replaces an existing theme-set file once to count its file-system calls, then
once per call with SIGKILL just before it (the killer, LD_PRELOADed: T21's,
with stdio's calls counted). After each kill the file must hold the old text
or the new, whole (D-CLOUD-078). A kill point that leaves anything else -- an
empty or cut file -- is a FAIL.
"""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

args = sys.argv[1:]
killer = None
if args[:1] == ["--killer"]:
    killer, args = args[1], args[2:]
source = (Path(args[0]) if len(args) > 0 else Path(__file__).resolve().parents[1] / "es-app/src/guis/GuiMenu.cpp").read_text()
es = Path(args[1]) if len(args) > 1 else Path(__file__).resolve().parents[1]

# The killer, built here unless one is given (--killer): T21's stage-B
# killer.c (#668), with stdio's calls counted as well.
KILLER_C = r'''/* T26 (#673) S12, from T21's stage-B killer.c (#668; copied by git show at
 * 4df674cc82), with stdio's fopen, fwrite and fclose counted as well: glibc's
 * stdio opens and writes through internal calls a preloaded open/write never
 * sees, so without them no kill could land inside writeAllText.
 *
 * T21 (#668) stage B: kill a writer at every step (D-CLOUD-078's test).
 *
 * LD_PRELOAD'd into state-fixture. Counts the file-system calls a writer
 * makes -- open, write, fsync, fchmod, close, rename, link, unlink -- and
 * with KILL_AT=N sends the process SIGKILL just before the Nth, so nothing
 * of that call or after it happens. Without KILL_AT it only counts, and
 * prints "ops=<n>" to stderr at exit, which is how a sweep knows its range.
 *
 *   gcc -shared -fPIC -O0 -o killer.so killer.c -ldl
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

static long ops = 0;
static long killAt = -1;

static void step(void)
{
	if (killAt < 0)
	{
		const char* e = getenv("KILL_AT");
		killAt = e ? atol(e) : 0;
	}
	ops++;
	if (killAt > 0 && ops == killAt)
		kill(getpid(), SIGKILL);
}

__attribute__((destructor)) static void report(void)
{
	char line[64];
	int n = snprintf(line, sizeof line, "ops=%ld\n", ops);
	syscall(SYS_write, 2, line, (size_t) n);
}

#define NEXT(name) static __typeof__(name)* real; if (!real) real = (__typeof__(name)*) dlsym(RTLD_NEXT, #name)

int open(const char* path, int flags, ...)
{
	NEXT(open);
	mode_t mode = 0;
	if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, mode_t); va_end(ap); }
	step();
	return real(path, flags, mode);
}

int open64(const char* path, int flags, ...)
{
	NEXT(open64);
	mode_t mode = 0;
	if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, mode_t); va_end(ap); }
	step();
	return real(path, flags, mode);
}

ssize_t write(int fd, const void* buf, size_t n) { NEXT(write); step(); return real(fd, buf, n); }
int fsync(int fd) { NEXT(fsync); step(); return real(fd); }
int fchmod(int fd, mode_t mode) { NEXT(fchmod); step(); return real(fd, mode); }
int close(int fd) { NEXT(close); step(); return real(fd); }
int rename(const char* a, const char* b) { NEXT(rename); step(); return real(a, b); }
int link(const char* a, const char* b) { NEXT(link); step(); return real(a, b); }
int unlink(const char* p) { NEXT(unlink); step(); return real(p); }
FILE* fopen(const char* p, const char* m) { NEXT(fopen); step(); return real(p, m); }
FILE* fopen64(const char* p, const char* m) { NEXT(fopen64); step(); return real(p, m); }
size_t fwrite(const void* b, size_t s, size_t n, FILE* f) { NEXT(fwrite); step(); return real(b, s, n, f); }
int fclose(FILE* f) { NEXT(fclose); step(); return real(f); }
'''

fsu = (es / "es-core/src/utils/FileSystemUtil.cpp").read_text()

fn = source.index("void GuiMenu::saveSubsetSettings(")
start = source.index("\tif (fileData.empty())", fn)
# the write: through the end of the statement after its `else`
els = source.index("else", start)
# the first statement end after the else, outside a string literal: the write
end = els
while source[end] != ";":
    if source[end] == '"':
        end += 1
        while source[end] != '"':
            end += 2 if source[end] == "\\" else 1
    end += 1
end += 1
write = source[start:end]
m = re.search(r"\t\tvoid writeAllText\(const std::string& fileName, const std::string& text\)\n\t\t\{", fsu)
body_start = m.start()
depth, i = 0, fsu.index("{", body_start)
while True:
    depth += {"{": 1, "}": -1}.get(fsu[i], 0)
    if depth == 0:
        break
    i += 1
write_all = fsu[body_start:i + 1].replace("#if defined(_WIN32)", "#if 0")

harness = r'''
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include "utils/AtomicFileUtil.h"
enum LogLevel { LogError, LogWarning, LogInfo };
struct LogLine { std::ostringstream o; template <class T> LogLine& operator<<(const T& v) { o << v; return *this; } };
#define LOG(level) LogLine()
namespace Utils { namespace FileSystem {
    namespace FileCache { inline void remove(const std::string&) {} }
    inline std::string getGenericPath(const std::string& p) { return p; }
    inline bool exists(const std::string& p) { struct stat st; return ::stat(p.c_str(), &st) == 0; }
    inline bool removeFile(const std::string& p) { return std::remove(p.c_str()) == 0; }
''' + write_all + r'''
} }
static void writeThemeSet(const std::string& fileName, const std::string& fileData)
{
''' + write + r'''
}
int main(int argc, char** argv)
{
    std::ifstream in(argv[2]); std::stringstream text; text << in.rdbuf();
    writeThemeSet(argv[1], text.str());
    return 0;
}
'''
OLD = b"snes.defaultView=grid\rnes.gridSize=3x3\r" * 3
NEW = b"snes.defaultView=detailed\rnes.gridSize=4x4\rgba.defaultView=basic\r" * 3
failures = []
with tempfile.TemporaryDirectory(prefix="theme-set-writer-") as d:
    d = Path(d)
    if killer is None:
        (d / "killer.c").write_text(KILLER_C)
        subprocess.run(["gcc", "-shared", "-fPIC", "-O0", "-o", str(d / "killer.so"), str(d / "killer.c"), "-ldl"], check=True)
        killer = str(d / "killer.so")
    (d / "f.cpp").write_text(harness)
    built = subprocess.run(["g++", "-std=c++17", "-O0", "-I", str(es / "es-core/src"), str(d / "f.cpp"),
                            str(es / "es-core/src/utils/AtomicFileUtil.cpp"), "-o", str(d / "f"), "-pthread"],
                           capture_output=True, text=True)
    if built.returncode:
        print("FAIL theme-set-writer: the extracted write does not compile\n" + built.stderr[-2000:]); sys.exit(1)
    (d / "new.txt").write_bytes(NEW)
    def run(n):
        work = d / ("k%03d" % n)
        shutil.rmtree(work, ignore_errors=True)
        work.mkdir()
        cfg = work / "art-book-next.cfg"
        cfg.write_bytes(OLD)
        env = {"PATH": "/usr/bin:/bin", "LD_PRELOAD": killer}
        if n:
            env["KILL_AT"] = str(n)
        r = subprocess.run([str(d / "f"), str(cfg), str(d / "new.txt")], env=env, capture_output=True, text=True, timeout=20)
        return r, cfg, work
    r, cfg, _w = run(0)
    ops = int(re.search(r"ops=(\d+)", r.stderr).group(1))
    if ops == 0:
        # A killer that sees no call places no kill: a sweep over nothing
        # has not passed (T21's killer, without stdio's calls, sees none of
        # writeAllText's).
        failures.append("the killer saw no file-system call: no kill point could be placed")
    if cfg.read_bytes() != NEW:
        failures.append("the unkilled write did not leave the new text")
    cut = []
    for n in range(1, ops + 1):
        r, cfg, work = run(n)
        got = cfg.read_bytes() if cfg.exists() else None
        if got not in (OLD, NEW):
            cut.append("%d (%s)" % (n, "missing" if got is None else "%d bytes" % len(got)))
    print("theme-set-writer: %d file-system calls; %d kill point(s) leave the file cut" % (ops, len(cut)))
    if cut:
        failures.append("kill points that leave the theme-set file cut: " + ", ".join(cut))
if failures:
    for f in failures:
        print("FAIL theme-set-writer: " + f)
    sys.exit(1)
print("theme-set-writer: PASS")
