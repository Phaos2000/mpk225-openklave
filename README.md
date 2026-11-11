# MPK225 custom MIDI firmware — development branch

**Goal:** a substantially more capable Akai MPK225, beginning with a polyphonic one-finger chord engine, comprehensive harmonic suggestions and real MIDI output. Sequencing and a Windows editor are subsequent goals.

**Safety: offline-built, NOT installable MPK225 firmware. DO NOT FLASH.** The C++ musical engine and guarded OpenKlave-derived Cortex-M3 ELF builds pass CI, but real MPK225 USB/keybed timing, velocity, bootloader backup and recovery remain unverified.

## Working host-side modules (v0.3 milestone)

- `include/ChordEngine.h`: **49 chord voicings** (triads, 6/9, 7ths, extended, altered, suspended, quartal and power chords), tracked notes and shared-note-safe releases, 1,025-byte engine.
- `include/SmartHarmony.h`: no-heap chord compatibility and ranked suggestions over **588 root × chord possibilities**. Supports 12 tonic pitch classes and **12 scale modes** (seven diatonic modes, harmonic/melodic minor, pentatonics and chromatic). Strict in-key, borrowed and unrestricted filters; previous-chord common tones and approximate voice-motion ranking. Enumerates **all** matches or gives a bounded top-N list.
- `include/VoicingPlanner.h`: **actual MIDI-note inversions and register selection** with bounded, ordered per-voice semitone movement. Tested Cmaj7 → Fmaj7 moves just 4 total semitones; no dynamic memory.
- `include/MpkMidiPipeline.h`: legacy-event MIDI pipeline, chord mode disabled by default, FIFO pressure control, pad/CC passthrough, USB-MIDI packet formatting. `playPlannedVoicing()` now accepts absolute inverted notes while retaining safe physical-key note ownership.
- `tests/test_harmony_to_midi.cpp`: suggested G7 → one keyboard note → four proper note-ons and note-offs, with chord-mode held-key protection.
- `tests/test_voicing_planner.cpp` and `tests/test_voiced_playback.cpp`: all 588 root/type voicings fit their pitch classes, and minimal-motion Fmaj7 playback cleanly retriggers/releases 16 MIDI events.

**Important musical limitation:** scale membership checks the *actual voiced pitches*, not complete theoretical chord spellings. Current ranking uses pitch-class proximity and shared tones, **not** full functional harmony, full functional chord substitutions or globally optimal multi-chord orchestration. Those are next design/implementation work, not completed features.

## Reproduce tests (Linux/macOS/MinGW with C++17 compiler)

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Werror -Iinclude tests/test_chord_engine.cpp -o test_chord_engine && ./test_chord_engine
g++ -std=c++17 -O2 -Wall -Wextra -Werror -Iinclude tests/test_smart_harmony.cpp -o test_smart_harmony && ./test_smart_harmony
g++ -std=c++17 -O2 -Wall -Wextra -Werror -Iinclude tests/test_harmony_to_midi.cpp -o test_harmony_to_midi && ./test_harmony_to_midi
```

**Full ARM Cortex-M3 ELF linking has passed GitHub Actions CI**, including a separate, byte-different model-1 MPK225 mapping + chord-on-boot candidate. The build remains **non-installable and no firmware has been flashed**. Live keyboard tests, USB enumeration and verified recovery are still outstanding. See [MPK225 factory-derived keybed mapping](docs/MPK225_FACTORY_KEYBED.md).

## Device research

See `docs/MPK225_MODEL_IDENTIFICATION_V102.md` (MPK225 variant in Akai shared image), `docs/BOOTLOADER_RECOVERY_PREFLIGHT.md` (recovery prerequisites), and `docs/MPK225_USB_V102_OBSERVATIONS.md`.

Never execute OpenKlave's upstream `make burn` on the owner's keyboard. The repo is an independent research project, not affiliated with Akai Professional.

## Bounded path to completion (November 11, 2026)

The offline musical engine, USB device model, and guarded ARM application are implemented; a **laboratory-only, opt-in** USART1 MIDI IN/OUT adapter derives its pins from the MPK225 service schematic. The inherited OpenKlave startup's actual 48 MHz PLL, not its nominal 72 MHz comment, determines the MIDI serial divider. MIDI receive-only lab builds preserve the PA9 output pin. The generated firmware cannot produce a flash image through its release targets.

The remaining gates are *hardware acceptance*, not more theoretical tests: verify stock-firmware MIDI USB cable routing with the passive `monitor-all` tool; verify genuine MPK225 GPIO/keybed/panel/LCD behavior; obtain a nondestructive device-specific backup and prove recovery on a nonessential target; then measure real USB enumeration and MIDI performance. Details and states are tracked in [COMPLETION_CHECKLIST.md](docs/COMPLETION_CHECKLIST.md). No one should install the lab ELF before those gates pass.

The experimental native transport now schedules HID idle reports using STM32 USB SOF, validates USB-MIDI framing for DIN TX, and separately dequeues the explicitly configured MIDI cable without discarding other factory cables. These are offline code paths **not verified on hardware**. See [completion gates](docs/COMPLETION_CHECKLIST.md).
