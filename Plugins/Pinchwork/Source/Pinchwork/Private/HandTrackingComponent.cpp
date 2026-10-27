#include "HandTrackingComponent.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/OverlapResult.h"
#include "Materials/MaterialInterface.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Engine/Engine.h"            // GEngine->AddOnScreenDebugMessage (debug depth-cycle feedback)
#include "HAL/IConsoleManager.h"      // runtime CVar set for the depth-fix test cycle
#include "Math/RotationMatrix.h"      // FRotationMatrix::MakeFromXZ for the gun-orientation cycler
#include "Components/AudioComponent.h"        // Superman-fly wind sound
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"                  // bLooping for the wind loop
#include "Particles/ParticleSystem.h"        // Superman-fly ambient dust
#include "Particles/ParticleSystemComponent.h"
#include "Features/IModularFeatures.h"
#include "HeadMountedDisplayTypes.h"
#include "IHandTracker.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Sound/SoundBase.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "HandSkeletalDriverComponent.h"

// --- Gun: middle-curl fire trigger + grip offset (tunable; dial on-device, then bake into the gun BP) ---
static TAutoConsoleVariable<float> CVarGunTriggerCurl(
	TEXT("r.Gun.TriggerCurlRatio"), 0.72f,
	TEXT("While holding a gun, fire when the MIDDLE finger's extension ratio drops BELOW this "
	     "(1.0 = straight, lower = curled). Non-thumb gesture so it coexists with the index-thumb grip."),
	ECVF_Default);
static TAutoConsoleVariable<float> CVarGunGripX(TEXT("r.Gun.GripOffsetX"), 0.0f, TEXT("Held-gun grip offset X (cm, relative to the pinch point)"), ECVF_Default);
static TAutoConsoleVariable<float> CVarGunGripY(TEXT("r.Gun.GripOffsetY"), 0.0f, TEXT("Held-gun grip offset Y (cm)"), ECVF_Default);
static TAutoConsoleVariable<float> CVarGunGripZ(TEXT("r.Gun.GripOffsetZ"), 0.0f, TEXT("Held-gun grip offset Z (cm)"), ECVF_Default);
static TAutoConsoleVariable<float> CVarGunGripPitch(TEXT("r.Gun.GripPitch"), 0.0f, TEXT("Held-gun grip relative pitch (deg)"), ECVF_Default);
static TAutoConsoleVariable<float> CVarGunGripYaw(TEXT("r.Gun.GripYaw"), 180.0f, TEXT("Held-gun grip relative yaw (deg). 180 = flip so the muzzle points away from the user, not at them."), ECVF_Default);
static TAutoConsoleVariable<float> CVarGunGripRoll(TEXT("r.Gun.GripRoll"), -90.0f, TEXT("Held-gun grip relative roll (deg). -90 = stand the pistol upright (yaw 180 tipped it onto its side; +90 tipped it the wrong way)."), ECVF_Default);
static TAutoConsoleVariable<float> CVarGunTriggerHyst(TEXT("r.Gun.TriggerCurlHysteresis"), 0.12f, TEXT("Release margin above the fire curl ratio (debounce, so firing doesn't flicker at the threshold)."), ECVF_Default);
static TAutoConsoleVariable<float> CVarGunAutoFireInterval(TEXT("r.Gun.AutoFireInterval"), 1.0f, TEXT("While the trigger (middle curl) is held, auto-fire one shot every this many seconds (first shot immediate)."), ECVF_Default);

// @AGILELENS - on-device A/B for the visionOS translucent-depth-fixup (glass over passthrough). The AVP
// has no in-app console, so a ring-thumb pinch on a FREE hand (no gun held) cycles the depth-fixup CVars
// at runtime. GHeldGrabCount gates the dual-purpose ring-pinch: hold a gun -> tune grip orient (below);
// hold nothing -> cycle the depth fix. See ~/knowledge/projects/pinchwork/translucency-depth-fix.md.
static int32 GHeldGrabCount = 0;             // # actors grabbed across both hands (0 => hands free)
static int32 GTranslucentDepthTestIndex = 0; // current slot in the depth-fixup A/B cycle

// Held-gun grip ORIENTATION as an index into the 24 axis-aligned orientations, instead of guessing
// Euler angles for the mesh's unknown native axes. A ring-thumb pinch on the FREE hand steps it (the
// holding hand's thumb is busy with the index-thumb grip), the gun snaps to it live, and the index is
// shown on the HUD — spin until the muzzle points forward + the pistol is upright, read the number,
// then bake it as this default. <0 = fall back to the GripPitch/Yaw/Roll Euler CVars above.
// Held-gun grip orientation is HAND-DEPENDENT: the pinch anchor rides each hand's pinch pose, which is
// MIRRORED between left and right, so the same gun mesh needs a different axis-aligned orientation to
// point muzzle-forward + upright in each hand. Baked on-device by cycling (ring-thumb pinch on the FREE
// hand steps the HELD hand's index): right=21, left=16. <0 on either = fall back to the Euler CVars.
static TAutoConsoleVariable<int32> CVarGunOrientRight(
	TEXT("r.Gun.OrientIndexRight"), 21,
	TEXT("Held-gun grip orientation for the RIGHT hand (0..23 axis-aligned). <0 = use the Euler CVars."),
	ECVF_Default);
static TAutoConsoleVariable<int32> CVarGunOrientLeft(
	TEXT("r.Gun.OrientIndexLeft"), 16,
	TEXT("Held-gun grip orientation for the LEFT hand (0..23, mirror of the right). <0 = use Euler CVars."),
	ECVF_Default);

// The Nth of the 24 proper axis-aligned orientations: local +X -> one of ±X/±Y/±Z, local +Z -> a
// perpendicular axis. Cycling all 24 is guaranteed to hit muzzle-forward + upright for ANY native mesh
// axes, so we never have to solve the Euler composition by hand.
static FRotator GunGripRotationFromIndex(int32 Idx)
{
	static const FVector Ax[6] = {
		FVector(1, 0, 0), FVector(-1, 0, 0), FVector(0, 1, 0),
		FVector(0, -1, 0), FVector(0, 0, 1), FVector(0, 0, -1) };
	const int32 FwdAxis = ((Idx / 4) % 6 + 6) % 6;
	const FVector F = Ax[FwdAxis];
	FVector Ups[4];
	int32 n = 0;
	for (int32 k = 0; k < 6 && n < 4; ++k)
	{
		if (FMath::Abs(FVector::DotProduct(Ax[k], F)) < 0.5f) { Ups[n++] = Ax[k]; }
	}
	const FVector U = Ups[((Idx % 4) + 4) % 4];
	return FRotationMatrix::MakeFromXZ(F, U).Rotator();
}
static constexpr int32 GGunOrientCount = 24;

// Held-gun grip rotation for the given hand: per-hand orientation index if >= 0, else the Euler CVars.
static FRotator CurrentGunGripRotation(bool bIsRight)
{
	const int32 Idx = bIsRight
		? CVarGunOrientRight.GetValueOnGameThread()
		: CVarGunOrientLeft.GetValueOnGameThread();
	if (Idx >= 0)
	{
		return GunGripRotationFromIndex(Idx);
	}
	return FRotator(CVarGunGripPitch.GetValueOnGameThread(),
		CVarGunGripYaw.GetValueOnGameThread(), CVarGunGripRoll.GetValueOnGameThread());
}

// Throttle accumulators for the continuous [GunTune] middle-ratio log (index 0 = right, 1 = left).
static float GGunTuneLogAccum[2] = { 0.0f, 0.0f };

// --- Superman two-fist fly locomotion ---
static TAutoConsoleVariable<int32> CVarFlyEnable(TEXT("r.Fly.Enable"), 1, TEXT("1 = two-fist (Superman) fly: both hands fisted -> drift along the average fist direction."), ECVF_Default);
static TAutoConsoleVariable<float> CVarFlySpeed(TEXT("r.Fly.Speed"), 80.0f, TEXT("Superman fly speed in cm/s (very slow by default)."), ECVF_Default);
static TAutoConsoleVariable<float> CVarFlyCoast(TEXT("r.Fly.CoastSec"), 0.3f, TEXT("Keep flying for this long after a fist briefly drops (debounces gesture/tracking flicker). The fist times out after this, so flight can't 'keep going' once you open your hands."), ECVF_Default);
// Shared across both per-hand components: the last game-time each hand showed a valid fist (timestamps,
// so a stale value can't latch — it just expires after the coast window).
static double  GFlyLeftFistTime  = -1000.0;
static double  GFlyRightFistTime = -1000.0;
static FVector GFlyLeftDir   = FVector::ZeroVector;
static FVector GFlyRightDir  = FVector::ZeroVector;

namespace
{
	// IHandTracker reports keypoints in world space already, so converting
	// from meters→centimeters isn't needed — UE's FTransform is in UU (cm).
	constexpr float ScaleToCmFactor = 1.0f;

	// Shared calibration state across every UHandTrackingComponent instance
	// in the process. Both left and right hand components read from and
	// write to these globals so the calibration phase stays synchronized
	// (the user does the same pose with both hands simultaneously) and the
	// resulting per-finger thresholds are shared — left and right hand
	// anatomy is symmetric enough that one calibration covers both.
	EHandCalibrationState GSharedCalibrationPhase = EHandCalibrationState::Uncalibrated;
	TArray<float> GSharedFistRatios;       // size 5 once captured
	TArray<float> GSharedOpenRatios;       // size 5 once captured
	TArray<float> GSharedCalibratedThresholds;  // size 5, zeroed before calibration

	// Cooldown (process-time) so one pinky-pinch = one level travel. Without it, a
	// pinch HELD across the (sub-100ms) OpenLevel re-fires on the freshly-spawned hand
	// component in the NEW level and travels straight back — flashing A<->B for a few
	// frames until release. FPlatformTime::Seconds() persists across level loads (world
	// time resets), so the window survives the world teardown. Also collapses a
	// simultaneous both-hands pinch into a single travel.
	double GLastLevelTravelSeconds = 0.0;
	constexpr double GLevelTravelCooldownSeconds = 1.5;

	void ResetSharedCalibration()
	{
		GSharedCalibrationPhase = EHandCalibrationState::AwaitingFist;
		GSharedFistRatios.Reset();
		GSharedOpenRatios.Reset();
		GSharedCalibratedThresholds.Reset();
	}

