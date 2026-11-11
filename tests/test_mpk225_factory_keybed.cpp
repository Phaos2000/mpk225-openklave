#include "Mpk225FactoryKeybedDecoder.h"
#include <cassert>
#include <cstdio>
#include <cstdint>
using namespace mpk225;
static constexpr uint32_t raw(unsigned scan, unsigned kind) {
    return static_cast<uint32_t>((scan << 8) | kind);
}
int main() {
    unsigned perNote[25]{};
    unsigned accepted=0, rejected=0;
    for (unsigned scan=0;scan<256;++scan) {
        auto down=decodeMpk225FactoryScan(raw(scan,0xFF));
        auto up=decodeMpk225FactoryScan(raw(scan,0xFE));
        assert(down.valid==up.valid);
        if (down.valid) {
            ++accepted;
            assert(down.down && !up.down);
            assert(down.midiNote==up.midiNote);
            assert(down.midiNote>=48 && down.midiNote<=72);
            ++perNote[down.midiNote-48];
            const uint8_t toggledContact=static_cast<uint8_t>(scan ^ 8);
            auto other=decodeMpk225FactoryScan(raw(toggledContact,0xFF));
            assert(other.valid && other.midiNote==down.midiNote);
        } else ++rejected;
        for (unsigned kind=0;kind<256;++kind) {
            if (kind==0xFF || kind==0xFE) continue;
            assert(!decodeMpk225FactoryScan(raw(scan,kind)).valid);
        }
    }
    assert(accepted==50 && rejected==206);
    for (unsigned i=0;i<25;++i) assert(perNote[i]==2);
    assert(decodeMpk225FactoryScan(raw(0x04,0xFF)).midiNote==48);
    assert(decodeMpk225FactoryScan(raw(0x40,0xFF)).midiNote==60);
    assert(decodeMpk225FactoryScan(raw(0x64,0xFF)).midiNote==72);
    assert(!decodeMpk225FactoryScan(raw(0x74,0xFF)).valid);
    assert(!decodeMpk225FactoryScan(raw(0x14,0xFF)).valid);
    assert(!decodeMpk225FactoryScan(raw(0x00,0xFF)).valid);
    std::printf("PASS: Akai model-1 MPK225 map, 25 MIDI notes 48..72, 2 scan codes/key, 206 rejected, all event types guarded\n");
}
