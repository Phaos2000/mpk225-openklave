# Offline firmware audit — Akai MPK249/261 v1.02

**Date:** 2026-11-10  
**Source:** User-supplied `MPK249_&_MPK261_PC_1.02_Update.zip`.  
**Safety:** Analysis of archive bytes only. No executable updater run; no USB commands or flashing. **This is NOT an MPK225 firmware dump.**

## Verified archive format

- `MPK249_MPK261_FW_v1_02.syx` is one SysEx envelope starting `F0 47 00 24 70` (MPK249 device byte `24`) containing ASCII Intel HEX.
- All **11,384 HEX records** have valid checksums: 11,375 data, 7 extended-linear-address, 1 start-address, 1 EOF. There are **181,988 specified flash bytes**, including 20 bytes of end metadata.
- Exactly the same `.syx` message is embedded inside `.upd` beginning at file offset **564**.
- The `.upd` includes three separate 8-byte update-mode messages addressed to `23` (MPK225), `24` (MPK249), and `25` (MPK261), but there is **no standalone MPK225 update-mode .syx** in this Windows ZIP.
- One image with MPK249 device header does **not prove** compatibility with MPK225, nor how the updater handles model routing.

## Flash image map (decoded, Intel HEX addresses)

| Range (inclusive) | Supplied bytes | Interpretation |
|---|---:|---|
| `08006000–080326CF` | 181,968 (177.70 KiB) | Application image: code, tables, data |
| `08033800–0803380F` | 16 | Metadata including ASCII `AD4A_Richard` |
| `08033FFC–08033FFF` | 4 | Metadata, meaning not established |

The updater supplies **no** bootloader bytes at `08000000–08005FFF`. OpenKlave independently reports that approximately 24 KiB is reserved for the MPK249 bootloader. The intended application partition is `08006000–08033FFF`, **184 KiB**. A gap in the HEX image is **not evidence of safely free flash**.

The application vector table at `08006000` contains reset address `080061AB` (Thumb mode). Intel HEX type-05 start address is `08006131`; firmware startup code branches through both paths.

## RAM initialization and stack

Disassembling the actual Cortex-M3 startup at `08006130–080061B7` gives this two-entry initialization table:

| Source | Destination | Destination bytes | Callback |
|---|---|---:|---|
| `08032378` | `20000000` | `0x0E48` = 3,656 | `0800627C` (data initialization/decompression routine) |
| `080326D0` | `20000E48` | `0x4198` = 16,792 | `08006178` (zero-fill routine) |

The runtime stack-pointer literal is **`20004FE0`**, equal to the end of the described RAM initialization region. This suggests a **~20 KiB configured application RAM region, including reserved stack/zero-initialized space**, not a measured 20 KiB peak usage. The initial vector entry `20000400` is replaced by startup code.

The referenced STM32F103VCT6 part has **48 KiB physical SRAM**. The difference suggests significant hardware headroom, **but no MPK225 runtime RAM usage is verified**, and dynamic access or variant-dependent configuration could change this assessment.

## Persistent preset storage — executable proof

The routine at `0800D834` rejects slot numbers **>=30**, multiplies the slot number by `0x305`, doubles the result, and adds the literal **`08034040`**. Therefore:

`slot_address = 0x08034040 + slot_number * 0x60A` for slots 0 through 29.

| Calculated allocation | Bytes |
|---|---:|
| Region `08034000–0803FFFF` | 49,152 (48 KiB) |
| 30 x 1,546-byte stored preset slots | 46,380 (45.29 KiB) |
| Space outside these slots, including the first 64 bytes | 2,772 |
| End of slot 29 (exclusive) | `0803F56C` |

This is **record address arithmetic in native executable code**, not a guess from string spacing. Other routines reference `08034000`, `08034010`, `08034014` and directly operate the STM32 flash controller at `40022000`. Flash unlock constants `45670123` and `CDEF89AB` appear in its code.

## Factory preset payloads in application flash

Two static groups each contain **25 recognizable eight-character preset names**, at:
- Bank A: `0801F30E + index * 0x60A` (`index=0..24`)
- Bank B: `08028AD0 + index * 0x60A` (`index=0..24`)

The names include LiveLite, MpcEsntl, Hybrid 3, ProTools, ScalePad and Generic. Most names match across groups; one differs (`DarkPad` / `DarkPads`). The fixed 1,546-byte stride is also used by firmware code. The two groups represent **~77,300 bytes (75.49 KiB)** of embedded preset records. Their relationship to the hardware models has **not** been fully determined.

This makes factory preset tables a credible major flash-budget opportunity for a *new* firmware design. It does not demonstrate a patchable free area within the factory program.

### Redundancy check: factory preset banks

A byte-by-byte comparison of the corresponding 25 records (1,546 bytes per record, 38,650 bytes per bank) found **32,039 matching bytes of 38,650 (82.90%)** across the two banks. Three entries are byte-identical, including Battery4, ScalePad and Generic.

As an *offline compressibility experiment only* (Python zlib level 9), the combined **77,300 bytes compressed to 9,099 bytes**. Compressing individual records separately produced 18,554 bytes combined. This is **not an implementable flash-footprint measurement**: decompression code, indexing, buffer allocation, latency and integrity controls cost additional memory and CPU time. But it shows the factory mapping tables are sparse/repetitive; a clean firmware can store factory-like presets much more compactly, or omit unused mappings entirely. Do **not** treat these bytes as free space in the shipped firmware.

## Model compatibility warning

Akai's [firmware support page](https://support.akaipro.com/en/support/solutions/articles/69000831517-akai-pro-mpk249-and-mpk261-updating-firmware-for-m1-compatibility) inconsistently mentions MPK225 but titles the update MPK249/261. Akai's [macOS compatibility table](https://support.akaipro.com/en/support/solutions/articles/69000824929-akai-pro-macos-13-ventura-compatibility) specifically says MPK249/261 need firmware v1.02 for Apple Silicon, while MPK225 is already listed compatible. The supplied ZIP contains only an MPK249/261-named application image with `0x24` SysEx target. **Do not send this firmware/update-mode payload to an MPK225.**

## Further disassembly: hardware variant selection and flash granularity

The function at `0800D850` reads GPIO inputs using a shared helper at `08008E46`, examining a bit on GPIOE and two bits on GPIOD. It returns one of **four configuration codes (1–4)**. At `08008A2C`, the firmware uses that value to select one of its two 25-record factory preset groups, or to skip initializing them. **This is evidence of runtime hardware-variant configuration**, not proof of which model maps to which code or that this update image is safe on an MPK225.

ST's [RM0008 reference manual](https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf) specifies **2 KiB flash erase pages** for high-density STM32F103 parts. The factory's 1,546-byte preset records do not align to those pages, so overwriting an individual preset requires careful preservation of adjacent data. A custom pattern/song store should instead use page-aligned records or a tested transactional scheme; this is especially important for power-loss recovery and flash endurance.

## Engineering consequence

For our **future MPK225 custom firmware**, a small chord/voice-leading engine is very plausible given the physical MCU. The main constraints are proving the MPK225's own flash layout, bootloader/recovery, keybed/pad handling, and allocating persistent data without destroying factory presets. Replacing redundant preset tables, and using a compact preset format, could make substantially more room for sophisticated chord logic and sequencer storage than the current OS exposes.

**Unresolved:** MPK225-specific firmware bytes and actual RAM layout; factory bootloader backup/readout; hardware-specific model selection; which exact flash pages can safely be repurposed. No device-modifying action is authorized by this audit.
