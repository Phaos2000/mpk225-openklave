#include "Mpk225UsbDeviceState.h"
#include <cassert>
#include <cstdio>
using namespace mpk225;

static unsigned getDescriptor(UsbDeviceState& usb, UsbSetupRequest req, unsigned length) {
    assert(usb.setup(req));
    unsigned bytes=0,tx=0;
    while(usb.stage()==ControlStage::DataIn){
        const auto p=usb.nextIn();
        assert(p.valid && p.size<=8);
        assert(p.size==((length-bytes)>8?8:(length-bytes)));
        bytes+=p.size; ++tx;
        usb.inAcknowledged();
    }
    assert(bytes==length && usb.stage()==ControlStage::StatusOut);
    assert(usb.outReceived(nullptr,0));
    assert(usb.stage()==ControlStage::WaitSetup);
    return tx;
}
static void statusIn(UsbDeviceState& usb) {
    assert(usb.stage()==ControlStage::StatusIn);
    const auto p=usb.nextIn();
    assert(p.valid && p.size==0);
    usb.inAcknowledged();
    assert(usb.stage()==ControlStage::WaitSetup);
}
static uint8_t dataByte(UsbDeviceState& usb,UsbSetupRequest req) {
    assert(usb.setup(req));
    const auto p=usb.nextIn();
    assert(p.valid && p.size==1);
    usb.inAcknowledged();
    assert(usb.stage()==ControlStage::StatusOut);
    assert(usb.outReceived(nullptr,0));
    return p.bytes[0];
}

