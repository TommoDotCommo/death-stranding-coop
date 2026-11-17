#!/bin/sh
# admin.sh: the owner's commands for the rented server, from this PC over SSH (README.md in this folder). Each runs
# coop-admin as root on the server; the owner's actions are signed calls to the server on its own loopback.
#
#   deploy/vps/admin.sh HOST status | logs [N] | follow | restart
#   deploy/vps/admin.sh HOST players | kick ID | ban ID | unban ID | remove ID | member ID yes|no
#   deploy/vps/admin.sh HOST review | accept OBJECT | reject OBJECT | accept-player ID
#   deploy/vps/admin.sh HOST policy | policy-set KEY=VALUE...
#   deploy/vps/admin.sh HOST rotate-key [--ip A.B.C.D]     a new group key, and the new invite (the old one stops working)
#   deploy/vps/admin.sh HOST invite [--ip A.B.C.D]         the current invite again
#   deploy/vps/admin.sh HOST block-ip A.B.C.D | unblock-ip A.B.C.D
#   deploy/vps/admin.sh HOST backup FILE                   the world file, copied here (it holds the players' keys and
#                                                          passes: keep it private)
set -eu
here=$(cd "$(dirname "$0")" && pwd)
die() { printf '%s\n' "admin: $*" >&2; exit 2; }
[ "$#" -ge 2 ] || die "usage: admin.sh HOST COMMAND [ARGS] (see the top of this file)"
host=$1; command=$2; shift 2
case $host in -*) die "HOST first";; esac

ip=''
args=''
while [ "$#" -gt 0 ]; do
    if [ "$1" = --ip ] && [ "$#" -ge 2 ]; then ip=$2; shift 2; continue; fi
    # Only plain words travel to the server's shell.
    case $1 in ''|*[!A-Za-z0-9_.:=-]*) die "'$1': letters, digits, _ . : = - only";; esac
    args="$args $1"; shift
done

sudo=sudo
[ "$(ssh "$host" id -u)" = 0 ] && sudo=''
remote() { ssh "$host" "$sudo /opt/coop-world/coop-admin $*"; }

invite() {
    info=$1
    if [ -z "$ip" ]; then
        name=$(ssh -G "$host" 2>/dev/null | awk '$1=="hostname"{print $2; exit}')
        if printf '%s' "$name" | grep -Eq '^([0-9]{1,3}\.){3}[0-9]{1,3}$'; then ip=$name
        else ip=$(getent ahostsv4 "$name" 2>/dev/null | awk 'NR==1{print $1}'); fi
        [ -n "$ip" ] || die "cannot tell the server's IPv4 address: give --ip A.B.C.D"
    fi
    make -s -C "$here" build/vps-invite
    port=$(printf '%s\n' "$info" | sed -n 's/.*port=\([0-9]*\).*/\1/p')
    key=$(printf '%s\n' "$info" | sed -n 's/.*key=\([0-9]*\).*/\1/p')
    [ -n "$port" ] && [ -n "$key" ] || die "the server did not say its port and key"
    printf 'Group invite (send it privately):\n\n    %s\n' "$("$here/build/vps-invite" "$key" "$port" "$ip")"
}

case $command in
    invite) invite "$(remote info | tr -d '\r' | grep '^COOP_INVITE_INFO')";;
    rotate-key)
        info=$(remote rotate-key | tr -d '\r' | grep '^COOP_INVITE_INFO') || die "rotate-key failed: admin.sh $host logs"
        echo "New group key: the old invite no longer works. Players keep their keys, builds and choices."
        invite "$info";;
    backup)
        # shellcheck disable=SC2086 # one checked word
        set -- $args
        [ "$#" -eq 1 ] || die "backup FILE"
        [ ! -e "$1" ] || die "$1 exists"
        (umask 077; remote backup >"$1")
        echo "world file saved to $1 (private: it holds the players' keys and passes)";;
    follow) ssh -t "$host" "$sudo /opt/coop-world/coop-admin follow";;
    *) remote "$command$args";;
esac
