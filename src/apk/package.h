#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace apk {
enum class Fault { format, unsupported, corrupt, open, read };
struct Error : std::runtime_error {
    const Fault fault;
    Error(Fault kind, const char* message) : std::runtime_error(message), fault(kind) {}
};
enum class Kind { directory, file, special };
struct Entry {
    std::string path;
    Kind kind;
    std::uint64_t size{}, mtime{};
    std::array<std::uint8_t, 32> digest{};
    std::uint32_t pathIndex{}, fileIndex{};
};
using Sink = std::function<void(const std::uint8_t*, std::size_t)>;
struct Stream;
class Package {
    std::unique_ptr<Stream> input_;
    std::vector<Entry> entries_;
    std::size_t next_{};
    bool dataStarted_{};
    Package();
public:
    ~Package();
    static std::unique_ptr<Package> open(const std::filesystem::path& path);
    static bool probe(const std::filesystem::path& path);
    const std::vector<Entry>& entries() const { return entries_; }
    void consume(std::size_t entry, const Sink& sink = {});
    void finish();
};
}
