#include "BillingServer.h"
#include "MainWindow.h"
#include "../common/protocol.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <sstream>

// ── GLib callbacks ────────────────────────────────────────────────────────

static gboolean cb_accept(GIOChannel*, GIOCondition, gpointer user_data) {
    reinterpret_cast<BillingServer*>(user_data)->onAcceptReady();
    return G_SOURCE_CONTINUE;
}

static gboolean cb_client_data(GIOChannel* ch, GIOCondition cond, gpointer user_data) {
    // user_data encodes [server ptr, client_id] via a small heap struct
    struct Ctx { BillingServer* srv; int cid; };
    auto* ctx = reinterpret_cast<Ctx*>(user_data);

    if (cond & (G_IO_HUP | G_IO_ERR | G_IO_NVAL)) {
        ctx->srv->onClientDisconnect(ctx->cid);
        delete ctx;
        return G_SOURCE_REMOVE;
    }

    gchar*  line = nullptr;
    gsize   len  = 0;
    GError* err  = nullptr;
    GIOStatus st = g_io_channel_read_line(ch, &line, &len, nullptr, &err);

    if (st == G_IO_STATUS_EOF || st == G_IO_STATUS_ERROR) {
        if (err) g_error_free(err);
        ctx->srv->onClientDisconnect(ctx->cid);
        delete ctx;
        return G_SOURCE_REMOVE;
    }

    if (st == G_IO_STATUS_NORMAL && line) {
        std::string s(line, len);
        g_free(line);
        ctx->srv->onClientData(ctx->cid, s);
    }
    return G_SOURCE_CONTINUE;
}

static gboolean cb_tick(gpointer user_data) {
    reinterpret_cast<BillingServer*>(user_data)->onTick();
    return G_SOURCE_CONTINUE;
}

// ── BillingServer ─────────────────────────────────────────────────────────

BillingServer::BillingServer(Database* db, MainWindow* window)
    : db_(db), window_(window) {
    if (db_) settings_ = db_->loadSettings();
}

BillingServer::~BillingServer() { stop(); }

bool BillingServer::start(int port) {
    port_ = port;

    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) { perror("socket"); return false; }

    int yes = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    // Non-blocking
    fcntl(server_fd_, F_SETFL, O_NONBLOCK);

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port_);

    if (bind(server_fd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(server_fd_); server_fd_ = -1; return false;
    }
    if (listen(server_fd_, 10) < 0) {
        perror("listen"); close(server_fd_); server_fd_ = -1; return false;
    }

    // Watch server socket for incoming connections via GIOChannel
    GIOChannel* srv_ch = g_io_channel_unix_new(server_fd_);
    g_io_channel_set_encoding(srv_ch, nullptr, nullptr);
    g_io_add_watch(srv_ch, G_IO_IN, cb_accept, this);
    g_io_channel_unref(srv_ch);
    timer_id_ = g_timeout_add_seconds(1, cb_tick, this);

    printf("[server] Listening on port %d\n", port_);
    return true;
}

void BillingServer::stop() {
    if (timer_id_) { g_source_remove(timer_id_); timer_id_ = 0; }

    for (auto& [id, c] : clients_) {
        if (c->channel) g_io_channel_shutdown(c->channel, FALSE, nullptr);
        if (c->sock_fd >= 0) close(c->sock_fd);
        delete c;
    }
    clients_.clear();

    if (server_fd_ >= 0) { close(server_fd_); server_fd_ = -1; }
}

// ── Accept ────────────────────────────────────────────────────────────────

void BillingServer::onAcceptReady() {
    sockaddr_in peer{};
    socklen_t   len = sizeof(peer);
    int fd = accept(server_fd_, (sockaddr*)&peer, &len);
    if (fd < 0) return;

    fcntl(fd, F_SETFL, O_NONBLOCK);

    int cid = next_id_++;
    auto* c = new ClientInfo;
    c->client_id = cid;
    c->sock_fd   = fd;
    c->hostname  = inet_ntoa(peer.sin_addr);

    GIOChannel* ch = g_io_channel_unix_new(fd);
    g_io_channel_set_encoding(ch, nullptr, nullptr);
    g_io_channel_set_buffered(ch, TRUE);
    g_io_channel_set_line_term(ch, "\n", 1);
    c->channel = ch;

    struct Ctx { BillingServer* srv; int cid; };
    auto* ctx = new Ctx{this, cid};
    g_io_add_watch(ch, (GIOCondition)(G_IO_IN | G_IO_HUP | G_IO_ERR),
                   cb_client_data, ctx);

    clients_[cid] = c;

    if (window_) window_->addClientStation(c);

    printf("[server] Client %d connected from %s\n", cid, c->hostname.c_str());
}

