#include "Mpk225UsbHardware.h"
#include <cassert>
#include <cstdio>
using mpk225::Mpk225UsbHardware;

// STM32F103 EPnR write model: CTR clear-on-zero, DTOG and STAT
// toggle-on-one, endpoint address/type programmed directly.
constexpr uint16_t epWrite(uint16_t old,uint16_t w) {
    return static_cast<uint16_t>((old&w&0x8080u) |
        ((old^w)&0x7070u) | (w&0x070Fu));
}
int main() {
    using H=Mpk225UsbHardware;
    // Test every status pair, both data toggles, and both completion bits.
    for(unsigned rx=0;rx<4;++rx)
    for(unsigned tx=0;tx<4;++tx)
    for(unsigned ctr=0;ctr<4;++ctr)
    for(unsigned dtog=0;dtog<4;++dtog) {
        const uint16_t old=static_cast<uint16_t>(
            0x0603u|(rx<<12u)|(tx<<4u)|
            ((ctr&1u)?0x8000u:0u)|((ctr&2u)?0x80u:0u)|
            ((dtog&1u)?0x4000u:0u)|((dtog&2u)?0x40u:0u));
        const uint16_t desired=0x0200u|3u|(3u<<12u)|(2u<<4u);
        const uint16_t cmd=H::endpointResetWrite(old,desired);
        const uint16_t after=epWrite(old,cmd);
        assert((cmd&0x8080u)==0);
        assert((after&0x8080u)==0); // stale completions discarded
        assert((after&0x4040u)==0); // both toggles DATA0
        assert((after&0x3030u)==(desired&0x3030u));
        assert((after&0x070Fu)==(desired&0x070Fu));
    }
    std::puts("PASS: USB RESET clears stale CTR flags/DTOGs and selects new endpoint type/status");
}
