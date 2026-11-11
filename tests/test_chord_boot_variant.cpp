#include "MpkMidiPipeline.h"
#include <cassert>
#include <cstdio>
using namespace mpk225;

#ifndef MPK225_CANDIDATE_CHORD_ON_BOOT
#define MPK225_CANDIDATE_CHORD_ON_BOOT 0
#endif

int main() {
    MpkMidiPipeline p;
    const bool expected = MPK225_CANDIDATE_CHORD_ON_BOOT != 0;
    assert(p.chordMode() == expected);
    // Identical key input; optional firmware build setting decides whether
    // the key triggers a single note or the default five-voice major ninth.
    assert(p.consume({0xFF,100,60,0}));
    const unsigned count = expected ? 5u : 1u;
    assert(p.queuedMessages() == count);
    unsigned on=0;
    while (p.queuedMessages()) {
        const auto frame = p.takeUsbFrame();
        for (unsigned i=0;i<frame.length;i+=4) {
            assert(frame.bytes[i] == 0x9);
            assert(frame.bytes[i+1] == 0x90);
            assert(frame.bytes[i+3] == 100);
            ++on;
        }
    }
    assert(on == count);
    assert(p.consume({0xFE,0,60,0}));
    unsigned off=0;
    while (p.queuedMessages()) {
        const auto frame = p.takeUsbFrame();
        for (unsigned i=0;i<frame.length;i+=4) {
            assert(frame.bytes[i] == 0x8);
            assert(frame.bytes[i+1] == 0x80);
            assert(frame.bytes[i+3] == 0);
            ++off;
        }
    }
    assert(off == count);
    std::printf("PASS: candidate chord-on-boot=%d, one key -> %u note-ons and %u note-offs\n",
                expected ? 1 : 0, on, off);
}
