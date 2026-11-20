#include "apk/package.h"
#include "apk/stream.h"
#include <algorithm>
#include <cstring>
#include <string_view>

namespace apk {
namespace {
[[noreturn]] void corrupt(const char* what) { throw Error(Fault::corrupt,what); }
constexpr std::uint32_t mask=0xf0000000u, offsetMask=0x0fffffffu;
constexpr std::uint32_t object=0xe0000000u, array=0xd0000000u;
struct Block { unsigned type; std::uint64_t size, padding; };
bool block(Stream& in,Block& b) {
    std::uint8_t h[16];
    if(!in.next(h,4)) return false;
    const auto word=little(h,4);
    b.type=static_cast<unsigned>(word>>30);
    auto total=word&0x3fffffff;
    unsigned hsize=4;
    if(b.type==3) {
        hsize=16; in.exact(h+4,12);
        const auto type=total;
        if(type>2 || little(h+4,4)!=0) corrupt("Invalid extended block header");
        b.type=static_cast<unsigned>(type); total=little(h+8,8);
    }
    if(total<hsize || total>(std::uint64_t{8}<<30)) corrupt("Invalid ADB block length");
    b.size=total-hsize;
    b.padding=(8-(total%8))%8;
    return true;
}
void check_magic(Stream& in) {
    char magic[8];
    if(!in.next(magic,sizeof magic) || std::memcmp(magic,"ADB.pckg",sizeof magic)!=0)
        throw Error(Fault::format,"ADB file is not a package");
}
class Db {
    std::vector<std::uint8_t> bytes_;
    const std::uint8_t* ref(std::size_t pos,std::size_t n) const {
        if(pos>bytes_.size() || n>bytes_.size()-pos) corrupt("ADB reference out of range");
        return bytes_.data()+pos;
    }
public:
    explicit Db(Stream& in,std::uint64_t size) {
        if(size<16 || size>(16u<<20)) corrupt("Invalid metadata size");
        bytes_.resize(static_cast<std::size_t>(size));
        in.exact(bytes_.data(),bytes_.size());
        if(bytes_[0]!=0) throw Error(Fault::unsupported,"Incompatible ADB version");
    }
    std::uint32_t word(std::size_t pos) const { return static_cast<std::uint32_t>(little(ref(pos,4),4)); }
    struct Slots {
        const Db* db;
        std::uint32_t pos,count;
        std::uint32_t at(std::uint32_t index) const { return index<count ? db->word(pos+4*index) : 0; }
    };
    Slots slots(std::uint32_t value,std::uint32_t tag) const {
        if(!value && tag==array) return {this,0,1};
        const auto pos=value&offsetMask;
        // The schema defines what the slots mean; mkpkg may encode an array with either tag.
        const auto type=value&mask;
        if((type!=object && type!=array) || pos<8 || (pos%4)) corrupt("Invalid ADB slot reference");
        const auto count=word(pos);
        if(count==0 || count>(bytes_.size()-pos)/4) corrupt("ADB slots out of range");
        if(tag==array && count>50001) corrupt("Too many ADB elements");
        return {this,pos,count};
    }
    Slots root() const { return slots(word(4),object); }
    std::string_view blob(std::uint32_t value) const {
        if(!value) return {};
        const auto pos=value&offsetMask;
        const auto type=value&mask;
        const unsigned width=type==0x80000000u?1:type==0x90000000u?2:type==0xa0000000u?4:0;
        if(pos<8 || !width) corrupt("Invalid ADB blob reference");
        const auto n=static_cast<std::size_t>(little(ref(pos,width),width));
        return {reinterpret_cast<const char*>(ref(pos+width,n)),n};
    }
    std::uint64_t number(std::uint32_t value) const {
        if(!value) return 0;
        const auto pos=value&offsetMask;
        const auto type=value&mask;
        if(type==0x10000000u) return pos;
        if(pos<8 || (pos%4)) corrupt("Misaligned ADB integer");
        if(type==0x20000000u) return little(ref(pos,4),4);
        if(type==0x30000000u) return little(ref(pos,8),8);
        corrupt("Invalid ADB integer type");
    }
};
void check_target(std::string_view target) {
    if(target.size()<2) corrupt("Invalid package target");
    const auto type=little(reinterpret_cast<const std::uint8_t*>(target.data()),2);
    switch(type) {
    case 0xa000: case 0x8000:
        if(target.size()<=2 || target.size()>4096 || target.substr(2).find('\0')!=target.npos) corrupt("Invalid link target");
        break;
    case 0x1000: case 0x2000: case 0x6000:
        if(target.size()!=10) corrupt("Invalid special file target");
        break;
    default: corrupt("Unknown file target type");
    }
}
void append_entries(const Db& db,std::vector<Entry>& entries) {
    constexpr std::size_t maxNames=8u<<20;
    std::size_t nameBytes=0;
    const auto paths=db.slots(db.root().at(2),array);
    auto add=[&](Entry&& entry){
        if(entries.size()>=50000 || entry.path.size()>maxNames-nameBytes) corrupt("Package index exceeds limits");
        nameBytes+=entry.path.size(); entries.push_back(std::move(entry));
    };
    for(std::uint32_t p=1;p<paths.count;++p) {
        const auto dir=db.slots(paths.at(p),object);
        const auto parent=db.blob(dir.at(1));
        if(parent.size()>4092) corrupt("Path is too long");
        if(!parent.empty()) add({std::string(parent),Kind::directory,0,0,{},p,0});
        const auto files=db.slots(dir.at(3),array);
        for(std::uint32_t f=1;f<files.count;++f) {
            const auto file=db.slots(files.at(f),object);
            const auto name=db.blob(file.at(1));
            if(name.empty() || name.size()>4092 || name.find('/')!=name.npos) corrupt("Invalid file name in metadata");
            if(!parent.empty() && parent.size()+1+name.size()>4092) corrupt("Path is too long");
            auto path=parent.empty()?std::string(name):std::string(parent)+"/"+std::string(name);
            Entry entry{std::move(path),Kind::file,db.number(file.at(3)),db.number(file.at(4)),{},p,f};
            if(entry.size>(std::uint64_t{8}<<30)) corrupt("Package file exceeds size limit");
            if(const auto target=file.at(6)) {
                check_target(db.blob(target));
                entry.kind=Kind::special;
                entry.size=0;
            } else {
                const auto digest=db.blob(file.at(5));
                if(digest.size()!=entry.digest.size()) throw Error(Fault::unsupported,"Package does not use SHA-256 file digests");
                std::memcpy(entry.digest.data(),digest.data(),entry.digest.size());
            }
            add(std::move(entry));
        }
    }
}
void skip_signature(Stream& in,const Block& b) {
    if(b.size<18 || b.size>2066) corrupt("Invalid ADB signature block length");
    in.discard(b.size+b.padding);
}
}
Package::Package()=default;
Package::~Package()=default;
bool Package::probe(const std::filesystem::path& path) {
    try { Stream input(path); check_magic(input); return true; } catch(const Error&) { return false; }
}
std::unique_ptr<Package> Package::open(const std::filesystem::path& path) {
    auto pkg=std::unique_ptr<Package>(new Package);
    pkg->input_=std::make_unique<Stream>(path);
    auto& input=*pkg->input_;
    check_magic(input);
    Block b{};
    if(!block(input,b) || b.type!=0) corrupt("Missing ADB metadata block");
    Db db(input,b.size); input.discard(b.padding);
    append_entries(db,pkg->entries_);
    return pkg;
}
void Package::consume(std::size_t index,const Sink& sink) {
    if(index!=next_ || index>=entries_.size()) corrupt("Entries must be processed in order");
    const auto& entry=entries_[index];
    auto& input=*input_;
    if(entry.kind==Kind::file && entry.size) {
        Block b{};
        while(block(input,b) && b.type==1) {
            if(dataStarted_) corrupt("Signature after DATA");
            skip_signature(input,b);
        }
        if(b.type!=2 || b.size!=entry.size+8) corrupt("Missing or wrong-sized DATA block");
        dataStarted_=true;
        std::uint8_t indices[8]; input.exact(indices,sizeof indices);
        if(little(indices,4)!=entry.pathIndex || little(indices+4,4)!=entry.fileIndex) corrupt("Wrong DATA block indices");
        std::array<std::uint8_t,64*1024> chunk;
        auto remaining=entry.size;
        while(remaining) {
            const auto n=static_cast<std::size_t>((std::min)(remaining,std::uint64_t{chunk.size()}));
            input.exact(chunk.data(),n);
            if(sink) sink(chunk.data(),n);
            remaining-=n;
        }
        input.discard(b.padding);
    }
    ++next_;
}
void Package::finish() {
    if(next_!=entries_.size()) corrupt("Not all entries processed");
    Block b{};
    auto& input=*input_;
    while(block(input,b)) {
        if(dataStarted_ || b.type!=1) corrupt("Unexpected ADB block at end of package");
        skip_signature(input,b);
    }
}
}
