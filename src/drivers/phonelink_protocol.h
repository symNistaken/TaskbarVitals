#pragma once
#include <cwchar>
#include <cwctype>
#include <map>
#include <string>
#include <vector>
#include <utility>
namespace PhoneLink {
struct Reading { std::wstring name; int percent=-1; bool charging=false; };
namespace Detail {
struct Value {
    wchar_t kind=0;
    std::wstring text;
    std::map<std::wstring, Value> object;
    std::vector<Value> array;
};
// The card is an undocumented format. Bound nesting and size, reject malformed JSON,
// and inspect fields on their owning object rather than searching notification text.
class Parser {
    const std::wstring& input;
    size_t at=0, nodes=0;
    void Space(){while(at<input.size() && (input[at]==L' '||input[at]==L'\r'||input[at]==L'\n'||input[at]==L'\t'))++at;}
    bool Take(wchar_t c){Space();if(at==input.size()||input[at]!=c)return false;++at;return true;}
    bool String(std::wstring& out){
        if(!Take(L'"'))return false;
        while(at<input.size()){
            wchar_t c=input[at++];if(c==L'"')return true;if(c<32)return false;
            if(c!=L'\\'){out+=c;continue;}
            if(at==input.size())return false;
            c=input[at++];
            if(c==L'u'){
                unsigned v=0;
                for(int i=0;i<4;++i){if(at==input.size())return false;wchar_t h=input[at++];unsigned n;
                    if(h>=L'0'&&h<=L'9')n=h-L'0';else if(h>=L'a'&&h<=L'f')n=h-L'a'+10;else if(h>=L'A'&&h<=L'F')n=h-L'A'+10;else return false;v=v*16+n;}
                out+=static_cast<wchar_t>(v);
            }else if(c==L'"'||c==L'\\'||c==L'/')out+=c;
            else if(c==L'n')out+=L'\n';else if(c==L't')out+=L'\t';else if(c==L'r')out+=L'\r';else if(c==L'b')out+=L'\b';else if(c==L'f')out+=L'\f';else return false;
        }return false;
    }
    bool Read(Value& v,int depth){
        Space();if(depth>64||++nodes>20000||at==input.size())return false;
        wchar_t c=input[at];v.kind=c;
        if(c==L'"')return String(v.text);
        if(c==L'{'){
            ++at;if(Take(L'}'))return true;
            do{std::wstring key;Value child;if(!String(key)||!Take(L':')||!Read(child,depth+1)||v.object.count(key))return false;v.object.emplace(std::move(key),std::move(child));if(Take(L'}'))return true;}while(Take(L','));return false;
        }
        if(c==L'['){++at;if(Take(L']'))return true;do{Value child;if(!Read(child,depth+1))return false;v.array.push_back(std::move(child));if(Take(L']'))return true;}while(Take(L','));return false;}
        for(const wchar_t* literal:{L"true",L"false",L"null"}){size_t n=wcslen(literal);if(input.compare(at,n,literal)==0){at+=n;return true;}}
        if(c==L'-')++at;
        if(at==input.size())return false;
        if(input[at]==L'0')++at;else {if(input[at]<L'1'||input[at]>L'9')return false;while(at<input.size()&&input[at]>=L'0'&&input[at]<=L'9')++at;}
        if(at<input.size()&&input[at]==L'.'){++at;size_t start=at;while(at<input.size()&&input[at]>=L'0'&&input[at]<=L'9')++at;if(at==start)return false;}
        if(at<input.size()&&(input[at]==L'e'||input[at]==L'E')){++at;if(at<input.size()&&(input[at]==L'+'||input[at]==L'-'))++at;size_t start=at;while(at<input.size()&&input[at]>=L'0'&&input[at]<=L'9')++at;if(at==start)return false;}
        return true;
    }
public:
    explicit Parser(const std::wstring& s):input(s){}
    bool Parse(Value& v){if(input.size()>4*1024*1024||!Read(v,0))return false;Space();return at==input.size();}
};
inline const std::wstring* Field(const Value& v,const wchar_t* key){auto i=v.object.find(key);return i!=v.object.end()&&i->second.kind==L'"'?&i->second.text:nullptr;}
} // namespace Detail

inline bool ConnectedPhoneName(const std::wstring& speak,
                               std::wstring& name) {
    const wchar_t* suffixes[] = {
        L" - Connected to your PC",
        L" - PC に接続しました"
    };

    for (const wchar_t* suffix : suffixes) {
        const std::wstring ending(suffix);

        if (speak.size() > ending.size() &&
            speak.compare(speak.size() - ending.size(),
                          ending.size(), ending) == 0) {
            name = speak.substr(0, speak.size() - ending.size());
            return !name.empty();
        }
    }

    return false;
}

inline int BatteryFromTooltip(const std::wstring& tooltip,
                              bool& charging) {
    const std::wstring english = L"Battery ";
    const std::wstring japanese = L"バッテリー 残量 ";

    bool en = tooltip.compare(0, english.size(), english) == 0;
    bool ja = tooltip.compare(0, japanese.size(), japanese) == 0;

    if (!en && !ja) return -1;

    size_t start = en ? english.size() : japanese.size();
    size_t end = start;

    while (end < tooltip.size() &&
           tooltip[end] >= L'0' && tooltip[end] <= L'9' &&
           end - start < 3) {
        ++end;
    }

    if (end == start || end >= tooltip.size() ||
        tooltip[end] != L'%') return -1;

    int level = std::stoi(tooltip.substr(start, end - start));
    if (level > 100) return -1;

    const std::wstring remainder = tooltip.substr(end + 1);

    if (en) {
        if (remainder != L" remaining" &&
            remainder != L" remaining, charging")
            return -1;

        charging = remainder == L" remaining, charging";
    } else {
        if (!remainder.empty()) return -1;
        charging = false;  // Charging state not established for Japanese cards.
    }

    return level;
}



namespace Detail {
inline void Battery(const Value& value,bool japanese,int& percent,bool& charging,bool& ambiguous){
    if(value.kind==L'['){for(const auto& child:value.array)Battery(child,japanese,percent,charging,ambiguous);return;}
    if(value.kind!=L'{')return;
    const auto* type=Field(value,L"type");
    if(type && *type!=L"AdaptiveCard" && *type!=L"Container" && *type!=L"ColumnSet" && *type!=L"Column" && *type!=L"TextBlock" && *type!=L"Image")return;
    if(const auto* tooltip=Field(value,L"tooltip")){
        bool ja=tooltip->rfind(L"バッテリー 残量 ",0)==0, charge=false;
        int n=ja==japanese?BatteryFromTooltip(*tooltip,charge):-1;
        const auto* text=Field(value,L"text");
        if(n>=0 && (!text || *text==std::to_wstring(n)+L"%")){
            if(percent>=0 && (percent!=n||charging!=charge))ambiguous=true;
            percent=n;charging=charge;
        }
    }
    // Only traverse card layout fields, never actions, arbitrary metadata or notification payloads.
    for(const wchar_t* field:{L"body",L"items",L"columns"}){auto i=value.object.find(field);if(i!=value.object.end())Battery(i->second,japanese,percent,charging,ambiguous);}
}
// Other Windows languages: the card's battery is a Container whose tooltip is localized text and whose
// TextBlock holds only the level ("44%", "%44", "44 %"). The level must also appear as a number in the tooltip.
inline int PercentText(std::wstring s){
    auto space=[](wchar_t c){return c==L' '||c==L' '||c==L' ';};
    while(!s.empty()&&space(s.back()))s.pop_back();
    while(!s.empty()&&space(s.front()))s.erase(0,1);
    if(s.size()<2)return -1;
    if(s.back()==L'%')s.pop_back();else if(s.front()==L'%')s.erase(0,1);else return -1;
    while(!s.empty()&&space(s.back()))s.pop_back();
    while(!s.empty()&&space(s.front()))s.erase(0,1);
    if(s.empty()||s.size()>3)return -1;
    for(wchar_t c:s)if(c<L'0'||c>L'9')return -1;
    int n=std::stoi(s);return n<=100?n:-1;
}
inline bool HasNumber(const std::wstring& text,int n){
    const std::wstring digits=std::to_wstring(n);
    for(size_t at=text.find(digits);at!=std::wstring::npos;at=text.find(digits,at+1)){
        bool before=at>0&&text[at-1]>=L'0'&&text[at-1]<=L'9', after=at+digits.size()<text.size()&&text[at+digits.size()]>=L'0'&&text[at+digits.size()]<=L'9';
        if(!before&&!after)return true;
    }
    return false;
}
inline void AnyLocaleBattery(const Value& value,int& percent,bool& ambiguous,bool& known){
    if(value.kind==L'['){for(const auto& child:value.array)AnyLocaleBattery(child,percent,ambiguous,known);return;}
    if(value.kind!=L'{')return;
    const auto* type=Field(value,L"type");
    if(type && *type!=L"AdaptiveCard" && *type!=L"Container" && *type!=L"ColumnSet" && *type!=L"Column" && *type!=L"TextBlock" && *type!=L"Image")return;
    if(const auto* tooltip=Field(value,L"tooltip")){
        if(tooltip->rfind(L"Battery ",0)==0||tooltip->rfind(L"バッテリー 残量 ",0)==0)known=true;
        auto items=value.object.find(L"items");
        if(items!=value.object.end())for(const auto& child:items->second.array){
            const auto* childType=Field(child,L"type");const auto* text=Field(child,L"text");
            int n=childType&&*childType==L"TextBlock"&&text?PercentText(*text):-1;
            if(n<0||!HasNumber(*tooltip,n))continue;
            if(percent>=0&&percent!=n)ambiguous=true;
            percent=n;
        }
    }
    for(const wchar_t* field:{L"body",L"items",L"columns"}){auto i=value.object.find(field);if(i!=value.object.end())AnyLocaleBattery(i->second,percent,ambiguous,known);}
}
// "Name - <localized status>": the name is the part before a " - " that the card also shows as a top-level TextBlock.
inline bool CardName(const Value& root,const std::wstring& speak,std::wstring& name){
    auto body=root.object.find(L"body");
    if(body==root.object.end())return false;
    for(size_t at=speak.rfind(L" - ");at!=std::wstring::npos&&at>0;at=speak.rfind(L" - ",at-1)){
        const std::wstring prefix=speak.substr(0,at);
        for(const auto& item:body->second.array){
            const auto* type=Field(item,L"type");const auto* text=Field(item,L"text");
            if(type&&*type==L"TextBlock"&&text&&*text==prefix){name=prefix;return true;}
        }
    }
    return false;
}
} // namespace Detail
inline bool Parse(const std::wstring& json,Reading& out){
    Detail::Value root;
    if(!Detail::Parser(json).Parse(root)||root.kind!=L'{')return false;
    const auto* speak=Detail::Field(root,L"speak");Reading parsed;
    if(!speak)return false;
    if(!ConnectedPhoneName(*speak,parsed.name)){
        // Not English or Japanese. A card in a known language with another status (e.g. disconnected) is still rejected.
        bool ambiguous=false,known=false;
        Detail::AnyLocaleBattery(root,parsed.percent,ambiguous,known);
        if(known||ambiguous||parsed.percent<0||!Detail::CardName(root,*speak,parsed.name))return false;
        parsed.charging=false;  // The charging wording is localized; not read for these languages.
        out=std::move(parsed);return true;
    }
    const std::wstring suffix=L" - PC に接続しました";
    bool ja=speak->size()>=suffix.size() && speak->compare(speak->size()-suffix.size(),suffix.size(),suffix)==0;
    bool ambiguous=false;
    Detail::Battery(root,ja,parsed.percent,parsed.charging,ambiguous);
    if(ambiguous||parsed.percent<0)return false;
    out=std::move(parsed);return true;
}
} // namespace PhoneLink
