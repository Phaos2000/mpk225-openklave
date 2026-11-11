#pragma once
// MPK225 model-1 scan-code mapping reconstructed independently from the
// Akai MPK249/261 v1.02 SHARED application:
//   0x080171C4 model-specific key-index transform (variant=1)
//   0x0800D88C model-specific first MIDI note (variant=1 => 48)
//
// Offline disassembly evidence, NOT a factory MPK225 binary dump or a
// physical-device test. The incoming UART event word layout and FE/FF
// press/release convention are inherited from OpenKlave's MPK249 reader.
#include "KeybedReferenceDecoder.h"
#include <stdint.h>

namespace mpk225 {

constexpr KeybedScan decodeMpk225FactoryScan(uint32_t rawEventWord) {
    const uint8_t kind = static_cast<uint8_t>(rawEventWord & 0xffu);
    if (kind != 0xFFu && kind != 0xFEu) return {};
    const uint8_t raw = static_cast<uint8_t>((rawEventWord >> 8) & 0xffu);

    // Reverse-engineered model-1 path in Akai application:
    // bit 4 invalidates the scan; bit 3 is not used for key identity.
    if ((raw & 0x10u) != 0) return {};
    const int keyIndex = static_cast<int>((raw >> 5) * 8u + (raw & 7u)) - 4;
    if (keyIndex < 0 || keyIndex >= 25) return {};
    return {true, kind == 0xFFu, static_cast<uint8_t>(48 + keyIndex)};
}

} // namespace mpk225
