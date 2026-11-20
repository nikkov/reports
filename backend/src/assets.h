// Изображения отчётов (ADR-169): определение формата по сигнатуре и разбор размеров.
// Формат берём из байтов, а не из расширения файла: имя приходит от клиента и доверять ему нельзя.
#pragma once

#include <string>

namespace rep {
namespace assets {

// Растровые форматы, которые принимаем. SVG намеренно нет: он может содержать скрипт.
struct ImageInfo {
    std::string mime;      // image/png, image/jpeg, image/gif, image/webp
    int width = 0;
    int height = 0;
    bool ok = false;
};

// Определяет MIME и размеры картинки по её содержимому. ok=false — формат не поддержан
// или заголовок разобрать не удалось (обрезанный файл, повреждение).
ImageInfo sniffImage(const std::string &bytes);

// Случайный идентификатор рисунка: 16 символов [a-z0-9]. Проверяется тем же шаблоном,
// что и поле src блока image, поэтому перечислять id по порядку нельзя.
std::string newAssetId();

// Проверка id из URL: ровно 16 строчных латинских букв или цифр.
bool isAssetId(const std::string &id);

// Байты в SQL-параметр и обратно. Двоичные данные передаём как hex: драйвер
// принимает только текстовые параметры, а base64 на стороне C++ не нужен.
std::string toHex(const std::string &bytes);
bool fromHex(const std::string &hex, std::string &out);

} // namespace assets
} // namespace rep
