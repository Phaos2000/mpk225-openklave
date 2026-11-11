#include "Mpk225UsbHardware.h"
#include <cassert>
#include <cstdio>
using mpk225::Mpk225UsbHardware;

int main() {
    using U=Mpk225UsbHardware;
    static_assert(U::pendingHostMidiFits(63,4),
                  "One pending note-off fits exactly one free USB-MIDI slot");
    static_assert(!U::pendingHostMidiFits(64,4),
                  "Full FIFO must NAK without dropping the note-off");
    static_assert(U::pendingHostMidiFits(48,64),
                  "16 packets need precisely 16 free slots");
    static_assert(!U::pendingHostMidiFits(49,64),
                  "Never overrun the event FIFO by one packet");
    static_assert(U::pendingHostMidiFits(60,16),
                  "Four USB packets resume with four free slots");
    static_assert(!U::pendingHostMidiFits(61,16),
                  "Four packets cannot fit into three slots");
    static_assert(!U::pendingHostMidiFits(0,65),
                  "USB bulk OUT cannot exceed endpoint max packet");
    static_assert(!U::pendingHostMidiFits(0,6),
                  "Reject non-multiple-of-four USB-MIDI packets");
    static_assert(!U::pendingHostMidiFits(65,4),
                  "Reject corrupt FIFO occupancy");
    for(unsigned length=0;length<=64;length+=4)
        for(unsigned queued=0;queued<=64;++queued)
            assert(U::pendingHostMidiFits(queued,length)==
                   (queued+length/4<=64));
    std::puts("PASS: USB EP1 pending short and full 64-byte OUT packets resume without FIFO overrun");
}
