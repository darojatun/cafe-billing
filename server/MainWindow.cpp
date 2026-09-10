#include "MainWindow.h"
#include "BillingServer.h"
#include "Database.h"
#include "Receipt.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <algorithm>

// ── Helpers ───────────────────────────────────────────────────────────────

std::string fmtTime(int sec) {
    if (sec < 0) sec = 0;
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", sec/3600, (sec%3600)/60, sec%60);
    return buf;
}

std::string fmtCost(double cost) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Rp %.0f", cost);
    return std::string(buf);
}

static std::string fmtDatetime(time_t t) {
    char buf[32];
    struct tm* tm = localtime(&t);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", tm);
    return buf;
}

static std::string fmtTimeShort(time_t t) {
    char buf[16];
    struct tm* tm = localtime(&t);
    strftime(buf, sizeof(buf), "%H:%M:%S", tm);
    return buf;
}

// ── Callback data structs ─────────────────────────────────────────────────

struct BtnData {
    MainWindow*    win;
    BillingServer* srv;
    int            client_id;
};

struct MenuData {
    MainWindow*    win;
    GtkTreeView*   tv;
};

struct PkgData {
    MainWindow*    win;
    GtkTreeView*   tv;
};

// ── MainWindow ────────────────────────────────────────────────────────────

MainWindow::MainWindow(BillingServer* server, Database* db)
    : server_(server), db_(db) {}

void MainWindow::show() {
    window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window_), "CaféBill — Admin Server");
    gtk_window_set_default_size(GTK_WINDOW(window_), 1200, 760);
    gtk_window_set_position(GTK_WINDOW(window_), GTK_WIN_POS_CENTER);
    g_signal_connect(window_, "destroy", G_CALLBACK(gtk_main_quit), nullptr);

    // ── Header bar ────────────────────────────────────────────────────────
    CafeSetting s = db_->loadSettings();
    GtkWidget* hbar = gtk_header_bar_new();
    gtk_header_bar_set_title(GTK_HEADER_BAR(hbar), "CaféBill");
    gtk_header_bar_set_subtitle(GTK_HEADER_BAR(hbar), s.cafe_name.c_str());
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(hbar), TRUE);

    lbl_revenue_ = gtk_label_new("Today: Rp 0");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_revenue_), "dim-label");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(hbar), lbl_revenue_);

    lbl_clients_ = gtk_label_new("0 clients");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(hbar), lbl_clients_);

    GtkWidget* btn_bcast = gtk_button_new_with_label("Broadcast");
    g_signal_connect(btn_bcast, "clicked", G_CALLBACK(onBroadcastClicked), this);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(hbar), btn_bcast);

    gtk_window_set_titlebar(GTK_WINDOW(window_), hbar);

    // ── Notebook ──────────────────────────────────────────────────────────
    GtkWidget* nb = gtk_notebook_new();
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(nb), GTK_POS_TOP);

    gtk_notebook_append_page(GTK_NOTEBOOK(nb), buildDashboardTab(),
        gtk_label_new("  Dashboard  "));
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), buildStationsTab(),
        gtk_label_new("  Stations  "));
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), buildRevenueTab(),
        gtk_label_new("  Revenue  "));
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), buildMenuTab(),
        gtk_label_new("  Food & Drinks  "));
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), buildPackagesTab(),
        gtk_label_new("  Packages  "));
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), buildSettingsTab(),
        gtk_label_new("  Settings  "));

    // Status bar
    lbl_status_ = gtk_statusbar_new();
    gtk_statusbar_push(GTK_STATUSBAR(lbl_status_), 0,
        (" Server running — Port: " + std::to_string(server_->port()) +
         "  |  " + s.cafe_name).c_str());

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(vbox), nb, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(vbox), lbl_status_, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(window_), vbox);

    // Apply CSS
    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css, R"CSS(
        .station-card {
            border-radius: 8px;
            border: 1px solid alpha(@theme_fg_color, 0.15);
            background: alpha(@theme_bg_color, 0.5);
            padding: 8px;
        }
        .station-card.busy {
            border-color: #3daee9;
            background: alpha(#3daee9, 0.05);
        }
        .station-card.free {
            border-color: #27ae60;
            background: alpha(#27ae60, 0.05);
        }
        .station-name   { font-size: 15px; font-weight: bold; }
        .status-free    { color: #27ae60; font-weight: bold; }
        .status-busy    { color: #3daee9; font-weight: bold; }
        .status-offline { color: #888888; }
        .timer-label    { font-size: 22px; font-family: monospace; font-weight: bold; }
        .cost-label     { font-size: 14px; color: #e74c3c; font-weight: bold; }
        .dash-card {
            border-radius: 8px;
            border: 1px solid alpha(@theme_fg_color, 0.12);
            background: alpha(@theme_bg_color, 0.7);
        }
        .dash-value {
            font-size: 26px;
            font-weight: bold;
        }
        .dash-title {
            font-size: 11px;
        }
        .section-title  { font-size: 13px; font-weight: bold; }
        .menu-badge {
            border-radius: 4px;
            background: alpha(#27ae60, 0.15);
            color: #27ae60;
            font-weight: bold;
            padding: 2px 6px;
        }
    )CSS", -1, nullptr);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    reloadDashboard();
    reloadRevenue();
    reloadPackages();
    reloadMenuItems();
    refreshRevenueLabel();

    gtk_widget_show_all(window_);
    gtk_main();
}

// ═══════════════════════════════════════════════════════════════════════════
// ── Dashboard Tab ─────────────────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════════════

static GtkWidget* makeDashCard(const char* title,
                                GtkWidget** val_lbl,
                                const char* init,
                                const char* color = nullptr)
{
    GtkWidget* frame = gtk_frame_new(nullptr);
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_NONE);
    gtk_style_context_add_class(gtk_widget_get_style_context(frame), "dash-card");
    gtk_widget_set_hexpand(frame, TRUE);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(box, 18);
    gtk_widget_set_margin_end(box, 18);
    gtk_widget_set_margin_top(box, 14);
    gtk_widget_set_margin_bottom(box, 14);

    GtkWidget* lbl_t = gtk_label_new(title);
    gtk_label_set_xalign(GTK_LABEL(lbl_t), 0.0f);
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_t), "dash-title");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_t), "dim-label");

    *val_lbl = gtk_label_new(init);
    gtk_label_set_xalign(GTK_LABEL(*val_lbl), 0.0f);
    gtk_style_context_add_class(gtk_widget_get_style_context(*val_lbl), "dash-value");
    if (color) {
        char markup[64];
        snprintf(markup, sizeof(markup), "<span color='%s'>%s</span>", color, init);
        gtk_label_set_markup(GTK_LABEL(*val_lbl), markup);
    }

    gtk_box_pack_start(GTK_BOX(box), lbl_t,    FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), *val_lbl, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(frame), box);
    return frame;
}

