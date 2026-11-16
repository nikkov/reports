#!/usr/bin/env bash
# Локальная инфраструктура Kerberos для проверки REST-источников (ADR-122).
#
# Поднимает изолированный KDC без root (каталог /tmp/rep-krb, порт 10088), создаёт
# principals HTTP/localhost (keytab) и demo, выдаёт тикет и запускает SPNEGO-демо :8098.
# Повторный запуск ничего не ломает: KDC и демо переиспользуются.
#
#   ./scripts/dev-kerberos.sh
#
# Остановка (PID-файлы в /tmp/rep-krb):
#   for f in /tmp/rep-krb/*.pid; do [ -f "$f" ] && kill "$(cat "$f")"; done
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIR=/tmp/rep-krb
REALM=REP.LOCAL
PORT=10088
SPNEGO_PORT=8098

command -v kinit >/dev/null 2>&1 || {
  echo "нужен krb5-user: sudo apt-get install -y krb5-kdc krb5-admin-server krb5-user python3-gssapi" >&2
  exit 2
}
[ -x /usr/sbin/krb5kdc ] || {
  echo "нужен krb5-kdc: sudo apt-get install -y krb5-kdc krb5-admin-server" >&2
  exit 2
}

mkdir -p "$DIR"
cat > "$DIR/krb5.conf" <<EOF
[libdefaults]
    default_realm = $REALM
    dns_lookup_realm = false
    dns_lookup_kdc = false
    rdns = false
    default_ccache_name = FILE:$DIR/ccache

[realms]
    $REALM = {
        kdc = 127.0.0.1:$PORT
        admin_server = 127.0.0.1:10749
    }

[domain_realm]
    .localhost = $REALM
    localhost = $REALM
EOF

cat > "$DIR/kdc.conf" <<EOF
[kdcdefaults]
    kdc_ports = $PORT
    kdc_tcp_ports = $PORT

[realms]
    $REALM = {
        database_name = $DIR/principal
        admin_keytab = $DIR/kadm5.keytab
        acl_file = $DIR/kadm5.acl
        key_stash_file = $DIR/.k5.$REALM
        max_life = 10h
        max_renewable_life = 7d
    }
EOF

export KRB5_CONFIG="$DIR/krb5.conf" KRB5_KDC_PROFILE="$DIR/kdc.conf"

if [ ! -f "$DIR/principal" ]; then
  /usr/sbin/kdb5_util create -s -r "$REALM" -P master-pass >/dev/null
  /usr/sbin/kadmin.local -r "$REALM" -q "addprinc -randkey HTTP/localhost@$REALM" >/dev/null
  /usr/sbin/kadmin.local -r "$REALM" -q "ktadd -k $DIR/http.keytab HTTP/localhost@$REALM" >/dev/null
  /usr/sbin/kadmin.local -r "$REALM" -q "addprinc -pw demo-pass demo@$REALM" >/dev/null
  chmod 600 "$DIR/http.keytab"
  echo "KDC создан в $DIR"
fi

if [ -f "$DIR/kdc.pid" ] && kill -0 "$(cat "$DIR/kdc.pid")" 2>/dev/null; then
  echo "KDC уже работает"
else
  /usr/sbin/krb5kdc -n &
  echo $! > "$DIR/kdc.pid"
  sleep 1
fi

echo demo-pass | kinit "demo@$REALM" >/dev/null 2>&1 || true
klist -s || { echo "kinit не получил тикет" >&2; exit 3; }

if [ -f "$DIR/spnego.pid" ] && kill -0 "$(cat "$DIR/spnego.pid")" 2>/dev/null; then
  echo "SPNEGO-демо уже работает"
else
  KRB5_CONFIG="$KRB5_CONFIG" KRB5_KTNAME="$DIR/http.keytab" SPNEGO_PORT="$SPNEGO_PORT" \
    python3 "$ROOT/scripts/rest-spnego-demo.py" >/tmp/spnego.log 2>&1 &
  echo $! > "$DIR/spnego.pid"
  sleep 1
fi

echo "KDC: 127.0.0.1:$PORT ($REALM), тикет demo получен"
echo "SPNEGO: http://localhost:$SPNEGO_PORT/api/negotiated"
echo "В шаблоне: auth: {type: kerberos, spn: HTTP@localhost, ccache: $DIR/ccache, krb5Config: $KRB5_CONFIG}"
