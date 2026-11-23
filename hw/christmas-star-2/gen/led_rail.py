"""Move the LED chain sheets from +5V to the switched +5V_LED rail."""
import sys
from pathlib import Path

import kisch
from kisch import parse, dump, q, unq, find

LED_ID = "christmas-star-2:+5V_LED"

for f in sys.argv[1:]:
    p = Path(f)
    tree = parse(p.read_text(encoding="utf-8"))
    libs = find(tree, "lib_symbols")[0]
    if not any(isinstance(s, list) and len(s) > 1 and unq(s[1]) == LED_ID for s in libs):
        libs.append(kisch.custom_power(LED_ID))
    n = 0
    for sym in find(tree, "symbol"):
        lid = find(sym, "lib_id")
        if lid and unq(lid[0][1]) == "power:+5V":
            lid[0][1] = q(LED_ID)
            for prop in find(sym, "property"):
                if unq(prop[1]) == "Value":
                    prop[2] = q("+5V_LED")
            n += 1
    p.write_text(dump(tree) + "\n", encoding="utf-8", newline="\n")
    print(p.name, "moved", n, "+5V symbols to +5V_LED")
