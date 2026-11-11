#pragma once
// Provisional contact-time velocity for the Akai MPK225 keybed path.
// The v1.02 Akai UART parser assembles two bytes as a little-endian 16-bit
// elapsed-contact value and feeds it to a response-curve routine. OpenKlave's
// old MPK249 decoder ignored those bytes and used velocity 100 for every key.
// The precise factory response curve, tick units and preset coefficients
// have NOT been verified. This bounded inverse-time curve is not byte-identical
// to Akai's response and must be calibrated against a physical MPK225.
#include <stdint.h>

namespace mpk225 {

struct ContactVelocitySettings {
    uint16_t referenceTicks = 256;
    uint8_t referenceVelocity = 64;
    uint8_t unavailableVelocity = 100;
    // The Akai v1.02 routine 0x0800EB96 uses pitch-class mask 0xAB5 to
    // select separate white/black velocity parameters (0x080076EC).
    // The exact user-preset response coefficients still need derivation.
    uint16_t blackReferenceTicks = 256;
};

constexpr bool isWhitePianoKey(uint8_t midiNote) {
    constexpr uint16_t kWhiteKeyMask = 0xAB5u; // C D E F G A B
    return midiNote < 128 &&
           (kWhiteKeyMask & (1u << (midiNote % 12u))) != 0u;
}

constexpr uint16_t contactTicksFromWord(uint32_t raw) {
    return static_cast<uint16_t>(raw >> 16);
}

// Short contact separation means greater velocity. All valid timings yield
// MIDI 1..127; zero time is treated as unavailable (never divide by zero).
// Completely deterministic: no float, heap, dynamic state or calibration
// learning during performance. Parameters allow later hardware calibration.
constexpr uint8_t contactVelocity(uint16_t ticks,
                                  ContactVelocitySettings cfg = {}) {
    if (ticks == 0 || cfg.referenceTicks == 0 ||
        cfg.referenceVelocity == 0 || cfg.referenceVelocity > 127)
        return cfg.unavailableVelocity >= 1 && cfg.unavailableVelocity <= 127
                   ? cfg.unavailableVelocity : 100;
    const uint32_t numerator =
        static_cast<uint32_t>(cfg.referenceTicks) * cfg.referenceVelocity;
    const uint32_t v = (numerator + ticks / 2u) / ticks;
    return static_cast<uint8_t>(v < 1u ? 1u : (v > 127u ? 127u : v));
}

constexpr uint8_t contactVelocityFromWord(uint32_t raw,
                                          ContactVelocitySettings cfg = {}) {
    return contactVelocity(contactTicksFromWord(raw), cfg);
}

// Reproduces the factory WHITE/BLACK key class decision, not its full
// (preset-adjustable) piecewise velocity response. Factory C4 and D4 are
// "white"; C#4 and Eb4 use a separately calibratable interval coefficient.
constexpr uint8_t contactVelocityForNote(uint16_t ticks, uint8_t midiNote,
                                         ContactVelocitySettings cfg = {}) {
    if (midiNote > 127) return cfg.unavailableVelocity;
    if (!isWhitePianoKey(midiNote)) cfg.referenceTicks = cfg.blackReferenceTicks;
    return contactVelocity(ticks, cfg);
}

constexpr uint8_t contactVelocityFromWordForNote(uint32_t raw, uint8_t midiNote,
                                                 ContactVelocitySettings cfg = {}) {
    return contactVelocityForNote(contactTicksFromWord(raw), midiNote, cfg);
}

static_assert(contactVelocity(256) == 64, "Reference calibration");
static_assert(contactVelocity(128) == 127, "Fast contact saturation");
static_assert(contactVelocity(512) == 32, "Slower contact");
static_assert(contactVelocity(0) == 100, "Unknown timer fallback");
static_assert(isWhitePianoKey(60) && isWhitePianoKey(62) &&
              !isWhitePianoKey(61) && !isWhitePianoKey(63),
              "Akai 0xAB5 piano-key class detection");
static_assert(contactVelocityForNote(512, 60, {256,64,100,512}) == 32,
              "White-key calibration");
static_assert(contactVelocityForNote(512, 61, {256,64,100,512}) == 64,
              "Separate black-key calibration");

} // namespace mpk225
