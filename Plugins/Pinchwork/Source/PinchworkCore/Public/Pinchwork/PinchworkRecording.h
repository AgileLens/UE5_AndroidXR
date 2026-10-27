// Copyright (c) 2026 Alex Coulombe. MIT License.
//
// PinchworkCore — gesture record / replay (NEW in 2.0). Pure C++.
//
// Capture a stream of hand poses to a plain-text fixture, then replay it
// through the recognizer to (a) author gestures off-headset and (b) lock in
// recognition behavior as a regression test. The intended workflow:
//
//   on device →  record real joints into FRecording (FRecorder::Capture)
//             →  serialize to a .pwrec text file (Serialize)
//   in CI     →  read the file (Deserialize) → ReplayRecognized → assert the
//                gesture timeline hasn't drifted.
//
// The format is line-based and human-diffable on purpose: a captured fixture in
// version control shows up as a readable diff, and a one-off can be hand-edited.

#pragma once

#include "PinchworkGestures.h"
#include <string>
#include <vector>

namespace Pinchwork
{
	struct FRecordedFrame
	{
		float     TimeSec = 0.0f;
		FHandPose Pose;
	};

	struct FRecording
	{
		std::vector<FRecordedFrame> Frames;
	};

	// Append-only capture helper (e.g. call Capture() each tick on device).
	struct FRecorder
	{
		FRecording Out;
		void Capture(float TimeSec, const FHandPose& Pose)
		{
			FRecordedFrame F;
			F.TimeSec = TimeSec;
			F.Pose = Pose;
			Out.Frames.push_back(F);
		}
	};

	// Text serialization. Format:
	//   PINCHWORK_REC v1
	//   F <time> <26×(x y z)>  V <26 validity bits>
	// (lines beginning with '#' are comments). File I/O is the caller's job —
	// the core stays free of <fstream>/platform concerns; pass the file's bytes.
	PINCHWORKCORE_API std::string Serialize(const FRecording& Rec);
	PINCHWORKCORE_API bool        Deserialize(const std::string& Text, FRecording& OutRec);

	// Replay: run every frame through RecognizeGesture + the stabilizer and
	// return the committed gesture timeline (one entry per frame, using each
	// frame's own timestamp delta). This is what a regression test asserts.
	PINCHWORKCORE_API std::vector<EGesture> ReplayRecognized(
		const FRecording& Rec, const FGestureConfig& Config, const FCalibration& Calib, float StabilityWindowSec = 0.15f);
}
