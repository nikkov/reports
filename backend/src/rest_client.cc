#include "rest_client.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

#include <drogon/HttpClient.h>

#include "env.h"

namespace rep {

namespace {

std::string trim(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// Хост и порт из baseUrl: "http://crm.example.com:8080/api" → "crm.example.com:8080".
bool splitHost(const std::string &baseUrl, std::string &hostPort, std::string &error) {
    std::string rest = baseUrl;
    const auto scheme = rest.find("://");
    if (scheme != std::string::npos) {
        const std::string proto = rest.substr(0, scheme);
        if (proto != "http" && proto != "https") {
            error = "источник REST: поддерживаются только http и https";
            return false;
        }
        rest = rest.substr(scheme + 3);
    }
    const auto slash = rest.find('/');
    if (slash != std::string::npos) rest = rest.substr(0, slash);
    if (rest.empty()) {
        error = "источник REST: в baseUrl нет хоста";
        return false;
    }
    hostPort = rest;
    return true;
}

std::string jsonScalar(const Json::Value &v) {
    if (v.isNull()) return {};
    if (v.isBool()) return v.asBool() ? "true" : "false";
    if (v.isInt64()) return std::to_string(v.asInt64());
    if (v.isUInt64()) return std::to_string(v.asUInt64());
    if (v.isDouble()) {
        std::ostringstream os;
        os << v.asDouble();
        return os.str();
    }
    return v.asString();
}

// Значение приводится к объявленному типу: int — целое, float — число, bool — true/false,
// дата и текст — как есть. Несовместимое значение остаётся текстом (видно в отчёте).
std::string coerce(const std::string &raw, const std::string &type) {
    if (raw.empty() || type == "text") return raw;
    if (type == "int") {
        try {
            return std::to_string(std::stoll(trim(raw)));
        } catch (...) {
            return raw;
        }
    }
    if (type == "float") {
        try {
            const double d = std::stod(trim(raw));
            std::ostringstream os;
            os << d;
            return os.str();
        } catch (...) {
            return raw;
        }
    }
    if (type == "bool") {
        const std::string low = trim(raw);
        if (low == "true" || low == "1" || low == "yes") return "true";
        if (low == "false" || low == "0" || low == "no") return "false";
        return raw;
    }
    return raw;
}

std::string urlEncode(const std::string &value) {
    static const char *hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}

}  // namespace

bool parseRestSpec(const Json::Value &rest, RestSpec &out, std::string &error) {
    out = RestSpec{};
    if (!rest.isObject()) {
        error = "datasets.<key>.rest должен быть объектом";
        return false;
    }
    out.path = rest.get("path", "").asString();
    if (out.path.empty() || out.path[0] != '/') {
        error = "rest.path обязателен и должен начинаться с '/'";
        return false;
    }
    const Json::Value &q = rest["query"];
    if (q.isObject()) {
        for (auto it = q.begin(); it != q.end(); ++it) out.query[it.name()] = jsonScalar(*it);
    }
    out.rowsPath = rest.get("rowsPath", "").asString();
    const Json::Value &cols = rest["columns"];
    if (!cols.isArray() || cols.empty()) {
        error = "rest.columns: нужен непустой массив колонок (типы из JSON неоднозначны)";
        return false;
    }
    for (const auto &c : cols) {
        if (!c.isObject()) {
            error = "rest.columns: колонка должна быть объектом";
            return false;
        }
        RestColumn col;
        col.field = c.get("field", "").asString();
        if (col.field.empty()) {
            error = "rest.columns: у колонки нет field";
            return false;
        }
        col.type = c.get("type", "text").asString();
        static const std::vector<std::string> types = {"text", "int", "float", "date", "datetime", "bool"};
        if (std::find(types.begin(), types.end(), col.type) == types.end()) {
            error = "rest.columns." + col.field + ": неизвестный тип '" + col.type + "'";
            return false;
        }
        col.title = c.get("title", "").asString();
        col.as = c.get("as", "").asString();
        out.columns.push_back(col);
    }
    return true;
}

std::vector<std::string> parseAllowHosts(const std::string &value) {
    std::vector<std::string> out;
    std::string cur;
    for (char ch : value) {
        if (ch == ',' || ch == ';') {
            const std::string t = trim(cur);
            if (!t.empty()) out.push_back(t);
            cur.clear();
            continue;
        }
        cur += ch;
    }
    const std::string t = trim(cur);
    if (!t.empty()) out.push_back(t);
    return out;
}

bool restHostAllowed(const std::string &baseUrl, const std::vector<std::string> &allowed,
                     std::string &host, std::string &error) {
    if (!splitHost(baseUrl, host, error)) return false;
    if (allowed.empty()) {
        error = "REST-источники запрещены: задайте REP_REST_ALLOW_HOSTS (список хостов через запятую)";
        return false;
    }
    std::string hostOnly = host;
    const auto colon = hostOnly.rfind(':');
    if (colon != std::string::npos && hostOnly.find(']') == std::string::npos) {
        hostOnly = hostOnly.substr(0, colon);
    }
    for (const auto &a : allowed) {
        if (a == host || a == hostOnly) return true;
    }
    error = "источник REST: хост '" + host + "' не входит в REP_REST_ALLOW_HOSTS";
    return false;
}

bool parseRestRows(const std::string &body, const RestSpec &spec, DatasetRows &out, std::string &error) {
    out = DatasetRows{};
    Json::CharReaderBuilder b;
    Json::Value root;
    std::string errs;
    std::istringstream in(body);
    if (!Json::parseFromStream(b, in, &root, &errs)) {
        error = "ответ REST не разбирается как JSON";
        return false;
    }
    const Json::Value *node = &root;
    if (!spec.rowsPath.empty()) {
        std::string seg;
        std::istringstream path(spec.rowsPath);
        while (std::getline(path, seg, '.')) {
            if (seg.empty()) continue;
            if (node->isObject() && node->isMember(seg)) {
                node = &(*node)[seg];
            } else if (node->isArray() && !seg.empty() &&
                       std::all_of(seg.begin(), seg.end(), [](unsigned char c) { return std::isdigit(c); })) {
                const auto idx = static_cast<Json::ArrayIndex>(std::stoul(seg));
                if (idx >= node->size()) {
                    error = "rest.rowsPath: в массиве нет индекса " + seg;
                    return false;
                }
                node = &(*node)[idx];
            } else {
                error = "rest.rowsPath: в ответе нет '" + seg + "'";
                return false;
            }
        }
    }
    if (!node->isArray()) {
        error = "rest.rowsPath: ожидался массив строк";
        return false;
    }
    for (const auto &c : spec.columns) {
        RowColumn col;
        col.name = c.as.empty() ? c.field : c.as;
        col.type = c.type;
        col.title = c.title;
        out.columns.push_back(col);
    }
    out.rows.reserve(node->size());
    for (const auto &item : *node) {
        std::vector<std::string> row;
        row.reserve(spec.columns.size());
        for (const auto &c : spec.columns) {
            std::string raw;
            if (item.isObject()) raw = jsonScalar(item[c.field]);
            row.push_back(coerce(raw, c.type));
        }
        out.rows.push_back(std::move(row));
    }
    return true;
}

RestCache::RestCache(size_t maxEntries, int defaultTtlSeconds)
    : maxEntries_(maxEntries), defaultTtl_(defaultTtlSeconds) {}

bool RestCache::get(const std::string &key, std::string &body) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = entries_.find(key);
    if (it == entries_.end()) return false;
    if (std::chrono::steady_clock::now() >= it->second.expires) {
        entries_.erase(it);
        return false;
    }
    body = it->second.body;
    return true;
}

void RestCache::put(const std::string &key, const std::string &body, int ttlSeconds) {
    const int ttl = ttlSeconds > 0 ? ttlSeconds : defaultTtl_;
    if (ttl <= 0) return; // кеш выключен
    std::lock_guard<std::mutex> lock(mutex_);
    if (entries_.size() >= maxEntries_ && entries_.find(key) == entries_.end()) {
        // простая эвикция: самая старая по сроку
        auto oldest = entries_.begin();
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->second.expires < oldest->second.expires) oldest = it;
        }
        if (oldest != entries_.end()) entries_.erase(oldest);
    }
    entries_[key] = {body, std::chrono::steady_clock::now() + std::chrono::seconds(ttl)};
}

