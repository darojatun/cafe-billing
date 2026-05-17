#pragma once
#include <string>
#include <map>
#include <vector>
#include <functional>
#include <ctime>
#include <gtk/gtk.h>
#include "Database.h"
#include "Receipt.h"

// ── Per-client state ──────────────────────────────────────────────────────
struct ClientInfo {
    int         client_id   = 0;
    int         sock_fd     = -1;
    GIOChannel* channel     = nullptr;

    std::string hostname;
    std::string username;

    // billing
    bool        session_active = false;
    Package     package;
    time_t      session_start  = 0;
    int         elapsed_sec    = 0;
    double      current_cost   = 0.0;
    bool        locked         = false;

    // gtk widgets (owned by the station grid)
    GtkWidget*  card_widget    = nullptr;
    GtkWidget*  lbl_user       = nullptr;
    GtkWidget*  lbl_timer      = nullptr;
    GtkWidget*  lbl_cost       = nullptr;
    GtkWidget*  lbl_status     = nullptr;
    GtkWidget*  lbl_package    = nullptr;
    GtkWidget*  btn_start      = nullptr;
    GtkWidget*  btn_stop       = nullptr;
    GtkWidget*  btn_lock       = nullptr;
    GtkWidget*  btn_chat       = nullptr;
};

class MainWindow;

// ── BillingServer ─────────────────────────────────────────────────────────
class BillingServer {
public:
    BillingServer(Database* db, MainWindow* window);
    ~BillingServer();

    bool  start(int port);
    void  stop();

    bool  isRunning() const { return server_fd_ >= 0; }
    int   port()      const { return port_; }

    // Session control
    void        startSession(int client_id, const Package& pkg, const std::string& username);
    std::string endSession(int client_id);   // returns html receipt path

    void  lockClient(int client_id, bool lock);
    void  reloadSettings();   // reload from DB (after admin saves)

    // Messaging
    void  sendChat(int client_id, const std::string& text);
    void  sendServerMsg(int client_id, const std::string& text);
    void  broadcastMsg(const std::string& text);
    void  sendMenuOffer(int client_id, const MenuItem& item);
    void  broadcastMenuOffer(const MenuItem& item);

    // Accessors
    ClientInfo*              getClient(int id);
    std::map<int, ClientInfo*>& clients() { return clients_; }
    std::vector<Package>     getPackages();

    // Called by GLib callbacks
    void  onAcceptReady();
    void  onClientData(int client_id, const std::string& line);
    void  onClientDisconnect(int client_id);
    void  onTick();

private:
    void  sendRaw(int client_id, const std::string& msg);
    void  sendPackageList(int client_id);
    double calcCost(const ClientInfo& c) const;

    CafeSetting              settings_;
    Database*                db_        = nullptr;
    MainWindow*              window_    = nullptr;
    int                      server_fd_ = -1;
    int                      port_      = 12345;
    int                      next_id_   = 1;
    guint                    timer_id_  = 0;
    std::map<int, ClientInfo*> clients_;
};
