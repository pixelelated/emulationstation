"""Two upstream rows store where their readers read, and read the old place too.

VIDEO FILTER (GAME SETTINGS) is read at launch by setsettings.sh as
`game_setting videofilter` (the distribution's
projects/ROCKNIX/packages/rocknix/sources/scripts/setsettings.sh:793 at
8ebaf8daad), so its global value lives at global.videofilter; the row wrote
global.videofilters (T26 PL-03, #1217). GAME MEDIAS DURING FAST SCROLLING (UI
SETTINGS) is read by es-core's IList as Settings::ScrollLoadMedias, from
es_settings.cfg; the row stored it in system.cfg (T26 PL-04, #1218).

The checks read GuiMenu.cpp and IList.h for the key and the store each row
writes, then extract the rows' helpers unchanged and run them against two
small stores, the device-state fixtures: the old key alone loads, the next
save writes the new place, and an OFF saved to es_settings.cfg (which keeps
no value equal to its default) is not overruled by the old key.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
source_path = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "es-app/src/guis/GuiMenu.cpp"
ilist_path = Path(sys.argv[2]) if len(sys.argv) > 2 else root / "es-core/src/components/IList.h"
source = source_path.read_text()
READER_KEY = "global.videofilter"   # setsettings.sh: game_setting videofilter

def statement(at):
    """The statement holding source[at], from the line's start to its ';'."""
    start = source.rfind("\n", 0, at) + 1
    return source[start:source.index(";", at) + 1]

def helper(name):
    m = re.search(r"^static [^\n(]*\b%s\(" % re.escape(name), source, re.M)
    if not m:
        return None
    end = source.index("{", m.start()) + 1
    depth = 1
    while depth:
        depth += {"{": 1, "}": -1}.get(source[end], 0)
        end += 1
    return source[m.start():end]

def calls(text):
    """The text plus the bodies of the file's static helpers it calls."""
    out = text
    for name in set(re.findall(r"\b([A-Za-z_]\w*)\(", text)):
        body = helper(name)
        if body:
            out += "\n" + body
    return out

failures = []

# VIDEO FILTER: the key its save writes.
label = source.index('addWithLabel(_("VIDEO FILTER"), videofilters_choices)')
save = source.index("addSaveFunc(", label)
written = set(re.findall(r'SystemConf::getInstance\(\)->set\("([^"]+)"', calls(statement(save))))
if written != {READER_KEY}:
    failures.append("VIDEO FILTER writes %s; setsettings.sh reads %s" % (sorted(written) or "nothing it can name", READER_KEY))
# ... and the keys its load reads: the new one, and the old one devices carry.
load = source.rfind("std::string currentVideofilter =", 0, label)
read = set(re.findall(r'SystemConf::getInstance\(\)->get\("([^"]+)"', calls(statement(load)))) if load >= 0 else set()
if not {READER_KEY, "global.videofilters"} <= read:
    failures.append("VIDEO FILTER loads %s; it must read %s and the old global.videofilters" % (sorted(read) or "nothing it can name", READER_KEY))

# GAME MEDIAS DURING FAST SCROLLING: the store its save writes, against IList's.
ilist = ilist_path.read_text()
if "Settings::ScrollLoadMedias()" not in ilist:
    failures.append("IList.h no longer reads Settings::ScrollLoadMedias(); this test reads the wrong reader")
row = source.index('_("GAME MEDIAS DURING FAST SCROLLING")')
row_stmt = statement(row)
m = re.search(r'addSwitch\(_\("GAME MEDIAS DURING FAST SCROLLING"\),\s*"ScrollLoadMedias",\s*(true|false)', row_stmt)
if m:
    if m.group(1) != "true":
        failures.append("GAME MEDIAS DURING FAST SCROLLING is an addSwitch storing in system.cfg (storeInSettings false); IList reads es_settings.cfg")
else:
    save = source.index("addSaveFunc(", row)
    if 'Settings::getInstance()->setBool("ScrollLoadMedias"' not in calls(statement(save)):
        failures.append("GAME MEDIAS DURING FAST SCROLLING's save does not write Settings ScrollLoadMedias")
    # ... and the switch's state loads from both stores.
    var = re.search(r'_\("GAME MEDIAS DURING FAST SCROLLING"\),\s*(\w+)\)', row_stmt)
    state = source.rfind("%s->setState(" % var.group(1), 0, row) if var else -1
    loaded = calls(statement(state)) if state >= 0 else ""
    if 'Settings::getInstance()->getBool("ScrollLoadMedias")' not in loaded or 'SystemConf::getInstance()->get("ScrollLoadMedias")' not in loaded:
        failures.append("GAME MEDIAS DURING FAST SCROLLING's switch does not load from es_settings.cfg and the old system.cfg key")

