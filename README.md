# Report Server — версия 1.58.0

Принята постановщиком 2026-11-05. Шаблон самодостаточен: оформление, форматы, правила подсветки,
SQL наборов данных и SQL справочников живут в нём. Есть админка `/admin.html`, экспорт в Excel (числа с форматами, объединения, подытоги), печать
с повтором шапки на страницах, переходы из групп в детальные отчёты, каталог с папками, тегами и избранным,
правка шаблонов идёт через черновик с явной публикацией, данные отчёта кэшируются (TTL, ETag),
один шаблон может объявлять несколько наборов данных — каждый блок читает свой, группировку
(`GROUP BY`) можно считать на сервере, любой отчёт выгружается в `.xlsx` сервером — без браузера, с учётом агрегации и с листом параметров,
а шаблон может включить автообновление данных на экране (`refreshSeconds`).
Состав и ограничения — `docs/dod.md`, история — `CHANGELOG.md`.

Декларативные отчёты: JSON-шаблон → страница с текстом, таблицами (собственный рендерер, ADR-20) и диаграммами (ECharts:
линия, столбцы, круг, область, точки, спидометр `gauge` и потоки `sankey`).
Шаблоны хранятся в PostgreSQL (ADR-21), параметры-справочники — ADR-22.
Бэкенд — C++/Drogon; строки отдаются как есть, агрегаты считаются на клиенте, а группировку
по крупным наборам можно перенести в PostgreSQL (`blocks[].aggregate`, ADR-37).
Полный план и принятые решения: `../PLAN.md`, `docs/decisions.md`.

## Документация

| Файл | О чём |
|---|---|
| `docs/build.md` | сборка бэкенда и фронтенда, зависимости, тесты, типичные ошибки |
| `docs/configuration.md` | все переменные окружения с значениями по умолчанию и их смысл |
| `docs/deployment.md` | развёртывание: БД, systemd, nginx, обновление, бэкап, мониторинг, ограничения |
| `docs/how-to-add-report.md` | пошагово: SQL → шаблон → валидатор → push → рестарт → проверка |
| `docs/template-reference.md` | справочник всех полей шаблона, включая `source`/`multiple`/`dependsOn` |
| `docs/expressions.md` | грамматика вычисляемых колонок, области видимости, null |
| `docs/adr.md` | **все ADR (1…59) с оглавлением** — генерируется из `PLAN.md`: `node scripts/adr.mjs` |
| `docs/decisions.md` | журнал решений «D-…», принятых по ходу исполнения (append-only) |
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

- Linux x86_64, g++ ≥ 12, CMake ≥ 3.20 (`cmake_minimum_required` в `backend/CMakeLists.txt`)
- Conan **v1** (1.66.0) из venv `~/python_venv` — bare `conan` в PATH это v2, он не подходит (ADR-13)
- Node.js ≥ 20 (nvm: `nvm use 24`)
- PostgreSQL 15+ (dev: локальный кластер `127.0.0.1:5432`, БД `reporting` — поднимается
  скриптом `./scripts/dev-db-init.sh`; стенд может смотреть на отдельный сервер)
- один процесс `backend/build/rep` отдаёт и API, и собранный фронтенд из `backend/build/web`
  (ADR-79): ни nginx, ни node для показа страниц не нужны
- роли БД: `reporting_owner` (DDL/миграции), `reporting_srv` (сервер), `reporting_ro` (чтение
  отчётов). Суперпользователь — только для установки, в эксплуатации не используется (ADR-75).
  Создание и ротация: `./scripts/db-users.sh` (пароли — в `backend/.env`, проверки прав встроены)

## Быстрый старт

```bash
./scripts/dev-db-init.sh                 # локальный PostgreSQL 15: роли, базы, схема, 50k строк, .env
#   стенд на отдельном сервере: cp backend/.env.example backend/.env && ./db/init.sh
./scripts/conan install backend -if backend/build --build missing -s build_type=Release
cmake -S backend -B backend/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$PWD/backend/build
cmake --build backend/build -j"$(nproc)"
./scripts/check.sh                       # conan + сборка + ctest (+ фронтенд, когда появится)
```

## Данные

