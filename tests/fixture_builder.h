#pragma once
#ifdef APKVIEW_HAVE_ZLIB
#include <zlib.h>
#endif
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fixture {
using Bytes = std::vector<std::uint8_t>;
inline void le(Bytes& out, std::uint64_t n, unsigned width = 4) { for (unsigned i=0; i<width; ++i) out.push_back(static_cast<std::uint8_t>(n >> (i*8))); }
inline void text(Bytes& out, std::string_view s) { out.insert(out.end(), s.begin(), s.end()); }
inline void block(Bytes& out, unsigned type, const Bytes& body, bool ext = false) {
    const auto n = body.size() + (ext ? 16 : 4);
    if (ext) { le(out, 0xc0000000u | type); le(out, 0); le(out, n, 8); }
    else le(out, (type << 30) | n);
    out.insert(out.end(), body.begin(), body.end());
    while (out.size() % 8) out.push_back(0);
}
struct Db {
    Bytes bytes{0,0,0,0,0,0,0,0};
    std::uint32_t blob(std::string_view s) {
        const auto at = static_cast<std::uint32_t>(bytes.size());
        if (s.size()>255) throw std::logic_error("fixture blob too large");
        le(bytes,s.size(),1); text(bytes,s); return 0x80000000u | at;
    }
    std::uint32_t slots(std::uint32_t tag, std::initializer_list<std::uint32_t> fields) {
        while (bytes.size()%4) bytes.push_back(0);
        auto value = tag | static_cast<std::uint32_t>(bytes.size());
        le(bytes,fields.size()+1); for (auto field : fields) le(bytes,field); return value;
    }
    std::uint32_t object(std::initializer_list<std::uint32_t> f) { return slots(0xe0000000u,f); }
    std::uint32_t array(std::initializer_list<std::uint32_t> f) { return slots(0xd0000000u,f); }
    void root(std::uint32_t value) { for (unsigned i=0;i<4;++i) bytes[4+i]=static_cast<std::uint8_t>(value>>(8*i)); }
};
inline std::string sha256(std::string_view hex) {
    auto digit = [](char c){ return c <= '9' ? c - '0' : c - 'a' + 10; };
    std::string bytes; for(std::size_t i=0;i<hex.size();i+=2) bytes.push_back(static_cast<char>(16*digit(hex[i])+digit(hex[i+1]))); return bytes;
}
inline Bytes package(bool extended=false, bool signature=false, bool emptyPackage=false, unsigned fileIndex=2,
                     std::string_view body="abc", std::string_view file=u8"файл.txt", bool badDigest=false) {
    using namespace std::literals;
    Db d;
    auto list=[&](std::initializer_list<std::uint32_t> fields){ return extended?d.object(fields):d.array(fields); };
    if (emptyPackage) d.root(d.object({0,list({})}));
    else {
        auto empty = d.object({d.blob("empty"),0,0,0,d.blob(sha256("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"))});
        auto regular = d.object({d.blob(file),0,0x10000003,0,d.blob(sha256(badDigest ?
          "0000000000000000000000000000000000000000000000000000000000000000" : "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"))});
        auto link = d.object({d.blob("link"),0,0,0,0,d.blob("\0\xa0" "empty"sv)});
        auto dir = d.object({d.blob("usr/share"),0,list({empty,regular,link})});
        d.root(d.object({0,list({dir})}));
    }
    Bytes bytes; text(bytes,"ADB.pckg"); block(bytes,0,d.bytes,extended);
    if(signature) block(bytes,1,Bytes(19,0));
    if (!emptyPackage) {
        Bytes data; le(data,1); le(data,fileIndex); text(data,body); block(bytes,2,data,extended);
    }
    return bytes;
}
inline Bytes wrap(const Bytes& plain, int mode) {
    if (!mode) return plain;
    Bytes out;
    if (mode==1) text(out,"ADBd");
    else { text(out,"ADBc"); out.push_back(static_cast<std::uint8_t>(mode-2)); out.push_back(0); }
    if(mode==2 || mode==4) { out.insert(out.end(),plain.begin(),plain.end()); return out; }
#ifdef APKVIEW_HAVE_ZLIB
    auto header = out.size(); out.resize(header+compressBound(static_cast<uLong>(plain.size())));
    z_stream z{};
    if (deflateInit2(&z,6,Z_DEFLATED,-15,8,Z_DEFAULT_STRATEGY)!=Z_OK) throw std::runtime_error("fixture deflate init");
    z.next_in=const_cast<Bytef*>(plain.data()); z.avail_in=static_cast<uInt>(plain.size());
    z.next_out=out.data()+header; z.avail_out=static_cast<uInt>(out.size()-header);
    const auto rc=deflate(&z,Z_FINISH); const auto size=z.total_out; deflateEnd(&z);
    if(rc!=Z_STREAM_END) throw std::runtime_error("fixture deflate");
    out.resize(header+size); return out;
#else
    throw std::runtime_error("fixture requires zlib");
#endif
}
inline void save(const std::filesystem::path& path,const Bytes& b) {
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    if(!out.write(reinterpret_cast<const char*>(b.data()),static_cast<std::streamsize>(b.size()))) throw std::runtime_error("fixture write");
}
}
