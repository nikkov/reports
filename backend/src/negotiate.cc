#include "negotiate.h"

#include "env.h"

#include <cstring>
#include <vector>

#ifdef REP_HAVE_GSSAPI
#include <gssapi/gssapi.h>
#endif

namespace rep {

std::string base64Decode(const std::string &value) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::string out;
    int buffer = 0;
    int bits = 0;
    for (char c : value) {
        if (c == '=') break;
        const int v = val(c);
        if (v < 0) continue; // переносы строк и пробелы пропускаем
        buffer = (buffer << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((buffer >> bits) & 0xFF);
        }
    }
    return out;
}

#ifdef REP_HAVE_GSSAPI

namespace {

// Ошибка GSSAPI в виде двух кодов и текста: иначе в логах остаются голые числа.
std::string describe(OM_uint32 major, OM_uint32 minor) {
    OM_uint32 majStat = 0;
    OM_uint32 minStat = 0;
    OM_uint32 ctx = 0;
    gss_buffer_desc msg = GSS_C_EMPTY_BUFFER;
    std::string text;
    gss_display_status(&minStat, major, GSS_C_GSS_CODE, GSS_C_NO_OID, &ctx, &msg);
    if (msg.length) text.assign(static_cast<char *>(msg.value), msg.length);
    gss_release_buffer(&minStat, &msg);
    if (minor != 0) {
        msg = GSS_C_EMPTY_BUFFER;
        gss_display_status(&minStat, minor, GSS_C_MECH_CODE, GSS_C_NO_OID, &ctx, &msg);
        if (msg.length) {
            text += " / ";
            text.append(static_cast<char *>(msg.value), msg.length);
        }
        gss_release_buffer(&minStat, &msg);
    }
    return text.empty() ? " ошибка GSSAPI" : text;
}

} // namespace

bool negotiateAvailable() { return true; }

NegotiateClient::NegotiateClient() = default;

NegotiateClient::~NegotiateClient() {
    if (context_ != nullptr) {
        OM_uint32 minor = 0;
        gss_delete_sec_context(&minor, reinterpret_cast<gss_ctx_id_t *>(&context_),
                               GSS_C_NO_BUFFER);
    }
}


namespace {

// Временная установка переменной окружения: восстанавливает прежнее значение на выходе.
class ScopedEnv {
public:
    ScopedEnv(const char *name, const std::string &value) : name_(name) {
        const char *old = std::getenv(name);
        if (old != nullptr) {
            had_ = true;
            old_ = old;
        }
        if (!value.empty()) {
            rep::setEnvVar(name, value);
        }
    }
    ~ScopedEnv() {
        if (!set_ && old_.empty() && !had_) return;
        if (had_) {
            rep::setEnvVar(name_, old_);
        } else {
            rep::unsetEnvVar(name_);
        }
    }
    void touch() { set_ = true; }

private:
    const char *name_;
    std::string old_;
    bool had_ = false;
    bool set_ = false;
};

} // namespace

bool NegotiateClient::start(const std::string &spn, const std::string &ccache,
                            const std::string &krb5Config, std::string &token,
                            std::string &error) {
    // Окружение — только на время этого вызова (см. комментарий в negotiate.h)
    ScopedEnv envConfig("KRB5_CONFIG", krb5Config);
    // KRB5CCNAME понимает и путь, и FILE:/путь; приводим к явному виду
    const std::string ccacheName =
        (!ccache.empty() && ccache.rfind("FILE:", 0) != 0 && ccache[0] == '/')
            ? ("FILE:" + ccache)
            : ccache;
    ScopedEnv envCcache("KRB5CCNAME", ccacheName);
    if (!ccache.empty()) envCcache.touch();
    if (!krb5Config.empty()) envConfig.touch();
    OM_uint32 major = 0;
    OM_uint32 minor = 0;
    gss_name_t target = GSS_C_NO_NAME;
    gss_buffer_desc nameBuf;
    nameBuf.value = const_cast<char *>(spn.c_str());
    nameBuf.length = spn.size();
    // "HTTP@host" — hostbased service name
    major = gss_import_name(&minor, &nameBuf, GSS_C_NT_HOSTBASED_SERVICE, &target);
    if (major != GSS_S_COMPLETE) {
        error = "SPN «" + spn + "»: " + describe(major, minor);
        return false;
    }
    gss_ctx_id_t ctx = GSS_C_NO_CONTEXT;
    gss_buffer_desc out = GSS_C_EMPTY_BUFFER;
    OM_uint32 flags = 0;
    major = gss_init_sec_context(&minor, GSS_C_NO_CREDENTIAL, &ctx, target, GSS_C_NO_OID,
                                 GSS_C_MUTUAL_FLAG, 0, GSS_C_NO_CHANNEL_BINDINGS, GSS_C_NO_BUFFER,
                                 nullptr, &out, &flags, nullptr);
    gss_release_name(&minor, &target);
    context_ = ctx;
    if (major != GSS_S_COMPLETE && major != GSS_S_CONTINUE_NEEDED) {
        error = "Kerberos: " + describe(major, minor);
        return false;
    }
    complete_ = (major == GSS_S_COMPLETE);
    token.assign(out.length ? static_cast<char *>(out.value) : "", out.length);
    gss_release_buffer(&minor, &out);
    return true;
}

bool NegotiateClient::step(const std::string &challenge, std::string &token, bool &complete,
                           std::string &error) {
    OM_uint32 major = 0;
    OM_uint32 minor = 0;
    gss_buffer_desc in;
    in.value = const_cast<char *>(challenge.data());
    in.length = challenge.size();
    gss_buffer_desc out = GSS_C_EMPTY_BUFFER;
    OM_uint32 flags = 0;
    gss_ctx_id_t ctx = reinterpret_cast<gss_ctx_id_t>(context_);
    major = gss_init_sec_context(&minor, GSS_C_NO_CREDENTIAL, &ctx, GSS_C_NO_NAME, GSS_C_NO_OID,
                                 GSS_C_MUTUAL_FLAG, 0, GSS_C_NO_CHANNEL_BINDINGS, &in, nullptr, &out,
                                 &flags, nullptr);
    context_ = ctx;
    if (major != GSS_S_COMPLETE && major != GSS_S_CONTINUE_NEEDED) {
        error = "Kerberos (продолжение): " + describe(major, minor);
        return false;
    }
    complete_ = (major == GSS_S_COMPLETE);
    complete = complete_;
    token.assign(out.length ? static_cast<char *>(out.value) : "", out.length);
    gss_release_buffer(&minor, &out);
    return true;
}

#else

bool negotiateAvailable() { return false; }

NegotiateClient::NegotiateClient() = default;
NegotiateClient::~NegotiateClient() = default;

bool NegotiateClient::start(const std::string &, const std::string &, const std::string &,
                            std::string &, std::string &error) {
    error = "сервер собран без GSSAPI: Kerberos-аутентификация REST недоступна";
    return false;
}

bool NegotiateClient::step(const std::string &, std::string &, bool &, std::string &error) {
    error = "сервер собран без GSSAPI: Kerberos-аутентификация REST недоступна";
    return false;
}

#endif

} // namespace rep
