#!/bin/sh
# coop-admin: the owner's commands on the server (README.md in this folder). install.sh puts it at
# /opt/coop-world/coop-admin; admin.sh runs it over SSH as root. The owner's actions are calls to the running server
# on 127.0.0.1, signed with the owner's key that install.sh made on this server (coop-world call); nothing here listens
# on the network.
#
#   coop-admin status | logs [N] | follow | restart | info
#   coop-admin players | kick ID | ban ID | unban ID | remove ID | member ID yes|no
#   coop-admin review | accept OBJECT | reject OBJECT | accept-player ID
#   coop-admin policy | policy-set KEY=VALUE... | rotate-key | block-ip ADDRESS | unblock-ip ADDRESS | backup
#
# ID is a player id as `players` lists it (zygo_...). For tests, COOP_CONF, COOP_BIN and COOP_OWNER_KEY replace the
# installed paths.
set -eu
CONF=${COOP_CONF:-/etc/coop-world/coop-world.conf}
BIN=${COOP_BIN:-/opt/coop-world/coop-world}
KEY=${COOP_OWNER_KEY:-/etc/coop-world/owner.key}
STATE=/var/lib/coop-world

die() { printf '%s\n' "coop-admin: $*" >&2; exit 2; }
conf_get() { sed -n "s/^$1=//p" "$CONF" | tail -n 1; }
[ -r "$CONF" ] || die "no configuration at $CONF (run install.sh first)"
PORT=$(conf_get COOP_PORT); GROUP=$(conf_get COOP_KEY)

# One argument: letters, digits and _ . : = - only (ids, keys, addresses), so nothing reaches JSON or a shell unquoted.
word() { case $1 in ''|*[!A-Za-z0-9_.:=-]*) die "'$1': letters, digits, _ . : = - only";; esac; }
ipv4() { printf '%s' "$1" | grep -Eq '^([0-9]{1,3}\.){3}[0-9]{1,3}$' || die "'$1': a numeric IPv4 address"; }
call() { "$BIN" call --key "$KEY" --port "$PORT" --prefix "w$GROUP" "$@"; }
pretty() { if command -v python3 >/dev/null 2>&1; then python3 -m json.tool; else cat; fi; }
# need N "$@": at least N arguments after the command.
need() { [ "$(($# - 1))" -ge "$1" ] || die "usage: see the top of $0"; }

[ "$#" -ge 1 ] || die "usage: see the top of $0"
command=$1; shift
case $command in
    status)
        systemctl status coop-world --no-pager --lines=5 || true
        if command -v ufw >/dev/null 2>&1; then ufw status verbose || true; fi
        printf 'players: '; call GET owner/players | grep -o '"id":' | wc -l;;
    logs) n=${1:-200}; case $n in *[!0-9]*) die "logs N";; esac; journalctl -u coop-world -n "$n" --no-pager;;
    follow) exec journalctl -u coop-world -f;;
    restart) systemctl restart coop-world; sleep 1; systemctl is-active coop-world;;
    info) printf 'COOP_INVITE_INFO port=%s key=%s name=%s\n' "$PORT" "$GROUP" "$(conf_get COOP_NAME)";;
    players) call GET owner/players | pretty;;
    kick) need 1 "$@"; word "$1"; call POST owner/kick "{\"player\":\"$1\"}";;
    ban) need 1 "$@"; word "$1"; call POST owner/ban "{\"player\":\"$1\",\"ban\":\"yes\"}" >/dev/null && echo "banned $1 (and kicked)";;
    unban) need 1 "$@"; word "$1"; call POST owner/ban "{\"player\":\"$1\",\"ban\":\"no\"}" >/dev/null && echo "ban lifted for $1";;
    remove) need 1 "$@"; word "$1"; call POST owner/remove-player "{\"player\":\"$1\"}";;
    member) need 2 "$@"; word "$1"; case $2 in yes|no) ;; *) die "member ID yes|no";; esac
        call POST owner/member "{\"player\":\"$1\",\"member\":\"$2\"}" >/dev/null && echo "member $1: $2";;
    review) call GET owner/review | pretty;;
    accept|reject) need 1 "$@"; word "$1"; call POST owner/review "{\"$command\":\"$1\"}";;
    accept-player) need 1 "$@"; word "$1"; call POST owner/review "{\"accept_player\":\"$1\"}";;
    policy) call GET policy | pretty;;
    policy-set)
        need 1 "$@"; list=''
        for kv in "$@"; do
            word "$kv"
            case $kv in identity=*) die "identity stays keys on this server (the unit sets it at every start)";; *=*) ;; *) die "policy-set KEY=VALUE...";; esac
            list="$list${list:+,}\"$kv\""
        done
        call POST owner/policy "{\"set\":[$list]}" | pretty;;
    rotate-key)
        # A new group key: the old invite stops working at once; players keep their keys, objects and choices.
        new=''; while [ -z "$new" ] || [ "$new" = 0 ] || [ "$new" = "$GROUP" ]; do new=$(od -An -tu4 -N4 /dev/urandom | tr -d ' '); done
        tmp=$(mktemp "$(dirname "$CONF")/conf.XXXXXX")
        sed "s/^COOP_KEY=.*/COOP_KEY=$new/" "$CONF" >"$tmp"; chmod --reference="$CONF" "$tmp"; chown --reference="$CONF" "$tmp"; mv -f "$tmp" "$CONF"
        systemctl restart coop-world; sleep 1; systemctl is-active --quiet coop-world || die "coop-world did not start again: coop-admin logs"
        printf 'COOP_INVITE_INFO port=%s key=%s name=%s\n' "$PORT" "$new" "$(conf_get COOP_NAME)";;
    block-ip) need 1 "$@"; ipv4 "$1"; ufw insert 1 deny from "$1" comment 'coop-admin block-ip';;
    unblock-ip) need 1 "$@"; ipv4 "$1"; ufw delete deny from "$1";;
    backup) cat "$STATE/world.json";;
    *) die "unknown command $command (see the top of $0)";;
esac
