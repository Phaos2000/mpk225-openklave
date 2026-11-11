#pragma once
// MPK225/OpenKlave *host-testable* stage: legacy events -> chord transformer
// -> bounded MIDI FIFO -> standard USB-MIDI 1.0 event packets.
// NO USB peripheral access, GPIO, flashing, SysEx, or device IO in this file.
#include "ChordEngine.h"
#include <stddef.h>
#include <stdint.h>

namespace mpk225 {

// Matches OpenKlave's four-field Event layout, not an independently verified
// MPK225 hardware event format.
struct LegacyEvent {
    uint32_t type, value, note, extra;
};

struct UsbFrame {
    uint8_t bytes[8]{};  // upstream USB endpoint advertises 8-byte max packet
    uint8_t length = 0;
};

template <size_t Capacity>
class FixedMidiQueue {
    static_assert(Capacity >= 10 && Capacity <= 256, "Invalid MIDI capacity");
public:
    size_t size() const { return size_; }
    size_t freeSlots() const { return Capacity - size_; }
    bool empty() const { return size_ == 0; }
    bool append(const MidiEvent& msg) {
        if (size_ == Capacity) return false;
        data_[tail_] = msg;
        tail_ = (tail_ + 1) % Capacity;
        ++size_;
        return true;
    }
    bool append(const MidiBatch& batch) {
        if (batch.count > freeSlots()) return false;
        for (uint8_t i = 0; i < batch.count; ++i) append(batch.events[i]);
        return true;
    }
    bool pop(MidiEvent& out) {
        if (empty()) return false;
        out = data_[head_];
        head_ = (head_ + 1) % Capacity;
        --size_;
        return true;
    }
private:
    MidiEvent data_[Capacity]{};
    size_t head_ = 0, tail_ = 0, size_ = 0;
};

// Encodes MIDI 1.0 channel-voice messages as USB-MIDI 1.0 packets.
// Header: cable 0 (high nibble) + Code Index Number (low nibble).
// Unrecognized/system status is rejected, not silently corrupted.
inline bool usbMidiPacket(MidiEvent m, uint8_t packet[4]) {
    if (m.note > 127 || m.velocity > 127) return false;
    const uint8_t cin = static_cast<uint8_t>(m.status >> 4);
    if (!(cin == 0x8 || cin == 0x9 || cin == 0xA || cin == 0xB || cin == 0xE))
        return false;
    packet[0] = cin;
    packet[1] = m.status;
    packet[2] = m.note;
    packet[3] = m.velocity;
    return true;
}

class MpkMidiPipeline {
public:
    static constexpr size_t kQueueSize = 96;
    size_t queuedMessages() const { return fifo_.size(); }
    size_t freeMessages() const { return fifo_.freeSlots(); }
    bool chordMode() const { return chordMode_; }
    ChordType chordType() const { return chords_.chordType(); }
    void setChordType(ChordType type) { chords_.setChordType(type); }

    // This is deliberately conservative: no mode switch while physical notes
    // are held, since otherwise note-offs could target the wrong pitches.
    bool setChordMode(bool enabled) {
        for (size_t i = 0; i < 128; ++i) {
            if (passthroughHeld_[i] || chords_.isHeld(static_cast<uint8_t>(i)))
                return false;
        }
        chordMode_ = enabled;
        return true;
    }

    // Optional absolute MIDI voicing chosen by VoicingPlanner. The physical
    // key remains the ownership key so its normal key-up releases all voices.
    // Invalid requests or full output queues have NO note-state side effects.
    bool playPlannedVoicing(uint8_t key, uint8_t velocity,
                           const uint8_t* notes, uint8_t count) {
        if (!chordMode_ || key >= 128 || velocity == 0 || notes == nullptr ||
            count == 0 || count > ChordEngine::kVoicesPerKey)
            return false;
        for (uint8_t i = 0; i < count; ++i)
            if (notes[i] > 127 || (i && notes[i] <= notes[i-1]))
                return false;
        // Allow the worst case: five old notes off, five new notes on.
        if (fifo_.freeSlots() < 2 * ChordEngine::kVoicesPerKey) return false;
        return fifo_.append(chords_.noteOnVoicing(key, velocity, notes, count));
    }

