#include "wcx/sdk.h"
#include "fixture_builder.h"
#include <cstring>
#include <iostream>
#include <iterator>

using namespace wcx;
namespace fs=std::filesystem;
void check(bool yes,const char* what){if(!yes)throw std::runtime_error(what);}
template<class F> F proc(HMODULE dll,const char* name){
    auto raw=GetProcAddress(dll,name); check(raw!=nullptr,name);
    static_assert(sizeof(F)==sizeof(raw)); F f; std::memcpy(&f,&raw,sizeof(f)); return f;
}
struct Module {
    HMODULE dll;
    explicit Module(PCWSTR path):dll(LoadLibraryW(path)){check(dll!=nullptr,"LoadLibraryW");}
    ~Module(){FreeLibrary(dll);}
};
struct Api {
    HANDLE(WINAPI* open)(tOpenArchiveDataW*);
    int(WINAPI* read)(HANDLE,tHeaderDataExW*);
    int(WINAPI* process)(HANDLE,int,WCHAR*,WCHAR*);
    int(WINAPI* close)(HANDLE);
    void(WINAPI* progress)(HANDLE,tProcessDataProcW);
    BOOL(WINAPI* probe)(WCHAR*);
    explicit Api(HMODULE dll):open(proc<decltype(open)>(dll,"OpenArchiveW")),read(proc<decltype(read)>(dll,"ReadHeaderExW")),
        process(proc<decltype(process)>(dll,"ProcessFileW")),close(proc<decltype(close)>(dll,"CloseArchive")),
        progress(proc<decltype(progress)>(dll,"SetProcessDataProcW")),probe(proc<decltype(probe)>(dll,"CanYouHandleThisFileW")) {
        check(proc<int(WINAPI*)()>(dll,"GetPackerCaps")()==PK_CAPS_BY_CONTENT,"caps");
        proc<void(WINAPI*)(HANDLE,tChangeVolProcW)>(dll,"SetChangeVolProcW");
    }
};
struct Archive {
    Api& api; HANDLE h{};
    Archive(Api& a,const fs::path& p,int mode):api(a){
        auto path=p.wstring(); tOpenArchiveDataW settings{}; settings.ArcName=path.data(); settings.OpenMode=mode;
        h=api.open(&settings); if(!h || settings.OpenResult) throw std::runtime_error("OpenArchiveW: "+std::to_string(settings.OpenResult));
    }
    ~Archive(){api.close(h);}
    tHeaderDataExW next(){tHeaderDataExW d{};check(api.read(h,&d)==0,"ReadHeaderExW");return d;}
    int action(int op,WCHAR* destPath=nullptr,WCHAR* destName=nullptr){return api.process(h,op,destPath,destName);}
    void done(){tHeaderDataExW d{};check(api.read(h,&d)==E_END_ARCHIVE,"end of archive");}
    void selectFile(){next();check(action(PK_SKIP)==0,"skip dir");next();check(action(PK_SKIP)==0,"skip empty");next();}
};
static int cancelledBytes;
int WINAPI cancel(WCHAR* name,int bytes){ if(name && std::wstring_view(name)==L"usr\\share\\файл.txt")cancelledBytes+=bytes; return 0; }
std::string read_file(const fs::path& p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
int wmain(int argc,wchar_t** argv){
    try {
        check(argc>=2,"DLL path required"); Module lib(argv[1]); Api api(lib.dll);
        const auto root=fs::temp_directory_path()/(L"apkview-wcx-"+std::to_wstring(GetCurrentProcessId()));
        check(fs::create_directory(root),"private test directory");
        struct Cleanup{fs::path p;~Cleanup(){std::error_code e;fs::remove_all(p,e);}} cleanup{root};
        if(argc==3) {
            Archive a(api,argv[2],PK_OM_EXTRACT);
            bool found=false;
            for(;;){ tHeaderDataExW h{}; auto rc=api.read(a.h,&h); if(rc==E_END_ARCHIVE)break;
                check(rc==0,"real package header");
                if(std::wstring_view(h.FileName)==L"usr\\lib\\libz.so.1.3.1") {
                    auto file=(root/L"libz.so.1.3.1").wstring(); check(h.UnpSize==77827 && h.UnpSizeHigh==0,"real file header size");
                    check(a.action(PK_EXTRACT,nullptr,file.data())==0,"real file extraction and SHA-256");
                    const auto contents=read_file(file); check(contents.size()==77827 && contents.substr(0,4)=="\x7f" "ELF","real ELF bytes"); found=true;
                } else check(a.action(PK_TEST)==0,"real file integrity");
            }
            check(found,"real zlib file found"); std::cout<<"Real OpenWrt package passed\n"; return 0;
        }
        const auto path=root/L"пакет.payload";
        fixture::save(path,fixture::package());
        auto probe=path.wstring(); check(api.probe(probe.data()),"content-based detection");
        auto android=(root/L"android.apk").wstring();fixture::save(android,{'P','K',3,4});
        check(!api.probe(android.data()),"Android ZIP detection");
        tOpenArchiveDataW bad{};bad.ArcName=android.data();check(!api.open(&bad)&&bad.OpenResult==E_UNKNOWN_FORMAT,"Android open rejected");
        {
            Archive list(api,path,PK_OM_LIST), run(api,path,PK_OM_EXTRACT);
            const auto dir=list.next();check(std::wstring_view(dir.FileName)==L"usr\\share" && (dir.FileAttr&FILE_ATTRIBUTE_DIRECTORY),"directory header");
            check(list.action(PK_SKIP)==0,"list skip");
            const auto out=(root/L"renamed.txt").wstring(); auto fullname=out;
            run.selectFile(); check(run.action(PK_EXTRACT,nullptr,fullname.data())==0 && read_file(out)=="abc","full destination extraction");
            const auto link=run.next();check(std::wstring_view(link.FileName)==L"usr\\share\\link","link listed");
            check(run.action(PK_EXTRACT,nullptr,fullname.data())==E_NOT_SUPPORTED,"link not extracted"); run.done();
            check(list.next().UnpSize==0 && list.action(PK_SKIP)==0,"empty listed independently");
            check(std::wstring_view(list.next().FileName)==L"usr\\share\\файл.txt" && list.action(PK_SKIP)==0,"Unicode list name");
            list.next();check(list.action(PK_SKIP)==0,"list links");list.done();
        }
        {
            Archive test(api,path,PK_OM_EXTRACT);
            for(int i=0;i<4;++i){test.next();check(test.action(PK_TEST)==0,"test including empty and link");} test.done();
        }
        {
            Archive a(api,path,PK_OM_EXTRACT); a.selectFile(); api.progress(a.h,cancel);
            auto dir=root.wstring(); WCHAR name[]=L"cancelled.txt";
            check(a.action(PK_EXTRACT,dir.data(),name)==E_EABORTED && cancelledBytes==3 && !fs::exists(root/name),"cancel and clean up");
        }
        {
            fixture::save(path,fixture::package(false,false,false,2,"abc",u8"файл.txt",true));
            Archive a(api,path,PK_OM_EXTRACT); a.selectFile();
            auto dir=root.wstring(); WCHAR name[]=L"renamed.txt";
            check(a.action(PK_EXTRACT,dir.data(),name)==E_BAD_DATA && read_file(root/name)=="abc","hash failure preserves prior file");
        }
#ifndef _WIN64
        {
            auto open=proc<HANDLE(WINAPI*)(tOpenArchiveData*)>(lib.dll,"OpenArchive");
            auto read=proc<int(WINAPI*)(HANDLE,tHeaderData*)>(lib.dll,"ReadHeader");
            auto process=proc<int(WINAPI*)(HANDLE,int,char*,char*)>(lib.dll,"ProcessFile");
            proc<int(WINAPI*)(HANDLE,tHeaderDataEx*)>(lib.dll,"ReadHeaderEx");
            proc<void(WINAPI*)(HANDLE,tProcessDataProc)>(lib.dll,"SetProcessDataProc");
            proc<void(WINAPI*)(HANDLE,tChangeVolProc)>(lib.dll,"SetChangeVolProc");
            // The path itself may be outside the active ANSI code page; use the W opener.
            Archive a(api,path,PK_OM_LIST); tHeaderData h{};
            check(read(a.h,&h)==0 && std::string_view(h.FileName)=="usr\\share" && process(a.h,PK_SKIP,nullptr,nullptr)==0,"ANSI compatibility");
            (void)open;
        }
#endif
        std::cout<<"WCX DLL tests passed\n"; return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
