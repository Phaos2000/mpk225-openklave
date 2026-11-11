#include "MpkMidiPipeline.h"
#include "Mpk225UsbHardware.h"
#include "EmbeddedSmartChordController.h"
#include <cassert>
#include <cstdio>
using namespace mpk225;

int main() {
    uint8_t packet[4]{};
    static_assert(kMpk225UsbCableCount==4,"Measured 4-cable MIDI descriptors");
    for(uint8_t cable=0;cable<kMpk225UsbCableCount;++cable) {
        MpkMidiPipeline midi;
        for(unsigned i=0;i<16;++i)
            assert(midi.consume({0xFF,99,48+i,0}));
        const auto f=midi.takeUsbBulkFrame(cable);
        assert(f.length==64 && midi.queuedMessages()==0);
        assert(usbMidiBulkHasKnownCables(f.bytes,f.length));
        for(unsigned off=0;off<64;off+=4) {
            assert(usbMidiCable(f.bytes[off])==cable);
            assert(usbMidiCin(f.bytes[off])==9);
            assert(f.bytes[off+1]==0x90);
            assert(f.bytes[off+2]==48+off/4);
            assert(f.bytes[off+3]==99);
        }
        // Mixed 4-port bulk transfers must be accepted as structurally valid.
        packet[0]=static_cast<uint8_t>((cable<<4)|0x0E);
        assert(usbMidiBulkHasKnownCables(packet,4));
    }
    MpkMidiPipeline protectedMidi;
    assert(protectedMidi.consume({0xFF,100,60,0}));
    const size_t retained=protectedMidi.queuedMessages();
    assert(protectedMidi.takeUsbBulkFrame(4).length==0);
    assert(protectedMidi.takeUsbFrame(255).length==0);
    assert(protectedMidi.queuedMessages()==retained);
    assert(!usbMidiPacket({0x90,60,100},packet,4));
    assert(protectedMidi.takeUsbBulkFrame().length==4);

    uint8_t mixed[16]{};
    for(unsigned c=0;c<4;++c) {
        mixed[c*4]=static_cast<uint8_t>((c<<4)|0x9);
        mixed[c*4+1]=0x90; mixed[c*4+2]=60; mixed[c*4+3]=100;
    }
    assert(usbMidiBulkHasKnownCables(mixed,16));
    mixed[8]=0x49; // cable 4 is not advertised
    assert(!usbMidiBulkHasKnownCables(mixed,16));
    mixed[8]=0x24; // system/SysEx CIN is not silently excluded
    assert(usbMidiBulkHasKnownCables(mixed,16));
    assert(!usbMidiBulkHasKnownCables(nullptr,4));
    assert(!usbMidiBulkHasKnownCables(mixed,3));
    assert(!usbMidiBulkHasKnownCables(mixed,0));
    assert(!usbMidiBulkHasKnownCables(mixed,65));

    UsbMidiOutEvent host{};
    host.data[0]=0x3B;
    assert(host.knownCable() && host.cable()==3 && host.cin()==11);
    host.data[0]=0x4B;
    assert(!host.knownCable());

    EmbeddedSmartChordController smart;
    assert(smart.consume({0xFF,97,60,0}));
    assert(smart.takeUsbBulkFrame(2).bytes[0]==0x29);
    std::puts("PASS: four descriptor-backed USB-MIDI cables, mixed bulk, invalid-cable rejection without losing queued notes");
}
