// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class IntelOIDN : ModuleRules
{
    public IntelOIDN(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

        if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows) && Target.WindowsPlatform.Architecture == WindowsArchitecture.x64)
        {
            PublicSystemIncludePaths.Add(Target.UEThirdPartySourceDirectory + "Intel/OIDN/include/");
            PublicSystemLibraryPaths.Add(Target.UEThirdPartySourceDirectory + "Intel/OIDN/lib/");
            PublicAdditionalLibraries.Add(Target.UEThirdPartySourceDirectory + "Intel/OIDN/lib/OpenImageDenoise.lib");
			RuntimeDependencies.Add("$(TargetOutputDir)/OpenImageDenoise.dll", Target.UEThirdPartySourceDirectory + "Intel/OIDN/bin/OpenImageDenoise.dll");
			RuntimeDependencies.Add("$(TargetOutputDir)/tbb12.dll", Target.UEThirdPartySourceDirectory + "Intel/OIDN/bin/tbb12.dll");
			PublicDelayLoadDLLs.Add("OpenImageDenoise.dll");
			PublicDelayLoadDLLs.Add("tbb12.dll");
			PublicDefinitions.Add("WITH_INTELOIDN=1");
        }
		else
		{
			PublicDefinitions.Add("WITH_INTELOIDN=0");
		}
    }
}
