# Pinchwork changelog

## 2.0 — 2026-06-18

### Added
- **`PinchworkCore`** — the recognition + transform math, extracted into a pure-C++
  module with **zero Unreal Engine dependencies**, so it compiles with a stock
  `clang++` and is unit-testable off-headset.
- **Two-hand transforms** (`UTwoHandManipulatorComponent` + `Pinchwork::FTwoHandManipulator`):
  scale by hand-distance ratio, rotate by the hand axis (full arc or yaw-only
  turntable), translate by the hand midpoint, pivoting about the grab-time
  midpoint. Scale clamping built in.
- **Gesture macros / sequences** (`UGestureSequenceComponent` + `Pinchwork::FGestureSequenceRecognizer`):
  register ordered gesture combos with a per-step time budget and fire a named
  event on completion. Neutral (`None`) frames between poses are ignored.
- **Mock-joint test harness** (`Tests/`): synthesizes the 26 OpenXR joints for
  known poses and asserts gestures, the thumb-orientation split, calibration
  normalization, pinch hysteresis, the stabilizer, two-hand scale/rotate/translate,
  sequence match/timeout/restart, record/replay round-trip, and edge cases.
  `./Tests/run_tests.sh` — 100 checks, 0 failures.
- **Gesture record / replay** (`Pinchwork::FRecorder` / `Serialize` /
  `Deserialize` / `ReplayRecognized`): capture a hand-pose stream to a
  human-diffable `.pwrec` fixture and replay it through the recognizer to author
  gestures off-headset and regression-test recognition. CLI:
  `./Tests/run_tests.sh --record-sample <file>` / `--replay <file>`.

### Changed
- The plugin now ships **two runtime modules**: `PinchworkCore` (pure math) and
  `Pinchwork` (the UObject components, which depend on the core).
- A compile-time `static_assert` keeps `PinchworkCore`'s keypoint and gesture
  enums in lockstep with UE's `EHandKeypoint` / the component's `EHandGesture`.

### Notes
- The 1.0 `UHandTrackingComponent` is unchanged and still owns the live
  per-frame hand sampling; 2.0's math core mirrors its recognition logic
  value-for-value. Unifying the component to call into the core (removing the
  duplicate math) is a tracked follow-up — see `REVIEW_NEEDED.md`.

## 1.0

- Per-finger pinches (index/middle/ring/pinky-thumb) with release hysteresis.
- Curl-pattern gesture recognition (open palm, fist, peace, finger guns, rock
  on, call me, thumbs up, thumb-over-fist, finger-guns-shot) with confidence +
  margin gating.
- Per-user fist→open calibration with per-finger thresholds.
- Pinch-to-grab at the pinch point + throw scaled by hand velocity.
- Gesture → Enhanced Input action injection.
- Floating gesture label for on-device tuning.
