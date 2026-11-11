# Сборка проекта

Windows (Visual Studio 2022) — отдельная инструкция: `docs/build-windows.md`, скрипт
`scripts/build.ps1`, пресеты CMake в `backend/CMakePresets.json`.

Сервер отчётов — два артефакта: C++/Drogon бэкенд (`backend/build/rep`) и собранный фронтенд
(`frontend/dist`).

## Требования

| Что | Версия | Зачем |
|---|---|---|
| CMake | ≥ 3.20 | сборка бэкенда (`backend/CMakeLists.txt`) |
| Компилятор C++ | C++17, g++/clang++ | код бэкенда |
| Conan | **v1**, 1.66.0 | зависимости: `drogon/1.9.8`, `catch2/3.7.1`, `libxlsxwriter/1.1.8` (`conanfile.txt`) |
| Node.js | 18+ (проверено на 24.22.1) | Vite 5, Playwright, ExcelJS |
| PostgreSQL | 16 | только сервер БД, для сборки не нужен |
| Python | 3.x + venv | Conan v1 и вспомогательные скрипты |

Conan v1 живёт в отдельном venv (`~/python_venv`), потому что обычный `conan` в PATH — это v2
с несовместимым кэшем `~/.conan2` (ADR-13). Все скрипты ходят через `scripts/conan`, он проверяет
мажорную версию и падает с понятным сообщением, если это v2.

## CI (GitHub Actions)

Job'ы `backend` и `e2e` работают на `ubuntu-24.04` с явно выбранным gcc 13: Conan 1.66 знает
`settings.compiler.version` только до 14.1, а `ubuntu-latest` приносит gcc 15 — автоматический
`detect` писал невалидную версию и `conan install` падал с
`Invalid setting '15' is not a valid 'settings.compiler.version' value` (ADR-83).
Поэтому профиль создаётся вручную:

```bash
mkdir -p "$CONAN_USER_HOME/.conan/profiles"
cat > "$CONAN_USER_HOME/.conan/profiles/default" <<'EOF'
[settings]
os=Linux
os_build=Linux
arch=x86_64
arch_build=x86_64
compiler=gcc
compiler.version=13
compiler.libcxx=libstdc++11
build_type=Release
[options]
[build_requires]
[env]
CC=/usr/bin/gcc-13
CXX=/usr/bin/g++-13
EOF
```

Кэш Conan в CI включает `gcc13` в ключ: пакеты, собранные другим компилятором, там не нужны.
При обновлении компилятора правьте и workflow, и этот профиль.

Автозапуск отключён (v1.57.2): workflow срабатывает только вручную (`workflow_dispatch`),
на `push` и `pull_request` не запускается. Проверки проекта — локально:
`REP_E2E=1 ./scripts/check.sh`.

## Полная сборка одной командой

```bash
./scripts/build.sh
```

Что происходит по шагам:

1. `source scripts/env.sh` — выставляет `CONAN_USER_HOME`, `VENV`, `PATH`, читает `backend/.env`;
2. зависимости: `scripts/conan install . -if build --build missing -s build_type=Release`
   (пропускается, если `backend/build/DrogonConfig.cmake` уже есть);
