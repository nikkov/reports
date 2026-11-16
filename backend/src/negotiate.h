// SPNEGO/Kerberos для REST-источников (ADR-122): GSSAPI-обёртка над одним
// контекстом аутентификации. Токены передаются в Authorization: Negotiate.
#pragma once

#include <string>

namespace rep {

class NegotiateClient {
public:
    NegotiateClient();
    ~NegotiateClient();
    NegotiateClient(const NegotiateClient &) = delete;
    NegotiateClient &operator=(const NegotiateClient &) = delete;

    // Первый токен для SPN (например, "HTTP@crm.example"). ccache и krb5Config — пути;
    // окружение меняется только на время вызова GSSAPI и восстанавливается: иначе libpq
    // видит KRB5_CONFIG/CCNAME и пытается GSSAPI-аутентификацию к PostgreSQL (ADR-122).
    bool start(const std::string &spn, const std::string &ccache, const std::string &krb5Config,
               std::string &token, std::string &error);

    // Продолжение по challenge от сервиса (WWW-Authenticate: Negotiate <токен>).
    bool step(const std::string &challenge, std::string &token, bool &complete, std::string &error);

    bool complete() const { return complete_; }

private:
    void *context_ = nullptr; // gss_ctx_id_t
    bool complete_ = false;
};

// Собран ли сервер с GSSAPI: если нет, kerberos-источник отвечает понятной ошибкой.
bool negotiateAvailable();

// Разбор base64 из challenge `WWW-Authenticate: Negotiate <токен>`.
std::string base64Decode(const std::string &value);

// Текст ошибки GSSAPI с подсказкой: при GSS_S_NO_CRED/GSS_S_CREDENTIALS_EXPIRED
// дописывается «получите тикет: kinit …» / «тикет истёк» (ADR-123). Коды — OM_uint32.
std::string kerbErrorText(unsigned major, unsigned minor);

} // namespace rep
