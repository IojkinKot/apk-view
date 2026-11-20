#pragma once
#include <string>
#include <string_view>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace win {
struct PathError : std::runtime_error { using std::runtime_error::runtime_error; };
struct NamedPath { std::wstring name; bool directory; };
std::wstring to_wcx_path(std::string_view utf8);
void validate_collisions(const std::vector<NamedPath>& paths);
std::filesystem::path destination(const wchar_t* directory, const wchar_t* name);
}
