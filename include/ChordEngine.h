#pragma once

// Hardware-independent, allocation-free chord MIDI transformer.
// C++17. Does not access/flash Akai hardware and makes no claim of MPK225
// integration or compatibility. Intended for standalone host validation first.

#include <stddef.h>
#include <stdint.h>

namespace mpk225 {

enum class ChordType : uint8_t {
    Major, Minor, Sus2, Sus4, Augmented, Diminished,
    Major7, Minor7, Dominant7, HalfDiminished7, Diminished7, MinorMajor7,
    Major9, Minor9, Dominant9, DominantFlat9, DominantSharp9,
    Dominant9Sharp11, Dominant13, Dominant13Flat9, Dominant13Sharp9,
    Minor11, Minor13, Major7Sharp11,
    Major6, Minor6, MajorSixNine, MinorSixNine, MajorAdd9, MinorAdd9,
    Dominant7Sus4, Dominant9Sus4, Major7Sus2, Major7Sus4,
    Dominant7Flat5, Dominant7Sharp5, Major7Flat5, Major7Sharp5,
    Minor7Flat9, Minor9Flat5, Dominant7Sharp11, Dominant7Flat13,
    Dominant7Sharp9Flat13, Quartal, Sus4Add9, DiminishedMajor7,
    MinorMajor9, Minor9Sharp11, Power5,
    Count
};

struct ChordDefinition {
    const char* name;
    uint8_t count;
    uint8_t semitone[5];  // compact 3-5 voice voicings, not full chord spellings
};

inline constexpr ChordDefinition kChords[] = {
    {"maj",       3, {0, 4, 7, 0, 0}},
    {"min",       3, {0, 3, 7, 0, 0}},
    {"sus2",      3, {0, 2, 7, 0, 0}},
    {"sus4",      3, {0, 5, 7, 0, 0}},
    {"aug",       3, {0, 4, 8, 0, 0}},
    {"dim",       3, {0, 3, 6, 0, 0}},
    {"maj7",      4, {0, 4, 7,11, 0}},
    {"m7",        4, {0, 3, 7,10, 0}},
    {"7",         4, {0, 4, 7,10, 0}},
    {"m7b5",      4, {0, 3, 6,10, 0}},
    {"dim7",      4, {0, 3, 6, 9, 0}},
    {"mMaj7",     4, {0, 3, 7,11, 0}},
    {"maj9",      5, {0, 4, 7,11,14}},
    {"m9",        5, {0, 3, 7,10,14}},
    {"9",         5, {0, 4, 7,10,14}},
    {"7b9",       5, {0, 4, 7,10,13}},
    {"7#9",       5, {0, 4, 7,10,15}},
    {"9#11",      5, {0, 4,10,14,18}}, // fifth omitted
    {"13",        5, {0, 4,10,14,21}}, // fifth omitted
    {"13b9",      5, {0, 4,10,13,21}}, // fifth omitted
    {"13#9",      5, {0, 4,10,15,21}}, // fifth omitted
    {"m11",       5, {0, 3,10,14,17}}, // fifth omitted
    {"m13",       5, {0, 3,10,14,21}}, // fifth omitted
    {"maj7#11",   5, {0, 4,11,14,18}}, // fifth omitted
    {"6",         4, {0, 4, 7, 9, 0}},
    {"m6",        4, {0, 3, 7, 9, 0}},
    {"6/9",       5, {0, 4, 7, 9,14}},
    {"m6/9",      5, {0, 3, 7, 9,14}},
    {"add9",      4, {0, 4, 7,14, 0}},
    {"madd9",     4, {0, 3, 7,14, 0}},
    {"7sus4",     4, {0, 5, 7,10, 0}},
    {"9sus4",     5, {0, 5, 7,10,14}},
    {"maj7sus2",  4, {0, 2, 7,11, 0}},
    {"maj7sus4",  4, {0, 5, 7,11, 0}},
    {"7b5",       4, {0, 4, 6,10, 0}},
    {"7#5",       4, {0, 4, 8,10, 0}},
    {"maj7b5",    4, {0, 4, 6,11, 0}},
    {"maj7#5",    4, {0, 4, 8,11, 0}},
    {"m7b9",      5, {0, 3, 7,10,13}},
    {"m9b5",      5, {0, 3, 6,10,14}},
    {"7#11",      5, {0, 4, 7,10,18}},
    {"7b13",      5, {0, 4, 7,10,20}},
    {"7#9b13",    5, {0, 4,10,15,20}}, // fifth omitted
    {"quartal",   4, {0, 5,10,15, 0}},
    {"sus4add9",  4, {0, 5, 7,14, 0}},
    {"dimMaj7",   4, {0, 3, 6,11, 0}},
    {"mMaj9",     5, {0, 3, 7,11,14}},
    {"m9#11",     5, {0, 3,10,14,18}}, // fifth omitted
    {"5",         3, {0, 7,12, 0, 0}}, // root, fifth and octave
};
static_assert(sizeof(kChords) / sizeof(kChords[0]) == static_cast<unsigned>(ChordType::Count),
              "ChordType and voicing table mismatch");

// All events currently use MIDI channel 1. 4-bit USB-MIDI cable number is not
// represented here; the transport adapter must packetize these MIDI bytes.
struct MidiEvent {
    uint8_t status;  // 0x90 note-on, 0x80 note-off
    uint8_t note;
    uint8_t velocity;
};

// One key retrigger can release five previous notes and start five new notes.
struct MidiBatch {
    MidiEvent events[10]{};
    uint8_t count = 0;

