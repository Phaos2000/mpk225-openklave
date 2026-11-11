#pragma once
// Deterministic, no-allocation harmonic suggestions for an embedded MIDI controller.
// A suggestion is compatible when the ACTUALLY VOICED pitch classes fit the
// selected scale. This does not claim full harmonic-function understanding.
#include "ChordEngine.h"
#include <stddef.h>
#include <stdint.h>

namespace mpk225 {

enum class ScaleMode : uint8_t {
    Major, Dorian, Phrygian, Lydian, Mixolydian, NaturalMinor, Locrian,
    HarmonicMinor, MelodicMinor, MajorPentatonic, MinorPentatonic, Chromatic,
    Count
};

// Relative-to-tonic pitch-class masks; bit 0 is scale degree 1.
inline constexpr uint16_t kScaleMasks[] = {
    0xAB5, // major: 0,2,4,5,7,9,11
    0x6AD, // dorian: 0,2,3,5,7,9,10
    0x5AB, // phrygian: 0,1,3,5,7,8,10
    0xAD5, // lydian: 0,2,4,6,7,9,11
    0x6B5, // mixolydian: 0,2,4,5,7,9,10
    0x5AD, // natural minor: 0,2,3,5,7,8,10
    0x56B, // locrian: 0,1,3,5,6,8,10
    0x9AD, // harmonic minor: 0,2,3,5,7,8,11
    0xAAD, // melodic minor ascending: 0,2,3,5,7,9,11
    0x295, // major pentatonic: 0,2,4,7,9
    0x4A9, // minor pentatonic: 0,3,5,7,10
    0xFFF, // chromatic
};
static_assert(sizeof(kScaleMasks) / sizeof(kScaleMasks[0]) == static_cast<unsigned>(ScaleMode::Count),
              "Scale mask table mismatch");

// Strict: all *voiced* pitch classes in scale. Borrowed: root in scale and
// at most two out-of-scale notes. Any: list every defined root/type pair.
enum class HarmonyFilter : uint8_t { Strict, Borrowed, Any };

struct HarmonyContext {
    uint8_t tonic = 0; // C = 0, C# = 1 ... B = 11
    ScaleMode scale = ScaleMode::Major;
    HarmonyFilter filter = HarmonyFilter::Strict;
    bool hasPrevious = false;
    uint8_t previousRoot = 0;
    ChordType previousType = ChordType::Major7;
};

struct HarmonySuggestion {
    uint8_t root = 0;
    ChordType type = ChordType::Major;
    uint8_t nonScaleTones = 0; // distinct voiced pitch classes outside scale
    uint8_t sharedTones = 0;   // distinct shared pitch classes with previous chord
    uint8_t motion = 0;        // approximate semitone movement, lower is better
    int16_t score = 0;         // only a deterministic ranking heuristic
};

class SmartHarmony {
public:
    static constexpr unsigned kRoots = 12;
    static constexpr unsigned kPossibilities = 12 * static_cast<unsigned>(ChordType::Count);

    static constexpr uint8_t pitchClass(unsigned x) { return static_cast<uint8_t>(x % 12u); }

    static uint16_t scaleMask(uint8_t tonic, ScaleMode mode) {
        const unsigned m = static_cast<unsigned>(mode);
        if (tonic > 11 || m >= static_cast<unsigned>(ScaleMode::Count)) return 0;
        const uint16_t shape = kScaleMasks[m];
        uint16_t result = 0;
        for (unsigned i = 0; i < 12; ++i)
            if (shape & (1u << i)) result |= static_cast<uint16_t>(1u << ((i + tonic) % 12));
        return result;
    }

    static uint16_t voicingMask(uint8_t root, ChordType type) {
        const unsigned t = static_cast<unsigned>(type);
        if (root > 11 || t >= static_cast<unsigned>(ChordType::Count)) return 0;
        const auto& chord = kChords[t];
        uint16_t result = 0;
        for (unsigned i = 0; i < chord.count; ++i)
            result |= static_cast<uint16_t>(1u << pitchClass(root + chord.semitone[i]));
        return result;
    }

    static uint8_t bitCount(uint16_t mask) {
        uint8_t count = 0;
        while (mask) { mask &= static_cast<uint16_t>(mask - 1u); ++count; }
        return count;
    }

