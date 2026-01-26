#include "AndroidXROpenXRLoader.h"
#include "Interfaces/IPluginManager.h"

#if PLATFORM_ANDROID
#include <android_native_app_glue.h>
#endif

#define LOCTEXT_NAMESPACE "FAndroidXROpenXRLoaderModule"

DEFINE_LOG_CATEGORY(LogAndroidXROpenXRLoader);

FAndroidXROpenXRLoaderModule::FAndroidXROpenXRLoaderModule() : LoaderLibraryHandle(nullptr)
{
}

FAndroidXROpenXRLoaderModule::~FAndroidXROpenXRLoaderModule()
{
}

void FAndroidXROpenXRLoaderModule::StartupModule()
{
	RegisterOpenXRExtensionModularFeature();

	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AndroidXROpenXR"));
	if (Plugin.IsValid())
	{
		UE_LOG(LogAndroidXROpenXRLoader, Log, TEXT("Android XR OpenXR Plugin Version: %s"), *Plugin->GetDescriptor().VersionName);
	}
}

void FAndroidXROpenXRLoaderModule::ShutdownModule()
{
	if (LoaderLibraryHandle)
	{
		FPlatformProcess::FreeDllHandle(LoaderLibraryHandle);
		LoaderLibraryHandle = nullptr;
	}
	
	UnregisterOpenXRExtensionModularFeature();
}

FString FAndroidXROpenXRLoaderModule::GetDisplayName()
{
	return FString(TEXT("Android XR OpenXR"));
}

bool FAndroidXROpenXRLoaderModule::GetCustomLoader(PFN_xrGetInstanceProcAddr* OutGetProcAddr)
{
#if PLATFORM_ANDROID
	LoaderLibraryHandle = FPlatformProcess::GetDllHandle(TEXT("libandroidxr_openxr_loader.so"));
	if (LoaderLibraryHandle == nullptr)
	{
		UE_LOG(LogAndroidXROpenXRLoader, Error, TEXT("Unable to load libandroidxr_openxr_loader.so!"));
		return false;
	}

	UE_LOG(LogAndroidXROpenXRLoader, Log, TEXT("Loaded libandroidxr_openxr_loader.so!"));

	PFN_xrGetInstanceProcAddr xrGetInstanceProcAddrPtr = (PFN_xrGetInstanceProcAddr)FPlatformProcess::GetDllExport(LoaderLibraryHandle, TEXT("xrGetInstanceProcAddr"));
	if (xrGetInstanceProcAddrPtr == nullptr)
	{
		UE_LOG(LogAndroidXROpenXRLoader, Error, TEXT("OpenXR xrGetInstanceProcAddr retrieval failed!"));
		return false;
	}
	
	*OutGetProcAddr = xrGetInstanceProcAddrPtr;

	UE_LOG(LogAndroidXROpenXRLoader, Log, TEXT("OpenXR xrGetInstanceProcAddr retrieved!"))

	extern struct android_app* GNativeAndroidApp;
	PFN_xrInitializeLoaderKHR xrInitializeLoaderKHR;

	if (!XR_CHECK(xrGetInstanceProcAddrPtr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction*)&xrInitializeLoaderKHR), TEXT("Android XR OpenXR xrInitializeLoaderKHR retieval failed!")))
	{
		return false;
	}

	UE_LOG(LogAndroidXROpenXRLoader, Log, TEXT("OpenXR xrInitializeLoaderKHR retrieved!"));

	XrLoaderInitInfoAndroidKHR LoaderInitializeInfoAndroid;
	LoaderInitializeInfoAndroid.type = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR;
	LoaderInitializeInfoAndroid.next = nullptr;
	LoaderInitializeInfoAndroid.applicationVM = GNativeAndroidApp->activity->vm;
	LoaderInitializeInfoAndroid.applicationContext = GNativeAndroidApp->activity->clazz;
	
	if (!XR_CHECK(xrInitializeLoaderKHR((XrLoaderInitInfoBaseHeaderKHR*)&LoaderInitializeInfoAndroid), TEXT("Android XR OpenXR Loader initialization failed!")))
	{
		return false;
	}
	else
	{
		UE_LOG(LogAndroidXROpenXRLoader, Log, TEXT("OpenXR xrInitializeLoaderKHR successful!"));
		UE_LOG(LogAndroidXROpenXRLoader, Log, TEXT("Android XR OpenXR Loader Initialized!"));
	}

	return true;
#else
	return false;
#endif
}

bool FAndroidXROpenXRLoaderModule::GetRequiredExtensions(TArray<const ANSICHAR*>& OutExtensions)
{
	OutExtensions.Add("XR_EXT_hand_tracking");
	OutExtensions.Add("XR_EXT_hand_interaction");

	//OutExtensions.Add("XR_ANDROID_avatar_eyes");
	//OutExtensions.Add("XR_EXT_eye_gaze_interaction");

	//OutExtensions.Add("XR_ANDROID_face_tracking");
	
	return true;
}

#undef LOCTEXT_NAMESPACE
    
IMPLEMENT_MODULE(FAndroidXROpenXRLoaderModule, AndroidXROpenXRLoader)