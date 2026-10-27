// Copyright (c) 2026 Alex Coulombe. MIT License.

#include "Pinchwork/PinchworkSequence.h"

namespace Pinchwork
{
	int FGestureSequenceRecognizer::AddSequence(const FGestureSequence& Sequence)
	{
		if (Sequence.Steps.empty())
		{
			return -1;
		}
		Sequences.push_back(Sequence);
		MatchedSteps.push_back(0);
		LastStepTime.push_back(0.0f);
		return static_cast<int>(Sequences.size()) - 1;
	}

	void FGestureSequenceRecognizer::ResetProgress()
	{
		for (size_t i = 0; i < MatchedSteps.size(); ++i)
		{
			MatchedSteps[i] = 0;
			LastStepTime[i] = 0.0f;
		}
	}

	std::vector<int> FGestureSequenceRecognizer::OnGesture(EGesture Gesture, float TimeSeconds)
	{
		std::vector<int> Completed;

		// None is a neutral separator — the hand passes through it between every
		// pose. Ignoring it is what lets "Fist then Open" match across the gap.
		if (Gesture == EGesture::None)
		{
			return Completed;
		}

		for (size_t i = 0; i < Sequences.size(); ++i)
		{
			const FGestureSequence& Seq = Sequences[i];

			// Time out a stalled in-flight combo before evaluating this step.
			if (MatchedSteps[i] > 0 && (TimeSeconds - LastStepTime[i]) > Seq.MaxStepIntervalSec)
			{
				MatchedSteps[i] = 0;
			}

			const EGesture Expected = Seq.Steps[MatchedSteps[i]];
			if (Gesture == Expected)
			{
				++MatchedSteps[i];
				LastStepTime[i] = TimeSeconds;
				if (MatchedSteps[i] == static_cast<int>(Seq.Steps.size()))
				{
					Completed.push_back(static_cast<int>(i));
					MatchedSteps[i] = 0; // re-armed for the next run
				}
			}
			else if (Gesture == Seq.Steps[0])
			{
				// A wrong gesture that nonetheless matches step 0 restarts the
				// combo (so Fist→Fist→Open still completes Fist→Open).
				MatchedSteps[i] = 1;
				LastStepTime[i] = TimeSeconds;
			}
			else
			{
				// Any other gesture breaks this combo.
				MatchedSteps[i] = 0;
			}
		}

		return Completed;
	}
}
