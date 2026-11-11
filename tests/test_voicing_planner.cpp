#include "VoicingPlanner.h"
#include "SmartHarmony.h"
#include <cassert>
#include <cstdio>
using namespace mpk225;

int main() {
    VoicedChord previous{};
    previous.count=4;
    previous.notes[0]=60; previous.notes[1]=64; previous.notes[2]=67; previous.notes[3]=71; // Cmaj7
    VoicedChord f{};
    assert(VoicingPlanner::plan(5, ChordType::Major7, previous, 48, 84, f));
    assert(f.count==4);
    // Fmaj7 with common tones C/E, only 4 total semitones of movement.
    assert(f.notes[0]==60 && f.notes[1]==64 && f.notes[2]==65 && f.notes[3]==69);
    assert(f.movement==4);
    const uint16_t fs=SmartHarmony::voicingMask(5, ChordType::Major7);
    uint16_t got=0;
    for (unsigned i=0;i<f.count;++i) got |= static_cast<uint16_t>(1u << (f.notes[i]%12));
    assert(got==fs);

    VoicedChord middle{};
    assert(VoicingPlanner::plan(0, ChordType::Major7, VoicedChord{}, 48, 84, middle));
    assert(middle.count==4 && middle.notes[0]>=48 && middle.notes[3]<=84);
    VoicedChord impossible{};
    assert(!VoicingPlanner::plan(5, ChordType::Major7, previous, 60, 61, impossible));
    assert(impossible.count==0); // unchanged on failure

    unsigned checked=0;
    for (uint8_t root=0;root<12;++root) {
        for (unsigned q=0;q<static_cast<unsigned>(ChordType::Count);++q) {
            VoicedChord next{};
            if (!VoicingPlanner::plan(root, static_cast<ChordType>(q), previous, 48, 96, next)) continue;
            assert(next.count>=3 && next.count<=5);
            uint16_t mask=0;
            for (unsigned j=0;j<next.count;++j) {
                assert(next.notes[j]>=48 && next.notes[j]<=96);
                if(j) assert(next.notes[j]>next.notes[j-1]);
                mask |= static_cast<uint16_t>(1u << (next.notes[j]%12));
            }
            assert(mask==SmartHarmony::voicingMask(root,static_cast<ChordType>(q)));
            ++checked;
        }
    }
    assert(checked>500);
    printf("PASS: bounded note-level voice-leading; Cmaj7->Fmaj7 moves 4 semitones; %u chords validated\n",checked);
}
