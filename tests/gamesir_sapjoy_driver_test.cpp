#include "../src/drivers/drivers.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <deque>
static std::deque<std::vector<BYTE>> pending;
static int opens;
static bool different=false, blank=false, invalid=false, cableFailure=false;
static HidColl Collection(USHORT pid) {
    HidColl c; c.vid=0x3537;c.pid=pid;c.product=L"GameSir Test";
    c.caps.UsagePage=0xFFA0;c.caps.InputReportByteLength=64;c.caps.OutputReportByteLength=32;
    return c;
}
std::wstring HidProduct(const HidColl& c) {return c.product;}
std::wstring Hex4(USHORT v) { wchar_t b[5];swprintf(b,5,L"%04X",v);return b; }
bool HidDev::Open(const HidColl& c) {c_=c;++opens;return true;}
void HidDev::Close() {}
void HidDev::Flush() {pending.clear();}
bool HidDev::Write(const std::vector<BYTE>& q,int) {
    if(cableFailure && c_.pid==0x100E)return false;
    assert(q.size()==32 && q[0]==7 && q[1]==3 && q[2]==10 && q[3]==1);
    assert(std::all_of(q.begin()+4,q.end(),[](BYTE b){return b==0;}));
    std::vector<BYTE> r(64);r[0]=6;r[1]=14;r[2]=10;r[3]=1;
    if(!blank) {for(int i=7;i<13;++i) r[i]=BYTE(i);if(different && c_.pid!=0x100E)r[7]=99;}
    r[13]=c_.pid==0x100E?75:96;
    pending.push_back(std::vector<BYTE>(5));
    auto noise=r;noise[2]=3;pending.push_back(noise);
    noise=r;noise[1]=64;pending.push_back(noise);
    if(!invalid)pending.push_back(r);
    return true;
}
int HidDev::Read(std::vector<BYTE>& r,int ms) {
    assert(ms>0 && ms<=200);if(pending.empty())return -1;
    r=pending.front();pending.pop_front();return int(r.size());
}
static PollContext Poll(std::vector<HidColl> colls) {
    PollContext c;c.colls=std::move(colls);opens=0;PollGameSirSapjoy(c);return c;
}
int main() {
    auto cable=Collection(0x100E),receiver=Collection(0x103E),tarantula=Collection(0x1050);
    auto c=Poll({receiver,cable});
    assert(opens==2 && c.out.size()==1 && c.out[0].percent==75 && c.out[0].via==L"USB cable");
    assert(c.IsClaimed(0x3537,0x100E)&&c.IsClaimed(0x3537,0x103E));
    cableFailure=true;c=Poll({cable,receiver});assert(c.out.size()==1 && c.out[0].percent==96 && c.out[0].via==L"2.4 GHz receiver");cableFailure=false;
    different=true;c=Poll({cable,receiver});assert(c.out.size()==2 && c.out[0].key!=c.out[1].key);
    blank=true;c=Poll({cable,receiver});assert(c.out.size()==2 && c.out[0].key!=c.out[1].key);
    blank=false;different=false;tarantula.product.clear();c=Poll({tarantula});assert(c.out.size()==1&&c.out[0].name==L"GameSir controller");
    invalid=true;c=Poll({receiver});assert(c.out.empty());invalid=false;
    for(int field=0;field<6;++field) {
        auto wrong=receiver;
        switch(field) {case 0:wrong.vid=1;break;case 1:wrong.pid=0x1098;break;case 2:wrong.caps.UsagePage=1;break;
        case 3:wrong.caps.InputReportByteLength=15;break;case 4:wrong.caps.OutputReportByteLength=64;break;case 5:wrong.caps.FeatureReportByteLength=32;break;}
        c=Poll({wrong});assert(c.out.empty() && opens==0);
    }
    c={};c.colls={receiver};c.Claim(receiver.vid,receiver.pid);opens=0;PollGameSirSapjoy(c);assert(opens==0&&c.out.empty());
    puts("GameSir production driver tests passed");
}
