"""Every row the FINISH RESTORE PROCESS page names is a row the menus build.

The relink page (GuiMenu::openRestoreRelink) sends the player elsewhere in
sentences such as "USE <ROW> IN GAME SETTINGS > MANAGE CLOUD STORAGE." and
"... IN NETWORK SETTINGS > FINISH RESTORE PROCESS.". Each path is walked
from the main menu: its first step must be a main-menu entry, each later
step a row the previous step's page builds, and a "USE <ROW> IN <path>" row
a row of the path's last page. A page this test cannot place fails rather
than passes (T26 PL-02, #1216: the pin's sentence named CONNECT OR REPAIR
CLOUD STORAGE, a row renamed CLOUD CONNECTIONS in c8dc72cf9).
"""
from pathlib import Path
import re
import sys

source_path = (Path(sys.argv[1]) if len(sys.argv) > 1 else
               Path(__file__).resolve().parents[1] / "es-app/src/guis/GuiMenu.cpp")
source = source_path.read_text()

def skip_branch(at):
    """From an #else or #elif line to its #endif, nested conditionals
    included: only the first branch of a conditional is walked, since the
    branches may each open a brace the other closes."""
    depth = 1
    for m in re.finditer(r'^[ \t]*#[ \t]*(if|ifdef|ifndef|endif)\b', source[at:], re.M):
        depth += -1 if m.group(1) == 'endif' else 1
        if depth == 0:
            return at + m.start()
    raise SystemExit('FAIL relink-row-names: an #else with no #endif')

def body(head):
    """The function from its head to its closing brace, braces inside
    strings, character literals, comments and later preprocessor branches
    skipped."""
    start = source.index(head)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        c = source[end]
        line_start = source.rfind('\n', 0, end) + 1
        if c == '#' and not source[line_start:end].strip() and \
                re.match(r'#[ \t]*(else|elif)\b', source[end:]):
            end = skip_branch(source.index('\n', end))
        elif source.startswith('//', end):
            end = source.index('\n', end)
        elif source.startswith('/*', end):
            end = source.index('*/', end) + 1
        elif c in '"\'':
            end += 1
            while source[end] != c:
                end += 2 if source[end] == '\\' else 1
        elif c in '{}':
            depth += 1 if c == '{' else -1
        end += 1
    return source[start:end]

ROW = re.compile(r'(?:addWithDescription|addEntry|addWithLabel)\(\s*_\("([^"]+)"\)'
                 r'|cloudAddGatedEntry\([^;]*?_\("([^"]+)"\)')
def rows(text):
    return {a or b for a, b in ROW.findall(text)}

main_menu = rows(body('GuiMenu::GuiMenu('))
# The page each row opens, by the function that builds it.
pages = {
    'GAME SETTINGS': body('void GuiMenu::openGamesSettings('),
    'NETWORK SETTINGS': body('void GuiMenu::openNetworkSettings('),
    'MANAGE CLOUD STORAGE': body('void GuiMenu::openCloud('),
}

relink = body('void GuiMenu::openRestoreRelink(')
failures, checked = [], 0
for sentence in re.findall(r'_\("((?:[^"\\]|\\.)*)"\)', relink):
    # A chain "A > B > C"; its head carries the words before the path, and
    # the path starts after the head's last UNDER or IN ("SIGN IN AGAIN
    # UNDER X" starts at X). "USE <ROW> IN <path>" names a row of the last page.
    # Each segment ends on a capital, or a greedy segment keeps the space
    # before the next ' > ' and the chain stops a step short.
    for m in re.finditer(r'[A-Z][A-Z \-]*[A-Z](?: > [A-Z][A-Z \-]*[A-Z])+', sentence):
        checked += 1
        head, *rest = [s.strip() for s in m.group(0).split(' > ')]
        keys = list(re.finditer(r'(?:^| )(?:UNDER|IN) ', head))
        if not keys:
            failures.append('%r: no UNDER or IN before the path %s' % (sentence, m.group(0)))
            continue
        steps = [head[keys[-1].end():]] + rest
        use = re.search(r'(?:^| )USE (.+)$', head[:keys[-1].start()])
        if use:
            steps.append(use.group(1).strip())
        where = 'the main menu'
        built = main_menu
        for i, step in enumerate(steps):
            if step not in built:
                failures.append('%r names %s, which %s does not build' % (sentence, step, where))
                break
            if i + 1 < len(steps):
                if step not in pages:
                    failures.append('%r: no page known for %s' % (sentence, step))
                    break
                where, built = step, rows(pages[step])
if checked == 0:
    failures.append('no path sentence found in openRestoreRelink')
if failures:
    for f in failures:
        print('FAIL relink-row-names: ' + f)
    sys.exit(1)
print('relink-row-names: PASS, %d path(s) walked' % checked)
