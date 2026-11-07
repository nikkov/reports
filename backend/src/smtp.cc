#include "smtp.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <cstring>
#include <ctime>
#include <random>
#include <sstream>
#include <vector>

namespace rep {
namespace {

const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Lines(const std::string &data, std::size_t width) {
    const std::string flat = base64Encode(data);
    if (!width) return flat;
    std::string out;
    for (std::size_t i = 0; i < flat.size(); i += width) {
        out += flat.substr(i, width);
        out += "\r\n";
    }
    return out;
}

// Точка в начале строки должна быть удвоена (RFC 5321, 4.5.2).
std::string dotStuff(const std::string &body) {
    std::string out;
    out.reserve(body.size() + 16);
    bool atLineStart = true;
    for (char c : body) {
        if (atLineStart && c == '.') out.push_back('.');
        out.push_back(c);
        atLineStart = (c == '\n');
    }
    return out;
}

class Connection {
  public:
    ~Connection() { close(); }

    bool open(const std::string &host, int port, int timeoutSec, std::string &err) {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo *res = nullptr;
        const std::string portText = std::to_string(port);
        if (getaddrinfo(host.c_str(), portText.c_str(), &hints, &res) != 0) {
            err = "не удалось разрешить адрес " + host;
            return false;
        }
        for (addrinfo *it = res; it; it = it->ai_next) {
            fd_ = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
            if (fd_ < 0) continue;
            timeval tv{timeoutSec, 0};
            setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
            if (::connect(fd_, it->ai_addr, it->ai_addrlen) == 0) break;
            ::close(fd_);
            fd_ = -1;
        }
        freeaddrinfo(res);
        if (fd_ < 0) {
            err = "не удалось подключиться к " + host + ":" + portText;
            return false;
        }
        return true;
    }

    bool startTls(const std::string &host, std::string &err) {
        SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx) {
            err = "не удалось создать TLS-контекст";
            return false;
        }
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr); // доверие — на уровне сети/прокси
        ssl_ = SSL_new(ctx);
        SSL_CTX_free(ctx);
        if (!ssl_) {
            err = "не удалось создать TLS-соединение";
            return false;
        }
        SSL_set_fd(ssl_, fd_);
        SSL_set_tlsext_host_name(ssl_, host.c_str());
        if (SSL_connect(ssl_) != 1) {
            err = "ошибка STARTTLS: " + lastSslError();
            return false;
        }
        return true;
    }

    bool write(const std::string &data, std::string &err) {
        std::size_t sent = 0;
        while (sent < data.size()) {
            ssize_t n = ssl_ ? SSL_write(ssl_, data.data() + sent, static_cast<int>(data.size() - sent))
                             : ::send(fd_, data.data() + sent, data.size() - sent, 0);
            if (n <= 0) {
                err = ssl_ ? lastSslError() : std::string(std::strerror(errno));
                return false;
            }
            sent += static_cast<std::size_t>(n);
        }
        return true;
    }

    // Читает ответ SMTP целиком: «250-…» продолжается, «250 …» завершает.
    bool readReply(int &code, std::string &text, std::string &err) {
        text.clear();
        for (;;) {
            const auto line = readLine(err);
            if (line.empty() && !err.empty()) return false;
            text += line;
            if (line.size() < 4) {
                err = "короткий ответ SMTP: «" + line + "»";
                return false;
            }
            code = std::atoi(line.substr(0, 3).c_str()); // код берём из первой строки ответа
            if (line[3] == ' ') return true;
            if (line[3] != '-') {
                err = "неожиданный ответ SMTP: «" + line + "»";
                return false;
            }
        }
    }

