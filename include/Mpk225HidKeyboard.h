#pragma once
// Standard USB HID boot-keyboard report and fixed-size key state.
// The Akai shared v1.02 application contains a matching 63-byte standard
// keyboard report descriptor, but the complete MPK225 HID interface and
// physical switch mapping still require hardware validation.
// No USB I/O, dynamic allocations, SysEx or firmware writing here.
#include <stdint.h>

namespace mpk225 {

// Standard HID keyboard usage page 0x07: 8 modifier bits, 1 reserved byte,
// up to 6 simultaneously held key usages. The five LED output bits follow
// the USB HID boot-keyboard convention.
inline constexpr uint8_t kHidKeyboardReportDescriptor[] = {
    0x05,0x01, 0x09,0x06, 0xA1,0x01,
    0x05,0x07, 0x19,0xE0, 0x29,0xE7,
    0x15,0x00, 0x25,0x01, 0x75,0x01, 0x95,0x08,
    0x81,0x02, 0x95,0x01, 0x75,0x08, 0x81,0x01,
    0x95,0x05, 0x75,0x01, 0x05,0x08, 0x19,0x01,
    0x29,0x05, 0x91,0x02, 0x95,0x01, 0x75,0x03,
    0x91,0x01, 0x95,0x06, 0x75,0x08, 0x15,0x00,
    0x25,0x65, 0x05,0x07, 0x19,0x00, 0x29,0x65,
    0x81,0x00, 0xC0
};
static_assert(sizeof(kHidKeyboardReportDescriptor)==63,
    "Boot keyboard descriptor must be 63 bytes");

struct HidKeyboardReport {
    uint8_t modifiers=0;
    uint8_t reserved=0;
    uint8_t keys[6]{};
};
static_assert(sizeof(HidKeyboardReport)==8, "USB HID boot report is 8 bytes");

class HidKeyboardState {
public:
    const HidKeyboardReport& report() const { return report_; }
    void clear() { report_=HidKeyboardReport{}; }

    // HID keyboard usages 0xE0..0xE7 map to eight modifier bits.
    // Nonmodifier usages 0x04..0x65 are supported; 0x01..0x03 are HID errors.
    // Ignore invalid/overflow key presses without changing held state.
    bool press(uint8_t usage) {
        if (usage>=0xE0 && usage<=0xE7) {
            report_.modifiers|=static_cast<uint8_t>(1u<<(usage-0xE0));
            return true;
        }
        if (usage<0x04 || usage>0x65) return false;
        for (uint8_t key:report_.keys) if (key==usage) return true;
        for (uint8_t& key:report_.keys)
            if (!key) { key=usage; return true; }
        return false;
    }

    bool release(uint8_t usage) {
        if (usage>=0xE0 && usage<=0xE7) {
            report_.modifiers&=static_cast<uint8_t>(~(1u<<(usage-0xE0)));
            return true;
        }
        for (unsigned i=0;i<6;++i) {
            if (report_.keys[i]!=usage) continue;
            for (unsigned j=i+1;j<6;++j) report_.keys[j-1]=report_.keys[j];
            report_.keys[5]=0;
            return true;
        }
        return false;
    }
private:
    HidKeyboardReport report_{};
};

// For HID idle rate zero, USB host expects a report only when changed.
// Nonzero idle is deliberately left eligible for retransmission until a
// verified device millisecond clock can implement exact four-ms intervals.
class HidReportTransmitGate {
public:
    void reset() { sent_=false; last_=HidKeyboardReport{}; }
    bool shouldSend(const HidKeyboardReport& now,uint8_t idleRate) const {
        if(!sent_ || idleRate!=0 || now.modifiers!=last_.modifiers ||
           now.reserved!=last_.reserved) return true;
        for(unsigned i=0;i<6;++i) if(now.keys[i]!=last_.keys[i]) return true;
        return false;
    }
    void markSent(const HidKeyboardReport& now) {
        last_=now;
        sent_=true;
    }
private:
    HidKeyboardReport last_{};
    bool sent_=false;
};

} // namespace mpk225