int main() {
    UsbDeviceState usb;
    assert(!usb.address() && !usb.configuration() && !usb.configured());
    const uint32_t initialEpoch=usb.configurationEpoch();
    assert(initialEpoch!=0);
    assert(getDescriptor(usb,{0x80,6,0x0100,0,18},18)==3);
    assert(getDescriptor(usb,{0x80,6,0x0200,0,169},169)==22);
    assert(getDescriptor(usb,{0x81,6,0x2200,2,63},63)==8);
    assert(getDescriptor(usb,{0x80,6,0x0302,0x0409,14},14)==2);
    // No-data control transfer: status is IN, regardless of request direction.
    assert(usb.setup({0x80,6,0x0100,0,0}));
    assert(usb.stage()==ControlStage::StatusIn);
    statusIn(usb);
    assert(!usb.nextIn().valid);
    // Unconfigured interfaces and nonzero endpoints are not yet accessible.
    assert(!usb.setup({0x81,10,0,2,1}));
    assert(!usb.setup({0x82,0,0,0x82,2}));
    assert(usb.setup({0x82,0,0,0x80,2}));
    auto ctrlZero=usb.nextIn(); assert(ctrlZero.valid && ctrlZero.size==2);
    usb.inAcknowledged(); assert(usb.outReceived(nullptr,0));
    assert(!usb.setup({0x82,0,0,0x03,2}));
    assert(!usb.setup({0x81,0,0,3,2}));
    assert(!usb.setup({0x80,0,0,1,2}));

    // SET_ADDRESS must update address only AFTER status IN is acknowledged.
    assert(usb.setup({0x00,5,17,0,0}));
    assert(usb.address()==0);
    statusIn(usb);assert(usb.address()==17);
    assert(dataByte(usb,{0x80,8,0,0,1})==0);
    assert(usb.setup({0x00,9,13,0,0}));
    assert(!usb.configured());
    statusIn(usb);assert(usb.configured());
    assert(usb.configurationEpoch()==initialEpoch+1);
    for(unsigned endpoint : {0x00u,0x80u,0x01u,0x82u,0x83u}) {
        assert(usb.setup({0x82,0,0,static_cast<uint16_t>(endpoint),2}));
        const auto status=usb.nextIn();
        assert(status.valid && status.size==2);
        usb.inAcknowledged();
        assert(usb.outReceived(nullptr,0));
    }
    assert(!usb.setup({0x82,0,0,0x03,2})); // listed HID OUT not operational
    assert(!usb.setup({0x82,0,0,0x84,2}));
    assert(!usb.setup({0x81,0,0,3,2}));
    assert(!usb.setup({0x80,0,0,1,2}));
    assert(!usb.setup({0x01,11,0,3,0}));
    // Re-applying configuration 13 must invalidate stale host MIDI too.
    assert(usb.setup({0x00,9,13,0,0}));
    statusIn(usb);
    assert(usb.configurationEpoch()==initialEpoch+2);
    assert(usb.setup({0x00,9,0,0,0}));
    statusIn(usb);assert(!usb.configured());
    assert(usb.configurationEpoch()==initialEpoch+3);
    assert(usb.setup({0x00,9,13,0,0}));
    statusIn(usb);assert(usb.configured());
    assert(usb.configurationEpoch()==initialEpoch+4);
    assert(dataByte(usb,{0x80,8,0,0,1})==13);
    assert(dataByte(usb,{0x81,10,0,2,1})==0);

    assert(usb.setup({0x21,10,0x0500,2,0}));
    statusIn(usb);assert(usb.idleRate()==5);
    assert(dataByte(usb,{0xA1,2,0,2,1})==5);
    assert(usb.setup({0x21,11,0,2,0}));
    statusIn(usb);assert(usb.protocol()==0);
    assert(dataByte(usb,{0xA1,3,0,2,1})==0);
    assert(usb.setup({0x21,11,1,2,0}));
    statusIn(usb);assert(usb.protocol()==1);
    assert(usb.setup({0x21,9,0x0200,2,1}));
    assert(usb.stage()==ControlStage::DataOut);
    const uint8_t led=0xA5;
    assert(usb.outReceived(&led,1));
    assert(usb.ledOutput()==0); // output is uncommitted until status IN
    statusIn(usb);
    assert(usb.ledOutput()==(led&31));
    assert(dataByte(usb,{0xA1,1,0x0200,2,1})==(led&31));
    // Aborting a pending SET_REPORT must preserve the previously stored LEDs.
    const uint8_t abandonedLed=0x03;
    assert(usb.setup({0x21,9,0x0200,2,1}));
    assert(usb.outReceived(&abandonedLed,1));
    assert(usb.ledOutput()==(led&31));
    assert(dataByte(usb,{0xA1,1,0x0200,2,1})==(led&31));
    assert(usb.ledOutput()==(led&31));
    assert(!usb.setup({0xA1,1,0x0201,2,1})); // unadvertised report ID
    assert(usb.stage()==ControlStage::Stall);

    assert(usb.hidKeys().press(4)); // A key
    assert(usb.setup({0xA1,1,0x0100,2,8}));
    auto report=usb.nextIn();
    assert(report.valid && report.size==8);
    assert(report.bytes[2]==4);
    usb.inAcknowledged();
    assert(usb.stage()==ControlStage::StatusOut);
    assert(usb.outReceived(nullptr,0));

    // 8-byte report when host requests more requires a terminating ZLP.
    assert(usb.setup({0xA1,1,0x0100,2,255}));
    report=usb.nextIn();assert(report.valid && report.size==8);
    usb.inAcknowledged();
    assert(usb.stage()==ControlStage::DataIn);
    report=usb.nextIn(); assert(report.valid && report.size==0);
    usb.inAcknowledged();
    assert(usb.stage()==ControlStage::StatusOut);
    assert(usb.outReceived(nullptr,0));

    assert(!usb.setup({0x00,9,5,0,0}));
    assert(usb.stage()==ControlStage::Stall);
    assert(usb.setup({0x80,6,0x0100,0,18})); // new SETUP cancels stall
    // Premature new SETUP aborts previous transfer cleanly.
    assert(usb.setup({0x00,5,27,0,0}));
    assert(usb.address()==17);
    statusIn(usb);assert(usb.address()==27);

    // All descriptor requested lengths (1..169) are serviced in EP0 8 B.
    for(unsigned n=1;n<=169;++n) {
        assert(getDescriptor(usb,{0x80,6,0x0200,0,static_cast<uint16_t>(n)},n)
               ==((n+7)/8));
    }
    const auto beforeReset=usb.configurationEpoch();
    usb.busReset();
    assert(usb.configurationEpoch()==beforeReset+1);
    assert(usb.address()==0 && usb.configuration()==0 && usb.idleRate()==0);
    assert(usb.protocol()==1 && usb.ledOutput()==0);
    assert(usb.hidKeys().report().keys[0]==0);
    std::puts("PASS: MPK225 composite EP0 state, 169 descriptor sizes, address/config handshake, HID requests and reset");
}
