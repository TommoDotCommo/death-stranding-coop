#!/bin/sh
# The test save pack for a release: deathstranding-coop-test-saves-<version>.zip and its .sha256 (sha256sum format),
# a separate download beside the release. The saves themselves stay outside the repository.
#
#   TEST_SAVES_DIR=/path/to/sets tools/build_test_saves.sh [OUT_DIR]
#
# TEST_SAVES_DIR holds sets.json and one folder of checkpoint folders per set (tools/make_test_saves.py). VERSION
# overrides the version, which is otherwise the tag (`git describe`, without a leading "v"). OUT_DIR defaults to
# build/release. A release script calls this when TEST_SAVES_DIR is set.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
: "${TEST_SAVES_DIR:?set TEST_SAVES_DIR to the folder with sets.json and the sets (kept outside the repository)}"
out=${1:-$root/build/release}
version=${VERSION:-$(git -C "$root" describe --tags --always --dirty)}
version=${version#v}
case $version in ''|*/*|*' '*) echo "build_test_saves: unusable version '$version'" >&2; exit 2;; esac
case $(cd "$TEST_SAVES_DIR" && pwd)/ in "$root"/*) echo "build_test_saves: TEST_SAVES_DIR is inside the repository; keep saves outside it" >&2; exit 2;; esac
mkdir -p "$out"
name=deathstranding-coop-test-saves-$version.zip
python3 -B "$root/tools/make_test_saves.py" "$TEST_SAVES_DIR" "$out/$name" --version "$version"
(cd "$out" && sha256sum "$name" > "$name.sha256")
echo "$out/$name"
