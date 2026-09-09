#include "ClientWindow.h"
#include "TcpClient.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cmath>
#include <algorithm>
#include <sstream>

// ── ClientWindow ──────────────────────────────────────────────────────────

ClientWindow::ClientWindow() {
    client_   = new TcpClient(this);
    chat_buf_ = gtk_text_buffer_new(nullptr);
}

void ClientWindow::show() {
    window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window_), "CaféBill — Client");
    gtk_window_set_default_size(GTK_WINDOW(window_), 800, 600);
    gtk_window_set_position(GTK_WINDOW(window_), GTK_WIN_POS_CENTER);
    gtk_window_set_decorated(GTK_WINDOW(window_), TRUE);
    // Block Alt+F4 / close-button while a session is locked so the
    // workstation cannot be left without staff action.
    g_signal_connect(window_, "delete-event",
        G_CALLBACK(+[](GtkWidget*, GdkEvent*, gpointer d) -> gboolean {
            auto* self = reinterpret_cast<ClientWindow*>(d);
            const char* page = gtk_stack_get_visible_child_name(GTK_STACK(self->stack_));
            bool on_main = page && strcmp(page, "main") == 0;
            return (self->locked_ && on_main) ? TRUE : FALSE;
        }), this);
    g_signal_connect(window_, "destroy", G_CALLBACK(gtk_main_quit), nullptr);

    // CSS
    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css, R"CSS(
        .connect-box {
            padding: 40px;
        }
        .timer-huge {
            font-size: 72px;
            font-family: monospace;
            font-weight: bold;
            color: #2ecc71;
        }
        .timer-label {
            font-size: 20px;
            font-family: monospace;
            color: #ecf0f1;
        }
        .cost-big {
            font-size: 32px;
            font-weight: bold;
            color: #e74c3c;
        }
        .info-label {
            font-size: 14px;
            color: #bdc3c7;
        }
        .lock-overlay {
            background: rgba(20,20,20,0.92);
        }
        .lock-text {
            font-size: 36px;
            font-weight: bold;
            color: white;
        }
        .dark-bg {
            background: #1a1a2e;
        }
        .session-inactive .timer-huge {
            color: #7f8c8d;
        }
        .chat-btn {
            font-size: 16px;
            padding: 10px 24px;
        }
        .status-bar {
            font-size: 12px;
            color: #7f8c8d;
            padding: 4px 8px;
        }
    )CSS", -1, nullptr);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    // Stack: "connect" | "main"
    stack_ = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack_), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(stack_), 300);

    buildConnectScreen();
    buildMainScreen();

    gtk_container_add(GTK_CONTAINER(window_), stack_);
    gtk_widget_show_all(window_);

    showConnectScreen();
    gtk_main();
}

// ── Connect Screen ────────────────────────────────────────────────────────

void ClientWindow::buildConnectScreen() {
    GtkWidget* outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_valign(outer, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(outer, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(outer, 360, -1);
    gtk_style_context_add_class(gtk_widget_get_style_context(outer), "connect-box");

    GtkWidget* lbl_title = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(lbl_title),
        "<span font='24' weight='bold'>☕ CaféBill</span>");
    gtk_widget_set_margin_bottom(lbl_title, 24);

    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);

    ent_host_ = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(ent_host_), "127.0.0.1");
    gtk_entry_set_placeholder_text(GTK_ENTRY(ent_host_), "Server IP address");
    gtk_widget_set_hexpand(ent_host_, TRUE);

    ent_port_ = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(ent_port_), "12345");
    gtk_entry_set_placeholder_text(GTK_ENTRY(ent_port_), "Port");

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Server:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), ent_host_,                1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Port:"),   0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), ent_port_,                1, 1, 1, 1);

    GtkWidget* btn = gtk_button_new_with_label("Connect to Server");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn), "suggested-action");
    gtk_widget_set_margin_top(btn, 16);
    g_signal_connect(btn, "clicked", G_CALLBACK(onConnectClicked), this);
    g_signal_connect(ent_port_, "activate", G_CALLBACK(onConnectClicked), this);

    lbl_conn_status_ = gtk_label_new("");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_conn_status_), "status-bar");
    gtk_widget_set_margin_top(lbl_conn_status_, 8);

    gtk_box_pack_start(GTK_BOX(outer), lbl_title,        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), grid,              FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), btn,               FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), lbl_conn_status_,  FALSE, FALSE, 0);

    gtk_stack_add_named(GTK_STACK(stack_), outer, "connect");
}

