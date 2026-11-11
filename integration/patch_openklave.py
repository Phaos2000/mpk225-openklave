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


BRIDGE = r'''static mpk225::EmbeddedSmartChordController OpenKlaveMidiBridge{};

// A USB reset invalidates every queued packet and held note on the old bus.
// The reset interrupt calls this while foreground MIDI servicing is paused.
void ResetMidiForUsbBus(){
  OpenKlaveMidiBridge.resetForUsbBus();
  resetEventQueue(GlobalEventStack); // includes pending count, not only indices
  gEventQueueNeedsPanic = 0; // the old connection can no longer sound notes
}

// For opt-in panel/LCD lab builds only. The inherited MPK249 LCD bus
// remains unverified on the MPK225. Never update on each note or USB ISR.
void UpdateSmartChordDisplay(){
#if MPK225_FACTORY_KEYBED && MPK225_DIN_TX_LAB
  // Opt-in, schematic-backed PA9 USART1. USB cable is deliberately supplied
  // by the lab build, not inferred from a port's display name.
  if (Mpk225NativeDin.ready()) {
    const uint32_t dinMask=__get_PRIMASK();
    __disable_irq();
    mpk225::UsbMidiOutEvent pending{};
    if (Mpk225NativeUsb.peekHostMidi(pending) &&
        Mpk225NativeDin.enqueue(pending,MPK225_DIN_USB_CABLE)) {
      mpk225::UsbMidiOutEvent accepted{};
      (void)Mpk225NativeUsb.popHostMidi(accepted);
    }
    __set_PRIMASK(dinMask);
    Mpk225NativeDin.serviceTx();
  }
#endif
#if MPK225_FACTORY_KEYBED
  // The USB reset/endpoint ISR can preempt this foreground PMA write.
  // Preserve the prior IRQ mask; MIDI IN already uses the same discipline.
  const uint32_t hidMask=__get_PRIMASK();
  __disable_irq();
  (void)Mpk225NativeUsb.submitHidIn();
  __set_PRIMASK(hidMask);
#endif
#if MPK225_CANDIDATE_PANEL_BUTTONS
  static bool first = true;
  if (!first && !OpenKlaveMidiBridge.panelDirty()) return;
  first = false;
  char line[17]{};
  mpk225::formatSmartChordStatus(line, OpenKlaveMidiBridge.tonic(),
      OpenKlaveMidiBridge.scale(), OpenKlaveMidiBridge.filter());
  clearScreen();
  writeString(line);
  OpenKlaveMidiBridge.clearPanelDirty();
#endif
}

// Main-loop-owned MIDI transmission. The USB interrupt handler acknowledges
// successful EP1 transfers ONLY; it never consumes MIDI or writes PMA.
// Queue producer(s) run in IRQ context, so the ring is read/changed with
// interrupts masked. Full USB/ISR behavior remains UNVERIFIED on MPK225.
void ConsumeNoteEventStackAndTransmitNotesOverUSB(){
  const uint32_t queueMask = __get_PRIMASK();
  __disable_irq();
  if (takeEventQueuePanic()) {
    resetEventQueue(GlobalEventStack);
    // Clear stale MIDI ownership and enqueue CC120/CC123 before accepting
    // more events. The transport will send these when the host resumes.
    OpenKlaveMidiBridge.panicForEventLoss();
  }
  for (int i = 0; i < 8 && !IsEventStackEmpty(GlobalEventStack); ++i) {
    Event e = PeekTopEvent(GlobalEventStack, Event{0,0,0,0});
    if (!OpenKlaveMidiBridge.consume(mpk225::LegacyEvent{e.A,e.B,e.C,e.D}))
        break; // FIFO full: keep this exact event pending for retry
    PopEvent(GlobalEventStack);
  }
  __set_PRIMASK(queueMask);

  if (OpenKlaveMidiBridge.queuedMessages() == 0) return;

#if MPK225_FACTORY_KEYBED
  // Factory MPK225: bulk IN EP2, 64 bytes, 16 USB-MIDI event packets.
  // Check endpoint readiness BEFORE removing notes from the queue.
  const uint32_t bulkMask=__get_PRIMASK();
  __disable_irq();
  if (!Mpk225NativeUsb.readyForMidiIn()) {
    __set_PRIMASK(bulkMask);
    return;
  }
  const mpk225::UsbBulkFrame bulk=OpenKlaveMidiBridge.takeUsbBulkFrame();
  if (bulk.length>0)
    (void)Mpk225NativeUsb.submitMidiIn(bulk.bytes,bulk.length);
  __set_PRIMASK(bulkMask);
  return;
#else
  // Protect EP1 status, packet buffer and arming from a concurrent USB reset
  // or transfer-complete interrupt. This critical section is short (<=8 B).
  const uint32_t usbMask = __get_PRIMASK();
  __disable_irq();
  volatile uint32_t * const ep1 = (volatile uint32_t *)0x40005c04;
  if (!mpk225::usbShouldSubmitFrame(*ep1,
        static_cast<unsigned>(OpenKlaveMidiBridge.queuedMessages()))) {
    __set_PRIMASK(usbMask);
    return;
  }
  const mpk225::UsbFrame frame = OpenKlaveMidiBridge.takeUsbFrame();
  if (frame.length == 0) {
    __set_PRIMASK(usbMask);
    return;
  }

  volatile uint32_t * const pma = (volatile uint32_t *)0x40006060;
  for (uint32_t i = 0; i < frame.length / 2; ++i) {
    pma[i] = static_cast<uint32_t>(frame.bytes[2*i]) |
             (static_cast<uint32_t>(frame.bytes[2*i+1]) << 8);
  }
  volatile btable * const b = (volatile btable *)0x40006010;
  b->count_tx = frame.length;
  USBEndpointState state = USBEndpointState{*ep1,0,0,0,NAK,VALID,INTERRUPT,1};
  *ep1 = setEndpointState(state);
  __set_PRIMASK(usbMask);
#endif
}

'''


