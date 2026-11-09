#!/usr/bin/env bash
# Prints the oldest glibc and qt6-base a built package can run on, from the newest
# GLIBC_ and Qt_ symbol versions its ELF files import:
#   glibc=2.41
#   qt=6.9
#   elf=12
# Usage: package-abi.sh <package.pkg.tar.zst | unpacked directory>
set -euo pipefail

if test "$#" -ne 1; then
  echo "usage: $0 <package or directory>" >&2
  exit 2
fi

root="$1"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
if test -f "$root"; then
  mkdir "$work/root"
  bsdtar -xf "$root" -C "$work/root" usr
  root="$work/root"
fi

symbols="$work/symbols"
: > "$symbols"
elf=0
while IFS= read -r -d '' file; do
  test "$(head -c 4 "$file" | od -An -tx1 | tr -d ' \n')" = 7f454c46 || continue
  elf=$((elf + 1))
  if ! objdump -T "$file" >> "$symbols"; then
    echo "objdump could not read $file" >&2
    exit 1
  fi
done < <(find "$root" -type f -print0)

if test "$elf" -eq 0; then
  echo "No ELF files in $1" >&2
  exit 1
fi

newest() { sort -V | tail -n 1; }
glibc="$(grep -oE 'GLIBC_2\.[0-9]+(\.[0-9]+)?' "$symbols" | sed 's/^GLIBC_//' | newest || true)"
qt="$(grep -oE 'Qt_6\.[0-9]+' "$symbols" | sed 's/^Qt_//' | newest || true)"
if test -z "$glibc" || test -z "$qt"; then
  echo "Could not find GLIBC_ and Qt_6 symbol versions in $1" >&2
  exit 1
fi

echo "glibc=$glibc"
echo "qt=$qt"
echo "elf=$elf"
