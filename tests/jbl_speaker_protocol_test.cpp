#include "../src/drivers/jbl_speaker_protocol.h"
#include <cassert>
#include <iostream>
int main(){
    using namespace jbl_speaker;
    Info i;
    const uint8_t a[]={0xAA,0x12,0x03,0x00,0x44,0x1E};
    i.Feed(a,2);assert(i.percent==-1);i.Feed(a+2,sizeof(a)-2);assert(i.percent==30&&!i.charging);
    const uint8_t b[]={0xAA,0x12,0x03,0x00,0x44,0x8A,0xAA,0x12,0x04,0x00,0x42,0x21,0x07};
    i.Feed(b,sizeof(b));assert(i.percent==10&&i.charging&&i.pid==product);
    const uint8_t bad[]={0xAA,0x12,0x03,0,0x44,0xFF};
    i.Feed(bad,sizeof(bad));assert(i.percent==10);
    const uint8_t mac[]={0xAA,0x12,0x08,0,0x48,1,2,3,4,5,6};
    i.Feed(mac,sizeof(mac));assert(i.address==0x010203040506ULL);
    const uint8_t zero[]={0xAA,0x12,0x03,0,0x44,0};
    i.Feed(zero,sizeof(zero));assert(i.percent==0&&!i.charging);
    std::cout<<"JBL speaker protocol tests passed\n";
}
