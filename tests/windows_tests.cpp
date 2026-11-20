#include "windows/archive_path.h"
#include "windows/output_file.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <iterator>

void check(bool good,const char* message){if(!good)throw std::runtime_error(message);}
template<class E,class F> void rejected(F f){try{f();}catch(const E&){return;}throw std::runtime_error("unexpected success");}
std::string contents(const std::filesystem::path& p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
int main(){
    namespace fs=std::filesystem;
    try {
        for (const auto* name : {"../x", "/usr/x", "C:/x", "//server/x", "usr\\file", "name:stream", "CON.txt", "dir/NUL", "COM1", "foo.", "foo ", "a//b", "a/./b", "a/b?", "\xc0\x80"})
            rejected<win::PathError>([&]{win::to_wcx_path(name);});
        rejected<win::PathError>([]{win::to_wcx_path(std::string(1100,'x'));});
        check(win::to_wcx_path(u8"usr/данные.txt")==L"usr\\данные.txt", "Unicode conversion");
        rejected<win::PathError>([]{win::validate_collisions({{L"a\\file",false},{L"A\\File",false}});});
        rejected<win::PathError>([]{win::validate_collisions({{L"a",false},{L"A\\b",false}});});
        rejected<win::PathError>([]{win::validate_collisions({{L"a\\x",false},{L"A\\y",false}});});
        win::validate_collisions({{L"a\\x",false},{L"a",true},{L"a\\y",false}});
        const auto root=fs::temp_directory_path()/(L"apkview-output-"+std::to_wstring(GetCurrentProcessId()));
        check(fs::create_directory(root),"private test directory already exists");
        struct Cleanup{fs::path p; ~Cleanup(){std::error_code e;fs::remove_all(p,e);}} cleanup{root};
        const auto renamed=root/L"переименован.txt";
        auto output=win::destination(root.c_str(),L"переименован.txt");
        check(output==renamed && win::destination(nullptr,renamed.c_str())==renamed,"destination arguments");
        rejected<win::PathError>([&]{win::destination(root.c_str(),L"..\\escape");});
        rejected<win::PathError>([&]{win::destination(nullptr,L"C:relative.txt");});
        {std::ofstream prior(renamed);prior<<"previous";}
        const std::uint8_t bytes[]{'n','e','w'};
        {win::OutputFile f(renamed); f.write(bytes,sizeof bytes);}
        check(contents(renamed)=="previous" && std::distance(fs::directory_iterator(root),fs::directory_iterator())==1,"abort leaves original and no temp");
        {win::OutputFile f(renamed); f.write(bytes,sizeof bytes); f.commit();}
        check(contents(renamed)=="new","overwrite authorized destination");
        const auto nested=root/L"dir"/L"empty";
        {win::OutputFile f(nested);f.commit();}
        check(fs::is_regular_file(nested) && fs::file_size(nested)==0,"empty file and implicit directory");
        const auto link=root/L"junction-test";
        if(CreateSymbolicLinkW(link.c_str(),nested.parent_path().c_str(),SYMBOLIC_LINK_FLAG_DIRECTORY|SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
            rejected<win::OutputError>([&]{win::OutputFile f(link/L"unsafe");});
            check(!fs::exists(nested.parent_path()/L"unsafe"),"no write through reparse point");
        } else std::cout<<"Symlink scenario skipped: unavailable ("<<GetLastError()<<")\n";
        std::cout<<"Windows path/output tests passed\n";return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
