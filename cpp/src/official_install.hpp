#pragma once
#include "core.hpp"
namespace ad {
struct InstallationCancelled : std::runtime_error {
    InstallationCancelled() : std::runtime_error("安装已取消") {}
};
bool valid_agent_executable(const std::string &, const fs::path &);
bool npm_agent_executable(const std::string &, const fs::path &);
fs::path npm_agent_target(const std::string &, const fs::path &);
J windows_platform();
J binary_compatibility(const fs::path &, const J &);
J node_compatibility(const std::string &, const fs::path &, const J &);
J agent_compatibility(const std::string &, const fs::path &, const J &);
J agent_compatibility(const std::string &, const fs::path &);
bool registration_matches_executable(const J &, const fs::path &);
J official_install_info(const std::string &, const J &, const fs::path &);
void run_install_plan(const J &, const fs::path &, std::atomic<bool> &);
void refresh_installed_apps();
J agent_install_choices(const std::string &, const fs::path &);
J desktop_install_plan(const std::string &, const fs::path &, const J &platform = {});
void run_desktop_install(const J &, const fs::path &, std::atomic<bool> &);
} // namespace ad
