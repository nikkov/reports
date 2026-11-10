# Как добавить отчёт

Путь шаблона: файл в репозитории → валидатор → загрузка в PostgreSQL → рестарт бэкенда → проверка в браузере.
SQL живёт в шаблоне: `datasets.<key>.sql` для данных и `params[].source.sql` для справочников (ADR-24, ADR-27).

Откуда брать данные, тоже задаётся шаблоном (ADR-76): по умолчанию это сервер из окружения,
а другой PostgreSQL объявляется в блоке `connections` — адрес, база, пользователь, схема и
`passwordEnv` с именем переменной окружения для пароля. Набор и справочник ссылаются на
источник полем `connection`. Пример — `sales-external`, вторая база создаётся
`./db/examples/external.sh`; подробности — `docs/template-reference.md`.

## 1. SQL набора данных — в шаблоне

Пишется прямо в шаблоне, в секции `datasets` (ADR-27). Ниже — как выглядел реестровый вариант,
он больше не нужен; сами SQL перенесены в шаблоны:

```json
"datasets": {
  "sales": {
    "sql": "SELECT s.region, s.category, s.product, s.amount, s.profit, s.order_date FROM reports_examples.sales s WHERE s.region_id = :region::int AND (:category = '' OR s.category = ANY(string_to_array(:category, ','))) AND s.order_date >= :from::date AND s.order_date <= :to::date ORDER BY s.category NULLS LAST, s.product, s.id",
    "params": { "region": "$region", "category": "$category", "from": "$from", "to": "$to" }
  }
}
```json
{
  "name": "region",
  "type": "select",
  "label": "Регион",
  "source": {
    "kind": "lookup",
    "sql": "SELECT id::text AS value, name AS label FROM reports_examples.regions ORDER BY name"
  }
}
```

Каскадный справочник принимает параметр родителя; подстановка — как в обычных запросах, через `:name`:

```json
{
  "name": "category",
  "type": "select",
  "label": "Категория",
  "source": {
    "kind": "lookup",
    "sql": "SELECT DISTINCT s.category AS value, s.category AS label FROM reports_examples.sales s WHERE s.region_id = :region::int AND s.category IS NOT NULL ORDER BY 1",
    "dependsOn": "region"
  }
}
```

Эндпоинт — `GET /api/reports/{reportId}/lookups/{param}?<родитель>=…`. Кэша на сервере нет:
список кэширует клиент на время сессии, каждый новый запрос идёт в БД. Шаблон, отдаваемый
браузеру, не содержит `sql` — он вырезается на сервере.

## 2. Шаблон

`backend/templates/<id>.json` — минимальный рабочий пример:

```json
{
  "version": 1,
  "id": "sales-by-region",
  "title": "Продажи по региону",
  "description": "Сводная по выбранному региону за период",
  "params": [
    { "name": "region", "type": "select", "label": "Регион", "required": true,
      "source": { "kind": "lookup", "sql": "SELECT id::text AS value, name AS label FROM reports_examples.regions ORDER BY name" } },
    { "name": "from", "type": "date", "label": "Период с", "required": true, "default": "2026-01-01", "source": { "kind": "date" } },
    { "name": "to", "type": "date", "label": "по", "required": true, "default": "2026-03-31", "source": { "kind": "date" } }
  ],
  "datasets": {
    "sales": { "sql": "SELECT region, category, product, amount, profit, order_date FROM reports_examples.sales WHERE region_id = :region::int AND order_date >= :from::date AND order_date <= :to::date", "params": { "region": "$region", "from": "$from", "to": "$to" } }
  },
  "blocks": [
    { "type": "text", "text": "Регион {{region}}, период {{from}} — {{to}}" },
    { "type": "table", "dataKey": "sales", "grandTotal": true, "columns": [
      { "field": "category", "title": "Категория", "group": true, "subtotal": true },
      { "field": "product", "title": "Продукт" },
      { "field": "amount", "title": "Сумма", "aggregate": "sum", "format": "number" }
    ] }
  ]
}
```

Полный справочник полей — `docs/template-reference.md`, выражения — `docs/expressions.md`.

Ключ в БД берётся из поля `id` (не из имени файла); имя файла совпадает с `id` только по соглашению,
чтобы `templates/*.json` было легко сопоставить с отчётами.

## 3. Проверка шаблона

```bash
cd frontend
npm run validate -- ../backend/templates/<id>.json
```

Ошибки печатаются путями (`params.0.source`, `blocks.0.columns.2`) и человекочитаемыми сообщениями.
Тот же валидатор вызывается из `scripts/check.sh`, поэтому «зелёный» `check.sh` гарантирует, что все
шаблоны в репозитории корректны.

## 4. Загрузка в базу

```bash
cd ..
. scripts/env.sh                     # PGPASSWORD и прочие переменные подключения
node scripts/templates-push.mjs backend/templates/<id>.json
# или все сразу:
node scripts/templates-push.mjs backend/templates/*.json
```

Скрипт делает `INSERT … ON CONFLICT (id) DO UPDATE SET body = …, updated_at = now()`; тело передаётся
через dollar-quoting, без интерполяции в SQL-строку.

## 5. Рестарт бэкенда

Шаблоны читаются из БД **на старте**, hot-reload не делается (ADR-21):

```bash
./scripts/demo.sh          # пересборка не нужна, только рестарт
```

Для правок фронтенда — `./scripts/build.sh`, затем `./scripts/demo.sh`.

## 6. Проверка в браузере

```bash
open http://127.0.0.1:8080/                                 # список отчётов
open "http://127.0.0.1:8080/report.html?report=<id>"        # сам отчёт
```

В списке карточка должна показать заголовок, описание и бейджи параметров с их видом
(`справочник`, `мультивыбор`, `набор`, `дата`). В отчёте — строку `<N> строк · запрос <M> мс · шаблон v1`.

Проверка API без браузера:

```bash
curl -s localhost:8080/api/reports | python3 -m json.tool | head -20
curl -s "localhost:8080/api/lookups/lookup_regions"
curl -s -X POST localhost:8080/api/report/data -H 'Content-Type: application/json' \
  -d '{"reportId":"<id>","params":{"region":"1","from":"2026-01-01","to":"2026-03-31"}}' | head -c 500
```

## 7. Тесты

```bash
cd frontend && npm test          # vitest: движок, выражения, валидатор
cd frontend && npm run e2e       # Playwright (нужен только backend :8080)
REP_E2E=1 ./scripts/check.sh     # всё вместе: conan, запрещённые компоненты, сборка, тесты, golden, e2e
```

Для нового отчёта полезно добавить e2e: открыть `report.html?report=<id>`, дождаться строк,
сменить параметр и убедиться, что данные и URL изменились.

## Типичные ошибки

| Симптом | Причина и что делать |
|---|---|
| `404 REPORT_NOT_FOUND` | шаблон не загружен в БД или бэкенд не перезапущен после push |
| `422 INVALID_PARAMS` | не передан обязательный параметр, либо значение не того типа (например, `region=abc`) |
| `404 LOOKUP_NOT_FOUND` | у параметра нет `source.sql`, либо имя параметра в URL не совпадает с шаблоном |
| `422 INVALID_PARAMS` на справочнике | не передан параметр родителя, который используется в `sql` через `:name` |
| `422 ROW_LIMIT` на `report/data` | выборка больше 50 000 строк — сузьте период или добавьте фильтр |
| `504` | запрос дольше `REP_STATEMENT_TIMEOUT_MS` (по умолчанию 30 с) — нужен индекс или более узкая выборка |
| пустой список отчётов | `REP_TEMPLATES_SOURCE=db`, а таблица `report_templates` пуста → повторите шаг 4 |
| отчёт открывается, но данных нет | параметры не доходят до SQL: проверьте имена в `datasets.*.params` (`$name`) и в `params` запроса |

## Где что лежит

| Файл | Назначение |
|---|---|
| `backend/templates/*.json` | SQL наборов данных и справочников — внутри шаблонов |
| `backend/templates/*.json` | шаблоны (источник в репозитории) |
| `frontend/src/template/schema.js` | канон JSON Schema |
| `frontend/src/template/validate.js` | семантический валидатор |
| `scripts/templates-push.mjs` | загрузка шаблонов в БД |
| `docs/template-reference.md` | справочник полей |
| `docs/expressions.md` | грамматика выражений |
| `docs/decisions.md` | ADR, включая ADR-21 (хранение шаблонов) и ADR-22 (параметры-справочники) |
