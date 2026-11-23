"""Hardware 2.0 power sheet: USB-C in, protection, 3.3 V buck, switched LED supply."""
import sys
from pathlib import Path

import kisch
from kisch import Sheet

OUT = Path(sys.argv[1])
ROOT_UUID = sys.argv[2]
SHEET_UUID = sys.argv[3]

kisch.GLOBAL.update({"USB_DP", "USB_DN", "LED_PWR_EN"})

R0402 = "Resistor_SMD:R_0402_1005Metric"
C0402 = "Capacitor_SMD:C_0402_1005Metric"
SOT23 = "Package_TO_SOT_SMD:SOT-23"

s = Sheet("christmas-star-2", f"/{ROOT_UUID}/{SHEET_UUID}", "Power")

# ---- USB-C input ---------------------------------------------------------------
s.text("USB-C INPUT\n5 V, up to 3 A from a USB-C charger (5.1k on CC1/CC2 = sink).\n"
       "D+/D- go to the ESP32-C6 USB serial/JTAG (console, flashing).", 25, 30)
s.part("Connector:USB_C_Receptacle_USB2.0_16P", "J601", "USB-C", 45, 95,
       {"A1": "GND", "A12": "GND", "B1": "GND", "B12": "GND", "S1": "GND",
        "A4": "VBUS", "A9": "VBUS", "B4": "VBUS", "B9": "VBUS",
        "A5": "CC1", "B5": "CC2", "A6": "USB_DP", "B6": "USB_DP", "A7": "USB_DN", "B7": "USB_DN",
        "A8": "NC", "B8": "NC"},
       footprint="Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12", lcsc="C165948")
s.part("Device:R_Small", "R601", "5.1k 1%", 90, 80, {"1": "CC1", "2": "GND"}, R0402, "C25905")
s.part("Device:R_Small", "R602", "5.1k 1%", 112, 80, {"1": "CC2", "2": "GND"}, R0402, "C25905")
s.part("Power_Protection:USBLC6-2SC6", "U602", "USBLC6-2SC6", 100, 125,
       {"1": "USB_DP", "6": "USB_DP", "3": "USB_DN", "4": "USB_DN", "5": "VBUS", "2": "GND"},
       "Package_TO_SOT_SMD:SOT-23-6", "C7519")
s.text("Alternative 5 V input for wiring\n(do not use together with USB-C)", 25, 150)
s.part("Connector:Conn_01x02_Pin", "J602", "5V IN", 40, 165, {"1": "VBUS", "2": "GND"},
       "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical")

# ---- protection ---------------------------------------------------------------
s.text("PROTECTION\nPolyfuse, then a P-MOSFET instead of v1's SS38 diode:\n"
       "~0.05 ohm instead of ~0.4 V drop, so the 3.3 V rail no longer sags\n"
       "with the LED current. Reverse polarity on J602: the FET stays off.", 140, 30)
s.part("Device:Polyfuse_Small", "F601", "3A hold 6V", 150, 85, {"1": "VBUS", "2": "VIN_F"},
       "Fuse:Fuse_1812_4532Metric")
s.part("Transistor_FET:AO3401A", "Q601", "AO3401A", 178, 85, {"3": "VIN_F", "2": "+5V", "1": "Q601_G"},
       SOT23, "C15127")
s.part("Device:R_Small", "R603", "10k", 165, 110, {"1": "Q601_G", "2": "GND"}, R0402, "C25744")
s.part("Device:C_Polarized_Small", "C611", "10uF 25V", 195, 110, {"1": "+5V", "2": "GND"},
       "Capacitor_SMD:CP_Elec_4x5.4", "C72484")
s.part("Device:C_Small", "C613", "47uF 10V X5R", 217, 110, {"1": "+5V", "2": "GND"}, "Capacitor_SMD:C_1206_3216Metric")
s.flag("VBUS", 75, 60)
s.flag("+5V", 240, 70)
s.flag("GND", 240, 135)

