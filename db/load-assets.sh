#!/usr/bin/env bash
# Загрузка рисунков примеров в БД (ADR-169): идемпотентно, без запущенного сервера.
#
#   ./db/load-assets.sh          — залить все файлы из db/examples/assets/
#   ./db/load-assets.sh --list   — что лежит в файлах и что уже в БД
#
# Идентификатор рисунка = имя файла без расширения (16 символов [a-z0-9]): шаблон
# примера ссылается на него как /api/assets/{id}, поэтому имя файла менять нельзя.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
. "$ROOT/scripts/env.sh"

DB="${REP_DB:-${PGDATABASE:-reporting}}"
DIR="$ROOT/db/examples/assets"
PSQL=(psql -v ON_ERROR_STOP=1 -q -d "$DB" -t -A)

die() { echo "load-assets: $*" >&2; exit 1; }
command -v psql >/dev/null || die "нужен psql (пакет postgresql-client)"
command -v python3 >/dev/null || die "нужен python3 (размеры PNG читаем им)"

if [ "${1:-}" = "--list" ]; then
  echo "в файлах:"
  for f in "$DIR"/*.png; do [ -e "$f" ] && echo "  $(basename "$f" .png)"; done
  echo "в БД:"
  "${PSQL[@]}" -c "SELECT '  ' || id FROM report_assets ORDER BY id" 2>/dev/null || true
  exit 0
fi

[ -d "$DIR" ] || die "нет каталога $DIR"
n=0
for f in "$DIR"/*.png; do
  [ -e "$f" ] || continue
  id="$(basename "$f" .png)"
  [[ "$id" =~ ^[a-z0-9]{16}$ ]] || die "имя файла '$id' не похоже на id рисунка (16 символов [a-z0-9])"
  read -r width height < <(python3 - "$f" <<'PY'
import struct, sys
head = open(sys.argv[1], 'rb').read(24)
if head[:8] != b'\x89PNG\r\n\x1a\n':
    sys.exit('не PNG: ' + sys.argv[1])
w, h = struct.unpack('>II', head[16:24])
print(w, h)
PY
)
  # xxd есть не везде: hex-строку делает python3, он всё равно нужен для размеров
  hexdata="$(python3 -c 'import binascii,sys;print(binascii.hexlify(open(sys.argv[1],"rb").read()).decode())' "$f")"
  "${PSQL[@]}" -c "
    INSERT INTO report_assets (id, name, mime, size, width, height, bytes, created_by)
    VALUES ('$id', '$(basename "$f")', 'image/png', ${#hexdata}/2, $width, $height,
            decode('$hexdata', 'hex'), 'example')
    ON CONFLICT (id) DO UPDATE SET
        name = EXCLUDED.name, mime = EXCLUDED.mime, size = EXCLUDED.size,
        width = EXCLUDED.width, height = EXCLUDED.height, bytes = EXCLUDED.bytes"
  n=$((n + 1))
  echo "  $id — ${width}×${height}, $((${#hexdata} / 2)) байт"
done
echo "рисунков загружено: $n"
