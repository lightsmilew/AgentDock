#include "core.hpp"
namespace ad {
DB::DB(const fs::path &p) {
    check_path(p);
    fs::create_directories(p.parent_path());
    auto name = pathstr(p);
    if (sqlite3_open_v2(name.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                        nullptr) != SQLITE_OK) {
        if (db)
            sqlite3_close(db);
        db = nullptr;
        throw std::runtime_error("无法打开本地数据库");
    }
    sqlite3_busy_timeout(db, 15000);
}
DB::~DB() {
    if (db)
        sqlite3_close(db);
}
void DB::exec(const std::string &s) {
    char *error = nullptr;
    if (sqlite3_exec(db, s.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
        std::string e = error ? error : "SQLite failure";
        sqlite3_free(error);
        throw std::runtime_error(e);
    }
}
J DB::query(const std::string &s, const J &params) {
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, s.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(db));
    struct Cleanup {
        sqlite3_stmt *s;
        ~Cleanup() {
            sqlite3_finalize(s);
        }
    } cleanup{stmt};
    for (size_t i = 0; i < params.size(); ++i) {
        const auto &v = params[i];
        int rc = SQLITE_OK;
        if (v.is_null())
            rc = sqlite3_bind_null(stmt, (int)i + 1);
        else if (v.is_boolean())
            rc = sqlite3_bind_int(stmt, (int)i + 1, v.get<bool>());
        else if (v.is_number_integer())
            rc = sqlite3_bind_int64(stmt, (int)i + 1, v.get<int64_t>());
        else if (v.is_number_float())
            rc = sqlite3_bind_double(stmt, (int)i + 1, v.get<double>());
        else {
            auto t = v.get<std::string>();
            rc = sqlite3_bind_text(stmt, (int)i + 1, t.data(), (int)t.size(), SQLITE_TRANSIENT);
        }
        if (rc != SQLITE_OK)
            throw std::runtime_error("数据库参数绑定失败");
    }
    J rows = J::array();
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        J row = J::object();
        for (int i = 0; i < sqlite3_column_count(stmt); ++i) {
            std::string k = sqlite3_column_name(stmt, i);
            switch (sqlite3_column_type(stmt, i)) {
            case SQLITE_NULL:
                row[k] = nullptr;
                break;
            case SQLITE_INTEGER:
                row[k] = sqlite3_column_int64(stmt, i);
                break;
            case SQLITE_FLOAT:
                row[k] = sqlite3_column_double(stmt, i);
                break;
            default:
                row[k] =
                    std::string((const char *)sqlite3_column_text(stmt, i), sqlite3_column_bytes(stmt, i));
            }
        }
        rows.push_back(row);
    }
    if (rc != SQLITE_DONE)
        throw std::runtime_error(sqlite3_errmsg(db));
    return rows;
}
} // namespace ad
