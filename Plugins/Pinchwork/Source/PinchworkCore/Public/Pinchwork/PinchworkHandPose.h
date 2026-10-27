// Copyright (c) 2026 Alex Coulombe. MIT License.
//
// PinchworkCore — a snapshot of one hand's 26 tracked joints, plus the
// per-finger geometry the gesture layer consumes. Pure C++.
//
// `FHandPose` is what the UE wrapper fills each tick (copying joint world
// positions out of the IHandTracker), and what the mock harness fabricates
// from synthetic finger curls. Everything downstream — pinch detection,
// gesture classification, calibration — reads only from this struct, so it is
// the single seam between "where the joints came from" and "what we infer."

#pragma once

#include "PinchworkMath.h"
#include "PinchworkKeypoints.h"

namespace Pinchwork
{
	struct FHandPose
	{
		FVec3 Joints[KKeypointCount];
		bool  bValid[KKeypointCount] = {}; // per-joint tracking validity

		void SetJoint(EKeypoint K, const FVec3& Pos, bool bIsValid = true)
		{
			const int Idx = KeypointIndex(K);
			Joints[Idx] = Pos;
			bValid[Idx] = bIsValid;
		}

		const FVec3& Joint(EKeypoint K) const { return Joints[KeypointIndex(K)]; }
		bool IsValid(EKeypoint K) const { return bValid[KeypointIndex(K)]; }
	};

	// Per-finger extension ratio in [0, 1.5]: straight-line metacarpal→tip
	// distance divided by the summed joint-to-joint chain length. ~1.0 when the
	// finger is straight, drops toward 0 as it curls. Returns 0 for an
	// untracked/degenerate finger (caller treats 0 as "no reading"). This is a
	// value-for-value port of UHandTrackingComponent::ComputeFingerExtensionRatio.
	inline float ComputeFingerExtensionRatio(const FHandPose& Pose, EKeypoint TipKey)
	{
		const int MetacarpalIdx = MetacarpalIndexForTip(TipKey);
		const int TipIdx = KeypointIndex(TipKey);
		if (MetacarpalIdx < 0 || TipIdx <= MetacarpalIdx)
		{
			return 0.0f;
		}

		float ChainLen = 0.0f;
		for (int i = MetacarpalIdx; i < TipIdx; ++i)
		{
			ChainLen += Dist(Pose.Joints[i], Pose.Joints[i + 1]);
		}
		if (ChainLen < 1.0f) // < 1 cm of chain ⇒ not a real reading
		{
			return 0.0f;
		}

		const float StraightDist = Dist(Pose.Joints[TipIdx], Pose.Joints[MetacarpalIdx]);
		return Clamp(StraightDist / ChainLen, 0.0f, 1.5f);
	}

	// Convenience: all five raw ratios in {thumb, index, middle, ring, little}
	// order, matching CaptureCurrentRatios() in the 1.0 component.
	inline void ComputeAllFingerRatios(const FHandPose& Pose, float OutRatios[5])
	{
		OutRatios[0] = ComputeFingerExtensionRatio(Pose, EKeypoint::ThumbTip);
		OutRatios[1] = ComputeFingerExtensionRatio(Pose, EKeypoint::IndexTip);
		OutRatios[2] = ComputeFingerExtensionRatio(Pose, EKeypoint::MiddleTip);
		OutRatios[3] = ComputeFingerExtensionRatio(Pose, EKeypoint::RingTip);
		OutRatios[4] = ComputeFingerExtensionRatio(Pose, EKeypoint::LittleTip);
	}

	// dot(thumbDir, worldUp) where thumbDir = ThumbTip − ThumbMetacarpal.
	// ~+1 = thumb up (true ThumbsUp), ~0 = thumb horizontal (laid across the
	// fist, i.e. ThumbOverFist), ~−1 = thumb down. Port of
	// UHandTrackingComponent::ComputeThumbUpAlignment.
	inline float ComputeThumbUpAlignment(const FHandPose& Pose)
	{
		const FVec3 ThumbDir =
			(Pose.Joint(EKeypoint::ThumbTip) - Pose.Joint(EKeypoint::ThumbMetacarpal)).GetSafeNormal();
		if (ThumbDir.IsNearlyZero())
		{
			return 0.0f;
		}
		return Dot(ThumbDir, WorldUp());
	}

	// Distance in cm between two fingertips (thumb-index pinch, etc.).
	inline float TipDistance(const FHandPose& Pose, EKeypoint A, EKeypoint B)
	{
		return Dist(Pose.Joint(A), Pose.Joint(B));
	}
}
