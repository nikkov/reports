# Report Server

Декларативные отчёты: JSON-шаблон → страница с текстом, таблицами (vxe-table) и диаграммами (ECharts).
Бэкенд — C++/Drogon, отдаёт сырые строки; все агрегаты считаются на фронтенде.
Полный план и принятые решения: `../PLAN.md`, `docs/adr.md`.

## Требования

- Linux x86_64, g++ ≥ 12, CMake ≥ 3.25
- Conan **v1** (1.66.0) из venv `~/python_venv` — bare `conan` в PATH это v2, он не подходит (ADR-13)
- Node.js ≥ 20 (nvm: `nvm use 24`)
- PostgreSQL 18 (dev: `192.168.0.3:32770`, БД `reporting`)

## Быстрый старт

```bash
cp backend/.env.example backend/.env     # креды БД (в git не попадает)
./db/init.sh                             # создать reporting + 50k синтетических строк
./scripts/conan install backend -if backend/build --build missing -s build_type=Release
cmake -S backend -B backend/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$PWD/backend/build
cmake --build backend/build -j"$(nproc)"
./scripts/check.sh                       # conan + сборка + ctest (+ фронтенд, когда появится)
```

## Данные

`db/schema.sql` — `sales` (50 000), `finance` (576), `ops` (540).
`db/seed.sql` детерминирован (значения — арифметика от `i`), повторный запуск безопасен.
Период продаж: 2025-10-01 … 2026-03-31. В данных намеренно есть `NULL`-ключи (49 регионов, 23 категории),
нулевые суммы (50), отрицательная прибыль (1221) и одна строка-выброс.

## Скрипты

| Скрипт | Назначение |
|---|---|
| `scripts/env.sh` | source: PATH с venv, креды БД из `backend/.env` |
| `scripts/conan` | обёртка Conan v1 с жёсткой проверкой версии |
| `scripts/check.sh` | полная проверка: версия Conan, запрещённые компоненты, сборка+тесты, валидация шаблонов, фронтенд-тесты |

## Запуск демонстрации

```bash
./scripts/build.sh     # conan install + cmake + ctest + npm ci + build (идемпотентно)
./scripts/demo.sh      # backend :8080 + preview dist :5173, печатает ссылку, Ctrl+C — стоп
```

## Страница отчёта и e2e

```bash
# бэкенд и dev-сервер должны быть запущены
./backend/build/rep &                      # :8080
cd frontend && npm run dev &               # :5173 (проксирует /api на :8080)

open http://127.0.0.1:5173/                                            # список отчётов
open "http://127.0.0.1:5173/report.html?report=sales-summary&from=2026-01-01&to=2026-03-31"
open http://127.0.0.1:5173/report.html?report=params-demo              # все виды параметров
open http://127.0.0.1:5173/spike.html                                  # песочница рендерера
cd frontend && npm run e2e                 # 18 тестов Playwright
REP_E2E=1 ./scripts/check.sh               # всё вместе
```
| `db/init.sh` | создание БД и загрузка синтетики (идемпотентно) |
