# MPK225 bootloader and recovery — evidence and safety gates

2026-11-10. **No commands in this note were sent to the keyboard.** No flashing, option-byte changes, ST-Link connection or update-mode entry has been performed.

## Confirmed / likely flash layout (shared MPK2 application; physical MPK225 partition not yet dumped)

| Region | Start | End (exclusive) | Size | Evidence |
|---|---|---|---|---|
| Factory bootloader | 0x08000000 | 0x08006000 | 24 KiB | OpenKlave MPK249 readback/tutorial |
| Application | 0x08006000 | 0x08034000 | 184 KiB | Official MPK249/261 v1.02 HEX contents and OpenKlave MPK249 |
| Boot signature / metadata | 0x08033800 | 0x08034000 | final 2 KiB application page | OpenKlave MPK249: signature "AD4A_Richard" at 0x08033800; official shared image has it |
| Persistent settings and 30 presets | 0x08034000 | 0x08040000 | 48 KiB | Executable pointer formula 0x08034040 + slot * 0x60A |

The signature region is a **subset** of the application partition, not an extra region. The entire memory map needs **MPK225 on-device confirmation** before writing anything.

## Risk confirmed by source review

1. Akai's official [MPK2 updater guide](https://support.akaipro.com/en/support/solutions/articles/69000831517-akai-pro-mpk249-and-mpk261-updating-firmware-for-m1-compatibility) states that sending the update-mode SysEx may erase the currently installed application. Its instructions cover MPK225, but the user-supplied **Windows** ZIP lacks a standalone MPK225 updatemode.syx.
2. OpenKlave's [MPK249 bootloader research](https://www.openklave.com/tutorial2-rippingos/) reports that a custom application missing the `AD4A_Richard` token at `0x08033800` was deleted by the bootloader on reset. This behavior is **not yet independently demonstrated on MPK225**.
3. Upstream OpenKlave's `make burn` calls `util/wipe_os.py` before `util/flash_os.py`. Do not run it on the MPK225.
4. OpenKlave's original linker has `FLASH ORIGIN=0x08006000, LENGTH=256k`, past physical end `0x08040000`. An initial candidate integration also incorrectly permitted application use of the preset partition (`LENGTH=0x3A000`); corrected on development branch to `0x2E000`, ending at `0x08034000`. This defines a **linker boundary**; it is not a proven flash-safe updater.
5. The STM32's Read-Out Protection (RDP) is not known on this particular MPK225. According to [ST AN5156](https://www.st.com/content/st_com/en/technical-documents/an5156-introduction-to-security-for-stm32-mcus-stmicroelectronics_asset/chunk-006-topic-an5156-sec-feat-read-out-protection-rdp-.html), **downgrading RDP 1 to 0 mass-erases flash**. Some debug software offers an "unlock" flow that accomplishes exactly this. An ST-Link alone does not guarantee a nondestructive backup.

## Physical-device safety gates: all must pass before *any* firmware write

- **Gate 1: identity and PCB pinout.** Identify actual MCU part and noninvasively document the accessible SWD/JTAG contacts. MPK249 PCB header and signals cannot be assumed identical to MPK225.
- **Gate 2: readout status.** Check the device's security/readout protection without changing option bytes. **If locked, stop**: no "remove protection", "unlock", "erase", or reset of option bytes.
- **Gate 3: verified recoverable backup.** If and only if readout is permitted, acquire full internal-flash and option-byte backups through *read-only* debug procedures, verify hashes and multiple identical reads, and compare the application bytes with the official image. Back up stored presets separately.
- **Gate 4: proven recovery on a nonessential target.** Verify the exact MPK225 recovery protocol, firmware signature, update-mode behavior and flash page boundaries on a sacrificial unit or suitable test setup before risking the owner's keyboard.
- **Gate 5: image validation.** The build must have zero ELF section overlap, contain a correct signature at `0x08033800`, stay inside `0x08006000–0x08033FFF`, avoid any writes to bootloader/preset pages, and have tests verifying MIDI and controller operation. A successful build alone doesn't satisfy the prior gates.

## Findings that do *not* require touching the device

- The v1.02 shared application's executable contains MPK225 model ID, 25-key count, USB descriptor string and a dedicated 25-record preset table. This is recorded in [model identification](MPK225_MODEL_IDENTIFICATION_V102.md).
- The user observed USB VID:PID 09E8:0023 and Windows drivers `usbccgp`, `usbaudio`, `HidUsb`, `kbdhid` in normal operation.
- The official MPK249/261 package provides **application** bytes from `0x08006000`, but does **not contain a factory bootloader backup**. It is not a full-device restore image.
- The public OpenKlave repository's `util` directory contains a GDB readback *script*, but no factory bootloader dump. It cannot replace a backup of the user's device.

## Outcome

**Firmware flashing is blocked.** We can continue Windows-side chord engine and MIDI interface development safely, while the hardware/recovery path remains unresolved. Never describe this as a verified MPK225 firmware backup or safe recovery process.
