#include "WsClient.h"
#include <cstdio>
#include <cstring>

WsClient::WsClient() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
}

WsClient::~WsClient() {
    disconnect();
    WSACleanup();
}

bool WsClient::connect(const std::string& host, int port, HWND hwnd) {
    hwnd_ = hwnd;

    sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock_ == INVALID_SOCKET) return false;

    // Resolve host
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host.c_str(), port_str, &hints, &res) != 0 || !res) {
        closesocket(sock_); sock_ = INVALID_SOCKET;
        return false;
    }

    if (::connect(sock_, res->ai_addr, (int)res->ai_addrlen) == SOCKET_ERROR) {
        freeaddrinfo(res);
        closesocket(sock_); sock_ = INVALID_SOCKET;
        return false;
    }
    freeaddrinfo(res);

// Send REGISTER
char hostname[256] = "WinClient";
DWORD sz = sizeof(hostname);

// Panggil sekali saja dengan benar. Jika gagal, "WinClient" tetap menjadi default.
if (!GetComputerNameA(hostname, &sz)) {
    snprintf(hostname, sizeof(hostname), "WinClient");
}

std::string reg = Protocol::encode(Protocol::REGISTER,
    {{"hostname", hostname}, {"username", ""}});
::send(sock_, reg.c_str(), (int)reg.size(), 0);

    // Start recv thread
    running_ = true;
    thread_ = CreateThread(nullptr, 0, recvThread, this, 0, nullptr);
    return true;
}

void WsClient::disconnect() {
    running_ = false;
    if (sock_ != INVALID_SOCKET) {
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
    }
    if (thread_) {
        WaitForSingleObject(thread_, 2000);
        CloseHandle(thread_);
        thread_ = nullptr;
    }
}

void WsClient::send(const std::string& data) {
    if (sock_ != INVALID_SOCKET)
        ::send(sock_, data.c_str(), (int)data.size(), 0);
}

DWORD WINAPI WsClient::recvThread(LPVOID param) {
    auto* self = reinterpret_cast<WsClient*>(param);
    std::string buf;

    char chunk[4096];
    while (self->running_) {
        int n = recv(self->sock_, chunk, sizeof(chunk) - 1, 0);
        if (n <= 0) break;
        chunk[n] = '\0';
        buf += chunk;

        // Process complete lines
        size_t pos;
        while ((pos = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, pos + 1);
            buf.erase(0, pos + 1);

            Protocol::Message msg = Protocol::decode(line);
            if (msg.valid && self->hwnd_) {
                // Heap-allocate the message and post to main thread
                auto* pm = new SocketMsg{std::move(msg)};
                PostMessage(self->hwnd_, WM_SOCKET_MSG, 0, (LPARAM)pm);
            }
        }
    }

    if (self->hwnd_ && self->running_)
        PostMessage(self->hwnd_, WM_SOCKET_DISC, 0, 0);

    return 0;
}
