#include "windows/output_file.h"
#include "windows/archive_path.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <utility>
#include <vector>

namespace win {
namespace fs=std::filesystem;
namespace {
struct Handle {
    HANDLE raw=INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h=INVALID_HANDLE_VALUE) : raw(h) {}
    ~Handle(){if(*this) CloseHandle(raw);}
    Handle(Handle&& other) noexcept : raw(std::exchange(other.raw,INVALID_HANDLE_VALUE)) {}
    Handle& operator=(Handle&& other) noexcept { std::swap(raw,other.raw); return *this; }
    explicit operator bool() const {return raw!=INVALID_HANDLE_VALUE;}
};
std::wstring extended(const fs::path& p) { return L"\\\\?\\"+p.native(); }
[[noreturn]] void cant_create() { throw OutputError(OutputFault::create,"Cannot create output or traverse the chosen destination"); }
[[noreturn]] void cant_write() { throw OutputError(OutputFault::write,"Cannot write or publish the output file"); }
Handle safe_directory(const fs::path& path,bool canCreate) {
    const auto name=extended(path);
    if(canCreate && !CreateDirectoryW(name.c_str(),nullptr) && GetLastError()!=ERROR_ALREADY_EXISTS) cant_create();
    Handle h(CreateFileW(name.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ|FILE_SHARE_WRITE,
                         nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    if(!h || !GetFileInformationByHandle(h.raw,&info) || !(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) ||
       (info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)) cant_create();
    return h;
}
std::vector<Handle> pin_dirs(const fs::path& dir) {
    std::vector<Handle> handles;
    auto element=dir.root_path();
    handles.push_back(safe_directory(element,false));
    for(const auto& part:dir.relative_path()) {
        element/=part;
        handles.push_back(safe_directory(element,true));
    }
    return handles;
}
void check_final(const std::wstring& name) {
    const auto attrs=GetFileAttributesW(name.c_str());
    if(attrs==INVALID_FILE_ATTRIBUTES) { if(GetLastError()!=ERROR_FILE_NOT_FOUND) cant_create(); return; }
    if(attrs&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) cant_create();
}
}
struct OutputFile::Impl {
    std::wstring target;
    std::vector<Handle> dirs;
    Handle file;
    bool committed{};
    ~Impl(){
        if(file && !committed) {
            FILE_DISPOSITION_INFO cleanup{TRUE};
            SetFileInformationByHandle(file.raw,FileDispositionInfo,&cleanup,sizeof cleanup);
        }
    }
};
void create_directory(const fs::path& path) {
    const auto valid=destination(nullptr,path.c_str());
    pin_dirs(valid);
}
OutputFile::OutputFile(const fs::path& path):p_(std::make_unique<Impl>()) {
    const auto valid=destination(nullptr,path.c_str());
    const auto parent=valid.parent_path();
    p_->target=extended(valid);
    p_->dirs=pin_dirs(parent);
    check_final(p_->target);
    static std::atomic<unsigned> sequence{};
    for(unsigned i=0;i<64;++i) {
        const auto temporary=parent/(L".apkview-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++sequence)+L".tmp");
        if(CompareStringOrdinal(valid.c_str(),-1,temporary.c_str(),-1,TRUE)==CSTR_EQUAL) continue;
        p_->file=Handle(CreateFileW(extended(temporary).c_str(),GENERIC_WRITE|DELETE,0,nullptr,CREATE_NEW,FILE_FLAG_SEQUENTIAL_SCAN,nullptr));
        if(p_->file) return;
        if(GetLastError()!=ERROR_FILE_EXISTS) cant_create();
    }
    cant_create();
}
OutputFile::~OutputFile()=default;
void OutputFile::write(const std::uint8_t* data,std::size_t count) {
    if(p_->committed) cant_write();
    while(count) {
        const auto requested=static_cast<DWORD>((std::min)(count,std::size_t{1}<<20));
        DWORD written=0;
        if(!WriteFile(p_->file.raw,data,requested,&written,nullptr) || written!=requested) cant_write();
        data+=written; count-=written;
    }
}
void OutputFile::commit() {
    if(p_->committed || !FlushFileBuffers(p_->file.raw)) cant_write();
    check_final(p_->target);
    const auto bytes=p_->target.size()*sizeof(wchar_t);
    const auto bufferSize=sizeof(FILE_RENAME_INFO)+bytes;
    std::vector<std::uint64_t> buffer((bufferSize+sizeof(std::uint64_t)-1)/sizeof(std::uint64_t));
    const auto rename=reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
    rename->ReplaceIfExists=TRUE;
    rename->FileNameLength=static_cast<DWORD>(bytes);
    std::memcpy(rename->FileName,p_->target.c_str(),bytes);
    if(!SetFileInformationByHandle(p_->file.raw,FileRenameInfo,rename,static_cast<DWORD>(bufferSize))) cant_write();
    p_->committed=true;
    p_->file=Handle();
}
}
