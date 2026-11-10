#include "MpkMidiPipeline.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <array>
#include <algorithm>
using namespace mpk225;

static std::vector<MidiEvent> drain(MpkMidiPipeline& p) {
    std::vector<MidiEvent> a;
    for (;;) {
        UsbFrame f = p.takeUsbFrame();
        if (f.length == 0) break;
        assert(f.length == 4 || f.length == 8);
        for (int j = 0; j < f.length; j += 4) {
            assert(f.bytes[j] == (f.bytes[j + 1] >> 4)); // cable 0, correct CIN
            a.push_back({f.bytes[j+1], f.bytes[j+2], f.bytes[j+3]});
        }
    }
    return a;
}
static void expect(const std::vector<MidiEvent>& got, const std::vector<MidiEvent>& want) {
    assert(got.size() == want.size());
    for (size_t i=0;i<want.size();++i) {
        assert(got[i].status == want[i].status);
        assert(got[i].note == want[i].note);
        assert(got[i].velocity == want[i].velocity);
    }
}
static void test_passthrough() {
    MpkMidiPipeline p;
    assert(!p.chordMode());
    assert(p.consume({0xFF, 93, 60, 0}));
    assert(p.consume({0xFE, 0, 60, 0}));
    expect(drain(p), {{0x90,60,93},{0x80,60,0}});
}
static void test_chord_mode() {
    MpkMidiPipeline p;
    assert(p.setChordMode(true));
    p.setChordType(ChordType::Dominant13Flat9);
    assert(p.consume({0xFF,90,60,0}));
    assert(!p.setChordMode(false)); // held key; must not release wrong pitches
    assert(p.consume({0xFE,0,60,0}));
    assert(p.setChordMode(false));
    expect(drain(p), {{0x90,60,90},{0x90,64,90},{0x90,70,90},
                      {0x90,73,90},{0x90,81,90},
                      {0x80,60,0},{0x80,64,0},{0x80,70,0},
                      {0x80,73,0},{0x80,81,0}});
}
static void test_pad_is_not_chord_and_controls() {
    MpkMidiPipeline p;
    assert(p.setChordMode(true));
    assert(p.consume({900,101,60,0}));
    assert(p.consume({905,50,60,0}));
    assert(p.consume({901,0,60,0}));
    assert(p.consume({201,72,1,0}));
    assert(p.consume({251,22,16,0}));
    assert(p.consume({500,91,0,0}));
    assert(p.consume({501,91,1,0}));
    assert(p.consume({500,91,70,0}));
    assert(p.consume({200,64,0,0}));
    expect(drain(p), {{0x90,60,101},{0xA0,60,50},{0x80,60,0},
                      {0xB0,1,72},{0xB0,16,22},{0xB0,70,65},
                      {0xB0,71,1},{0xB0,70,65},{0xE0,0,64}});
}
static void test_backpressure_atomicity() {
    MpkMidiPipeline p;
    assert(p.setChordMode(true));
    p.setChordType(ChordType::Major9);
    // 18 chords => 90 queued notes (no shared notes if spaced? use alternating
    // notes; shared notes reduce number, so use hundreds of unique retriggers).
    for (unsigned i = 0; i < 500; ++i) {
        const unsigned before = p.queuedMessages();
        const auto key = static_cast<uint32_t>(i%60 + 30);
        if (!p.consume({0xFF, 96, key, 0})) {
            assert(p.queuedMessages() == before);
            break;
        }
        if (i == 499) assert(false && "expected backpressure");
    }
    auto count = p.queuedMessages();
    assert(count >= 86 && count <= 96);
    std::vector<MidiEvent> emitted = drain(p);
    assert(emitted.size() == count);
    // New event after transport drains should be accepted.
    assert(p.consume({0xFF,97,120,0}));
    assert(!drain(p).empty());
}
static void test_shared_notes_retrigger_order() {
    MpkMidiPipeline p;
    assert(p.setChordMode(true));
    p.setChordType(ChordType::Major9);
    p.consume({0xFF,90,60,0});
    p.setChordType(ChordType::Minor9);
    p.consume({0xFF,88,64,0});
    p.consume({0xFE,0,60,0});
    p.consume({0xFE,0,64,0});
    auto msgs = drain(p);
    assert(msgs.size() == 12);
    const auto offs = std::count_if(msgs.begin(), msgs.end(), [](MidiEvent m){ return m.status==0x80;});
    assert(offs == 6);
    assert(msgs.front().note == 60 && msgs.front().status == 0x90);
    assert(msgs.back().note == 78 && msgs.back().status == 0x80);
}
static void test_long_stress() {
    MpkMidiPipeline p;
    assert(p.setChordMode(true));
    uint32_t rng=0x12345678;
    auto rnd=[&](){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;};
    unsigned retry=0;
    for (unsigned i=0;i<200000;i++) {
        if ((i%13)==0) drain(p);
        if (i%241 == 0) p.setChordType(static_cast<ChordType>(rnd()%static_cast<unsigned>(ChordType::Count)));
        uint32_t key=24+(rnd()%70);
        uint32_t type=(rnd()%4)==0?0xFE:0xFF;
        LegacyEvent e{type, type==0xFE?0u:90u,key,0};
        if (!p.consume(e)) {
            ++retry;
            drain(p);
            assert(p.consume(e));
        }
        assert(p.queuedMessages()<=MpkMidiPipeline::kQueueSize);
    }
    drain(p);
    (void)retry; // dedicated backpressure test guarantees rejection path.
}
int main() {
    test_passthrough();
    test_chord_mode();
    test_pad_is_not_chord_and_controls();
    test_backpressure_atomicity();
    test_shared_notes_retrigger_order();
    test_long_stress();
    printf("PASS: six MIDI-pipeline suites, 200000 stress events, %zu-byte pipeline\n",sizeof(MpkMidiPipeline));
}