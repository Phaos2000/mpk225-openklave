#pragma once
// Experimental native STM32F103 USB full-speed composite peripheral driver.
// EP0: control (8 B); EP1 OUT: 64 B USB MIDI bulk; EP2 IN: 64 B MIDI bulk;
// EP3 IN: 8 B interrupt HID keyboard. Derived from user-measured MPK225 v1.02
// descriptors. No firmware flashing. Hardware behavior NOT VERIFIED.
// MPK225 supports USB config value 13; EP3 OUT is advertised by factory but
// bNumEndpoints=1 and Windows has no open pipe, so is intentionally unused.
#include "Mpk225UsbDeviceState.h"
#include "Mpk225UsbDescriptors.h"
#include "Mpk225UsbMidiCables.h"
#include "Mpk225HostMidiQueue.h"
#include <stdint.h>

namespace mpk225 {

class Mpk225UsbHardware {
public:
    // EPnR STAT bits toggle on write-one, while CTR_RX/CTR_TX clear on
    // write-zero. Keep pending transfer flags when changing RX/TX status;
    // only explicit ackEndpoint() may acknowledge those transfers.
    // Pure calculation: host tests can exercise it without touching MMIO.
    static constexpr uint16_t endpointStatusWrite(uint16_t old,
                                                    unsigned rx,unsigned tx) {
        return static_cast<uint16_t>(
            (old&0x070Fu) |
            ((old^static_cast<uint16_t>((rx<<12u)|(tx<<4u)))&0x3030u) |
            (old&0x8080u));
    }
    // A fresh EP0 SETUP supersedes any older IN completion already latched.
    // That old CTR_TX still needs acknowledging, but must not advance the
    // newly started descriptor/status stage.
    static constexpr bool ep0SetupAbortsTx(unsigned endpoint,uint16_t flags) {
        return endpoint==0 && (flags&0x8880u)==0x8880u;
    }
    // Reset-only EPnR write. CTR bits are write-zero-to-clear, and
    // DTOG bits are write-one-to-toggle; unlike ordinary status changes
    // neither old CTR flags nor old DATA1 toggles may survive a bus reset.
    static constexpr uint16_t endpointResetWrite(uint16_t old,
                                                  uint16_t desired) {
        return static_cast<uint16_t>((desired&0x070Fu) |
            ((old^desired)&0x3030u) | (old&0x4040u));
    }
    static constexpr uint16_t kPmaEp0Rx=0x40, kPmaEp0Tx=0x48;
    static constexpr uint16_t kPmaEp1Rx=0x50, kPmaEp2Tx=0x90;
    static constexpr uint16_t kPmaEp3Tx=0xD0;
    // STM32F1 USB COUNT_RX encodes 32-byte block count in bits 14:10
    // when BLSIZE (bit 15) is set. 0x8400=32B; 0x8800=64B.
    static constexpr uint16_t kPmaRx64Count=0x8800;
    static_assert(((kPmaRx64Count>>10u)&31u)*32u==64u,
                  "EP1 OUT must allocate a real 64-byte PMA receive buffer");
    static_assert(kPmaEp3Tx+8<=0x200,"PMA exceeds STM32 FS 512-byte allocation");
    bool configured() const { return control_.configured(); }
    uint32_t configurationEpoch() const { return control_.configurationEpoch(); }
    bool readyForMidiIn() const {
        return configured() && ((ep(2)&0x80B0u)==0x0020u);
    }
    uint32_t errorCount() const { return errors_; }
    unsigned queuedHostMidi() const { return hostMidi_.size(); }
    // When EP1 has an already-ACKed USB OUT packet held in PMA, restore
    // RX VALID as soon as that *specific* packet fits. Requiring 16 slots
    // for a short transfer can unnecessarily hold a note-off under NAK.
    static constexpr bool pendingHostMidiFits(unsigned queued,
                                               unsigned packetBytes) {
        return Mpk225HostMidiQueue::fits(queued,packetBytes);
    }
    // Examine before delivering. No MPK225 DIN/Remote destination is mapped
    // until independently verified; retain undeliverable packets and NAK
    // upstream instead of silently discarding them. Caller masks USB IRQ
    // throughout peek, hardware accept, and pop.
    bool peekHostMidi(UsbMidiOutEvent& out) const {
        return hostMidi_.peek(out);
    }
    // Select only the explicitly verified physical destination cable.
    // An unsupported Port A/B/Remote event is never dropped to unstick DIN.
    bool peekHostMidiCable(uint8_t cable,UsbMidiOutEvent& out) const {
        return hostMidi_.peekCable(cable,out);
    }
    bool popHostMidiCable(uint8_t cable,UsbMidiOutEvent& out) {
        if(!hostMidi_.popCable(cable,out)) return false;
        if(ep1Pending_ && pendingHostMidiFits(hostMidi_.size(),bt(1,3)&0x3FFu))
            serviceRxPending();
        return true;
    }
    // Complete an accepted incoming four-byte MIDI event.
    bool popHostMidi(UsbMidiOutEvent& out) {
        if(!hostMidi_.pop(out)) return false;
        // The caller must mask the USB IRQ before touching this FIFO.
        if(ep1Pending_ && pendingHostMidiFits(hostMidi_.size(),bt(1,3)&0x3FFu))
            serviceRxPending();
        return true;
    }
    bool submitMidiIn(const uint8_t* data,uint8_t length) {
        // Reject invalid cable numbers before touching PMA or dequeueing any
        // producer-owned bytes. Actual outgoing keyboard traffic uses Port A.
        if(!usbMidiBulkHasKnownCables(data,length) || !readyForMidiIn())
            return false;
        pmaWrite(kPmaEp2Tx,data,length);
        btWrite(2,1,length);
        setEndpoint(2,0,3); // BULK EP2: RX DISABLED, TX VALID
        return true;
    }
    bool submitHidIn() {
        if(!configured() || (ep(3)&0x80B0u)!=0x0020u) return false;
        const auto& report=control_.hidKeys().report();
        if(!hidGate_.shouldSend(report,control_.idleRate(),usbMillis_))
            return false;
        uint8_t bytes[8]{report.modifiers,report.reserved,0,0,0,0,0,0};
        for(unsigned i=0;i<6;++i) bytes[i+2]=report.keys[i];
        pmaWrite(kPmaEp3Tx,bytes,8);
        btWrite(3,1,8);
        setEndpoint(3,0,3); // HID interrupt endpoint IN 0x83
        hidGate_.markSent(report,usbMillis_);
        return true;
    }
    UsbDeviceState& control() { return control_; }

