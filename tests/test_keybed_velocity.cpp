#include "KeybedContactVelocity.h"
#include "Mpk225FactoryKeybedDecoder.h"
#include "MpkMidiPipeline.h"
#include <cassert>
#include <cstdint>
#include <cstdio>

using namespace mpk225;

static constexpr uint32_t packet(uint8_t kind, uint8_t code, uint16_t ticks) {
    return static_cast<uint32_t>(kind) |
           (static_cast<uint32_t>(code) << 8) |
           (static_cast<uint32_t>(ticks) << 16);
}

int main() {
    // Exhaustive check over the entire possible two-byte contact interval.
    uint8_t prior = 127;
    for (unsigned t = 1; t <= 65535; ++t) {
        const uint8_t v = contactVelocity(static_cast<uint16_t>(t));
        assert(v >= 1 && v <= 127);
        assert(v <= prior); // slower contacts cannot become louder
        prior = v;
    }
    assert(contactVelocity(0) == 100);
    assert(contactVelocity(1) == 127);
    assert(contactVelocity(128) == 127);
    assert(contactVelocity(256) == 64);
    assert(contactVelocity(512) == 32);
    assert(contactVelocity(1024) == 16);
    assert(contactVelocity(65535) == 1);
    assert(contactVelocity(256,{512,64,95}) == 127);
    assert(contactVelocity(0,{512,64,95}) == 95);
    // Real Akai key-class code: 0xAB5 selects C/D/E/F/G/A/B; distinguish
    // white and black key response coefficients without dynamic allocations.
    ContactVelocitySettings split{256,64,100,512};
    for (unsigned i=0;i<128;++i) {
        const bool expected=(0xAB5u & (1u << (i%12u))) != 0;
        assert(isWhitePianoKey(static_cast<uint8_t>(i))==expected);
        const auto v=contactVelocityForNote(512,static_cast<uint8_t>(i),split);
        assert(v==(expected ? 32 : 64));
    }
    assert(contactVelocityFromWordForNote(packet(0xFF,0x40,512),60,split)==32);
    assert(contactVelocityFromWordForNote(packet(0xFF,0x41,512),61,split)==64);
    assert(contactVelocityForNote(512,128,split)==100);

    // Validate UART word extraction (little-endian low scan/status bytes,
    // high two bytes carrying the factory contact timer).
    const uint32_t slow = packet(0xFF,0x40,1024);
    const uint32_t fast = packet(0xFF,0x48,128);
    const auto slowKey = decodeMpk225FactoryScan(slow);
    const auto fastKey = decodeMpk225FactoryScan(fast);
    assert(slowKey.valid && fastKey.valid && slowKey.down && fastKey.down);
    assert(slowKey.midiNote == 60 && fastKey.midiNote == 60);
    assert(contactTicksFromWord(slow) == 1024);
    assert(contactTicksFromWord(fast) == 128);
    assert(contactVelocityFromWord(slow) == 16);
    assert(contactVelocityFromWord(fast) == 127);

    // Same key at distinct contact timings produces different MIDI velocities.
    MpkMidiPipeline midi;
    assert(!midi.chordMode()); // factory-style passthrough remains default
    assert(midi.consume({0xFF,contactVelocityFromWord(slow),slowKey.midiNote,0}));
    auto out = midi.takeUsbFrame();
    assert(out.length == 4 && out.bytes[1] == 0x90 &&
           out.bytes[2] == 60 && out.bytes[3] == 16);
    assert(midi.consume({0xFE,0,60,0}));
    out = midi.takeUsbFrame();
    assert(out.length == 4 && out.bytes[1] == 0x80 && out.bytes[3] == 0);
    assert(midi.consume({0xFF,contactVelocityFromWord(fast),fastKey.midiNote,0}));
    out = midi.takeUsbFrame();
    assert(out.length == 4 && out.bytes[1] == 0x90 &&
           out.bytes[2] == 60 && out.bytes[3] == 127);
    assert(midi.consume({0xFE,0,60,0}));
    out = midi.takeUsbFrame();
    assert(out.length == 4 && out.bytes[1] == 0x80 && out.bytes[3] == 0);

    // The same velocity propagates to every chord voice (5-note major 9th).
    assert(midi.setChordMode(true));
    assert(midi.consume({0xFF,contactVelocityFromWord(slow),60,0}));
    unsigned noteOns=0;
    while (midi.queuedMessages()) {
        out=midi.takeUsbFrame();
        for (unsigned i=0;i<out.length;i+=4) {
            assert(out.bytes[i+1]==0x90 && out.bytes[i+3]==16);
            ++noteOns;
        }
    }
    assert(noteOns==5);
    assert(midi.consume({0xFE,0,60,0}));
    unsigned noteOffs=0;
    while (midi.queuedMessages()) {
        out=midi.takeUsbFrame();
        for (unsigned i=0;i<out.length;i+=4) {
            assert(out.bytes[i+1]==0x80 && out.bytes[i+3]==0);
            ++noteOffs;
        }
    }
    assert(noteOffs==5);
    std::puts("PASS: 65,535 contact intervals monotonic; factory white/black classification; note-specific velocity and 5-voice releases");
}
