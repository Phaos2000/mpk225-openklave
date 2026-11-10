# MPK225 USB read-only investigation (factory firmware v1.02)

**Safety:** This stage reads PnP metadata and incoming MIDI only. Do not send arbitrary SysEx, enter firmware-update mode, run the OpenKlave `wipe_os.py` / `flash_os.py`, replace drivers, or flash the keyboard. Nothing here constitutes verified memory-read access.

## Known versus unverified

- **Known:** Factory firmware presents MIDI ports to the host and supports exchanging user preset data via its documented SysEx features. Akai publishes a separate firmware-updater/emergency-SysEx process, which **must not** be used for routine investigation.
- **Known:** Windows can obtain USB device/configuration/interface/endpoint descriptors using [Microsoft USBView](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/usbview). [USB Device Tree Viewer](https://www.uwe-sieber.de/usbtreeview_e.html) offers an easy GUI for this.
- **Known only for MPK249:** The OpenKlave author dumped flash by connecting a physical ST-Link debugger, not over standard MIDI USB; the author documented an approximately 24 KiB bootloader plus operating system and data region on the MPK249. Do not assume an MPK225 has the same flash partition or bootloader commands.
- **Unknown:** The MPK225's exact USB VID:PID, interface count, endpoint settings, memory readback capabilities, and undocumented SysEx commands. This stage measures identity and endpoints without sending device-modifying commands.

## Windows 10: passive device inventory

From the checked-out repository, with Python 3 installed:

```powershell
py tools\mpk225_usb_probe.py inventory
py tools\mpk225_usb_probe.py inventory --json mpk225-usb-sanitized.json
```

The report omits serial numbers and raw device-instance identifiers. If no MPK225-named device appears, use USBTreeView to identify its VID:PID and retry using `--vid-pid XXXX:XXXX`.

### Full USB descriptor details

Use USBTreeView (or Microsoft's USBView) to select MPK225 while connected in **normal operating mode**. Record the VID/PID, `bDeviceClass`, number and classes of interfaces, endpoint addresses, types and maximum packet sizes. Avoid posting device serial numbers or entire unredacted system reports publicly.

Note: Windows PnP inventory does *not* return the full USB endpoint descriptors. Use the viewer for that part.

## Windows 10: passive incoming MIDI

Only if you want to measure keys, pads, encoders and velocity:

```powershell
py -m pip install python-rtmidi
py tools\mpk225_usb_probe.py midi-ports
py tools\mpk225_usb_probe.py monitor --seconds 30
```

If multiple MPK225 input ports appear, select one with `--port INDEX`. Run separately for each input port. The monitor opens **MIDI input only**; SysEx, timing messages and active sensing are ignored. It never sends MIDI or SysEx. Do not use this mode while a DAW needs exclusive ownership of the same MIDI port.

## Next engineering questions

1. Which exact USB MIDI IN/OUT endpoint configurations and host-visible ports exist on MPK225 v1.02?
2. Does the controller expose a separate non-MIDI interface for factory updates, or is the update process carried entirely over USB-MIDI SysEx?
3. Which physical controls generate ordinary MIDI messages under stock firmware, and which only affect internal preset/navigation state?
4. Is the MPK225 USB and keybed architecture genuinely compatible with MPK249 OpenKlave assumptions?

Device discovery and MIDI event logging are useful, but **cannot establish flash/RAM contents or factory-firmware recovery**. That needs a separate, model-specific procedure.