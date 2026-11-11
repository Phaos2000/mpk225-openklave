# MPK225 factory-derived keybed mapping (offline)

**Source:** User-supplied official Akai MPK249/261 v1.02 shared-application ZIP, decoded as Intel HEX, disassembled as ARM Thumb-2. No proprietary application binary is included in this repository. Not yet validated on the owner's physical MPK225.

## Code evidence

- `0x0800D850`: GPIO model variant detection. Variant 1 is MPK225, independently established by `0x0800D3A2` model ID `0x23` and `0x0800D4E2` 25-key count.
- `0x080171C4`: UART/keybed scan-code normalization. For model variant 1 (`0x0801721E` path), bit 4 rejects the byte, bits 0–2 and 5–7 determine the key index, and bit 3 does not alter key identity:
  ```cpp
  if (raw_scan_byte & 0x10) invalid;
  key_index = ((raw_scan_byte >> 5) * 8)
            + (raw_scan_byte & 7) - 4;
  if (key_index < 0 || key_index >= 25) invalid;
  ```
- `0x0800D88C`: returns model-specific **base MIDI offset** 48 for variant 1, 36 for 2/4, 24 for 3. The routine at `0x08009472` incorporates that offset into its MIDI-note calculation together with preset-adjustable pitch/octave settings. Thus **48–72 is the nominal untransposed MPK225 range**, not a guarantee for all user presets.
- A factory UART receive/transmit processing path (e.g. `0x0800EC50`, `0x080131D4`) refers to the shared communication buffer address `0x20001A86`, consistent with OpenKlave's MPK249 research. It does not prove identical physical wiring on MPK225.
- The low-byte `0xFF` key-down / `0xFE` key-up interpretation comes from OpenKlave's MPK249 receiver; **it has not been independently verified on the physical MPK225**. The factory parser checks both marker values, but the full action semantics remain a hardware acceptance gate.

## Mapping and evidence check

Each of the 25 normalized keys has two scan codes (differing only in bit 3). Examples:

| Raw scan bytes | Normalized key index | Nominal MIDI note |
|---|---:|---:|
| `04`, `0C` | 0 | 48 |
| `20`, `28` | 4 | 52 |
| `27`, `2F` | 11 | 59 |
| `40`, `48` | 12 | 60 |
| `47`, `4F` | 19 | 67 |
| `60`, `68` | 20 | 68 |
| `64`, `6C` | 24 | 72 |

All 256 raw scan bytes were enumerated: **50 accepted codes covering exactly 25 distinct notes**, 206 rejected; all other event-status bytes were rejected. See `include/Mpk225FactoryKeybedDecoder.h` and `tests/test_mpk225_factory_keybed.cpp`. The optional generated build selection is `FACTORY_SCAN=1`, with `CHORD_ON_BOOT=1` for a one-finger chord test. The untouched default remains `FACTORY_SCAN=0` (MPK249 reference mode), `CHORD_ON_BOOT=0` (passthrough). No image is installable.

## Important unresolved items

The incoming 32-bit UART packet layout used by OpenKlave has not been checked directly on MPK225. The official application also processes the packet's additional timing bytes and computes velocity from user settings (e.g. `0x080076EC`), whereas OpenKlave currently sends a **fixed velocity of 100**. Native USB-MIDI enumeration, preset compatibility, pad/control handling, scan timing, and bootloader recovery are not hardware-verified.

**Do not flash any generated ELF.** The offline build deliberately blocks update SysEx packaging and flashing. The firmware recovery gate must be passed before hardware experimentation.
