#pragma once
// Experimental MPK225 five-pin MIDI OUT over MCU USART1 PA9 (USB cable
// assignment is NOT known from schematics). No flashing/SysEx updater.
// Akai MPK225 service schematic pp.30-31: MIDI_TX -> USART1_TX PA9 ->
// jack J5; crystal 8MHz. OpenKlave STM32 startup: PCLK2=72MHz.
#include "Mpk225HostMidiQueue.h"
#include <stdint.h>

namespace mpk225 {
constexpr unsigned usbMidiSerialLength(unsigned cin) {
    return cin==2||cin==6||cin==0xC||cin==0xD ? 2 :
           cin==5||cin==0xF ? 1 :
           cin==3||cin==4||cin==7||
           (cin>=8&&cin<=0xE) ? 3 : 0;
}
static_assert(usbMidiSerialLength(9)==3 && usbMidiSerialLength(0xC)==2,
              "USB-MIDI channel packet sizes");
static_assert(usbMidiSerialLength(7)==3 && usbMidiSerialLength(5)==1,
              "SysEx termination packet sizes");
static_assert(usbMidiSerialLength(0)==0 && usbMidiSerialLength(1)==0,
              "Reserved USB MIDI CIN must never produce serial bytes");

class Mpk225DinTxQueue {
public:
    static constexpr unsigned kCapacity=128;
    unsigned size() const { return count_; }
    unsigned freeSlots() const { return kCapacity-count_; }
    void clear() { head_=tail_=count_=0; }
    // Preserve the USB event in its source queue if this transfer cannot
    // be accepted whole. A caller must explicitly bind the MIDI DIN cable.
    bool accept(const UsbMidiOutEvent& event,uint8_t boundCable) {
        if(boundCable>=kMpk225UsbCableCount || event.cable()!=boundCable)
            return false;
        const unsigned cin=event.cin();
        const unsigned bytes=usbMidiSerialLength(cin);
        if(!bytes || bytes>freeSlots()) return false;
        if(cin>=8 && cin<=0xE &&
           (event.data[1]>>4)!=cin) return false;
        if(cin==0xF && event.data[1]<0xF8) return false;
        for(unsigned i=1;i<=bytes;++i) {
            fifo_[tail_]=event.data[i];
            tail_=(tail_+1u)%kCapacity;
            ++count_;
        }
        return true;
    }
    bool pop(uint8_t& b) {
        if(!count_) return false;
        b=fifo_[head_];
        head_=(head_+1u)%kCapacity;
        --count_;
        return true;
    }
private:
    uint8_t fifo_[kCapacity]{};
    unsigned head_=0,tail_=0,count_=0;
};

// Real USART1 register path. Enabled ONLY in explicit non-installable
// model-1 DIN lab builds; it never remaps pins or changes option bytes.
class Mpk225DinUart1 {
public:
    static constexpr uint32_t kPclk2=72000000u;
    static constexpr uint32_t kBaud=31250u;
    static constexpr uint16_t kBrr=0x0900u; // exact 72MHz / 31250
    static_assert(kPclk2/kBaud==kBrr && kPclk2%kBaud==0,
                  "31.25-kbaud MIDI UART divider");
    bool ready() const { return ready_; }
    uint32_t receiveErrors() const { return receiveErrors_; }
    unsigned queuedBytes() const { return tx_.size(); }
    bool enqueue(const UsbMidiOutEvent& ev,uint8_t cable) {
        return ready_ && tx_.accept(ev,cable);
    }
    // Reject unexpected clock or USART1 remapping: do not touch TX pins.
    bool init(bool enableRx=false) {
        const uint32_t clock=reg(0x40021004u); // RCC_CFGR
        if((clock&0x0000000Cu)!=0x00000008u || // SWS=PLL
           (clock&0x00003800u)!=0) return false; // PPRE2=DIV1
        reg(0x40021018u)|=(1u<<0)|(1u<<2)|(1u<<14); // AFIO GPIOA USART1
        if(reg(0x40010004u)&(1u<<2)) return false; // USART1 remap forbidden
        volatile uint32_t& crh=reg(0x40010804u); // GPIOA CRH
        crh=(crh&~(0xFFu<<4)) | (0xBu<<4) | (0x4u<<8);
        reg(0x4001380Cu)=0; // USART1_CR1 disabled
        reg(0x40013810u)=0; // CR2: 1 stop bit
        reg(0x40013814u)=0; // CR3: no flow control
        reg(0x40013808u)=kBrr;
        reg(0x4001380Cu)=(1u<<13)|(1u<<3) |
            (enableRx?(1u<<2):0u); // UE/TE plus opt-in RE
        receiving_=enableRx;receiveErrors_=0;
        tx_.clear();
        ready_=true;
        return true;
    }
    // Read SR then DR to acknowledge USART1 RXNE and error flags.
    // Caller triggers all-channel MIDI cleanup if receiveErrors changes.
    bool pollRx(uint8_t& value) {
        if(!ready_ || !receiving_) return false;
        const uint32_t sr=reg(0x40013800u);
        if(!(sr&((1u<<5)|(1u<<3)|(1u<<2)|(1u<<1)|(1u<<0))))
            return false;
        const uint32_t data=reg(0x40013804u);
        if(sr&((1u<<3)|(1u<<2)|(1u<<1)|(1u<<0))) {
            ++receiveErrors_;return false;
        }
        if(!(sr&(1u<<5))) return false;
        value=static_cast<uint8_t>(data);
        return true;
    }
    // Foreground-only, nonblocking. Never lose queued bytes when TXE low.
    void serviceTx() {
        if(!ready_ || tx_.size()==0) return;
        if(!(reg(0x40013800u)&(1u<<7))) return; // USART1_SR TXE
        uint8_t byte=0;
        if(tx_.pop(byte)) reg(0x40013804u)=byte; // USART1_DR
    }
private:
    static volatile uint32_t& reg(uint32_t address) {
        return *reinterpret_cast<volatile uint32_t*>(address);
    }
    Mpk225DinTxQueue tx_{};
    uint32_t receiveErrors_=0;
    bool ready_=false,receiving_=false;
};
} // namespace mpk225