def patch_kernel(src: str) -> str:
    src = one_replace(src, '#include "kernel.h"', '#include "kernel.h"\n#include "MpkMidiPipeline.h"\n#include "KeybedReferenceDecoder.h"\n#include "Mpk225FactoryKeybedDecoder.h"\n#include "KeybedContactVelocity.h"\n#include "EmbeddedSmartChordController.h"\n#include "Stm32UsbTxGate.h"\n#include "SmartChordDisplay.h"\n#include "Mpk225UsbHardware.h"\n#include "Mpk225DinMidiTx.h"\n#if MPK225_FACTORY_KEYBED\nstatic mpk225::Mpk225UsbHardware Mpk225NativeUsb{};\n#if MPK225_DIN_TX_LAB\nstatic_assert(MPK225_DIN_USB_CABLE < 4, "Explicit USB DIN cable required");\nstatic mpk225::Mpk225DinUart1 Mpk225NativeDin{};\n#endif\n#endif\nvoid ResetMidiForUsbBus();\nextern "C" void _init(void) {}  // newlib init hook; startup uses __libc_init_array', 'include')
    src = block_replace(src, 'struct Event{', '\nvoid clearScreen(){',
        '#include "OpenKlaveEventQueue.h"\n\n', 'event FIFO')
    src = block_replace(src, 'void ConsumeNoteEventStackAndTransmitNotesOverUSB(){',
        '\nvoid usb(){', BRIDGE, 'MIDI->USB bridge')
    # EP1 CTR completions must be acknowledged before subsequent PMA rearm.
    # The original handler called the MIDI service only from a USB IRQ;
    # following idle/NAK, newly pressed notes could never kick off IN traffic.
    src = block_replace(src, '  if ((USB->ISTR &0b1111) == 1 ){',
                        '\n  // Check if I need to reset',
                        '''  if ((USB->ISTR & 0x800Fu) == 0x8001u) {
    volatile uint32_t * const ep1 = (volatile uint32_t *)0x40005c04;
    const uint32_t old = *ep1;
    if (old & (mpk225::kUsbCtrTx | mpk225::kUsbCtrRx))
        *ep1 = mpk225::usbEpCtrAcknowledgeWrite(old);
    // Main loop will queue and arm pending notes, not this USB ISR.
    return;
  }
''',
                        'ack EP1 transfer, no MIDI consumption inside USB ISR')
    # A separate full-speed composite controller replaces MPK249 USB on
    # the MPK225 build only, using the user's real descriptor map.
    src = one_replace(src, 'void usbReset(void){',
        'void usbReset(void){\n'
        '#if MPK225_FACTORY_KEYBED\n'
        '    ResetMidiForUsbBus();\n'
        '    GPIOD->CRL = 0x88844244; // inherited USB pull-up GPIO, unverified\n'
        '    GPIOD->BSRR = 4;\n'
        '    Mpk225NativeUsb.reset();\n'
        '    return;\n'
        '#endif',
        'native MPK225 bus reset')
    # USB reset must leave the MIDI IN endpoint idle/NAK with no phantom
    # four-byte packet. Main loop arms it only when a real MIDI frame exists.
    a = src.index('void usbReset(void){')
    b = src.index('\nvoid writeNumberToScreen(', a)
    reset = src[a:b]
    reset = one_replace(reset, '    amIInASetAddressTransaction = 0;',
                        '    amIInASetAddressTransaction = 0;\n'
                        '    ResetMidiForUsbBus();',
                        'drop stale MIDI state on bus reset')
    reset = one_replace(reset, 'NAK, VALID, INTERRUPT, 1',
                        'NAK, NAK, INTERRUPT, 1',
                        'EP1 TX NAK on USB reset')
    reset = one_replace(reset, 'b->count_tx= 4;', 'b->count_tx= 0;',
                        'EP1 TX empty on USB reset')
    src = src[:a] + reset + src[b:]
    # The MPK225 must not answer the inherited MPK249 MIDI-only
    # descriptor requests. Its EP0/EP1/EP2/EP3 interrupts are driven by
    # Mpk225UsbHardware on the FACTORY_SCAN=1 build.
    src = one_replace(src, 'void usb(){',
        'void usb(){\n'
        '#if MPK225_FACTORY_KEYBED\n'
        '    static uint32_t lastUsbConfigurationEpoch = 0;\n'
        '    Mpk225NativeUsb.irq();\n'
        '    const uint32_t epoch=Mpk225NativeUsb.configurationEpoch();\n'
        '    if (epoch!=lastUsbConfigurationEpoch) {\n'
        '        lastUsbConfigurationEpoch=epoch;\n'
        '        ResetMidiForUsbBus();\n'
        '    }\n'
        '    return;\n'
        '#else',
        'replace MPK225 USB IRQ service')
    usb_start=src.index('void usb(){')
    usb_end=src.index('\n}',usb_start)
    src=src[:usb_end]+'\n#endif'+src[usb_end:]
    # Akai official v1.02 shared image has EXACTLY 16 bytes at 0x08033800:
    # 41 44 34 41 5F 52 69 63 68 61 72 64 00 01 00 02
    # OpenKlave's upstream token omitted the separator byte (15 bytes).
    # This is an image-integrity correction, NOT proof of boot acceptance:
    # the separate 0x08033FFC footer and bootloader checks remain unknown.
    src = one_replace(src,
                      "{'A','D','4','A','_','R','i','c','h','a','r','d', 0x1 , 0x0 , 0x2}",
                      "{'A','D','4','A','_','R','i','c','h','a','r','d', 0x0, 0x1, 0x0, 0x2}",
                      'match official Akai 16-byte boot signature')
    # OpenKlave's device descriptor is declared in an unusual pre-swap
    # order; NewDeviceConfiguration swaps USB multi-byte fields into little
    # endian PMA bytes. 0x00,0x23 -> 0x23,0x00 -> USB PID 0x0023.
    # Only the MPK225 factory-scan lab build identifies as MPK225.
    src = one_replace(src, '0x00, 0x24, //idProduct',
                      '#if MPK225_FACTORY_KEYBED\n'
                      '    0x00, 0x23, //model-1 MPK225 PID (PMA permutation -> 23 00)\n'
                      '#else\n'
                      '    0x00, 0x24, //MPK249 reference PID (PMA permutation -> 24 00)\n'
                      '#endif',
                      'model-specific USB device PID')
    # The declaration above uses Akai-era pre-permuted multi-byte fields.
    # Validate the bytes that actually reach USB EP0 after PMA packing.
    src = one_replace(src, '// Make a ConfigurationDescriptor into new PMA Allocation.',
        '''#if MPK225_FACTORY_KEYBED
static_assert((NewDeviceConfiguration(dev_descriptor).a[5] & 0xFFFFu) == 0x0023u,
              "MPK225 USB PID must enumerate as 0x0023");
#else
static_assert((NewDeviceConfiguration(dev_descriptor).a[5] & 0xFFFFu) == 0x0024u,
              "MPK249 reference USB PID must enumerate as 0x0024");
#endif
static_assert((NewDeviceConfiguration(dev_descriptor).a[4] & 0xFFFFu) == 0x09E8u,
              "USB vendor ID must enumerate as Akai 0x09E8");

// Make a ConfigurationDescriptor into new PMA Allocation.''',
        'compiled USB identity after PMA permutation')
    # Do NOT reuse OpenKlave's unchecked raw scan lookup. Preserve only its
    # published MPK249 mapping, explicitly rejecting unmapped codes and unknown
    # event types; this is NOT MPK225 keybed validation.
    src = block_replace(src, 'unsigned int decodeMPK249NoteValue(uint32_t u){',
                        '\nvoid CheckKeyBed(){',
                        '// MPK249 reference scan ranges now in KeybedReferenceDecoder.h\n',
                        'remove unsafe MPK249 scan table')
    src = one_replace(src, '  uint32_t val = (decodeMPK249NoteValue(event));',
                      '#if MPK225_FACTORY_KEYBED\n'
                      '  const auto decoded = mpk225::decodeMpk225FactoryScan(event);\n'
                      '#else\n'
                      '  const auto decoded = mpk225::decodeMpk249ReferenceScan(event);\n'
                      '#endif\n'
                      '  if (!decoded.valid) return;\n'
                      '  const uint32_t val = decoded.midiNote;',
                      'guard invalid key scan and type')
    src = one_replace(src, 'if ((event & 0x000000FF) == 0xFE){',
                      'if (!decoded.down){',
                      'use validated action in keybed handler')
    # Factory parser packs two timing bytes above the key scan/status bytes.
    # Preserve them in MPK225 mode; use a bounded, calibrated-later response
    # instead of OpenKlave's hard-coded velocity 100. Release velocity stays 0.
    # MPK249 reference mode is intentionally left unchanged.
    src = one_replace(src, '  uint32_t velocity = 100;',
                      '#if MPK225_FACTORY_KEYBED\n'
                      '  const uint32_t velocity = mpk225::contactVelocityFromWordForNote(event, static_cast<uint8_t>(val));\n'
                      '#else\n'
                      '  const uint32_t velocity = 100;\n'
                      '#endif',
                      'use contact timing for MPK225 candidate')
    src = one_replace(src, 'Event e = {0xFF,100,val,0};',
                      'Event e = {0xFF,velocity,val,0};',
                      'forward velocity to key press')
    # OpenKlave keeps a pointer to these descriptors between USB IRQ calls.
    # Function-local constexpr objects have automatic storage duration;
    # make them static to prevent a dangling descriptor pointer.
    for name in ('deviceDescriptor', 'mouseTry', 'mouseHID'):
        src = one_replace(src, 'constexpr PMAAllocation ' + name + ' =',
                          'static constexpr PMAAllocation ' + name + ' =',
                          'USB descriptor lifetime: ' + name)
    src = one_replace(src, 'constexpr PMAAllocation USBMidi  =',
                      'static constexpr PMAAllocation USBMidi  =',
                      'USB descriptor lifetime: USBMidi')
    # Model-1 opt-in DIN output is initialized only after system clocks,
    # keypad USART3 and AFIO are ready. It must never touch production
    # USB/serial hardware in the default firmware candidate.
    src = one_replace(src, '    USARTSetup();',
        '    USARTSetup();\n'
        '#if MPK225_FACTORY_KEYBED && MPK225_DIN_TX_LAB\n'
        '    (void)Mpk225NativeDin.init();\n'
        '#endif',
        'opt-in schematic-backed USART1 DIN TX initialization')
    src = one_replace(src, 'GlobalEventStack->capacity = 100;',
        'GlobalEventStack->capacity = 100;\n    GlobalEventStack->scratch_2 = 0;\n    GlobalEventStack->scratch_3 = 0;',
        'FIFO head/tail init')
    # Upstream kernel.start() contains a terminal infinite loop, so main()
    # never receives control. The foreground USB MIDI service installed in
    # main.c would otherwise be DEAD CODE despite a successfully linked ELF.
    src = one_replace(src, '    while(1){\n\n    };\n\n}\n\n#endif',
                      '    return 0;\n\n}\n\n#endif',
                      'allow firmware initialization to reach main MIDI loop')
    # Conservative firmware default: raw keyboard passthrough.
    # No unverified MPK225 button is assigned to enable Chord Mode.
    return src



