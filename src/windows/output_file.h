#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>

namespace win {
enum class OutputFault { create, write };
struct OutputError : std::runtime_error {
    OutputFault fault;
    OutputError(OutputFault kind, const char* what) : std::runtime_error(what), fault(kind) {}
};
void create_directory(const std::filesystem::path& path);
class OutputFile {
    struct Impl;
    std::unique_ptr<Impl> p_;
public:
    explicit OutputFile(const std::filesystem::path& path);
    ~OutputFile();
    void write(const std::uint8_t* data, std::size_t count);
    void commit();
};
}
