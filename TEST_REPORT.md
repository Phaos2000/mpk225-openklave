# MPK225 Jazz Chord Engine v0.2 — test evidence

Date: 2026-11-10

Test environment: local x86-64 Linux GCC and Clang; no MPK225 connection. Target is C++17 freestanding Cortex-M3 for the isolated modules, not a full flashed firmware.

| Validation | Observed result |
|---|---|
| Existing chord-engine suites | PASS — 7 suites, 200,000 deterministic reference-model operations, 24 chord types |
| MIDI pipeline | PASS — 6 suites, 200,000 deterministic events; chord selection, raw-note passthrough, pad and CC formatting, overlaps, queue backpressure |
| Legacy FIFO | PASS — FIFO ordering/wrap, 200,000 operations, legacy constexpr compatibility |
| MIDI end-to-end | PASS — 1, 25 and 50 key presses/releases; 50-key case: 128 emitted USB-MIDI messages in 64 8-byte frames, 4 input queue stalls, 0 stuck notes |
| Guarded patch tool | PASS — synthetic upstream fixture patched to new directory, original unchanged, `burn` disabled, refuses overwrite |
| Address/UB sanitizer | PASS — chord, pipeline, and FIFO C++ tests with ASan + UBSan |
| ARM compiler | PASS — Clang 17 `--target=arm-none-eabi -mcpu=cortex-m3 -mthumb -ffreestanding` generated ELF32 ARM relocatable object from `arm_compile.cpp` |
| Main Cortex-M3 object size (probe only) | 3,048 bytes text + 1,460 bytes data, zero BSS; sizes vary with optimization and code referenced |
| ChordEngine host object size | 1,025 bytes |
| MpkMidiPipeline host object size | 1,480 bytes |
| EventStack | 1,616 bytes of RAM, within the isolated 0xB00-byte region assumed by upstream MPK249 source |

### Not validated

- No real MPK225 firmware build and no ARM GCC full project linking. The patch script was tested against a fixture, not the full upstream tree.
- No USB peripheral IRQ correctness, real endpoint/DMA timing, keybed velocity mapping, encoder/button behavior, SysEx, flash layout readback or factory-firmware recovery.
- No Windows machine/hardware connection or Windows/MSVC host test execution.
- The upstream event FIFO's interrupt race/overflow strategy is NOT production ready. A failing incoming event queue enters a safe *fail-stop* loop, but this is unacceptable for live playing.
- MIDI output reference counting covers overlapping chords, not interactions with separate pad/raw MIDI streams sharing channel/note.

### Safety

This package intentionally contains **no flashing executable**, no generated `.syx` and no instructions to erase the OS. Do not install on the hardware until an independent recovery procedure has been confirmed.