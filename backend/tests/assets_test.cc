// Определение формата и размеров рисунка по байтам (ADR-169).
#include "assets.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using rep::assets::ImageInfo;
using rep::assets::sniffImage;

namespace {

void putBe32(std::string &s, unsigned v) {
    s.push_back(static_cast<char>((v >> 24) & 0xFF));
    s.push_back(static_cast<char>((v >> 16) & 0xFF));
    s.push_back(static_cast<char>((v >> 8) & 0xFF));
    s.push_back(static_cast<char>(v & 0xFF));
}

void putBe16(std::string &s, unsigned v) {
    s.push_back(static_cast<char>((v >> 8) & 0xFF));
    s.push_back(static_cast<char>(v & 0xFF));
}

void putLe16(std::string &s, unsigned v) {
    s.push_back(static_cast<char>(v & 0xFF));
    s.push_back(static_cast<char>((v >> 8) & 0xFF));
}

void putLe24(std::string &s, unsigned v) {
    s.push_back(static_cast<char>(v & 0xFF));
    s.push_back(static_cast<char>((v >> 8) & 0xFF));
    s.push_back(static_cast<char>((v >> 16) & 0xFF));
}

std::string pngBytes(unsigned w, unsigned h) {
    std::string s("\x89PNG\r\n\x1a\n", 8);
    putBe32(s, 13); // длина IHDR
    s += "IHDR";
    putBe32(s, w);
    putBe32(s, h);
    s += std::string(20, '\0'); // глубина, тип цвета, остальное — для разбора не нужно
    return s;
}

std::string gifBytes(unsigned w, unsigned h) {
    std::string s = "GIF89a";
    putLe16(s, w);
    putLe16(s, h);
    s += std::string(4, '\0');
    return s;
}

std::string jpegBytes(unsigned w, unsigned h) {
    std::string s;
    s.push_back(static_cast<char>(0xFF));
    s.push_back(static_cast<char>(0xD8));
    // APP0 с длиной 16 — пропускается по длине
    s.push_back(static_cast<char>(0xFF));
    s.push_back(static_cast<char>(0xE0));
    putBe16(s, 16);
    s += std::string(14, '\0');
    // SOF0: длина, точность, высота, ширина, число компонент
    s.push_back(static_cast<char>(0xFF));
    s.push_back(static_cast<char>(0xC0));
    putBe16(s, 17);
    s.push_back(static_cast<char>(8));
    putBe16(s, h);
    putBe16(s, w);
    s.push_back(static_cast<char>(3));
    s += std::string(9, '\0');
    return s;
}

std::string webpVp8xBytes(unsigned w, unsigned h) {
    std::string s = "RIFF";
    putLe16(s, 0);
    putLe16(s, 0);
    s += "WEBP";
    s += "VP8X";
    s += std::string(4, '\0'); // размер чанка
    s += std::string(4, '\0'); // флаги и резерв
    putLe24(s, w - 1);
    putLe24(s, h - 1);
    return s;
}

} // namespace

TEST_CASE("png: формат и размеры из IHDR") {
    const ImageInfo info = sniffImage(pngBytes(320, 80));
    REQUIRE(info.ok);
    REQUIRE(info.mime == "image/png");
    REQUIRE(info.width == 320);
    REQUIRE(info.height == 80);
}

TEST_CASE("jpeg: размеры из кадра после APP0") {
    const ImageInfo info = sniffImage(jpegBytes(640, 480));
    REQUIRE(info.ok);
    REQUIRE(info.mime == "image/jpeg");
    REQUIRE(info.width == 640);
    REQUIRE(info.height == 480);
}

TEST_CASE("gif: размеры из логического экрана") {
    const ImageInfo info = sniffImage(gifBytes(48, 48));
    REQUIRE(info.ok);
    REQUIRE(info.mime == "image/gif");
    REQUIRE(info.width == 48);
    REQUIRE(info.height == 48);
}

TEST_CASE("webp VP8X: размеры холста на единицу больше записанного") {
    const ImageInfo info = sniffImage(webpVp8xBytes(200, 100));
    REQUIRE(info.ok);
    REQUIRE(info.mime == "image/webp");
    REQUIRE(info.width == 200);
    REQUIRE(info.height == 100);
}

TEST_CASE("svg и мусор не принимаются") {
    const std::string svg = "<svg xmlns=\"http://www.w3.org/2000/svg\"><script>1</script></svg>";
    REQUIRE_FALSE(sniffImage(svg).ok);
    REQUIRE_FALSE(sniffImage("просто текст").ok);
    REQUIRE_FALSE(sniffImage(std::string()).ok);
}

TEST_CASE("обрезанный заголовок не угадывается") {
    std::string truncated = pngBytes(100, 100);
    truncated.resize(20); // нет высоты в IHDR
    REQUIRE_FALSE(sniffImage(truncated).ok);
}

TEST_CASE("id рисунка: 16 символов [a-z0-9] и проверка") {
    const std::string id = rep::assets::newAssetId();
    REQUIRE(id.size() == 16);
    REQUIRE(rep::assets::isAssetId(id));
    REQUIRE_FALSE(rep::assets::isAssetId("ABC"));
    REQUIRE_FALSE(rep::assets::isAssetId("0123456789abcde"));   // 15
    REQUIRE_FALSE(rep::assets::isAssetId("0123456789abcdefg")); // 17
    REQUIRE_FALSE(rep::assets::isAssetId("0123456789ABCDEF"));  // верхний регистр
}

TEST_CASE("идентификаторы не повторяются") {
    REQUIRE(rep::assets::newAssetId() != rep::assets::newAssetId());
}
