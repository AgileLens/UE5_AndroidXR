// Copyright (c) 2026 Alex Coulombe. MIT License.
//
// PinchworkCore — curl-pattern gesture recognition. Pure C++.
//
// This is the faithful extraction of the recognition pipeline that lived in
// UHandTrackingComponent: raw finger ratios → per-finger normalization (with
// optional per-user calibration) → fingerprint match with confidence/margin
// gating → orientation disambiguation (ThumbsUp vs ThumbOverFist). Keeping the
// algorithm here — and identical to 1.0 — is what lets the mock harness assert
// the on-device classifier's behavior without a headset.

#pragma once

#include "PinchworkHandPose.h"

namespace Pinchwork
{
	// Mirrors EHandGesture (HandTrackingComponent.h) value-for-value so the UE
	// wrapper can static_cast between them.
	enum class EGesture : uint8_t
	{
		None,
		OpenPalm,
		Fist,
		ThumbsUp,
		Peace,
		FingerGuns,
		RockOn,
		CallMe,
		ThumbOverFist,
		FingerGunsShoot
	};

	PINCHWORKCORE_API const char* GestureName(EGesture G);

	// Tunables that in 1.0 were UPROPERTYs on the component. Defaults match the
	// shipped component values exactly.
	struct FGestureConfig
	{
		// Fallback extension ratio treated as "extended" when no calibration is
		// present. Also the pivot of the uncalibrated normalization ramp.
		float FingerExtendedRatioThreshold = 0.92f;
		// Absolute floor a fingerprint's match score must clear to classify.
		float MinGestureConfidence = 0.40f;
		// The winner must beat the runner-up by at least this margin.
		float MinGestureConfidenceMargin = 0.05f;
		// Min dot(thumbDir, up) for a {1,0,0,0,0} curl to count as ThumbsUp
		// rather than ThumbOverFist.
		float ThumbUpAlignmentThreshold = 0.5f;
	};

	// Per-user calibration: the fist/open reference ratios captured during the
	// two-pose walkthrough, plus the per-finger thresholds derived from them.
	// Port of the shared calibration state (GSharedFistRatios / GSharedOpenRatios
	// / GSharedCalibratedThresholds + TryAdvanceCalibrationPhase's midpoint math).
	struct PINCHWORKCORE_API FCalibration
	{
		bool  bCalibrated = false;
		float FistRatios[5] = {};
		float OpenRatios[5] = {};
		float Thresholds[5] = {};

		// Derive per-finger thresholds at the midpoint of each finger's fist and
		// open ratios. A finger whose open ratio isn't meaningfully higher than
		// its fist ratio (e.g. a thumb that can't straighten) falls back to the
		// global threshold instead of poisoning its own classification.
		// Sets bCalibrated. Mirrors TryAdvanceCalibrationPhase (AwaitingOpen leg).
		void ComputeThresholds(float GlobalFallback, float MinFingerSeparation = 0.10f);
	};

	// Map a raw extension ratio for finger `FingerIdx` (0..4) into [0,1] where
	// 0 ≈ the user's fist pose and 1 ≈ their open pose. Calibrated path is a
	// clean per-finger remap; uncalibrated path is a soft ramp around the global
	// threshold. Port of NormalizeFingerRatio.
	PINCHWORKCORE_API float NormalizeFingerRatio(int FingerIdx, float Ratio, const FGestureConfig& Config, const FCalibration& Calib);

	struct FGestureResult
	{
		EGesture Gesture = EGesture::None;
		float    Confidence = 0.0f;
	};

	// Score the five normalized ratios against every gesture fingerprint and
	// return the winner, gated by the absolute floor and the runner-up margin.
	// Does NOT apply the ThumbsUp/ThumbOverFist orientation split — that needs
	// the 3D pose and is handled in RecognizeGesture. Port of
	// ClassifyGestureByConfidence.
	PINCHWORKCORE_API FGestureResult ClassifyByConfidence(const float NormalizedRatios[5], const FGestureConfig& Config);

	// Full single-frame recognition: raw ratios → normalize → classify →
	// orientation disambiguation. This is the function the mock harness and the
	// UE wrapper both call. (Temporal stability debouncing is a stateful layer
	// on top — see FGestureStabilizer.)
	PINCHWORKCORE_API FGestureResult RecognizeGesture(const FHandPose& Pose, const FGestureConfig& Config, const FCalibration& Calib);

	// Per-finger pinch detector with release hysteresis — Pinchwork's namesake.
	// Enter the pinch when the two fingertips close within ThresholdCm; only
	// release once they separate past ThresholdCm + ReleaseHysteresisCm, so a
	// fingertip hovering at the boundary doesn't chatter the pinch on/off. Port
	// of UpdatePinchState's enter/exit logic.
	struct PINCHWORKCORE_API FPinchDetector
	{
		float ThresholdCm = 3.0f;
		float ReleaseHysteresisCm = 1.0f;
		bool  bPinching = false;

		// Feed the current fingertip-to-fingertip distance. Returns true on the
		// frame the pinch state flips (so the caller can fire started/ended);
		// read bPinching for the current state.
		bool Update(float TipDistanceCm);
	};

	// Temporal debounce: a freshly recognized gesture must persist for
	// `StabilityWindowSec` before it commits, filtering the momentary "None"
	// frames seen during transitions (fist→peace, etc.). Port of the
	// PendingGesture / PendingGestureStableFor logic in UpdateGestureState.
	struct PINCHWORKCORE_API FGestureStabilizer
	{
		float    StabilityWindowSec = 0.15f;
		EGesture Active = EGesture::None;
		EGesture Pending = EGesture::None;
		float    PendingStableFor = 0.0f;

		// Feed the per-frame recognized gesture + frame delta; returns the
		// (possibly unchanged) committed gesture. Compare the return value
		// across calls to detect started/ended transitions.
		EGesture Update(EGesture Detected, float DeltaSeconds);
	};
}
