# Christmas Star – Hardware Schematic Review

**Design:** [hw/christmas-star](../hw/christmas-star/) (KiCad 8)
**Review date:** 2026-11-13
**Method:** KiCad ERC run with all severities, netlist export, and checks of pin connections against the BOM ([production/bom.csv](../hw/christmas-star/production/bom.csv)) and the PCB placement ([production/positions.csv](../hw/christmas-star/production/positions.csv)).

## Summary

| # | Severity | Area | Finding |
|---|----------|------|---------|
| 1 | Critical | Controller | STM32 BOOT0 is floating |
| 2 | High | Power | TPS62056 has no ceramic input capacitor |
| 3 | High | Controller | Reset switch SW402 uses a symbol with an LED; RESET is wired to the LED cathode |
| 4 | Medium | Communication | UART flow control is on pins without USART CTS/RTS functions |
| 5 | Medium | Communication | BM78 BAT_IN is at the bottom of its supply range |
| 6 | Medium | Power | LED power path is undersized: diode drop, too little bulk capacitance, no fuse or TVS |
| 7 | Medium | Controller | LSM6DS3 INT1/INT2 are not connected |
| 8 | Low | Controller | Flash CS has no pull-up |
| 9 | Low | Controller | Oscillator enable (tri-state) pin is floating |
| 10–14 | Docs | Various | Symbol/value mismatches and ERC noise |

---

## Critical

### 1. STM32 BOOT0 is floating

- **Where:** U402 (STM32F030RCT6) pin 60, Controller sheet
- **Problem:** The `unconnected-(U402-BOOT0-Pad60)` net has nothing on it. If BOOT0 floats, the MCU can randomly start in ST's built-in bootloader instead of the user firmware, depending on noise, humidity or contamination.
- **Fix:** Add a 10 kΩ pull-down from BOOT0 to GND. Optionally add a pad or jumper to 3V3 so the UART bootloader can still be reached.
- **Confirmed on the prototype (2026-11-13):** after every reset (power-on, reset pin, debugger reset) the MCU was executing ST's ROM bootloader at 0x1FFFxxxx instead of the firmware. The STM32F030 has no option bit to ignore the BOOT0 pin, so this can only be fixed in hardware. Until then, the firmware is started through the J-Link (`sw/tools/rtt_shell.py --boot-flash`).

## High

### 2. TPS62056 has no ceramic input capacitor

- **Where:** U601, Power sheet
- **Problem:** The only capacitors on +5V near the regulator are C611 (a 10 µF aluminium electrolytic, LCSC C72484) and C502, which is next to the level shifter U501. The TPS6205x datasheet expects a low-ESR ceramic capacitor of about 10 µF right at VIN/PGND. Without it, the input can ring and switching can become unstable.
- **Fix:** Add a 10 µF X5R/X7R ceramic capacitor rated ≥10 V right at the VIN pin.

### 3. Reset switch SW402 symbol mismatch

- **Where:** SW402, Controller sheet
- **Problem:** The symbol is `Switch:SW_MEC_5E`. In that symbol, pins 1–2 are the switch contacts and pins 3 (K) and 4 (A) are an LED. In the schematic:
  - pin 1: unconnected
  - pin 2: GND
  - pin 3 (K): RESET
  - pin 4 (A): unconnected

  As drawn, the button doesn't reset anything. On the PCB, pads 1 and 2 are on one side (y = −2.25 mm) and pads 3 and 4 on the other. A typical 6×6 tactile switch internally connects 1↔2 and 3↔4, so the button will probably still work, but only by luck.
- **Fix:** Check the pad mapping against the C2909692 datasheet. Change the symbol to a 4-pin tactile switch (e.g. `Switch:SW_Push_Dual`) so the schematic matches the hardware.

## Medium

### 4. UART flow control is on pins without CTS/RTS functions

