#include "EmbeddedSmartChordController.h"
#include <cassert>
#include <cstdio>
using namespace mpk225;
int main() {
    EmbeddedSmartChordController c;
    assert(c.tonic()==0 && c.scale()==ScaleMode::Major);
    assert(c.filter()==HarmonyFilter::Strict);
    assert(!c.panelDirty());
#if MPK225_CANDIDATE_PANEL_BUTTONS
    // Reference MPK249 button events (MPK225 hardware layout not verified).
    assert(c.consume({300,0,13,0})); // right: C -> C#
    assert(c.tonic()==1 && c.panelDirty());
    c.clearPanelDirty();
    assert(c.consume({301,0,13,0})); // release must not change tonic
    assert(c.tonic()==1 && !c.panelDirty());
    for (unsigned i=0;i<11;++i) assert(c.consume({300,0,13,0}));
    assert(c.tonic()==0);
    assert(c.consume({300,0,12,0})); // left: B
    assert(c.tonic()==11);
    assert(c.consume({300,0,14,0})); // mode major -> dorian
    assert(c.scale()==ScaleMode::Dorian);
    assert(c.consume({300,0,15,0})); // mode dorian -> major
    assert(c.scale()==ScaleMode::Major);
    assert(c.consume({300,0,15,0})); // mode major -> chromatic
    assert(c.scale()==ScaleMode::Chromatic);
    assert(c.consume({300,0,14,0}));
    assert(c.scale()==ScaleMode::Major);
    for (unsigned i=0;i<3;++i) assert(c.consume({300,0,8,0}));
    assert(c.filter()==HarmonyFilter::Strict);
    assert(c.consume({300,0,8,0}));
    assert(c.filter()==HarmonyFilter::Borrowed);
    assert(c.consume({300,0,8,0}));
    assert(c.filter()==HarmonyFilter::Any);
    assert(c.consume({300,0,8,0}));
    assert(c.filter()==HarmonyFilter::Strict);
    assert(!c.handlePanelButton({300,0,99,0}));
    assert(!c.handlePanelButton({251,0,13,0}));
    // Change scale while a chord is sounding; key-up still releases its
    // actual notes, not the new scale's chord selection.
    assert(c.consume({300,0,13,0})); // B->C
    assert(c.consume({0xFF,88,60,0}));
    unsigned ons=0;
    while(c.queuedMessages()) {
        const auto f=c.takeUsbFrame();
        for(unsigned j=0;j<f.length;j+=4) {
            assert(f.bytes[j+1]==0x90); ++ons;
        }
    }
    assert(ons>=3 && ons<=5);
    assert(c.consume({300,0,14,0})); // switch to Dorian while C chord held
    assert(c.consume({0xFE,0,60,0}));
    unsigned offs=0;
    while(c.queuedMessages()) {
        const auto f=c.takeUsbFrame();
        for(unsigned j=0;j<f.length;j+=4) {
            assert(f.bytes[j+1]==0x80); ++offs;
        }
    }
    assert(ons==offs);
    std::puts("PASS: opt-in panel tonic/mode/filter wrap, press-only actions and held-note-safe release");
#else
    assert(!c.handlePanelButton({300,0,13,0}));
    assert(c.consume({300,0,13,0})); // unknown event ignored
    assert(c.tonic()==0 && c.scale()==ScaleMode::Major);
    assert(!c.panelDirty());
    std::puts("PASS: onboard panel map disabled by default, untouched MIDI behavior");
#endif
}