// ── Main Screen ───────────────────────────────────────────────────────────

void ClientWindow::buildMainScreen() {
    // Overlay for lock screen
    GtkWidget* overlay = gtk_overlay_new();

    // ── Main content ──────────────────────────────────────────────────────
    GtkWidget* main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(main_box), "dark-bg");

    // Top bar
    GtkWidget* topbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(topbar, 20);
    gtk_widget_set_margin_end(topbar, 20);
    gtk_widget_set_margin_top(topbar, 12);
    gtk_widget_set_margin_bottom(topbar, 12);

    lbl_station_ = gtk_label_new("Station: —");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_station_), "info-label");
    lbl_status_ = gtk_label_new("● Idle");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_status_), "info-label");

    gtk_box_pack_start(GTK_BOX(topbar), lbl_station_, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(topbar), lbl_status_,   FALSE, FALSE, 0);

    // Centre: timer + cost
    GtkWidget* center = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_valign(center, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(center, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(center, TRUE);

    // Big elapsed/remaining timer
    lbl_timer_ = gtk_label_new("00:00:00");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_timer_), "timer-huge");

    // Sub-label: "Elapsed" or "Remaining"
    lbl_remaining_ = gtk_label_new("Waiting for session...");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_remaining_), "timer-label");

    // Progress bar (for timed packages)
    progress_bar_ = gtk_progress_bar_new();
    gtk_widget_set_size_request(progress_bar_, 400, -1);
    gtk_widget_set_margin_top(progress_bar_, 8);

    // Cost
    lbl_cost_ = gtk_label_new("Rp 0");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_cost_), "cost-big");
    gtk_widget_set_margin_top(lbl_cost_, 16);

    // Info row
    GtkWidget* info_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
    gtk_widget_set_halign(info_row, GTK_ALIGN_CENTER);
    lbl_user_    = gtk_label_new("User: —");
    lbl_package_ = gtk_label_new("Package: —");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_user_),    "info-label");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_package_), "info-label");
    gtk_box_pack_start(GTK_BOX(info_row), lbl_user_,    FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(info_row), lbl_package_, FALSE, FALSE, 0);

    // Chat button
    btn_chat_ = gtk_button_new_with_label("💬  Chat with Admin");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_chat_), "chat-btn");
    gtk_widget_set_margin_top(btn_chat_, 20);
    gtk_widget_set_halign(btn_chat_, GTK_ALIGN_CENTER);
    g_signal_connect(btn_chat_, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) {
        reinterpret_cast<ClientWindow*>(d)->openChatDialog();
    }), this);

    gtk_box_pack_start(GTK_BOX(center), lbl_timer_,     FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(center), lbl_remaining_,  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(center), progress_bar_,   FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(center), lbl_cost_,       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(center), info_row,        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(center), btn_chat_,       FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(main_box), topbar,  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(main_box),
        gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(main_box), center, TRUE, TRUE, 0);

    // ── Lock overlay ──────────────────────────────────────────────────────
    overlay_lock_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_style_context_add_class(gtk_widget_get_style_context(overlay_lock_), "lock-overlay");
    gtk_widget_set_vexpand(overlay_lock_, TRUE);
    gtk_widget_set_hexpand(overlay_lock_, TRUE);
    gtk_widget_set_valign(overlay_lock_, GTK_ALIGN_FILL);
    gtk_widget_set_halign(overlay_lock_, GTK_ALIGN_FILL);

    GtkWidget* lbl_lock1 = gtk_label_new("🔒");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_lock1), "lock-text");
    GtkWidget* lbl_lock2 = gtk_label_new("Workstation Locked");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_lock2), "lock-text");
    GtkWidget* lbl_lock3 = gtk_label_new("Please contact staff to start a session");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_lock3), "info-label");

    gtk_widget_set_valign(overlay_lock_, GTK_ALIGN_FILL);
    gtk_widget_set_halign(overlay_lock_, GTK_ALIGN_FILL);

    GtkWidget* lock_inner = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_valign(lock_inner, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(lock_inner, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(lock_inner, TRUE);
    gtk_box_pack_start(GTK_BOX(lock_inner), lbl_lock1, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(lock_inner), lbl_lock2, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(lock_inner), lbl_lock3, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(overlay_lock_), lock_inner, TRUE, TRUE, 0);

    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), overlay_lock_);
    gtk_container_add(GTK_CONTAINER(overlay), main_box);

    gtk_stack_add_named(GTK_STACK(stack_), overlay, "main");
}

