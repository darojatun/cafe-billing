#include "Database.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>

Database::Database(const std::string& path) : path_(path) {}

Database::~Database() {
    if (db_) sqlite3_close(db_);
}

bool Database::exec(const std::string& sql) {
    char* err = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\nSQL: %s\n", err, sql.c_str());
        sqlite3_free(err);
        return false;
    }
    return true;
}

// Basic SQL injection prevention for string values
std::string Database::escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        if (c == '\'') out += "''";
        else out += c;
    }
    return out;
}

bool Database::init() {
    int rc = sqlite3_open(path_.c_str(), &db_);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db_));
        return false;
    }

    exec("PRAGMA journal_mode=WAL;");
    exec("PRAGMA foreign_keys=ON;");

    exec(R"(
        CREATE TABLE IF NOT EXISTS packages (
            id            INTEGER PRIMARY KEY AUTOINCREMENT,
            name          TEXT NOT NULL,
            is_timed      INTEGER NOT NULL DEFAULT 0,
            duration_sec  INTEGER NOT NULL DEFAULT 0,
            price         REAL    NOT NULL DEFAULT 0
        );
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS menu_items (
            id            INTEGER PRIMARY KEY AUTOINCREMENT,
            name          TEXT    NOT NULL,
            category      TEXT    NOT NULL DEFAULT 'Other',
            price         REAL    NOT NULL DEFAULT 0,
            description   TEXT    NOT NULL DEFAULT '',
            available     INTEGER NOT NULL DEFAULT 1
        );
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS sessions (
            id            INTEGER PRIMARY KEY AUTOINCREMENT,
            station_name  TEXT    NOT NULL,
            username      TEXT    NOT NULL DEFAULT '',
            package_name  TEXT    NOT NULL DEFAULT '',
            start_time    INTEGER NOT NULL,
            end_time      INTEGER NOT NULL,
            duration_sec  INTEGER NOT NULL DEFAULT 0,
            total_cost    REAL    NOT NULL DEFAULT 0
        );
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS settings (
            key   TEXT PRIMARY KEY,
            value TEXT NOT NULL
        );
    )");

    // Seed defaults only for a new database. Without a uniqueness constraint,
    // INSERT OR IGNORE would still add the same rows on every startup.
    exec(R"(
        INSERT INTO packages (name, is_timed, duration_sec, price)
        VALUES
            ('Open / Hourly', 0, 0, 3000),
            ('1 Hour',        1, 3600,  3000),
            ('2 Hours',       1, 7200,  5500),
            ('3 Hours',       1, 10800, 7500),
            ('5 Hours',       1, 18000, 12000)
        WHERE NOT EXISTS (SELECT 1 FROM packages);
    )");

    // Default menu items
    exec(R"(
        INSERT INTO menu_items (name, category, price, description, available)
        VALUES
            ('Mie Goreng',       'Food',  12000, 'Fried noodles with egg',         1),
            ('Nasi Goreng',      'Food',  13000, 'Fried rice with chicken',         1),
            ('Roti Bakar',       'Food',   8000, 'Toast with butter and jam',       1),
            ('Es Teh Manis',     'Drink',  5000, 'Iced sweet tea',                  1),
            ('Es Jeruk',         'Drink',  7000, 'Iced fresh orange juice',         1),
            ('Kopi Susu',        'Drink',  8000, 'Milk coffee',                     1),
            ('Air Mineral',      'Drink',  3000, '600ml mineral water',             1),
            ('Indomie Kuah',     'Food',  10000, 'Cup noodle soup',                 1),
            ('Keripik Singkong', 'Snack',  5000, 'Cassava chips',                   1),
            ('Cokelat Wafer',    'Snack',  4000, 'Chocolate wafer bar',             1)
        WHERE NOT EXISTS (SELECT 1 FROM menu_items);
    )");

    // Default settings
    exec(R"(
        INSERT OR IGNORE INTO settings (key, value) VALUES
            ('cafe_name',      'My Internet Cafe'),
            ('cafe_address',   ''),
            ('cafe_phone',     ''),
            ('wifi_password',  ''),
            ('server_ip',      '0.0.0.0'),
            ('server_port',    '12345'),
            ('default_rate',   '3000'),
            ('rate_per_minute','50');
    )");

    return true;
}

// ── Packages ──────────────────────────────────────────────────────────────

bool Database::addPackage(const Package& p) {
    char sql[512];
    snprintf(sql, sizeof(sql),
        "INSERT INTO packages (name, is_timed, duration_sec, price) "
        "VALUES ('%s', %d, %d, %.2f);",
        escape(p.name).c_str(), p.is_timed ? 1 : 0, p.duration_sec, p.price);
    return exec(sql);
}

