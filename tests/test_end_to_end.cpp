#include "MpkMidiPipeline.h"
#include "OpenKlaveEventQueue.h"
#include <cassert>
#include <cstdio>
#include <array>
using namespace mpk225;
static void run(int numNotes) {
    EventStack es{}; es.top=-1; es.capacity=100;
    MpkMidiPipeline pipe;
    assert(pipe.setChordMode(true));
    pipe.setChordType(ChordType::Major9);
    for (int i=0;i<numNotes;i++) PushEvent(&es,{0xFF,105,static_cast<uint32_t>(48+i),0});
    for (int i=0;i<numNotes;i++) PushEvent(&es,{0xFE,0,static_cast<uint32_t>(48+i),0});
    std::array<int,128> notes{};
    unsigned tx=0,stall=0,frames=0;
    for (unsigned tick=0;tick<20000;tick++) {
        for (int i=0; i<8 && !IsEventStackEmpty(&es);++i) {
            Event e=PeekTopEvent(&es,Event{});
            if (!pipe.consume({e.A,e.B,e.C,e.D})) { ++stall; break; }
            PopEvent(&es);
        }
        const auto frame=pipe.takeUsbFrame();
        if (frame.length > 0) {
            assert(frame.length <= 8 && frame.length % 4 == 0);
            ++frames;
            for(unsigned j=0;j<frame.length;j+=4) {
                assert(frame.bytes[j] == (frame.bytes[j+1]>>4));
                if (frame.bytes[j+1]==0x90) notes[frame.bytes[j+2]]++;
                if (frame.bytes[j+1]==0x80) notes[frame.bytes[j+2]]--;
                assert(notes[frame.bytes[j+2]]>=0);
                ++tx;
            }
        }
        if (IsEventStackEmpty(&es) && pipe.queuedMessages() == 0) {
            for (int n : notes) assert(n==0);
            assert(tx>=static_cast<unsigned>(numNotes*2));
            printf("PASS: end-to-end %d key presses/releases, %u USB packets, %u messages, %u queue stalls\n",numNotes,frames,tx,stall);
            return;
        }
    }
    assert(false && "queue did not drain");
}
int main(){run(1);run(25);run(50);}