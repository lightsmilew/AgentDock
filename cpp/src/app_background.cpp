#include "official_install.hpp"
#include "ui.hpp"
#include <fstream>
namespace ad {
void App::requestRefresh(bool searchDisks) {
    if (!searchDisks && searchingDisks)
        searchStop = true;
    {
        std::lock_guard lock(maintenanceMutex);
        refreshPending = true;
        if (searchDisks) {
            searchStop = false;
            searchingDisks = true;
            diskSearchPending = true;
        }
    }
    if (!maintenance.joinable()) {
        auto target = hwnd;
        maintenance = std::thread([this, target] {
            auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            auto publish = [&](J result) {
                {
                    std::lock_guard lock(maintenanceMutex);
                    if (!backgroundHasResult)
                        backgroundResult = J::object();
                    backgroundResult.update(result);
                    backgroundHasResult = true;
                }
                PostMessageW(target, WM_APP + 3, 0, 0);
            };
            while (!maintenanceStop) {
                bool deep = false, refresh = false;
                std::string config;
                {
                    std::unique_lock lock(maintenanceMutex);
                    maintenanceWake.wait(
                        lock, [&] { return maintenanceStop || refreshPending || !configPending.empty(); });
                    if (maintenanceStop)
                        break;
                    deep = diskSearchPending;
                    refresh = refreshPending;
                    config = std::move(configPending);
                    configPending.clear();
                    diskSearchPending = refreshPending = false;
                }
                try {
                    if (!config.empty())
                        publish({{"model_state_id", config}, {"model_state", adapters.model_state(config)}});
                    if (!refresh)
                        continue;
                    refresh_installed_apps();
                    auto rows = adapters.list();
                    auto backups = adapters.backups();
                    // Publish discovery before measuring a potentially large data directory.
                    publish({{"agents", rows}, {"backups", backups}});
                    store.measure_storage(maintenanceStop);
                    auto summary = store.stats();
                    publish({{"agents", rows},
                             {"backups", backups},
                             {"data_bytes", summary["data_bytes"]},
                             {"storage_ready", summary["storage_ready"]}});
                    if (deep && !maintenanceStop) {
                        auto found = adapters.search_installations(searchStop);
                        auto cancelled = searchStop.load();
                        searchingDisks = false;
                        publish(
                            {{"agents", adapters.list()},
                             {"backups", backups},
                             {"data_bytes", summary["data_bytes"]},
                             {"storage_ready", summary["storage_ready"]},
                             {"search_message", cancelled ? "磁盘搜索已停止，已保留找到的结果"
                                                          : "磁盘搜索完成 · 找到 " +
                                                                std::to_string(found.size()) + " 个 Agent"}});
                    }
                } catch (const std::exception &e) {
                    searchingDisks = false;
                    J result = {{"error", e.what()}};
                    if (!config.empty()) {
                        result["model_state_id"] = config;
                        result["model_state"] = {{"models", J::array()}, {"loading", false}};
                    }
                    publish(result);
                }
            }
            if (SUCCEEDED(com))
                CoUninitialize();
        });
    }
    maintenanceWake.notify_one();
    invalidate();
}
void App::stopMaintenance() {
    maintenanceStop = searchStop = true;
    maintenanceWake.notify_one();
    if (maintenance.joinable())
        maintenance.join();
}
void App::backgroundReady() {
    J result;
    {
        std::lock_guard lock(maintenanceMutex);
        if (!backgroundHasResult)
            return;
        result = std::move(backgroundResult);
        backgroundHasResult = false;
    }
    if (result.contains("agents"))
        agents = std::move(result["agents"]);
    if (result.contains("backups"))
        backupRows = std::move(result["backups"]);
    if (result.value("model_state_id", "") == expandedAgent && result.contains("model_state"))
        agentState = std::move(result["model_state"]);
    for (auto key : {"data_bytes", "storage_ready"})
        if (result.contains(key))
            stats[key] = result[key];
    if (result.contains("search_message"))
        notice(result["search_message"]);
    if (result.contains("error"))
        notice("后台检测未完成：" + result["error"].get<std::string>());
    invalidate();
}
void App::loadAgentState(const std::string &id) {
    if (searchingDisks)
        searchStop = true;
    agentState = {{"models", J::array()}, {"loading", true}};
    {
        std::lock_guard lock(maintenanceMutex);
        configPending = id;
    }
    maintenanceWake.notify_one();
    invalidate();
}
void App::framePresented() {
    if (!navigationPaintPending || navigationTrace.empty())
        return;
    navigationPaintPending = false;
    LARGE_INTEGER end{}, frequency{};
    QueryPerformanceCounter(&end);
    QueryPerformanceFrequency(&frequency);
    auto elapsed = (end.QuadPart - navigationStart.QuadPart) * 1000.0 / frequency.QuadPart;
    std::ofstream trace(path(navigationTrace), std::ios::app | std::ios::binary);
    trace << J{{"page", page}, {"handler_ms", navigationHandlerMs}, {"first_frame_ms", elapsed}}.dump()
          << '\n';
}
void App::detectionMenu() {
    std::vector<Choice> choices = {{"quick", "快速检测", "读取系统安装记录、快捷方式和命令路径", ""},
                                   {"all", "搜索所有磁盘", "后台查找未登记的安装与便携版，可随时停止", ""}};
    if (searchingDisks)
        choices.push_back({"cancel", "停止磁盘搜索", "保留已经找到的结果", ""});
    popup(choices, [this](std::string choice) {
        if (choice == "cancel") {
            searchStop = true;
            notice("正在停止磁盘搜索");
        } else if (choice == "all") {
            if (!searchingDisks) {
                requestRefresh(true);
                notice("正在后台搜索所有本地磁盘，可继续切换页面");
            }
        } else {
            requestRefresh();
            notice("正在后台重新检测");
        }
    });
}
void App::locateAgent(const std::string &id) {
    // This is a location override, never a source installer or an installation directory option.
    auto chosen = id == "roo" ? choose(true) : choose(false, false, "*.exe;*.cmd;*.bat");
    if (!chosen.empty()) {
        adapters.locate(id, chosen);
        requestRefresh();
        notice("已记录程序位置，正在刷新检测");
    }
}
} // namespace ad