// ── Client data ───────────────────────────────────────────────────────────

void BillingServer::onClientData(int client_id, const std::string& line) {
    auto* c = getClient(client_id);
    if (!c) return;

    auto msg = Protocol::decode(line);
    if (!msg.valid) return;

    if (msg.type == Protocol::REGISTER) {
        c->hostname = msg.fields.count("hostname") ? msg.fields.at("hostname") : c->hostname;
        c->username = msg.fields.count("username") ? msg.fields.at("username") : "";

        // Send registered ack
        sendRaw(client_id,
            Protocol::encode(Protocol::REGISTERED,
                {{"client_id", std::to_string(client_id)}}));

        // Send current package list
        sendPackageList(client_id);

        if (window_) window_->updateClientStation(c);
    }
    else if (msg.type == Protocol::CHAT_CLIENT) {
        std::string text = msg.fields.count("text") ? msg.fields.at("text") : "";
        if (window_) window_->appendChatMessage(client_id, c->hostname, text);
    }
    else if (msg.type == Protocol::HEARTBEAT) {
        sendRaw(client_id, Protocol::encode("PONG", {}));
    }
}

// ── Client disconnect ─────────────────────────────────────────────────────

void BillingServer::onClientDisconnect(int client_id) {
    auto* c = getClient(client_id);
    if (!c) return;

    printf("[server] Client %d disconnected (%s)\n", client_id, c->hostname.c_str());

    if (c->session_active) {
        // Save session to DB on disconnect
        SessionRecord r;
        r.station_name = c->hostname;
        r.username     = c->username;
        r.package_name = c->package.name;
        r.start_time   = c->session_start;
        r.end_time     = time(nullptr);
        r.duration_sec = c->elapsed_sec;
        r.total_cost   = c->current_cost;
        db_->saveSession(r);
    }

    if (c->channel) { g_io_channel_unref(c->channel); c->channel = nullptr; }
    if (c->sock_fd >= 0) { close(c->sock_fd); c->sock_fd = -1; }

    if (window_) window_->removeClientStation(client_id);

    clients_.erase(client_id);
    delete c;
}

// ── Tick (every second) ───────────────────────────────────────────────────

void BillingServer::onTick() {
    for (auto& [cid, c] : clients_) {
        if (!c->session_active) continue;

        c->elapsed_sec++;
        c->current_cost = calcCost(*c);

        // Check time expiry for timed packages
        bool expired = c->package.is_timed &&
                       c->package.duration_sec > 0 &&
                       c->elapsed_sec >= c->package.duration_sec;

        int remaining = c->package.is_timed
            ? std::max(0, c->package.duration_sec - c->elapsed_sec)
            : 0;

        // Send TICK to client
        sendRaw(cid, Protocol::encode(Protocol::TICK, {
            {"elapsed",   std::to_string(c->elapsed_sec)},
            {"remaining", std::to_string(remaining)},
            {"cost",      std::to_string((int)std::round(c->current_cost))}
        }));

        // Update station widget
        if (window_) window_->updateClientStation(c);

        if (expired) {
            // endSession sends SESSION_END and generates receipt automatically
            endSession(cid);
        }
    }

    if (window_) window_->refreshRevenueLabel();
}

// ── Session control ───────────────────────────────────────────────────────

void BillingServer::startSession(int client_id, const Package& pkg,
                                  const std::string& username) {
    auto* c = getClient(client_id);
    if (!c) return;

    c->session_active = true;
    c->package        = pkg;
    c->username       = username;
    c->session_start  = time(nullptr);
    c->elapsed_sec    = 0;
    c->current_cost   = 0.0;
    c->locked         = false;

    sendRaw(client_id, Protocol::encode(Protocol::SESSION_START, {
        {"package",  pkg.name},
        {"duration", std::to_string(pkg.duration_sec)},
        {"rate",     std::to_string((int)pkg.price)},
        {"username", username}
    }));

    // Unlock client
    sendRaw(client_id, Protocol::encode(Protocol::LOCK, {{"locked","0"}}));

    if (window_) window_->updateClientStation(c);
}

