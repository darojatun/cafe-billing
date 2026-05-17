#pragma once
#include <gtk/gtk.h>
#include <string>
#include <functional>

class ClientWindow;

class TcpClient {
public:
    explicit TcpClient(ClientWindow* window);
    ~TcpClient();

    bool connect(const std::string& host, int port);
    void disconnect();
    bool isConnected() const { return sock_fd_ >= 0; }

    void send(const std::string& msg);

    // Called by GLib IO watch
    void onData(const std::string& line);
    void onDisconnect();

private:
    ClientWindow* window_   = nullptr;
    int           sock_fd_  = -1;
    GIOChannel*   channel_  = nullptr;
    guint         hb_timer_ = 0;
};