def patch_main(src: str) -> str:
    # A key arriving after EP1 becomes NAK cannot cause a new USB interrupt.
    # Polling in the already-empty foreground loop allows the first note
    # to initiate an IN packet without waiting for a previous transfer.
    src = one_replace(src, '#include "kernel.h"',
                      '#include "kernel.h"\n'
                      'void ConsumeNoteEventStackAndTransmitNotesOverUSB();\n'
                      'void UpdateSmartChordDisplay();',
                      'foreground USB MIDI service declaration')
    src = one_replace(src, '  while(1){\n\n  }',
                      '  while(1){\n'
                      '    ConsumeNoteEventStackAndTransmitNotesOverUSB();\n'
                      '    UpdateSmartChordDisplay();\n'
                      '  }',
                      'start first USB IN transfer from main loop')
    return src


def patch_linker(src: str) -> str:
    src = one_replace(src, 'ORIGIN = 0x08006000, LENGTH = 256k',
        'ORIGIN = 0x08006000, LENGTH = 0x2E000', '184-KiB application-only flash boundary')
    # The original MPK249 linker places the stack at 0x2000BB00, only
    # 0x4B0 (1200) bytes above our 1616-byte event queue at 0x2000B000.
    # STM32F103VC provides 48 KiB SRAM ending at 0x2000C000; using its
    # actual end gives at least 2 KiB of stack below this fixed queue.
    src = one_replace(src, '_estack = 0x2000BB00;',
        '_estack = 0x2000C000;', 'full 48-KiB SRAM stack top')
    return src


