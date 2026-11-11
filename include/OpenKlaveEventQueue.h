#pragma once
// Replacement for OpenKlave's LIFO/event buffer mismatch.
// Compatible with the original Event/PushEvent/PopEvent names and tests.
// Exactly one producer and one consumer expected from separate ISRs;
// cross-ISR ordering/atomicity needs target-side verification before deployment.
#include <stdint.h>

struct Event {
    uint32_t A, B, C, D;
};
struct EventStack {
    int32_t top;       // legacy invariant: pending_count - 1
    int32_t capacity;  // initialized to 100 by kernel.start()
    uint32_t scratch_2; // read index
    uint32_t scratch_3; // write index
    Event Buffer[100];
};
// Firmware FIFO starts at 0x2000B000. A stack top of 0x2000C000
// requires at least 2048 bytes of clear stack space above this object.
// This is a minimum layout guard, not a worst-case ISR stack proof.
static_assert(sizeof(EventStack) <= 0x800,
              "Event FIFO would leave less than 2KiB for the ARM stack");

// MIDI input can stall when the USB host is absent. Never spin forever in
// the timer ISR on a full event queue. Low-priority CC/pressure events may be
// discarded; losing any note on/off requests a deterministic MIDI panic once
// the foreground consumer can run again. No heap, no logging in an ISR.
inline volatile uint32_t gEventQueueDropped = 0;
inline volatile uint32_t gEventQueueNeedsPanic = 0;

constexpr bool eventCouldHoldNote(uint32_t type) {
    return type == 0xFFu || type == 0xFEu ||
           type == 900u || type == 901u ||
           type == 300u || type == 301u;
}

inline void recordEventQueueOverflow(Event e) {
    ++gEventQueueDropped;
    if (eventCouldHoldNote(e.A)) gEventQueueNeedsPanic = 1;
}

inline bool takeEventQueuePanic() {
    const bool pending = gEventQueueNeedsPanic != 0;
    gEventQueueNeedsPanic = 0;
    return pending;
}

inline void resetEventQueue(EventStack* es) {
    if (!es) return;
    es->top = -1;
    es->scratch_2 = 0;
    es->scratch_3 = 0;
}

[[maybe_unused]] static EventStack* GlobalEventStack = reinterpret_cast<EventStack*>(0x2000B000u);

// The original call sites ignore the return result, which remains compatible.
// Never overwrite a pending note-off when capacity is exhausted.
constexpr bool PushEvent(EventStack* es, Event e) {
    if (!es || es->capacity <= 0 || es->capacity > 100 ||
        es->top >= es->capacity - 1 ||
        es->scratch_3 >= static_cast<uint32_t>(es->capacity)) {
        recordEventQueueOverflow(e);
        return false;
    }
    es->Buffer[es->scratch_3] = e;
    es->scratch_3 = (es->scratch_3 + 1u) % static_cast<uint32_t>(es->capacity);
    ++es->top;
    return true;
}

constexpr Event PopEvent(EventStack* es) {
    if (es->top < 0) return Event{0,0,0,0};
    Event result = es->Buffer[es->scratch_2];
    es->scratch_2 = (es->scratch_2 + 1) % static_cast<uint32_t>(es->capacity);
    --es->top;
    return result;
}
constexpr int IsEventStackEmpty(EventStack* es) { return es->top < 0; }
inline Event PeekTopEvent(EventStack* es, Event) {
    if (es->top < 0) return Event{0,0,0,0};
    return es->Buffer[es->scratch_2];
}