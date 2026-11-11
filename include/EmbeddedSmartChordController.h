#pragma once
// Real Cortex-M3-friendly smart harmony -> voicing -> USB-MIDI adapter.
// NO allocation, filesystem, USB peripheral access, SysEx or flashing.
// Works on OpenKlave's *candidate* normalized LegacyEvent scheme;
// native MPK225 switch/LCD controls and hardware USB are not yet validated.
#include "SmartHarmony.h"
#include "VoicingPlanner.h"
#include "MpkMidiPipeline.h"
#include <stdint.h>

#ifndef MPK225_CANDIDATE_SMART_ON_BOOT
#define MPK225_CANDIDATE_SMART_ON_BOOT 0
#endif
#ifndef MPK225_CANDIDATE_PANEL_BUTTONS
#define MPK225_CANDIDATE_PANEL_BUTTONS 0
#endif
static_assert(MPK225_CANDIDATE_PANEL_BUTTONS == 0 ||
              MPK225_CANDIDATE_PANEL_BUTTONS == 1,
              "PANEL_BUTTONS requires 0/1");
static_assert(!MPK225_CANDIDATE_PANEL_BUTTONS ||
              MPK225_CANDIDATE_SMART_ON_BOOT,
              "PANEL_BUTTONS requires smart chord mode");

#ifndef MPK225_CANDIDATE_CHORD_ON_BOOT
#define MPK225_CANDIDATE_CHORD_ON_BOOT 0
#endif
static_assert(MPK225_CANDIDATE_SMART_ON_BOOT == 0 ||
              MPK225_CANDIDATE_SMART_ON_BOOT == 1, "SMART_ON_BOOT requires 0/1");
static_assert(!MPK225_CANDIDATE_SMART_ON_BOOT ||
              MPK225_CANDIDATE_CHORD_ON_BOOT,
              "SMART_ON_BOOT requires CHORD_ON_BOOT=1");

namespace mpk225 {

class EmbeddedSmartChordController {
public:
    // Preserve chosen key/scale and chord-on/off configuration while
    // dropping stale notes after USB bus reset or host re-enumeration.
    void resetForUsbBus() {
        pipeline_.resetForUsbBus();
        previous_ = VoicedChord{};
        harmony_.hasPrevious = false;
        fallbacks_ = 0;
    }

    // If any physical note event was lost during FIFO overflow, discard
    // queued MIDI from the stale state and enqueue channel-1 All Sound Off
    // and All Notes Off. This avoids silently sustaining notes at the host.
    // These messages are transported normally once EP1 can transmit again.
    void panicForEventLoss() {
        resetForUsbBus();
        (void)pipeline_.consume({251, 0, 120, 0}); // CC120 All Sound Off
        (void)pipeline_.consume({251, 0, 123, 0}); // CC123 All Notes Off
    }

    // Upstream OpenKlave MPK249 panel event IDs, not yet matched to the
    // physical MPK225 wiring. Only present in an explicitly requested lab
    // build (PANEL_BUTTONS=1; SMART_ON_BOOT=1).
    // SELECT L/R = tonic -/+ ; SELECT U/D = mode next/previous ;
    // knob push = strict/borrowed/any filter cycle.
    bool handlePanelButton(LegacyEvent event) {
#if MPK225_CANDIDATE_PANEL_BUTTONS
        const unsigned id=event.note;
        if (event.type!=300 && event.type!=301) return false;
        if (id!=12 && id!=13 && id!=14 && id!=15 && id!=8)
            return false;
        if (event.type==301) return true; // consume release, never change twice
        switch (id) {
            case 12: harmony_.tonic=static_cast<uint8_t>((harmony_.tonic+11u)%12u);break;
            case 13: harmony_.tonic=static_cast<uint8_t>((harmony_.tonic+1u)%12u);break;
            case 14: harmony_.scale=static_cast<ScaleMode>(
                (static_cast<unsigned>(harmony_.scale)+1u) %
                static_cast<unsigned>(ScaleMode::Count));break;
            case 15: harmony_.scale=static_cast<ScaleMode>(
                (static_cast<unsigned>(harmony_.scale)+
                 static_cast<unsigned>(ScaleMode::Count)-1u) %
                static_cast<unsigned>(ScaleMode::Count));break;
            case 8: harmony_.filter=static_cast<HarmonyFilter>(
                (static_cast<unsigned>(harmony_.filter)+1u)%3u);break;
        }
        harmony_.hasPrevious=false;
        previous_=VoicedChord{};
        panelDirty_=true;
        return true;
#else
        (void)event;
        return false;
#endif
    }
    uint8_t tonic() const { return harmony_.tonic; }
    ScaleMode scale() const { return harmony_.scale; }
    HarmonyFilter filter() const { return harmony_.filter; }
    bool panelDirty() const { return panelDirty_; }
    void clearPanelDirty() { panelDirty_=false; }

