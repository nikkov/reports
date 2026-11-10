# Развёртывание

## Топология

Самодостаточный вариант (ADR-79) — один процесс и никаких nginx/node:

```
браузер ──HTTP/HTTPS──► rep :8080  ── статика из <бинарник>/web
                          └─ /api/* ──► PostgreSQL :5432
```

Если нужен префикс (`/reports`) или TLS — перед ним ставится любой реверс-прокси, но это
уже необязательно: сервер сам отдаёт страницы, ассеты и API.

Один процесс бэкенда обслуживает API, экспорт, рассылку и планировщик подписок. Отдельного
воркера нет: подписки отправляет тот же процесс (раз в 30 с).

## Комплект «бинарник + web»

`./scripts/build.sh` собирает обе части и кладёт статику рядом с сервером:

```
backend/build/rep      ← исполняемый файл
backend/build/web/     ← index.html, report.html, admin.html, assets/…
```

Порядок поиска каталога статики при старте:

1. `REP_WEB_ROOT` — если задан, используется как есть (ошибка в пути видна в логе, а не
   подменяется другим каталогом);
2. `./web` и `../web` — запуск из любого каталога раскладки;
3. `<каталог бинарника>/web` — комплект из `build.sh`;
4. `<каталог бинарника>/../../frontend/dist` — запуск прямо из репозитория.

Если ни один каталог не найден, сервер работает как чистое API и пишет предупреждение —
так его можно ставить за nginx или запускать в CI без сборки фронтенда.

Перенос на другой сервер: скопировать `rep` и `web/` (и `backend/config/queries.json`),
указать окружение и запустить — статика найдётся сама.

## Что нужно на сервере

| Компонент | Требования |
|---|---|
| PostgreSQL | 16; база `reporting` (или своя), роль с правом записи, отдельная роль только-чтения |
| Бэкенд | артефакт `backend/build/rep`, `backend/config/queries.json`, доступ к БД |
| Фронтенд | каталог `backend/build/web` рядом с бинарником (или `REP_WEB_ROOT`); сборка переносимая |
| Прокси | не обязателен; нужен для TLS, префикса и SSE-настроек (см. `deploy/nginx-reports.conf`) |

## Порядок первого развёртывания

```bash
# 1. Сборка (на этой же машине или перенос артефактов)
./scripts/build.sh

# 2. База: роли (owner/srv/ro), ядро (reports), примеры (reports_examples), миграции
#    под суперпользователем создаются роли и базы, дальше DDL идёт владельцем reporting_owner
./scripts/db-users.sh                    # reporting_srv + reporting_ro: пароли, права, проверки
./scripts/db-users.sh --rotate           # плановая смена паролей (обновляет backend/.env)
./scripts/db-users.sh --password-stdin   # пароли из stdin: строка 1 — srv, строка 2 — ro
REP_DB=reporting ./db/init.sh            # ядро + примеры; только ядро: --core-only
REP_DB=reporting ./db/init.sh --core-only  # база «продукт без демо-данных»

# 3. Шаблоны из файлов в базу
REP_ADMIN=1 REP_ADMIN_LOGIN=admin REP_ADMIN_PASSWORD=... \
  node scripts/templates-push.mjs

# 4. Пользователи (режим сессий) — логины и пароли из переменных, не из репозитория
REP_DB=reporting REP_ADMIN_PASSWORD=... REP_VIEWER_PASSWORD=... ./scripts/auth-init.sh
```

Роли и права (ADR-75): `reporting_owner` — владелец схем и таблиц, под ним идут DDL и миграции
(`REP_OWNER_DB_USER`/`REP_OWNER_DB_PASSWORD`); `reporting_srv` — под ней работает сервер
(`PGUSER`): DML в ядре и чтение примеров, **без права создавать объекты**; `reporting_ro` — только
чтение `reports_examples`. Суперпользователь нужен лишь на установке; в эксплуатации
`REP_STRICT_ROLE=1` заставляет сервер отказаться стартовать под ним.

