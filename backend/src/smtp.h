// Отправка писем для рассылки отчётов (ADR-46): минимальный SMTP-клиент.
// Поддерживает STARTTLS и AUTH LOGIN/PLAIN, вложение в base64. Блокирующий:
// работает в отдельном потоке планировщика, не в цикле событий.
#pragma once

#include <string>

namespace rep {

struct MailConfig {
    std::string host;
    int port = 25;
    std::string user;      // пусто — без авторизации
    std::string password;
    std::string from;      // адрес отправителя
    bool starttls = true;  // если сервер умеет STARTTLS — переключаемся на TLS
};

struct MailMessage {
    std::string from;             // заполняется sendMail из MailConfig.from
    std::string to;               // один получатель (список рассылает планировщик)
    std::string subject;
    std::string text;
    std::string attachmentName;
    std::string attachmentData;   // байты файла
    std::string attachmentType;   // например, text/csv
};

// true — сервер принял сообщение (после ответа 250 на точку).
bool sendMail(const MailConfig &config, const MailMessage &message, std::string &err);

// Утилиты вынесены наружу: их проверяют тесты без сети.
std::string base64Encode(const std::string &data);
std::string mimeHeader(const std::string &utf8);       // =?UTF-8?B?...?=
std::string buildMime(const MailMessage &message);     // полное письмо с границей

} // namespace rep