	// Atomic-ish phase advance. Pass the phase the caller observed; if the
	// shared state has already moved on (the other hand captured first this
	// same frame), the report is dropped. Returns true on successful advance.
	bool TryAdvanceCalibrationPhase(EHandCalibrationState ObservedPhase, const TArray<float>& Ratios, float MinFingerSeparation, float GlobalFallback)
	{
		if (GSharedCalibrationPhase != ObservedPhase || Ratios.Num() != 5)
		{
			return false;
		}

		if (ObservedPhase == EHandCalibrationState::AwaitingFist)
		{
			GSharedFistRatios = Ratios;
			GSharedCalibrationPhase = EHandCalibrationState::AwaitingOpen;
			return true;
		}
		if (ObservedPhase == EHandCalibrationState::AwaitingOpen)
		{
			GSharedOpenRatios = Ratios;
			// Compute per-finger thresholds at the midpoint. Any finger
			// whose Open ratio isn't meaningfully higher than its Fist
			// ratio falls back to the global threshold — a degenerate
			// capture for that finger (e.g., thumb that can't straighten)
			// shouldn't poison its classification.
			GSharedCalibratedThresholds.SetNum(5);
			for (int32 i = 0; i < 5; ++i)
			{
				const float Fist = GSharedFistRatios.IsValidIndex(i) ? GSharedFistRatios[i] : 0.0f;
				const float Open = GSharedOpenRatios.IsValidIndex(i) ? GSharedOpenRatios[i] : 0.0f;
				if (Open > Fist + MinFingerSeparation)
				{
					GSharedCalibratedThresholds[i] = (Fist + Open) * 0.5f;
				}
				else
				{
					GSharedCalibratedThresholds[i] = GlobalFallback;
				}
			}
			GSharedCalibrationPhase = EHandCalibrationState::Calibrated;
			return true;
		}
		return false;
	}

	IHandTracker* FindHandTracker()
	{
		IModularFeatures& Features = IModularFeatures::Get();
		const FName FeatureName = IHandTracker::GetModularFeatureName();
		if (!Features.IsModularFeatureAvailable(FeatureName))
		{
			return nullptr;
		}
		TArray<IHandTracker*> Trackers = Features.GetModularFeatureImplementations<IHandTracker>(FeatureName);
		for (IHandTracker* Tracker : Trackers)
		{
			if (Tracker != nullptr)
			{
				return Tracker;
			}
		}
		return nullptr;
	}
}

UHandTrackingComponent::UHandTrackingComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	bWantsInitializeComponent = true;
	CachedKeypoints.SetNum(EHandKeypointCount);

	// Five-slot arrays — one entry per finger (thumb, index, middle, ring, little).
	CalibratedThresholds.Init(0.0f, 5);
	CalibrationStabilityWindow.SetNum(5);

	// FObjectFinder* internally calls CheckIfIsInConstructor which fatal-asserts
	// (LogClass: ConstructorHelpers used outside of constructor) if invoked
	// from runtime code. Resolving here — the only legal site — caches the
	// engine sphere mesh for use at BeginPlay.
	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> EngineSphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	DefaultJointMesh = EngineSphereFinder.Get();

	// Hard-reference the per-hand gesture Input Actions from the CDO so they are
	// always cooked — StaticLoadObject / string paths are NOT cook references, so
	// without this the assets wouldn't be packaged and the injection would no-op.
	static ConstructorHelpers::FObjectFinderOptional<UInputAction> IdxL(TEXT("/Pinchwork/Input/IA_IndexThumbPinch_Left.IA_IndexThumbPinch_Left"));
	static ConstructorHelpers::FObjectFinderOptional<UInputAction> IdxR(TEXT("/Pinchwork/Input/IA_IndexThumbPinch_Right.IA_IndexThumbPinch_Right"));
	static ConstructorHelpers::FObjectFinderOptional<UInputAction> MidL(TEXT("/Pinchwork/Input/IA_MiddleThumbPinch_Left.IA_MiddleThumbPinch_Left"));
	static ConstructorHelpers::FObjectFinderOptional<UInputAction> MidR(TEXT("/Pinchwork/Input/IA_MiddleThumbPinch_Right.IA_MiddleThumbPinch_Right"));
	static ConstructorHelpers::FObjectFinderOptional<UInputAction> PnkL(TEXT("/Pinchwork/Input/IA_PinkyThumbPinch_Left.IA_PinkyThumbPinch_Left"));
	static ConstructorHelpers::FObjectFinderOptional<UInputAction> PnkR(TEXT("/Pinchwork/Input/IA_PinkyThumbPinch_Right.IA_PinkyThumbPinch_Right"));
	static ConstructorHelpers::FObjectFinderOptional<UInputAction> ShtR(TEXT("/Pinchwork/Input/IA_Shoot_Right.IA_Shoot_Right"));
	IndexThumbPinchActionLeft  = IdxL.Get();
	IndexThumbPinchActionRight = IdxR.Get();
	MiddlePinchActionLeft      = MidL.Get();
	MiddlePinchActionRight     = MidR.Get();
	PinkyPinchActionLeft       = PnkL.Get();
	PinkyPinchActionRight      = PnkR.Get();
	ShootActionRight           = ShtR.Get();
}

void UHandTrackingComponent::BeginPlay()
{
	Super::BeginPlay();
	EnsureInstancesInitialized();

	if (bAutoCalibrateOnStart && CalibrationState == EHandCalibrationState::Uncalibrated)
	{
		StartCalibration();
	}

	// Scene-load sting — left instance only so it fires exactly once per level load.
	if (!bIsRight && bPlaySceneLoadSound)
	{
		PlaySceneLoadSound();
	}
}

void UHandTrackingComponent::EnsureInstancesInitialized()
{
	if (JointInstances)
	{
		return;
	}

	JointInstances = NewObject<UInstancedStaticMeshComponent>(GetOwner(), UInstancedStaticMeshComponent::StaticClass(), NAME_None, RF_Transient);
	JointInstances->SetupAttachment(this);
	JointInstances->SetMobility(EComponentMobility::Movable);
	JointInstances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	JointInstances->SetCastShadow(false);
	JointInstances->SetReceivesDecals(false);
	JointInstances->bSelectable = false;
	JointInstances->SetGenerateOverlapEvents(false);
	JointInstances->RegisterComponent();

	if (UStaticMesh* Mesh = ResolveJointMesh())
	{
		JointInstances->SetStaticMesh(Mesh);
	}

	const FTransform HiddenInstance(FRotator::ZeroRotator, FVector::ZeroVector, FVector(KINDA_SMALL_NUMBER));
	for (int32 i = 0; i < EHandKeypointCount; ++i)
	{
		JointInstances->AddInstance(HiddenInstance, /*bWorldSpace=*/true);
	}
}

UStaticMesh* UHandTrackingComponent::ResolveJointMesh() const
{
	if (JointMesh)
	{
		return JointMesh;
	}
	// DefaultJointMesh was resolved in the constructor (the only legal site
	// for ConstructorHelpers). Engine BasicShape Sphere is shipped with every
	// project, so this should virtually never be null in a real build, but
	// caller paths guard for nullptr regardless.
	return DefaultJointMesh;
}

EControllerHand UHandTrackingComponent::ResolveControllerHand() const
{
	return bIsRight ? EControllerHand::Right : EControllerHand::Left;
}

void UHandTrackingComponent::PlaySceneLoadSound()
{
	USoundBase* Sound = IsInLevelB() ? SceneLoadSoundLevelB : SceneLoadSoundLevelA;
	if (!Sound)
	{
		// StarterContent defaults: Level A (Cobalt Lab) = an electrical "power-on";
		// Level B (Stone Courtyard) = birdsong. Both are one-shot SoundWaves.
		const TCHAR* Path = IsInLevelB()
			? TEXT("/Game/StarterContent/Audio/Starter_Birds01.Starter_Birds01")
			: TEXT("/Game/StarterContent/Audio/Light01.Light01");
		Sound = Cast<USoundBase>(StaticLoadObject(USoundBase::StaticClass(), nullptr, Path));
	}
	if (Sound)
	{
		UGameplayStatics::PlaySound2D(this, Sound);
	}
}

void UHandTrackingComponent::InjectGestureActions()
{
	if (!bInjectGestureInputActions)
	{
		return;
	}
	const UWorld* World = GetWorld();
	APlayerController* PC = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	ULocalPlayer* LP = PC ? PC->GetLocalPlayer() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* EI =
		LP ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LP) : nullptr;
	if (!EI)
	{
		return;
	}
	// Injecting each frame the gesture is active keeps the action "triggered";
	// when the gesture ends, injection stops and the action completes — giving a
	// clean hold/release that any binding (e.g. the VRPawn grab) can consume.
	auto Inject = [EI](UInputAction* IA, bool bActive)
	{
		if (IA && bActive)
		{
			EI->InjectInputForAction(IA, FInputActionValue(true), {}, {});
		}
	};
	Inject(bIsRight ? IndexThumbPinchActionRight : IndexThumbPinchActionLeft, bIsPinching);
	Inject(bIsRight ? MiddlePinchActionRight : MiddlePinchActionLeft, bIsMiddlePinching);
	Inject(bIsRight ? PinkyPinchActionRight : PinkyPinchActionLeft, bIsPinkyPinching);
}

namespace
{
	bool ActorHasGrabComponent(AActor* Actor)
	{
		if (!Actor) { return false; }
		for (UActorComponent* C : Actor->GetComponents())
		{
			if (C && C->GetClass()->GetName().Contains(TEXT("Grab")))
			{
				return true;
			}
		}
		return false;
	}
}

void UHandTrackingComponent::EnsurePinchAnchor()
{
	if (PinchAnchor)
	{
		return;
	}
	PinchAnchor = NewObject<USceneComponent>(GetOwner());
	PinchAnchor->SetupAttachment(this);
	PinchAnchor->RegisterComponent();
}

void UHandTrackingComponent::NotifyPinchGrab(bool bPressed)
{
	// Index-thumb pinch grab. On pinch: grab the nearest actor with a GrabComponent
	// AT THE PINCH POINT (thumb-index midpoint), not the wrist. On release: throw it
	// with the hand's velocity. The held object rides PinchAnchor (kept on the pinch
	// point each Tick by UpdateHeldActorFollow).
	if (bPressed)
	{
		TryPinchGrab(CurrentPinchMidpoint);
	}
	else
	{
		ReleasePinchGrab();
	}
}

