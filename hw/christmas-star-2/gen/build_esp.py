"""ESP32-C6-WROOM-1: KiCad symbol and footprint for the hardware 2.0 project library.

From the ESP32-C6-WROOM-1 & WROOM-1U datasheet v1.4: Table 3-1 (pins), Figure 3-1
(pin layout) and Figure 11-1 (recommended PCB land pattern).
"""
import sys
import uuid
from pathlib import Path

import kisch
from kisch import parse, dump, q, find, unq

P = Path(sys.argv[1])
NAME = "ESP32-C6-WROOM-1"
DATASHEET = "https://documentation.espressif.com/esp32-c6-wroom-1_wroom-1u_datasheet_en.pdf"

# (number, name, type) from Table 3-1.
PINS = [
    ("1", "GND", "power_in"), ("2", "3V3", "power_in"), ("3", "EN", "input"),
    ("4", "IO4/MTMS", "bidirectional"), ("5", "IO5/MTDI", "bidirectional"),
    ("6", "IO6/MTCK", "bidirectional"), ("7", "IO7/MTDO", "bidirectional"),
    ("8", "IO0", "bidirectional"), ("9", "IO1", "bidirectional"), ("10", "IO8", "bidirectional"),
    ("11", "IO10", "bidirectional"), ("12", "IO11", "bidirectional"),
    ("13", "IO12/USB_D-", "bidirectional"), ("14", "IO13/USB_D+", "bidirectional"),
    ("15", "IO9", "bidirectional"), ("16", "IO18", "bidirectional"), ("17", "IO19", "bidirectional"),
    ("18", "IO20", "bidirectional"), ("19", "IO21", "bidirectional"), ("20", "IO22", "bidirectional"),
    ("21", "IO23", "bidirectional"), ("22", "NC", "no_connect"), ("23", "IO15", "bidirectional"),
    ("24", "RXD0/IO17", "bidirectional"), ("25", "TXD0/IO16", "bidirectional"),
    ("26", "IO3", "bidirectional"), ("27", "IO2", "bidirectional"), ("28", "GND", "power_in"),
    ("29", "GND", "power_in"),
]


def font(size=1.27):
    return ["effects", ["font", ["size", str(size), str(size)]]]


def prop(k, v, x, y, hide=False):
    eff = font()
    if hide:
        eff.append("hide")
    return ["property", q(k), q(v), ["at", str(x), str(y), "0"], eff]


# ---- symbol: pins placed as on the module (Figure 3-1) -----------------------------
pins = []
for num, name, kind in PINS:
    n = int(num)
    if n <= 14:      # left side, 1 at the top
        x, y, ang = -17.78, 16.51 - 2.54 * (n - 1), 0
    elif n <= 28:    # right side, 28 at the top
        x, y, ang = 17.78, 16.51 - 2.54 * (28 - n), 180
    else:            # exposed pad, bottom
        x, y, ang = 0, -24.13, 90
    style = "line"
    pins.append(["pin", kind, style, ["at", f"{x:g}", f"{y:g}", str(ang)], ["length", "5.08"],
                 ["name", q(name), font()], ["number", q(num), font()]])

symbol = ["symbol", q(NAME), ["pin_names", ["offset", "1.016"]], ["exclude_from_sim", "no"],
          ["in_bom", "yes"], ["on_board", "yes"],
          prop("Reference", "U", 0, 21.59), prop("Value", NAME, 0, -27.94),
          prop("Footprint", f"christmas-star-2:{NAME}", 0, -30.48, True),
          prop("Datasheet", DATASHEET, 0, -33.02, True),
          prop("Description", "ESP32-C6 module, Wi-Fi 6, Bluetooth 5 LE, 802.15.4, 8 MB flash, PCB antenna", 0, -35.56, True),
          prop("LCSC Part #", "", 0, -38.1, True),
          ["symbol", q(f"{NAME}_0_1"),
           ["rectangle", ["start", "-12.7", "19.05"], ["end", "12.7", "-19.05"],
            ["stroke", ["width", "0.254"], ["type", "default"]], ["fill", ["type", "background"]]]],
          ["symbol", q(f"{NAME}_1_1")] + pins]

lib_path = P / "christmas-star-2.kicad_sym"
lib = parse(lib_path.read_text(encoding="utf-8"))
lib = [x for x in lib if not (isinstance(x, list) and x[0] == "symbol" and unq(x[1]) == NAME)]
lib.append(symbol)
lib_path.write_text(dump(lib) + "\n", encoding="utf-8", newline="\n")

# ---- footprint: Figure 11-1, origin at the module centre ------------------------------
W, H = 18.0, 25.5
PITCH, TOP_PAD = 1.27, 7.49          # pad 1 centre below the top edge
PAD_X = (W / 2 - 0.25)               # pads reach 0.5 beyond the edge: centres 17.5 apart
EP_X, EP_Y = 7.495 - W / 2, (H - 12.29) - H / 2   # exposed pad centre
ANT = 6.0                            # antenna area at the top


def u():
    return ["uuid", q(str(uuid.uuid4()))]


def line(layer, x1, y1, x2, y2, w=0.1):
    return ["fp_line", ["start", f"{x1:g}", f"{y1:g}"], ["end", f"{x2:g}", f"{y2:g}"],
            ["stroke", ["width", str(w)], ["type", "default"]], ["layer", q(layer)], u()]


def rect(layer, x1, y1, x2, y2, w=0.1):
    return ["fp_rect", ["start", f"{x1:g}", f"{y1:g}"], ["end", f"{x2:g}", f"{y2:g}"],
            ["stroke", ["width", str(w)], ["type", "default"]], ["fill", "none"], ["layer", q(layer)], u()]


