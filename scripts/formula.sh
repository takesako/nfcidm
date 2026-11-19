#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
  echo 'Usage: sh scripts/formula.sh ARCHIVE URL [HOMEPAGE]' >&2
  exit 64
fi
archive=$1
source_url=$2
homepage=${3:-https://github.com/OWNER/nfcidm}
root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
version=$(awk -F '"' '/^#define NFCIDM_VERSION / { print $2 }' "$root/src/nfcidm.h")
test -f "$archive" || { echo "Archive does not exist: $archive" >&2; exit 1; }
case "$source_url" in https://*|file:///*) ;; *) echo 'URL must use https:// or file:///' >&2; exit 1;; esac
case "$homepage" in https://*) ;; *) echo 'Homepage must use https://' >&2; exit 1;; esac
# Reject characters that could turn metadata into Ruby code. Percent-encode
# spaces in published URLs. Local Homebrew tap paths normally have no spaces.
for value in "$source_url" "$homepage" "$version"; do
  case "$value" in *[!A-Za-z0-9:/._?\&=%+~-]*) echo 'Unsupported URL/version character' >&2; exit 1;; esac
done
if command -v shasum >/dev/null 2>&1; then
  hash=$(shasum -a 256 "$archive" | awk '{print $1}')
else
  hash=$(sha256sum "$archive" | awk '{print $1}')
fi
while IFS= read -r line; do
  case "$line" in
    *'@HOMEPAGE@'*) printf '  homepage "%s"\n' "$homepage";;
    *'@URL@'*) printf '  url "%s"\n' "$source_url";;
    *'@VERSION@'*) printf '  version "%s"\n' "$version";;
    *'@SHA256@'*) printf '  sha256 "%s"\n' "$hash";;
    *) printf '%s\n' "$line";;
  esac
done <"$root/Formula/nfcidm.rb.in"