- **Where:** U402 PB3/PB4 ↔ U502 P0_0/P1_7
- **Problem:** On the STM32F030, USART1's hardware flow-control pins are PA11 (CTS) and PA12 (RTS), and both are unused. PB3 and PB4 have no USART CTS/RTS function, so flow control can only be done in software.
- **Fix:** Move UART-CTS and UART-RTS to PA11/PA12.
- **Note:** The connection to the BM78 is correct. BM78 P0_0 (its RTS output) goes to the MCU, and P1_7 (its CTS input) comes from the MCU. However, the net names UART-RTS and UART-CTS use the BM78's point of view, while UART-TX and UART-RX use the MCU's. Consider renaming them consistently.

### 5. BM78 BAT_IN is at the bottom of its supply range

- **Where:** U502 pin 4 (BAT_IN) on +3V3
- **Problem:** The BM78SPPS5 datasheet gives an operating range of 3.3–4.2 V. Tolerance on the regulator output can put BAT_IN below the minimum. In addition, the module's internal 3.3 V regulator (LDO33_O) feeds VDD_IO, so the module's I/O will sit slightly below the MCU's 3.3 V I/O.
- **Fix:** Consider feeding BAT_IN from a separate ~3.6 V regulator supplied from +5V. At a minimum, check the margins against the exact datasheet revision.

### 6. LED power path is undersized

- **Where:** J601, D601, C611, +5V rail
- **Problem:**
  - 132 LEDs run from +5V through a 2.54 mm pin header (J601) and an SS38 Schottky diode (D601). Both are rated around 3 A.
  - Full-white current for 132 LEDs can reach several amps, depending on the LED's per-channel current.
  - The diode drops about 0.4–0.5 V, so the LEDs see roughly 4.5 V, and D601 heats up at high current.
  - The only bulk capacitance on +5V is C611 (10 µF). Each LED has only a local 100 nF capacitor.
  - The input has no fuse and no TVS (surge protector).
- **Fix:**
  - Add 100–470 µF of bulk capacitance at the power input.
  - Consider replacing the diode with P-MOSFET reverse-polarity protection, which drops much less voltage.
  - Add a fuse or PTC, plus a TVS diode.
  - Limit brightness or total current in firmware.

### 7. LSM6DS3 interrupt pins are not connected

- **Where:** U401 pins 4 (INT1) and 9 (INT2)
- **Problem:** Without the interrupt pins, the MCU can't wake on motion or tap, and the firmware has to poll the sensor.
- **Fix:** Route INT1 and INT2 to free MCU pins, e.g. PA0 and PA1, which support EXTI and wakeup.

## Low

### 8. Flash chip-select has no pull-up

- **Where:** MEM-CS (U403 pin 1), and also MEM-WP and MEM-HOLD
- **Problem:** While the MCU is in reset or being programmed over SWD, its pins float, so the flash can be accidentally selected or held.
- **Fix:** Add 10 kΩ pull-ups to 3V3 on CS, and ideally on WP and HOLD.

### 9. Oscillator enable (tri-state) pin is floating

- **Where:** X401 pin 1
- **Problem:** The pin probably has an internal pull-up, but leaving it floating relies on that.
- **Fix:** Tie pin 1 to VDD.

## Labels and documentation

### 10. U501 value doesn't match its footprint

The value says `SN74LVC1T45DBV` (SOT-23-6), but the footprint is SC-70-6 and LCSC part C9382 is the SN74LVC1T45**DCKR** (SC-70). The board as built is correct. Rename the value to `SN74LVC1T45DCK` so nobody orders the wrong package.

### 11. U403 uses a NOR flash symbol for a NAND part

The symbol is `Memory_Flash:W25Q32JVZP`, a NOR flash that has also been modified locally. The part is a ZD35Q2GC-IB, which is a 2 Gbit **SPI NAND** flash. The pinout matches, but the firmware has to treat the chip as NAND: page read and program, ECC, and bad-block management. Consider making a dedicated symbol.

### 12. X401 symbol is the wrong size variant

