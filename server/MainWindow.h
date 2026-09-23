#pragma once
#include <gtk/gtk.h>
#include <string>
#include <map>
#include "BillingServer.h"
#include "Database.h"

class MainWindow {
public:
    MainWindow(BillingServer* server, Database* db);
    ~MainWindow() = default;

    void setServer(BillingServer* s) { server_ = s; }
    void show();

    // Called by BillingServer
    void addClientStation(ClientInfo* c);
    void removeClientStation(int client_id);
    void updateClientStation(ClientInfo* c);
    void appendChatMessage(int client_id, const std::string& from, const std::string& text);
    void reloadRevenue();
    void refreshRevenueLabel();
    void showReceiptDialog(const std::string& html_path, ClientInfo* c);

    // Public so free-function dialogs can call them
    void reloadPackages();
    void reloadMenuItems();
    void reloadDashboard();

private:
    // Tab builders
    GtkWidget* buildStationsTab();
    GtkWidget* buildDashboardTab();
    GtkWidget* buildRevenueTab();
    GtkWidget* buildMenuTab();
    GtkWidget* buildPackagesTab();
    GtkWidget* buildSettingsTab();

    // Station card helpers
    GtkWidget* buildStationCard(ClientInfo* c);
    void       updateStationCard(ClientInfo* c);

    // Dialog helpers
    void openStartSessionDialog(ClientInfo* c);
    void openChatDialog(ClientInfo* c);
    void openMenuOfferDialog(int client_id);

    // ── Static GTK callbacks ──────────────────────────────────────────────
    static void onStartClicked(GtkWidget*, gpointer);
    static void onStopClicked(GtkWidget*, gpointer);
    static void onLockClicked(GtkWidget*, gpointer);
    static void onChatClicked(GtkWidget*, gpointer);

    static void onAddPkgClicked(GtkWidget*, gpointer);
    static void onEditPkgClicked(GtkWidget*, gpointer);
    static void onDelPkgClicked(GtkWidget*, gpointer);

    static void onAddMenuClicked(GtkWidget*, gpointer);
    static void onEditMenuClicked(GtkWidget*, gpointer);
    static void onDelMenuClicked(GtkWidget*, gpointer);
    static void onSendMenuOfferClicked(GtkWidget*, gpointer);
    static void onBroadcastMenuClicked(GtkWidget*, gpointer);

    static void onSaveSettingsClicked(GtkWidget*, gpointer);
    static void onBroadcastClicked(GtkWidget*, gpointer);
    static void onRefreshDashClicked(GtkWidget*, gpointer);
    static void onRefreshRevClicked(GtkWidget*, gpointer);

    // ── Data ─────────────────────────────────────────────────────────────
    BillingServer* server_   = nullptr;
    Database*      db_       = nullptr;

    // Main window
    GtkWidget*    window_       = nullptr;
    GtkWidget*    flow_box_     = nullptr;
    GtkWidget*    lbl_revenue_  = nullptr;
    GtkWidget*    lbl_status_   = nullptr;
    GtkWidget*    lbl_clients_  = nullptr;

    // Dashboard widgets
    GtkWidget*    dash_active_   = nullptr;
    GtkWidget*    dash_revenue_  = nullptr;
    GtkWidget*    dash_sessions_ = nullptr;
    GtkWidget*    dash_avg_      = nullptr;
    GtkListStore* dash_store_    = nullptr;

    // Revenue tab
    GtkListStore* rev_store_    = nullptr;

    // Packages tab
    GtkListStore* pkg_store_    = nullptr;
    GtkTreeView*  pkg_tv_       = nullptr;

    // Menu tab
    GtkListStore* menu_store_   = nullptr;
    GtkTreeView*  menu_tv_      = nullptr;

    // Settings entries
    GtkWidget* ent_cafe_name_   = nullptr;
    GtkWidget* ent_address_     = nullptr;
    GtkWidget* ent_phone_       = nullptr;
    GtkWidget* ent_server_ip_   = nullptr;
    GtkWidget* ent_port_        = nullptr;
    GtkWidget* ent_rate_        = nullptr;
    GtkWidget* ent_rate_min_    = nullptr;
    GtkWidget* ent_qris_type_   = nullptr;
    GtkWidget* ent_qris_merchant_ = nullptr;
    GtkWidget* ent_qris_nmid_   = nullptr;
    GtkWidget* ent_qris_payload_ = nullptr;

    // Chat: client_id → GtkTextBuffer
    std::map<int, GtkTextBuffer*> chat_buffers_;
    std::map<int, GtkWidget*>     chat_windows_;
};

// Utility helpers (used in BillingServer.cpp too)
std::string fmtTime(int sec);
std::string fmtCost(double cost);
