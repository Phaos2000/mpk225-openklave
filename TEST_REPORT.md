# Host test report — smart chord milestone v0.3

**2026-11-10.** Tested on local Linux x64 with GCC C++17. No MPK225 USB or firmware flash operations performed.

## Functional result

- **49** fixed chord voicings, **588** chromatic-root/chord combinations, **12** scale modes.
- Full deterministic enumeration; strict scale-membership, borrowed-chord and unrestricted filters; stable bounded top-N ranking.
- Chord pitches retain per-key note ownership. MIDI note-on/off traffic is ordered correctly, including planned inversion changes on a held trigger key.
- `VoicingPlanner.h` chooses octave/inversion candidates using actual MIDI note numbers and minimized ordered voice displacement. Cmaj7 `[60,64,67,71]` to Fmaj7 `[60,64,65,69]` costs **four semitones** in the tested range.
- `SmartHarmony.h` is a pure stateless query utility; `VoicingPlanner.h` is stateless too. Neither allocates heap memory.

## Executed checks — all passed

| Test | Evidence |
|---|---|
| `tests/test_chord_engine.cpp` | 7 suites, 200,000 randomized operations; 49 chord types; chord engine = 1,025 bytes |
| `tests/test_midi_pipeline.cpp` | 6 suites, 200,000 events; output queue and backpressure; pipeline = 1,480 bytes |
| `tests/test_openklave_fifo.cpp` | 200,000 FIFO operations, wrap and ordering |
| `tests/test_end_to_end.cpp` | 1/25/50 key scenarios, 4 backpressure stalls in the 50-key case, valid USB-MIDI packets |
| `tests/test_smart_harmony.cpp` | 73 strictly diatonic C-major options, all 588 unrestricted options, all key transpositions/modes, 20,000 randomized deterministic query comparisons |
| `tests/test_harmony_to_midi.cpp` | G7 recommendation played as four note-ons and four note-offs, correct note ownership |
| `tests/test_voicing_planner.cpp` | All 588 chord/root combinations maintain their pitch-class identity and stay within register; four-semitone Cmaj7→Fmaj7 case |
| `tests/test_voiced_playback.cpp` | Planned Cmaj7→Fmaj7 retrigger, sixteen ordered MIDI events, final key-up without stuck notes |
| `tests/test_patch_tool.py` | Source-preserving generated tree; correct 184 KiB linker boundary; new harmony/voicing headers copied; firmware flashing disabled |
| Clang Cortex-M3 object | `tests/arm_smart.cpp` compiled for `armv7m-none-eabi -mcpu=cortex-m3 -mthumb` |

GCC `-fsanitize=address,undefined` completed successfully on the four new musical tests (suggestions, voicing planner, G7 MIDI and inverted MIDI playback).

## Important limits

This is **not** a full ARM firmware build or USB device test. The OpenKlave bridge still relies on MPK249-era keybed/USB assumptions; MPK225-specific key scanning, UI, button mapping, recovery and flash write protection remain unresolved. Hardware flashing is forbidden in the candidate integration tree.

Compatibility examines **voiced tones only**, not complete chord spellings or harmonic function. Suggestion scoring uses common pitch classes, scale membership and a pitch-class motion approximation. The later `VoicingPlanner` uses actual note-level movement but searches a bounded family of inversions, **not every possible voicing**. Neither module yet implements formal chord-function substitutions.

## Reproduce

```sh
for test in test_chord_engine test_midi_pipeline test_openklave_fifo test_end_to_end test_smart_harmony test_harmony_to_midi test_voicing_planner test_voiced_playback; do
  g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -Iinclude tests/$test.cpp -o /tmp/$test && /tmp/$test
done
python3 tests/test_patch_tool.py
clang++ -target armv7m-none-eabi -mcpu=cortex-m3 -mthumb -std=c++17 -fno-rtti -fno-exceptions -ffreestanding -O2 -Iinclude -c tests/arm_smart.cpp -o /tmp/arm_smart.o
```
