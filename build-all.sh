#!/usr/bin/env bash
# Build all targets (encryptEd + etui) for all platforms.
# Platforms where prerequisites are missing are skipped gracefully.
#
# This is a convenience wrapper around:  build.sh --all

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$SCRIPT_DIR/build.sh" --all "$@"
