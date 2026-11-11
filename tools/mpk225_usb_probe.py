#!/usr/bin/env python3
"""Passive MPK225 USB/PnP inventory and MIDI input monitoring on Windows 10.

No outbound USB/MIDI messages, firmware changes, or driver changes.
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import time
from pathlib import Path

USB_ID_RE = re.compile(r"VID_([0-9A-F]{4})&PID_([0-9A-F]{4})", re.IGNORECASE)
MI_RE = re.compile(r"&MI_([0-9A-F]{2})", re.IGNORECASE)

# This command only queries the Windows Plug-and-Play device inventory.
POWERSHELL_INVENTORY = r"""
$ErrorActionPreference = 'Stop'
Get-CimInstance -ClassName Win32_PnPEntity |
    Select-Object Name, Description, Manufacturer, PNPDeviceID, PNPClass, Service, Status, HardwareID |
    ConvertTo-Json -Depth 4 -Compress
"""


def parse_pnp_json(raw: str) -> list[dict]:
    data = json.loads(raw.strip())
    if data is None:
        return []
    if isinstance(data, dict):
        data = [data]
    if not isinstance(data, list) or not all(isinstance(x, dict) for x in data):
        raise ValueError("Unexpected Windows PnP inventory response")
    return data


def device_vid_pid(device: dict) -> tuple[str, str] | None:
    fields = [device.get("PNPDeviceID") or ""]
    hardware = device.get("HardwareID") or []
    fields.extend(hardware if isinstance(hardware, list) else [hardware])
    for field in fields:
        match = USB_ID_RE.search(str(field))
        if match:
            return match.group(1).upper(), match.group(2).upper()
    return None


def select_mpk_devices(devices: list[dict], vid_pid: str | None = None) -> list[dict]:
    if vid_pid:
        match = re.fullmatch(r"([0-9A-Fa-f]{4}):([0-9A-Fa-f]{4})", vid_pid)
        if not match:
            raise ValueError("--vid-pid must be in XXXX:XXXX hexadecimal form")
        target = (match.group(1).upper(), match.group(2).upper())
        return [d for d in devices if device_vid_pid(d) == target]
    seeds = [d for d in devices if "mpk225" in " ".join(str(d.get(k) or "") for k in
             ("Name", "Description", "PNPDeviceID")).lower()]
    ids = {device_vid_pid(d) for d in seeds} - {None}
    selected = [d for d in devices if d in seeds or device_vid_pid(d) in ids]
    return selected


def safe_identity(device: dict) -> dict:
    # Raw PNPDeviceID may contain a serial number; never print or save it.
    raw_id = str(device.get("PNPDeviceID") or "")
    match = USB_ID_RE.search(raw_id)
    interface = MI_RE.search(raw_id)
    hardware = device.get("HardwareID") or []
    hardware = hardware if isinstance(hardware, list) else [hardware]
    safe_ids = []
    for item in hardware:
        m = USB_ID_RE.search(str(item))
        if m:
            safe_ids.append(f"VID_{m.group(1).upper()}&PID_{m.group(2).upper()}")
    return {
        "name": device.get("Name") or "",
        "description": device.get("Description") or "",
        "manufacturer": device.get("Manufacturer") or "",
        "pnp_class": device.get("PNPClass") or "",
        "service": device.get("Service") or "",
        "status": device.get("Status") or "",
        "vid_pid": f"{match.group(1).upper()}:{match.group(2).upper()}" if match else
                   (":".join(device_vid_pid(device)) if device_vid_pid(device) else None),
        "interface_number": interface.group(1).upper() if interface else None,
        "hardware_ids": sorted(set(safe_ids)),
    }


def inventory(vid_pid: str | None, output: Path | None) -> int:
    if sys.platform != "win32":
        print("Inventory requires Windows 10/11.", file=sys.stderr)
        return 2
    try:
        result = subprocess.run(
            ["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", POWERSHELL_INVENTORY],
            check=True, capture_output=True, text=True, timeout=45,
        )
        matches = select_mpk_devices(parse_pnp_json(result.stdout), vid_pid)
    except (subprocess.CalledProcessError, OSError, subprocess.TimeoutExpired, ValueError) as exc:
        print(f"PnP inventory failed: {exc}", file=sys.stderr)
        return 2
    report = {"target": "Akai MPK225", "passive": True,
              "matches": [safe_identity(d) for d in matches],
              "endpoint_descriptors_included": False}
    print(json.dumps(report, indent=2, ensure_ascii=True))
    if output is not None:
        output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(f"Sanitized report saved to {output}")
    if not matches:
        print("No MPK225-named devices found. USBTreeView can reveal VID:PID; "
              "then retry with --vid-pid XXXX:XXXX.", file=sys.stderr)
        return 1
    return 0


def available_input_ports():
    try:
        import rtmidi  # Optional: pip install python-rtmidi
    except ImportError as exc:
        raise RuntimeError("MIDI monitoring requires: py -m pip install python-rtmidi") from exc
    midi_in = rtmidi.MidiIn()
    return midi_in, midi_in.get_ports()


def midi_ports() -> int:
    try:
        midi_in, ports = available_input_ports()
    except RuntimeError as exc:
        print(exc, file=sys.stderr)
        return 2
    try:
        for i, port in enumerate(ports):
            print(f"{i}: {port}")
        if not ports:
            print("No MIDI input ports available")
    finally:
        del midi_in
    return 0


def describe_midi(message: list[int]) -> str:
    if not message:
        return "empty"
    status = message[0]
    if 0x80 <= status <= 0xEF:
        ch = (status & 0x0F) + 1
        kind = status & 0xF0
        data = message[1:]
        if kind in (0x80, 0x90) and len(data) >= 2:
            name = "NOTE_OFF" if kind == 0x80 or data[1] == 0 else "NOTE_ON"
            return f"{name} ch={ch} note={data[0]} velocity={data[1]}"
        if kind == 0xA0 and len(data) >= 2:
            return f"POLY_PRESSURE ch={ch} note={data[0]} value={data[1]}"
        if kind == 0xB0 and len(data) >= 2:
            return f"CC ch={ch} controller={data[0]} value={data[1]}"
        if kind == 0xC0 and len(data) >= 1:
            return f"PROGRAM_CHANGE ch={ch} program={data[0]}"
        if kind == 0xD0 and len(data) >= 1:
            return f"CHANNEL_PRESSURE ch={ch} value={data[0]}"
        if kind == 0xE0 and len(data) >= 2:
            return f"PITCH_BEND ch={ch} value={(data[1] << 7) | data[0]}"
    return "OTHER_MIDI status=" + hex(status) + " bytes=" + str(len(message))



def matched_input_indices(ports: list[str], name: str = "MPK225") -> list[tuple[int, str]]:
    """Select actual WinMM input ports by display name, never output ports."""
    if not name or not name.strip():
        raise ValueError("--match must contain a device name")
    key = name.casefold()
    return [(i, label) for i, label in enumerate(ports) if key in label.casefold()]


def classify_note_message(message: list[int]) -> str:
    if len(message) >= 3 and (message[0] & 0xF0) == 0x90 and message[2] != 0:
        return "note_on"
    if len(message) >= 3 and ((message[0] & 0xF0) == 0x80 or
                             ((message[0] & 0xF0) == 0x90 and message[2] == 0)):
        return "note_off"
    return "other"


def monitor_all_inputs(seconds: int, match: str = "MPK225",
                       output: Path | None = None,
                       *, clock=None, pause=None) -> int:
    """Read-only, simultaneous passive monitor for factory USB virtual ports.

    Opens MIDI INPUTS only. Does not send SysEx, MIDI, firmware commands, or
    device inquiries. A printed port assignment is observational evidence,
    not a verified hardware cable-number or DIN-transceiver mapping.
    """
    if seconds < 1 or seconds > 600:
        raise ValueError("duration must be 1..600 seconds")
    clock = clock or time.monotonic
    pause = pause or time.sleep
    try:
        prototype, ports = available_input_ports()
    except RuntimeError as exc:
        print(exc, file=sys.stderr)
        return 2
    selections = matched_input_indices(ports, match)
    client_type = type(prototype)
    del prototype
    if not selections:
        print("No matching MIDI inputs. Use midi-ports to inspect names; "
              "choose --match TEXT to narrow the selection.", file=sys.stderr)
        return 2
    handles = []
    events = []
    dropped = 0
    summary = {str(i): {"name": name, "note_on": 0, "note_off": 0, "other": 0}
               for i, name in selections}
    try:
        for index, name in selections:
            handle = client_type()
            handles.append(handle)  # close even if open_port raises
            handle.open_port(index)
            handle.ignore_types(sysex=True, timing=True, active_sense=True)
            print(f"Monitoring MIDI input {index}: {name}")
        print("All ports are input-only. No MIDI output and no SysEx "
              "transmission; stop with Ctrl+C.")
        start = clock()
        while clock() - start < seconds:
            got = False
            for (index, name), handle in zip(selections, handles):
                packet = handle.get_message()
                if packet is None:
                    continue
                got = True
                message, _delta = packet
                if not message:
                    continue
                category = classify_note_message(message)
                summary[str(index)][category] += 1
                at = round(clock() - start, 3)
                print(f"{at:8.3f}s port={index} {name}: {describe_midi(message)}")
                if len(events) < 5000:
                    events.append({"seconds": at, "port_index": index,
                                   "message": list(message), "type": category})
                else:
                    dropped += 1
            if not got:
                pause(0.005)
    except KeyboardInterrupt:
        print("Stopped")
    finally:
        for handle in handles:
            handle.close_port()
    report = {"device": "MPK225", "mode": "passive multi-input",
              "match": match, "seconds_requested": seconds,
              "ports": summary, "events": events,
              "events_omitted_after_5000": dropped,
              "outputs_opened": 0, "device_modified": False}
    print("Per-port note-on/note-off counts:")
    for index, s in summary.items():
        print(f"  {index}: {s['name']}: ON {s['note_on']} / "
              f"OFF {s['note_off']} / other {s['other']}")
    if output is not None:
        output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(f"Sanitized passive MIDI audit saved to {output}")
    return 0


def monitor(port_index: int | None, seconds: int) -> int:
    try:
        midi_in, ports = available_input_ports()
    except RuntimeError as exc:
        print(exc, file=sys.stderr)
        return 2
    matches = [i for i, name in enumerate(ports) if "MPK225" in name.upper()]
    if port_index is None:
        if len(matches) != 1:
            print("Select a MIDI input explicitly using --port INDEX. Available inputs:", file=sys.stderr)
            for i, name in enumerate(ports):
                print(f"  {i}: {name}", file=sys.stderr)
            return 2
        port_index = matches[0]
    if port_index < 0 or port_index >= len(ports):
        print("Invalid MIDI input port index", file=sys.stderr)
        return 2
    try:
        midi_in.open_port(port_index)
        # Suppress SysEx, MIDI timing messages and active sensing. No MIDI output is opened.
        midi_in.ignore_types(sysex=True, timing=True, active_sense=True)
        print(f"Listening to {ports[port_index]} for {seconds} seconds (read-only; press Ctrl+C to stop).")
        start = time.monotonic()
        while time.monotonic() - start < seconds:
            packet = midi_in.get_message()
            if packet is not None:
                message, _delta = packet
                print(f"{time.monotonic() - start:8.3f}s {describe_midi(message)}")
            else:
                time.sleep(0.005)
    except KeyboardInterrupt:
        print("Stopped")
    finally:
        midi_in.close_port()
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    pi = sub.add_parser("inventory", help="Read sanitized Windows PnP metadata; no USB commands")
    pi.add_argument("--vid-pid", default=None, help="Optional HEX VID:PID when device name is generic")
    pi.add_argument("--json", type=Path, default=None, help="Save sanitized report (optional)")
    sub.add_parser("midi-ports", help="List MIDI input ports; optional python-rtmidi required")
    pm = sub.add_parser("monitor", help="Passively display received MIDI; no MIDI output")
    pm.add_argument("--port", type=int, default=None, help="Select an input MIDI port by index")
    pm.add_argument("--seconds", type=int, default=20)
    pa = sub.add_parser("monitor-all",
                        help="Observe all matching MIDI inputs simultaneously, never transmit")
    pa.add_argument("--seconds", type=int, default=30)
    pa.add_argument("--match", default="MPK225",
                    help="Case-insensitive input port name substring (default MPK225)")
    pa.add_argument("--json", type=Path, default=None,
                    help="Optional sanitized JSON audit, at most 5000 events")
    args = parser.parse_args(argv)
    if args.action == "inventory":
        return inventory(args.vid_pid, args.json)
    if args.action == "midi-ports":
        return midi_ports()
    if args.seconds < 1 or args.seconds > 600:
        parser.error("--seconds must be between 1 and 600")
    if args.action == "monitor-all":
        if not args.match.strip():
            parser.error("--match must include a nonblank input name")
        return monitor_all_inputs(args.seconds, args.match, args.json)
    return monitor(args.port, args.seconds)


if __name__ == "__main__":
    raise SystemExit(main())