std::string BillingServer::endSession(int client_id) {
    auto* c = getClient(client_id);
    if (!c || !c->session_active) return {};

    SessionRecord r;
    r.station_name = c->hostname;
    r.username     = c->username;
    r.package_name = c->package.name;
    r.start_time   = c->session_start;
    r.end_time     = time(nullptr);
    r.duration_sec = c->elapsed_sec;
    r.total_cost   = c->current_cost;
    db_->saveSession(r);

    // Generate receipt
    ReceiptData rd;
    rd.cafe_name    = settings_.cafe_name;
    rd.station_name = r.station_name;
    rd.username     = r.username;
    rd.package_name = r.package_name;
    rd.start_time   = r.start_time;
    rd.end_time     = r.end_time;
    rd.duration_sec = r.duration_sec;
    rd.total_cost   = r.total_cost;
    std::string receipt_html = Receipt::save(rd);

    sendRaw(client_id, Protocol::encode(Protocol::SESSION_END, {
        {"elapsed", std::to_string(c->elapsed_sec)},
        {"cost",    std::to_string((int)std::round(c->current_cost))}
    }));
    sendRaw(client_id, Protocol::encode(Protocol::LOCK, {{"locked","1"}}));

    c->session_active = false;
    c->elapsed_sec    = 0;
    c->current_cost   = 0.0;
    c->locked         = true;

    if (window_) {
        window_->updateClientStation(c);
        window_->reloadRevenue();
    }

    return receipt_html;
}

void BillingServer::lockClient(int client_id, bool lock) {
    auto* c = getClient(client_id);
    if (!c) return;
    c->locked = lock;
    sendRaw(client_id, Protocol::encode(Protocol::LOCK,
        {{"locked", lock ? "1" : "0"}}));
    if (window_) window_->updateClientStation(c);
}

// ── Messaging ─────────────────────────────────────────────────────────────

void BillingServer::sendChat(int client_id, const std::string& text) {
    sendRaw(client_id, Protocol::encode(Protocol::CHAT_SERVER,
        {{"from","Admin"},{"text",text}}));
}

void BillingServer::sendServerMsg(int client_id, const std::string& text) {
    sendRaw(client_id, Protocol::encode(Protocol::SERVER_MSG, {{"text",text}}));
}

void BillingServer::broadcastMsg(const std::string& text) {
    for (auto& [cid, _] : clients_)
        sendServerMsg(cid, text);
}

void BillingServer::sendMenuOffer(int client_id, const MenuItem& item) {
    char buf[512];
    snprintf(buf, sizeof(buf),
        "[MENU OFFER] %s — %s\nPrice: Rp %.0f\n%s",
        item.category.c_str(), item.name.c_str(),
        item.price,
        item.description.empty() ? "" : item.description.c_str());
    sendRaw(client_id, Protocol::encode(Protocol::MENU_OFFER, {
        {"name",     item.name},
        {"category", item.category},
        {"price",    std::to_string((int)item.price)},
        {"desc",     item.description},
        {"text",     buf}
    }));
}

void BillingServer::broadcastMenuOffer(const MenuItem& item) {
    for (auto& [cid, _] : clients_)
        sendMenuOffer(cid, item);
}

void BillingServer::reloadSettings() {
    if (db_) settings_ = db_->loadSettings();
}

// ── Helpers ───────────────────────────────────────────────────────────────

void BillingServer::sendRaw(int client_id, const std::string& msg) {
    auto* c = getClient(client_id);
    if (!c || c->sock_fd < 0) return;

    // Use GIOChannel to write safely
    if (c->channel) {
        gsize written = 0;
        GError* err = nullptr;
        g_io_channel_write_chars(c->channel, msg.c_str(), msg.size(), &written, &err);
        g_io_channel_flush(c->channel, nullptr);
        if (err) g_error_free(err);
    }
}

void BillingServer::sendPackageList(int client_id) {
    auto pkgs = db_->getPackages();
    // Encode: id:name:duration:price  separated by ';'
    std::ostringstream oss;
    for (size_t i = 0; i < pkgs.size(); i++) {
        if (i) oss << ';';
        oss << pkgs[i].id << ':' << pkgs[i].name << ':'
            << pkgs[i].duration_sec << ':' << (int)pkgs[i].price;
    }
    sendRaw(client_id, Protocol::encode(Protocol::PACKAGES, {{"data", oss.str()}}));
}

double BillingServer::calcCost(const ClientInfo& c) const {
    if (c.package.is_timed) {
        return c.package.price;   // fixed price
    }
    // Hourly rate
    return (c.elapsed_sec / 3600.0) * c.package.price;
}

ClientInfo* BillingServer::getClient(int id) {
    auto it = clients_.find(id);
    return it != clients_.end() ? it->second : nullptr;
}

std::vector<Package> BillingServer::getPackages() {
    return db_->getPackages();
}