void UHandTrackingComponent::TryPinchGrab(const FVector& PinchLocation)
{
	if (HeldActor)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	EnsurePinchAnchor();

	TArray<FOverlapResult> Hits;
	FCollisionQueryParams Params(TEXT("HandPinchGrab"), false);
	Params.AddIgnoredActor(GetOwner());
	World->OverlapMultiByObjectType(Hits, PinchLocation, FQuat::Identity,
		FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllDynamicObjects),
		FCollisionShape::MakeSphere(GrabRadiusCm), Params);

	AActor* Best = nullptr;
	float BestDistSq = FLT_MAX;
	for (const FOverlapResult& Hit : Hits)
	{
		AActor* Candidate = Hit.GetActor();
		if (!Candidate || !ActorHasGrabComponent(Candidate))
		{
			continue;
		}
		const float DistSq = FVector::DistSquared(Candidate->GetActorLocation(), PinchLocation);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Candidate;
		}
	}
	if (!Best)
	{
		return;
	}

	// Physics must be disabled before attaching, or the attach is ignored.
	for (UActorComponent* C : Best->GetComponents())
	{
		if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(C))
		{
			Prim->SetSimulatePhysics(false);
			Prim->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		}
	}

	// Anchor at the pinch point WITH the hand's rotation, so the object rides it
	// like a motion-controller grip pose. Snap location to the pinch point; keep the
	// object's current world rotation (no jarring re-orient), then it follows the
	// hand's rotation from there via the rigid attach.
	PinchAnchor->SetWorldLocationAndRotation(PinchLocation, CurrentPinchRotation);
	Best->AttachToComponent(PinchAnchor, FAttachmentTransformRules(
		EAttachmentRule::SnapToTarget, EAttachmentRule::KeepWorld, EAttachmentRule::KeepWorld, false));

	// Grip offset: shift/orient the held object (relative to PinchAnchor) so its HANDLE sits in the
	// thumb-index "O" instead of its origin. Orientation comes from the per-hand cycler (r.Gun.OrientIndex{Right,Left})
	// or the legacy Euler CVars; position from r.Gun.GripOffset{X,Y,Z}. Re-applied every tick in
	// UpdateHeldActorFollow so a ring-pinch can retune it live. Set once here for the first frame.
	Best->SetActorRelativeRotation(CurrentGunGripRotation(bIsRight));
	Best->SetActorRelativeLocation(FVector(
		CVarGunGripX.GetValueOnGameThread(), CVarGunGripY.GetValueOnGameThread(), CVarGunGripZ.GetValueOnGameThread()));

	HeldActor = Best;
	++GHeldGrabCount;  // @AGILELENS - a grab is active -> free-hand ring-pinch tunes gun orient (not depth)
	HeldPinchLocation = PinchLocation;
	HeldPinchVelocity = FVector::ZeroVector;
	// Stamp the grab so fire is suppressed for ~0.3s (no discharge on pickup); start un-fired.
	GunGrabTimeSeconds = World->GetTimeSeconds();
	bIsGunFiring = false;
	// Hide this hand's mesh while holding so it doesn't clip through the object.
	SetHeldHandMeshHidden(true);
}

void UHandTrackingComponent::ReleasePinchGrab()
{
	if (!HeldActor)
	{
		return;
	}
	AActor* Released = HeldActor;
	HeldActor = nullptr;
	if (GHeldGrabCount > 0) { --GHeldGrabCount; }  // @AGILELENS - keep the depth-cycle gate in sync
	// Show this hand's mesh again.
	SetHeldHandMeshHidden(false);

	Released->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

	const FVector ThrowVelocity = HeldPinchVelocity * ThrowVelocityScale;
	UPrimitiveComponent* ThrowPrim = nullptr;
	for (UActorComponent* C : Released->GetComponents())
	{
		if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(C))
		{
			Prim->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Prim->SetSimulatePhysics(true);
			if (!ThrowPrim && Prim->IsSimulatingPhysics())
			{
				ThrowPrim = Prim;
			}
		}
	}
	if (ThrowPrim)
	{
		ThrowPrim->SetPhysicsLinearVelocity(ThrowVelocity);
	}
}

void UHandTrackingComponent::UpdateHeldActorFollow(const FVector& PinchLocation, float DeltaTime)
{
	if (!HeldActor || !PinchAnchor)
	{
		return;
	}
	const float Dt = FMath::Max(DeltaTime, 1e-4f);
	const FVector InstantVelocity = (PinchLocation - HeldPinchLocation) / Dt;
	// Light smoothing so one noisy frame doesn't wreck the throw, but stay snappy.
	HeldPinchVelocity = FMath::Lerp(HeldPinchVelocity, InstantVelocity, 0.5f);
	HeldPinchLocation = PinchLocation;
	// Track both location and hand rotation so the held object rotates naturally.
	PinchAnchor->SetWorldLocationAndRotation(PinchLocation, CurrentPinchRotation);
	// Re-apply the grip pose (relative to the anchor) every tick so the held-gun orientation can be
	// tuned LIVE: a ring-thumb pinch on the free hand steps the held hand's r.Gun.OrientIndex{Right,Left}.
	HeldActor->SetActorRelativeRotation(CurrentGunGripRotation(bIsRight));
	HeldActor->SetActorRelativeLocation(FVector(
		CVarGunGripX.GetValueOnGameThread(), CVarGunGripY.GetValueOnGameThread(), CVarGunGripZ.GetValueOnGameThread()));
}

void UHandTrackingComponent::SetHeldHandMeshHidden(bool bHidden)
{
	if (!HandMeshDriver)
	{
		// Find this hand's sibling skeletal-mesh driver (same side) once.
		if (AActor* Owner = GetOwner())
		{
			TArray<UHandSkeletalDriverComponent*> Drivers;
			Owner->GetComponents<UHandSkeletalDriverComponent>(Drivers);
			for (UHandSkeletalDriverComponent* Driver : Drivers)
			{
				if (Driver && Driver->bIsRight == bIsRight)
				{
					HandMeshDriver = Driver;
					break;
				}
			}
		}
	}
	if (HandMeshDriver)
	{
		HandMeshDriver->SetHandMeshHidden(bHidden);
	}
}

// Lazily create the Superman-fly feedback FX (once), attached to the pawn: a soft looping wind sound and
// an ambient-dust particle system. Both are StarterContent assets (cooked via DirectoriesToAlwaysCook).
// Null-safe: if an asset can't be loaded, that FX is simply skipped (no crash).
void UHandTrackingComponent::EnsureFlyFX(APawn* Pawn)
{
	if (!Pawn) { return; }
	USceneComponent* Root = Pawn->GetRootComponent();
	if (!Root) { return; }

	if (!FlySoundComp)
	{
		if (USoundBase* Wind = Cast<USoundBase>(StaticLoadObject(USoundBase::StaticClass(), nullptr,
			TEXT("/Game/StarterContent/Audio/Starter_Wind06.Starter_Wind06"))))
		{
			if (USoundWave* Wave = Cast<USoundWave>(Wind)) { Wave->bLooping = true; }
			FlySoundComp = NewObject<UAudioComponent>(Pawn);
			if (FlySoundComp)
			{
				FlySoundComp->bAutoActivate = false;
				FlySoundComp->SetSound(Wind);
				FlySoundComp->SetVolumeMultiplier(0.0f); // start silent; faded in on activate
				FlySoundComp->SetupAttachment(Root);
				FlySoundComp->RegisterComponent();
			}
		}
	}

	if (!FlyParticleComp)
	{
		if (UParticleSystem* Dust = Cast<UParticleSystem>(StaticLoadObject(UParticleSystem::StaticClass(), nullptr,
			TEXT("/Game/StarterContent/Particles/P_Ambient_Dust.P_Ambient_Dust"))))
		{
			FlyParticleComp = NewObject<UParticleSystemComponent>(Pawn);
			if (FlyParticleComp)
			{
				FlyParticleComp->bAutoActivate = false;
				FlyParticleComp->SetTemplate(Dust);
				FlyParticleComp->SetupAttachment(Root);
				FlyParticleComp->RegisterComponent();
			}
		}
	}
}

// Fade the fly sound in/out and activate/deactivate the dust while flying. Soft (low volume) and quick
// fades so it doesn't pop. Idempotent — safe to call every tick.
void UHandTrackingComponent::SetFlyFXActive(bool bActive)
{
	if (FlySoundComp)
	{
		if (bActive)
		{
			if (!FlySoundComp->IsPlaying()) { FlySoundComp->FadeIn(0.4f, 0.35f); } // soft target volume
		}
		else if (FlySoundComp->IsPlaying())
		{
			FlySoundComp->FadeOut(0.4f, 0.0f);
		}
	}
	if (FlyParticleComp)
	{
		if (bActive && !FlyParticleComp->IsActive()) { FlyParticleComp->ActivateSystem(); }
		else if (!bActive && FlyParticleComp->IsActive()) { FlyParticleComp->DeactivateSystem(); }
	}
}

void UHandTrackingComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	EnsureInstancesInitialized();

	// Level banner draws independently of hand tracking so it's visible even when
	// the hands aren't in view. Left instance only, to avoid drawing it twice.
	if (bShowLevelLabel && !bIsRight)
	{
		DrawLevelLabel();
	}

	IHandTracker* Tracker = FindHandTracker();
	const bool bTrackerValid = Tracker && Tracker->IsHandTrackingStateValid();
	bIsTracking = bTrackerValid;

	if (!bTrackerValid)
	{
		if ((bHideMarkersWhenUntracked || !bShowJointMarkers) && JointInstances)
		{
			JointInstances->SetVisibility(false);
		}
		// Release any in-progress gesture state if we lose tracking mid-pose
		// so the label hides and downstream listeners get a clean ended event.
		if (bIsPinching)
		{
			bIsPinching = false;
			OnPinchEnded.Broadcast(GetSide());
			NotifyPinchGrab(false);
		}
		if (bIsMiddlePinching)
		{
			bIsMiddlePinching = false;
			OnMiddlePinchEnded.Broadcast(GetSide());
		}
		if (bIsPinkyPinching)
		{
			bIsPinkyPinching = false;
			OnPinkyPinchEnded.Broadcast(GetSide());
		}
		if (ActiveGesture != EHandGesture::None)
		{
			const EHandGesture EndedGesture = ActiveGesture;
			ActiveGesture = EHandGesture::None;
			PendingGesture = EHandGesture::None;
			PendingGestureStableFor = 0.0f;
			OnGestureEnded.Broadcast(EndedGesture, GetSide());
		}
		// DrawDebugString is per-frame so silence is automatic when we don't call it.
		return;
	}

	const EControllerHand Hand = ResolveControllerHand();
	const FVector MarkerScale(JointMeshScale);

	// Keep the instanced markers' visibility in sync with the user toggle.
	// Per-instance transform writes are skipped below when markers are
	// off, but the visibility itself also needs to flip on/off explicitly.
	if (JointInstances)
	{
		JointInstances->SetVisibility(bShowJointMarkers);
	}

	FTransform ThumbTipWorld = FTransform::Identity;
	FTransform IndexTipWorld = FTransform::Identity;
	FTransform MiddleTipWorld = FTransform::Identity;
	FTransform LittleTipWorld = FTransform::Identity;
	FTransform RingTipWorld = FTransform::Identity;
	bool bHaveThumbTip = false;
	bool bHaveIndexTip = false;
	bool bHaveMiddleTip = false;
	bool bHaveLittleTip = false;
	bool bHaveRingTip = false;

	for (int32 i = 0; i < EHandKeypointCount; ++i)
	{
		const EHandKeypoint Keypoint = static_cast<EHandKeypoint>(i);
		FTransform KeypointTransform;
		float Radius = 0.0f;
		const bool bGot = Tracker->GetKeypointState(Hand, Keypoint, KeypointTransform, Radius);
		if (!bGot)
		{
			CachedKeypoints[i] = FTransform::Identity;
			if (JointInstances)
			{
				const FTransform HiddenInstance(FRotator::ZeroRotator, FVector::ZeroVector, FVector(KINDA_SMALL_NUMBER));
				JointInstances->UpdateInstanceTransform(i, HiddenInstance, /*bWorldSpace=*/true, /*bMarkRenderStateDirty=*/false, /*bTeleport=*/true);
			}
			continue;
		}

		KeypointTransform.SetScale3D(KeypointTransform.GetScale3D() * ScaleToCmFactor);
		CachedKeypoints[i] = KeypointTransform;

		if (JointInstances && bShowJointMarkers)
		{
			FTransform InstanceTransform = KeypointTransform;
			InstanceTransform.SetScale3D(MarkerScale);
			JointInstances->UpdateInstanceTransform(i, InstanceTransform, /*bWorldSpace=*/true, /*bMarkRenderStateDirty=*/false, /*bTeleport=*/true);
		}

		if (Keypoint == EHandKeypoint::ThumbTip)
		{
			ThumbTipWorld = KeypointTransform;
			bHaveThumbTip = true;
		}
		else if (Keypoint == EHandKeypoint::IndexTip)
		{
			IndexTipWorld = KeypointTransform;
			bHaveIndexTip = true;
		}
		else if (Keypoint == EHandKeypoint::MiddleTip)
		{
			MiddleTipWorld = KeypointTransform;
			bHaveMiddleTip = true;
		}
		else if (Keypoint == EHandKeypoint::LittleTip)
		{
			LittleTipWorld = KeypointTransform;
			bHaveLittleTip = true;
		}
		else if (Keypoint == EHandKeypoint::RingTip)
		{
			RingTipWorld = KeypointTransform;
			bHaveRingTip = true;
		}
	}

	if (JointInstances && bShowJointMarkers)
	{
		JointInstances->MarkRenderStateDirty();
	}

	if (bHaveThumbTip && bHaveIndexTip)
	{
		CurrentPinchMidpoint = (ThumbTipWorld.GetLocation() + IndexTipWorld.GetLocation()) * 0.5f;
		// Hand orientation from the Palm keypoint, so a held object rotates with the
		// hand (the anchor acts like a motion-controller grip pose). Keep the last
		// good rotation if the palm isn't tracked this frame.
		const FTransform PalmXform = GetKeypointWorldTransform(EHandKeypoint::Palm);
		if (!PalmXform.GetLocation().IsNearlyZero())
		{
			CurrentPinchRotation = PalmXform.GetRotation();
		}
		UpdatePinchState(ThumbTipWorld, IndexTipWorld);
	}
	if (bHaveThumbTip && bHaveMiddleTip)
	{
		UpdateMiddlePinchState(ThumbTipWorld, MiddleTipWorld);
	}
	if (bHaveThumbTip && bHaveLittleTip)
	{
		UpdatePinkyPinchState(ThumbTipWorld, LittleTipWorld);
	}
	if (bHaveThumbTip && bHaveRingTip)
	{
		UpdateRingPinchState(ThumbTipWorld, RingTipWorld);
	}
	// While a pinch-grabbed object is held, keep it on the pinch point and track
	// the hand's velocity (used for the throw on release).
	if (HeldActor)
	{
		UpdateHeldActorFollow(CurrentPinchMidpoint, DeltaTime);
	}

	// Gun trigger: while holding a gun, a MIDDLE-finger curl fires it. The middle finger is free (the gun
	// is held by the index-thumb pinch), so this coexists with the grip. OCCLUSION-COAST: curling the
	// firing finger frequently hides its own tip from the cameras, so the tip reads ratio 0 ("untracked")
	// at exactly the moment you mean to fire. We must NOT treat that as "stop". So: a CLEAR curl starts
	// firing, a CONFIDENT open stops it, and an occluded/ambiguous reading HOLDS the current state — the
	// same robust pattern as the fly quick-stop. (Earlier a `ratio > 0.05` floor killed firing entirely
	// because the strong-curl/occluded case it was meant to reject is the firing case.) Fires the
	// HAND-held actor directly (EnableInput + inject IA_Shoot_Right, same as the gamepad R2 path).
	{
		const float MiddleRatio = ComputeFingerExtensionRatio(EHandKeypoint::MiddleTip);
		const float CurlEnter = CVarGunTriggerCurl.GetValueOnGameThread();           // curl below -> fire
		const float CurlExit  = CurlEnter + CVarGunTriggerHyst.GetValueOnGameThread(); // extend above -> stop
		const bool  bValid     = MiddleRatio > 0.02f;          // 0 == untracked/occluded -> coast, don't drop
		const bool  bClearCurl     = bValid && MiddleRatio < CurlEnter;
		const bool  bConfidentOpen = bValid && MiddleRatio > CurlExit;
		const double NowSec = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		const bool  bGrabSettled = (NowSec - GunGrabTimeSeconds) > 0.3;  // no discharge for 0.3s after grab

		if (!HeldActor)
		{
			bIsGunFiring = false;
		}
		else if (bClearCurl && bGrabSettled)
		{
			bIsGunFiring = true;
		}
		else if (bConfidentOpen)
		{
			bIsGunFiring = false;
		}
		// else: occluded/ambiguous tip -> keep the current bIsGunFiring (don't drop fire on a hidden tip).
		const bool bFire = HeldActor && bIsGunFiring;

		// CONTINUOUS (throttled ~3x/sec) tuning log: prints the middle ratio WHENEVER a gun is held —
		// not just on a fire-state change — so even a never-fires case still shows the real curl numbers.
		const int32 FireHandIdx = bIsRight ? 0 : 1;
		if (HeldActor)
		{
			GGunTuneLogAccum[FireHandIdx] += DeltaTime;
			if (GGunTuneLogAccum[FireHandIdx] >= 0.33f)
			{
				GGunTuneLogAccum[FireHandIdx] = 0.0f;
				UE_LOG(LogTemp, Warning, TEXT("[GunTune] %s middleRatio=%.3f valid=%d fire=%d (enter<%.2f exit>%.2f)"),
					bIsRight ? TEXT("R") : TEXT("L"), MiddleRatio, bValid ? 1 : 0, bFire ? 1 : 0, CurlEnter, CurlExit);
			}
		}
		APlayerController* FirePC = GetWorld() ? UGameplayStatics::GetPlayerController(GetWorld(), 0) : nullptr;
		if (HeldActor && FirePC)
		{
			if (bFire) HeldActor->EnableInput(FirePC); else HeldActor->DisableInput(FirePC);
			// Auto-fire: while the trigger is HELD, repeat the shot once per r.Gun.AutoFireInterval (default
			// 1s). First shot is immediate (LastGunShotTime starts at -1000); on release we reset it so the
			// next curl fires immediately again. (Previously injected every tick, deferring to the weapon's
			// own fire rate; now it's a deliberate steady cadence.)
			if (bFire)
			{
				const double NowFire = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
				if (NowFire - LastGunShotTime >= CVarGunAutoFireInterval.GetValueOnGameThread())
				{
					LastGunShotTime = NowFire;
					if (ULocalPlayer* LP = FirePC->GetLocalPlayer())
					{
						if (auto* EI = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LP))
						{
							if (ShootActionRight)
							{
								EI->InjectInputForAction(ShootActionRight, FInputActionValue(true), {}, {});
							}
						}
					}
				}
			}
			else
			{
				LastGunShotTime = -1000.0; // not firing -> next curl fires immediately
			}
		}
	}
	// Calibration runs every tick while not yet completed so it can finish
	// the moment the user holds a stable pose, even if it's mid-Tick.
	if (CalibrationState != EHandCalibrationState::Calibrated)
	{
		UpdateCalibrationState(DeltaTime);
	}

	if (bDetectCurlGestures)
	{
		UpdateGestureState(DeltaTime);
	}

	// Superman fly: BOTH hands fisted -> drift slowly along the average fist direction (wrist ->
	// middle-metacarpal). Robustness: a fist is a DIRECT valid-curl check (ratio in a curled-but-valid
	// band, gated on IsTracking) — NOT the gesture classifier (which flickers) and NOT a bare ratio<thr
	// (an UNtracked finger reads ratio 0, which would falsely look "curled" and keep flying). Each hand
	// stamps the game-time it last showed a fist; flight needs both stamps within a short coast window,
	// so brief flicker/dropout doesn't stutter it AND it stops on its own once you open your hands.
	{
		auto IsCurled   = [this](EHandKeypoint Tip) { const float R = ComputeFingerExtensionRatio(Tip); return R > 0.08f && R < 0.72f; };
		auto IsExtended = [this](EHandKeypoint Tip) { return ComputeFingerExtensionRatio(Tip) > 0.85f; };
		const bool bRawFist = IsTracking()
			&& IsCurled(EHandKeypoint::IndexTip) && IsCurled(EHandKeypoint::MiddleTip)
			&& IsCurled(EHandKeypoint::RingTip)  && IsCurled(EHandKeypoint::LittleTip);
		// A CONFIDENT open palm (index/middle/ring clearly extended) -> stop NOW. This is the difference
		// between "maybe still a fist" (coast/keep flying) and "that's definitely open" (cut immediately).
		const bool bRawOpen = IsTracking()
			&& IsExtended(EHandKeypoint::IndexTip) && IsExtended(EHandKeypoint::MiddleTip)
			&& IsExtended(EHandKeypoint::RingTip);

		const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		if (bRawFist)
		{
			const FVector WristLoc = GetKeypointWorldTransform(EHandKeypoint::Wrist).GetLocation();
			const FVector MetaLoc  = GetKeypointWorldTransform(EHandKeypoint::MiddleMetacarpal).GetLocation();
			const FVector FwdDir = (MetaLoc - WristLoc).GetSafeNormal();
			if (bIsRight) { GFlyRightFistTime = Now; GFlyRightDir = FwdDir; }
			else          { GFlyLeftFistTime  = Now; GFlyLeftDir  = FwdDir; }
		}
		else if (bRawOpen)
		{
			// Expire this hand's fist stamp at once so flight stops immediately (no coast on a clear open).
			if (bIsRight) GFlyRightFistTime = -1000.0; else GFlyLeftFistTime = -1000.0;
		}
		// else: ambiguous pose -> leave the stamp; the coast window keeps flight steady through flicker.

		// Apply the movement once — from the right-hand instance — when BOTH hands fisted recently.
		if (bIsRight && CVarFlyEnable.GetValueOnGameThread() != 0)
		{
			const double Coast = CVarFlyCoast.GetValueOnGameThread();
			const bool bBothFist = (Now - GFlyLeftFistTime < Coast) && (Now - GFlyRightFistTime < Coast);
			APawn* FlyPawn = nullptr;
			if (APlayerController* PC = GetWorld() ? UGameplayStatics::GetPlayerController(GetWorld(), 0) : nullptr)
			{
				FlyPawn = PC->GetPawn();
			}
			if (bBothFist)
			{
				const FVector Dir = (GFlyLeftDir + GFlyRightDir).GetSafeNormal();
				if (!Dir.IsNearlyZero() && FlyPawn)
				{
					FlyPawn->AddActorWorldOffset(Dir * CVarFlySpeed.GetValueOnGameThread() * DeltaTime, false);
				}
				// Soft wind + ambient dust whizzing past: a vection cue that helps reduce motion sickness.
				EnsureFlyFX(FlyPawn);
				SetFlyFXActive(true);
			}
			else
			{
				SetFlyFXActive(false);
			}
		}
	}

	// Surface active gestures as Enhanced Input actions (e.g. IA_IndexThumbPinch).
	InjectGestureActions();

	if (bShowGestureLabel)
	{
		UpdateGestureLabel();
	}
}

