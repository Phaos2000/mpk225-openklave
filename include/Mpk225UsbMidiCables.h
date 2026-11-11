#pragma once
// MPK225 factory USB MIDIStreaming descriptor advertises four logical
// USB-MIDI cable numbers on the SAME 64-byte bulk endpoints. These are
// host-visible jack slots, not evidence of physical UART or Remote wiring.
// Pure MIDI packet helpers: no peripheral, SysEx updater, or flash access.
#include <stddef.h>
#include <stdint.h>

namespace mpk225 {
inline constexpr uint8_t kMpk225UsbCableCount=4;
enum class Mpk225UsbCable : uint8_t {
    PortA=0, PortB=1, Midi=2, Remote=3
};

constexpr uint8_t usbMidiCable(uint8_t cableCin) {
    return static_cast<uint8_t>(cableCin>>4);
}
constexpr uint8_t usbMidiCin(uint8_t cableCin) {
    return static_cast<uint8_t>(cableCin&15u);
}
constexpr bool usbMidiKnownCable(uint8_t cableCin) {
    return usbMidiCable(cableCin)<kMpk225UsbCableCount;
}
// No MIDI-content assumption: CIN 2..7 (system/SysEx) must remain possible.
// Validate only the descriptor-backed cable numbers and four-byte framing.
inline bool usbMidiBulkHasKnownCables(const uint8_t* data,size_t bytes) {
    if(!data || bytes==0 || bytes>64 || (bytes&3u)) return false;
    for(size_t i=0;i<bytes;i+=4)
        if(!usbMidiKnownCable(data[i])) return false;
    return true;
}
static_assert(usbMidiCable(0x0Eu)==0,"Port A packet");
static_assert(usbMidiCable(0x1Eu)==1,"Port B packet");
static_assert(usbMidiCable(0x2Eu)==2,"MIDI packet");
static_assert(usbMidiCable(0x3Eu)==3,"Remote packet");
static_assert(!usbMidiKnownCable(0x4Eu),"Fifth cable is not in factory descriptor");
} // namespace mpk225
