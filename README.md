# Report Server — версия 1.12.0

Принята постановщиком 2026-11-05. Шаблон самодостаточен: оформление, форматы, правила подсветки,
SQL наборов данных и SQL справочников живут в нём. Есть админка `/admin.html`, экспорт в Excel (числа с форматами, объединения, подытоги), печать
с повтором шапки на страницах, переходы из групп в детальные отчёты, каталог с папками, тегами и избранным,
правка шаблонов идёт через черновик с явной публикацией, данные отчёта кэшируются (TTL, ETag),
один шаблон может объявлять несколько наборов данных — каждый блок читает свой, группировку
(`GROUP BY`) можно считать на сервере, а любой отчёт выгружается в `.xlsx` сервером — без браузера,
с учётом агрегации и с листом параметров.
Состав и ограничения — `docs/dod.md`, история — `CHANGELOG.md`.

Декларативные отчёты: JSON-шаблон → страница с текстом, таблицами (собственный рендерер, ADR-20) и диаграммами (ECharts).
Шаблоны хранятся в PostgreSQL (ADR-21), параметры-справочники — ADR-22.
Бэкенд — C++/Drogon, отдаёт сырые строки; все агрегаты считаются на фронтенде.
Полный план и принятые решения: `../PLAN.md`, `docs/decisions.md`.

## Документация

| Файл | О чём |
|---|---|
| `docs/how-to-add-report.md` | пошагово: SQL → шаблон → валидатор → push → рестарт → проверка |
| `docs/template-reference.md` | справочник всех полей шаблона, включая `source`/`multiple`/`dependsOn` |
| `docs/expressions.md` | грамматика вычисляемых колонок, области видимости, null |
| `docs/decisions.md` | принятые решения (ADR-1…ADR-22) |
| `docs/styling-design.md` | план версии 1.1: стили таблиц из шаблона (шрифты, цвета, ширины, высоты зон) |
| `docs/styling-examples.md` | готовые примеры оформления: пресеты и собственные цвета |
| `docs/conditional-formatting-design.md` | план версии 1.2: условное форматирование по правилам |
| `docs/admin.md` | админка: создание и правка отчётов, редактор, история, ограничения |
| `docs/admin-design.md` | проектное описание админки (ADR-26) |
| `docs/export-design.md` | план версии 1.4: экспорт в Excel и печатный вид |
| `docs/plan/PLAN.md` | план проекта: ADR, фазы, гейты (копия рабочего документа в git) |
| `docs/plan/spec.txt` | исходное ТЗ |
| `docs/dod.md` | чек-лист готовности: критерии ТЗ §5 с подтверждениями, состав версии 1.0 |
| `CHANGELOG.md` | история версий |
| `THIRD_PARTY.md` | лицензии зависимостей (проверка: `node scripts/licenses.mjs`) |

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

`db/schema.sql` — `sales` (50 000, с `region_id` → `regions`), `regions` (5), `finance` (576), `ops` (540), `report_templates`.
`db/seed.sql` детерминирован (значения — арифметика от `i`), повторный запуск безопасен.
Период продаж: 2025-10-01 … 2026-03-31. В данных намеренно есть `NULL`-ключи (49 регионов, 23 категории),
нулевые суммы (50), отрицательная прибыль (1221) и одна строка-выброс.

## Скрипты

| Скрипт | Назначение |
|---|---|
| `scripts/env.sh` | source: PATH с venv, креды БД из `backend/.env` |
| `scripts/conan` | обёртка Conan v1 с жёсткой проверкой версии |
| `scripts/check.sh` | полная проверка: версия Conan, запрещённые компоненты, сборка+тесты, валидация шаблонов, фронтенд-тесты |

## Проверки

```bash
./scripts/ci.sh              # сборка, ctest, валидация шаблонов, vitest, сборка фронтенда
REP_E2E=1 ./scripts/ci.sh    # то же + Playwright (нужны backend и preview: ./scripts/demo.sh)
```

В репозитории есть `.github/workflows/check.yml`: бэкенд (Conan v1, cmake, ctest) и фронтенд
(валидация, vitest, сборка) на push и pull request. e2e в CI не запускаются — им нужны живые
сервисы и БД.

## Вход и роли

Сервер работает с включённой аутентификацией (`REP_AUTH=1` в `scripts/demo.sh`):

```bash
./scripts/auth-init.sh          # создать admin и viewer, пароли — в backend/.env
./scripts/auth-init.sh --rotate # сменить пароли
```

| Роль | Что может |
|---|---|
| `viewer` | читать отчёты, справочники и данные |
| `author` | + создавать отчёты, сохранять черновики |
| `admin` | + публиковать, удалять, откатывать версии, открывать админку |

Вход — `/login.html`, сессия в cookie `rep_session` (HttpOnly, SameSite=Lax, 12 ч).
`GET /api/auth/me` возвращает логин и роль. Без `REP_AUTH=1` сервер работает без входа
(режим разработки).

## Роль БД только-чтения

Данные отчётов и справочники читаются под ролью `reporting_ro`, запись шаблонов — под основной.
Роль заводится один раз:

```bash
./scripts/db-ro.sh            # создать роль, выдать SELECT, пароль дописать в backend/.env
./scripts/db-ro.sh --rotate   # сменить пароль
```

Скрипт сам проверяет, что чтение `sales` работает, а `INSERT` отклоняется СУБД.
Признак в `/api/health`: `"readOnlyRole": true`. Без `REP_RO_DB_*` сервис работает на основной
роли и пишет предупреждение в лог.

## Запуск демонстрации

> Чистая сборка на пустом Conan-кэше требует `-s compiler.libcxx=libstdc++11`: без этого Conan v1
> собирает jsoncpp/drogon со старым ABI `_GLIBCXX_USE_CXX11_ABI=0`, и линковка падает на
> `Json::ValueIteratorBase::name`. `scripts/build.sh` и `scripts/ci.sh` передают этот флаг сами.

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
