#pragma once

#include "HAL/Platform.h"
#if PLATFORM_ANDROID
#define XR_USE_PLATFORM_ANDROID 1
#endif

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include "OpenXRCore.h"

#define XR_CHECK(x, y) [] (XrResult Result, const TCHAR* Message) \
	{ \
		return ensureMsgf(XR_SUCCEEDED(Result), TEXT("%s (Error %d: %s)"), Message, (int)Result, OpenXRResultToString(Result)); \
	} (x, y)