The symbol is `Oscillator:ECS-2520MV-xxx-xx` (2.5 × 2.0 mm), while the footprint and ordered part are the 2.0 × 1.6 mm size. The pinout is the same, so this is cosmetic.

### 13. ERC noise hides real errors

The ERC reports 57 errors and 1 warning, almost all of them noise:

- Unused STM32 and BM78 pins have no no-connect markers.
- +3V3 and +5V have no `PWR_FLAG`.
- A GND symbol at (11.43 mm, 76.2 mm) on both LED chain sheets isn't connected to anything.
- The W25Q32JVZP library symbol has been modified locally.

The floating BOOT0 (finding 1) was hidden among these errors. Clean them up so real ERC errors stand out.

### 14. "Chain A" and "Chain B" are one chain

The DOUT of D266 feeds the DIN of D301, so all 132 LEDs form a single chain driven by one data line (PC3 → R501 → U501 → D201). That works, but if two independent parallel chains were intended, this is wired wrong.

## Checked and OK

- SPI1 to the flash on PA4–PA7: MISO (PA6) ← DO and MOSI (PA7) → DI are correct.
- I2C1 on PA9/PA10 has 10 kΩ pull-ups. The LSM6DS3 is strapped for I2C (CS high, SA0 high gives address 0x6B), and its unused aux I2C pins are tied to VDDIO.
- USART1 TX/RX on PB6/PB7 is crossed correctly to BM78 HCI_RXD/HCI_TXD.
- The SN74LVC1T45 direction and supplies are correct: VCCA = 3V3, VCCB = +5V, DIR = VCCA, so data flows A → B.
- TPS62056 is the fixed 3.3 V version, so FB tied to the output is correct. Unused LBI, SYNC and LBO are tied to GND, as allowed.
- The 8 MHz oscillator drives PF0 (OSC_IN), so the HSE has to be configured in bypass mode.
- The MCU decoupling capacitors C601–C608 are drawn on the Power sheet but placed around U402 on the PCB.
- Every LED has its own 100 nF decoupling capacitor.

## References

- [LCSC C9382 – TI SN74LVC1T45DCKR](https://www.lcsc.com/product-detail/C9382.html)
- [Microchip BM78 datasheet DS60001380](https://www.mouser.lt/datasheet/3/282/1/BM78_Bluetooth_Dual_Mode_Module_DS60001380.pdf)
- [ST STM32F030xC datasheet](https://www.st.com/resource/en/datasheet/stm32f030rc.pdf)
- [TI TPS62056 datasheet](http://www.ti.com/lit/ds/symlink/tps62056.pdf)

## Found while testing the first prototype (2026-11-13)

- **D201 works; the fault was in the firmware.** D201 used to keep a stray colour and pass every frame straight through to D202, on all three boards. The SPI output idles *high* between frames, so the first LED never saw the low reset it needs to take new data; the later LEDs were fine because D201's own output idles low. The old bit-banged driver left the line low and D201 worked. The LED driver now drives PC3 low as a GPIO between frames (sw/src/led/led_spi_dma.c). Only the bridged D259 on the first prototype needs `led missing add D259`.
- **The BM78 works.** It boots in application mode with the board's strap levels, advertises as "Dual-SPP", and passes data both ways through the BLE Transparent UART service. Windows 11 pairs it only as a BLE device, and its modern *Add device* dialog did not list it; the classic `DevicePairingWizard.exe` did.
- **The 8 MHz oscillator works.** The firmware runs from HSE × 6 = 48 MHz, even though the enable pin floats (finding 9).
- **The NAND matches its datasheet.** It reports ID `2C 24`, and erase/program/read work in both planes.
- **The accelerometer is an LSM6DS3** (WHO_AM_I 0x69). With the default axis map, a star standing upright reads about +1 g on board +Y, so the Y axis is right; the X sign has not been checked yet (tilt the star and use `imu read` / `imu axis`).