    void close() {
        if (ssl_) {
            SSL_shutdown(ssl_);
            SSL_free(ssl_);
            ssl_ = nullptr;
        }
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

  private:
    std::string readLine(std::string &err) {
        std::string line;
        char c = 0;
        while (readChar(c)) {
            if (c == '\n') break;
            if (c != '\r') line.push_back(c);
        }
        if (line.empty() && !err.empty()) return {};
        return line;
    }

    bool readChar(char &c) {
        if (ssl_) {
            const int n = SSL_read(ssl_, &c, 1);
            if (n == 1) return true;
            err_ = lastSslError();
            return false;
        }
        const ssize_t n = ::recv(fd_, &c, 1, 0);
        if (n == 1) return true;
        err_ = std::strerror(errno);
        return false;
    }

    std::string lastSslError() {
        const unsigned long code = ERR_get_error();
        if (!code) return err_.empty() ? "соединение закрыто" : err_;
        char buffer[256];
        ERR_error_string_n(code, buffer, sizeof(buffer));
        return buffer;
    }

    int fd_ = -1;
    SSL *ssl_ = nullptr;
    std::string err_;
};

bool expect(Connection &conn, int wanted, std::string &err, const std::string &step) {
    int code = 0;
    std::string text;
    if (!conn.readReply(code, text, err)) return false;
    if (code != wanted) {
        err = step + ": SMTP ответил " + std::to_string(code) + " «" + text + "»";
        return false;
    }
    return true;
}

bool command(Connection &conn, const std::string &line, int wanted, std::string &err, const std::string &step) {
    if (!conn.write(line + "\r\n", err)) return false;
    return expect(conn, wanted, err, step);
}

} // namespace

std::string base64Encode(const std::string &data) {
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    std::size_t i = 0;
    while (i + 2 < data.size()) {
        const unsigned int v = (static_cast<unsigned char>(data[i]) << 16) |
                               (static_cast<unsigned char>(data[i + 1]) << 8) |
                               static_cast<unsigned char>(data[i + 2]);
        out.push_back(kAlphabet[(v >> 18) & 0x3f]);
        out.push_back(kAlphabet[(v >> 12) & 0x3f]);
        out.push_back(kAlphabet[(v >> 6) & 0x3f]);
        out.push_back(kAlphabet[v & 0x3f]);
        i += 3;
    }
    const std::size_t rest = data.size() - i;
    if (rest == 1) {
        const unsigned int v = static_cast<unsigned char>(data[i]) << 16;
        out.push_back(kAlphabet[(v >> 18) & 0x3f]);
        out.push_back(kAlphabet[(v >> 12) & 0x3f]);
        out += "==";
    } else if (rest == 2) {
        const unsigned int v = (static_cast<unsigned char>(data[i]) << 16) |
                               (static_cast<unsigned char>(data[i + 1]) << 8);
        out.push_back(kAlphabet[(v >> 18) & 0x3f]);
        out.push_back(kAlphabet[(v >> 12) & 0x3f]);
        out.push_back(kAlphabet[(v >> 6) & 0x3f]);
        out.push_back('=');
    }
    return out;
}

std::string mimeHeader(const std::string &utf8) {
    bool ascii = true;
    for (char c : utf8) {
        if (static_cast<unsigned char>(c) > 127) {
            ascii = false;
            break;
        }
    }
    if (ascii) return utf8;
    // длинные заголовки режем на части по 45 байт, чтобы не превысить 76 символов
    std::string out;
    std::size_t pos = 0;
    while (pos < utf8.size()) {
        std::size_t take = std::min<std::size_t>(45, utf8.size() - pos);
        // не разрезаем многобайтовый символ
        while (take > 1 && (static_cast<unsigned char>(utf8[pos + take]) & 0xC0) == 0x80) --take;
        if (!out.empty()) out += "\r\n ";
        out += "=?UTF-8?B?" + base64Encode(utf8.substr(pos, take)) + "?=";
        pos += take;
    }
    return out;
}

