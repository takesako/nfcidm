#!/bin/sh
# SPDX-License-Identifier: MIT
# Install through a local Homebrew tap before publishing a GitHub release.
set -eu
test "$(uname -s)" = Darwin -a "$(uname -m)" = arm64 || {
  echo 'This installer requires Apple Silicon macOS.' >&2
  exit 1
}
command -v brew >/dev/null 2>&1 || { echo 'Homebrew is required.' >&2; exit 1; }
root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
version=$(awk -F '"' '/^#define NFCIDM_VERSION / { print $2 }' "$root/src/nfcidm.h")
tap=local/nfcidm
tap_dir=$(brew --repository)/Library/Taps/local/homebrew-nfcidm
if [ ! -d "$tap_dir" ]; then
  brew tap-new --no-git "$tap"
fi
make -C "$root" dist
mkdir -p "$tap_dir/Formula" "$tap_dir/source"
archive=$tap_dir/source/nfcidm-$version.tar.gz
cp "$root/dist/nfcidm-$version.tar.gz" "$archive"
sh "$root/scripts/formula.sh" "$archive" "file://$archive" >"$tap_dir/Formula/nfcidm.rb"
printf '\nLocal formula: %s\n' "$tap_dir/Formula/nfcidm.rb"
if brew list --versions "$tap/nfcidm" >/dev/null 2>&1; then
  brew reinstall --build-from-source "$tap/nfcidm"
else
  brew install --build-from-source "$tap/nfcidm"
fi
