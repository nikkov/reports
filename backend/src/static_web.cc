#include "static_web.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <vector>

namespace rep {

namespace fs = std::filesystem;

namespace {

bool hasIndex(const std::string &dir) {
    std::error_code ec;
    return fs::is_regular_file(fs::path(dir) / "index.html", ec);
}

// %20 и прочие escape-последовательности: браузер их присылает, файловая система — нет.
std::string urlDecode(const std::string &value) {
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size() &&
            std::isxdigit(static_cast<unsigned char>(value[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(value[i + 2]))) {
            out += static_cast<char>(std::stoi(value.substr(i + 1, 2), nullptr, 16));
            i += 2;
            continue;
        }
        out += value[i];
    }
    return out;
}

std::vector<std::string> splitPath(const std::string &path) {
    std::vector<std::string> parts;
    std::string current;
    for (char ch : path) {
        if (ch == '/') {
            if (!current.empty()) parts.push_back(current);
            current.clear();
            continue;
        }
        current += ch;
    }
    if (!current.empty()) parts.push_back(current);
    return parts;
}

bool startsWith(const std::string &value, const std::string &prefix) {
    return value.rfind(prefix, 0) == 0;
}

bool insideRoot(const std::string &root, const std::string &candidate) {
#ifdef _WIN32
    const std::string normalizedRoot = root.back() == '\\' ? root : root + "\\";
#else
    const std::string normalizedRoot = root.back() == '/' ? root : root + "/";
#endif
    return startsWith(candidate, normalizedRoot) || candidate == root;
}

}  // namespace

bool findWebRoot(const std::string &explicitRoot, const std::string &exeDir, WebRoot &out) {
    std::error_code ec;

    // Явный путь — единственный кандидат: если он неверен, лучше сказать об этом,
    // чем молча отдать статику из другого каталога.
    if (!explicitRoot.empty()) {
        const auto canonical = fs::weakly_canonical(explicitRoot, ec);
        const std::string path = ec ? explicitRoot : canonical.string();
        if (!hasIndex(path)) return false;
        out.path = path;
        out.source = "REP_WEB_ROOT";
        return true;
    }

    std::vector<std::pair<std::string, std::string>> candidates;  // путь, как назвать в логе
    candidates.emplace_back("./web", "./web");
    candidates.emplace_back("../web", "../web");
    if (!exeDir.empty()) {
        candidates.emplace_back((fs::path(exeDir) / "web").string(), "<бинарник>/web");
        candidates.emplace_back((fs::path(exeDir) / ".." / ".." / "frontend" / "dist").string(),
                                "<бинарник>/../../frontend/dist");
    }

    for (const auto &[candidate, label] : candidates) {
        const auto canonical = fs::weakly_canonical(candidate, ec);
        const std::string path = ec ? candidate : canonical.string();
        if (!hasIndex(path)) continue;
        out.path = path;
        out.source = label;
        return true;
    }
    return false;
}

StaticFile resolveStatic(const std::string &root, const std::string &urlPath) {
    StaticFile result;
    if (root.empty()) return result;
    if (urlPath.find('\0') != std::string::npos) return result;

    const std::string decoded = urlDecode(urlPath);
    const auto parts = splitPath(decoded);
    // '..' в URL — попытка выйти из каталога статики: отказываем целиком.
    for (const auto &part : parts) {
        if (part == "..") return result;
    }

    fs::path relative;
    for (const auto &part : parts) relative /= part;

    std::error_code ec;
    const bool wantsDirectory = decoded.empty() || decoded.back() == '/';
    fs::path candidate = (fs::path(root) / relative);
    if (wantsDirectory) candidate /= "index.html";

    const auto canonical = fs::weakly_canonical(candidate, ec);
    const std::string target = ec ? candidate.string() : canonical.string();
    if (!insideRoot(root, target)) return result;

    if (fs::is_regular_file(target, ec)) {
        result.found = true;
        result.path = target;
        return result;
    }

    // SPA: маршрут без расширения — отдаём index.html. Каталог (путь с '/') и запрос
    // файла с расширением подменять страницей нельзя: иначе отсутствующий app.js
    // вернул бы HTML с кодом 200 и браузер молча сломался бы.
    const std::string last = parts.empty() ? "" : parts.back();
    if (!wantsDirectory && !last.empty() && last.find('.') == std::string::npos) {
        const fs::path index = fs::path(root) / "index.html";
        if (fs::is_regular_file(index, ec)) {
            result.spaFallback = true;
            result.path = index.string();
        }
    }
    return result;
}

std::string contentTypeFor(const std::string &fileName) {
    const auto dot = fileName.find_last_of('.');
    std::string ext = dot == std::string::npos ? "" : fileName.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    if (ext == "html" || ext == "htm") return "text/html; charset=utf-8";
    if (ext == "js" || ext == "mjs") return "text/javascript; charset=utf-8";
    if (ext == "css") return "text/css; charset=utf-8";
    if (ext == "json" || ext == "map") return "application/json; charset=utf-8";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif") return "image/gif";
    if (ext == "webp") return "image/webp";
    if (ext == "ico") return "image/x-icon";
    if (ext == "woff") return "font/woff";
    if (ext == "woff2") return "font/woff2";
    if (ext == "ttf") return "font/ttf";
    if (ext == "eot") return "application/vnd.ms-fontobject";
    if (ext == "txt") return "text/plain; charset=utf-8";
    if (ext == "wasm") return "application/wasm";
    return "application/octet-stream";
}

std::string cacheControlFor(const std::string &urlPath, const std::string &fileName) {
    const std::string file = fileName.empty() ? urlPath : fileName;
    const auto dot = file.find_last_of('.');
    std::string ext = dot == std::string::npos ? "" : file.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (ext == "html" || ext == "htm" || urlPath == "/" || urlPath.empty()) return "no-cache";
    if (startsWith(urlPath, "/assets/")) return "public, max-age=31536000, immutable";
    return "public, max-age=3600";
}

std::string describeWebRoot(const std::string &path, const std::string &exeDir) {
    if (exeDir.empty() || path.rfind(exeDir, 0) != 0) return path;
    return "<бинарник>" + path.substr(exeDir.size());
}

}  // namespace rep
