"""Hardware 2.0 ESP32-C6 sheet: module, boot/reset, NAND, motion sensor, LED data level shifter."""
import sys
from pathlib import Path

import kisch
from kisch import Sheet, parse

OUT = Path(sys.argv[1])
ROOT_UUID, SHEET_UUID = sys.argv[2], sys.argv[3]
PROJ = OUT.parent

kisch.GLOBAL.update({"USB_DP", "USB_DN", "LED_PWR_EN", "CHAIN-A"})

R0402 = "Resistor_SMD:R_0402_1005Metric"
C0402 = "Capacitor_SMD:C_0402_1005Metric"
BUTTON = "my-buttons:SW-SMD_4P-L6.2-W6.2-P4.50-LS8.6"   # the v1 button

s = Sheet("christmas-star-2", f"/{ROOT_UUID}/{SHEET_UUID}", "ESP32-C6", ref_base=700)
s.extra_lib = parse((PROJ / "christmas-star-2.kicad_sym").read_text(encoding="utf-8"))

# ---- the module ------------------------------------------------------------------
s.text("ESP32-C6-WROOM-1 (8 MB flash, PCB antenna)\n"
       "Antenna end at a board edge or the star's open centre: no copper on any layer under\n"
       "and around it (Espressif hardware design guidelines). EPAD to ground (thermal).\n"
       "LED data uses I2S or RMT in the firmware: SPI2 serves the NAND.", 25, 25)
s.part("christmas-star-2:ESP32-C6-WROOM-1", "U701", "ESP32-C6-WROOM-1-N8", 120, 110,
       {"1": "GND", "28": "GND", "29": "GND", "2": "+3V3", "3": "EN",
        "4": "MEM_HOLD", "5": "MEM_WP", "6": "MEM_CLK", "7": "MEM_DI",
        "8": "NC", "9": "NC", "10": "IO8_STRAP", "11": "MEM_CS", "12": "LED_DATA",
        "13": "USB_DN", "14": "USB_DP", "15": "BOOT", "16": "LED_PWR_EN",
        "17": "NC", "18": "NC", "19": "NC", "20": "I2C_SDA", "21": "I2C_SCL", "22": "NC",
        "23": "NC", "24": "UART_RX", "25": "UART_TX", "26": "IMU_INT1", "27": "MEM_DO"},
       footprint="christmas-star-2:ESP32-C6-WROOM-1")
s.text("Spare: IO0, IO1, IO15, IO19, IO20, IO21\n(microphone, light sensor ...)", 140, 150, 1.27)
s.part("Device:C_Small", "C701", "22uF 6.3V X5R", 60, 60, {"1": "+3V3", "2": "GND"},
       "Capacitor_SMD:C_0805_2012Metric", "C7467039")
s.part("Device:C_Small", "C702", "100nF 16V X7R", 80, 60, {"1": "+3V3", "2": "GND"}, C0402, "C328914")

# ---- reset, boot ------------------------------------------------------------------------
s.text("RESET AND BOOT\nEN: 10k + 1uF delay (datasheet ch. 9). BOOT (IO9) held at reset =\n"
       "download mode over USB; afterwards a free user button. IO8 pulled up.", 25, 175)
s.part("Device:R_Small", "R701", "10k", 35, 205, {"1": "+3V3", "2": "EN"}, R0402, "C25744")
s.part("Device:C_Small", "C703", "1uF 16V X5R", 55, 215, {"1": "EN", "2": "GND"}, C0402, "C29266")
s.part("Switch:SW_MEC_5E", "SW701", "RESET", 85, 210, {"3": "EN", "2": "GND", "1": "NC", "4": "NC"}, BUTTON)
s.part("Device:R_Small", "R702", "10k", 35, 245, {"1": "+3V3", "2": "BOOT"}, R0402, "C25744")
s.part("Switch:SW_MEC_5E", "SW702", "BOOT", 85, 250, {"3": "BOOT", "2": "GND", "1": "NC", "4": "NC"}, BUTTON)
s.part("Device:R_Small", "R703", "10k", 55, 245, {"1": "+3V3", "2": "IO8_STRAP"}, R0402, "C25744")
s.text("Debug UART", 120, 230)
s.part("Connector:Conn_01x03_Pin", "J701", "UART", 120, 250, {"1": "UART_TX", "2": "UART_RX", "3": "GND"},
       "Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical")

