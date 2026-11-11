#include "MpkMidiPipeline.h"
#include "EmbeddedSmartChordController.h"
#include <cassert>
#include <cstdio>
using namespace mpk225;

static void drain(MpkMidiPipeline& p, uint8_t status, unsigned count) {
    unsigned got=0;
    while (p.queuedMessages()) {
        UsbFrame frame=p.takeUsbFrame();
        for(unsigned i=0;i<frame.length;i+=4) {
            assert(frame.bytes[i+1] == status);
            ++got;
        }
    }
    assert(got==count);
}
int main() {
    MpkMidiPipeline p;
    assert(p.setChordMode(false)); // explicit passthrough even in smart-build tests
    assert(!p.chordMode());
    assert(p.consume({0xFF,101,60,0}));
    assert(p.queuedMessages()==1);
    p.resetForUsbBus();
    assert(p.queuedMessages()==0);
    assert(!p.chordMode());
    // A release of a key from the previous USB connection must not send
    // a ghost MIDI off to the newly enumerated host.
    assert(p.consume({0xFE,0,60,0}));
    assert(p.queuedMessages()==0);
    assert(p.consume({0xFF,88,60,0}));
    drain(p,0x90,1);
    assert(p.consume({0xFE,0,60,0}));
    drain(p,0x80,1);

    assert(p.setChordMode(true));
    p.setChordType(ChordType::Dominant7);
    assert(p.consume({0xFF,87,61,0}));
    assert(p.queuedMessages()==4);
    p.resetForUsbBus();
    assert(p.queuedMessages()==0);
    assert(p.chordMode() && p.chordType()==ChordType::Dominant7);
    assert(p.consume({0xFE,0,61,0}));
    assert(p.queuedMessages()==0);
    assert(p.consume({0xFF,95,61,0}));
    drain(p,0x90,4);
    assert(p.consume({0xFE,0,61,0}));
    drain(p,0x80,4);

    EmbeddedSmartChordController c;
    assert(c.consume({0xFF,84,60,0}));
    assert(c.queuedMessages()>0);
    c.resetForUsbBus();
    assert(c.queuedMessages()==0);
    assert(c.consume({0xFE,0,60,0}));
    assert(c.queuedMessages()==0);
    assert(c.consume({0xFF,84,60,0}));
#if MPK225_CANDIDATE_SMART_ON_BOOT
    assert(c.smartEnabled() && c.chordEnabled());
    assert(c.queuedMessages()>=3);
#else
    assert(!c.smartEnabled() && !c.chordEnabled());
    assert(c.queuedMessages()==1);
#endif
    c.resetForUsbBus();
    assert(c.queuedMessages()==0);
    std::puts("PASS: USB reset discards old pending MIDI and held notes; preserves chord mode/type and resumes cleanly");
}
