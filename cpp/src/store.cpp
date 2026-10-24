#include "core.hpp"
#include "skill_category.hpp"
#include <algorithm>
#include <chrono>
#include <set>
#include <sstream>

namespace ad {
static std::string scalar(std::string s) {
    s = trim(s);
    if (s.empty())
        return s;
    if (s[0] == '"') {
        auto j = J::parse(s, nullptr, false);
        if (!j.is_string())
            throw std::runtime_error("frontmatter 双引号字符串无效");
        return j.get<std::string>();
    }
    if (s[0] == '\'') {
        if (s.size() < 2 || s.back() != '\'')
            throw std::runtime_error("frontmatter 单引号未闭合");
        s = s.substr(1, s.size() - 2);
        size_t p = 0;
        while ((p = s.find("''", p)) != std::string::npos) {
            s.replace(p, 2, "'");
            ++p;
        }
        return s;
    }
    auto p = s.find(" #");
    if (p != std::string::npos)
        s.resize(p);
    return trim(s);
}
J metadata(const std::string &raw) {
    if (raw.size() > 512 * 1024)
        throw std::runtime_error("SKILL.md 超过 512 KB 上限");
    wide(raw);
    std::string s = raw;
    if (s.compare(0, 3, "\xef\xbb\xbf") == 0)
        s.erase(0, 3);
    std::istringstream in(s);
    std::string line;
    if (!std::getline(in, line) || trim(line) != "---")
        throw std::runtime_error("缺少 YAML frontmatter：需要 name 和 description");
    std::vector<std::string> lines;
    bool closed = false;
    size_t body = 0;
    while (std::getline(in, line)) {
        if (trim(line) == "---") {
            closed = true;
            body = (size_t)in.tellg();
            break;
        }
        lines.push_back(line);
    }
    if (!closed)
        throw std::runtime_error("frontmatter 未闭合");
    J v = J::object();
    for (size_t i = 0; i < lines.size(); ++i) {
        line = lines[i];
        if (line.empty() || line[0] == ' ' || line[0] == '\t' || line[0] == '#')
            continue;
        auto p = line.find(':');
        if (p == std::string::npos)
            continue;
        auto k = trim(line.substr(0, p));
        if (k != "name" && k != "description" && k != "category")
            continue;
        if (v.contains(k))
            throw std::runtime_error("frontmatter 存在重复字段：" + k);
        auto val = trim(line.substr(p + 1));
        if (val == ">" || val == ">-" || val == "|" || val == "|-") {
            val = "";
            while (i + 1 < lines.size() &&
                   (lines[i + 1].empty() || lines[i + 1][0] == ' ' || lines[i + 1][0] == '\t')) {
                if (!val.empty())
                    val += ' ';
                val += trim(lines[++i]);
            }
        } else
            val = scalar(val);
        v[k] = val;
    }
    auto name = v.value("name", "");
    bool valid = !name.empty() && name.size() <= 64 && name.front() != '-' && name.back() != '-';
    bool dash = false;
    for (char c : name) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') || (dash && c == '-'))
            valid = false;
        dash = c == '-';
    }
    if (!valid)
        throw std::runtime_error("name 需为 1–64 位小写英文、数字和单连字符");
    auto desc = trim(v.value("description", ""));
    if (desc.empty() || wide(desc).size() > 1024)
        throw std::runtime_error("description 需为 1–1024 个字符");
    if (body == std::string::npos || body > s.size() || trim(s.substr(body)).size() < 20)
        throw std::runtime_error("请提供完整的使用步骤和输出要求");
    return {{"name", name}, {"description", desc}, {"category", v.value("category", "通用工具")}};
}
J skill_quality(const std::string &content) {
    J report = {{"valid", false}, {"suggestions", J::array()}, {"description_chars", 0}, {"body_chars", 0}};
    try {
        auto meta = metadata(content);
        report["valid"] = true;
        auto description = meta["description"].get<std::string>();
        report["description_chars"] = wide(description).size();
        auto end = content.find("\n---", content.find("---") + 3);
        auto body = end == std::string::npos ? content : content.substr(end + 4);
        report["body_chars"] = wide(trim(body)).size();
        auto lower = body;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return c < 128 ? (char)tolower(c) : (char)c; });
        auto has = [&](std::initializer_list<const char *> words) {
            for (auto word : words)
                if (lower.find(word) != std::string::npos)
                    return true;
            return false;
        };
        if (wide(description).size() < 40)
            report["suggestions"].push_back("描述较短：确认已说明具体能力，以及何时、针对什么输入使用。");
        if (wide(trim(body)).size() < 400)
            report["suggestions"].push_back("正文较短：检查关键步骤是否包含具体操作、判断条件和失败处理。");
        if (!has({"输出", "产物", "output", "deliverable"}))
            report["suggestions"].push_back("补充输出结构、字段或交付物要求。");
        if (!has({"验证", "校验", "检查", "复核", "验收", "validat", "verif", "check", "test"}))
            report["suggestions"].push_back("补充可执行的验证或验收方法。");
        if (!has({"示例", "例如", "example", "sample"}))
            report["suggestions"].push_back("考虑加入一个展示关键决策的具体输入与输出示例。");
        if (has({"todo", "tbd", "待补充", "lorem ipsum"}))
            report["suggestions"].push_back("存在可能未完成的占位内容，请逐项检查。");
    } catch (const std::exception &e) {
        report["suggestions"].push_back(e.what());
    }
    return report;
}
std::vector<fs::path> bundle(const fs::path &root) {
    check_path(root);
    std::vector<fs::path> files;
    uint64_t total = 0;
    if (!fs::is_directory(root))
        return files;
    fs::recursive_directory_iterator it(root), end;
    for (; it != end; ++it) {
        const auto &p = it->path();
        check_path(p);
        auto name = pathstr(p.filename());
        if (it->is_directory()) {
            if (name == ".git" || name == "node_modules" || name == "__pycache__")
                it.disable_recursion_pending();
            if (it.depth() > 32)
                throw std::runtime_error("Skill 目录层级过深");
            continue;
        }
        if (!it->is_regular_file())
            throw std::runtime_error("Skill 包含不支持的文件");
        auto rel = p.lexically_relative(root);
        if (!inside(p, root))
            throw std::runtime_error("Skill 路径越界");
        total += it->file_size();
        if (total > 20 * 1024 * 1024 || files.size() >= 2000)
            throw std::runtime_error("Skill 附件最多 20 MB / 2000 个文件");
        files.push_back(rel);
    }
    std::sort(files.begin(), files.end(), [](const fs::path &a, const fs::path &b) {
        return pathstr(a.generic_wstring()) < pathstr(b.generic_wstring());
    });
    return files;
}
std::string tree_hash(const fs::path &root) {
    std::string acc;
    for (auto &p : bundle(root)) {
        auto rel = utf8(p.generic_wstring());
        auto h = sha(read(root / p));
        acc += rel;
        acc += '\0';
        for (size_t i = 0; i < h.size(); i += 2)
            acc += (char)std::stoi(h.substr(i, 2), nullptr, 16);
    }
    return sha(acc);
}
static void copy_tree(const fs::path &src, const fs::path &dst) {
    check_path(dst);
    if (fs::exists(dst))
        throw std::runtime_error("目标目录已存在");
    fs::create_directories(dst);
    try {
        for (const auto &p : bundle(src))
            atomic_write(dst / p, read(src / p));
        if (tree_hash(src) != tree_hash(dst))
            throw std::runtime_error("复制过程中源目录发生变化");
    } catch (...) {
        fs::remove_all(dst);
        throw;
    }
}
static void move_tree(const fs::path &src, const fs::path &dst) {
    check_path(src);
    check_path(dst);
    fs::create_directories(dst.parent_path());
    if (fs::exists(dst))
        throw std::runtime_error("目标位置已有文件");
    std::error_code ec;
    fs::rename(src, dst, ec);
    if (!ec)
        return;
    if (ec.value() != ERROR_NOT_SAME_DEVICE)
        throw std::runtime_error("无法移动部署目录，请检查目录权限和文件占用");
    auto stage = dst;
    stage += L".stage-" + wide(uuid());
    copy_tree(src, stage);
    fs::rename(stage, dst);
    try {
        fs::remove_all(src);
    } catch (...) {
        // A failed recursive removal may already have removed some files. Restore
        // missing files before discarding the complete destination snapshot.
        bool restored = false;
        try {
            for (auto &rel : bundle(dst))
                if (!fs::exists(src / rel))
                    atomic_write(src / rel, read(dst / rel));
            restored = tree_hash(src) == tree_hash(dst);
        } catch (...) {
        }
        if (restored) {
            fs::remove_all(dst);
            throw;
        }
        throw std::runtime_error("跨盘移动未完成，完整副本已保留在：" + pathstr(dst));
    }
}
Store::Store(fs::path p) : root(fs::absolute(p).lexically_normal()), db(root / "agentdock.db") {
    // SQLite online backup is consistent even if a prior desktop version is still open.
    auto tables = db.query("SELECT name FROM sqlite_master WHERE type='table' AND name='skills'");
    if (!tables.empty() && db.query("SELECT value FROM settings WHERE key='native_schema'").empty() &&
        !fs::exists(root / "backups/pre-cpp.db")) {
        fs::create_directories(root / "backups");
        DB backup(root / "backups/pre-cpp.db");
        auto b = sqlite3_backup_init(backup.db, "main", db.db, "main");
        if (!b)
            throw std::runtime_error("迁移前备份失败");
        int r = sqlite3_backup_step(b, -1);
        sqlite3_backup_finish(b);
        if (r != SQLITE_DONE)
            throw std::runtime_error("数据库正在使用，请退出旧版后重试");
    }
    db.exec(R"SQL(
PRAGMA journal_mode=WAL;
CREATE TABLE IF NOT EXISTS blobs(hash TEXT PRIMARY KEY,content TEXT NOT NULL,bytes INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS skills(id TEXT PRIMARY KEY,name TEXT,title TEXT,description TEXT,category TEXT,source TEXT,origin TEXT UNIQUE,content_hash TEXT,fingerprint TEXT,managed INTEGER,enabled INTEGER DEFAULT 1,favorite INTEGER DEFAULT 0,modified REAL);
CREATE TABLE IF NOT EXISTS revisions(skill_id TEXT,hash TEXT,created REAL,PRIMARY KEY(skill_id,hash));
CREATE TABLE IF NOT EXISTS providers(id TEXT PRIMARY KEY,data TEXT NOT NULL,secret TEXT);
CREATE TABLE IF NOT EXISTS deployments(skill_id TEXT,agent TEXT,path TEXT UNIQUE,hash TEXT,active INTEGER DEFAULT 1,PRIMARY KEY(skill_id,agent));
CREATE TABLE IF NOT EXISTS events(id INTEGER PRIMARY KEY,kind TEXT,title TEXT,detail TEXT,created REAL);
CREATE TABLE IF NOT EXISTS settings(key TEXT PRIMARY KEY,value TEXT);
CREATE INDEX IF NOT EXISTS skills_managed ON skills(managed,modified DESC);
CREATE VIRTUAL TABLE IF NOT EXISTS skill_fts USING fts5(id UNINDEXED,name,title,description,tokenize='trigram');
)SQL");
    if (setting("seeded").is_null()) {
        auto seed_catalog = resource_json(100);
        for (auto &seed : seed_catalog["seeds"]) {
            std::string text = "---\nname: " + seed[0].get<std::string>() +
                               "\ndescription: " + seed[2].dump(-1, ' ', false) + "\n---\n\n# " +
                               seed[1].get<std::string>() + "\n\n## 工作流程\n\n" +
                               seed[4].get<std::string>() +
                               "\n\n## 输出检查\n确认结果回应了用户目标，保留依据与限制。\n";
            save({{"content", text}, {"title", seed[1]}, {"category", seed[3]}, {"source", "内置模板"}});
        }
        setting("seeded", true);
    }
    setting("native_schema", 1);
    if (setting("category_schema") != 1) {
        Tx tx(db);
        for (auto &row :
             db.query("SELECT id,name,description FROM skills WHERE category='通用工具' OR category=''"))
            db.run("UPDATE skills SET category=? WHERE id=?",
                   {classify_skill(row["name"], row["description"]), row["id"]});
        setting("category_schema", 1);
        tx.commit();
    }
    if (setting("fts_schema") != 2) {
        db.exec("DELETE FROM skill_fts; INSERT INTO skill_fts(rowid,id,name,title,description) SELECT "
                "rowid,id,name,title,description FROM skills;");
        setting("fts_schema", 2);
    }
}
J Store::setting(const std::string &k) {
    std::lock_guard lock(mutex);
    auto r = db.query("SELECT value FROM settings WHERE key=?", {k});
    return r.empty() ? J() : J::parse(r[0]["value"].get<std::string>());
}
void Store::setting(const std::string &k, const J &v) {
    std::lock_guard lock(mutex);
    db.run("INSERT OR REPLACE INTO settings VALUES(?,?)", {k, v.dump()});
}
void Store::event(const std::string &k, const std::string &t, const std::string &d) {
    std::lock_guard lock(mutex);
    db.run("INSERT INTO events(kind,title,detail,created) VALUES(?,?,?,?)", {k, t, d, now()});
    db.exec("DELETE FROM events WHERE id NOT IN (SELECT id FROM events ORDER BY id DESC LIMIT 200)");
}
void Store::index(const std::string &id) {
    db.run("DELETE FROM skill_fts WHERE rowid=(SELECT rowid FROM skills WHERE id=?)", {id});
    db.run("INSERT INTO skill_fts(rowid,id,name,title,description) SELECT rowid,id,name,title,description "
           "FROM skills WHERE id=?",
           {id});
}
J Store::save(J data) {
    std::lock_guard lock(mutex);
    auto text = data.value("content", "");
    auto meta = metadata(text);
    auto id = data.value("id", uuid());
    auto title = data.value("title", meta["name"].get<std::string>());
    if (trim(title).empty())
        title = meta["name"];
    if (wide(title).size() > 120)
        throw std::runtime_error("标题最多 120 个字符");
    auto h = sha(text);
    Tx tx(db);
    auto old = db.query("SELECT managed FROM skills WHERE id=?", {id});
    if (!old.empty() && old[0]["managed"] == 0)
        throw std::runtime_error("外部 Skill 请先导入工具库后再编辑");
    db.run("INSERT OR IGNORE INTO blobs VALUES(?,?,?)", {h, text, text.size()});
    db.run("INSERT INTO "
           "skills(id,name,title,description,category,source,origin,content_hash,fingerprint,managed,enabled,"
           "favorite,modified) VALUES(?,?,?,?,?,?,NULL,?,'',1,1,0,?) ON CONFLICT(id) DO UPDATE SET "
           "name=excluded.name,title=excluded.title,description=excluded.description,category=excluded."
           "category,content_hash=excluded.content_hash,modified=excluded.modified",
           {id, meta["name"], title, meta["description"],
            data.contains("category") && !data["category"].get<std::string>().empty()
                ? data["category"].get<std::string>()
                : classify_skill(meta["name"], meta["description"], meta["category"]),
            data.value("source", "手动创建"), h, now()});
    db.run("INSERT OR IGNORE INTO revisions VALUES(?,?,?)", {id, h, now()});
    index(id);
    tx.commit();
    event("skill", "已保存 " + title, "旧版本可在编辑窗口恢复");
    return skill(id);
}
J Store::skill(const std::string &id) {
    std::lock_guard lock(mutex);
    auto r = db.query(
        "SELECT s.*,b.content,b.bytes FROM skills s JOIN blobs b ON b.hash=s.content_hash WHERE s.id=?",
        {id});
    if (r.empty())
        throw std::runtime_error("Skill 不存在");
    auto s = r[0];
    s["deployments"] = db.query("SELECT * FROM deployments WHERE skill_id=?", {id});
    s["revisions"] =
        db.query("SELECT hash,created FROM revisions WHERE skill_id=? ORDER BY created DESC LIMIT 30", {id});
    return s;
}
J Store::list(const std::string &q, const std::string &scope, const std::string &category, int limit,
              int offset) {
    std::lock_guard lock(mutex);
    std::string where = "1=1";
    J args = J::array();
    if (scope == "managed")
        where += " AND s.managed=1";
    else if (scope == "external")
        where += " AND s.managed=0";
    else if (scope == "favorites")
        where += " AND s.favorite=1";
    if (category != "all") {
        where += " AND s.category=?";
        args.push_back(category);
    }
    if (!q.empty()) {
        size_t characters = 0;
        for (unsigned char c : q)
            if ((c & 0xC0) != 0x80)
                ++characters;
        if (characters >= 3) {
            where += " AND s.id IN (SELECT id FROM skill_fts WHERE skill_fts MATCH ?)";
            std::string pat = "\"";
            for (char c : q) {
                pat += c;
                if (c == '"')
                    pat += c;
            }
            pat += '"';
            args.push_back(pat);
        } else {
            where += " AND (s.name LIKE ? ESCAPE '!' OR s.title LIKE ? ESCAPE '!' OR s.description LIKE ? "
                     "ESCAPE '!')";
            std::string pat = "%";
            for (char c : q) {
                if (c == '%' || c == '_' || c == '!')
                    pat += '!';
                pat += c;
            }
            pat += '%';
            for (int i = 0; i < 3; ++i)
                args.push_back(pat);
        }
    }
    auto count = db.query("SELECT count(*) AS total FROM skills s WHERE " + where, args)[0]["total"];
    args.push_back(std::clamp(limit, 1, 100));
    args.push_back(std::max(offset, 0));
    auto rows = db.query("SELECT s.*,b.bytes FROM skills s JOIN blobs b ON b.hash=s.content_hash WHERE " +
                             where + " ORDER BY favorite DESC,managed DESC,modified DESC LIMIT ? OFFSET ?",
                         args);
    return {{"items", rows},
            {"total", count},
            {"categories", db.query("SELECT category,count(*) AS count FROM skills GROUP BY category")}};
}
J Store::scan(const std::vector<std::pair<std::string, fs::path>> &roots) {
    cancel_scan = false;
    auto started = std::chrono::steady_clock::now();
    std::map<std::string, J> cache;
    {
        std::lock_guard lock(mutex);
        for (auto &row : db.query("SELECT id,origin,fingerprint FROM skills WHERE managed=0"))
            cache[row["origin"]] = row;
    }
    J pending = J::array(), warnings = J::array();
    std::set<std::string> visited;
    size_t traversed = 0;
    for (auto &[agent, rootPath] : roots) {
        if (cancel_scan)
            throw std::runtime_error("扫描已取消");
        try {
            check_path(rootPath);
            if (!fs::is_directory(rootPath))
                continue;
            std::vector<std::pair<fs::path, int>> stack = {{rootPath, 0}};
            while (!stack.empty()) {
                if (cancel_scan)
                    throw std::runtime_error("扫描已取消");
                if (++traversed > 100000)
                    throw std::runtime_error("扫描目录超过 100000 项，请缩小技能目录范围");
                auto [folder, depth] = stack.back();
                stack.pop_back();
                try {
                    check_path(folder);
                    auto file = folder / "SKILL.md";
                    if (fs::is_regular_file(file)) {
                        auto origin = pathstr(fs::absolute(file).lexically_normal());
                        auto key = wide(origin);
                        std::transform(key.begin(), key.end(), key.begin(), towlower);
                        if (!visited.insert(utf8(key)).second)
                            continue;
                        auto fp =
                            "cpp:" + std::to_string(fs::last_write_time(file).time_since_epoch().count()) +
                            ":" + std::to_string(fs::file_size(file));
                        auto old = cache.find(origin);
                        if (old != cache.end() && old->second["fingerprint"] == fp)
                            continue;
                        auto text = read(file, 512 * 1024);
                        if (text.compare(0, 3, "\xef\xbb\xbf") == 0)
                            text.erase(0, 3);
                        auto meta = metadata(text);
                        pending.push_back(
                            {{"id", old == cache.end() ? uuid() : old->second["id"].get<std::string>()},
                             {"origin", origin},
                             {"fingerprint", fp},
                             {"content", text},
                             {"hash", sha(text)},
                             {"name", meta["name"]},
                             {"description", meta["description"]},
                             {"category",
                              classify_skill(meta["name"], meta["description"], meta["category"])},
                             {"source", agent}});
                    } else if (depth < 3) {
                        for (auto &entry : fs::directory_iterator(folder)) {
                            auto name = pathstr(entry.path().filename());
                            if (entry.is_directory() && !name.empty() && name[0] != '.' &&
                                name != "node_modules" && name != "__pycache__")
                                stack.emplace_back(entry.path(), depth + 1);
                        }
                    }
                } catch (const std::exception &e) {
                    if (warnings.size() < 20)
                        warnings.push_back(pathstr(folder) + ": " + e.what());
                }
            }
        } catch (const std::exception &e) {
            if (cancel_scan || traversed > 100000)
                throw;
            if (warnings.size() < 20)
                warnings.push_back(agent + ": " + e.what());
        }
    }
    std::vector<std::string> missing;
    for (auto &[origin, row] : cache) {
        std::error_code ec;
        if (!fs::exists(path(origin), ec) && !ec)
            missing.push_back(origin);
    }
    if (cancel_scan)
        throw std::runtime_error("扫描已取消");
    std::lock_guard lock(mutex);
    Tx tx(db);
    for (auto &row : pending) {
        auto id = row["id"].get<std::string>();
        auto text = row["content"].get<std::string>();
        db.run("INSERT OR IGNORE INTO blobs VALUES(?,?,?)", {row["hash"], text, text.size()});
        db.run("INSERT INTO "
               "skills(id,name,title,description,category,source,origin,content_hash,fingerprint,managed,"
               "modified) VALUES(?,?,?,?,?,?,?,?,?,0,?) ON CONFLICT(id) DO UPDATE SET "
               "name=excluded.name,title=excluded.title,description=excluded.description,content_hash="
               "excluded.content_hash,fingerprint=excluded.fingerprint,modified=excluded.modified",
               {id, row["name"], row["name"], row["description"], row["category"], row["source"],
                row["origin"], row["hash"], row["fingerprint"], now()});
        index(id);
    }
    for (auto &origin : missing) {
        auto id = cache.at(origin)["id"];
        if (db.query("SELECT 1 FROM deployments WHERE skill_id=? LIMIT 1", {id}).empty()) {
            db.run("DELETE FROM skill_fts WHERE rowid=(SELECT rowid FROM skills WHERE id=? AND managed=0)",
                   {id});
            db.run("DELETE FROM skills WHERE id=? AND managed=0", {id});
        }
    }
    db.exec("DELETE FROM blobs WHERE hash NOT IN (SELECT content_hash FROM skills) AND hash NOT IN (SELECT "
            "hash FROM revisions)");
    tx.commit();
    scan_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    setting("last_scan", now());
    return {{"changed", pending.size()}, {"warnings", warnings}, {"ms", scan_ms}};
}
J Store::import_skill(fs::path p) {
    std::lock_guard lock(mutex);
    p = fs::absolute(p).lexically_normal();
    check_path(p);
    if (fs::is_regular_file(p)) {
        if (p.filename() != L"SKILL.md")
            throw std::runtime_error("请选择 SKILL.md 或完整 Skill 文件夹");
        p = p.parent_path();
    }
    auto text = read(p / "SKILL.md", 512 * 1024);
    metadata(text);
    auto id = uuid();
    auto dest = root / "resources" / id;
    try {
        for (auto &rel : bundle(p))
            if (rel != L"SKILL.md")
                atomic_write(dest / rel, read(p / rel));
        return save({{"id", id}, {"content", text}, {"source", "本地导入"}});
    } catch (...) {
        if (inside(dest, root / "resources"))
            fs::remove_all(dest);
        throw;
    }
}
J Store::deployment_check(const std::string &id, const std::string &agent, fs::path target) {
    std::lock_guard lock(mutex);
    auto s = skill(id);
    if (s["enabled"] == 0)
        throw std::runtime_error("请先启用此 Skill");
    auto resources = s["managed"] != 0 ? root / "resources" / id : path(s["origin"]).parent_path();
    if (s["managed"] == 0) {
        auto current = read(resources / "SKILL.md", 512 * 1024);
        if (current.compare(0, 3, "\xef\xbb\xbf") == 0)
            current.erase(0, 3);
        if (sha(current) != s["content_hash"].get<std::string>())
            throw std::runtime_error("源 Skill 已变化，请扫描更新后重新部署");
    }
    target = fs::absolute(target).lexically_normal();
    check_path(target);
    auto dest = target / path(s["name"]);
    check_path(dest);
    if (s["managed"] == 0) {
        if (fs::exists(dest) && fs::equivalent(dest, resources))
            return {{"path", pathstr(dest)}, {"already", true}, {"files", bundle(resources).size()}};
    }
    auto old = db.query("SELECT * FROM deployments WHERE skill_id=? AND agent=?", {id, agent});
    if (!old.empty()) {
        if (old[0]["active"] == 0)
            throw std::runtime_error("此部署已暂停，请在部署记录中恢复");
        if (path(old[0]["path"]) != dest)
            throw std::runtime_error("名称或部署路径已变化，请先处理原部署");
    }
    if (fs::exists(dest) && (old.empty() || tree_hash(dest) != old[0]["hash"].get<std::string>()))
        throw std::runtime_error("目标已有同名 Skill 或已被外部修改，未覆盖任何文件");
    auto collision = db.query("SELECT skill_id,agent FROM deployments WHERE path=?", {pathstr(dest)});
    if (!collision.empty() && (collision[0]["skill_id"] != id || collision[0]["agent"] != agent))
        throw std::runtime_error("该目录已由其他部署记录管理");
    size_t files = fs::is_directory(resources) ? bundle(resources).size() : 0;
    if (!fs::is_regular_file(resources / "SKILL.md"))
        ++files;
    return {{"path", pathstr(dest)}, {"already", false}, {"files", files}};
}
fs::path Store::deploy(const std::string &id, const std::string &agent, fs::path target) {
    std::lock_guard lock(mutex);
    auto report = deployment_check(id, agent, target);
    auto dest = path(report["path"]);
    if (report["already"].get<bool>())
        return dest;
    auto s = skill(id);
    auto resources = s["managed"] != 0 ? root / "resources" / id : path(s["origin"]).parent_path();
    auto originalTree = s["managed"] == 0 ? tree_hash(resources) : "";
    target = dest.parent_path();
    auto stage = target / (".agentdock-" + uuid()), backup = target / (".agentdock-backup-" + uuid());
    fs::create_directories(stage);
    bool swapped = false;
    try {
        if (fs::is_directory(resources))
            for (auto &p : bundle(resources))
                if (p != L"SKILL.md")
                    atomic_write(stage / p, read(resources / p));
        atomic_write(stage / "SKILL.md", s["content"]);
        if (s["managed"] == 0 && tree_hash(resources) != originalTree)
            throw std::runtime_error("源目录在部署过程中发生变化，请重试");
        auto h = tree_hash(stage);
        if (fs::exists(dest))
            fs::rename(dest, backup);
        fs::rename(stage, dest);
        swapped = true;
        try {
            db.run("INSERT OR REPLACE INTO deployments VALUES(?,?,?,?,1)", {id, agent, pathstr(dest), h});
        } catch (...) {
            fs::remove_all(dest);
            swapped = false;
            if (fs::exists(backup))
                fs::rename(backup, dest);
            throw;
        }
        if (fs::exists(backup))
            fs::remove_all(backup);
    } catch (...) {
        if (fs::exists(stage))
            fs::remove_all(stage);
        if (!swapped && fs::exists(backup) && !fs::exists(dest))
            fs::rename(backup, dest);
        throw;
    }
    event("deploy", "Skill 已部署", s["title"].get<std::string>() + " → " + agent);
    return dest;
}
J Store::deployments() {
    std::lock_guard lock(mutex);
    return db.query("SELECT d.*,s.title,s.name,s.managed FROM deployments d JOIN skills s ON s.id=d.skill_id "
                    "ORDER BY d.rowid DESC");
}
void Store::toggle_deployment(const std::string &id, const std::string &agent, bool active) {
    std::lock_guard lock(mutex);
    auto rows = db.query("SELECT * FROM deployments WHERE skill_id=? AND agent=?", {id, agent});
    if (rows.empty())
        throw std::runtime_error("部署记录不存在");
    auto d = rows[0];
    if ((d["active"] != 0) == active)
        return;
    auto live = path(d["path"]), shelf = root / "shelf" / (id + "-" + sha(agent).substr(0, 16));
    if (agent.find_first_of(":<>\"|?*") == std::string::npos) {
        auto old = root / "shelf" / (id + "-" + agent);
        if (fs::exists(old) && !fs::exists(shelf))
            shelf = old;
    }
    auto from = active ? shelf : live, to = active ? live : shelf;
    check_path(from);
    check_path(to);
    if (!fs::is_directory(from) || tree_hash(from) != d["hash"].get<std::string>())
        throw std::runtime_error("部署副本已被外部修改或移除，未改动任何文件");
    if (fs::exists(to))
        throw std::runtime_error("目标位置已存在文件，请先检查目录");
    move_tree(from, to);
    try {
        db.run("UPDATE deployments SET active=? WHERE skill_id=? AND agent=?", {active, id, agent});
    } catch (...) {
        move_tree(to, from);
        throw;
    }
    event("deploy", active ? "已恢复部署副本" : "已暂停部署副本", agent);
}
void Store::toggle(const std::string &id, const std::string &field, bool value) {
    std::lock_guard lock(mutex);
    auto s = skill(id);
    if (field == "favorite") {
        db.run("UPDATE skills SET favorite=? WHERE id=?", {value, id});
        return;
    }
    if (field != "enabled")
        throw std::runtime_error("不支持的字段");
    if (s["managed"] == 0)
        throw std::runtime_error("外部 Skill 的启用状态请在原工具中管理");
    std::vector<std::pair<fs::path, fs::path>> moves, done;
    for (auto &d : s["deployments"]) {
        if ((d["active"] != 0) == value)
            continue;
        auto live = path(d["path"]),
             shelf = root / "shelf" /
                     (id + "-" +
                      sha(d["agent"])
                          .substr(0, 16)); // Stable legal Windows name, including old keys containing ':'.
        // Read the old Python shelf when its legal path exists.
        std::string oldkey = d["agent"];
        if (oldkey.find_first_of(":<>\"|?*") == std::string::npos) {
            auto old = root / "shelf" / (id + "-" + oldkey);
            if (fs::exists(old) && !fs::exists(shelf))
                shelf = old;
        }
        auto src = value ? shelf : live, dst = value ? live : shelf;
        check_path(src);
        check_path(dst);
        if (!fs::is_directory(src) || tree_hash(src) != d["hash"].get<std::string>())
            throw std::runtime_error("已部署的 Skill 被外部修改或移除，请先检查目标目录");
        if (fs::exists(dst))
            throw std::runtime_error("目标位置已有文件，无法切换");
        moves.emplace_back(src, dst);
    }
    try {
        for (auto &m : moves) {
            move_tree(m.first, m.second);
            done.push_back(m);
        }
        Tx tx(db);
        db.run("UPDATE deployments SET active=? WHERE skill_id=?", {value, id});
        db.run("UPDATE skills SET enabled=? WHERE id=?", {value, id});
        tx.commit();
    } catch (...) {
        for (auto it = done.rbegin(); it != done.rend(); ++it) {
            try {
                move_tree(it->second, it->first);
            } catch (...) {
                event("error", "恢复部署目录失败", "请检查 shelf 与目标目录");
            }
        }
        throw;
    }
}
std::string Store::revision(const std::string &id, const std::string &h) {
    std::lock_guard lock(mutex);
    auto r = db.query(
        "SELECT b.content FROM revisions r JOIN blobs b ON b.hash=r.hash WHERE r.skill_id=? AND r.hash=?",
        {id, h});
    if (r.empty())
        throw std::runtime_error("版本不存在");
    return r[0]["content"];
}
static uint32_t crc32(const std::string &s) {
    uint32_t c = ~0u;
    for (auto ch : s) {
        c ^= (unsigned char)ch;
        for (int i = 0; i < 8; ++i)
            c = (c >> 1) ^ (0xedb88320u & -(int)(c & 1));
    }
    return ~c;
}
void Store::export_zip(const std::string &id, fs::path out) {
    std::lock_guard lock(mutex);
    auto s = skill(id);
    std::vector<std::pair<std::string, std::string>> files;
    auto name = s["name"].get<std::string>();
    files.emplace_back(name + "/SKILL.md", s["content"]);
    auto dir = s["managed"] != 0 ? root / "resources" / id : path(s["origin"]).parent_path();
    for (auto &p : bundle(dir))
        if (p != L"SKILL.md")
            files.emplace_back(name + "/" + utf8(p.generic_wstring()), read(dir / p));
    std::string zip, central;
    auto u16 = [](std::string &z, uint32_t n) {
        z += (char)n;
        z += (char)(n >> 8);
    };
    auto u32 = [&](std::string &z, uint32_t n) {
        u16(z, n);
        u16(z, n >> 16);
    };
    for (auto &[n, b] : files) {
        uint32_t off = (uint32_t)zip.size(), crc = crc32(b);
        u32(zip, 0x04034b50);
        u16(zip, 20);
        u16(zip, 0x800);
        u16(zip, 0);
        u16(zip, 0);
        u16(zip, 33);
        u32(zip, crc);
        u32(zip, (uint32_t)b.size());
        u32(zip, (uint32_t)b.size());
        u16(zip, (uint32_t)n.size());
        u16(zip, 0);
        zip += n;
        zip += b;
        u32(central, 0x02014b50);
        u16(central, 20);
        u16(central, 20);
        u16(central, 0x800);
        u16(central, 0);
        u16(central, 0);
        u16(central, 33);
        u32(central, crc);
        u32(central, (uint32_t)b.size());
        u32(central, (uint32_t)b.size());
        u16(central, (uint32_t)n.size());
        for (int i = 0; i < 4; ++i)
            u16(central, 0);
        u32(central, 0);
        u32(central, off);
        central += n;
    }
    uint32_t offset = (uint32_t)zip.size();
    zip += central;
    u32(zip, 0x06054b50);
    u16(zip, 0);
    u16(zip, 0);
    u16(zip, (uint32_t)files.size());
    u16(zip, (uint32_t)files.size());
    u32(zip, (uint32_t)central.size());
    u32(zip, offset);
    u16(zip, 0);
    atomic_write(out, zip);
}
J Store::stats() {
    std::lock_guard lock(mutex);
    auto r = db.query("SELECT count(*) AS total,coalesce(sum(managed),0) AS "
                      "managed,coalesce(sum(favorite),0) AS favorites FROM skills")[0];
    r["deployments"] = db.query("SELECT count(*) AS n FROM deployments WHERE active=1")[0]["n"];
    r["events"] = db.query("SELECT * FROM events ORDER BY id DESC LIMIT 8");
    r["scan_ms"] = scan_ms;
    r["data_bytes"] = storage_bytes.load();
    r["storage_ready"] = storage_ready.load();
    auto b =
        db.query(
              "SELECT coalesce(sum(bytes),0) AS n FROM blobs WHERE hash IN (SELECT content_hash FROM skills)")
            [0]["n"]
                .get<int64_t>();
    auto raw =
        db.query("SELECT coalesce(sum(b.bytes),0) AS n FROM skills s JOIN blobs b ON b.hash=s.content_hash")
            [0]["n"]
                .get<int64_t>();
    r["saved_bytes"] = std::max<int64_t>(0, raw - b);
    return r;
}
uint64_t Store::measure_storage(std::atomic<bool> &cancel) {
    uint64_t total = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator i(root, fs::directory_options::skip_permission_denied, ec), end;
         i != end && !cancel; i.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        auto attributes = GetFileAttributesW(i->path().c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            i.disable_recursion_pending();
            continue;
        }
        if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            auto size = i->file_size(ec);
            if (!ec)
                total += size;
            ec.clear();
        }
    }
    if (!cancel) {
        storage_bytes = total;
        storage_ready = true;
    }
    return storage_bytes.load();
}
} // namespace ad