Схемы разделены (ADR-74): ядро — `reports` (`db/core.sql`): `report_templates`, `report_templates_history`,
`users`, `sessions`, `report_subscriptions`, `subscription_runs`, `audit_log`. Примеры — `reports_examples`
(`db/examples/schema.sql` + `seed.sql`): `sales` (50 000, с `region_id` → `regions`), `regions` (5),
`finance` (576), `ops` (540). `./db/init.sh --core-only` ставит только ядро, без демо-данных.
`db/examples/seed.sql` детерминирован (значения — арифметика от `i`), повторный запуск безопасен.
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
./scripts/build.sh     # conan install + cmake + ctest + npm ci + build + web рядом с бинарником
./scripts/demo.sh      # один процесс :8080 — API и страницы, печатает ссылку, Ctrl+C — стоп
```

## Страница отчёта и e2e

```bash
# один процесс: и API, и статика (ADR-79). Порт всегда 8080
./backend/build/rep &

# приложение живёт под префиксом /reports (ADR-81)
open http://127.0.0.1:8080/reports/                                    # список отчётов
open "http://127.0.0.1:8080/reports/report.html?report=sales-summary&from=2026-01-01&to=2026-03-31"
open http://127.0.0.1:8080/reports/report.html?report=params-demo      # все виды параметров
open http://127.0.0.1:8080/reports/spike.html                          # песочница рендерера
cd frontend && npm run e2e                 # Playwright против :8080
REP_E2E=1 ./scripts/check.sh               # всё вместе
```
| `db/init.sh` | создание БД и загрузка синтетики (идемпотентно) |
| `db/load-examples.sh` | загрузка шаблонов примеров прямо в БД: без сервера и API, с валидацией, историей и аудитом |
| `scripts/templates-push.mjs` | то же через админский API (когда сервер уже работает) |

## Серверный экспорт, каталог и обновление по событию

- CSV: `POST /api/report/export.csv` — тот же расчёт, что у `.xlsx`; файл в UTF-8 с BOM и `;`,
  поэтому Excel открывает его без мастера импорта (ADR-40);
- каталог: `GET /api/reports?q=&folder=&tag=&limit=&offset=` — серверный поиск и страницы;
  без `limit` отдаётся прежний массив (ADR-41);
- обновление по событию: в шаблоне `"live": true`, страница держит SSE-поток
  `GET /api/report/events?report=<id>` и обновляется сразу после публикации (ADR-44).
  За nginx нужен `proxy_buffering off` для этого пути; прокси стенда (`frontend/sse-proxy.js`)
  уже пробрасывает поток без буферизации.

```bash
# CSV за март
curl -b cookie -X POST localhost:8080/api/report/export.csv \
  -H 'Content-Type: application/json' \
  -d '{"reportId":"sales-summary","params":{"from":"2026-03-01","to":"2026-03-31"}}' -o sales.csv

# поиск по каталогу, вторая страница
curl -b cookie 'localhost:8080/api/reports?q=продаж&limit=10&offset=10'
```

## Валидация шаблонов

Шаблон проверяется дважды: в редакторе (мгновенная подсказка) и на сервере — истина вторая, без неё
сохранить шаблон нельзя. Правила живут в `frontend/src/template/validate.js` и `backend/src/validate.cc`,
а их совпадение держат фикстуры `docs/fixtures/validation-cases.json`: файл читают и Catch2, и vitest.

```bash
# список ошибок по шаблону
curl -b cookie -X POST localhost:8080/api/reports/sales-summary/validate \
  -H 'Content-Type: application/json' -d '{"body": '"$(cat backend/templates/sales-summary.json)"'}'
```

## Рассылка по расписанию

Подписка = отчёт + cron + параметры + получатели + формат вложения (`csv` или `xlsx`). Сервер раз в 30 с
проверяет расписание, собирает файл тем же кодом, что и серверный экспорт, и отправляет письмо
с вложением (ADR-46, ADR-48). Серверный XLSX содержит объединения групповых ячеек и уровни структуры,
порядок строк совпадает с экраном (ADR-49).

```bash
# подписка: будни в 9:00, отчёт за март
curl -b cookie -X POST localhost:8080/api/admin/subscriptions \
  -H 'Content-Type: application/json' \
  -d '{"reportId":"sales-aggregate","title":"Продажи за март","cron":"0 9 * * 1-5",
       "params":{"from":"2026-03-01","to":"2026-03-31"},"recipients":["po@example.test"],
       "format":"xlsx"}'

