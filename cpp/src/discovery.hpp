#pragma once
#include "core.hpp"
namespace ad {
J registered_agents();
void refresh_registered_agents();
J registered_agent_paths(const std::string &);
J paths_from_registration(const std::string &, const J &);
J paths_from_folder(const std::string &, const fs::path &, const fs::path &entry = {});
std::vector<fs::path> local_search_roots();
J search_agent_files(const J &, const std::vector<fs::path> &, std::atomic<bool> &);
void refresh_cli_paths();
bool is_roo_extension(const fs::path &);
bool verified_disk_cli(const std::string &, const fs::path &);
bool verified_disk_desktop(const std::string &, const fs::path &);
fs::path roo_extension(const fs::path &, const fs::path &preferred = {});
} // namespace ad
