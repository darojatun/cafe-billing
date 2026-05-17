# CaféBill — Internet Cafe Billing System

A complete GTK3/C++ internet cafe billing system similar to GBilling.

## Features

- **Admin server**: Manage stations, start/stop sessions, view revenue
- **Client app**: Timer display, cost tracking, locked workstation screen
- **Real-time billing**: Supports fixed-time packages and open/hourly billing
- **SQLite database**: Persistent revenue records and session history
- **Live chat**: Admin ↔ client chat over TCP
- **Package management**: Create custom billing packages
- **Broadcast messages**: Send messages to all connected clients

## Screenshots

### Admin Dashboard & Server Overview
![Admin Server Dashboard](capture/Cuplikan%20layar%20dari%202026-05-17%2003-47-51.png)

### Client Connection & Authentication
<p align="center">
  <img src="capture/Cuplikan layar dari 2026-05-17 03-47-58.png" alt="Client Connection Screen" width="60%"/>
</p>

### Station Management
| Client Connection Pending | Active Stations Hub |
|:---:|:---:|
| ![Client Connected - Dashboard View](capture/Cuplikan%20layar%20dari%202026-05-18%2004-51-31.png) | ![Stations Control Panel](capture/Cuplikan%20layar%20dari%202026-05-18%2004-51-54.png) |

### Session Lifecycle
| 1. Workstation Locked | 2. Start Session Trigger |
|:---:|:---:|
| ![Workstation Locked](capture/Cuplikan%20layar%20dari%202026-05-18%2004-51-25.png) | ![Start Session Dialog](capture/Cuplikan%20layar%20dari%202026-05-18%2004-52-57.png) |

| 3. Client Active Timer | 4. Server Active Station Monitor |
|:---:|:---:|
| ![Active Client Screen](capture/Cuplikan%20layar%20dari%202026-05-18%2004-53-40.png) | ![Active Station Server View](capture/Cuplikan%20layar%20dari%202026-05-18%2004-53-50.png) |

### Additional Modules
| Food & Drinks POS / Offer | Chat with Admin System |
|:---:|:---:|
| ![Food & Drinks Menu](capture/Cuplikan%20layar%20dari%202026-05-18%2004-52-09.png) | ![Client Admin Chat](capture/Cuplikan%20layar%20dari%202026-05-18%2004-53-36.png) |

---

## Architecture


```

cafe-server  ←──── TCP (port 12345) ────→  cafe-client
│                                         │
SQLite                                   GTK3 UI
(.cafebill.db)                         (fullscreen timer)

```

## Build

### Requirements

Install via nix or manual

```

gtk3, sqlite, cmake, gcc, pkg-config

```

### Compile

```bash
bash cafe-billing/build.sh

```

Binaries will be in `cafe-billing/build/`.

## Usage

### 1. Start the Admin Server

```bash
./cafe-billing/build/cafe-server

```

The admin window opens. Server listens on port **12345** by default (configurable in the Settings tab).

### 2. Start Client(s)

```bash
./cafe-billing/build/cafe-client

```

Enter the server IP and port, then click **Connect**.
The client workstation will be locked until the admin starts a session.

### Admin Features

| Tab | Description |
| --- | --- |
| **Stations** | See all connected PCs. Start/Stop sessions, Lock, Chat |
| **Revenue** | Session history with duration and cost |
| **Packages** | Add/remove billing packages |
| **Settings** | Cafe name, server port, default rate |

### Billing Packages

* **Fixed time** — Customer pays a flat price for a set duration
e.g. "1 Hour → Rp 3,000"
* **Open/Hourly** — Timer counts up, cost = elapsed hours × rate
e.g. "Open → Rp 3,000/hr"

## Protocol

All messages are newline-terminated pipe-delimited strings:

```
TYPE|key1|val1|key2|val2\n

```

Key message types:

| Type | Direction | Description |
| --- | --- | --- |
| `REGISTER` | Client→Server | Client identifies itself |
| `REGISTERED` | Server→Client | Registration acknowledged |
| `SESSION_START` | Server→Client | Session begins |
| `TICK` | Server→Client | Timer update (every second) |
| `SESSION_END` | Server→Client | Session terminated |
| `CHAT_CLIENT` | Client→Server | Chat message from client |
| `CHAT_SERVER` | Server→Client | Chat message from admin |
| `LOCK` | Server→Client | Lock/unlock workstation |
| `SERVER_MSG` | Server→Client | Broadcast/info message |
| `PACKAGES` | Server→Client | Package list |

## Database Schema

```sql
packages  (id, name, is_timed, duration_sec, price)
sessions  (id, station_name, username, package_name,
           start_time, end_time, duration_sec, total_cost)
settings  (key, value)

```

Database is stored at `~/.cafebill.db`.

---

Made with ❤️ by Fajar Julyana @ QWare, Inc.

Copyright [MIT LICENSE](LICENSE)
