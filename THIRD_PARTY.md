# Сторонние компоненты и лицензии

Требование DoD (ТЗ §5): лицензии MIT / BSD / Apache-2.0, список зафиксирован в репозитории,
коммерческих компонентов нет.

Таблица собрана из фактически установленных пакетов, а не из документации:

```bash
node scripts/licenses.mjs          # таблица + вердикт по allowlist
node scripts/licenses.mjs --json   # то же в JSON
```

Шаг включён в `scripts/check.sh`, поэтому лицензия вне allowlist валит общую проверку.

## Прямые зависимости

### Frontend (npm, `frontend/package.json`)

| Пакет | Версия | Лицензия | Назначение |
|---|---|---|---|
| vue | 3.5.44 | MIT | фреймворк |
| echarts | 5.6.0 | Apache-2.0 | диаграммы |
| ajv | 8.20.0 | MIT | валидация шаблонов по JSON Schema |
| vite | 5.4.21 | MIT | сборка |
| @vitejs/plugin-vue | 5.2.4 | MIT | плагин Vite для SFC |
| vitest | 2.1.9 | MIT | юнит-тесты |
| @playwright/test | 1.64.1 | Apache-2.0 | e2e |
| fast-check | 4.10.2 | MIT | property-тесты движка группировок |

### Backend (Conan v1, `backend/conanfile.txt`)

| Пакет | Версия | Лицензия | Назначение |
|---|---|---|---|
| drogon | 1.9.8 | MIT | HTTP-сервис, ORM |
| catch2 | 3.7.1 | BSL-1.0 ⚠ | тесты C++ |

## Транзитивные зависимости, попадающие в сборку

| Пакет | Версия | Лицензия | Откуда |
|---|---|---|---|
| trantor | 1.5.21 | BSD-3-Clause | event loop Drogon |
| jsoncpp | 1.9.5 | MIT | JSON в Drogon |
| openssl | 3.3.2 | Apache-2.0 | TLS в Drogon |
| zlib | 1.3.1 | Zlib ⚠ | gzip/deflate в Drogon |
| libpq | 15.4 | PostgreSQL ⚠ | клиент PostgreSQL для Drogon ORM |

Остальные пакеты в кэше Conan (`boost`, `brotli`, `sqlite3`, `mysql` и т. п.) к этому проекту
не относятся: в рецепте отключены опциями `drogon:with_boost=False`, `with_brotli=False`,
`with_sqlite=False`, `with_mysql=False`, `with_redis=False`.

## Отклонения от дословного списка DoD

Три компонента permissive, но их лицензии не называются «MIT / BSD / Apache-2.0» дословно.
Все OSI-совместимы и не ограничивают распространение.

| Компонент | Лицензия | Почему нельзя заменить | Решение |
|---|---|---|---|
| libpq 15.4 | PostgreSQL License (BSD-подобная) | официальный клиент PostgreSQL, без него не работает Drogon ORM | принято постановщиком |
| catch2 3.7.1 | BSL-1.0 | permissive, требует сохранить текст лицензии; альтернатива `doctest` (MIT) — миграция ≈ 1 день | принято постановщиком |
| zlib 1.3.1 | Zlib | транзитивная зависимость Drogon для gzip/deflate; отключается только вместе с HTTP-сжатием | принято постановщиком 2026-11-05 |

Основание: все три — permissive без copyleft-обязательств; libpq и zlib не выбираются разработчиком
(их навязывает Drogon), замена Catch2 не даёт выигрыша. DoD трактуется как «только permissive-лицензии»,
а не как буквальный перечень трёх имён.

**Статус: все три отклонения приняты постановщиком 2026-11-05** (libpq и catch2 — ранее, zlib — по итогам
ревизии). Блокирующих лицензионных вопросов к приёмке не остаётся.

## Коммерческие компоненты

Исключены и проверяются автоматически (`scripts/check.sh`, шаг «Запрещённые коммерческие/enterprise-компоненты»):

`vxe-table`, `vxe-table-pro`, `@vxe-ui/*`, `aggregateConfig`, `ag-grid`, `handsontable`, `ag-studio`, `企业版`.

Таблицы рендерит собственный компонент `frontend/src/components/ReportTable.vue` (ADR-20): vxe-table
удалён после замера — 20 000 строк с `mergeCells` и виртуальным скроллом давали >60 с, свой рендерер
50 000 строк — 229 мс.

## Как обновлять

1. `node scripts/licenses.mjs` — фактическая таблица и вердикт.
2. Лицензия вне allowlist → либо заменить пакет, либо зафиксировать отклонение в таблице выше
   и в `DEVIATIONS` внутри `scripts/licenses.mjs`.
3. `REP_E2E=1 ./scripts/check.sh` — полная проверка.
