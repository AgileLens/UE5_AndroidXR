using System.IO;
using UnrealBuildTool;

public class AndroidXROpenXRLoader : ModuleRules
{
    public AndroidXROpenXRLoader(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "Engine",
                "OpenXRHMD",
                "Projects"
            }
        );
        
        AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenXR");
        
        if (Target.Platform == UnrealTargetPlatform.Android)
        {
            RuntimeDependencies.Add(Path.Combine(ModuleDirectory, "Libs/Android/arm64-v8a/libandroidxr_openxr_loader.so"));
            
            string PluginPath = Utils.MakePathRelativeTo(ModuleDirectory, Target.RelativeEnginePath);
            AdditionalPropertiesForReceipt.Add("AndroidPlugin", Path.Combine(PluginPath, "AndroidXROpenXR_UPL.xml"));
        }
    }
}