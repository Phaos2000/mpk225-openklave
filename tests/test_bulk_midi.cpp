#include "MpkMidiPipeline.h"
#include "EmbeddedSmartChordController.h"
#include <cassert>
#include <cstdio>
using namespace mpk225;
int main() {
    MpkMidiPipeline midi;
    assert(sizeof(UsbBulkFrame)==65);
    for (unsigned i=0;i<40;++i) assert(midi.consume({0xFF,91,36+i,0}));
    for (unsigned i=0;i<40;++i) assert(midi.consume({0xFE,0,36+i,0}));
    assert(midi.queuedMessages()==80);
    unsigned count=0, frames=0, noteOn=0, noteOff=0;
    while(midi.queuedMessages()) {
        const auto f=midi.takeUsbBulkFrame();
        ++frames;
        assert(f.length>=4 && f.length<=64 && (f.length%4)==0);
        assert(f.length==(frames<=4?64:64)); // exactly 5 x 64B
        for(unsigned p=0;p<f.length;p+=4){
            assert((f.bytes[p]>>4)==0); // factory port-A cable
            const unsigned status=f.bytes[p+1] & 0xF0;
            assert((f.bytes[p]&0xF)==(status>>4));
            assert(f.bytes[p+2]>=36 && f.bytes[p+2]<76);
            if(status==0x90){assert(f.bytes[p+3]==91);++noteOn;}
            else {assert(status==0x80 && f.bytes[p+3]==0);++noteOff;}
            ++count;
        }
    }
    assert(frames==5 && count==80 && noteOn==40 && noteOff==40);
    assert(midi.takeUsbBulkFrame().length==0);
    EmbeddedSmartChordController embedded;
    assert(embedded.consume({0xFF,99,60,0}));
    const auto packet=embedded.takeUsbBulkFrame();
    assert(packet.length==4 && packet.bytes[1]==0x90);
    assert(packet.bytes[2]==60 && packet.bytes[3]==99);
    assert(embedded.consume({0xFE,0,60,0}));
    assert(embedded.takeUsbBulkFrame().length==4);
    std::puts("PASS: 80 USB MIDI events drained in five real 64-byte bulk packets with balanced note releases");
}
