"""Write KiCad 8 schematic sheets from Python.

Symbols come from KiCad's own libraries (embedded into the sheet the way
eeschema does it). Parts are connected with power symbols and net labels
placed exactly on the pin ends, so no wire geometry is needed.
"""
import re
import uuid as _uuid
from pathlib import Path

KICAD_SYM = Path(r"C:/Program Files/KiCad/8.0/share/kicad/symbols")
GRID = 1.27


# ---- S-expressions ------------------------------------------------------------

def parse(text):
    tokens = re.findall(r'"(?:\\.|[^"\\])*"|\(|\)|[^\s()"]+', text)
    stack, cur = [], []
    for t in tokens:
        if t == "(":
            stack.append(cur)
            cur = []
        elif t == ")":
            done = cur
            cur = stack.pop()
            cur.append(done)
        else:
            cur.append(t)
    return cur[0]


def dump(node, indent=0):
    if not isinstance(node, list):
        return node
    pad = "\t" * indent
    if all(not isinstance(x, list) for x in node):
        return "(" + " ".join(node) + ")"
    head = [x for x in node[:2] if not isinstance(x, list)]
    lead = len(head) if len(head) == 2 or (node and not isinstance(node[0], list)) else 0
    lead = 0
    while lead < len(node) and not isinstance(node[lead], list):
        lead += 1
    out = "(" + " ".join(node[:lead])
    for x in node[lead:]:
        out += "\n" + pad + "\t" + dump(x, indent + 1)
    return out + "\n" + pad + ")"


def q(s):
    return '"' + str(s).replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def unq(s):
    return s[1:-1].replace('\\"', '"').replace("\\\\", "\\") if s.startswith('"') else s


def uid():
    return str(_uuid.uuid4())


def find(node, key):
    return [x for x in node if isinstance(x, list) and x and x[0] == key]


# ---- library symbols ------------------------------------------------------------

_libs = {}


def _lib(name):
    if name not in _libs:
        _libs[name] = parse((KICAD_SYM / f"{name}.kicad_sym").read_text(encoding="utf-8"))
    return _libs[name]


def lib_symbol(lib_id, extra_lib=None):
    """Flattened symbol definition named 'Lib:Name' (as embedded in a sheet)."""
    lib, name = lib_id.split(":")
    tree = extra_lib if extra_lib is not None and lib == "christmas-star-2" else _lib(lib)
    syms = {unq(s[1]): s for s in find(tree, "symbol")}
    sym = syms[name]
    ext = find(sym, "extends")
    if ext:
        parent = lib_symbol(f"{lib}:{unq(ext[0][1])}", extra_lib)
        pname = unq(parent[1]).split(":")[1]
        body = []
        for x in parent[2:]:
            if isinstance(x, list) and x[0] == "property":
                continue
            if isinstance(x, list) and x[0] == "symbol":
                x = ["symbol", q(unq(x[1]).replace(pname + "_", name + "_", 1))] + x[2:]
            body.append(x)
        props = [x for x in sym[2:] if isinstance(x, list) and x[0] == "property"]
        others = [x for x in sym[2:] if isinstance(x, list) and x[0] not in ("property", "extends")]
        head = [x for x in body if isinstance(x, list) and x[0] not in ("symbol",)]
        units = [x for x in body if isinstance(x, list) and x[0] == "symbol"]
        flat = ["symbol", q(lib_id)] + head + props + others + units
    else:
        flat = ["symbol", q(lib_id)] + sym[2:]
    return flat


def pins_of(symdef):
    """[(number, name, x, y, angle, unit, hidden, etype)] of a flattened symbol."""
    out = []
    for u in find(symdef, "symbol"):
        m = re.search(r"_(\d+)_(\d+)$", unq(u[1]))
        unit = int(m.group(1)) if m else 0
        for p in find(u, "pin"):
            at = find(p, "at")[0]
            num = unq(find(p, "number")[0][1])
            name = unq(find(p, "name")[0][1])
            hidden = "hide" in p
            out.append((num, name, float(at[1]), float(at[2]), float(at[3]) if len(at) > 3 else 0.0,
                        unit, hidden, p[1]))
    return out


# ---- a sheet ------------------------------------------------------------------------

POWER = {"GND", "+5V", "+3V3", "VBUS", "+5V_LED", "PWR_FLAG"}


