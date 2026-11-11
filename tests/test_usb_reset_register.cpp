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
    // The exact same register reset is needed after SET_CONFIGURATION,
    // including repeat configuration 13 and a switch to configuration 0.
    // EP1: bulk RX VALID only for config 13. EP2/EP3: TX NAK.
    for(unsigned configured=0;configured<2;++configured) {
        const uint16_t desired[]={
            static_cast<uint16_t>((configured?0x3000u:0x2000u)|1u),
            0x0022u,0x0623u
        };
        for(unsigned endpoint=0;endpoint<3;++endpoint)
        for(unsigned ctr=0;ctr<4;++ctr)
        for(unsigned dtog=0;dtog<4;++dtog) {
            const uint16_t old=static_cast<uint16_t>(
                0x3030u | 0x0600u | (endpoint+1u) |
                ((ctr&1u)?0x8000u:0u) |
                ((ctr&2u)?0x0080u:0u) |
                ((dtog&1u)?0x4000u:0u) |
                ((dtog&2u)?0x0040u:0u));
            const uint16_t applied=epWrite(old,
                H::endpointResetWrite(old,desired[endpoint]));
            assert((applied&0x8080u)==0);
            assert((applied&0x4040u)==0);
            assert((applied&0x3030u)==(desired[endpoint]&0x3030u));
            assert((applied&0x070Fu)==(desired[endpoint]&0x070Fu));
        }
    }
    std::puts("PASS: USB bus reset and SET_CONFIGURATION clear endpoint completion/data-toggle state");
}
