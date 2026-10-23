#!/usr/bin/env bash

set -euo pipefail

usage() {
  cat >&2 <<'EOF'
Usage: build/package-rdp-sdk.sh STAGE_ROOT OUTPUT_DIR VERSION [TARGET]

Package a staged openh264-rdp installation as a relocatable SDK archive.

STAGE_ROOT must contain include/ and lib/ from "make install". VERSION must
use the native release form vMAJOR.MINOR.PATCH-rdp.REVISION (the leading v is
optional). TARGET defaults to the current Linux architecture.
EOF
}

die() {
  printf 'package-rdp-sdk: %s\n' "$*" >&2
  exit 1
}

require_file() {
  [[ -f "$1" ]] || die "required file is missing: $1"
}

detect_target() {
  local arch

  case "$(uname -s)" in
    Linux) ;;
    *) die "only Linux SDK archives are currently supported" ;;
  esac

  case "$(uname -m)" in
    x86_64|amd64) arch=amd64 ;;
    aarch64|arm64) arch=arm64 ;;
    *) die "unsupported host architecture; pass TARGET explicitly" ;;
  esac

  printf 'linux-%s\n' "$arch"
}

if (( $# < 3 || $# > 4 )); then
  usage
  exit 2
fi

stage_root=$1
output_dir=$2
version=${3#v}
target=${4:-}

[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+-rdp\.[0-9]+$ ]] ||
  die "invalid VERSION: $3"

[[ -d "$stage_root/include" && -d "$stage_root/lib" ]] ||
  die "STAGE_ROOT must contain include/ and lib/: $stage_root"

if [[ -z "$target" ]]; then
  target=$(detect_target)
fi
[[ "$target" =~ ^linux-(amd64|arm64)$ ]] || die "unsupported TARGET: $target"

script_dir=$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
repo_root=$(CDPATH='' cd -- "$script_dir/.." && pwd -P)
stage_root=$(CDPATH='' cd -- "$stage_root" && pwd -P)
mkdir -p -- "$output_dir"
output_dir=$(CDPATH='' cd -- "$output_dir" && pwd -P)

require_file "$repo_root/LICENSE"
require_file "$stage_root/include/wels/codec_api.h"
require_file "$stage_root/lib/libopenh264-rdp.a"
require_file "$stage_root/lib/pkgconfig/openh264-rdp.pc"
require_file "$stage_root/lib/pkgconfig/openh264-rdp-static.pc"

shopt -s nullglob
shared_libraries=("$stage_root"/lib/libopenh264-rdp.so*)
(( ${#shared_libraries[@]} > 0 )) ||
  die "no shared libopenh264-rdp.so library was found in $stage_root/lib"

archive_root="openh264-rdp-${version}-${target}"
archive_path="$output_dir/${archive_root}.tar.gz"
[[ ! -e "$archive_path" ]] || die "archive already exists: $archive_path"

work_dir=$(mktemp -d "${TMPDIR:-/tmp}/openh264-rdp-sdk.XXXXXX")
trap 'rm -rf -- "$work_dir"' EXIT
sdk_root="$work_dir/$archive_root"
mkdir -p -- "$sdk_root/include/wels" "$sdk_root/lib/pkgconfig"

cp -a -- "$stage_root/include/wels/." "$sdk_root/include/wels/"
cp -a -- "$stage_root/lib/libopenh264-rdp.a" "$sdk_root/lib/"
cp -a -- "${shared_libraries[@]}" "$sdk_root/lib/"
cp -a -- "$stage_root/lib/pkgconfig/openh264-rdp.pc" \
  "$stage_root/lib/pkgconfig/openh264-rdp-static.pc" \
  "$sdk_root/lib/pkgconfig/"
cp -a -- "$repo_root/LICENSE" "$sdk_root/LICENSE"

# Make pkg-config metadata resolve include/ and lib/ relative to its own
# location so the extracted SDK does not depend on the staging prefix.
for pc_file in "$sdk_root"/lib/pkgconfig/*.pc; do
  sed -i \
    -e "s|^prefix=.*$|prefix=\${pcfiledir}/../..|" \
    -e "s|^libdir=.*$|libdir=\${prefix}/lib|" \
    -e "s|^includedir=.*$|includedir=\${prefix}/include|" \
    "$pc_file"
  grep -Fqx "prefix=\${pcfiledir}/../.." "$pc_file"
  grep -Fqx "libdir=\${prefix}/lib" "$pc_file"
  grep -Fqx "includedir=\${prefix}/include" "$pc_file"
done

pkg_config_path="$sdk_root/lib/pkgconfig"
PKG_CONFIG_PATH="$pkg_config_path" pkg-config --validate openh264-rdp
PKG_CONFIG_PATH="$pkg_config_path" pkg-config --validate openh264-rdp-static
shared_version=$(PKG_CONFIG_PATH="$pkg_config_path" \
  pkg-config --modversion openh264-rdp)
static_version=$(PKG_CONFIG_PATH="$pkg_config_path" \
  pkg-config --modversion openh264-rdp-static)
[[ "$shared_version" == "$version" && "$static_version" == "$version" ]] ||
  die "pkg-config version mismatch: shared=$shared_version static=$static_version expected=$version"
static_flags=$(PKG_CONFIG_PATH="$pkg_config_path" \
  pkg-config --libs --static openh264-rdp-static)
[[ "$static_flags" == *libopenh264-rdp.a* ]] ||
  die "openh264-rdp-static.pc must link libopenh264-rdp.a explicitly"

revision=unknown
if git -C "$repo_root" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  revision=$(git -C "$repo_root" rev-parse HEAD)
fi

source_date_epoch=${SOURCE_DATE_EPOCH:-}
if [[ -z "$source_date_epoch" && "$revision" != unknown ]]; then
  source_date_epoch=$(git -C "$repo_root" show -s --format=%ct "$revision")
fi
source_date_epoch=${source_date_epoch:-0}
[[ "$source_date_epoch" =~ ^[0-9]+$ ]] ||
  die "SOURCE_DATE_EPOCH must be a non-negative integer"

cc_version=$(cc --version | sed -n '1p')
cxx_version=$(c++ --version | sed -n '1p')
cat >"$sdk_root/BUILDINFO" <<EOF
name=openh264-rdp
version=$version
revision=$revision
target=$target
source_date_epoch=$source_date_epoch
cc=$cc_version
cxx=$cxx_version
EOF

tar --sort=name \
  --mtime="@$source_date_epoch" \
  --owner=0 \
  --group=0 \
  --numeric-owner \
  -C "$work_dir" \
  -cf - "$archive_root" |
  gzip -n -9 >"$archive_path"

printf '%s\n' "$archive_path"
