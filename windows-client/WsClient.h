#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include "Protocol.h"

// Windows message IDs posted from the socket thread to the main window
#define WM_SOCKET_MSG   (WM_USER + 100)  // wParam = msg type hash, lParam = heap Protocol::Message*
#define WM_SOCKET_DISC  (WM_USER + 101)  // server disconnected

struct SocketMsg {
    Protocol::Message msg;
};

class WsClient {
public:
    WsClient();
    ~WsClient();

    bool connect(const std::string& host, int port, HWND notify_hwnd);
    void disconnect();
    bool isConnected() const { return sock_ != INVALID_SOCKET; }

    void send(const std::string& data);

private:
    static DWORD WINAPI recvThread(LPVOID param);

    SOCKET  sock_        = INVALID_SOCKET;
    HWND    hwnd_        = nullptr;
    HANDLE  thread_      = nullptr;
    bool    running_     = false;
};
