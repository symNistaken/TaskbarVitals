// Production poller with simulated HID replies; no hardware is accessed.
#include "../src/drivers/drivers.h"
#include <cassert>
#include <cstdio>
static std::vector<BYTE> reply;
static int opens;
static bool openOk=true;
bool HidDev::Open(const HidColl& c){++opens;c_=c;return openOk;}
void HidDev::Close(){}
void HidDev::Flush(){}
int HidDev::SetOutputAndRead(const std::vector<BYTE>& q,std::vector<BYTE>& r,int ms){
 std::vector<BYTE> expected(64);expected[0]=6;expected[1]=5;assert(q==expected&&ms==500);
 r=reply;return r.empty()?-1:(int)r.size();
}
HidColl Collection(){HidColl c;c.vid=0x7768;c.pid=0x6434;c.caps.UsagePage=0xFFB2;c.caps.Usage=1;c.caps.InputReportByteLength=c.caps.OutputReportByteLength=64;return c;}
int main(){
 for(int level:{0,90,100,101}){PollContext c;c.colls={Collection()};reply.assign(64,0);reply[0]=6;reply[1]=5;reply[2]=0x26;reply[13]=level;PollPressPlayIcarus(c);assert(c.out.size()==(level<=100?1u:0u));if(level<=100){assert(c.out[0].percent==level);int before=opens;PollPressPlayIcarus(c);assert(opens==before);}}
 for(int fault=0;fault<5;fault++){PollContext c;c.colls={Collection()};reply.assign(64,0);reply[0]=6;reply[1]=5;reply[2]=0x26;reply[13]=90;if(fault<3)reply[fault]^=1;if(fault==3)reply.resize(13);if(fault==4)reply.clear();PollPressPlayIcarus(c);assert(c.out.empty());}
 for(int fault=0;fault<6;fault++){auto h=Collection();if(fault==0)h.vid++;if(fault==1)h.pid++;if(fault==2)h.caps.UsagePage++;if(fault==3)h.caps.Usage++;if(fault==4)h.caps.InputReportByteLength--;if(fault==5)h.caps.OutputReportByteLength--;PollContext c;c.colls={h};opens=0;PollPressPlayIcarus(c);assert(!opens&&c.out.empty());}
 openOk=false;PollContext c;c.colls={Collection()};PollPressPlayIcarus(c);assert(c.out.empty());puts("Press Play tests passed");
}
