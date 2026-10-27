// Copyright (c) 2026 Alex Coulombe. MIT License.
//
// Mock-joint pose synthesis for off-headset testing.
//
// The visionOS simulator has no hand tracking, so the only way to regression
// test Pinchwork's recognition is to FABRICATE the 26 joint positions a real
// hand would produce for a known pose and run them through the same core code
// the device uses. This header builds anatomically-plausible finger chains from
// two knobs per finger — a direction and a curl — so an "extended" finger comes
// out with extension ratio ≈ 1.0 and a "curled" finger well below threshold,
// exactly as the live IHandTracker would report.

#pragma once

#include "Pinchwork/PinchworkHandPose.h"

namespace PinchworkTest
{
	using namespace Pinchwork;

	// Fill a finger's contiguous joint range [metacarpal .. tip]. The finger
	// starts at `Base` heading along `Dir`; each successive segment is bent by
	// `CurlAnglePerSegRad` about an axis perpendicular to Dir, so 0 = straight
	// (extension ratio ≈ 1) and a larger angle hooks the finger into the palm
	// (ratio drops well below the extended threshold).
	inline void FillFinger(FHandPose& Pose, EKeypoint Metacarpal, EKeypoint Tip,
		const FVec3& Base, const FVec3& Dir, float SegLenCm, float CurlAnglePerSegRad)
	{
		const int M = KeypointIndex(Metacarpal);
		const int T = KeypointIndex(Tip);
		const FVec3 D = Dir.GetSafeNormal(KSmallNumber, FVec3(0, 1, 0));

		// Bend axis perpendicular to the finger direction.
		FVec3 Axis = Cross(D, WorldUp());
		if (Axis.IsNearlyZero())
		{
			Axis = Cross(D, FVec3(1, 0, 0));
		}
		const FQuat Step = FQuat::FromAxisAngle(Axis, CurlAnglePerSegRad);

		FVec3 Pos = Base;
		FVec3 StepDir = D;
		Pose.Joints[M] = Pos;
		Pose.bValid[M] = true;
		for (int k = M + 1; k <= T; ++k)
		{
			Pos = Pos + StepDir * SegLenCm;
			Pose.Joints[k] = Pos;
			Pose.bValid[k] = true;
			StepDir = Step.RotateVector(StepDir);
		}
	}

	struct FFingerInput
	{
		bool  bExtended = true;
		FVec3 Dir = FVec3(0, 1, 0); // direction when extended; +Y = away from palm
	};

	// Anatomical-ish constants. Segment length and curl are tuned so extended
	// fingers land ≈1.0 and curled fingers well under the 0.92 default threshold.
	constexpr float KSegLenCm = 2.5f;
	constexpr float KCurlPerSegRad = 1.15f;

	// Build a full 26-joint hand from per-finger {extended, direction} inputs,
	// order {thumb, index, middle, ring, little}.
	inline FHandPose MakeHand(const FFingerInput In[5])
	{
		FHandPose Pose;
		Pose.SetJoint(EKeypoint::Palm,  FVec3(0, 0, 0));
		Pose.SetJoint(EKeypoint::Wrist, FVec3(0, -3, 0));

		const EKeypoint Meta[5] = {
			EKeypoint::ThumbMetacarpal, EKeypoint::IndexMetacarpal, EKeypoint::MiddleMetacarpal,
			EKeypoint::RingMetacarpal,  EKeypoint::LittleMetacarpal };
		const EKeypoint Tip[5] = {
			EKeypoint::ThumbTip, EKeypoint::IndexTip, EKeypoint::MiddleTip,
			EKeypoint::RingTip,  EKeypoint::LittleTip };
		// Metacarpal bases fanned across X so the fingers are spatially distinct
		// (cosmetic — extension ratio is per-finger and base-independent).
		const FVec3 Base[5] = {
			FVec3(-5.0f, 0.0f, 0.0f), FVec3(-3.0f, 1.0f, 0.0f), FVec3(-1.0f, 1.2f, 0.0f),
			FVec3( 1.0f, 1.0f, 0.0f), FVec3( 3.0f, 0.5f, 0.0f) };

		for (int f = 0; f < 5; ++f)
		{
			FillFinger(Pose, Meta[f], Tip[f], Base[f], In[f].Dir, KSegLenCm,
				In[f].bExtended ? 0.0f : KCurlPerSegRad);
		}
		return Pose;
	}

