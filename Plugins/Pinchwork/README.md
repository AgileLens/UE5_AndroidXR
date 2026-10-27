# Pinchwork

**An OpenXR hand-tracking and gesture system for Unreal Engine.** Drop it into any UE project to get per-finger pinch detection, curl-pattern gesture recognition with per-user calibration, pinch-to-grab with throw physics, and gesture → Enhanced Input injection — built and tuned on Apple Vision Pro, and portable to any OpenXR hand-tracking runtime.

Pinchwork is a self-contained plugin: it depends only on Epic's OpenXR / Enhanced Input modules and ships none of your project's code. Add the components to a pawn and bind the events.

## Features

- **Per-finger pinches** — independent index-thumb, middle-thumb, ring-thumb, and pinky-thumb pinch events, each with its own threshold and release hysteresis (no jitter).
- **Curl-pattern gestures** — open palm, fist, peace, finger guns, rock-on, call-me, thumbs-up, and more, classified by per-finger extension signature with confidence + margin gating.
- **Per-user calibration** — walks the user through fist → open-hand and sets per-finger thresholds at the midpoint, adapting to hand size and joint flexibility instead of a hard-coded threshold.
- **Pinch-to-grab** — index-thumb pinch grabs the nearest actor with a `GrabComponent` *at the pinch point*, rides it on the hand each frame, and throws it on release scaled by hand velocity.
- **Gesture → Enhanced Input** — recognized gestures inject `IA_*` input actions each active frame, so existing Enhanced Input bindings consume hand gestures exactly like controller input.
- **Floating gesture label** — optional billboarded text above the hand showing the live gesture / finger-extension state, for on-device tuning.
- **Two-hand transforms** *(2.0)* — pinch an object with both hands and move / rotate / separate your hands to translate / rotate / scale it; the signature Vision Pro manipulation gesture, with scale clamping and an optional yaw-only "turntable" mode.
- **Gesture macros / sequences** *(2.0)* — register an ordered combo (e.g. fist → open palm → finger guns) within a time budget and fire a named event; the neutral frames between poses are ignored so combos feel natural.
- **Engine-agnostic, unit-tested core** *(2.0)* — all recognition + transform math lives in `PinchworkCore`, pure C++ with zero Unreal dependencies, regression-tested off-headset with a synthetic mock-joint harness (the only way to test gestures where the visionOS simulator has no hand tracking).

## Requirements

- Unreal Engine 5.x with the **OpenXR**, **OpenXRHandTracking**, **XRBase**, and **EnhancedInput** plugins (Pinchwork enables them as dependencies).
- An OpenXR runtime that provides hand tracking (Apple Vision Pro via the visionOS OpenXR path, Quest, etc.).

## Quickstart

1. Copy the `Pinchwork` folder into your project's `Plugins/` directory and regenerate project files.
2. Enable **Pinchwork** in *Edit → Plugins* (it auto-enables its OpenXR / Enhanced Input dependencies). Restart the editor.
3. On your VR pawn, add two **Hand Tracking (OpenXR)** components — set **Is Right Hand** on one of them.
4. (Optional) Add a **Hand Skeletal Driver** component per hand if you want a rigged hand mesh driven by the tracked joints.
5. Bind the pinch / gesture delegates (`On Pinch Started`, `On Gesture Started`, …) in your pawn Blueprint and react.

## What's new in 2.0

### Two-hand manipulation
Add a **Two-Hand Manipulator (Pinchwork)** component, set its `Target Actor`, and drive it from both hands' index-thumb pinches:

1. On the right hand's `OnPinchStarted`, cache the right pinch point; same for the left. When *both* hands are pinching, call `Begin Grab(LeftPinch, RightPinch)`.
2. Each tick while both are pinching, call `Update Grab(LeftPinch, RightPinch)` — the target scales by the hand-distance ratio, rotates with the hand axis, and translates with the hand midpoint.
3. When either pinch releases, call `End Grab`.

