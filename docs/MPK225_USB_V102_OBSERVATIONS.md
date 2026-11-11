# MPK225 v1.02 — measured USB device/configuration descriptors

## Evidence

Owner-provided **read-only USBTreeView** dump, November 11, 2026, from a physically connected MPK225 running factory firmware v1.02 on Windows 10, in normal operating mode. The dump includes full device, configuration, interface, endpoint and driver information. Host-specific device paths, identifiers and serial fields are intentionally excluded here.

These are **measured USB descriptors**, superseding our earlier Windows Plug-and-Play inference.

## Device and configuration

| Field | Observed value |
|---|---|
| Vendor:Product | `09E8:0023` (Akai MPK225) |
| USB version / speed | USB 1.1 / full speed 12 Mbit/s |
| Factory device version | `0x0102` |
| EP0 max packet | 8 bytes |
| Configuration descriptor `wTotalLength` | **169** bytes (`0x00A9`) |
| `bConfigurationValue` | **13** (`0x0D`) |
| Interfaces | **3** |
| Power | Bus-powered, 500 mA (`bmAttributes=0x80`, `bMaxPower=0xFA`) |
| Windows parent | USB Composite Device (`usbccgp`) |

### Interfaces and endpoints

| Interface | Class / subclass / protocol | Endpoint addresses and transfer details |
|---|---|---|
| 0 | Audio Control `01/01/00` | None |
| 1 | MIDIStreaming `01/03/00` | **`0x01` bulk OUT 64 bytes**, **`0x82` bulk IN 64 bytes** |
| 2 | HID Boot Keyboard `03/01/01` | **`0x83` interrupt IN 8 bytes, 10 ms** |

The MIDIStreaming interface has **four embedded IN jacks** (IDs 1–4) and **four embedded OUT jacks** (IDs 5–8), named `Port A`, `Port B`, `MIDI`, `Remote`. The class-specific MIDI header uses `wTotalLength=0x0065` (101). Windows exposes eight MIDI pins (four in each direction), consistent with this.

**USB direction is relative to the host:** `0x82` is the MPK225→computer MIDI stream; `0x01` is host→MPK225 MIDI traffic. The four jack numbers appear in USB-MIDI cable-number fields, not as four separate physical USB endpoints.

### HID interface anomaly

The factory configuration's HID Interface 2 sets `bNumEndpoints=1` yet USBTreeView lists **both** `0x83` interrupt IN and **`0x03` interrupt OUT**, each 8 bytes with 10-ms interval. Windows reported only three open data pipes overall: `0x01`, `0x82`, and `0x83`. We preserve the extra endpoint descriptor in the exact 169-byte model, but cannot assume that `0x03` is actually operational. The HID descriptor advertises one report descriptor of **63 bytes**; the Windows USB API could not read those report bytes in USBTreeView. The original Akai shared v1.02 application includes a standard keyboard report descriptor of the same length, but byte-for-byte equality over USB is not independently established.

## What was implemented

- `include/Mpk225UsbDescriptors.h`: a **169-byte configuration descriptor model** and 18-byte device descriptor, with the observed composite topology, jack IDs, endpoint attributes and anomaly.
- `include/Mpk225UsbControl.h`: allocation-free **EP0 descriptor request and 8-byte packet model**, including strings and HID report.
- `include/Mpk225UsbDeviceState.h`: EP0 enumeration state machine, including address/configuration and HID control requests.
- `include/Mpk225UsbHardware.h`: **experimental, ARM-compiled STM32F103 composite USB MMIO driver**. In the `FACTORY_SCAN=1` candidate it is connected to the USB interrupt and MIDI foreground loop. Uses EP1 OUT / EP2 IN 64-byte bulk, EP3 IN HID interrupt; the old MPK249 transport remains only in the reference branch.
- `tests/test_factory_usb_descriptors.cpp`, `tests/test_usb_control_model.cpp`, `tests/test_usb_device_state.cpp`, `tests/test_usb_pma_layout.cpp`: descriptor, EP0 state, PMA size/overlap tests. **EP1's COUNT_RX was corrected from 0x8400 (32 B) to 0x8800 (64 B)**; a regression test verifies the STM32 register encoding.

No device communication, driver modification, update SysEx, firmware backup or flashing occurred.

## What this definitively changes

The inherited `sillsm/openklave` USB implementation is **not compatible with the observed MPK225 configuration**. It declares a MIDI-only two-interface configuration with a single jack pair, 8-byte interrupt endpoint traffic and a simple EP1 IN transmit bridge. Changing `PID` to `0023` alone cannot produce equivalent MPK225 functionality.

The integrated composite USB **candidate now exists and compiles/links offline**. Its enumeration state machine, buffer layout, EP0 responses and basic MIDI/HID endpoint scheduling are tested without connecting to the keyboard. **A successful ELF link is not proof of USB enumeration**: EP0 transfer timing, the actual hardware interrupt response, endpoints' DMA/PMA behavior, and compatibility with the MPK225's HID buttons remain untested on the device.

The four cable/jack descriptors are preserved, but **four-port MIDI routing is not complete**: incoming host USB-MIDI packets have a queue, while physical MIDI DIN/OUT and Remote routing are not yet bound to verified MPK225 peripheral paths. Similarly, HID report production exists but factory button-to-HID mapping is not verified. Do not claim hardware compatibility or factory feature parity. The HID OUT descriptor discrepancy requires special care.

Firmware bootloader readback, recovery, MPK225 USB enumeration under the candidate, and real keybed timing remain **unverified**. All candidate flashing remains disabled.

## References

- Owner's USBTreeView report (provided in the development conversation, 2026-11-11; serial/host fields omitted from repository)
- [OpenKlave reference](https://github.com/sillsm/openklave)
- [Microsoft composite device interface collections](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/support-for-interface-collections)
