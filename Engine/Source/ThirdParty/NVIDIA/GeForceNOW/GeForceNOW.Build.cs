// Copyright Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;
using System;

public class GeForceNOW : ModuleRules
{
    public GeForceNOW(ReadOnlyTargetRules Target)
        : base(Target)
	{
		Type = ModuleType.External;

        if (Target.Type != TargetRules.TargetType.Server
			&& Target.Configuration != UnrealTargetConfiguration.Unknown
			&& Target.Configuration != UnrealTargetConfiguration.Debug
            && (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Win32))
		{
            String GFNPath = Target.UEThirdPartySourceDirectory + "NVIDIA/GeForceNOW/";
            PublicSystemIncludePaths.Add(GFNPath + "include");

			String GFNLibPath = GFNPath + (Target.Platform == UnrealTargetPlatform.Win64 ? "lib/x64/" : "lib/x86/");
			PublicAdditionalLibraries.Add(GFNLibPath + "GfnRuntimeMD.lib");

            String GFNDllName = "GfnRuntimeSdk.dll";
			String PlatformFolder = (Target.Platform == UnrealTargetPlatform.Win64 ? "Win64/" : "Win32/");
			String GFNDLLPath = "$(EngineDir)/Binaries/ThirdParty/NVIDIA/GeForceNOW/" + PlatformFolder + GFNDllName;
			PublicDelayLoadDLLs.Add(GFNDllName);
			RuntimeDependencies.Add(GFNDLLPath);
        }
	}
}

