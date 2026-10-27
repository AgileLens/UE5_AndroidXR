// Copyright (c) 2026 Alex Coulombe. MIT License.

using UnrealBuildTool;

// PinchworkCore — the engine-agnostic recognition + transform math, as a thin
// UE module so the runtime Pinchwork module (and any project) can depend on it.
// The algorithm sources include NO Unreal headers; only PinchworkCoreModule.cpp
// pulls in ModuleManager for IMPLEMENT_MODULE. That same set of algorithm
// sources is compiled standalone by Tests/run_tests.sh with a stock clang++.
public class PinchworkCore : ModuleRules
{
	public PinchworkCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Core only — for IMPLEMENT_MODULE. The math itself needs nothing.
		PublicDependencyModuleNames.AddRange(new string[] { "Core" });
	}
}