def text(kind, val, x, y, layer, size=1.0, hide=False):
    e = ["effects", ["font", ["size", str(size), str(size)], ["thickness", "0.15"]]]
    node = ["property", q(kind), q(val), ["at", f"{x:g}", f"{y:g}", "0"], ["layer", q(layer)]]
    if hide:
        node.append(["hide", "yes"])
    return node + [u(), e]


fp = ["footprint", q(NAME), ["version", "20240108"], ["generator", q("pcbnew")], ["generator_version", q("8.0")],
      ["layer", q("F.Cu")],
      ["descr", q("ESP32-C6-WROOM-1, PCB antenna, 18 x 25.5 mm; datasheet v1.4 Figure 11-1. " + DATASHEET)],
      ["tags", q("ESP32-C6 WROOM-1 Espressif module")],
      text("Reference", "REF**", 0, -H / 2 - 1.5, "F.SilkS"),
      text("Value", NAME, 0, H / 2 + 1.5, "F.Fab"),
      text("Footprint", "", 0, 0, "F.Fab", hide=True),
      text("Datasheet", DATASHEET, 0, 0, "F.Fab", hide=True),
      text("Description", "ESP32-C6-WROOM-1 module", 0, 0, "F.Fab", hide=True),
      ["attr", "smd"]]
fp.append(rect("F.Fab", -W / 2, -H / 2, W / 2, H / 2))
fp.append(line("F.Fab", -W / 2, -H / 2 + ANT, W / 2, -H / 2 + ANT))
fp.append(["fp_text", "user", q("ANTENNA: no copper"), ["at", "0", f"{-H / 2 + ANT / 2:g}", "0"],
           ["layer", q("F.Fab")], u(), ["effects", ["font", ["size", "0.8", "0.8"], ["thickness", "0.12"]]]])
fp.append(rect("F.CrtYd", -W / 2 - 0.75, -H / 2 - 0.5, W / 2 + 0.75, H / 2 + 0.5, 0.05))
# Silkscreen: the outline outside the pad rows, a mark at pin 1.
s = 0.12
for x in (-W / 2 - s, W / 2 + s):
    fp.append(line("F.SilkS", x, -H / 2 - s, x, -H / 2 + TOP_PAD - 1.2, s))
    fp.append(line("F.SilkS", x, H / 2 - 0.8, x, H / 2 + s, s))
fp.append(line("F.SilkS", -W / 2 - s, -H / 2 - s, W / 2 + s, -H / 2 - s, s))
fp.append(line("F.SilkS", -W / 2 - s, H / 2 + s, W / 2 + s, H / 2 + s, s))
fp.append(line("F.SilkS", -W / 2 - 1.2, -H / 2 + TOP_PAD, -W / 2 - 0.6, -H / 2 + TOP_PAD, s))

for num, name, kind in PINS:
    n = int(num)
    if n == 29:
        continue
    if n <= 14:
        x, y = -PAD_X, -H / 2 + TOP_PAD + PITCH * (n - 1)
    else:
        x, y = PAD_X, -H / 2 + TOP_PAD + PITCH * (28 - n)
    fp.append(["pad", q(num), "smd", "rect", ["at", f"{x:.4g}", f"{y:.4g}"], ["size", "1.5", "0.9"],
               ["layers", q("F.Cu"), q("F.Paste"), q("F.Mask")], u()])
# Exposed ground pad: 3 x 3 squares of 0.8 mm with 0.45 mm gaps (3.3 mm overall).
for i in (-1, 0, 1):
    for j in (-1, 0, 1):
        fp.append(["pad", q("29"), "smd", "rect", ["at", f"{EP_X + 1.25 * i:.4g}", f"{EP_Y + 1.25 * j:.4g}"],
                   ["size", "0.8", "0.8"], ["layers", q("F.Cu"), q("F.Paste"), q("F.Mask")], u()])
# Antenna keep-out on every copper layer (the board must also keep its margin around it).
y0, y1 = -H / 2, -H / 2 + ANT
fp.append(["zone", ["net", "0"], ["net_name", q("")], ["layers", q("*.Cu")], u(), ["name", q("ESP32 antenna")],
           ["hatch", "full", "0.5"], ["connect_pads", ["clearance", "0"]], ["min_thickness", "0.25"],
           ["filled_areas_thickness", "no"],
           ["keepout", ["tracks", "not_allowed"], ["vias", "not_allowed"], ["pads", "not_allowed"],
            ["copperpour", "not_allowed"], ["footprints", "allowed"]],
           ["fill", ["thermal_gap", "0.5"], ["thermal_bridge_width", "0.5"]],
           ["polygon", ["pts", ["xy", f"{-W / 2:g}", f"{y0:g}"], ["xy", f"{W / 2:g}", f"{y0:g}"],
                        ["xy", f"{W / 2:g}", f"{y1:g}"], ["xy", f"{-W / 2:g}", f"{y1:g}"]]]])

pretty = P / "christmas-star-2.pretty"
pretty.mkdir(exist_ok=True)
(pretty / f"{NAME}.kicad_mod").write_text(dump(fp) + "\n", encoding="utf-8", newline="\n")
(P / "fp-lib-table").write_text(
    '(fp_lib_table\n\t(version 7)\n\t(lib (name "christmas-star-2")(type "KiCad")(uri "${KIPRJMOD}/christmas-star-2.pretty")(options "")(descr "Christmas Star hardware 2.0 footprints"))\n)\n',
    encoding="utf-8", newline="\n")
print("symbol", NAME, "with", len(PINS), "pins; footprint with", len([x for x in fp if x[0] == "pad"]), "pads")
