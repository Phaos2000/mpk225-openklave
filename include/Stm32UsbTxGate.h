#pragma once
// STM32F103 single-buffered USB IN endpoint readiness test, pure and read-only.
// RM0008 USB_EPnR STAT_TX[1:0] is bits 5:4:
// 0=DISABLED, 1=STALL, 2=NAK (host has ACKed previous IN),
// 3=VALID (a prepared packet may still be in flight).
// This gate is necessary but not sufficient for correct USB servicing:
// CTR_TX clearing, interrupt ordering, enumeration and endpoint descriptors
// remain hardware validation work.
#include <stdint.h>

namespace mpk225 {
constexpr uint16_t kUsbStatTxMask = 0x0030u;
constexpr uint16_t kUsbStatTxNak  = 0x0020u;

constexpr bool usbTxBufferReusable(uint32_t epRegister) {
    return (epRegister & kUsbStatTxMask) == kUsbStatTxNak;
}
static_assert(usbTxBufferReusable(0x0020u), "NAK means ready to prepare IN buffer");
static_assert(!usbTxBufferReusable(0x0030u), "VALID may be in-flight");
static_assert(!usbTxBufferReusable(0x0000u), "DISABLED endpoint not ready");
static_assert(!usbTxBufferReusable(0x0010u), "STALL endpoint not ready");
} // namespace mpk225
