#include "SmartHarmony.h"
#include "MpkMidiPipeline.h"
#include <cassert>
#include <cstdio>
using namespace mpk225;

int main() {
    HarmonyContext ctx{}; // strict C major
    HarmonySuggestion chosen{};
    assert(SmartHarmony::evaluate(ctx, 7, ChordType::Dominant7, chosen));
    assert(chosen.nonScaleTones == 0);
    MpkMidiPipeline pipeline;
    assert(pipeline.setChordMode(true));
    pipeline.setChordType(chosen.type);
    // One finger on G3 triggers a proper G7 voicing: G3, B3, D4, F4.
    assert(pipeline.consume({0xFF, 97, 55, 0}));
    assert(!pipeline.setChordMode(false)); // block unsafe toggle while held
    assert(pipeline.consume({0xFE, 0, 55, 0}));
    assert(pipeline.setChordMode(false));
    const uint8_t pitches[] = {55, 59, 62, 65};
    for (unsigned phase = 0; phase < 2; ++phase) {
        for (unsigned packet = 0; packet < 2; ++packet) {
            const UsbFrame frame = pipeline.takeUsbFrame();
            assert(frame.length == 8);
            for (unsigned event = 0; event < 2; ++event) {
                const unsigned ix = packet * 2 + event;
                const unsigned byte = event * 4;
                const uint8_t status = phase == 0 ? 0x90 : 0x80;
                assert(frame.bytes[byte] == (status >> 4));
                assert(frame.bytes[byte + 1] == status);
                assert(frame.bytes[byte + 2] == pitches[ix]);
                assert(frame.bytes[byte + 3] == (phase == 0 ? 97 : 0));
            }
        }
    }
    assert(pipeline.queuedMessages() == 0);
    assert(pipeline.takeUsbFrame().length == 0);
    printf("PASS: smart suggestion G7 -> one-finger MIDI -> 4 note-ons and 4 note-offs, zero stuck notes\n");
}
