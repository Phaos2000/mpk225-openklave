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

inline void GlobalEventStackOverflow() {
    // Not a recovery strategy: prevents overwriting RAM. Must be replaced
    // with verified event-priority handling before any real hardware rollout.
    volatile uint32_t halted = 1;
    while (halted) { /* intentional no-op; fail closed on overflow */ }
}

[[maybe_unused]] static EventStack* GlobalEventStack = reinterpret_cast<EventStack*>(0x2000B000u);

constexpr void PushEvent(EventStack* es, Event e) {
    if (es->capacity <= 0 || es->capacity > 100 || es->top >= es->capacity - 1)
        GlobalEventStackOverflow();
    es->Buffer[es->scratch_3] = e;
    es->scratch_3 = (es->scratch_3 + 1) % static_cast<uint32_t>(es->capacity);
    ++es->top;
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