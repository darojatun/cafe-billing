#pragma once
#include <string>
#include <vector>
#include <ctime>
#include <sqlite3.h>

struct Package {
    int    id            = 0;
    std::string name;
    bool   is_timed      = false;
    int    duration_sec  = 0;
    double price         = 0.0;
};

struct MenuItem {
    int    id            = 0;
    std::string name;
    std::string category;      // e.g. "Food", "Drink", "Snack"
    double price         = 0.0;
    std::string description;
    bool   available     = true;
};

struct SessionRecord {
    int    id            = 0;
    std::string station_name;
    std::string username;
    std::string package_name;
    time_t start_time    = 0;
    time_t end_time      = 0;
    int    duration_sec  = 0;
    double total_cost    = 0.0;
};

struct CafeSetting {
    std::string cafe_name      = "My Internet Cafe";
    std::string cafe_address   = "";
    std::string cafe_phone     = "";
    std::string wifi_password  = "";
    std::string server_ip      = "0.0.0.0";   // bind address (0.0.0.0 = all)
    int    server_port         = 12345;
    double default_rate        = 3000.0;       // Rp per hour (open/hourly packages)
    double rate_per_minute     = 50.0;         // Rp per minute (alternative display)
};

class Database {
public:
    explicit Database(const std::string& path);
    ~Database();

    bool init();

    // Packages
    bool                    addPackage(const Package& p);
    bool                    updatePackage(const Package& p);
    bool                    deletePackage(int id);
    std::vector<Package>    getPackages();

    // Menu items (F&B)
    bool                    addMenuItem(const MenuItem& m);
    bool                    updateMenuItem(const MenuItem& m);
    bool                    deleteMenuItem(int id);
    std::vector<MenuItem>   getMenuItems();
    std::vector<MenuItem>   getMenuItemsByCategory(const std::string& cat);
    std::vector<std::string> getMenuCategories();

    // Sessions
    int                          saveSession(const SessionRecord& r);
    std::vector<SessionRecord>   getSessions(int limit = 200);
    std::vector<SessionRecord>   getSessionsToday();
    double                       getTodayRevenue();
    int                          getSessionCountToday();
    double                       getAvgSessionCostToday();

    // Settings
    CafeSetting loadSettings();
    bool        saveSettings(const CafeSetting& s);

private:
    sqlite3*    db_   = nullptr;
    std::string path_;

    bool exec(const std::string& sql);
    std::string escape(const std::string& s);
};
