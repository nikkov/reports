# Спайк S2: Drogon + PostgreSQL через Conan v1

Дата: 2026-11-05. Задача 0.3 плана.

## Что проверялось

1. Сборка Drogon из Conan v1 (`drogon/1.9.8`) и линковка с Catch2.
2. Асинхронные запросы к PostgreSQL 18.6 (192.168.0.3:32770) с привязкой параметров.
3. `statement_timeout` на уровне соединения.
4. JSON-сериализация результата с типами колонок.
5. Коды ошибок и лимит строк.

## Результаты

| Проверка | Результат |
|---|---|
| `conan install` (первый прогон, с boost) | прерван: boost собирался ~10 мин без пользы |
| `conan install` без boost/brotli | успех, `EXIT=0` |
| `find_package(Drogon CONFIG)` + сборка `rep`, `rep_tests` | успех, `[100%] Built target` |
| Catch2-тесты | 13 кейсов, 30 assertions, 0 падений |
| `GET /api/health` | `{"db":true,"maxRows":50000,"schemaVersion":1,"status":"ok"}` |
| `POST /api/report/data` (период 2026-01-01…2026-03-31) | 24 723 строки, 5,4 МБ JSON, **0,44 с** |
| Сверка `sum(amount)`/`sum(profit)` с SQL | совпадает до единицы (1 356 707 500 / 278 669 375) |
| Привязка параметров | инъекция `2026-01-01'` отклонена валидацией (422), `ops` осталась 540 строк |
| `statement_timeout=2s` на запросе `pg_sleep(5)` | `504 SQL_TIMEOUT` |
| Лимит строк (`REP_MAX_ROWS=100`) | `422 ROW_LIMIT` с числом строк и подсказкой |
| Отсутствующий отчёт | `404 REPORT_NOT_FOUND` |
| Обязательность/тип/лишний параметр | `422 INVALID_PARAMS` с именем параметра |

## Решения, принятые по факту

- `with_boost=False`: boost обязателен только для C++14, у нас C++17. Экономит десятки минут сборки и ~600 МБ.
- `with_brotli=False`: рецепт `drogon/1.9.8` не прокидывает include-пути brotli → сборка падает
  (`fatal error: brotli/decode.h`). Остаётся gzip/deflate. При необходимости brotli —
  отдельная задача с патчем рецепта или системным `libbrotli-dev`.
- `with_profile=False`: профилировщик `coz` не нужен.
- Типы колонок: Drogon не отдаёт OID результата, поэтому тип объявляется в `queries.json`
  (секция `columns`), а при отсутствии — выводится из значения. См. ADR-18 в плане.
- `statement_timeout` задаётся в conninfo через `options='-c statement_timeout=...'` — работает
  без правки пула соединений.

## Артефакты

- `backend/src/{main.cc,env.cc,params.cc,queries.cc}` — HTTP-слой, .env, валидация, компилятор SQL.
- `backend/tests/*` — 13 тестов (в т.ч. `injection payload stays a value`).
- `backend/config/queries.json` — реестр из 4 запросов с типами колонок.
