#pragma once
// Lightweight MIDI chord voicing planner. No allocations or device I/O.
// Explores real note-number inversions and octaves; minimizes ordered semitone
// movement from a previous voiced chord, with a fixed cost for added/dropped voices.
#include "ChordEngine.h"
#include <stdint.h>

namespace mpk225 {

struct VoicedChord {
    uint8_t notes[5]{};     // ascending MIDI note numbers
    uint8_t count = 0;
    uint8_t inversion = 0;  // number of lowest chord tones raised an octave
    uint16_t movement = 0;  // minimized semitone/voice-edit cost
};

class VoicingPlanner {
    static unsigned absDiff(unsigned a, unsigned b) { return a > b ? a-b : b-a; }

    // Monotonic voice matching avoids voice crossings. Each unmatched voice
    // costs 12 semitones. This is a specific, testable voicing heuristic.
    static uint16_t movementCost(const VoicedChord& previous, const VoicedChord& next) {
        uint16_t dp[6][6]{};
        for (unsigned i=0; i<=previous.count; ++i) dp[i][0] = static_cast<uint16_t>(i*12);
        for (unsigned j=0; j<=next.count; ++j) dp[0][j] = static_cast<uint16_t>(j*12);
        for (unsigned i=1; i<=previous.count; ++i)
            for (unsigned j=1; j<=next.count; ++j) {
                const unsigned match = dp[i-1][j-1] + absDiff(previous.notes[i-1],next.notes[j-1]);
                const unsigned remove = dp[i-1][j] + 12;
                const unsigned insert = dp[i][j-1] + 12;
                unsigned minimum = match < remove ? match : remove;
                if (insert < minimum) minimum = insert;
                dp[i][j] = static_cast<uint16_t>(minimum);
            }
        return dp[previous.count][next.count];
    }

    static uint16_t centerCost(const VoicedChord& v) {
        return static_cast<uint16_t>(absDiff((static_cast<unsigned>(v.notes[0])+v.notes[v.count-1])/2, 60));
    }

public:
    static bool plan(uint8_t rootPitchClass, ChordType type, const VoicedChord& previous,
                     uint8_t lowest, uint8_t highest, VoicedChord& output) {
        if (rootPitchClass >= 12 ||
            static_cast<unsigned>(type) >= static_cast<unsigned>(ChordType::Count) ||
            previous.count > 5 || lowest >= highest || highest > 127) return false;
        for (unsigned i=0; i<previous.count; ++i) {
            if (previous.notes[i] > 127 || (i && previous.notes[i] <= previous.notes[i-1]))
                return false;
        }
        const auto& definition = kChords[static_cast<unsigned>(type)];
        bool found = false;
        uint16_t bestMovement = 0xFFFFu;
        uint16_t bestCenter = 0xFFFFu;
        VoicedChord best{};
        for (unsigned base=rootPitchClass; base<=127; base+=12) {
            for (unsigned inv=0; inv<definition.count; ++inv) {
                VoicedChord v{};
                v.count = definition.count;
                v.inversion = static_cast<uint8_t>(inv);
                bool within = true;
                for (unsigned i=0; i<v.count; ++i) {
                    const unsigned midi = base + definition.semitone[i] + (i<inv ? 12u : 0u);
                    if (midi < lowest || midi > highest || midi > 127) { within=false; break; }
                    // Insert into ascending list, at most five elements.
                    unsigned j=i;
                    while (j>0 && v.notes[j-1]>midi) { v.notes[j]=v.notes[j-1]; --j; }
                    v.notes[j]=static_cast<uint8_t>(midi);
                }
                if (!within) continue;
                bool duplicates = false;
                for (unsigned i=1; i<v.count; ++i) if (v.notes[i] == v.notes[i-1]) duplicates=true;
                if (duplicates) continue;
                const uint16_t movement = previous.count ? movementCost(previous,v) : 0;
                const uint16_t center = centerCost(v);
                if (!found || movement<bestMovement ||
                    (movement==bestMovement && center<bestCenter)) {
                    found=true;
                    v.movement=movement;
                    best=v;
                    bestMovement=movement;
                    bestCenter=center;
                }
            }
        }
        if (!found) return false;
        output=best;
        return true;
    }
};

} // namespace mpk225