def patch_makefile(src: str) -> str:
    src = one_replace(src, 'all: $(PROJECT).elf MakeAkaiSysex test.elf',
        'all: $(PROJECT).elf test.elf', 'disable automatic SysEx packaging')
    # Default remains passthrough. Test-build chord-on-boot only when
    # CHORD_ON_BOOT=1 is explicitly passed to make; never a release setting.
    src = one_replace(src, 'CFLAGS += -DSTM32F10X_MD -DUSE_STDPERIPH_DRIVER',
                      'CFLAGS += -DSTM32F10X_MD -DUSE_STDPERIPH_DRIVER\n'
                      'CHORD_ON_BOOT ?= 0\n'
                      'FACTORY_SCAN ?= 0\n'
                      'CFLAGS += -DMPK225_FACTORY_KEYBED=$(FACTORY_SCAN)\n'
                      'CFLAGS += -DMPK225_CANDIDATE_CHORD_ON_BOOT=$(CHORD_ON_BOOT)',
                      'select test-only startup chord mode')
    # Smart harmony and MIDI-note inversion are dormant by default.
    # A distinct lab-only ELF may enable this via SMART_ON_BOOT=1.
    src = one_replace(src, 'FACTORY_SCAN ?= 0\n',
                      'FACTORY_SCAN ?= 0\n'
                      'SMART_ON_BOOT ?= 0\n'
                      'CFLAGS += -DMPK225_CANDIDATE_SMART_ON_BOOT=$(SMART_ON_BOOT)\n',
                      'select smart harmony firmware build')
    # Unverified MPK249 select/knob button identifiers are opt-in lab only.
    src = one_replace(src, 'SMART_ON_BOOT ?= 0\n',
                      'SMART_ON_BOOT ?= 0\n'
                      'PANEL_BUTTONS ?= 0\n'
                      'CFLAGS += -DMPK225_CANDIDATE_PANEL_BUTTONS=$(PANEL_BUTTONS)\n',
                      'select experimental physical panel control binding')
    src = one_replace(src, 'PANEL_BUTTONS ?= 0\n',
        'PANEL_BUTTONS ?= 0\n'
        'DIN_TX_LAB ?= 0\n'
        'DIN_USB_CABLE ?= 255\n'
        'CFLAGS += -DMPK225_DIN_TX_LAB=$(DIN_TX_LAB)\n'
        'CFLAGS += -DMPK225_DIN_USB_CABLE=$(DIN_USB_CABLE)\n',
        'explicit opt-in physical DIN TX cable')
    # Use the freestanding Cortex-M3 ABI without libstdc++: the candidate
    # makes no exception, RTTI, thread-safe-static, or C++ library calls.
    # Explicit C runtime link avoids multi-gigabyte target C++ package.
    src = one_replace(src, 'CFLAGS += -I.', 
        'CFLAGS += -nostartfiles -nodefaultlibs -fno-exceptions -fno-rtti -fno-threadsafe-statics\nCFLAGS += -I.',
        'freestanding link options')
    src = one_replace(src, '$(CC) $(CFLAGS) $^ -o $@',
        '$(CC) $(CFLAGS) $^ -Wl,--start-group -lc -lgcc -lnosys -Wl,--end-group -o $@',
        'ARM C runtime link')
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
    if not all((base / 'src' / x).is_file() for x in ('kernel.c', 'main.c', 'Makefile')):
        ap.error('expected OpenKlave checkout with src/kernel.c and src/Makefile')
    if out.exists() or out == base or base in out.parents or out in base.parents:
        ap.error('output must be a NEW, separate directory, not inside upstream')
    includes = Path(__file__).resolve().parent.parent / 'include'
    if not (includes / 'ChordEngine.h').exists():
        ap.error('matching source package missing')
    # Prepare every change in memory before writing anything.
    files = {
        'src/kernel.c': patch_kernel((base / 'src/kernel.c').read_text()),
        'src/main.c': patch_main((base / 'src/main.c').read_text()),
        'src/stm32_flash.ld': patch_linker((base / 'src/stm32_flash.ld').read_text()),
        'src/Makefile': patch_makefile((base / 'src/Makefile').read_text()),
    }
    shutil.copytree(base, out)
    for name, contents in files.items():
        (out / name).write_text(contents)
    for name in ('ChordEngine.h', 'MpkMidiPipeline.h', 'Mpk225UsbMidiCables.h', 'OpenKlaveEventQueue.h',
                 'SmartHarmony.h', 'VoicingPlanner.h', 'KeybedReferenceDecoder.h',
                 'Mpk225FactoryKeybedDecoder.h', 'KeybedContactVelocity.h',
                 'EmbeddedSmartChordController.h', 'Stm32UsbTxGate.h',
                 'SmartChordDisplay.h', 'Mpk225UsbDescriptors.h',
                 'Mpk225HidKeyboard.h', 'Mpk225UsbControl.h',
                 'Mpk225UsbDeviceState.h', 'Mpk225HostMidiQueue.h', 'Mpk225UsbHardware.h',
                 'Mpk225DinMidiTx.h'):
        shutil.copy2(includes / name, out / 'src' / name)
    print('Created candidate integration tree:', out)
    print('IMPORTANT: MPK225 timing/velocity curve and USB IRQ still need hardware validation; recovery NOT verified.')
    print('Flashing and SysEx build targets intentionally disabled. No device access performed.')
    return 0


if __name__ == '__main__':
    sys.exit(main())