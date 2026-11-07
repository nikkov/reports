// Кэш результатов отчёта (ADR-32): TTL + ETag, ключ — отчёт, версия шаблонов и параметры.
// Данные читаются только на чтение, поэтому кэш можно держать в памяти процесса:
// публикация шаблона поднимает версию, и старые ключи перестают находиться.
#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>

namespace rep {

class ResultCache {
public:
    ResultCache(int ttlSeconds, std::size_t maxEntries, std::size_t maxBodyBytes = 2u * 1024 * 1024)
        : ttl_(ttlSeconds), max_(maxEntries == 0 ? 1 : maxEntries), maxBody_(maxBodyBytes) {}

    bool enabled() const { return ttl_ > 0; }

    struct Entry {
        std::string body;
        std::string etag;
    };

    // Свежая запись или пусто. Просроченная запись удаляется.
    bool get(const std::string &key, Entry &out) {
        if (!enabled()) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = map_.find(key);
        if (it == map_.end()) return false;
        const auto age = std::chrono::steady_clock::now() - it->second.at;
        if (age > std::chrono::seconds(ttl_)) {
            map_.erase(it);
            return false;
        }
        out = {it->second.body, it->second.etag};
        return true;
    }

    void put(const std::string &key, std::string body, std::string etag) {
        // крупные ответы не кэшируем: 2 МБ × сотни записей — это память процесса впустую
        if (!enabled() || body.size() > maxBody_) return;
        std::lock_guard<std::mutex> lock(mutex_);
        const auto now = std::chrono::steady_clock::now();
        if (map_.find(key) == map_.end()) order_.push_back(key);
        map_[key] = {std::move(body), std::move(etag), now};
        // простая эвикция: самые старые ключи уходят первыми, память ограничена
        while (order_.size() > max_) {
            const auto old = order_.front();
            order_.pop_front();
            map_.erase(old);
        }
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        map_.clear();
        order_.clear();
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return map_.size();
    }

private:
    struct Value {
        std::string body;
        std::string etag;
        std::chrono::steady_clock::time_point at;
    };

    int ttl_;
    std::size_t max_;
    std::size_t maxBody_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, Value> map_;
    std::deque<std::string> order_;
};

} // namespace rep
