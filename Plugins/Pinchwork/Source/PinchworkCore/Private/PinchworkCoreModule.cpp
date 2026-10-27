// Copyright (c) 2026 Alex Coulombe. MIT License.
//
// The ONLY file in PinchworkCore that includes an Unreal header. It exists so
// UBT can register the module; the test harness excludes it (it compiles the
// algorithm sources directly with clang++, no UE). Keeping IMPLEMENT_MODULE
// isolated here is what keeps every other core source pure C++ — and lets
// PinchworkCore depend on nothing but "Core" (the EKeypoint↔EHandKeypoint
// lockstep static_assert lives in the Pinchwork module's PinchworkUE.h, which
// already links HeadMountedDisplay).

#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, PinchworkCore);
