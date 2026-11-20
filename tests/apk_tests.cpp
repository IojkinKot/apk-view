#include "apk/package.h"
#include "fixture_builder.h"
#include <iostream>
#include <windows.h>

using namespace apk;
void check(bool pass, const char* message) { if(!pass) throw std::runtime_error(message); }
template<class Fn> void fails(Fault expected, Fn fn) {
    try { fn(); } catch(const Error& e) { check(e.fault==expected,e.what()); return; }
    throw std::runtime_error("expected reader error");
}
int wmain(int argc,wchar_t** argv) {
    if(argc==2){
        try {
            auto p=Package::open(argv[1]);
            for(std::size_t i=0;i<p->entries().size();++i)p->consume(i);
            p->finish(); return 0;
        } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
    }
    const auto path=std::filesystem::temp_directory_path() / (L"apkview-parser-"+std::to_wstring(GetCurrentProcessId())+L".apk");
    struct Cleanup { std::filesystem::path p; ~Cleanup(){std::error_code ec;std::filesystem::remove(p,ec);} } cleanup{path};
    try {
        std::vector<int> wrappers{0,2};
#ifdef APKVIEW_HAVE_ZLIB
        wrappers.insert(wrappers.end(),{1,3});
#endif
        for(int mode:wrappers) {
            fixture::save(path,fixture::wrap(fixture::package(mode!=0,true),mode));
            check(Package::probe(path),"valid package not detected");
            auto p=Package::open(path);
            const auto& list=p->entries();
            check(list.size()==4,"directory plus three files");
            check(list[0].kind==Kind::directory && list[0].path=="usr/share","directory listing");
            check(list[1].size==0 && list[1].kind==Kind::file,"empty file");
            check(list[2].path==u8"usr/share/файл.txt" && list[2].size==3,"Unicode/size");
            check(list[3].kind==Kind::special,"link visible");
            p->consume(0); p->consume(1);
            std::string result;
            p->consume(2,[&](auto b,auto n){result.append(reinterpret_cast<const char*>(b),n);});
            p->consume(3); p->finish();
            check(result=="abc","payload mismatch");
        }
        fixture::save(path,fixture::package(false,false,true));
        {auto p=Package::open(path); check(p->entries().empty(),"not empty"); p->finish();}
        auto reject=[&](const fixture::Bytes& b,Fault fault){
            fixture::save(path,b); check(!Package::probe(path) || fault==Fault::corrupt,"unsupported file probed true");
            fails(fault,[&]{ Package::open(path); });
        };
        reject({'P','K',3,4},Fault::format);
        auto wrongSchema=fixture::package(); wrongSchema[4]='i'; reject(wrongSchema,Fault::format);
        reject(fixture::wrap(fixture::package(),4),Fault::unsupported);
        auto invalid=fixture::package(); invalid[16]=0xff; invalid[17]=0xff; invalid[18]=0xff;
        reject(invalid,Fault::corrupt);
        auto broken=[&](const fixture::Bytes& b){
            fixture::save(path,b);
            auto p=Package::open(path);
            check(p->entries().size()==4,"metadata-only list should work");
            fails(Fault::corrupt,[&]{for(std::size_t i=0;i<4;++i)p->consume(i); p->finish();});
        };
        broken(fixture::package(false,false,false,1));
        broken(fixture::package(false,false,false,2,"abcd"));
        auto shortData=fixture::package(); shortData.pop_back(); broken(shortData);
        auto tail=fixture::package(); tail.push_back(0); broken(tail);
#ifdef APKVIEW_HAVE_ZLIB
        auto compressedTail=fixture::wrap(fixture::package(),1); compressedTail.push_back(0); broken(compressedTail);
#endif
        std::cout<<"APK reader tests passed\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
