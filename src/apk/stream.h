#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>

namespace apk {
struct Stream {
    explicit Stream(const std::filesystem::path& file);
    ~Stream();
    bool next(void* dest, std::size_t bytes);
    void exact(void* dest, std::size_t bytes);
    void discard(std::uint64_t bytes);
private:
    struct Inflater;
    std::ifstream file_;
    std::unique_ptr<Inflater> inflater_;
    std::uint64_t decoded_{};
    std::size_t read_file(void* dest, std::size_t bytes);
};
std::uint64_t little(const std::uint8_t* bytes, unsigned width);
}
