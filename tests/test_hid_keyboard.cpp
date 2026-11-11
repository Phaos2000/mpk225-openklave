#include "Mpk225HidKeyboard.h"
#include <cassert>
#include <cstdio>
using namespace mpk225;
int main() {
    static_assert(sizeof(kHidKeyboardReportDescriptor)==63,"HID descriptor");
    HidKeyboardState state;
    assert(sizeof(state.report())==8);
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
    assert(!state.press(0) && !state.press(0x66));
    assert(state.release(0xE0));
    state.clear();
    assert(state.report().modifiers==0);
    for(unsigned i=0;i<6;++i)assert(state.report().keys[i]==0);
    for(unsigned usage=1;usage<=101;++usage) {
        assert(state.press(static_cast<uint8_t>(usage)));
        assert(state.release(static_cast<uint8_t>(usage)));
    }
    std::puts("PASS: 63-byte HID report descriptor; modifiers, six-key rollover cap, release/clear and 101 standard keys");
}