3. бэкенд: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$PWD/build`
   и `cmake --build build -j$(nproc)`;
4. тесты: `ctest --output-on-failure` в `backend/build`;
5. фронтенд: `npm ci` (или `npm install`), `npm run validate -- ../backend/templates/*.json`,
   `npm run build`.

## Только бэкенд

```bash
. scripts/env.sh
cd backend
../scripts/conan install . -if build --build missing -s build_type=Release -s compiler.libcxx=libstdc++11
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$PWD/build"
cmake --build build -j"$(nproc)"
./build/rep_tests          # Catch2, без базы и сети
ctest --test-dir build     # то же через ctest
```

Бэкенд не зависит от файлов фронтенда; статику отдаёт любой веб-сервер или Vite-превью.

## Только фронтенд

```bash
cd frontend
npm ci
npm run build        # → frontend/dist, ссылки на ассеты относительные
npm test             # vitest
npm run e2e          # Playwright, нужны запущенные бэкенд и preview
```

Сборка переносимая: `dist` можно скопировать на другой хост и в другой каталог, адрес API
вычисляется на клиенте (ADR-53). Переменные `REP_BASE` и `VITE_API_BASE` нужны только для
нестандартных схем — см. `docs/configuration.md`.

## Версия приложения

Единственный источник — файл `VERSION` в корне репозитория. Его читает CMake: версия попадает
в бинарник и в ответ `/api/health` (`version`). Копии в `README.md`, `CHANGELOG.md` и
`frontend/package.json` сверяются скриптом:

```bash
./scripts/version.sh            # показать версию и расхождения
./scripts/version.sh --check    # только проверка, код 1 при расхождении
```

Шаг входит в `./scripts/check.sh`. Поднимая версию, правьте `VERSION`, первую строку README,
заголовок CHANGELOG и `npm pkg set version=…`, затем ставьте тег `v<версия>`.

## Схема шаблона

Канон — `frontend/src/template/schema.js`; машинночитаемая копия `docs/template.schema.json`
генерируется из него:

```bash
cd frontend && npm run schema:json
```

Правки шаблона линейки формата (версии) идут через `frontend/src/template/migrate.js` и
`backend/src/template_migrate.cc` — константа версии сверяется тестами с обеих сторон.

## Проверки

| Команда | Что проверяет | Время |
|---|---|---|
| `ctest --test-dir backend/build` | 59 тестов ядра: агрегация, параметры, миграции, валидация, CSV/XLSX, SMTP/cron, аутентификация | ~1 с |
| `cd frontend && npm test` | 131 тест: форматирование, группировка, экспорт, валидатор, схема | ~10 с |
| `./scripts/golden.sh` | 15 эталонных отчётов (рендер) | ~20 с |
| `cd frontend && npm run e2e` | сценарии в браузере; нужен только бэкенд `:8080` (он же отдаёт статику) | ~2 мин |
| `REP_E2E=1 ./scripts/check.sh` | всё вместе | ~2 мин |
| `./scripts/check-proxy-auth.sh` | режим аутентификации на прокси (15 проверок), поднимает свой экземпляр | ~20 с |

Что нужно для прогонов:

| Нужно | Для чего | Как получить |
|---|---|---|
| PostgreSQL с базой и шаблонами | e2e, golden, демонстрация | `db/init.sh`, затем `scripts/templates-push.mjs` |
| Браузер Playwright | `npm run e2e` | `npx playwright install chromium` |
| SMTP-приёмник | рассылка на стенде | `scripts/smtp-sink.py` (поднимается `demo.sh`) |

## Пустой кэш Conan (чистая сборка)

Сборка «с нуля» проверяется так:

```bash
CONAN_USER_HOME=$(mktemp -d) ./scripts/build.sh
```

Каталог `CONAN_USER_HOME` изолирует кэш: все зависимости скачиваются заново. На чистом кэше
сборка дольше (drogon тянет boost-заголовки и openssl), поэтому в обычном `build.sh` кэш
переиспользуется. **Честно:** полный прогон на пустом кэше на этом стенде не зафиксирован в DoD —
это известный незакрытый пункт.

## Типичные проблемы

| Симптом | Причина | Что делать |
|---|---|---|
| `ERROR: ожидался Conan v1, получен '2.x'` | в PATH Conan 2 | не вызывать `conan` напрямую, только `scripts/conan` или `./scripts/build.sh` |
| `CMake Error: Drogon not found` | не выполнен `conan install` или не передан `CMAKE_PREFIX_PATH` | повторить шаги из «Только бэкенд» |
| `порт 8080 уже занят — сервер не запущен` | второй экземпляр | остановить прежний процесс |
| e2e падает с 401 | бэкенд и превью на разных портах/без сессии | поднять оба: `./scripts/demo.sh`, либо `REP_API` для `templates-push` |
| пустая страница под префиксом, 404 на `/assets/…` | в `dist` абсолютные пути | пересобрать `npm run build` (сейчас base `./`) |
| шаблон не виден после правки | шаблоны читаются на старте | перезапустить бэкенд |