    bool append(uint8_t status, uint8_t note, uint8_t velocity) {
        if (count >= 10) return false;
        events[count++] = {status, note, velocity};
        return true;
    }
};

class ChordEngine {
public:
    static constexpr size_t kInputKeys = 128;
    static constexpr size_t kVoicesPerKey = 5;

    void setChordType(ChordType type) {
        if (static_cast<unsigned>(type) < static_cast<unsigned>(ChordType::Count)) {
            current_ = type;
        }
    }

    ChordType chordType() const { return current_; }
    const char* chordName() const { return kChords[static_cast<unsigned>(current_)].name; }

    // A type change applies to NEW key presses only. Notes already sounding
    // must release the pitches they generated, irrespective of current type.
    MidiBatch noteOn(uint8_t key, uint8_t velocity) {
        if (key >= kInputKeys) return MidiBatch{};
        if (velocity == 0) return noteOff(key);  // MIDI note-on / velocity 0 = off
        MidiBatch result;
        release(key, result);  // safe on repeat note-on, bounded max 5 offs

        const auto& chord = kChords[static_cast<unsigned>(current_)];
        HeldKey& held = held_[key];
        held.active = true;
        for (uint8_t i = 0; i < chord.count; ++i) {
            const unsigned pitch = static_cast<unsigned>(key) + chord.semitone[i];
            if (pitch > 127) continue; // no MIDI note number exists above 127
            const uint8_t n = static_cast<uint8_t>(pitch);
            held.notes[held.count++] = n;
            // Reference counts prevent duplicate note-ons/early offs for shared
            // notes across simultaneously held chords.
            if (references_[n]++ == 0) {
                result.append(0x90, n, static_cast<uint8_t>(velocity > 127 ? 127 : velocity));
            }
        }
        return result;
    }

    MidiBatch noteOff(uint8_t key) {
        if (key >= kInputKeys) return MidiBatch{};
        MidiBatch result;
        release(key, result);
        return result;
    }

    // Optional host-selected absolute MIDI voicing, for scale-aware inversions.
    // Reject invalid note lists BEFORE releasing any existing held chord.
    // Default noteOn() behaviour is unchanged.
    MidiBatch noteOnVoicing(uint8_t key, uint8_t velocity,
                            const uint8_t* notes, uint8_t count) {
        if (key >= kInputKeys || notes == nullptr || count == 0 ||
            count > kVoicesPerKey) return MidiBatch{};
        if (velocity == 0) return noteOff(key);
        for (uint8_t i = 0; i < count; ++i) {
            if (notes[i] > 127 || (i > 0 && notes[i] <= notes[i-1]))
                return MidiBatch{};
        }
        MidiBatch result;
        release(key, result);
        HeldKey& held = held_[key];
        held.active = true;
        for (uint8_t i = 0; i < count; ++i) {
            const uint8_t n = notes[i];
            held.notes[held.count++] = n;
            if (references_[n]++ == 0) {
                result.append(0x90, n, static_cast<uint8_t>(velocity > 127 ? 127 : velocity));
            }
        }
        return result;
    }

    // Emit currently sounding notes and clear internal state. Intended for
    // disabling chord mode or error recovery. Caller must drain all outputs.
    template <typename Emit>
    void allNotesOff(Emit emit) {
        for (unsigned n = 0; n < 128; ++n) {
            if (references_[n]) emit(MidiEvent{0x80, static_cast<uint8_t>(n), 0});
            references_[n] = 0;
        }
        for (auto& held : held_) held = HeldKey{};
    }

    bool isHeld(uint8_t key) const { return key < kInputKeys && held_[key].active; }
    uint8_t noteReferences(uint8_t note) const { return note < 128 ? references_[note] : 0; }

private:
    struct HeldKey {
        bool active = false;
        uint8_t count = 0;
        uint8_t notes[kVoicesPerKey]{};
    };

    void release(uint8_t key, MidiBatch& result) {
        HeldKey& held = held_[key];
        if (!held.active) return;
        for (uint8_t i = 0; i < held.count; ++i) {
            const uint8_t n = held.notes[i];
            // Invariant: each held key owns one reference for each output note.
            if (references_[n] != 0 && --references_[n] == 0) {
                result.append(0x80, n, 0);
            }
        }
        held = HeldKey{};
    }

    ChordType current_ = ChordType::Major9;
    HeldKey held_[kInputKeys]{};
    uint8_t references_[128]{};
};

static_assert(sizeof(ChordEngine) < 2048, "ChordEngine exceeds planned RAM budget");

}  // namespace mpk225