#!/usr/bin/env bash
# Единая точка настройки окружения. Использование: . scripts/env.sh
set -euo pipefail

export REP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export VENV="${VENV:-$HOME/python_venv}"
export CONAN="$VENV/bin/conan"
export PYTHON="$VENV/bin/python"

# Conan v1 хранит кэш в ~/.conan; CONAN_USER_HOME фиксируем, чтобы не уехать в другой профиль.
export CONAN_USER_HOME="${CONAN_USER_HOME:-$HOME}"

# venv первым в PATH: иначе bare `conan` = Conan 2 из ~/.local/bin (см. ADR-13).
export PATH="$VENV/bin:$PATH"

# Секреты БД
if [ -f "$REP_ROOT/backend/.env" ]; then
  set -a
  # shellcheck disable=SC1091
  . "$REP_ROOT/backend/.env"
  set +a
fi

# Режим аутентификации (ADR-54): пусто — выключена, 1 — сессии, proxy — личность от прокси.
export REP_AUTH="${REP_AUTH:-}"
export REP_AUTH_PROXY_HEADER="${REP_AUTH_PROXY_HEADER:-X-Remote-User}"
# Кому верим: адрес, с которого пришёл запрос. Остальным заголовок не читается вовсе.
export REP_AUTH_PROXY_TRUSTED="${REP_AUTH_PROXY_TRUSTED:-127.0.0.1,::1}"

export PGHOST="${PGHOST:-192.168.0.3}"
export PGPORT="${PGPORT:-32770}"
export PGUSER="${PGUSER:-postgres}"
export PGPASSWORD="${PGPASSWORD:-postgres}"
export PGDATABASE="${PGDATABASE:-reporting}"