size_t RestCache::size() {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
}

void restFetch(const ConnSpec &conn, const RestSpec &spec, RestCache &cache,
               std::function<void(const DatasetRows &)> onOk,
               std::function<void(const std::string &, const std::string &)> onErr) {
    std::string error;
    const std::string baseUrl = expandEnvValue(conn.baseUrl, conn.key, error);
    if (!error.empty()) {
        onErr("REST_ERROR", "источник " + conn.key + ": " + error);
        return;
    }
    // allowlist хостов: без него REST выключен целиком (защита от SSRF)
    const std::vector<std::string> allowed = parseAllowHosts(envOr("REP_REST_ALLOW_HOSTS", ""));
    std::string host;
    if (!restHostAllowed(baseUrl, allowed, host, error)) {
        onErr("REST_FORBIDDEN", "источник " + conn.key + ": " + error);
        return;
    }

    std::string pathWithQuery = spec.path;
    bool first = spec.path.find('?') == std::string::npos;
    for (const auto &kv : spec.query) {
        pathWithQuery += first ? '?' : '&';
        first = false;
        pathWithQuery += urlEncode(kv.first) + "=" + urlEncode(kv.second);
    }

    // Ключ кеша: адрес+путь+query и хеш заголовков. Сами заголовки (токены) в ключ не попадают.
    size_t headersHash = 0;
    std::map<std::string, std::string> headers;
    for (const auto &kv : conn.headers) {
        const std::string value = expandEnvValue(kv.second, conn.key, error);
        if (!error.empty()) {
            onErr("REST_ERROR", "источник " + conn.key + ": " + error);
            return;
        }
        headers[kv.first] = value;
        headersHash ^= std::hash<std::string>{}(kv.first + ":" + value) + 0x9e3779b97f4a7c15ULL;
    }
    const std::string cacheKey = baseUrl + "|" + pathWithQuery + "|" + std::to_string(headersHash);

    std::string cached;
    if (cache.get(cacheKey, cached)) {
        DatasetRows rows;
        if (parseRestRows(cached, spec, rows, error)) {
            onOk(rows);
            return;
        }
        // битое в кеше — идём в сеть
    }

    auto client = drogon::HttpClient::newHttpClient(baseUrl);
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Get);
    req->setPath(pathWithQuery);
    for (const auto &kv : headers) req->addHeader(kv.first, kv.second);

    const double timeoutSec = std::max(1, conn.timeoutMs) / 1000.0;
    const size_t maxBytes = static_cast<size_t>(std::stoul(envOr("REP_REST_MAX_BYTES", "5242880")));
    client->sendRequest(
        req,
        [spec, cacheKey, &cache, maxBytes, onOk, onErr, conn](drogon::ReqResult result,
                                                              const drogon::HttpResponsePtr &resp) {
            if (result != drogon::ReqResult::Ok || !resp) {
                if (result == drogon::ReqResult::Timeout) {
                    onErr("REST_TIMEOUT", "источник " + conn.key + ": превышен таймаут " +
                                              std::to_string(conn.timeoutMs) + " мс");
                } else {
                    onErr("REST_ERROR", "источник " + conn.key + ": нет ответа от сервера");
                }
                return;
            }
            const auto status = resp->statusCode();
            if (status >= 300 && status < 400) {
                onErr("REST_INVALID", "источник " + conn.key + ": редирект " + std::to_string(status) +
                                          " запрещён");
                return;
            }
            if (status < 200 || status >= 300) {
                onErr("REST_ERROR", "источник " + conn.key + ": сервер ответил " +
                                        std::to_string(status));
                return;
            }
            const std::string body = std::string(resp->body());
            if (body.size() > maxBytes) {
                onErr("REST_TOO_LARGE", "источник " + conn.key + ": ответ " + std::to_string(body.size()) +
                                            " байт больше лимита " + std::to_string(maxBytes));
                return;
            }
            rep::DatasetRows rows;
            std::string error;
            if (!rep::parseRestRows(body, spec, rows, error)) {
                onErr("REST_INVALID", "источник " + conn.key + ": " + error);
                return;
            }
            cache.put(cacheKey, body, conn.cacheSeconds);
            onOk(rows);
        },
        timeoutSec);
}

} // namespace rep
