// Phase 2: drive UE Manny-XR skeletal-mesh hand bones from OpenXR keypoints.
//
// Pairs with UHandTrackingComponent (which spawns sphere markers + detects
// gestures). This component is the "rigged hand visualization" — it takes
// the same IHandTracker keypoint stream and pushes per-bone world transforms
// into an attached UPoseableMeshComponent so the user sees an actual hand
// mesh that finger-curls live.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "HeadMountedDisplayTypes.h"
#include "HandSkeletalDriverComponent.generated.h"

class UPoseableMeshComponent;
class USkeletalMesh;
class UMaterialInterface;
class IHandTracker;
enum class EControllerHand : uint8;

// Strategies for converting an OpenXR keypoint pose into a Manny-XR bone
// world rotation. Used for a side-by-side test rig: spawn N ghost meshes,
// drive each one with a different strategy, see which looks right.
UENUM(BlueprintType)
enum class EBoneRotationStrategy : uint8
{
	// V22 winners: bone-along is +Z, direction is -aim (joint→parent), and
	// twist is locked with X = wristForward (MakeFromZX(-aim, wristFwd)).
	// All v23+ finger driving is hard-locked to this formula — the enum
	// remains only because other code reads ActiveStrategy as an enum.
	AimZX_Rev_WristFwd   UMETA(DisplayName = "Locked: MakeFromZX(-aim, wristFwd)")
};

// V23 cycle test: with finger formula locked, the open question is the
// wrist bone's own orientation. User reports the mesh wrist is "twisted
// onto itself" by ~90° — its cylindrical cuff faces the camera instead
// of pointing back toward the elbow. We can't compute an elbow-direction
// from OpenXR (the spec only goes wrist→fingertips), so we cycle six
// candidate 90° corrections applied to the OpenXR wrist rotation and
// let the user pick visually. Each ghost uses the same (correct) finger
// formula above, so the only visible difference is wrist orientation.
UENUM(BlueprintType)
enum class EWristAdjustTest : uint8
{
	None       UMETA(DisplayName = "A: no adjust (raw OpenXR wrist)"),
	PitchPlus  UMETA(DisplayName = "B: Pitch +90 (around right axis)"),
	PitchMinus UMETA(DisplayName = "C: Pitch -90"),
	YawPlus    UMETA(DisplayName = "D: Yaw +90 (around up axis)"),
	YawMinus   UMETA(DisplayName = "E: Yaw -90"),
	RollPlus   UMETA(DisplayName = "F: Roll +90 (around forward / bone axis)")
};

// V28: with finger formula and wrist correction locked, the open issue is
// the thumb's visual proportions and the thenar-webbing gap (the mesh's
// webbing is geometrically thin and doesn't fill space when the user's
// thumb is widely abducted). These six strategies cycle different thumb
// driving behaviors to find the best procedural compromise. The "real"
// fix is a thenar_extend morph target authored in Blender; this rig is
// to land an acceptable visual without that.
UENUM(BlueprintType)
enum class EThumbStrategyTest : uint8
{
	// A: current v27 — skip thumb_01 only, drive thumb_02 + thumb_03 with full live position
	SkipThumb01        UMETA(DisplayName = "A: skip thumb_01 (v27 baseline)"),
	// B: pre-v27 — drive ALL thumb bones from live OpenXR positions
	DriveAllNormal     UMETA(DisplayName = "B: drive all thumb bones live"),
	// C: drive normally but apply uniform 0.8x scale to every thumb bone
	ScaleDown          UMETA(DisplayName = "C: scale thumb bones 0.8x"),
	// D: drive normally but pull every thumb keypoint 1.5 cm toward the wrist (reduces abduction)
	PullTowardWrist15  UMETA(DisplayName = "D: pull thumb 1.5cm toward wrist"),
	// E: same as D but 3 cm (more aggressive)
	PullTowardWrist30  UMETA(DisplayName = "E: pull thumb 3cm toward wrist"),
	// F: skip every thumb bone — thumb stays at bind pose, no articulation but stable webbing
	SkipAllThumb       UMETA(DisplayName = "F: skip all thumb bones (bind pose)")
};

