# MPK225 v1.02 — USB interface evidence (2026-11-10)

## Observed on the owner's Windows 10 machine

Factory firmware version **1.02** (read from MPK225 Global/OS). Connected normally by USB, no bootloader/update mode. Windows Plug-and-Play enumerated **VID_09E8 & PID_0023**:

| PnP device | Reported interface | Windows service | Status |
|---|---|---|---|
| USB Composite Device | device parent (none) | `usbccgp` | OK |
| MPK225 | `MI_00` | `usbaudio` | OK |
| USB Input Device | `MI_02` | `HidUsb` | OK |
| HID Keyboard Device | `MI_02` | `kbdhid` | OK |

These are **observations from the owner's read-only PowerShell queries**, not decoded USB descriptor bytes. Windows reports two PnP entries for one HID interface; these do *not* establish two HID USB interfaces.

## Interpretation (not yet measured)

- USB-MIDI 1.0 devices normally use an **AudioControl** interface and a **MIDIStreaming** interface, which Windows may present under a single `usbaudio` device/function. Thus `MI_01` is *likely* MIDIStreaming, even though no separate `MI_01` PnP entry appeared. **This remains a hypothesis until descriptor readout.**
- Akai documents assignable switches sending computer keystrokes via HID. This explains the `MI_02` HID keyboard interface. It is *not* evidence of a factory debugging facility.
- No evidence so far that stock firmware exposes RAM, flash dump, register debugging, or arbitrary-program execution over USB. OpenKlave's MPK249 author used a *physical* ST-Link debugger for memory readback.

## Next safe observation

Use USB Device Tree Viewer or Microsoft USBView **in normal keyboard operating mode** and record the **configuration/interface/endpoint descriptors** for VID 09E8, PID 0023, particularly:

- `bNumInterfaces`
- for each interface: `bInterfaceNumber`, `bAlternateSetting`, `bInterfaceClass`, `bInterfaceSubClass`, `bInterfaceProtocol`
- each `bEndpointAddress`, `bmAttributes`, `wMaxPacketSize` (and interval where present)

Strip serial numbers, instance paths, host/hub identifiers before publishing. This is a descriptor read; no need to send SysEx, modify drivers, flash, or open the keyboard.

## Public references

- [Akai MPK225 Quickstart Guide (HID keyboard shortcuts)](https://cdn.inmusicbrands.com/akai/attachments/MPK225/MPK225%20-%20Quickstart%20Guide%20-%20v1.1.pdf)
- [USB-IF USB MIDI Device Class Definition](https://www.usb.org/sites/default/files/USB%20MIDI%20v2_0.pdf) (legacy AudioControl/MIDIStreaming interface model)
- [Microsoft: Interface collections on USB composite devices](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/support-for-interface-collections)
- [Microsoft: Windows built-in USB class drivers](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/supported-usb-classes)
- [OpenKlave (MPK249 research, not MPK225 verification)](https://github.com/sillsm/openklave)

**No software was installed or tested on the owner's device as part of this source review.**
