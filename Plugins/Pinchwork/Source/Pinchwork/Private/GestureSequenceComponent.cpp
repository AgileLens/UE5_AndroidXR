// Copyright (c) 2026 Alex Coulombe. MIT License.

#include "GestureSequenceComponent.h"
#include "Engine/World.h"

// EHandGesture (HandTrackingComponent.h) and Pinchwork::EGesture are declared
// in the same order intentionally; these asserts make the static_cast below
// safe and fail loudly if either enum is ever reordered.
static_assert((uint8)EHandGesture::None == (uint8)Pinchwork::EGesture::None, "EHandGesture/EGesture drift");
static_assert((uint8)EHandGesture::OpenPalm == (uint8)Pinchwork::EGesture::OpenPalm, "EHandGesture/EGesture drift");
static_assert((uint8)EHandGesture::Fist == (uint8)Pinchwork::EGesture::Fist, "EHandGesture/EGesture drift");
static_assert((uint8)EHandGesture::ThumbsUp == (uint8)Pinchwork::EGesture::ThumbsUp, "EHandGesture/EGesture drift");
static_assert((uint8)EHandGesture::Peace == (uint8)Pinchwork::EGesture::Peace, "EHandGesture/EGesture drift");
static_assert((uint8)EHandGesture::FingerGuns == (uint8)Pinchwork::EGesture::FingerGuns, "EHandGesture/EGesture drift");
static_assert((uint8)EHandGesture::RockOn == (uint8)Pinchwork::EGesture::RockOn, "EHandGesture/EGesture drift");
static_assert((uint8)EHandGesture::CallMe == (uint8)Pinchwork::EGesture::CallMe, "EHandGesture/EGesture drift");
static_assert((uint8)EHandGesture::ThumbOverFist == (uint8)Pinchwork::EGesture::ThumbOverFist, "EHandGesture/EGesture drift");
static_assert((uint8)EHandGesture::FingerGunsShoot == (uint8)Pinchwork::EGesture::FingerGunsShoot, "EHandGesture/EGesture drift");

namespace
{
	Pinchwork::EGesture ToCoreGesture(EHandGesture G)
	{
		return static_cast<Pinchwork::EGesture>((uint8)G);
	}
}

UGestureSequenceComponent::UGestureSequenceComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // driven by FeedGesture
}

int32 UGestureSequenceComponent::RegisterSequence(FString Name, const TArray<EHandGesture>& Steps, float MaxStepIntervalSec)
{
	Pinchwork::FGestureSequence Seq;
	Seq.Name = TCHAR_TO_UTF8(*Name);
	Seq.MaxStepIntervalSec = MaxStepIntervalSec;
	Seq.Steps.reserve(Steps.Num());
	for (EHandGesture G : Steps)
	{
		Seq.Steps.push_back(ToCoreGesture(G));
	}
	return Recognizer.AddSequence(Seq);
}

void UGestureSequenceComponent::FeedGesture(EHandGesture Gesture)
{
	const std::vector<int> Completed = Recognizer.OnGesture(ToCoreGesture(Gesture), (float)GetNowSeconds());
	for (int Id : Completed)
	{
		const FString Name = UTF8_TO_TCHAR(Recognizer.Sequence(Id).Name.c_str());
		OnSequenceCompleted.Broadcast(Name, Id);
	}
}

void UGestureSequenceComponent::ResetProgress()
{
	Recognizer.ResetProgress();
}

double UGestureSequenceComponent::GetNowSeconds() const
{
	if (const UWorld* World = GetWorld())
	{
		return World->GetTimeSeconds();
	}
	return 0.0;
}
