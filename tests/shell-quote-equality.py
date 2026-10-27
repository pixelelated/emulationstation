"""One quoting function: Utils::String::shellQuote, and cloudShellQuote while it exists.

    python3 tests/shell-quote-equality.py [GuiMenu.cpp] [StringUtil.cpp]

T26 PL-21 (#1225's stream): GuiMenu.cpp's cloudShellQuote and es-core's
Utils::String::shellQuote were one algorithm in two functions. Before one
replaced the other this guard compared them over a corpus of names and
secrets -- quotes, $( ), backticks, backslashes, spaces, newlines, UTF-8, the
empty string -- and both must give the same bytes. On every run the shell
must also give each value back unchanged through shellQuote: sh -c 'printf
%s <quoted>' prints the value itself. After the replacement only shellQuote
remains, and the second half is the check.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
guimenu = (Path(sys.argv[1]) if len(sys.argv) > 1 else root / "es-app/src/guis/GuiMenu.cpp").read_text()
stringutil = (Path(sys.argv[2]) if len(sys.argv) > 2 else root / "es-core/src/utils/StringUtil.cpp").read_text()

def function(text, head):
    """The definition, never a declaration (a ';' before its '{')."""
    m = next((m for m in re.finditer(head, text, re.M)
              if text.index("{", m.start()) < text.index(";", m.start())), None)
    if not m:
        return None
    end = text.index("{", m.start()) + 1
    depth = 1
    while depth:
        depth += {"{": 1, "}": -1}.get(text[end], 0)
        end += 1
    return text[m.start():end]

shell = function(stringutil, r"^\s*std::string shellQuote\(const std::string& _string\)")
cloud = function(guimenu, r"^static std::string cloudShellQuote\(const std::string& value\)")
corpus = ["", "alpha", "two words", "it's", "''", "'\"'", "$(reboot)", "`id`", "a\\b", "back\\'slash",
          "tab\there", "new\nline", "café ü 名前", "--name", "-n", "*", "~root", "a;b|c&d", "pass=sec'ret$x", "'" * 5]
harness = "#include <iostream>\n#include <string>\n#include <vector>\nnamespace Utils { namespace String {\n" + shell + "\n}}\n"
if cloud:
    harness += cloud + "\n"
harness += r'''
int main()
{
    const std::vector<std::string> corpus = {
''' + "".join("        std::string(%s, %d),\n" % ('"' + "".join("\\x%02x" % b for b in s.encode()) + '"', len(s.encode())) for s in corpus) + r'''    };
    int differ = 0;
    for (const auto& v : corpus)
    {
        const std::string q = Utils::String::shellQuote(v);
''' + (r'''        if (cloudShellQuote(v) != q) { std::cout << "DIFFER\n"; differ++; }
''' if cloud else "") + r'''        std::cout << "Q" << q.size() << ":" << q << "\n";
    }
    return differ ? 1 : 0;
}
'''
failures = []
with tempfile.TemporaryDirectory(prefix="shell-quote-equality-") as d:
    d = Path(d)
    (d / "q.cpp").write_text(harness)
    subprocess.run(["g++", "-std=c++14", "-O0", str(d / "q.cpp"), "-o", str(d / "q")], check=True)
    run = subprocess.run([str(d / "q")], capture_output=True)
    if run.returncode:
        failures.append("cloudShellQuote and shellQuote differ on %d of %d values" % (run.stdout.count(b"DIFFER\n"), len(corpus)))
    out = run.stdout.replace(b"DIFFER\n", b"")   # the count above; no corpus value holds it
    quoted = []
    while out:
        m = re.match(rb"Q(\d+):", out)
        n = int(m.group(1)); quoted.append(out[m.end():m.end() + n]); out = out[m.end() + n + 1:]
    for value, q in zip(corpus, quoted):
        back = subprocess.run(["sh", "-c", "printf %s " + q.decode()], capture_output=True).stdout.decode()
        if back != value:
            failures.append("the shell gave %r back as %r" % (value, back))
if failures:
    for f in failures:
        print("FAIL shell-quote-equality: " + f)
    sys.exit(1)
print("shell-quote-equality: PASS, %d values%s" % (len(corpus), ", cloudShellQuote and shellQuote byte-equal" if cloud else ", shellQuote alone (cloudShellQuote gone)"))