	// --- Canonical gesture poses -------------------------------------------------
	// Thumb default direction +X (splayed to the side). ThumbsUp overrides it to
	// +Z so the orientation check fires.

	inline FHandPose PoseOpenPalm()
	{
		FFingerInput In[5];
		for (int f = 0; f < 5; ++f) { In[f].bExtended = true; In[f].Dir = FVec3(0, 1, 0); }
		In[0].Dir = FVec3(1, 0.3f, 0); // thumb splayed
		return MakeHand(In);
	}

	inline FHandPose PoseFist()
	{
		FFingerInput In[5];
		for (int f = 0; f < 5; ++f) { In[f].bExtended = false; In[f].Dir = FVec3(0, 1, 0); }
		In[0].Dir = FVec3(1, 0.3f, 0);
		return MakeHand(In);
	}

	inline FHandPose PoseThumbsUp()
	{
		FFingerInput In[5];
		In[0] = { true,  FVec3(0, 0, 1) };  // thumb straight UP
		In[1] = { false, FVec3(0, 1, 0) };
		In[2] = { false, FVec3(0, 1, 0) };
		In[3] = { false, FVec3(0, 1, 0) };
		In[4] = { false, FVec3(0, 1, 0) };
		return MakeHand(In);
	}

	inline FHandPose PoseThumbOverFist()
	{
		FFingerInput In[5];
		In[0] = { true,  FVec3(1, 0, 0) };  // thumb extended but HORIZONTAL
		In[1] = { false, FVec3(0, 1, 0) };
		In[2] = { false, FVec3(0, 1, 0) };
		In[3] = { false, FVec3(0, 1, 0) };
		In[4] = { false, FVec3(0, 1, 0) };
		return MakeHand(In);
	}

	inline FHandPose PosePeace()
	{
		FFingerInput In[5];
		In[0] = { false, FVec3(1, 0.3f, 0) };
		In[1] = { true,  FVec3(0, 1, 0) };
		In[2] = { true,  FVec3(0.2f, 1, 0) };
		In[3] = { false, FVec3(0, 1, 0) };
		In[4] = { false, FVec3(0, 1, 0) };
		return MakeHand(In);
	}

	inline FHandPose PoseFingerGuns()
	{
		FFingerInput In[5];
		In[0] = { true,  FVec3(0, 0, 1) };  // thumb up (cocked)
		In[1] = { true,  FVec3(0, 1, 0) };  // index out (barrel)
		In[2] = { false, FVec3(0, 1, 0) };
		In[3] = { false, FVec3(0, 1, 0) };
		In[4] = { false, FVec3(0, 1, 0) };
		return MakeHand(In);
	}

	inline FHandPose PoseFingerGunsShoot()
	{
		FFingerInput In[5];
		In[0] = { false, FVec3(1, 0.3f, 0) }; // thumb folded down
		In[1] = { true,  FVec3(0, 1, 0) };    // index out
		In[2] = { false, FVec3(0, 1, 0) };
		In[3] = { false, FVec3(0, 1, 0) };
		In[4] = { false, FVec3(0, 1, 0) };
		return MakeHand(In);
	}

	inline FHandPose PoseCallMe()
	{
		FFingerInput In[5];
		In[0] = { true,  FVec3(1, 0.2f, 0) }; // thumb
		In[1] = { false, FVec3(0, 1, 0) };
		In[2] = { false, FVec3(0, 1, 0) };
		In[3] = { false, FVec3(0, 1, 0) };
		In[4] = { true,  FVec3(0, 1, 0) };    // little
		return MakeHand(In);
	}

	inline FHandPose PoseRockOn()
	{
		FFingerInput In[5];
		In[0] = { false, FVec3(1, 0.3f, 0) }; // thumb tucked (don't-care in fingerprint)
		In[1] = { true,  FVec3(0, 1, 0) };    // index
		In[2] = { false, FVec3(0, 1, 0) };
		In[3] = { false, FVec3(0, 1, 0) };
		In[4] = { true,  FVec3(0, 1, 0) };    // little
		return MakeHand(In);
	}
}
