// Copyright (c) 2026 Alex Coulombe. MIT License.

#include "Pinchwork/PinchworkTwoHand.h"

namespace Pinchwork
{
	void FTwoHandManipulator::Begin(const FVec3& LeftPinch, const FVec3& RightPinch)
	{
		StartLeft = LeftPinch;
		StartRight = RightPinch;
		StartMid = Midpoint(LeftPinch, RightPinch);
		const FVec3 Span = RightPinch - LeftPinch;
		StartDist = Span.Size();
		StartDir = Span.GetSafeNormal();
		bActive = true;
	}

	FManipulationDelta FTwoHandManipulator::Update(const FVec3& LeftPinch, const FVec3& RightPinch) const
	{
		FManipulationDelta Delta;
		if (!bActive)
		{
			return Delta;
		}

		const FVec3 Span = RightPinch - LeftPinch;
		const float CurrentDist = Span.Size();
		const FVec3 CurrentDir = Span.GetSafeNormal();
		const FVec3 CurrentMid = Midpoint(LeftPinch, RightPinch);

		// Scale: ratio of current to grab-time hand separation. Guard a
		// degenerate (hands coincident at grab) span so we never divide by ~0.
		Delta.Scale = (StartDist > KSmallNumber) ? (CurrentDist / StartDist) : 1.0f;

		// Rotation: shortest arc that carries the grab-time hand axis onto the
		// current one. Identity when either span is degenerate.
		Delta.Rotation = FQuat::FromTo(StartDir, CurrentDir);

		// Translation: how far the point between the hands has moved.
		Delta.Translation = CurrentMid - StartMid;

		// Turntable yaw (rotation about world up only), for callers that want a
		// constrained one-axis spin instead of the full arc.
		Delta.YawDeltaDeg = YawDeltaDegrees(StartDir, CurrentDir);

		return Delta;
	}

	FObjectTransform FTwoHandManipulator::ApplyDelta(const FObjectTransform& GrabTimeTransform, const FManipulationDelta& Delta) const
	{
		FObjectTransform Result;

		// Offset of the object from the pivot (hand midpoint at grab), scaled
		// and rotated, then re-anchored and translated by the midpoint drift.
		const FVec3 Offset = GrabTimeTransform.Position - StartMid;
		const FVec3 NewOffset = Delta.Rotation.RotateVector(Offset * Delta.Scale);
		Result.Position = StartMid + NewOffset + Delta.Translation;

		Result.Rotation = (Delta.Rotation * GrabTimeTransform.Rotation).Normalized();
		Result.Scale = GrabTimeTransform.Scale * Delta.Scale;
		return Result;
	}

	float YawDeltaDegrees(const FVec3& DirA, const FVec3& DirB)
	{
		// Project both directions onto the horizontal (XY) plane and take the
		// signed angle about +Z via atan2(cross.z, dot).
		const FVec3 A = FVec3(DirA.X, DirA.Y, 0.0f).GetSafeNormal();
		const FVec3 B = FVec3(DirB.X, DirB.Y, 0.0f).GetSafeNormal();
		if (A.IsNearlyZero() || B.IsNearlyZero())
		{
			return 0.0f; // span is near-vertical: no meaningful yaw
		}
		const float CrossZ = A.X * B.Y - A.Y * B.X;
		const float D = Clamp(Dot(A, B), -1.0f, 1.0f);
		return RadToDeg(std::atan2(CrossZ, D));
	}
}
