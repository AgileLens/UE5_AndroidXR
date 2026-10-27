#!/usr/bin/env bash
# Pinchwork 2.0 — off-headset core test harness.
#
# Compiles PinchworkCore + the mock-joint tests with a stock clang++ (NO Unreal
# Engine, NO UnrealBuildTool, NO headset) and runs them. Exit code = failure
# count, so it drops straight into CI / a pre-commit hook.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
INC="$ROOT/Source/PinchworkCore/Public"
SRC="$ROOT/Source/PinchworkCore/Private"

OUTDIR="$(mktemp -d)"
OUT="$OUTDIR/pinchwork_tests"

echo "Compiling Pinchwork core tests (clang++ -std=c++17)..."
clang++ -std=c++17 -O2 -Wall -Wextra -Werror \
	-I "$INC" \
	"$HERE/PinchworkCoreTests.cpp" \
	"$SRC/PinchworkGestures.cpp" \
	"$SRC/PinchworkTwoHand.cpp" \
	"$SRC/PinchworkSequence.cpp" \
	"$SRC/PinchworkRecording.cpp" \
	-o "$OUT"

echo "Running..."
"$OUT"
RC=$?

# Optional pass-through: ./run_tests.sh --replay <file>  (or --record-sample <file>)
if [ "${1:-}" = "--replay" ] || [ "${1:-}" = "--record-sample" ]; then
	"$OUT" "$@"
	RC=$?
fi

rm -rf "$OUTDIR"
exit $RC
