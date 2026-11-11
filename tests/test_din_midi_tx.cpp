#include "Mpk225DinMidiTx.h"
#include <cassert>
#include <cstdio>
using namespace mpk225;
int main() {
    Mpk225DinTxQueue q;
    UsbMidiOutEvent e{{0x29,0x90,60,100}};
    assert(q.accept(e,2) && q.size()==3);
    assert(!q.accept(e,0)); // no implicit USB cable -> DIN assignment
    uint8_t byte=0;
    assert(q.pop(byte) && byte==0x90);
    assert(q.pop(byte) && byte==60);
    assert(q.pop(byte) && byte==100);
    assert(!q.pop(byte));
    const uint8_t cin[]={0x2,0x3,0x4,0x5,0x6,0x7,0x8,0x9,
                         0xA,0xB,0xC,0xD,0xE,0xF};
    for(unsigned i=0;i<sizeof(cin);++i) {
        e.data[0]=static_cast<uint8_t>((2u<<4)|cin[i]);
        e.data[1]=cin[i]>=8&&cin[i]<=0xE
                  ?static_cast<uint8_t>(cin[i]<<4):0xF8;
        e.data[2]=0x11; e.data[3]=0x22;
        assert(q.accept(e,2));
    }
    unsigned expected=0;
    for(unsigned c:cin) expected+=usbMidiSerialLength(c);
    assert(q.size()==expected);
    for(unsigned i=0;i<expected;++i) assert(q.pop(byte));
    assert(q.size()==0);
    e.data[0]=0x20;assert(!q.accept(e,2));
    e.data[0]=0x21;assert(!q.accept(e,2));
    e.data[0]=0x29; e.data[1]=0x80;
    assert(!q.accept(e,2)); // mismatched status and CIN
    e.data[1]=0x90;
    for(unsigned i=0;i<42;++i)assert(q.accept(e,2));
    assert(q.size()==126);
    assert(!q.accept(e,2) && q.size()==126);
    uint8_t packet[3]{};
    for(unsigned i=0;i<123;++i) assert(q.pop(byte));
    assert(q.size()==3);
    assert(q.accept(e,2));
    unsigned total=0;
    while(q.pop(byte)) {packet[total%3]=byte;++total;}
    assert(total==6 && packet[0]==0x90 && packet[1]==0x11);
    q.clear();
    assert(!q.pop(byte));
    static_assert(Mpk225DinUart1::kBrr==0x900,"exact 31.25 kbaud");
    std::puts("PASS: schematic-backed USART1 TX, USB CIN byte lengths, explicit cable binding, bounded atomic 128-byte FIFO");
}
