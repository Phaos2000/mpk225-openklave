#pragma once
// This is a GUARDED REFERENCE DECODER for OpenKlave's MPK249 USART/DMA
// keybed format. It is NOT an MPK225 hardware mapping. All unrecognized
// scan indices and event types fail closed instead of producing MIDI note 0.
#include <stdint.h>

namespace mpk225 {

struct KeybedScan {
    bool valid = false;
    bool down = false;
    uint8_t midiNote = 0;
};

struct LegacyKeyRange {
    uint8_t firstCode;
    uint8_t firstNote;
    uint8_t count;
};

// Compact transcription of the 49 nonzero table entries in OpenKlave
// src/kernel.c::decodeMPK249NoteValue (2021 revision 44a10eae...).
// OpenKlave's last range repeats 78 and 79; this is preserved as evidence,
// not silently "fixed" into a fabricated MPK225 map.
inline constexpr LegacyKeyRange kMpk249Ranges[] = {
    {8, 56, 8}, {36, 36, 4}, {40, 64, 8}, {64, 40, 8},
    {72, 72, 8}, {96, 48, 8}, {104, 78, 5},
};

constexpr KeybedScan decodeMpk249ReferenceScan(uint32_t raw) {
    const uint8_t action = static_cast<uint8_t>(raw & 0xFFu);
    if (action != 0xFFu && action != 0xFEu) return {};
    const unsigned code = (raw >> 8) & 0xFFu;
    for (const auto& range : kMpk249Ranges) {
        if (code >= range.firstCode && code < range.firstCode + range.count)
            return {true, action == 0xFFu,
                    static_cast<uint8_t>(range.firstNote + code - range.firstCode)};
    }
    return {};
}

} // namespace mpk225
