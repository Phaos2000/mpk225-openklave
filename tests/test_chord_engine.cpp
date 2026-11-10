#include "ChordEngine.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <algorithm>

using namespace mpk225;

static std::vector<int> pitches(const MidiBatch& b, uint8_t status) {
    std::vector<int> out;
    for (unsigned i = 0; i < b.count; ++i) if (b.events[i].status == status) out.push_back(b.events[i].note);
    return out;
}

static void test_chord_vectors() {
    ChordEngine e;
    e.setChordType(ChordType::Major9);
    assert((pitches(e.noteOn(60, 90), 0x90) == std::vector<int>{60,64,67,71,74}));
    assert(pitches(e.noteOff(60), 0x80).size() == 5);
    e.setChordType(ChordType::Minor9);
    assert((pitches(e.noteOn(60, 90), 0x90) == std::vector<int>{60,63,67,70,74}));
    assert(pitches(e.noteOff(60), 0x80).size() == 5);
    e.setChordType(ChordType::Dominant13Flat9);
    assert((pitches(e.noteOn(60, 90), 0x90) == std::vector<int>{60,64,70,73,81}));
    assert(pitches(e.noteOff(60), 0x80).size() == 5);
}

static void test_change_type_during_hold() {
    ChordEngine e;
    e.setChordType(ChordType::Major9);
    e.noteOn(60, 70);
    e.setChordType(ChordType::Minor9);
    assert((pitches(e.noteOff(60), 0x80) == std::vector<int>{60,64,67,71,74}));
}

static void test_shared_notes() {
    ChordEngine e;
    e.setChordType(ChordType::Major9);
    auto a = e.noteOn(60, 90);   // C E G B D
    assert(a.count == 5);
    e.setChordType(ChordType::Minor9);
    auto b = e.noteOn(64, 90);   // E G B D F#
    assert((pitches(b, 0x90) == std::vector<int>{78}));
    auto c = e.noteOff(60);
    assert((pitches(c, 0x80) == std::vector<int>{60}));
    auto d = e.noteOff(64);
    assert((pitches(d, 0x80) == std::vector<int>{64,67,71,74,78}));
}

static void test_retrigger_and_zero_velocity() {
    ChordEngine e;
    auto first = e.noteOn(60, 80);
    assert(first.count == 5);
    auto second = e.noteOn(60, 90);
    assert(second.count == 10);
    for (unsigned i = 0; i < 5; ++i) {
        assert(second.events[i].status == 0x80);
        assert(second.events[i+5].status == 0x90);
    }
    assert(e.noteOn(60, 0).count == 5);
    assert(e.noteOff(60).count == 0);
}

static void test_panic_and_upper_bounds() {
    ChordEngine e;
    e.setChordType(ChordType::Dominant13Flat9);
    auto hi = e.noteOn(127, 200);
    assert(hi.count == 1 && hi.events[0].note == 127 && hi.events[0].velocity == 127);
    e.noteOn(60, 80);
    unsigned offs = 0;
    e.allNotesOff([&](MidiEvent m) { assert(m.status == 0x80); ++offs; });
    assert(offs == 6);
    assert(e.noteOff(60).count == 0);
    assert(e.noteOff(127).count == 0);
}

static void test_all_chord_definitions() {
    ChordEngine e;
    for (unsigned c = 0; c < static_cast<unsigned>(ChordType::Count); ++c) {
        e.setChordType(static_cast<ChordType>(c));
        const auto& def = kChords[c];
        assert(def.count >= 3 && def.count <= 5);
        for (unsigned j = 1; j < def.count; ++j) assert(def.semitone[j] > def.semitone[j-1]);
        const auto on = e.noteOn(48, 101);
        assert(on.count == def.count);
        for (unsigned j = 0; j < def.count; ++j) assert(on.events[j].note == 48 + def.semitone[j]);
        assert(e.noteOff(48).count == def.count);
    }
}

static uint32_t seed = 0xF00D1234u;
static uint32_t rand32() { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }

static void test_randomized_note_consistency() {
    // Independent host-side state model compares every emitted MIDI message.
    // This guards against hanging notes and premature releases, not just crashes.
    ChordEngine engine;
    std::array<std::vector<uint8_t>,128> active{};
    std::array<unsigned,128> refs{};
    ChordType type = ChordType::Major9;

    auto release = [&](uint8_t key, std::vector<MidiEvent>& expected) {
        for (const auto note : active[key]) {
            assert(refs[note] >= 1);
            if (--refs[note] == 0) expected.push_back({0x80, note, 0});
        }
        active[key].clear();
    };

    for (unsigned it = 0; it < 200000; ++it) {
        uint8_t key = static_cast<uint8_t>(rand32() % 128);
        unsigned operation = rand32() % 32;
        if (operation == 0) {
            type = static_cast<ChordType>(rand32() % static_cast<unsigned>(ChordType::Count));
            engine.setChordType(type);
            continue;
        }
        if (operation == 1) {
            std::vector<MidiEvent> expected;
            for (uint16_t n = 0; n < 128; ++n) {
                if (refs[n]) expected.push_back({0x80, static_cast<uint8_t>(n), 0});
                refs[n] = 0;
            }
            for (auto& k : active) k.clear();
            std::vector<MidiEvent> got;
            engine.allNotesOff([&](MidiEvent m) { got.push_back(m); });
            assert(got.size() == expected.size());
            for (unsigned j = 0; j < got.size(); ++j) {
                assert(got[j].status == expected[j].status);
                assert(got[j].note == expected[j].note);
            }
            continue;
        }
        bool noteOn = ((operation & 1) != 0);
        auto velocity = static_cast<uint8_t>((rand32() % 127) + 1);
        std::vector<MidiEvent> expected;
        release(key, expected);
        if (noteOn) {
            const auto& def = kChords[static_cast<unsigned>(type)];
            for (uint8_t i = 0; i < def.count; ++i) {
                unsigned note = static_cast<unsigned>(key) + def.semitone[i];
                if (note > 127) continue;
                active[key].push_back(static_cast<uint8_t>(note));
                if (refs[note]++ == 0) expected.push_back({0x90, static_cast<uint8_t>(note), velocity});
            }
        }
        const MidiBatch got = noteOn ? engine.noteOn(key, velocity) : engine.noteOff(key);
        assert(got.count == expected.size());
        for (unsigned j = 0; j < got.count; ++j) {
            assert(got.events[j].status == expected[j].status);
            assert(got.events[j].note == expected[j].note);
            assert(got.events[j].velocity == expected[j].velocity);
        }
        for (unsigned n = 0; n < 128; ++n) {
            assert(engine.noteReferences(static_cast<uint8_t>(n)) == refs[n]);
        }
    }
    engine.allNotesOff([](MidiEvent){});
    for (unsigned n = 0; n < 128; ++n) assert(engine.noteReferences(static_cast<uint8_t>(n)) == 0);
}

int main() {
    test_chord_vectors();
    test_change_type_during_hold();
    test_shared_notes();
    test_retrigger_and_zero_velocity();
    test_panic_and_upper_bounds();
    test_all_chord_definitions();
    test_randomized_note_consistency();
    std::printf("PASS: 7 suites, 200,000 deterministic stress operations; engine %zu bytes; %u chords\n",
                sizeof(ChordEngine), static_cast<unsigned>(ChordType::Count));
    return 0;
}