bool Database::updatePackage(const Package& p) {
    char sql[512];
    snprintf(sql, sizeof(sql),
        "UPDATE packages SET name='%s', is_timed=%d, duration_sec=%d, price=%.2f "
        "WHERE id=%d;",
        escape(p.name).c_str(), p.is_timed ? 1 : 0, p.duration_sec, p.price, p.id);
    return exec(sql);
}

bool Database::deletePackage(int id) {
    char sql[128];
    snprintf(sql, sizeof(sql), "DELETE FROM packages WHERE id=%d;", id);
    return exec(sql);
}

std::vector<Package> Database::getPackages() {
    std::vector<Package> list;
    const char* sql = "SELECT id, name, is_timed, duration_sec, price FROM packages ORDER BY id;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return list;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Package p;
        p.id           = sqlite3_column_int(stmt, 0);
        p.name         = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        p.is_timed     = sqlite3_column_int(stmt, 2) != 0;
        p.duration_sec = sqlite3_column_int(stmt, 3);
        p.price        = sqlite3_column_double(stmt, 4);
        list.push_back(p);
    }
    sqlite3_finalize(stmt);
    return list;
}

// ── Menu Items ─────────────────────────────────────────────────────────────

bool Database::addMenuItem(const MenuItem& m) {
    char sql[1024];
    snprintf(sql, sizeof(sql),
        "INSERT INTO menu_items (name, category, price, description, available) "
        "VALUES ('%s', '%s', %.2f, '%s', %d);",
        escape(m.name).c_str(), escape(m.category).c_str(),
        m.price, escape(m.description).c_str(), m.available ? 1 : 0);
    return exec(sql);
}

bool Database::updateMenuItem(const MenuItem& m) {
    char sql[1024];
    snprintf(sql, sizeof(sql),
        "UPDATE menu_items SET name='%s', category='%s', price=%.2f, "
        "description='%s', available=%d WHERE id=%d;",
        escape(m.name).c_str(), escape(m.category).c_str(),
        m.price, escape(m.description).c_str(), m.available ? 1 : 0, m.id);
    return exec(sql);
}

bool Database::deleteMenuItem(int id) {
    char sql[128];
    snprintf(sql, sizeof(sql), "DELETE FROM menu_items WHERE id=%d;", id);
    return exec(sql);
}

std::vector<MenuItem> Database::getMenuItems() {
    std::vector<MenuItem> list;
    const char* sql =
        "SELECT id, name, category, price, description, available "
        "FROM menu_items ORDER BY category, name;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return list;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MenuItem m;
        m.id          = sqlite3_column_int(stmt, 0);
        m.name        = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        m.category    = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        m.price       = sqlite3_column_double(stmt, 3);
        auto* desc    = sqlite3_column_text(stmt, 4);
        m.description = desc ? reinterpret_cast<const char*>(desc) : "";
        m.available   = sqlite3_column_int(stmt, 5) != 0;
        list.push_back(m);
    }
    sqlite3_finalize(stmt);
    return list;
}

std::vector<MenuItem> Database::getMenuItemsByCategory(const std::string& cat) {
    std::vector<MenuItem> list;
    char sql[512];
    snprintf(sql, sizeof(sql),
        "SELECT id, name, category, price, description, available "
        "FROM menu_items WHERE category='%s' AND available=1 ORDER BY name;",
        escape(cat).c_str());
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return list;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MenuItem m;
        m.id          = sqlite3_column_int(stmt, 0);
        m.name        = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        m.category    = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        m.price       = sqlite3_column_double(stmt, 3);
        auto* desc    = sqlite3_column_text(stmt, 4);
        m.description = desc ? reinterpret_cast<const char*>(desc) : "";
        m.available   = sqlite3_column_int(stmt, 5) != 0;
        list.push_back(m);
    }
    sqlite3_finalize(stmt);
    return list;
}

std::vector<std::string> Database::getMenuCategories() {
    std::vector<std::string> cats;
    const char* sql =
        "SELECT DISTINCT category FROM menu_items ORDER BY category;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return cats;
    while (sqlite3_step(stmt) == SQLITE_ROW)
        cats.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    sqlite3_finalize(stmt);
    return cats;
}

// ── Sessions ──────────────────────────────────────────────────────────────

int Database::saveSession(const SessionRecord& r) {
    char sql[1024];
    snprintf(sql, sizeof(sql),
        "INSERT INTO sessions (station_name, username, package_name, "
        "start_time, end_time, duration_sec, total_cost) "
        "VALUES ('%s', '%s', '%s', %ld, %ld, %d, %.2f);",
        escape(r.station_name).c_str(), escape(r.username).c_str(),
        escape(r.package_name).c_str(),
        (long)r.start_time, (long)r.end_time,
        r.duration_sec, r.total_cost);
    if (!exec(sql)) return -1;
    return (int)sqlite3_last_insert_rowid(db_);
}