# ---- 3.3 V for the ESP32-C6 ------------------------------------------------------
s.text("3.3 V FOR THE ESP32-C6 (and the sensors)\nTPS62056 fixed 3.3 V buck as on v1 (800 mA); LBO/PG unused.\n"
       "Wi-Fi TX peaks ~350 mA. The LEDs never load this rail.", 262, 30)
s.part("Regulator_Switching:TPS62056DGS", "U601", "TPS62056DGS", 300, 95,
       {"1": "+5V", "8": "+5V", "5": "+3V3", "9": "SW_3V3",
        "2": "NC", "4": "NC", "3": "GND", "6": "GND", "7": "GND", "10": "GND"},
       "Package_SO:VSSOP-10_3x3mm_P0.5mm", "C182547")
s.part("Device:C_Small", "C601", "10uF 6.3V X5R", 262, 130, {"1": "+5V", "2": "GND"}, C0402, "C15525")
s.part("Device:C_Small", "C602", "100nF 16V X7R", 285, 130, {"1": "+5V", "2": "GND"}, C0402, "C328914")
s.part("Device:L_Small", "L601", "10uH 720mA 450mOhm", 340, 85, {"1": "SW_3V3", "2": "+3V3"},
       "Inductor_SMD:L_1008_2520Metric")
s.part("Device:C_Small", "C612", "22uF 6.3V X5R", 335, 130, {"1": "+3V3", "2": "GND"},
       "Capacitor_SMD:C_0805_2012Metric", "C7467039")
s.part("Device:C_Small", "C603", "22uF 6.3V X5R", 358, 130, {"1": "+3V3", "2": "GND"},
       "Capacitor_SMD:C_0805_2012Metric", "C7467039")
s.part("Device:C_Small", "C604", "100nF 16V X7R", 381, 130, {"1": "+3V3", "2": "GND"}, C0402, "C328914")
s.flag("+3V3", 395, 70)

# ---- switched LED supply -----------------------------------------------------------
s.text("LED SUPPLY SWITCH\n132 LEDs draw ~100 mA even when dark: the ESP32-C6 cuts their\n"
       "supply for standby. LED_PWR_EN high = on; low or floating (boot) = off.\n"
       "R605/C605 slow the turn-on (inrush into the LEDs' 15 uF).", 25, 190)
s.part("Transistor_FET:AO3401A", "Q602", "AO3401A", 100, 228, {"2": "+5V", "3": "+5V_LED", "1": "LED_G"},
       SOT23, "C15127")
s.part("Device:R_Small", "R604", "100k", 60, 218, {"1": "+5V", "2": "LED_G"}, R0402, "C25741")
s.part("Device:C_Small", "C605", "100nF 16V X7R", 30, 218, {"1": "+5V", "2": "LED_G"}, C0402, "C328914")
s.part("Device:R_Small", "R605", "10k", 60, 248, {"1": "LED_G", "2": "LED_SW"}, R0402, "C25744")
s.part("Transistor_FET:AO3400A", "Q603", "AO3400A", 100, 268, {"3": "LED_SW", "2": "GND", "1": "LED_PWR_EN"},
       SOT23, "C20917")
s.part("Device:R_Small", "R606", "100k", 60, 278, {"1": "LED_PWR_EN", "2": "GND"}, R0402, "C25741")
s.part("Device:C_Polarized_Small", "C606", "10uF 25V", 135, 245, {"1": "+5V_LED", "2": "GND"},
       "Capacitor_SMD:CP_Elec_4x5.4", "C72484")
s.flag("+5V_LED", 150, 225)

# ---- test points ---------------------------------------------------------------
s.text("TEST POINTS", 200, 190)
for i, net in enumerate(["VBUS", "+5V", "+5V_LED", "+3V3", "GND"]):
    s.part("Connector:TestPoint", f"TP60{i + 1}", net, 205 + 22 * i, 220, {"1": net},
           "TestPoint:TestPoint_Pad_D1.5mm")

s.write(OUT)
print("wrote", OUT, "with", len([i for i in s.items if i[0] == "symbol"]), "symbols")