`db/init.sh` идемпотентен по структуре (CREATE ... IF NOT EXISTS) и по миграциям, но примеры
перезаливает (`db/examples/seed.sql` делает TRUNCATE). На рабочей базе запускают только миграции из
`db/migrations/*.sql` — они идемпотентны. Ядро и примеры живут в разных схемах: `reports` и
`reports_examples` (ADR-74), поэтому `--core-only` поднимает продукт без синтетики.

## Конфигурация

Полный список переменных с значениями по умолчанию — `docs/configuration.md`. Минимальный набор
для эксплуатации:

```ini
# /etc/rep/rep.env  (права 0600, владелец — сервисный пользователь)
PGHOST=127.0.0.1
PGPORT=5432
PGDATABASE=reporting
PGUSER=rep_app
PGPASSWORD=...
REP_RO_DB_USER=reporting_ro
REP_RO_DB_PASSWORD=...
REP_HOST=127.0.0.1
REP_PORT=8080
REP_ADMIN=1
REP_AUTH=proxy
REP_AUTH_PROXY_TRUSTED=127.0.0.1
REP_SMTP_HOST=smtp.corp.local
REP_SMTP_PORT=587
REP_SMTP_USER=reports
REP_SMTP_PASSWORD=...
REP_SMTP_FROM=reports@corp.local
```

Бэкенд читает переменные окружения процесса, затем `backend/.env` (путь меняется `REP_ENV_FILE`).
В рабочем развёртывании удобнее файл окружения системы (`EnvironmentFile=`), а `backend/.env`
не создавать.

## systemd

```ini
# /etc/systemd/system/rep-backend.service
[Unit]
Description=Report Server backend
After=network-online.target postgresql.service

[Service]
Type=simple
User=rep
Group=rep
WorkingDirectory=/opt/reporting/rep
EnvironmentFile=/etc/rep/rep.env
ExecStart=/opt/reporting/rep/backend/build/rep
Restart=on-failure
RestartSec=3
# логи: drogon пишет в stderr, их забирает journald
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
```

```bash
systemctl daemon-reload && systemctl enable --now rep-backend
journalctl -u rep-backend -f
```

При занятом порте процесс завершается с кодом 1 и понятным сообщением «порт 8080 уже занят —
сервер не запущен» (v1.19.1), а не падает с SIGSEGV.

## Реверс-прокси

Готовый пример — `deploy/nginx-reports.conf`:

- `/reports/` — статика из `frontend/dist`, `try_files` отдаёт каталог для незнакомых путей;
- `/reports/api/` — `proxy_pass http://127.0.0.1:8080/api/`, префикс снимается;
- `/reports/api/report/events` — отдельный блок с `proxy_buffering off` и `proxy_read_timeout 1h`,
  иначе живые события не доходят;
- при `REP_AUTH=proxy` — `auth_basic` (или SSO/`auth_request`) и
  `proxy_set_header X-Remote-User $remote_user`: прокси обязан затирать клиентский заголовок.

Фронтенд пересобирать при смене адреса не нужно: пути к ассетам относительные, адрес API считается
из расположения страницы (ADR-53). Достаточно разложить `dist` и указать префикс в nginx.

## Обновление версии

1. Остановить сервис (или подготовить второй порт и переключить прокси);
2. заменить `backend/build/rep` и каталог `dist`;
3. применить новые миграции: `psql -d reporting -f db/migrations/<новая>.sql`;
4. если менялись шаблоны — `node scripts/templates-push.mjs` и перезапуск (шаблоны читаются на старте);
5. запустить сервис, проверить `curl -s localhost:8080/api/health` — `status: ok`, `db: true`,
   `schemaVersion` соответствует ожидаемой версии формата шаблона;
6. проверить каталог и один отчёт глазами.