    // Called by the STM32 USB RESET interrupt; no SysEx or flash operations.
    void reset() {
        control_.busReset();
        hidGate_.reset();
        usbMillis_=0;
        errors_=0;
        hostMidi_.clear();
        ep1Pending_=false;
        endpointsEnabled_=false;
        reg(0x44)=static_cast<uint16_t>(~0x0400u); // acknowledge RESET
        reg(0x50)=0; // BTABLE starts at 0x40006000
        reg(0x4C)=0x0080; // EF, address zero
        btWrite(0,0,kPmaEp0Tx);
        btWrite(0,1,0);
        btWrite(0,2,kPmaEp0Rx);
        btWrite(0,3,0x1000); // 8-byte EP0 RX PMA allocation
        btWrite(1,0,0);
        btWrite(1,1,0);
        btWrite(1,2,kPmaEp1Rx);
        btWrite(1,3,kPmaRx64Count); // 64-byte EP1 bulk OUT, two 32-byte blocks
        btWrite(2,0,kPmaEp2Tx);
        btWrite(2,1,0);
        btWrite(2,2,0);
        btWrite(2,3,0);
        btWrite(3,0,kPmaEp3Tx);
        btWrite(3,1,0);
        btWrite(3,2,0);
        btWrite(3,3,0);
        configureEp(0,0x0200,3,2); // control EP0 RX VALID / TX NAK
        configureEp(1,0x0000,2,0); // bulk EP1 OUT NAK until configured
        configureEp(2,0x0000,0,2); // bulk EP2 IN NAK
        configureEp(3,0x0600,0,2); // interrupt EP3 IN NAK
        reg(0x40)=0x8600; // CTRM + RESETM + SOFM, HID idle clock (1ms)
    }