# The device-state fixtures, over the helpers as written.
names = ["globalVideoFilter", "setGlobalVideoFilter", "scrollLoadMedias", "setScrollLoadMedias"]
bodies = [helper(n) for n in names]
if None in bodies:
    failures.append("no helper %s to run the device-state fixtures against" % ", ".join(n for n, b in zip(names, bodies) if b is None))
else:
    harness = r'''
#include <cassert>
#include <iostream>
#include <map>
#include <string>
// system.cfg: a key=value store; getBool(name) is get(name) == "1".
struct SystemConf {
    std::map<std::string, std::string> v;
    static SystemConf* getInstance() { static SystemConf c; return &c; }
    std::string get(const std::string& k) { auto i = v.find(k); return i == v.end() ? "" : i->second; }
    bool set(const std::string& k, const std::string& x) { bool c = get(k) != x; v[k] = x; return c; }
    bool getBool(const std::string& k) { return get(k) == "1"; }
    bool setBool(const std::string& k, bool b) { return set(k, b ? "1" : "0"); }
};
// es_settings.cfg: an absent bool reads false, and a reload keeps no value
// equal to that default (Settings::saveFile's saveMap).
struct Settings {
    std::map<std::string, bool> v;
    static Settings* getInstance() { static Settings s; return &s; }
    bool getBool(const std::string& k) { auto i = v.find(k); return i != v.end() && i->second; }
    bool setBool(const std::string& k, bool b) { bool c = getBool(k) != b; v[k] = b; return c; }
    void reload() { for (auto i = v.begin(); i != v.end();) i = i->second ? std::next(i) : v.erase(i); }
};
static SystemConf& conf = *SystemConf::getInstance();
static Settings& settings = *Settings::getInstance();
static void reset() { conf.v.clear(); settings.v.clear(); }
''' + "\n".join(bodies) + r'''
int main()
{
    int cases = 0;
    // VIDEO FILTER: the old key alone loads; the save writes the new key and removes nothing.
    reset(); conf.v["global.videofilters"] = "crt";
    assert(globalVideoFilter() == "crt");
    setGlobalVideoFilter(globalVideoFilter());
    assert(conf.get("global.videofilter") == "crt" && conf.get("global.videofilters") == "crt"); cases++;
    // The new key wins over the old.
    reset(); conf.v["global.videofilter"] = "none"; conf.v["global.videofilters"] = "crt";
    assert(globalVideoFilter() == "none"); cases++;
    // Neither: empty, which the row shows as AUTO.
    reset(); assert(globalVideoFilter().empty()); cases++;
    // SCROLL: the old ON alone loads ON, and the save writes es_settings.cfg.
    reset(); conf.v["ScrollLoadMedias"] = "1";
    assert(scrollLoadMedias());
    setScrollLoadMedias(true); settings.reload();
    assert(settings.getBool("ScrollLoadMedias") && scrollLoadMedias()); cases++;
    // Turned OFF: es_settings.cfg keeps nothing, and the old key no longer says ON.
    setScrollLoadMedias(false); settings.reload();
    assert(settings.v.count("ScrollLoadMedias") == 0 && !scrollLoadMedias()); cases++;
    // A device that never wrote the old key: the save creates none.
    reset(); setScrollLoadMedias(true); settings.reload();
    assert(scrollLoadMedias() && conf.v.count("ScrollLoadMedias") == 0); cases++;
    // es_settings.cfg ON over an old OFF.
    reset(); conf.v["ScrollLoadMedias"] = "0"; settings.v["ScrollLoadMedias"] = true;
    assert(scrollLoadMedias()); cases++;
    // Neither: OFF, the default.
    reset(); assert(!scrollLoadMedias()); cases++;
    std::cout << cases << " fixture cases\n";
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix="upstream-row-stores-") as d:
        cpp, exe = Path(d) / "rows.cpp", Path(d) / "rows"
        cpp.write_text(harness)
        subprocess.run(["g++", "-std=c++14", "-O0", "-g", "-fsanitize=address,undefined", str(cpp), "-o", str(exe)], check=True)
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        if run.returncode:
            failures.append("a device-state fixture failed: " + (run.stderr.strip().splitlines() or ["rc %d" % run.returncode])[-1])
        else:
            fixtures = run.stdout.strip()

if failures:
    for f in failures:
        print("FAIL upstream-row-stores: " + f)
    sys.exit(1)
print("upstream-row-stores: PASS, VIDEO FILTER writes %s, GAME MEDIAS DURING FAST SCROLLING writes es_settings.cfg, %s" % (READER_KEY, fixtures))
