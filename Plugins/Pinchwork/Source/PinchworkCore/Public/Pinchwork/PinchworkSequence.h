// Copyright (c) 2026 Alex Coulombe. MIT License.
//
// PinchworkCore — gesture macros / sequences (NEW in 2.0). Pure C++.
//
// Recognize an ordered run of gestures — e.g. Fist → OpenPalm → FingerGuns —
// performed within a time budget, and fire a named event. This turns Pinchwork
// from a single-pose classifier into a small gesture "combo" system: think
// cheat codes, mode switches, or deliberate confirm/cancel motions that a
// single static pose can't express safely.
//
// Contract: feed OnGesture() ONE committed gesture transition at a time (the
// new Active gesture out of FGestureStabilizer), with a monotonic timestamp.
// EGesture::None transitions are ignored — the neutral frames a hand passes
// through between poses must not break a combo — so "Fist then Open" works even
// though the hand is briefly None in between.

#pragma once

#include "PinchworkGestures.h"
#include <string>
#include <vector>

namespace Pinchwork
{
	struct FGestureSequence
	{
		std::string Name;
		std::vector<EGesture> Steps;
		// Max wall-clock allowed between two consecutive matched steps. If the
		// user dawdles past this, progress resets (so an old half-combo can't
		// complete minutes later). The clock starts at the first matched step.
		float MaxStepIntervalSec = 1.5f;

		FGestureSequence() = default;
		FGestureSequence(std::string InName, std::vector<EGesture> InSteps, float InMaxStepIntervalSec = 1.5f)
			: Name(std::move(InName)), Steps(std::move(InSteps)), MaxStepIntervalSec(InMaxStepIntervalSec) {}
	};

	// Tracks progress through any number of registered sequences in parallel.
	class PINCHWORKCORE_API FGestureSequenceRecognizer
	{
	public:
		// Register a sequence; returns its id (index). Empty sequences are
		// rejected (returns -1).
		int AddSequence(const FGestureSequence& Sequence);

		// Feed one committed gesture transition. Returns the ids of every
		// sequence that COMPLETED on this event (usually empty or one). A
		// completed sequence's progress resets so it can fire again. None is
		// ignored (returns empty, no state change).
		std::vector<int> OnGesture(EGesture Gesture, float TimeSeconds);

		// Clear all in-flight progress (e.g. on tracking loss). Registered
		// sequences are kept.
		void ResetProgress();

		int Count() const { return static_cast<int>(Sequences.size()); }
		const FGestureSequence& Sequence(int Id) const { return Sequences[Id]; }

		// How many steps of sequence `Id` are currently matched (for HUD/debug).
		int Progress(int Id) const { return MatchedSteps[Id]; }

	private:
		std::vector<FGestureSequence> Sequences;
		std::vector<int>   MatchedSteps;  // matched-step count per sequence
		std::vector<float> LastStepTime;  // timestamp of the last matched step
	};
}
