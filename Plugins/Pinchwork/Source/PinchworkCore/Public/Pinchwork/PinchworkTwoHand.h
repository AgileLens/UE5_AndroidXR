// Copyright (c) 2026 Alex Coulombe. MIT License.
//
// PinchworkCore — two-hand object manipulation (NEW in 2.0). Pure C++.
//
// The signature Apple Vision Pro gesture: grab an object with a pinch in each
// hand, then move/rotate/separate your hands to translate / rotate / scale it.
// All three transform components fall out of the two pinch points:
//
//   scale       = |R−L now|  /  |R−L at grab|        (hands apart ⇒ bigger)
//   rotation    = shortest arc from (R−L at grab) to (R−L now)
//   translation = midpoint(now) − midpoint(at grab)
//
// The transform pivots about the midpoint between the hands at grab time, which
// is what makes it feel like you are physically holding the object between your
// palms. This file is pure math so the mock harness can assert exact outputs
// (hands 2× apart ⇒ scale 2.0, 90° hand twist ⇒ 90° object yaw, etc.).

#pragma once

#include "PinchworkMath.h"

namespace Pinchwork
{
	// The frame-to-frame transform to apply to a grabbed object, relative to
	// its pose at grab time.
	struct FManipulationDelta
	{
		float Scale = 1.0f;                  // uniform scale multiplier
		FQuat Rotation = FQuat::Identity();  // full shortest-arc hand rotation
		FVec3 Translation;                   // midpoint displacement (cm)
		float YawDeltaDeg = 0.0f;            // rotation about world up only (turntable)
	};

	// A rigid-ish object pose the manipulator can transform. Scale is uniform
	// (a single multiplier) — the common case for grab-to-resize.
	struct FObjectTransform
	{
		FVec3 Position;
		FQuat Rotation = FQuat::Identity();
		float Scale = 1.0f;
	};

	// Stateful two-hand manipulator. Begin() latches the grab reference frame;
	// Update() returns the delta for the live hand positions. One instance per
	// grabbed object.
	class PINCHWORKCORE_API FTwoHandManipulator
	{
	public:
		// Latch the reference frame from the two pinch points at grab time.
		void Begin(const FVec3& LeftPinch, const FVec3& RightPinch);

		// True between Begin() and End().
		bool IsActive() const { return bActive; }

		// Compute the transform delta for the current pinch points. Returns an
		// identity delta if not active or the grab span was degenerate.
		FManipulationDelta Update(const FVec3& LeftPinch, const FVec3& RightPinch) const;

		// Apply a delta to an object's grab-time transform, pivoting about the
		// hand midpoint at grab time. Static so callers can store the object's
		// start transform however they like.
		FObjectTransform ApplyDelta(const FObjectTransform& GrabTimeTransform, const FManipulationDelta& Delta) const;

		void End() { bActive = false; }

		// Reference-frame accessors (useful for the UE wrapper / debugging).
		const FVec3& StartMidpoint() const { return StartMid; }
		float StartSpan() const { return StartDist; }

	private:
		bool  bActive = false;
		FVec3 StartLeft;
		FVec3 StartRight;
		FVec3 StartMid;
		FVec3 StartDir;       // normalized (right − left) at grab
		float StartDist = 0.0f;
	};

	// Signed yaw, in degrees, rotating direction A onto direction B about world
	// up (+Z). Positive = counter-clockwise seen from above. Exposed for
	// turntable-style rotate and unit-tested directly.
	PINCHWORKCORE_API float YawDeltaDegrees(const FVec3& DirA, const FVec3& DirB);
}