// Which variable the 6-ghost test rig varies. The apparatus must outlive the
// answer: v23 answered the wrist question for Apple then deleted the ability to
// ask it, which is exactly why re-deriving it for PICO needed a code change.
UENUM(BlueprintType)
enum class EMultiStrategyAxis : uint8
{
	Wrist       UMETA(DisplayName = "Wrist bone correction (6x 90 deg)"),
	FingerTwist UMETA(DisplayName = "Finger twist reference (6x 90 deg)"),
	Thumb       UMETA(DisplayName = "Thumb strategy (A-F)")
};

UCLASS(ClassGroup = (VR), meta = (BlueprintSpawnableComponent), DisplayName = "Hand Skeletal Driver")
class PINCHWORK_API UHandSkeletalDriverComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UHandSkeletalDriverComponent();

	// Mirror of UHandTrackingComponent's handedness toggle so the same
	// editor automation flow that flips bIsRight works here.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Skeletal Driver", meta = (DisplayName = "Is Right Hand"))
	bool bIsRight = false;

	// Override the default Manny-XR mesh. Leave null to use the engine /
	// project default (SKM_MannyXR_left / SKM_MannyXR_right).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Skeletal Driver")
	TObjectPtr<USkeletalMesh> HandMeshOverride = nullptr;

	// When on, override every material slot of the Poseable mesh with
	// DebugMaterialOverride. The default DebugMaterialOverride
	// (engine WireframeMaterial) is intrinsically two-sided, so the back
	// of the hand becomes visible even if the mesh is rendered
	// inside-out. V25: defaults off now that wrist rotation is correct —
	// flip true to debug UV winding or mesh inversion issues.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Skeletal Driver|Debug")
	bool bUseDebugMaterial = false;

	// The material to apply when bUseDebugMaterial is true. Defaults to
	// engine WireframeMaterial (loaded in the constructor); replace with
	// a custom unlit two-sided material to get a solid look. Any
	// two-sided material works — the point is that bIsTwoSided is true
	// so winding-flipped or back-facing geometry still renders.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Skeletal Driver|Debug")
	TObjectPtr<UMaterialInterface> DebugMaterialOverride = nullptr;

	// Hide the mesh entirely when tracking is invalid. Recommended on —
	// otherwise the hand freezes at its last pose, which reads as broken.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Skeletal Driver")
	bool bHideWhenUntracked = true;

	// In the "wooded" level (Stone Courtyard / TravelTestMap), skin the hand mesh
	// in walnut wood instead of its default material. Applied at BeginPlay.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Skeletal Driver")
	bool bSwapHandMaterialInWoodedLevel = true;

	// Material used for the wooded-level hand skin. Defaults to StarterContent
	// M_Wood_Walnut (loaded in the constructor).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Skeletal Driver")
	TObjectPtr<UMaterialInterface> WoodedLevelHandMaterial = nullptr;

	// Short map name that counts as the "wooded" level for the walnut hand skin.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Skeletal Driver")
	FString WoodedLevelName = TEXT("TravelTestMap");

	// Manny-XR's wrist bone is rotated 90° around its bone-along axis relative
	// to the OpenXR wrist convention (verified empirically in v23 by cycling
	// six 90° candidates; Roll +90 was the unambiguous winner — the wrist's
	// cylindrical cuff then pointed back along the forearm instead of facing
	// the camera). FRotator(Pitch, Yaw, Roll) maps Roll to rotation around X
	// (the forward / bone-along axis). Different skeletal mesh? Reset this to
	// zero and run the v23 test rig (bMultiStrategyTest=true) to find the
	// right correction for that rig.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Skeletal Driver|Tuning")
	FRotator WristAdjustRotation = FRotator(0.0f, 0.0f, 90.0f);

	// DECOUPLED 2026-08-17 (PICO). WristAdjustRotation was doing DOUBLE DUTY: it set the
	// wrist BONE orientation *and* supplied WristForward, the twist-lock reference for every
	// finger bone (MakeFromZX(-aim, wristForward)). On Apple's runtime both wanted Roll+90,
	// so the overload was invisible and got locked in as one constant. On PICO they want
	// DIFFERENT corrections — Alex's cycler read: "B has the closest to a correct wrist,
	// D has the closest correct fingers" — which is unachievable with a single parameter.
	// This is the twist reference only; it does NOT move the wrist bone.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Skeletal Driver|Tuning")
	FRotator FingerTwistAdjustRotation = FRotator(0.0f, 0.0f, 0.0f);

	// V26: stretch DISTAL bones only along their bone-along (Z) axis so the
	// fingertip mesh reaches where the user's actual fingertip is. Earlier
	// v25 scaled every bone, but that pulled apart thumb-thenar webbing
	// vertices and made the thumb look oversized — vertices between
	// non-distal bones already interpolate correctly because we WORLD-
	// position every bone. Only the mesh PAST the distal bone (skinned
	// 100% to it) needs explicit scaling.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Skeletal Driver|Tuning")
	bool bScaleBonesToHandSize = true;

	// Empirical multiplier on the distal bone's bind length, used as a proxy
	// for "how far the mesh actually extends past the distal bone tip at
	// unit scale". The mesh tapers and the visible fingertip ends some
	// fraction of the bone's length past the bone tip on Manny-XR. Lower
	// values produce longer rendered fingertips (smaller proxy → bigger
	// scale ratio). v25 default 1.0 → ~5cm short pinky reach. v26 0.5 →
	// ~1cm short. v27 default 0.3 to close that residual gap empirically.
	// Dial up toward 0.5+ if fingertips overshoot.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Skeletal Driver|Tuning", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float DistalTipMeshFactor = 0.3f;

	// V27: when true, do NOT world-position the five metacarpal bones
	// (ThumbMetacarpal, Index/Middle/Ring/LittleMetacarpal). They inherit
	// from the wrist via the skeleton hierarchy at bind-pose relative
	// transform, keeping palm geometry rigid. Driving the thumb metacarpal
	// from the live OpenXR keypoint pulled thenar-webbing vertices ~5 cm
	// away from the palm and made the thumb look oversized. Finger
	// articulation still drives correctly from the proximal joints onward.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Skeletal Driver|Tuning")
	bool bSkipMetacarpalBones = true;

	// At BeginPlay, search the owning actor for sibling components named
	// "HandLeft" / "HandRight" (the PSVR2 controller-attached mannequin
	// BPs) and hide them. We want a single mesh per side while in
	// hand-tracking mode, not the static controller hands too.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Skeletal Driver")
	bool bHidePsvr2ControllerHandsAtStart = true;

	// In Tick, keep re-hiding the PSVR2 mannequins. Some BP component
	// logic on B_MannequinsXR_C may re-show them every frame.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Skeletal Driver")
	bool bHidePsvr2ControllerHandsEveryTick = true;

	// Active strategy when not in multi-strategy test mode.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Skeletal Driver|Tuning")
	EBoneRotationStrategy ActiveStrategy = EBoneRotationStrategy::AimZX_Rev_WristFwd;

	// Test rig: cycles 6 candidates of ONE axis, selected by MultiStrategyAxis.
	//
	// CORRECTED 2026-08-17: this comment previously claimed the rig cycled wrist
	// corrections. It did in v23; V28 silently repurposed the ghost loop to cycle
	// THUMB strategies and left GetWristAdjustForGhost/ComputeWristAdjustQuat as
	// dead code with zero call sites. Anyone following the old comment would have
	// been shown six variants differing in the wrong variable — a cycler that
	// cycles something other than what you think. The axis is now explicit.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Skeletal Driver|Test")
	bool bMultiStrategyTest = true;   // ON for the PICO wrist-calibration build

	// Which axis the 6 ghosts vary. Wrist FIRST on any new runtime: the wrist
	// basis is upstream of every child bone, so a wrong wrist corrupts fingers
	// and thumb and makes those axes unreadable.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Skeletal Driver|Test")
	EMultiStrategyAxis MultiStrategyAxis = EMultiStrategyAxis::FingerTwist;

	// V29: locked thumb strategy used in single-mesh mode. B (DriveAllNormal)
	// won the v28 thumb test rig on spatial accuracy — user's real thumbs
	// touching produces rendered thumbs touching. The visible "thick thumb"
	// is now addressed via the thenar_extend morph target (see Blender
	// authoring flow in HANDOFF_HAND_TRACKING.md) driven by abduction angle.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Skeletal Driver|Tuning")
	EThumbStrategyTest ActiveThumbStrategy = EThumbStrategyTest::DriveAllNormal;

	// Centimeters above the wrist to anchor the first test ghost.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Skeletal Driver|Test", meta = (ClampMin = "5.0", ClampMax = "100.0"))
	float TestRigHeightAboveWristCm = 30.0f;

	// Horizontal spacing between adjacent test ghosts (unused in cycle mode).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Skeletal Driver|Test", meta = (ClampMin = "5.0", ClampMax = "100.0"))
	float TestRigHorizontalSpacingCm = 18.0f;

	// How long each strategy stays active before the cycle advances to
	// the next. Both hand components share a world-time-based clock so
	// both sides show the same strategy at the same moment.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Skeletal Driver|Test", meta = (ClampMin = "0.5", ClampMax = "20.0"))
	float MultiStrategyCycleSec = 2.5f;

	// Hide/show this hand's mesh (called by the hand-tracking component so the hand
	// disappears while it's grabbing an object). Sticks across ticks.
	void SetHandMeshHidden(bool bHidden);

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	// True while this hand is grabbing (set via SetHandMeshHidden) — the mesh stays
	// hidden and the pose isn't driven until release.
	bool bForceHidden = false;

	// In normal mode this array holds one Poseable. In multi-strategy test
	// mode it holds one per strategy (6) — driven independently per Tick.
	UPROPERTY()
	TArray<TObjectPtr<UPoseableMeshComponent>> Poseables;

