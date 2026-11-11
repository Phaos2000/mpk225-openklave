#pragma once
// MPK225 composite USB EP0 state machine. No peripherals, allocation, I/O.
// Delivers real factory device/config/string/HID descriptors and supports
// standard enumeration + boot-keyboard HID class control requests.
// The MMIO interrupt handler is separate and still requires hardware tests.
#include "Mpk225UsbControl.h"
#include "Mpk225HidKeyboard.h"
#include <stdint.h>

namespace mpk225 {

enum class ControlStage : uint8_t {
    WaitSetup, DataIn, StatusOut, StatusIn, DataOut, Stall
};
struct ControlInPacket {
    uint8_t bytes[8]{};
    uint8_t size=0;
    bool valid=false; // valid empty packet means a required USB ZLP
};
class UsbDeviceState {
public:
    void busReset() {
        stage_=ControlStage::WaitSetup;
        address_=0; configuration_=0; pendingAddress_=0;
        pendingConfiguration_=0; pendingAction_=0;
        requested_=0; sent_=0; needZlp_=false;
        current_=UsbDescriptorView{};
        protocol_=1; idle_=0; leds_=0; pendingLeds_=0;
        ++configurationEpoch_; // all previous transfers belong to old bus
        hid_.clear();
    }
    UsbDeviceState() { busReset(); }
    uint8_t address() const { return address_; }
    uint8_t configuration() const { return configuration_; }
    uint32_t configurationEpoch() const { return configurationEpoch_; }
    bool configured() const { return configuration_==13; }
    uint8_t ledOutput() const { return leds_; }
    uint8_t protocol() const { return protocol_; }
    uint8_t idleRate() const { return idle_; }
    ControlStage stage() const { return stage_; }
    HidKeyboardState& hidKeys() { return hid_; }

    bool setup(UsbSetupRequest req) {
        // A new USB SETUP cancels the previous incomplete transfer.
        pendingAction_=0; current_={}; sent_=0; needZlp_=false;
        requested_=req.wLength; stage_=ControlStage::Stall;
        if (req.bRequest==6) {
            current_=factoryGetDescriptor(req);
            if (!current_.bytes) return false;
            dataIn(req.wLength);
            return true;
        }
        // The MPK225 is bus-powered; neither remote wake nor self power.
        if (req.bRequest==0 && req.wLength>=2 && req.wValue==0) {
            // GET_STATUS is not a query for arbitrary interface or endpoint
            // numbers. Endpoint 0 is present before SET_CONFIGURATION;
            // the other addresses exist only after configuration 13.
            const bool target=
                (req.bmRequestType==0x80 && req.wIndex==0) ||
                (req.bmRequestType==0x81 && configured() && req.wIndex<=2) ||
                (req.bmRequestType==0x82 && (
                    req.wIndex==0x00 || req.wIndex==0x80 ||
                    (configured() && (req.wIndex==0x01 ||
                                      req.wIndex==0x82 ||
                                      req.wIndex==0x83))));
            if (!target) return false;
            scratch_[0]=scratch_[1]=0;
            current_={scratch_,2}; dataIn(req.wLength); return true;
        }
        if (req.bmRequestType==0x00 && req.wLength==0 &&
            req.wIndex==0 && req.bRequest==5 && req.wValue<=127) {
            pendingAddress_=static_cast<uint8_t>(req.wValue);
            pendingAction_=1; stage_=ControlStage::StatusIn; return true;
        }
        if (req.bmRequestType==0x00 && req.wLength==0 &&
            req.wIndex==0 && req.bRequest==9 &&
            (req.wValue==0 || req.wValue==13)) {
            pendingConfiguration_=static_cast<uint8_t>(req.wValue);
            pendingAction_=2; stage_=ControlStage::StatusIn; return true;
        }
        if (req.bmRequestType==0x80 && req.bRequest==8 &&
            req.wIndex==0 && req.wValue==0 && req.wLength>=1) {
            scratch_[0]=configuration_;
            current_={scratch_,1}; dataIn(req.wLength); return true;
        }
        if (req.bmRequestType==0x81 && req.bRequest==10 &&
            configured() && req.wValue==0 && req.wIndex<=2 &&
            req.wLength>=1) {
            scratch_[0]=0; current_={scratch_,1}; dataIn(req.wLength);
            return true;
        }
        if (req.bmRequestType==0x01 && req.bRequest==11 &&
            configured() && req.wLength==0 && req.wValue==0 &&
            req.wIndex<=2) {
            stage_=ControlStage::StatusIn; return true;
        }
        // HID keyboard class requests, always interface 2.
        if (req.wIndex==2 && req.bmRequestType==0x21 &&
            req.bRequest==10 && req.wLength==0 &&
            (req.wValue&0xFFu)==0) { // SET_IDLE
            pendingIdle_=static_cast<uint8_t>(req.wValue>>8);
            pendingAction_=3; stage_=ControlStage::StatusIn; return true;
        }
        if (req.wIndex==2 && req.bmRequestType==0xA1 &&
            req.bRequest==2 && req.wValue==0 && req.wLength>=1) {
            scratch_[0]=idle_; current_={scratch_,1}; dataIn(req.wLength);
            return true;
        }
        if (req.wIndex==2 && req.bmRequestType==0x21 &&
            req.bRequest==11 && req.wLength==0 && req.wValue<=1) {
            pendingProtocol_=static_cast<uint8_t>(req.wValue);
            pendingAction_=4; stage_=ControlStage::StatusIn; return true;
        }
        if (req.wIndex==2 && req.bmRequestType==0xA1 &&
            req.bRequest==3 && req.wValue==0 && req.wLength>=1) {
            scratch_[0]=protocol_; current_={scratch_,1}; dataIn(req.wLength);
            return true;
        }
        if (req.wIndex==2 && req.bmRequestType==0xA1 &&
            req.bRequest==1 && req.wValue==0x0100 && req.wLength>=1) {
            const auto& report=hid_.report();
            scratch_[0]=report.modifiers;
            scratch_[1]=report.reserved;
            for(unsigned i=0;i<6;++i) scratch_[i+2]=report.keys[i];
            const uint16_t len=req.wLength<8?req.wLength:8;
            current_={scratch_,len}; dataIn(req.wLength);
            return true;
        }
        if (req.wIndex==2 && req.bmRequestType==0xA1 &&
            req.bRequest==1 && req.wValue==0x0200 && req.wLength>=1) {
            scratch_[0]=leds_; current_={scratch_,1};
            dataIn(req.wLength); return true; // GET_REPORT: LED output
        }
        if (req.wIndex==2 && req.bmRequestType==0x21 &&
            req.bRequest==9 && req.wValue==0x0200 && req.wLength==1) {
            stage_=ControlStage::DataOut; return true; // LED output
        }
        return false;
    }

