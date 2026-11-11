#pragma once
// STM32F103 USB single-buffered EP1 IN scheduling (RM0008).
// This is pure endpoint-register logic, safe for host simulation.
// MMIO and hardware interrupt correctness still require physical validation.
#include <stdint.h>

namespace mpk225 {
constexpr uint32_t kUsbStatTxMask = 0x0030u;
constexpr uint32_t kUsbStatTxNak  = 0x0020u;
constexpr uint32_t kUsbCtrTx      = 0x0080u;
constexpr uint32_t kUsbCtrRx      = 0x8000u;
constexpr uint32_t kUsbEpReadWriteMask = 0x070Fu; // type, kind, address

// A transmission can start only if the endpoint is NAK (idle) AND no
// completed TX/RX interrupt remains unacknowledged. No PMA write in VALID.
constexpr bool usbTxBufferReusable(uint32_t epRegister) {
    return (epRegister & (kUsbStatTxMask | kUsbCtrTx | kUsbCtrRx)) ==
           kUsbStatTxNak;
}

// STM32 endpoint bits STAT_RX/TX and DTOG_RX/TX toggle on write-1, while
// CTR_RX/TX clear on write-0. Preserve EP type, kind, address and write
// zero to toggle fields and both completed-transfer indicators.
// Invoke only when servicing a real endpoint-1 CTR interrupt.
constexpr uint32_t usbEpCtrAcknowledgeWrite(uint32_t epRegister) {
    return epRegister & kUsbEpReadWriteMask;
}

// Program the next IN frame only after a new keyboard event or a completed
// prior transfer, not merely when a USB interrupt happens.
constexpr bool usbShouldSubmitFrame(uint32_t epRegister, unsigned queued) {
    return queued > 0 && usbTxBufferReusable(epRegister);
}

static_assert(usbTxBufferReusable(0x0020u), "NAK means ready");
static_assert(!usbTxBufferReusable(0x0030u), "VALID may be in flight");
static_assert(!usbTxBufferReusable(0x00A0u), "CTR_TX not yet acknowledged");
static_assert(!usbTxBufferReusable(0x8020u), "CTR_RX not yet acknowledged");
static_assert(usbEpCtrAcknowledgeWrite(0x86B1u)==0x0611u,
              "Endpoint write must clear CTR and leave toggles untouched");
} // namespace mpk225
