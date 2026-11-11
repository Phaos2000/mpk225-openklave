# MPK225 custom firmware completion gates

Updated 2026-11-11. A compiler passing is not permission to install.

| Step | Bounded deliverable | Status / acceptance |
|---|---|---|
| 1. USB MIDI transport | Four cables, FIFO, NAK backpressure, peek/ACK without dropping undeliverable events | Offline code and tests; actual cable-to-physical DIN routes **blocked** |
| 2. USB composite and HID | Correct EP0, reset/reconfigure, change-only HID traffic | Offline code and tests; physical HID controls and nonzero-idle timing **blocked** |
| 3. Musical behavior | 49 chords, inversions, shared pad/key/chord note owners, panic, repeated presses | Offline host stress tests passed; listening/hardware test **blocked** |
| 4. Firmware build guards | ARM full link, app-only flash, exact shared metadata token, no overlap with inherited fixed RAM, no flashing commands | Offline CI; NOT installable |
| 5. Hardware identity | Verify MPK225 MIDI DIN UART routing, panel/LCD, keybed contact timing and velocity | USART1 PA9/PA10 schematic identified; opt-in DIN TX 31.25-kbaud lab candidate added; RX, physical cable mapping, live acceptance still blocked |
| 6. Recovery | Nondestructive backup of flash+option bytes, two identical read hashes, verified restore on nonessential unit | **Blocked**; no physical backup or verified restore |
| 7. Physical end-to-end | USB enumeration, four cable responses, DIN IN/OUT, 25 keys, pads, buttons, releases, unplug/replug | **Blocked** by steps 5-6 |
| 8. Release | Build/version manifest and repeatable install/restore under verified procedure | **Blocked** until 5-7 pass |

Do not send update-mode SysEx, use OpenKlave burn/wipe, alter option bytes,
or install generated candidate images. Public MPK225 documentation confirms
DIN IN/OUT existence but does not prove the GPIO/UART wiring in this source.

Model-specific schematics: https://www.manualslib.com/manual/2857215/Akai-Mpk225.html?page=30 (STM32F103VC U12 MIDI_TX PA9 / MIDI_RX PA10); https://www.manualslib.com/manual/2857215/Akai-Mpk225.html?page=31 (DIN jacks J5/J4, input optocoupler). USB cable 2 binding is an **explicit lab experiment**, not yet a verified factory routing rule.
