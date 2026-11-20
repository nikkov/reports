#include "assets.h"

#include <cstdint>
#include <random>

namespace rep {
namespace assets {
namespace {

unsigned char at(const std::string &b, std::size_t i) {
    return i < b.size() ? static_cast<unsigned char>(b[i]) : 0u;
}

std::uint32_t be32(const std::string &b, std::size_t i) {
    return (std::uint32_t(at(b, i)) << 24) | (std::uint32_t(at(b, i + 1)) << 16) |
           (std::uint32_t(at(b, i + 2)) << 8) | std::uint32_t(at(b, i + 3));
}

std::uint16_t be16(const std::string &b, std::size_t i) {
    return static_cast<std::uint16_t>((at(b, i) << 8) | at(b, i + 1));
}

std::uint16_t le16(const std::string &b, std::size_t i) {
    return static_cast<std::uint16_t>(at(b, i) | (at(b, i + 1) << 8));
}

std::uint32_t le24(const std::string &b, std::size_t i) {
    return std::uint32_t(at(b, i)) | (std::uint32_t(at(b, i + 1)) << 8) |
           (std::uint32_t(at(b, i + 2)) << 16);
}

bool startsWith(const std::string &b, const char *sig, std::size_t n) {
    if (b.size() < n) return false;
    for (std::size_t i = 0; i < n; ++i) {
        if (b[i] != sig[i]) return false;
    }
    return true;
}

// PNG: сигнатура 8 байт, затем чанк IHDR — ширина и высота первыми двумя полями.
ImageInfo png(const std::string &b) {
    ImageInfo info;
    if (b.size() < 24 || !startsWith(b, "\x89PNG\r\n\x1a\n", 8)) return info;
    if (b.compare(12, 4, "IHDR") != 0) return info;
    info.mime = "image/png";
    info.width = static_cast<int>(be32(b, 16));
    info.height = static_cast<int>(be32(b, 20));
    info.ok = info.width > 0 && info.height > 0;
    return info;
}

// JPEG: идём по маркерам до кадра (SOFn), в нём высота и ширина. Пропускаем сегменты
// с длиной; маркеры без длины (RST, SOI, EOI) обрабатываются отдельно.
ImageInfo jpeg(const std::string &b) {
    ImageInfo info;
    if (b.size() < 4 || at(b, 0) != 0xFF || at(b, 1) != 0xD8) return info;
    std::size_t i = 2;
    while (i + 1 < b.size()) {
        if (at(b, i) != 0xFF) {
            ++i;
            continue;
        }
        const unsigned char marker = at(b, i + 1);
        if (marker == 0xFF) { // заполнитель
            ++i;
            continue;
        }
        if (marker == 0xD8 || marker == 0xD9 || (marker >= 0xD0 && marker <= 0xD7)) {
            i += 2;
            continue;
        }
        const std::size_t len = be16(b, i + 2);
        if (len < 2 || i + 2 + len > b.size()) return info;
        const bool isSof = (marker >= 0xC0 && marker <= 0xCF) && marker != 0xC4 && marker != 0xC8 &&
                           marker != 0xCC;
        if (isSof) {
            if (len < 7) return info;
            info.mime = "image/jpeg";
            info.height = static_cast<int>(be16(b, i + 5));
            info.width = static_cast<int>(be16(b, i + 7));
            info.ok = info.width > 0 && info.height > 0;
            return info;
        }
        i += 2 + len;
    }
    return info;
}

// GIF: размеры — первые два поля после сигнатуры, little-endian.
ImageInfo gif(const std::string &b) {
    ImageInfo info;
    if (b.size() < 10) return info;
    if (!startsWith(b, "GIF87a", 6) && !startsWith(b, "GIF89a", 6)) return info;
    info.mime = "image/gif";
    info.width = le16(b, 6);
    info.height = le16(b, 8);
    info.ok = info.width > 0 && info.height > 0;
    return info;
}

// WebP: RIFF-контейнер; размеры лежат в первом чанке, вид зависит от кодека:
// VP8X — холст (24 бита, минус единица), VP8L — 14 бит каждого измерения,
// VP8 — кадр с сигнатурой 9d 01 2a.
ImageInfo webp(const std::string &b) {
    ImageInfo info;
    if (b.size() < 30 || !startsWith(b, "RIFF", 4) || b.compare(8, 4, "WEBP") != 0) return info;
    const std::string chunk = b.substr(12, 4);
    if (chunk == "VP8X") {
        info.width = static_cast<int>(le24(b, 24)) + 1;
        info.height = static_cast<int>(le24(b, 27)) + 1;
    } else if (chunk == "VP8L") {
        if (at(b, 20) != 0x2F) return info;
        const std::uint32_t bits = static_cast<std::uint32_t>(at(b, 21)) |
                                   (static_cast<std::uint32_t>(at(b, 22)) << 8) |
                                   (static_cast<std::uint32_t>(at(b, 23)) << 16) |
                                   (static_cast<std::uint32_t>(at(b, 24)) << 24);
        info.width = static_cast<int>((bits & 0x3FFFu) + 1);
        info.height = static_cast<int>(((bits >> 14) & 0x3FFFu) + 1);
    } else if (chunk == "VP8 ") {
        if (at(b, 23) != 0x9D || at(b, 24) != 0x01 || at(b, 25) != 0x2A) return info;
        info.width = static_cast<int>(le16(b, 26) & 0x3FFFu);
        info.height = static_cast<int>(le16(b, 28) & 0x3FFFu);
    } else {
        return info;
    }
    info.mime = "image/webp";
    info.ok = info.width > 0 && info.height > 0;
    return info;
}

} // namespace

ImageInfo sniffImage(const std::string &bytes) {
    ImageInfo info = png(bytes);
    if (info.ok) return info;
    info = jpeg(bytes);
    if (info.ok) return info;
    info = gif(bytes);
    if (info.ok) return info;
    return webp(bytes);
}

std::string newAssetId() {
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, 35);
    std::string id;
    id.reserve(16);
    for (int i = 0; i < 16; ++i) id.push_back(alphabet[dist(rng)]);
    return id;
}

std::string toHex(const std::string &bytes) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (unsigned char ch : bytes) {
        out.push_back(digits[ch >> 4]);
        out.push_back(digits[ch & 0x0F]);
    }
    return out;
}

bool fromHex(const std::string &hex, std::string &out) {
    if (hex.size() % 2 != 0) return false;
    out.clear();
    out.reserve(hex.size() / 2);
    auto val = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const int hi = val(hex[i]);
        const int lo = val(hex[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out.push_back(static_cast<char>((hi << 4) | lo));
    }
    return true;
}

bool isAssetId(const std::string &id) {
    if (id.size() != 16) return false;
    for (unsigned char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (!ok) return false;
    }
    return true;
}

} // namespace assets
} // namespace rep