GtkWidget* MainWindow::buildDashboardTab() {
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    // ── Summary cards ─────────────────────────────────────────────────────
    GtkWidget* card_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(card_row, 16);
    gtk_widget_set_margin_end(card_row, 16);
    gtk_widget_set_margin_top(card_row, 16);
    gtk_widget_set_margin_bottom(card_row, 12);

    gtk_box_pack_start(GTK_BOX(card_row),
        makeDashCard("Active Clients",    &dash_active_,   "0",     "#3daee9"),
        TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(card_row),
        makeDashCard("Today's Revenue",   &dash_revenue_,  "Rp 0",  "#27ae60"),
        TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(card_row),
        makeDashCard("Sessions Today",    &dash_sessions_, "0",     nullptr),
        TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(card_row),
        makeDashCard("Avg Session Cost",  &dash_avg_,      "Rp 0",  nullptr),
        TRUE, TRUE, 0);

    // ── Today's session log ───────────────────────────────────────────────
    // Columns: Login Time, Logout Time, Station, User, Package, Duration, Cost
    dash_store_ = gtk_list_store_new(7,
        G_TYPE_STRING,  // login time
        G_TYPE_STRING,  // logout time
        G_TYPE_STRING,  // station
        G_TYPE_STRING,  // user
        G_TYPE_STRING,  // package
        G_TYPE_STRING,  // duration
        G_TYPE_STRING   // cost
    );

    GtkWidget* tv = gtk_tree_view_new_with_model(GTK_TREE_MODEL(dash_store_));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tv), TRUE);
    gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(tv), GTK_TREE_VIEW_GRID_LINES_HORIZONTAL);
    gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(tv), TRUE);

    const char* dcols[] = {"Login","Logout","Station","User","Package","Duration","Cost"};
    int dcol_widths[]   = {120, 120, 120, 100, 130, 90, 90};
    for (int i = 0; i < 7; i++) {
        GtkCellRenderer* r = gtk_cell_renderer_text_new();
        GtkTreeViewColumn* col = gtk_tree_view_column_new_with_attributes(
            dcols[i], r, "text", i, nullptr);
        gtk_tree_view_column_set_resizable(col, TRUE);
        gtk_tree_view_column_set_min_width(col, dcol_widths[i]);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tv), col);
    }

    GtkWidget* sw = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(sw), tv);

    // toolbar
    GtkWidget* toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(toolbar, 16);
    gtk_widget_set_margin_end(toolbar, 12);
    gtk_widget_set_margin_top(toolbar, 6);
    gtk_widget_set_margin_bottom(toolbar, 6);

    GtkWidget* lbl_log = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(lbl_log), "<b>Login / Logout Log — Today</b>");
    gtk_label_set_xalign(GTK_LABEL(lbl_log), 0.0f);

    GtkWidget* btn_refresh = gtk_button_new_with_label("⟳  Refresh");
    g_signal_connect(btn_refresh, "clicked", G_CALLBACK(onRefreshDashClicked), this);

    gtk_box_pack_start(GTK_BOX(toolbar), lbl_log,      TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(toolbar),   btn_refresh,  FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), card_row, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox),
        gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox),
        gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), sw, TRUE, TRUE, 0);

    return vbox;
}

void MainWindow::reloadDashboard() {
    if (!dash_store_) return;

    // Update summary cards
    int   active   = (int)server_->clients().size();
    double rev     = db_->getTodayRevenue();
    int   sessions = db_->getSessionCountToday();
    double avg     = db_->getAvgSessionCostToday();

    // Count active sessions
    int active_sessions = 0;
    for (auto& [cid, c] : server_->clients())
        if (c->session_active) active_sessions++;

    if (dash_active_) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d / %d", active_sessions, active);
        gtk_label_set_markup(GTK_LABEL(dash_active_),
            (std::string("<span color='#3daee9'>") + buf + "</span>").c_str());
    }
    if (dash_revenue_) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Rp %.0f", rev);
        gtk_label_set_markup(GTK_LABEL(dash_revenue_),
            (std::string("<span color='#27ae60'>") + buf + "</span>").c_str());
    }
    if (dash_sessions_) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", sessions);
        gtk_label_set_text(GTK_LABEL(dash_sessions_), buf);
    }
    if (dash_avg_) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Rp %.0f", avg);
        gtk_label_set_text(GTK_LABEL(dash_avg_), buf);
    }

    // Reload today's session log
    gtk_list_store_clear(dash_store_);
    auto recs = db_->getSessionsToday();
    for (auto& r : recs) {
        GtkTreeIter iter;
        gtk_list_store_append(dash_store_, &iter);
        gtk_list_store_set(dash_store_, &iter,
            0, fmtDatetime(r.start_time).c_str(),
            1, r.end_time > 0 ? fmtDatetime(r.end_time).c_str() : "—",
            2, r.station_name.c_str(),
            3, r.username.empty() ? "—" : r.username.c_str(),
            4, r.package_name.c_str(),
            5, fmtTime(r.duration_sec).c_str(),
            6, fmtCost(r.total_cost).c_str(),
            -1);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// ── Stations Tab ──────────────────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════════════

GtkWidget* MainWindow::buildStationsTab() {
    GtkWidget* sw = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    flow_box_ = gtk_flow_box_new();
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(flow_box_), 2);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow_box_), 6);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(flow_box_), 12);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(flow_box_), 12);
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow_box_), GTK_SELECTION_NONE);
    gtk_widget_set_margin_start(flow_box_, 16);
    gtk_widget_set_margin_end(flow_box_, 16);
    gtk_widget_set_margin_top(flow_box_, 16);
    gtk_widget_set_margin_bottom(flow_box_, 16);

    gtk_container_add(GTK_CONTAINER(sw), flow_box_);
    return sw;
}

GtkWidget* MainWindow::buildStationCard(ClientInfo* c) {
    GtkWidget* frame = gtk_frame_new(nullptr);
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_NONE);
    gtk_style_context_add_class(gtk_widget_get_style_context(frame), "station-card");
    gtk_style_context_add_class(gtk_widget_get_style_context(frame), "free");
    gtk_widget_set_size_request(frame, 210, -1);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(vbox, 10);
    gtk_widget_set_margin_end(vbox, 10);
    gtk_widget_set_margin_top(vbox, 10);
    gtk_widget_set_margin_bottom(vbox, 10);

    GtkWidget* lbl_name = gtk_label_new(c->hostname.c_str());
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_name), "station-name");
    gtk_label_set_xalign(GTK_LABEL(lbl_name), 0.0f);

    c->lbl_status = gtk_label_new("⬤ Online");
    gtk_style_context_add_class(gtk_widget_get_style_context(c->lbl_status), "status-free");
    gtk_label_set_xalign(GTK_LABEL(c->lbl_status), 0.0f);

    c->lbl_user = gtk_label_new("User: —");
    gtk_label_set_xalign(GTK_LABEL(c->lbl_user), 0.0f);

    c->lbl_package = gtk_label_new("Package: —");
    gtk_label_set_xalign(GTK_LABEL(c->lbl_package), 0.0f);

    c->lbl_timer = gtk_label_new("00:00:00");
    gtk_style_context_add_class(gtk_widget_get_style_context(c->lbl_timer), "timer-label");
    gtk_label_set_xalign(GTK_LABEL(c->lbl_timer), 0.5f);
    gtk_widget_set_margin_top(c->lbl_timer, 6);
    gtk_widget_set_margin_bottom(c->lbl_timer, 4);

    c->lbl_cost = gtk_label_new("Rp 0");
    gtk_style_context_add_class(gtk_widget_get_style_context(c->lbl_cost), "cost-label");
    gtk_label_set_xalign(GTK_LABEL(c->lbl_cost), 0.5f);

    gtk_box_pack_start(GTK_BOX(vbox), lbl_name,       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), c->lbl_status,  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), c->lbl_user,    FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), c->lbl_package, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox),
        gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(vbox), c->lbl_timer,   FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), c->lbl_cost,    FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox),
        gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 4);

    GtkWidget* hbtn = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_set_homogeneous(GTK_BOX(hbtn), TRUE);

    c->btn_start = gtk_button_new_with_label("Start");
    c->btn_stop  = gtk_button_new_with_label("Stop");
    c->btn_lock  = gtk_button_new_with_label("Lock");
    c->btn_chat  = gtk_button_new_with_label("Chat");

    gtk_style_context_add_class(gtk_widget_get_style_context(c->btn_start), "suggested-action");
    gtk_style_context_add_class(gtk_widget_get_style_context(c->btn_stop),  "destructive-action");

    auto* sd = new BtnData{this, server_, c->client_id};
    g_signal_connect(c->btn_start, "clicked", G_CALLBACK(onStartClicked), sd);
    g_signal_connect(c->btn_stop,  "clicked", G_CALLBACK(onStopClicked),  sd);
    g_signal_connect(c->btn_lock,  "clicked", G_CALLBACK(onLockClicked),  sd);
    g_signal_connect(c->btn_chat,  "clicked", G_CALLBACK(onChatClicked),  sd);

    gtk_box_pack_start(GTK_BOX(hbtn), c->btn_start, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbtn), c->btn_stop,  TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbtn), c->btn_lock,  TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbtn), c->btn_chat,  TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), hbtn, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(frame), vbox);
    c->card_widget = frame;
    return frame;
}

