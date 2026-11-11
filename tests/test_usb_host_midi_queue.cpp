#include "Mpk225HostMidiQueue.h"
#include <cassert>
#include <cstdio>
using mpk225::Mpk225HostMidiQueue;
using mpk225::UsbMidiOutEvent;
int main() {
    Mpk225HostMidiQueue q;
    unsigned invalid=99;
    assert(q.size()==0);
    UsbMidiOutEvent held{};
    assert(!q.peek(held));
    uint8_t frame[64]{};
    for(unsigned i=0;i<16;++i) {
        frame[i*4]=static_cast<uint8_t>(((i%4)<<4)|9u);
        frame[i*4+1]=0x90;
        frame[i*4+2]=static_cast<uint8_t>(48u+i);
        frame[i*4+3]=100;
    }
    for(unsigned i=0;i<4;++i) {
        assert(q.appendFrame(frame,64,invalid) && invalid==0);
        assert(q.size()==16*(i+1u));
    }
    assert(!q.appendFrame(frame,64,invalid) && q.size()==64);
    UsbMidiOutEvent event{};
    assert(q.peek(held) && held.data[2]==48);
    assert(q.peek(held) && q.size()==64); // destination refused: no pop
    for(unsigned i=0;i<48;++i) {
        assert(q.peek(held));
        assert(q.pop(event) && event.knownCable());
        for(unsigned k=0;k<4;++k) assert(event.data[k]==held.data[k]);
        assert(event.cable()==(i%16u)%4u);
        assert(event.cin()==9 && event.data[2]==48+(i%16u));
    }
    assert(q.size()==16 && q.appendFrame(frame,64,invalid));
    for(unsigned i=0;i<32;++i)
        assert(q.pop(event) && event.data[2]==48+(i%16u));
    assert(!q.pop(event) && q.size()==0);

    uint8_t mixed[16]{0x03,0xF0,1,2,0x15,0xF7,0,0,
                       0x2F,0xF8,0,0,0x3B,0xB0,1,127};
    assert(q.appendFrame(mixed,16,invalid) && invalid==0);
    for(unsigned i=0;i<4;++i) {
        assert(q.pop(event) && event.cable()==i);
        for(unsigned j=0;j<4;++j) assert(event.data[j]==mixed[4*i+j]);
    }
    mixed[4]=0x45;
    assert(q.appendFrame(mixed,16,invalid) && invalid==1 && q.size()==3);
    assert(q.pop(event) && event.cable()==0);
    assert(q.pop(event) && event.cable()==2);
    assert(q.pop(event) && event.cable()==3);
    assert(!q.pop(event));
    for(unsigned i=0;i<16;++i) assert(q.appendFrame(frame,16,invalid));
    assert(q.size()==64 && !q.appendFrame(frame,4,invalid));
    assert(q.pop(event) && q.appendFrame(frame,4,invalid));
    assert(q.size()==64 && !q.appendFrame(frame,64,invalid));
    q.clear();
    assert(!q.pop(event));
    assert(!q.appendFrame(nullptr,4,invalid));
    assert(!q.appendFrame(frame,3,invalid));
    assert(!q.appendFrame(frame,65,invalid));
    assert(q.appendFrame(frame,0,invalid) && q.size()==0);
    // A packet for an unmapped cable cannot obstruct valid DIN traffic
    // assigned to another cable. The unhandled events stay fully queued.
    assert(!q.peekCable(4,event) && !q.popCable(4,event));
    assert(!q.peekCable(2,event) && !q.popCable(2,event));
    for(unsigned i=0;i<4;++i) {
        assert(q.appendFrame(frame,64,invalid) && invalid==0);
    }
    assert(q.size()==64);
    for(unsigned n=0;n<16;++n) {
        assert(q.peekCable(2,held));
        assert(held.cable()==2 && held.data[2]==50+4*(n%4));
        assert(q.popCable(2,event));
        for(unsigned j=0;j<4;++j) assert(held.data[j]==event.data[j]);
    }
    assert(q.size()==48);
    for(unsigned i=0;i<64;++i) {
        if(i%4==2)continue;
        assert(q.pop(event));
        assert(event.cable()==i%4 && event.data[2]==48+i%16);
    }
    assert(q.size()==0 && !q.popCable(2,event));
    // Exercise removal from a wrapped ring: old head is nonzero while
    // the next appended USB frame crosses the physical array boundary.
    for(unsigned i=0;i<4;++i)assert(q.appendFrame(frame,64,invalid));
    for(unsigned i=0;i<40;++i)assert(q.pop(event));
    for(unsigned i=0;i<2;++i)assert(q.appendFrame(frame,64,invalid));
    assert(q.size()==56);
    unsigned cable2Count=0;
    while(q.popCable(2,event)){++cable2Count;assert(event.cable()==2);}
    assert(cable2Count==14 && q.size()==42);
    while(q.pop(event)) assert(event.cable()!=2);
    assert(q.size()==0);
    std::puts("PASS: USB MIDI OUT four-cable FIFO, raw CIN, wrapped selective dequeue, no cross-cable loss, backpressure");
}
