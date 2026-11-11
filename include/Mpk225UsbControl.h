#pragma once
// Read-only, allocation-free USB control-descriptor responder MODEL for MPK225.
// Sources: factory 1.02 USBTreeView descriptor dump for device/config/strings;
// 63-byte HID report is from standard boot-keyboard declaration in official
// Akai shared firmware, NOT retrievable by USBTreeView's Win32 API.
// This module has no USB peripheral operations and is not a flashable driver.
#include "Mpk225UsbDescriptors.h"
#include "Mpk225HidKeyboard.h"
#include <stdint.h>
#include <stddef.h>

namespace mpk225 {
inline constexpr uint8_t kFactoryString0[] = { 0x04, 0x03, 0x09, 0x04 };
inline constexpr uint8_t kFactoryString1[] = { 0x0A, 0x03, 0x41, 0x00, 0x6B, 0x00, 0x61, 0x00, 0x69, 0x00 };
inline constexpr uint8_t kFactoryString2[] = { 0x0E, 0x03, 0x4D, 0x00, 0x50, 0x00, 0x4B, 0x00, 0x32, 0x00, 0x32, 0x00, 0x35, 0x00 };
inline constexpr uint8_t kFactoryString3[] = { 0x14, 0x03, 0x49, 0x00, 0x6E, 0x00, 0x74, 0x00, 0x65, 0x00, 0x72, 0x00, 0x66, 0x00, 0x61, 0x00, 0x63, 0x00, 0x65, 0x00 };
inline constexpr uint8_t kFactoryString4[] = { 0x22, 0x03, 0x4E, 0x00, 0x4F, 0x00, 0x20, 0x00, 0x53, 0x00, 0x45, 0x00, 0x52, 0x00, 0x49, 0x00, 0x41, 0x00, 0x4C, 0x00, 0x20, 0x00, 0x4E, 0x00, 0x55, 0x00, 0x4D, 0x00, 0x42, 0x00, 0x45, 0x00, 0x52, 0x00 };
inline constexpr uint8_t kFactoryString6[] = { 0x0E, 0x03, 0x50, 0x00, 0x6F, 0x00, 0x72, 0x00, 0x74, 0x00, 0x20, 0x00, 0x41, 0x00 };
inline constexpr uint8_t kFactoryString7[] = { 0x0E, 0x03, 0x50, 0x00, 0x6F, 0x00, 0x72, 0x00, 0x74, 0x00, 0x20, 0x00, 0x42, 0x00 };
inline constexpr uint8_t kFactoryString8[] = { 0x0A, 0x03, 0x4D, 0x00, 0x49, 0x00, 0x44, 0x00, 0x49, 0x00 };
inline constexpr uint8_t kFactoryString9[] = { 0x0E, 0x03, 0x52, 0x00, 0x65, 0x00, 0x6D, 0x00, 0x6F, 0x00, 0x74, 0x00, 0x65, 0x00 };
inline constexpr uint8_t kFactoryString13[] = { 0x0E, 0x03, 0x50, 0x00, 0x6F, 0x00, 0x72, 0x00, 0x74, 0x00, 0x20, 0x00, 0x41, 0x00 };
inline constexpr uint8_t kFactoryString14[] = { 0x0E, 0x03, 0x50, 0x00, 0x6F, 0x00, 0x72, 0x00, 0x74, 0x00, 0x20, 0x00, 0x42, 0x00 };
inline constexpr uint8_t kFactoryString15[] = { 0x0A, 0x03, 0x4D, 0x00, 0x49, 0x00, 0x44, 0x00, 0x49, 0x00 };
inline constexpr uint8_t kFactoryString16[] = { 0x0E, 0x03, 0x52, 0x00, 0x65, 0x00, 0x6D, 0x00, 0x6F, 0x00, 0x74, 0x00, 0x65, 0x00 };

inline constexpr uint8_t kFactoryHidDescriptor[] = {
    0x09,0x21,0x10,0x00,0x00,0x01,0x22,0x3F,0x00
};
struct UsbSetupRequest {
    uint8_t bmRequestType=0;
    uint8_t bRequest=0;
    uint16_t wValue=0;
    uint16_t wIndex=0;
    uint16_t wLength=0;
};
struct UsbDescriptorView {
    const uint8_t* bytes=nullptr;
    uint16_t count=0;
};
struct UsbEp0Packet {
    uint8_t bytes[8]{};
    uint8_t count=0;
};
constexpr UsbDescriptorView factoryGetDescriptor(const UsbSetupRequest& req) {
    // Only standard USB GET_DESCRIPTOR transfers are served.
    if (req.bRequest!=6) return {};
    const uint8_t type=static_cast<uint8_t>(req.wValue>>8);
    const uint8_t index=static_cast<uint8_t>(req.wValue);
    const uint8_t* p=nullptr;
    size_t n=0;
    if (req.bmRequestType==0x80) {
        if (type==1 && index==0 && req.wIndex==0) {
            p=kFactoryUsbDeviceDescriptor;
            n=sizeof(kFactoryUsbDeviceDescriptor);
        } else if (type==2 && index==0 && req.wIndex==0) {
            p=kFactoryUsbConfigDescriptor;
            n=sizeof(kFactoryUsbConfigDescriptor);
        } else if (type==3 && (req.wIndex==0 || req.wIndex==0x0409)) {
            switch (index) {
            case 0: p=kFactoryString0; n=sizeof(kFactoryString0); break;
            case 1: p=kFactoryString1; n=sizeof(kFactoryString1); break;
            case 2: p=kFactoryString2; n=sizeof(kFactoryString2); break;
            case 3: p=kFactoryString3; n=sizeof(kFactoryString3); break;
            case 4: p=kFactoryString4; n=sizeof(kFactoryString4); break;
            case 6: p=kFactoryString6; n=sizeof(kFactoryString6); break;
            case 7: p=kFactoryString7; n=sizeof(kFactoryString7); break;
            case 8: p=kFactoryString8; n=sizeof(kFactoryString8); break;
            case 9: p=kFactoryString9; n=sizeof(kFactoryString9); break;
            case 13: p=kFactoryString13; n=sizeof(kFactoryString13); break;
            case 14: p=kFactoryString14; n=sizeof(kFactoryString14); break;
            case 15: p=kFactoryString15; n=sizeof(kFactoryString15); break;
            case 16: p=kFactoryString16; n=sizeof(kFactoryString16); break;
                default: break;
            }
        }
    } else if (req.bmRequestType==0x81 && req.wIndex==2 && index==0) {
        if (type==0x21) {
            p=kFactoryHidDescriptor;
            n=sizeof(kFactoryHidDescriptor);
        } else if (type==0x22) {
            p=kHidKeyboardReportDescriptor;
            n=sizeof(kHidKeyboardReportDescriptor);
        }
    }
    if (!p) return {};
    return {p,static_cast<uint16_t>(n<req.wLength?n:req.wLength)};
}
// Each USB0 control IN packet is 8 bytes max (factory bMaxPacketSize0=8).
// Caller handles OUT status/SET_ADDRESS/SET_CONFIGURATION independently.
constexpr UsbEp0Packet factoryDescriptorPacket(const UsbDescriptorView& desc,
                                                uint16_t offset) {
    UsbEp0Packet packet{};
    if (!desc.bytes || offset>=desc.count) return packet;
    for (unsigned i=0;i<8 && offset+i<desc.count;++i)
        packet.bytes[packet.count++]=desc.bytes[offset+i];
    return packet;
}
static_assert(sizeof(kFactoryHidDescriptor)==9, "HID descriptor size");
static_assert(factoryGetDescriptor({0x80,6,0x0100,0,255}).count==18,
              "Factory device descriptor");
static_assert(factoryGetDescriptor({0x80,6,0x0200,0,255}).count==169,
              "Factory configuration descriptor");
static_assert(factoryGetDescriptor({0x81,6,0x2200,2,255}).count==63,
              "Factory HID report descriptor length");
} // namespace mpk225
