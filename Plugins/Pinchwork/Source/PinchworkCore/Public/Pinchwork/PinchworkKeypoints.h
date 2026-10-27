// Copyright (c) 2026 Alex Coulombe. MIT License.
//
// PinchworkCore — hand keypoint identifiers.
//
// This enum MIRRORS Unreal's `EHandKeypoint` (Engine/Source/Runtime/
// HeadMountedDisplay/Public/HeadMountedDisplayTypes.h) value-for-value. The
// ordering is the standard 26-joint OpenXR `XR_HAND_JOINT_*_EXT` layout and is
// load-bearing: `ComputeFingerExtensionRatio` sums distances along the
// contiguous Metacarpal→…→Tip run, which is only correct because each finger's
// joints are stored consecutively in this exact order.
//
// Keep in lockstep with `EHandKeypoint`. The UE wrapper static_asserts the
// counts match so a future engine change can't silently desync them.

#pragma once

#include <cstdint>

namespace Pinchwork
{
	enum class EKeypoint : uint8_t
	{
		Palm,
		Wrist,
		ThumbMetacarpal,
		ThumbProximal,
		ThumbDistal,
		ThumbTip,
		IndexMetacarpal,
		IndexProximal,
		IndexIntermediate,
		IndexDistal,
		IndexTip,
		MiddleMetacarpal,
		MiddleProximal,
		MiddleIntermediate,
		MiddleDistal,
		MiddleTip,
		RingMetacarpal,
		RingProximal,
		RingIntermediate,
		RingDistal,
		RingTip,
		LittleMetacarpal,
		LittleProximal,
		LittleIntermediate,
		LittleDistal,
		LittleTip
	};

	constexpr int KKeypointCount = static_cast<int>(EKeypoint::LittleTip) + 1; // 26

	inline int KeypointIndex(EKeypoint K) { return static_cast<int>(K); }

	// Finger index 0..4 = thumb / index / middle / ring / little, or -1 for a
	// keypoint that is not a fingertip. Matches FingerIndexFromTipKey() in the
	// 1.0 component.
	inline int FingerIndexFromTip(EKeypoint Tip)
	{
		switch (Tip)
		{
			case EKeypoint::ThumbTip:  return 0;
			case EKeypoint::IndexTip:  return 1;
			case EKeypoint::MiddleTip: return 2;
			case EKeypoint::RingTip:   return 3;
			case EKeypoint::LittleTip: return 4;
			default: return -1;
		}
	}

	// The metacarpal that begins the chain for a given fingertip, or -1.
	inline int MetacarpalIndexForTip(EKeypoint Tip)
	{
		switch (Tip)
		{
			case EKeypoint::ThumbTip:  return static_cast<int>(EKeypoint::ThumbMetacarpal);
			case EKeypoint::IndexTip:  return static_cast<int>(EKeypoint::IndexMetacarpal);
			case EKeypoint::MiddleTip: return static_cast<int>(EKeypoint::MiddleMetacarpal);
			case EKeypoint::RingTip:   return static_cast<int>(EKeypoint::RingMetacarpal);
			case EKeypoint::LittleTip: return static_cast<int>(EKeypoint::LittleMetacarpal);
			default: return -1;
		}
	}
}
