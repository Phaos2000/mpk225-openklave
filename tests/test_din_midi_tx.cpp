#include "Mpk225DinMidiTx.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
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
        const unsigned code=cin[i];
        e.data[0]=static_cast<uint8_t>((2u<<4)|code);
        e.data[1]=code>=8&&code<=0xE
                  ?static_cast<uint8_t>(code<<4):0xF8;
        e.data[2]=0x11; e.data[3]=0x22;
        switch(code) {
            case 2: e.data[1]=0xF1; break;
            case 3: e.data[1]=0xF2; break;
            case 4: e.data[1]=0xF0; break;
            case 5: e.data[1]=0xF7; break;
            case 6: e.data[1]=0x01; e.data[2]=0xF7; break;
            case 7: e.data[1]=0x01; e.data[3]=0xF7; break;
            default: break;
        }
        assert(q.accept(e,2));
    }
    unsigned expected=0;
    for(unsigned c:cin) expected+=usbMidiSerialLength(c);
    assert(q.size()==expected);
    for(unsigned i=0;i<expected;++i) assert(q.pop(byte));
    assert(q.size()==0);
    // Malformed CIN/status combinations must never reach the MIDI DIN wire.
    for(const auto& bad : {
        UsbMidiOutEvent{{0x22,0x90,10,0}},
        UsbMidiOutEvent{{0x23,0xF1,10,20}},
        UsbMidiOutEvent{{0x24,0x90,10,20}},
        UsbMidiOutEvent{{0x25,0x90,0,0}},
        UsbMidiOutEvent{{0x26,0x01,0x02,0}},
        UsbMidiOutEvent{{0x27,0x01,0x02,0x03}},
        UsbMidiOutEvent{{0x29,0x90,0x80,10}},
        UsbMidiOutEvent{{0x2B,0x90,10,20}}
    }) assert(!q.accept(bad,2) && q.size()==0);
    // USB MIDI 1.0 CIN=0xF is an unparsed one-byte event (not realtime only).
    UsbMidiOutEvent raw{{0x2F,0xF0,0,0}};
    assert(q.accept(raw,2) && q.pop(byte) && byte==0xF0);
    e.data[0]=0x20;assert(!q.accept(e,2));
    e.data[0]=0x21;assert(!q.accept(e,2));
    e.data[0]=0x29; e.data[1]=0x80;
    assert(!q.accept(e,2)); // mismatched status and CIN
    e.data[1]=0x90;
    e.data[2]=0x11;e.data[3]=0x22;
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
    // Every channel receives All Sound Off then All Notes Off after an
    // interrupted USB connection; all prior pending notes are replaced.
    assert(q.accept(e,2));
    q.panicAllChannels();
    assert(q.size()==96);
    for(unsigned ch=0;ch<16;++ch) {
        assert(q.pop(byte) && byte==(0xB0u|ch));
        assert(q.pop(byte) && byte==120);
        assert(q.pop(byte) && byte==0);
        assert(q.pop(byte) && byte==(0xB0u|ch));
        assert(q.pop(byte) && byte==123);
        assert(q.pop(byte) && byte==0);
    }
    assert(q.size()==0 && !q.pop(byte));
    static_assert(Mpk225DinUart1::kBrr==0x600,"exact 31.25 kbaud from 48MHz PCLK2");
    static_assert(Mpk225DinUart1::matchesClock(0x51440Au),
                  "accept pinned 48MHz PLL/USB clock");
    static_assert(!Mpk225DinUart1::matchesClock(
                   (0x51440Au & ~0x003C0000u) | (7u<<18)),
                  "reject 72MHz PLL clock, which needs a different BRR");
    static_assert(!Mpk225DinUart1::matchesClock(0x51440Au | (1u<<11)),
                  "reject changed APB2 divider");
    static_assert(!Mpk225DinUart1::matchesClock(0x51440Au & ~0x00400000u),
                  "reject USB clock prescaler mismatch");
    // RX-only must not drive the PA9 MIDI OUT pin or enable USART1 TE.
    static_assert(Mpk225DinUart1::gpioAHighConfig(0xFFFFFFFFu,false,true)
                  ==0xFFFFF4FFu,"RX-only preserves PA9 and other GPIO pins");
    static_assert(Mpk225DinUart1::gpioAHighConfig(0xFFFFFFFFu,true,false)
                  ==0xFFFFFFBFu,"TX-only preserves PA10");
    static_assert(Mpk225DinUart1::gpioAHighConfig(0xFFFFFFFFu,true,true)
                  ==0xFFFFF4BFu,"full UART config alters PA9/PA10 only");
    static_assert((Mpk225DinUart1::controlBits(false,true)&(1u<<3))==0u,
                  "RX-only must not enable the transmitter");
    static_assert((Mpk225DinUart1::controlBits(true,false)&(1u<<2))==0u,
                  "TX-only must not enable the receiver");
    std::puts("PASS: schematic-backed USART1 TX, USB CIN byte lengths, explicit cable binding, bounded atomic 128-byte FIFO");
}
