#!/bin/sh
# install.sh: sets up coop-world on a rented Debian or Ubuntu server (README.md in this folder). Run as root on the
# server; deploy.sh copies it there with the binary and runs it. Running it again is safe: it keeps the world, the
# group's key and the owner's key, replaces the binary and the unit, and restarts the service.
#
#   sh install.sh --binary FILE [--admin FILE] [--port 49100] [--name TEXT] [--ssh-port N] [--fail2ban yes|no]
#   sh install.sh --print-unit [--conf FILE]         the systemd unit for a configuration (no root, nothing changed)
#
# Options (each only changes what it names; without it a value set before is kept):
#   --binary FILE   the coop-world built by `make -C world linux` (static, x86_64)
#   --admin FILE    coop-admin.sh, installed as /opt/coop-world/coop-admin (deploy.sh passes it)
#   --port N        the server's TCP and UDP port, 49100..49114 (the launcher's group ports); default 49100
#   --name TEXT     the world's display name (1..12 characters); default "Alpha"
#   --ssh-port N    the SSH port the firewall keeps open; default: the port sshd says it listens on, else 22
#   --fail2ban      yes: install fail2ban and ban addresses that keep failing SSH logins; default no
set -eu

STATE=/var/lib/coop-world
OPT=/opt/coop-world
ETC=/etc/coop-world
CONF=$ETC/coop-world.conf
UNIT=/etc/systemd/system/coop-world.service

say() { printf '%s\n' "install: $*"; }
die() { printf '%s\n' "install: $*" >&2; exit 1; }

# The unit, for the values in a configuration file. Every value is checked here as well, so a configuration edited by
# hand cannot put another option on the command line.
print_unit() {
    cat <<'EOF'
# coop-world: the testers' shared-world server and relay (deploy/vps/README.md). Written by install.sh.
[Unit]
Description=coop-world (Death Stranding co-op shared world and relay)
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=coopworld
Group=coopworld
EnvironmentFile=/etc/coop-world/coop-world.conf
WorkingDirectory=/var/lib/coop-world
# Public rules with identity keys, enforced at every start: nobody signs in, builds or withdraws in another's name.
ExecStart=/opt/coop-world/coop-world --port ${COOP_PORT} --bind 0.0.0.0 --data /var/lib/coop-world/world.json \
    --prefix w${COOP_KEY} --relay-port ${COOP_PORT} --relay-key w${COOP_KEY} \
    --policy public --policy-set identity=keys --owner ${COOP_OWNER} --name ${COOP_NAME} \
    --max-connections 64 --ip-connections 8 --ip-rate 20 --ip-burst 200 --ip-strikes 30 --ip-block 900
Restart=always
RestartSec=2
# Hardening: the process writes its state folder and nothing else.
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes
PrivateDevices=yes
ReadWritePaths=/var/lib/coop-world
StateDirectory=coop-world
StateDirectoryMode=0700
UMask=0077
ProtectKernelTunables=yes
ProtectKernelModules=yes
ProtectKernelLogs=yes
ProtectControlGroups=yes
ProtectClock=yes
ProtectHostname=yes
ProtectProc=invisible
RestrictAddressFamilies=AF_INET AF_UNIX
RestrictNamespaces=yes
RestrictRealtime=yes
RestrictSUIDSGID=yes
LockPersonality=yes
MemoryDenyWriteExecute=yes
SystemCallArchitectures=native
SystemCallFilter=@system-service
CapabilityBoundingSet=
AmbientCapabilities=
MemoryMax=512M
TasksMax=256
LimitNOFILE=1024

[Install]
WantedBy=multi-user.target
EOF
}

# KEY=VALUE from the configuration file (the last one wins), without running it.
conf_get() {
    [ -f "$2" ] || return 0
    sed -n "s/^$1=//p" "$2" | tail -n 1
}

check_conf() {
    port=$(conf_get COOP_PORT "$1"); key=$(conf_get COOP_KEY "$1"); owner=$(conf_get COOP_OWNER "$1"); name=$(conf_get COOP_NAME "$1")
    case $port in 491[0-1][0-9]) [ "$port" -ge 49100 ] && [ "$port" -le 49114 ] || die "COOP_PORT: 49100..49114";; *) die "COOP_PORT: 49100..49114";; esac
    case $key in ''|*[!0-9]*) die "COOP_KEY: a number 1..4294967295";; esac
    [ "${#key}" -le 10 ] && [ "$key" -ge 1 ] && [ "$key" -le 4294967295 ] || die "COOP_KEY: a number 1..4294967295"
    case $owner in *[!0-9a-f]*) die "COOP_OWNER: 64 lower-case hex digits";; esac
    [ "${#owner}" -eq 64 ] || die "COOP_OWNER: 64 lower-case hex digits"
    case $name in ''|*[!A-Za-z0-9_-]*) die "COOP_NAME: 1..12 letters, digits, - and _";; esac
    [ "${#name}" -le 12 ] || die "COOP_NAME: 1..12 letters, digits, - and _"
}

