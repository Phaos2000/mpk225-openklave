#include "Mpk225UsbHardware.h"
#include <cassert>
#include <cstdio>
using namespace mpk225;
static_assert(Mpk225UsbHardware::kPmaEp0Rx==0x40,"PMA EP0 RX");
static_assert(Mpk225UsbHardware::kPmaEp0Tx==0x48,"PMA EP0 TX");
static_assert(Mpk225UsbHardware::kPmaEp1Rx==0x50,"PMA EP1 OUT");
static_assert(Mpk225UsbHardware::kPmaEp2Tx==0x90,"PMA EP2 IN");
static_assert(Mpk225UsbHardware::kPmaEp3Tx==0xD0,"PMA EP3 IN");
static_assert(Mpk225UsbHardware::kPmaEp3Tx+8<=512,"STM32 FS PMA bound");
int main(){
    // Compile-only probe: NEVER call reset/irq/submit on a host machine.
    Mpk225UsbHardware controller;
    assert(!controller.configured());
    assert(controller.queuedHostMidi()==0);
    assert(controller.errorCount()==0);
    UsbMidiOutEvent packet{};
    assert(!controller.popHostMidi(packet));
    // The controller state itself is safe to test without MMIO.
    assert(!controller.control().configured());
    // USB full-speed SOF is the 1ms source used for HID SET_IDLE; no host
    // MMIO accesses are performed by this compile-only test.
    std::puts("PASS: native USB driver builds on host; PMA layout stays inside 512 B");
}
