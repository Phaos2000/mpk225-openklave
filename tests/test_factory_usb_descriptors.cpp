#include "Mpk225UsbDescriptors.h"
#include "Mpk225HidKeyboard.h"
#include <cassert>
#include <cstdio>
#include <cstdint>
using namespace mpk225;

int main() {
    const auto* d=kFactoryUsbDeviceDescriptor;
    const auto* c=kFactoryUsbConfigDescriptor;
    assert(sizeof(kFactoryUsbDeviceDescriptor)==18);
    assert(d[0]==18 && d[1]==1 && d[2]==0x10 && d[3]==1);
    assert(d[7]==8 && d[8]==0xe8 && d[9]==9 && d[10]==0x23 && d[11]==0);
    assert(d[12]==2 && d[13]==1 && d[14]==1 && d[15]==2 && d[16]==4 && d[17]==1);

    assert(c[0]==9 && c[1]==2 && (c[2] | (c[3]<<8))==169);
    assert(c[4]==3 && c[5]==13 && c[7]==0x80 && c[8]==250);

    unsigned pos=0, elements=0, interfaces=0, endpoints=0, midiJacks=0;
    bool ac=false, ms=false, hid=false;
    bool out=false,in=false,hidIn=false,hidOut=false;
    bool midiHeader=false,acHeader=false,hidDescriptor=false;
    unsigned interfaceIndex=255, declaredHidEndpoints=255;
    while (pos < sizeof(kFactoryUsbConfigDescriptor)) {
        const unsigned size=c[pos], kind=c[pos+1];
        assert(size>=2 && pos+size<=sizeof(kFactoryUsbConfigDescriptor));
        ++elements;
        if (kind==4) {
            assert(size==9);
            interfaceIndex=c[pos+2];
            assert(interfaceIndex==interfaces);
            ++interfaces;
            if (interfaceIndex==0) {
                assert(c[pos+4]==0 && c[pos+5]==1 && c[pos+6]==1);
                ac=true;
            } else if (interfaceIndex==1) {
                assert(c[pos+4]==2 && c[pos+5]==1 && c[pos+6]==3);
                ms=true;
            } else if (interfaceIndex==2) {
                assert(c[pos+4]==1 && c[pos+5]==3 && c[pos+6]==1 && c[pos+7]==1);
                declaredHidEndpoints=c[pos+4];
                hid=true;
            }
        } else if (kind==5) {
            ++endpoints;
            assert(c[pos+4]==(interfaceIndex==1 ? 64 : 8));
            assert(c[pos+5]==0);
            const unsigned addr=c[pos+2], attr=c[pos+3];
            if (addr==0x01) { assert(interfaceIndex==1 && attr==2 && size==9); out=true; }
            else if(addr==0x82){assert(interfaceIndex==1 && attr==2 && size==9); in=true;}
            else if(addr==0x83){assert(interfaceIndex==2 && attr==3 && size==7 && c[pos+6]==10); hidIn=true;}
            else if(addr==0x03){assert(interfaceIndex==2 && attr==3 && size==7 && c[pos+6]==10); hidOut=true;}
            else assert(false && "unexpected USB endpoint");
        } else if(kind==0x21) {
            assert(interfaceIndex==2 && size==9 && c[pos+2]==0x10 && c[pos+3]==0);
            assert(c[pos+6]==0x22 && c[pos+7]==63 && c[pos+8]==0);
            hidDescriptor=true;
        } else if(kind==0x24) {
            if (interfaceIndex==0) {
                assert(size==9 && c[pos+2]==1 && c[pos+5]==18 && c[pos+6]==0);
                acHeader=true;
            } else if(interfaceIndex==1 && size==7) {
                assert(c[pos+2]==1 && c[pos+5]==101 && c[pos+6]==0);
                midiHeader=true;
            } else if(interfaceIndex==1 && (size==6 || size==9)) {
                assert(c[pos+2]==2 || c[pos+2]==3);
                ++midiJacks;
            } else assert(false);
        }
        pos+=size;
    }
    assert(pos==169 && elements==21 && interfaces==3);
    assert(ac && ms && hid && acHeader && midiHeader && hidDescriptor);
    assert(endpoints==4 && midiJacks==8);
    assert(out && in && hidIn && hidOut);
    assert(declaredHidEndpoints==1);
    // USBTreeView reported only 3 open data pipes: EP1 OUT, EP2 IN,
    // EP3 IN. The extra EP3 OUT is listed despite bNumEndpoints=1.
    assert(kFactoryMidiOutEndpoint==0x01 && kFactoryMidiInEndpoint==0x82);
    assert(kFactoryHidInEndpoint==0x83);
    assert(kFactoryMidiMaxPacket==64 && kFactoryHidMaxPacket==8);
    assert(sizeof(kHidKeyboardReportDescriptor)==63);
    std::puts("PASS: 169-byte factory MPK225 USB configuration; 3 interfaces, 4 MIDI jacks each way, 64-byte bulk, HID anomaly preserved");
}
