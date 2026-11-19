# Changelog

Release notes for the Christmas Star. A release tag `vX.Y.Z` matches the
firmware version in `sw/VERSION`; the release workflow publishes the section
for that version in
[christmas-star-releases](https://github.com/unary-fi/christmas-star-releases/releases).

## [1.6.0] - 2026-11-19

### Comet, a game on the star

- A comet runs along the star's lights; steer it from the app, catch the
  golden sparks to grow and don't run into your own tail. The track is the
  outline and the inner circle, joined by six passages; a faint white light
  shows the next place to turn.
- **One side**: the game is mirrored to the back, so everyone around the star
  sees the same game.
- **Both sides**: flip the comet through the star at any moment; it keeps
  its direction on the other face. Sparks appear on either face and count
  double.
- Best scores per mode are kept on the star. Leaving the app or losing the
  link pauses the game; the star then returns to its effect.

### Android app 1.1.0

- New *Play* page: choose the mode, steer with large Left / Right (and Flip)
  pads, with haptic feedback.
- Carries firmware 1.6.0 for stars that need the update.

### Star Studio

- Comet card on the Developer page: play with the arrow keys.

## [1.5.2] - 2026-11-19

The first public release.

### Firmware

- 16 effects that run on the star: Golden Star, Liquid Gold (flows as the
  star is tilted), Snowfall, Shooting Stars, Candlelight, Hearth & Frost (two
  different faces), Northern Lights, Candy Cane, Tree Lights, Star of
  Bethlehem, Fireworks, Christmas Disco, Frost, Twinkle, Comets and Rainbow.
- Shake to drop: shaking knocks the lights loose, with any effect.
- Pictures and videos from the app, live or stored in the star's flash.
- Pairing: one star per phone; the Bluetooth command line is locked until
  the owner's key is proven (challenge-response). Shake the star for 5 s to
  pair it again.
- The star names itself in Bluetooth scans; rename it from the app.
- Firmware updates over Bluetooth with the MCUboot bootloader, signed images
  and automatic rollback. A gold ring on the star shows the progress.
- Supply protection keeps the Bluetooth module's 3.3 V supply in range.
- The app reads the effect list and preview clips from the star, so new
  effects need no app update.

### Android app 1.0.0

- Colour, Picture (photo/video, live or saved) and Effects pages with
  animated previews; the two-faced effects turn to show both sides.
- Stars sheet: nearby stars, pairing, renaming, releasing.
- Updates the star's firmware from the app.

### Files

| File | Use |
|---|---|
| `christmas-star-fw-<version>.signed.bin` | Firmware update over Bluetooth (app, `update_firmware.py`, Star Studio) |
| `christmas-star-fw-<version>-full.hex` | First install with a J-Link: bootloader + firmware |
| `christmas-star-fw-<version>-debug.*` | Debug variant with an extra command line over the J-Link (RTT) |
| `christmas-star-app-<version>.apk` | Android app for installing directly |
| `christmas-star-app-<version>.aab` | Android app bundle for Google Play |
| `SHA256SUMS.txt` | Checksums |

Bluetooth updates need a star that runs 1.3.0 or newer (the bootloader) and
has the BOOT0 pull-down (4.7–10 kΩ from pin 60 to ground).