    ControlInPacket nextIn() {
        ControlInPacket out{};
        if (stage_==ControlStage::StatusIn) {
            out.valid=true; return out; // status ZLP
        }
        if (stage_!=ControlStage::DataIn) return out;
        if (sent_==current_.count && !needZlp_) return out;
        out.valid=true;
        if (sent_==current_.count && needZlp_) {
            needZlp_=false; // terminating short packet
            return out;
        }
        const uint16_t remaining=static_cast<uint16_t>(current_.count-sent_);
        out.size=static_cast<uint8_t>(remaining>8?8:remaining);
        for(unsigned i=0;i<out.size;++i) out.bytes[i]=current_.bytes[sent_+i];
        sent_=static_cast<uint16_t>(sent_+out.size);
        return out;
    }
    void inAcknowledged() {
        if (stage_==ControlStage::DataIn) {
            if (sent_==current_.count && !needZlp_)
                stage_=ControlStage::StatusOut;
        } else if (stage_==ControlStage::StatusIn) {
            switch (pendingAction_) {
                case 1: address_=pendingAddress_; break;
                case 2:
                    configuration_=pendingConfiguration_;
                    ++configurationEpoch_; // even repeat SET_CONFIGURATION(13)
                    break;
                case 3: idle_=pendingIdle_; break;
                case 4: protocol_=pendingProtocol_; break;
                case 5: leds_=pendingLeds_; break; // SET_REPORT status ACK
            }
            pendingAction_=0;
            stage_=ControlStage::WaitSetup;
        }
    }
    // EP0 OUT: status ZLP at end of data IN, or 1-byte keyboard LED report.
    bool outReceived(const uint8_t* bytes,uint8_t count) {
        if (stage_==ControlStage::StatusOut && count==0) {
            stage_=ControlStage::WaitSetup; return true;
        }
        if (stage_==ControlStage::DataOut && count==1 && bytes) {
            // Control OUT data is provisional until the status IN succeeds.
            // A new SETUP or bus reset must not commit an aborted LED change.
            pendingLeds_=static_cast<uint8_t>(bytes[0]&0x1Fu);
            pendingAction_=5;
            stage_=ControlStage::StatusIn; return true;
        }
        stage_=ControlStage::Stall; return false;
    }

private:
    void dataIn(uint16_t wLength) {
        // Without a data stage, USB ignores bmRequestType's direction
        // bit and uses an IN zero-length status transaction (USB 1.1 9.3.1).
        // In particular, GET_DESCRIPTOR with wLength=0 must not wait for
        // an OUT status that the host will never send.
        stage_=wLength?ControlStage::DataIn:ControlStage::StatusIn;
        sent_=0;
        // Zero-length termination only when actual descriptor shorter than
        // requested and exact multiple of EP0's eight-byte packet.
        needZlp_=(current_.count<wLength && (current_.count&7u)==0);
    }
    ControlStage stage_=ControlStage::WaitSetup;
    UsbDescriptorView current_{};
    uint16_t requested_=0, sent_=0;
    bool needZlp_=false;
    uint8_t scratch_[8]{};
    uint8_t address_=0, configuration_=0;
    uint32_t configurationEpoch_=0;
    uint8_t pendingAddress_=0, pendingConfiguration_=0;
    uint8_t pendingAction_=0;
    uint8_t idle_=0,protocol_=1,leds_=0,pendingIdle_=0,pendingProtocol_=1;
    uint8_t pendingLeds_=0;
    HidKeyboardState hid_{};
};

} // namespace mpk225
