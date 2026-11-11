#include "Mpk225UsbHardware.h"
#include "EmbeddedSmartChordController.h"
#include <stdint.h>

// Compile-only Cortex-M3 probe: force emission of both interrupt handling
// and 64-byte USB-MIDI submit logic without linking or touching a device.
static mpk225::Mpk225UsbHardware usbDevice;
static mpk225::EmbeddedSmartChordController chords;

extern "C" void mpk225_usb_arm_probe(const uint8_t* packet) {
    usbDevice.reset();
    usbDevice.irq();
    (void)usbDevice.submitMidiIn(packet,64);
    (void)usbDevice.submitHidIn();
    if (chords.consume({0xFF,100,60,0})) {
        const auto frame=chords.takeUsbBulkFrame();
        if (frame.length) (void)usbDevice.submitMidiIn(frame.bytes,frame.length);
    }
}
