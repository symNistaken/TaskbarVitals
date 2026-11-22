// Exercise the production Razer poller with a simulated feature-report mailbox.
#include "../src/drivers/drivers.h"
#include <cassert>
#include <map>
#include <cstdio>
struct State { bool online; int raw; bool charging; };
static std::map<USHORT,State> states;
static std::vector<BYTE> request;
bool HidDev::OpenFeatureOnly(const HidColl& c){c_=c;return true;}
void HidDev::Close(){}
bool HidDev::SetFeature(const std::vector<BYTE>& q){assert(q.size()==91&&q[7]==7&&(q[8]==0x80||q[8]==0x84));request=q;return true;}
bool HidDev::GetFeature(BYTE,std::vector<BYTE>& r){r=request;auto s=states.at(c_.pid);r[1]=s.online?2:4;r[10]=request[8]==0x80?s.raw:s.charging;BYTE crc=0;for(int i=3;i<89;i++)crc^=r[i];r[89]=crc;return true;}
std::wstring HidProduct(const HidColl&){return L"Razer Test Keyboard";}
std::wstring Hex4(USHORT v){wchar_t b[8];swprintf(b,8,L"%04X",v);return b;}
int Clamp100(int x){return x<0?0:x>100?100:x;}
DeviceKind GuessKind(const std::wstring&){return DeviceKind::Keyboard;}
DeviceKind KindFromCollections(const std::vector<HidColl>&,USHORT,USHORT){return DeviceKind::Keyboard;}
HidColl Coll(USHORT pid){HidColl c;c.vid=0x1532;c.pid=pid;c.caps.FeatureReportByteLength=91;return c;}
int main(){
 for(bool reverse:{false,true})for(bool firstOnline:{false,true})for(int raw:{0,38,255}){
  states={{0x02D5,{firstOnline,firstOnline?raw:0,firstOnline}},{0x02D7,{!firstOnline,firstOnline?0:raw,!firstOnline}}};
  PollContext c;c.colls=reverse?std::vector<HidColl>{Coll(0x02D7),Coll(0x02D5)}:std::vector<HidColl>{Coll(0x02D5),Coll(0x02D7)};
  DeviceStatus other;other.name=L"Razer Test Keyboard";other.key=L"other";other.percent=77;c.out.push_back(other);
  PollRazer(c);assert(c.out.size()==2&&c.out[0].percent==77&&!c.out[0].charging);auto s=c.out[1];assert(s.connected&&s.percent==(raw*100+127)/255&&s.charging&&s.key==L"razer:02D5");assert(c.IsClaimed(0x1532,0x02D5)&&c.IsClaimed(0x1532,0x02D7));
 }
 states={{0x02D5,{false,0,false}},{0x02D7,{false,0,false}}};PollContext c;c.colls={Coll(0x02D5),Coll(0x02D7)};PollRazer(c);assert(c.out.size()==1&&!c.out[0].connected&&c.out[0].percent==-1);puts("Razer connection tests passed");
}