binary='' admin='' port='' name='' ssh_port='' fail2ban=no mode=install conf=$CONF
while [ $# -gt 0 ]; do
    case $1 in
        --binary) binary=$2; shift 2;;
        --admin) admin=$2; shift 2;;
        --port) port=$2; shift 2;;
        --name) name=$2; shift 2;;
        --ssh-port) ssh_port=$2; shift 2;;
        --fail2ban) fail2ban=$2; shift 2;;
        --print-unit) mode=unit; shift;;
        --conf) conf=$2; shift 2;;
        *) die "unknown option $1 (see the top of this file)";;
    esac
done

if [ "$mode" = unit ]; then
    [ -f "$conf" ] && check_conf "$conf"
    print_unit; exit 0
fi

[ "$(id -u)" -eq 0 ] || die "run as root (sudo sh install.sh ...)"
command -v apt-get >/dev/null 2>&1 || die "this is for Debian or Ubuntu (apt-get not found)"
command -v systemctl >/dev/null 2>&1 || die "systemd is required"
[ "$(uname -m)" = x86_64 ] || die "the binary is built for x86_64; this machine is $(uname -m)"
[ -n "$binary" ] && [ -f "$binary" ] || die "--binary FILE: the coop-world built by make -C world linux"
case $fail2ban in yes|no) ;; *) die "--fail2ban yes or no";; esac
if [ -z "$ssh_port" ]; then
    ssh_port=$(sshd -T 2>/dev/null | sed -n 's/^port //p' | head -n 1)
    [ -n "$ssh_port" ] || ssh_port=22
fi
case $ssh_port in ''|*[!0-9]*) die "--ssh-port: a port number";; esac

case $port in ''|491[0-1][0-9]) ;; *) die "--port: 49100..49114";; esac
[ -z "$port" ] || { [ "$port" -ge 49100 ] && [ "$port" -le 49114 ]; } || die "--port: 49100..49114"
case $name in *[!A-Za-z0-9_-]*) die "--name: 1..12 letters, digits, - and _";; esac
[ "${#name}" -le 12 ] || die "--name: 1..12 letters, digits, - and _"
# The binary must run here, and be this version (it has keygen), before anything is changed.
file_type=$(od -An -c -N4 "$binary" | tr -d ' ')
[ "$file_type" = '177ELF' ] || die "the binary is not a Linux executable"
"$binary" keygen 2>&1 | grep -q 'coop-world keygen --out' || die "the binary does not run here, or is a coop-world from before keygen"

say "packages: ufw, unattended-upgrades$( [ "$fail2ban" = yes ] && echo ', fail2ban')"
export DEBIAN_FRONTEND=noninteractive
apt-get update -q
apt-get install -y -q ufw unattended-upgrades
[ "$fail2ban" = yes ] && apt-get install -y -q fail2ban

say "user coopworld and folders"
getent group coopworld >/dev/null || groupadd --system coopworld
id coopworld >/dev/null 2>&1 || useradd --system --gid coopworld --home-dir "$STATE" --no-create-home --shell /usr/sbin/nologin coopworld
install -d -m 0755 -o root -g root "$OPT"
install -d -m 0700 -o coopworld -g coopworld "$STATE"
install -d -m 0750 -o root -g coopworld "$ETC"

say "binary"
install -m 0755 -o root -g root "$binary" "$OPT/coop-world.new"
mv -f "$OPT/coop-world.new" "$OPT/coop-world"
if [ -n "$admin" ]; then
    install -m 0750 -o root -g root "$admin" "$OPT/coop-admin.new"
    mv -f "$OPT/coop-admin.new" "$OPT/coop-admin"
fi

# The owner's key: made once on this server, never leaves it (root only). Its calls are the owner's (coop-admin).
if [ ! -f "$ETC/owner.key" ]; then
    say "owner key (made once)"
    "$OPT/coop-world" keygen --out "$ETC/owner.key" >/dev/null