std::vector<SessionRecord> Database::getSessions(int limit) {
    std::vector<SessionRecord> list;
    char sql[256];
    snprintf(sql, sizeof(sql),
        "SELECT id, station_name, username, package_name, "
        "start_time, end_time, duration_sec, total_cost "
        "FROM sessions ORDER BY id DESC LIMIT %d;", limit);
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return list;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        SessionRecord r;
        r.id           = sqlite3_column_int(stmt, 0);
        r.station_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        r.username     = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        r.package_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        r.start_time   = (time_t)sqlite3_column_int64(stmt, 4);
        r.end_time     = (time_t)sqlite3_column_int64(stmt, 5);
        r.duration_sec = sqlite3_column_int(stmt, 6);
        r.total_cost   = sqlite3_column_double(stmt, 7);
        list.push_back(r);
    }
    sqlite3_finalize(stmt);
    return list;
}

std::vector<SessionRecord> Database::getSessionsToday() {
    std::vector<SessionRecord> list;
    const char* sql =
        "SELECT id, station_name, username, package_name, "
        "start_time, end_time, duration_sec, total_cost "
        "FROM sessions "
        "WHERE date(start_time,'unixepoch','localtime') = date('now','localtime') "
        "ORDER BY id DESC;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return list;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        SessionRecord r;
        r.id           = sqlite3_column_int(stmt, 0);
        r.station_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        r.username     = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        r.package_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        r.start_time   = (time_t)sqlite3_column_int64(stmt, 4);
        r.end_time     = (time_t)sqlite3_column_int64(stmt, 5);
        r.duration_sec = sqlite3_column_int(stmt, 6);
        r.total_cost   = sqlite3_column_double(stmt, 7);
        list.push_back(r);
    }
    sqlite3_finalize(stmt);
    return list;
}

double Database::getTodayRevenue() {
    const char* sql =
        "SELECT COALESCE(SUM(total_cost),0) FROM sessions "
        "WHERE date(start_time,'unixepoch','localtime') = date('now','localtime');";
    sqlite3_stmt* stmt;
    double total = 0.0;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW)
            total = sqlite3_column_double(stmt, 0);
        sqlite3_finalize(stmt);
    }
    return total;
}

int Database::getSessionCountToday() {
    const char* sql =
        "SELECT COUNT(*) FROM sessions "
        "WHERE date(start_time,'unixepoch','localtime') = date('now','localtime');";
    sqlite3_stmt* stmt;
    int count = 0;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW)
            count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    return count;
}

double Database::getAvgSessionCostToday() {
    const char* sql =
        "SELECT COALESCE(AVG(total_cost),0) FROM sessions "
        "WHERE date(start_time,'unixepoch','localtime') = date('now','localtime');";
    sqlite3_stmt* stmt;
    double avg = 0.0;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW)
            avg = sqlite3_column_double(stmt, 0);
        sqlite3_finalize(stmt);
    }
    return avg;
}

// ── Settings ─────────────────────────────────────────────────────────────

CafeSetting Database::loadSettings() {
    CafeSetting s;
    const char* sql = "SELECT key, value FROM settings;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return s;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string key = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        std::string val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (key == "cafe_name")       s.cafe_name      = val;
        if (key == "cafe_address")    s.cafe_address   = val;
        if (key == "cafe_phone")      s.cafe_phone     = val;
        if (key == "wifi_password")   s.wifi_password  = val;
        if (key == "server_ip")       s.server_ip      = val;
        if (key == "server_port")     s.server_port    = std::stoi(val);
        if (key == "default_rate")    s.default_rate   = std::stod(val);
        if (key == "rate_per_minute") s.rate_per_minute= std::stod(val);
    }
    sqlite3_finalize(stmt);
    return s;
}

bool Database::saveSettings(const CafeSetting& s) {
    char sql[2048];
    snprintf(sql, sizeof(sql),
        "INSERT OR REPLACE INTO settings (key, value) VALUES "
        "('cafe_name','%s'),"
        "('cafe_address','%s'),"
        "('cafe_phone','%s'),"
        "('wifi_password','%s'),"
        "('server_ip','%s'),"
        "('server_port','%d'),"
        "('default_rate','%.2f'),"
        "('rate_per_minute','%.2f');",
        escape(s.cafe_name).c_str(),
        escape(s.cafe_address).c_str(),
        escape(s.cafe_phone).c_str(),
        escape(s.wifi_password).c_str(),
        escape(s.server_ip).c_str(),
        s.server_port,
        s.default_rate,
        s.rate_per_minute);
    return exec(sql);
}
