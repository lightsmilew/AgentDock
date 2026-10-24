#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "json.hpp"
#include "sqlite3.h"
#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <objbase.h>
#include <string>
#include <vector>
#include <windows.h>

namespace ad {
using J = nlohmann::json;
namespace fs = std::filesystem;
std::wstring wide(const std::string &);
std::string utf8(const std::wstring &);
std::string pathstr(const fs::path &);
std::string display_path(fs::path);
fs::path path(const std::string &);
std::string trim(std::string);
std::string env(const std::string &);
std::string uuid();
double now();
std::string read(const fs::path &, size_t max = 20 * 1024 * 1024);
void atomic_write(const fs::path &, const std::string &);
void check_path(const fs::path &);
bool inside(const fs::path &, const fs::path &);
bool allowed_agent_path(const fs::path &);
std::string sha(const std::string &);
J metadata(const std::string &);
J skill_quality(const std::string &);
std::vector<fs::path> bundle(const fs::path &);
std::string tree_hash(const fs::path &);
std::string protect(const std::string &, bool decrypt = false);
std::string endpoint(const std::string &);
J resource_json(int);
std::string resource(int);
std::string bytes(uint64_t);

struct DB {
    sqlite3 *db = nullptr;
    explicit DB(const fs::path &);
    ~DB();
    DB(const DB &) = delete;
    void exec(const std::string &);
    J query(const std::string &, const J &params = J::array());
    void run(const std::string &s, const J &p = J::array()) {
        query(s, p);
    }
};
struct Tx {
    DB &db;
    bool done = false;
    explicit Tx(DB &d) : db(d) {
        db.exec("BEGIN IMMEDIATE");
    }
    void commit() {
        db.exec("COMMIT");
        done = true;
    }
    ~Tx() {
        if (!done)
            sqlite3_exec(db.db, "ROLLBACK", nullptr, nullptr, nullptr);
    }
};
class Store {
  public:
    fs::path root;
    DB db;
    std::recursive_mutex mutex;
    std::atomic<bool> cancel_scan{false};
    explicit Store(fs::path);
    J setting(const std::string &);
    void setting(const std::string &, const J &);
    void event(const std::string &, const std::string &, const std::string & = "");
    J save(J);
    J skill(const std::string &);
    J list(const std::string &q = "", const std::string &scope = "all", const std::string &category = "all",
           int limit = 50, int offset = 0);
    J scan(const std::vector<std::pair<std::string, fs::path>> &);
    J import_skill(fs::path);
    void toggle(const std::string &, const std::string &, bool);
    void toggle_deployment(const std::string &, const std::string &, bool);
    J deployments();
    fs::path deploy(const std::string &, const std::string &, fs::path);
    J deployment_check(const std::string &, const std::string &, fs::path);
    void export_zip(const std::string &, fs::path);
    std::string revision(const std::string &, const std::string &);
    J stats();
    uint64_t measure_storage(std::atomic<bool> &);

  private:
    void index(const std::string &);
    double scan_ms = 0;
    std::atomic<uint64_t> storage_bytes{0};
    std::atomic<bool> storage_ready{false};
};
class Models {
  public:
    Store &store;
    explicit Models(Store &s) : store(s) {}
    J list();
    std::pair<J, std::string> get(const std::string &);
    J save(J);
    J duplicate(const std::string &);
    void remove(const std::string &);
    J request(const J &, const std::string &, const std::string &, const J &body = nullptr);
    J discover(const std::string &);
    J complete(const std::string &, const std::string &, const std::string &, int max_tokens = 5000);
    J test(const std::string &);
    J generate(const std::string &, const std::string &, const J &options = J::object());
    void cancel();
    static J payload(const J &, const std::string &, const std::string &, int);
    static std::string extract(const J &, const std::string &);

  private:
    std::mutex request_mutex;
    std::atomic<void *> active{nullptr};
};
class Adapters {
  public:
    Store &store;
    Models &models;
    fs::path home;
    J catalog;
    Adapters(Store &, Models &, fs::path home = {});
    J item(const std::string &);
    fs::path target(const std::string &, bool project = false);
    std::vector<fs::path> configs(const std::string &);
    J list();
    std::vector<std::pair<std::string, fs::path>> roots();
    J preview(const std::string &, const std::string &, const std::string &model = "");
    J model_state(const std::string &);
    J capabilities(const std::string &);
    J preview_default(const std::string &, const std::string &);
    J extra_preview(const std::string &, const J &, const std::string &);
    void apply(const std::string &);
    J backups();
    void restore(const std::string &);
    std::string launch(const std::string &, const std::string &pid = "", bool interactive = true,
                       const std::vector<std::string> &arguments = {});
    J launch_plan(const std::string &, const std::string &pid = "");
    fs::path managed_root(const std::string &);
    fs::path executable(const std::string &);
    fs::path desktop_executable(const std::string &);
    void locate(const std::string &, const fs::path &);
    J search_installations(std::atomic<bool> &, const std::vector<fs::path> &roots = {});
    J installation(const std::string &);
    J installation_plan(const std::string &, bool remove = false, const fs::path &test_root = {},
                        bool thorough = false, const std::string &variant = "",
                        const fs::path &directory = {});
    J manage_installation(const std::string &, bool remove = false, const fs::path &test_root = {},
                          bool thorough = false, const std::string &variant = "",
                          const fs::path &directory = {});
    J install(const std::string &, const fs::path &portable = {}, const std::string &entry = "");
    void uninstall(const std::string &);
    std::atomic<bool> cancel_install{false};
    static std::string merge_codex(const std::string &, const J &, const std::string &,
                                   std::string &fragment);

  private:
    std::map<std::string, J> previews;
};
fs::path find_exe(const std::string &, const std::function<bool(const fs::path &)> &accept = {});
std::wstring cli_search_path();
std::string patch_json(const std::string &, const std::string &, const J &);
std::string erase_json_member(const std::string &, const std::string &);
std::string patch_toml(const std::string &, const std::string &, const J &);
J parse_jsonc(const std::string &);
std::string merge_zcode(const std::string &, const J &, const std::string &);
std::string select_zcode_model(const std::string &, const std::string &);
J zcode_model_rows(const J &);
std::string patch_harness(const std::string &, const std::string &, const J &);
std::string patch_harness_default(const std::string &, const std::string &, const std::string &);
} // namespace ad