Порядок «сначала миграции, потом код» допустим, обратный — нет: новый код ожидает новые колонки.
Версия формата шаблона (`version`) мигрируется сервером автоматически при загрузке; шаблон новее
сервера отклоняется с 422 «обновите сервер», поэтому сервер обновляют раньше шаблонов.

## Резервное копирование

```bash
# структура и данные: шаблоны, история, пользователи, подписки, журнал
pg_dump -Fc -d reporting -f /backup/reporting-$(date +%F).dump
# восстановление
pg_restore -d reporting_restore /backup/reporting-2026-11-08.dump
```

Что важно в дампе: `report_templates` (включая `draft`), `report_templates_history`, `users`,
`report_subscriptions`. Файлы шаблонов в `backend/templates` — источник, но рабочая версия живёт в БД.
Внешних файлов бэкенд не хранит, кроме временных `.xlsx` в `TMPDIR`.

## Мониторинг и журналы

- `GET /api/health` → `{"status":"ok","db":true,"maxRows":50000,"readOnlyRole":true,"authMode":"proxy","schemaVersion":2,"version":"1.28.1"}`;
  `version` берётся из файла `VERSION` при сборке — по нему видно, какая сборка развёрнута,
  `schemaVersion` — версия формата шаблона.
  Пропал `db` — потеряна БД; `readOnlyRole: false` — данные читаются основной ролью (проверьте `REP_RO_DB_*`);
- журнал действий — `audit_log`, страница `/reports/audit.html` (роль admin): публикации, роли,
  рассылка, входы;
- журнал рассылки — `subscription_runs` (когда, строк, писем, ошибка, длительность);
- логи процесса — stderr → journald; `LOG_INFO` на старте перечисляет источник шаблонов, роль чтения,
  размер пула.

## Ограничения, о которых нужно знать заранее

| Ограничение | Последствие | Что делать |
|---|---|---|
| Cookie сессии `Secure` не выставляется | при HTTPS флаг отсутствует | ставить TLS на прокси и включать `Secure` отдельной правкой; пока — только доверенная сеть |
| Журналы чистятся по расписанию | история старше `REP_LOG_RETENTION_DAYS` (по умолчанию 90 дней) удаляется | увеличить глубину или `0`; проверка — `./scripts/check-retention.sh` |
| Шаблоны в памяти | правка требует перезапуска | порядок обновления выше |
| Планировщик внутри процесса | при остановке сервиса подписки не уходят | `Restart=on-failure`, либо отдельный экземпляр с `REP_SCHEDULER=0` для API |
| Нет ограничения попыток входа | перебор пароля в режиме сессий | использовать `REP_AUTH=proxy` с проверкой на прокси |
| Кэш 60 с | данные могут отставать | уменьшить `REP_CACHE_TTL_SECONDS` |
| Повторы ограничены | после `REP_SMTP_RETRY_ATTEMPTS` неудач письмо не уйдёт | смотреть `subscription_runs` и `subscription.given_up`, отправлять вручную (`POST …/run`) |
| Доставка at-least-once | обрыв процесса в момент отправки может дать дубль | так и задумано: письмо важнее; дубли видны в журнале отправок |
| «Заём» подписки | пока идёт отправка, подписка занята `REP_SUBSCRIPTION_LEASE_SECONDS` | увеличить заём, если сборка файла дольше (по умолчанию 120 с) |
| Часовой пояс подписки | cron считается по `timezone` подписки, а не сервера | задать пояс в подписке; старые подписки по умолчанию `UTC` |
| Нет горизонтального масштабирования | один процесс на БД | несколько экземпляров возможны, но кэш у каждого свой |

## Проверки после развёртывания

```bash
curl -s localhost:8080/api/health                       # status ok, db true
curl -s localhost:8080/api/reports | head -c 200        # каталог отдаётся
curl -s -o /dev/null -w '%{http_code}\n' localhost:8080/reports/   # 200 через прокси
./scripts/check-proxy-auth.sh                            # если включён режим прокси
```
