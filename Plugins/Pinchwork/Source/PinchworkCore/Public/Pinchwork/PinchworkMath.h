// Copyright (c) 2026 Alex Coulombe. MIT License.
//
// PinchworkCore — pure C++ math primitives. NO Unreal Engine dependency.
//
// Why this exists: Pinchwork 1.0 welded all of its gesture math into a
// UObject component, so the recognition logic could only run inside a live
// UE world on a headset. PinchworkCore lifts the math into plain C++ that
// compiles with a stock `clang++ -std=c++17` and can therefore be unit-tested
// off-headset with synthetic ("mock") joint data — the only way to regression
// test gestures when the visionOS simulator has no hand tracking.
//
// Coordinate convention: this core assumes the data it is fed already lives in
// Unreal's world space — left-handed, Z-up, centimetre units — because that is
// what `IHandTracker::GetKeypointState` reports. The UE wrapper converts
// `FVector`/`FQuat` to these plain structs at the boundary; everything inside
// the core is convention-agnostic except `WorldUp()` (defined as +Z below) and
// the gesture orientation checks that consume it.

#pragma once

#include <cmath>

// In a UE build this expands to the module's import/export macro; standalone
// (the test harness) it is empty. Guarding here lets the exact same headers
// compile in both worlds.
#ifndef PINCHWORKCORE_API
#define PINCHWORKCORE_API
#endif

namespace Pinchwork
{
	constexpr float KSmallNumber = 1.0e-4f;
	constexpr float KPi = 3.14159265358979323846f;

	inline float DegToRad(float Deg) { return Deg * (KPi / 180.0f); }
	inline float RadToDeg(float Rad) { return Rad * (180.0f / KPi); }

	inline float Clamp(float V, float Lo, float Hi)
	{
		return V < Lo ? Lo : (V > Hi ? Hi : V);
	}

	inline float Lerp(float A, float B, float Alpha)
	{
		return A + (B - A) * Alpha;
	}

	// Minimal 3-component vector. Float (not double): every quantity the core
	// derives is a dimensionless, scale-invariant ratio or an angle, so single
	// precision is ample and keeps the struct trivially copyable/POD.
	struct FVec3
	{
		float X = 0.0f;
		float Y = 0.0f;
		float Z = 0.0f;

		FVec3() = default;
		FVec3(float InX, float InY, float InZ) : X(InX), Y(InY), Z(InZ) {}

		FVec3 operator+(const FVec3& O) const { return FVec3(X + O.X, Y + O.Y, Z + O.Z); }
		FVec3 operator-(const FVec3& O) const { return FVec3(X - O.X, Y - O.Y, Z - O.Z); }
		FVec3 operator*(float S) const { return FVec3(X * S, Y * S, Z * S); }

		float SizeSquared() const { return X * X + Y * Y + Z * Z; }
		float Size() const { return std::sqrt(SizeSquared()); }

		bool IsNearlyZero(float Tol = KSmallNumber) const { return Size() <= Tol; }

		// Returns a unit vector, or ZeroFallback when this vector is shorter
		// than the tolerance (mirrors FVector::GetSafeNormal semantics).
		FVec3 GetSafeNormal(float Tol = KSmallNumber, const FVec3& ZeroFallback = FVec3(0, 0, 0)) const
		{
			const float Len = Size();
			if (Len <= Tol)
			{
				return ZeroFallback;
			}
			const float Inv = 1.0f / Len;
			return FVec3(X * Inv, Y * Inv, Z * Inv);
		}
	};

	inline float Dot(const FVec3& A, const FVec3& B)
	{
		return A.X * B.X + A.Y * B.Y + A.Z * B.Z;
	}

	inline FVec3 Cross(const FVec3& A, const FVec3& B)
	{
		return FVec3(
			A.Y * B.Z - A.Z * B.Y,
			A.Z * B.X - A.X * B.Z,
			A.X * B.Y - A.Y * B.X);
	}

	inline float Dist(const FVec3& A, const FVec3& B)
	{
		return (A - B).Size();
	}

	inline FVec3 Midpoint(const FVec3& A, const FVec3& B)
	{
		return (A + B) * 0.5f;
	}

	// Unreal world up (+Z). The thumb-orientation check (ThumbsUp vs
	// ThumbOverFist) dots the thumb direction against this.
	inline FVec3 WorldUp() { return FVec3(0.0f, 0.0f, 1.0f); }

	// Minimal unit quaternion for the two-hand rotation feature. Only the
	// operations the manipulator needs are implemented.
	struct FQuat
	{
		float X = 0.0f;
		float Y = 0.0f;
		float Z = 0.0f;
		float W = 1.0f;

		FQuat() = default;
		FQuat(float InX, float InY, float InZ, float InW) : X(InX), Y(InY), Z(InZ), W(InW) {}

		static FQuat Identity() { return FQuat(0, 0, 0, 1); }

		static FQuat FromAxisAngle(const FVec3& Axis, float AngleRad)
		{
			const FVec3 N = Axis.GetSafeNormal();
			if (N.IsNearlyZero())
			{
				return Identity();
			}
			const float Half = AngleRad * 0.5f;
			const float S = std::sin(Half);
			return FQuat(N.X * S, N.Y * S, N.Z * S, std::cos(Half));
		}

		// Shortest-arc rotation taking unit-ish From onto unit-ish To.
		static FQuat FromTo(const FVec3& From, const FVec3& To)
		{
			const FVec3 F = From.GetSafeNormal();
			const FVec3 T = To.GetSafeNormal();
			if (F.IsNearlyZero() || T.IsNearlyZero())
			{
				return Identity();
			}
			const float D = Clamp(Dot(F, T), -1.0f, 1.0f);
			if (D >= 1.0f - KSmallNumber)
			{
				return Identity(); // already aligned
			}
			if (D <= -1.0f + KSmallNumber)
			{
				// Antiparallel: rotate 180° about any axis perpendicular to F.
				FVec3 Axis = Cross(FVec3(1, 0, 0), F);
				if (Axis.IsNearlyZero())
				{
					Axis = Cross(FVec3(0, 1, 0), F);
				}
				return FromAxisAngle(Axis, KPi);
			}
			const FVec3 Axis = Cross(F, T);
			const float Angle = std::acos(D);
			return FromAxisAngle(Axis, Angle);
		}

		float Size() const { return std::sqrt(X * X + Y * Y + Z * Z + W * W); }

		FQuat Normalized() const
		{
			const float Len = Size();
			if (Len <= KSmallNumber)
			{
				return Identity();
			}
			const float Inv = 1.0f / Len;
			return FQuat(X * Inv, Y * Inv, Z * Inv, W * Inv);
		}

		// Rotation angle in radians, in [0, Pi].
		float GetAngleRad() const
		{
			const float ClampedW = Clamp(W, -1.0f, 1.0f);
			return 2.0f * std::acos(std::fabs(ClampedW));
		}

		FVec3 RotateVector(const FVec3& V) const
		{
			// v' = v + 2 * cross(q.xyz, cross(q.xyz, v) + w*v)
			const FVec3 U(X, Y, Z);
			const FVec3 T = Cross(U, V) + V * W;
			return V + Cross(U, T) * 2.0f;
		}

		FQuat operator*(const FQuat& O) const
		{
			return FQuat(
				W * O.X + X * O.W + Y * O.Z - Z * O.Y,
				W * O.Y - X * O.Z + Y * O.W + Z * O.X,
				W * O.Z + X * O.Y - Y * O.X + Z * O.W,
				W * O.W - X * O.X - Y * O.Y - Z * O.Z);
		}
	};
}
