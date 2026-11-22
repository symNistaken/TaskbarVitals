#include "../src/drivers/drivers.h"
#include <cassert>
#include <cstdio>
static int opens,sets;
static BYTE command,link=1,level=94,marker=0x11;
static bool corrupt=false,fail=false;
bool HidDev::OpenFeatureOnly(const HidColl& c){c_=c;++opens;return !fail;}
void HidDev::Close(){}
bool HidDev::SetFeature(const std::vector<BYTE>& q){assert(q.size()==8&&q[0]==5&&(q[1]==0x80||q[1]==0x90));for(size_t i=2;i<q.size();++i)assert(q[i]==0);command=q[1];++sets;return true;}
bool HidDev::GetFeature(BYTE id,std::vector<BYTE>& r){assert(id==5);r={5,command,command==0x80?link:marker,level,0,0,0,0};if(corrupt)r[1]^=1;return true;}
HidColl Coll(){HidColl c;c.vid=0x258A;c.pid=0x002F;c.iface=1;c.caps.UsagePage=0xFF00;c.caps.Usage=1;c.caps.FeatureReportByteLength=8;return c;}
PollContext Poll(HidColl h){opens=sets=0;PollContext c;c.colls={h};PollSinowealthReceiver(c);return c;}
int main(){
 for(BYTE n:{BYTE(0),BYTE(94),BYTE(100),BYTE(101)}){level=n;auto c=Poll(Coll());assert(c.out.size()==(n<=100?1u:0u));if(n<=100){assert(c.out[0].percent==n&&!c.out[0].charging);PollSinowealthReceiver(c);assert(opens==1);}}
 level=94;link=0;assert(Poll(Coll()).out.empty()&&sets==1);link=1;marker=0x10;assert(Poll(Coll()).out.empty());marker=0x11;corrupt=true;assert(Poll(Coll()).out.empty());corrupt=false;fail=true;assert(Poll(Coll()).out.empty()&&sets==0);fail=false;
 for(int f=0;f<8;++f){auto h=Coll();if(f==0)++h.vid;if(f==1)++h.pid;if(f==2)++h.iface;if(f==3)++h.caps.UsagePage;if(f==4)++h.caps.Usage;if(f==5)++h.caps.FeatureReportByteLength;if(f==6)++h.caps.InputReportByteLength;if(f==7)++h.caps.OutputReportByteLength;assert(Poll(h).out.empty()&&opens==0);}
 puts("Sinowealth receiver tests passed");
}
