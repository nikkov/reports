// CSV для серверного экспорта (ADR-40): BOM + ';' — так Excel открывает файл без мастера импорта.
#pragma once

#include <string>

#include "xlsx_export.h"

namespace rep {

// Разделитель ';', перевод строки CRLF, кавычки при ';', '"', переводе строки.
// В начало пишется BOM, чтобы Excel не считал UTF-8 однобайтовой кодировкой.
std::string toCsv(const ExportSheet &sheet);

} // namespace rep
