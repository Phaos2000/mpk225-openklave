#include "Mpk225HidKeyboard.h"
#include <cassert>
#include <cstdio>
using namespace mpk225;
int main() {
    static_assert(sizeof(kHidKeyboardReportDescriptor)==63,"HID descriptor");
    HidKeyboardState state;
    HidReportTransmitGate gate;
    assert(sizeof(state.report())==8);
    assert(gate.shouldSend(state.report(),0,0));
    gate.markSent(state.report(),0);
    assert(!gate.shouldSend(state.report(),0,100000));
    assert(!gate.shouldSend(state.report(),4,15)); // 4*4ms
    assert(gate.shouldSend(state.report(),4,16));
    gate.markSent(state.report(),16);
    assert(!gate.shouldSend(state.report(),4,31));
    assert(gate.shouldSend(state.report(),4,32));
    assert(state.press(4));
    assert(gate.shouldSend(state.report(),0,33)); // changed, immediate
    gate.markSent(state.report(),33);
    assert(!gate.shouldSend(state.report(),0,49));
    assert(state.release(4));
    assert(gate.shouldSend(state.report(),0,49)); // release immediate
    gate.markSent(state.report(),49);
    gate.reset();
    assert(gate.shouldSend(state.report(),0,49)); // bus reset
    gate.markSent(state.report(),0xFFFFFFFEu);
    assert(!gate.shouldSend(state.report(),4,13)); // rollover after 15ms
    assert(gate.shouldSend(state.report(),4,14)); // rollover after 16ms
    gate.markSent(state.report(),14);
    assert(gate.shouldSend(state.report(),255,1034)); // max idle=1020ms
    for(unsigned i=0;i<8;++i) assert(state.press(static_cast<uint8_t>(0xE0+i)));
    assert(state.report().modifiers==0xFFu);
    assert(state.release(0xE2) && state.release(0xE7));
    assert(state.report().modifiers==0x7Bu);
    for(unsigned i=0;i<6;++i) assert(state.press(static_cast<uint8_t>(4+i)));
    assert(!state.press(10)); // 7th simultaneous key: fail without overflow
    assert(state.report().keys[0]==4 && state.report().keys[5]==9);
    assert(state.press(4)); // repeat is idempotent
    assert(state.release(6));
    assert(state.report().keys[2]==7 && state.report().keys[5]==0);
    assert(state.press(10));
    assert(state.report().keys[5]==10);
    assert(!state.press(0) && !state.press(1) && !state.press(2) && !state.press(3) && !state.press(0x66));
    assert(state.release(0xE0));
    state.clear();
    assert(state.report().modifiers==0);
    for(unsigned i=0;i<6;++i)assert(state.report().keys[i]==0);
    for(unsigned usage=4;usage<=101;++usage) {
        assert(state.press(static_cast<uint8_t>(usage)));
        assert(state.release(static_cast<uint8_t>(usage)));
    }
    std::puts("PASS: 63-byte HID report descriptor; modifiers, six-key rollover cap, release/clear and 98 valid key usages");
}