    // Find closest previous pitch class for each proposed voice. No octave or
    // one-to-one assignment constraints: ranking hint, NOT playable voice-leading.
    static uint8_t approximateMotion(uint8_t root, ChordType type, uint16_t previousMask) {
        const auto& chord = kChords[static_cast<unsigned>(type)];
        unsigned cost = 0;
        for (unsigned i = 0; i < chord.count; ++i) {
            const unsigned current = pitchClass(root + chord.semitone[i]);
            unsigned best = 6;
            for (unsigned prev = 0; prev < 12; ++prev) {
                if (!(previousMask & (1u << prev))) continue;
                const unsigned linear = current > prev ? current - prev : prev - current;
                const unsigned distance = linear > 6 ? 12 - linear : linear;
                if (distance < best) best = distance;
            }
            cost += best;
        }
        return static_cast<uint8_t>(cost);
    }

    // Pure evaluation: does not change notes being played.
    static bool evaluate(const HarmonyContext& ctx, uint8_t root, ChordType type,
                         HarmonySuggestion& out) {
        if (ctx.tonic > 11 || root > 11 ||
            static_cast<unsigned>(ctx.scale) >= static_cast<unsigned>(ScaleMode::Count) ||
            static_cast<unsigned>(ctx.filter) > static_cast<unsigned>(HarmonyFilter::Any) ||
            static_cast<unsigned>(type) >= static_cast<unsigned>(ChordType::Count) ||
            (ctx.hasPrevious && (ctx.previousRoot > 11 ||
             static_cast<unsigned>(ctx.previousType) >= static_cast<unsigned>(ChordType::Count))))
            return false;
        const uint16_t scale = scaleMask(ctx.tonic, ctx.scale);
        const uint16_t notes = voicingMask(root, type);
        const uint8_t outside = bitCount(static_cast<uint16_t>(notes & ~scale));
        const bool rootInScale = (scale & (1u << root)) != 0;
        if (ctx.filter == HarmonyFilter::Strict && (outside || !rootInScale)) return false;
        if (ctx.filter == HarmonyFilter::Borrowed && (!rootInScale || outside > 2)) return false;
        const uint16_t previous = ctx.hasPrevious ? voicingMask(ctx.previousRoot, ctx.previousType) : 0;
        const uint8_t shared = ctx.hasPrevious ? bitCount(static_cast<uint16_t>(notes & previous)) : 0;
        const uint8_t motion = ctx.hasPrevious ? approximateMotion(root, type, previous) : 0;
        // A reproducible ordering, not a claim that harmonic context is solved.
        const int score = 60 - 24 * outside + (rootInScale ? 5 : 0) +
                          (root == ctx.tonic ? 3 : 0) + 5 * shared - 2 * motion;
        out = {root, type, outside, shared, motion, static_cast<int16_t>(score)};
        return true;
    }

    // Enumerates ALL matches, no heap or arbitrary top-N cutoff. MIDI device
    // can page the results; a Windows companion can consume the whole list.
    template <class Visit>
    static size_t forEach(const HarmonyContext& ctx, Visit visit) {
        size_t n = 0;
        for (uint8_t root = 0; root < 12; ++root) {
            for (unsigned quality = 0; quality < static_cast<unsigned>(ChordType::Count); ++quality) {
                HarmonySuggestion s;
                if (evaluate(ctx, root, static_cast<ChordType>(quality), s)) {
                    visit(s);
                    ++n;
                }
            }
        }
        return n;
    }

    // Return the best candidates without heap allocation; stable deterministic
    // ties favor lower MIDI root and original chord-table order.
    static size_t top(const HarmonyContext& ctx, HarmonySuggestion* output, size_t capacity) {
        if (!output || capacity == 0) return 0;
        size_t filled = 0;
        forEach(ctx, [&](HarmonySuggestion candidate) {
            size_t at = 0;
            while (at < filled && candidate.score <= output[at].score) ++at;
            if (at >= capacity) return;
            if (filled < capacity) ++filled;
            for (size_t j = filled - 1; j > at; --j) output[j] = output[j - 1];
            output[at] = candidate;
        });
        return filled;
    }
};

} // namespace mpk225
