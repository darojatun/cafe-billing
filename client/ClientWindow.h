#pragma once
#include <gtk/gtk.h>
#include <string>
#include <vector>
#include "../common/protocol.h"

struct PkgInfo { int id; std::string name; int duration; int price; };

class TcpClient;

class ClientWindow {
public:
    ClientWindow();
    ~ClientWindow() = default;

    void show();

    // Called by TcpClient
    void handleMessage(const Protocol::Message& msg);
    void onServerDisconnected();

private:
    void buildConnectScreen();
    void buildMainScreen();
    void showMainScreen();
    void showConnectScreen();

    void onSessionStart(const Protocol::Message& msg);
    void onSessionEnd(const Protocol::Message& msg);
    void onTick(const Protocol::Message& msg);
    void onChatReceived(const std::string& from, const std::string& text);
    void onLock(bool locked);
    void onServerMsg(const std::string& text);

    void openChatDialog();
    void updateTimerDisplay();
    void applyLockOverlay(bool locked);
    void grabKeys(bool grab);

    // callbacks
    static void onConnectClicked(GtkWidget*, gpointer);
    static void onSendChat(GtkWidget*, gpointer);
    static void onChatEntryActivate(GtkWidget*, gpointer);

    TcpClient*  client_   = nullptr;

    // window
    GtkWidget*  window_   = nullptr;
    GtkWidget*  stack_    = nullptr;        // GtkStack: connect / main

    // Connect screen
    GtkWidget*  ent_host_ = nullptr;
    GtkWidget*  ent_port_ = nullptr;
    GtkWidget*  lbl_conn_status_ = nullptr;

    // Main screen
    GtkWidget*  lbl_station_    = nullptr;
    GtkWidget*  lbl_timer_      = nullptr;
    GtkWidget*  lbl_remaining_  = nullptr;
    GtkWidget*  lbl_cost_       = nullptr;
    GtkWidget*  lbl_user_       = nullptr;
    GtkWidget*  lbl_package_    = nullptr;
    GtkWidget*  lbl_status_     = nullptr;
    GtkWidget*  overlay_lock_   = nullptr;  // fullscreen lock overlay
    GtkWidget*  btn_chat_       = nullptr;
    GtkWidget*  progress_bar_   = nullptr;

    // Chat
    GtkWidget*     chat_win_    = nullptr;
    GtkTextBuffer* chat_buf_    = nullptr;
    GtkWidget*     chat_entry_  = nullptr;

    // Session state
    bool        session_active_ = false;
    bool        locked_         = true;
    bool        kb_grabbed_     = false;
    int         elapsed_sec_    = 0;
    int         remaining_sec_  = 0;
    int         duration_sec_   = 0;
    double      current_cost_   = 0.0;
    std::string package_name_;
    std::string username_;

    std::vector<PkgInfo> packages_;
};
