#include "Mpk225DinMidiRx.h"
#include <cassert>
#include <cstdio>
using namespace mpk225;
int main() {
    Mpk225DinMidiRx rx;
    const uint8_t running[]={0x90,60,100,62,101,0x80,60,0};
    for(uint8_t b:running) assert(rx.feed(b,2));
    auto f=rx.takeBulkFrame();
    assert(f.length==12 && f.bytes[0]==0x29 && f.bytes[2]==60);
    assert(f.bytes[4]==0x29 && f.bytes[6]==62);
    assert(f.bytes[8]==0x28 && f.bytes[9]==0x80);
    const uint8_t realtime[]={0x91,65,0xF8,87};
    for(uint8_t b:realtime) assert(rx.feed(b,3));
    f=rx.takeBulkFrame();
    assert(f.length==8 && f.bytes[0]==0x3F &&
           f.bytes[1]==0xF8 && f.bytes[4]==0x39 &&
           f.bytes[5]==0x91 && f.bytes[6]==65 && f.bytes[7]==87);
    const uint8_t sys[]={0xF1,3,0xF2,1,2,0xF6,0xF0,0x7D,1,2,0xF7};
    for(uint8_t b:sys) assert(rx.feed(b,0));
    f=rx.takeBulkFrame();
    assert(f.length==20 && f.bytes[0]==0x02 &&
           f.bytes[4]==0x03 && f.bytes[8]==0x05 &&
           f.bytes[12]==0x04 && f.bytes[16]==0x06 &&
           f.bytes[17]==2 && f.bytes[18]==0xF7);
    for(unsigned cable=0;cable<4;++cable)
    for(unsigned ch=0;ch<16;++ch) {
        assert(rx.feed(static_cast<uint8_t>(0xC0+ch),static_cast<uint8_t>(cable)));
        assert(rx.feed(static_cast<uint8_t>(ch),static_cast<uint8_t>(cable)));
    }
    for(unsigned cable=0;cable<4;++cable) {
        f=rx.takeBulkFrame();assert(f.length==64);
        for(unsigned ch=0;ch<16;++ch)
            assert(f.bytes[ch*4]==((cable<<4)|0xC) &&
                   f.bytes[ch*4+1]==(0xC0|ch));
    }
    assert(!rx.feed(0x90,4));
    rx.clear();assert(rx.feed(60,2));
    assert(rx.malformedCount()!=0);
    for(unsigned i=0;i<64;++i) assert(rx.feed(0xF8,2));
    assert(!rx.feed(0xF8,2));
    assert(rx.overflowCount()==1 && rx.queuedEvents()==32);
    f=rx.takeBulkFrame();assert(f.length==64);
    for(unsigned i=0;i<16;++i)
        assert(f.bytes[4*i]==0x2B &&
               f.bytes[4*i+1]==(0xB0|(i/2)) &&
               f.bytes[4*i+2]==((i&1)?123:120));
    f=rx.takeBulkFrame();
    assert(f.length==64 && f.bytes[0]==0x2B && f.bytes[1]==0xB8);
    assert(rx.takeBulkFrame().length==0);
    rx.clear();assert(rx.feed(0x90,2) && rx.feed(60,2));
    rx.signalLoss(2);
    assert(rx.feed(100,2)); // orphan data cannot complete stale note
    assert(rx.takeBulkFrame().length==64);
    std::puts("PASS: DIN RX running status, realtime, SysEx, all four USB cables, overflow and 16-channel cleanup");
}
