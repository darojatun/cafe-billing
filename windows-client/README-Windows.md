# CaféBill Windows Client

A native Win32 desktop application that connects to the CaféBill Linux server.

## Features

- Native Win32 API (no external dependencies)
- Winsock2 for TCP communication
- Same billing protocol as the Linux GTK3 client
- Lock screen overlay
- Real-time timer display
- Chat with admin dialog
- Session summary on disconnect

## Build on Windows

### Option A — MSVC (Visual Studio)

1. Install [Visual Studio 2019/2022](https://visualstudio.microsoft.com/) (Community edition is free)
2. Open a **Developer Command Prompt**
3. Navigate to this folder and run:

```bat
build-native.bat
```

### Option B — MinGW-w64

1. Install [MinGW-w64](https://www.mingw-w64.org/) (MSYS2 recommended)
2. Open MSYS2 MINGW64 terminal
3. Navigate to this folder and run:

```bat
build-native.bat
```

Or manually:

```bash
g++ -std=c++17 -O2 -mwindows \
    -DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0601 \
    main.cpp WsClient.cpp \
    -lws2_32 -lcomctl32 \
    -static-libgcc -static-libstdc++ \
    -o cafe-client.exe
```

## Cross-compile from Linux (optional)

Install MinGW-w64 on your Linux machine:

```bash
# Debian/Ubuntu
sudo apt-get install mingw-w64

# Arch
sudo pacman -S mingw-w64-gcc

# Fedora
sudo dnf install mingw64-gcc-c++
```

Then run from the `cafe-billing/` directory:

```bash
bash windows-client/build-windows.sh
```

Output: `windows-client/build-win/cafe-client.exe`

## Deployment

1. Copy `cafe-client.exe` to each Windows workstation
2. Run `cafe-client.exe`
3. Enter the server's IP address and port (default: `12345`)
4. Click **Connect to Server**

The workstation will be locked until the admin starts a billing session.

## Requirements

- Windows 7 or later (x64 or x86)
- Network access to the CaféBill server
- No additional runtime libraries needed (fully static build)

## Source files

| File | Description |
|------|-------------|
| `main.cpp` | WinMain entry point, all Win32 UI, message handling |
| `WsClient.h/.cpp` | Winsock2 TCP client with recv thread |
| `Protocol.h` | Pipe-delimited protocol (shared with Linux) |
| `CMakeLists.txt` | CMake build config for cross-compilation |
| `build-native.bat` | Windows native build script (MSVC + MinGW) |
| `build-windows.sh` | Linux cross-compilation script |
