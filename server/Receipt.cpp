#include "Receipt.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <sys/stat.h>

static std::string fmtDateTime(time_t t) {
    struct tm* tm = localtime(&t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
    return buf;
}

static std::string fmtDuration(int sec) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", sec/3600, (sec%3600)/60, sec%60);
    return buf;
}

static std::string makeReceiptNo() {
    time_t now = time(nullptr);
    char buf[32];
    strftime(buf, sizeof(buf), "RCP%Y%m%d%H%M%S", localtime(&now));
    return buf;
}

// ── Plain text receipt ────────────────────────────────────────────────────

std::string Receipt::makeText(const ReceiptData& d) {
    std::ostringstream o;
    const int W = 42;
    auto line  = [&](const std::string& s = "") {
        o << "| " << std::left << std::setw(W-2) << s << " |\n";
    };
    auto divider = [&]() {
        o << "+" << std::string(W, '-') << "+\n";
    };
    auto center  = [&](const std::string& s) {
        int pad = (W - 2 - (int)s.size()) / 2;
        int rpad = W - 2 - pad - (int)s.size();
        o << "| " << std::string(std::max(0,pad),' ') << s
          << std::string(std::max(0,rpad),' ') << " |\n";
    };
    auto kv = [&](const std::string& k, const std::string& v) {
        std::string row = k + ": " + v;
        line(row);
    };

    divider();
    center(d.cafe_name);
    center("PAYMENT RECEIPT");
    divider();
    line();
    kv("Receipt No",  d.receipt_no);
    kv("Station",     d.station_name);
    kv("Customer",    d.username.empty() ? "—" : d.username);
    kv("Package",     d.package_name);
    line();
    divider();
    kv("Start Time",  fmtDateTime(d.start_time));
    kv("End Time",    fmtDateTime(d.end_time));
    kv("Duration",    fmtDuration(d.duration_sec));
    divider();
    line();
    {
        char cost[32];
        snprintf(cost, sizeof(cost), "Rp %.0f", d.total_cost);
        std::string row = std::string("TOTAL DUE") +
                          std::string(W - 2 - 9 - strlen(cost), ' ') + cost;
        line(row);
    }
    line();
    divider();
    center("Thank you for your visit!");
    center("Please come again.");
    divider();
    o << "\n";
    return o.str();
}

// ── HTML receipt ──────────────────────────────────────────────────────────

std::string Receipt::makeHtml(const ReceiptData& d) {
    char cost_buf[32];
    snprintf(cost_buf, sizeof(cost_buf), "%.0f", d.total_cost);

    std::ostringstream o;
    o << R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>Receipt )HTML" << d.receipt_no << R"HTML(</title>
<style>
  @media print {
    body { margin: 0; }
    .no-print { display: none; }
  }
  body {
    font-family: 'Courier New', monospace;
    max-width: 400px;
    margin: 20px auto;
    background: #fff;
    color: #111;
  }
  .receipt {
    border: 2px solid #222;
    padding: 20px;
    border-radius: 4px;
  }
  h1 { text-align: center; font-size: 1.4em; margin: 0 0 4px; }
  h2 { text-align: center; font-size: 1em; margin: 0 0 16px; color: #555; }
  hr { border: 1px dashed #aaa; margin: 12px 0; }
  table { width: 100%; border-collapse: collapse; }
  td { padding: 3px 0; font-size: 0.92em; }
  td:last-child { text-align: right; }
  .total-row td { font-size: 1.2em; font-weight: bold; border-top: 2px solid #222;
                   padding-top: 8px; }
  .footer { text-align: center; margin-top: 16px; font-size: 0.85em; color: #555; }
  .print-btn {
    display: block; margin: 16px auto 0; padding: 10px 28px;
    background: #2196F3; color: white; border: none; border-radius: 4px;
    font-size: 1em; cursor: pointer;
  }
  .print-btn:hover { background: #1976D2; }
</style>
</head>
<body>
<div class="receipt">
  <h1>)HTML" << d.cafe_name << R"HTML(</h1>
  <h2>PAYMENT RECEIPT</h2>
  <hr>
  <table>
    <tr><td>Receipt No</td><td>)HTML" << d.receipt_no << R"HTML(</td></tr>
    <tr><td>Station</td><td>)HTML" << d.station_name << R"HTML(</td></tr>
    <tr><td>Customer</td><td>)HTML" << (d.username.empty() ? "—" : d.username) << R"HTML(</td></tr>
    <tr><td>Package</td><td>)HTML" << d.package_name << R"HTML(</td></tr>
  </table>
  <hr>
  <table>
    <tr><td>Start Time</td><td>)HTML" << fmtDateTime(d.start_time) << R"HTML(</td></tr>
    <tr><td>End Time</td><td>)HTML" << fmtDateTime(d.end_time) << R"HTML(</td></tr>
    <tr><td>Duration</td><td>)HTML" << fmtDuration(d.duration_sec) << R"HTML(</td></tr>
  </table>
  <hr>
  <table>
    <tr class="total-row">
      <td>TOTAL DUE</td>
      <td>Rp )HTML" << cost_buf << R"HTML(</td>
    </tr>
  </table>
  <div class="footer">
    <p>Thank you for your visit!<br>Please come again.</p>
  </div>
</div>
<button class="print-btn no-print" onclick="window.print()">🖨 Print Receipt</button>
</body>
</html>
)HTML";
    return o.str();
}

// ── Save to disk ──────────────────────────────────────────────────────────

std::string Receipt::save(const ReceiptData& d_in) {
    ReceiptData d = d_in;
    if (d.receipt_no.empty()) d.receipt_no = makeReceiptNo();

    // Create receipts directory
    const char* home = getenv("HOME");
    std::string dir = home ? std::string(home) + "/cafebill-receipts" : "cafebill-receipts";
    mkdir(dir.c_str(), 0755);

    // Save HTML
    std::string html_path = dir + "/" + d.receipt_no + ".html";
    {
        std::ofstream f(html_path);
        f << makeHtml(d);
    }

    // Save plain text
    std::string txt_path = dir + "/" + d.receipt_no + ".txt";
    {
        std::ofstream f(txt_path);
        f << makeText(d);
    }

    printf("[receipt] Saved: %s\n", html_path.c_str());
    return html_path;
}
