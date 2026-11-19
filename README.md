# Christmas Star

[![CI](https://github.com/unary-fi/christmas-star/actions/workflows/ci.yml/badge.svg)](https://github.com/unary-fi/christmas-star/actions/workflows/ci.yml)
[![Releases](https://img.shields.io/github/v/release/unary-fi/christmas-star-releases?label=release)](https://github.com/unary-fi/christmas-star-releases/releases)
[![License: MIT](https://img.shields.io/badge/code-MIT-gold.svg)](LICENSE)
[![Hardware: CERN-OHL-P](https://img.shields.io/badge/hardware-CERN--OHL--P--2.0-gold.svg)](hw/LICENSE)

A decorative smart LED Christmas star: 132 RGB LEDs on two faces, Bluetooth,
an accelerometer and a 2 Gbit flash for animations. This repository holds the
hardware design (KiCad), the Zephyr firmware, the Android app and the PC tools.

Releases (firmware images and the Android app) are published in
[unary-fi/christmas-star-releases](https://github.com/unary-fi/christmas-star-releases/releases).

# Hardware

The ChristmasStar 1.0 hardware comprises the following components:
- STM32F030RCT6 ARM Cortex-M0 MCU (256 KB flash, 32 KB RAM)
- 8.0 MHz oscillator
- BM78SPPS5MC2 BlueTooth serial port module
- LSM6DS3 iNEMO 6-axis MEMS inertial module
- ZD35Q2GC-IB NAND Flash memory, 2 Gb
- 132 digital RGB LEDs in one chain, 66 LEDs per side

See [docs/led-layout.md](docs/led-layout.md) for how the LEDs are arranged and
[docs/hw-schematic-review.md](docs/hw-schematic-review.md) for known hardware issues.

# Software

The firmware in [sw/](sw/) is a [Zephyr](https://zephyrproject.org) application
with its own board definition (`christmas_star`). The Android app in
[android/](android/) controls the star over Bluetooth LE.

## Features

- Real-time LED effects at 50 fps. The LED data is sent by SPI2 (transmit-only)
  with DMA from the app's own driver, so it is unaffected by interrupts.
- Christmas effects that run on the star itself: Golden Star, Liquid Gold
  (flows as the star is tilted), Snowfall, Shooting Stars, Candlelight,
  Hearth & Frost (different faces that trade places), Northern Lights, Candy
  Cane, Tree Lights, Star of Bethlehem, Fireworks, Christmas Disco, Frost, Twinkle, Comets and
  Rainbow, with an adjustable speed.
- The app and Star Studio get the effect list and animated previews from the
  star (`app effects`, `app preview`), so new effects arrive with a firmware
  update alone. Previews are cached per firmware version.
- Shake to drop: shaking knocks lights loose in their current colours, on top
  of any effect, picture or video (`shake on|off`).
- **Comet**, a game played on the star and steered from the app (see
  *Comet, the game*).
- Command line over Bluetooth (BM78), including direct LED control and a
  binary live-stream mode used by the app for photos and videos.
- One effect at a time in the 2 Gbit NAND (about 655 000 frames, 3.6 h at
  50 fps), uploaded over Bluetooth with XMODEM-1K.
- The star resumes what it was showing after a restart (effect, manual or
  picture frame, speed, shake on/off).
- Missing LEDs: if an LED is bridged on the board, or is faulty and only
  passes the data through, mark it missing and the firmware skips it in the
  chain. The default list is empty (`CONFIG_STAR_DEFAULT_MISSING_LEDS`); the
  first prototype has D259 bridged (`led missing add D259`).
- Supply protection: the firmware measures the 3V3 rail every frame and
  lowers the LED current limit when it sags, so the Bluetooth module (3.3 V
  minimum) keeps running. The default limit is 600 mA (`led limit`);
  `led status` shows the rail voltage and the learnt limit.
- Pairing: each star belongs to one phone (or PC). Its Bluetooth command line
  stays locked until the app proves the owner's key, so several stars and
  phones can share a room. A developer key opens every star (see *Pairing*).
- Names: stars can be renamed from the app or Star Studio; the name is what
  phones and PCs see when they search.
- Settings (missing LEDs, brightness, current limit, IMU axes) and the
  running state are stored in the MCU's internal flash.

## Setting up (Windows)

1. Install Python 3.12, CMake, Ninja and the SEGGER J-Link software.
2. Create the Python environment and the west workspace (the repository root
   is the workspace, `sw/` is the manifest):

   ```powershell
   py -3.12 -m venv .venv
   .venv\Scripts\pip install west pyserial xmodem pillow pylink-square pytest bleak cryptography intelhex click cbor2
   .venv\Scripts\west init -l sw
   .venv\Scripts\west update
   .venv\Scripts\pip install -r zephyr\scripts\requirements-base.txt
   ```

3. Install the [Zephyr SDK](https://github.com/zephyrproject-rtos/sdk-ng/releases)
   1.0.1 minimal bundle with the `arm-zephyr-eabi` toolchain into
   `%USERPROFILE%\zephyr-sdk-1.0.1`.
4. In each new PowerShell session: `. .\sw\tools\env.ps1`
5. Developer key (optional, for your own builds only): put 32 random hex
   digits in `sw/dev_key.txt` (ignored by git), e.g.
   `python -c "import secrets; open('sw/dev_key.txt','w').write(secrets.token_hex(16))"`.
   Firmware built with it accepts this key on every star; `star_ble.py` reads
   it from the same file, and Star Studio takes it on its *Developer* page.
   Keep it private, and never
   publish firmware built with it: anyone could read the key out of the image.
   Published releases are built without one (CI has no key; locally,
   `$env:STAR_NO_DEV_KEY = "1"` before a pristine build leaves it out).

## Building and flashing

```powershell
west build --sysbuild -b christmas_star sw -d build/star            # release
west build --sysbuild -b christmas_star sw -d build/debug -- -DEXTRA_CONF_FILE=debug.conf
west flash -d build/star                                 # J-Link on J401
```

Each build contains the MCUboot bootloader and the firmware, signed with
`sw/signing_key.pem` (ECDSA P-256, ignored by git). Stars only start firmware
signed with the key their bootloader was built with. Official releases are
signed with the project's key, which is not public; for your own builds,
create a key of your own and install your bootloader once with a J-Link:

```powershell
python bootloader\mcuboot\scripts\imgtool.py keygen -k sw\signing_key.pem -t ecdsa-p256
```

Flash layout (256 KB): MCUboot 32 KB, the running firmware 108 KB, the slot
for an update 108 KB, settings 8 KB (at the same place as before the
bootloader, so the stored settings survive). The version comes from
[sw/VERSION](sw/VERSION).

### Firmware update over Bluetooth

```powershell
python sw\tools\update_firmware.py build\star\sw\zephyr\zephyr.signed.bin
python sw\tools\update_firmware.py --star "Living room" build\star\sw\zephyr\zephyr.signed.bin
```

or the *Firmware* card in Star Studio (several stars, one after the other).
The image goes into the second slot with XMODEM-1K (about 35 s from a PC);
`fw apply` restarts the star and MCUboot checks the signature and swaps the
slots. The new firmware confirms itself after 20 s; if it crashes or hangs
before that, the next restart brings the old one back. On the command line:
`fw status`, `fw upload`, `fw apply [permanent]`, `fw confirm`.

Updates need the BOOT0 pull-down: without it, the restart ends in ST's ROM
bootloader. Boards are switched to the bootloader layout once with the
J-Link (`west flash`); after that, Bluetooth is enough.

The debug build adds a second shell over SEGGER RTT, so the star can be
controlled through the J-Link without Bluetooth:

```powershell
python sw\tools\rtt_shell.py --boot-flash "led status" "nand info" "imu read"
```

The debug build can also reach the BM78 through the J-Link: `bt bridge test`
restarts the module in test mode and relays its UART over RTT.
`python sw\tools\bm78_bridge.py probe|dump|hci|com|rename|restore` uses this
to read the module's version and configuration EEPROM, rename the module,
write a backup back (keep backups private: they contain the module's address and
pairing keys), or connect Microchip's
configuration tool through a virtual null-modem pair (com0com).

> **BOOT0 on hardware 1.0:** BOOT0 has no pull-down, and on the prototype
> every reset starts ST's ROM bootloader instead of the firmware. Connecting
> the J-Link also resets the MCU. Until the board gets a 10 kΩ pull-down,
> start the firmware with `rtt_shell.py --boot-flash`, which maps flash to
> address 0 and starts MCUboot, which starts the firmware.

## Tests

```powershell
west build -p always -b qemu_cortex_m3 sw/tests/unit -d build/tests -t run
python -m pytest sw/tools/tests pc/tests
```

## Command line

The star's Bluetooth module appears under its name (**"Dual-SPP"** until it is
first renamed; after renaming it is also found by its service, whatever the
name). Windows 11's
*Add device* dialog may not list it; pair it with the classic wizard instead
(run `DevicePairingWizard.exe`). Windows pairs it as a Bluetooth LE device,
which creates no COM port, so use the BLE terminal:

```powershell
python sw\tools\star_ble.py --list             # nearby stars
python sw\tools\star_ble.py                    # interactive terminal (nearest star)
python sw\tools\star_ble.py --star "Living room" "led status"
python sw\tools\star_ble.py --pair             # pair the star with this PC
```

The tool logs in with the developer key, or with this PC's own key
(`~/.christmas-star/pc_key`) for a star paired with the PC.

Phones can use any BLE terminal app that supports the Microchip Transparent
UART service. On a system where the star pairs as a Classic SPP device, open
its COM port at 115200 baud instead. Type `help` for all commands.

| Command | Description |
|---|---|
| `set_led <side> <index> <color>` | Set one LED. Side 0 = both, 1 = front (D2xx), 2 = back (D3xx); index 1–66 is the designator number; colour `0xRRGGBB`, `#RRGGBB` or `RRGGBB`. Example: `set_led 1 32 0xffee33` sets D232. |
| `set_all <side> <color>` | Set all LEDs of a side, e.g. `set_all 1 0xffee33`. |
| `effect list` | Effects, and the one stored in the flash. |
| `effect play <name\|flash>` | Start an effect (`flash` = the stored one). |
| `effect color <color>` | Solid colour. |
| `effect speed [25-400]` | Effect speed in percent. |
| `effect upload` | Receive a `.csfx` file with XMODEM-1K (replaces the stored effect). |
| `effect info`, `effect delete` | Stored effect details, delete it. |
| `shake [on\|off]` | Shake to drop lights, for every effect. |
| `app hello`, `app state`, `stream` | Used by the Android app (state line, binary live frames). |
| `app effects`, `app preview <id> [frames] [step_ms]` | The effects to offer (`fx <id> <flags> <title>\|<subtitle>`), and a preview clip rendered on the star. |
| `app game [start single\|double \| l \| r \| f \| pause \| resume \| stop]` | Comet, the game: start, steer (left, right, flip), pause, leave. Without arguments one `game state=... score=... best1=... best2=...` line. |
| `led missing list\|add\|del\|clear` | LEDs bridged on the board, e.g. `led missing add D259`. |
| `led test <led\|all>` | Light one LED (e.g. `D232` or logical index 0–131) or walk the chain. |
| `led brightness [0-255]`, `led limit [mA]` | Global brightness, total current limit. |
| `led map [1\|2]`, `led status` | Print the LED bitmap; frame rate and current. |
| `imu read [n]`, `imu axis [+x+y+z]`, `imu threshold [mg]` | Accelerometer values, axis mapping, shake sensitivity. |
| `bt status`, `bt reset` | Bluetooth module status pins, reset. |
| `star name [text\|--default]` | Show or set the star's name (1–16 characters, what phones see). Bluetooth restarts for a few seconds. |
| `star unpair [force]` | Release the star from its owner (`force` with developer access). |
| `star pairing [open\|reset]` | Pairing status; open the window or forget the owner (developer or J-Link). |
| `app hello`, `app auth <hmac>`, `app pair <key>` | Unlock the Bluetooth command line (see *Pairing*). |
| `nand info`, `nand test <block>` | Flash ID and geometry, read/write test. |
| `reboot` | Restart. |

## Pairing

Every star has at most one owner: a phone (the app) or a PC. Until a client
proves a key, the star's Bluetooth command line only answers `app hello`,
`app auth` and `app pair`, and it locks again whenever the link drops. The
J-Link (RTT) command line is never locked.

- `app hello` gives a fresh random nonce; the client answers
  `app auth <HMAC-SHA256(key, "auth" + nonce)>` with the owner's key or the
  developer key. The key itself never goes over the air.
- A star without an owner (new, or released by its owner) accepts a new owner
  during the first **60 s after it is plugged in**. An owned star does not.
- **Shaking the star for 5 seconds** opens pairing in any case: the star
  breathes gold for 60 s, and the next phone that taps it in the app becomes
  its owner (green flash). Releasing shows a red flash.
- The owner can release the star in the app (*Stars* → *Release this star*).
- Developer mode in the app: tap the title "Christmas Star" seven times and
  enter the developer key; *Stars* then switches freely between all stars.
  Star Studio uses the developer key saved on its *Developer* page.

The BM78 accepts one connection at a time, so a star connected to Star Studio
is not available to a phone until the studio disconnects it.

## Comet, the game

A comet runs along the star's lights. Catch the golden sparks to grow (and
the red ornaments for five points); don't run into your own tail. Every five
sparks the comet speeds up; at 40 segments the star is full and you win.

- **The track**: on each face the outline and the inner circle (the 12 LEDs
  around the centre), joined by six passages: the single LEDs between them,
  each leading from an inner corner of the outline to the circle.
- **Steering**: the comet follows its line. Tap *Left* or *Right* and it turns
  that way at the next place where it can, if that turn exists there;
  otherwise the tap is forgotten. At the end of a passage the comet turns to
  the side you tapped last (before your first tap, it keeps its direction of
  travel). A faint white light shows the next place to turn; it turns blue
  when your tap will take it. Left and right are as you see them on the face
  the comet is on.
- **One side**: the game stays on the front and is mirrored to the back, so
  everyone around the star sees the same game.
- **Both sides**: *Flip* sends the comet through the star; it carries on in
  the same direction on the other face. Sparks appear on either face, so walk
  around the star to find them, and mind your tail when you flip onto it.
  Sparks count double. A faint glow on the far face shows where the comet is.
- The app has a *Play* page (firmware 1.6 or later); Star Studio has a Comet
  card on the Developer page (arrow keys). Leaving the app or losing the
  Bluetooth link pauses the game; after a minute in pause or game over the
  star goes back to the effect it showed before. Each mode keeps its best
  score on the star.

The rules are in `sw/src/game/comet_logic.c` (unit-tested), the drawing in
`fx_cometgame.c`.

# Star Studio (PC)

Star Studio controls every star in the room from a computer, with the app's
look. It is a web page that talks to the stars itself, with Web Bluetooth:
nothing to install.

**Open https://unary-fi.github.io/christmas-star/ in Chrome or Edge** (Windows,
macOS, ChromeOS, Android; on Linux, Chrome may need
`chrome://flags/#enable-web-bluetooth`). Firefox and Safari have no Web
Bluetooth.

- Left: your stars. *Add star* opens the browser's Bluetooth chooser; pick a
  star and it connects. Where the browser remembers permissions, the stars
  come back on the next visit. Click to select; Ctrl+click selects several
  stars, which then all get the same commands, colours, pictures and effects.
- Stars open with the developer key (*Developer* page, kept in this browser
  only) or with this browser's own key: a star in its pairing window (shake
  it for 5 s) pairs with *Pair*, like a phone.
- *Colour*, *Picture* (photo or video, live or saved to the star) and
  *Effects* work as in the app.
- *Developer*: a console for every selected star, status and supply, renaming
  (several stars at once get numbered names), pairing, missing LEDs (click an
  LED on the star), the current limit and `.csfx` uploads.

The page is `pc/web` (plain HTML, CSS and JavaScript modules, no build step
and no libraries), published by `.github/workflows/pages.yml`. To work on it,
serve it locally (`localhost` counts as secure for Web Bluetooth) and run its
tests with Node.js:

```powershell
cd pc\web
python -m http.server 8000      # then open http://localhost:8000
node --test
```

## Making effects

Draw a 32×32 image or animated GIF using the bitmap in
[docs/led-layout.md](docs/led-layout.md) as a template, then:

```powershell
python sw\tools\make_effect.py snow.gif -o snow.csfx --loop
python sw\tools\send_effect.py --ble snow.csfx --play       # Bluetooth LE
python sw\tools\send_effect.py --port COM12 snow.csfx        # serial port
```

Over BLE, uploads run at about 2.3 KB/s from Windows and 4.6 KB/s from the
Android app (one second of 50 fps animation is about 20 KB). With a serial
port, Tera Term's *File → Transfer → XMODEM → Send* (1K) also works after
typing `effect upload`.

# Android app

The app ([android/](android/), Kotlin + Jetpack Compose, Android 9 or newer)
finds the star ("Dual-SPP") over Bluetooth LE and has three pages:

- **Colour:** red, green and blue sliders for the front, the back or both
  faces (choosing *Both* copies the front colour to the back), plus festive
  presets.
- **Picture:** choose a photo or video and move or zoom it behind the star's
  outline. The star shows the colours under its LEDs live (about 20 frames/s),
  the same picture on both faces. A photo stays on the star when you leave the
  page. A video loop can be saved into the star's flash and keeps playing
  without the app.
- **Effects:** animated previews of the star's Christmas effects; tap one to
  play it on the star. A speed slider sits on top.

The top bar shows the connected star and has the brightness slider and the
*Shake to drop* switch. Tap the star's name to open *Stars*: the stars nearby,
pairing, renaming and releasing (see *Pairing*). All effects run on the star itself, so everything
keeps going when the phone disconnects.

## Building the app

Requires JDK 17 and the Android SDK (platform 37). From `android/`:

```powershell
$env:JAVA_HOME = "C:\Program Files\Microsoft\jdk-17.0.21.21-hotspot"
.\gradlew.bat assembleDebug testDebugUnitTest lintDebug
adb install -r app\build\outputs\apk\debug\app-debug.apk
```

Release builds for Google Play (application ID `fi.unary.christmasstar`,
signed with the upload key in the git-ignored `android/keystore.properties`):
see [android/play/RELEASE.md](android/play/RELEASE.md). The store texts,
graphics and privacy policy are in [android/play/](android/play/).

The star outline and LED positions in the app come from the PCB files:
`sw/tools/gen_led_layout.py` regenerates `StarLayout.kt` together with the
firmware's layout table.

# Contributing

Issues and pull requests are welcome. The `main` branch, merges and releases
are maintained by the project owners; to contribute, fork the repository,
make your change on a branch and open a pull request. Every pull request runs
the CI (firmware builds and unit tests, Android build, tests and lint, Python
tests); a maintainer approves the first run from a new contributor. See
[CONTRIBUTING.md](CONTRIBUTING.md).

# License

- Software (firmware, Android app, PC tools): [MIT](LICENSE)
- Hardware design files in [hw/](hw/): [CERN-OHL-P-2.0](hw/LICENSE)

Copyright (c) 2024-2026 Tuomas Terho.
