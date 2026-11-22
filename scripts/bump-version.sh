#!/bin/sh
# SPDX-License-Identifier: MIT
# Usage: make bump [VERSION=0.5.5]
set -eu

if [ "$#" -gt 1 ]; then
    echo 'Usage: scripts/bump-version.sh [MAJOR.MINOR.PATCH]' >&2
    exit 2
fi

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
old=$(sed -n 's/^#define NFCIDM_VERSION "\([^"]*\)"$/\1/p' src/nfcidm.h)
if ! printf '%s\n' "$old" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$'; then
    echo "Invalid current version: $old" >&2
    exit 1
fi

new=${1:-}
if [ -z "$new" ]; then
    new=$(printf '%s\n' "$old" | awk -F. '{ printf "%d.%d.%d", $1, $2, $3 + 1 }')
else
    new=${new#v}
fi
if ! printf '%s\n' "$new" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$'; then
    echo "Invalid new version: $new (expected MAJOR.MINOR.PATCH)" >&2
    exit 2
fi
if [ "$old" = "$new" ]; then
    echo "Already at $new"
    exit 0
fi

escaped_old=$(printf '%s\n' "$old" | sed 's/\./\\./g')
for file in src/nfcidm.h tests/test_cli.sh Makefile README.md; do
    if [ -f "$file" ] && grep -Fq "$old" "$file"; then
        tmp=$(mktemp "${TMPDIR:-/tmp}/nfcidm-bump.XXXXXX")
        sed "s/$escaped_old/$new/g" "$file" > "$tmp"
        cat "$tmp" > "$file"
        rm -f "$tmp"
        printf 'Updated %s\n' "$file"
    fi
done

printf 'Version: %s -> %s\n' "$old" "$new"
printf 'Commit the changes, then create and push tag v%s.\n' "$new"
printf 'Update the separate Homebrew tap Formula URL and SHA-256 after tagging.\n'
