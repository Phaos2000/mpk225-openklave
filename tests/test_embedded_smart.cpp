#include "EmbeddedSmartChordController.h"
#include <cassert>
#include <cstdio>
using namespace mpk225;

static void checkFrame(EmbeddedSmartChordController& dev,
                       unsigned& ons, unsigned& offs, uint16_t allowedMask) {
    while (dev.queuedMessages() > 0) {
        const UsbFrame f = dev.takeUsbFrame();
        assert(f.length && f.length <= 8 && f.length % 4 == 0);
        for (unsigned i = 0; i < f.length; i += 4) {
            const unsigned pitch = f.bytes[i+2];
            if (f.bytes[i+1] == 0x90) {
                assert(f.bytes[i] == 9 && f.bytes[i+3] == 91);
                assert(allowedMask & (1u << (pitch % 12u)));
                ++ons;
            } else {
                assert(f.bytes[i+1] == 0x80 && f.bytes[i+3] == 0);
                ++offs;
            }
        }
    }
}

int main() {
    EmbeddedSmartChordController dev{};
#if MPK225_CANDIDATE_SMART_ON_BOOT
    assert(dev.smartEnabled());
    assert(dev.chordEnabled());
    const uint16_t major = SmartHarmony::scaleMask(0, ScaleMode::Major);
    // Actual firmware event path, not merely a Windows tester:
    // C4 produces a scale-compatible chord and releasing C4 ends every voice.
    assert(dev.consume({0xFF,91,60,0}));
    const unsigned cvoices = static_cast<unsigned>(dev.queuedMessages());
    assert(cvoices >= 3 && cvoices <= 5);
    unsigned on=0,off=0;
    checkFrame(dev,on,off,major);
    assert(on == cvoices && off == 0);
    assert(dev.consume({0xFE,0,60,0}));
    checkFrame(dev,on,off,major);
    assert(off == on);
    // Invalid diatonic root is not silently discarded.
    on=off=0;
    assert(dev.consume({0xFF,91,61,0}));
    assert(dev.fallbackCount()==1 && dev.queuedMessages()==1);
    checkFrame(dev,on,off,static_cast<uint16_t>(1u << 1));
    assert(on==1);
    assert(dev.consume({0xFE,0,61,0}));
    checkFrame(dev,on,off,0xFFF);
    assert(off==1);
    // A previous chord influences subsequent G voicing; all notes remain in key.
    on=off=0;
    assert(dev.consume({0xFF,91,67,0}));
    const unsigned gvoices=static_cast<unsigned>(dev.queuedMessages());
    assert(gvoices >= 3 && gvoices <= 5);
    checkFrame(dev,on,off,major);
    assert(on==gvoices);
    assert(dev.consume({0xFE,0,67,0}));
    checkFrame(dev,on,off,major);
    assert(off==on);

    // Backpressure is atomic: no smart history, note ownership, or
    // fallback counter may advance before an event is accepted.
    EmbeddedSmartChordController full{};
    for (unsigned i=0;i<94;++i)
        assert(full.consume({201,23,0,0}));
    assert(full.queuedMessages()==94);
    assert(!full.consume({0xFF,91,61,0}));
    assert(full.queuedMessages()==94);
    assert(full.fallbackCount()==0);
    while (full.queuedMessages())
        full.takeUsbFrame();
    assert(full.consume({0xFF,91,61,0}));
    assert(full.fallbackCount()==1);
    assert(full.consume({0xFE,0,61,0}));
    // Stop/restart stress: 2000 sequential real key presses/releases.
    for (unsigned i=0;i<2000;++i) {
        const uint8_t key = static_cast<uint8_t>(48+(i%25));
        assert(dev.consume({0xFF,91,key,0}));
        on=off=0;
        checkFrame(dev,on,off,0xFFF);
        assert(on>=1 && on<=5);
        assert(dev.consume({0xFE,0,key,0}));
        checkFrame(dev,on,off,0xFFF);
        assert(off==on);
    }
    std::puts("PASS: embedded smart mode, real in-key voicings, safe fallback/releases, FIFO retry and 2000 plays");
#else
    assert(!dev.smartEnabled());
    assert(!dev.chordEnabled());
    assert(dev.consume({0xFF,91,60,0}));
    assert(dev.queuedMessages()==1);
    UsbFrame f=dev.takeUsbFrame();
    assert(f.length==4 && f.bytes[1]==0x90 && f.bytes[2]==60 && f.bytes[3]==91);
    assert(dev.consume({0xFE,0,60,0}));
    f=dev.takeUsbFrame();
    assert(f.length==4 && f.bytes[1]==0x80 && f.bytes[2]==60);
    std::puts("PASS: default embedded adapter keeps normal one-note MIDI passthrough");
#endif
}
