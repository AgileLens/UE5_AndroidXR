// Copyright (c) 2026 Alex Coulombe. MIT License.

#include "TwoHandManipulatorComponent.h"
#include "PinchworkUE.h"
#include "GameFramework/Actor.h"

UTwoHandManipulatorComponent::UTwoHandManipulatorComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // driven by explicit Update calls
}

void UTwoHandManipulatorComponent::BeginGrab(FVector LeftPinch, FVector RightPinch)
{
	Manipulator.Begin(PinchworkUE::ToCore(LeftPinch), PinchworkUE::ToCore(RightPinch));
	bIsManipulating = true;

	// Capture the target's transform at grab time as the basis for ApplyDelta.
	bHaveTargetBasis = false;
	if (TargetActor)
	{
		const FTransform Xf = TargetActor->GetActorTransform();
		GrabTimeTransform.Position = PinchworkUE::ToCore(Xf.GetLocation());
		GrabTimeTransform.Rotation = PinchworkUE::ToCore(Xf.GetRotation());
		// Uniform scale: take the max component so a non-uniform actor still
		// scales sensibly (the core uses a single multiplier).
		GrabTimeTransform.Scale = (float)Xf.GetScale3D().GetMax();
		bHaveTargetBasis = true;
	}
}

void UTwoHandManipulatorComponent::UpdateGrab(FVector LeftPinch, FVector RightPinch)
{
	if (!bIsManipulating || !Manipulator.IsActive())
	{
		return;
	}

	Pinchwork::FManipulationDelta Delta =
		Manipulator.Update(PinchworkUE::ToCore(LeftPinch), PinchworkUE::ToCore(RightPinch));

	// Optionally constrain rotation to the turntable (world-up) component.
	if (bYawOnlyRotation)
	{
		Delta.Rotation = Pinchwork::FQuat::FromAxisAngle(
			Pinchwork::WorldUp(), Pinchwork::DegToRad(Delta.YawDeltaDeg));
	}

	// Clamp cumulative scale against the grab-time basis so the held object
	// can't collapse or balloon past the configured bounds.
	if (bHaveTargetBasis && GrabTimeTransform.Scale > KINDA_SMALL_NUMBER)
	{
		const float Projected = GrabTimeTransform.Scale * Delta.Scale;
		const float Clamped = FMath::Clamp(Projected, MinScale, MaxScale);
		Delta.Scale = Clamped / GrabTimeTransform.Scale;
	}

	if (TargetActor && bHaveTargetBasis)
	{
		const Pinchwork::FObjectTransform Result = Manipulator.ApplyDelta(GrabTimeTransform, Delta);
		TargetActor->SetActorLocationAndRotation(
			PinchworkUE::ToUE(Result.Position), PinchworkUE::ToUE(Result.Rotation));
		TargetActor->SetActorScale3D(FVector(Result.Scale));
	}

	OnManipulationUpdated.Broadcast(
		Delta.Scale, PinchworkUE::ToUE(Delta.Rotation), PinchworkUE::ToUE(Delta.Translation), Delta.YawDeltaDeg);
}

void UTwoHandManipulatorComponent::EndGrab()
{
	Manipulator.End();
	bIsManipulating = false;
	bHaveTargetBasis = false;
}