class Sheet:
    def __init__(self, project, sheet_path, title, paper="A3", ref_base=600):
        self.ref_base = ref_base
        self.project, self.path, self.title, self.paper = project, sheet_path, title, paper
        self.uuid = uid()
        self.libs = {}
        self.items = []
        self.refs = {}
        self.extra_lib = None
        self.texts = []
        self.pwr = 0

    def _need(self, lib_id):
        if lib_id not in self.libs:
            if lib_id in CUSTOM_POWER:
                self.libs[lib_id] = custom_power(lib_id)
            else:
                self.libs[lib_id] = lib_symbol(lib_id, self.extra_lib)
        return self.libs[lib_id]

    def _instance(self, lib_id, ref, value, x, y, footprint="", fields=None, rot=0, hide_ref=False,
                  power=False):
        sym = self._need(lib_id)
        props = [("Reference", ref), ("Value", value), ("Footprint", footprint),
                 ("Datasheet", ""), ("Description", "")]
        props += list((fields or {}).items())
        s = ["symbol", ["lib_id", q(lib_id)], ["at", f"{x:.2f}", f"{y:.2f}", str(rot)],
             ["unit", "1"], ["exclude_from_sim", "no"], ["in_bom", "no" if power else "yes"],
             ["on_board", "no" if power else "yes"], ["dnp", "no"], ["uuid", q(uid())]]
        place = self._text_places(sym, x, y, rot, power)
        for i, (k, v) in enumerate(props):
            hide = k not in ("Reference", "Value") or (power and k == "Reference") or (hide_ref and k == "Reference")
            eff = ["effects", ["font", ["size", "1.27", "1.27"]]]
            px, py, just = place.get(k, (x, y, None))
            if just:
                eff.append(["justify", just])
            if hide:
                eff.append("hide")
            s.append(["property", q(k), q(v), ["at", f"{px:.2f}", f"{py:.2f}", "0"], eff])
        for p in pins_of(sym):
            s.append(["pin", q(p[0]), ["uuid", q(uid())]])
        s.append(["instances", ["project", q(self.project), ["path", q(self.path), ["reference", q(ref)], ["unit", "1"]]]])
        self.items.append(s)
        return sym

    @staticmethod
    def _text_places(sym, x, y, rot, power):
        """Where Reference and Value go, clear of the body and of the pins' labels."""
        if power:
            # The value sits beyond the symbol's graphic, away from the pin.
            off = {0: (0, -3.3), 180: (0, 3.3), 90: (-3.3, 0), 270: (3.3, 0)}[rot % 360]
            if "GND" in str(sym[1]) and "PWR_FLAG" not in str(sym[1]):
                off = {0: (0, 3.3), 180: (0, -3.3), 90: (3.3, 0), 270: (-3.3, 0)}[rot % 360]
            return {"Value": (x + off[0], y + off[1], None), "Reference": (x, y, None)}
        pins = [(p[2], p[3]) for p in pins_of(sym)]
        xs, ys = [p[0] for p in pins], [p[1] for p in pins]
        if pins and max(xs) == min(xs):
            # A small two-pin part drawn upright: texts to its right.
            return {"Reference": (x + 2.0, y - 0.8, "left"), "Value": (x + 2.0, y + 0.8, "left")}
        top, bottom = y - max(ys), y - min(ys)
        # Beyond the pins and the power symbols on them.
        return {"Reference": (x, top - 6.0, None), "Value": (x, bottom + 7.0, None)}

    # A part; nets: {pin number or name: net}. Net "NC" = no-connect marker.
    def part(self, lib_id, ref, value, x, y, nets, footprint="", lcsc="", fields=None):
        f = dict(fields or {})
        if lcsc:
            f["LCSC Part #"] = lcsc
        x, y = round(x / GRID) * GRID, round(y / GRID) * GRID
        sym = self._instance(lib_id, ref, value, x, y, footprint, f)
        done = set()
        pins = pins_of(sym)
        names = {p[0] for p in pins}
        for key, net in nets.items():
            match = [p for p in pins if p[0] == key] if key in names else [p for p in pins if p[1] == key]
            if not match:
                raise KeyError(f"{ref}: no pin {key}")
            for num, name, px, py, ang, unit, hidden, et in match:
                ex, ey = x + px, y - py
                spot = (round(ex, 2), round(ey, 2))
                if spot in done:
                    continue
                done.add(spot)
                if net == "NC" and et == "no_connect":
                    continue  # already "not connected" by its type
                self.attach(net, ex, ey, ang)
        missing = [p for p in pins if p[0] not in nets and p[1] not in nets]
        if missing:
            raise KeyError(f"{ref}: pins not assigned: {sorted({p[0] + '/' + p[1] for p in missing})}")
        return sym

    def attach(self, net, x, y, pin_angle):
        # Direction from the body towards the pin end, in sheet coordinates.
        d = {0: "left", 90: "down", 180: "right", 270: "up"}[int(pin_angle) % 360]
        if net == "NC":
            self.items.append(["no_connect", ["at", f"{x:.2f}", f"{y:.2f}"], ["uuid", q(uid())]])
        elif net in POWER:
            lib = "power:GND" if net == "GND" else f"power:{net}"
            if net in ("+5V_LED", "VBUS"):
                lib = "power:VBUS" if net == "VBUS" else "power:+5V"
            down = net == "GND"
            rot = {("left", True): 270, ("right", True): 90, ("up", True): 180, ("down", True): 0,
                   ("left", False): 90, ("right", False): 270, ("up", False): 0, ("down", False): 180}[(d, down)]
            self.pwr += 1
            n = self.pwr
            if net == "+5V_LED":
                self._power_custom(x, y, rot, n)
            else:
                self._instance(lib, f"#PWR{self.ref_base + n}", net if net != "VBUS" else "VBUS", x, y, rot=rot, power=True)
        else:
            ang = {"left": 180, "right": 0, "up": 90, "down": 270}[d]
            kind = "global_label" if net.isupper() or net.startswith("/") is False and net in GLOBAL else "label"
            kind = "global_label" if net in GLOBAL else "label"
            just = "right" if ang == 180 else "left" if ang == 0 else ("left" if ang == 90 else "right")
            lab = [kind, q(net)]
            if kind == "global_label":
                lab.append(["shape", "bidirectional"])
            lab += [["at", f"{x:.2f}", f"{y:.2f}", str(ang)],
                    ["effects", ["font", ["size", "1.27", "1.27"]], ["justify", just]],
                    ["uuid", q(uid())]]
            self.items.append(lab)

    def _power_custom(self, x, y, rot, n):
        # +5V_LED: its own power symbol (the net name comes from the hidden pin's name).
        self._instance("christmas-star-2:+5V_LED", f"#PWR{self.ref_base + n}", "+5V_LED", x, y, rot=rot, power=True)

    def flag(self, net, x, y):
        x, y = round(x / GRID) * GRID, round(y / GRID) * GRID
        self._instance("power:PWR_FLAG", f"#FLG0{len([i for i in self.items if 'PWR_FLAG' in str(i[1])]) + 1:02d}",
                       "PWR_FLAG", x, y, power=True)
        # The flag points up; its net's power symbol points down, so they do not overlap.
        self.attach(net, x, y, 90)

    def text(self, s, x, y, size=1.5):
        self.items.append(["text", q(s), ["exclude_from_sim", "no"], ["at", f"{x:.2f}", f"{y:.2f}", "0"],
                           ["effects", ["font", ["size", str(size), str(size)]], ["justify", "left", "top"]],
                           ["uuid", q(uid())]])

    def write(self, path):
        lib = ["lib_symbols"] + list(self.libs.values())
        tree = ["kicad_sch", ["version", "20231120"], ["generator", q("eeschema")],
                ["generator_version", q("8.0")], ["uuid", q(self.uuid)], ["paper", q(self.paper)],
                ["title_block", ["title", q(self.title)], ["company", q("Christmas Star, hardware 2.0")]],
                lib] + self.items
        Path(path).write_text(dump(tree) + "\n", encoding="utf-8", newline="\n")


GLOBAL = set()
CUSTOM_POWER = {"christmas-star-2:+5V_LED": ("+5V", "+5V_LED")}


def custom_power(lib_id):
    """A power symbol copied from power:<base>, with its own name (= its own net)."""
    base, name = CUSTOM_POWER[lib_id]
    text = dump(lib_symbol(f"power:{base}"))
    text = text.replace(f'"power:{base}"', q(lib_id), 1)
    text = text.replace(f'(symbol "{base}_', f'(symbol "{name}_')
    text = text.replace(f'(name "{base}"', f'(name "{name}"')
    text = re.sub(r'\(property "Value" "[^"]*"', f'(property "Value" "{name}"', text, count=1)
    return parse(text)