public:
	// Host-assignable tracked-hand meshes. Previously private UPROPERTY()s populated ONLY by a
	// hardcoded ConstructorHelpers lookup at Epic's VR-Template path — so in any project whose
	// content sits elsewhere these silently stayed null, the driver had nothing to pose, and the
	// user saw controller-attached hands instead of conforming tracked hands. Joint data was
	// flowing the whole time; it was being written to nothing.
	// Exposed so the HOST project assigns its own meshes; the constructor's finders remain only
	// as a convenience default. A plugin intended for sale must not depend on the consumer
	// mirroring Epic's folder layout.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hand Tracking|Meshes")
	TObjectPtr<USkeletalMesh> DefaultLeftMesh = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hand Tracking|Meshes")
	TObjectPtr<USkeletalMesh> DefaultRightMesh = nullptr;

private:

	// Silent multi-layout default resolution; see the .cpp for why this is not in the constructor.
	void ResolveDefaultMeshes();
	void EnsurePoseablesInitialized();
	void HidePsvr2ControllerHands();
	bool IsWoodedLevel() const;
	FName ResolveBoneName(EHandKeypoint Keypoint) const;
	EHandKeypoint NextKeypointInChain(EHandKeypoint Keypoint) const;

	// V25: cached bind-pose parent-relative offset magnitudes (= bind segment
	// lengths) per keypoint. Populated once after the Poseable's mesh is set,
	// before any SetBoneTransformByName overrides corrupt the local pose.
	TMap<EHandKeypoint, float> BindKeypointLengths;
	void CacheBindLengths(class UPoseableMeshComponent* P);

	// Strategy-aware bone driving. Reads keypoint data and applies the
	// requested per-bone rotation formula to all 21 driven bones on this
	// Poseable.
	// TwistOverride is the finger twist-reference correction for THIS call.
	// Passed explicitly rather than read from FingerTwistAdjustRotation so the
	// per-ghost test rig cannot mutate a UPROPERTY during Tick (which persisted
	// whatever the cycle last landed on and clobbered the user's configured
	// value). Callers outside the rig pass FingerTwistAdjustRotation.
	void DrivePoseableWithStrategy(
		UPoseableMeshComponent* Pose,
		EBoneRotationStrategy Strategy,
		EThumbStrategyTest ThumbStrategy,
		const FTransform& WristXform,
		IHandTracker* Tracker,
		EControllerHand Hand,
		const FQuat& TwistOverride) const;

	FRotator ComputeBoneRotation(
		EBoneRotationStrategy Strategy,
		const FVector& AimDirection,
		const FTransform& WristXform,
		const FQuat& OXRJointRotation,
		const FQuat& TwistOverride) const;

	static EBoneRotationStrategy GetStrategyForGhost(int32 GhostIndex);
	static const TCHAR* GetStrategyLetter(EBoneRotationStrategy Strategy);
	static int32 NumTestStrategies();
};