void UHandTrackingComponent::UpdatePinchState(const FTransform& ThumbTipWorld, const FTransform& IndexTipWorld)
{
	const float DistanceCm = FVector::Dist(ThumbTipWorld.GetLocation(), IndexTipWorld.GetLocation());
	const float EnterCm = PinchThresholdCm;
	const float ExitCm = PinchThresholdCm + PinchReleaseHysteresisCm;

	if (!bIsPinching && DistanceCm <= EnterCm)
	{
		bIsPinching = true;
		OnPinchStarted.Broadcast(GetSide());
		NotifyPinchGrab(true);
		if (bSpawnDotOnIndexPinch)
		{
			const FVector Midpoint = (ThumbTipWorld.GetLocation() + IndexTipWorld.GetLocation()) * 0.5f;
			SpawnTransientSphere(Midpoint, IndexPinchDotRadiusCm, IndexPinchDotDurationSec);
		}
	}
	else if (bIsPinching && DistanceCm >= ExitCm)
	{
		bIsPinching = false;
		OnPinchEnded.Broadcast(GetSide());
		NotifyPinchGrab(false);
	}
}

void UHandTrackingComponent::UpdatePinkyPinchState(const FTransform& ThumbTipWorld, const FTransform& PinkyTipWorld)
{
	const float DistanceCm = FVector::Dist(ThumbTipWorld.GetLocation(), PinkyTipWorld.GetLocation());
	const float EnterCm = PinkyPinchThresholdCm;
	const float ExitCm = PinkyPinchThresholdCm + PinkyPinchReleaseHysteresisCm;

	if (!bIsPinkyPinching && DistanceCm <= EnterCm)
	{
		bIsPinkyPinching = true;
		OnPinkyPinchStarted.Broadcast(GetSide());
		if (bSpawnExplosionOnPinkyPinch)
		{
			const FVector Midpoint = (ThumbTipWorld.GetLocation() + PinkyTipWorld.GetLocation()) * 0.5f;
			SpawnExplosion(Midpoint);
		}
		// Pinky-to-thumb pinch toggles Level A <-> Level B (test). The
		// GLevelTravelInProgress guard inside TravelToOtherLevel makes a
		// simultaneous both-hands pinch fire only once.
		if (bPinchToTravel)
		{
			TravelToOtherLevel();
		}
	}
	else if (bIsPinkyPinching && DistanceCm >= ExitCm)
	{
		bIsPinkyPinching = false;
		OnPinkyPinchEnded.Broadcast(GetSide());
	}
}

// Ring-thumb pinch (on a FREE hand) is dual-purpose, gated by GHeldGrabCount:
//  - hands EMPTY  -> A/B the visionOS translucent-depth-fixup CVars (glass-over-passthrough fix; no AVP console).
//  - gun HELD     -> step the HELD gun's grip orientation through the 24 axis-aligned poses (r.Gun.OrientIndex*).
// Both set CVars at runtime so the right value is found on-device without a rebuild per guess.
void UHandTrackingComponent::UpdateRingPinchState(const FTransform& ThumbTipWorld, const FTransform& RingTipWorld)
{
	const float DistanceCm = FVector::Dist(ThumbTipWorld.GetLocation(), RingTipWorld.GetLocation());
	const float EnterCm = PinkyPinchThresholdCm;
	const float ExitCm = PinkyPinchThresholdCm + PinkyPinchReleaseHysteresisCm;

	if (!bIsRingPinching && DistanceCm <= EnterCm)
	{
		bIsRingPinching = true;

		// @AGILELENS - dual-purpose ring-pinch. With nothing held, A/B the visionOS translucent-depth-fixup
		// over passthrough (no console on AVP). The engine now writes the RESOLVED depth swapchain (what the
		// compositor reads), so unlike the pre-fix build a NORMAL non-zero value should actually land — find
		// the smallest value that kills the blocky. {WritesDepth on/off, reverse-Z fixup value}; fixed order
		// so you can count pinches from launch. Slot 0 = OFF (blocky baseline). HUD tag: [DepthCycle].
		if (GHeldGrabCount <= 0)
		{
			static const struct { int32 On; float Value; } DepthPresets[] = {
				{ 0, 0.0f }, { 1, 0.001f }, { 1, 0.01f }, { 1, 0.05f }, { 1, 0.2f }, { 1, 0.5f },
			};
			const int32 DepthN = UE_ARRAY_COUNT(DepthPresets);
			GTranslucentDepthTestIndex = (GTranslucentDepthTestIndex + 1) % DepthN;
			const auto& P = DepthPresets[GTranslucentDepthTestIndex];
			if (IConsoleVariable* CvOn = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Mobile.VisionOS.TranslucentWritesDepth")))
			{
				CvOn->Set(P.On, ECVF_SetByConsole);
			}
			if (IConsoleVariable* CvVal = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Mobile.VisionOS.TranslucentDepthFixupValue")))
			{
				CvVal->Set(P.Value, ECVF_SetByConsole);
			}
			const FString DMsg = FString::Printf(TEXT("[DepthCycle] %d/%d  %s  value=%.4f"),
				GTranslucentDepthTestIndex, DepthN - 1, P.On ? TEXT("ON") : TEXT("OFF"), P.Value);
			UE_LOG(LogTemp, Warning, TEXT("%s"), *DMsg);
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(7788, 8.0f, P.On ? FColor::Green : FColor::Orange, DMsg);
			}
			return;
		}

		// Step the held-gun grip orientation (0..23) for the HOLDING hand. This ring-pinch is on the FREE
		// hand (the holding hand's thumb is busy with the index-thumb grip), so it tunes the OPPOSITE hand —
		// which is the one actually holding the gun. Per-hand because the pinch anchor is mirrored L/R.
		// Spin until muzzle-forward + upright, read the index off the HUD, then bake it into the default.
		const bool bHoldingRight = !bIsRight;  // the free (pinching) hand tunes the other hand's gun
		const TCHAR* CvName = bHoldingRight ? TEXT("r.Gun.OrientIndexRight") : TEXT("r.Gun.OrientIndexLeft");
		const int32 Cur = bHoldingRight
			? CVarGunOrientRight.GetValueOnGameThread() : CVarGunOrientLeft.GetValueOnGameThread();
		const int32 Next = (FMath::Max(0, Cur) + 1) % GGunOrientCount;
		if (IConsoleVariable* Cv = IConsoleManager::Get().FindConsoleVariable(CvName))
		{
			Cv->Set(Next, ECVF_SetByConsole);
		}
		const FRotator R = GunGripRotationFromIndex(Next);
		const FString Msg = FString::Printf(TEXT("[GunOrient] %s-hand index=%d/%d  rot=(P=%.0f Y=%.0f R=%.0f)"),
			bHoldingRight ? TEXT("R") : TEXT("L"), Next, GGunOrientCount - 1, R.Pitch, R.Yaw, R.Roll);
		UE_LOG(LogTemp, Warning, TEXT("%s"), *Msg);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(7788, 8.0f, FColor::Cyan, Msg);
		}
	}
	else if (bIsRingPinching && DistanceCm >= ExitCm)
	{
		bIsRingPinching = false;
	}
}

void UHandTrackingComponent::UpdateMiddlePinchState(const FTransform& ThumbTipWorld, const FTransform& MiddleTipWorld)
{
	const float DistanceCm = FVector::Dist(ThumbTipWorld.GetLocation(), MiddleTipWorld.GetLocation());
	const float EnterCm = MiddlePinchThresholdCm;
	const float ExitCm = MiddlePinchThresholdCm + MiddlePinchReleaseHysteresisCm;

	if (!bIsMiddlePinching && DistanceCm <= EnterCm)
	{
		bIsMiddlePinching = true;
		OnMiddlePinchStarted.Broadcast(GetSide());
	}
	else if (bIsMiddlePinching && DistanceCm >= ExitCm)
	{
		bIsMiddlePinching = false;
		OnMiddlePinchEnded.Broadcast(GetSide());
	}
}

void UHandTrackingComponent::SpawnExplosion(const FVector& WorldLocation)
{
	SpawnTransientSphere(WorldLocation, ExplosionRadiusCm, ExplosionDurationSec);
}

