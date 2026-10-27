// Copyright (c) 2026 Alex Coulombe. MIT License.

#include "Modules/ModuleManager.h"

// Pinchwork is a plain runtime module — the gameplay lives in the components
// (UHandTrackingComponent, UHandSkeletalDriverComponent), so the default module
// implementation is all that's needed here.
IMPLEMENT_MODULE(FDefaultModuleImpl, Pinchwork);
