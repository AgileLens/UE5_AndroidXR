// Copyright (c) 2026 Alex Coulombe. MIT License.

using UnrealBuildTool;

public class Pinchwork : ModuleRules
{
	public Pinchwork(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// PinchworkCore: the engine-agnostic recognition + two-hand + sequence
		// math (also unit-tested standalone via Tests/run_tests.sh). Public
		// because Pinchwork's public component headers expose core types.
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "PinchworkCore" });

		// XRBase: IHandTracker modular feature + HMD keypoint types.
		// EnhancedInput: gesture -> input-action injection.
		// HeadMountedDisplay: EHandKeypoint / HeadMountedDisplayTypes.
		PrivateDependencyModuleNames.AddRange(new string[] { "XRBase", "EnhancedInput", "HeadMountedDisplay" });
	}
}
