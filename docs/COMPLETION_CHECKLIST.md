# MPK225 custom firmware completion gates

Updated 2026-11-11. A compiler passing is not permission to install.

| Step | Bounded deliverable | Status / acceptance |
|---|---|---|
| 1. USB MIDI transport | Four cables, FIFO, NAK backpressure, peek/ACK without dropping undeliverable events | Offline code and tests; actual cable-to-physical DIN routes **blocked** |
| 2. USB composite and HID | Correct EP0, reset/reconfigure, change-only HID traffic | Offline code and tests; physical HID controls and nonzero-idle timing **blocked** |
| 3. Musical behavior | 49 chords, inversions, shared pad/key/chord note owners, panic, repeated presses | Offline host stress tests passed; listening/hardware test **blocked** |
| 4. Firmware build guards | ARM full link, app-only flash, exact shared metadata token, no overlap with inherited fixed RAM, no flashing commands | Offline CI; NOT installable |
| 5. Hardware identity | Verify MPK225 MIDI DIN UART routing, panel/LCD, keybed contact timing and velocity | Schematic identifies USART1 PA9/PA10; guarded TX/RX lab firmware builds exist but physical USB cable mapping, controller/LCD mapping, and live acceptance remain blocked |
| 6. Recovery | Nondestructive backup of flash+option bytes, two identical read hashes, verified restore on nonessential unit | **Blocked**; no physical backup or verified restore |
| 7. Physical end-to-end | USB enumeration, four cable responses, DIN IN/OUT, 25 keys, pads, buttons, releases, unplug/replug | **Blocked** by steps 5-6 |
| 8. Release | Build/version manifest and repeatable install/restore under verified procedure | **Blocked** until 5-7 pass |

Do not send update-mode SysEx, use OpenKlave burn/wipe, alter option bytes,
or install generated candidate images. Public MPK225 documentation confirms
DIN IN/OUT existence but does not prove the GPIO/UART wiring in this source.

Model-specific schematics: https://www.manualslib.com/manual/2857215/Akai-Mpk225.html?page=30 (STM32F103VC U12 MIDI_TX PA9 / MIDI_RX PA10); https://www.manualslib.com/manual/2857215/Akai-Mpk225.html?page=31 (DIN jacks J5/J4, input optocoupler). USB cable 2 binding is an **explicit lab experiment**, not yet a verified factory routing rule.

Read-only USB virtual-port mapping tool: py tools\mpk225_usb_probe.py monitor-all --seconds 60 --json mpk225-passive-routing.json. See docs/DIN_ROUTE_PASSIVE_AUDIT.md. Await factory MPK225 results to map MIDI IN cable.

DIN IN experimental software now links on the Cortex-M3: PA10 USART1 RX poll, MIDI status/SysEx/realtime packetization, bounded USB MIDI FIFO and 16-channel recovery after serial loss. Opt-in flags DIN_RX_LAB=1 DIN_RX_USB_CABLE=0..3; defaults disabled. Physical MIDI cable mapping and recovery gates remain OPEN.

**Clock correction (offline):** pinned upstream OpenKlave `src/config/system_stm32f10x.c` writes `RCC->CFGR=0x51440a` overriding its nominal 72 MHz code; for the documented 8 MHz crystal this produces 48 MHz PCLK2. DIN USART1 baud divider is 0x0600 (48 MHz/31,250), with fail-closed runtime clock-register checks. This is a code-path derivation, not a measurement of the owner's MPK225 firmware.

**Offline gate progress (2026-11-11):** RX-only USART1 no longer configures PA9 as an output or sets TX enable; the 31,250-baud divider is corrected for the upstream 48 MHz PLL; four-cable DIN-serial→USB→DIN roundtrip and full-queue retry tests added; undefined serial status bytes are rejected. The generated integration explicitly rejects DIN lab builds without FACTORY_SCAN=1. Physical tests and verified recovery remain strictly blocked.

**ELF flash-load gate:** CI now validates ELF32 PT_LOAD program headers and maps every allocated initialized section back to its flash load address. This additionally rejects `.data` initializers whose LMA overlaps bootloader, boot token, or preset flash—even if their virtual address correctly lies in SRAM. Guarded offline ELF only; not proof of successful hardware boot/recovery.
