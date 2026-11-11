# MPK225 model identification inside Akai MPK249/261 v1.02 application

**2026-11-10.** Verified offline by decoding the user-supplied `MPK249_&_MPK261_PC_1.02_Update.zip` and disassembling its Intel-HEX application payload for Thumb-2 (STM32F103). Firmware bytes are **not redistributed**. No USB traffic, device modification or flash operation occurred.

Firmware package SHA-256 (SysEx image): `1b3af40f844e040854375f28df0ff587a80ebcbff47714f0eba548d1abe6510b`.

## Major correction to earlier assessment

The application inside the nominal **MPK249/261** update package explicitly recognizes and configures the **MPK225**. This is much stronger than the updater's MPK225 port-name list.

### Internal model mapping — executable evidence

- Function `0x0800D850` returns a model-variant code **1–4**, based on GPIO inputs. It reads GPIOE's input-data register bit 8 (`0x40011808`) and GPIOD's input-data register bits 7 and 6 (`0x40011408`). No actual MPK225 pin states were measured.
- Function `0x0800D3A2` converts that model code to the Akai **product/model byte** (`0x23`, `0x24`, or `0x25`).
- Function `0x0800D4E2` converts it to the exact **keyboard key count** (25, 49 or 61).
- Function `0x0800DC3C` returns a model-specific **USB product-string descriptor**.

| Variant code | Model ID from executable | Key count from executable | USB descriptor |
|---|---|---:|---|
| 1 | `0x23` → MPK225 | 25 | MPK225 |
| 2 | `0x24` → MPK249 | 49 | MPK249 |
| 3 | `0x25` → MPK261 | 61 | MPK261 |
| 4 | `0x24` → MPK249 variant | 49 | generic MPK2 |

String descriptor sequence: `0x0801E4D4` generic MPK2; `0x0801E4DE` MPK225; `0x0801E4EC` MPK249; `0x0801E4FA` MPK261. The UTF-16LE product strings have normal USB string-descriptor `0x03` headers.

A second function `0x0800DC14` independently derives the model-ID byte; a USB device-descriptor preparation routine at `0x08018AC8` writes that value to offset +10 (USB `idProduct` position) in a RAM descriptor. This agrees with the owner's observed Windows ID **VID_09E8&PID_0023** for a physical MPK225 on factory firmware v1.02. Note that an exact complete device descriptor has not yet been captured from hardware.

### Factory presets — two variant-specific banks

`0x08008A2C` calls the hardware-model selector. For variant **1** it selects the second table of 25 pointers (table base `0x08028A08 + 100`), while variants **2 or 3** select the first table (base `0x08028A08`). Variant 4 skips this routine's preset initialization.

| Source bank | 25 records start at | Record stride | Assigned variants |
|---|---|---:|---|
| A | `0x0801F30E` | `0x60A` (1,546 bytes) | 2 and 3 (MPK249/MPK261) |
| B | `0x08028AD0` | `0x60A` (1,546 bytes) | 1 (MPK225) |

Each is **38,650 bytes / 37.74 KiB**; both together are 77,300 bytes / 75.49 KiB. Table pointers (25 `uint32_t` each) confirm the record addresses and stride. This is a plausible candidate for reclaiming about **37.7 KiB of application flash** in a clean, MPK225-only firmware build by omitting the irrelevant bank; it is **not** free space in Akai's existing executable.

### Other variant-specific behavior

A routine at `0x0800D88C` returns key-range offsets of 48 (variant 1), 36 (variants 2/4), or 24 (variant 3). Various other routines branch on the same model codes to vary controller-layout limits.

## What the finding does and does not establish

**Established:** The distributed v1.02 firmware executable has model-specific behavior for the MPK225 and recognizes its 25-key hardware, USB identity and dedicated factory preset bank.

**Still unverified:** Whether the *exact supplied firmware image* is identical to the one installed on the owner's MPK225; the MPK225 bootloader, flash write protections, readout restrictions and recovery behavior; unmodified RAM peak usage; model-specific hardware functionality under a newly built firmware.

**Safety:** Do not flash the MPK249/261 package, send update/erase SysEx, or patch stock firmware on the basis of this analysis. Hardware confirmation must be planned separately after a verified MPK225-specific recovery/backup route exists.
