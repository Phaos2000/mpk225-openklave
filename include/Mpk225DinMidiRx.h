#pragma once
// Offline-testable 31.25-kbaud MPK225 DIN serial MIDI -> USB-MIDI events.
// Physical USART1 RX is PA10 by service schematic; actual factory USB cable
// routing remains unverified, so a cable must be explicitly provided.
// No GPIO, USB hardware, SysEx updater, flash operations, or heap.
#include "Mpk225UsbMidiCables.h"
#include <stdint.h>
namespace mpk225 {
struct DinUsbBulkFrame { uint8_t bytes[64]{}; uint8_t length=0; };
class Mpk225DinMidiRx {
public:
    static constexpr unsigned kCapacity=64;
    unsigned queuedEvents() const { return size_+panicRemaining_; }
    uint32_t overflowCount() const { return overflow_; }
    uint32_t malformedCount() const { return malformed_; }
    void clear() {
        head_=tail_=size_=0;
        running_=status_=received_=0;
        sysex_=false;sysexCount_=0;panicRemaining_=0;
    }
    // Serial MIDI cannot be NAKed. Any dropped bytes invalidate the message
    // state and require All Sound Off + All Notes Off on all MIDI channels.
    void signalLoss(uint8_t cable) {
        ++overflow_;
        head_=tail_=size_=0;
        running_=status_=received_=0;
        sysex_=false;sysexCount_=0;
        panicCable_=cable;panicRemaining_=32;
    }
    bool feed(uint8_t byte,uint8_t cable) {
        if(cable>=kMpk225UsbCableCount) {++malformed_;return false;}
        if(byte>=0xF8) return emit(cable,15,byte,0,0);
        if(sysex_) {
            if(byte<0x80 || byte==0xF7) {
                sysexBytes_[sysexCount_++]=byte;
                const bool ended=byte==0xF7;
                if(ended || sysexCount_==3) {
                    const unsigned cin=ended?4u+sysexCount_:4u;
                    const uint8_t a=sysexBytes_[0];
                    const uint8_t b=sysexCount_>1?sysexBytes_[1]:0;
                    const uint8_t c=sysexCount_>2?sysexBytes_[2]:0;
                    sysexCount_=0;
                    if(ended) sysex_=false;
                    return emit(cable,cin,a,b,c);
                }
                return true;
            }
            sysex_=false;sysexCount_=0;++malformed_;
        }
        if(byte&0x80) {
            status_=0;received_=0;running_=0;
            if(byte==0xF0) {
                sysex_=true;sysexCount_=1;sysexBytes_[0]=0xF0;
                return true;
            }
            if(byte<=0xEF) {status_=running_=byte;return true;}
            if(byte==0xF1 || byte==0xF2 || byte==0xF3) {
                status_=byte;return true;
            }
            if(byte==0xF6 || byte==0xF7)
                return emit(cable,5,byte,0,0);
            // F4/F5 are undefined MIDI status bytes, not serial messages.
            ++malformed_;return true;
        }
        if(!status_) status_=running_;
        if(!status_) {++malformed_;return true;}
        const uint8_t needed=status_==0xF2?2:
            (status_==0xF1 || status_==0xF3 ||
             (status_>=0xC0 && status_<=0xDF))?1:2;
        data_[received_++]=byte;
        if(received_<needed) return true;
        const uint8_t s=status_,a=data_[0],b=needed==2?data_[1]:0;
        status_=0;received_=0;
        return emit(cable,s>=0xF0?(needed==1?2u:3u):s>>4,s,a,b);
    }
    // Call only when the USB IN endpoint is ready; a full transfer is
    // at most 16 four-byte events and panic cleanup has priority.
    DinUsbBulkFrame takeBulkFrame() {
        DinUsbBulkFrame frame{};
        while(frame.length<=60 && (panicRemaining_ || size_)) {
            uint8_t p[4]{};
            if(panicRemaining_) {
                const unsigned step=32u-panicRemaining_;
                p[0]=static_cast<uint8_t>((panicCable_<<4)|0xB);
                p[1]=static_cast<uint8_t>(0xB0|(step/2u));
                p[2]=static_cast<uint8_t>((step&1u)?123:120);
                --panicRemaining_;
            } else {
                for(unsigned k=0;k<4;++k) p[k]=events_[head_][k];
                head_=(head_+1u)%kCapacity;--size_;
            }
            for(unsigned k=0;k<4;++k) frame.bytes[frame.length++]=p[k];
        }
        return frame;
    }
private:
    bool emit(uint8_t cable,unsigned cin,uint8_t a,uint8_t b,uint8_t c) {
        if(size_>=kCapacity) {signalLoss(cable);return false;}
        events_[tail_][0]=static_cast<uint8_t>((cable<<4)|(cin&15u));
        events_[tail_][1]=a;events_[tail_][2]=b;events_[tail_][3]=c;
        tail_=(tail_+1u)%kCapacity;++size_;return true;
    }
    uint8_t events_[kCapacity][4]{};
    unsigned head_=0,tail_=0,size_=0;
    uint8_t running_=0,status_=0,received_=0,data_[2]{};
    bool sysex_=false;
    uint8_t sysexBytes_[3]{},sysexCount_=0,panicCable_=0;
    unsigned panicRemaining_=0;
    uint32_t overflow_=0,malformed_=0;
};
} // namespace mpk225
