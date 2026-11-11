#include "Mpk225UsbHardware.h"
#include <cassert>
#include <cstdio>

// STM32F103 EPnR: STAT/DTOG are write-one-to-toggle; CTR_RX/CTR_TX
// are write-zero-to-clear. Simulate the register write without MMIO.
constexpr uint16_t applyEpWrite(uint16_t before,uint16_t write) {
    return static_cast<uint16_t>(
        (before & write & 0x8080u) |
        ((before ^ write) & 0x3030u) |
        (before & 0x4040u) |
        (write & 0x070Fu));
}

int main() {
    using mpk225::Mpk225UsbHardware;
    // Exercise all STAT transitions while both CTR flags are pending,
    // and with only one or neither pending. Include both DTOG bits.
    for(unsigned oldRx=0;oldRx<4;++oldRx)
    for(unsigned oldTx=0;oldTx<4;++oldTx)
    for(unsigned newRx=0;newRx<4;++newRx)
    for(unsigned newTx=0;newTx<4;++newTx)
    for(unsigned ctr=0;ctr<4;++ctr) {
        const uint16_t pending=static_cast<uint16_t>(
            ((ctr&1u)?0x8000u:0u) | ((ctr&2u)?0x0080u:0u));
        const uint16_t old=static_cast<uint16_t>(
            pending | 0x4642u | (oldRx<<12u) | (oldTx<<4u));
        const uint16_t command=Mpk225UsbHardware::endpointStatusWrite(
            old,newRx,newTx);
        const uint16_t after=applyEpWrite(old,command);
        assert((command&0x8080u)==pending);
        assert((after&0x8080u)==pending);
        assert((after&0x3030u)==((newRx<<12u)|(newTx<<4u)));
        assert((after&0x4040u)==(old&0x4040u));
        assert((after&0x070Fu)==(old&0x070Fu));
    }
    std::puts("PASS: STM32 USB EP status updates preserve pending CTR flags and DTOG bits");
}
