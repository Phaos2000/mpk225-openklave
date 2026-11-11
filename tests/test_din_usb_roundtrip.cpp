#include "Mpk225DinMidiRx.h"
#include "Mpk225DinMidiTx.h"
#include "Mpk225HostMidiQueue.h"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace mpk225;

static std::vector<uint8_t> roundtrip(const std::vector<uint8_t>& input,
                                      uint8_t cable) {
    Mpk225DinMidiRx decoder;
    for(uint8_t b:input) assert(decoder.feed(b,cable));
    Mpk225HostMidiQueue usbOut;
    Mpk225DinTxQueue transmitter;
    unsigned packetCount=0;
    while(decoder.queuedEvents()) {
        const auto packet=decoder.takeBulkFrame();
        assert(packet.length!=0 && packet.length<=64 && packet.length%4==0);
        unsigned invalid=999;
        assert(usbOut.appendFrame(packet.bytes,packet.length,invalid));
        assert(invalid==0);
        UsbMidiOutEvent event{};
        while(usbOut.peek(event)) {
            assert(event.cable()==cable);
            assert(transmitter.accept(event,cable));
            UsbMidiOutEvent acknowledged{};
            assert(usbOut.pop(acknowledged));
            for(unsigned k=0;k<4;++k)
                assert(acknowledged.data[k]==event.data[k]);
            ++packetCount;
        }
    }
    assert(packetCount>0);
    std::vector<uint8_t> result;
    uint8_t b=0;
    while(transmitter.pop(b)) result.push_back(b);
    return result;
}
int main() {
    // A canonical serial stream survives the DIN RX -> USB-MIDI event FIFO
    // -> DIN TX path byte-for-byte. Includes channel voice, realtime,
    // system-common, complete SysEx and two-byte channel messages.
    const std::vector<uint8_t> canonical={
        0x90,60,99,0x80,60,0,0xF8,0xF1,3,
        0xF0,0x7D,1,2,0xF7,0xC2,9,0xD2,65,0xF2,1,2,0xF6
    };
    for(unsigned cable=0;cable<4;++cable)
        assert(roundtrip(canonical,static_cast<uint8_t>(cable))==canonical);

    // Running status compresses serial data. USB transfers preserve meaning,
    // and DIN TX correctly regenerates explicit statuses for each event.
    const std::vector<uint8_t> running={0x91,60,80,62,81,0xF8,64,82};
    const std::vector<uint8_t> expanded={
        0x91,60,80,0x91,62,81,0xF8,0x91,64,82
    };
    assert(roundtrip(running,1)==expanded);

    // Backpressure: if the serial transmitter has no room for a 3-byte
    // packet, the USB source must retain its exact event for a retry.
    Mpk225DinTxQueue tx;
    UsbMidiOutEvent repeated{{0x29,0x90,60,90}};
    for(unsigned i=0;i<42;++i) assert(tx.accept(repeated,2));
    assert(tx.size()==126);
    uint8_t bytes[4]{0x29,0x90,72,80};
    Mpk225HostMidiQueue host;
    unsigned invalid=0;
    assert(host.appendFrame(bytes,4,invalid) && invalid==0);
    UsbMidiOutEvent peek{};
    assert(host.peek(peek));
    assert(!tx.accept(peek,2));
    assert(host.size()==1 && tx.size()==126);
    uint8_t byte=0;
    for(unsigned i=0;i<3;++i) assert(tx.pop(byte));
    assert(tx.accept(peek,2));
    UsbMidiOutEvent accepted{};
    assert(host.pop(accepted) && host.size()==0);
    assert(accepted.data[2]==72);

    // Undefined status bytes F4/F5 must not appear as spurious MIDI packets.
    Mpk225DinMidiRx invalidStatus;
    assert(invalidStatus.feed(0xF4,0));
    assert(invalidStatus.feed(0xF5,0));
    assert(invalidStatus.malformedCount()==2);
    assert(invalidStatus.queuedEvents()==0);
    // Error recovery: partial key-down bytes cannot escape after a drop.
    assert(invalidStatus.feed(0x90,1));
    assert(invalidStatus.feed(70,1));
    invalidStatus.signalLoss(1);
    assert(invalidStatus.feed(90,1)); // orphan velocity after loss
    auto panic=invalidStatus.takeBulkFrame();
    assert(panic.length==64 && panic.bytes[0]==0x1B);
    std::puts("PASS: 4-cable DIN serial/USB roundtrip, running-status expansion, FIFO retry, reserved status rejection and loss recovery");
}
