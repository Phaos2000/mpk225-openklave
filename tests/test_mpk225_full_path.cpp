// Complete OFFLINE simulation of the candidate MPK225 raw keybed -> velocity
// -> smart chord selection -> MIDI FIFO -> multi-frame USB IN endpoint path.
// Physical UART framing, LCD/buttons, USB enumeration and booting remain untested.
#include "Mpk225FactoryKeybedDecoder.h"
#include "KeybedContactVelocity.h"
#include "EmbeddedSmartChordController.h"
#include "Stm32UsbTxGate.h"
#include <cassert>
#include <cstdio>
using namespace mpk225;

constexpr uint32_t rawWord(unsigned keyIndex, bool down, uint16_t ticks,
                           bool secondContact=false) {
    const unsigned k=keyIndex+4u;
    const unsigned scan=((k/8u)<<5u)|(k%8u)|(secondContact?8u:0u);
    return static_cast<uint32_t>(down?0xFFu:0xFEu) |
           static_cast<uint32_t>(scan<<8u) |
           (static_cast<uint32_t>(ticks)<<16u);
}
struct MidiWire {
    unsigned noteOns[128]{}, noteOffs[128]{}, ccPanic=0, frames=0, bytes=0;
    uint32_t ep=0x0621u; // EP1 NAK, interrupt type, endpoint address 1
    void drainOne(EmbeddedSmartChordController& core) {
        assert(usbShouldSubmitFrame(ep,static_cast<unsigned>(core.queuedMessages())));
        const auto f=core.takeUsbFrame();
        assert(f.length==4 || f.length==8);
        ++frames; bytes+=f.length;
        for(unsigned i=0;i<f.length;i+=4) {
            const uint8_t status=f.bytes[i+1];
            const uint8_t note=f.bytes[i+2];
            if(status==0x90) {
                assert(f.bytes[i]==9 && note<128 && f.bytes[i+3]!=0);
                ++noteOns[note];
            } else if(status==0x80) {
                assert(f.bytes[i]==8 && note<128 && f.bytes[i+3]==0);
                ++noteOffs[note];
            } else if(status==0xB0) {
                assert(f.bytes[i]==0x0B && (note==120 || note==123));
                ++ccPanic;
            } else {
                assert(false && "unexpected MIDI status in keybed test");
            }
        }
        ep=(ep&~0x30u)|0x30u; // VALID while host has not ACKed
        assert(!usbShouldSubmitFrame(ep,static_cast<unsigned>(core.queuedMessages())));
        ep=(ep&~0x30u)|0x20u|0x80u; // hardware NAK + CTR_TX
        assert(!usbShouldSubmitFrame(ep,static_cast<unsigned>(core.queuedMessages())));
        ep=usbEpCtrAcknowledgeWrite(ep)|0x20u;
    }
    void drainAll(EmbeddedSmartChordController& core) {
        while (core.queuedMessages()) drainOne(core);
    }
    void assertBalanced() const {
        for(unsigned i=0;i<128;++i) assert(noteOns[i]==noteOffs[i]);
    }
};

static void deliver(EmbeddedSmartChordController& core, MidiWire& bus,
                    uint32_t raw) {
    const auto key=decodeMpk225FactoryScan(raw);
    assert(key.valid);
    const uint8_t velocity=key.down
        ? contactVelocityFromWordForNote(raw,key.midiNote) : 0;
    const LegacyEvent event{key.down?0xFFu:0xFEu,velocity,key.midiNote,0};
    unsigned retry=0;
    while (!core.consume(event)) {
        assert(++retry<=10 && core.queuedMessages()>0);
        bus.drainOne(core);
    }
}
int main() {
    EmbeddedSmartChordController core;
    MidiWire bus;
    unsigned keyPresses=0;
    // All 25 positions, BOTH scan-contact bytes, varied 16-bit velocities.
    for (unsigned round=0;round<16;++round) {
        for(unsigned k=0;k<25;++k) {
            const uint16_t ticks=static_cast<uint16_t>(128u<<((k+round)%5u));
            const bool second=((k+round)&1u)!=0;
            const uint32_t down=rawWord(k,true,ticks,second);
            const uint32_t up=rawWord(k,false,0,second);
            assert(decodeMpk225FactoryScan(down).midiNote==48+k);
            deliver(core,bus,down);
            bus.drainAll(core);
            deliver(core,bus,up);
            bus.drainAll(core);
            ++keyPresses;
        }
    }
    bus.assertBalanced();
    assert(keyPresses==400u && bus.frames>400u);
    // Pressure test 25 physically held keys before USB has had a chance to
    // transmit; drive USB ACK/draining only when backpressure requires it.
    for(unsigned k=0;k<25;++k)
        deliver(core,bus,rawWord(k,true,512,false));
    bus.drainAll(core);
    for(unsigned k=25;k-->0;)
        deliver(core,bus,rawWord(k,false,0,true));
    bus.drainAll(core);
    bus.assertBalanced();
    assert(bus.ccPanic==0);
    assert(core.queuedMessages()==0);
    std::printf("PASS: 25-key factory-derived scan/velocity -> MIDI/USB; %u sequential keys, 25 held, %u USB frames, balanced on/off\n",
                keyPresses,bus.frames);
}
