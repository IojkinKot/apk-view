#include "apk/stream.h"
#include "apk/package.h"
#include <algorithm>
#include <cstring>
#ifdef APKVIEW_HAVE_ZLIB
#include <zlib.h>
#endif

namespace apk {
std::uint64_t little(const std::uint8_t* bytes, unsigned width) {
    std::uint64_t value=0;
    for(unsigned i=0;i<width;++i) value |= std::uint64_t{bytes[i]} << (8*i);
    return value;
}
struct Stream::Inflater {
#ifdef APKVIEW_HAVE_ZLIB
    z_stream z{};
    std::array<Bytef,64*1024> input;
    bool ended{};
    Inflater(){
        const auto rc=inflateInit2(&z,-15); // ADB uses bare RFC 1951, without a zlib/gzip envelope.
        if(rc==Z_MEM_ERROR) throw std::bad_alloc{};
        if(rc!=Z_OK) throw Error(Fault::unsupported,"Cannot initialize DEFLATE");
    }
    ~Inflater(){inflateEnd(&z);}

    std::size_t read(Stream& stream,void* dest,std::size_t count) {
        z.next_out=static_cast<Bytef*>(dest);
        z.avail_out=static_cast<uInt>(count);
        while(z.avail_out && !ended){
            if(!z.avail_in){
                z.next_in=input.data();
                z.avail_in=static_cast<uInt>(stream.read_file(input.data(),input.size()));
            }
            const auto beforeIn=z.avail_in,beforeOut=z.avail_out;
            const auto rc=inflate(&z,Z_NO_FLUSH);
            if(rc==Z_STREAM_END){ended=true;break;}
            if(rc==Z_MEM_ERROR) throw std::bad_alloc{};
            if((rc!=Z_OK && rc!=Z_BUF_ERROR) || (beforeIn==z.avail_in && beforeOut==z.avail_out))
                throw Error(Fault::corrupt,"Invalid or truncated DEFLATE data");
        }
        const auto actual=count-z.avail_out;
        if(!actual && ended){
            std::uint8_t extra;
            if(z.avail_in || stream.read_file(&extra,1)) throw Error(Fault::corrupt,"Trailing bytes after DEFLATE stream");
        }
        return actual;
    }
#endif
};
Stream::Stream(const std::filesystem::path& path) : file_(path,std::ios::binary) {
    if(!file_) throw Error(Fault::open,"Cannot open package");
    char magic[4]{};
    if(read_file(magic,sizeof magic)!=sizeof magic) throw Error(Fault::format,"Not an ADB file");
    int algorithm=0;
    if(std::memcmp(magic,"ADB.",4)==0) {
        if(!file_.seekg(0)) throw Error(Fault::read,"Cannot rewind package");
    } else if(std::memcmp(magic,"ADBc",4)==0) {
        std::uint8_t spec[2]{};
        if(read_file(spec,sizeof spec)!=sizeof spec) throw Error(Fault::corrupt,"Truncated compression spec");
        algorithm=spec[0];
    } else if(std::memcmp(magic,"ADBd",4)==0) algorithm=1;
    else throw Error(Fault::format,"Not an ADB file");
    if(algorithm==0) return;
#ifdef APKVIEW_HAVE_ZLIB
    if(algorithm==1){inflater_=std::make_unique<Inflater>();return;}
#endif
    throw Error(Fault::unsupported,"Compression algorithm unavailable");
}
Stream::~Stream()=default;
std::size_t Stream::read_file(void* dest,std::size_t bytes){
    file_.read(static_cast<char*>(dest),static_cast<std::streamsize>(bytes));
    if(file_.bad() || (file_.fail() && !file_.eof())) throw Error(Fault::read,"Package read failed");
    return static_cast<std::size_t>(file_.gcount());
}
bool Stream::next(void* dest, std::size_t bytes) {
    if(!bytes) return true;
#ifdef APKVIEW_HAVE_ZLIB
    const auto actual=inflater_?inflater_->read(*this,dest,bytes):read_file(dest,bytes);
#else
    const auto actual=read_file(dest,bytes);
#endif
    if(!actual) return false;
    if(actual!=bytes) throw Error(Fault::corrupt,"Unexpected end of package");
    constexpr auto quota=std::uint64_t{8}<<30;
    if(actual>quota-decoded_) throw Error(Fault::corrupt,"Package stream exceeds size limit");
    decoded_+=actual;
    return true;
}
void Stream::exact(void* dest, std::size_t bytes) {
    if(!next(dest,bytes)) throw Error(Fault::corrupt,"Unexpected end of package");
}
void Stream::discard(std::uint64_t bytes) {
    std::array<std::uint8_t,16*1024> unused;
    while(bytes) {
        const auto part=static_cast<std::size_t>((std::min)(bytes,std::uint64_t{unused.size()}));
        exact(unused.data(),part); bytes-=part;
    }
}
}
