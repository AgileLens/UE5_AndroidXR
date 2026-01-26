#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "IOpenXRExtensionPlugin.h"
#include "AndroidXROpenXRCore.h"

DECLARE_LOG_CATEGORY_EXTERN(LogAndroidXROpenXRLoader, Log, All);

class FAndroidXROpenXRLoaderModule : public IModuleInterface, public IOpenXRExtensionPlugin
{
public:
    FAndroidXROpenXRLoaderModule();
    virtual ~FAndroidXROpenXRLoaderModule() override;
	
    /** IModuleInterface Implementation */
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

    /** IOpenXRExtensionPlugin Implementation */
    virtual FString GetDisplayName() override;
    virtual bool GetCustomLoader(PFN_xrGetInstanceProcAddr* OutGetProcAddr) override;
    virtual bool GetRequiredExtensions(TArray<const ANSICHAR*>& OutExtensions) override;

private:
    void* LoaderLibraryHandle;
};