#include "../src/history_model.h"
#include <cassert>
#include <cstdio>

int main() {
    using namespace battery_history;
    const int64_t now = 1795000000;
    Sample a{L"coolermaster:demo", now-7200, 85, false};
    assert(Valid(a));
    Sample decoded;
    assert(Decode(Encode(a).substr(0, Encode(a).size()-1), decoded));
    assert(decoded.key==a.key && decoded.percent==a.percent && decoded.timestamp==a.timestamp);
    assert(!Decode("1\tevil\t20\t0", decoded));
    assert(!Decode("1795000000\t00000001\t101\t0", decoded));
    assert(!Decode("1795000000\t00000001\t15\t5", decoded));
    assert(!Decode("1795000000\t00000000\t15\t0", decoded));
    std::wstring unicode = L"Mysz:ą🎧";
    assert(UnhexKey(HexKey(unicode), decoded.key) && decoded.key == unicode);
    assert(!UnhexKey("GGGGGGGG", decoded.key));
    assert(!UnhexKey("FFFF", decoded.key));
    Store s;
    assert(s.Record(a));
    assert(!s.Record(a));
    assert(!s.Record({a.key, a.timestamp+10, 80, true}));
    assert(!s.Record({a.key, a.timestamp+100, 80, false}));
    assert(s.Record({a.key, a.timestamp+100, 80, true})); // state flip
    assert(!s.Record({a.key, a.timestamp+150, 79, false})); // one-minute minimum
    assert(s.Record({a.key, a.timestamp+200, 78, false}));
    assert(!s.Record({a.key, a.timestamp+300, 77, false}));
    assert(s.Record({L"bt:other", a.timestamp+600, 70, false}));
    assert(s.Record({a.key, a.timestamp+2200, 60, false}));
    auto stats=s.Summarize(a.key,now-10000,now);
    assert(stats.count==4 && stats.first==85 && stats.last==60 && stats.lowest==60 && stats.highest==85);
    auto small=s.Summarize(a.key,a.timestamp+201,now);
    assert(small.count==1 && small.first==60);
    assert(s.For(a.key, now-200,now).empty());
    std::string persisted=s.Serialize();
    Store loaded; loaded.Load(persisted, now);
    assert(loaded.samples.size()==s.samples.size());
    loaded.Clear(a.key);
    assert(loaded.For(a.key, now-10000,now).empty());
    assert(loaded.For(L"bt:other", now-10000,now).size()==1);
    loaded.Load(persisted+"999\tBROKEN",now);
    assert(loaded.samples.size()==s.samples.size()); // ignore truncated row
    Store retention;
    assert(retention.Record({L"old",now-kRetentionSeconds-1,30,false}));
    assert(retention.Record({L"fresh",now,70,false}));
    assert(retention.samples.size()==1);
    assert(retention.samples[0].key==L"fresh");
    Store cap;
    for(int i=0;i<16500;i++) cap.Record({std::to_wstring(i), now,50,false});
    assert(cap.samples.size()==kMaxSamples);
    puts("Battery History & Insights policy tests passed");
}