void MainWindow::updateStationCard(ClientInfo* c) {
    if (!c->card_widget) return;

    GtkStyleContext* ctx = gtk_widget_get_style_context(c->card_widget);
    gtk_style_context_remove_class(ctx, "busy");
    gtk_style_context_remove_class(ctx, "free");

    if (c->session_active) {
        gtk_style_context_add_class(ctx, "busy");
        gtk_label_set_text(GTK_LABEL(c->lbl_status), "⬤ Active");
        gtk_style_context_add_class(
            gtk_widget_get_style_context(c->lbl_status), "status-busy");
        gtk_style_context_remove_class(
            gtk_widget_get_style_context(c->lbl_status), "status-free");

        char user_buf[128];
        snprintf(user_buf, sizeof(user_buf), "User: %s",
            c->username.empty() ? "—" : c->username.c_str());
        gtk_label_set_text(GTK_LABEL(c->lbl_user), user_buf);

        char pkg_buf[128];
        snprintf(pkg_buf, sizeof(pkg_buf), "Pkg: %s", c->package.name.c_str());
        gtk_label_set_text(GTK_LABEL(c->lbl_package), pkg_buf);

        if (c->package.is_timed && c->package.duration_sec > 0) {
            int remaining = std::max(0, c->package.duration_sec - c->elapsed_sec);
            gtk_label_set_text(GTK_LABEL(c->lbl_timer), fmtTime(remaining).c_str());
        } else {
            gtk_label_set_text(GTK_LABEL(c->lbl_timer), fmtTime(c->elapsed_sec).c_str());
        }

        char cost_buf[64];
        snprintf(cost_buf, sizeof(cost_buf), "Rp %.0f", c->current_cost);
        gtk_label_set_text(GTK_LABEL(c->lbl_cost), cost_buf);

        gtk_widget_set_sensitive(c->btn_start, FALSE);
        gtk_widget_set_sensitive(c->btn_stop,  TRUE);
    } else {
        gtk_style_context_add_class(ctx, "free");
        gtk_label_set_text(GTK_LABEL(c->lbl_status), "⬤ Free");
        gtk_style_context_add_class(
            gtk_widget_get_style_context(c->lbl_status), "status-free");
        gtk_style_context_remove_class(
            gtk_widget_get_style_context(c->lbl_status), "status-busy");

        gtk_label_set_text(GTK_LABEL(c->lbl_user),    "User: —");
        gtk_label_set_text(GTK_LABEL(c->lbl_package), "Package: —");
        gtk_label_set_text(GTK_LABEL(c->lbl_timer),   "00:00:00");
        gtk_label_set_text(GTK_LABEL(c->lbl_cost),    "Rp 0");

        gtk_widget_set_sensitive(c->btn_start, TRUE);
        gtk_widget_set_sensitive(c->btn_stop,  FALSE);
    }

    gtk_button_set_label(GTK_BUTTON(c->btn_lock), c->locked ? "Unlock" : "Lock");

    char cc[32];
    snprintf(cc, sizeof(cc), "%zu client(s)", server_->clients().size());
    gtk_label_set_text(GTK_LABEL(lbl_clients_), cc);
}

// ═══════════════════════════════════════════════════════════════════════════
// ── Revenue Tab ───────────────────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════════════

GtkWidget* MainWindow::buildRevenueTab() {
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    rev_store_ = gtk_list_store_new(7,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);

    GtkWidget* tv = gtk_tree_view_new_with_model(GTK_TREE_MODEL(rev_store_));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tv), TRUE);
    gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(tv), GTK_TREE_VIEW_GRID_LINES_HORIZONTAL);
    gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(tv), TRUE);

    const char* cols[] = {"Date/Time","Station","User","Package","Duration","Cost"};
    for (int i = 0; i < 6; i++) {
        GtkCellRenderer* r = gtk_cell_renderer_text_new();
        GtkTreeViewColumn* col = gtk_tree_view_column_new_with_attributes(
            cols[i], r, "text", i, nullptr);
        gtk_tree_view_column_set_resizable(col, TRUE);
        gtk_tree_view_column_set_min_width(col, 90);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tv), col);
    }

    GtkWidget* sw = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(sw), tv);

    GtkWidget* hbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(hbar, 12);
    gtk_widget_set_margin_end(hbar, 12);
    gtk_widget_set_margin_top(hbar, 8);
    gtk_widget_set_margin_bottom(hbar, 8);

    GtkWidget* lbl = gtk_label_new("Session History (latest 200)");
    GtkWidget* btn_ref = gtk_button_new_with_label("⟳  Refresh");
    g_signal_connect(btn_ref, "clicked", G_CALLBACK(onRefreshRevClicked), this);

    gtk_box_pack_start(GTK_BOX(hbar), lbl,     TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(hbar),   btn_ref, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), hbar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox),
        gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), sw, TRUE, TRUE, 0);

    return vbox;
}

void MainWindow::reloadRevenue() {
    if (!rev_store_) return;
    gtk_list_store_clear(rev_store_);

    auto sessions = db_->getSessions();
    for (auto& r : sessions) {
        char date_buf[32];
        struct tm* tm = localtime(&r.start_time);
        strftime(date_buf, sizeof(date_buf), "%Y-%m-%d %H:%M", tm);

        GtkTreeIter iter;
        gtk_list_store_append(rev_store_, &iter);
        gtk_list_store_set(rev_store_, &iter,
            0, date_buf,
            1, r.station_name.c_str(),
            2, r.username.empty() ? "—" : r.username.c_str(),
            3, r.package_name.c_str(),
            4, fmtTime(r.duration_sec).c_str(),
            5, fmtCost(r.total_cost).c_str(),
            6, r.id,
            -1);
    }
    refreshRevenueLabel();
    reloadDashboard();
}

void MainWindow::refreshRevenueLabel() {
    if (!lbl_revenue_) return;
    double rev = db_->getTodayRevenue();
    char buf[64];
    snprintf(buf, sizeof(buf), "Today: Rp %.0f", rev);
    gtk_label_set_text(GTK_LABEL(lbl_revenue_), buf);
}

// ═══════════════════════════════════════════════════════════════════════════
// ── Food & Drinks (Menu) Tab ──────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════════════

