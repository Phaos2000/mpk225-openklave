#pragma once
// Offline-testable MPK225 USB-MIDI OUT FIFO. Four factory cable numbers share
// the same 64-byte endpoint. Physical MIDI DIN/Remote mapping is UNVERIFIED.
#include "Mpk225UsbMidiCables.h"
#include <stdint.h>
namespace mpk225 {
struct UsbMidiOutEvent {
    uint8_t data[4]{};
    uint8_t cable() const { return usbMidiCable(data[0]); }
    uint8_t cin() const { return usbMidiCin(data[0]); }
    bool knownCable() const { return usbMidiKnownCable(data[0]); }
};
class Mpk225HostMidiQueue {
public:
    static constexpr unsigned kCapacity=64;
    unsigned size() const { return count_; }
    void clear() { head_=tail_=count_=0; }
    static constexpr bool fits(unsigned occupied,unsigned bytes) {
        return bytes<=64 && (bytes&3u)==0 && occupied<=kCapacity &&
               kCapacity-occupied>=bytes/4u;
    }
    // Capacity check is transfer-atomic; unadvertised cables are individually
    // discarded. Caller synchronizes with USB IRQ and controls EP1 NAK.
    bool appendFrame(const uint8_t* frame,unsigned bytes,unsigned& invalid) {
        invalid=0;
        if(!frame || !fits(count_,bytes)) return false;
        for(unsigned i=0;i<bytes;i+=4) {
            if(!usbMidiKnownCable(frame[i])) { ++invalid; continue; }
            UsbMidiOutEvent& packet=packets_[tail_];
            for(unsigned j=0;j<4;++j) packet.data[j]=frame[i+j];
            tail_=(tail_+1u)%kCapacity;
            ++count_;
        }
        return true;
    }
    bool pop(UsbMidiOutEvent& out) {
        if(!count_) return false;
        out=packets_[head_];
        head_=(head_+1u)%kCapacity;
        --count_;
        return true;
    }
private:
    UsbMidiOutEvent packets_[kCapacity]{};
    unsigned head_=0,tail_=0,count_=0;
};
static_assert(Mpk225HostMidiQueue::fits(48,64),"16 available event slots");
static_assert(!Mpk225HostMidiQueue::fits(49,64),"no FIFO overrun");
} // namespace mpk225
