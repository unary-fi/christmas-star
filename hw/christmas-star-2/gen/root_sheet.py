"""Hardware 2.0 root sheet: keep the LED chain sheets, add the new sheets."""
import json
import re
import sys
from pathlib import Path

from kisch import parse, dump, q, unq, find

PROJ = Path(sys.argv[1])          # hw/christmas-star-2
NEW = json.loads(sys.argv[2])     # [[uuid, name, file], ...]
KEEP = {"chain_a_leds_1-66.kicad_sch", "chain_b_leds_1-66.kicad_sch"}

root_file = PROJ / "christmas-star-2.kicad_sch"
text = root_file.read_text(encoding="utf-8")
tree = parse(text)
root_uuid = unq(find(tree, "uuid")[0][1])


def sheetfile(sh):
    for p in find(sh, "property"):
        if unq(p[1]) == "Sheetfile":
            return unq(p[2])


out = [x for x in tree if not (isinstance(x, list) and x[0] == "sheet" and sheetfile(x) not in KEEP)]
# Insert the new sheets before sheet_instances.
idx = next(i for i, x in enumerate(out) if isinstance(x, list) and x[0] == "sheet_instances")
for k, (suid, name, fname) in enumerate(NEW):
    x, y = 25.4 + 58.42 * (k % 4), 76.2 + 40.64 * (k // 4)
    sheet = ["sheet", ["at", f"{x:.2f}", f"{y:.2f}"], ["size", "50.8", "25.4"], ["fields_autoplaced", "yes"],
             ["stroke", ["width", "0.1524"], ["type", "solid"]], ["fill", ["color", "0", "0", "0", "0.0000"]],
             ["uuid", q(suid)],
             ["property", q("Sheetname"), q(name), ["at", f"{x:.2f}", f"{y - 0.71:.2f}", "0"],
              ["effects", ["font", ["size", "1.27", "1.27"]], ["justify", "left", "bottom"]]],
             ["property", q("Sheetfile"), q(fname), ["at", f"{x:.2f}", f"{y + 25.98:.2f}", "0"],
              ["effects", ["font", ["size", "1.27", "1.27"]], ["justify", "left", "top"]]],
             ["instances", ["project", q("christmas-star-2"), ["path", q(f"/{root_uuid}"), ["page", q(str(4 + k))]]]]]
    out.insert(idx, sheet)
    idx += 1

text = dump(out) + "\n"
text = text.replace('(project "christmas-star"', '(project "christmas-star-2"')
root_file.write_text(text, encoding="utf-8", newline="\n")

for f in KEEP:
    p = PROJ / f
    t = p.read_text(encoding="utf-8").replace('(project "christmas-star"', '(project "christmas-star-2"')
    p.write_text(t, encoding="utf-8", newline="\n")
print("root", root_uuid, "sheets:", [sheetfile(x) for x in out if isinstance(x, list) and x[0] == "sheet"])