GtkWidget* MainWindow::buildMenuTab() {
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    // Columns: ID, Category, Name, Price, Description, Available
    menu_store_ = gtk_list_store_new(6,
        G_TYPE_INT,     // id
        G_TYPE_STRING,  // category
        G_TYPE_STRING,  // name
        G_TYPE_STRING,  // price
        G_TYPE_STRING,  // description
        G_TYPE_STRING   // available
    );

    GtkWidget* tv = gtk_tree_view_new_with_model(GTK_TREE_MODEL(menu_store_));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tv), TRUE);
    gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(tv), GTK_TREE_VIEW_GRID_LINES_HORIZONTAL);
    gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(tv), TRUE);
    menu_tv_ = GTK_TREE_VIEW(tv);

    struct { const char* title; int idx; int width; } mcols[] = {
        {"Category",    1, 100},
        {"Name",        2, 160},
        {"Price (Rp)",  3, 100},
        {"Description", 4, 220},
        {"Available",   5, 80},
    };
    for (auto& mc : mcols) {
        GtkCellRenderer* r = gtk_cell_renderer_text_new();
        GtkTreeViewColumn* col = gtk_tree_view_column_new_with_attributes(
            mc.title, r, "text", mc.idx, nullptr);
        gtk_tree_view_column_set_resizable(col, TRUE);
        gtk_tree_view_column_set_min_width(col, mc.width);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tv), col);
    }

    GtkWidget* sw = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(sw), tv);

    // ── Bottom toolbar ────────────────────────────────────────────────────
    GtkWidget* hbtn = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(hbtn, 12);
    gtk_widget_set_margin_end(hbtn, 12);
    gtk_widget_set_margin_top(hbtn, 8);
    gtk_widget_set_margin_bottom(hbtn, 8);

    GtkWidget* btn_add  = gtk_button_new_with_label("Add Item");
    GtkWidget* btn_edit = gtk_button_new_with_label("Edit Selected");
    GtkWidget* btn_del  = gtk_button_new_with_label("Delete Selected");
    GtkWidget* btn_sep  = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    GtkWidget* btn_send = gtk_button_new_with_label("Send Offer to Client…");
    GtkWidget* btn_bcast= gtk_button_new_with_label("Broadcast Offer to All");

    gtk_style_context_add_class(gtk_widget_get_style_context(btn_add),  "suggested-action");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_del),  "destructive-action");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_send), "suggested-action");

    auto* md = new MenuData{this, GTK_TREE_VIEW(tv)};

    g_signal_connect(btn_add,  "clicked", G_CALLBACK(onAddMenuClicked),  md);
    g_signal_connect(btn_edit, "clicked", G_CALLBACK(onEditMenuClicked), md);
    g_signal_connect(btn_del,  "clicked", G_CALLBACK(onDelMenuClicked),  md);
    g_signal_connect(btn_send, "clicked", G_CALLBACK(onSendMenuOfferClicked), md);
    g_signal_connect(btn_bcast,"clicked", G_CALLBACK(onBroadcastMenuClicked), md);

    gtk_box_pack_start(GTK_BOX(hbtn), btn_add,   FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbtn), btn_edit,  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbtn), btn_del,   FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbtn), btn_sep,   FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(hbtn), btn_send,  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbtn), btn_bcast, FALSE, FALSE, 0);

    GtkWidget* info = gtk_label_new("Select a menu item and click 'Send Offer' to push it as a chat message to a workstation.");
    gtk_style_context_add_class(gtk_widget_get_style_context(info), "dim-label");
    gtk_widget_set_margin_start(info, 12);
    gtk_widget_set_margin_bottom(info, 4);
    gtk_label_set_xalign(GTK_LABEL(info), 0.0f);

    gtk_box_pack_start(GTK_BOX(vbox), sw, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox),
        gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbtn,  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), info,  FALSE, FALSE, 0);

    return vbox;
}

void MainWindow::reloadMenuItems() {
    if (!menu_store_) return;
    gtk_list_store_clear(menu_store_);
    auto items = db_->getMenuItems();
    for (auto& m : items) {
        char price_buf[32];
        snprintf(price_buf, sizeof(price_buf), "%.0f", m.price);
        GtkTreeIter iter;
        gtk_list_store_append(menu_store_, &iter);
        gtk_list_store_set(menu_store_, &iter,
            0, m.id,
            1, m.category.c_str(),
            2, m.name.c_str(),
            3, price_buf,
            4, m.description.c_str(),
            5, m.available ? "Yes" : "No",
            -1);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// ── Packages Tab ──────────────────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════════════

GtkWidget* MainWindow::buildPackagesTab() {
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    pkg_store_ = gtk_list_store_new(5,
        G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

    GtkWidget* tv = gtk_tree_view_new_with_model(GTK_TREE_MODEL(pkg_store_));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tv), TRUE);
    pkg_tv_ = GTK_TREE_VIEW(tv);

    const char* pcols[] = {"ID","Name","Type","Duration","Price (Rp)"};
    for (int i = 0; i < 5; i++) {
        GtkCellRenderer* r = gtk_cell_renderer_text_new();
        GtkTreeViewColumn* col = gtk_tree_view_column_new_with_attributes(
            pcols[i], r, "text", i, nullptr);
        gtk_tree_view_column_set_resizable(col, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tv), col);
    }

    GtkWidget* sw = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(sw), tv);

    GtkWidget* hbtn = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(hbtn, 12);
    gtk_widget_set_margin_end(hbtn, 12);
    gtk_widget_set_margin_top(hbtn, 8);
    gtk_widget_set_margin_bottom(hbtn, 8);

    auto* pd = new PkgData{this, GTK_TREE_VIEW(tv)};

    GtkWidget* btn_add  = gtk_button_new_with_label("Add Package");
    GtkWidget* btn_edit = gtk_button_new_with_label("Edit Selected");
    GtkWidget* btn_del  = gtk_button_new_with_label("Delete Selected");

    gtk_style_context_add_class(gtk_widget_get_style_context(btn_add), "suggested-action");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_del), "destructive-action");

    g_signal_connect(btn_add,  "clicked", G_CALLBACK(onAddPkgClicked),  pd);
    g_signal_connect(btn_edit, "clicked", G_CALLBACK(onEditPkgClicked), pd);
    g_signal_connect(btn_del,  "clicked", G_CALLBACK(onDelPkgClicked),  pd);

    gtk_box_pack_start(GTK_BOX(hbtn), btn_add,  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbtn), btn_edit, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbtn), btn_del,  FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), sw, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox),
        gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(vbox), hbtn, FALSE, FALSE, 0);

    return vbox;
}

void MainWindow::reloadPackages() {
    if (!pkg_store_) return;
    gtk_list_store_clear(pkg_store_);
    auto pkgs = db_->getPackages();
    for (auto& p : pkgs) {
        char dur_buf[32] = "Hourly";
        if (p.is_timed && p.duration_sec > 0)
            snprintf(dur_buf, sizeof(dur_buf), "%s", fmtTime(p.duration_sec).c_str());
        char price_buf[32];
        snprintf(price_buf, sizeof(price_buf), "%.0f", p.price);

        GtkTreeIter iter;
        gtk_list_store_append(pkg_store_, &iter);
        gtk_list_store_set(pkg_store_, &iter,
            0, p.id,
            1, p.name.c_str(),
            2, p.is_timed ? "Fixed Time" : "Hourly",
            3, dur_buf,
            4, price_buf,
            -1);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// ── Settings Tab ──────────────────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════════════

GtkWidget* MainWindow::buildSettingsTab() {
    GtkWidget* outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget* sw = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 16);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_widget_set_margin_start(grid, 28);
    gtk_widget_set_margin_top(grid, 20);
    gtk_widget_set_margin_end(grid, 28);
    gtk_widget_set_margin_bottom(grid, 20);

    CafeSetting s = db_->loadSettings();

    int row = 0;

    auto add_section = [&](const char* title) {
        GtkWidget* lbl = gtk_label_new(nullptr);
        char markup[128];
        snprintf(markup, sizeof(markup), "<b>%s</b>", title);
        gtk_label_set_markup(GTK_LABEL(lbl), markup);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
        gtk_widget_set_margin_top(lbl, row > 0 ? 16 : 0);
        gtk_grid_attach(GTK_GRID(grid), lbl, 0, row++, 2, 1);
    };

    auto add_row = [&](const char* label, GtkWidget** entry,
                       const std::string& val, bool password = false) {
        GtkWidget* lbl = gtk_label_new(label);
        gtk_label_set_xalign(GTK_LABEL(lbl), 1.0f);
        *entry = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(*entry), val.c_str());
        gtk_widget_set_hexpand(*entry, TRUE);
        if (password) gtk_entry_set_visibility(GTK_ENTRY(*entry), FALSE);
        gtk_grid_attach(GTK_GRID(grid), lbl,    0, row, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), *entry, 1, row, 1, 1);
        row++;
    };

    // ── Cafe Information ─────────────────────────────────────────────────
    add_section("Cafe Information");
    add_row("Cafe Name:",    &ent_cafe_name_, s.cafe_name);
    add_row("Address:",      &ent_address_,   s.cafe_address);
    add_row("Phone:",        &ent_phone_,     s.cafe_phone);
    add_row("WiFi Password:",&ent_wifi_,      s.wifi_password, true);

    // ── Network Configuration ─────────────────────────────────────────────
    add_section("Network Configuration");
    add_row("Server IP (bind):", &ent_server_ip_, s.server_ip);
    add_row("Server Port:",      &ent_port_,      std::to_string(s.server_port));

    // ── Billing Rates ─────────────────────────────────────────────────────
    add_section("Billing Rates");
    add_row("Hourly Rate (Rp):", &ent_rate_,
        std::to_string((int)s.default_rate));
    add_row("Per-Minute Rate (Rp):", &ent_rate_min_,
        std::to_string((int)s.rate_per_minute));

    // Info label
    GtkWidget* info = gtk_label_new(
        "Hourly rate applies to Open/Hourly packages. "
        "Fixed-time packages use their own price.");
    gtk_label_set_xalign(GTK_LABEL(info), 0.0f);
    gtk_style_context_add_class(gtk_widget_get_style_context(info), "dim-label");
    gtk_label_set_line_wrap(GTK_LABEL(info), TRUE);
    gtk_grid_attach(GTK_GRID(grid), info, 1, row++, 1, 1);

    // Save button
    GtkWidget* btn_save = gtk_button_new_with_label("Save All Settings");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_save), "suggested-action");
    g_signal_connect(btn_save, "clicked", G_CALLBACK(onSaveSettingsClicked), this);
    gtk_widget_set_margin_top(btn_save, 12);
    gtk_grid_attach(GTK_GRID(grid), btn_save, 1, row++, 1, 1);

    gtk_container_add(GTK_CONTAINER(sw), grid);
    gtk_box_pack_start(GTK_BOX(outer), sw, TRUE, TRUE, 0);
    return outer;
}

