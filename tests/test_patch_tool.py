import tempfile
from pathlib import Path
from subprocess import run
import sys

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='mpk225_patch_test_') as d:
    base=Path(d)/'upstream';(base/'src').mkdir(parents=True)
    (base/'src/kernel.c').write_text('''#include "kernel.h"
char boot_secret[] __attribute__((section(".mySection"))) =
{'A','D','4','A','_','R','i','c','h','a','r','d', 0x1 , 0x0 , 0x2};
struct Event{\n};\n
void clearScreen(){\n}\n
void ConsumeNoteEventStackAndTransmitNotesOverUSB(){\n}
void usb(){
  if ((USB->ISTR &0b1111) == 1 ){
    ConsumeNoteEventStackAndTransmitNotesOverUSB();
    return;
  }
  // Check if I need to reset
}
void usbReset(void){
  USBEndpointState state = USBEndpointState{*USB_EP1R, 0, 0, 0, NAK, VALID, INTERRUPT, 1 };
  b->count_tx= 4;
}
void writeNumberToScreen(uint32_t u){}
unsigned int decodeMPK249NoteValue(uint32_t u){
  unsigned int map[] = {0,0,60};
  return map[(u&0x0000FF00)>>8];
}
void CheckKeyBed(){
  uint32_t event = 0;
  uint32_t val = (decodeMPK249NoteValue(event));
  uint32_t velocity = 100;
  Event e = {0xFF,100,val,0};
  if ((event & 0x000000FF) == 0xFE){
    consume(val);
  }
}
static constexpr uint8_t dev_descriptor[] = {
  0x09, 0xe8, //idVendor
  0x00, 0x24, //idProduct
};
constexpr PMAAllocation NewDeviceConfiguration(const uint8_t* src){ return {}; }
// Make a ConfigurationDescriptor into new PMA Allocation.
constexpr PMAAllocation deviceDescriptor = fixture();
constexpr PMAAllocation mouseTry = fixture();
constexpr PMAAllocation mouseHID = fixture();
constexpr PMAAllocation USBMidi  = fixture();
GlobalEventStack->capacity = 100;
''')
    (base/'src/main.c').write_text('''#include "kernel.h"

int main(void) {
  start();
  while(1){

  }
}
''')
    (base/'src/stm32_flash.ld').write_text('FLASH (rx): ORIGIN = 0x08006000, LENGTH = 256k')
    (base/'src/Makefile').write_text('''CFLAGS += -DSTM32F10X_MD -DUSE_STDPERIPH_DRIVER
CFLAGS += -I.
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
    assert "'A','D','4','A','_','R','i','c','h','a','r','d', 0x0, 0x1, 0x0, 0x2" in k
    assert "'A','D','4','A','_','R','i','c','h','a','r','d', 0x1 , 0x0 , 0x2" not in k
    assert 'MpkMidiPipeline.h' in k
    assert (out/'src/KeybedReferenceDecoder.h').is_file()
    assert (out/'src/Mpk225FactoryKeybedDecoder.h').is_file()
    assert '#if MPK225_FACTORY_KEYBED' in k
    assert 'decodeMpk225FactoryScan(event)' in k
    assert (out/'src/KeybedContactVelocity.h').is_file()
    assert (out/'src/EmbeddedSmartChordController.h').is_file()
    assert (out/'src/Stm32UsbTxGate.h').is_file()
    # A previously idle USB endpoint must be kicked from the main loop,
    # not require a prior USB transfer. The USB interrupt only acknowledges.
    main=(out/'src/main.c').read_text()
    assert 'ConsumeNoteEventStackAndTransmitNotesOverUSB();' in main
    assert 'if ((USB->ISTR & 0x800Fu) == 0x8001u)' in k
    usb_handler=k[k.index('void usb(){'):k.index('// Check if I need to reset')]
    assert 'ConsumeNoteEventStackAndTransmitNotesOverUSB();' not in usb_handler
    assert 'usbEpCtrAcknowledgeWrite(old)' in usb_handler
    assert 'void ResetMidiForUsbBus(){' in k
    assert 'OpenKlaveMidiBridge.resetForUsbBus();' in k
    reset_section=k[k.index('void usbReset(void){'):k.index('void writeNumberToScreen')]
    assert 'ResetMidiForUsbBus();' in reset_section
    assert 'GlobalEventStack->scratch_2 = 0;' in k
    assert 'if (!mpk225::usbShouldSubmitFrame(*ep1,' in k
    assert k.index('if (!mpk225::usbShouldSubmitFrame(*ep1,') < k.index('OpenKlaveMidiBridge.takeUsbFrame()')
    assert 'USB->ISTR = 0' not in k[k.index('void ConsumeNoteEventStackAndTransmitNotesOverUSB(){'):k.index('void usb(){')]
    assert 'b->count_tx = frame.length;' in k
    assert 'NAK, NAK, INTERRUPT, 1' in k
    assert 'b->count_tx= 0;' in k
    assert '__disable_irq()' in k and '__set_PRIMASK' in k
    assert 'static mpk225::EmbeddedSmartChordController OpenKlaveMidiBridge{};' in k
    assert '#if MPK225_FACTORY_KEYBED' in k
    assert '0x00, 0x23, //model-1 MPK225 PID' in k
    assert '0x00, 0x24, //MPK249 reference PID' in k
    assert '(NewDeviceConfiguration(dev_descriptor).a[5] & 0xFFFFu) == 0x0023u' in k
    assert '(NewDeviceConfiguration(dev_descriptor).a[5] & 0xFFFFu) == 0x0024u' in k
    assert '(NewDeviceConfiguration(dev_descriptor).a[4] & 0xFFFFu) == 0x09E8u' in k
    assert 'mpk225::contactVelocityFromWordForNote(event, static_cast<uint8_t>(val))' in k
    assert '#if MPK225_FACTORY_KEYBED' in k
    assert 'Event e = {0xFF,velocity,val,0};' in k
    assert 'Event e = {0xFF,100,val,0};' not in k
    assert 'e = {0xFE,0,val,0}' in k or 'consume(val)' in k
    assert 'decodeMpk249ReferenceScan(event)' in k
    assert 'if (!decoded.valid) return;' in k
    assert 'if (!decoded.down)' in k
    assert 'unsigned int map[]' not in k, 'unsafe lookup table must be removed'
    assert (out/'src/SmartHarmony.h').is_file()
    assert (out/'src/VoicingPlanner.h').is_file()
    assert 'OpenKlaveMidiBridge.consume' in k
    assert 'extern "C" void _init(void) {}' in k
    for n in ('deviceDescriptor', 'mouseTry', 'mouseHID', 'USBMidi'):
        assert 'static constexpr PMAAllocation ' + n in k
    assert 'CHORD_ON_BOOT ?= 0' in mk
    assert 'FACTORY_SCAN ?= 0' in mk
    assert 'SMART_ON_BOOT ?= 0' in mk
    assert '-DMPK225_CANDIDATE_SMART_ON_BOOT=$(SMART_ON_BOOT)' in mk
    assert '-DMPK225_FACTORY_KEYBED=$(FACTORY_SCAN)' in mk
    assert '-DMPK225_CANDIDATE_CHORD_ON_BOOT=$(CHORD_ON_BOOT)' in mk
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
    assert 'ConsumeNoteEventStackAndTransmitNotesOverUSB();' not in (base/'src/main.c').read_text()
    proc2=run([sys.executable,str(ROOT/'integration/patch_openklave.py'), '--upstream',str(base),'--output',str(out)],capture_output=True,text=True)
    assert proc2.returncode != 0, 'Must refuse overwriting existing output'
    print('PASS: guarded patch fixture, source unchanged, no overwrite, flashing disabled')