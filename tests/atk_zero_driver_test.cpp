#include "../src/drivers/drivers.h"
#include <cassert>
#include <cstdio>
#include <deque>
static std::deque<std::vector<BYTE>> pending;
static int opens, writes;
static BYTE level=46, status=0;
static bool badHeader=false;
bool HidDev::Open(const HidColl& c) {c_=c;++opens;return true;}
void HidDev::Close() {}
void HidDev::Flush() {pending.clear();}
bool HidDev::SetFeature(const std::vector<BYTE>&) {return false;}
bool HidDev::Write(const std::vector<BYTE>& q,int) {
 ++writes;assert(q.size()==64 && q[0]==8 && q[2]==0x72 && q[3]==2 && q[7]==1);
 bool nk=c_.pid==0x1216, cable=c_.pid==0x1154;
 assert(q[5]==(cable?0:1));
 if(nk){unsigned sum=0;for(size_t i=2;i<q.size();++i)sum+=q[i];assert(q[1]==(sum&255));}
 else assert(q[1]==0x7D && (q[6]==7 || (cable&&q[6]==8)));
 std::vector<BYTE> r={8,0x72,status,0x3A,0,q[6],1,q[6]==7?level:BYTE(nk?1:2)};
 if(badHeader)r[6]=0;
 pending.push_back({8});auto wrong=r;wrong[5]=0xFF;pending.push_back(wrong);pending.push_back(r);return true;
}
int HidDev::Read(std::vector<BYTE>& r,int ms){assert(ms>0&&ms<=700);if(pending.empty())return -1;r=pending.front();pending.pop_front();return (int)r.size();}
std::wstring Hex4(USHORT n){wchar_t b[5];swprintf(b,5,L"%04X",n);return b;}
std::wstring HidProduct(const HidColl&){return L"";}
HidColl Coll(USHORT pid){HidColl c;c.vid=0x373B;c.pid=pid;c.caps.UsagePage=0xFF05;c.caps.Usage=1;c.caps.InputReportByteLength=c.caps.OutputReportByteLength=64;return c;}
PollContext Poll(std::vector<HidColl> h){opens=writes=0;PollContext c;c.colls=h;PollAtkBitmouse(c);return c;}
int main(){
 for(USHORT pid:{USHORT(0x1154),USHORT(0x124F),USHORT(0x1155)}){auto c=Poll({Coll(pid)});assert(c.out.size()==1&&c.out[0].percent==46&&c.out[0].charging==(pid==0x1154)&&c.out[0].key==L"atkzero:1154");}
 auto c=Poll({Coll(0x1155),Coll(0x124F),Coll(0x1154)});assert(c.out.size()==1&&c.out[0].via==L"USB cable"&&opens==1);assert(c.IsClaimed(0x373B,0x1155)&&c.IsClaimed(0x373B,0x124F));
 for(BYTE n:{BYTE(0),BYTE(100),BYTE(101)}){level=n;auto c=Poll({Coll(0x124F)});assert(c.out.size()==(n<=100?1u:0u));}
 level=46;status=1;assert(Poll({Coll(0x124F)}).out.empty());status=0;badHeader=true;assert(Poll({Coll(0x124F)}).out.empty());badHeader=false;
 c=Poll({Coll(0x1216)});assert(c.out.size()==1&&c.out[0].percent==46&&c.out[0].charging&&c.out[0].key==L"atknk:1216");
 for(int fault=0;fault<6;++fault){auto h=Coll(0x124F);if(fault==0)++h.vid;if(fault==1)h.pid=0xFFFF;if(fault==2)++h.caps.UsagePage;if(fault==3)++h.caps.Usage;if(fault==4)--h.caps.InputReportByteLength;if(fault==5)++h.caps.OutputReportByteLength;assert(Poll({h}).out.empty()&&opens==0);}
 puts("ATK Zero and NearLink regression tests passed");
}
