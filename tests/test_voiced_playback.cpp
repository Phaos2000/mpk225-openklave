#include "VoicingPlanner.h"
#include "MpkMidiPipeline.h"
#include <cassert>
#include <cstdio>
using namespace mpk225;

static MidiEvent next(MpkMidiPipeline& pipeline, UsbFrame& frame, unsigned& cursor) {
    if (cursor >= frame.length) {
        frame = pipeline.takeUsbFrame();
        cursor = 0;
    }
    assert(frame.length && cursor + 4 <= frame.length);
    assert(frame.bytes[cursor] == (frame.bytes[cursor+1] >> 4));
    MidiEvent m{frame.bytes[cursor+1], frame.bytes[cursor+2], frame.bytes[cursor+3]};
    cursor += 4;
    return m;
}

int main() {
    VoicedChord c{};
    c.count = 4; c.notes[0]=60; c.notes[1]=64; c.notes[2]=67; c.notes[3]=71;
    VoicedChord f{};
    assert(VoicingPlanner::plan(5, ChordType::Major7, c, 48, 84, f));
    assert(f.movement == 4);
    MpkMidiPipeline p;
    assert(p.setChordMode(true));
    assert(p.playPlannedVoicing(60, 100, c.notes, c.count));
    const size_t before=p.queuedMessages();
    const uint8_t unsorted[]={65,60,64,69};
    assert(!p.playPlannedVoicing(60, 100, unsorted, 4));
    assert(p.queuedMessages()==before);
    assert(p.playPlannedVoicing(60, 100, f.notes, f.count));
    assert(p.consume({0xFE,0,60,0})); // physical key release owns new Fmaj7 notes
    assert(p.setChordMode(false));
    UsbFrame frame{};
    unsigned cursor=0;
    for (unsigned phase=0; phase<4; ++phase) {
        const uint8_t* expected=phase==0 || phase==1 ? c.notes : f.notes;
        const uint8_t status=(phase==0 || phase==2) ? 0x90 : 0x80;
        for (unsigned i=0; i<4; ++i) {
            MidiEvent m=next(p,frame,cursor);
            assert(m.status==status && m.note==expected[i]);
            assert(m.velocity==(status==0x90 ? 100 : 0));
        }
    }
    assert(p.queuedMessages()==0 && cursor==frame.length);
    assert(p.takeUsbFrame().length==0);
    printf("PASS: Cmaj7 -> minimal-motion Fmaj7, retrigger ownership, 16 MIDI events, zero stuck notes\n");
}
