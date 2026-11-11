# MPK225 custom MIDI firmware — development branch

**Goal:** a substantially more capable Akai MPK225, beginning with a polyphonic one-finger chord engine, comprehensive harmonic suggestions and real MIDI output. Sequencing and a Windows editor are subsequent goals.

**Safety: host-tested prototype, not installable MPK225 firmware. DO NOT FLASH.** Full MPK225 keybed/USB wiring, bootloader backup and recovery are not yet validated.

## Working host-side modules (v0.3 milestone)

- `include/ChordEngine.h`: **49 chord voicings** (triads, 6/9, 7ths, extended, altered, suspended, quartal and power chords), tracked notes and shared-note-safe releases, 1,025-byte engine.
- `include/SmartHarmony.h`: no-heap chord compatibility and ranked suggestions over **588 root × chord possibilities**. Supports 12 tonic pitch classes and **12 scale modes** (seven diatonic modes, harmonic/melodic minor, pentatonics and chromatic). Strict in-key, borrowed and unrestricted filters; previous-chord common tones and approximate voice-motion ranking. Enumerates **all** matches or gives a bounded top-N list.
- `include/MpkMidiPipeline.h`: legacy-event MIDI pipeline, chord mode disabled by default, FIFO pressure control, pad/CC passthrough, USB-MIDI packet formatting.
- `tests/test_harmony_to_midi.cpp`: suggested G7 → one keyboard note → four proper note-ons and note-offs, with chord-mode held-key protection.

**Important musical limitation:** scale membership checks the *actual voiced pitches*, not complete theoretical chord spellings. Current ranking uses pitch-class proximity and shared tones, **not** full functional harmony, genuine voice assignments, chord substitutions or optimal inverted MIDI voicings. Those are next design/implementation work, not completed features.

## Reproduce tests (Linux/macOS/MinGW with C++17 compiler)

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Werror -Iinclude tests/test_chord_engine.cpp -o test_chord_engine && ./test_chord_engine
g++ -std=c++17 -O2 -Wall -Wextra -Werror -Iinclude tests/test_smart_harmony.cpp -o test_smart_harmony && ./test_smart_harmony
g++ -std=c++17 -O2 -Wall -Wextra -Werror -Iinclude tests/test_harmony_to_midi.cpp -o test_harmony_to_midi && ./test_harmony_to_midi
```

Cortex-M3 syntax/object compilation was checked with Clang using `-target armv7m-none-eabi -mcpu=cortex-m3 -mthumb -ffreestanding -fno-exceptions -fno-rtti`. Full firmware build and real-device tests **have not** been done.

## Device research

See `docs/MPK225_MODEL_IDENTIFICATION_V102.md` (MPK225 variant in Akai shared image), `docs/BOOTLOADER_RECOVERY_PREFLIGHT.md` (recovery prerequisites), and `docs/MPK225_USB_V102_OBSERVATIONS.md`.

Never execute OpenKlave's upstream `make burn` on the owner's keyboard. The repo is an independent research project, not affiliated with Akai Professional.
