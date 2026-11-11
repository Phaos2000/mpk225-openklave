#include "Mpk225UsbControl.h"
#include <cassert>
#include <cstdio>
#include <cstdint>
using namespace mpk225;

static unsigned stream(UsbDescriptorView d, unsigned expected) {
    assert(d.count==expected);
    unsigned total=0, packets=0;
    while (total<d.count) {
        const auto p=factoryDescriptorPacket(d,static_cast<uint16_t>(total));
        const unsigned remaining=d.count-total;
        assert(p.count==(remaining>=8?8:remaining));
        for(unsigned i=0;i<p.count;++i) assert(p.bytes[i]==d.bytes[total+i]);
        total+=p.count; ++packets;
    }
    assert(factoryDescriptorPacket(d,static_cast<uint16_t>(total)).count==0);
    assert(total==expected);
    return packets;
}
int main() {
    assert(stream(factoryGetDescriptor({0x80,6,0x0100,0,255}),18)==3);
    assert(stream(factoryGetDescriptor({0x80,6,0x0200,0,255}),169)==22);
    assert(stream(factoryGetDescriptor({0x81,6,0x2200,2,255}),63)==8);
    assert(stream(factoryGetDescriptor({0x81,6,0x2100,2,255}),9)==2);
    assert(stream(factoryGetDescriptor({0x80,6,0x0200,0,9}),9)==2);
    assert(stream(factoryGetDescriptor({0x80,6,0x0100,0,8}),8)==1);

    for(unsigned n=0;n<=169;++n) {
        const auto d=factoryGetDescriptor({0x80,6,0x0200,0,static_cast<uint16_t>(n)});
        assert(d.count==n);
        if (n) stream(d,n);
    }
    const unsigned strings[]={0,1,2,3,4,6,7,8,9,13,14,15,16};
    for(unsigned i:strings) {
        auto d=factoryGetDescriptor({0x80,6,static_cast<uint16_t>(0x0300u+i),0x0409,255});
        assert(d.bytes && d.count>=4 && d.count==d.bytes[0] && d.bytes[1]==3);
        assert(d.count<=34 && (d.count%2)==0);
        for(unsigned j=3;j<d.count;j+=2)assert(d.bytes[j]==0);
        stream(d,d.count);
    }
    // Unsupported requests or wrong HID interface do not impersonate data.
    assert(!factoryGetDescriptor({0x80,6,0x2200,0,255}).bytes);
    assert(!factoryGetDescriptor({0x81,6,0x2200,1,255}).bytes);
    assert(!factoryGetDescriptor({0x81,6,0x2201,2,255}).bytes);
    assert(!factoryGetDescriptor({0x80,6,0x0101,0,255}).bytes);
    assert(!factoryGetDescriptor({0x80,6,0x0305,0,255}).bytes);
    assert(!factoryGetDescriptor({0x80,7,0x0200,0,255}).bytes);
    assert(!factoryGetDescriptor({0x21,6,0x2200,2,255}).bytes);
    assert(factoryDescriptorPacket({},0).count==0);
    std::puts("PASS: factory MPK225 EP0 descriptor model, all 169 request lengths, 22 config packets, HID and 13 strings");
}