    bool smartEnabled() const { return MPK225_CANDIDATE_SMART_ON_BOOT != 0; }
    bool chordEnabled() const { return pipeline_.chordMode(); }
    unsigned fallbackCount() const { return fallbacks_; }
    size_t queuedMessages() const { return pipeline_.queuedMessages(); }
    UsbFrame takeUsbFrame() { return pipeline_.takeUsbFrame(); }

    // A pure, finite, bounded operation in the keybed event path.
    // Input event is *already* decoded to a valid MIDI pitch by the
    // separate, experimental hardware-specific scan decoder.
    bool consume(LegacyEvent e) {
        if (handlePanelButton(e)) return true;
        if (!smartEnabled() || !pipeline_.chordMode() ||
            e.type != 0xFF || e.value == 0 || e.note > 127)
            return pipeline_.consume(e);

        const uint8_t key = static_cast<uint8_t>(e.note);
        const uint8_t velocity =
            static_cast<uint8_t>(e.value > 127 ? 127 : e.value);
        const uint8_t root = static_cast<uint8_t>(key % 12u);

        // Select an in-key quality whose root is the pressed note.
        // This is a rule-based heuristic, not inferred musical function.
        bool found = false;
        int bestScore = -32768;
        ChordType bestType = ChordType::Major;
        for (unsigned q = 0; q < static_cast<unsigned>(ChordType::Count); ++q) {
            HarmonySuggestion s{};
            const ChordType type = static_cast<ChordType>(q);
            if (!SmartHarmony::evaluate(harmony_, root, type, s))
                continue;
            const int score = static_cast<int>(s.score) +
                              3 * kChords[q].count;
            if (!found || score > bestScore) {
                found = true;
                bestScore = score;
                bestType = type;
            }
        }

        VoicedChord voiced{};
        if (found) {
            const uint8_t low = key > 12 ? static_cast<uint8_t>(key - 12u) : 0;
            const uint8_t high = key < 99 ? static_cast<uint8_t>(key + 28u) : 127;
            found = VoicingPlanner::plan(root, bestType, previous_,
                                         low, high, voiced);
        }
        if (!found) {
            // Never swallow an out-of-key press: play the original note.
            voiced.count = 1;
            voiced.notes[0] = key;
        }

        // On FIFO backpressure, return false without changing harmonic
        // history OR note ownership; upstream retries exactly this event.
        if (!pipeline_.playPlannedVoicing(key, velocity,
                                         voiced.notes, voiced.count))
            return false;
        if (!found) ++fallbacks_;
        previous_ = voiced;
        // Keep root and chord quality for pitch-class candidate ranking;
        // the exact MIDI voice movement is taken from previous_.
        if (found) {
            harmony_.previousRoot = root;
            harmony_.previousType = bestType;
            harmony_.hasPrevious = true;
        } else {
            harmony_.hasPrevious = false;
        }
        return true;
    }

private:
    MpkMidiPipeline pipeline_{};
    HarmonyContext harmony_{}; // experimental default: strict C major
    VoicedChord previous_{};
    unsigned fallbacks_ = 0;
    bool panelDirty_ = false;
};

} // namespace mpk225