// ── Screen transitions ────────────────────────────────────────────────────

void ClientWindow::showConnectScreen() {
    gtk_stack_set_visible_child_name(GTK_STACK(stack_), "connect");
}

void ClientWindow::showMainScreen() {
    gtk_stack_set_visible_child_name(GTK_STACK(stack_), "main");
    // Start locked until SESSION_START
    applyLockOverlay(true);
}

// ── Message handling ──────────────────────────────────────────────────────

void ClientWindow::handleMessage(const Protocol::Message& msg) {
    if (msg.type == Protocol::REGISTERED) {
        char hostname[256] = "Client";
        gethostname(hostname, sizeof(hostname));
        gtk_label_set_text(GTK_LABEL(lbl_station_),
            (std::string("Station: ") + hostname).c_str());
        showMainScreen();
        gtk_label_set_text(GTK_LABEL(lbl_status_), "● Connected — waiting for session");
    }
    else if (msg.type == Protocol::SESSION_START) {
        onSessionStart(msg);
    }
    else if (msg.type == Protocol::SESSION_END) {
        onSessionEnd(msg);
    }
    else if (msg.type == Protocol::TICK) {
        onTick(msg);
    }
    else if (msg.type == Protocol::CHAT_SERVER) {
        std::string from = msg.fields.count("from") ? msg.fields.at("from") : "Admin";
        std::string text = msg.fields.count("text") ? msg.fields.at("text") : "";
        onChatReceived(from, text);
    }
    else if (msg.type == Protocol::LOCK) {
        bool locked = msg.fields.count("locked") && msg.fields.at("locked") == "1";
        onLock(locked);
    }
    else if (msg.type == Protocol::SERVER_MSG) {
        std::string text = msg.fields.count("text") ? msg.fields.at("text") : "";
        onServerMsg(text);
    }
    else if (msg.type == Protocol::PACKAGES) {
        // Parse "1:1 Hour:3600:5000;2:Open:0:3000"
        packages_.clear();
        if (msg.fields.count("data")) {
            std::istringstream ss(msg.fields.at("data"));
            std::string entry;
            while (std::getline(ss, entry, ';')) {
                if (entry.empty()) continue;
                std::istringstream es(entry);
                std::string tok;
                PkgInfo p;
                int col = 0;
                while (std::getline(es, tok, ':')) {
                    if (col == 0) p.id = std::stoi(tok);
                    else if (col == 1) p.name = tok;
                    else if (col == 2) p.duration = std::stoi(tok);
                    else if (col == 3) p.price = std::stoi(tok);
                    col++;
                }
                if (col >= 4) packages_.push_back(p);
            }
        }
    }
}

void ClientWindow::onSessionStart(const Protocol::Message& msg) {
    session_active_ = true;
    locked_         = false;
    elapsed_sec_    = 0;

    package_name_ = msg.fields.count("package")  ? msg.fields.at("package")  : "";
    username_     = msg.fields.count("username")  ? msg.fields.at("username") : "";
    duration_sec_ = msg.fields.count("duration")  ? std::stoi(msg.fields.at("duration")) : 0;

    char pkg_buf[128];
    snprintf(pkg_buf, sizeof(pkg_buf), "Package: %s", package_name_.c_str());
    gtk_label_set_text(GTK_LABEL(lbl_package_), pkg_buf);

    char user_buf[128];
    snprintf(user_buf, sizeof(user_buf), "User: %s",
        username_.empty() ? "—" : username_.c_str());
    gtk_label_set_text(GTK_LABEL(lbl_user_), user_buf);

    if (duration_sec_ > 0) {
        gtk_label_set_text(GTK_LABEL(lbl_remaining_), "Time Remaining");
        gtk_widget_set_visible(progress_bar_, TRUE);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar_), 1.0);
    } else {
        gtk_label_set_text(GTK_LABEL(lbl_remaining_), "Time Elapsed");
        gtk_widget_set_visible(progress_bar_, FALSE);
    }

    gtk_label_set_text(GTK_LABEL(lbl_status_), "● Session Active");
    gtk_label_set_text(GTK_LABEL(lbl_timer_), "00:00:00");
    gtk_label_set_text(GTK_LABEL(lbl_cost_), "Rp 0");

    applyLockOverlay(false);
}