// ═══════════════════════════════════════════════════════════════════════════
// ── BillingServer callbacks ───────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════════════

void MainWindow::addClientStation(ClientInfo* c) {
    if (!flow_box_) return;
    GtkWidget* card = buildStationCard(c);
    gtk_flow_box_insert(GTK_FLOW_BOX(flow_box_), card, -1);
    gtk_widget_show_all(card);
    updateStationCard(c);
    reloadDashboard();
}

void MainWindow::removeClientStation(int client_id) {
    if (!flow_box_) return;
    GList* children = gtk_container_get_children(GTK_CONTAINER(flow_box_));
    for (GList* l = children; l; l = l->next) {
        GtkFlowBoxChild* fbc = GTK_FLOW_BOX_CHILD(l->data);
        auto* c = server_->getClient(client_id);
        if (c && gtk_bin_get_child(GTK_BIN(fbc)) == c->card_widget) {
            gtk_widget_destroy(GTK_WIDGET(fbc));
            break;
        }
    }
    g_list_free(children);

    char cc[32];
    snprintf(cc, sizeof(cc), "%zu client(s)", server_->clients().size() - 1);
    gtk_label_set_text(GTK_LABEL(lbl_clients_), cc);
    reloadDashboard();
}

void MainWindow::updateClientStation(ClientInfo* c) {
    updateStationCard(c);
}

void MainWindow::appendChatMessage(int client_id, const std::string& from,
                                    const std::string& text) {
    if (!chat_buffers_.count(client_id))
        chat_buffers_[client_id] = gtk_text_buffer_new(nullptr);

    GtkTextBuffer* buf = chat_buffers_[client_id];
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buf, &end);

    char ts[16];
    time_t now = time(nullptr);
    struct tm* tm = localtime(&now);
    strftime(ts, sizeof(ts), "%H:%M:%S", tm);

    std::string line = "[" + std::string(ts) + "] " + from + ": " + text + "\n";
    gtk_text_buffer_insert(buf, &end, line.c_str(), -1);
}

// ═══════════════════════════════════════════════════════════════════════════
// ── Static callbacks ───────────────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════════════

void MainWindow::onRefreshDashClicked(GtkWidget*, gpointer data) {
    reinterpret_cast<MainWindow*>(data)->reloadDashboard();
}

void MainWindow::onRefreshRevClicked(GtkWidget*, gpointer data) {
    reinterpret_cast<MainWindow*>(data)->reloadRevenue();
}

void MainWindow::onStartClicked(GtkWidget*, gpointer data) {
    auto* d = reinterpret_cast<BtnData*>(data);
    d->win->openStartSessionDialog(d->srv->getClient(d->client_id));
}

void MainWindow::onStopClicked(GtkWidget*, gpointer data) {
    auto* d = reinterpret_cast<BtnData*>(data);
    auto* c = d->srv->getClient(d->client_id);
    if (!c || !c->session_active) return;

    GtkWidget* dlg = gtk_message_dialog_new(
        GTK_WINDOW(d->win->window_), GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
        "End session for %s?\n\nDuration: %s  —  Cost: Rp %.0f",
        c->hostname.c_str(), fmtTime(c->elapsed_sec).c_str(), c->current_cost);
    int resp = gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);

    if (resp == GTK_RESPONSE_YES) {
        std::string html = d->srv->endSession(d->client_id);
        if (!html.empty())
            d->win->showReceiptDialog(html, c);
    }
}

void MainWindow::onLockClicked(GtkWidget*, gpointer data) {
    auto* d = reinterpret_cast<BtnData*>(data);
    auto* c = d->srv->getClient(d->client_id);
    if (c) d->srv->lockClient(d->client_id, !c->locked);
}

void MainWindow::onChatClicked(GtkWidget*, gpointer data) {
    auto* d = reinterpret_cast<BtnData*>(data);
    d->win->openChatDialog(d->srv->getClient(d->client_id));
}

void MainWindow::onBroadcastClicked(GtkWidget*, gpointer data) {
    auto* win = reinterpret_cast<MainWindow*>(data);
    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "Broadcast Message", GTK_WINDOW(win->window_), GTK_DIALOG_MODAL,
        "_Send", GTK_RESPONSE_OK, "_Cancel", GTK_RESPONSE_CANCEL, nullptr);
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Type message to all clients...");
    gtk_widget_set_margin_start(entry, 16);
    gtk_widget_set_margin_end(entry, 16);
    gtk_widget_set_margin_top(entry, 12);
    gtk_widget_set_margin_bottom(entry, 12);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg))),
        entry, TRUE, TRUE, 0);
    gtk_widget_show_all(dlg);
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK) {
        const char* text = gtk_entry_get_text(GTK_ENTRY(entry));
        if (text && text[0]) win->server_->broadcastMsg(text);
    }
    gtk_widget_destroy(dlg);
}

void MainWindow::onSaveSettingsClicked(GtkWidget*, gpointer data) {
    auto* win = reinterpret_cast<MainWindow*>(data);
    CafeSetting s;
    s.cafe_name     = gtk_entry_get_text(GTK_ENTRY(win->ent_cafe_name_));
    s.cafe_address  = gtk_entry_get_text(GTK_ENTRY(win->ent_address_));
    s.cafe_phone    = gtk_entry_get_text(GTK_ENTRY(win->ent_phone_));
    s.wifi_password = gtk_entry_get_text(GTK_ENTRY(win->ent_wifi_));
    s.server_ip     = gtk_entry_get_text(GTK_ENTRY(win->ent_server_ip_));
    s.server_port   = std::stoi(gtk_entry_get_text(GTK_ENTRY(win->ent_port_)));
    s.default_rate  = std::stod(gtk_entry_get_text(GTK_ENTRY(win->ent_rate_)));
    s.rate_per_minute = std::stod(gtk_entry_get_text(GTK_ENTRY(win->ent_rate_min_)));
    win->db_->saveSettings(s);
    win->server_->reloadSettings();

    // Update header bar subtitle with new cafe name
    GtkWidget* hbar = gtk_window_get_titlebar(GTK_WINDOW(win->window_));
    if (hbar)
        gtk_header_bar_set_subtitle(GTK_HEADER_BAR(hbar), s.cafe_name.c_str());

    GtkWidget* dlg = gtk_message_dialog_new(GTK_WINDOW(win->window_),
        GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
        "Settings saved.\n\nNote: Port/IP changes require a server restart to take effect.");
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

// ── Package callbacks ─────────────────────────────────────────────────────

static void pkgEditDialog(MainWindow* win, Database* db,
                           GtkWidget* parent, const Package* existing = nullptr)
{
    bool is_edit = (existing != nullptr);
    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        is_edit ? "Edit Package" : "Add Package",
        GTK_WINDOW(parent), GTK_DIALOG_MODAL,
        is_edit ? "_Save" : "_Add", GTK_RESPONSE_OK,
        "_Cancel", GTK_RESPONSE_CANCEL, nullptr);

    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_widget_set_margin_start(grid, 16); gtk_widget_set_margin_end(grid, 16);
    gtk_widget_set_margin_top(grid, 16);   gtk_widget_set_margin_bottom(grid, 16);

    GtkWidget* ent_name  = gtk_entry_new();
    GtkWidget* ent_price = gtk_entry_new();
    GtkWidget* ent_dur   = gtk_entry_new();
    GtkWidget* chk_timed = gtk_check_button_new_with_label("Fixed duration (time-limited)");

    gtk_entry_set_placeholder_text(GTK_ENTRY(ent_name),  "e.g. 2 Hours");
    gtk_entry_set_placeholder_text(GTK_ENTRY(ent_price), "Price in Rp");
    gtk_entry_set_placeholder_text(GTK_ENTRY(ent_dur),   "Duration in seconds (0 = open)");

    if (existing) {
        gtk_entry_set_text(GTK_ENTRY(ent_name),  existing->name.c_str());
        gtk_entry_set_text(GTK_ENTRY(ent_price),
            std::to_string((int)existing->price).c_str());
        gtk_entry_set_text(GTK_ENTRY(ent_dur),
            std::to_string(existing->duration_sec).c_str());
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_timed), existing->is_timed);
    }

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Name:"),     0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), ent_name,                   1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Price (Rp):"),0,1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), ent_price,                  1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), chk_timed,                  0, 2, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Duration (sec):"),0,3,1,1);
    gtk_grid_attach(GTK_GRID(grid), ent_dur,                    1, 3, 1, 1);

    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg))),
        grid, TRUE, TRUE, 0);
    gtk_widget_show_all(dlg);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK) {
        Package p;
        if (existing) p.id = existing->id;
        p.name     = gtk_entry_get_text(GTK_ENTRY(ent_name));
        p.price    = std::stod(gtk_entry_get_text(GTK_ENTRY(ent_price)));
        p.is_timed = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_timed));
        const char* dstr = gtk_entry_get_text(GTK_ENTRY(ent_dur));
        p.duration_sec = dstr[0] ? std::stoi(dstr) : 0;
        if (is_edit) db->updatePackage(p);
        else         db->addPackage(p);
        win->reloadPackages();
    }
    gtk_widget_destroy(dlg);
}

