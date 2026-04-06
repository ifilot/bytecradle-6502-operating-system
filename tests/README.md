# End-to-end tests

This directory contains end-to-end tests that validate the ByteCradle software stack:

1. build the emulator (`emulator/build/bc6502emu`)
2. build BCOS (`src/bin/bcos.bin`)
3. build example programs (`programs/*`)
4. run the emulator under a pseudo-terminal (PTY) and assert observable terminal output

## Test layout

- `tests/e2e/harness.py`: shared build/spawn/read helpers
- `tests/e2e/test_basic.py`: baseline E2E checks (`E2E OK` ROM and BCOS startup)
- `tests/e2e/test_hello_world.py`: BCOS shell flow (`cd programs`, `hello`)

## Running locally

```bash
pytest -q tests/e2e
```

A PTY is used on purpose because the emulator enables raw terminal mode on `stdin`, and interactive behavior is more reliable when run in a TTY-like environment.

## Optional SD-card integration test

`test_can_boot_bcos_and_run_hello_from_sdcard` expects an SD-card image that contains the sample programs.

- Default lookup path: `emulator/script/sdcard.img`
- Override path with: `BC6502_SDCARD_IMAGE=/path/to/sdcard.img`

If no SD image is found, that test is skipped.