void UHandTrackingComponent::SpawnTransientSphere(const FVector& WorldLocation, float RadiusCm, float DurationSec)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UStaticMesh* MeshToUse = ExplosionMesh ? ExplosionMesh.Get() : DefaultJointMesh.Get();
	if (!MeshToUse)
	{
		return;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AStaticMeshActor* SphereActor = World->SpawnActor<AStaticMeshActor>(WorldLocation, FRotator::ZeroRotator, Params);
	if (!SphereActor)
	{
		return;
	}

	SphereActor->SetMobility(EComponentMobility::Movable);
	if (UStaticMeshComponent* MeshComp = SphereActor->GetStaticMeshComponent())
	{
		MeshComp->SetStaticMesh(MeshToUse);
		MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeshComp->SetCastShadow(false);
		MeshComp->SetReceivesDecals(false);
		// Engine BasicShapes sphere is 100 cm diameter (50 cm radius) at scale 1.
		const float Scale = FMath::Max(RadiusCm, 0.1f) / 50.0f;
		MeshComp->SetWorldScale3D(FVector(Scale));
	}

	// Auto-destroy after the configured lifetime. Weak-lambda guards against
	// a stale timer poking a GC'd actor.
	FTimerHandle DestroyHandle;
	World->GetTimerManager().SetTimer(
		DestroyHandle,
		FTimerDelegate::CreateWeakLambda(SphereActor, [SphereActor]()
		{
			if (IsValid(SphereActor))
			{
				SphereActor->Destroy();
			}
		}),
		FMath::Max(DurationSec, 0.05f),
		false);
}

float UHandTrackingComponent::ComputeFingerExtensionRatio(EHandKeypoint TipKey) const
{
	// Resolve the matching Metacarpal joint for this finger.
	int32 MetacarpalIdx = INDEX_NONE;
	switch (TipKey)
	{
		case EHandKeypoint::ThumbTip:  MetacarpalIdx = static_cast<int32>(EHandKeypoint::ThumbMetacarpal);  break;
		case EHandKeypoint::IndexTip:  MetacarpalIdx = static_cast<int32>(EHandKeypoint::IndexMetacarpal);  break;
		case EHandKeypoint::MiddleTip: MetacarpalIdx = static_cast<int32>(EHandKeypoint::MiddleMetacarpal); break;
		case EHandKeypoint::RingTip:   MetacarpalIdx = static_cast<int32>(EHandKeypoint::RingMetacarpal);   break;
		case EHandKeypoint::LittleTip: MetacarpalIdx = static_cast<int32>(EHandKeypoint::LittleMetacarpal); break;
		default: return 0.0f;
	}

	const int32 TipIdx = static_cast<int32>(TipKey);
	if (!CachedKeypoints.IsValidIndex(TipIdx) || !CachedKeypoints.IsValidIndex(MetacarpalIdx))
	{
		return 0.0f;
	}

	// Sum the joint-to-joint distances along the chain (Metacarpal → … → Tip).
	float ChainLen = 0.0f;
	for (int32 i = MetacarpalIdx; i < TipIdx; ++i)
	{
		ChainLen += FVector::Dist(CachedKeypoints[i].GetLocation(), CachedKeypoints[i + 1].GetLocation());
	}
	if (ChainLen < 1.0f)
	{
		return 0.0f;
	}

	const float StraightDist = FVector::Dist(
		CachedKeypoints[TipIdx].GetLocation(),
		CachedKeypoints[MetacarpalIdx].GetLocation());
	return FMath::Clamp(StraightDist / ChainLen, 0.0f, 1.5f);
}

bool UHandTrackingComponent::IsFingerExtended(EHandKeypoint TipKey) const
{
	const float Ratio = ComputeFingerExtensionRatio(TipKey);
	const int32 FingerIdx = FingerIndexFromTipKey(TipKey);

	if (GSharedCalibrationPhase == EHandCalibrationState::Calibrated
		&& GSharedCalibratedThresholds.IsValidIndex(FingerIdx)
		&& GSharedCalibratedThresholds[FingerIdx] > 0.0f)
	{
		return Ratio >= GSharedCalibratedThresholds[FingerIdx];
	}
	return Ratio >= FingerExtendedRatioThreshold;
}

int32 UHandTrackingComponent::FingerIndexFromTipKey(EHandKeypoint TipKey) const
{
	switch (TipKey)
	{
		case EHandKeypoint::ThumbTip:  return 0;
		case EHandKeypoint::IndexTip:  return 1;
		case EHandKeypoint::MiddleTip: return 2;
		case EHandKeypoint::RingTip:   return 3;
		case EHandKeypoint::LittleTip: return 4;
		default: return INDEX_NONE;
	}
}

void UHandTrackingComponent::StartCalibration()
{
	// Resets the SHARED calibration state — every UHandTrackingComponent
	// instance in the process is now in AwaitingFist.
	ResetSharedCalibration();
	CalibrationState = GSharedCalibrationPhase;
	CalibrationCurrentStableSec = 0.0f;
	for (TArray<float>& Window : CalibrationStabilityWindow)
	{
		Window.Reset();
	}
	for (float& V : CalibratedThresholds) { V = 0.0f; }
}

void UHandTrackingComponent::CaptureCurrentRatios(TArray<float>& OutRatios) const
{
	OutRatios.SetNum(5);
	OutRatios[0] = ComputeFingerExtensionRatio(EHandKeypoint::ThumbTip);
	OutRatios[1] = ComputeFingerExtensionRatio(EHandKeypoint::IndexTip);
	OutRatios[2] = ComputeFingerExtensionRatio(EHandKeypoint::MiddleTip);
	OutRatios[3] = ComputeFingerExtensionRatio(EHandKeypoint::RingTip);
	OutRatios[4] = ComputeFingerExtensionRatio(EHandKeypoint::LittleTip);
}

bool UHandTrackingComponent::AreRatiosStable() const
{
	for (const TArray<float>& Window : CalibrationStabilityWindow)
	{
		if (Window.Num() < CalibrationStabilityWindowSize)
		{
			return false;
		}
		float MinVal = TNumericLimits<float>::Max();
		float MaxVal = TNumericLimits<float>::Lowest();
		for (float V : Window)
		{
			MinVal = FMath::Min(MinVal, V);
			MaxVal = FMath::Max(MaxVal, V);
		}
		if ((MaxVal - MinVal) > CalibrationStabilityMaxRange)
		{
			return false;
		}
	}
	return true;
}

void UHandTrackingComponent::UpdateCalibrationState(float DeltaTime)
{
	// Always mirror the shared phase into the instance UPROPERTY so BP and
	// the label code see the current global state. If another hand
	// advanced the phase between frames, our stability counter is no
	// longer relevant — reset it so this hand starts measuring stability
	// for the NEW pose.
	if (CalibrationState != GSharedCalibrationPhase)
	{
		CalibrationState = GSharedCalibrationPhase;
		CalibrationCurrentStableSec = 0.0f;
		for (TArray<float>& Window : CalibrationStabilityWindow)
		{
			Window.Reset();
		}
	}
	CalibratedThresholds = GSharedCalibratedThresholds;

	if (CalibrationState == EHandCalibrationState::Uncalibrated
		|| CalibrationState == EHandCalibrationState::Calibrated)
	{
		return;
	}

	// Tracking must be live to capture meaningful ratios.
	if (!bIsTracking)
	{
		CalibrationCurrentStableSec = 0.0f;
		return;
	}

	// Push the latest ratio into each finger's rolling window.
	TArray<float> Current;
	CaptureCurrentRatios(Current);
	for (int32 i = 0; i < 5; ++i)
	{
		TArray<float>& Window = CalibrationStabilityWindow[i];
		Window.Add(Current[i]);
		if (Window.Num() > CalibrationStabilityWindowSize)
		{
			Window.RemoveAt(0, 1, EAllowShrinking::No);
		}
	}

	if (!AreRatiosStable())
	{
		CalibrationCurrentStableSec = 0.0f;
		return;
	}
	CalibrationCurrentStableSec += DeltaTime;
	if (CalibrationCurrentStableSec < CalibrationStabilityRequiredSec)
	{
		return;
	}

	// Stable long enough — try to advance the shared phase. The within-pose
	// sanity gates are gone: we trust whatever stable pose the user holds.
	// Validation happens cross-pose in TryAdvanceCalibrationPhase (per
	// finger: Open - Fist must exceed CalibrationFistMaxRatio's distance,
	// otherwise that finger falls back to the global threshold).
	constexpr float MinFingerSeparation = 0.10f;
	const bool bAdvanced = TryAdvanceCalibrationPhase(CalibrationState, Current, MinFingerSeparation, FingerExtendedRatioThreshold);
	if (bAdvanced)
	{
		CalibrationState = GSharedCalibrationPhase;
		CalibratedThresholds = GSharedCalibratedThresholds;
		CalibrationCurrentStableSec = 0.0f;
		for (TArray<float>& Window : CalibrationStabilityWindow)
		{
			Window.Reset();
		}
	}
}

EHandGesture UHandTrackingComponent::ClassifyGesture(bool bThumb, bool bIndex, bool bMiddle, bool bRing, bool bLittle) const
{
	// Legacy binary classifier — kept for completeness but the confidence
	// path is what UpdateGestureState calls. Not used in the main flow.
	const int32 ExtendedCount = (bThumb?1:0) + (bIndex?1:0) + (bMiddle?1:0) + (bRing?1:0) + (bLittle?1:0);

	if (ExtendedCount == 5) return EHandGesture::OpenPalm;
	if (ExtendedCount == 0) return EHandGesture::Fist;
	if (bThumb && !bIndex && !bMiddle && !bRing && !bLittle) return EHandGesture::ThumbsUp;
	if (bIndex && bMiddle && !bRing && !bLittle && !bThumb)  return EHandGesture::Peace;
	if (bThumb && bIndex && !bMiddle && !bRing && !bLittle) return EHandGesture::FingerGuns;
	if (bThumb && bLittle && !bIndex && !bMiddle && !bRing) return EHandGesture::CallMe;
	if (bIndex && bLittle && !bMiddle && !bRing)             return EHandGesture::RockOn;
	return EHandGesture::None;
}

float UHandTrackingComponent::NormalizeFingerRatio(int32 FingerIdx, float Ratio) const
{
	// Normalize the raw chain ratio into 0–1 where 0 = user's fist pose,
	// 1 = user's open pose. With calibration this is a clean per-finger
	// mapping; without it we approximate via the global threshold.
	if (GSharedCalibrationPhase == EHandCalibrationState::Calibrated
		&& GSharedFistRatios.IsValidIndex(FingerIdx)
		&& GSharedOpenRatios.IsValidIndex(FingerIdx))
	{
		const float Fist = GSharedFistRatios[FingerIdx];
		const float Open = GSharedOpenRatios[FingerIdx];
		const float Range = Open - Fist;
		if (Range > 0.05f)
		{
			return FMath::Clamp((Ratio - Fist) / Range, 0.0f, 1.0f);
		}
	}
	// Uncalibrated fallback: a smooth ramp around the global threshold so
	// classification still has a gradient (no calibration → softer reads).
	const float Pivot = FingerExtendedRatioThreshold;
	const float HalfWidth = 0.12f;
	return FMath::Clamp((Ratio - (Pivot - HalfWidth)) / (2.0f * HalfWidth), 0.0f, 1.0f);
}

