#pragma once

// Статика внутри сервера (ADR-79): drogon отдаёт и API, и собранный фронтенд,
// поэтому отдельные nginx и node на стенде не нужны. Каталог статики ищется рядом
// с бинарником, а SPA-маршруты (например /report.html) отдают index.html.

#include <string>

namespace rep {

struct WebRoot {
    std::string path;    // абсолютный путь к каталогу
    std::string source;  // откуда взят: REP_WEB_ROOT, ./web, <бинарник>/web, ...
};

// Порядок поиска:
//   1. REP_WEB_ROOT — если задан, используется как есть (никаких альтернатив);
//   2. ./web и ../web — комплект «бинарник + web» рядом, запуск из любого каталога;
//   3. <каталог бинарника>/web;
//   4. <каталог бинарника>/../../frontend/dist — раскладка репозитория (backend/build/rep).
// Возвращает false, если ни один каталог с index.html не найден.
bool findWebRoot(const std::string &explicitRoot, const std::string &exeDir, WebRoot &out);

struct StaticFile {
    bool found = false;       // файл существует и лежит внутри root
    bool spaFallback = false; // отдать index.html: маршрут без расширения
    std::string path;         // абсолютный путь к файлу
};

// Путь из URL → файл внутри root. Выход за пределы каталога запрещён.
StaticFile resolveStatic(const std::string &root, const std::string &urlPath);

// Content-Type по расширению: index.html, *.js, *.css, шрифты, картинки, wasm.
std::string contentTypeFor(const std::string &fileName);

// Кэш: index.html — no-cache (после обновления сразу видно), /assets/* — immutable
// (у Vite в имени хэш), остальное — час.
std::string cacheControlFor(const std::string &urlPath, const std::string &fileName);

// Человеческие имена для лога: «<бинарник>/web» вместо абсолютного пути.
std::string describeWebRoot(const std::string &path, const std::string &exeDir);

}  // namespace rep
