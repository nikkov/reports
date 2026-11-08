#!/usr/bin/env bash
# Единая точка версии: файл VERSION. Скрипт печатает версию и проверяет,
# что её не разъехались копии в README, CHANGELOG, package.json и теге.
#
#   ./scripts/version.sh          # показать версию и расхождения
#   ./scripts/version.sh --check  # только проверка: код 1 при расхождении
set -uo pipefail
cd "$(dirname "$0")/.."

CHECK_ONLY=0
[ "${1:-}" = "--check" ] && CHECK_ONLY=1

[ -f VERSION ] || { echo "нет файла VERSION" >&2; exit 1; }
VERSION="$(tr -d ' \t\r\n' < VERSION)"
FAILS=0
fail() { echo "  ✗ $*" >&2; FAILS=$((FAILS + 1)); }

[ "$CHECK_ONLY" -eq 0 ] && echo "версия: $VERSION"

# README: первая строка «# Report Server — версия X»
README_VERSION="$(head -1 README.md | grep -oP 'версия \K[0-9]+\.[0-9]+\.[0-9]+' || true)"
[ "$README_VERSION" = "$VERSION" ] || fail "README: «${README_VERSION:-нет}», ожидалась $VERSION"

# CHANGELOG: первый раздел «## vX»
CHANGELOG_VERSION="$(grep -m1 -oP '^## v\K[0-9]+\.[0-9]+\.[0-9]+' CHANGELOG.md || true)"
[ "$CHANGELOG_VERSION" = "$VERSION" ] || fail "CHANGELOG: «${CHANGELOG_VERSION:-нет}», ожидалась $VERSION"

# frontend/package.json
PKG_VERSION="$(node -p "require('./frontend/package.json').version" 2>/dev/null || echo '')"
[ "$PKG_VERSION" = "$VERSION" ] || fail "frontend/package.json: «${PKG_VERSION:-нет}», ожидалась $VERSION"

# git-тег на HEAD (в CI тега может не быть — это не ошибка, только подсказка)
if git rev-parse --git-dir >/dev/null 2>&1; then
  TAG_AT_HEAD="$(git tag --points-at HEAD | grep -x "v$VERSION" || true)"
  LAST_TAG="$(git tag --sort=-creatordate | head -1)"
  if [ -z "$TAG_AT_HEAD" ]; then
    if [ "$CHECK_ONLY" -eq 1 ]; then
      echo "  ! HEAD не помечен тегом v$VERSION (последний тег: ${LAST_TAG:-нет})" >&2
    else
      echo "  ! HEAD не помечен тегом v$VERSION (последний тег: ${LAST_TAG:-нет})"
    fi
  fi
fi

if [ "$FAILS" -eq 0 ]; then
  [ "$CHECK_ONLY" -eq 1 ] || echo "  ✓ README, CHANGELOG, package.json совпадают"
  exit 0
fi
echo "версия расходится в $FAILS месте(ах)" >&2
exit 1