    // Interrupt handler. Called only from the USB_LP_CAN1_RX0_IRQHandler.
    // A bounded loop handles simultaneous RX and TX completions.
    void irq() {
        for(unsigned iteration=0;iteration<16;++iteration) {
            const uint16_t pending=reg(0x44);
            if(pending&0x0400u){reset();continue;}
            // RM0008 USB_ISTR SOF bit 9 and USB_CNTR SOFM bit 9.
            // Acknowledge only SOF; never clear concurrent CTR/RESET flags.
            // SOF is delivered by the host every 1ms while the bus is active.
            if(pending&0x0200u) {
                ++usbMillis_;
                reg(0x44)=static_cast<uint16_t>(~0x0200u);
                continue;
            }
            if(!(pending&0x8000u)) break;
            const unsigned number=pending&0x000Fu;
            if(number>3) { ++errors_; break; }
            const uint16_t flags=ep(number);
            // Discard the old EP0 IN completion BEFORE arming the response
            // to a new SETUP, so it cannot complete that new transfer early.
            const bool abortOldTx=ep0SetupAbortsTx(number,flags);
            if(abortOldTx) ackEndpoint(0,false,true);
            if(flags&0x8000u) {
                const bool setup=(number==0) && ((flags&0x0800u)!=0);
                ackEndpoint(number,true,false);
                if(number==0) controlOut(setup);
                else if(number==1) hostMidiReceived();
                else ++errors_;
            }
            if((flags&0x0080u) && !abortOldTx) {
                ackEndpoint(number,false,true);
                if(number==0) controlTxCompleted();
                // EP2/EP3 are now NAK; foreground may schedule next message.
            }
        }
    }

private:
    static volatile uint16_t& reg(unsigned offset){
        return *reinterpret_cast<volatile uint16_t*>(0x40005C00u+offset);
    }
    static volatile uint16_t& ep(unsigned epNumber) {
        return reg(epNumber*4u);
    }
    static volatile uint16_t& bt(unsigned endpoint,unsigned field) {
        // PMA BTABLE: four 16-bit fields, word-spaced (4-byte CPU stride).
        return *reinterpret_cast<volatile uint16_t*>(
            0x40006000u + endpoint*16u + field*4u);
    }
    static void btWrite(unsigned endpoint,unsigned field,uint16_t value) {
        bt(endpoint,field)=value;
    }
    static void pmaWrite(uint16_t address,const uint8_t* bytes,unsigned n) {
        for(unsigned i=0;i<n;i+=2) {
            const uint16_t word=static_cast<uint16_t>(bytes[i] |
                    (i+1<n?static_cast<uint16_t>(bytes[i+1])<<8:0));
            *reinterpret_cast<volatile uint16_t*>(
                0x40006000u+address*2u+i*2u)=word;
        }
    }
    static void pmaRead(uint16_t address,uint8_t* bytes,unsigned n) {
        for(unsigned i=0;i<n;i+=2) {
            const uint16_t word=*reinterpret_cast<volatile uint16_t*>(
                0x40006000u+address*2u+i*2u);
            bytes[i]=static_cast<uint8_t>(word);
            if(i+1<n) bytes[i+1]=static_cast<uint8_t>(word>>8);
        }
    }
    static void configureEp(unsigned number,uint16_t type,
                             unsigned rxStatus,unsigned txStatus) {
        // Called on USB bus reset and SET_CONFIGURATION: discard stale
        // completion flags and force both data toggles to DATA0.
        const uint16_t old=ep(number);
        const uint16_t desired=static_cast<uint16_t>(type | number |
                                  (rxStatus<<12u) | (txStatus<<4u));
        ep(number)=endpointResetWrite(old,desired);
    }
    static void setEndpoint(unsigned number,unsigned rx,unsigned tx) {
        const uint16_t old=ep(number);
        ep(number)=endpointStatusWrite(old,rx,tx);
    }
    static void ackEndpoint(unsigned number,bool ackRx,bool ackTx) {
        const uint16_t old=ep(number);
        ep(number)=static_cast<uint16_t>((old&0x070Fu) |
                    (ackRx?0:(old&0x8000u)) |
                    (ackTx?0:(old&0x0080u)));
    }
    void stallControl() { setEndpoint(0,3,1); }
    void armControlNext() {
        const auto p=control_.nextIn();
        if(!p.valid) { setEndpoint(0,3,2); return; }
        pmaWrite(kPmaEp0Tx,p.bytes,p.size);
        btWrite(0,1,p.size);
        setEndpoint(0,3,3);
    }
    void controlOut(bool setupToken) {
        uint8_t bytes[8]{};
        const uint16_t count=static_cast<uint16_t>(bt(0,3)&0x3FFu);
        if(count>8) { ++errors_;stallControl();return; }
        pmaRead(kPmaEp0Rx,bytes,count);
        if(setupToken) {
            if(count!=8) { ++errors_;stallControl();return; }
            UsbSetupRequest req{};
            req.bmRequestType=bytes[0];
            req.bRequest=bytes[1];
            req.wValue=static_cast<uint16_t>(bytes[2]|(bytes[3]<<8));
            req.wIndex=static_cast<uint16_t>(bytes[4]|(bytes[5]<<8));
            req.wLength=static_cast<uint16_t>(bytes[6]|(bytes[7]<<8));
            if(!control_.setup(req)){stallControl();return;}
            if(control_.stage()==ControlStage::DataIn ||
               control_.stage()==ControlStage::StatusIn) armControlNext();
            else setEndpoint(0,3,2); // HID SET_REPORT OUT data stage
        } else {
            if(!control_.outReceived(bytes,static_cast<uint8_t>(count))) {
                ++errors_;stallControl();return;
            }
            if(control_.stage()==ControlStage::StatusIn) armControlNext();
            else setEndpoint(0,3,2);
        }
    }
    void controlTxCompleted() {
        const uint32_t wasEpoch=control_.configurationEpoch();
        control_.inAcknowledged();
        if(control_.stage()==ControlStage::DataIn)
            armControlNext();
        else setEndpoint(0,3,2);
        // Address must be committed only after SET_ADDRESS status IN ACK.
        reg(0x4C)=static_cast<uint16_t>(0x80u|control_.address());
        if(control_.configurationEpoch()!=wasEpoch) {
            // SET_CONFIGURATION (including a repeated value) resets all
            // non-control endpoint traffic, not just their readiness.
            hostMidi_.clear();
            hidGate_.reset();
            ep1Pending_=false;
            // The host restarts bulk/interrupt DATA0 after SET_CONFIGURATION.
            // A STAT-only transition would retain stale DTOG and CTR bits,
            // potentially rejecting the next note-off after reconnect.
            // Drop incomplete TX packets and restore the 64-byte RX size.
            btWrite(1,3,kPmaRx64Count);
            btWrite(2,1,0);
            btWrite(3,1,0);
            configureEp(1,0x0000,control_.configured()?3:2,0);
            configureEp(2,0x0000,0,2);
            configureEp(3,0x0600,0,2);
            endpointsEnabled_=control_.configured();
        }
    }
    void hostMidiReceived() {
        const unsigned length=bt(1,3)&0x3FFu;
        if(length>64 || (length&3u)) {
            ++errors_;
            setEndpoint(1,3,0);
            return;
        }
        // Do not overwrite PMA if the host temporarily outruns our FIFO.
        if(!pendingHostMidiFits(hostMidi_.size(),length)){
            ep1Pending_=true;
            setEndpoint(1,2,0); // NAK until there is room
            return;
        }
        copyHostMidiFromPma(length);
        setEndpoint(1,3,0);
    }
    void serviceRxPending() {
        if(!ep1Pending_) return;
        const unsigned length=bt(1,3)&0x3FFu;
        if(pendingHostMidiFits(hostMidi_.size(),length))
            copyHostMidiFromPma(length);
        else ++errors_;
        ep1Pending_=false;
        setEndpoint(1,3,0);
    }
    void copyHostMidiFromPma(unsigned length) {
        uint8_t bytes[64]{};
        pmaRead(kPmaEp1Rx,bytes,length);
        unsigned invalid=0;
        if(!hostMidi_.appendFrame(bytes,length,invalid)) ++errors_;
        errors_+=invalid;
    }
    UsbDeviceState control_{};
    bool endpointsEnabled_=false,ep1Pending_=false;
    Mpk225HostMidiQueue hostMidi_{};
    HidReportTransmitGate hidGate_{};
    uint32_t usbMillis_=0;
    uint32_t errors_=0;
};
} // namespace mpk225