# отправить сейчас (расписание не сдвигается)
curl -b cookie -X POST localhost:8080/api/admin/subscriptions/1/run
```

Настройки SMTP: `REP_SMTP_HOST`, `REP_SMTP_PORT`, `REP_SMTP_USER`, `REP_SMTP_PASSWORD`, `REP_SMTP_FROM`,
`REP_SMTP_TLS`. На стенде `scripts/demo.sh` поднимает локальный приёмник (`scripts/smtp-sink.py`),
письма складываются в `/tmp/rep-mail`. Страница — `subscriptions.html`.

### E2E в CI и локально

Сквозные тесты гоняются на изолированном стенде: своя БД и локальный SMTP-приёмник,
порт один — 8080 (сервер отдаёт и API, и статику).

```bash
./scripts/ci-e2e.sh                  # локально: база reporting_ci, порт 8080, SMTP 1025
REP_CI_DB=my_ci ./scripts/ci-e2e.sh  # другая база — если reporting_ci занята
```

В GitHub Actions это job `e2e` (сервис `postgres:16`, Conan-кэш, артефакты при падении).
Требования: собранные `backend/build/rep` и `frontend/dist`, Playwright-браузеры
(`npx playwright install --with-deps chromium`).

### Управление пользователями

Страница `users.html` (ссылка из админки, роль admin): создание пользователя, смена роли,
отключение без удаления, сброс пароля, удаление. API: `GET/POST /api/admin/users`,
`PUT/DELETE /api/admin/users/{id}`. Отключение и смена пароля завершают активные сессии;
сервер не даст удалить себя или остаться без единого администратора (ADR-50).

### Журнал действий

Страница `audit.html` (роль admin): кто и когда публиковал шаблоны, менял роли пользователей,
отправлял рассылку, входил в систему. API — `GET /api/admin/audit?action=&actor=&limit=&offset=`,
только чтение: подчистить журнал через интерфейс нельзя (ADR-52).

## За реверс-прокси: сборка переносимая

Собирается один раз, копируется куда угодно — ни пересборки, ни правки настроек:

```bash
./scripts/build.sh          # frontend/dist
```

- ссылки на ассеты в `dist` относительные (`./assets/…`), поэтому каталог можно отдать
  и из корня, и из `/reports/`, и из `/portal/x/`;
- адрес API вычисляется во время выполнения: по умолчанию это каталог приложения
  (`/reports/report.html` → `/reports/api`, корень → `/api`). Если API живёт не там,
  префикс задаётся без пересборки — `<meta name="rep-api-base" content="/reports">` в HTML
  или `window.REP_API_BASE = '/reports'` перед скриптами (полная форма `/reports/api` тоже
  принимается);
- в бандле нет ни имени хоста, ни порта: `grep -l '127.0.0.1\|localhost\|:8080' dist/assets/*.js` пуст;
- прокси должен снимать префикс: `/reports/api/… → http://127.0.0.1:8080/api/…`
  (готовый пример — `deploy/nginx-reports.conf`, включая отдельный блок для SSE
  с `proxy_buffering off`);
- проверить без nginx: `./scripts/demo.sh` — сервер сам отдаёт `/report.html` и `/api` на :8080
  (тот же `dist`, префикс задаётся только при запуске стенда).

## Режимы входа

| `REP_AUTH` | Как входит пользователь | Что отвечает сервер отчётов |
|---|---|---|
| не задан | входа нет (стенд, локальная демонстрация) | всё доступно |
| `1` | логин и пароль, cookie `rep_session` (ADR-34) | `users`/`sessions`, вход и выход на `/api/auth/*` |
| `proxy` | вход выполняет реверс-прокси (basic, SSO) | логин из заголовка `X-Remote-User`, роль из `users` |

Режим прокси: `REP_AUTH=proxy`, `REP_AUTH_PROXY_HEADER` (по умолчанию `X-Remote-User`),
`REP_AUTH_PROXY_TRUSTED` (адреса прокси, по умолчанию `127.0.0.1,::1`),
`REP_AUTH_PROXY_AUTOCREATE=1` (заводить неизвестных с ролью `REP_AUTH_PROXY_ROLE`).
Заголовку верим только с доверенных адресов; проверка — `./scripts/check-proxy-auth.sh`.
Пример nginx — `deploy/nginx-reports.conf`.
