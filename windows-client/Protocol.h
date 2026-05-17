#pragma once
#include <string>
#include <map>
#include <vector>
#include <sstream>

// Shared pipe-delimited protocol (Windows-compatible, no GLib)
// Format: TYPE|key1|val1|key2|val2\n

namespace Protocol {

    using Fields = std::map<std::string, std::string>;

    struct Message {
        std::string type;
        Fields      fields;
        bool        valid = false;
    };

    inline std::string encode(const std::string& type,
        std::initializer_list<std::pair<std::string, std::string>> kv)
    {
        std::string msg = type;
        for (auto& [k, v] : kv) { msg += '|'; msg += k; msg += '|'; msg += v; }
        msg += '\n';
        return msg;
    }

    inline Message decode(const std::string& line) {
        Message m;
        if (line.empty()) return m;
        std::vector<std::string> parts;
        std::istringstream ss(line);
        std::string tok;
        while (std::getline(ss, tok, '|')) parts.push_back(tok);
        if (parts.empty()) return m;
        m.type = parts[0];
        while (!m.type.empty() && (m.type.back() == '\r' || m.type.back() == '\n'))
            m.type.pop_back();
        for (size_t i = 1; i + 1 < parts.size(); i += 2) {
            std::string val = parts[i + 1];
            while (!val.empty() && (val.back() == '\r' || val.back() == '\n'))
                val.pop_back();
            m.fields[parts[i]] = val;
        }
        m.valid = !m.type.empty();
        return m;
    }

    static const char* REGISTER      = "REGISTER";
    static const char* REGISTERED    = "REGISTERED";
    static const char* SESSION_START  = "SESSION_START";
    static const char* SESSION_END    = "SESSION_END";
    static const char* TICK           = "TICK";
    static const char* CHAT_CLIENT    = "CHAT_CLIENT";
    static const char* CHAT_SERVER    = "CHAT_SERVER";
    static const char* LOCK           = "LOCK";
    static const char* SERVER_MSG     = "SERVER_MSG";
    static const char* PACKAGES       = "PACKAGES";
    static const char* MENU_OFFER     = "MENU_OFFER";
    static const char* HEARTBEAT      = "HEARTBEAT";

} // namespace Protocol
