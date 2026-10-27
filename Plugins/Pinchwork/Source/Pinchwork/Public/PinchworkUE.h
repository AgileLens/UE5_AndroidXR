// Copyright (c) 2026 Alex Coulombe. MIT License.
//
// The boundary between Unreal types and PinchworkCore's engine-agnostic types.
// Anything UE-side that talks to the core goes through these inline converters,
// so the core never sees an FVector/FQuat and stays clang-testable.
//
// This header also carries the compile-time lockstep guard between the core's
// EKeypoint and UE's EHandKeypoint — placed here (not in PinchworkCore) because
// the Pinchwork module already links HeadMountedDisplay, keeping PinchworkCore
// dependency-free of the engine HMD module.

#pragma once

#include "CoreMinimal.h"
#include "HeadMountedDisplayTypes.h"
#include "Pinchwork/PinchworkMath.h"
#include "Pinchwork/PinchworkKeypoints.h"
#include "Pinchwork/PinchworkHandPose.h"

// The finger chain-sum assumes the core enum matches UE's joint ordering
// value-for-value. If a future engine bump reorders EHandKeypoint, this fails
// to compile instead of silently mis-classifying gestures on device.
static_assert((int32)Pinchwork::KKeypointCount == (int32)EHandKeypointCount,
	"PinchworkCore EKeypoint count drifted from UE EHandKeypoint — re-sync PinchworkKeypoints.h");
static_assert((int32)Pinchwork::EKeypoint::ThumbTip == (int32)EHandKeypoint::ThumbTip,
	"PinchworkCore EKeypoint ordering drifted from UE EHandKeypoint — re-sync PinchworkKeypoints.h");
static_assert((int32)Pinchwork::EKeypoint::LittleTip == (int32)EHandKeypoint::LittleTip,
	"PinchworkCore EKeypoint ordering drifted from UE EHandKeypoint — re-sync PinchworkKeypoints.h");

namespace PinchworkUE
{
	// FVector/FQuat are double-precision in UE5; the core is float (every value
	// it derives is a scale-invariant ratio or angle). The narrowing is
	// deliberate and lossless at hand-tracking scales.
	FORCEINLINE Pinchwork::FVec3 ToCore(const FVector& V)
	{
		return Pinchwork::FVec3((float)V.X, (float)V.Y, (float)V.Z);
	}

	FORCEINLINE FVector ToUE(const Pinchwork::FVec3& V)
	{
		return FVector(V.X, V.Y, V.Z);
	}

	FORCEINLINE Pinchwork::FQuat ToCore(const FQuat& Q)
	{
		return Pinchwork::FQuat((float)Q.X, (float)Q.Y, (float)Q.Z, (float)Q.W);
	}

	FORCEINLINE FQuat ToUE(const Pinchwork::FQuat& Q)
	{
		return FQuat(Q.X, Q.Y, Q.Z, Q.W);
	}

	// Copy a component's per-frame cached keypoint transforms into a core pose.
	// Validity is inferred from a non-identity location (the 1.0 component stores
	// FTransform::Identity for an untracked joint) — adequate for the recognition
	// math, which already treats a degenerate finger chain as "no reading".
	FORCEINLINE void FillHandPose(Pinchwork::FHandPose& OutPose, const TArray<FTransform>& CachedKeypoints)
	{
		const int32 N = FMath::Min((int32)Pinchwork::KKeypointCount, CachedKeypoints.Num());
		for (int32 i = 0; i < N; ++i)
		{
			const FVector Loc = CachedKeypoints[i].GetLocation();
			OutPose.Joints[i] = ToCore(Loc);
			OutPose.bValid[i] = !Loc.IsNearlyZero();
		}
	}
}
