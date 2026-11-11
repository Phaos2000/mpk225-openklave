#include "Stm32UsbTxGate.h"
#include "MpkMidiPipeline.h"
#include <cassert>
#include <cstdio>
using namespace mpk225;

int main() {
    for (uint32_t reg = 0; reg <= 65535u; ++reg) {
        const bool ready = (reg & (0x30u | 0x80u | 0x8000u)) == 0x20u;
        assert(usbTxBufferReusable(reg) == ready);
        assert(usbEpCtrAcknowledgeWrite(reg) == (reg & 0x070Fu));
        assert(usbShouldSubmitFrame(reg,0) == false);
        assert(usbShouldSubmitFrame(reg,1) == ready);
    }

    MpkMidiPipeline p;
    assert(p.setChordMode(true));
    assert(p.consume({0xFF,93,60,0})); // Cmaj9 -> five note-ons
    assert(p.queuedMessages()==5u);
    uint32_t ep = 0x0621u; // EP1 NAK, interrupt endpoint
    unsigned sent=0,frames=0;
    // No USB interrupt is required to *initiate* first transfer.
    for (unsigned i=0;i<3;++i) {
        assert(usbShouldSubmitFrame(ep,static_cast<unsigned>(p.queuedMessages())));
        const auto frame = p.takeUsbFrame();
        assert(frame.length==8 || frame.length==4);
        sent += frame.length/4;
        ++frames;
        ep = (ep & ~0x30u) | 0x30u; // host has not ACKed: VALID
        assert(!usbShouldSubmitFrame(ep,static_cast<unsigned>(p.queuedMessages())));
        // Host ACK: NAK plus CTR_TX pending
        ep = (ep & ~0x30u) | 0x20u | 0x80u;
        assert(!usbShouldSubmitFrame(ep,static_cast<unsigned>(p.queuedMessages())));
        // USB IRQ clears CTR_TX, preserving type/address.
        ep = usbEpCtrAcknowledgeWrite(ep) | 0x20u;
    }
    assert(frames==3u && sent==5u && p.queuedMessages()==0u);
    assert(!usbShouldSubmitFrame(ep,0));

    // Note-off expansion must also drain completely after quiet endpoint.
    assert(p.consume({0xFE,0,60,0}));
    assert(p.queuedMessages()==5u);
    unsigned released=0;
    for (unsigned i=0;i<3;++i) {
        assert(usbShouldSubmitFrame(ep,static_cast<unsigned>(p.queuedMessages())));
        auto frame=p.takeUsbFrame();
        for (unsigned j=0;j<frame.length;j+=4) {
            assert(frame.bytes[j+1]==0x80 && frame.bytes[j+3]==0);
            ++released;
        }
        ep=(ep & ~0x30u)|0x30u;
        assert(!usbShouldSubmitFrame(ep,static_cast<unsigned>(p.queuedMessages())));
        ep=(ep & ~0x30u)|0x20u|0x80u;
        ep=usbEpCtrAcknowledgeWrite(ep)|0x20u;
    }
    assert(released==5u && p.queuedMessages()==0u);
    std::puts("PASS: 65,536 USB endpoint states and idle kick/host ACK/3-frame chord-on + chord-off draining without missing MIDI");
}
