#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
version=$(awk -F '"' '/^#define NFCIDM_VERSION / { print $2 }' "$root/src/nfcidm.h")
case "$version" in ''|*[!0-9A-Za-z.-]*) echo 'Invalid version' >&2; exit 1;; esac
work=$(mktemp -d "${TMPDIR:-/tmp}/nfcidm-release.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
name=nfcidm-$version
mkdir -p "$work/$name" "$root/dist"
for item in src tests scripts docs zadig Formula Makefile README.md LICENSE CHANGELOG.md; do
  cp -R "$root/$item" "$work/$name/"
done
# Source archive only: no build/, dist/, SDKs, toolchains, or binaries.
tar -cf "$work/source.tar" -C "$work" "$name"
gzip -n -c "$work/source.tar" >"$root/dist/$name.tar.gz"
if command -v shasum >/dev/null 2>&1; then
  hash=$(shasum -a 256 "$root/dist/$name.tar.gz" | awk '{print $1}')
else
  hash=$(sha256sum "$root/dist/$name.tar.gz" | awk '{print $1}')
fi
printf '%s  %s.tar.gz\n' "$hash" "$name" >"$root/dist/SHA256SUMS"
printf '%s\n' "$root/dist/$name.tar.gz"
