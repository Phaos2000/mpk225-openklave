#include "SmartHarmony.h"
#include <assert.h>
#include <stdio.h>
using namespace mpk225;

static bool has(const HarmonyContext& ctx, uint8_t root, ChordType type) {
    HarmonySuggestion candidate{};
    return SmartHarmony::evaluate(ctx, root, type, candidate);
}

int main() {
    const HarmonyContext c{}; // C major strict
    assert(SmartHarmony::scaleMask(0, ScaleMode::Major) == 0xAB5);
    assert(SmartHarmony::scaleMask(9, ScaleMode::NaturalMinor) == 0xAB5); // A minor has C major pitch classes
    assert(SmartHarmony::scaleMask(0, ScaleMode::Chromatic) == 0xFFF);
    assert(has(c, 0, ChordType::Major7));
    assert(has(c, 9, ChordType::Minor7));
    assert(has(c, 7, ChordType::Dominant9));
    assert(has(c, 5, ChordType::Major7Sharp11));
    assert(!has(c, 0, ChordType::Minor7));
    assert(!has(c, 0, ChordType::DominantFlat9));
    assert(!has(c, 1, ChordType::Major7));
    unsigned observed = 0;
    bool discoveredG7 = false;
    const auto count = SmartHarmony::forEach(c, [&](HarmonySuggestion s) {
        ++observed;
        assert(s.nonScaleTones == 0);
        if (s.root == 7 && s.type == ChordType::Dominant7) discoveredG7 = true;
    });
    assert(count == observed && count > 10 && discoveredG7);

    HarmonyContext borrowed = c;
    borrowed.filter = HarmonyFilter::Borrowed;
    assert(has(borrowed, 0, ChordType::Minor7)); // C Eb G Bb has 2 out-of-scale tones
    assert(!has(borrowed, 1, ChordType::Major7)); // root not in C major
    HarmonyContext all = c;
    all.filter = HarmonyFilter::Any;
    assert(SmartHarmony::forEach(all, [](HarmonySuggestion) {}) == SmartHarmony::kPossibilities);

    HarmonyContext previous = c;
    previous.hasPrevious = true;
    previous.previousRoot = 0;
    previous.previousType = ChordType::Major7;
    HarmonySuggestion same{}, different{};
    assert(SmartHarmony::evaluate(previous, 0, ChordType::Major7, same));
    assert(SmartHarmony::evaluate(previous, 7, ChordType::Dominant7, different));
    assert(same.motion == 0);
    assert(same.sharedTones == 4);
    assert(different.sharedTones > 0);
    HarmonySuggestion top[8]{};
    const auto n = SmartHarmony::top(previous, top, 8);
    assert(n == 8);
    for (unsigned i = 1; i < n; ++i) assert(top[i-1].score >= top[i].score);
    HarmonySuggestion top2[8]{};
    assert(SmartHarmony::top(previous, top2, 8) == 8);
    for (unsigned i = 0; i < n; ++i) {
        assert(top[i].root == top2[i].root && top[i].type == top2[i].type);
        assert(top[i].score == top2[i].score);
    }
    // No writes into caller's buffer when capacity is 0 or result is invalid.
    assert(SmartHarmony::top(previous, top, 0) == 0);
    HarmonyContext invalid = c;
    invalid.tonic = 12;
    assert(SmartHarmony::forEach(invalid, [](HarmonySuggestion) {}) == 0);
    invalid = c;
    invalid.scale = static_cast<ScaleMode>(99);
    assert(SmartHarmony::forEach(invalid, [](HarmonySuggestion) {}) == 0);

    // Check transposition invariance in every key for every strict suggestion.
    for (uint8_t key = 0; key < 12; ++key) {
        HarmonyContext ctx = c;
        ctx.tonic = key;
        const auto matches = SmartHarmony::forEach(ctx, [&](HarmonySuggestion s) {
            const uint16_t mask = SmartHarmony::voicingMask(s.root, s.type);
            assert((mask & ~SmartHarmony::scaleMask(key, ScaleMode::Major)) == 0);
        });
        assert(matches == count);
    }
    // Full matrix: every mode, tonic and type either passes its defined filter
    // or is excluded, and all 12 transpositions have equal candidate counts.
    for (unsigned mode = 0; mode < static_cast<unsigned>(ScaleMode::Count); ++mode) {
        size_t baseline = 0;
        for (uint8_t tonic = 0; tonic < 12; ++tonic) {
            HarmonyContext ctx = c;
            ctx.tonic = tonic;
            ctx.scale = static_cast<ScaleMode>(mode);
            auto total = SmartHarmony::forEach(ctx, [&](HarmonySuggestion s) {
                const uint16_t notes = SmartHarmony::voicingMask(s.root, s.type);
                assert((notes & ~SmartHarmony::scaleMask(tonic, ctx.scale)) == 0);
                assert(s.nonScaleTones == 0);
            });
            if (tonic == 0) baseline = total;
            else assert(total == baseline);
        }
    }
    // A caller must be able to request all options with bounded output storage.
    HarmonySuggestion topAll[SmartHarmony::kPossibilities]{};
    assert(SmartHarmony::top(all, topAll, SmartHarmony::kPossibilities) == SmartHarmony::kPossibilities);
    for (unsigned i = 1; i < SmartHarmony::kPossibilities; ++i)
        assert(topAll[i-1].score >= topAll[i].score);
    // Stress query determinism without modifying the MIDI engine.
    uint32_t rng = 0xA2F56021;
    for (unsigned i = 0; i < 20000; ++i) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        HarmonyContext ctx;
        ctx.tonic = static_cast<uint8_t>(rng % 12);
        ctx.scale = static_cast<ScaleMode>((rng >> 5) % static_cast<unsigned>(ScaleMode::Count));
        ctx.filter = static_cast<HarmonyFilter>((rng >> 11) % 3);
        ctx.hasPrevious = (rng & 1u) != 0;
        ctx.previousRoot = static_cast<uint8_t>((rng >> 13) % 12);
        ctx.previousType = static_cast<ChordType>((rng >> 17) % static_cast<unsigned>(ChordType::Count));
        HarmonySuggestion a[4]{}, b[4]{};
        const auto aa = SmartHarmony::top(ctx, a, 4);
        const auto bb = SmartHarmony::top(ctx, b, 4);
        assert(aa == bb);
        for (unsigned j = 0; j < aa; ++j) {
            assert(a[j].root == b[j].root && a[j].type == b[j].type && a[j].score == b[j].score);
            if (j) assert(a[j-1].score >= a[j].score);
        }
    }
    printf("PASS: smart harmony, %zu diatonic C-major options out of %u; fixed-memory enumeration/ranking\n",
           count, SmartHarmony::kPossibilities);
}
