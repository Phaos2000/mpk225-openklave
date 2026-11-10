#include "OpenKlaveEventQueue.h"
#include <cassert>
#include <cstdio>
#include <cstdint>
constexpr int originalStyleConstexpr() {
    EventStack es = {-1, 10, 0, 0, {}};
    PushEvent(&es, Event{44, 55, 66, 77});
    PushEvent(&es, Event{44, 55, 66, 77});
    PopEvent(&es);
    return es.top;
}
static_assert(originalStyleConstexpr() == 0, "Legacy compile-time tests remain compatible");
static void test_order_and_wrap() {
    EventStack s{};
    s.top=-1; s.capacity=100;
    for (uint32_t i=0;i<100;i++) PushEvent(&s, {i,0,0,0});
    assert(s.top == 99);
    for (uint32_t i=0;i<70;i++) { assert(PeekTopEvent(&s, {}).A == i); assert(PopEvent(&s).A==i); }
    for (uint32_t i=100;i<160;i++) PushEvent(&s, {i,0,0,0});
    for (uint32_t i=70;i<160;i++) assert(PopEvent(&s).A == i);
    assert(IsEventStackEmpty(&s));
}
static void test_original_stack_model() {
    EventStack s = {-1,10,0,0,{}};
    for(uint32_t i=0;i<5;i++) PushEvent(&s,{i,0,0,0});
    assert(s.top==4);
    assert(PopEvent(&s).A==0); assert(PopEvent(&s).A==1);
    assert(s.top==2);
}
static void test_stress() {
    EventStack s{}; s.top=-1;s.capacity=100;
    uint32_t front=0, back=0;
    for (uint32_t i=0;i<200000;i++) {
        if ((i%9)!=0 && s.top<99) PushEvent(&s, {back++,0,0,0});
        else if (s.top>=0) assert(PopEvent(&s).A==front++);
    }
    while(s.top>=0) assert(PopEvent(&s).A==front++);
    assert(front==back);
}
int main(){test_order_and_wrap();test_original_stack_model();test_stress();printf("PASS: OpenKlave FIFO order, wrap, 200000 operations; %zu-byte buffer\n",sizeof(EventStack));}