#!/usr/bin/env bash
# Пользователи для аутентификации (ADR-34).
#   ./scripts/auth-init.sh            — создать admin и viewer, пароли дописать в backend/.env
#   ./scripts/auth-init.sh --rotate   — сменить пароли
#
# Пароли хранятся в backend/.env (файл в git не попадает) и печатаются один раз.
# Хэш — PBKDF2-HMAC-SHA256, 120 000 итераций, соль 16 байт (hex): то же, что проверяет сервер.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
. "$ROOT/scripts/env.sh"

ENV_FILE="$ROOT/backend/.env"
rotate=false
[ "${1:-}" = "--rotate" ] && rotate=true

upsert_env() {
  local key="$1" value="$2"
  touch "$ENV_FILE"
  if grep -qE "^${key}=" "$ENV_FILE"; then
    local tmp
    tmp="$(mktemp)"
    sed "s|^${key}=.*|${key}=${value}|" "$ENV_FILE" >"$tmp"
    mv "$tmp" "$ENV_FILE"
  else
    printf '%s=%s\n' "$key" "$value" >>"$ENV_FILE"
  fi
}

read_env() {
  grep -E "^$1=" "$ENV_FILE" 2>/dev/null | tail -1 | cut -d= -f2- || true
}

create_user() {
  local login="$1" role="$2" key="$3"
  local password
  password="$(read_env "$key")"
  if [ -z "$password" ] || [ "$rotate" = true ]; then
    password="$(openssl rand -hex 8)"
    upsert_env "$key" "$password"
    echo "  пароль $login: $password"
  fi

  local salt hash
  salt="$(openssl rand -hex 16)"
  hash="$(python3 - "$password" "$salt" <<'PY'
import hashlib, sys
password, salt = sys.argv[1], bytes.fromhex(sys.argv[2])
print(hashlib.pbkdf2_hmac('sha256', password.encode(), salt, 120000).hex())
PY
)"

  psql -v ON_ERROR_STOP=1 -q \
    -v login="$login" -v salt="$salt" -v hash="$hash" -v role="$role" <<'SQL'
INSERT INTO users (login, password_salt, password_hash, role)
VALUES (:'login', :'salt', :'hash', :'role')
ON CONFLICT (login) DO UPDATE
  SET password_salt = EXCLUDED.password_salt,
      password_hash = EXCLUDED.password_hash,
      role = EXCLUDED.role;
SQL
  echo "  пользователь $login ($role) готов"
}

echo "== пользователи =="
create_user "${REP_ADMIN_LOGIN:-admin}" admin REP_ADMIN_PASSWORD
create_user "${REP_VIEWER_LOGIN:-viewer}" viewer REP_VIEWER_PASSWORD

echo
echo "вход: POST /api/auth/login {login, password}, cookie rep_session"
echo "пароли — в backend/.env: REP_ADMIN_PASSWORD, REP_VIEWER_PASSWORD"
