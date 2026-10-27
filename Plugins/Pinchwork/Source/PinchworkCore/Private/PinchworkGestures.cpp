// Copyright (c) 2026 Alex Coulombe. MIT License.

#include "Pinchwork/PinchworkGestures.h"

namespace Pinchwork
{
	const char* GestureName(EGesture G)
	{
		switch (G)
		{
			case EGesture::None:            return "None";
			case EGesture::OpenPalm:        return "Open Palm";
			case EGesture::Fist:            return "Fist";
			case EGesture::ThumbsUp:        return "Thumbs Up";
			case EGesture::Peace:           return "Peace";
			case EGesture::FingerGuns:      return "Finger Guns";
			case EGesture::RockOn:          return "Rock On";
			case EGesture::CallMe:          return "Call Me";
			case EGesture::ThumbOverFist:   return "Thumb Over Fist";
			case EGesture::FingerGunsShoot: return "Finger Guns (Shot)";
		}
		return "?";
	}

	void FCalibration::ComputeThresholds(float GlobalFallback, float MinFingerSeparation)
	{
		for (int i = 0; i < 5; ++i)
		{
			const float Fist = FistRatios[i];
			const float Open = OpenRatios[i];
			if (Open > Fist + MinFingerSeparation)
			{
				Thresholds[i] = (Fist + Open) * 0.5f;
			}
			else
			{
				Thresholds[i] = GlobalFallback;
			}
		}
		bCalibrated = true;
	}

	float NormalizeFingerRatio(int FingerIdx, float Ratio, const FGestureConfig& Config, const FCalibration& Calib)
	{
		if (Calib.bCalibrated && FingerIdx >= 0 && FingerIdx < 5)
		{
			const float Fist = Calib.FistRatios[FingerIdx];
			const float Open = Calib.OpenRatios[FingerIdx];
			const float Range = Open - Fist;
			if (Range > 0.05f)
			{
				return Clamp((Ratio - Fist) / Range, 0.0f, 1.0f);
			}
		}
		// Uncalibrated fallback: a smooth ramp around the global threshold so
		// classification still has a gradient.
		const float Pivot = Config.FingerExtendedRatioThreshold;
		const float HalfWidth = 0.12f;
		return Clamp((Ratio - (Pivot - HalfWidth)) / (2.0f * HalfWidth), 0.0f, 1.0f);
	}

	FGestureResult ClassifyByConfidence(const float NormalizedRatios[5], const FGestureConfig& Config)
	{
		// Fingerprint encoding: 1 = expected extended, 0 = expected curled,
		// -1 = don't care. RockOn doesn't care about the thumb. ThumbOverFist is
		// intentionally absent — it shares {1,0,0,0,0} with ThumbsUp and is split
		// off by orientation in RecognizeGesture.
		struct FFingerprint
		{
			EGesture Gesture;
			int8_t Thumb, Index, Middle, Ring, Little;
		};
		static const FFingerprint Fingerprints[] = {
			{ EGesture::OpenPalm,         1,  1,  1,  1,  1 },
			{ EGesture::Fist,             0,  0,  0,  0,  0 },
			{ EGesture::ThumbsUp,         1,  0,  0,  0,  0 },
			{ EGesture::Peace,            0,  1,  1,  0,  0 },
			{ EGesture::FingerGuns,       1,  1,  0,  0,  0 },
			{ EGesture::FingerGunsShoot,  0,  1,  0,  0,  0 },
			{ EGesture::CallMe,           1,  0,  0,  0,  1 },
			{ EGesture::RockOn,          -1,  1,  0,  0,  1 },
		};

		float TopScore = 0.0f;
		float SecondScore = 0.0f;
		EGesture TopGesture = EGesture::None;

		for (const FFingerprint& FP : Fingerprints)
		{
			const int8_t Expect[5] = { FP.Thumb, FP.Index, FP.Middle, FP.Ring, FP.Little };
			// MIN-based (weakest-link) scoring: for near-identical fingerprints
			// the single differentiating finger dominates, instead of being
			// averaged away by the matching fingers.
			float MinScore = 1.0f;
			bool bHasContribution = false;
			for (int i = 0; i < 5; ++i)
			{
				if (Expect[i] < 0) { continue; }
				const float Want = (Expect[i] == 1) ? NormalizedRatios[i] : (1.0f - NormalizedRatios[i]);
				MinScore = (Want < MinScore) ? Want : MinScore;
				bHasContribution = true;
			}
			const float Score = bHasContribution ? MinScore : 0.0f;

			if (Score > TopScore)
			{
				SecondScore = TopScore;
				TopScore = Score;
				TopGesture = FP.Gesture;
			}
			else if (Score > SecondScore)
			{
				SecondScore = Score;
			}
		}

		FGestureResult Result;
		Result.Confidence = TopScore;

		// Gate 1: absolute floor.
		if (TopScore < Config.MinGestureConfidence)
		{
			Result.Gesture = EGesture::None;
			return Result;
		}
		// Gate 2: beat the runner-up by the margin (anti-flicker).
		if ((TopScore - SecondScore) < Config.MinGestureConfidenceMargin)
		{
			Result.Gesture = EGesture::None;
			return Result;
		}
		Result.Gesture = TopGesture;
		return Result;
	}

	FGestureResult RecognizeGesture(const FHandPose& Pose, const FGestureConfig& Config, const FCalibration& Calib)
	{
		float Raw[5];
		ComputeAllFingerRatios(Pose, Raw);

		float Normalized[5];
		for (int i = 0; i < 5; ++i)
		{
			Normalized[i] = NormalizeFingerRatio(i, Raw[i], Config, Calib);
		}

		FGestureResult Result = ClassifyByConfidence(Normalized, Config);

		// Orientation split: the curl pattern alone can't tell a genuine
		// thumbs-up from a thumb laid flat across the fist. Use the 3D thumb
		// direction vs world up.
		if (Result.Gesture == EGesture::ThumbsUp)
		{
			if (ComputeThumbUpAlignment(Pose) < Config.ThumbUpAlignmentThreshold)
			{
				Result.Gesture = EGesture::ThumbOverFist;
			}
		}
		return Result;
	}

	bool FPinchDetector::Update(float TipDistanceCm)
	{
		const float EnterCm = ThresholdCm;
		const float ExitCm = ThresholdCm + ReleaseHysteresisCm;
		if (!bPinching && TipDistanceCm <= EnterCm)
		{
			bPinching = true;
			return true;
		}
		if (bPinching && TipDistanceCm >= ExitCm)
		{
			bPinching = false;
			return true;
		}
		return false;
	}

	EGesture FGestureStabilizer::Update(EGesture Detected, float DeltaSeconds)
	{
		if (Detected == Pending)
		{
			PendingStableFor += DeltaSeconds;
		}
		else
		{
			Pending = Detected;
			PendingStableFor = 0.0f;
		}

		if (PendingStableFor >= StabilityWindowSec && Pending != Active)
		{
			Active = Pending;
		}
		return Active;
	}
}