void MainWindow::onAddPkgClicked(GtkWidget*, gpointer data) {
    auto* pd = reinterpret_cast<PkgData*>(data);
    pkgEditDialog(pd->win, pd->win->db_, pd->win->window_);
}

void MainWindow::onEditPkgClicked(GtkWidget*, gpointer data) {
    auto* pd = reinterpret_cast<PkgData*>(data);
    GtkTreeSelection* sel = gtk_tree_view_get_selection(pd->tv);
    GtkTreeModel* model; GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(sel, &model, &iter)) return;

    int id; char* name; char* price_s; char* dur_s; char* type_s;
    gtk_tree_model_get(model, &iter,
        0, &id, 1, &name, 3, &dur_s, 4, &price_s, -1);
    gtk_tree_model_get(model, &iter, 2, &type_s, -1);

    // Find the full package from DB
    auto pkgs = pd->win->db_->getPackages();
    for (auto& p : pkgs) {
        if (p.id == id) {
            pkgEditDialog(pd->win, pd->win->db_, pd->win->window_, &p);
            break;
        }
    }
    g_free(name); g_free(price_s); g_free(dur_s); g_free(type_s);
}

void MainWindow::onDelPkgClicked(GtkWidget*, gpointer data) {
    auto* pd = reinterpret_cast<PkgData*>(data);
    GtkTreeSelection* sel = gtk_tree_view_get_selection(pd->tv);
    GtkTreeModel* model; GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(sel, &model, &iter)) return;

    int id; char* name;
    gtk_tree_model_get(model, &iter, 0, &id, 1, &name, -1);

    GtkWidget* conf = gtk_message_dialog_new(GTK_WINDOW(pd->win->window_),
        GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO,
        "Delete package \"%s\"?", name);
    if (gtk_dialog_run(GTK_DIALOG(conf)) == GTK_RESPONSE_YES) {
        pd->win->db_->deletePackage(id);
        pd->win->reloadPackages();
    }
    gtk_widget_destroy(conf);
    g_free(name);
}

// ── Menu item callbacks ───────────────────────────────────────────────────

static void menuItemEditDialog(MainWindow* win, Database* db,
                                GtkWidget* parent, const MenuItem* existing = nullptr)
{
    bool is_edit = (existing != nullptr);
    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        is_edit ? "Edit Menu Item" : "Add Menu Item",
        GTK_WINDOW(parent), GTK_DIALOG_MODAL,
        is_edit ? "_Save" : "_Add", GTK_RESPONSE_OK,
        "_Cancel", GTK_RESPONSE_CANCEL, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 420, -1);

    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_widget_set_margin_start(grid, 16); gtk_widget_set_margin_end(grid, 16);
    gtk_widget_set_margin_top(grid, 16);   gtk_widget_set_margin_bottom(grid, 16);

    GtkWidget* ent_name  = gtk_entry_new();
    GtkWidget* ent_cat   = gtk_combo_box_text_new_with_entry();
    GtkWidget* ent_price = gtk_entry_new();
    GtkWidget* ent_desc  = gtk_entry_new();
    GtkWidget* chk_avail = gtk_check_button_new_with_label("Available for order");

    // Populate categories
    const char* default_cats[] = {"Food","Drink","Snack","Dessert","Other"};
    for (auto& cat : default_cats)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ent_cat), cat);
    auto db_cats = db->getMenuCategories();
    for (auto& cat : db_cats)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ent_cat), cat.c_str());

    gtk_entry_set_placeholder_text(GTK_ENTRY(ent_name),  "e.g. Mie Goreng");
    gtk_entry_set_placeholder_text(GTK_ENTRY(ent_price), "Price in Rp");
    gtk_entry_set_placeholder_text(GTK_ENTRY(ent_desc),  "Short description (optional)");

    if (existing) {
        gtk_entry_set_text(GTK_ENTRY(ent_name),  existing->name.c_str());
        gtk_entry_set_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(ent_cat))),
            existing->category.c_str());
        gtk_entry_set_text(GTK_ENTRY(ent_price),
            std::to_string((int)existing->price).c_str());
        gtk_entry_set_text(GTK_ENTRY(ent_desc), existing->description.c_str());
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_avail), existing->available);
    } else {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_avail), TRUE);
    }

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Name:"),       0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), ent_name,                     1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Category:"),   0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), ent_cat,                      1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Price (Rp):"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), ent_price,                    1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Description:"),0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), ent_desc,                     1, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), chk_avail,                    1, 4, 1, 1);

    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg))),
        grid, TRUE, TRUE, 0);
    gtk_widget_show_all(dlg);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK) {
        MenuItem m;
        if (existing) m.id = existing->id;
        m.name        = gtk_entry_get_text(GTK_ENTRY(ent_name));
        m.category    = gtk_entry_get_text(
            GTK_ENTRY(gtk_bin_get_child(GTK_BIN(ent_cat))));
        const char* ps = gtk_entry_get_text(GTK_ENTRY(ent_price));
        m.price       = ps[0] ? std::stod(ps) : 0.0;
        m.description = gtk_entry_get_text(GTK_ENTRY(ent_desc));
        m.available   = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_avail));
        if (is_edit) db->updateMenuItem(m);
        else         db->addMenuItem(m);
        win->reloadMenuItems();
    }
    gtk_widget_destroy(dlg);
}

void MainWindow::onAddMenuClicked(GtkWidget*, gpointer data) {
    auto* md = reinterpret_cast<MenuData*>(data);
    menuItemEditDialog(md->win, md->win->db_, md->win->window_);
}

void MainWindow::onEditMenuClicked(GtkWidget*, gpointer data) {
    auto* md = reinterpret_cast<MenuData*>(data);
    GtkTreeSelection* sel = gtk_tree_view_get_selection(md->tv);
    GtkTreeModel* model; GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(sel, &model, &iter)) return;

    int id;
    gtk_tree_model_get(model, &iter, 0, &id, -1);

    auto items = md->win->db_->getMenuItems();
    for (auto& m : items) {
        if (m.id == id) {
            menuItemEditDialog(md->win, md->win->db_, md->win->window_, &m);
            break;
        }
    }
}

