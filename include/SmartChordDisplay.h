#pragma once
// Compact 16-column display status for experimental MPK225 chord UI.
// No allocation, library formatting or hardware calls. The real MPK225 LCD
// bus has not yet been verified against the MPK249-derived OpenKlave driver.
#include "SmartHarmony.h"
#include <stdint.h>

namespace mpk225 {
inline constexpr const char* kTonicNames[12] = {
    "C","C#","D","Eb","E","F","F#","G","Ab","A","Bb","B"
};
inline constexpr const char* kModeShortNames[] = {
    "MAJ","DOR","PHR","LYD","MIX","MIN","LOC",
    "HMIN","MMIN","PMAJ","PMIN","CHR"
};
inline constexpr const char* kFilterShortNames[] = {
    "IN","BOR","ALL"
};
static_assert(sizeof(kModeShortNames)/sizeof(kModeShortNames[0]) ==
    static_cast<unsigned>(ScaleMode::Count), "Mode display table mismatch");

inline void formatSmartChordStatus(char out[17], uint8_t tonic,
                                   ScaleMode mode, HarmonyFilter filter) {
    const unsigned m=static_cast<unsigned>(mode);
    const unsigned f=static_cast<unsigned>(filter);
    const char* pieces[3] = {
        tonic < 12 ? kTonicNames[tonic] : "?",
        m < static_cast<unsigned>(ScaleMode::Count) ? kModeShortNames[m] : "?",
        f < 3 ? kFilterShortNames[f] : "?"
    };
    unsigned n=0;
    for (unsigned p=0;p<3;++p) {
        if (p && n<16) out[n++]=' ';
        for (const char* c=pieces[p]; *c && n<16;++c) out[n++]=*c;
    }
    out[n]=0;
}
} // namespace mpk225
