// Copyright (c) 2026 Alex Coulombe. MIT License.
//
// UGestureSequenceComponent — Blueprint-facing wrapper around PinchworkCore's
// FGestureSequenceRecognizer (NEW in 2.0).
//
// Register ordered gesture combos (e.g. Fist → OpenPalm → FingerGuns) and get a
// named event when the user performs one within the time budget. Drive it by
// wiring UHandTrackingComponent::OnGestureStarted into FeedGesture — the
// recognizer ignores the neutral None frames between poses, so natural combos
// just work.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HandTrackingComponent.h"   // EHandGesture
#include "Pinchwork/PinchworkSequence.h"
#include "GestureSequenceComponent.generated.h"

// Fired when a registered sequence completes. Name is the sequence's label;
// SequenceId is the index returned by RegisterSequence.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGestureSequenceCompleted, FString, Name, int32, SequenceId);

UCLASS(ClassGroup = (VR), meta = (BlueprintSpawnableComponent), DisplayName = "Gesture Sequence (Pinchwork)")
class PINCHWORK_API UGestureSequenceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGestureSequenceComponent();

	// Register a combo. Returns its id (>=0), or -1 if Steps is empty.
	// MaxStepIntervalSec is the longest pause allowed between consecutive steps
	// before progress resets.
	UFUNCTION(BlueprintCallable, Category = "Pinchwork|Sequence")
	int32 RegisterSequence(FString Name, const TArray<EHandGesture>& Steps, float MaxStepIntervalSec = 1.5f);

	// Feed one committed gesture transition (wire this to OnGestureStarted).
	// Broadcasts OnSequenceCompleted for every sequence that completes.
	UFUNCTION(BlueprintCallable, Category = "Pinchwork|Sequence")
	void FeedGesture(EHandGesture Gesture);

	// Clear all in-flight progress (e.g. on tracking loss). Registered
	// sequences are kept.
	UFUNCTION(BlueprintCallable, Category = "Pinchwork|Sequence")
	void ResetProgress();

	UPROPERTY(BlueprintAssignable, Category = "Pinchwork|Sequence")
	FGestureSequenceCompleted OnSequenceCompleted;

private:
	Pinchwork::FGestureSequenceRecognizer Recognizer;

	// Monotonic seconds for the step-timeout clock. Prefers world time; falls
	// back to an internal accumulator if there's no world.
	double GetNowSeconds() const;
};