std::string buildMime(const MailMessage &message) {
    static const char *alphabet = "0123456789abcdef";
    std::mt19937 rng(std::random_device{}());
    std::string boundary = "rep-";
    for (int i = 0; i < 16; ++i) boundary.push_back(alphabet[rng() % 16]);

    std::ostringstream out;
    out << "From: " << message.from << "\r\n";
    out << "To: " << message.to << "\r\n";
    out << "Subject: " << mimeHeader(message.subject) << "\r\n";
    out << "MIME-Version: 1.0\r\n";
    out << "Date: " << [] {
        char buffer[64];
        std::time_t now = std::time(nullptr);
        std::tm local = *std::localtime(&now);
        std::strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S %z", &local);
        return std::string(buffer);
    }() << "\r\n";
    out << "Content-Type: multipart/mixed; boundary=\"" << boundary << "\"\r\n\r\n";
    out << "--" << boundary << "\r\n";
    out << "Content-Type: text/plain; charset=utf-8\r\n";
    out << "Content-Transfer-Encoding: 8bit\r\n\r\n";
    out << dotStuff(message.text) << "\r\n\r\n";

    if (!message.attachmentData.empty()) {
        const std::string type = message.attachmentType.empty() ? "application/octet-stream" : message.attachmentType;
        out << "--" << boundary << "\r\n";
        out << "Content-Type: " << type << "; name=\"" << mimeHeader(message.attachmentName) << "\"\r\n";
        out << "Content-Transfer-Encoding: base64\r\n";
        out << "Content-Disposition: attachment; filename=\"" << mimeHeader(message.attachmentName) << "\"\r\n\r\n";
        out << base64Lines(message.attachmentData, 76) << "\r\n";
    }
    out << "--" << boundary << "--\r\n";
    return out.str();
}

bool sendMail(const MailConfig &config, const MailMessage &message, std::string &err) {
    if (config.host.empty() || config.from.empty() || message.to.empty()) {
        err = "не заданы хост SMTP, отправитель или получатель";
        return false;
    }
    Connection conn;
    if (!conn.open(config.host, config.port, 20, err)) return false;

    int code = 0;
    std::string text;
    if (!conn.readReply(code, text, err) || code != 220) {
        err = "SMTP не поздоровался: " + text;
        return false;
    }

    const std::string hostName = "rep.local";
    if (!command(conn, "EHLO " + hostName, 250, err, "EHLO")) return false;
    const bool supportsStartTls = text.find("STARTTLS") != std::string::npos;

    if (config.starttls && supportsStartTls) {
        if (!command(conn, "STARTTLS", 220, err, "STARTTLS")) return false;
        if (!conn.startTls(config.host, err)) return false;
        if (!command(conn, "EHLO " + hostName, 250, err, "EHLO после TLS")) return false;
    }

    if (!config.user.empty()) {
        const bool plain = text.find("AUTH") != std::string::npos && text.find("PLAIN") != std::string::npos;
        if (plain) {
            const std::string token = base64Encode("\0" + config.user + "\0" + config.password);
            if (!command(conn, "AUTH PLAIN " + token, 235, err, "AUTH PLAIN")) return false;
        } else {
            if (!command(conn, "AUTH LOGIN", 334, err, "AUTH LOGIN")) return false;
            if (!command(conn, base64Encode(config.user), 334, err, "AUTH пользователь")) return false;
            if (!command(conn, base64Encode(config.password), 235, err, "AUTH пароль")) return false;
        }
    }

    if (!command(conn, "MAIL FROM:<" + config.from + ">", 250, err, "MAIL FROM")) return false;
    if (!command(conn, "RCPT TO:<" + message.to + ">", 250, err, "RCPT TO")) return false;
    if (!command(conn, "DATA", 354, err, "DATA")) return false;

    MailMessage filled = message;
    filled.from = config.from;
    const std::string body = buildMime(filled);
    if (!conn.write(body, err)) return false;
    if (!conn.write("\r\n.\r\n", err)) return false;
    if (!conn.readReply(code, text, err) || code != 250) {
        err = "письмо не принято: " + text;
        return false;
    }
    conn.write("QUIT\r\n", err);
    conn.close();
    return true;
}

} // namespace rep