# ---- NAND flash (effects) -------------------------------------------------------------
s.text("EFFECT STORAGE\nZD35Q2GC 2 Gbit SPI NAND as on v1, on SPI2's IO_MUX pins.\n"
       "CS, WP and HOLD pulled up: inactive while the ESP32-C6 boots.", 210, 25)
s.part("Memory_Flash:W25Q32JVZP", "U703", "ZD35Q2GC-IB", 250, 75,
       {"1": "MEM_CS", "2": "MEM_DO", "3": "MEM_WP", "4": "GND", "5": "MEM_DI", "6": "MEM_CLK",
        "7": "MEM_HOLD", "8": "+3V3", "9": "GND"},
       "Package_SON:WSON-8-1EP_8x6mm_P1.27mm_EP3.4x4.3mm", "C5366334")
s.part("Device:C_Small", "C704", "100nF 16V X7R", 280, 60, {"1": "+3V3", "2": "GND"}, C0402, "C328914")
s.part("Device:R_Small", "R704", "10k", 205, 55, {"1": "+3V3", "2": "MEM_CS"}, R0402, "C25744")
s.part("Device:R_Small", "R705", "10k", 205, 75, {"1": "+3V3", "2": "MEM_WP"}, R0402, "C25744")
s.part("Device:R_Small", "R706", "10k", 205, 95, {"1": "+3V3", "2": "MEM_HOLD"}, R0402, "C25744")

# ---- motion sensor ---------------------------------------------------------------------
s.text("MOTION SENSOR\nLSM6DS3 on I2C as on v1 (address 0x6B: SA0 high).\n"
       "INT1 to IO3 (a low-power GPIO): shaking can wake the star.", 210, 125)
s.part("Sensor_Motion:LSM6DS3", "U702", "LSM6DS3", 255, 175,
       {"1": "+3V3", "2": "+3V3", "3": "+3V3", "4": "IMU_INT1", "5": "+3V3", "6": "GND", "7": "GND",
        "8": "+3V3", "9": "NC", "10": "NC", "11": "NC", "12": "+3V3", "13": "I2C_SCL", "14": "I2C_SDA"},
       "Package_LGA:LGA-14_3x2.5mm_P0.5mm_LayoutBorder3x4y", "C95230")
s.part("Device:C_Small", "C705", "100nF 16V X7R", 290, 150, {"1": "+3V3", "2": "GND"}, C0402, "C328914")
s.part("Device:C_Small", "C706", "100nF 16V X7R", 310, 150, {"1": "+3V3", "2": "GND"}, C0402, "C328914")
s.part("Device:R_Small", "R707", "10k", 205, 165, {"1": "+3V3", "2": "I2C_SCL"}, R0402, "C25744")
s.part("Device:R_Small", "R708", "10k", 205, 185, {"1": "+3V3", "2": "I2C_SDA"}, R0402, "C25744")

# ---- LED data: 3.3 V -> 5 V --------------------------------------------------------------
s.text("LED DATA 3.3 V -> 5 V (the LEDs are 5 V only)\nSN74LVC1T45 as on v1; B side on the switched +5V_LED.\n"
       "Pull-downs on both sides and 47R before D201 (the D201 lesson):\n"
       "the chain sees a steady low while the ESP32-C6 boots.", 210, 195)
s.part("Logic_LevelTranslator:SN74LVC1T45DCK", "U704", "SN74LVC1T45DCK", 255, 232,
       {"1": "+3V3", "5": "+3V3", "6": "+5V_LED", "2": "GND", "3": "LED_DATA", "4": "LED_DATA_5V"},
       "Package_TO_SOT_SMD:SOT-363_SC-70-6", "C9382")
s.part("Device:R_Small", "R709", "10k", 228, 245, {"1": "LED_DATA", "2": "GND"}, R0402, "C25744")
s.part("Device:R_Small", "R710", "47R", 285, 225, {"1": "LED_DATA_5V", "2": "CHAIN-A"}, R0402)
s.part("Device:R_Small", "R711", "10k", 300, 238, {"1": "CHAIN-A", "2": "GND"}, R0402, "C25744")
s.part("Device:C_Small", "C707", "100nF 16V X7R", 320, 212, {"1": "+3V3", "2": "GND"}, C0402, "C328914")
s.part("Device:C_Small", "C708", "100nF 16V X7R", 345, 212, {"1": "+5V_LED", "2": "GND"}, C0402, "C328914")

s.write(OUT)
print("wrote", OUT, "with", len([i for i in s.items if i[0] == "symbol"]), "symbols")
