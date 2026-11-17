#!/usr/bin/env bash
# Build the release (co-op mod standard J5, J8). Everything lands in local/release/ (ignored by git):
#   DeathStrandingCoop-Setup.exe (+ .sha256)        the single-file Setup, the main download: the zip's files embedded,
#                                                    checked against their SHA-256 before they are unpacked
#   deathstranding-coop-<version>.zip (+ .sha256)    the same files in one top folder, for a manual install or Nexus; its
#                                                    own DeathStrandingCoop-Setup.exe uses the files beside it
#   NEXUS-<version>.txt, PUBLISH-<version>.txt       the copy note for Nexus and the gh release command (never run here)
#   tools/release.sh                version from `git describe --tags --always --dirty`
#   tools/release.sh v0.1.0-alpha.1 explicit version (the tag)
# One version string everywhere: VERSIONINFO of every exe and dll, the launcher's About page, coop-mod.json, Setup's
# payload and the file names. Code signing: SIGN_CMD, a command that signs one file in place, {file} replaced by its
# quoted path (for example osslsigncode or signtool); every exe and dll is signed before it is hashed and embedded, the
# single-file Setup last. Unset: unsigned, with a warning (SmartScreen warns players; README says what to do).
# Publishing is a separate step with the owner's go: PUBLISH-<version>.txt.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
export SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-$(git -C "$here" log -1 --format=%ct)}"
export TZ=UTC
umask 022
ver="${1:-$(git -C "$here" describe --tags --always --dirty)}"
if [[ ! "$ver" =~ ^[A-Za-z0-9][A-Za-z0-9._+-]*$ ]]; then
    echo 'release: the version may contain only letters, digits, dot, underscore, plus or hyphen' >&2; exit 2
fi
name="deathstranding-coop-$ver"
out="${RELEASE_OUT:-$here/local/release}"
mkdir -p "$out"; out="$(cd "$out" && pwd)"
stage="$out/$name"; work="$out/work-$ver"
CC="${MINGW_CC:-x86_64-w64-mingw32-gcc}"
WINDRES="${WINDRES:-x86_64-w64-mingw32-windres}"

sign_files() {
    [ -n "${SIGN_CMD:-}" ] || return 0
    local f
    for f in "$@"; do
        bash -c "${SIGN_CMD//\{file\}/$(printf '%q' "$f")}" || { echo "release: signing failed for $f" >&2; exit 1; }
    done
}
[ -n "${SIGN_CMD:-}" ] || echo 'release: WARNING: SIGN_CMD is not set, so the exes and dlls are UNSIGNED (Windows SmartScreen will warn players).' >&2

rm -rf "$stage" "$work" "$out/$name.zip" "$out/$name.zip.sha256" "$out/DeathStrandingCoop-Setup.exe" "$out/DeathStrandingCoop-Setup.exe.sha256"
mkdir -p "$work"

# VERSIONINFO for every shipped binary, then everything built (make all) with them linked in.
res() {   # res <file name> <description> -> object path
    python3 -B "$here/tools/versioninfo.py" "$ver" "$1" "$2" "$work/$1.rc"
    "$WINDRES" "$work/$1.rc" -o "$work/$1-res.o"
    echo "$work/$1-res.o"
}
if [ -z "${RELEASE_PREBUILT:-}" ]; then
    avrt_res=$(res avrt.dll 'Death Stranding Co-op proxy')
    mod_res=$(res mod.dll 'Death Stranding Co-op mod')
    world_res=$(res coop-world.exe 'Death Stranding Co-op server')
    rm -f "$here/redirect/build/avrt.dll" "$here/mod/build/mod.dll" "$here/world/build/coop-world.exe" "$here/launcher/build/deathstranding-coop.exe"
    make -C "$here" all VERSION="$ver" AVRT_VERSION_RES="$avrt_res" MOD_VERSION_RES="$mod_res" WORLD_VERSION_RES="$world_res"
    bin=("$here/launcher/build/deathstranding-coop.exe" "$here/world/build/coop-world.exe" "$here/redirect/build/avrt.dll" "$here/mod/build/mod.dll")
else    # tools/test_release.py only: stand-in binaries from this folder instead of `make all`
    bin=("$RELEASE_PREBUILT/deathstranding-coop.exe" "$RELEASE_PREBUILT/coop-world.exe" "$RELEASE_PREBUILT/avrt.dll" "$RELEASE_PREBUILT/mod.dll")
fi

python3 -B "$here/tools/package.py" stage "$stage" "$ver" "${bin[@]}"

# The zip's Setup: no payload, it starts the scripts beside it.
python3 -B "$here/tools/versioninfo.py" "$ver" DeathStrandingCoop-Setup.exe 'Death Stranding Co-op Setup' "$work/setup-version.rc"
"$WINDRES" "$work/setup-version.rc" -o "$work/setup-version-res.o"
(cd "$here/dist" && "$WINDRES" installer.rc -o "$work/installer-res.o")
"$CC" -Os -Wall -Wextra -Werror -Wl,--no-insert-timestamp -mwindows -o "$stage/DeathStrandingCoop-Setup.exe" \
    "$here/dist/installer.c" "$work/installer-res.o" "$work/setup-version-res.o" -lshell32
sign_files "$stage/deathstranding-coop.exe" "$stage/coop-world.exe" "$stage/avrt.dll" "$stage/redirect-data/mod/mod.dll" "$stage/DeathStrandingCoop-Setup.exe"

# coop-mod.json of the signed bytes; then the single-file Setup with every file of the zip but its own Setup embedded.
python3 -B "$here/tools/mod_manifest.py" "$ver" "$stage" > "$stage/coop-mod.json"
mapfile -t args < <(python3 -B "$here/tools/package.py" payload-args "$stage")
python3 -B "$here/tools/payload.py" pack "$work" "$ver" "${args[@]}"
"$WINDRES" "$work/setup.rc" -o "$work/setup-res.o"
"$CC" -Os -Wall -Wextra -Werror -Wl,--no-insert-timestamp -mwindows -o "$out/DeathStrandingCoop-Setup.exe" \
    "$here/dist/installer.c" "$work/setup-res.o" "$work/setup-version-res.o" -lshell32
sign_files "$out/DeathStrandingCoop-Setup.exe"
python3 -B "$here/tools/package.py" sha256 "$out/DeathStrandingCoop-Setup.exe"
rm -rf "$work"

python3 -B "$here/tools/package.py" zip "$out" "$name"
python3 -B "$here/tools/package.py" notes "$out" "$ver"
python3 -B "$here/tools/check_release_package.py" "$out/$name.zip"
echo "$out/DeathStrandingCoop-Setup.exe ($(du -h "$out/DeathStrandingCoop-Setup.exe" | cut -f1))"
echo "$out/$name.zip ($(du -h "$out/$name.zip" | cut -f1))"
echo "$out/PUBLISH-$ver.txt (the gh release command; nothing was published)"
