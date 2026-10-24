#pragma once
#include "core.hpp"
namespace ad {
std::vector<fs::path> roo_extension_directories(const fs::path &, const fs::path & = {});
J removal_files(const std::string &, const fs::path &, const fs::path &, const fs::path &, bool);
void execute_removal_files(const J &);
void validate_removal_files(const J &);
void append_cursor_desktop_files(J &, const fs::path &);
J cursor_desktop_layout(const fs::path &);
} // namespace ad
