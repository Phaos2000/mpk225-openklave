# Factory MPK225: passive four-port MIDI routing audit

**No new firmware and no USB/MIDI output from the computer.**

The factory MPK225 exposes four virtual USB-MIDI jacks over one bulk
endpoint. Its schematic shows a hardware MIDI IN jack and a USART1 connection,
but it does **not** establish which USB virtual port carries DIN traffic.
We must observe that mapping before enabling the candidate's MIDI DIN adapter.

## Stock firmware, read-only procedure (Windows 10)

Install only the standard Python MIDI listener if needed:
    py -m pip install python-rtmidi

Keep the MPK225 in normal firmware v1.02 mode and close DAWs using its MIDI
inputs exclusively. No firmware updater or SysEx commands are involved.

    py tools\mpk225_usb_probe.py midi-ports
    py tools\mpk225_usb_probe.py monitor-all --seconds 60 --match MPK225 --json mpk225-passive-routing.json

The listener opens all matching **input ports only**, reports every channel
voice MIDI message tagged by input index, counts note-on versus note-off per
port, and writes sanitized JSON (at most 5000 events). It never opens MIDI
output and ignores SysEx.

First press a few physical MPK225 keys, noting which virtual input reports
them. Next, **only if you already have** an ordinary external MIDI keyboard
with a 5-pin MIDI OUT, connect that output to MPK225's MIDI IN and play distinct
notes. Compare the receiving virtual input with the keybed input. Do not make
a MIDI feedback loop, and do not send SysEx or firmware update messages.

If the MPK225 internally forwards DIN to USB only under a particular factory
preset, the result is preset-dependent; capture that setting before inferring
the default routing. **Input observations do not prove the corresponding
host-to-device cable number for MIDI OUT.**

Use the JSON for subsequent firmware analysis. No serial numbers, PnP device
instance IDs, USB writes, driver edits, bootloader entry, or flashing are
necessary. A mismatch or absent messages is an unresolved observation, not
permission to guess the routing.
