// Статика в сервере (ADR-79): поиск каталога, защита от выхода за его пределы,
// SPA-fallback и заголовки кэша. Проверяется на временном каталоге, без drogon.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>

#include "static_web.h"

namespace fs = std::filesystem;

namespace {

struct WebDir {
    fs::path root;
    WebDir() {
        std::random_device rd;
        root = fs::temp_directory_path() / ("rep-web-" + std::to_string(rd()));
        fs::remove_all(root);
        fs::create_directories(root / "assets");
        std::ofstream(root / "index.html") << "<!doctype html><title>ok</title>";
        std::ofstream(root / "assets" / "app-abc123.js") << "console.log(1)";
        std::ofstream(root / "assets" / "app.css") << "body{}";
        std::ofstream(root / "logo.svg") << "<svg/>";
    }
    ~WebDir() { fs::remove_all(root); }
};

// Тесты запускаются из каталога сборки, где уже лежит web/ (её кладёт build.sh).
// Чтобы проверять приоритеты, уводим рабочий каталог во временный.
struct CwdGuard {
    fs::path old;
    explicit CwdGuard(const fs::path &dir) : old(fs::current_path()) { fs::current_path(dir); }
    ~CwdGuard() { std::error_code ec; fs::current_path(old, ec); }
};

}  // namespace

TEST_CASE("статика: явный каталог используется и называется в логе") {
    WebDir dir;
    rep::WebRoot web;
    REQUIRE(rep::findWebRoot(dir.root.string(), "", web));
    REQUIRE(web.source == "REP_WEB_ROOT");
    REQUIRE(web.path == fs::weakly_canonical(dir.root).string());
}

TEST_CASE("статика: каталог без index.html не принимается") {
    WebDir dir;
    rep::WebRoot web;
    REQUIRE_FALSE(rep::findWebRoot((dir.root / "assets").string(), "", web));
}

TEST_CASE("статика: бинарник рядом с frontend/dist находит сборку репозитория") {
    // <бинарник>/../../frontend/dist: backend/build → frontend/dist
    WebDir dir;
    const auto exe = dir.root / "backend" / "build";
    fs::create_directories(exe);
    const auto dist = dir.root / "frontend" / "dist";
    fs::create_directories(dist);
    std::ofstream(dist / "index.html") << "<!doctype html>";

    rep::WebRoot web;
    CwdGuard cwd(dir.root);   // иначе первым найдётся ./web из каталога сборки
    REQUIRE(rep::findWebRoot("", exe.string(), web));
    REQUIRE(web.source == "<бинарник>/../../frontend/dist");
    REQUIRE(fs::equivalent(web.path, dist));
}

TEST_CASE("статика: web рядом с бинарником важнее frontend/dist") {
    WebDir dir;
    const auto exe = dir.root / "backend" / "build";
    fs::create_directories(exe / "web");
    std::ofstream(exe / "web" / "index.html") << "local";
    const auto dist = dir.root / "frontend" / "dist";
    fs::create_directories(dist);
    std::ofstream(dist / "index.html") << "repo";

    rep::WebRoot web;
    CwdGuard cwd(dir.root);
    REQUIRE(rep::findWebRoot("", exe.string(), web));
    REQUIRE(web.source == "<бинарник>/web");
}

TEST_CASE("статика: корень и каталог отдают index.html") {
    WebDir dir;
    for (const std::string path : {"/", ""}) {
        const auto file = rep::resolveStatic(dir.root.string(), path);
        REQUIRE(file.found);
        REQUIRE(fs::path(file.path).filename() == "index.html");
    }
    const auto nested = rep::resolveStatic(dir.root.string(), "/assets/");
    REQUIRE_FALSE(nested.found);   // index.html в assets нет
    REQUIRE_FALSE(nested.spaFallback);
}

TEST_CASE("статика: файл находится, MIME по расширению") {
    WebDir dir;
    const auto js = rep::resolveStatic(dir.root.string(), "/assets/app-abc123.js");
    REQUIRE(js.found);
    REQUIRE(rep::contentTypeFor(js.path) == "text/javascript; charset=utf-8");

    const auto css = rep::resolveStatic(dir.root.string(), "/assets/app.css");
    REQUIRE(css.found);
    REQUIRE(rep::contentTypeFor(css.path) == "text/css; charset=utf-8");

    const auto svg = rep::resolveStatic(dir.root.string(), "/logo.svg");
    REQUIRE(svg.found);
    REQUIRE(rep::contentTypeFor(svg.path) == "image/svg+xml");

    const auto png = rep::resolveStatic(dir.root.string(), "/no.png");
    REQUIRE_FALSE(png.found);
    REQUIRE_FALSE(png.spaFallback);   // файл с расширением не подменяется страницей
}

TEST_CASE("статика: SPA-маршрут без расширения отдаёт index.html") {
    WebDir dir;
    const auto page = rep::resolveStatic(dir.root.string(), "/reports/dashboard");
    REQUIRE_FALSE(page.found);
    REQUIRE(page.spaFallback);
    REQUIRE(fs::path(page.path).filename() == "index.html");
}

TEST_CASE("статика: отсутствующий файл с расширением — 404, а не страница") {
    WebDir dir;
    const auto page = rep::resolveStatic(dir.root.string(), "/report.html");
    REQUIRE_FALSE(page.found);
    REQUIRE_FALSE(page.spaFallback);
}

TEST_CASE("статика: выход за пределы каталога запрещён") {
    WebDir dir;
    for (const std::string path : {"/../etc/passwd", "/assets/../../etc/passwd", "/..%2f..%2fetc/passwd"}) {
        const auto file = rep::resolveStatic(dir.root.string(), path);
        REQUIRE_FALSE(file.found);
        REQUIRE_FALSE(file.spaFallback);
    }
}

TEST_CASE("статика: кэш — страницы без кэша, хэшированные ассеты надолго") {
    REQUIRE(rep::cacheControlFor("/", "index.html") == "no-cache");
    REQUIRE(rep::cacheControlFor("/report.html", "report.html") == "no-cache");
    REQUIRE(rep::cacheControlFor("/assets/app-abc123.js", "assets/app-abc123.js") ==
            "public, max-age=31536000, immutable");
    REQUIRE(rep::cacheControlFor("/logo.svg", "logo.svg") == "public, max-age=3600");
}

TEST_CASE("статика: имя каталога в логе сокращается до <бинарник>") {
    REQUIRE(rep::describeWebRoot("/opt/rep/web", "/opt/rep") == "<бинарник>/web");
    REQUIRE(rep::describeWebRoot("/var/www/web", "/opt/rep") == "/var/www/web");
}
