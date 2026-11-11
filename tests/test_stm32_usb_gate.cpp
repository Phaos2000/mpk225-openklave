#include "Stm32UsbTxGate.h"
#include "MpkMidiPipeline.h"
#include <cassert>
#include <cstdio>
using namespace mpk225;

int main() {
    for (uint32_t status = 0; status <= 65535; ++status) {
        const bool shouldAllow = ((status >> 4u) & 3u) == 2u;
        assert(usbTxBufferReusable(status) == shouldAllow);
    }
    MpkMidiPipeline p;
    assert(p.consume({0xFF,93,60,0}));
    assert(p.queuedMessages() == 1);
    // Model the exact bridge contract: do not take a frame until the USB
    // endpoint has completed the previous IN transfer.
    for (unsigned status : {0x0u,0x10u,0x30u,0xB0u}) {
        if (usbTxBufferReusable(status)) p.takeUsbFrame();
        assert(p.queuedMessages() == 1);
    }
    assert(usbTxBufferReusable(0xA0));
    const auto f = p.takeUsbFrame();
    assert(f.length==4 && f.bytes[1]==0x90 && f.bytes[2]==60);
    assert(p.queuedMessages()==0);
    std::puts("PASS: STM32 USB IN endpoint guard across all 65,536 bit patterns; pending MIDI kept during VALID/STALL/DISABLED");
}
