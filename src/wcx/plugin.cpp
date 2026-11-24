#include "wcx/sdk.h"
#include "apk/package.h"
#include "windows/archive_path.h"
#include "windows/output_file.h"
#include <bcrypt.h>
#include <algorithm>
#include <limits>
#include <utility>

namespace {
using namespace wcx;
struct Status { int code; };
int error() noexcept {
    try {throw;}
    catch(const Status& e){return e.code;}
    catch(const apk::Error& e){
        switch(e.fault){
        case apk::Fault::open:return E_EOPEN;
        case apk::Fault::read:return E_EREAD;
        case apk::Fault::unsupported:return E_NOT_SUPPORTED;
        case apk::Fault::format:return E_UNKNOWN_FORMAT;
        case apk::Fault::corrupt:return E_BAD_ARCHIVE;
        }
        return E_BAD_ARCHIVE;
    }
    catch(const win::PathError&){return E_BAD_ARCHIVE;}
    catch(const win::OutputError& e){return e.fault==win::OutputFault::create?E_ECREATE:E_EWRITE;}
    catch(const std::bad_alloc&){return E_NO_MEMORY;}
    catch(...){return E_BAD_ARCHIVE;}
}
struct Context {
    std::unique_ptr<apk::Package> pkg;
    std::vector<win::NamedPath> names;
    std::size_t index{};
    int mode{}, failure{};
    bool pending{}, finished{};
    tProcessDataProcW progressW{};
    tProcessDataProc progressA{};
};
Context& context(HANDLE h){if(!h)throw Status{E_BAD_ARCHIVE};return *static_cast<Context*>(h);}
HANDLE open(const wchar_t* path,int mode){
    if(!path || !*path) throw Status{E_EOPEN};
    if(mode!=PK_OM_LIST && mode!=PK_OM_EXTRACT) throw Status{E_NOT_SUPPORTED};
    auto ctx=std::make_unique<Context>();
    ctx->mode=mode;
    ctx->pkg=apk::Package::open(path);
    ctx->names.reserve(ctx->pkg->entries().size());
    for(const auto& entry:ctx->pkg->entries()) ctx->names.push_back({win::to_wcx_path(entry.path),entry.kind==apk::Kind::directory});
    win::validate_collisions(ctx->names);
    return ctx.release();
}
int dos_time(std::uint64_t seconds){
    if(seconds==0 || seconds>4354819199ull)return 0;
    ULARGE_INTEGER ticks{};
    ticks.QuadPart=(seconds+11644473600ull)*10000000ull;
    const FILETIME fileTime{ticks.LowPart,ticks.HighPart};
    FILETIME local{}; WORD date=0,time=0;
    if(!FileTimeToLocalFileTime(&fileTime,&local)||!FileTimeToDosDateTime(&local,&date,&time))return 0;
    return static_cast<int>((static_cast<unsigned>(date)<<16)|time);
}
const apk::Entry& current(Context& ctx){
    if(ctx.failure)throw Status{ctx.failure};
    if(ctx.index==ctx.names.size()){
        if(!ctx.finished){
            if(ctx.mode==PK_OM_EXTRACT) {
                try{ctx.pkg->finish();}catch(...){ctx.failure=error();throw;}
            }
            ctx.finished=true;
        }
        throw Status{E_END_ARCHIVE};
    }
    return ctx.pkg->entries()[ctx.index];
}
template<class Char,std::size_t N> void copy_name(Char (&dest)[N],std::basic_string_view<Char> src){
    if(src.size()>=N) throw Status{E_SMALL_BUF};
    std::copy(src.begin(),src.end(),dest); dest[src.size()]={};
}
template<class Header> void attributes(Header& out,const apk::Entry& entry){
    out.Flags=0;out.HostOS=0;out.FileCRC=0;out.UnpVer=0;out.Method=0;
    out.FileTime=dos_time(entry.mtime);
    out.FileAttr=entry.kind==apk::Kind::directory?FILE_ATTRIBUTE_DIRECTORY:FILE_ATTRIBUTE_ARCHIVE;
    out.ArcName[0]={};out.CmtSize=0;out.CmtState=0;
}
template<class Char> void lengths(HeaderDataEx<Char>& out,const apk::Entry& entry){
    out.PackSize=0;out.PackSizeHigh=0;
    out.UnpSize=static_cast<unsigned>(entry.size);out.UnpSizeHigh=static_cast<unsigned>(entry.size>>32);
}
std::wstring from_ansi(const char* text){
    if(!text)return {};
    const int len=MultiByteToWideChar(CP_ACP,MB_ERR_INVALID_CHARS,text,-1,nullptr,0);
    if(len<1)throw Status{E_BAD_ARCHIVE};
    std::wstring s(static_cast<std::size_t>(len),L'\0');
    if(!MultiByteToWideChar(CP_ACP,MB_ERR_INVALID_CHARS,text,-1,s.data(),len))throw Status{E_BAD_ARCHIVE};
    s.pop_back();return s;
}
std::string to_ansi(const std::wstring& text){
    BOOL used=FALSE;
    const auto codePage=GetACP();
    const DWORD flags=codePage==CP_UTF8?WC_ERR_INVALID_CHARS:WC_NO_BEST_FIT_CHARS;
    auto indicator=codePage==CP_UTF8?nullptr:&used;
    const int len=WideCharToMultiByte(codePage,flags,text.c_str(),-1,nullptr,0,nullptr,indicator);
    if(len<1 || used)throw Status{E_SMALL_BUF};
    std::string s(static_cast<std::size_t>(len),'\0');
    if(!WideCharToMultiByte(codePage,flags,text.c_str(),-1,s.data(),len,nullptr,indicator)||used)throw Status{E_SMALL_BUF};
    s.pop_back();return s;
}
class Sha256 {
    BCRYPT_ALG_HANDLE alg_{};
    BCRYPT_HASH_HANDLE hash_{};
    static void check(NTSTATUS s){if(s<0)throw Status{E_BAD_DATA};}
public:
    Sha256(){
        check(BCryptOpenAlgorithmProvider(&alg_,BCRYPT_SHA256_ALGORITHM,nullptr,0));
        try{check(BCryptCreateHash(alg_,&hash_,nullptr,0,nullptr,0,0));}
        catch(...){BCryptCloseAlgorithmProvider(alg_,0);throw;}
    }
    Sha256(const Sha256&)=delete;
    ~Sha256(){BCryptDestroyHash(hash_);BCryptCloseAlgorithmProvider(alg_,0);}
    void update(const std::uint8_t* data,std::size_t size){
        check(BCryptHashData(hash_,const_cast<PUCHAR>(data),static_cast<ULONG>(size),0));
    }
    std::array<std::uint8_t,32> finish(){
        std::array<std::uint8_t,32> result{};
        check(BCryptFinishHash(hash_,result.data(),static_cast<ULONG>(result.size()),0));return result;
    }
};
std::filesystem::path output_path(const wchar_t* dir,const wchar_t* name){
    try{return win::destination(dir,name);}catch(const win::PathError&){throw Status{E_ECREATE};}
}
void progress(Context& ctx,int size){
    const auto& name=ctx.names[ctx.index].name;
    int proceed=1;
    if(ctx.progressW){auto value=name;proceed=ctx.progressW(value.data(),size);}
    else if(ctx.progressA){
        std::string value;
        try{value=to_ansi(name);}catch(const Status&){}
        proceed=ctx.progressA(value.empty()?nullptr:value.data(),size);
    }
    if(!proceed)throw Status{E_EABORTED};
}
int process(Context& ctx,int op,const wchar_t* dir,const wchar_t* name){
    if(ctx.failure)return ctx.failure;
    if(!ctx.pending || ctx.index>=ctx.names.size()) return E_BAD_ARCHIVE;
    if(op!=PK_SKIP && op!=PK_TEST && op!=PK_EXTRACT) return E_NOT_SUPPORTED;
    if(ctx.mode==PK_OM_LIST){if(op!=PK_SKIP)return E_NOT_SUPPORTED;}
    else {
        const auto& entry=ctx.pkg->entries()[ctx.index];
        if(op==PK_EXTRACT && entry.kind==apk::Kind::special){
            ctx.pkg->consume(ctx.index++); ctx.pending=false;
            return E_NOT_SUPPORTED;
        }
        if(op==PK_SKIP || entry.kind!=apk::Kind::file){
            if(op==PK_EXTRACT)win::create_directory(output_path(dir,name));
            ctx.pkg->consume(ctx.index);
        } else {
            std::unique_ptr<win::OutputFile> output;
            if(op==PK_EXTRACT) output=std::make_unique<win::OutputFile>(output_path(dir,name));
            Sha256 digest;
            ctx.pkg->consume(ctx.index,[&](auto data,auto bytes){
                digest.update(data,bytes);
                if(output) output->write(data,bytes);
                progress(ctx,static_cast<int>(bytes));
            });
            if(digest.finish()!=entry.digest)throw Status{E_BAD_DATA};
            if(output)output->commit();
        }
    }
    ++ctx.index;ctx.pending=false;
    return 0;
}
int guarded_process(HANDLE h,int op,const wchar_t* dir,const wchar_t* name) noexcept {
    try{return process(context(h),op,dir,name);}
    catch(...){const auto code=error();if(h) static_cast<Context*>(h)->failure=code;return code;}
}
}

