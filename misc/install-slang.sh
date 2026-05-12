#!/usr/bin/env bash
# Fetch the upstream Slang shader compiler for the godot-free
# slang_validate test suite. Run from the repo root or pass the
# install prefix explicitly.
#
# Usage:
#   misc/install-slang.sh [install_prefix]
#
# Defaults to $SLANG_HOME or /tmp/slang. Prints the prefix on exit
# so the caller can capture it (`SLANG_HOME=$(misc/install-slang.sh)`).
# After install, the binary is at $prefix/bin/slangc.
#
# brew has no Slang shader formula (`s-lang` is John E. Davis's
# S-Lang interpreter, unrelated). Upstream ships pre-built tarballs
# on GitHub Releases. We pin against a known version so the
# bit-exact slangc-cpp emits in tests/slang_validate stay stable.

set -euo pipefail

PREFIX="${1:-${SLANG_HOME:-/tmp/slang}}"
SLANG_VERSION="${SLANG_VERSION:-2026.8.1}"

case "$(uname -s)-$(uname -m)" in
	Darwin-arm64)
		ASSET="slang-${SLANG_VERSION}-macos-aarch64.zip"
		EXT="zip"
		;;
	Darwin-x86_64)
		ASSET="slang-${SLANG_VERSION}-macos-x86_64.zip"
		EXT="zip"
		;;
	Linux-x86_64)
		ASSET="slang-${SLANG_VERSION}-linux-x86_64.tar.gz"
		EXT="tar.gz"
		;;
	Linux-aarch64)
		ASSET="slang-${SLANG_VERSION}-linux-aarch64.tar.gz"
		EXT="tar.gz"
		;;
	*)
		echo "no prebuilt slang asset for $(uname -s)-$(uname -m)" >&2
		echo "see https://github.com/shader-slang/slang/releases" >&2
		exit 1
		;;
esac

URL="https://github.com/shader-slang/slang/releases/download/v${SLANG_VERSION}/${ASSET}"

echo "Installing Slang ${SLANG_VERSION} into ${PREFIX}" >&2
echo "Fetching ${URL}" >&2
mkdir -p "${PREFIX}"
TMP="$(mktemp -d)"
trap "rm -rf '${TMP}'" EXIT

cd "${TMP}"
curl -fL -o slang.archive "${URL}"
case "${EXT}" in
	zip)
		unzip -q slang.archive
		;;
	tar.gz)
		tar -xzf slang.archive
		;;
esac

rm -rf "${PREFIX}"/{bin,lib,include,share,LICENSE,README.md} 2>/dev/null || true
mv bin lib include share LICENSE README.md "${PREFIX}/"

# Smoke-test the install.
"${PREFIX}/bin/slangc" -v >&2

echo "${PREFIX}"
