import tempfile
from pathlib import Path
from subprocess import run
import sys

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='mpk225_patch_test_') as d:
    base=Path(d)/'upstream';(base/'src').mkdir(parents=True)
    (base/'src/kernel.c').write_text('''#include "kernel.h"
struct Event{\n};\n
void clearScreen(){\n}\n
void ConsumeNoteEventStackAndTransmitNotesOverUSB(){\n}
void usb(){\n}
constexpr PMAAllocation deviceDescriptor = fixture();
constexpr PMAAllocation mouseTry = fixture();
constexpr PMAAllocation mouseHID = fixture();
constexpr PMAAllocation USBMidi  = fixture();
GlobalEventStack->capacity = 100;
''')
    (base/'src/stm32_flash.ld').write_text('FLASH (rx): ORIGIN = 0x08006000, LENGTH = 256k')
    (base/'src/Makefile').write_text('''CFLAGS += -I.
led.elf:
	$(CC) $(CFLAGS) $^ -o $@
all: $(PROJECT).elf MakeAkaiSysex test.elf
.PHONY: MakeAkaiSysex
MakeAkaiSysex:
	echo syx
# remove binary files
clean:
	echo clean
# flash
burn:
	echo burn
''')
    out=Path(d)/'candidate'
    proc=run([sys.executable,str(ROOT/'integration/patch_openklave.py'), '--upstream',str(base),'--output',str(out)],capture_output=True,text=True)
    assert proc.returncode==0, proc.stderr
    k=(out/'src/kernel.c').read_text()
    mk=(out/'src/Makefile').read_text()
    assert 'OpenKlaveEventQueue.h' in k
    assert 'MpkMidiPipeline.h' in k
    assert (out/'src/SmartHarmony.h').is_file()
    assert (out/'src/VoicingPlanner.h').is_file()
    assert 'OpenKlaveMidiBridge.consume' in k
    for n in ('deviceDescriptor', 'mouseTry', 'mouseHID', 'USBMidi'):
        assert 'static constexpr PMAAllocation ' + n in k
    assert '-nostartfiles -nodefaultlibs' in mk
    assert '-Wl,--start-group -lc -lgcc -lnosys -Wl,--end-group' in mk
    assert 'GlobalEventStack->scratch_3 = 0;' in k
    assert 'MakeAkaiSysex test.elf' not in mk
    assert 'Disabled: MPK225 recovery not verified' in mk
    ld = (out/'src/stm32_flash.ld').read_text()
    assert 'ORIGIN = 0x08006000, LENGTH = 0x2E000' in ld
    assert '0x3A000' not in ld, 'Cannot include persistent preset region'
    assert 0x08006000 + 0x2E000 == 0x08034000
    assert 0x08033800 < 0x08034000, 'Boot signature must remain in app partition'
    assert 'struct Event{' in (base/'src/kernel.c').read_text()
    assert not (base/'src'/'ChordEngine.h').exists()
    proc2=run([sys.executable,str(ROOT/'integration/patch_openklave.py'), '--upstream',str(base),'--output',str(out)],capture_output=True,text=True)
    assert proc2.returncode != 0, 'Must refuse overwriting existing output'
    print('PASS: guarded patch fixture, source unchanged, no overwrite, flashing disabled')