# Как добавить отчёт

Путь шаблона: файл в репозитории → валидатор → загрузка в PostgreSQL → рестарт бэкенда → проверка в браузере.
SQL живёт только на сервере (`backend/config/queries.json`), шаблон его не содержит.

## 1. Запрос в реестре SQL

`backend/config/queries.json`:

```json
{
  "sales_by_region_id": {
    "sql": "SELECT s.region, s.category, s.product, s.amount, s.profit, s.order_date FROM sales s WHERE s.region_id = :region::int AND (:category = '' OR s.category = ANY(string_to_array(:category, ','))) AND s.order_date >= :from::date AND s.order_date <= :to::date ORDER BY s.category NULLS LAST, s.product, s.id",
    "params": {
      "region":   { "type": "int",  "required": true },
      "category": { "type": "text", "required": false, "default": "" },
      "from":     { "type": "date", "required": true },
      "to":       { "type": "date", "required": true }
    },
    "columns": { "region": "text", "category": "text", "product": "text", "amount": "int", "profit": "int", "order_date": "date" }
  }
}
```

Правила, которые проверены на практике:

- параметры пишутся как `:name`, сервер сам превращает их в `$1…$N` и передаёт через binding;
- рядом с placeholder нужен явный каст PostgreSQL: `:from::date`, `:region::int` — иначе СУБД не выведет тип;
- множественный выбор приходит строкой: `= ANY(string_to_array(:param, ','))`;
- необязательный фильтр удобно делать как `(:param = '' OR col = :param)` с `default: ""`;
- `columns` задаёт типы полей для фронтенда; перечисляйте все поля из `SELECT`;
- лимит строк — 50 000 (`maxRows`, `REP_MAX_ROWS`), при превышении API вернёт `422 ROW_LIMIT`;
- `statement_timeout` — 2 секунды, дольше — 504.

### Справочник для параметра (`source.kind = "lookup"`)

Отдельный запрос возвращает **ровно две колонки**: значение и подпись.

```json
{
  "lookup_regions": {
    "sql": "SELECT id::text AS value, name AS label FROM regions ORDER BY name",
    "params": {},
    "columns": { "value": "text", "label": "text" }
  }
}
```

Каскадный справочник принимает параметр родителя:

```json
{
  "lookup_categories_by_region": {
    "sql": "SELECT DISTINCT s.category AS value, s.category AS label FROM sales s WHERE s.region_id = :region::int AND s.category IS NOT NULL ORDER BY 1",
    "params": { "region": { "type": "int", "required": true } },
    "columns": { "value": "text", "label": "text" }
  }
}
```

Ответ эндпоинта `GET /api/lookups/{id}` кешируется в памяти на `REP_LOOKUP_TTL_SECONDS` (по умолчанию 60 с).

## 2. Шаблон

`backend/templates/<id>.json` — минимальный рабочий пример:

```json
{
  "version": 1,
  "id": "sales-by-region",
  "title": "Продажи по региону",
  "description": "Сводная по выбранному региону за период",
  "params": [
    { "name": "region", "type": "select", "label": "Регион", "required": true, "source": { "kind": "lookup", "queryId": "lookup_regions" } },
    { "name": "from", "type": "date", "label": "Период с", "required": true, "default": "2026-01-01", "source": { "kind": "date" } },
    { "name": "to", "type": "date", "label": "по", "required": true, "default": "2026-03-31", "source": { "kind": "date" } }
  ],
  "datasets": {
    "sales": { "queryId": "sales_by_region_id", "params": { "region": "$region", "from": "$from", "to": "$to" } }
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
open http://127.0.0.1:5173/                                 # список отчётов
open "http://127.0.0.1:5173/report.html?report=<id>"        # сам отчёт
```

В списке карточка должна показать заголовок, описание и бейджи параметров с их видом
(`справочник`, `мультивыбор`, `набор`, `дата`). В отчёте — строку `<N> строк · запрос <M> мс · шаблон v1`.

Проверка API без браузера:

```bash
curl -s localhost:8080/api/reports | python3 -m json.tool | head -20
curl -s "localhost:8080/api/lookups/lookup_regions"
curl -s "localhost:8080/api/report/data?id=<id>&region=1&from=2026-01-01&to=2026-03-31" | head -c 500
```

## 7. Тесты

```bash
cd frontend && npm test          # vitest: движок, выражения, валидатор
cd frontend && npm run e2e       # Playwright (нужны запущенные backend :8080 и preview :5173)
REP_E2E=1 ./scripts/check.sh     # всё вместе: conan, запрещённые компоненты, сборка, тесты, golden, e2e
```

Для нового отчёта полезно добавить e2e: открыть `report.html?report=<id>`, дождаться строк,
сменить параметр и убедиться, что данные и URL изменились.

## Типичные ошибки

| Симптом | Причина и что делать |
|---|---|
| `404 REPORT_NOT_FOUND` | шаблон не загружен в БД или бэкенд не перезапущен после push |
| `422 INVALID_PARAMS` | не передан обязательный параметр, либо значение не того типа (например, `region=abc`) |
| `404 LOOKUP_NOT_FOUND` | в `source.queryId` указан запрос, которого нет в `queries.json` |
| `422 ROW_LIMIT` на `report/data` | выборка больше 50 000 строк — сузьте период или добавьте фильтр |
| `504` | запрос дольше 2 с — нужен индекс или более узкая выборка |
| пустой список отчётов | `REP_TEMPLATES_SOURCE=db`, а таблица `report_templates` пуста → повторите шаг 4 |
| отчёт открывается, но данных нет | параметры не доходят до SQL: проверьте имена в `datasets.*.params` (`$name`) и в `params` запроса |

## Где что лежит

| Файл | Назначение |
|---|---|
| `backend/config/queries.json` | реестр SQL и типы колонок |
| `backend/templates/*.json` | шаблоны (источник в репозитории) |
| `frontend/src/template/schema.js` | канон JSON Schema |
| `frontend/src/template/validate.js` | семантический валидатор |
| `scripts/templates-push.mjs` | загрузка шаблонов в БД |
| `docs/template-reference.md` | справочник полей |
| `docs/expressions.md` | грамматика выражений |
| `docs/decisions.md` | ADR, включая ADR-21 (хранение шаблонов) и ADR-22 (параметры-справочники) |
