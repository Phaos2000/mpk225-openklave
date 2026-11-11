#include "SmartChordDisplay.h"
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace mpk225;
int main() {
    char out[19]={};
    out[17]='Q';out[18]='Z';
    formatSmartChordStatus(out,0,ScaleMode::Major,HarmonyFilter::Strict);
    assert(std::strcmp(out,"C MAJ IN")==0);
    formatSmartChordStatus(out,10,ScaleMode::HarmonicMinor,HarmonyFilter::Borrowed);
    assert(std::strcmp(out,"Bb HMIN BOR")==0);
    for(unsigned root=0;root<12;++root)
        for(unsigned mode=0;mode<static_cast<unsigned>(ScaleMode::Count);++mode)
            for(unsigned f=0;f<3;++f) {
                formatSmartChordStatus(out,static_cast<uint8_t>(root),
                    static_cast<ScaleMode>(mode),static_cast<HarmonyFilter>(f));
                assert(std::strlen(out)<=16);
                assert(out[17]=='Q'&&out[18]=='Z');
            }
    formatSmartChordStatus(out,255,static_cast<ScaleMode>(255),
                           static_cast<HarmonyFilter>(255));
    assert(std::strcmp(out,"? ? ?")==0);
    std::puts("PASS: 432 tonic/mode/filter LCD states, no overrun, invalid settings fail closed");
}