    // Returns false only when an input needs retry due to FIFO backpressure.
    // In that case neither the MIDI output nor the internal note state changes.
    // Returns true for unknown event types after ignoring them, NOT for hardware
    // validation; an MPK225-specific adapter remains necessary.
    bool consume(LegacyEvent e) {
        if (e.type == 0xFF || e.type == 0xFE) {
            if (e.note > 127) return true; // unmapped/corrupt input
            const uint8_t n = static_cast<uint8_t>(e.note);
            const uint8_t v = static_cast<uint8_t>(e.value > 127 ? 127 : e.value);
            const bool on = e.type == 0xFF && v != 0;
            if (chordMode_) {
                // Maximum chord engine expansion is 10: retrigger 5 off + 5 on.
                if (fifo_.freeSlots() < (on ? 10u : 5u)) return false;
                const auto batch = on ? chords_.noteOn(n, v) : chords_.noteOff(n);
                return fifo_.append(batch);
            }
            const bool old = passthroughHeld_[n];
            const size_t needed = on ? (old ? 2 : 1) : (old ? 1 : 0);
            if (fifo_.freeSlots() < needed) return false;
            if (old) fifo_.append(MidiEvent{0x80, n, 0});
            passthroughHeld_[n] = false;
            if (on) {
                fifo_.append(MidiEvent{0x90, n, v});
                passthroughHeld_[n] = true;
            }
            return true;
        }
        // Pad events retain their own output notes; they never become chords.
        if (e.type == 900 || e.type == 901 || e.type == 905) {
            if (e.note > 127 || fifo_.freeSlots() < 1) return e.note > 127;
            const uint8_t status = e.type == 900 ? 0x90 : (e.type == 901 ? 0x80 : 0xA0);
            return fifo_.append(MidiEvent{status, static_cast<uint8_t>(e.note),
                static_cast<uint8_t>(e.value > 127 ? 127 : e.value)});
        }
        // Early source's ModWheel and (fader/knob) CC events.
        if (e.type == 201 || e.type == 251 || e.type == 500 || e.type == 501) {
            uint32_t cc = 0, value = 0;
            if (e.type == 201) { cc = 1; value = e.value; }
            else if (e.type == 251) { cc = e.note; value = e.value; }
            else {
                if (e.note <= 7) cc = e.note + 70;
                else if (e.note == 70 || e.note == 71) cc = e.note;
                else return true;
                value = e.type == 500 ? 65 : 1;
            }
            if (cc > 127) return true;
            if (fifo_.freeSlots() < 1) return false;
            return fifo_.append(MidiEvent{0xB0, static_cast<uint8_t>(cc),
                static_cast<uint8_t>(value > 127 ? 127 : value)});
        }
        // Legacy pitch-wheel event: note field is LSB, value field is MSB.
        if (e.type == 200) {
            if (fifo_.freeSlots() < 1) return false;
            return fifo_.append(MidiEvent{0xE0,
                static_cast<uint8_t>(e.note > 127 ? 127 : e.note),
                static_cast<uint8_t>(e.value > 127 ? 127 : e.value)});
        }
        return true;
    }

    // Drain up to TWO 4-byte event packets per 8-byte endpoint transfer.
    // Call only when device endpoint is ready. No transport timing claim.
    UsbFrame takeUsbFrame() {
        UsbFrame out;
        for (unsigned i = 0; i < 2; ++i) {
            MidiEvent m{};
            if (!fifo_.pop(m)) break;
            uint8_t data[4]{};
            if (!usbMidiPacket(m, data)) continue;
            for (unsigned j = 0; j < 4; ++j) out.bytes[out.length++] = data[j];
        }
        return out;
    }
private:
    ChordEngine chords_{};
    FixedMidiQueue<kQueueSize> fifo_{};
    bool passthroughHeld_[128]{};
    bool chordMode_ = false; // factory-style notes by default, opt-in chord mode
};

} // namespace mpk225