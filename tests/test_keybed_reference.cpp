#include "KeybedReferenceDecoder.h"
#include <cassert>
#include <cstdio>
using namespace mpk225;

static constexpr uint32_t raw(unsigned index, unsigned action) {
    return static_cast<uint32_t>((index << 8) | action);
}

int main() {
    assert(decodeMpk249ReferenceScan(raw(12, 0xFF)).valid);
    assert(decodeMpk249ReferenceScan(raw(12, 0xFF)).midiNote == 60);
    assert(decodeMpk249ReferenceScan(raw(12, 0xFE)).midiNote == 60);
    assert(decodeMpk249ReferenceScan(raw(12, 0xFE)).down == false);
    assert(decodeMpk249ReferenceScan(raw(12, 0xFF)).down == true);
    assert(decodeMpk249ReferenceScan(raw(40, 0xFF)).midiNote == 64);
    assert(decodeMpk249ReferenceScan(raw(96, 0xFF)).midiNote == 48);
    assert(decodeMpk249ReferenceScan(raw(104, 0xFF)).midiNote == 78);

    unsigned matched = 0;
    unsigned rejected = 0;
    for (unsigned code=0; code<256; ++code) {
        const KeybedScan press = decodeMpk249ReferenceScan(raw(code, 0xFF));
        const KeybedScan release = decodeMpk249ReferenceScan(raw(code, 0xFE));
        assert(press.valid == release.valid);
        if (press.valid) {
            ++matched;
            assert(press.down && !release.down);
            assert(press.midiNote == release.midiNote);
            assert(press.midiNote >= 36 && press.midiNote <= 82);
        } else ++rejected;
        for (unsigned action=0;action<256;++action) {
            if (action==0xFF || action==0xFE) continue;
            assert(!decodeMpk249ReferenceScan(raw(code, action)).valid);
        }
    }
    // The 2021 MPK249 map has exactly 49 nonzero entries; other 207 codes
    // used to index outside its 109-entry array or yield MIDI note 0.
    assert(matched == 49 && rejected == 207);
    assert(!decodeMpk249ReferenceScan(raw(109,0xFF)).valid);
    assert(!decodeMpk249ReferenceScan(raw(255,0xFE)).valid);
    assert(!decodeMpk249ReferenceScan(raw(12,0)).valid);
    std::printf("PASS: 49 known MPK249 scan codes, 207 rejected; 65,536 unknown statuses rejected\n");
}