`Min Scale` / `Max Scale` clamp the result; enable `Yaw Only Rotation` for a steadier turntable spin. Pinch points come from `GetKeypointWorldTransform` (thumb-tip ↔ index-tip midpoint), which the hand component already computes.

### Gesture macros / sequences
Add a **Gesture Sequence (Pinchwork)** component, register combos at `BeginPlay`, and feed it the hand component's gestures:

1. `Register Sequence("unlock", [Fist, OpenPalm, FingerGuns], 1.5)` → returns an id.
2. Wire `UHandTrackingComponent::OnGestureStarted` → `Feed Gesture`.
3. Bind `OnSequenceCompleted(Name, Id)` and react.

A step must follow the previous one within the per-sequence time budget or progress resets; completing a combo re-arms it.

## Testing off-headset

The visionOS simulator has no hand tracking, so gestures can't be verified in-sim. Instead, `PinchworkCore` (the pure-C++ recognition/transform math) is regression-tested with a **mock-joint harness** that fabricates the 26 OpenXR joints for known poses and asserts the outputs:

```bash
cd Plugins/Pinchwork
./Tests/run_tests.sh      # clang++ -std=c++17 -Werror; exit code = failure count
```

No Unreal Engine, no UnrealBuildTool, no headset required. The same core sources compile both standalone (here) and inside UE. See [`Tests/`](Tests/) for the harness and [`Source/PinchworkCore/`](Source/PinchworkCore/) for the math.

### Record / replay
Capture a stream of hand poses to a plain-text `.pwrec` fixture and replay it through the recognizer — author gestures off-headset and lock recognition behavior into a regression test:

```bash
./Tests/run_tests.sh --record-sample Tests/fixtures/fist-to-open.pwrec   # write a clip
./Tests/run_tests.sh --replay        Tests/fixtures/fist-to-open.pwrec   # print its gesture timeline
```

On device, `Pinchwork::FRecorder::Capture` accumulates frames you can `Serialize` to a file; back in CI, `Deserialize` + `ReplayRecognized` asserts the gesture timeline hasn't drifted. The format is line-based and diffs cleanly in version control. A sample fixture lives in [`Tests/fixtures/`](Tests/fixtures/).

## Things to Try

1. **See your gestures live.** Leave `Show Gesture Label` on, build to your headset, and make a fist / peace sign / finger guns — the detected gesture name floats above your hand in real time.
2. **Calibrate to your hand.** Keep `Auto Calibrate On Start` enabled; on launch, make a fist when prompted, then open your hand. Pinch detection now adapts to *your* finger geometry.
3. **Grab and throw.** Add a `GrabComponent` to any actor, then index-thumb pinch near it — it snaps to your pinch point. Flick and release to throw it with your hand's velocity.
4. **Drive Enhanced Input from gestures.** Bind `IA_IndexThumbPinch` (or any gesture action) in your Input Mapping Context and watch a pinch trigger it exactly like a controller button.
5. **Tune the pinch feel.** Adjust `Pinch Threshold Cm` and `Pinch Release Hysteresis Cm` on the component and feel how it changes the grab/release responsiveness on-device.
6. **Resize a hologram with both hands.** Add a `Two-Hand Manipulator`, point it at a cube, and two-hand pinch — pull your hands apart to scale it up, twist to rotate, slide to move. *(2.0)*
7. **Author a gesture combo.** Register `fist → open palm → finger guns` on a `Gesture Sequence` component, bind `OnSequenceCompleted`, and trigger a secret action by performing the sequence. *(2.0)*
8. **Run the test harness.** `./Tests/run_tests.sh` — watch the synthetic poses classify and the two-hand math verify in under a second, no headset needed. *(2.0)*
9. **Replay a recorded gesture.** `./Tests/run_tests.sh --replay Tests/fixtures/fist-to-open.pwrec` and watch the committed gesture timeline (fist → open palm) print from a saved clip. *(2.0)*

## License

MIT © 2026 Alex Coulombe. See [LICENSE](LICENSE).
