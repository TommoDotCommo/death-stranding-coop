#!/bin/sh
# deploy.sh: builds coop-world for Linux on this PC, copies it to a rented server over SSH, installs or updates it
# there (install.sh) and prints the group invite for the testers (README.md in this folder).
#
#   deploy/vps/deploy.sh HOST [--ip A.B.C.D] [--port 49100] [--name TEXT] [--ssh-port N] [--fail2ban yes|no]
#
# HOST is what `ssh HOST` reaches (user@address, or a name from ~/.ssh/config); the user is root or may sudo.
# --ip is the server's public IPv4 address for the invite (default: the address HOST resolves to). The other options
# go to install.sh. Running it again updates the binary and keeps the world, the group's key and the owner's key.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
die() { printf '%s\n' "deploy: $*" >&2; exit 1; }
[ "$#" -ge 1 ] || die "usage: deploy.sh HOST [--ip A.B.C.D] [--port N] [--name TEXT] [--ssh-port N] [--fail2ban yes|no]"
host=$1; shift
case $host in -*) die "HOST first";; esac
ip='' install_args=''
while [ "$#" -gt 0 ]; do
    [ "$#" -ge 2 ] || die "$1 needs a value"
    case $1 in
        --ip) ip=$2;;
        --port|--name|--ssh-port|--fail2ban)
            case $2 in ''|*[!A-Za-z0-9_-]*) die "$1: letters, digits, - and _ only";; esac
            install_args="$install_args $1 $2";;
        *) die "unknown option $1";;
    esac
    shift 2
done

echo "deploy: building coop-world for Linux (make -C world linux) and the invite tool"
make -s -C "$root/world" linux
make -s -C "$here" build/vps-invite
binary=$root/world/build/coop-world-linux
if command -v file >/dev/null 2>&1; then
    file "$binary" | grep -q 'x86-64.*statically linked' || die "$binary is not a static x86-64 executable"
fi

# The address for the invite: given, else what HOST resolves to.
if [ -z "$ip" ]; then
    name=$(ssh -G "$host" 2>/dev/null | awk '$1=="hostname"{print $2; exit}')
    [ -n "$name" ] || name=${host#*@}
    if printf '%s' "$name" | grep -Eq '^([0-9]{1,3}\.){3}[0-9]{1,3}$'; then ip=$name
    else ip=$(getent ahostsv4 "$name" 2>/dev/null | awk 'NR==1{print $1}'); fi
    [ -n "$ip" ] || die "cannot tell the server's IPv4 address: give --ip A.B.C.D"
fi
"$here/build/vps-invite" 1 49100 "$ip" >/dev/null || die "$ip is not a public IPv4 address (the invite is for testers elsewhere): give --ip"

echo "deploy: copying to $host"
remote=$(ssh "$host" 'mktemp -d /tmp/coop-world-deploy.XXXXXX')
case $remote in /tmp/coop-world-deploy.*) ;; *) die "unexpected answer from the server: $remote";; esac
scp -q "$binary" "$host:$remote/coop-world"
scp -q "$here/install.sh" "$host:$remote/install.sh"
scp -q "$here/coop-admin.sh" "$host:$remote/coop-admin.sh"

sudo=sudo
[ "$(ssh "$host" id -u)" = 0 ] && sudo=''
log=$(mktemp)
trap 'rm -f "$log"' EXIT
echo "deploy: installing (install.sh as root on $host)"
# -t: sudo may ask for a password. The output is kept to read the invite line from it.
# install_args are checked words; the server's shell splits them.
ssh -t "$host" "$sudo sh $remote/install.sh --binary $remote/coop-world --admin $remote/coop-admin.sh $install_args; status=\$?; rm -rf $remote; exit \$status" | tee "$log"
info=$(tr -d '\r' <"$log" | sed -n 's/^COOP_INVITE_INFO //p' | tail -n 1)
[ -n "$info" ] || die "install.sh did not finish (see above)"
port=$(printf '%s\n' "$info" | sed -n 's/.*port=\([0-9]*\).*/\1/p')
key=$(printf '%s\n' "$info" | sed -n 's/.*key=\([0-9]*\).*/\1/p')
invite=$("$here/build/vps-invite" "$key" "$port" "$ip")
cat <<EOF

coop-world runs on $ip, port $port (TCP and UDP), Public rules with player keys.

Group invite for the testers (send it privately; whoever has it can join):

    $invite

Testers: Join a friend, paste the invite, Join group (docs/TESTERS.md).
Administration: deploy/vps/admin.sh $host status | players | kick ID | ban ID | rotate-key | ... (README.md here).
EOF