void ClientWindow::onSessionEnd(const Protocol::Message& msg) {
    session_active_ = false;
    locked_         = true;

    double cost = msg.fields.count("cost") ? std::stod(msg.fields.at("cost")) : 0;
    int elapsed  = msg.fields.count("elapsed") ? std::stoi(msg.fields.at("elapsed")) : 0;

    gtk_label_set_text(GTK_LABEL(lbl_status_), "● Session Ended — please pay");
    gtk_label_set_text(GTK_LABEL(lbl_remaining_), "Session finished");

    // Lock and cover the workstation before showing the session summary.
    applyLockOverlay(true);

    // Show summary dialog
    char sum[256];
    snprintf(sum, sizeof(sum),
        "Session Ended\n\nDuration: %02d:%02d:%02d\nTotal Cost: Rp %.0f\n\nThank you!",
        elapsed/3600, (elapsed%3600)/60, elapsed%60, cost);

    GtkWidget* dlg = gtk_message_dialog_new(
        GTK_WINDOW(window_), GTK_DIALOG_MODAL,
        GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", sum);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);

}

void ClientWindow::onTick(const Protocol::Message& msg) {
    elapsed_sec_   = msg.fields.count("elapsed")   ? std::stoi(msg.fields.at("elapsed"))   : 0;
    remaining_sec_ = msg.fields.count("remaining") ? std::stoi(msg.fields.at("remaining")) : 0;
    current_cost_  = msg.fields.count("cost")      ? std::stod(msg.fields.at("cost"))      : 0;

    // Display time
    if (duration_sec_ > 0) {
        // Fixed-time packages show elapsed time while the bar shows time left.
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
            elapsed_sec_/3600, (elapsed_sec_%3600)/60, elapsed_sec_%60);
        gtk_label_set_text(GTK_LABEL(lbl_timer_), buf);

        // The remaining-time bar starts full and decreases to zero.
        double frac = duration_sec_ > 0
            ? (double)remaining_sec_ / duration_sec_ : 0;
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar_),
            std::max(0.0, std::min(1.0, frac)));
    } else {
        // Hourly: show elapsed
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
            elapsed_sec_/3600, (elapsed_sec_%3600)/60, elapsed_sec_%60);
        gtk_label_set_text(GTK_LABEL(lbl_timer_), buf);
    }

    // Cost
    char cbuf[32];
    snprintf(cbuf, sizeof(cbuf), "Rp %.0f", current_cost_);
    gtk_label_set_text(GTK_LABEL(lbl_cost_), cbuf);
}

void ClientWindow::onChatReceived(const std::string& from, const std::string& text) {
    // Open chat dialog if not open
    openChatDialog();

    GtkTextIter end;
    gtk_text_buffer_get_end_iter(chat_buf_, &end);

    time_t now = time(nullptr);
    struct tm* tm = localtime(&now);
    char ts[16]; strftime(ts, sizeof(ts), "%H:%M:%S", tm);

    std::string line = "[" + std::string(ts) + "] " + from + ": " + text + "\n";
    gtk_text_buffer_insert(chat_buf_, &end, line.c_str(), -1);

    // Flash the chat button
    gtk_button_set_label(GTK_BUTTON(btn_chat_), "💬  New message!");
    g_timeout_add_seconds(3, [](gpointer d) -> gboolean {
        gtk_button_set_label(GTK_BUTTON(d), "💬  Chat with Admin");
        return G_SOURCE_REMOVE;
    }, btn_chat_);
}

void ClientWindow::onLock(bool locked) {
    locked_ = locked;
    applyLockOverlay(locked);
    if (!locked)
        gtk_label_set_text(GTK_LABEL(lbl_status_), "● Unlocked");
}

