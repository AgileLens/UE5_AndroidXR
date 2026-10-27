// Copyright (c) 2026 Alex Coulombe. MIT License.
//
// Pinchwork 2.0 mock-joint regression harness.
//
// Compiles with a stock `clang++ -std=c++17` — NO Unreal Engine, NO UBT, NO
// headset. Feeds synthetic 26-joint hand poses (MockHand.h) and direct vector
// inputs through PinchworkCore and asserts the recognized gestures, pinch
// states, calibration math, two-hand transforms, and gesture sequences. This
// is Pinchwork's "test often" path while the visionOS simulator has no hand
// tracking. Run via ./run_tests.sh; exit code is the failure count.

#include "MockHand.h"
#include "Pinchwork/PinchworkGestures.h"
#include "Pinchwork/PinchworkTwoHand.h"
#include "Pinchwork/PinchworkSequence.h"
#include "Pinchwork/PinchworkRecording.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

using namespace Pinchwork;
using namespace PinchworkTest;

// --- Tiny assertion framework (no gtest dependency) --------------------------
static int g_Checks = 0;
static int g_Fails = 0;

#define CHECK(cond)                                                            \
	do {                                                                       \
		++g_Checks;                                                            \
		if (!(cond)) { ++g_Fails; std::printf("  FAIL %s:%d  CHECK(%s)\n", __FILE__, __LINE__, #cond); } \
	} while (0)

#define CHECK_NEAR(a, b, tol)                                                  \
	do {                                                                       \
		++g_Checks;                                                            \
		const double _va = (a), _vb = (b), _t = (tol);                         \
		if (std::fabs(_va - _vb) > _t) { ++g_Fails;                            \
			std::printf("  FAIL %s:%d  %s (%.4f) !~ %s (%.4f) tol %.4f\n",     \
				__FILE__, __LINE__, #a, _va, #b, _vb, _t); }                   \
	} while (0)

#define CHECK_GESTURE(got, want)                                              \
	do {                                                                       \
		++g_Checks;                                                            \
		const EGesture _g = (got), _w = (want);                                \
		if (_g != _w) { ++g_Fails;                                             \
			std::printf("  FAIL %s:%d  got %s, want %s\n",                     \
				__FILE__, __LINE__, GestureName(_g), GestureName(_w)); }       \
	} while (0)

static void Section(const char* Name) { std::printf("[ %s ]\n", Name); }

// Convenience: recognize with default config + no calibration.
static EGesture Recognize(const FHandPose& Pose)
{
	FGestureConfig Cfg;
	FCalibration Cal; // uncalibrated
	return RecognizeGesture(Pose, Cfg, Cal).Gesture;
}

// =============================================================================
static void Test_FingerRatios()
{
	Section("finger extension ratios");
	const FHandPose Open = PoseOpenPalm();
	const FHandPose Fist = PoseFist();

	// Extended fingers ≈ 1.0; curled fingers well below the 0.92 threshold.
	for (EKeypoint Tip : { EKeypoint::IndexTip, EKeypoint::MiddleTip, EKeypoint::RingTip, EKeypoint::LittleTip })
	{
		CHECK(ComputeFingerExtensionRatio(Open, Tip) > 0.97f);
		CHECK(ComputeFingerExtensionRatio(Fist, Tip) < 0.6f);
	}
	// A finger with < 1 cm of chain is treated as no-reading (0).
	FHandPose Degenerate;
	CHECK_NEAR(ComputeFingerExtensionRatio(Degenerate, EKeypoint::IndexTip), 0.0f, 1e-6f);
}

static void Test_GestureClassification()
{
	Section("single-pose gesture classification");
	CHECK_GESTURE(Recognize(PoseOpenPalm()),        EGesture::OpenPalm);
	CHECK_GESTURE(Recognize(PoseFist()),            EGesture::Fist);
	CHECK_GESTURE(Recognize(PosePeace()),           EGesture::Peace);
	CHECK_GESTURE(Recognize(PoseFingerGuns()),      EGesture::FingerGuns);
	CHECK_GESTURE(Recognize(PoseFingerGunsShoot()), EGesture::FingerGunsShoot);
	CHECK_GESTURE(Recognize(PoseCallMe()),          EGesture::CallMe);
	CHECK_GESTURE(Recognize(PoseRockOn()),          EGesture::RockOn);
}

static void Test_ThumbOrientationSplit()
{
	Section("ThumbsUp vs ThumbOverFist orientation split");
	// Identical curl pattern {1,0,0,0,0}; only the thumb DIRECTION differs.
	CHECK_GESTURE(Recognize(PoseThumbsUp()),      EGesture::ThumbsUp);
	CHECK_GESTURE(Recognize(PoseThumbOverFist()), EGesture::ThumbOverFist);

	// The alignment metric itself: thumb up ≈ +1, thumb sideways ≈ 0.
	CHECK(ComputeThumbUpAlignment(PoseThumbsUp()) > 0.9f);
	CHECK(ComputeThumbUpAlignment(PoseThumbOverFist()) < 0.5f);
}

static void Test_Calibration()
{
	Section("per-user calibration");
	// Capture reference ratios from synthetic fist/open poses.
	FCalibration Cal;
	ComputeAllFingerRatios(PoseFist(), Cal.FistRatios);
	ComputeAllFingerRatios(PoseOpenPalm(), Cal.OpenRatios);
	CHECK(!Cal.bCalibrated);
	Cal.ComputeThresholds(/*GlobalFallback=*/0.92f);
	CHECK(Cal.bCalibrated);

	// Each finger's threshold should sit between its fist and open ratios.
	for (int i = 1; i < 5; ++i) // index..little (thumb chain is shorter/noisier)
	{
		CHECK(Cal.Thresholds[i] > Cal.FistRatios[i]);
		CHECK(Cal.Thresholds[i] < Cal.OpenRatios[i]);
	}

	// Normalization maps the fist ratio → ~0 and the open ratio → ~1.
	FGestureConfig Cfg;
	CHECK_NEAR(NormalizeFingerRatio(1, Cal.FistRatios[1], Cfg, Cal), 0.0f, 0.02f);
	CHECK_NEAR(NormalizeFingerRatio(1, Cal.OpenRatios[1], Cfg, Cal), 1.0f, 0.02f);

	// Calibrated recognition still classifies the canonical poses correctly.
	CHECK_GESTURE(RecognizeGesture(PoseOpenPalm(), Cfg, Cal).Gesture, EGesture::OpenPalm);
	CHECK_GESTURE(RecognizeGesture(PoseFist(),     Cfg, Cal).Gesture, EGesture::Fist);
}

static void Test_PinchDetector()
{
	Section("pinch detector hysteresis");
	FPinchDetector P; // threshold 3.0, release hysteresis 1.0
	CHECK(!P.bPinching);
	CHECK(P.Update(2.5f));          // 2.5 <= 3.0 -> enter (state changed)
	CHECK(P.bPinching);
	CHECK(!P.Update(3.5f));         // within hysteresis band -> no release
	CHECK(P.bPinching);
	CHECK(P.Update(4.2f));          // >= 3.0 + 1.0 -> release (state changed)
	CHECK(!P.bPinching);
	CHECK(!P.Update(3.5f));         // 3.5 > 3.0 -> no (re)enter
	CHECK(!P.bPinching);

	// Driven from a synthetic pose: thumb/index tips far apart -> not pinching.
	const FHandPose Open = PoseOpenPalm();
	const float D = TipDistance(Open, EKeypoint::ThumbTip, EKeypoint::IndexTip);
	CHECK(D > 3.0f);
}

static void Test_Stabilizer()
{
	Section("gesture temporal stabilizer");
	FGestureStabilizer S;
	S.StabilityWindowSec = 0.15f;
	// A momentary detection shorter than the window must not commit.
	CHECK_GESTURE(S.Update(EGesture::Fist, 0.05f), EGesture::None);
	CHECK_GESTURE(S.Update(EGesture::Fist, 0.05f), EGesture::None); // 0.10 total
	CHECK_GESTURE(S.Update(EGesture::Fist, 0.10f), EGesture::Fist); // 0.20 -> commit
	// Switching requires the new pose to hold the window again.
	CHECK_GESTURE(S.Update(EGesture::OpenPalm, 0.05f), EGesture::Fist);
	CHECK_GESTURE(S.Update(EGesture::OpenPalm, 0.20f), EGesture::OpenPalm);
}

static void Test_TwoHandScale()
{
	Section("two-hand: scale");
	FTwoHandManipulator M;
	M.Begin(FVec3(-5, 0, 0), FVec3(5, 0, 0));   // span 10
	FManipulationDelta D = M.Update(FVec3(-10, 0, 0), FVec3(10, 0, 0)); // span 20
	CHECK_NEAR(D.Scale, 2.0f, 1e-4f);
	CHECK_NEAR(D.Translation.Size(), 0.0f, 1e-4f);
	CHECK_NEAR(D.Rotation.GetAngleRad(), 0.0f, 1e-3f);

	// Hands together -> shrink.
	D = M.Update(FVec3(-2.5f, 0, 0), FVec3(2.5f, 0, 0)); // span 5
	CHECK_NEAR(D.Scale, 0.5f, 1e-4f);
}

static void Test_TwoHandTranslate()
{
	Section("two-hand: translate");
	FTwoHandManipulator M;
	M.Begin(FVec3(-5, 0, 0), FVec3(5, 0, 0));
	// Both hands shift +10 in Y: pure translation, no scale/rotation.
	FManipulationDelta D = M.Update(FVec3(-5, 10, 0), FVec3(5, 10, 0));
	CHECK_NEAR(D.Translation.X, 0.0f, 1e-4f);
	CHECK_NEAR(D.Translation.Y, 10.0f, 1e-4f);
	CHECK_NEAR(D.Translation.Z, 0.0f, 1e-4f);
	CHECK_NEAR(D.Scale, 1.0f, 1e-4f);
}

static void Test_TwoHandRotate()
{
	Section("two-hand: rotate");
	FTwoHandManipulator M;
	M.Begin(FVec3(-10, 0, 0), FVec3(10, 0, 0)); // span dir +X
	// Rotate the hand axis 90° CCW about +Z -> span dir +Y.
	FManipulationDelta D = M.Update(FVec3(0, -10, 0), FVec3(0, 10, 0));
	CHECK_NEAR(D.YawDeltaDeg, 90.0f, 0.5f);
	CHECK_NEAR(RadToDeg(D.Rotation.GetAngleRad()), 90.0f, 0.5f);
	CHECK_NEAR(D.Scale, 1.0f, 1e-4f);

	// −90°: span dir +X -> −Y.
	D = M.Update(FVec3(0, 10, 0), FVec3(0, -10, 0));
	CHECK_NEAR(D.YawDeltaDeg, -90.0f, 0.5f);
}

static void Test_TwoHandApplyDelta()
{
	Section("two-hand: apply delta to an object");
	FTwoHandManipulator M;
	M.Begin(FVec3(-5, 0, 0), FVec3(5, 0, 0)); // midpoint (0,0,0)

	FObjectTransform Obj;
	Obj.Position = FVec3(10, 0, 0); // offset +10X from pivot
	Obj.Scale = 1.0f;

	// Pure 2× scale about the pivot: offset doubles -> position (20,0,0).
	FManipulationDelta Scale2 = M.Update(FVec3(-10, 0, 0), FVec3(10, 0, 0));
	FObjectTransform R = M.ApplyDelta(Obj, Scale2);
	CHECK_NEAR(R.Position.X, 20.0f, 1e-3f);
	CHECK_NEAR(R.Scale, 2.0f, 1e-4f);

	// Pure 90° rotation about +Z: offset (10,0,0) -> (0,10,0).
	FManipulationDelta Rot90 = M.Update(FVec3(0, -5, 0), FVec3(0, 5, 0));
	R = M.ApplyDelta(Obj, Rot90);
	CHECK_NEAR(R.Position.X, 0.0f, 1e-3f);
	CHECK_NEAR(R.Position.Y, 10.0f, 1e-3f);
	CHECK_NEAR(R.Scale, 1.0f, 1e-4f);
}

static void Test_GestureSequences()
{
	Section("gesture sequences / macros");
	FGestureSequenceRecognizer R;
	const int ComboId = R.AddSequence(FGestureSequence(
		"unlock", { EGesture::Fist, EGesture::OpenPalm, EGesture::FingerGuns }, /*maxStep=*/1.5f));
	CHECK(ComboId == 0);
	CHECK(R.AddSequence(FGestureSequence("empty", {})) == -1); // empty rejected

	// Correct order within the time budget -> completes on the final step.
	float t = 0.0f;
	CHECK(R.OnGesture(EGesture::Fist,       t += 0.1f).empty());
	CHECK(R.OnGesture(EGesture::None,       t += 0.1f).empty()); // None ignored, no break
	CHECK(R.OnGesture(EGesture::OpenPalm,   t += 0.1f).empty());
	{
		std::vector<int> done = R.OnGesture(EGesture::FingerGuns, t += 0.1f);
		CHECK(done.size() == 1 && done[0] == ComboId);
	}
	// Re-armed: progress reset to 0 after completion.
	CHECK(R.Progress(ComboId) == 0);

	// Wrong order does not complete.
	R.ResetProgress();
	CHECK(R.OnGesture(EGesture::Fist,     1.0f).empty());
	CHECK(R.OnGesture(EGesture::FingerGuns, 1.1f).empty()); // skipped OpenPalm
	CHECK(R.Progress(ComboId) == 0); // broke (FingerGuns != step0 Fist)

	// Timeout between steps resets progress.
	R.ResetProgress();
	CHECK(R.OnGesture(EGesture::Fist,     10.0f).empty());
	CHECK(R.OnGesture(EGesture::OpenPalm, 12.0f).empty()); // 2s > 1.5s budget -> timed out
	CHECK(R.Progress(ComboId) == 0);

	// A wrong gesture that equals step0 restarts rather than fully breaking.
	R.ResetProgress();
	R.OnGesture(EGesture::Fist, 20.0f);     // progress 1
	R.OnGesture(EGesture::Fist, 20.1f);     // restart -> progress 1 (not 2, not 0)
	CHECK(R.Progress(ComboId) == 1);
}

static void Test_EdgeCases()
{
	Section("edge cases (bulletproofing)");

	// Quaternion FromTo: 180° flip (antiparallel) must produce a real π
	// rotation, not identity or NaN. +X -> -X should carry +Y to -Y.
	{
		const FQuat Q = FQuat::FromTo(FVec3(1, 0, 0), FVec3(-1, 0, 0));
		CHECK_NEAR(RadToDeg(Q.GetAngleRad()), 180.0f, 0.5f);
		const FVec3 R = Q.RotateVector(FVec3(0, 1, 0));
		CHECK_NEAR(R.X, 0.0f, 1e-3f);
		CHECK_NEAR(R.Y, -1.0f, 1e-3f);
	}
	// FromTo identical directions -> identity (no rotation, no NaN).
	{
		const FQuat Q = FQuat::FromTo(FVec3(0, 1, 0), FVec3(0, 1, 0));
		CHECK_NEAR(Q.GetAngleRad(), 0.0f, 1e-3f);
	}

	// Two-hand: hands coincident at grab (zero span) must not divide by zero —
	// scale stays 1, rotation identity.
	{
		FTwoHandManipulator M;
		M.Begin(FVec3(0, 0, 0), FVec3(0, 0, 0));
		const FManipulationDelta D = M.Update(FVec3(-5, 0, 0), FVec3(5, 0, 0));
		CHECK_NEAR(D.Scale, 1.0f, 1e-4f);
		CHECK_NEAR(D.Rotation.GetAngleRad(), 0.0f, 1e-3f);
	}

	// YawDelta with a near-vertical span (no horizontal component) -> 0, not NaN.
	CHECK_NEAR(YawDeltaDegrees(FVec3(0, 0, 1), FVec3(0, 0, 1)), 0.0f, 1e-3f);

	// Inactive manipulator returns an identity delta.
	{
		FTwoHandManipulator M;
		const FManipulationDelta D = M.Update(FVec3(-5, 0, 0), FVec3(5, 0, 0));
		CHECK_NEAR(D.Scale, 1.0f, 1e-4f);
		CHECK(!M.IsActive());
	}

	// Two sequences sharing a prefix both advance; only the matched one fires.
	{
		FGestureSequenceRecognizer R;
		const int A = R.AddSequence(FGestureSequence("a", { EGesture::Fist, EGesture::OpenPalm, EGesture::Peace }));
		const int B = R.AddSequence(FGestureSequence("b", { EGesture::Fist, EGesture::OpenPalm, EGesture::RockOn }));
		R.OnGesture(EGesture::Fist, 0.0f);
		R.OnGesture(EGesture::OpenPalm, 0.1f);
		CHECK(R.Progress(A) == 2 && R.Progress(B) == 2); // both at the shared prefix
		const std::vector<int> Done = R.OnGesture(EGesture::Peace, 0.2f);
		CHECK(Done.size() == 1 && Done[0] == A);          // only A completes
		CHECK(R.Progress(B) == 0);                        // B broke on Peace
	}
}

// A synthetic ~0.8 s clip: 8 fist frames then 8 open-palm frames at 20 fps.
// Long enough that the stabilizer commits Fist, then OpenPalm.
static FRecording MakeFistToOpenRecording()
{
	FRecorder Rec;
	float t = 0.0f;
	for (int i = 0; i < 8; ++i) { Rec.Capture(t, PoseFist());     t += 0.05f; }
	for (int i = 0; i < 8; ++i) { Rec.Capture(t, PoseOpenPalm()); t += 0.05f; }
	return Rec.Out;
}

static void Test_RecordReplay()
{
	Section("record / replay round-trip");
	const FRecording Rec = MakeFistToOpenRecording();

	// Serialize → deserialize → the frames survive intact.
	const std::string Text = Serialize(Rec);
	FRecording Back;
	CHECK(Deserialize(Text, Back));
	CHECK(Back.Frames.size() == Rec.Frames.size());
	CHECK_NEAR(Back.Frames[3].TimeSec, Rec.Frames[3].TimeSec, 1e-4f);
	CHECK_NEAR(Back.Frames[3].Pose.Joint(EKeypoint::IndexTip).Y,
	           Rec.Frames[3].Pose.Joint(EKeypoint::IndexTip).Y, 1e-3f);

	// A missing/garbage header is rejected.
	FRecording Bad;
	CHECK(!Deserialize("garbage\nF 0 0 0 0", Bad));

	// Replaying the deserialized clip reproduces the Fist→OpenPalm timeline.
	FGestureConfig Cfg;
	FCalibration Cal;
	const std::vector<EGesture> Timeline = ReplayRecognized(Back, Cfg, Cal);
	CHECK(Timeline.size() == Back.Frames.size());
	CHECK_GESTURE(Timeline.back(), EGesture::OpenPalm);
	bool bSawFist = false;
	for (EGesture G : Timeline) { if (G == EGesture::Fist) { bSawFist = true; } }
	CHECK(bSawFist);
}

// CLI: author a sample fixture, or replay one and print its gesture timeline.
static int RunRecordSample(const char* Path)
{
	const FRecording Rec = MakeFistToOpenRecording();
	std::ofstream F(Path);
	if (!F) { std::printf("cannot write %s\n", Path); return 1; }
	F << Serialize(Rec);
	std::printf("wrote %zu frames -> %s\n", Rec.Frames.size(), Path);
	return 0;
}

static int RunReplay(const char* Path)
{
	std::ifstream F(Path);
	if (!F) { std::printf("cannot read %s\n", Path); return 1; }
	std::stringstream SS;
	SS << F.rdbuf();
	FRecording Rec;
	if (!Deserialize(SS.str(), Rec)) { std::printf("parse failed: %s\n", Path); return 1; }

	FGestureConfig Cfg;
	FCalibration Cal;
	const std::vector<EGesture> Timeline = ReplayRecognized(Rec, Cfg, Cal);
	std::printf("replayed %zu frames from %s — committed gesture timeline:\n", Rec.Frames.size(), Path);
	EGesture Last = EGesture::None;
	bool bFirst = true;
	for (size_t i = 0; i < Timeline.size(); ++i)
	{
		if (bFirst || Timeline[i] != Last)
		{
			std::printf("  t=%.2f  %s\n", Rec.Frames[i].TimeSec, GestureName(Timeline[i]));
			Last = Timeline[i];
			bFirst = false;
		}
	}
	return 0;
}

int main(int argc, char** argv)
{
	if (argc >= 3 && std::strcmp(argv[1], "--record-sample") == 0) { return RunRecordSample(argv[2]); }
	if (argc >= 3 && std::strcmp(argv[1], "--replay") == 0)        { return RunReplay(argv[2]); }

	std::printf("=== Pinchwork 2.0 core tests ===\n");
	Test_FingerRatios();
	Test_GestureClassification();
	Test_ThumbOrientationSplit();
	Test_Calibration();
	Test_PinchDetector();
	Test_Stabilizer();
	Test_TwoHandScale();
	Test_TwoHandTranslate();
	Test_TwoHandRotate();
	Test_TwoHandApplyDelta();
	Test_GestureSequences();
	Test_RecordReplay();
	Test_EdgeCases();

	std::printf("=== %d checks, %d failures ===\n", g_Checks, g_Fails);
	return g_Fails;
}
