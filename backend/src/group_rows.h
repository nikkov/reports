// Подытоги и общий итог в серверном экспорте (ADR-66).
// Клиентский экспорт строит книгу по модели витрины и знает подытоги; серверный читает
// набор данных и раньше выгружал плоскую таблицу. Здесь группировка повторяется один раз
// и на тех же правилах, что на экране: строка «Итого …» на уровне, объявленном в шаблоне,
// общий итог последней строкой, объединения групповых колонок без строк итога своего уровня.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "xlsx_export.h"

namespace rep {

struct GroupLayout {
    std::vector<std::size_t> groupColumns;   // индексы колонок-групп по уровням (0 — верхний)
    std::vector<int> subtotalLevels;         // уровни, где нужна строка «Итого <ключ>»
    std::vector<std::size_t> measureColumns; // что суммировать в итогах
    bool grandTotal = false;
    // collapsible: false — группы всегда раскрыты и в книге: без outline-уровней (ADR-94)
    bool collapsible = true;
    std::string grandLabel = "ИТОГО";
    std::string nullLabel = "(пусто)";
};

// Вставляет подытоги и общий итог, пересчитывает объединения и outline-уровни.
// sheet.rows на входе — детальные строки в порядке SQL; порядок сохраняется.
void buildGroupedSheet(ExportSheet &sheet, const GroupLayout &layout);

// Оставить только детальные строки (CSV — машинный формат, строки итогов ему мешают).
void dropSummaryRows(ExportSheet &sheet);

} // namespace rep