void ClientWindow::onServerMsg(const std::string& text) {
    GtkWidget* dlg = gtk_message_dialog_new(
        GTK_WINDOW(window_), GTK_DIALOG_MODAL,
        GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
        "Message from Admin:\n\n%s", text.c_str());
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

void ClientWindow::applyLockOverlay(bool locked) {
    if (overlay_lock_)
        gtk_widget_set_visible(overlay_lock_, locked);

    if (!window_) return;

    if (locked) {
        // Keep the workstation covered while it is waiting for staff action.
        gtk_window_set_keep_above(GTK_WINDOW(window_), TRUE);
        gtk_window_fullscreen(GTK_WINDOW(window_));
        gtk_window_present(GTK_WINDOW(window_));
    } else {
        gtk_window_set_keep_above(GTK_WINDOW(window_), FALSE);
        gtk_window_unfullscreen(GTK_WINDOW(window_));
        gtk_window_set_default_size(GTK_WINDOW(window_), 800, 600);
        gtk_window_present(GTK_WINDOW(window_));
    }
}

void ClientWindow::onServerDisconnected() {
    gtk_label_set_text(GTK_LABEL(lbl_conn_status_), "Disconnected from server");
    showConnectScreen();
}

// ── Chat dialog ───────────────────────────────────────────────────────────

void ClientWindow::openChatDialog() {
    if (chat_win_ && GTK_IS_WIDGET(chat_win_)) {
        gtk_window_present(GTK_WINDOW(chat_win_));
        return;
    }

    chat_win_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(chat_win_), "Chat with Admin");
    gtk_window_set_default_size(GTK_WINDOW(chat_win_), 400, 480);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(vbox, 10);
    gtk_widget_set_margin_end(vbox, 10);
    gtk_widget_set_margin_top(vbox, 10);
    gtk_widget_set_margin_bottom(vbox, 10);

    GtkWidget* tv = gtk_text_view_new_with_buffer(chat_buf_);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tv), GTK_WRAP_WORD_CHAR);
    GtkWidget* sw = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_container_add(GTK_CONTAINER(sw), tv);

    GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    chat_entry_ = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(chat_entry_), "Type message...");
    gtk_widget_set_hexpand(chat_entry_, TRUE);

    GtkWidget* btn = gtk_button_new_with_label("Send");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn), "suggested-action");
    g_signal_connect(btn,         "clicked",  G_CALLBACK(onSendChat),           this);
    g_signal_connect(chat_entry_, "activate", G_CALLBACK(onChatEntryActivate),  this);

    gtk_box_pack_start(GTK_BOX(hbox), chat_entry_, TRUE,  TRUE,  0);
    gtk_box_pack_start(GTK_BOX(hbox), btn,         FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), sw,   TRUE,  TRUE,  0);
    gtk_box_pack_end(GTK_BOX(vbox),   hbox, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(chat_win_), vbox);

    g_signal_connect(chat_win_, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer d) {
        reinterpret_cast<ClientWindow*>(d)->chat_win_ = nullptr;
        reinterpret_cast<ClientWindow*>(d)->chat_entry_ = nullptr;
    }), this);

    gtk_widget_show_all(chat_win_);
}

// ── Static callbacks ──────────────────────────────────────────────────────

void ClientWindow::onConnectClicked(GtkWidget*, gpointer data) {
    auto* self = reinterpret_cast<ClientWindow*>(data);
    const char* host = gtk_entry_get_text(GTK_ENTRY(self->ent_host_));
    const char* port_s = gtk_entry_get_text(GTK_ENTRY(self->ent_port_));
    int port = port_s[0] ? std::stoi(port_s) : 12345;

    gtk_label_set_text(GTK_LABEL(self->lbl_conn_status_), "Connecting...");
    gtk_widget_queue_draw(self->window_);

    if (!self->client_->connect(host, port)) {
        gtk_label_set_text(GTK_LABEL(self->lbl_conn_status_),
            "Failed to connect. Check host/port and try again.");
    }
}

void ClientWindow::onSendChat(GtkWidget*, gpointer data) {
    auto* self = reinterpret_cast<ClientWindow*>(data);
    if (!self->chat_entry_) return;
    const char* text = gtk_entry_get_text(GTK_ENTRY(self->chat_entry_));
    if (!text || !text[0]) return;

    // Append to buffer
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(self->chat_buf_, &end);
    time_t now = time(nullptr);
    struct tm* tm = localtime(&now);
    char ts[16]; strftime(ts, sizeof(ts), "%H:%M:%S", tm);
    std::string line = std::string("[") + ts + "] Me: " + text + "\n";
    gtk_text_buffer_insert(self->chat_buf_, &end, line.c_str(), -1);

    // Send to server
    self->client_->send(Protocol::encode(Protocol::CHAT_CLIENT, {{"text", text}}));
    gtk_entry_set_text(GTK_ENTRY(self->chat_entry_), "");
}

void ClientWindow::onChatEntryActivate(GtkWidget* w, gpointer data) {
    onSendChat(w, data);
}
