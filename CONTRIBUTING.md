# Contributing to Christmas Star

Thank you for your interest! Bug reports, ideas, new effects and fixes are
all welcome.

## How changes get in

- The `main` branch, merges and releases are handled by the maintainers.
- To contribute, **fork** the repository, make your change on a branch of
  your fork and open a **pull request** against `main`.
- The CI runs on every pull request: firmware builds (release and debug) and
  unit tests in QEMU, the Android build with tests and lint, and the Python
  tests. For a first-time contributor a maintainer approves the run.
- A maintainer reviews and merges. Releases are made by the maintainers from
  version tags.

## Good to know

- **Effects** live in `sw/src/effects/`. A new effect needs a name, title,
  subtitle and flags in its `struct effect`, and an entry in
  `effects_builtin[]` (`engine.c`). The apps pick it up from the star, so no
  app change is needed. Keep the frame rate at 50 fps on the Cortex-M0: work
  out per-frame values once, not for every LED.
- **Tests**: add a unit test in `sw/tests/unit/src/` for new logic.
- **Secrets never go into the repository**: `sw/dev_key.txt`,
  `sw/signing_key.pem` and the Android keystore are ignored by git. Your
  own builds need your own signing key (see the README); official releases
  are signed by the maintainers.
- Code style follows the surrounding code (Zephyr style in C, standard
  Kotlin style, PEP 8 in Python).

## Licenses

By contributing you agree that your contribution is licensed under the
project's licenses: [MIT](LICENSE) for software and
[CERN-OHL-P-2.0](hw/LICENSE) for the hardware files in `hw/`.

## Security

Please report security issues privately, not as public issues: see
[SECURITY.md](SECURITY.md).
