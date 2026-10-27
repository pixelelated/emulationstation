"""Script sentences and provider phrases reach the French.

    python3 tests/script-text-french.py [<es tree>]

T26 PL-09 (#1223): a failed maintenance run (backuptool, factoryreset) and
the match's refusal (cloud_content_restore --match) showed the scripts'
English upper-cased, whatever the language. PL-10 (#1224): WINDOWS SHARE
(SMB) and AMAZON S3 AND COMPATIBLE were never translated.

The tree's CloudText.cpp is compiled with gettext on (HAVE_INTL) and a
gettext that answers from the tree's French catalog
(locale/lang/fr/LC_MESSAGES/emulationstation2.po), the way the device's does
under system.language=fr_FR. Then:

- every backuptool and factoryreset fail() sentence, as maintenanceFailText
  says it, and a fuller sentence's why alone, reach the French;
- the match's refusals -- one with its ">>> why" and a plan line, the empty-
  folder one printed across three lines, one naming a system -- reach the
  French, with no protocol line and no English;
- providerLabel gives the two phrases in French and a name as it is;
- GuiMenu.cpp builds the two dialogs and the provider list through those,
  not by upper-casing the script's line.

Where the tree has no maintenanceFailText (the pin), the dialogs are checked
by what GuiMenu.cpp does with the script's line, and providerLabel runs.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

es = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
cloudtext = (es / "es-app/src/CloudText.cpp").read_text()
guimenu = (es / "es-app/src/guis/GuiMenu.cpp").read_text()
po = (es / "locale/lang/fr/LC_MESSAGES/emulationstation2.po").read_text()

def unescape(s):
    return s.encode().decode("unicode_escape").encode("latin-1").decode("utf-8")

catalog = {}
for m in re.finditer(r'^msgid "((?:[^"\\]|\\.)*)"\nmsgstr "((?:[^"\\]|\\.)*)"$', po, re.M):
    if m.group(2):
        catalog[m.group(1)] = m.group(2)   # kept escaped: written back into C++ as is
wanted = {k: v for k, v in catalog.items() if ('"' + k + '"') in cloudtext}

def c(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')

failures = []
composer = "maintenanceFailText" in cloudtext
if not composer:
    if "Utils::String::toUpper(result.second)" in guimenu:
        failures.append("a failed maintenance run's dialog upper-cases the script's line (runMaintenanceCommand): English whatever the language")
    if "Utils::String::toUpper(line)" in guimenu and "cloudOpenMatch" in guimenu:
        failures.append("the match's refusal upper-cases each line the script printed (cloudOpenMatch), its >>> why line included: English whatever the language")
else:
    if "Utils::String::toUpper(result.second)" in guimenu:
        failures.append("runMaintenanceCommand still upper-cases the script's line")
    if "CloudText::matchRefusalText(result.lines)" not in guimenu:
        failures.append("cloudOpenMatch does not compose its refusal through CloudText::matchRefusalText")
if "_(want.second.c_str())" in guimenu:
    failures.append("the provider list builds a msgid at run time (_(want.second.c_str())), which xgettext cannot carry")

cases = r'''
    // PL-10: the two phrases, and a name.
    expect(CloudText::providerLabel("smb"), "PARTAGE WINDOWS (SMB)", "providerLabel(smb)");
    expect(CloudText::providerLabel("s3"), "AMAZON S3 ET COMPATIBLES", "providerLabel(s3)");
    expect(CloudText::providerLabel("dropbox"), "DROPBOX", "providerLabel(dropbox), a name");
'''
if composer:
    cases += r'''
    // PL-09: every fail() sentence of the two maintenance scripts.
    int sentences = 0;
    for (auto& line : CloudText::scriptLines())
    {
        if (line.first.find('\n') != std::string::npos || line.first.find("%s") != std::string::npos)
            continue;
        if (line.first != Utils::String::toUpper(line.first))
            continue;   // the match's lines, below
        const std::string said = CloudText::maintenanceFailText(line.first, "");
        french(said, line.first);
        sentences++;
    }
    if (sentences < 25) { std::cout << "FAIL script-text-french: only " << sentences << " maintenance sentences in the table\n"; failed++; }
    // A tool's line after a known why: the why's French.
    expect(CloudText::maintenanceFailText("tar: short read", "THE RESTORE COULDN'T FINISH"), "LA RESTAURATION N’A PAS PU SE TERMINER", "a tool line after its why");
    // The match's refusals.
    expect(CloudText::matchRefusalText({ ">>> why YOUR CLOUD STORAGE ISN'T SET UP YET",
        "Your cloud storage isn't set up yet. Set it up under GAME SETTINGS > MANAGE CLOUD STORAGE first." }),
        "VOTRE STOCKAGE CLOUD N’EST PAS ENCORE CONFIGURÉ. CONFIGUREZ-LE D’ABORD DANS PARAMÈTRES DES JEUX > GÉRER LE STOCKAGE CLOUD.",
        "the refusal with its why");
    expect(CloudText::matchRefusalText({ "Refusing to change anything: your cloud's ROMs and BIOS folder looks empty.",
        "That looks the same whether it really is empty, you're offline, or the", "folder was renamed - so nothing is removed." }),
        "AUCUNE MODIFICATION : LE DOSSIER DES ROMS ET DU BIOS DE VOTRE CLOUD SEMBLE VIDE. C’EST PAREIL S’IL EST VRAIMENT VIDE, SI VOUS ÊTES HORS LIGNE OU SI LE DOSSIER A ÉTÉ RENOMMÉ - DONC RIEN N’EST SUPPRIMÉ.",
        "the empty-folder refusal, three lines said as one");
    expect(CloudText::matchRefusalText({ "nes|1|2|3", "Couldn't check snes against your cloud. Try again." }),
        "IMPOSSIBLE DE COMPARER SNES AVEC VOTRE CLOUD. RÉESSAYEZ.", "a refusal naming a system, its plan line left out");
'''
harness = r'''
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include "CloudText.h"
#include "utils/StringUtil.h"
// gettext, answering from the French catalog (only the msgids CloudText.cpp carries).
static const std::map<std::string, std::string>& frCatalog()
{
    static const std::map<std::string, std::string> m = {
''' + "".join('        { "%s", "%s" },\n' % (k, v) for k, v in wanted.items()) + r'''    };
    return m;
}
extern "C" char* gettext(const char* id)
{
    auto it = frCatalog().find(id);
    return const_cast<char*>(it == frCatalog().end() ? id : it->second.c_str());
}
static int failed = 0;
static void expect(const std::string& got, const std::string& want, const char* what)
{
    if (got != want) { std::cout << "FAIL script-text-french: " << what << ": got \"" << got << "\"\n"; failed++; }
}
// French: an accented letter or a typographic apostrophe, and not the English.
static void french(const std::string& got, const std::string& english)
{
    if (got == english || (got.find("\xc3") == std::string::npos && got.find("\xe2\x80\x99") == std::string::npos))
    { std::cout << "FAIL script-text-french: still English: " << english << "\n"; failed++; }
}
int main()
{
''' + cases + r'''
    if (failed) return 1;
    std::cout << "checked\n";
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="script-text-french-") as d:
    d = Path(d)
    (d / "fr.cpp").write_text(harness)
    srcs = [str(d / "fr.cpp"), str(es / "es-app/src/CloudText.cpp"), str(es / "es-core/src/utils/StringUtil.cpp")]
    built = subprocess.run(["g++", "-std=c++17", "-O0", "-DHAVE_INTL", "-I", str(es / "es-app/src"), "-I", str(es / "es-core/src"),
                            "-I", str(es / "external")] + srcs + ["-o", str(d / "fr")], capture_output=True, text=True)
    if built.returncode:
        print("FAIL script-text-french: CloudText.cpp with the French gettext does not build\n" + built.stderr[-2000:])
        sys.exit(1)
    run = subprocess.run([str(d / "fr")], capture_output=True, text=True)
    failures += [l[len("FAIL script-text-french: "):] for l in run.stdout.splitlines() if l.startswith("FAIL")]
    if run.returncode and not failures:
        failures.append("rc %d: %s" % (run.returncode, run.stderr[-300:]))
if failures:
    for f in failures:
        print("FAIL script-text-french: " + f)
    sys.exit(1)
print("script-text-french: PASS, %d catalog entries used" % len(wanted))
