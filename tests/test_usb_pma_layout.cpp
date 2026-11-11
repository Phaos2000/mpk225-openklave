#include "Mpk225UsbHardware.h"
#include <cassert>
#include <cstdio>
using mpk225::Mpk225UsbHardware;

// STM32F103 PMA byte offsets and COUNT_RX encoding, no hardware access.
constexpr unsigned decodeRxCapacity(unsigned count) {
    return count & 0x8000u
        ? ((count>>10u)&31u)*32u
        : ((count>>10u)&31u)*2u;
}
int main() {
    using U=Mpk225UsbHardware;
    static_assert(U::kPmaRx64Count==0x8800u,"two 32-byte blocks");
    static_assert(decodeRxCapacity(U::kPmaRx64Count)==64u,"RX capacity");
    static_assert(decodeRxCapacity(0x8400u)==32u,"regression: old encoding wrong");
    static_assert(U::kPmaEp0Rx>=0x40u,"BTABLE occupies 0x00..0x3f");
    static_assert(U::kPmaEp0Rx+8u<=U::kPmaEp0Tx,"EP0 buffers must not overlap");
    static_assert(U::kPmaEp0Tx+8u<=U::kPmaEp1Rx,"EP0/EP1 overlap");
    static_assert(U::kPmaEp1Rx+64u<=U::kPmaEp2Tx,"EP1/EP2 overlap");
    static_assert(U::kPmaEp2Tx+64u<=U::kPmaEp3Tx,"EP2/EP3 overlap");
    static_assert(U::kPmaEp3Tx+8u<=0x200u,"STM32F103 512-byte PMA");
    assert(decodeRxCapacity(U::kPmaRx64Count)==64u);
    std::puts("PASS: MPK225 64-byte EP1 bulk OUT PMA encoded as 0x8800; all EP0/EP1/EP2/EP3 buffer regions disjoint");
}