EHandGesture UHandTrackingComponent::ClassifyGestureByConfidence(const float NormalizedRatios[5], float& OutTopConfidence) const
{
	// Fingerprint encoding: 1 = expected extended, 0 = expected curled,
	// -1 = don't care. Each gesture is one row. RockOn doesn't care about
	// the thumb (some people tuck it, some leave it out).
	struct FFingerprint
	{
		EHandGesture Gesture;
		int8 Thumb, Index, Middle, Ring, Little;
	};
	static const FFingerprint Fingerprints[] = {
		{ EHandGesture::OpenPalm,         1,  1,  1,  1,  1 },
		{ EHandGesture::Fist,             0,  0,  0,  0,  0 },
		{ EHandGesture::ThumbsUp,         1,  0,  0,  0,  0 },
		{ EHandGesture::Peace,            0,  1,  1,  0,  0 },
		{ EHandGesture::FingerGuns,       1,  1,  0,  0,  0 },
		{ EHandGesture::FingerGunsShoot,  0,  1,  0,  0,  0 },
		{ EHandGesture::CallMe,           1,  0,  0,  0,  1 },
		{ EHandGesture::RockOn,          -1,  1,  0,  0,  1 },
		// V27: ThumbOverFist intentionally NOT in this table — it shares the
		// {1,0,0,0,0} curl pattern with ThumbsUp. Orientation disambiguation
		// happens post-classification in UpdateGestureState.
	};

	float TopScore = 0.0f;
	float SecondScore = 0.0f;
	EHandGesture TopGesture = EHandGesture::None;

	for (const FFingerprint& FP : Fingerprints)
	{
		const int8 Expect[5] = { FP.Thumb, FP.Index, FP.Middle, FP.Ring, FP.Little };
		// MIN-based scoring (weakest-link). For ThumbsUp-vs-CallMe the only
		// differentiator is the pinky, so a clearly-extended pinky drops
		// ThumbsUp's score to ~0 while CallMe stays high (or vice versa).
		// Average would let the other 4 matching fingers dominate and
		// leave a tiny margin between similar gestures.
		float MinScore = 1.0f;
		bool bHasContribution = false;
		for (int32 i = 0; i < 5; ++i)
		{
			if (Expect[i] < 0) { continue; }
			const float Want = (Expect[i] == 1) ? NormalizedRatios[i] : (1.0f - NormalizedRatios[i]);
			MinScore = FMath::Min(MinScore, Want);
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

	OutTopConfidence = TopScore;

	// Gate 1: absolute floor — anything below it is "neutral" (no label).
	if (TopScore < MinGestureConfidence)
	{
		return EHandGesture::None;
	}
	// Gate 2: must beat runner-up by the margin. Prevents flicker between
	// nearly-tied gestures (e.g., during a transition).
	if ((TopScore - SecondScore) < MinGestureConfidenceMargin)
	{
		return EHandGesture::None;
	}
	return TopGesture;
}

float UHandTrackingComponent::ComputeThumbUpAlignment() const
{
	// V27: thumb direction = (thumb tip world position) − (thumb metacarpal
	// world position), normalized; then dotted with world up. Returns:
	//   ~+1  →  thumb pointing straight up (genuine ThumbsUp)
	//    0   →  thumb horizontal (typical ThumbOverFist pose, where the
	//           thumb is laid across the curled fingers)
	//   ~-1  →  thumb pointing straight down
	// Reads from the per-frame cached keypoint transforms, so this is
	// effectively free to call once per Tick after UpdateKeypoints has
	// populated the cache.
	const FTransform ThumbTipX  = GetKeypointWorldTransform(EHandKeypoint::ThumbTip);
	const FTransform ThumbMetaX = GetKeypointWorldTransform(EHandKeypoint::ThumbMetacarpal);
	const FVector ThumbDir = (ThumbTipX.GetLocation() - ThumbMetaX.GetLocation()).GetSafeNormal();
	if (ThumbDir.IsNearlyZero())
	{
		return 0.0f;
	}
	return FVector::DotProduct(ThumbDir, FVector::UpVector);
}

void UHandTrackingComponent::UpdateGestureState(float DeltaTime)
{
	// Confidence-based classification: each gesture has an expected
	// per-finger fingerprint (extended/curled/don't-care). We score how
	// well the live normalized ratios match each fingerprint, then pick
	// the winner only if it clears the absolute confidence floor AND
	// beats the runner-up by a margin. The binary IsFingerExtended path
	// stays in service for the [TIMRL] debug strip.
	float NormalizedRatios[5];
	NormalizedRatios[0] = NormalizeFingerRatio(0, ComputeFingerExtensionRatio(EHandKeypoint::ThumbTip));
	NormalizedRatios[1] = NormalizeFingerRatio(1, ComputeFingerExtensionRatio(EHandKeypoint::IndexTip));
	NormalizedRatios[2] = NormalizeFingerRatio(2, ComputeFingerExtensionRatio(EHandKeypoint::MiddleTip));
	NormalizedRatios[3] = NormalizeFingerRatio(3, ComputeFingerExtensionRatio(EHandKeypoint::RingTip));
	NormalizedRatios[4] = NormalizeFingerRatio(4, ComputeFingerExtensionRatio(EHandKeypoint::LittleTip));

	float TopConfidence = 0.0f;
	EHandGesture Detected = ClassifyGestureByConfidence(NormalizedRatios, TopConfidence);

	// V27: orientation-aware disambiguation. The curl-pattern classifier
	// can't distinguish "thumb genuinely pointing up" from "thumb laid flat
	// across the curled fingers" — both have thumb extended + everything
	// else curled. Use the 3D thumb direction vs world up to split them.
	if (Detected == EHandGesture::ThumbsUp)
	{
		const float UpAlignment = ComputeThumbUpAlignment();
		if (UpAlignment < ThumbUpAlignmentThreshold)
		{
			Detected = EHandGesture::ThumbOverFist;
		}
	}

	if (Detected == PendingGesture)
	{
		PendingGestureStableFor += DeltaTime;
	}
	else
	{
		PendingGesture = Detected;
		PendingGestureStableFor = 0.0f;
	}

	// Only commit a transition once the new pose has held long enough.
	if (PendingGestureStableFor >= GestureStabilityWindowSec && PendingGesture != ActiveGesture)
	{
		const EHandGesture OldGesture = ActiveGesture;
		ActiveGesture = PendingGesture;

		if (OldGesture != EHandGesture::None)
		{
			OnGestureEnded.Broadcast(OldGesture, GetSide());
		}
		if (ActiveGesture != EHandGesture::None)
		{
			OnGestureStarted.Broadcast(ActiveGesture, GetSide());
			if (bSpawnShapesOnGestures)
			{
				const FTransform PalmTransform = GetKeypointWorldTransform(EHandKeypoint::Palm);
				SpawnGestureVisual(ActiveGesture, PalmTransform);
			}
		}
	}
}

void UHandTrackingComponent::SpawnGestureVisual(EHandGesture Gesture, const FTransform& PalmTransform)
{
	const FVector PalmLoc   = PalmTransform.GetLocation();
	const FVector PalmFwd   = PalmTransform.GetRotation().GetForwardVector();
	const FVector PalmRight = PalmTransform.GetRotation().GetRightVector();
	const FVector PalmUp    = PalmTransform.GetRotation().GetUpVector();

	switch (Gesture)
	{
		case EHandGesture::OpenPalm:
		{
			// 1 huge shield sphere centered on the palm.
			SpawnTransientSphere(PalmLoc, 20.0f, 0.8f);
			break;
		}
		case EHandGesture::Fist:
		{
			// 8 small frag spheres in a tight cluster around the palm.
			const FVector Offsets[] = {
				{  2.0f,  0.0f,  0.0f }, { -2.0f,  0.0f,  0.0f },
				{  0.0f,  2.0f,  0.0f }, {  0.0f, -2.0f,  0.0f },
				{  0.0f,  0.0f,  2.0f }, {  0.0f,  0.0f, -2.0f },
				{  1.5f,  1.5f,  1.5f }, { -1.5f, -1.5f, -1.5f }
			};
			for (const FVector& Off : Offsets)
			{
				SpawnTransientSphere(PalmLoc + Off, 2.0f, 0.5f);
			}
			break;
		}
		case EHandGesture::Peace:
		{
			// V shape: 2 medium spheres in front of and offset from the palm.
			SpawnTransientSphere(PalmLoc + PalmFwd * 8.0f + PalmRight * -3.0f, 3.5f, 0.6f);
			SpawnTransientSphere(PalmLoc + PalmFwd * 8.0f + PalmRight *  3.0f, 3.5f, 0.6f);
			break;
		}
		case EHandGesture::FingerGuns:
		{
			// 5 spheres firing along palm-forward, decreasing radius (bullet trail).
			for (int32 i = 0; i < 5; ++i)
			{
				const float DistFromPalm = 5.0f + 4.0f * static_cast<float>(i);
				const float Radius = FMath::Max(1.0f, 5.0f - 0.8f * static_cast<float>(i));
				SpawnTransientSphere(PalmLoc + PalmFwd * DistFromPalm, Radius, 0.7f);
			}
			break;
		}
		case EHandGesture::RockOn:
		{
			// 4 spheres: two "horns" up-left and up-right of the palm, plus
			// a stacked centerline (base + neck) for the goat silhouette.
			SpawnTransientSphere(PalmLoc + PalmUp * 7.0f + PalmRight * -4.0f, 2.5f, 0.6f);
			SpawnTransientSphere(PalmLoc + PalmUp * 7.0f + PalmRight *  4.0f, 2.5f, 0.6f);
			SpawnTransientSphere(PalmLoc + PalmUp * 3.0f,                      3.0f, 0.6f);
			SpawnTransientSphere(PalmLoc,                                      3.5f, 0.6f);
			break;
		}
		case EHandGesture::CallMe:
		{
			// 6-sphere ring (halo) in the palm-up/right plane.
			constexpr int32 N = 6;
			const float Radius = 8.0f;
			for (int32 i = 0; i < N; ++i)
			{
				const float Theta = (2.0f * PI * static_cast<float>(i)) / static_cast<float>(N);
				const FVector Offset = PalmRight * (Radius * FMath::Cos(Theta))
				                     + PalmUp    * (Radius * FMath::Sin(Theta));
				SpawnTransientSphere(PalmLoc + Offset, 2.5f, 0.7f);
			}
			break;
		}
		default:
			break;
	}
}

FTransform UHandTrackingComponent::GetKeypointWorldTransform(EHandKeypoint Keypoint) const
{
	const int32 Index = static_cast<int32>(Keypoint);
	if (CachedKeypoints.IsValidIndex(Index))
	{
		return CachedKeypoints[Index];
	}
	return FTransform::Identity;
}

bool UHandTrackingComponent::IsInLevelB() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	// GetMapName() returns the short package name, possibly with a PIE streaming
	// prefix (e.g. "UEDPIE_0_TravelTestMap"). Compare against LevelBPath's leaf name.
	FString MapName = World->GetMapName();
	MapName.RemoveFromStart(World->StreamingLevelsPrefix);
	return MapName.Equals(FPackageName::GetShortName(LevelBPath), ESearchCase::IgnoreCase);
}

void UHandTrackingComponent::TravelToOtherLevel()
{
	// Debounce on process time: ignore travels within the cooldown of the last one.
	// This collapses a held pinch (which would otherwise re-fire in the freshly-loaded
	// level and bounce A<->B) and a simultaneous both-hands pinch into ONE travel.
	const double Now = FPlatformTime::Seconds();
	if (Now - GLastLevelTravelSeconds < GLevelTravelCooldownSeconds)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	GLastLevelTravelSeconds = Now;
	const FString Target = IsInLevelB() ? LevelAPath : LevelBPath;
	UE_LOG(LogTemp, Warning, TEXT("HandTracking: pinky-pinch level travel -> %s"), *Target);
	UGameplayStatics::OpenLevel(this, FName(*Target));
}

void UHandTrackingComponent::DrawLevelLabel()
{
	// Rendered as an OPAQUE TextRenderComponent (scene geometry) rather than
	// DrawDebugString — debug-canvas text composites poorly over passthrough/MR,
	// reading as ghosted/translucent. Opaque scene text composites cleanly.
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC || !PC->PlayerCameraManager)
	{
		if (LevelLabelText) { LevelLabelText->SetVisibility(false); }
		return;
	}
	const FVector CamLoc = PC->PlayerCameraManager->GetCameraLocation();
	const FRotator CamRot = PC->PlayerCameraManager->GetCameraRotation();
	const FVector Anchor = CamLoc + CamRot.Vector() * 200.0f + FVector(0.0f, 0.0f, 25.0f);

	const bool bLevelB = IsInLevelB();
	const FString Text = bLevelB
		? TEXT("LEVEL B\npinky-pinch -> back to A")
		: TEXT("LEVEL A\npinky-pinch -> travel to B");
	// Level B reads cyan, Level A green — an instant, unmistakable colour flip on travel.
	const FColor Color = bLevelB ? FColor(80, 200, 255) : FColor(120, 255, 120);

	if (!LevelLabelText)
	{
		LevelLabelText = NewObject<UTextRenderComponent>(GetOwner());
		LevelLabelText->SetupAttachment(this);
		LevelLabelText->RegisterComponent();
		LevelLabelText->SetHorizontalAlignment(EHTA_Center);
		LevelLabelText->SetVerticalAlignment(EVRTA_TextCenter);
		LevelLabelText->SetWorldSize(9.0f);
		LevelLabelText->SetCastShadow(false);
		// Opaque text material so it composites correctly over passthrough.
		if (UMaterialInterface* OpaqueText = Cast<UMaterialInterface>(StaticLoadObject(
				UMaterialInterface::StaticClass(), nullptr,
				TEXT("/Engine/EngineMaterials/DefaultTextMaterialOpaque.DefaultTextMaterialOpaque"))))
		{
			LevelLabelText->SetTextMaterial(OpaqueText);
		}
	}
	LevelLabelText->SetVisibility(true);
	LevelLabelText->SetText(FText::FromString(Text));
	LevelLabelText->SetTextRenderColor(Color);
	LevelLabelText->SetWorldLocation(Anchor);
	// Billboard: +X (text facing) points back at the camera so it's readable.
	LevelLabelText->SetWorldRotation((CamLoc - Anchor).Rotation());
}

