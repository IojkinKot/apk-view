#include "windows/archive_path.h"
#include <windows.h>
#include <algorithm>
#include <climits>
#include <map>

namespace win {
namespace {
[[noreturn]] void invalid() { throw PathError("Invalid or ambiguous Windows path"); }
struct FoldedLess {
    bool operator()(std::wstring_view a, std::wstring_view b) const {
        return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE)==CSTR_LESS_THAN;
    }
};
bool equals(std::wstring_view a,std::wstring_view b) { return !FoldedLess{}(a,b) && !FoldedLess{}(b,a); }
void check_component(std::wstring_view s) {
    if(s.empty() || s.size()>255 || s.back()==L' ' || s.back()==L'.') invalid();
    for(const auto c:s) if(c<32 || std::wstring_view(L"<>:\"/\\|?*").find(c)!=std::wstring_view::npos) invalid();
    auto base=s.substr(0,s.find(L'.'));
    while(!base.empty() && base.back()==L' ') base.remove_suffix(1);
    for(const auto reserved : {L"CON",L"PRN",L"AUX",L"NUL",L"CONIN$",L"CONOUT$"}) if(equals(base,reserved)) invalid();
    if(base.size()==4 && (equals(base.substr(0,3),L"COM") || equals(base.substr(0,3),L"LPT"))
      && std::wstring_view(L"123456789¹²³").find(base.back())!=std::wstring_view::npos) invalid();
}
template<class F> void components(std::wstring_view s,F visit) {
    unsigned depth=0;
    while(true) {
        const auto i=s.find(L'\\');
        const auto part=s.substr(0,i);
        if(++depth>64) invalid();
        check_component(part); visit(part,i==s.npos);
        if(i==s.npos) break;
        s.remove_prefix(i+1);
    }
}
bool absolute_drive(std::wstring_view s) {
    if(s.size()<3) return false;
    const auto c=s[0];
    return ((c>=L'A' && c<=L'Z') || (c>=L'a' && c<=L'z')) && s[1]==L':' && s[2]==L'\\';
}
void strip_trailing(std::wstring& s) { while(s.size()>3 && s.back()==L'\\') s.pop_back(); }
}
std::wstring to_wcx_path(std::string_view utf8) {
    if(utf8.empty() || utf8.size()>4092 || utf8.find('\\')!=utf8.npos) invalid();
    const int bytes=static_cast<int>(utf8.size());
    const int count=MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), bytes, nullptr, 0);
    if(count<=0 || count>=1024) invalid();
    std::wstring result(static_cast<std::size_t>(count),L'\0');
    if(!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), bytes, result.data(), count)) invalid();
    std::replace(result.begin(),result.end(),L'/',L'\\');
    components(result,[](auto,auto){});
    return result;
}
void validate_collisions(const std::vector<NamedPath>& paths) {
    struct Node { std::map<std::wstring_view,Node,FoldedLess> children; bool terminal{}, directory{}; };
    Node root;
    std::size_t nodes=0;
    for(const auto& item:paths) {
        auto* parent=&root;
        components(item.name,[&](std::wstring_view part,bool last){
            const auto [it,inserted]=parent->children.try_emplace(part);
            if((inserted && ++nodes>200000) || it->first!=part) invalid();
            auto& node=it->second;
            if(last) {
                if(node.terminal || (!item.directory && !node.children.empty())) invalid();
                node.terminal=true;
                node.directory=item.directory;
            } else if(node.terminal && !node.directory) invalid();
            parent=&node;
        });
    }
}
std::filesystem::path destination(const wchar_t* directory,const wchar_t* name) {
    if(!name || !*name) invalid();
    std::wstring relative(name);
    std::replace(relative.begin(),relative.end(),L'/',L'\\');
    strip_trailing(relative);
    std::wstring full=relative;
    if(directory && *directory) {
        components(relative,[](auto,auto){});
        full=directory;
        std::replace(full.begin(),full.end(),L'/',L'\\');
        strip_trailing(full);
        if(!absolute_drive(full)) invalid();
        if(full.back()!=L'\\') full+=L'\\';
        full+=relative;
    }
    if(!absolute_drive(full) || full.size()<=3 || full.size()>32750) invalid();
    components(std::wstring_view(full).substr(3),[](auto,auto){});
    return std::filesystem::path(std::move(full));
}
}
