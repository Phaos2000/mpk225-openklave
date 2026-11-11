#pragma once
// Experimental MPK225 five-pin MIDI OUT over MCU USART1 PA9 (USB cable
// assignment is NOT known from schematics). No flashing/SysEx updater.
// Akai MPK225 service schematic pp.30-31: MIDI_TX -> USART1_TX PA9 ->
// jack J5; crystal 8MHz. OpenKlave's pinned system_stm32f10x.c
// explicitly overrides nominal 72 MHz startup with RCC_CFGR=0x51440A.
// HSE * 6, AHB /1, APB2 /1 => USART1 PCLK2 is 48 MHz, not 72 MHz.
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
    // An interrupted USB connection can otherwise leave a hardware DIN
    // receiver holding notes whose matching note-offs were discarded.
    // These 32 CC messages are exactly 96 serial bytes (< 128 capacity).
    void panicAllChannels() {
        static_assert(kCapacity>=96,"full 16-channel MIDI cleanup fits");
        clear();
        for(unsigned ch=0;ch<16;++ch) {
            appendRaw(static_cast<uint8_t>(0xB0u|ch));
            appendRaw(120); appendRaw(0); // All Sound Off
            appendRaw(static_cast<uint8_t>(0xB0u|ch));
            appendRaw(123); appendRaw(0); // All Notes Off
        }
    }
    // Validate USB MIDI 1.0 CIN framing before sending bytes to a physical
    // serial port. CIN=F is explicitly allowed to carry *unparsed single*
    // bytes, so do NOT require it to be a realtime status.
    static constexpr bool validEvent(const UsbMidiOutEvent& ev) {
        const unsigned cin=ev.cin();
        const uint8_t a=ev.data[1],b=ev.data[2],c=ev.data[3];
        switch(cin) {
        case 2: return (a==0xF1 || a==0xF3) && b<0x80;
        case 3: return a==0xF2 && b<0x80 && c<0x80;
        case 4: return (a==0xF0 || a<0x80) && b<0x80 && c<0x80;
        case 5: return a==0xF6 || a==0xF7;
        case 6: return (a==0xF0 || a<0x80) && b==0xF7;
        case 7: return (a==0xF0 || a<0x80) && b<0x80 && c==0xF7;
        case 15: return true; // USB-MIDI raw single byte, any value
        default:
            return cin>=8 && cin<=0xE &&
                (a>>4)==cin && b<0x80 && c<0x80;
        }
    }
    // Preserve the USB event in its source queue if this transfer cannot
    // be accepted whole. A caller must explicitly bind the MIDI DIN cable.
    bool accept(const UsbMidiOutEvent& event,uint8_t boundCable) {
        if(boundCable>=kMpk225UsbCableCount || event.cable()!=boundCable)
            return false;
        const unsigned cin=event.cin();
        const unsigned bytes=usbMidiSerialLength(cin);
        if(!bytes || bytes>freeSlots() || !validEvent(event)) return false;
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
    void appendRaw(uint8_t b) {
        fifo_[tail_]=b;
        tail_=(tail_+1u)%kCapacity;
        ++count_;
    }
    uint8_t fifo_[kCapacity]{};
    unsigned head_=0,tail_=0,count_=0;
};

// Real USART1 register path. Enabled ONLY in explicit non-installable
// model-1 DIN lab builds; it never remaps pins or changes option bytes.
class Mpk225DinUart1 {
public:
    static constexpr uint32_t kPclk2=48000000u;
    static constexpr uint32_t kBaud=31250u;
    static constexpr uint16_t kBrr=0x0600u; // exact 48MHz / 31250
    static_assert(kPclk2/kBaud==kBrr && kPclk2%kBaud==0,
                  "31.25-kbaud MIDI UART divider");
    // Validate PLL source/multiplier, bus prescalers, active system clock,
    // and USB prescaler against pinned OpenKlave startup before enabling
    // any UART pin. A nominal 72MHz build must not use our 48MHz divider.
    static constexpr uint32_t kClockMask =
        0x0000000Fu | 0x000000F0u | 0x00003800u |
        0x00010000u | 0x00020000u | 0x003C0000u | 0x00400000u;
    static constexpr bool matchesClock(uint32_t cfgr) {
        return (cfgr & kClockMask)==(0x0051440Au & kClockMask);
    }

    bool ready() const { return ready_; }
    uint32_t receiveErrors() const { return receiveErrors_; }
    // PA9 stays in its original GPIO state in RX-only lab configurations.
    static constexpr uint32_t gpioAHighConfig(uint32_t original,
                                               bool enableTx,bool enableRx) {
        return (original & ~((enableTx?0xF0u:0u) |
                              (enableRx?0xF00u:0u))) |
               (enableTx?0xB0u:0u) | (enableRx?0x400u:0u);
    }
    static constexpr uint32_t controlBits(bool enableTx,bool enableRx) {
        return (1u<<13) | (enableTx?(1u<<3):0u) |
               (enableRx?(1u<<2):0u);
    }
    unsigned queuedBytes() const { return tx_.size(); }
    void resetForUsbBus() {
        // No physical MIDI was transmitted before the first configuration.
        // After physical TX begins, always prioritize 16-channel cleanup
        // over stale queued traffic on a bus reset/reconfiguration.
        if(transmitting_ && hasTransmitted_) tx_.panicAllChannels();
        else tx_.clear();
    }
    bool enqueue(const UsbMidiOutEvent& ev,uint8_t cable) {
        return ready_ && transmitting_ && tx_.accept(ev,cable);
    }
    // Reject unexpected clock/remapping; do not touch PA9 unless TX enabled.
    // This is experimental and must never be called on the owner's unit
    // before full factory-image backup and recovery have been established.
    bool init(bool enableTx,bool enableRx) {
        if(!enableTx && !enableRx) return false;
        const uint32_t clock=reg(0x40021004u); // RCC_CFGR
        if(!matchesClock(clock)) return false; // fail closed on clock change
        reg(0x40021018u)|=(1u<<0)|(1u<<2)|(1u<<14); // AFIO GPIOA USART1
        if(reg(0x40010004u)&(1u<<2)) return false; // USART1 remap forbidden
        volatile uint32_t& crh=reg(0x40010804u); // GPIOA CRH
        crh=gpioAHighConfig(crh,enableTx,enableRx);
        reg(0x4001380Cu)=0; // USART1_CR1 disabled
        reg(0x40013810u)=0; // CR2: 1 stop bit
        reg(0x40013814u)=0; // CR3: no flow control
        reg(0x40013808u)=kBrr;
        reg(0x4001380Cu)=controlBits(enableTx,enableRx);
        transmitting_=enableTx;receiving_=enableRx;receiveErrors_=0;
        hasTransmitted_=false;
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
        if(!ready_ || !transmitting_ || tx_.size()==0) return;
        if(!(reg(0x40013800u)&(1u<<7))) return; // USART1_SR TXE
        uint8_t byte=0;
        if(tx_.pop(byte)) {
            reg(0x40013804u)=byte; // USART1_DR
            hasTransmitted_=true;
        }
    }
private:
    static volatile uint32_t& reg(uint32_t address) {
        return *reinterpret_cast<volatile uint32_t*>(address);
    }
    Mpk225DinTxQueue tx_{};
    uint32_t receiveErrors_=0;
    bool ready_=false,transmitting_=false,receiving_=false;
    bool hasTransmitted_=false;
};
static_assert(Mpk225DinUart1::matchesClock(0x0051440Au),
              "match pinned OpenKlave clock");
} // namespace mpk225