void MainWindow::onDelMenuClicked(GtkWidget*, gpointer data) {
    auto* md = reinterpret_cast<MenuData*>(data);
    GtkTreeSelection* sel = gtk_tree_view_get_selection(md->tv);
    GtkTreeModel* model; GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(sel, &model, &iter)) return;

    int id; char* name;
    gtk_tree_model_get(model, &iter, 0, &id, 2, &name, -1);

    GtkWidget* conf = gtk_message_dialog_new(GTK_WINDOW(md->win->window_),
        GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO,
        "Delete menu item \"%s\"?", name);
    if (gtk_dialog_run(GTK_DIALOG(conf)) == GTK_RESPONSE_YES) {
        md->win->db_->deleteMenuItem(id);
        md->win->reloadMenuItems();
    }
    gtk_widget_destroy(conf);
    g_free(name);
}

void MainWindow::onSendMenuOfferClicked(GtkWidget*, gpointer data) {
    auto* md = reinterpret_cast<MenuData*>(data);
    MainWindow* win = md->win;

    // Get selected menu item
    GtkTreeSelection* sel = gtk_tree_view_get_selection(md->tv);
    GtkTreeModel* model; GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(sel, &model, &iter)) {
        GtkWidget* d = gtk_message_dialog_new(GTK_WINDOW(win->window_),
            GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
            "Please select a menu item first.");
        gtk_dialog_run(GTK_DIALOG(d));
        gtk_widget_destroy(d);
        return;
    }

    int id;
    gtk_tree_model_get(model, &iter, 0, &id, -1);
    auto items = win->db_->getMenuItems();
    MenuItem selected_item;
    bool found = false;
    for (auto& m : items) {
        if (m.id == id) { selected_item = m; found = true; break; }
    }
    if (!found) return;

    // Choose target client
    auto& clients = win->server_->clients();
    if (clients.empty()) {
        GtkWidget* d = gtk_message_dialog_new(GTK_WINDOW(win->window_),
            GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
            "No clients connected.");
        gtk_dialog_run(GTK_DIALOG(d));
        gtk_widget_destroy(d);
        return;
    }

    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "Send Menu Offer", GTK_WINDOW(win->window_), GTK_DIALOG_MODAL,
        "_Send", GTK_RESPONSE_OK, "_Cancel", GTK_RESPONSE_CANCEL, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 360, -1);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(vbox, 16); gtk_widget_set_margin_end(vbox, 16);
    gtk_widget_set_margin_top(vbox, 12);   gtk_widget_set_margin_bottom(vbox, 12);

    // Item preview
    char preview[256];
    snprintf(preview, sizeof(preview),
        "Sending: <b>%s</b> — %s  |  Rp %.0f",
        selected_item.category.c_str(), selected_item.name.c_str(), selected_item.price);
    GtkWidget* lbl_preview = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(lbl_preview), preview);
    gtk_label_set_xalign(GTK_LABEL(lbl_preview), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(lbl_preview), TRUE);

    GtkWidget* lbl_to = gtk_label_new("Send to:");
    gtk_label_set_xalign(GTK_LABEL(lbl_to), 0.0f);

    GtkWidget* combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "All Clients");
    std::vector<int> cids;
    for (auto& [cid, c] : clients) {
        std::string label = c->hostname;
        if (!c->username.empty()) label += " (" + c->username + ")";
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), label.c_str());
        cids.push_back(cid);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);

    gtk_box_pack_start(GTK_BOX(vbox), lbl_preview, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox),
        gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(vbox), lbl_to, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), combo,  FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg))),
        vbox, TRUE, TRUE, 0);
    gtk_widget_show_all(dlg);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK) {
        int idx = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
        if (idx == 0) {
            win->server_->broadcastMenuOffer(selected_item);
        } else if (idx > 0 && idx - 1 < (int)cids.size()) {
            win->server_->sendMenuOffer(cids[idx - 1], selected_item);
        }
    }
    gtk_widget_destroy(dlg);
}

void MainWindow::onBroadcastMenuClicked(GtkWidget*, gpointer data) {
    auto* md = reinterpret_cast<MenuData*>(data);
    MainWindow* win = md->win;

    GtkTreeSelection* sel = gtk_tree_view_get_selection(md->tv);
    GtkTreeModel* model; GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(sel, &model, &iter)) {
        GtkWidget* d = gtk_message_dialog_new(GTK_WINDOW(win->window_),
            GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
            "Please select a menu item first.");
        gtk_dialog_run(GTK_DIALOG(d)); gtk_widget_destroy(d);
        return;
    }

    int id;
    gtk_tree_model_get(model, &iter, 0, &id, -1);
    auto items = win->db_->getMenuItems();
    for (auto& m : items) {
        if (m.id == id) {
            win->server_->broadcastMenuOffer(m);
            break;
        }
    }
}

// ── Receipt dialog ────────────────────────────────────────────────────────

void MainWindow::showReceiptDialog(const std::string& html_path, ClientInfo* c) {
    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "Payment Receipt", GTK_WINDOW(window_), GTK_DIALOG_MODAL,
        "_Print / Open", GTK_RESPONSE_OK,
        "_Close",        GTK_RESPONSE_CLOSE, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 480, 380);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(vbox, 16); gtk_widget_set_margin_end(vbox, 16);
    gtk_widget_set_margin_top(vbox, 12);   gtk_widget_set_margin_bottom(vbox, 12);

    GtkWidget* lbl_title = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(lbl_title),
        "<b>Session ended — Receipt generated</b>");
    gtk_label_set_xalign(GTK_LABEL(lbl_title), 0.0f);

    GtkWidget* tv = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tv), GTK_WRAP_NONE);

    // Force a monospace font regardless of theme; set_monospace() alone is
    // unreliable on some desktop themes.
    gtk_widget_set_name(tv, "receipt-view");
    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        "#receipt-view { font-family: monospace; font-size: 13px; }", -1, nullptr);
    gtk_style_context_add_provider(gtk_widget_get_style_context(tv),
        GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    std::string txt_path = html_path;
    auto dot = txt_path.rfind('.');
    if (dot != std::string::npos) txt_path = txt_path.substr(0, dot) + ".txt";

    FILE* f = fopen(txt_path.c_str(), "r");
    std::string receipt_text;
    if (f) {
        char buf[128];
        while (fgets(buf, sizeof(buf), f)) receipt_text += buf;
        fclose(f);
    } else {
        receipt_text = "Receipt saved to:\n" + html_path +
            "\n\nClick 'Print/Open' to view in browser.";
    }
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv)),
        receipt_text.c_str(), -1);

    GtkWidget* sw = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(sw), tv);
    gtk_widget_set_vexpand(sw, TRUE);

    GtkWidget* lbl_path = gtk_label_new(("HTML: " + html_path).c_str());
    gtk_label_set_xalign(GTK_LABEL(lbl_path), 0.0f);
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_path), "dim-label");
    gtk_label_set_ellipsize(GTK_LABEL(lbl_path), PANGO_ELLIPSIZE_MIDDLE);

    gtk_box_pack_start(GTK_BOX(vbox), lbl_title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), sw,        TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), lbl_path,  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg))),
        vbox, TRUE, TRUE, 0);
    gtk_widget_show_all(dlg);

    int resp = gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);

    if (resp == GTK_RESPONSE_OK && !html_path.empty()) {
        std::string cmd = "xdg-open \"" + html_path + "\" &";
        system(cmd.c_str());
    }
}

// ── Start session dialog ──────────────────────────────────────────────────

