# Сторонние компоненты и лицензии

DoD ТЗ §5: только MIT / BSD / Apache-2.0. Проверка — `scripts/check.sh`, ревизия — задача 4.4.

## Frontend (npm, фактические версии)

| Пакет | Версия | Лицензия | Назначение |
|---|---|---|---|
| vue | 3.5.44 | MIT | фреймворк |
| vxe-table | 4.23.14 | MIT | таблицы (только ядро; enterprise-фичи запрещены, ADR-10) |
| echarts | 5.6.0 | Apache-2.0 | диаграммы |
| vite | 5.4.21 | MIT | сборка |
| @vitejs/plugin-vue | 5.x | MIT | плагин Vite |

Требуется также peer-пакет `@vxe-ui/core` (MIT) — без него vxe-table 4.23 не собирается.

## Backend (Conan v1, фактические версии)

| Пакет | Версия | Лицензия | Назначение |
|---|---|---|---|
| drogon | 1.9.8 | MIT | HTTP-сервис |
| trantor | 1.5.21 | BSD-3-Clause | event loop Drogon |
| jsoncpp | 1.9.5 | MIT | JSON |
| openssl | 3.3.x | Apache-2.0 | TLS |
| zlib | 1.3.x | Zlib | сжатие |
| libpq | 15.4 | **PostgreSQL License** | клиент PostgreSQL |
| catch2 | 3.7.1 | **BSL-1.0** | тесты C++ |

## Требуют решения PO (формально вне списка MIT/BSD/Apache)

1. **libpq 15.4 — PostgreSQL License** (permissive, OSI-совместима, BSD-подобна). Замена невозможна:
   это официальный клиент PostgreSQL, без него Drogon ORM не работает. Предлагается считать
   соответствующей требованию DoD.
2. **Catch2 3.7.1 — BSL-1.0** (permissive, требует сохранения текста лицензии). Альтернатива при
   строгом прочтении DoD — `doctest` (MIT), миграция тестов ≈ 1 день (задача 1.3).

Зафиксировать решение в `docs/adr.md` до приёмки Этапа 1.
