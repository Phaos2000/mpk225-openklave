# First real-keyboard audition — NO firmware installation

**What it proves:** Actual MPK225 key MIDI messages can drive the same 49-chord C++ engine that we intend to embed in firmware, with overlapping-note ownership and note-off cleanup. The keyboard continues running its untouched factory firmware v1.02. This does **not** validate OpenKlave's keybed decoder, USB firmware or bootloader.

## Obtain the program

Use the **MPK225 offline build and tests** workflow on the development branch's [GitHub Actions page](https://github.com/Phaos2000/mpk225-openklave/actions/workflows/offline-verify.yml). Download its `MPK225-Live-Chords-Win64-NOT-FIRMWARE` artifact from a successful run; extract `MPK225-Live-Chords.exe`. There is no updater, firmware `.syx`, driver replacement or installer.

The executable uses Windows' built-in **WinMM MIDI API**; no Python needed. It does not transmit SysEx and **refuses to open an output port whose name includes MPK225**.

## Observe the actual MPK225 keys and controls first (no output)

The same Windows executable has a **read-only MIDI monitor** that does not open a MIDI output port:

```bat
MPK225-Live-Chords.exe --list
MPK225-Live-Chords.exe --monitor --input 0
```

Replace `0` with the input index named **MPK225**. Press two or three keys at different velocities; release them, turn an encoder, move the pitch wheel, and optionally tap a pad. Each short channel-voice message is printed with the channel, event type, note/CC number and 7-bit value. Stop using **Ctrl+C**; the program reports note-on and note-off totals. No SysEx messages are sent or intercepted, and no firmware state changes.

This verifies the keyboard's **factory USB-MIDI output**, not its internal raw keybed wiring. If note-on counts exceed note-off counts after releasing all keys, capture the output for diagnosis. On some devices Note Off is delivered as Note On velocity zero; the monitor counts those correctly. Do not keep a DAW connected exclusively to the same MIDI input during capture.

## Connect MIDI safely

1. Connect your stock MPK225 to Windows by USB, normal mode. Do **not** press update-mode buttons.
2. Create a virtual MIDI port called `MPK225_Chords` using a utility such as [loopMIDI](https://www.tobias-erichsen.de/software/loopmidi.html), unless you already have a software synthesizer's MIDI output destination available.
3. Open a Command Prompt in the folder containing the extracted executable.
4. Run `MPK225-Live-Chords.exe --list`. Note the number for **MPK225 under MIDI inputs** and **MPK225_Chords under MIDI outputs**. Input and output numbering are independent.
5. Run, substituting your actual device indices:
   ```bat
   MPK225-Live-Chords.exe --input 0 --output 1 --chord maj7 --channel 1
   ```
6. In your DAW/instrument (e.g. Renoise), enable the `MPK225_Chords` MIDI input port as the note source. **Disable direct MPK225 note input** there while testing, or you'll hear doubled notes.
7. Play notes and listen; the physical key C produces C major seventh, for example. Ctrl+C stops the program and transmits releases for all active generated notes.

The program chords all note events on the selected input MIDI channel (default 1). If your pads transmit on the same channel as the keyboard, pads will also trigger chords. Set pads to a separate channel in the Akai factory preset or leave them unused for this test. Other channel-voice MIDI (pitch wheel, CC, notes on other channels) passes through unchanged.

## Inspect every compatible chord without connecting a keyboard

The Windows program now includes an **offline suggestion query**. It does not open any MIDI ports or transmit messages:

```bat
MPK225-Live-Chords.exe --suggest G --tonic C --scale major --filter strict
MPK225-Live-Chords.exe --suggest all --tonic C --scale major --filter strict
```

The first command lists *every compatible G-rooted chord quality* in C major, ranked by a deterministic heuristic; the second lists all compatible roots and qualities. For comparison try `--filter borrowed` or `--filter any`. The `non-scale` column reports the distinct voiced pitch classes outside the selected scale, not the complete theoretical chord spelling. A score is useful for ordering options, **not proof of musical correctness**.

## Optional live smart-chord mode

The same executable also supports an **opt-in** scale-aware mode with the actual `SmartHarmony` and `VoicingPlanner` code:

```bat
MPK225-Live-Chords.exe --input 0 --output 1 --smart --tonic C --scale major
```

Substitute the real MIDI input/output indices from `--list`. Default is still fixed-chord mode. With `--smart`, each played key is treated as a proposed **root** and the program selects a compatible chord quality from the library, then plans MIDI pitches near the pressed key with approximate minimal voice movement from the previous selected voicing. The console reports each choice. Scale choices include `major`, `minor`, `dorian`, `phrygian`, `lydian`, `mixolydian`, `locrian`, `harmonic-minor`, `melodic-minor`, `major-pentatonic`, `minor-pentatonic`, and `chromatic`. Tonic examples: `C`, `F#`, `Bb`.

Filters: `--filter strict` (default, all voiced notes in the scale), `--filter borrowed` (root in scale, up to two notes outside) or `--filter any` (all defined chords). An out-of-scale pressed root in strict mode plays a **single unchanged note**, never silence. The previous voicing is remembered even after releasing the previous key, so the next chord can minimize voice movement. In contrast, the individual MIDI notes currently held are tracked separately to guarantee correct note-offs.

This mode is **a transparent rule-based heuristic**, not complete harmonic intelligence: it does not infer a song's harmonic function or distinguish common jazz substitutions contextually. Test it as an audible proof of key/scale recognition and actual MIDI voicings.

## Limitations / acceptance checklist

- Source: `tools/mpk225_live_chords_win.cpp`; same `ChordEngine.h` as embedded prototype, but Windows handles MIDI transport.
- In fixed mode the chord type is selected at launch (`--chord maj7`, `--chord m9`, `--chord 7#9`, etc.). `--chord invalid` lists available names. Smart mode is experimental and the final hardware LCD recommendation/control UI remains unfinished.
- The executable's no-device `--selftest` runs in CI. Actual MPK225/DAW latency, MIDI routing, pads, Note Off and Ctrl+C shutdown must be confirmed by the owner.
- The tool will send **only channel-voice short MIDI messages** to the explicitly chosen output, never firmware commands.
- If a DAW has opened the keyboard's WinMM input exclusively, close that input before starting the bridge.
- Once the test succeeds, the next firmware-specific question is mapping real MPK225 keybed events onto those same chord operations. **Do not flash current OpenKlave-derived ELF.**