extern "C" {
HANDLE WINAPI OpenArchiveW(tOpenArchiveDataW* in) noexcept {
    if(!in)return nullptr;
    in->CmtSize=0;in->CmtState=0;
    try{auto h=open(in->ArcName,in->OpenMode);in->OpenResult=0;return h;}
    catch(...){in->OpenResult=error();return nullptr;}
}
int WINAPI ReadHeaderExW(HANDLE h,tHeaderDataExW* out) noexcept {
    try {
        if(!out)return E_SMALL_BUF;
        auto& ctx=context(h);const auto& entry=current(ctx);
        copy_name(out->FileName,std::wstring_view(ctx.names[ctx.index].name));
        lengths(*out,entry);attributes(*out,entry);ctx.pending=true;return 0;
    } catch(...){return error();}
}
int WINAPI ProcessFileW(HANDLE h,int op,WCHAR* path,WCHAR* name) noexcept {return guarded_process(h,op,path,name);}
int WINAPI CloseArchive(HANDLE h) noexcept {delete static_cast<Context*>(h);return 0;}
void WINAPI SetChangeVolProcW(HANDLE,tChangeVolProcW) noexcept {} // Single-volume format.
void WINAPI SetProcessDataProcW(HANDLE h,tProcessDataProcW callback) noexcept {if(h)static_cast<Context*>(h)->progressW=callback;}
int WINAPI GetPackerCaps() noexcept {return PK_CAPS_BY_CONTENT;}
BOOL WINAPI CanYouHandleThisFileW(WCHAR* path) noexcept {
    try{return path && apk::Package::probe(path);}catch(...){return FALSE;}
}
HANDLE WINAPI OpenArchive(tOpenArchiveData* in) noexcept {
    if(!in)return nullptr;
    in->CmtSize=0;in->CmtState=0;
    try{auto h=open(from_ansi(in->ArcName).c_str(),in->OpenMode);in->OpenResult=0;return h;}
    catch(...){in->OpenResult=error();return nullptr;}
}
int WINAPI ReadHeader(HANDLE h,tHeaderData* out) noexcept {
    try {
        if(!out)return E_SMALL_BUF;
        auto& ctx=context(h);const auto& entry=current(ctx);
        if(entry.size>static_cast<unsigned>((std::numeric_limits<int>::max)())) return E_SMALL_BUF;
        copy_name(out->FileName,std::string_view(to_ansi(ctx.names[ctx.index].name)));
        out->PackSize=0;out->UnpSize=static_cast<int>(entry.size);attributes(*out,entry);
        ctx.pending=true;return 0;
    } catch(...){return error();}
}
int WINAPI ReadHeaderEx(HANDLE h,tHeaderDataEx* out) noexcept {
    try {
        if(!out)return E_SMALL_BUF;
        auto& ctx=context(h);const auto& entry=current(ctx);
        copy_name(out->FileName,std::string_view(to_ansi(ctx.names[ctx.index].name)));
        lengths(*out,entry);attributes(*out,entry);ctx.pending=true;return 0;
    } catch(...){return error();}
}
int WINAPI ProcessFile(HANDLE h,int op,char* path,char* name) noexcept {
    if(op!=PK_EXTRACT) return guarded_process(h,op,nullptr,nullptr);
    try {auto dir=from_ansi(path),file=from_ansi(name);return guarded_process(h,op,path?dir.c_str():nullptr,name?file.c_str():nullptr);}
    catch(...){return error();}
}
void WINAPI SetChangeVolProc(HANDLE,tChangeVolProc) noexcept {}
void WINAPI SetProcessDataProc(HANDLE h,tProcessDataProc callback) noexcept {if(h)static_cast<Context*>(h)->progressA=callback;}
BOOL WINAPI CanYouHandleThisFile(char* path) noexcept {
    try{return path && apk::Package::probe(from_ansi(path));}catch(...){return FALSE;}
}
}