fi
chown root:root "$ETC/owner.key"; chmod 0600 "$ETC/owner.key"
owner=$("$OPT/coop-world" pubkey --key "$ETC/owner.key")

# The configuration: the group's key is made once (a random number 1..4294967295, as the launcher makes it); the
# port and the name only change when given.
old_key=$(conf_get COOP_KEY "$CONF"); old_port=$(conf_get COOP_PORT "$CONF"); old_name=$(conf_get COOP_NAME "$CONF")
key=$old_key
while [ -z "$key" ] || [ "$key" = 0 ]; do key=$(od -An -tu4 -N4 /dev/urandom | tr -d ' '); done
[ -n "$port" ] || port=${old_port:-49100}
[ -n "$name" ] || name=${old_name:-Alpha}
tmp=$(mktemp "$ETC/conf.XXXXXX")
cat >"$tmp" <<EOF
# coop-world on this server (deploy/vps/README.md). Written by install.sh; coop-admin changes COOP_KEY.
# COOP_KEY is the group's key: the server's secret path element and relay key are "w" and it. Whoever has the
# invite has it. COOP_OWNER is the public half of $ETC/owner.key.
COOP_PORT=$port
COOP_KEY=$key
COOP_NAME=$name
COOP_OWNER=$owner
EOF
( check_conf "$tmp" ) || { rm -f "$tmp"; exit 1; }
chown root:coopworld "$tmp"; chmod 0640 "$tmp"; mv -f "$tmp" "$CONF"

say "systemd unit"
print_unit >"$UNIT.new"; mv -f "$UNIT.new" "$UNIT"; chmod 0644 "$UNIT"

say "journald: at most 200 MB of logs, kept 14 days (the log names players' addresses)"
install -d -m 0755 /etc/systemd/journald.conf.d
cat >/etc/systemd/journald.conf.d/coop-world.conf <<'EOF'
# Written by coop-world's install.sh: the server's log holds players' IP addresses and nicknames; keep it short.
[Journal]
SystemMaxUse=200M
MaxRetentionSec=14day
EOF
systemctl restart systemd-journald

say "automatic security updates"
cat >/etc/apt/apt.conf.d/20auto-upgrades <<'EOF'
APT::Periodic::Update-Package-Lists "1";
APT::Periodic::Unattended-Upgrade "1";
EOF
systemctl enable --now unattended-upgrades >/dev/null 2>&1 || true

if [ "$fail2ban" = yes ]; then
    say "fail2ban for sshd"
    cat >/etc/fail2ban/jail.d/coop-world-sshd.local <<EOF
# Written by coop-world's install.sh.
[sshd]
enabled = true
port = $ssh_port
backend = systemd
maxretry = 5
bantime = 1h
EOF
    systemctl enable fail2ban >/dev/null 2>&1 || true
    systemctl restart fail2ban
fi

say "firewall: SSH ($ssh_port/tcp) and the server ($port/tcp, $port/udp) only"
# SSH first, so enabling the firewall never cuts this session off.
ufw allow "$ssh_port/tcp" comment 'ssh' >/dev/null
ufw default deny incoming >/dev/null
ufw default allow outgoing >/dev/null
# A port used before (the server moved): closed again.
if [ -n "$old_port" ] && [ "$old_port" != "$port" ]; then
    ufw delete allow "$old_port/tcp" >/dev/null 2>&1 || true
    ufw delete allow "$old_port/udp" >/dev/null 2>&1 || true
fi
ufw allow "$port/tcp" comment 'coop-world' >/dev/null
ufw allow "$port/udp" comment 'coop-world relay' >/dev/null
ufw --force enable >/dev/null

say "service"
systemctl daemon-reload
systemctl enable coop-world >/dev/null 2>&1
systemctl restart coop-world
sleep 2
if ! systemctl is-active --quiet coop-world; then
    journalctl -u coop-world -n 30 --no-pager >&2 || true
    die "coop-world did not start (the lines above say why)"
fi
# The server answers its rules on loopback: it is up and its prefix is the group's.
if command -v curl >/dev/null 2>&1; then
    curl -fsS "http://127.0.0.1:$port/w$key/coop/policy" >/dev/null || die "coop-world runs but does not answer on port $port"
fi
say "done"
# One line for deploy.sh (the key is the group's secret: it goes into the invite only).
printf 'COOP_INVITE_INFO port=%s key=%s name=%s\n' "$port" "$key" "$name"
