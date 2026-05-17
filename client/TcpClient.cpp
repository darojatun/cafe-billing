#include "TcpClient.h"
#include "ClientWindow.h"
#include "../common/protocol.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <cstring>

// ── GLib callbacks ────────────────────────────────────────────────────────

static gboolean cb_data(GIOChannel* ch, GIOCondition cond, gpointer user_data) {
    auto* client = reinterpret_cast<TcpClient*>(user_data);

    if (cond & (G_IO_HUP | G_IO_ERR | G_IO_NVAL)) {
        client->onDisconnect();
        return G_SOURCE_REMOVE;
    }

    gchar*  line = nullptr;
    gsize   len  = 0;
    GError* err  = nullptr;
    GIOStatus st = g_io_channel_read_line(ch, &line, &len, nullptr, &err);

    if (st == G_IO_STATUS_EOF || st == G_IO_STATUS_ERROR) {
        if (err) g_error_free(err);
        client->onDisconnect();
        return G_SOURCE_REMOVE;
    }

    if (st == G_IO_STATUS_NORMAL && line) {
        std::string s(line, len);
        g_free(line);
        client->onData(s);
    }
    return G_SOURCE_CONTINUE;
}

static gboolean cb_heartbeat(gpointer user_data) {
    auto* client = reinterpret_cast<TcpClient*>(user_data);
    if (client->isConnected())
        client->send(Protocol::encode(Protocol::HEARTBEAT, {}));
    return G_SOURCE_CONTINUE;
}

// ── TcpClient ─────────────────────────────────────────────────────────────

TcpClient::TcpClient(ClientWindow* window) : window_(window) {}

TcpClient::~TcpClient() { disconnect(); }

bool TcpClient::connect(const std::string& host, int port) {
    sock_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd_ < 0) { perror("socket"); return false; }

    // Resolve host
    struct hostent* he = gethostbyname(host.c_str());
    if (!he) {
        fprintf(stderr, "Cannot resolve host: %s\n", host.c_str());
        close(sock_fd_); sock_fd_ = -1;
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);

    if (::connect(sock_fd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sock_fd_); sock_fd_ = -1;
        return false;
    }

    // Non-blocking after connect
    fcntl(sock_fd_, F_SETFL, O_NONBLOCK);

    channel_ = g_io_channel_unix_new(sock_fd_);
    g_io_channel_set_encoding(channel_, nullptr, nullptr);
    g_io_channel_set_buffered(channel_, TRUE);
    g_io_channel_set_line_term(channel_, "\n", 1);

    g_io_add_watch(channel_,
        (GIOCondition)(G_IO_IN | G_IO_HUP | G_IO_ERR),
        cb_data, this);

    // Send REGISTER
    char hostname[256] = "Client";
    gethostname(hostname, sizeof(hostname));
    send(Protocol::encode(Protocol::REGISTER, {
        {"hostname", hostname},
        {"username", ""}
    }));

    // Heartbeat every 30s
    hb_timer_ = g_timeout_add_seconds(30, cb_heartbeat, this);

    printf("[client] Connected to %s:%d\n", host.c_str(), port);
    return true;
}

void TcpClient::disconnect() {
    if (hb_timer_) { g_source_remove(hb_timer_); hb_timer_ = 0; }
    if (channel_) {
        g_io_channel_shutdown(channel_, FALSE, nullptr);
        g_io_channel_unref(channel_);
        channel_ = nullptr;
    }
    if (sock_fd_ >= 0) { close(sock_fd_); sock_fd_ = -1; }
}

void TcpClient::send(const std::string& msg) {
    if (!channel_ || sock_fd_ < 0) return;
    gsize written = 0;
    GError* err = nullptr;
    g_io_channel_write_chars(channel_, msg.c_str(), msg.size(), &written, &err);
    g_io_channel_flush(channel_, nullptr);
    if (err) g_error_free(err);
}

void TcpClient::onData(const std::string& line) {
    auto msg = Protocol::decode(line);
    if (!msg.valid) return;
    if (window_) window_->handleMessage(msg);
}

void TcpClient::onDisconnect() {
    disconnect();
    if (window_) window_->onServerDisconnected();
}
