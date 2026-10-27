// Copyright (c) 2026 Alex Coulombe. MIT License.
//
// UTwoHandManipulatorComponent — the Blueprint-facing wrapper around
// PinchworkCore's FTwoHandManipulator (NEW in 2.0).
//
// Two-hand grab is the signature Apple Vision Pro manipulation: pinch an object
// with both hands, then move / rotate / separate your hands to translate /
// rotate / scale it. Wire each hand's index-thumb pinch (the existing
// UHandTrackingComponent OnPinchStarted/Ended delegates) to BeginGrab/EndGrab,
// and feed the two live pinch points each tick. All math lives in the tested
// core; this component only converts FVector↔FVec3 and drives a target actor.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Pinchwork/PinchworkTwoHand.h"
#include "TwoHandManipulatorComponent.generated.h"

// Broadcast each tick a two-hand grab updates, with the live transform deltas
// (relative to grab time) for callers that want to drive something other than
// the bound TargetActor.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FTwoHandManipUpdated,
	float, Scale, FQuat, Rotation, FVector, Translation, float, YawDegrees);

UCLASS(ClassGroup = (VR), meta = (BlueprintSpawnableComponent), DisplayName = "Two-Hand Manipulator (Pinchwork)")
class PINCHWORK_API UTwoHandManipulatorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTwoHandManipulatorComponent();

	// The actor to scale/rotate/translate while grabbed. Optional — leave null
	// and consume OnManipulationUpdated to drive something yourself.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pinchwork|Two-Hand")
	TObjectPtr<AActor> TargetActor = nullptr;

	// Clamp the cumulative scale so a held object can't vanish or explode.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pinchwork|Two-Hand", meta = (ClampMin = "0.01"))
	float MinScale = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pinchwork|Two-Hand", meta = (ClampMin = "0.01"))
	float MaxScale = 20.0f;

	// When true, only the world-up (turntable) component of the hand rotation is
	// applied — often steadier than the full arc for placing objects.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pinchwork|Two-Hand")
	bool bYawOnlyRotation = false;

	UPROPERTY(BlueprintAssignable, Category = "Pinchwork|Two-Hand")
	FTwoHandManipUpdated OnManipulationUpdated;

	UPROPERTY(BlueprintReadOnly, Category = "Pinchwork|Two-Hand")
	bool bIsManipulating = false;

	// Latch the grab reference frame from both hands' pinch points. Captures
	// TargetActor's current transform as the basis for ApplyDelta.
	UFUNCTION(BlueprintCallable, Category = "Pinchwork|Two-Hand")
	void BeginGrab(FVector LeftPinch, FVector RightPinch);

	// Feed the live pinch points; applies the resulting delta to TargetActor (if
	// set) and broadcasts OnManipulationUpdated. No-op if not manipulating.
	UFUNCTION(BlueprintCallable, Category = "Pinchwork|Two-Hand")
	void UpdateGrab(FVector LeftPinch, FVector RightPinch);

	UFUNCTION(BlueprintCallable, Category = "Pinchwork|Two-Hand")
	void EndGrab();

private:
	Pinchwork::FTwoHandManipulator Manipulator;
	Pinchwork::FObjectTransform GrabTimeTransform;
	bool bHaveTargetBasis = false;
};