void MainWindow::openStartSessionDialog(ClientInfo* c) {
    if (!c) return;

    GtkWidget* dlg = gtk_dialog_new_with_buttons("Start Session",
        GTK_WINDOW(window_), GTK_DIALOG_MODAL,
        "_Start", GTK_RESPONSE_OK, "_Cancel", GTK_RESPONSE_CANCEL, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 380, -1);

    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_widget_set_margin_start(grid, 16); gtk_widget_set_margin_end(grid, 16);
    gtk_widget_set_margin_top(grid, 16);   gtk_widget_set_margin_bottom(grid, 16);

    GtkWidget* ent_user = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ent_user), "Customer name (optional)");

    GtkWidget* combo = gtk_combo_box_text_new();
    auto pkgs = db_->getPackages();
    for (auto& p : pkgs) {
        char buf[128];
        if (p.is_timed)
            snprintf(buf, sizeof(buf), "%s — Rp %.0f (fixed)", p.name.c_str(), p.price);
        else
            snprintf(buf, sizeof(buf), "%s — Rp %.0f/hr", p.name.c_str(), p.price);
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), buf);
    }
    if (!pkgs.empty()) gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Station:"),  0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid),
        gtk_label_new(c->hostname.c_str()),                     1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Username:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), ent_user,                   1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Package:"),  0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), combo,                      1, 2, 1, 1);

    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg))),
        grid, TRUE, TRUE, 0);
    gtk_widget_show_all(dlg);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK) {
        int idx = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
        if (idx >= 0 && idx < (int)pkgs.size()) {
            const char* uname = gtk_entry_get_text(GTK_ENTRY(ent_user));
            server_->startSession(c->client_id, pkgs[idx], uname ? uname : "");
        }
    }
    gtk_widget_destroy(dlg);
}

// ── Chat dialog ───────────────────────────────────────────────────────────

void MainWindow::openChatDialog(ClientInfo* c) {
    if (!c) return;

    if (chat_windows_.count(c->client_id)) {
        gtk_window_present(GTK_WINDOW(chat_windows_[c->client_id]));
        return;
    }

    if (!chat_buffers_.count(c->client_id))
        chat_buffers_[c->client_id] = gtk_text_buffer_new(nullptr);

    GtkWidget* win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    char title[64];
    snprintf(title, sizeof(title), "Chat — %s", c->hostname.c_str());
    gtk_window_set_title(GTK_WINDOW(win), title);
    gtk_window_set_default_size(GTK_WINDOW(win), 440, 540);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(vbox, 10); gtk_widget_set_margin_end(vbox, 10);
    gtk_widget_set_margin_top(vbox, 10);   gtk_widget_set_margin_bottom(vbox, 10);

    // Chat history
    GtkWidget* tv = gtk_text_view_new_with_buffer(chat_buffers_[c->client_id]);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tv), GTK_WRAP_WORD_CHAR);
    GtkWidget* sw = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_container_add(GTK_CONTAINER(sw), tv);

    // Input row
    GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Type message...");
    gtk_widget_set_hexpand(entry, TRUE);

    struct SendCtx {
        BillingServer* srv; int cid;
        MainWindow* win; GtkWidget* entry; GtkTextBuffer* buf;
    };
    auto* sctx = new SendCtx{server_, c->client_id, this,
                              entry, chat_buffers_[c->client_id]};

    auto send_fn = [](GtkWidget*, gpointer d) {
        auto* ctx = reinterpret_cast<SendCtx*>(d);
        const char* text = gtk_entry_get_text(GTK_ENTRY(ctx->entry));
        if (!text || !text[0]) return;
        ctx->srv->sendChat(ctx->cid, text);
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(ctx->buf, &end);
        time_t now = time(nullptr);
        struct tm* tm = localtime(&now);
        char ts[16]; strftime(ts, sizeof(ts), "%H:%M:%S", tm);
        std::string line = std::string("[") + ts + "] Admin: " + text + "\n";
        gtk_text_buffer_insert(ctx->buf, &end, line.c_str(), -1);
        gtk_entry_set_text(GTK_ENTRY(ctx->entry), "");
    };

    // Menu offer button
    struct MenuOfferCtx { MainWindow* win; int cid; };
    auto* moctx = new MenuOfferCtx{this, c->client_id};

    auto menu_offer_fn = [](GtkWidget*, gpointer d) {
        auto* ctx = reinterpret_cast<MenuOfferCtx*>(d);
        ctx->win->openMenuOfferDialog(ctx->cid);
    };

    GtkWidget* btn_menu  = gtk_button_new_with_label("📋 Menu");
    GtkWidget* btn_send  = gtk_button_new_with_label("Send");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_send), "suggested-action");

    g_signal_connect(btn_send, "clicked", G_CALLBACK(+send_fn),       sctx);
    g_signal_connect(entry,    "activate", G_CALLBACK(+send_fn),      sctx);
    g_signal_connect(btn_menu, "clicked", G_CALLBACK(+menu_offer_fn), moctx);

    gtk_box_pack_start(GTK_BOX(hbox), btn_menu, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), entry,    TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), btn_send, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), sw,   TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(vbox),   hbox, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(win), vbox);

    int cid = c->client_id;
    chat_windows_[cid] = win;
    g_object_set_data(G_OBJECT(win), "mw", this);

    g_signal_connect(win, "destroy", G_CALLBACK(+[](GtkWidget* w, gpointer) {
        auto* self = reinterpret_cast<MainWindow*>(g_object_get_data(G_OBJECT(w), "mw"));
        if (!self) return;
        // Remove closed window from map
        for (auto it = self->chat_windows_.begin(); it != self->chat_windows_.end(); ) {
            if (it->second == w) it = self->chat_windows_.erase(it);
            else ++it;
        }
    }), nullptr);

    gtk_widget_show_all(win);
}

// ── Menu offer dialog (from chat window) ─────────────────────────────────

void MainWindow::openMenuOfferDialog(int client_id) {
    auto items = db_->getMenuItems();
    if (items.empty()) {
        GtkWidget* d = gtk_message_dialog_new(GTK_WINDOW(window_),
            GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
            "No menu items configured. Add items in the Food & Drinks tab.");
        gtk_dialog_run(GTK_DIALOG(d)); gtk_widget_destroy(d);
        return;
    }

    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "Send Menu Offer", GTK_WINDOW(window_), GTK_DIALOG_MODAL,
        "_Send Offer", GTK_RESPONSE_OK, "_Cancel", GTK_RESPONSE_CANCEL, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 400, 360);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(vbox, 16); gtk_widget_set_margin_end(vbox, 16);
    gtk_widget_set_margin_top(vbox, 12);   gtk_widget_set_margin_bottom(vbox, 12);

    GtkWidget* lbl = gtk_label_new("Select a menu item to send as an offer:");
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);

    // Build a simple list store
    GtkListStore* ls = gtk_list_store_new(3,
        G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);
    for (auto& m : items) {
        char price[32];
        snprintf(price, sizeof(price), "Rp %.0f", m.price);
        std::string label = "[" + m.category + "] " + m.name;
        GtkTreeIter it;
        gtk_list_store_append(ls, &it);
        gtk_list_store_set(ls, &it, 0, m.id, 1, label.c_str(), 2, price, -1);
    }

    GtkWidget* tv = gtk_tree_view_new_with_model(GTK_TREE_MODEL(ls));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tv), TRUE);
    g_object_unref(ls);

    GtkCellRenderer* r1 = gtk_cell_renderer_text_new();
    GtkCellRenderer* r2 = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(tv),
        gtk_tree_view_column_new_with_attributes("Item", r1, "text", 1, nullptr));
    gtk_tree_view_append_column(GTK_TREE_VIEW(tv),
        gtk_tree_view_column_new_with_attributes("Price", r2, "text", 2, nullptr));

    GtkWidget* sw = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(sw), tv);
    gtk_widget_set_vexpand(sw, TRUE);

    gtk_box_pack_start(GTK_BOX(vbox), lbl, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), sw,  TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg))),
        vbox, TRUE, TRUE, 0);
    gtk_widget_show_all(dlg);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK) {
        GtkTreeSelection* sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(tv));
        GtkTreeModel* model; GtkTreeIter iter;
        if (gtk_tree_selection_get_selected(sel, &model, &iter)) {
            int id;
            gtk_tree_model_get(model, &iter, 0, &id, -1);
            for (auto& m : items) {
                if (m.id == id) {
                    server_->sendMenuOffer(client_id, m);
                    break;
                }
            }
        }
    }
    gtk_widget_destroy(dlg);
}
