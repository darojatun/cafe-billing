#pragma once
#include <string>
#include <ctime>

struct ReceiptData {
    std::string cafe_name;
    std::string station_name;
    std::string username;
    std::string package_name;
    time_t      start_time   = 0;
    time_t      end_time     = 0;
    int         duration_sec = 0;
    double      total_cost   = 0.0;
    std::string receipt_no;   // auto-generated
};

class Receipt {
public:
    // Generate a plain-text receipt and return as string
    static std::string makeText(const ReceiptData& d);

    // Generate an HTML receipt and return as string
    static std::string makeHtml(const ReceiptData& d);

    // Save receipt files to ~/cafebill-receipts/ and return html path
    static std::string save(const ReceiptData& d);
};