void UHandTrackingComponent::EnsureLabelInitialized()
{
	// No-op — DrawDebugString is per-frame and needs no persistent component.
	// Kept as a member to preserve the .h signature in case we switch back
	// to a component-based renderer.
}

FString UHandTrackingComponent::BuildLabelText() const
{
	// While calibrating, hijack the label to walk the user through poses.
	if (CalibrationState != EHandCalibrationState::Calibrated)
	{
		// Build version marker bumped each pass to confirm new install
		// actually loaded on device — visionOS sometimes serves a cached
		// bundle, and "looks the same" can mean "stale cache".
		FString CalHeadline = TEXT("[v29] ");
		switch (CalibrationState)
		{
			case EHandCalibrationState::Uncalibrated:
				CalHeadline += TEXT("CALIBRATION PENDING");
				break;
			case EHandCalibrationState::AwaitingFist:
				CalHeadline += TEXT("CALIBRATE: MAKE A TIGHT FIST");
				break;
			case EHandCalibrationState::AwaitingOpen:
				CalHeadline += TEXT("CALIBRATE: OPEN HAND, FINGERS SPREAD");
				break;
			default: break;
		}
		const float HoldProgress = FMath::Clamp(
			CalibrationCurrentStableSec / FMath::Max(CalibrationStabilityRequiredSec, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
		const int32 TP = FMath::FloorToInt(ComputeFingerExtensionRatio(EHandKeypoint::ThumbTip)  * 100.0f);
		const int32 IP = FMath::FloorToInt(ComputeFingerExtensionRatio(EHandKeypoint::IndexTip)  * 100.0f);
		const int32 MP = FMath::FloorToInt(ComputeFingerExtensionRatio(EHandKeypoint::MiddleTip) * 100.0f);
		const int32 RP = FMath::FloorToInt(ComputeFingerExtensionRatio(EHandKeypoint::RingTip)   * 100.0f);
		const int32 LP = FMath::FloorToInt(ComputeFingerExtensionRatio(EHandKeypoint::LittleTip) * 100.0f);
		return FString::Printf(TEXT("%s\nhold steady %d%%\nT%d I%d M%d R%d L%d"),
			*CalHeadline,
			FMath::FloorToInt(HoldProgress * 100.0f),
			TP, IP, MP, RP, LP);
	}

	// Priority: pinches read first (they're explicit and override curl
	// classification), then curl gesture. RockOn is disambiguated from
	// MiddlePinch because the pinch check beats the curl-pattern check.
	FString Headline;
	if (bIsPinching)
	{
		Headline = TEXT("INDEX PINCH");
	}
	else if (bIsMiddlePinching)
	{
		Headline = TEXT("MIDDLE PINCH");
	}
	else if (bIsPinkyPinching)
	{
		Headline = TEXT("PINKY PINCH");
	}
	else
	{
		switch (ActiveGesture)
		{
			case EHandGesture::OpenPalm:        Headline = TEXT("OPEN PALM");        break;
			case EHandGesture::Fist:            Headline = TEXT("FIST");             break;
			case EHandGesture::ThumbsUp:        Headline = TEXT("THUMBS UP");        break;
			case EHandGesture::Peace:           Headline = TEXT("PEACE");            break;
			case EHandGesture::FingerGuns:      Headline = TEXT("FINGER GUNS");      break;
			case EHandGesture::FingerGunsShoot: Headline = TEXT("FINGER GUNS SHOT"); break;
			case EHandGesture::RockOn:          Headline = TEXT("ROCK ON");          break;
			case EHandGesture::CallMe:          Headline = TEXT("CALL ME");          break;
			case EHandGesture::ThumbOverFist:   Headline = TEXT("THUMB OVER FIST");  break;
			default: break;
		}
	}

	if (Headline.IsEmpty() && !bDebugShowFingerStates)
	{
		return FString();
	}

	if (Headline.IsEmpty())
	{
		Headline = TEXT("...");
	}

	if (!bDebugShowFingerStates)
	{
		return Headline;
	}

	// Per-finger extension-ratio strip. Each number is the live ratio * 100.
	// If calibration has run, the bracketed number after each letter is that
	// finger's personalized extended threshold; otherwise the trailing
	// (>=N ext) shows the global fallback.
	const float Ratios[5] = {
		ComputeFingerExtensionRatio(EHandKeypoint::ThumbTip),
		ComputeFingerExtensionRatio(EHandKeypoint::IndexTip),
		ComputeFingerExtensionRatio(EHandKeypoint::MiddleTip),
		ComputeFingerExtensionRatio(EHandKeypoint::RingTip),
		ComputeFingerExtensionRatio(EHandKeypoint::LittleTip),
	};
	const TCHAR Letters[5] = { TEXT('T'), TEXT('I'), TEXT('M'), TEXT('R'), TEXT('L') };

	if (CalibratedThresholds.Num() == 5 && CalibratedThresholds[1] > 0.0f)
	{
		// Calibrated: show each finger's live ratio with its personalized threshold.
		FString Strip;
		for (int32 i = 0; i < 5; ++i)
		{
			Strip += FString::Printf(TEXT("%c%d/%d "),
				Letters[i],
				FMath::FloorToInt(Ratios[i] * 100.0f),
				FMath::FloorToInt(CalibratedThresholds[i] * 100.0f));
		}
		return FString::Printf(TEXT("%s\n%s"), *Headline, *Strip.TrimEnd());
	}

	const int32 ThreshP = FMath::FloorToInt(FingerExtendedRatioThreshold * 100.0f);
	return FString::Printf(TEXT("%s\nT%d I%d M%d R%d L%d  (>=%d ext)"),
		*Headline,
		FMath::FloorToInt(Ratios[0] * 100.0f),
		FMath::FloorToInt(Ratios[1] * 100.0f),
		FMath::FloorToInt(Ratios[2] * 100.0f),
		FMath::FloorToInt(Ratios[3] * 100.0f),
		FMath::FloorToInt(Ratios[4] * 100.0f),
		ThreshP);
}

void UHandTrackingComponent::UpdateGestureLabel()
{
#if ENABLE_DRAW_DEBUG
	if (!bIsTracking)
	{
		return;
	}

	const FString Text = BuildLabelText();
	if (Text.IsEmpty())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// DrawDebugString draws screen-space text at a world position — auto
	// billboard, auto camera-face, and routes through the debug-primitive
	// pipeline which renders reliably on the visionOS forward path where
	// UTextRenderComponent's static-glyph material did not.
	const FTransform PalmTransform = GetKeypointWorldTransform(EHandKeypoint::Palm);
	const FVector LabelLoc = PalmTransform.GetLocation() + FVector(0.0f, 0.0f, LabelHeightAbovePalmCm);
	const float FontScale = FMath::Max(0.5f, LabelWorldSizeCm / 3.0f);
	DrawDebugString(World, LabelLoc, Text, /*BaseActor=*/nullptr, LabelColor, /*Duration=*/0.0f, /*bDrawShadow=*/true, FontScale);
#endif
}
