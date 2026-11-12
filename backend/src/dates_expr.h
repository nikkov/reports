// Арифметика дат в default (ADR-92) — та же грамматика, что у клиента
// (frontend/src/template/dates.js). Сервер проверяет синтаксис при валидации шаблона,
// чтобы опечатка в шаблоне была ошибкой сохранения, а не пустым значением на экране.
//
//   *            — текущий момент
//   today(*)     — сегодня, 00:00
//   week(*)      — понедельник текущей недели, 00:00
//   month(*)     — первое число месяца, 00:00
//   quarter(*)   — первое число месяца квартала, 00:00
//   year(*)      — 1 января, 00:00
// плюс цепочка смещений +N/-N с единицами h, d, m, y.

#ifndef REP_DATES_EXPR_H
#define REP_DATES_EXPR_H

#include <string>

namespace rep {

// Похоже ли значение на выражение: начинается с «*» или содержит «(»
// (константы вида 2026-03-01 выражением не считаются).
bool LooksLikeDateExpr(const std::string &value);

// Разбор синтаксиса. false — выражение не разобралось, error объясняет место.
bool CheckDateExprSyntax(const std::string &expr, std::string &error);

} // namespace rep

#endif // REP_DATES_EXPR_H
