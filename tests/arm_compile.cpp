#include "MpkMidiPipeline.h"
#include "OpenKlaveEventQueue.h"
static mpk225::MpkMidiPipeline bridge;
extern "C" uint32_t mpk225_arm_smoke(uint32_t note) {
    bridge.setChordMode(true);
    bridge.consume({0xFF, 88, note, 0});
    const mpk225::UsbFrame f = bridge.takeUsbFrame();
    return f.length == 0 ? 0 : f.bytes[3];
}