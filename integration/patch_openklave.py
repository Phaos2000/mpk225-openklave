#!/usr/bin/env python3
"""Create a non-flashable *candidate* OpenKlave MIDI integration tree.

Usage: python integration/patch_openklave.py --upstream PATH --output NEW_PATH

This tool DOES NOT flash, connect to, probe or send SysEx to hardware. It
requires an independently obtained checkout of sillsm/openklave at a source
revision compatible with the documented October 2021 baseline. It produces a
separate copy, leaving the upstream tree unmodified. No source-level change is
claimed to be validated on the MPK225, or to be a buildable ARM binary.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import sys


def one_replace(text: str, old: str, new: str, label: str) -> str:
    if text.count(old) != 1:
        raise ValueError(f"{label}: expected exactly one upstream occurrence; got {text.count(old)}")
    return text.replace(old, new, 1)


def block_replace(text: str, before: str, after: str, new: str, label: str) -> str:
    if text.count(before) != 1 or text.count(after) != 1:
        raise ValueError(f"{label}: incompatible upstream anchors")
    start = text.index(before)
    end = text.index(after, start)
    if end < start:
        raise ValueError(f"{label}: unexpected source layout")
    return text[:start] + new + text[end:]


BRIDGE = r'''static mpk225::MpkMidiPipeline OpenKlaveMidiBridge{};

// Candidate integration of the legacy MPK249 event scheme. Until the MPK225
// keybed and USB endpoint have been verified, DO NOT flash this image.
// The event-to-MIDI conversion and 8-byte packetization are host-tested;
// endpoint handling, DMA synchronization and MCU timing are NOT.
void ConsumeNoteEventStackAndTransmitNotesOverUSB(){
  // FIFO queue preserves order; if pipeline cannot accept an event, leave
  // the legacy event pending (do not mutate chord state or drop a note-off).
  for (int i = 0; i < 8 && !IsEventStackEmpty(GlobalEventStack); ++i) {
    Event e = PeekTopEvent(GlobalEventStack, Event{0,0,0,0});
    if (!OpenKlaveMidiBridge.consume(mpk225::LegacyEvent{e.A,e.B,e.C,e.D}))
        break;
    PopEvent(GlobalEventStack);
  }

  const mpk225::UsbFrame frame = OpenKlaveMidiBridge.takeUsbFrame();
  uint32_t * USB_EP1R = (uint32_t *)0x40005c04;
  USBEndpointState state = USBEndpointState{*USB_EP1R,0,0,0,NAK,VALID,INTERRUPT,1};
  uint32_t * PMAWrite = (uint32_t *)0x40006060;
  // STM32F103 PMA: each 16-bit word occupies one 32-bit mapped address.
  for (uint32_t i=0; i<frame.length / 2; ++i) {
    PMAWrite[i] = static_cast<uint32_t>(frame.bytes[2*i]) |
                    (static_cast<uint32_t>(frame.bytes[2*i+1]) << 8);
  }
  btable * b = (btable *)0x40006010;
  b->count_tx = frame.length;
  USB->ISTR = 0;
  SetRegister(USB_EP1R, setEndpointState(state));
}

'''


def patch_kernel(src: str) -> str:
    src = one_replace(src, '#include "kernel.h"', '#include "kernel.h"\n#include "MpkMidiPipeline.h"', 'include')
    src = block_replace(src, 'struct Event{', '\nvoid clearScreen(){',
        '#include "OpenKlaveEventQueue.h"\n\n', 'event FIFO')
    src = block_replace(src, 'void ConsumeNoteEventStackAndTransmitNotesOverUSB(){',
        '\nvoid usb(){', BRIDGE, 'MIDI->USB bridge')
    src = one_replace(src, 'GlobalEventStack->capacity = 100;',
        'GlobalEventStack->capacity = 100;\n    GlobalEventStack->scratch_2 = 0;\n    GlobalEventStack->scratch_3 = 0;',
        'FIFO head/tail init')
    # Conservative firmware default: raw keyboard passthrough.
    # No unverified MPK225 button is assigned to enable Chord Mode.
    return src


def patch_linker(src: str) -> str:
    return one_replace(src, 'ORIGIN = 0x08006000, LENGTH = 256k',
        'ORIGIN = 0x08006000, LENGTH = 0x2E000', '184-KiB application-only flash boundary')


def patch_makefile(src: str) -> str:
    src = one_replace(src, 'all: $(PROJECT).elf MakeAkaiSysex test.elf',
        'all: $(PROJECT).elf test.elf', 'disable automatic SysEx packaging')
    # Keep non-hardware elf build; disallow dangerous targets in generated tree.
    src = block_replace(src, '.PHONY: MakeAkaiSysex', '\n# remove binary files',
        '.PHONY: MakeAkaiSysex\nMakeAkaiSysex:\n\t@echo "Disabled: MPK225 recovery not verified" && exit 1\n',
        'disable SysEx target')
    guard = '\n# flash\nburn:'
    if src.count(guard) != 1:
        raise ValueError('burn: upstream anchor missing or duplicated')
    # Upstream burn target is last in file. Drop its destructive recipe.
    src = src.split(guard, 1)[0]
    src += ('\n# Device flashing is intentionally disabled in this candidate tree.\n'
            'burn:\n\t@echo "Disabled: MPK225 recovery not verified" && exit 1\n')
    return src


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--upstream', required=True, type=Path)
    ap.add_argument('--output', required=True, type=Path)
    args = ap.parse_args()
    base = args.upstream.resolve()
    out = args.output.resolve()
    if not (base / 'src/kernel.c').is_file() or not (base / 'src/Makefile').is_file():
        ap.error('expected OpenKlave checkout with src/kernel.c and src/Makefile')
    if out.exists() or out == base or base in out.parents or out in base.parents:
        ap.error('output must be a NEW, separate directory, not inside upstream')
    includes = Path(__file__).resolve().parent.parent / 'include'
    if not (includes / 'ChordEngine.h').exists():
        ap.error('matching source package missing')
    # Prepare every change in memory before writing anything.
    files = {
        'src/kernel.c': patch_kernel((base / 'src/kernel.c').read_text()),
        'src/stm32_flash.ld': patch_linker((base / 'src/stm32_flash.ld').read_text()),
        'src/Makefile': patch_makefile((base / 'src/Makefile').read_text()),
    }
    shutil.copytree(base, out)
    for name, contents in files.items():
        (out / name).write_text(contents)
    for name in ('ChordEngine.h', 'MpkMidiPipeline.h', 'OpenKlaveEventQueue.h',
                 'SmartHarmony.h', 'VoicingPlanner.h'):
        shutil.copy2(includes / name, out / 'src' / name)
    print('Created candidate integration tree:', out)
    print('IMPORTANT: MPK249 keymap, fixed key velocity, USB IRQ, and MPK225 recovery NOT verified.')
    print('Flashing and SysEx build targets intentionally disabled. No device access performed.')
    return 0


if __name__ == '__main__':
    sys.exit(main())